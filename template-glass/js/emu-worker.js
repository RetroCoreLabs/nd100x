//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// emu-worker.js - Web Worker script for WASM emulation core
//
// Runs the ND-100 emulator in a dedicated Worker thread so the emulation loop
// continues unthrottled even when the browser tab is backgrounded.
// Communication with the main thread happens via postMessage.
//
// Activated by ?worker=1 URL parameter or localStorage 'nd100x-worker' = 'true'.

'use strict';

// =========================================================
// State
// =========================================================
var running = false;
var initialized = false;

// =========================================================
// Gateway WebSocket statistics
// =========================================================
var _wsStats = {
  connectedSince: 0,
  termIn:    { frames: 0, bytes: 0 },
  termOut:   { frames: 0, bytes: 0 },
  diskRead:  { ops: 0, bytes: 0 },
  diskWrite: { ops: 0, bytes: 0 },
  hdlcRx:    { frames: 0, bytes: 0 },
  hdlcTx:    { frames: 0, bytes: 0 },
  // ND-500 ethernet (NDIX's et0) on the gateway's emulated segment.
  ethRx:     { frames: 0, bytes: 0 },
  ethTx:     { frames: 0, bytes: 0 },
  clientConnects: 0,
  clientDisconnects: 0
};
var _wsStatsTimer = null;

// =========================================================
// Unthrottled scheduling via MessageChannel
// =========================================================
// setTimeout(0) gets throttled to 1s+ in background tabs, even in Workers.
// MessageChannel.postMessage is NOT throttled - it fires immediately.
var _schedCh = new MessageChannel();
// _loopArmed exists because the loop now has TWO reasons to run - the ND-100
// and the ND-500 - and either may start it. Without the flag, booting the
// ND-500 while the ND-100 is already looping would start a SECOND chain and
// every frame would step both machines twice.
var _loopArmed = false;
_schedCh.port1.onmessage = function() { _loopArmed = false; runLoop(); };
function scheduleNext() { _loopArmed = true; _schedCh.port2.postMessage(null); }
function ensureLoop() { if (!_loopArmed) scheduleNext(); }

// =========================================================
// Module pre-configuration (non-modularized build)
// =========================================================
// The build is NOT modularized - nd100wasm.js creates a global Module.
// We pre-define Module config here; importScripts merges into it.
// The build's cache-bust token (?v=...) arrives as this Worker's own query
// string (emu-proxy-worker.js puts it there) and goes on to every file this
// Worker loads by URL: nd100wasm.js, the .wasm and disk-io-worker.js. Without
// it a browser or edge cache can pair an old module with a new page.
var _bust = (self.location && self.location.search) || '';

var Module = {
  locateFile: function(path) {
    // .wasm file is in parent dir relative to js/ where this worker lives
    return '../' + path + _bust;
  },
  print: function(text) {
    postMessage({ type: 'log', level: 'info', text: text });
  },
  printErr: function(text) {
    postMessage({ type: 'log', level: 'error', text: text });
  },
  onRuntimeInitialized: function() {
    Module._EnableTerminalRingBuffer(1);
    // Whether the module has an ND-500 is known the moment it runs, and an
    // NDIX machine (ndix-machine.js) asks before any ND-100 Init() - which it
    // never does - so it is answered here, not only at 'initialized'.
    postMessage({ type: 'ready',
                  nd500Available: (typeof Module._Nd500_Available === 'function')
                                  ? !!Module._Nd500_Available() : false });
  }
};

// Load the Emscripten-generated JS (merges into our pre-defined Module)
importScripts('../nd100wasm.js' + _bust);

// =========================================================
// OPFS SyncAccessHandle pool for persistent disc block I/O
// =========================================================
// Handles are keyed by "driveType-unit" so SCSI unit 0 does not alias SMD
// unit 0. driveType matches the C DRIVE_TYPE enum (0=SMD, 2=SCSI); the OPFS
// directory is per type ('smd-images', 'scsi-images'). See disk-types.js.
var _opfsHandles = {};   // key -> SyncAccessHandle
var _opfsReady = {};     // key -> bool

function opfsKey(driveType, unit) { return driveType + '-' + unit; }

// OPFS directory for a driveType. Kept in one place so the naming is stable.
function opfsDirForType(driveType) {
  return (driveType === 2) ? 'scsi-images' : 'smd-images';
}

// Open OPFS file for a (driveType, unit) using SyncAccessHandle (synchronous
// I/O in Worker). Always closes any existing handle on this key first.
async function opfsOpenUnit(driveType, unit, fileName) {
  if (unit < 0) return false;

  var key = opfsKey(driveType, unit);

  // Close existing handle first (prevents SyncAccessHandle leaks)
  opfsCloseUnit(driveType, unit);

  try {
    var root = await navigator.storage.getDirectory();
    var dir = await root.getDirectoryHandle(opfsDirForType(driveType), { create: false });
    var fileHandle = await dir.getFileHandle(fileName);
    var accessHandle = await fileHandle.createSyncAccessHandle();
    _opfsHandles[key] = accessHandle;
    _opfsReady[key] = true;
    postMessage({ type: 'log', level: 'info', text: '[OPFS] ' + key + ' opened: ' + fileName + ' (' + accessHandle.getSize() + ' bytes)' });
    return true;
  } catch (e) {
    postMessage({ type: 'log', level: 'error', text: '[OPFS] Failed to open ' + key + ': ' + e.message });
    _opfsHandles[key] = null;
    _opfsReady[key] = false;
    return false;
  }
}

function opfsCloseUnit(driveType, unit) {
  var key = opfsKey(driveType, unit);
  if (_opfsHandles[key]) {
    try {
      _opfsHandles[key].flush();
      _opfsHandles[key].close();
    } catch (e) {}
    _opfsHandles[key] = null;
    _opfsReady[key] = false;
  }
}

function opfsGetSize(driveType, unit) {
  var h = _opfsHandles[opfsKey(driveType, unit)];
  return h ? h.getSize() : 0;
}

// Synchronous block read from OPFS (called from C via EM_JS -> opfsBlockRead)
function opfsBlockRead(driveType, unit, wasmPtr, bytes, offset) {
  var key = opfsKey(driveType, unit);
  if (!_opfsReady[key] || !_opfsHandles[key]) return -1;
  var dest = new Uint8Array(Module.HEAPU8.buffer, wasmPtr, bytes);
  var read = _opfsHandles[key].read(dest, { at: offset });
  return read;
}

// Synchronous block write to OPFS (called from C via EM_JS -> opfsBlockWrite)
function opfsBlockWrite(driveType, unit, wasmPtr, bytes, offset) {
  var key = opfsKey(driveType, unit);
  if (!_opfsReady[key] || !_opfsHandles[key]) return -1;
  var src = new Uint8Array(Module.HEAPU8.buffer, wasmPtr, bytes);
  var written = _opfsHandles[key].write(src, { at: offset });
  return written;
}

// Check if OPFS is available for a (driveType, unit)
function opfsIsAvailable(driveType, unit) {
  return _opfsReady[opfsKey(driveType, unit)] ? 1 : 0;
}

// =========================================================
// Gateway disk I/O sub-worker (SharedArrayBuffer + Atomics)
// =========================================================
var _diskWorker = null;
var _diskControlArray = null;  // Int32Array view (8 x Int32)
var _diskDataArray = null;     // Uint8Array view at offset 32
var _diskSharedBuffer = null;
var _diskReady = false;
var _diskGatewayDrives = { smd: {}, floppy: {}, scsi: {}, winchester: {} };  // [type][unit] -> true

// driveType number -> name. Index matches the C DRIVE_TYPE enum. See disk-types.js.
var DRIVE_TYPE_NAMES = ['smd', 'floppy', 'scsi', 'winchester'];
var _fullReadRequestId = null;  // pending gatewayReadFullImage request id

// Block read cache: key = "driveType-unit-offset-bytes" -> Uint8Array
var _diskBlockCache = new Map();
var _diskReadCount = 0;   // total gateway reads (cache misses)
var _diskWriteCount = 0;  // total gateway writes

