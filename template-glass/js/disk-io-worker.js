//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs -- https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// disk-io-worker.js - Disk I/O Sub-Worker
//
// Dedicated sub-worker that owns a WebSocket connection to the gateway.
// Communicates synchronously with the main Worker via SharedArrayBuffer +
// Atomics for blocking disk block read/write.
//
// SharedArrayBuffer layout (32 control + 64KB data = 65568 bytes):
//   Offset  Type         Purpose
//   0       Int32        control: 0=idle, 1=request pending, 2=response ready
//   4       Int32        driveType (0=SMD, 1=floppy)
//   8       Int32        unit (0-3)
//   12      Int32        offset (byte offset into image)
//   16      Int32        size (bytes to read/write)
//   20      Int32        response status (0=ok, -1=error)
//   24      Int32        response data length
//   28      Int32        isWrite (0=read, 1=write)
//   32+     Uint8[65536] data area (request data for writes, response data for reads)

'use strict';

var controlArray = null;  // Int32Array view (8 x Int32 = 32 bytes)
var dataArray = null;     // Uint8Array view at offset 32
var ws = null;
var connected = false;
var hasBlockIO = false;   // true if SharedArrayBuffer available for sync I/O

// Full-image read state (Copy to Library)
var _fullReadMode = false;
var _fullReadBuffer = null;
var _fullReadOffset = 0;
var _fullReadSize = 0;
var _fullReadDriveType = 0;
var _fullReadUnit = 0;
var _fullReadNextOffset = 0;
var FULL_READ_BLOCK_SIZE = 65535;

onmessage = function(e) {
  if (e.data.type === 'init') {
    if (e.data.sharedBuffer) {
      controlArray = new Int32Array(e.data.sharedBuffer, 0, 8);
      dataArray = new Uint8Array(e.data.sharedBuffer, 32);
      hasBlockIO = true;
    }
    connectWebSocket(e.data.wsUrl);
  }
  else if (e.data.type === 'read-full') {
    _fullReadMode = true;
    _fullReadDriveType = e.data.driveType;
    _fullReadUnit = e.data.unit;
    _fullReadSize = e.data.size;
    _fullReadBuffer = new Uint8Array(e.data.size);
    _fullReadOffset = 0;
    _fullReadNextOffset = 0;
    sendNextFullReadBlock();
  }
  else if (e.data.type === 'close') {
    if (ws) {
      try { ws.close(); } catch(err) {}
    }
  }
};

function sendNextFullReadBlock() {
  if (!ws || ws.readyState !== 1) {
    _fullReadMode = false;
    _fullReadBuffer = null;
    postMessage({ type: 'full-image-data', driveType: _fullReadDriveType, unit: _fullReadUnit, error: 'WebSocket not connected' });
    return;
  }
  var remaining = _fullReadSize - _fullReadNextOffset;
  var blockSize = Math.min(FULL_READ_BLOCK_SIZE, remaining);
  var frame = new Uint8Array(9);
  frame[0] = 0x20;
  frame[1] = _fullReadDriveType;
  frame[2] = _fullReadUnit;
  frame[3] = (_fullReadNextOffset >>> 24) & 0xFF;
  frame[4] = (_fullReadNextOffset >>> 16) & 0xFF;
  frame[5] = (_fullReadNextOffset >>> 8) & 0xFF;
  frame[6] = _fullReadNextOffset & 0xFF;
  frame[7] = (blockSize >>> 8) & 0xFF;
  frame[8] = blockSize & 0xFF;
  ws.send(frame.buffer);
  _fullReadNextOffset += blockSize;
}

function connectWebSocket(url) {
  try {
    ws = new WebSocket(url);
  } catch(err) {
    postMessage({ type: 'error', error: 'Failed to create WebSocket: ' + err.message });
    return;
  }

  ws.binaryType = 'arraybuffer';

  ws.onopen = function() {
    connected = true;
    postMessage({ type: 'connected' });
    if (hasBlockIO) {
      waitForRequest();
    }
  };

  ws.onmessage = function(ev) {
    if (typeof ev.data === 'string') {
      // JSON text frame: disk-list
      try {
        postMessage({ type: 'disk-list', data: JSON.parse(ev.data) });
      } catch(e) {}
      return;
    }

    var buf = new Uint8Array(ev.data);
    if (buf.length < 3) return;

    // Full-image read mode: accumulate blocks into local buffer
    if (_fullReadMode && buf[0] === 0x21) {
      var blockData = buf.subarray(3);
      _fullReadBuffer.set(blockData, _fullReadOffset);
      _fullReadOffset += blockData.length;
      // Progress every 64 blocks (~4MB at 65535 bytes/block)
      var blockNum = Math.floor(_fullReadOffset / FULL_READ_BLOCK_SIZE);
      if (blockNum % 64 === 0 || _fullReadOffset >= _fullReadSize) {
        postMessage({ type: 'full-image-progress', done: _fullReadOffset, total: _fullReadSize });
      }
      if (_fullReadOffset >= _fullReadSize) {
        _fullReadMode = false;
        var result = _fullReadBuffer.buffer.slice(0, _fullReadSize);
        _fullReadBuffer = null;
        postMessage({ type: 'full-image-data',
                      driveType: _fullReadDriveType, unit: _fullReadUnit,
                      buffer: result }, [result]);
        if (hasBlockIO) waitForRequest();  // resume normal sync loop
      } else {
        sendNextFullReadBlock();
      }
      return;
    }

    // Binary frame (only relevant if block I/O is active)
    if (!hasBlockIO) return;

    if (buf[0] === 0x21) {
      // Block read response: [0x21][driveType][unit][data...]
      var data = buf.subarray(3);
      if (data.length > dataArray.length) {
        // Data exceeds shared buffer - signal error
        controlArray[5] = -1;
        controlArray[6] = 0;
        postMessage({ type: 'error', error: 'Read response too large: ' + data.length + ' > ' + dataArray.length });
      } else {
        dataArray.set(data, 0);
        controlArray[5] = 0;            // status = ok
        controlArray[6] = data.length;  // data length
      }
      Atomics.store(controlArray, 0, 2);  // response ready
      Atomics.notify(controlArray, 0);
    }
    else if (buf[0] === 0x23) {
      // Block write ack: [0x23][driveType][unit][status]
      controlArray[5] = (buf[3] === 0x00) ? 0 : -1;  // status
      controlArray[6] = 0;
      Atomics.store(controlArray, 0, 2);  // response ready
      Atomics.notify(controlArray, 0);
    }
  };

  ws.onclose = function() {
    connected = false;
    postMessage({ type: 'disconnected' });
    // If there's a pending block I/O request, signal error
    if (hasBlockIO && Atomics.load(controlArray, 0) === 1) {
      controlArray[5] = -1;  // error
      controlArray[6] = 0;
      Atomics.store(controlArray, 0, 2);
      Atomics.notify(controlArray, 0);
    }
  };

  ws.onerror = function() {
    postMessage({ type: 'error', error: 'WebSocket error' });
  };
}

