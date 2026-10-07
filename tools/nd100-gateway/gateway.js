#!/usr/bin/env node
//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs -- https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//
// gateway.js - ND-100/CX Gateway Server
//
// Bridges TCP terminal/HDLC connections and disk block I/O to the ND-100
// WASM emulator running in a browser Web Worker via WebSocket.
//
// Architecture:
//   Remote client (PuTTY/telnet) --TCP--> Gateway --WebSocket--> Worker
//   Disk I/O sub-worker --WebSocket--> Gateway --fs.readSync/writeSync
//   HDLC TCP client --TCP--> Gateway --WebSocket--> Worker
//
// Single WebSocket server, all message types multiplexed by frame type byte.
// Emulator worker and disk sub-worker both connect to the same endpoint.
//
// Usage: node gateway.js [--config path] [--static dir] [--verbose]

'use strict';

const http = require('http');
const net = require('net');
const fs = require('fs');
const path = require('path');
const { WebSocketServer } = require('ws');

// =========================================================
// Configuration
// =========================================================
const args = process.argv.slice(2);
let configPath = path.join(__dirname, 'gateway.conf.json');
let verbose = false;
let staticDirOverride = null;

for (let i = 0; i < args.length; i++) {
  if (args[i] === '--config' && args[i + 1]) {
    configPath = args[++i];
  } else if (args[i] === '--static' && args[i + 1]) {
    staticDirOverride = args[++i];
  } else if (args[i] === '--verbose' || args[i] === '-v') {
    verbose = true;
  } else if (args[i] === '--help' || args[i] === '-h') {
    console.log('Usage: node gateway.js [--config path] [--static dir] [--verbose]');
    console.log('');
    console.log('Options:');
    console.log('  --config path  Path to config file (default: gateway.conf.json)');
    console.log('  --static dir   Serve static files from directory (with COOP/COEP headers)');
    console.log('  --verbose, -v  Enable verbose logging');
    console.log('  --help, -h     Show this help');
    process.exit(0);
  }
}

let config;
try {
  config = JSON.parse(fs.readFileSync(configPath, 'utf8'));
} catch (err) {
  console.error('Failed to read config file:', configPath);
  console.error(err.message);
  process.exit(1);
}

// CLI --static overrides config
// CLI path resolves relative to CWD, config path resolves relative to config file
var staticDir = '';
if (staticDirOverride) {
  staticDir = path.resolve(process.cwd(), staticDirOverride);
} else if (config.staticDir) {
  staticDir = path.resolve(path.dirname(configPath), config.staticDir);
}

function log(...args) {
  const ts = new Date().toISOString().slice(11, 19);
  console.log(`[${ts}]`, ...args);
}

function vlog(...args) {
  if (verbose) log(...args);
}

// =========================================================
// MIME types for static file serving
// =========================================================
var MIME_TYPES = {
  '.html': 'text/html',
  '.css':  'text/css',
  '.js':   'application/javascript',
  '.json': 'application/json',
  '.wasm': 'application/wasm',
  '.png':  'image/png',
  '.ico':  'image/x-icon',
  '.jpg':  'image/jpeg',
  '.gif':  'image/gif',
  '.svg':  'image/svg+xml',
  '.txt':  'text/plain',
  '.map':  'application/json',
  '.img':  'application/octet-stream',
  '.IMG':  'application/octet-stream'
};

// =========================================================
// State
// =========================================================
let emulatorWs = null;           // Emulator WebSocket (terminal + HDLC + control)
let diskWs = null;               // Disk I/O sub-worker WebSocket
let registeredTerminals = [];    // Terminal list from emulator { identCode, name, logicalDevice }
const tcpBindings = new Map();   // identCode -> { socket, clientAddr }
const menuClients = new Set();   // TCP sockets in menu-selection mode

// HDLC state
const hdlcBindings = new Map();  // channel -> { socket, clientAddr }
const hdlcServers = [];          // Active HDLC TCP servers

// Ethernet state.
//
// NOTE THE SHAPE, AND HOW IT DIFFERS FROM HDLC. hdlcBindings is
// channel -> ONE socket, because HDLC is a point-to-point line. Ethernet is
// MULTIPOINT: a segment has any number of members and a frame from one goes to
// every OTHER one. So this is segment -> Set of members, and the handler
// repeats. Copying the HDLC one-socket shape would give a link that works
// perfectly with exactly two machines and silently drops the third - and two
// machines is the first thing anybody tests.
//
// A member is { socket, clientAddr, rx } where rx is the partial-frame buffer.
// The emulator's own WebSocket is NOT in this set; it is handled separately,
// because it is reached through emulatorWs rather than a TCP socket.
const ethBindings = new Map();   // segment -> Set({ socket, clientAddr, rx })
const ethServers = [];           // Active ethernet TCP servers