function initDiskWorker(gatewayUrl) {
  // Close existing sub-worker before creating a new one
  closeDiskWorker();

  var hasSAB = (typeof SharedArrayBuffer !== 'undefined');
  var initMsg = { type: 'init', wsUrl: gatewayUrl };

  if (hasSAB) {
    _diskSharedBuffer = new SharedArrayBuffer(32 + 65536);  // 32 bytes control + 64KB data (SMD reads up to 64KB)
    _diskControlArray = new Int32Array(_diskSharedBuffer, 0, 8);
    _diskDataArray = new Uint8Array(_diskSharedBuffer, 32);
    Atomics.store(_diskControlArray, 0, 0);  // idle
    initMsg.sharedBuffer = _diskSharedBuffer;
  } else {
    postMessage({ type: 'log', level: 'warn',
      text: '[DiskIO] SharedArrayBuffer not available - disk listing only (no block I/O). Serve page via gateway for full support.' });
  }

  _diskWorker = new Worker('disk-io-worker.js' + _bust);
  _diskWorker.postMessage(initMsg);

  _diskWorker.onmessage = function(e) {
    if (e.data.type === 'connected') {
      _diskReady = true;
      postMessage({ type: 'disk-connected', blockIO: hasSAB });
    }
    if (e.data.type === 'disconnected') {
      _diskReady = false;
      postMessage({ type: 'disk-disconnected' });
    }
    if (e.data.type === 'disk-list') {
      postMessage({ type: 'disk-list', data: e.data.data });
    }
    if (e.data.type === 'error') {
      postMessage({ type: 'log', level: 'error', text: '[DiskIO] ' + e.data.error });
    }
    if (e.data.type === 'full-image-progress') {
      postMessage({ type: 'full-image-progress', id: _fullReadRequestId,
                    done: e.data.done, total: e.data.total });
    }
    if (e.data.type === 'full-image-data') {
      if (e.data.error) {
        postMessage({ type: 'full-image-data', id: _fullReadRequestId, error: e.data.error });
      } else {
        var buf = e.data.buffer;
        postMessage({ type: 'full-image-data', id: _fullReadRequestId,
                      driveType: e.data.driveType, unit: e.data.unit,
                      buffer: buf }, [buf]);
      }
      _fullReadRequestId = null;
    }
  };
}

function closeDiskWorker() {
  if (_diskWorker) {
    _diskWorker.postMessage({ type: 'close' });
    _diskWorker = null;
  }
  _diskReady = false;
  _diskGatewayDrives = { smd: {}, floppy: {}, scsi: {}, winchester: {} };
  _diskBlockCache.clear();
}

// Synchronous block read from gateway (called from C via EM_JS -> gatewayBlockRead)
function gatewayBlockRead(driveType, unit, wasmPtr, bytes, offset) {
  if (!_diskReady || !_diskControlArray) return -1;
  var typeName = DRIVE_TYPE_NAMES[driveType];
  if (!typeName || !_diskGatewayDrives[typeName][unit]) return -1;

  // Check block cache first
  var cacheKey = driveType + '-' + unit + '-' + offset + '-' + bytes;
  var cached = _diskBlockCache.get(cacheKey);
  if (cached) {
    Module.HEAPU8.set(cached, wasmPtr);
    _wsStats.diskRead.ops++;
    _wsStats.diskRead.bytes += cached.length;
    return cached.length;
  }

  // Cache miss - fetch from gateway via sub-worker
  _diskControlArray[1] = driveType;
  _diskControlArray[2] = unit;
  _diskControlArray[3] = offset;
  _diskControlArray[4] = bytes;
  _diskControlArray[7] = 0;  // read

  // Signal request and wait for response
  Atomics.store(_diskControlArray, 0, 1);
  Atomics.notify(_diskControlArray, 0);
  // Poll with short timeouts until sub-worker signals response ready
  var _rwc = 0;
  while (Atomics.load(_diskControlArray, 0) === 1) {
    Atomics.wait(_diskControlArray, 0, 1, 1);  // 1ms timeout
    if (++_rwc > 30000) {  // 30 second safety bail-out
      console.warn('[GW-READ] Timeout waiting for response');
      Atomics.store(_diskControlArray, 0, 0);
      return -1;
    }
  }

  // Read response
  var status = _diskControlArray[5];
  var dataLen = _diskControlArray[6];
  Atomics.store(_diskControlArray, 0, 0);  // reset to idle
  Atomics.notify(_diskControlArray, 0);    // wake sub-worker

  if (status !== 0 || dataLen <= 0) return -1;

  // Cache the block and copy to WASM heap
  var blockCopy = new Uint8Array(dataLen);
  blockCopy.set(_diskDataArray.subarray(0, dataLen));
  _diskBlockCache.set(cacheKey, blockCopy);

  Module.HEAPU8.set(blockCopy, wasmPtr);
  _wsStats.diskRead.ops++;
  _wsStats.diskRead.bytes += dataLen;
  _diskReadCount++;
  if (_diskReadCount === 1) {
    console.log('[Gateway I/O] First disk read: ' + DRIVE_TYPE_NAMES[driveType] + ' unit ' + unit + ' offset ' + offset + ' (' + dataLen + ' bytes)');
  } else if (_diskReadCount % 100 === 0) {
    console.log('[Gateway I/O] Disk reads: ' + _diskReadCount + ' blocks (' + (_wsStats.diskRead.bytes / 1024).toFixed(0) + ' KB), cache: ' + _diskBlockCache.size + ' entries');
  }
  return dataLen;
}

// Synchronous block write to gateway (called from C via EM_JS -> gatewayBlockWrite)
function gatewayBlockWrite(driveType, unit, wasmPtr, bytes, offset) {
  if (!_diskReady || !_diskControlArray) return -1;
  var typeName = DRIVE_TYPE_NAMES[driveType];
  if (!typeName || !_diskGatewayDrives[typeName][unit]) return -1;

  // Copy write data to shared buffer
  _diskDataArray.set(Module.HEAPU8.subarray(wasmPtr, wasmPtr + bytes), 0);

  // Write request parameters
  _diskControlArray[1] = driveType;
  _diskControlArray[2] = unit;
  _diskControlArray[3] = offset;
  _diskControlArray[4] = bytes;
  _diskControlArray[7] = 1;  // write

  // Signal request and wait for response
  Atomics.store(_diskControlArray, 0, 1);
  Atomics.notify(_diskControlArray, 0);
  // Poll with short timeouts - Atomics.notify from sub-worker may not wake Atomics.wait
  var _wwc = 0;
  while (Atomics.load(_diskControlArray, 0) === 1) {
    Atomics.wait(_diskControlArray, 0, 1, 1);  // 1ms timeout
    if (++_wwc > 30000) {  // 30 second safety bail-out
      console.warn('[GW-WRITE] Timeout waiting for response');
      Atomics.store(_diskControlArray, 0, 0);
      return -1;
    }
  }

  var status = _diskControlArray[5];
  Atomics.store(_diskControlArray, 0, 0);
  Atomics.notify(_diskControlArray, 0);
  if (status === 0) {
    // Update block cache with written data
    var cacheKey = driveType + '-' + unit + '-' + offset + '-' + bytes;
    var blockCopy = new Uint8Array(bytes);
    blockCopy.set(Module.HEAPU8.subarray(wasmPtr, wasmPtr + bytes));
    _diskBlockCache.set(cacheKey, blockCopy);

    _wsStats.diskWrite.ops++;
    _wsStats.diskWrite.bytes += bytes;
    _diskWriteCount++;
    if (_diskWriteCount === 1) {
      console.log('[Gateway I/O] First disk write: ' + DRIVE_TYPE_NAMES[driveType] + ' unit ' + unit + ' offset ' + offset + ' (' + bytes + ' bytes)');
    } else if (_diskWriteCount % 100 === 0) {
      console.log('[Gateway I/O] Disk writes: ' + _diskWriteCount + ' blocks (' + (_wsStats.diskWrite.bytes / 1024).toFixed(0) + ' KB)');
    }
  }
  return status === 0 ? bytes : -1;
}

// Check if gateway disk is available for a unit
function gatewayIsAvailable(driveType, unit) {
  var typeName = DRIVE_TYPE_NAMES[driveType];
  return (_diskReady && typeName && _diskGatewayDrives[typeName][unit]) ? 1 : 0;
}


// =========================================================
// WebSocket bridge state (remote terminal gateway)
// =========================================================
var _ws = null;              // WebSocket connection to gateway
var _wsUrl = '';             // For reconnection
var _wsReconnectTimer = null;
var _remoteIdentCodes = {};  // identCode -> true: GATEWAY-ONLY terminals (TERMINAL 12-19, no window in the page)
var _remoteTerminals = [];   // { identCode, name, logicalDevice } - what 'register' sends: every terminal but the console
var _windowIdentCodes = {};  // identCode -> true: the machine's own terminals (made by Init, each has a window)
var _gatewayBound = {};      // identCode -> true while a gateway TCP client is bound to that terminal
var _hungUp = {};            // identCode -> true: window terminal whose gateway client left (carrier dropped), until a key is typed in its window

// The list the gateway gets is EVERY terminal of the machine except the
// console: the machine's own terminals, which also have windows in the page,
// plus the gateway-only TERMINAL 12-19 that EnableRemoteTerminals() adds.
// Output of a gateway-only terminal goes to the gateway alone; a window
// terminal's output goes to its window, and to the gateway as well while a
// client is bound to it. Until 08-OCT-2026 only 12-19 were offered, so a
// guest that knows no terminal beyond 11 - BSD 2.11's kernel has tty1-7 on
// TERMINAL 5-11 (bsd211_481 cons_mt.c) - could not be reached at all.
// Standalone ND-500 running NDIX (no ND-100 Init): the gateway gets the NDIX
// ttys instead. tty unit u is terminal identCode u+1 - the same mapping
// ndix-machine.js uses (identCodeOfUnit), so the page's window for a tty and
// the gateway's binding name the same terminal. NDIX_TTY_COUNT mirrors
// TTY_COUNT there (units 1..3 are ttys; unit 0 is the console, excluded).
var NDIX_TTY_COUNT = 4;
function ndixStandalone() { return _nd500Booted && !initialized; }
function ndixIdentOfUnit(u) { return u + 1; }
function ndixUnitOfIdent(id) { return id - 1; }

