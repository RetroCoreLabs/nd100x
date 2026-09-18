//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// HackerCorp Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// emu-proxy-worker.js - Worker-backed proxy with the exact same API as emu-proxy.js
//
// In Worker mode, the WASM module runs in a Web Worker. This proxy:
//  - Returns cached snapshot values for all getters
//  - Sends fire-and-forget commands via postMessage
//  - Uses Promise-based request/response for operations that return results
//  - Dispatches terminal output from Worker frame messages
//
// This file MUST load after module-init.js and before all other JS modules.

(function() {
  'use strict';

  // =========================================================
  // Worker lifecycle
  // =========================================================
  var _worker = new Worker('js/emu-worker.js');
  var _ready = false;
  var _initialized = false;
  var _pendingId = 0;
  var _pending = {};  // id -> {resolve, reject}
  var _snapshot = {};  // cached register/state snapshot
  var _terminals = [];  // cached terminal info from 'initialized' response

  // ---- ND-500 state, mirrored from the Worker ----
  // These exist because the direct-mode ND-500 API is synchronous and a
  // message port is not. Every getter below reads one of these; every setter
  // is a message whose real answer arrives later as nd500Result.
  var _nd500Available = false;   // answered by the module at 'initialized'
  var _nd500Created = false;
  var _nd500Booted = false;
  var _nd500StopReason = '';
  var _nd500Console = [];        // {unit, text} chunks, drained by pollConsole()

  // Phase 3 stub warnings: log each message at most once
  var _warned = {};
  function warnOnce(msg) {
    if (!_warned[msg]) { _warned[msg] = true; console.warn(msg); }
  }

  // Callbacks for async operations
  var _onInitialized = null;  // set by toolbar.js
  var _onBooted = null;       // set by toolbar.js
  var _onBreakpoint = null;   // set by emulation.js / debugger.js
  var _onStopped = null;      // set by emulation.js
  var _onStepDone = null;     // set by debugger.js
  var _onRunDbgDone = null;   // set by debugger.js

  function nextId() {
    return ++_pendingId;
  }

  function resolveRequest(id, data) {
    if (_pending[id]) {
      _pending[id].resolve(data);
      delete _pending[id];
    }
  }

  // =========================================================
  // Worker message handler
  // =========================================================
  _worker.onmessage = function(e) {
    var msg = e.data;
    if (!msg || !msg.type) return;

    switch (msg.type) {

      case 'ready':
        _ready = true;
        // Trigger module-init.js startup path
        if (typeof window.onWorkerReady === 'function') {
          window.onWorkerReady();
        }
        break;

      case 'initialized':
        _initialized = true;
        _terminals = msg.terminals || [];
        _nd500Available = !!msg.nd500Available;
        if (_onInitialized) {
          _onInitialized(msg);
          _onInitialized = null;
        }
        // Dispatch DOM event so smd-manager (and others) can react to init
        window.dispatchEvent(new CustomEvent('emu-initialized'));
        break;

      case 'booted':
        if (msg.snapshot) applySnapshot(msg.snapshot);
        dispatchTermOutput(msg.termOutput);
        if (_onBooted) {
          _onBooted(msg);
          _onBooted = null;
        }
        resolveRequest(msg.id, msg);
        break;

      case 'frame':
        // Update CPU state from frame
        _snapshot.runMode = msg.runMode;
        _snapshot.pil = msg.pil;
        _snapshot.sts = msg.sts;
        _snapshot.pc = msg.pc;
        _snapshot.instrCount = msg.instrCount;
        _snapshot.regA = msg.regA;
        _snapshot.regD = msg.regD;
        _snapshot.regB = msg.regB;
        _snapshot.regT = msg.regT;
        _snapshot.regL = msg.regL;
        _snapshot.regX = msg.regX;
        _snapshot.ea = msg.ea;
        // Dispatch terminal output
        dispatchTermOutput(msg.termOutput);
        if (msg.nd500Console && msg.nd500Console.length)
          _nd500Console = _nd500Console.concat(msg.nd500Console);
        break;

      case 'nd500Result':
        // The real return value of a command sent earlier. The boot log
        // produced during Nd500_Boot comes back with it - losing that would
        // make every failed boot look identical.
        if (msg.op === 'boot') {
          _nd500Booted = (msg.rc === 0);
          if (!_nd500Booted) _nd500StopReason = 'boot failed (rc ' + msg.rc + ')';
        }
        if (msg.op === 'create' && msg.rc !== 0) _nd500Created = false;
        if (msg.console && msg.console.length)
          _nd500Console = _nd500Console.concat(msg.console);
        resolveRequest(msg.id, msg);
        break;

      case 'nd500Stopped':
        _nd500Booted = false;
        _nd500StopReason = msg.reason || 'stopped';
        break;

      case 'breakpoint':
        if (msg.snapshot) applySnapshot(msg.snapshot);
        if (_onBreakpoint) _onBreakpoint(msg);
        break;

      case 'stopped':
        if (msg.snapshot) applySnapshot(msg.snapshot);
        if (_onStopped) _onStopped(msg);
        break;

      case 'stepDone':
        if (msg.snapshot) applySnapshot(msg.snapshot);
        dispatchTermOutput(msg.termOutput);
        if (_onStepDone) _onStepDone(msg);
        resolveRequest(msg.id, msg);
        break;

      case 'runDbgDone':
        if (msg.snapshot) applySnapshot(msg.snapshot);
        dispatchTermOutput(msg.termOutput);
        if (_onRunDbgDone) _onRunDbgDone(msg);
        resolveRequest(msg.id, msg);
        break;

      case 'snapshot':
        if (msg.snapshot) applySnapshot(msg.snapshot);
        break;

      case 'printerJobCompleted':
        if (typeof window.onPrinterJobCompleted === 'function') {
          window.onPrinterJobCompleted(msg);
        }
        break;

      case 'printerGetTypeResult':
        resolveRequest(msg.id, msg.value);
        break;

      case 'printerCheckTimeoutResult':
        resolveRequest(msg.id, msg.value);
        break;

      case 'printerGetStateResult':
        resolveRequest(msg.id, msg);
        break;

      case 'diskLoaded':
        resolveRequest(msg.id, msg);
        break;

      case 'fsResult':
        resolveRequest(msg.id, msg);
        break;

      // --- Phase 3: read operation results ---
      case 'readMemoryResult':
        resolveRequest(msg.id, msg.value);
        break;
      case 'readMemoryBlockResult':
        resolveRequest(msg.id, msg.values);
        break;
      case 'disassembleWordsResult':
        if (msg.error) {
          if (_pending[msg.id]) {
            _pending[msg.id].reject(new Error(msg.error));
            delete _pending[msg.id];
          }
        } else {
          resolveRequest(msg.id, msg.value);
        }
        break;
      case 'readSMDSectorsResult':
        if (msg.error) {
          if (_pending[msg.id]) {
            _pending[msg.id].reject(new Error(msg.error));
            delete _pending[msg.id];
          }
        } else {
          resolveRequest(msg.id, new Uint8Array(msg.data));
        }
        break;
      case 'readPhysicalMemoryResult':
        resolveRequest(msg.id, msg.value);
        break;
      case 'readPhysicalMemoryBlockResult':
        resolveRequest(msg.id, msg.values);
        break;
      case 'dumpPhysicalMemoryResult':
        resolveRequest(msg.id, msg.value);
        break;
      case 'getBreakpointListResult':
        resolveRequest(msg.id, msg.value);
        break;
      case 'getWatchpointInfoResult':
        resolveRequest(msg.id, msg);
        break;
      case 'disassembleResult':
        resolveRequest(msg.id, msg.value);
        break;
      case 'getLevelInfoResult':
        resolveRequest(msg.id, msg.value);
        break;
      case 'getPageTableEntryRawResult':
        resolveRequest(msg.id, msg.value);
        break;
      case 'getPageTableMapResult':
        resolveRequest(msg.id, msg.entries);
        break;
      case 'getDriveInfoResult':
        resolveRequest(msg.id, msg.data);
        break;
      case 'ccallResult':
        resolveRequest(msg.id, msg.error ? null : msg.value);
        break;
      case 'fsReadResult':
        if (msg.error) {
          resolveRequest(msg.id, new Uint8Array(0));
        } else {
          resolveRequest(msg.id, new Uint8Array(msg.buffer));
        }
        break;
      case 'fsStatResult':
        if (msg.error) {
          resolveRequest(msg.id, { size: 0 });
        } else {
          resolveRequest(msg.id, msg.stat);
        }
        break;

      // --- WebSocket bridge status ---
      case 'ws-status':
        if (typeof window.onWsStatusChange === 'function') {
          window.onWsStatusChange(msg.connected, msg.error);
        }
        break;

      case 'ws-client':
        if (typeof window.onWsClientChange === 'function') {
          window.onWsClientChange(msg.action, msg.identCode, msg.clientAddr);
        }
        break;

      case 'ws-stats':
        if (typeof window.onWsStatsUpdate === 'function') {
          window.onWsStatsUpdate(msg.stats);
        }
        break;

      case 'enableRemoteTerminalsResult':
        resolveRequest(msg.id, msg);
        break;

      case 'opfsMountResult':
        resolveRequest(msg.id, msg);
        break;

      case 'gatewayMountResult':
        resolveRequest(msg.id, msg);
        break;

      case 'full-image-progress':
        if (typeof window.onGatewayCopyProgress === 'function') {
          window.onGatewayCopyProgress({ done: msg.done, total: msg.total });
        }
        break;

      case 'full-image-data':
        resolveRequest(msg.id, msg);
        break;

      case 'disk-connected':
        if (typeof window.onDiskConnected === 'function') {
          window.onDiskConnected(msg.blockIO);
        }
        break;

      case 'disk-disconnected':
        if (typeof window.onDiskDisconnected === 'function') {
          window.onDiskDisconnected();
        }
        break;

      case 'disk-list':
        if (typeof window.onDiskList === 'function') {
          window.onDiskList(msg.data);
        }
        break;

      case 'log':
        if (msg.level === 'error') {
          console.error('[Worker]', msg.text);
        } else if (msg.level === 'warn') {
          warnOnce('[Worker]', msg.text);
        } else {
          console.log('[Worker]', msg.text);
        }
        break;
    }
  };

  // =========================================================
  // Helpers
  // =========================================================
  function applySnapshot(snap) {
    for (var key in snap) {
      _snapshot[key] = snap[key];
    }
  }

  /*
   * Ring buffer entry encoding for character device output:
   *   Terminal:   (identCode << 8) | charCode   [bits 0-15]
   *   Printer:    0x80000000 | (1 << 16) | charCode  [bit 31 = device class flag]
   *   PaperTape:  0x80000000 | (2 << 16) | charCode
   */
  function dispatchTermOutput(termOutput) {
    if (!termOutput || termOutput.length === 0) return;
    var handler = window.handleTerminalOutputFromC;
    for (var i = 0; i < termOutput.length; i++) {
      var entry = termOutput[i];
      if (entry & 0x80000000) {
        var devClass = (entry >> 16) & 0xFF;
        var charCode = entry & 0xFF;
        if (devClass === 1 && typeof window.handlePrinterOutput === 'function') {
          window.handlePrinterOutput(charCode);
        } else if (devClass === 2 && typeof window.handlePaperTapeWriterOutput === 'function') {
          window.handlePaperTapeWriterOutput(charCode);
        }
      } else {
        // Terminal output
        if (handler) handler((entry >> 8) & 0xFF, entry & 0xFF);
      }
    }
  }

  function postCmd(type, payload) {
    var msg = payload || {};
    msg.type = type;
    _worker.postMessage(msg);
  }

  function postRequest(type, payload) {
    var msg = payload || {};
    msg.type = type;
    var id = nextId();
    msg.id = id;
    return new Promise(function(resolve, reject) {
      _pending[id] = { resolve: resolve, reject: reject };
      _worker.postMessage(msg);
    });
  }

  function postTransfer(type, payload, transferList) {
    var msg = payload || {};
    msg.type = type;
    var id = nextId();
    msg.id = id;
    return new Promise(function(resolve, reject) {
      _pending[id] = { resolve: resolve, reject: reject };
      _worker.postMessage(msg, transferList);
    });
  }

  // =========================================================
  // Proxy API (same surface as emu-proxy.js)
  // =========================================================
  function _nd500NotHere() {
    console.warn('The ND-500 needs direct mode - Worker mode does not forward it yet.');
    return -1;
  }

  window.emu = {

    // --- Lifecycle ---
    init: function(ini) {
      // Same contract as direct mode, minus the return value: the Worker
      // answers with 'initialized' and any config error arrives there.
      postCmd('init', { ini: ini || '' });
      return 0;  // Return immediately; result arrives via callback
    },
    boot: function(t) {
      postCmd('boot', { bootType: t, id: nextId() });
      return 0;  // Async - result via onBooted callback
    },
    step: function(n) {
      // In Worker mode, step is a no-op - Worker runs autonomously
    },
    stop: function() {
      postCmd('stop');
    },
    isInitialized: function() { return _initialized ? 1 : 0; },

    // Machine Setup runs against the module directly, and in Worker mode the
    // module lives in the Worker. Neither of these is wired through yet, so
    // they answer honestly instead of leaving the window blank with no reason:
    // machine-setup.js shows the message and keeps the INI view usable.
    validateMachineINI: function(ini) {
      return Promise.resolve('');   // cannot check here; the C validator still runs at boot
    },
    describeMachineINI: function(ini) {
      return Promise.resolve('{"error":"the form needs direct mode (the emulator is in a Worker)"}');
    },

    // --- Terminal I/O ---
    sendKey:              function(id, k) { postCmd('key', { identCode: id, keyCode: k }); return 1; },
    getTerminalAddress:   function(i) {
      for (var t = 0; t < _terminals.length; t++) {
        if (_terminals[t].index === i) return _terminals[t].address;
      }
      return -1;
    },
    getTerminalIdentCode: function(i) {
      for (var t = 0; t < _terminals.length; t++) {
        if (_terminals[t].index === i) return _terminals[t].identCode;
      }
      return -1;
    },
    getTerminalLogicalDevice: function(i) {
      for (var t = 0; t < _terminals.length; t++) {
        if (_terminals[t].index === i) return _terminals[t].logicalDevice;
      }
      return -1;
    },
    setTerminalCarrier: function(f, id) { postCmd('carrier', { flag: f, identCode: id }); },

    // --- Terminal output handler setup ---
    hasJSTerminalHandler: function() { return true; },
    setJSTerminalOutputHandler: function(v) { /* no-op in Worker mode */ },

    // --- Terminal ring buffer ---
    enableRingBuffer: function() { return true; },  // Worker enables it at init
    hasRingBuffer: function() { return true; },
    flushTerminalOutput: function() {
      // No-op in Worker mode - output arrives via frame messages
    },

    // --- Printer PDF pipeline (Worker mode) ---
    printerCheckTimeout: function() {
      return postRequest('printerCheckTimeout', {});
    },
    printerFlushJob: function() {
      postCmd('printerFlushJob');
    },
    printerGetLastCompletedJob: function() {
      return postRequest('printerGetState', {}).then(function(r) { return r.lastCompletedJob; });
    },
    printerGetLastJobStartTime: function() {
      return postRequest('printerGetState', {}).then(function(r) { return r.lastJobStartTime; });
    },
    printerGetLastJobEndTime: function() {
      return postRequest('printerGetState', {}).then(function(r) { return r.lastJobEndTime; });
    },
    printerGetLastJobBytes: function() {
      return postRequest('printerGetState', {}).then(function(r) { return r.lastJobBytes; });
    },
    printerGetLastJobLines: function() {
      return postRequest('printerGetState', {}).then(function(r) { return r.lastJobLines; });
    },
    printerGetActiveJobBytes: function() {
      return postRequest('printerGetState', {}).then(function(r) { return r.activeJobBytes; });
    },
    printerGetActiveJobLines: function() {
      return postRequest('printerGetState', {}).then(function(r) { return r.activeJobLines; });
    },
    printerIsJobActive: function() {
      return postRequest('printerGetState', {}).then(function(r) { return r.isJobActive; });
    },
    printerGetJobNumber: function() {
      return postRequest('printerGetState', {}).then(function(r) { return r.jobNumber; });
    },
    printerGetType: function() {
      return postRequest('printerGetType', {});
    },
    printerSetType: function(type) {
      postCmd('printerSetType', { value: type });
    },

    // --- Paper tape API (Worker mode) ---
    loadPaperTape: function(data) {
      // Send data to worker for loading into the paper tape reader
      _worker.postMessage({ type: 'loadPaperTape', data: data });
    },
    getPaperTapeWriterData: function() {
      // TODO: Worker mode needs async round-trip - not yet implemented
      console.warn('getPaperTapeWriterData not yet supported in Worker mode');
      return null;
    },

    // --- Legacy terminal callback registration ---
    hasAddFunction: function() { return false; },
    addFunction: function(fn, sig) { return 0; },
    hasSetTerminalOutputCallback: function() { return false; },
    setTerminalOutputCallback: function(id, cb) { },

    // --- Registers (getters from snapshot) ---
    getPC:    function() { return _snapshot.pc || 0; },
    getRegA:  function() { return _snapshot.regA || 0; },
    getRegD:  function() { return _snapshot.regD || 0; },
    getRegB:  function() { return _snapshot.regB || 0; },
    getRegT:  function() { return _snapshot.regT || 0; },
    getRegL:  function() { return _snapshot.regL || 0; },
    getRegX:  function() { return _snapshot.regX || 0; },
    getSTS:   function() { return _snapshot.sts || 0; },
    getEA:    function() { return _snapshot.ea || 0; },
    getPIL:   function() { return _snapshot.pil || 0; },

    // --- Registers (setters - fire-and-forget) ---
    setPC:    function(v) { postCmd('setReg', { reg: 'pc', value: v }); _snapshot.pc = v; },
    setRegA:  function(v) { postCmd('setReg', { reg: 'a', value: v }); _snapshot.regA = v; },
    setRegD:  function(v) { postCmd('setReg', { reg: 'd', value: v }); _snapshot.regD = v; },
    setRegB:  function(v) { postCmd('setReg', { reg: 'b', value: v }); _snapshot.regB = v; },
    setRegT:  function(v) { postCmd('setReg', { reg: 't', value: v }); _snapshot.regT = v; },
    setRegL:  function(v) { postCmd('setReg', { reg: 'l', value: v }); _snapshot.regL = v; },
    setRegX:  function(v) { postCmd('setReg', { reg: 'x', value: v }); _snapshot.regX = v; },
    setSTS:   function(v) { postCmd('setReg', { reg: 'sts', value: v }); _snapshot.sts = v; },

    // --- System registers (from snapshot) ---
    getPANS: function() { return _snapshot.pans || 0; },
    getOPR:  function() { return _snapshot.opr || 0; },
    getPGS:  function() { return _snapshot.pgs || 0; },
    getPVL:  function() { return _snapshot.pvl || 0; },
    getIIC:  function() { return _snapshot.iic || 0; },
    getIID:  function() { return _snapshot.iid || 0; },
    getPID:  function() { return _snapshot.pid || 0; },
    getPIE:  function() { return _snapshot.pie || 0; },
    getCSR:  function() { return _snapshot.csr || 0; },
    getALD:  function() { return _snapshot.ald || 0; },
    getPES:  function() { return _snapshot.pes || 0; },
    getPGC:  function() { return _snapshot.pgc || 0; },
    getPEA:  function() { return _snapshot.pea || 0; },

    // --- System registers (write-only / mixed from snapshot) ---
    getPANC: function() { return _snapshot.panc || 0; },
    getLMP:  function() { return _snapshot.lmp || 0; },
    getIIE:  function() { return _snapshot.iie || 0; },
    getCCL:  function() { return _snapshot.ccl || 0; },
    getLCIL: function() { return _snapshot.lcil || 0; },
    getUCIL: function() { return _snapshot.ucil || 0; },
    getECCR: function() { return _snapshot.eccr || 0; },
    getPCR:  function(l) {
      if (_snapshot.pcr && l >= 0 && l < 16) return _snapshot.pcr[l];
      return 0;
    },

    // --- Execution state (from snapshot) ---
    getRunMode:    function() { return _snapshot.runMode || 0; },
    getStopReason: function() { return _snapshot.stopReason || 0; },
    getInstrCount: function() { return _snapshot.instrCount || 0; },
    setPaused:     function(p) { postCmd('setPaused', { paused: !!p }); },
    isPaused:      function() { return _snapshot.isPaused || 0; },

    // --- Step/Run (async in Worker mode) ---
    stepOne: function() {
      return postRequest('step', { method: 'stepOne', count: 1 });
    },
    stepOver: function() {
      return postRequest('step', { method: 'stepOver', count: 1 });
    },
    stepOut: function() {
      return postRequest('step', { method: 'stepOut', count: 1 });
    },
    runWithBreakpoints: function(n) {
      return postRequest('runDbg', { maxSteps: n });
    },

    // --- Memory (async in Worker mode - returns Promises) ---
    readMemory: function(a) {
      return postRequest('readMemory', { addr: a });
    },
    writeMemory: function(a, v) {
      postCmd('writeMemory', { addr: a, value: v });
    },
    readMemoryBlock: function(a, n) {
      return postRequest('readMemoryBlock', { addr: a, count: n });
    },
    readPhysicalMemory: function(a) {
      return postRequest('readPhysicalMemory', { addr: a });
    },
    readPhysicalMemoryBlock: function(a, n) {
      return postRequest('readPhysicalMemoryBlock', { addr: a, count: n });
    },
    dumpPhysicalMemory: function(n) {
      return postRequest('dumpPhysicalMemory', { count: n });
    },

    // --- Breakpoints (fire-and-forget) ---
    addBreakpoint:     function(a) { postCmd('addBreakpoint', { addr: a }); },
    removeBreakpoint:  function(a) { postCmd('removeBreakpoint', { addr: a }); },
    clearBreakpoints:  function()  { postCmd('clearBreakpoints'); },
    getBreakpointList: function() {
      return postRequest('getBreakpointList', {});
    },

    // --- Watchpoints (fire-and-forget) ---
    addWatchpoint:      function(a, t) { postCmd('addWatchpoint', { addr: a, wtype: t }); },
    removeWatchpoint:   function(a)    { postCmd('removeWatchpoint', { addr: a }); },
    clearWatchpoints:   function()     { postCmd('clearWatchpoints'); },
    getWatchpointCount: function() {
      return postRequest('getWatchpointInfo', {}).then(function(r) { return r.count; });
    },
    getWatchpointAddr: function(i) {
      return postRequest('getWatchpointInfo', {}).then(function(r) {
        return (r.watchpoints && r.watchpoints[i]) ? r.watchpoints[i].addr : 0;
      });
    },
    getWatchpointType: function(i) {
      return postRequest('getWatchpointInfo', {}).then(function(r) {
        return (r.watchpoints && r.watchpoints[i]) ? r.watchpoints[i].wtype : 0;
      });
    },

    // --- Disassembly / Level info (async in Worker mode) ---
    disassemble: function(a, n) {
      return postRequest('disassemble', { addr: a, count: n });
    },
    getLevelInfo: function() {
      return postRequest('getLevelInfo', {});
    },

    // --- DAP (proxied through Worker) ---
    ccall: function(name, retType, argTypes, argValues) {
      return postRequest('ccall', { name: name, retType: retType, argTypes: argTypes || [], argValues: argValues || [] });
    },
    validateMachineINI: function(ini) {
      return postRequest('ccall', { name: 'ValidateMachineINI', retType: 'string', argTypes: ['string'], argValues: [ini] });
    },

    // --- Page tables (from snapshot) ---
    getPageTableCount:    function() { return _snapshot.pageTableCount || 0; },
    getPageTableEntryRaw: function(p, v) {
      return postRequest('getPageTableEntryRaw', { pt: p, vpage: v });
    },
    getPageTableMap: function(pt) {
      return postRequest('getPageTableMap', { pt: pt });
    },
    getExtendedMode: function() { return _snapshot.extendedMode || 0; },

    // --- Floppy/SMD ---
    remountFloppy: function(u) {
      postRequest('remountFloppy', { unit: u });
      return 0;  // Fire and forget - result comes async
    },
    remountSMD: function(u) {
      postRequest('remountSMD', { unit: u });
      return 0;
    },
    unmountFloppy: function(u) { postCmd('unmountFloppy', { unit: u }); },
    unmountSMD:    function(u) { postCmd('unmountSMD', { unit: u }); },
    remountSCSI:   function(u) { postRequest('remountSCSI', { unit: u }); return 0; },
    unmountSCSI:   function(u) { postCmd('unmountSCSI', { unit: u }); },

    // --- FS (routed through Worker) ---
    fsWriteFile: function(p, d) {
      // Transfer ArrayBuffer to Worker
      var buffer = d.buffer ? d.buffer.slice(0) : d.slice(0);
      postTransfer('fsWrite', { path: p, buffer: buffer }, [buffer]);
    },
    fsReadFile: function(p) {
      return postRequest('fsRead', { path: p });
    },
    fsChmod: function(p, m) {
      // Chmod handled in Worker's fsWrite - no separate command needed
    },
    fsStat: function(p) {
      return postRequest('fsStat', { path: p });
    },
    fsUnlink: function(p) { postCmd('fsUnlink', { path: p }); },
    fsAvailable: function() { return true; },  // Worker has FS

    // --- HEAPU16 access ---
    getHEAPU16Buffer: function() { return null; },  // Not available in Worker mode
    hasHEAPU16: function() { return false; },

    // --- Module state queries ---
    isReady: function() { return _ready && _initialized; },
    hasFunction: function(fnName) { return false; },  // Module not on main thread
    callFunction: function(fnName) { return undefined; },

    // --- Drive info (unified registry query) ---
    getDriveInfo: function() {
      return postRequest('getDriveInfo', {});
    },

    // --- ND-500, in Worker mode ---
    //
    // Worker mode is the ONLY mode that reaches the gateway: emu-worker.js
    // owns the WebSocket, and the gateway's ethernet segment is how a browser
    // NDIX gets onto a wire with anything else. Direct mode has the ND-500 but
    // refuses wsConnect, so the two used to be mutually exclusive and the
    // browser could not use the segment at all.
    //
    // The API below keeps the SHAPE of the direct-mode one so nd500-window.js
    // works either way, but the meaning underneath differs and it matters:
    //
    //   COMMANDS are fire-and-forget. create/loadKernel/mountDisk/boot cross
    //   the port as messages, so a return value here cannot be the emulator's.
    //   They return 0 for "sent", and the real result arrives as nd500Result -
    //   which is what sets the cached flags these getters read.
    //
    //   STEPPING is not done from here. emu-worker.js steps the ND-500 in the
    //   same loop as the ND-100, because both CPUs are in one wasm module.
    //   step() therefore only sets the slice; calling it per frame the way the
    //   direct path does would double-step the guest.
    //
    //   CONSOLE output is pushed up in every frame message and buffered here,
    //   so pollConsole() stays synchronous and still returns chunks in order.
    nd500: {
      available:  function() { return _nd500Available; },
      isCreated:  function() { return _nd500Created; },
      isBooted:   function() { return _nd500Booted; },
      isRunning:  function() { return _nd500Booted; },

      setEnv: function(name, value) {
        postCmd('nd500SetEnv', { name: name, value: value });
        return 0;
      },
      create: function(memBytes) {
        postCmd('nd500Create', { memBytes: memBytes || 0 });
        _nd500Created = true;
        return 0;
      },
      loadKernel: function(bytes) {
        postCmd('nd500LoadKernel', { data: bytes });
        return 0;
      },
      loadSegments: function() {
        warnOnce('ND-500 segment files are not forwarded in Worker mode - the ' +
                 'sizes are derived from the a.out header instead, which is the ' +
                 'path a kernel taken out of a disk image uses anyway.');
        return -1;
      },
      mountDisk: function(unit, bytes, writable) {
        postCmd('nd500MountDisk', { unit: unit, data: bytes, writable: !!writable });
        return 0;
      },
      unmountDisk: function() {
        warnOnce('ND-500 unmountDisk is not forwarded in Worker mode yet.');
        return -1;
      },
      // The disc lives in the Worker's heap. Handing the page a view of it is
      // not possible across the port, and copying 70 MB back per call would be
      // worse than useless - so this says so instead of returning something
      // plausible.
      diskSize:  function() { return 0; },
      diskBytes: function() { return null; },

      boot: function() {
        postCmd('nd500Boot');
        return 0;
      },
      // Only the slice. See the note above: the Worker does the stepping.
      step: function(count) {
        if (count > 0) postCmd('nd500SetSlice', { slice: count });
        return 0;
      },
      pause: function() { postCmd('nd500Pause'); },

      // ---- ethernet: the whole point of Worker mode ----
      // Call AFTER boot. The guest still configures the interface itself:
      //     /etc/etconfig et0 0x08 0x00 0x26 0xF4 0x01 0x00
      //     /etc/ifconfig et0 inet 223.255.254.8 -trailers up
      // -trailers is NOT optional - without it every IP packet is dropped
      // inside the driver while the interface looks perfectly healthy.
      ethAttach: function(segment) {
        postCmd('nd500EthAttach', { segment: segment || 0 });
        return 0;
      },
      ethDetach: function() { postCmd('nd500EthDetach'); },

      pc: function() { return 0; },
      stopReason: function() { return _nd500StopReason; },

      // Chunks buffered out of the frame messages, in the order the guest
      // produced them. Unit 255 is the emulator's own boot log, not guest
      // output, and the page marks it differently - so the unit is preserved.
      pollConsole: function() {
        var out = _nd500Console;
        _nd500Console = [];
        return out;
      },
      sendInput: function(unit, text) {
        var b = new TextEncoder().encode(text);
        postCmd('nd500SendInput', { unit: unit, data: b });
      }
    },

    isWorkerMode: function() { return true; },

    // --- Worker-specific methods ---
    workerStart: function() {
      postCmd('start');
    },
    workerStop: function() {
      postCmd('stop');
    },
    workerLoadDisk: function(path, arrayBuffer) {
      var buffer = arrayBuffer.slice(0);  // Copy for transfer
      postTransfer('loadDisk', { path: path, buffer: buffer }, [buffer]);
    },
    requestSnapshot: function() {
      postCmd('snapshot');
    },

    // --- OPFS persistent storage ---
    opfsMountSMD: function(unit, fileName) {
      return postRequest('opfsMountSMD', { unit: unit, fileName: fileName });
    },
    opfsUnmountSMD: function(unit) {
      return postRequest('opfsUnmountSMD', { unit: unit });
    },
    // SCSI OPFS mount reuses the driveType-aware opfsMountDisc handler (driveType 2).
    opfsMountSCSI: function(unit, fileName) {
      return postRequest('opfsMountDisc', { unit: unit, fileName: fileName, driveType: 2 });
    },
    opfsUnmountSCSI: function(unit) {
      return postRequest('opfsUnmountDisc', { unit: unit, driveType: 2 });
    },
    mountSMDFromBuffer: function(unit, data) {
      // Not used in Worker mode - Worker uses OPFS directly
      console.warn('mountSMDFromBuffer not applicable in Worker mode');
      return Promise.reject(new Error('Use opfsMountSMD in Worker mode'));
    },
    mountSCSIFromBuffer: function(unit, data) {
      console.warn('mountSCSIFromBuffer not applicable in Worker mode');
      return Promise.reject(new Error('Use opfsMountSCSI in Worker mode'));
    },
    getSCSIBuffer: function(unit) { return 0; },
    getSCSIBufferSize: function(unit) { return 0; },
    getSMDBuffer: function(unit) { return 0; },
    getSMDBufferSize: function(unit) { return 0; },
    // Mode-independent sector read. Resolves with a copy (count*1024 bytes)
    // transferred from the worker heap.
    readSMDSectorsAsync: function(unit, lba, count) {
      return postRequest('readSMDSectors', { unit: unit, lba: lba, count: count });
    },

    // --- Segment disassembler buffer (not available in Worker mode) ---
    loadInspectBuffer: function() { console.warn('loadInspectBuffer not available in Worker mode'); },
    disassembleFromBuffer: function() { return ''; },
    // Mode-independent: disassemble a Uint16Array of words at `baseAddr`.
    // The words are copied (not transferred) so the caller keeps its buffer.
    disassembleWordsAsync: function(words, baseAddr) {
      var copy = new Uint16Array(words).buffer;
      return postTransfer('disassembleWords', { words: copy, baseAddr: baseAddr }, [copy]);
    },
    getHEAPU8: function() { return null; },

    // --- Gateway disk mount/unmount ---
    gatewayMountSMD: function(unit, imageSize) {
      return postRequest('gatewayMountSMD', { unit: unit, imageSize: imageSize });
    },
    gatewayUnmountSMD: function(unit) {
      return postRequest('gatewayUnmountSMD', { unit: unit });
    },
    gatewayMountSCSI: function(unit, imageSize) {
      return postRequest('gatewayMountSCSI', { unit: unit, imageSize: imageSize });
    },
    gatewayUnmountSCSI: function(unit) {
      return postRequest('gatewayUnmountSCSI', { unit: unit });
    },
    gatewayMountFloppy: function(unit, imageSize) {
      return postRequest('gatewayMountFloppy', { unit: unit, imageSize: imageSize });
    },
    gatewayUnmountFloppy: function(unit) {
      return postRequest('gatewayUnmountFloppy', { unit: unit });
    },
    gatewayReadFullImage: function(driveType, unit, size) {
      return postRequest('gatewayReadFullImage', { driveType: driveType, unit: unit, size: size });
    },

    // --- WebSocket bridge ---
    wsConnect: function(url) { postCmd('ws-connect', { url: url }); },
    wsDisconnect: function() { postCmd('ws-disconnect'); },
    enableRemoteTerminals: function() {
      return postRequest('enableRemoteTerminals', {});
    },

    // --- Callback registration ---
    set onInitialized(fn)  { _onInitialized = fn; },
    get onInitialized()    { return _onInitialized; },
    set onBooted(fn)       { _onBooted = fn; },
    get onBooted()         { return _onBooted; },
    set onBreakpoint(fn)   { _onBreakpoint = fn; },
    get onBreakpoint()     { return _onBreakpoint; },
    set onStopped(fn)      { _onStopped = fn; },
    get onStopped()        { return _onStopped; },
    set onStepDone(fn)     { _onStepDone = fn; },
    get onStepDone()       { return _onStepDone; },
    set onRunDbgDone(fn)   { _onRunDbgDone = fn; },
    get onRunDbgDone()     { return _onRunDbgDone; }
  };

})();