// RETH wire format, as spoken by RetroCore's TcpEthernetBackend/TcpEthernetRelay
// and by nd500x's ND500X_ETH_UPLINK=tcp:host:port. Reading it here means a
// browser NDIX and a NATIVE nd500x can share one segment with no adapter.
//   handshake  "RETH" + version byte   both sides write, then read
//   frames     [u16 BIG-ENDIAN length][bytes], 0 < len <= 2048
const RETH_MAGIC = Buffer.from('RETH', 'ascii');
const RETH_VERSION_MEMBER = 1;   // an ordinary member with one NIC
const RETH_HANDSHAKE_LEN = 5;
const RETH_MAX_FRAME = 2048;

// Disk image state
const diskFds = { smd: {}, floppy: {}, scsi: {}, winchester: {} };  // [type][unit] -> { fd, path, size, name }

// driveType byte (wire protocol) -> config/name. Index matches the C DRIVE_TYPE
// enum: 0=SMD, 1=floppy, 2=SCSI, 3=Winchester. An unknown driveType falls back
// to 'smd' so older clients that only ever send 0/1 stay fully compatible.
const DRIVE_TYPE_NAMES = ['smd', 'floppy', 'scsi', 'winchester'];
function driveTypeName(driveType) {
  return DRIVE_TYPE_NAMES[driveType] || 'smd';
}

// =========================================================
// Disk image file management
// =========================================================
function openDiskImages() {
  ['smd', 'floppy', 'scsi', 'winchester'].forEach(function(type) {
    var imgConfig = config[type];
    if (!imgConfig || !imgConfig.images) return;
    imgConfig.images.forEach(function(img, unit) {
      if (!img) return;
      // Support both string paths and object entries { path, name, description }
      var imgPath = (typeof img === 'string') ? img : img.path;
      if (!imgPath) return;
      var resolved = path.resolve(path.dirname(configPath), imgPath);
      try {
        var fd = fs.openSync(resolved, 'r+');
        var stat = fs.fstatSync(fd);
        diskFds[type][unit] = {
          fd: fd,
          path: resolved,
          size: stat.size,
          name: (typeof img === 'object' && img.name) ? img.name : path.basename(resolved)
        };
      } catch (err) {
        log('WARNING: Failed to open', type.toUpperCase(), 'unit', unit + ':', resolved, '-', err.message);
      }
    });
  });
}

function closeDiskImages() {
  DRIVE_TYPE_NAMES.forEach(function(type) {
    for (var unit in diskFds[type]) {
      try { fs.closeSync(diskFds[type][unit].fd); } catch(e) {}
    }
    diskFds[type] = {};
  });
}

// =========================================================
// Disk listing JSON (sent to disk sub-worker on connect)
// =========================================================
function buildDiskList() {
  var list = { type: 'disk-list' };
  DRIVE_TYPE_NAMES.forEach(function(type) {
    list[type] = Object.entries(diskFds[type]).map(function(e) {
      return { unit: parseInt(e[0]), name: e[1].name, size: e[1].size };
    });
  });
  return JSON.stringify(list);
}

// =========================================================
// HTTP Server (static files with COOP/COEP headers)
// =========================================================
function handleHttpRequest(req, res) {
  // COOP/COEP headers for SharedArrayBuffer support
  res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
  res.setHeader('Cross-Origin-Embedder-Policy', 'credentialless');

  // Parse URL, strip query string
  var urlPath = req.url.split('?')[0];

  // Dynamic SMD catalog endpoint (generated from gateway.conf.json)
  if (urlPath === '/smd-catalog.json') {
    var catalog = [];
    var images = (config.smd && config.smd.images) ? config.smd.images : [];
    images.forEach(function(img, idx) {
      var imgPath = (typeof img === 'string') ? img : img.path;
      var imgName = (typeof img === 'object' && img.name)
        ? img.name : path.basename(imgPath, path.extname(imgPath));
      var imgDesc = (typeof img === 'object' && img.description) ? img.description : '';
      var absPath = path.resolve(path.dirname(configPath), imgPath);
      var stat = null;
      try { stat = fs.statSync(absPath); } catch(e) {}
      catalog.push({
        name: imgName,
        description: imgDesc,
        size: stat ? stat.size : 0,
        url: 'smd-images/' + idx,
        available: stat !== null
      });
    });
    var body = JSON.stringify(catalog);
    res.writeHead(200, {
      'Content-Type': 'application/json',
      'Content-Length': Buffer.byteLength(body)
    });
    res.end(body);
    return;
  }

  // SMD image streaming endpoint
  var smdMatch = /^\/smd-images\/(\d+)$/.exec(urlPath);
  if (smdMatch) {
    var smdIdx = parseInt(smdMatch[1], 10);
    var smdImages = (config.smd && config.smd.images) ? config.smd.images : [];
    if (smdIdx < 0 || smdIdx >= smdImages.length) {
      res.writeHead(404); res.end('Not Found'); return;
    }
    var smdImgPath = (typeof smdImages[smdIdx] === 'string') ? smdImages[smdIdx] : smdImages[smdIdx].path;
    var smdAbsPath = path.resolve(path.dirname(configPath), smdImgPath);
    fs.stat(smdAbsPath, function(err, stat) {
      if (err || !stat.isFile()) { res.writeHead(404); res.end('Not Found'); return; }
      res.writeHead(200, {
        'Content-Type': 'application/octet-stream',
        'Content-Length': stat.size
      });
      fs.createReadStream(smdAbsPath).pipe(res);
    });
    return;
  }

  if (!staticDir) {
    res.writeHead(404);
    res.end('Not Found (no static directory configured)');
    return;
  }

  if (urlPath === '/') urlPath = '/index.html';

  // Security: prevent path traversal
  var safePath = path.normalize(urlPath).replace(/^(\.\.[\/\\])+/, '');
  var filePath = path.join(staticDir, safePath);

  // Ensure we're still within staticDir
  if (!filePath.startsWith(staticDir)) {
    res.writeHead(403);
    res.end('Forbidden');
    return;
  }

  fs.stat(filePath, function(err, stat) {
    if (err || !stat.isFile()) {
      res.writeHead(404);
      res.end('Not Found');
      return;
    }

    var ext = path.extname(filePath);
    var contentType = MIME_TYPES[ext] || 'application/octet-stream';
    res.writeHead(200, { 'Content-Type': contentType, 'Content-Length': stat.size });
    var stream = fs.createReadStream(filePath);
    stream.pipe(res);
    stream.on('error', function() {
      res.writeHead(500);
      res.end('Internal Server Error');
    });
  });
}