function buildGatewayTerminalList() {
  _remoteTerminals = [];
  _remoteIdentCodes = {};
  if (ndixStandalone()) {
    for (var u = 1; u < NDIX_TTY_COUNT; u++) {
      _remoteTerminals.push({ identCode: ndixIdentOfUnit(u), name: 'tty' + u + ' (NDIX)', logicalDevice: -1 });
    }
    return _remoteTerminals;
  }
  for (var i = 1; i < 16; i++) {
    var id = Module._GetTerminalIdentCode(i);
    if (id === -1) continue;
    var nm = '';
    try {
      var np = Module._GetTerminalName(i);
      if (np) nm = Module.UTF8ToString(np);
    } catch (e) {}
    _remoteTerminals.push({ identCode: id, name: nm || ('Terminal ' + i),
                            logicalDevice: Module._GetTerminalLogicalDevice(i) });
    if (!_windowIdentCodes[id]) _remoteIdentCodes[id] = true;
  }
  return _remoteTerminals;
}

function wsConnect(url) {
  if (_ws) {
    try { _ws.close(); } catch(e) {}
  }
  _wsUrl = url;
  if (_wsReconnectTimer) { clearTimeout(_wsReconnectTimer); _wsReconnectTimer = null; }

  try {
    _ws = new WebSocket(url);
  } catch(err) {
    postMessage({ type: 'ws-status', connected: false, error: err.message });
    return;
  }

  _ws.binaryType = 'arraybuffer';

  _ws.onopen = function() {
    // Reset stats on new connection
    _wsStats.connectedSince = Date.now();
    _wsStats.termIn.frames = 0;    _wsStats.termIn.bytes = 0;
    _wsStats.termOut.frames = 0;   _wsStats.termOut.bytes = 0;
    _wsStats.diskRead.ops = 0;     _wsStats.diskRead.bytes = 0;
    _wsStats.diskWrite.ops = 0;    _wsStats.diskWrite.bytes = 0;
    _wsStats.hdlcRx.frames = 0;   _wsStats.hdlcRx.bytes = 0;
    _wsStats.hdlcTx.frames = 0;   _wsStats.hdlcTx.bytes = 0;
    _wsStats.clientConnects = 0;
    _wsStats.clientDisconnects = 0;
    // Start periodic stats posting (~1/s)
    if (_wsStatsTimer) clearInterval(_wsStatsTimer);
    _wsStatsTimer = setInterval(function() {
      postMessage({ type: 'ws-stats', stats: _wsStats });
    }, 1000);
    postMessage({ type: 'ws-status', connected: true, error: null });
    // Send register message with the terminal list, built now: the machine
    // may have been powered on (or an NDIX booted) after enableRemoteTerminals.
    if (initialized || _nd500Booted) buildGatewayTerminalList();
    if (_remoteTerminals.length > 0) {
      _ws.send(JSON.stringify({ type: 'register', terminals: _remoteTerminals }));
    }
  };

  _ws.onmessage = function(ev) {
    // Binary frame: [type:1][identCode:1][data:N]
    // Type 0x01 = term-input
    if (ev.data instanceof ArrayBuffer) {
      var buf = new Uint8Array(ev.data);
      if (buf.length >= 2 && buf[0] === 0x01) {
        // term-input (binary)
        _wsStats.termIn.frames++;
        _wsStats.termIn.bytes += buf.length;
        var identCode = buf[1];
        if (ndixStandalone()) {
          // NDIX tty: the bytes go to the ND-500's tty unit, as the page's
          // sendKey does, in one call.
          if (typeof Module._Nd500_SendInput === 'function' && buf.length > 2) {
            var inPtr = Module._malloc(buf.length - 2);
            Module.HEAPU8.set(buf.subarray(2), inPtr);
            Module._Nd500_SendInput(ndixUnitOfIdent(identCode), inPtr, buf.length - 2);
            Module._free(inPtr);
          }
        } else {
          for (var i = 2; i < buf.length; i++) {
            Module._SendKeyToTerminal(identCode, buf[i]);
          }
        }
      }
      else if (buf[0] === 0x10 && buf.length >= 4) {
        // HDLC RX frame: [0x10][channel][lenHi][lenLo][data...]
        _wsStats.hdlcRx.frames++;
        _wsStats.hdlcRx.bytes += buf.length;
        var channel = buf[1];
        var len = (buf[2] << 8) | buf[3];
        if (typeof Module._HDLC_InjectRxFrame === 'function' && len > 0) {
          var frameData = buf.subarray(4, 4 + len);
          var ptr = Module._malloc(len);
          Module.HEAPU8.set(frameData, ptr);
          Module._HDLC_InjectRxFrame(channel, ptr, len);
          Module._free(ptr);
        }
      }
      else if (buf[0] === 0x12 && buf.length >= 3) {
        // HDLC carrier status: [0x12][channel][present]
        if (typeof Module._HDLC_SetCarrier === 'function') {
          Module._HDLC_SetCarrier(buf[1], buf[2] & 0x01);
        }
      }
      else if (buf[0] === 0x30 && buf.length >= 4) {
        // Ethernet RX frame for the ND-500: [0x30][segment][lenHi][lenLo][data...]
        // Straight into the XMSG server, which raises the receive interrupt -
        // there is nothing to poll on this side.
        _wsStats.ethRx.frames++;
        _wsStats.ethRx.bytes += buf.length;
        var ethSeg = buf[1];
        var ethLen = (buf[2] << 8) | buf[3];
        // The guard is not paranoia: a length that overruns the message would
        // read whatever follows it in the heap and hand it to NDIX as a frame.
        if (ethLen > 0 && buf.length >= 4 + ethLen &&
            (typeof Module._Nd500_Eth_InjectRxFrame === 'function' ||
             typeof Module._Nd100_Eth_InjectRxFrame === 'function')) {
          var ethData = buf.subarray(4, 4 + ethLen);
          var ethPtr = Module._malloc(ethLen);
          Module.HEAPU8.set(ethData, ethPtr);
          // Both machines are offered the frame; each takes only its own segment.
          if (typeof Module._Nd500_Eth_InjectRxFrame === 'function') {
            Module._Nd500_Eth_InjectRxFrame(ethSeg, ethPtr, ethLen);
          }
          // ND-100 Ethernet II card (net = gateway[:segment]); queued, the card
          // collects it on its next tick.
          if (typeof Module._Nd100_Eth_InjectRxFrame === 'function') {
            Module._Nd100_Eth_InjectRxFrame(ethSeg, ethPtr, ethLen);
          }
          Module._free(ethPtr);
        }
      }
      else if (buf[0] === 0x32 && buf.length >= 3) {
        // Ethernet link status: [0x32][segment][present]
        if (typeof Module._Nd500_Eth_SetLink === 'function') {
          Module._Nd500_Eth_SetLink(buf[1], buf[2] & 0x01);
        }
        if (typeof Module._Nd100_Eth_SetLink === 'function') {
          Module._Nd100_Eth_SetLink(buf[1], buf[2] & 0x01);
        }
      }
      return;
    }

    // JSON text frame (control messages only)
    var msg;
    try { msg = JSON.parse(ev.data); } catch(e) { return; }

    switch (msg.type) {
      case 'client-connected': {
        _wsStats.clientConnects++;
        _gatewayBound[msg.identCode] = true;
        delete _hungUp[msg.identCode];
        // Restore carrier on this terminal (ND-100 terminals have a carrier
        // line; an NDIX tty has none)
        if (!ndixStandalone()) Module._SetTerminalCarrier(0, msg.identCode);
        postMessage({
          type: 'ws-client',
          action: 'connected',
          identCode: msg.identCode,
          clientAddr: msg.clientAddr
        });
        break;
      }
      case 'client-disconnected': {
        _wsStats.clientDisconnects++;
        delete _gatewayBound[msg.identCode];
        // Set carrier missing - the guest hangs the session up, as it does
        // for a dropped telnet line on the native build. A window terminal
        // gets its carrier back on the next key typed in its window.
        if (_windowIdentCodes[msg.identCode]) _hungUp[msg.identCode] = true;
        if (!ndixStandalone()) Module._SetTerminalCarrier(1, msg.identCode);
        postMessage({
          type: 'ws-client',
          action: 'disconnected',
          identCode: msg.identCode
        });
        break;
      }
    }
  };

  _ws.onclose = function(ev) {
    _wsStats.connectedSince = 0;
    if (_wsStatsTimer) { clearInterval(_wsStatsTimer); _wsStatsTimer = null; }
    postMessage({ type: 'ws-stats', stats: _wsStats });
    postMessage({ type: 'ws-status', connected: false, error: null });
    // Drop carrier on all active remote terminals. The gateway WebSocket is
    // gone, so any TCP clients it was serving are now disconnected. Signal
    // carrier missing for each so SINTRAN can clean up the sessions.
    if (Module && Module._SetTerminalCarrier) {
      for (var ic in _remoteIdentCodes) {
        Module._SetTerminalCarrier(1, parseInt(ic, 10));
      }
      // Window terminals a client was bound to: hang up the same way; the
      // window gets the carrier back on its next key.
      for (var bc in _gatewayBound) {
        if (_windowIdentCodes[bc]) {
          _hungUp[bc] = true;
          Module._SetTerminalCarrier(1, parseInt(bc, 10));
        }
      }
    }
    _gatewayBound = {};
    _ws = null;
    // Auto-reconnect after 3 seconds if we had a URL
    if (_wsUrl) {
      _wsReconnectTimer = setTimeout(function() {
        wsConnect(_wsUrl);
        // Re-init disk sub-worker so it reconnects and sends fresh disk list
        initDiskWorker(_wsUrl);
      }, 3000);
    }
  };

  _ws.onerror = function(ev) {
    postMessage({ type: 'ws-status', connected: false, error: 'WebSocket error' });
  };
}

