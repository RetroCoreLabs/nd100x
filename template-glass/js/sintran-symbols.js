//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// sintran-symbols.js - Shared symbol table and memory read helpers for SINTRAN III
// Provides addresses, structure offsets, and utility functions used by all
// SINTRAN debug windows (processes, queues, segments, devices).

(function() {
  'use strict';

  // =========================================================
  // Fixed root pointers (identical across K03/L07/M06)
  // =========================================================
  var FIXED = {
    RTREF:  0x807,   // 004007 - current running process RT address
    CURPR:  0x808,   // 004010 - current process number
    MQUEU:  0x809,   // 004011 - monitor queue head (I/O datafield chain)
    BTIMQ:  0x80A,   // 004012 - time queue head
    BEXQU:  0x80B,   // 004013 - execution queue head
    SGMAX:  0x80D,   // 004015 - max segment number
    RTSTA:  0x810,   // 004020 - RT description table start
    SEGTB:  0x8D0,   // 004320 - segment table bank number
    SEGST:  0x8D1,   // 004321 - segment table offset within bank
    CORMB:  0x8D2,   // 004322 - core map bank number
    RTEND:  0x8D3,   // 004323 - RT description table end pointer
    CNVRT:  0x8D7,   // 004327 - CCNVRT array (32 group offsets for LOGDBANK)
    SINVER: 0x82D    // 004055 - SINVER0: version letter (low byte), OS type (bits 8-10)
  };

  // =========================================================
  // Version-dependent symbols
  // =========================================================
  var VERSIONED = {
    K: { FBPR:0x6DA2, LTBP:0x77B0, LBPR:0x788C, STSIN:0x5F06,
         SBPRT:0x9AAF, DT01R:0x23DA, BPRTS:11,
         LGTFP:null, LGDGM:25 },       // K03: no LGTFPHPAGE symbol, max group 31oct=25dec
    L: { FBPR:0x26DF, LTBP:0x3145, LBPR:0x3221, STSIN:0x144F,
         SBPRT:0xBE73, DT01R:0x5787, BPRTS:12,
         LGTFP:0xF093, LGDGM:31 },     // L07: LGTFPHPAGE=170223oct, max group 37oct=31dec
    M: { FBPR:0x29CC, LTBP:0x3B12, LBPR:0x3BEE, STSIN:0x147C,
         SBPRT:0xBF08, DT01R:0x6013, BPRTS:12,
         LGTFP:0xF093, LGDGM:31 }      // M06: LGTFPHPAGE=170223oct, max group 37oct=31dec
  };

  // =========================================================
  // RT-Description field offsets (22 words, stable across versions)
  // =========================================================
  var RT_DESC = {
    SIZE:   22,
    TLINK:  0x00,   // Time queue link
    STATU:  0x01,   // Status flags
    INPRI:  0x02,   // Initial priority
    PRITY:  0x03,   // Priority / Type+Ring
    DTIM1:  0x04,   // Delay time high
    DTIM2:  0x05,   // Delay time low
    DTIN1:  0x06,   // DT interval high
    DTIN2:  0x07,   // DT interval low
    STADR:  0x08,   // Start address
    SEGM1:  0x09,   // Code segment
    SEGM2:  0x0A,   // Data segment
    WLINK:  0x0B,   // Exec/wait queue link
    ACT1S:  0x0C,   // Active segment 1
    ACT2S:  0x0D,   // Active segment 2
    INIPR:  0x0E,   // Initial priority register
    ACTPR:  0x0F,   // Active PCR value
    BRESL:  0x10,   // Reservation chain head
    RSEGM:  0x11,   // Reentrant segment
    BUFWI:  0x12,   // Buffer window page
    TRMWI:  0x13,   // Terminal window page
    N5WIN:  0x14,   // ND-500 window page
    RTDLG:  0x15    // Pointer to register save block
  };

  // =========================================================
  // STATU bit definitions
  // =========================================================
  var STATU_BITS = {
    BACK: 0,    // Background process
    USED: 1,    // Entry in use
    TSLI: 2,    // Time slice
    ESCF: 3,    // Escape flag
    BRKF: 4,    // Break flag
    SWWA: 8,    // Swap wait
    RTOF: 9,    // RT off
    TMOU: 10,   // Timeout
    INT:  12,   // Internal process
    RWAI: 13,   // Resource wait
    WAIT: 15    // I/O wait
  };

  // =========================================================
  // Segment table entry offsets (8 words per entry)
  // =========================================================
  var SEG_ENTRY = {
    SIZE:   8,
    SEGLI:  0,    // Segment link (next in chain)
    PRESE:  1,    // Previous segment
    LOGAD:  2,    // Logical address
    SEGLE:  3,    // Segment length (pages)
    MADR:   4,    // Memory address
    FLAG:   5,    // Flags
    SGSTA:  6,    // Segment status
    BPAGL:  7     // Base page logical
  };

  // =========================================================
  // I/O Datafield offsets
  // =========================================================
  var IO_DF = {
    RESLI:  0x00,   // Reservation link (next DF in chain)
    RTRES:  0x01,   // RT that reserved this DF
    BWLIN:  0x02,   // Buffer wait link
    TYPRI:  0x03,   // Device type + ring bits
    ISTAT:  0x04,   // I/O status word
    MLINK:  0x05,   // Monitor queue link
    MFUNC:  0x06,   // Monitor function code
    HSTAT:  0x08,   // Hardware status
    MTRAN:  0x09,   // Monitor transfer count
    MEMA1:  0x0D    // Memory address 1
  };

  // =========================================================
  // TYPRI bit definitions (device type word at IO_DF offset 003)
  // Tested in GDEVTY priority order: TERM, BAD, IBDV, FLOP, MT, RFIL
  // =========================================================
  var TYPRI_BITS = {
    CLDV:  2,    // Closable device
    NORE:  3,    // No reservation required
    BAD:   4,    // TAD (Terminal Adapter)
    TERM:  5,    // Terminal
    IBDV:  6,    // Indexed block device (disk)
    HDMA:  7,    // HDMA / X.21
    FLOP:  8,    // Floppy disk
    MT:    9,    // Magnetic tape
    M144: 10,    // 144-byte block format
    SPLI: 11,    // Split datafield (R/W halves)
    ISET: 12,    // I/O initialized
    CONC: 13,    // Concurrent I/O
    RFIL: 14,    // Remote file
    IOBT: 15     // Block transfer capable
  };

  // Legacy alias for backward compatibility
  var ISTAT_BITS = TYPRI_BITS;

  // =========================================================
  // Register save block offsets (via RTDLG pointer)
  // =========================================================
  var REG_SAVE = {
    P: 0, X: 1, T: 2, A: 3,
    D: 4, L: 5, S: 6, B: 7,
    BITMAP_START: 8,  // 8 words (128 bits) page bitmap
    BITMAP_END: 15
  };

  // =========================================================
  // Utility functions
  // =========================================================

  // Detect once whether Module.HEAPU16 is safely accessible.
  // Emscripten creates an abort-getter for non-exported items that poisons
  // the entire Module if triggered, so we must check without calling it.
  var heapU16Safe = false;
  function checkHeapU16() {
    if (heapU16Safe) return true;
    if (!emu) return false;
    heapU16Safe = emu.hasHEAPU16();
    return heapU16Safe;
  }

  // Raw read: always goes through MemoryRead (standard PIT), no DPIT translation
  // Returns a Promise<Uint16Array> (resolves immediately in direct mode)
  function readBlockRaw(addr, count) {
    if (!emu || !emu.isReady()) return Promise.resolve(new Uint16Array(0));

    // Fast path: bulk read + HEAPU16 (only if safely available, direct mode only)
    if (!emu.isWorkerMode() && emu.hasFunction('_Dbg_ReadMemoryBlock') && checkHeapU16()) {
      var ptr = emu.readMemoryBlock(addr, count);
      var buf = emu.getHEAPU16Buffer();
      if (buf) {
        var view = new Uint16Array(buf, ptr, count);
        var copy = new Uint16Array(count);
        copy.set(view);
        return Promise.resolve(copy);
      }
    }

    // Worker mode: batch read returns Promise<array>
    if (emu.isWorkerMode()) {
      return Promise.resolve(emu.readMemoryBlock(addr, count)).then(function(values) {
        var result = new Uint16Array(count);
        for (var i = 0; i < count; i++) {
          result[i] = (values[i] || 0) & 0xFFFF;
        }
        return result;
      });
    }

    // Direct mode fallback: read word by word (always works)
    var result = new Uint16Array(count);
    for (var i = 0; i < count; i++) {
      result[i] = emu.readMemory(addr + i) & 0xFFFF;
    }
    return Promise.resolve(result);
  }

  // Raw read: single word through standard PIT
  // Returns a Promise<number> (resolves immediately in direct mode)
  function readWordRaw(addr) {
    if (!emu || !emu.isReady()) return Promise.resolve(0);
    return Promise.resolve(emu.readMemory(addr)).then(function(v) { return v & 0xFFFF; });
  }

  // Smart read: uses DPIT translation when SINTRAN is detected, otherwise raw
  // Returns a Promise<Uint16Array>
  function readBlock(addr, count) {
    if (typeof sintranState !== 'undefined' && sintranState.detected &&
        emu && emu.isReady()) {
      return readBlockDPIT(addr, count);
    }
    return readBlockRaw(addr, count);
  }

  // Smart read: single word, uses DPIT when available
  // Returns a Promise<number>
  function readWord(addr) {
    if (typeof sintranState !== 'undefined' && sintranState.detected &&
        emu && emu.isReady()) {
      return readWordDPIT(addr);
    }
    return readWordRaw(addr);
  }

  // Get version-specific symbols based on detected SINTRAN version
  function getVersionSymbols() {
    if (!sintranState || !sintranState.versionLetter) return null;
    var letter = sintranState.versionLetter.toUpperCase();
    return VERSIONED[letter] || null;
  }

  // Decode ND-100 packed ASCII string (2 chars per word, strip parity bit 7)
  function decodeNDString(words, maxChars) {
    var result = '';
    var max = maxChars || words.length * 2;
    for (var i = 0; i < words.length && result.length < max; i++) {
      var w = words[i];
      var hi = (w >> 8) & 0x7F;
      var lo = w & 0x7F;
      if (hi >= 0x20 && hi <= 0x7E) result += String.fromCharCode(hi);
      else break;
      if (result.length < max) {
        if (lo >= 0x20 && lo <= 0x7E) result += String.fromCharCode(lo);
        else break;
      }
    }
    return result.replace(/\s+$/, '');
  }

  // Format a 16-bit value as 6-digit octal
  function toOctal(val) {
    return (val & 0xFFFF).toString(8).padStart(6, '0');
  }

  // Test if a specific bit is set in a 16-bit word
  function testBit(word, bit) {
    return (word & (1 << bit)) !== 0;
  }

  // =========================================================
  // Physical memory access (bypasses MMS, reads VolatileMemory directly)
  // =========================================================

  // Returns a Promise<number>
  function readWordPhysical(physAddr) {
    if (!emu || !emu.isReady()) return Promise.resolve(0);
    return Promise.resolve(emu.readPhysicalMemory(physAddr)).then(function(v) { return v & 0xFFFF; });
  }

  // Returns a Promise<Uint16Array>
  function readBlockPhysical(physAddr, count) {
    if (!emu || !emu.isReady()) return Promise.resolve(new Uint16Array(0));

    // Fast path: direct mode only
    if (!emu.isWorkerMode() && emu.hasFunction('_Dbg_ReadPhysicalMemoryBlock') && checkHeapU16()) {
      var ptr = emu.readPhysicalMemoryBlock(physAddr, count);
      var buf = emu.getHEAPU16Buffer();
      if (buf) {
        var view = new Uint16Array(buf, ptr, count);
        var copy = new Uint16Array(count);
        copy.set(view);
        return Promise.resolve(copy);
      }
    }

    // Worker mode: batch read
    if (emu.isWorkerMode()) {
      return Promise.resolve(emu.readPhysicalMemoryBlock(physAddr, count)).then(function(values) {
        var result = new Uint16Array(count);
        for (var i = 0; i < count; i++) {
          result[i] = (values[i] || 0) & 0xFFFF;
        }
        return result;
      });
    }

    // Direct mode fallback: word by word
    var result = new Uint16Array(count);
    for (var i = 0; i < count; i++) {
      result[i] = emu.readPhysicalMemory(physAddr + i) & 0xFFFF;
    }
    return Promise.resolve(result);
  }

  // =========================================================
  // DPIT (Data Page Table) translation
  // Kernel data structures are accessed through the Alternative PT.
  // We read DPIT entries via Dbg_GetPageTableEntryRaw, extract the PPN,
  // then do physical memory reads.
  // =========================================================

  // =========================================================
  // Page table translation cache
  // Keyed by PT number -> Array of 64 PPNs
  // =========================================================
  var ptCache = {};  // { ptNumber: [ppn0, ppn1, ..., ppn63] }

  // Build a translation map for any given page table: vpn -> ppn (64 entries)
  // Returns a Promise<Array> (resolves immediately in direct mode)
  function buildPTMap(ptNumber) {
    if (!emu || !emu.isReady()) return Promise.resolve(null);
    if (ptNumber < 0 || ptNumber >= 16) return Promise.resolve(null);
    var extended = emu.getExtendedMode();
    return Promise.resolve(emu.getPageTableMap(ptNumber)).then(function(entries) {
      var map = new Array(64);
      for (var vpn = 0; vpn < 64; vpn++) {
        var pte = (entries[vpn] || 0) >>> 0;
        if (extended) {
          map[vpn] = pte & 0x3FFF;   // 14-bit PPN
        } else {
          map[vpn] = pte & 0x01FF;   // 9-bit PPN
        }
      }
      return map;
    });
  }

  // Get or build cached PT map for a given page table number
  // Returns a Promise<Array>
  function ensurePTCache(ptNumber) {
    if (ptCache[ptNumber]) {
      return Promise.resolve(ptCache[ptNumber]);
    }
    return buildPTMap(ptNumber).then(function(map) {
      ptCache[ptNumber] = map;
      return map;
    });
  }

  // Invalidate all PT caches
  function invalidatePTCache() {
    ptCache = {};
  }

  // Get the DPIT (Alternative Page Table) number.
  // DPIT is always PIT #7 in SINTRAN III - hardcoded because:
  // 1) Level 0 (idle/DUMMY) uses ADTPI (PIT#15), not DPIT
  // 2) All kernel data levels (1,2,4,5,10-16) use APIT = DPIT #7
  function getDPITNumber() {
    return 7;
  }

  // Translate a logical address through a specific page table to physical
  // Returns Promise<number> (physical word address, or -1 if translation fails)
  function translateViaPT(ptNumber, logicalAddr) {
    return ensurePTCache(ptNumber).then(function(map) {
      if (!map) return -1;
      var vpn = (logicalAddr >>> 10) & 0x3F; // 6-bit VPN (addr / 1024)
      var offset = logicalAddr & 0x3FF;       // 10-bit offset within page
      var ppn = map[vpn];
      return ppn * 1024 + offset;
    });
  }

  // Convenience: translate through the DPIT specifically
  // Returns Promise<number>
  function translateDPIT(logicalAddr) {
    var dpit = getDPITNumber();
    if (dpit < 0) return Promise.resolve(-1);
    return translateViaPT(dpit, logicalAddr);
  }

  // Read a single 16-bit word via a specific page table translation
  // Returns Promise<number>
  function readWordViaPT(ptNumber, logicalAddr) {
    return translateViaPT(ptNumber, logicalAddr).then(function(phys) {
      if (phys < 0) return readWordRaw(logicalAddr); // fallback to raw
      return readWordPhysical(phys);
    });
  }

  // Read a block of N consecutive words via a specific page table translation
  // Handles page boundary crossings correctly
  // Returns Promise<Uint16Array>
  function readBlockViaPT(ptNumber, logicalAddr, count) {
    return ensurePTCache(ptNumber).then(function(map) {
      if (!map) return readBlockRaw(logicalAddr, count); // fallback to raw

      // Build list of physical read chunks (handle page boundaries)
      var chunks = [];
      var i = 0;
      while (i < count) {
        var addr = (logicalAddr + i) & 0xFFFF;
        var vpn = (addr >>> 10) & 0x3F;
        var offset = addr & 0x3FF;
        var ppn = map[vpn];
        var physBase = ppn * 1024 + offset;
        var remaining = 1024 - offset;
        var chunk = Math.min(remaining, count - i);
        chunks.push({ physBase: physBase, chunk: chunk, offset: i });
        i += chunk;
      }

      // Read all chunks (chain Promises sequentially to avoid race conditions)
      var result = new Uint16Array(count);
      var p = Promise.resolve();
      chunks.forEach(function(c) {
        p = p.then(function() {
          return readBlockPhysical(c.physBase, c.chunk).then(function(data) {
            for (var j = 0; j < c.chunk; j++) {
              result[c.offset + j] = data[j];
            }
          });
        });
      });
      return p.then(function() { return result; });
    });
  }

  // Convenience: DPIT-specific reads (uses kernel APT)
  function readWordDPIT(logicalAddr) {
    var dpit = getDPITNumber();
    if (dpit < 0) return readWordRaw(logicalAddr);
    return readWordViaPT(dpit, logicalAddr);
  }

  function readBlockDPIT(logicalAddr, count) {
    var dpit = getDPITNumber();
    if (dpit < 0) return readBlockRaw(logicalAddr, count);
    return readBlockViaPT(dpit, logicalAddr, count);
  }

  // =========================================================
  // Linker symbol table (SYMBOL-2-LIST.SYMB.TXT)
  //
  // RT-program names are NOT stored in memory (RT descriptions have
  // no name field). The authoritative name source is the running
  // version's own linker symbol table, shipped verbatim under
  // data/symbols/{K03,L07,M06}/ and parsed here at load time into an
  // address -> name map. The version letter detected from SINVER0 in
  // memory selects which table to load.
  // Line format: "NAME=OCTALVALUE" (possibly space-padded).
  // =========================================================

  var VERSION_DIRS = { K: 'K03', L: 'L07', M: 'M06' };

  // { letter: { addrToName: {addr: name}, count: N } }
  var symbolTables = {};
  // { letter: Promise } - in-flight loads
  var symbolTableLoads = {};

  // Parse "NAME=OCTAL" lines into an address -> name map.
  // Several symbols can share one address (range markers like 9FBPR
  // sit on the same slot as the real name, e.g. BAK01). Preference:
  // a name starting with a letter beats one starting with a digit;
  // between two names of the same class, the later line wins.
  function parseSymbolList(text) {
    var addrToName = {};
    var count = 0;
    var lines = text.split('\n');
    for (var i = 0; i < lines.length; i++) {
      var m = lines[i].trim().match(/^([^=\s]+)=([0-7]+)$/);
      if (!m) continue;
      var name = m[1];
      var addr = parseInt(m[2], 8);
      var prev = addrToName[addr];
      if (prev !== undefined) {
        var prevAlpha = /^[A-Z]/i.test(prev);
        var newAlpha = /^[A-Z]/i.test(name);
        if (prevAlpha && !newAlpha) continue;   // keep letter-name over marker
      }
      addrToName[addr] = name;
      count++;
    }
    return { addrToName: addrToName, count: count };
  }

  // Load the symbol table for a version letter (K/L/M).
  // Returns Promise<table|null>; result is cached.
  function loadSymbolTable(letter) {
    if (!letter) return Promise.resolve(null);
    var key = letter.toUpperCase();
    if (symbolTables[key]) return Promise.resolve(symbolTables[key]);
    if (symbolTableLoads[key]) return symbolTableLoads[key];
    var dir = VERSION_DIRS[key];
    if (!dir) {
      console.warn('[symtab] No symbol table shipped for SINTRAN version ' + key);
      return Promise.resolve(null);
    }
    var url = 'data/symbols/' + dir + '/SYMBOL-2-LIST.SYMB.TXT';
    symbolTableLoads[key] = fetch(url)
      .then(function(r) {
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.text();
      })
      .then(function(text) {
        var table = parseSymbolList(text);
        symbolTables[key] = table;
        delete symbolTableLoads[key];
        console.log('[symtab] Loaded ' + dir + ' symbol table: ' + table.count + ' symbols');
        return table;
      })
      .catch(function(err) {
        delete symbolTableLoads[key];
        console.warn('[symtab] Failed to load ' + url + ':', err.message);
        return null;
      });
    return symbolTableLoads[key];
  }

  // Sync: symbol table for the currently detected version, or null
  function getSymbolTableSync() {
    if (typeof sintranState === 'undefined' || !sintranState.versionLetter) return null;
    return symbolTables[sintranState.versionLetter.toUpperCase()] || null;
  }

  // Sync: reverse-lookup a name for an address in the detected
  // version's symbol table. Returns name or null.
  function lookupSymbolName(addr) {
    var table = getSymbolTableSync();
    if (!table) return null;
    return table.addrToName[addr] || null;
  }

  // =========================================================
  // RT table discovery (shared by all SINTRAN windows)
  // =========================================================

  var cachedRtInfo = null;

  // Returns Promise<{base, count}>
  function discoverRtTable() {
    // Return cached result if available (cleared on each refresh cycle)
    if (cachedRtInfo) return Promise.resolve(cachedRtInfo);

    // Simple approach matching Python reference: use RTSTA as base, RTEND as end
    return Promise.all([readWord(FIXED.RTSTA), readWord(FIXED.RTEND)]).then(function(vals) {
      var rtstaVal = vals[0];
      var rtendVal = vals[1];

      if (rtstaVal === 0 && rtendVal === 0) {
        cachedRtInfo = { base: 0, count: 0 };
        return cachedRtInfo;
      }

      var tableBase = Math.min(rtstaVal, rtendVal);
      var tableEnd = Math.max(rtstaVal, rtendVal);
      if (tableBase === 0) tableBase = tableEnd;

      var rtCount = Math.floor((tableEnd - tableBase) / RT_DESC.SIZE);

      if (rtCount <= 0) rtCount = 1;
      if (rtCount > 512) rtCount = 512;

      cachedRtInfo = { base: tableBase, count: rtCount };
      return cachedRtInfo;
    });
  }

  function invalidateRtCache() {
    cachedRtInfo = null;
    invalidatePTCache();
  }

  // Resolve an RT address to its RT number using discovered table base
  // Returns Promise<number>
  function rtAddrToNumber(rtAddr) {
    return discoverRtTable().then(function(info) {
      if (info.base === 0 || rtAddr < info.base) return -1;
      var offset = rtAddr - info.base;
      if (offset % RT_DESC.SIZE !== 0) return -1;
      return Math.floor(offset / RT_DESC.SIZE);
    });
  }

  // Synchronous versions for render-time use (read from cache populated by
  // the async discoverRtTable() call at the start of each refresh cycle).
  // Returns {base, count} or {base:0, count:0} if cache not yet populated.
  function discoverRtTableSync() {
    return cachedRtInfo || { base: 0, count: 0 };
  }

  // Synchronous RT address to number (uses cached RT table info)
  function rtAddrToNumberSync(rtAddr) {
    var info = discoverRtTableSync();
    if (info.base === 0 || rtAddr < info.base) return -1;
    var offset = rtAddr - info.base;
    if (offset % RT_DESC.SIZE !== 0) return -1;
    return Math.floor(offset / RT_DESC.SIZE);
  }

  // =========================================================
  // Export
  // =========================================================
  window.sintranSymbols = {
    FIXED: FIXED,
    VERSIONED: VERSIONED,
    RT_DESC: RT_DESC,
    STATU_BITS: STATU_BITS,
    SEG_ENTRY: SEG_ENTRY,
    IO_DF: IO_DF,
    TYPRI_BITS: TYPRI_BITS,
    ISTAT_BITS: ISTAT_BITS,
    REG_SAVE: REG_SAVE,
    readBlock: readBlock,
    readWord: readWord,
    readWordRaw: readWordRaw,
    readBlockRaw: readBlockRaw,
    readWordPhysical: readWordPhysical,
    readBlockPhysical: readBlockPhysical,
    readWordViaPT: readWordViaPT,
    readBlockViaPT: readBlockViaPT,
    readWordDPIT: readWordDPIT,
    readBlockDPIT: readBlockDPIT,
    translateViaPT: translateViaPT,
    translateDPIT: translateDPIT,
    getDPITNumber: getDPITNumber,
    ensurePTCache: ensurePTCache,
    invalidatePTCache: invalidatePTCache,
    getVersionSymbols: getVersionSymbols,
    loadSymbolTable: loadSymbolTable,
    getSymbolTableSync: getSymbolTableSync,
    lookupSymbolName: lookupSymbolName,
    decodeNDString: decodeNDString,
    toOctal: toOctal,
    testBit: testBit,
    discoverRtTable: discoverRtTable,
    discoverRtTableSync: discoverRtTableSync,
    invalidateRtCache: invalidateRtCache,
    rtAddrToNumber: rtAddrToNumber,
    rtAddrToNumberSync: rtAddrToNumberSync
  };
})();