const wsPort = config.websocket.port || 8765;
const server = http.createServer(handleHttpRequest);

// =========================================================
// Single WebSocket Server (all connections)
// =========================================================
const wss = new WebSocketServer({ server: server });

wss.on('connection', function(ws, req) {
  const clientIp = req.socket.remoteAddress;

  // Determine connection role based on state:
  // - If no emulator connected yet, this is the emulator
  // - If emulator is connected but no disk client, this is the disk sub-worker
  // - Otherwise reject
  if (!emulatorWs) {
    emulatorWs = ws;
    log('Emulator connected from', clientIp);
    setupEmulatorWs(ws);
  } else if (!diskWs) {
    diskWs = ws;
    log('Disk I/O client connected from', clientIp);
    setupDiskWs(ws);
  } else {
    log('Rejecting additional WebSocket connection from', clientIp);
    ws.close(4000, 'Only one emulator + one disk connection allowed');
  }
});

// =========================================================
// Emulator WebSocket handler (terminal + HDLC + control)
// =========================================================
function setupEmulatorWs(ws) {
  ws.on('message', function(data, isBinary) {
    if (isBinary) {
      var buf = Buffer.isBuffer(data) ? data : Buffer.from(data);
      if (buf.length < 2) return;

      if (buf[0] === 0x02) {
        // term-output (binary): [0x02][identCode][data...]
        var identCode = buf[1];
        var binding = tcpBindings.get(identCode);
        if (binding && binding.socket && !binding.socket.destroyed) {
          binding.socket.write(buf.slice(2));
        }
      }
      else if (buf[0] === 0x31 && buf.length >= 4) {
        // Ethernet TX frame from emulator: [0x31][segment][lenHi][lenLo][data...]
        // Repeat to every TCP member of the segment. There is no "other
        // emulator" to send to - one gateway serves one emulatorWs.
        var ethSeg = buf[1];
        var ethLen = (buf[2] << 8) | buf[3];
        if (ethLen > 0 && ethLen <= RETH_MAX_FRAME && buf.length >= 4 + ethLen) {
          ethRepeat(ethSeg, buf.slice(4, 4 + ethLen), null);
          vlog('ETH TX seg=' + ethSeg + ' len=' + ethLen);
        } else {
          // Say so rather than dropping quietly: a length that does not match
          // the message is a framing bug on the emulator side, and a silent
          // drop there looks exactly like a dead network.
          log('ETH TX seg=' + ethSeg + ' bad length ' + ethLen +
              ' for a ' + buf.length + '-byte message - dropped');
        }
      }
      else if (buf[0] === 0x11 && buf.length >= 4) {
        // HDLC TX frame from emulator: [0x11][channel][lenHi][lenLo][data...]
        var channel = buf[1];
        var len = (buf[2] << 8) | buf[3];
        var frameData = buf.slice(4, 4 + len);
        var hdlcBinding = hdlcBindings.get(channel);
        if (hdlcBinding && hdlcBinding.socket && !hdlcBinding.socket.destroyed) {
          hdlcBinding.socket.write(frameData);
          vlog('HDLC TX ch=' + channel + ' len=' + len + ' -> TCP ' + Array.from(frameData.slice(0, 20)).map(b => b.toString(16).padStart(2,'0')).join(' '));
        }
      }
      return;
    }

    // JSON text frame (control messages)
    let msg;
    try {
      msg = JSON.parse(data);
    } catch (err) {
      vlog('Invalid JSON from emulator:', data.toString().slice(0, 100));
      return;
    }

    switch (msg.type) {

      case 'register': {
        registeredTerminals = msg.terminals || [];
        log('Registered', registeredTerminals.length, 'terminal(s):');
        for (const t of registeredTerminals) {
          log('  identCode=' + t.identCode, t.name,
              '(logical device ' + t.logicalDevice + ')');
        }
        break;
      }

      case 'carrier': {
        vlog('Carrier', msg.missing ? 'missing' : 'present',
             'on identCode', msg.identCode);
        break;
      }

      default:
        vlog('Unknown emulator message type:', msg.type);
    }
  });

  ws.on('close', function(code, reason) {
    log('Emulator disconnected (code=' + code + ')');
    emulatorWs = null;
    registeredTerminals = [];

    // Disconnect all TCP clients gracefully
    for (const [identCode, binding] of tcpBindings) {
      if (binding.socket && !binding.socket.destroyed) {
        binding.socket.write('\r\nEmulator disconnected.\r\n');
        binding.socket.destroy();
      }
    }
    tcpBindings.clear();

    // Notify HDLC TCP clients
    for (const [channel, binding] of hdlcBindings) {
      if (binding.socket && !binding.socket.destroyed) {
        binding.socket.destroy();
      }
    }
    hdlcBindings.clear();

    // Also close disk sub-worker (it belongs to this emulator session)
    if (diskWs) {
      diskWs.close(1000, 'Emulator disconnected');
    }
  });

  ws.on('error', function(err) {
    log('Emulator WebSocket error:', err.message);
  });
}