function wsDisconnect() {
  _wsUrl = '';  // Clear URL to prevent reconnection
  if (_wsReconnectTimer) { clearTimeout(_wsReconnectTimer); _wsReconnectTimer = null; }
  if (_wsStatsTimer) { clearInterval(_wsStatsTimer); _wsStatsTimer = null; }
  if (_ws) {
    try { _ws.close(); } catch(e) {}
    _ws = null;
  }
  _remoteIdentCodes = {};
  _remoteTerminals = [];
  _gatewayBound = {};
  _wsStats.connectedSince = 0;
  postMessage({ type: 'ws-stats', stats: _wsStats });
  postMessage({ type: 'ws-status', connected: false, error: null });
}

function wsSend(msg) {
  if (_ws && _ws.readyState === 1) {
    _ws.send(JSON.stringify(msg));
  }
}

// =========================================================
// Terminal ring buffer flush
// =========================================================
function flushRingBuffer() {
  var output = [];
  if (typeof Module._PollTerminalOutput !== 'function') return output;
  var entry;
  var wsOutBuf = {};
  while ((entry = Module._PollTerminalOutput()) >= 0) {
    var ident = (entry >> 8) & 0xFF;
    if (_remoteIdentCodes[ident]) {
      // Gateway-only terminal: the gateway is its only screen.
      if (!wsOutBuf[ident]) wsOutBuf[ident] = [];
      wsOutBuf[ident].push(entry & 0xFF);
    } else {
      output.push(entry);  // packed: (identCode << 8) | charCode
      if (_gatewayBound[ident]) {
        // Window terminal with a gateway client on it: the window mirrors.
        if (!wsOutBuf[ident]) wsOutBuf[ident] = [];
        wsOutBuf[ident].push(entry & 0xFF);
      }
    }
  }
  // Send remote output over WebSocket as binary
  if (_ws && _ws.readyState === 1) {
    for (var rid in wsOutBuf) {
      var bytes = wsOutBuf[rid];
      var frame = new Uint8Array(2 + bytes.length);
      frame[0] = 0x02;  // term-output
      frame[1] = parseInt(rid) & 0xFF;
      for (var bi = 0; bi < bytes.length; bi++) {
        frame[2 + bi] = bytes[bi];
      }
      _ws.send(frame.buffer);
      _wsStats.termOut.frames++;
      _wsStats.termOut.bytes += frame.length;
    }
  }
  return output;
}

// =========================================================
// Snapshot builder
// =========================================================
function buildSnapshot() {
  var snap = {
    // CPU registers
    pc:    Module._Dbg_GetPC(),
    regA:  Module._Dbg_GetRegA(),
    regD:  Module._Dbg_GetRegD(),
    regB:  Module._Dbg_GetRegB(),
    regT:  Module._Dbg_GetRegT(),
    regL:  Module._Dbg_GetRegL(),
    regX:  Module._Dbg_GetRegX(),
    sts:   Module._Dbg_GetSTS(),
    ea:    Module._Dbg_GetEA(),
    pil:   Module._Dbg_GetPIL(),

    // System registers
    pans:  Module._Dbg_GetPANS(),
    opr:   Module._Dbg_GetOPR(),
    pgs:   Module._Dbg_GetPGS(),
    pvl:   Module._Dbg_GetPVL(),
    iic:   Module._Dbg_GetIIC(),
    iid:   Module._Dbg_GetIID(),
    pid:   Module._Dbg_GetPID(),
    pie:   Module._Dbg_GetPIE(),
    csr:   Module._Dbg_GetCSR(),
    ald:   Module._Dbg_GetALD(),
    pes:   Module._Dbg_GetPES(),
    pgc:   Module._Dbg_GetPGC(),
    pea:   Module._Dbg_GetPEA(),

    // Write-only / mixed registers
    panc:  Module._Dbg_GetPANC(),
    lmp:   Module._Dbg_GetLMP(),
    iie:   Module._Dbg_GetIIE(),
    ccl:   Module._Dbg_GetCCL(),
    lcil:  Module._Dbg_GetLCIL(),
    ucil:  Module._Dbg_GetUCIL(),
    eccr:  Module._Dbg_GetECCR(),

    // Execution state
    runMode:    Module._Dbg_GetRunMode(),
    stopReason: Module._Dbg_GetStopReason(),
    instrCount: Module._Dbg_GetInstrCount(),
    isPaused:   Module._Dbg_IsPaused(),

    // Page tables
    pageTableCount: Module._Dbg_GetPageTableCount(),
    extendedMode:   Module._Dbg_GetExtendedMode()
  };

  // PCR for all 16 levels
  snap.pcr = [];
  for (var i = 0; i < 16; i++) {
    snap.pcr.push(Module._Dbg_GetPCR(i));
  }

  return snap;
}

// =========================================================
// Execution loop
// =========================================================
// Run many Step batches per frame, post to main thread at ~60fps.
// This avoids flooding the main thread with thousands of postMessages/sec.
var STEPS_PER_BATCH = 10000;   // instructions per Step() call
var FRAME_INTERVAL_MS = 16;    // ~60fps target for posting to main thread

// ---- ND-500 -------------------------------------------------------------
// Stepped from the same run loop as the ND-100 rather than a timer of its
// own: the two share ONE wasm module because they share MPM5 memory, so two
// independent loops would re-enter it.
var ND500_SLICE_DEFAULT = 300000;   // instructions per frame, as ndix-machine.js uses
var _nd500Booted = false;
var _nd500Slice = ND500_SLICE_DEFAULT;

// Drain the ND-500 console queue into {unit, text} chunks, in the order the
// guest produced them. Unit 255 is the emulator's own boot log, not guest
// output, and the page marks it differently - so the unit has to survive the
// trip and the chunks cannot simply be concatenated.
function drainNd500Console(max) {
  if (typeof Module._Nd500_PollConsole !== 'function') return [];
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
}