function waitForRequest() {
  // Wait asynchronously for control[0] to become 1 (request pending)
  var result = Atomics.waitAsync(controlArray, 0, 0);
  if (result.async) {
    result.value.then(function() {
      processIfRequest();
    });
  } else {
    // Already not 0 - check immediately
    processIfRequest();
  }
}

function processIfRequest() {
  var state = Atomics.load(controlArray, 0);
  if (state === 1) {
    handleRequest();
  } else if (state === 0) {
    // Spurious wakeup, wait again
    waitForRequest();
  } else {
    // Unexpected state - yield to avoid sync recursion, then retry
    setTimeout(waitForRequest, 0);
  }
}

function handleRequest() {
  var driveType = controlArray[1];
  var unit = controlArray[2];
  var offset = controlArray[3];
  var size = controlArray[4];
  var isWrite = controlArray[7];

  if (!ws || ws.readyState !== 1) {
    // WebSocket not connected - error
    controlArray[5] = -1;
    controlArray[6] = 0;
    Atomics.store(controlArray, 0, 2);
    Atomics.notify(controlArray, 0);
    waitForNextIdle();
    return;
  }

  if (isWrite) {
    // Build write frame: [0x22][driveType][unit][offset:4BE][size:2BE][data...]
    var frame = new Uint8Array(9 + size);
    frame[0] = 0x22;
    frame[1] = driveType;
    frame[2] = unit;
    frame[3] = (offset >>> 24) & 0xFF;
    frame[4] = (offset >>> 16) & 0xFF;
    frame[5] = (offset >>> 8) & 0xFF;
    frame[6] = offset & 0xFF;
    frame[7] = (size >>> 8) & 0xFF;
    frame[8] = size & 0xFF;
    frame.set(dataArray.subarray(0, size), 9);
    ws.send(frame.buffer);
  } else {
    // Build read frame: [0x20][driveType][unit][offset:4BE][size:2BE]
    var frame = new Uint8Array(9);
    frame[0] = 0x20;
    frame[1] = driveType;
    frame[2] = unit;
    frame[3] = (offset >>> 24) & 0xFF;
    frame[4] = (offset >>> 16) & 0xFF;
    frame[5] = (offset >>> 8) & 0xFF;
    frame[6] = offset & 0xFF;
    frame[7] = (size >>> 8) & 0xFF;
    frame[8] = size & 0xFF;
    ws.send(frame.buffer);
  }

  // Wait for ws.onmessage to deliver the response (sets control[0] from 1 to 2)
  waitForResponse();
}

function waitForResponse() {
  // control[0] is 1 (request pending). Wait for it to change (ws.onmessage sets it to 2).
  var result = Atomics.waitAsync(controlArray, 0, 1);
  if (result.async) {
    result.value.then(function() {
      waitForNextIdle();
    });
  } else {
    // Already changed from 1 (response arrived very fast)
    waitForNextIdle();
  }
}

function waitForNextIdle() {
  // control[0] should be 2 (response ready). Wait for main Worker to reset to 0.
  // Race condition: parent may reset to 0 AND set to 1 (new request) before we run.
  // If we see state=1 here, it's a new request - handle it directly.
  var state = Atomics.load(controlArray, 0);
  if (state === 0) {
    // Already idle - wait for next request
    waitForRequest();
    return;
  }
  if (state === 1) {
    // Parent already queued a new request (raced past idle state).
    // Handle it directly to avoid deadlock.
    setTimeout(handleRequest, 0);
    return;
  }
  // state === 2: response still pending, wait for parent to reset to 0
  var result = Atomics.waitAsync(controlArray, 0, state);
  if (result.async) {
    result.value.then(function() {
      // Re-check state - could be 0 (idle) or 1 (new request already queued)
      var newState = Atomics.load(controlArray, 0);
      if (newState === 1) {
        handleRequest();
      } else {
        waitForRequest();
      }
    });
  } else {
    // Already changed - re-check
    var newState = Atomics.load(controlArray, 0);
    if (newState === 1) {
      setTimeout(handleRequest, 0);
    } else {
      setTimeout(waitForRequest, 0);
    }
  }
}