// =========================================================
// Disk I/O WebSocket handler (block read/write)
// =========================================================
var _gwDiskReadCount = 0;
var _gwDiskWriteCount = 0;
var _gwDiskReadBytes = 0;
var _gwDiskWriteBytes = 0;

function setupDiskWs(ws) {
  // Send disk listing on connect
  ws.send(buildDiskList());

  ws.on('message', function(data, isBinary) {
    if (!isBinary) return;
    var buf = Buffer.isBuffer(data) ? data : Buffer.from(data);
    if (buf.length < 3) return;

    var type = buf[0];
    var driveType = buf[1];  // 0=smd, 1=floppy, 2=scsi, 3=winchester
    var unit = buf[2];
    var typeName = driveTypeName(driveType);
    var disk = diskFds[typeName] && diskFds[typeName][unit];

    if ((type === 0x20 || type === 0x22) && !disk) {
      log('Disk ERROR: no image for', typeName, 'unit', unit, '- request type 0x' + type.toString(16), 'dropped');
      // Send error response so the sub-worker doesn't hang
      if (type === 0x20) {
        var errResp = Buffer.alloc(4);
        errResp[0] = 0x21; errResp[1] = driveType; errResp[2] = unit; errResp[3] = 0xFF;
        ws.send(errResp);
      } else {
        ws.send(Buffer.from([0x23, driveType, unit, 0xFF]));
      }
      return;
    }

    if (type === 0x20 && buf.length >= 9 && disk) {
      // Block read request: [0x20][driveType][unit][offset:4BE][size:2BE]
      var offset = buf.readUInt32BE(3);
      var size = buf.readUInt16BE(7);
      var readBuf = Buffer.alloc(size);
      try {
        fs.readSync(disk.fd, readBuf, 0, size, offset);
      } catch (err) {
        log('Disk read error:', typeName, 'unit', unit, 'offset', offset, '-', err.message);
        var errResp = Buffer.alloc(4);
        errResp[0] = 0x21;
        errResp[1] = driveType;
        errResp[2] = unit;
        errResp[3] = 0xFF;  // error flag
        ws.send(errResp);
        return;
      }
      // Response: [0x21][driveType][unit][data...]
      var resp = Buffer.alloc(3 + size);
      resp[0] = 0x21;
      resp[1] = driveType;
      resp[2] = unit;
      readBuf.copy(resp, 3);
      ws.send(resp);
      _gwDiskReadCount++;
      _gwDiskReadBytes += size;
      if (_gwDiskReadCount === 1) {
        log('Disk read: first block -', typeName, 'unit', unit, 'offset', offset, 'size', size, '- sent', resp.length, 'bytes');
      } else if (_gwDiskReadCount % 100 === 0) {
        log('Disk I/O: ' + _gwDiskReadCount + ' reads (' + (_gwDiskReadBytes / 1024).toFixed(0) + ' KB), ' + _gwDiskWriteCount + ' writes (' + (_gwDiskWriteBytes / 1024).toFixed(0) + ' KB)');
      }
      vlog('Disk read:', typeName, 'unit', unit, 'offset', offset, 'size', size);
    }

    else if (type === 0x22 && buf.length >= 9 && disk) {
      // Block write request: [0x22][driveType][unit][offset:4BE][size:2BE][data...]
      var offset = buf.readUInt32BE(3);
      var size = buf.readUInt16BE(7);
      var writeData = buf.slice(9, 9 + size);
      var status = 0x00;
      try {
        fs.writeSync(disk.fd, writeData, 0, writeData.length, offset);
      } catch (err) {
        log('Disk write error:', typeName, 'unit', unit, 'offset', offset, '-', err.message);
        status = 0xFF;
      }
      // Ack: [0x23][driveType][unit][status]
      ws.send(Buffer.from([0x23, driveType, unit, status]));
      _gwDiskWriteCount++;
      _gwDiskWriteBytes += size;
      if (_gwDiskWriteCount === 1) {
        log('Disk write: first block -', typeName, 'unit', unit, 'offset', offset, 'size', size);
      } else if (_gwDiskWriteCount % 100 === 0) {
        log('Disk I/O: ' + _gwDiskReadCount + ' reads (' + (_gwDiskReadBytes / 1024).toFixed(0) + ' KB), ' + _gwDiskWriteCount + ' writes (' + (_gwDiskWriteBytes / 1024).toFixed(0) + ' KB)');
      }
      vlog('Disk write:', typeName, 'unit', unit, 'offset', offset, 'size', size, 'status', status);
    }
  });

  ws.on('close', function() {
    log('Disk I/O client disconnected');
    diskWs = null;
  });

  ws.on('error', function(err) {
    log('Disk WebSocket error:', err.message);
  });
}