function runLoop() {
  // NOT just `running`. That is the ND-100's flag, and a page can boot the
  // ND-500 alone - which is exactly what the ND-500 window does. Gating on it
  // meant Nd500_Boot() completed, printed its whole setup log, and then the
  // machine was never stepped: the console stopped dead after "set CAD 1" and
  // the guest never reached its banner.
  if (!running && !_nd500Booted) return;

  var frameStart = performance.now();
  var termOutput = [];
  var runMode = 1;  // CPU_RUNNING

  // Execute as many batches as fit in one frame interval
  var wsOutBuf = {};  // identCode -> [bytes] for batching across all Step batches

  while (running) {
    Module._Step(STEPS_PER_BATCH);

    // Drain ring buffer, routing remote output to WebSocket
    var entry;
    while ((entry = Module._PollTerminalOutput()) >= 0) {
      var ident = (entry >> 8) & 0xFF;
      if (_remoteIdentCodes[ident]) {
        // Gateway-only terminal -> batch for WebSocket
        if (!wsOutBuf[ident]) wsOutBuf[ident] = [];
        wsOutBuf[ident].push(entry & 0xFF);
      } else {
        // Window terminal -> main thread, and to the gateway too while a
        // client is bound to it (the window mirrors the session)
        termOutput.push(entry);
        if (_gatewayBound[ident]) {
          if (!wsOutBuf[ident]) wsOutBuf[ident] = [];
          wsOutBuf[ident].push(entry & 0xFF);
        }
      }
    }
    /*
     * Ring buffer entry encoding for character device output:
     *   Terminal:   (identCode << 8) | charCode   [bits 0-15]
     *   Printer:    0x80000000 | (1 << 16) | charCode  [bit 31 = device class flag]
     *   PaperTape:  0x80000000 | (2 << 16) | charCode
     */
    // Drain printer ring buffer
    if (typeof Module._PollPrinterOutput === 'function') {
      while ((entry = Module._PollPrinterOutput()) >= 0) {
        termOutput.push(0x80000000 | (1 << 16) | (entry & 0xFF));
      }
    }
    // Drain paper tape writer ring buffer
    if (typeof Module._PollPaperTapeWriterOutput === 'function') {
      while ((entry = Module._PollPaperTapeWriterOutput()) >= 0) {
        termOutput.push(0x80000000 | (2 << 16) | (entry & 0xFF));
      }
    }

    runMode = Module._Dbg_GetRunMode();
    if (runMode === 2 || runMode === 4 || runMode === 5) break;

    // Yield after frame interval so main thread can process
    if (performance.now() - frameStart >= FRAME_INTERVAL_MS) break;
  }

  // ---- ND-500 ----
  // Stepped here, in the ND-100's loop, because both CPUs live in ONE wasm
  // module (they share MPM5 memory) and a second loop would re-enter it.
  //
  // This must run BEFORE the WebSocket block below: the ethernet TX pump down
  // there drains what the guest transmitted, and stepping is what produces it.
  // With the two the other way round every frame would be one tick stale.
  var nd500Console = [];
  if (_nd500Booted && typeof Module._Nd500_Step === 'function') {
    Module._Nd500_Step(_nd500Slice);
    nd500Console = drainNd500Console();
    // A tty with a gateway client bound to it: its chunks go to the gateway
    // as well (0x02 frames); the page's window keeps mirroring them.
    if (_ws && _ws.readyState === 1 && ndixStandalone()) {
      for (var nc = 0; nc < nd500Console.length; nc++) {
        var ch = nd500Console[nc], chId = ndixIdentOfUnit(ch.unit);
        if (ch.unit === 255 || !_gatewayBound[chId]) continue;
        var chFrame = new Uint8Array(2 + ch.text.length);
        chFrame[0] = 0x02; chFrame[1] = chId & 0xFF;
        for (var cb = 0; cb < ch.text.length; cb++) chFrame[2 + cb] = ch.text.charCodeAt(cb) & 0xFF;
        _ws.send(chFrame.buffer);
        _wsStats.termOut.frames++;
        _wsStats.termOut.bytes += chFrame.length;
      }
    }
    // The run FLAG, not the stop reason. NDIX takes page faults constantly -
    // that is what demand paging is - and each one leaves a stop reason behind
    // while the machine carries on perfectly happily.
    if (typeof Module._Nd500_IsRunning === 'function' && !Module._Nd500_IsRunning()) {
      _nd500Booted = false;
      postMessage({
        type: 'nd500Stopped',
        reason: (typeof Module._Nd500_GetStopReasonText === 'function' &&
                 typeof Module.UTF8ToString === 'function')
                 ? Module.UTF8ToString(Module._Nd500_GetStopReasonText()) : 'stopped'
      });
    }
  }

  // Send batched remote terminal output over WebSocket as binary (once per frame)
  // Binary format: [0x02][identCode][data bytes...]
  if (_ws && _ws.readyState === 1) {
    for (var rid in wsOutBuf) {
      var bytes = wsOutBuf[rid];
      var frame = new Uint8Array(2 + bytes.length);
      frame[0] = 0x02;  // term-output
      frame[1] = parseInt(rid) & 0xFF;
      for (var bi = 0; bi < bytes.length; bi++) {
        frame[2 + bi] = bytes[bi];
      }
      _ws.send(frame.buffer);
      _wsStats.termOut.frames++;
      _wsStats.termOut.bytes += frame.length;
    }

    // Poll HDLC TX frames and send over WebSocket
    if (typeof Module._HDLC_PollTxFrame === 'function') {
      while (Module._HDLC_PollTxFrame() > 0) {
        var txLen = Module._HDLC_GetLastTxLength();
        var txPtr = Module._HDLC_GetLastTxBuffer();
        var txChan = Module._HDLC_GetLastTxChannel();
        if (txLen > 0) {
          var txFrame = new Uint8Array(4 + txLen);
          txFrame[0] = 0x11;  // HDLC TX
          txFrame[1] = txChan;
          txFrame[2] = (txLen >> 8) & 0xFF;
          txFrame[3] = txLen & 0xFF;
          txFrame.set(Module.HEAPU8.subarray(txPtr, txPtr + txLen), 4);
          _ws.send(txFrame.buffer);
          _wsStats.hdlcTx.frames++;
          _wsStats.hdlcTx.bytes += txFrame.length;
        }
      }
    }

    // Poll ethernet TX frames from the ND-100 Ethernet II card (net = gateway[:segment])
    // and send them to the segment. Drain all: the card's ring holds 32.
    if (typeof Module._Nd100_Eth_PollTxFrame === 'function') {
      while (Module._Nd100_Eth_PollTxFrame() > 0) {
        var cLen = Module._Nd100_Eth_GetLastTxLength();
        var cPtr = Module._Nd100_Eth_GetLastTxBuffer();
        var cSeg = Module._Nd100_Eth_GetLastTxSegment();
        if (cLen > 0) {
          var cFrame = new Uint8Array(4 + cLen);
          cFrame[0] = 0x31;  // ethernet TX
          cFrame[1] = cSeg;
          cFrame[2] = (cLen >> 8) & 0xFF;
          cFrame[3] = cLen & 0xFF;
          cFrame.set(Module.HEAPU8.subarray(cPtr, cPtr + cLen), 4);
          _ws.send(cFrame.buffer);
          _wsStats.ethTx.frames++;
          _wsStats.ethTx.bytes += cFrame.length;
        }
      }
    }

    // Poll ethernet TX frames from the ND-500 and send them to the segment.
    //
    // Drain EVERY frame each tick rather than one. The ring in nd500_wasm.c is
    // 16 deep and drops when it fills; NDIX sends an ARP burst at interface
    // bring-up, so leaving frames behind here loses exactly the packets that
    // start a conversation.
    if (typeof Module._Nd500_Eth_PollTxFrame === 'function') {
      while (Module._Nd500_Eth_PollTxFrame() > 0) {
        var eLen = Module._Nd500_Eth_GetLastTxLength();
        var ePtr = Module._Nd500_Eth_GetLastTxBuffer();
        var eSeg = Module._Nd500_Eth_GetLastTxSegment();
        if (eLen > 0) {
          var eFrame = new Uint8Array(4 + eLen);
          eFrame[0] = 0x31;  // ethernet TX
          eFrame[1] = eSeg;
          eFrame[2] = (eLen >> 8) & 0xFF;
          eFrame[3] = eLen & 0xFF;
          eFrame.set(Module.HEAPU8.subarray(ePtr, ePtr + eLen), 4);
          _ws.send(eFrame.buffer);
          _wsStats.ethTx.frames++;
          _wsStats.ethTx.bytes += eFrame.length;
        }
      }
    }
  }

  // Throttled printer job timeout check (~1Hz)
  var _pNow = performance.now();
  if (!runLoop._lastPrinterCheck || _pNow - runLoop._lastPrinterCheck >= 1000) {
    runLoop._lastPrinterCheck = _pNow;
    if (typeof Module._PrinterCheckTimeout === 'function' && Module._PrinterCheckTimeout() === 1) {
      postMessage({
        type: 'printerJobCompleted',
        jobNumber: Module._PrinterGetLastCompletedJob(),
        startTime: Module._PrinterGetLastJobStartTime(),
        endTime: Module._PrinterGetLastJobEndTime(),
        bytes: Module._PrinterGetLastJobBytes(),
        lines: Module._PrinterGetLastJobLines()
      });
    }
  }

  // Send one consolidated frame to main thread (includes all CPU registers)
  postMessage({
    type: 'frame',
    termOutput: termOutput,
    nd500Console: nd500Console,
    runMode: runMode,
    pil: Module._Dbg_GetPIL(),
    sts: Module._Dbg_GetSTS(),
    pc: Module._Dbg_GetPC(),
    instrCount: Module._Dbg_GetInstrCount(),
    regA: Module._Dbg_GetRegA(),
    regD: Module._Dbg_GetRegD(),
    regB: Module._Dbg_GetRegB(),
    regT: Module._Dbg_GetRegT(),
    regL: Module._Dbg_GetRegL(),
    regX: Module._Dbg_GetRegX(),
    ea:   Module._Dbg_GetEA()
  });

  if (runMode === 2) { // CPU_BREAKPOINT
    running = false;
    postMessage({ type: 'breakpoint', snapshot: buildSnapshot() });
    if (_nd500Booted) scheduleNext();   // an ND-100 breakpoint is not the ND-500's
    return;
  }

  if (runMode === 4 || runMode === 5) { // CPU_STOPPED or CPU_SHUTDOWN
    running = false;
    postMessage({ type: 'stopped', snapshot: buildSnapshot() });
    // The ND-100 halting says nothing about the ND-500. Keep the loop alive
    // for it rather than freezing a guest that is running perfectly well.
    if (_nd500Booted) scheduleNext();
    return;
  }

  scheduleNext();  // MessageChannel — immune to background-tab throttling
}

