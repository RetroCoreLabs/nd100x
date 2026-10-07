//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// HackerCorp Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// emu-proxy.js - Proxy abstraction layer for Module._* WASM calls
//
// All JS modules call emu.* instead of Module._* directly.
// In direct mode (default), each method delegates to Module._* synchronously.
// In worker mode (Phase 2), the proxy returns cached snapshot values.
//
// This file MUST load after module-init.js (Module config) and before nd100wasm.js.

(function() {
  'use strict';

  // =========================================================
  // Direct-mode proxy: every call passes through to Module._*
  // =========================================================
  // Copy a Uint8Array into the wasm heap. The caller frees it, except for a
  // mounted disk, which the emulator keeps.
  function _copyIn(bytes) {
    var p = Module._malloc(bytes.length);
    if (!p) throw new Error('malloc(' + bytes.length + ') failed');
    Module.HEAPU8.set(bytes, p);
    return p;
  }
  function _utf8(s) {
    var out = new Uint8Array(s.length);
    for (var i = 0; i < s.length; i++) out[i] = s.charCodeAt(i) & 0xFF;
    return out;
  }

  window.emu = {

    // --- Lifecycle ---
    // init(ini): with an INI the machine is BUILT FROM IT - terminals,
    // controllers, CPU model, the lot (InitWithConfig, src/frontend/nd100wasm).
    // Without one, or against a module built before that export existed, this
    // is the old Init() and the built-in device set. Returns "" on success or
    // the parser's error message, so a bad profile reports itself instead of
    // booting something the user did not ask for.
    init: function(ini) {
      if (ini && Module._InitWithConfig) {
        try { return Module.ccall('InitWithConfig', 'string', ['string'], [ini]); }
        catch (e) { return 'init failed: ' + e.message; }
      }
      Module._Init();
      return '';
    },
    // boot(type, unit, image): the [boot] device's type (0=FLOPPY 1=SMD
    // 2=BPUN 3=SCSI 4=WINCHESTER), its unit, and the MEMFS image for a unit
    // that was not mounted at Init (null for the conventional name).
    boot: function(t, unit, image) {
      if (typeof Module._BootFrom !== 'function') return Module._Boot(t);
      return Module.ccall('BootFrom', 'number', ['number', 'number', 'string'],
                          [t, unit | 0, image || null]);
    },
    // Validate a machine INI string via the native validator. Returns "" if OK,
    // or a "file:line message" string. Promise-wrapped for a uniform API with
    // Worker mode.
    // --- Winchester (ST506) and the floppy mounts that were missing ---
    // Winchester had NO mount path in the browser at all until now, and floppy
    // could only come from the gateway. Both now match SMD and SCSI.
    mountWinchesterFromOPFS:    function(u, sz) { return Module._MountWinchesterFromOPFS(u, sz); },
    mountWinchesterFromGateway: function(u, sz) { return Module._MountWinchesterFromGateway(u, sz); },
    // (unit, bytes), like mountSMDFromBuffer/mountSCSIFromBuffer: the bytes go
    // through a heap copy that the C side copies again, so it is freed here.
    // It used to take a raw heap pointer, which no caller had - the HDD
    // manager handed it a Uint8Array and the mount silently did nothing.
    mountWinchesterFromBuffer:  function(unit, data) {
      var bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
      var ptr = Module._malloc(bytes.byteLength);
      Module.HEAPU8.set(bytes, ptr);
      var rc = Module._MountWinchesterFromBuffer(unit, ptr, bytes.byteLength);
      Module._free(ptr);
      return rc;
    },
    getWinchesterBuffer:        function(u) { return Module._GetWinchesterBuffer(u); },
    getWinchesterBufferSize:    function(u) { return Module._GetWinchesterBufferSize(u); },
    remountWinchester:          function(u) { return Module._RemountWinchester(u); },
    unmountWinchester:          function(u) { return Module._UnmountWinchester(u); },

    mountFloppyFromOPFS:        function(u, sz) { return Module._MountFloppyFromOPFS(u, sz); },
    mountFloppyFromBuffer:      function(unit, data) {
      var bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
      var ptr = Module._malloc(bytes.byteLength);
      Module.HEAPU8.set(bytes, ptr);
      var rc = Module._MountFloppyFromBuffer(unit, ptr, bytes.byteLength);
      Module._free(ptr);
      return rc;
    },
    getFloppyBuffer:            function(u) { return Module._GetFloppyBuffer(u); },
    getFloppyBufferSize:        function(u) { return Module._GetFloppyBufferSize(u); },

    validateMachineINI: function(ini) {
      try { return Promise.resolve(Module.ccall('ValidateMachineINI', 'string', ['string'], [ini])); }
      catch (e) { return Promise.resolve('validation unavailable: ' + e.message); }
    },
    // Describe an INI as JSON, for the Machine Setup form. Reading a config
    // goes through the C parser so the form and the emulator can never disagree
    // about what a file means. Always resolves to something parseable.
    describeMachineINI: function(ini) {
      try { return Promise.resolve(Module.ccall('DescribeMachineINI', 'string', ['string'], [ini])); }
      catch (e) { return Promise.resolve('{"error":"description unavailable: ' + e.message + '"}'); }
    },
    step:          function(n) { Module._Step(n); },
    stop:          function()  { Module._Stop(); },
    isInitialized: function()  { return Module._IsInitialized ? Module._IsInitialized() : 0; },

    // --- Terminal I/O ---
    sendKey:              function(id, k) { return Module._SendKeyToTerminal(id, k); },
    getTerminalAddress:   function(i)     { return Module._GetTerminalAddress(i); },
    getTerminalIdentCode: function(i)     { return Module._GetTerminalIdentCode(i); },
    getTerminalLogicalDevice: function(i) { return Module._GetTerminalLogicalDevice(i); },
    setTerminalCarrier:   function(f, id) { Module._SetTerminalCarrier(f, id); },

    // --- Terminal output handler setup ---
    hasJSTerminalHandler: function() {
      return typeof Module._TerminalOutputToJS !== 'undefined' &&
             typeof Module._SetJSTerminalOutputHandler !== 'undefined';
    },
    setJSTerminalOutputHandler: function(v) { Module._SetJSTerminalOutputHandler(v); },

    // --- Terminal ring buffer ---
    enableRingBuffer: function() {
      if (typeof Module._EnableTerminalRingBuffer === 'function') {
        Module._EnableTerminalRingBuffer(1);
        return true;
      }
      return false;
    },
    hasRingBuffer: function() {
      return typeof Module._PollTerminalOutput === 'function';
    },
    flushTerminalOutput: function() {
      if (typeof Module._PollTerminalOutput !== 'function') return;
      var handler = window.handleTerminalOutputFromC;
      if (!handler) return;
      var entry;
      while ((entry = Module._PollTerminalOutput()) >= 0) {
        handler((entry >> 8) & 0xFF, entry & 0xFF);
      }
      // Flush printer output
      if (typeof Module._PollPrinterOutput === 'function') {
        while ((entry = Module._PollPrinterOutput()) >= 0) {
          if (typeof window.handlePrinterOutput === 'function') {
            window.handlePrinterOutput(entry & 0xFF);
          }
        }
      }
      // Flush paper tape writer output
      if (typeof Module._PollPaperTapeWriterOutput === 'function') {
        while ((entry = Module._PollPaperTapeWriterOutput()) >= 0) {
          if (typeof window.handlePaperTapeWriterOutput === 'function') {
            window.handlePaperTapeWriterOutput(entry & 0xFF);
          }
        }
      }
      // Throttled printer job timeout check (~1Hz)
      var now = performance.now();
      if (!emu._lastPrinterCheck || now - emu._lastPrinterCheck >= 1000) {
        emu._lastPrinterCheck = now;
        if (emu.printerCheckTimeout() === 1) {
          if (typeof window.onPrinterJobCompleted === 'function') {
            window.onPrinterJobCompleted();
          }
        }
      }
    },

    // --- Printer PDF pipeline ---
    printerCheckTimeout: function() {
      return typeof Module._PrinterCheckTimeout === 'function' ? Module._PrinterCheckTimeout() : 0;
    },
    printerFlushJob: function() {
      if (typeof Module._PrinterFlushJob === 'function') Module._PrinterFlushJob();
    },
    printerGetLastCompletedJob: function() {
      return typeof Module._PrinterGetLastCompletedJob === 'function' ? Module._PrinterGetLastCompletedJob() : 0;
    },
    printerGetLastJobStartTime: function() {
      return typeof Module._PrinterGetLastJobStartTime === 'function' ? Module._PrinterGetLastJobStartTime() : 0;
    },
    printerGetLastJobEndTime: function() {
      return typeof Module._PrinterGetLastJobEndTime === 'function' ? Module._PrinterGetLastJobEndTime() : 0;
    },
    printerGetLastJobBytes: function() {
      return typeof Module._PrinterGetLastJobBytes === 'function' ? Module._PrinterGetLastJobBytes() : 0;
    },
    printerGetLastJobLines: function() {
      return typeof Module._PrinterGetLastJobLines === 'function' ? Module._PrinterGetLastJobLines() : 0;
    },
    printerGetActiveJobBytes: function() {
      return typeof Module._PrinterGetActiveJobBytes === 'function' ? Module._PrinterGetActiveJobBytes() : 0;
    },
    printerGetActiveJobLines: function() {
      return typeof Module._PrinterGetActiveJobLines === 'function' ? Module._PrinterGetActiveJobLines() : 0;
    },
    printerIsJobActive: function() {
      return typeof Module._PrinterIsJobActive === 'function' ? Module._PrinterIsJobActive() : 0;
    },
    printerGetJobNumber: function() {
      return typeof Module._PrinterGetJobNumber === 'function' ? Module._PrinterGetJobNumber() : 0;
    },
    printerGetType: function() {
      return typeof Module._PrinterGetType === 'function' ? Module._PrinterGetType() : 0;
    },
    printerSetType: function(type) {
      if (typeof Module._PrinterSetType === 'function') Module._PrinterSetType(type);
    },

    // --- Paper tape API ---
    loadPaperTape: function(data) {
      if (typeof Module._LoadPaperTape !== 'function') return;
      var ptr = Module._malloc(data.length);
      Module.HEAPU8.set(data, ptr);
      Module._LoadPaperTape(ptr, data.length);
      Module._free(ptr);
    },
    getPaperTapeWriterData: function() {
      if (typeof Module._GetPaperTapeWriterDataLength !== 'function') return null;
      var length = Module._GetPaperTapeWriterDataLength();
      if (length <= 0) return null;
      var ptr = Module._GetPaperTapeWriterDataPtr();
      if (!ptr) return null;
      return new Uint8Array(Module.HEAPU8.buffer, ptr, length).slice();
    },

    // --- Legacy terminal callback registration ---
    hasAddFunction: function()  { return typeof Module.addFunction !== 'undefined'; },
    addFunction:    function(fn, sig) { return Module.addFunction(fn, sig); },
    hasSetTerminalOutputCallback: function() { return typeof Module._SetTerminalOutputCallback === 'function'; },
    setTerminalOutputCallback: function(id, cb) { Module._SetTerminalOutputCallback(id, cb); },

    // --- Registers (getters) ---
    getPC:    function() { return Module._Dbg_GetPC(); },
    getRegA:  function() { return Module._Dbg_GetRegA(); },
    getRegD:  function() { return Module._Dbg_GetRegD(); },
    getRegB:  function() { return Module._Dbg_GetRegB(); },
    getRegT:  function() { return Module._Dbg_GetRegT(); },
    getRegL:  function() { return Module._Dbg_GetRegL(); },
    getRegX:  function() { return Module._Dbg_GetRegX(); },
    getSTS:   function() { return Module._Dbg_GetSTS(); },
    getEA:    function() { return Module._Dbg_GetEA(); },
    getPIL:   function() { return Module._Dbg_GetPIL(); },

    // --- Registers (setters) ---
    setPC:    function(v) { Module._Dbg_SetPC(v); },
    setRegA:  function(v) { Module._Dbg_SetRegA(v); },
    setRegD:  function(v) { Module._Dbg_SetRegD(v); },
    setRegB:  function(v) { Module._Dbg_SetRegB(v); },
    setRegT:  function(v) { Module._Dbg_SetRegT(v); },
    setRegL:  function(v) { Module._Dbg_SetRegL(v); },
    setRegX:  function(v) { Module._Dbg_SetRegX(v); },
    setSTS:   function(v) { Module._Dbg_SetSTS(v); },

    // --- System registers (read-only) ---
    getPANS: function() { return Module._Dbg_GetPANS(); },
    getOPR:  function() { return Module._Dbg_GetOPR(); },
    getPGS:  function() { return Module._Dbg_GetPGS(); },
    getPVL:  function() { return Module._Dbg_GetPVL(); },
    getIIC:  function() { return Module._Dbg_GetIIC(); },
    getIID:  function() { return Module._Dbg_GetIID(); },
    getPID:  function() { return Module._Dbg_GetPID(); },
    getPIE:  function() { return Module._Dbg_GetPIE(); },
    getCSR:  function() { return Module._Dbg_GetCSR(); },
    getALD:  function() { return Module._Dbg_GetALD(); },
    getPES:  function() { return Module._Dbg_GetPES(); },
    getPGC:  function() { return Module._Dbg_GetPGC(); },
    getPEA:  function() { return Module._Dbg_GetPEA(); },

    // --- System registers (write-only / mixed) ---
    getPANC: function()  { return Module._Dbg_GetPANC(); },
    getLMP:  function()  { return Module._Dbg_GetLMP(); },
    getIIE:  function()  { return Module._Dbg_GetIIE(); },
    getCCL:  function()  { return Module._Dbg_GetCCL(); },
    getLCIL: function()  { return Module._Dbg_GetLCIL(); },
    getUCIL: function()  { return Module._Dbg_GetUCIL(); },
    getECCR: function()  { return Module._Dbg_GetECCR(); },
    getPCR:  function(l) { return Module._Dbg_GetPCR(l); },

    // --- Execution state ---
    getRunMode:    function()  { return Module._Dbg_GetRunMode(); },
    getStopReason: function()  { return Module._Dbg_GetStopReason(); },
    getInstrCount: function()  { return Module._Dbg_GetInstrCount(); },
    setPaused:     function(p) { Module._Dbg_SetPaused(p); },
    isPaused:      function()  { return Module._Dbg_IsPaused(); },

    // --- Step/Run ---
    stepOne:            function()  { Module._Dbg_StepOne(); },
    stepOver:           function()  { Module._Dbg_StepOver(); },
    stepOut:            function()  { Module._Dbg_StepOut(); },
    runWithBreakpoints: function(n) { return Module._Dbg_RunWithBreakpoints(n); },

    // --- Memory ---
    readMemory:      function(a)    { return Module._Dbg_ReadMemory(a); },
    writeMemory:     function(a, v) { Module._Dbg_WriteMemory(a, v); },
    readMemoryBlock: function(a, n) { return Module._Dbg_ReadMemoryBlock(a, n); },
    readPhysicalMemory:      function(a)    { return Module._Dbg_ReadPhysicalMemory(a); },
    readPhysicalMemoryBlock: function(a, n) { return Module._Dbg_ReadPhysicalMemoryBlock(a, n); },
    dumpPhysicalMemory:      function(n)    { return Module._Dbg_DumpPhysicalMemory(n); },

    // --- Breakpoints ---
    addBreakpoint:     function(a)    { Module._Dbg_AddBreakpoint(a); },
    removeBreakpoint:  function(a)    { Module._Dbg_RemoveBreakpoint(a); },
    clearBreakpoints:  function()     { Module._Dbg_ClearBreakpoints(); },
    getBreakpointList: function()     { return Module.UTF8ToString(Module._Dbg_GetBreakpointList()); },

    // --- Watchpoints ---
    addWatchpoint:      function(a, t) { Module._Dbg_AddWatchpoint(a, t); },
    removeWatchpoint:   function(a)    { Module._Dbg_RemoveWatchpoint(a); },
    clearWatchpoints:   function()     { Module._Dbg_ClearWatchpoints(); },
    getWatchpointCount: function()     { return Module._Dbg_GetWatchpointCount(); },
    getWatchpointAddr:  function(i)    { return Module._Dbg_GetWatchpointAddr(i); },
    getWatchpointType:  function(i)    { return Module._Dbg_GetWatchpointType(i); },

    // --- Disassembly / Level info ---
    disassemble:  function(a, n) { return Module.UTF8ToString(Module._Dbg_Disassemble(a, n)); },
    getLevelInfo:  function()     { return Module.UTF8ToString(Module._Dbg_GetLevelInfo()); },

    // --- DAP (JSON via ccall) ---
    ccall: function(name, retType, argTypes, argValues) {
      return Module.ccall(name, retType, argTypes, argValues);
    },

    // --- Page tables ---
    getPageTableCount:    function()     { return Module._Dbg_GetPageTableCount(); },
    getPageTableEntryRaw: function(p, v) { return Module._Dbg_GetPageTableEntryRaw(p, v); },
    getPageTableMap: function(pt) {
      var entries = new Array(64);
      for (var i = 0; i < 64; i++) {
        entries[i] = Module._Dbg_GetPageTableEntryRaw(pt, i) >>> 0;
      }
      return entries;
    },
    getExtendedMode:      function()     { return Module._Dbg_GetExtendedMode(); },

    // --- Floppy/SMD ---
    remountFloppy:  function(u) { return Module._RemountFloppy(u); },
    remountSMD:     function(u) { return Module._RemountSMD(u); },
    unmountFloppy:  function(u) { Module._UnmountFloppy(u); },
    unmountSMD:     function(u) { Module._UnmountSMD(u); },

    // --- SCSI (IDs 0-6) ---
    remountSCSI:    function(u) { return Module._RemountSCSI(u); },
    unmountSCSI:    function(u) { Module._UnmountSCSI(u); },
    mountSCSIFromBuffer: function(unit, data) {
      var ptr = Module._malloc(data.byteLength);
      Module.HEAPU8.set(data instanceof Uint8Array ? data : new Uint8Array(data), ptr);
      var rc = Module._MountSCSIFromBuffer(unit, ptr, data.byteLength);
      Module._free(ptr);
      return rc;
    },
    getSCSIBuffer: function(unit) { return Module._GetSCSIBuffer(unit); },
    getSCSIBufferSize: function(unit) { return Module._GetSCSIBufferSize(unit); },
    opfsMountSCSI: function(unit, fileName) {
      return Promise.reject(new Error('OPFS SyncAccessHandle not available in Direct mode'));
    },
    opfsUnmountSCSI: function(unit) { Module._UnmountSCSI(unit); },

    // --- FS (pass-through in direct mode) ---
    fsWriteFile: function(p, d) { Module.FS.writeFile(p, d); },
    fsReadFile:  function(p)    { return Module.FS.readFile(p); },
    fsChmod:     function(p, m) { Module.FS.chmod(p, m); },
    fsStat:      function(p)    { return Module.FS.stat(p); },
    fsUnlink:    function(p)    { Module.FS.unlink(p); },
    fsAvailable: function()     { return typeof Module.FS !== 'undefined'; },

    // --- HEAPU16 access for bulk memory reads ---
    getHEAPU16Buffer: function() { return Module.HEAPU16 ? Module.HEAPU16.buffer : null; },
    hasHEAPU16: function() {
      var desc = Object.getOwnPropertyDescriptor(Module, 'HEAPU16');
      if (!desc) return false;
      if ('value' in desc) return !!desc.value;
      if (desc.get && desc.get.toString().indexOf('abort') === -1) return true;
      return false;
    },

    // --- Module state queries ---
    isReady: function() {
      return Module && Module.calledRun && Module._Dbg_GetPC;
    },
    hasFunction: function(fnName) {
      return typeof Module[fnName] === 'function';
    },
    callFunction: function(fnName) {
      if (typeof Module[fnName] === 'function') {
        return Module[fnName].apply(Module, Array.prototype.slice.call(arguments, 1));
      }
      return undefined;
    },

    // --- OPFS persistent storage (Direct mode: buffer-based) ---
    opfsMountSMD: function(unit, fileName) {
      // In Direct mode, we cannot use SyncAccessHandle. Reject so caller falls back.
      return Promise.reject(new Error('OPFS SyncAccessHandle not available in Direct mode'));
    },
    opfsUnmountSMD: function(unit) {
      Module._UnmountSMD(unit);
    },
    mountSMDFromBuffer: function(unit, data) {
      // Copy data into WASM heap and call MountSMDFromBuffer
      var ptr = Module._malloc(data.byteLength);
      Module.HEAPU8.set(data instanceof Uint8Array ? data : new Uint8Array(data), ptr);
      var rc = Module._MountSMDFromBuffer(unit, ptr, data.byteLength);
      // Do NOT free ptr - MountSMDFromBuffer copies into its own malloc'd buffer,
      // but the C function already copied so we free the temp copy
      Module._free(ptr);
      return rc;
    },
    getSMDBuffer: function(unit) { return Module._GetSMDBuffer(unit); },
    getSMDBufferSize: function(unit) { return Module._GetSMDBufferSize(unit); },
    readSMDSectors: function(unit, lba, count) {
      if (!Module._Dbg_ReadSMDSectors) return 0;
      return Module._Dbg_ReadSMDSectors(unit, lba, count);
    },
    // Mode-independent sector read. Resolves with a private copy (count*1024 bytes).
    readSMDSectorsAsync: function(unit, lba, count) {
      try {
        if (!Module._Dbg_ReadSMDSectors) {
          throw new Error('WASM Dbg_ReadSMDSectors not exported (rebuild?)');
        }
        var ptr = Module._Dbg_ReadSMDSectors(unit, lba, count);
        if (!ptr) {
          throw new Error('Dbg_ReadSMDSectors returned 0 (unit=' + unit + ' lba=' + lba +
            ' count=' + count + ' — drive not mounted? read past end?)');
        }
        return Promise.resolve(new Uint8Array(Module.HEAPU8.buffer, ptr, count * 1024).slice());
      } catch (e) {
        return Promise.reject(e);
      }
    },

    // --- Physical memory size ---
    getPhysMemWords: function() {
      if (!Module._Dbg_GetPhysMemWords) return 2 * 1024 * 1024;
      return Module._Dbg_GetPhysMemWords();
    },

    // --- Segment disassembler buffer ---
    loadInspectBuffer: function(heapPtr, wordCount, baseAddr) {
      if (Module._Dbg_LoadInspectBuffer) Module._Dbg_LoadInspectBuffer(heapPtr, wordCount, baseAddr);
    },
    disassembleFromBuffer: function(startWord, count) {
      if (!Module._Dbg_DisassembleFromBuffer) return '';
      return Module.UTF8ToString(Module._Dbg_DisassembleFromBuffer(startWord, count));
    },
    // Mode-independent: disassemble a Uint16Array of words at `baseAddr`.
    disassembleWordsAsync: function(words, baseAddr) {
      try {
        if (!Module._Dbg_LoadInspectBuffer || !Module._Dbg_DisassembleFromBuffer) {
          throw new Error('WASM disassembler not exported (rebuild?)');
        }
        var byteLen = words.length * 2;
        var ptr = Module._malloc(byteLen);
        if (!ptr) throw new Error('malloc(' + byteLen + ') failed');
        Module.HEAPU8.set(new Uint8Array(words.buffer, words.byteOffset, byteLen), ptr);
        Module._Dbg_LoadInspectBuffer(ptr, words.length, baseAddr);
        Module._free(ptr);
        return Promise.resolve(Module.UTF8ToString(Module._Dbg_DisassembleFromBuffer(0, words.length)));
      } catch (e) {
        return Promise.reject(e);
      }
    },

    getHEAPU8: function() { return Module.HEAPU8; },

    // --- WebSocket bridge (requires Worker mode) ---
    wsConnect: function(url) { console.warn('WebSocket bridge requires Worker mode'); },
    wsDisconnect: function() { },
    enableRemoteTerminals: function() {
      console.warn('WebSocket bridge requires Worker mode');
      return Promise.resolve({ count: 0 });
    },

    // --- Drive info (unified registry query) ---
    getDriveInfo: function() {
      return JSON.parse(Module.UTF8ToString(Module._GetDriveInfo()));
    },

    // --- The ND-500 -------------------------------------------------------
    // Present only when the module was built with an nd500x checkout. Ask
    // available() first: every function below exists either way, and without
    // an ND-500 they all refuse, because emscripten cannot link a module whose
    // exported name is missing - so "no ND-500" is an answer, not an absence.
    //
    // The ND-500 lives in THIS module rather than a second one because an
    // ND-100 + ND-500 machine has shared memory (MPM5), and a Module owns its
    // memory. See src/frontend/nd100wasm/nd500_wasm.c.
    nd500: {
      available:  function() { return Module._Nd500_Available ? !!Module._Nd500_Available() : false; },
      // One of nd500x's ~40 ND500X_* diagnostic switches. They are read from the
      // environment once, on first use, and a browser has no environment - so
      // without this every one of them is permanently off. MUST come before
      // create(); afterwards it does nothing.
      setEnv: function(name, value) {
        return Module.ccall('Nd500_SetEnv', 'number', ['string','string'], [name, value == null ? '1' : String(value)]);
      },
      create:     function(memBytes) { return Module._Nd500_Create(memBytes || 0); },
      isCreated:  function() { return !!Module._Nd500_IsCreated(); },
      isBooted:   function() { return !!Module._Nd500_IsBooted(); },

      // Each of these takes a Uint8Array and copies it into the wasm heap.
      // The kernel and its segment files are staged in the module's in-memory
      // filesystem, because the boot library takes file paths - that keeps the
      // browser on the same boot path that is exercised natively.
      loadKernel: function(bytes) {
        var p = _copyIn(bytes);
        try { return Module._Nd500_LoadKernel(p, bytes.length); }
        finally { Module._free(p); }
      },
      // BOTH files or neither: the library derives both sizes from the a.out
      // header when the pair is not complete, which is the path a kernel taken
      // out of a disk image has to use.
      loadSegments: function(pseg, dseg) {
        var a = _copyIn(pseg), b = _copyIn(dseg);
        try { return Module._Nd500_LoadSegments(a, pseg.length, b, dseg.length); }
        finally { Module._free(a); Module._free(b); }
      },

      // The disk buffer is NOT freed here and NOT copied again: the emulator
      // reads and writes it in place, and getDiskBuffer() is how the page gets
      // written blocks back out. Freeing it would pull the disc out from under
      // a running guest.
      mountDisk: function(unit, bytes, writable) {
        var p = _copyIn(bytes);
        var rc = Module._Nd500_MountDisk(unit, p, bytes.length, writable ? 1 : 0);
        if (rc !== 0) Module._free(p);
        return rc;
      },
      unmountDisk: function(unit) { return Module._Nd500_UnmountDisk(unit); },
      diskSize:    function(unit) { return Module._Nd500_GetDiskSize(unit); },
      // A VIEW on the heap, not a copy - and it goes stale the moment the heap
      // grows. Read what you need out of it straight away.
      diskBytes:   function(unit) {
        var p = Module._Nd500_GetDiskBuffer(unit), n = Module._Nd500_GetDiskSize(unit);
        if (!p || !n) return null;
        return Module.HEAPU8.subarray(p, p + n);
      },

      boot: function() { return Module._Nd500_Boot(); },

      // ---- ethernet: put NDIX's et0 on the gateway's emulated segment ----
      //
      // Call AFTER boot. Before it there is no machine to hand frames to, and
      // ethAttach says so rather than half-working.
      //
      // The guest still has to configure the interface itself - the emulator
      // only carries frames:
      //     /etc/etconfig et0 0x08 0x00 0x26 0xF4 0x01 0x00
      //     /etc/ifconfig et0 inet 223.255.254.8 -trailers up
      // -trailers is NOT optional: NDIX's ARP advertises trailer encapsulation
      // that its own driver refuses to send, and without the flag every IP
      // packet is dropped inside etoutput while the interface looks healthy.
      ethAttach: function(segment) { return Module._Nd500_Eth_Attach(segment || 0); },
      ethDetach: function() { Module._Nd500_Eth_Detach(); },
      // Outbound frames lost because the worker stopped draining the ring.
      // Worth showing somewhere: from inside the guest a stalled tab and a slow
      // network look exactly the same.
      ethTxDropped: function() { return Module._Nd500_Eth_GetTxDropped() >>> 0; },
      // Instructions, not time. Stepping rather than running because the page
      // has one thread and the library's run() does not come back until the
      // guest stops.
      step: function(count) { return Module._Nd500_Step(count || 1); },
      // Ask this, not "is stopReason still none": NDIX takes page faults by
      // design and leaves a reason set while running perfectly happily.
      isRunning: function() { return !!Module._Nd500_IsRunning(); },
      pc:   function() { return Module._Nd500_GetPC() >>> 0; },
      stopReason: function() { return Module.UTF8ToString(Module._Nd500_GetStopReasonText()); },

      // Drain the console queue. Returns {unit, text} chunks in the order the
      // guest produced them; unit 255 is the emulator's own boot log, not
      // guest output.
      pollConsole: function(max) {
        var out = [], cur = -1, buf = '';
        var limit = max || 65536;
        for (var i = 0; i < limit; i++) {
          var v = Module._Nd500_PollConsole();
          if (v < 0) break;
          var u = (v >> 8) & 0xFF;
          if (u !== cur) { if (buf) out.push({ unit: cur, text: buf }); cur = u; buf = ''; }
          buf += String.fromCharCode(v & 0xFF);
        }
        if (buf) out.push({ unit: cur, text: buf });
        return out;
      },
      sendInput: function(unit, text) {
        var b = _utf8(text);
        var p = _copyIn(b);
        try { Module._Nd500_SendInput(unit, p, b.length); }
        finally { Module._free(p); }
      }
    },

    // --- Mode flag ---
    isWorkerMode: function() { return false; }
  };

})();