// =========================================================
// Helper: send JSON control message to emulator
// =========================================================
function sendToEmulator(msg) {
  if (emulatorWs && emulatorWs.readyState === 1) {
    emulatorWs.send(JSON.stringify(msg));
    return true;
  }
  return false;
}

// =========================================================
// Helper: send binary term-input to emulator
// Format: [0x01][identCode][data bytes...]
// =========================================================
function sendTermInput(identCode, bytes) {
  if (emulatorWs && emulatorWs.readyState === 1) {
    var frame = Buffer.alloc(2 + bytes.length);
    frame[0] = 0x01;  // term-input
    frame[1] = identCode & 0xFF;
    for (var i = 0; i < bytes.length; i++) {
      frame[2 + i] = bytes[i];
    }
    emulatorWs.send(frame);
    return true;
  }
  return false;
}

// =========================================================
// Helper: send binary HDLC RX frame to emulator
// Format: [0x10][channel][lenHi][lenLo][data...]
// =========================================================
function sendHdlcRxFrame(channel, data) {
  if (emulatorWs && emulatorWs.readyState === 1) {
    var frame = Buffer.alloc(4 + data.length);
    frame[0] = 0x10;  // HDLC RX
    frame[1] = channel & 0xFF;
    frame[2] = (data.length >> 8) & 0xFF;
    frame[3] = data.length & 0xFF;
    data.copy(frame, 4);
    emulatorWs.send(frame);
    return true;
  }
  return false;
}

// =========================================================
// Helper: send an ethernet frame UP to the emulator
// Format: [0x30][segment][lenHi][lenLo][data...]
// =========================================================
function sendEthRxFrame(segment, data) {
  if (emulatorWs && emulatorWs.readyState === 1) {
    var frame = Buffer.alloc(4 + data.length);
    frame[0] = 0x30;  // ethernet RX
    frame[1] = segment & 0xFF;
    frame[2] = (data.length >> 8) & 0xFF;
    frame[3] = data.length & 0xFF;
    data.copy(frame, 4);
    emulatorWs.send(frame);
    return true;
  }
  return false;
}

// =========================================================
// Helper: tell the emulator whether anything else is on its segment
// Format: [0x32][segment][present]
// =========================================================
function sendEthLink(segment, present) {
  if (emulatorWs && emulatorWs.readyState === 1) {
    emulatorWs.send(Buffer.from([0x32, segment & 0xFF, present ? 0x01 : 0x00]));
    return true;
  }
  return false;
}

// =========================================================
// Repeat one frame to every member of a segment EXCEPT its sender.
//
// `from` is the member that sent it, or null when the emulator did. Never echo
// a frame back to whoever sent it: NDIX would see its own ARP request arrive
// and drop it as "from me" (if_ether.c:286) - harmless, but it doubles every
// packet count and reads exactly like a duplicate-address fault.
// =========================================================
function ethRepeat(segment, frameData, from) {
  var members = ethBindings.get(segment);
  if (members) {
    // The RETH length prefix is built once and shared - every TCP member wants
    // the identical bytes.
    var out = Buffer.alloc(2 + frameData.length);
    out[0] = (frameData.length >> 8) & 0xFF;
    out[1] = frameData.length & 0xFF;
    frameData.copy(out, 2);
    for (const m of members) {
      if (m === from) continue;
      if (m.socket && !m.socket.destroyed) m.socket.write(out);
    }
  }
  // ...and up to the emulator, unless the emulator is where it came from.
  if (from !== null) sendEthRxFrame(segment, frameData);
}

// =========================================================
// Helper: send HDLC carrier status to emulator
// Format: [0x12][channel][present]
// =========================================================
function sendHdlcCarrier(channel, present) {
  if (emulatorWs && emulatorWs.readyState === 1) {
    emulatorWs.send(Buffer.from([0x12, channel & 0xFF, present ? 0x01 : 0x00]));
    return true;
  }
  return false;
}