// =========================================================
// Message handler
// =========================================================
onmessage = function(e) {
  var msg = e.data;
  if (!msg || !msg.type) return;

  switch (msg.type) {

    // --- Lifecycle ---
    case 'init': {
      // Build from the config when one came with the command and the module is
      // new enough to have the export; otherwise the built-in device set.
      var cfgErr = '';
      var result;
      if (msg.ini && Module._InitWithConfig) {
        cfgErr = Module.ccall('InitWithConfig', 'string', ['string'], [msg.ini]);
        result = 0;
      } else {
        result = Module._Init();
      }
      if (cfgErr) console.error('[worker] machine config rejected: ' + cfgErr);
      initialized = true;
      // Gather terminal info. These are the machine's own terminals - each
      // gets a window in the page - as opposed to the gateway-only ones
      // EnableRemoteTerminals() adds later.
      var terminalInfo = [];
      _windowIdentCodes = {};
      for (var i = 0; i < 16; i++) {
        var address = Module._GetTerminalAddress(i);
        if (address !== -1) {
          _windowIdentCodes[Module._GetTerminalIdentCode(i)] = true;
          terminalInfo.push({
            index: i,
            address: address,
            identCode: Module._GetTerminalIdentCode(i),
            logicalDevice: Module._GetTerminalLogicalDevice(i)
          });
        }
      }
      // nd500Available comes from the module, not from a guess: a build made
      // without an nd500x checkout exports the stubs and answers 0, and the
      // page must disable the ND-500 window rather than offer a Boot button
      // that cannot work.
      postMessage({ type: 'initialized', result: result, terminals: terminalInfo,
                    configError: cfgErr,
                    nd500Available: (typeof Module._Nd500_Available === 'function')
                                    ? !!Module._Nd500_Available() : false });

      // After Init creates terminal devices, if WS is already connected
      // (auto-connect may have fired before Init), re-discover and register
      // remote terminals with the gateway.
      if (_ws && _ws.readyState === 1) {
        Module._EnableRemoteTerminals();
        buildGatewayTerminalList();
        if (_remoteTerminals.length > 0) {
          _ws.send(JSON.stringify({ type: 'register', terminals: _remoteTerminals }));
          console.log('[Worker] Post-Init: re-registered ' + _remoteTerminals.length + ' remote terminals with gateway');
        }
      }
      break;
    }

    case 'boot': {
      var bootResult = (typeof Module._BootFrom === 'function')
        ? Module.ccall('BootFrom', 'number', ['number', 'number', 'string'],
                       [msg.bootType, msg.bootUnit | 0, msg.image || null])
        : Module._Boot(msg.bootType);
      var termOutput = flushRingBuffer();
      postMessage({
        type: 'booted',
        result: bootResult,
        snapshot: buildSnapshot(),
        termOutput: termOutput,
        id: msg.id
      });
      break;
    }

    case 'start': {
      if (!running) {
        running = true;
        ensureLoop();   // may already be armed by a booted ND-500
      }
      break;
    }

    case 'stop': {
      running = false;
      Module._Stop();
      postMessage({ type: 'stopped', snapshot: buildSnapshot() });
      break;
    }

    // ================================================================
    // ND-500
    //
    // The ethernet transport above (0x30 / 0x31 / 0x32) has always been here,
    // but nothing could CREATE an ND-500 inside this worker, so there was
    // never a machine for it to carry frames to and from. That is what these
    // commands are for.
    //
    // The direct-mode API in emu-proxy.js is synchronous - boot() returns a
    // number, pollConsole() returns an array. None of that survives a Worker
    // boundary, so the split here is deliberate: COMMANDS come down as
    // fire-and-forget messages and results go back either as a reply keyed by
    // msg.id or, for console output, batched into the frame message the run
    // loop already sends. The ND-100 terminals work exactly this way.
    // ================================================================

    case 'nd500SetEnv': {
      // Must precede nd500Create: nd500x reads its ND500X_* switches once, on
      // first use, and a browser has no environment to read them from.
      var seRc = -1;
      if (typeof Module.ccall === 'function') {
        seRc = Module.ccall('Nd500_SetEnv', 'number', ['string', 'string'],
                            [msg.name, msg.value == null ? '1' : String(msg.value)]);
      }
      postMessage({ type: 'nd500Result', id: msg.id, op: 'setEnv', rc: seRc });
      break;
    }

    case 'nd500Create': {
      var crRc = -1;
      if (typeof Module._Nd500_Create === 'function') {
        crRc = Module._Nd500_Create(msg.memBytes || 0);
      }
      postMessage({ type: 'nd500Result', id: msg.id, op: 'create', rc: crRc });
      break;
    }

    case 'nd500LoadKernel': {
      var lkRc = -1;
      if (typeof Module._Nd500_LoadKernel === 'function' && msg.data) {
        var lkPtr = Module._malloc(msg.data.length);
        Module.HEAPU8.set(msg.data, lkPtr);
        lkRc = Module._Nd500_LoadKernel(lkPtr, msg.data.length);
        Module._free(lkPtr);          // staged into MEMFS, the copy is done with
      }
      postMessage({ type: 'nd500Result', id: msg.id, op: 'loadKernel', rc: lkRc });
      break;
    }

    case 'nd500MountDisk': {
      // The pointer is NOT freed on success. nd500_wasm.c keeps it and the
      // guest reads and writes the disc in place; freeing it would pull the
      // disc out from under a running machine.
      var mdRc = -1;
      if (typeof Module._Nd500_MountDisk === 'function' && msg.data) {
        var mdPtr = Module._malloc(msg.data.length);
        Module.HEAPU8.set(msg.data, mdPtr);
        mdRc = Module._Nd500_MountDisk(msg.unit | 0, mdPtr, msg.data.length,
                                       msg.writable ? 1 : 0);
        if (mdRc !== 0) Module._free(mdPtr);
      }
      postMessage({ type: 'nd500Result', id: msg.id, op: 'mountDisk', rc: mdRc });
      break;
    }

    case 'nd500Boot': {
      var btRc = -1;
      if (typeof Module._Nd500_Boot === 'function') btRc = Module._Nd500_Boot();
      if (btRc === 0) {
        _nd500Booted = true;
        ensureLoop();
        // A gateway already connected gets the NDIX ttys now
        if (_ws && _ws.readyState === 1) {
          buildGatewayTerminalList();
          if (_remoteTerminals.length > 0) _ws.send(JSON.stringify({ type: 'register', terminals: _remoteTerminals }));
        }
      }
      postMessage({ type: 'nd500Result', id: msg.id, op: 'boot', rc: btRc,
                    console: drainNd500Console() });
      break;
    }

    case 'nd500EthAttach': {
      // AFTER boot, on purpose: before it there is no machine to hand frames
      // to and Nd500_Eth_Attach says so rather than half-working.
      var eaRc = -1;
      if (typeof Module._Nd500_Eth_Attach === 'function') {
        eaRc = Module._Nd500_Eth_Attach(msg.segment | 0);
      }
      postMessage({ type: 'nd500Result', id: msg.id, op: 'ethAttach', rc: eaRc });
      break;
    }

    case 'nd500EthDetach': {
      if (typeof Module._Nd500_Eth_Detach === 'function') Module._Nd500_Eth_Detach();
      break;
    }

    case 'nd500SendInput': {
      if (typeof Module._Nd500_SendInput === 'function' && msg.data) {
        var siPtr = Module._malloc(msg.data.length);
        Module.HEAPU8.set(msg.data, siPtr);
        Module._Nd500_SendInput(msg.unit | 0, siPtr, msg.data.length);
        Module._free(siPtr);
      }
      break;
    }

    case 'nd500SetSlice': {
      _nd500Slice = msg.slice > 0 ? msg.slice : ND500_SLICE_DEFAULT;
      break;
    }

    case 'nd500Pause': {
      _nd500Booted = false;   // stop stepping; the machine itself is untouched
      break;
    }

    // --- Terminal I/O ---
    case 'key': {
      // A key typed in the window of a terminal whose gateway client hung
      // up gives the terminal its carrier back first (the native build's
      // "re-activate locally").
      if (_hungUp[msg.identCode]) {
        delete _hungUp[msg.identCode];
        Module._SetTerminalCarrier(0, msg.identCode);
      }
      Module._SendKeyToTerminal(msg.identCode, msg.keyCode);
      break;
    }

    case 'carrier': {
      Module._SetTerminalCarrier(msg.flag, msg.identCode);
      break;
    }

    case 'loadPaperTape': {
      if (typeof Module._LoadPaperTape === 'function' && msg.data) {
        var ptr = Module._malloc(msg.data.length);
        Module.HEAPU8.set(new Uint8Array(msg.data), ptr);
        Module._LoadPaperTape(ptr, msg.data.length);
        Module._free(ptr);
      }
      break;
    }

    // --- Printer PDF pipeline ---
    case 'printerFlushJob': {
      if (typeof Module._PrinterFlushJob === 'function') Module._PrinterFlushJob();
      break;
    }
    case 'printerSetType': {
      if (typeof Module._PrinterSetType === 'function') Module._PrinterSetType(msg.value);
      break;
    }
    case 'printerGetType': {
      var ptype = typeof Module._PrinterGetType === 'function' ? Module._PrinterGetType() : 0;
      postMessage({ type: 'printerGetTypeResult', id: msg.id, value: ptype });
      break;
    }
    case 'printerCheckTimeout': {
      var flushed = typeof Module._PrinterCheckTimeout === 'function' ? Module._PrinterCheckTimeout() : 0;
      postMessage({ type: 'printerCheckTimeoutResult', id: msg.id, value: flushed });
      break;
    }
    case 'printerGetState': {
      postMessage({
        type: 'printerGetStateResult', id: msg.id,
        lastCompletedJob: typeof Module._PrinterGetLastCompletedJob === 'function' ? Module._PrinterGetLastCompletedJob() : 0,
        lastJobStartTime: typeof Module._PrinterGetLastJobStartTime === 'function' ? Module._PrinterGetLastJobStartTime() : 0,
        lastJobEndTime: typeof Module._PrinterGetLastJobEndTime === 'function' ? Module._PrinterGetLastJobEndTime() : 0,
        lastJobBytes: typeof Module._PrinterGetLastJobBytes === 'function' ? Module._PrinterGetLastJobBytes() : 0,
        lastJobLines: typeof Module._PrinterGetLastJobLines === 'function' ? Module._PrinterGetLastJobLines() : 0,
        activeJobBytes: typeof Module._PrinterGetActiveJobBytes === 'function' ? Module._PrinterGetActiveJobBytes() : 0,
        activeJobLines: typeof Module._PrinterGetActiveJobLines === 'function' ? Module._PrinterGetActiveJobLines() : 0,
        isJobActive: typeof Module._PrinterIsJobActive === 'function' ? Module._PrinterIsJobActive() : 0,
        jobNumber: typeof Module._PrinterGetJobNumber === 'function' ? Module._PrinterGetJobNumber() : 0,
        printerType: typeof Module._PrinterGetType === 'function' ? Module._PrinterGetType() : 0
      });
      break;
    }

    // --- Debugger step operations ---
    case 'step': {
      var stepMethods = {
        'stepOne':  Module._Dbg_StepOne,
        'stepOver': Module._Dbg_StepOver,
        'stepOut':  Module._Dbg_StepOut
      };
      var stepFn = stepMethods[msg.method];
      if (stepFn) {
        var count = msg.count || 1;
        for (var s = 0; s < count; s++) {
          stepFn();
        }
      }
      var stepTermOutput = flushRingBuffer();
      postMessage({
        type: 'stepDone',
        snapshot: buildSnapshot(),
        termOutput: stepTermOutput,
        id: msg.id
      });
      break;
    }

    case 'setPaused': {
      Module._Dbg_SetPaused(msg.paused ? 1 : 0);
      break;
    }

    case 'runDbg': {
      var wasRunning = running;
      running = false;  // Stop autonomous loop if running
      var remaining = Module._Dbg_RunWithBreakpoints(msg.maxSteps || 10000);
      var rdbTermOutput = flushRingBuffer();
      postMessage({
        type: 'runDbgDone',
        snapshot: buildSnapshot(),
        termOutput: rdbTermOutput,
        remaining: remaining,
        id: msg.id
      });
      break;
    }

    // --- Breakpoints ---
    case 'addBreakpoint': {
      Module._Dbg_AddBreakpoint(msg.addr);
      break;
    }

    case 'removeBreakpoint': {
      Module._Dbg_RemoveBreakpoint(msg.addr);
      break;
    }

    case 'clearBreakpoints': {
      Module._Dbg_ClearBreakpoints();
      break;
    }

    // --- Watchpoints ---
    case 'addWatchpoint': {
      Module._Dbg_AddWatchpoint(msg.addr, msg.wtype);
      break;
    }

    case 'removeWatchpoint': {
      Module._Dbg_RemoveWatchpoint(msg.addr);
      break;
    }

    case 'clearWatchpoints': {
      Module._Dbg_ClearWatchpoints();
      break;
    }

    // --- Register setters ---
    case 'setReg': {
      var setters = {
        pc:  Module._Dbg_SetPC,
        a:   Module._Dbg_SetRegA,
        d:   Module._Dbg_SetRegD,
        b:   Module._Dbg_SetRegB,
        t:   Module._Dbg_SetRegT,
        l:   Module._Dbg_SetRegL,
        x:   Module._Dbg_SetRegX,
        sts: Module._Dbg_SetSTS
      };
      var setter = setters[msg.reg];
      if (setter) setter(msg.value);
      break;
    }

    // --- Memory write ---
    case 'writeMemory': {
      Module._Dbg_WriteMemory(msg.addr, msg.value);
      break;
    }

    // --- Memory read ---
    case 'readMemory': {
      var val = Module._Dbg_ReadMemory(msg.addr);
      postMessage({ type: 'readMemoryResult', id: msg.id, value: val });
      break;
    }

    case 'readMemoryBlock': {
      var blockResult = [];
      for (var bi = 0; bi < msg.count; bi++) {
        blockResult.push(Module._Dbg_ReadMemory(msg.addr + bi));
      }
      postMessage({ type: 'readMemoryBlockResult', id: msg.id, values: blockResult });
      break;
    }

    case 'disassembleWords': {
      if (!Module._Dbg_LoadInspectBuffer || !Module._Dbg_DisassembleFromBuffer) {
        postMessage({ type: 'disassembleWordsResult', id: msg.id,
                      error: 'WASM disassembler not exported (rebuild?)' });
        break;
      }
      var dwWords = new Uint16Array(msg.words);
      var dwPtr = Module._malloc(dwWords.byteLength);
      if (!dwPtr) {
        postMessage({ type: 'disassembleWordsResult', id: msg.id,
                      error: 'malloc(' + dwWords.byteLength + ') failed' });
        break;
      }
      Module.HEAPU8.set(new Uint8Array(msg.words), dwPtr);
      Module._Dbg_LoadInspectBuffer(dwPtr, dwWords.length, msg.baseAddr);
      Module._free(dwPtr);
      var dwText = Module.UTF8ToString(Module._Dbg_DisassembleFromBuffer(0, dwWords.length));
      postMessage({ type: 'disassembleWordsResult', id: msg.id, value: dwText });
      break;
    }

    case 'readSMDSectors': {
      var smdErr = null;
      var smdBuf = null;
      if (!Module._Dbg_ReadSMDSectors) {
        smdErr = 'WASM Dbg_ReadSMDSectors not exported (rebuild?)';
      } else if (msg.count <= 0 || msg.count > 256) {
        smdErr = 'count=' + msg.count + ' out of range [1,256]';
      } else {
        var smdPtr = Module._Dbg_ReadSMDSectors(msg.unit, msg.lba, msg.count);
        if (!smdPtr) {
          smdErr = 'Dbg_ReadSMDSectors returned 0 (unit=' + msg.unit + ' lba=' + msg.lba +
                   ' count=' + msg.count + ' — drive not mounted? read past end?)';
        } else {
          smdBuf = new Uint8Array(Module.HEAPU8.buffer, smdPtr, msg.count * 1024).slice().buffer;
        }
      }
      if (smdErr) {
        postMessage({ type: 'readSMDSectorsResult', id: msg.id, error: smdErr });
      } else {
        postMessage({ type: 'readSMDSectorsResult', id: msg.id, data: smdBuf }, [smdBuf]);
      }
      break;
    }

    case 'readPhysicalMemory': {
      var pval = Module._Dbg_ReadPhysicalMemory(msg.addr);
      postMessage({ type: 'readPhysicalMemoryResult', id: msg.id, value: pval });
      break;
    }

    case 'readPhysicalMemoryBlock': {
      var pblockResult = [];
      for (var pi = 0; pi < msg.count; pi++) {
        pblockResult.push(Module._Dbg_ReadPhysicalMemory(msg.addr + pi));
      }
      postMessage({ type: 'readPhysicalMemoryBlockResult', id: msg.id, values: pblockResult });
      break;
    }

    case 'dumpPhysicalMemory': {
      var dumpResult = Module._Dbg_DumpPhysicalMemory(msg.count);
      postMessage({ type: 'dumpPhysicalMemoryResult', id: msg.id, value: dumpResult });
      break;
    }

    // --- Breakpoint/Watchpoint queries ---
    case 'getBreakpointList': {
      var bpStr = Module.UTF8ToString(Module._Dbg_GetBreakpointList());
      postMessage({ type: 'getBreakpointListResult', id: msg.id, value: bpStr });
      break;
    }

    case 'getWatchpointInfo': {
      var wpCount = Module._Dbg_GetWatchpointCount();
      var wps = [];
      for (var wi = 0; wi < wpCount; wi++) {
        wps.push({
          addr: Module._Dbg_GetWatchpointAddr(wi),
          wtype: Module._Dbg_GetWatchpointType(wi)
        });
      }
      postMessage({ type: 'getWatchpointInfoResult', id: msg.id, count: wpCount, watchpoints: wps });
      break;
    }

    // --- Disassembly / Level info ---
    case 'disassemble': {
      var disasmStr = Module.UTF8ToString(Module._Dbg_Disassemble(msg.addr, msg.count));
      postMessage({ type: 'disassembleResult', id: msg.id, value: disasmStr });
      break;
    }

    case 'getLevelInfo': {
      var lvlStr = Module.UTF8ToString(Module._Dbg_GetLevelInfo());
      postMessage({ type: 'getLevelInfoResult', id: msg.id, value: lvlStr });
      break;
    }

    // --- ccall (generic Module.ccall proxy) ---
    case 'ccall': {
      try {
        var ccResult = Module.ccall(msg.name, msg.retType, msg.argTypes || [], msg.argValues || []);
        postMessage({ type: 'ccallResult', id: msg.id, value: ccResult });
      } catch(ccErr) {
        postMessage({ type: 'ccallResult', id: msg.id, error: ccErr.message });
      }
      break;
    }

    // --- Page tables ---
    case 'getPageTableEntryRaw': {
      var pteVal = Module._Dbg_GetPageTableEntryRaw(msg.pt, msg.vpage);
      postMessage({ type: 'getPageTableEntryRawResult', id: msg.id, value: pteVal });
      break;
    }

    case 'getPageTableMap': {
      var ptEntries = new Array(64);
      for (var pti = 0; pti < 64; pti++) {
        ptEntries[pti] = Module._Dbg_GetPageTableEntryRaw(msg.pt, pti) >>> 0;
      }
      postMessage({ type: 'getPageTableMapResult', id: msg.id, entries: ptEntries });
      break;
    }

    // --- FS read operations ---
    case 'fsRead': {
      try {
        var fileData = Module.FS.readFile(msg.path);
        var buf = fileData.buffer.slice(fileData.byteOffset, fileData.byteOffset + fileData.byteLength);
        postMessage({ type: 'fsReadResult', id: msg.id, buffer: buf }, [buf]);
      } catch(err) {
        postMessage({ type: 'fsReadResult', id: msg.id, error: err.message });
      }
      break;
    }

    case 'fsStat': {
      try {
        var stat = Module.FS.stat(msg.path);
        postMessage({ type: 'fsStatResult', id: msg.id, stat: { size: stat.size, mtime: stat.mtime } });
      } catch(err) {
        postMessage({ type: 'fsStatResult', id: msg.id, error: err.message });
      }
      break;
    }

    // --- Snapshot request ---
    case 'snapshot': {
      postMessage({ type: 'snapshot', snapshot: buildSnapshot() });
      break;
    }

    // --- Filesystem operations ---
    case 'loadDisk': {
      try {
        var data = new Uint8Array(msg.buffer);
        Module.FS.writeFile(msg.path, data);
        try { Module.FS.chmod(msg.path, 0x1B6); } catch(e) {}  // 0o666
        postMessage({ type: 'diskLoaded', path: msg.path, size: data.length, id: msg.id });
      } catch(err) {
        postMessage({ type: 'diskLoaded', path: msg.path, error: err.message, id: msg.id });
      }
      break;
    }

    case 'fsWrite': {
      try {
        var writeData = new Uint8Array(msg.buffer);
        Module.FS.writeFile(msg.path, writeData);
        try { Module.FS.chmod(msg.path, 0x1B6); } catch(e) {}
        postMessage({ type: 'fsResult', id: msg.id, size: writeData.length });
      } catch(err) {
        postMessage({ type: 'fsResult', id: msg.id, error: err.message });
      }
      break;
    }

    case 'fsUnlink': {
      try {
        Module.FS.unlink(msg.path);
      } catch(e) {}
      break;
    }

    // --- Floppy/SMD remount ---
    case 'remountFloppy': {
      var rf = Module._RemountFloppy(msg.unit);
      postMessage({ type: 'fsResult', id: msg.id, result: rf });
      break;
    }

    case 'remountSMD': {
      var rs = Module._RemountSMD(msg.unit);
      postMessage({ type: 'fsResult', id: msg.id, result: rs });
      break;
    }

    case 'unmountFloppy': {
      Module._UnmountFloppy(msg.unit);
      break;
    }

    case 'unmountSMD': {
      Module._UnmountSMD(msg.unit);
      break;
    }

    case 'remountSCSI': {
      var rsc = Module._RemountSCSI(msg.unit);
      postMessage({ type: 'fsResult', id: msg.id, result: rsc });
      break;
    }

    case 'unmountSCSI': {
      Module._UnmountSCSI(msg.unit);
      break;
    }

    // --- OPFS persistent storage ---
    // driveType defaults to 0 (SMD) for backward-compatible messages. SCSI
    // (driveType 2) sends the same message with driveType set. The C mount
    // export is chosen by driveType so one handler serves both.
    case 'opfsMountSMD':
    case 'opfsMountDisc': {
      var mdType = msg.driveType || 0;
      postMessage({ type: 'log', level: 'info', text: '[Worker] opfsMountDisc type=' + mdType + ' unit=' + msg.unit + ' file=' + msg.fileName });
      opfsOpenUnit(mdType, msg.unit, msg.fileName).then(function(ok) {
        if (ok) {
          var size = opfsGetSize(mdType, msg.unit);
          if (mdType === 2) {
            Module._MountSCSIFromOPFS(msg.unit, size);
          } else {
            Module._MountSMDFromOPFS(msg.unit, size);
          }
          postMessage({ type: 'log', level: 'info', text: '[Worker] MountDiscFromOPFS type=' + mdType + ' unit=' + msg.unit + ' size=' + size + ' OK' });
          postMessage({ type: 'opfsMountResult', id: msg.id, unit: msg.unit, ok: true, size: size });
        } else {
          postMessage({ type: 'log', level: 'error', text: '[Worker] opfsMountDisc FAILED type=' + mdType + ' unit=' + msg.unit });
          postMessage({ type: 'opfsMountResult', id: msg.id, unit: msg.unit, ok: false, size: 0 });
        }
      });
      break;
    }

    case 'opfsUnmountSMD':
    case 'opfsUnmountDisc': {
      var muType = msg.driveType || 0;
      postMessage({ type: 'log', level: 'info', text: '[Worker] opfsUnmountDisc type=' + muType + ' unit=' + msg.unit });
      opfsCloseUnit(muType, msg.unit);
      if (muType === 2) {
        Module._UnmountSCSI(msg.unit);
      } else {
        Module._UnmountSMD(msg.unit);
      }
      postMessage({ type: 'fsResult', id: msg.id, result: 0 });
      break;
    }

    // --- WebSocket bridge ---
    case 'ws-connect': {
      // Connect to gateway WebSocket (single endpoint for all traffic)
      wsConnect(msg.url);
      // Also init disk I/O sub-worker (connects to same gateway)
      initDiskWorker(msg.url);
      break;
    }

    case 'ws-disconnect': {
      wsDisconnect();
      closeDiskWorker();
      break;
    }

    // --- Gateway disk mount/unmount ---
    case 'gatewayMountSMD': {
      _diskGatewayDrives.smd[msg.unit] = true;
      var gmsResult = Module._MountSMDFromGateway(msg.unit, msg.imageSize);
      postMessage({ type: 'gatewayMountResult', id: msg.id, unit: msg.unit, driveType: 'smd', ok: gmsResult === 0 });
      break;
    }

    case 'gatewayUnmountSMD': {
      delete _diskGatewayDrives.smd[msg.unit];
      Module._UnmountSMD(msg.unit);
      postMessage({ type: 'fsResult', id: msg.id, result: 0 });
      break;
    }

    case 'gatewayMountFloppy': {
      _diskGatewayDrives.floppy[msg.unit] = true;
      var gmfResult = Module._MountFloppyFromGateway(msg.unit, msg.imageSize);
      postMessage({ type: 'gatewayMountResult', id: msg.id, unit: msg.unit, driveType: 'floppy', ok: gmfResult === 0 });
      break;
    }

    case 'gatewayUnmountFloppy': {
      delete _diskGatewayDrives.floppy[msg.unit];
      Module._UnmountFloppy(msg.unit);
      postMessage({ type: 'fsResult', id: msg.id, result: 0 });
      break;
    }

    case 'gatewayMountSCSI': {
      _diskGatewayDrives.scsi[msg.unit] = true;
      var gmscResult = Module._MountSCSIFromGateway(msg.unit, msg.imageSize);
      postMessage({ type: 'gatewayMountResult', id: msg.id, unit: msg.unit, driveType: 'scsi', ok: gmscResult === 0 });
      break;
    }

    case 'gatewayUnmountSCSI': {
      delete _diskGatewayDrives.scsi[msg.unit];
      Module._UnmountSCSI(msg.unit);
      postMessage({ type: 'fsResult', id: msg.id, result: 0 });
      break;
    }

    case 'getDriveInfo': {
      var driveJson = Module.UTF8ToString(Module._GetDriveInfo());
      var driveData;
      try { driveData = JSON.parse(driveJson); } catch(e) { driveData = []; }
      postMessage({ type: 'getDriveInfoResult', id: msg.id, data: driveData });
      break;
    }

    case 'gatewayReadFullImage': {
      if (!_diskWorker || !_diskReady) {
        postMessage({ type: 'full-image-data', id: msg.id, error: 'Disk worker not connected' });
        break;
      }
      _fullReadRequestId = msg.id;
      _diskWorker.postMessage({ type: 'read-full', driveType: msg.driveType,
                                unit: msg.unit, size: msg.size });
      break;
    }

    case 'enableRemoteTerminals': {
      Module._EnableRemoteTerminals();
      // The register list: the machine's own terminals plus the gateway-only ones
      buildGatewayTerminalList();
      // Send register message to gateway if WebSocket is connected
      if (_ws && _ws.readyState === 1 && _remoteTerminals.length > 0) {
        _ws.send(JSON.stringify({ type: 'register', terminals: _remoteTerminals }));
      }
      postMessage({
        type: 'enableRemoteTerminalsResult',
        id: msg.id,
        count: _remoteTerminals.length,
        terminals: _remoteTerminals
      });
      break;
    }

    default:
      postMessage({ type: 'log', level: 'warn', text: '[Worker] Unknown message type: ' + msg.type });
  }
};