// =========================================================
// Helper: build terminal menu text
// =========================================================
function buildMenu() {
  let text = '\r\n';
  text += '  ' + (config.terminals.welcome || 'ND-100/CX Terminal Server') + '\r\n';
  text += '  ' + '='.repeat((config.terminals.welcome || 'ND-100/CX Terminal Server').length) + '\r\n';
  text += '\r\n';

  if (registeredTerminals.length === 0) {
    text += '  No terminals available. Is the emulator connected?\r\n\r\n';
    text += '  Press Enter to refresh, or Ctrl-C to disconnect.\r\n';
    return text;
  }

  text += '  Available terminals:\r\n\r\n';
  for (let i = 0; i < registeredTerminals.length; i++) {
    const t = registeredTerminals[i];
    const inUse = tcpBindings.has(t.identCode);
    const num = String(i + 1).padStart(2, ' ');
    const status = inUse ? ' [in use]' : '';
    text += '  ' + num + '. ' + t.name + status + '\r\n';
  }
  text += '\r\n   0. Disconnect\r\n';
  text += '\r\n  Select terminal (1-' + registeredTerminals.length + ', 0=quit): ';
  return text;
}

// =========================================================
// TCP Server (remote terminal clients)
// =========================================================
const tcpPort = config.terminals.port || 5001;
const tcpServer = net.createServer(function(socket) {
  const clientAddr = socket.remoteAddress + ':' + socket.remotePort;
  log('TCP client connected from', clientAddr);

  // Start in menu mode
  let menuMode = true;
  let inputBuf = '';
  let boundIdentCode = null;

  menuClients.add(socket);

  // Send banner + menu
  socket.write(buildMenu());

  socket.on('data', function(data) {
    if (menuMode) {
      // Accumulate input, process on CR or LF
      inputBuf += data.toString('ascii');

      // Check for line completion
      const lineEnd = inputBuf.indexOf('\r');
      const lineEnd2 = inputBuf.indexOf('\n');
      const endPos = (lineEnd >= 0 && lineEnd2 >= 0) ? Math.min(lineEnd, lineEnd2)
                   : (lineEnd >= 0) ? lineEnd : lineEnd2;

      if (endPos < 0) return;  // Wait for more data

      const line = inputBuf.slice(0, endPos).trim();
      inputBuf = '';

      if (line === '') {
        // Refresh menu
        socket.write(buildMenu());
        return;
      }

      const choice = parseInt(line, 10);
      if (choice === 0) {
        socket.write('\r\n  Goodbye.\r\n');
        socket.end();
        return;
      }
      if (isNaN(choice) || choice < 1 || choice > registeredTerminals.length) {
        socket.write('\r\n  Invalid selection. Try again.\r\n');
        socket.write(buildMenu());
        return;
      }

      const terminal = registeredTerminals[choice - 1];

      // Check if already in use
      if (tcpBindings.has(terminal.identCode)) {
        socket.write('\r\n  ' + terminal.name + ' is already in use.\r\n');
        socket.write(buildMenu());
        return;
      }

      // Check emulator is connected
      if (!emulatorWs) {
        socket.write('\r\n  Emulator not connected.\r\n');
        socket.write(buildMenu());
        return;
      }

      // Bind TCP socket to this terminal
      boundIdentCode = terminal.identCode;
      tcpBindings.set(boundIdentCode, { socket: socket, clientAddr: clientAddr });
      menuMode = false;
      menuClients.delete(socket);

      log('Client', clientAddr, 'connected to', terminal.name,
          '(identCode=' + boundIdentCode + ')');

      socket.write('\r\n  Connected to ' + terminal.name + '\r\n\r\n');

      // Send telnet negotiation: character mode, no local echo
      // IAC WILL ECHO (I'll echo, you don't), IAC WILL SGA, IAC DO SGA
      socket.write(Buffer.from([
        255, 251, 1,   // IAC WILL ECHO
        255, 251, 3,   // IAC WILL SUPPRESS-GO-AHEAD
        255, 253, 3    // IAC DO SUPPRESS-GO-AHEAD
      ]));

      // Notify emulator
      sendToEmulator({
        type: 'client-connected',
        identCode: boundIdentCode,
        clientAddr: clientAddr
      });

    } else {
      // Connected mode: forward raw bytes as term-input
      // Strip telnet IAC sequences (0xFF followed by command + option bytes)
      const raw = Array.from(data);
      const bytes = [];
      for (let i = 0; i < raw.length; i++) {
        if (raw[i] === 255 && i + 2 < raw.length) {
          // IAC + command + option: skip 3 bytes
          vlog('Telnet IAC:', raw[i], raw[i+1], raw[i+2]);
          i += 2;
        } else {
          bytes.push(raw[i]);
        }
      }
      if (bytes.length > 0) {
        vlog('TCP->WS term-input identCode=' + boundIdentCode +
            ' bytes=[' + bytes.map(b => '0x' + b.toString(16)).join(',') + ']');
        sendTermInput(boundIdentCode, bytes);
      }
    }
  });

  socket.on('close', function() {
    log('TCP client', clientAddr, 'disconnected');
    menuClients.delete(socket);

    if (boundIdentCode !== null) {
      tcpBindings.delete(boundIdentCode);
      sendToEmulator({
        type: 'client-disconnected',
        identCode: boundIdentCode
      });
      log('Released terminal identCode=' + boundIdentCode);
    }
  });

  socket.on('error', function(err) {
    vlog('TCP socket error from', clientAddr + ':', err.message);
  });
});

tcpServer.on('error', function(err) {
  console.error('TCP server error:', err.message);
  process.exit(1);
});

// =========================================================
// HDLC TCP servers
// =========================================================
function createHdlcServer(hdlcConf) {
  var channel = hdlcConf.channel;
  var hdlcPort = hdlcConf.port;

  var hdlcServer = net.createServer(function(socket) {
    var clientAddr = socket.remoteAddress + ':' + socket.remotePort;
    log('HDLC ch=' + channel + ' TCP client connected from', clientAddr);

    // Only one client per HDLC channel
    if (hdlcBindings.has(channel)) {
      var existing = hdlcBindings.get(channel);
      if (existing.socket && !existing.socket.destroyed) {
        log('HDLC ch=' + channel + ' replacing existing connection');
        existing.socket.destroy();
      }
    }

    hdlcBindings.set(channel, { socket: socket, clientAddr: clientAddr });

    // Notify emulator: carrier present
    sendHdlcCarrier(channel, true);

    socket.on('data', function(data) {
      var buf = Buffer.isBuffer(data) ? data : Buffer.from(data);
      sendHdlcRxFrame(channel, buf);
      vlog('HDLC RX ch=' + channel + ' len=' + buf.length + ' <- TCP ' + Array.from(buf.slice(0, 20)).map(b => b.toString(16).padStart(2,'0')).join(' '));
    });

    socket.on('close', function() {
      log('HDLC ch=' + channel + ' TCP client disconnected');
      hdlcBindings.delete(channel);
      sendHdlcCarrier(channel, false);
    });

    socket.on('error', function(err) {
      vlog('HDLC ch=' + channel + ' TCP error:', err.message);
    });
  });

  hdlcServer.on('error', function(err) {
    log('HDLC server ch=' + channel + ' error:', err.message);
  });

  hdlcServer.listen(hdlcPort, function() {
    log('HDLC', hdlcConf.name, 'ch=' + channel, 'listening on port', hdlcPort);
  });

  hdlcServers.push(hdlcServer);
}

if (config.hdlc) {
  for (const hdlcConf of config.hdlc) {
    if (!hdlcConf.enabled) {
      vlog('HDLC', hdlcConf.name, 'disabled (port', hdlcConf.port + ')');
      continue;
    }
    createHdlcServer(hdlcConf);
  }
}

// =========================================================
// Ethernet segment TCP servers
//
// One server per emulated ethernet segment. Anything that speaks RETH can
// join: a native nd500x with ND500X_ETH_UPLINK=tcp:host:port, a RetroCore
// machine, or another gateway. Frames are repeated to every other member and
// up to the emulator, so a browser NDIX and a native one land on one wire.
//
// UNLIKE HDLC there is no "one client per channel" rule. That is the whole
// point - see the comment on ethBindings.
// =========================================================
function createEthServer(ethConf) {
  var segment = ethConf.segment || 0;
  var ethPort = ethConf.port;

  var ethServer = net.createServer(function(socket) {
    var clientAddr = socket.remoteAddress + ':' + socket.remotePort;
    var member = { socket: socket, clientAddr: clientAddr, rx: Buffer.alloc(0) };
    var greeted = false;

    // Nagle would batch small frames together. They are already framed, so it
    // costs latency and buys nothing.
    socket.setNoDelay(true);

    // Both sides write the hello immediately, then read the other's. Writing
    // first means two peers that connect simultaneously cannot deadlock.
    var hello = Buffer.alloc(RETH_HANDSHAKE_LEN);
    RETH_MAGIC.copy(hello, 0);
    hello[4] = RETH_VERSION_MEMBER;
    socket.write(hello);

    log('ETH seg=' + segment + ' TCP client connected from', clientAddr);

    socket.on('data', function(data) {
      member.rx = Buffer.concat([member.rx, Buffer.isBuffer(data) ? data : Buffer.from(data)]);

      // The 5-byte hello comes first and once.
      if (!greeted) {
        if (member.rx.length < RETH_HANDSHAKE_LEN) return;
        if (member.rx.slice(0, 4).compare(RETH_MAGIC) !== 0) {
          log('ETH seg=' + segment + ' ' + clientAddr +
              ' is not speaking RETH (got ' +
              JSON.stringify(member.rx.slice(0, 4).toString('latin1')) + ') - closing');
          socket.destroy();
          return;
        }
        vlog('ETH seg=' + segment + ' ' + clientAddr + ' hello ok, version ' + member.rx[4]);
        member.rx = member.rx.slice(RETH_HANDSHAKE_LEN);
        greeted = true;

        if (!ethBindings.has(segment)) ethBindings.set(segment, new Set());
        ethBindings.get(segment).add(member);
        sendEthLink(segment, true);
      }

      // Then [u16 BE length][frame], any number of them, split across reads.
      for (;;) {
        if (member.rx.length < 2) break;
        var len = (member.rx[0] << 8) | member.rx[1];
        if (len === 0 || len > RETH_MAX_FRAME) {
          // Out of step with the sender and there is no way to resynchronise a
          // pure length-prefixed stream. Dropping the connection is the only
          // honest move; carrying on would forward rubbish as frames.
          log('ETH seg=' + segment + ' ' + clientAddr + ' bad frame length ' +
              len + ' - closing');
          socket.destroy();
          return;
        }
        if (member.rx.length < 2 + len) break;   // rest of it has not arrived
        var frameData = member.rx.slice(2, 2 + len);
        member.rx = member.rx.slice(2 + len);
        ethRepeat(segment, frameData, member);
        vlog('ETH RX seg=' + segment + ' len=' + len + ' <- ' + clientAddr);
      }
    });

    socket.on('close', function() {
      log('ETH seg=' + segment + ' TCP client disconnected', clientAddr);
      var members = ethBindings.get(segment);
      if (members) {
        members.delete(member);
        if (members.size === 0) sendEthLink(segment, false);
      }
    });

    socket.on('error', function(err) {
      vlog('ETH seg=' + segment + ' TCP error:', err.message);
    });
  });

  ethServer.listen(ethPort, function() {
    log('ETH segment ' + segment + ' listening on port ' + ethPort +
        ' (' + ethConf.name + ')');
  });
  ethServer.on('error', function(err) {
    log('ETH segment ' + segment + ' cannot listen on ' + ethPort + ':', err.message);
  });

  ethServers.push(ethServer);
}

if (config.ethernet) {
  for (const ethConf of config.ethernet) {
    if (!ethConf.enabled) {
      vlog('ETH', ethConf.name, 'disabled (port', ethConf.port + ')');
      continue;
    }
    createEthServer(ethConf);
  }
}

// =========================================================
// Start servers
// =========================================================
openDiskImages();

tcpServer.listen(tcpPort, function() {
  log('TCP terminal server listening on port', tcpPort);
});

server.listen(wsPort, function() {
  log('');
  log('=== ND-100/CX Gateway Server ===');
  log('');
  log('Config: ' + configPath);
  log('');

  // Network
  log('Network:');
  log('  WebSocket:  ws://localhost:' + wsPort + '/');
  log('  Terminals:  tcp://localhost:' + tcpPort + '/');
  if (staticDir) {
    log('  Static:     http://localhost:' + wsPort + '/ (' + staticDir + ')');
    log('  COOP/COEP:  enabled (SharedArrayBuffer support)');
  }

  // HDLC
  if (config.hdlc && config.hdlc.length > 0) {
    log('');
    log('HDLC:');
    config.hdlc.forEach(function(h) {
      log('  ' + h.name + ': port ' + h.port + ' (' + (h.enabled ? 'enabled' : 'disabled') + ')');
    });
  }

  // Ethernet
  if (config.ethernet && config.ethernet.length > 0) {
    log('');
    log('Ethernet:');
    config.ethernet.forEach(function(e) {
      log('  ' + e.name + ': segment ' + (e.segment || 0) + ', port ' + e.port +
          ' (' + (e.enabled ? 'enabled' : 'disabled') + ')');
    });
  }

  // Disk images
  var smdCount = Object.keys(diskFds.smd).length;
  var floppyCount = Object.keys(diskFds.floppy).length;
  log('');
  log('Disk images:');
  if (smdCount === 0 && floppyCount === 0) {
    log('  (none configured)');
  } else {
    Object.entries(diskFds.smd).forEach(function(e) {
      log('  SMD ' + e[0] + ':     ' + e[1].name + ' (' + Math.round(e[1].size / 1024 / 1024) + ' MB)');
    });
    Object.entries(diskFds.floppy).forEach(function(e) {
      log('  Floppy ' + e[0] + ':  ' + e[1].name + ' (' + Math.round(e[1].size / 1024) + ' KB)');
    });
  }

  log('');
  log('Waiting for emulator connection...');
});

server.on('error', function(err) {
  console.error('HTTP server error:', err.message);
  process.exit(1);
});

// =========================================================
// Graceful shutdown
// =========================================================
function shutdown() {
  log('Shutting down...');

  // Close disk file descriptors
  closeDiskImages();

  // Close all TCP client connections
  for (const [identCode, binding] of tcpBindings) {
    if (binding.socket && !binding.socket.destroyed) {
      binding.socket.destroy();
    }
  }
  for (const sock of menuClients) {
    if (!sock.destroyed) sock.destroy();
  }

  // Close HDLC TCP connections
  for (const [channel, binding] of hdlcBindings) {
    if (binding.socket && !binding.socket.destroyed) {
      binding.socket.destroy();
    }
  }

  // Close HDLC servers
  for (const hs of hdlcServers) {
    hs.close();
  }

  // Close ethernet segment servers and every member on them
  for (const members of ethBindings.values()) {
    for (const m of members) {
      if (m.socket && !m.socket.destroyed) m.socket.destroy();
    }
  }
  ethBindings.clear();
  for (const es of ethServers) {
    es.close();
  }

  tcpServer.close();
  wss.close();
  server.close();
  process.exit(0);
}

process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
