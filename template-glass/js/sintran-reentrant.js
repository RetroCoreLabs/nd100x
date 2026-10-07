//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// sintran-reentrant.js - Reentrant Programs Window
//
// REECOMT (Reentrant Command Table) is in 5OPSEG (segment 3), page 6 (LOGPA=LOGAD+6).
// The page can be evicted from RAM (it's only loaded on demand, e.g. when @LIST-REENTRANT runs).
//
// Read strategy (in order):
//   1. Walk BPAGL core-map chain, scan each RAM page for signature at word 303 = 0x4E52
//   2. If in RAM: derive SEGFIL0 base = diskLBA(reecomt) - seg3.madr - relPage
//      where diskLBA comes from SMD scan and relPage from core-map LOGPA
//   3. If REECOMT not in RAM: return empty — run @LIST-REENTRANT to load it
//
// REECOMT table structure (word 0, 4 words per entry):
//   [0] start address    [1] restart address
//   [2] name pointer     [3] segment number
// Terminator: entry with segno=0 and name_ptr=0xffff
//
// Name table starts at word 303 (byte 606).
// Names are packed ASCII bytes, separated by 0x27 (apostrophe).
// Entry[i] matches name[i] (same sequential order).

(function() {
  'use strict';

  var sym = window.sintranSymbols;

  var refreshTimer = null;
  var lastEntries = [];
  var selectedEntry = null;
  var cachedSegmentMap = {};  // segNum -> full segment entry (for detail pane)
  var segfilBases   = null;    // [base0..base3] for SEGFIL 0-3; null = not read

  // Read up to 256 disk sectors via Dbg_ReadSMDSectors. Returns a Uint8Array view
  // (count*1024 bytes) into the C-side static buffer.
  // Throws Error with specific reason on failure.
  // IMPORTANT: view is invalidated by the next readSectors() call.
  function readSectors(lba, count) {
    var emuRef = (typeof emu !== 'undefined') ? emu : null;
    if (!emuRef) throw new Error('readSectors: emu proxy not loaded');
    if (!emuRef.readSMDSectors) throw new Error('readSectors: emu.readSMDSectors missing (WASM rebuild needed?)');
    if (!emuRef.getHEAPU8) throw new Error('readSectors: emu.getHEAPU8 missing');
    if (count <= 0 || count > 256) throw new Error('readSectors: count=' + count + ' out of [1,256]');
    var ptr = emuRef.readSMDSectors(0, lba, count);
    if (!ptr) throw new Error('readSectors: Dbg_ReadSMDSectors returned 0 (unit=0 lba=' + lba +
      ' count=' + count + ' — drive not mounted? past end of disk?)');
    var heapu8 = emuRef.getHEAPU8();
    if (!heapu8) throw new Error('readSectors: HEAPU8 not accessible');
    return new Uint8Array(heapu8.buffer, ptr, count * 1024);
  }

  // Scan the SMD disk for the REECOMT signature (word303 = 0x4E52).
  // Returns the first matching LBA. Throws Error if not found or scan failed.
  function scanDiskForREECOMT() {
    var emuRef = (typeof emu !== 'undefined') ? emu : null;
    if (!emuRef || !emuRef.getSMDBufferSize) {
      throw new Error('scanDiskForREECOMT: emu.getSMDBufferSize missing');
    }
    var totalBytes = emuRef.getSMDBufferSize(0);
    if (!totalBytes) {
      throw new Error('scanDiskForREECOMT: SMD unit 0 not mounted');
    }
    var totalSectors = Math.min((totalBytes / 1024) | 0, 8192);
    var BATCH = 256;
    for (var base = 0; base < totalSectors; base += BATCH) {
      var n = Math.min(BATCH, totalSectors - base);
      var view = readSectors(base, n);   // throws on failure
      for (var i = 0; i < n; i++) {
        var off = i * 1024 + 606;
        if (((view[off] << 8) | view[off + 1]) === 0x4E52) return base + i;
      }
    }
    throw new Error('scanDiskForREECOMT: signature 0x4E52 not found in ' +
      totalSectors + ' sectors — disk may not contain SINTRAN III filesystem');
  }

  // Discover SEGFIL0 base sector.
  //
  // Step 1: disk scan for REECOMT signature → reecomtLBA (always works).
  // Step 2: walk 5OPSEG chain → collect all in-RAM relPage → physPage entries.
  // Step 3a: check each in-RAM page for REECOMT signature (word 303 = 0x4E52).
  //          If found at relPage R: base = reecomtLBA - seg3.madr - R.
  // Step 3b: if REECOMT not in RAM, use disk/RAM comparison as anchor:
  //          for each in-RAM page, try all ~pages3 candidate bases, pick the one
  //          where first 8 disk words at (base + madr + rAnchor) match first 8 RAM
  //          words at physPage.  Threshold: ≥6/8 matching words.
  //
  // Note: BLST=004023₈ is a segment LRU-list head pointer, NOT the disk-base table.
  function discoverSegfilBaseAsync(seg3) {
    var reecomtLBA;
    try {
      reecomtLBA = scanDiskForREECOMT();
    } catch (e) {
      console.warn('[reentrant] disk scan failed:', e.message);
      return Promise.reject(e);
    }
    console.log('[reentrant] REECOMT at disk LBA=' + reecomtLBA);
    return sym.readWord(sym.FIXED.CORMB).then(function(cormb) {
      var cormBase = (cormb & 0xFF) << 16;
      var logad3   = seg3.logad;
      var bpagl3   = seg3.bpagl;
      var pages3   = seg3.segle & 0x3FF;
      if (!bpagl3) throw new Error('seg3 bpagl=0');

      var physForRelPage3 = {};
      var chainLimit = 256;
      function walkSeg3(entryAddr) {
        if (entryAddr === 0 || chainLimit-- <= 0) return Promise.resolve();
        return sym.readBlockPhysical(cormBase + entryAddr, 4).then(function(cme) {
          if (!cme || cme.length < 4) return Promise.resolve();
          var physPage = (entryAddr / 4) | 0;
          var relPage  = cme[3] - logad3;
          if (relPage >= 0 && relPage < pages3) {
            physForRelPage3[relPage] = physPage;
          }
          return walkSeg3(cme[0]);
        });
      }

      return walkSeg3(bpagl3).then(function() {
        var inRamRelPages = Object.keys(physForRelPage3).map(Number);
        console.log('[reentrant] 5OPSEG in-RAM relPages: [' +
          inRamRelPages.sort(function(a,b){return a-b;}).join(',') + ']');

        // Fast path: check each in-RAM page for REECOMT signature.
        // Strict validation requires:
        //   - word 303 = 0x4E52 ('NR' from "NRL")
        //   - 0x4E52 followed by 0x4C00 or 0x4C__ at word 304 byte 0 = 'L' (third char of "NRL")
        //   - first 2 entries have plausible distinct segnos in range 1..255
        //   - first 2 entries have non-zero namePtr (word 2)
        //   - signature 0x27 ('apostrophe' separator) appears in name table
        function checkSig(list, idx) {
          if (idx >= list.length) return Promise.resolve(-1);
          var rp = list[idx];
          var pp = physForRelPage3[rp];
          // Read words 0..511 (full first half = entries + name table)
          return sym.readBlockPhysical(pp * 1024, 512).then(function(w) {
            if (!w || w.length < 512) return checkSig(list, idx + 1);
            if (w[303] !== 0x4E52) return checkSig(list, idx + 1);

            var why = validateREECOMTPage(w);
            if (why) {
              console.log('[reentrant] checkSig: rp=' + rp + ' physPage=' + pp +
                ' has w303=0x4E52 but FAILED validation: ' + why + ' — false positive, skipping');
              return checkSig(list, idx + 1);
            }
            console.log('[reentrant] checkSig: REECOMT confirmed at rp=' + rp + ' physPage=' + pp +
              ' (entry0 segno=' + w[3] + ' entry1 segno=' + w[7] + ')');
            return rp;
          });
        }

        // Returns null if the page IS valid REECOMT, otherwise a string explaining why not.
        function validateREECOMTPage(w) {
          // Check 'L' = 0x4C as 3rd char (high byte of word 303+1 = word 304... but
          // names are byte-stream, so 'L' is the high byte of word 304)
          if ((w[303] >>> 8) !== 0x4E) return 'word 303 high byte != N';
          if ((w[303] & 0xFF) !== 0x52) return 'word 303 low byte != R';
          // Word 304 high byte should be 'L' (= 0x4C)
          if ((w[304] >>> 8) !== 0x4C) return 'word 304 high byte != L (expected "NRL...")';
          // Entries: segno is word 3, 7, 11, ...
          var seg0 = w[3], seg1 = w[7];
          if (seg0 < 1 || seg0 > 255) return 'entry0 segno=' + seg0 + ' out of range';
          if (seg1 < 1 || seg1 > 255) return 'entry1 segno=' + seg1 + ' out of range';
          if (seg0 === seg1) return 'entry0 and entry1 have same segno=' + seg0;
          // Name pointers should both be non-zero (not terminator)
          if (w[2] === 0)      return 'entry0 namePtr is 0';
          if (w[2] === 0xFFFF) return 'entry0 namePtr is terminator';
          if (w[6] === 0)      return 'entry1 namePtr is 0';
          if (w[6] === 0xFFFF) return 'entry1 namePtr is terminator';
          // Apostrophe separator (0x27) must appear in name table
          var foundSep = false;
          for (var nw = 303; nw < 380; nw++) {
            if ((w[nw] & 0xFF) === 0x27 || (w[nw] >>> 8) === 0x27) { foundSep = true; break; }
          }
          if (!foundSep) return 'no apostrophe separator (0x27) in name table';
          return null;
        }

        return checkSig(inRamRelPages, 0).then(function(reecomtRelPage) {
          if (reecomtRelPage >= 0) {
            var base = reecomtLBA - seg3.madr - reecomtRelPage;
            console.log('[reentrant] SEGFIL0 base=' + base +
              ' (REECOMT in RAM relPage=' + reecomtRelPage + ')');
            return [Math.max(0, base), 0, 0, 0];
          }

          // Fallback: disk/RAM comparison
          console.log('[reentrant] REECOMT not in RAM — using disk/RAM comparison');

          function diskFirst8(lba) {
            var view = readSectors(lba, 1);
            if (!view) return null;
            var w = [];
            for (var i = 0; i < 8; i++) {
              w.push(((view[i*2] << 8) | view[i*2+1]) & 0xFFFF);
            }
            return w;
          }

          var sortedAnchors = inRamRelPages.slice().sort(function(a, b) { return a - b; });
          function tryAnchor(aIdx) {
            if (aIdx >= sortedAnchors.length) return Promise.resolve(null);
            var rAnchor  = sortedAnchors[aIdx];
            var physPage = physForRelPage3[rAnchor];
            return sym.readBlockPhysical(physPage * 1024, 8).then(function(ramW) {
              if (!ramW || ramW.length < 8) return tryAnchor(aIdx + 1);
              var nz = 0; for (var i = 0; i < 8; i++) { if (ramW[i]) nz++; }
              if (nz < 3) return tryAnchor(aIdx + 1);
              for (var rReecomt = 0; rReecomt < pages3; rReecomt++) {
                var candidateBase = reecomtLBA - seg3.madr - rReecomt;
                if (candidateBase <= 0) continue;
                var diskW = diskFirst8(candidateBase + seg3.madr + rAnchor);
                if (!diskW) continue;
                var matches = 0;
                for (var wi = 0; wi < 8; wi++) { if (ramW[wi] === diskW[wi]) matches++; }
                if (matches >= 6) {
                  console.log('[reentrant] base=' + candidateBase +
                    ' (disk/RAM: anchor relPage=' + rAnchor + ' rReecomt=' + rReecomt +
                    ' matches=' + matches + '/8)');
                  return candidateBase;
                }
              }
              return tryAnchor(aIdx + 1);
            });
          }

          return tryAnchor(0).then(function(base) {
            if (base === null) throw new Error('disk/RAM comparison failed for all anchors');
            return [Math.max(0, base), 0, 0, 0];
          });
        });
      });
    }).catch(function(e) {
      console.warn('[reentrant] discoverSegfilBase failed:', e.message || e);
      throw e;   // propagate, do NOT swallow
    });
  }

  function getSegfilBasesAsync(seg3Hint) {
    if (segfilBases) return Promise.resolve(segfilBases);
    if (!sym) return Promise.reject(new Error('getSegfilBasesAsync: sintranSymbols not loaded'));
    if (!seg3Hint) return Promise.reject(new Error('getSegfilBasesAsync: 5OPSEG (seg3) entry not provided'));
    return discoverSegfilBaseAsync(seg3Hint).then(function(bases) {
      if (bases && bases[0] > 0) {
        segfilBases = bases;   // cache only on success
      }
      return bases;
    });
  }

  // REECOMT constants (confirmed from physical memory dump analysis)
  var REECOMT_ENTRY_WORDS = 4;   // [start, restart, name_ptr, segno]
  var REECOMT_MAX         = 16;  // max entries (terminator at group 15)
  var REECOMT_NAME_WORD   = 303; // word offset in page 0 where name table begins
  var REECOMT_NAME_SEP    = 0x27; // apostrophe byte separates names in name table

  // =========================================================
  // Segment table scan
  // Returns Promise<{reentrantSegs, seg3}>
  // =========================================================

  function readSegTableData() {
    var SE = sym.SEG_ENTRY;
    return Promise.all([
      sym.readWord(sym.FIXED.SEGTB),
      sym.readWord(sym.FIXED.SEGST),
      sym.readWord(sym.FIXED.SGMAX)
    ]).then(function(vals) {
      var segtb = vals[0], segst = vals[1], sgmax = vals[2];
      if (sgmax === 0 || sgmax > 4096) {
        return { reentrantSegs: [], seg3: null };
      }

      var physBase = (segtb << 16) + segst;
      var totalWords = (sgmax + 1) * SE.SIZE;

      return sym.readBlockPhysical(physBase, totalWords).then(function(data) {
        var reentrantSegs = [];
        var seg3 = null;
        cachedSegmentMap = {};

        for (var segNum = 0; segNum <= sgmax; segNum++) {
          var off = segNum * SE.SIZE;
          if (off + SE.SIZE > data.length) break;

          var allZero = true;
          for (var w = 0; w < SE.SIZE; w++) {
            if (data[off + w] !== 0) { allZero = false; break; }
          }
          if (allZero) continue;

          var entry = {
            segNum: segNum,
            segli:  data[off + SE.SEGLI],
            prese:  data[off + SE.PRESE],
            logad:  data[off + SE.LOGAD],
            segle:  data[off + SE.SEGLE] & 0x3FF,
            madr:   data[off + SE.MADR],
            flag:   data[off + SE.FLAG],
            sgsta:  data[off + SE.SGSTA],
            bpagl:  data[off + SE.BPAGL],
            usedBy: []
          };

          cachedSegmentMap[segNum] = entry;
          if (segNum === 3) seg3 = entry;

          // FLAG bit 4 = 5SREE (SHARED/REENTRANT)
          if (entry.flag & (1 << 4)) {
            reentrantSegs.push(entry);
          }
        }

        return { reentrantSegs: reentrantSegs, seg3: seg3 };
      });
    });
  }

  // =========================================================
  // Read REECOMT from 5OPSEG page 0 via physical memory
  //
  // 5OPSEG page 0 is always resident in memory (it contains SINTRAN kernel code).
  //
  // Core map (CORMB bank): one 4-word entry per physical page.
  //   word 0: PAGLI  — next entry in segment's chain (0 = end)
  //   word 1: PREVI  — back pointer / segment reference
  //   word 2: PROTE  — protection (written to PIT)
  //   word 3: LOGPA  — logical page number of this physical page
  //
  // BPAGL = head of the chain = most recently used page of the segment,
  // NOT necessarily logical page 0.  We must walk PAGLI links until LOGPA=0.
  //
  // physPage = entry_addr / 4  (since entries are 4 words, address = page * 4)
  // physWordAddr = physPage * 1024
  //
  // Entry format at words 0-63, stride 4:
  //   [i*4+0] = start address
  //   [i*4+1] = restart address
  //   [i*4+2] = name pointer (SPIT word address of name, for reference)
  //   [i*4+3] = segment number
  // Terminator: segno==0 and name_ptr==0xffff
  //
  // Name table at word 303: packed bytes, 0x27-separated, in entry order.
  // =========================================================

  // Parse a 512-word block known to contain REECOMT data.
  // Returns array of {name, segno, stadr, restart} or [] on failure.
  function parseREECOMTWords(words) {
    if (!words || words.length < REECOMT_NAME_WORD + 4) return [];

    var entrySegno   = [];
    var entryStart   = [];
    var entryRestart = [];
    for (var i = 0; i < REECOMT_MAX; i++) {
      var start   = words[i*4 + 0];
      var restart = words[i*4 + 1];
      var nameptr = words[i*4 + 2];
      var segno   = words[i*4 + 3];
      if (segno === 0 && nameptr === 0xffff) break;
      if (segno === 0) continue;
      entrySegno.push(segno);
      entryStart.push(start);
      entryRestart.push(restart);
    }
    if (entrySegno.length === 0) return [];

    var names = [];
    var cur   = '';
    outer: for (var w = REECOMT_NAME_WORD; w < words.length; w++) {
      var word  = words[w];
      var bytes = [(word >> 8) & 0xFF, word & 0xFF];
      for (var b = 0; b < 2; b++) {
        var ch = bytes[b];
        if (ch === REECOMT_NAME_SEP) {
          if (cur.length > 0) names.push(cur);
          cur = '';
          if (names.length >= entrySegno.length) break outer;
        } else if (ch === 0) {
          if (cur.length > 0) names.push(cur);
          break outer;
        } else if (ch >= 0x20 && ch < 0x7f) {
          cur += String.fromCharCode(ch);
        }
      }
    }
    if (cur.length > 0) names.push(cur);

    var result = [];
    for (var j = 0; j < entrySegno.length; j++) {
      result.push({
        name:    names[j] || ('REENT-' + entrySegno[j].toString(8).padStart(3, '0')),
        segno:   entrySegno[j],
        stadr:   entryStart[j],
        restart: entryRestart[j]
      });
    }
    return result;
  }

  // Try to read REECOMT directly from the SMD disk buffer.
  // LBA = segfilBase + seg3.madr + relPage
  // segfilBase comes from BLSTX[segfilNum] — NOT zero.
  // Scans all pages of 5OPSEG looking for word 303 = 0x4E52 ('NR' = start of "NRL").
  // Returns Uint16Array (512 words) or null.
  function tryReadREECOMTFromDisk(seg3, segfilBase) {
    if (!seg3) return null;
    var pages = (seg3.segle & 0x3FF) || 32;

    for (var p = 0; p < pages; p++) {
      var lba  = segfilBase + seg3.madr + p;
      var view = readSectors(lba, 1);
      if (!view) continue;
      var w303 = ((view[606] << 8) | view[607]) & 0xFFFF;
      if (w303 !== 0x4E52) continue;
      var words = new Uint16Array(512);
      for (var w = 0; w < 512; w++) {
        words[w] = ((view[w*2] << 8) | view[w*2 + 1]) & 0xFFFF;
      }
      console.log('[reentrant] REECOMT on disk: segfilBase=' + segfilBase +
        ' madr=' + seg3.madr + ' relPage=' + p + ' lba=' + lba);
      return words;
    }
    console.log('[reentrant] disk scan: REECOMT signature not found in ' + pages + ' pages' +
      ' (base=' + segfilBase + ' madr=' + seg3.madr + ')');
    return null;
  }

  function readREECOMTFromPage0(seg3) {
    if (!seg3) return Promise.resolve([]);

    var segfilNum = (seg3.flag >> 13) & 0x7;

    return getSegfilBasesAsync(seg3).then(function(bases) {
      var segfilBase = bases[segfilNum] || 0;
      console.log('[reentrant] readREECOMT: segfil=' + segfilNum + ' base=' + segfilBase +
        ' madr=' + seg3.madr + ' bpagl=' + seg3.bpagl);
      return readREECOMTWithBase(seg3, segfilBase);
    });
  }

  function readREECOMTWithBase(seg3, segfilBase) {
    // No BPAGL → segment not in RAM at all → try disk immediately
    if (!seg3.bpagl) {
      var diskWords = tryReadREECOMTFromDisk(seg3, segfilBase);
      if (diskWords) {
        var r = parseREECOMTWords(diskWords);
        if (r.length > 0) { return Promise.resolve(r); }
      }
      return Promise.resolve([]);
    }

    return sym.readWord(sym.FIXED.CORMB).then(function(cormb) {
      var cormBase = (cormb & 0xFF) << 16;
      var limit = 128;
      var chainPages = [];
      var logpasSeen = [];
      console.log('[reentrant] chain walk: bpagl=' + seg3.bpagl + ' cormBase=0x' + cormBase.toString(16) +
        ' logad=' + seg3.logad + ' (logad&63=' + (seg3.logad & 63) + ')');

      function collectChain(entryAddr) {
        if (entryAddr === 0 || limit-- <= 0) {
          console.log('[reentrant] chain: ' + chainPages.length + ' pages, logpas=[' + logpasSeen.join(',') + ']');
          return Promise.resolve();
        }
        return sym.readBlockPhysical(cormBase + entryAddr, 4).then(function(cme) {
          if (!cme || cme.length < 4) return Promise.resolve();
          var physPage = (entryAddr / 4) | 0;
          chainPages.push(physPage);
          logpasSeen.push(cme[3]);
          return collectChain(cme[0]);
        });
      }

      return collectChain(seg3.bpagl).then(function() {
        if (chainPages.length === 0) return Promise.resolve(-1);

        var idx = 0;
        function scanNext() {
          if (idx >= chainPages.length) return Promise.resolve(-1);
          var pp = chainPages[idx++];
          return sym.readBlockPhysical(pp * 1024 + REECOMT_NAME_WORD, 2).then(function(w) {
            if (w && w.length >= 1 && w[0] === 0x4E52) {
              console.log('[reentrant] REECOMT signature at physPage=' + pp + ' word303=0x' + w[0].toString(16));
              return Promise.resolve(pp);
            }
            return scanNext();
          });
        }
        return scanNext();
      });

    }).then(function(physPage) {
      if (physPage < 0) {
        // REECOMT page not in RAM — try disk (evicted dirty page)
        var diskWords = tryReadREECOMTFromDisk(seg3, segfilBase);
        if (diskWords) {
          var r = parseREECOMTWords(diskWords);
          if (r.length > 0) {
            console.log('[reentrant] REECOMT from disk: ' + r.length + ' entries');
            return r;
          }
        }
        return [];
      }

      var physWordAddr = physPage * 1024;
      console.log('[reentrant] 5OPSEG page 0: physPage=' + physPage);

      return sym.readBlockPhysical(physWordAddr, 512).then(function(words) {
        if (!words || words.length < REECOMT_NAME_WORD + 4) {
          console.warn('[reentrant] page read failed, physPage=' + physPage);
          return [];
        }
        var result = parseREECOMTWords(words);
        if (result.length === 0) {
          console.warn('[reentrant] no entries parsed, physPage=' + physPage);
          return [];
        }
        console.log('[reentrant] REECOMT physPage=' + physPage + ': ' + result.length + ' entries');
        return result;
      });
    });
  }

  // =========================================================
  // Main table reader
  // =========================================================

  function readReentrantTable() {
    return readSegTableData().then(function(result) {
      var seg3 = result.seg3;

      if (!seg3) {
        console.warn('[reentrant] seg3 (5OPSEG) not found — SINTRAN not running?');
        return [];
      }

      return readREECOMTFromPage0(seg3).then(function(entries) {
        if (entries.length > 0) return entries;

        // Fallback: list reentrant segments with generated names
        console.warn('[reentrant] REECOMT empty — falling back to FLAG-4 segment scan');
        var fallback = [];
        for (var j = 0; j < result.reentrantSegs.length; j++) {
          var s = result.reentrantSegs[j];
          fallback.push({
            name:    'REENT-' + s.segNum.toString(8).padStart(3, '0'),
            segno:   s.segNum,
            stadr:   0,
            restart: 0
          });
        }
        return fallback;
      });
    });
  }

  // =========================================================
  // Rendering
  // =========================================================

  function escHtml(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }
  function escAttr(s) {
    return String(s).replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  function renderTable(entries) {
    var body = document.getElementById('reentrant-table-body');
    if (!body) return;

    if (entries.length === 0) {
      body.innerHTML = '<div style="padding:12px;color:rgba(160,175,210,0.5);font-style:italic;">No reentrant programs found — SINTRAN may not be running or no SHARED segments exist</div>';
      return;
    }

    var html = '<table class="proc-table"><thead><tr>' +
      '<th>Name</th><th>Seg#</th><th>Start</th><th>Restart</th>' +
      '</tr></thead><tbody>';

    for (var i = 0; i < entries.length; i++) {
      var e = entries[i];
      html += '<tr class="proc-row" data-reent-idx="' + i + '">' +
        '<td>' + escHtml(e.name) + '</td>' +
        '<td>' + e.segno.toString(8).padStart(3, '0') + '</td>' +
        '<td>' + e.stadr.toString(8).padStart(6, '0') + '</td>' +
        '<td>' + e.restart.toString(8).padStart(6, '0') + '</td>' +
        '</tr>';
    }

    html += '</tbody></table>';
    body.innerHTML = html;

    // Attach click handlers
    var rows = body.querySelectorAll('.proc-row');
    for (var j = 0; j < rows.length; j++) {
      rows[j].addEventListener('click', function() {
        var idx = parseInt(this.getAttribute('data-reent-idx'));
        selectEntry(idx, entries);
      });
    }

    // Restore selection
    if (selectedEntry) {
      for (var k = 0; k < entries.length; k++) {
        if (entries[k].name === selectedEntry.name) {
          selectEntry(k, entries);
          break;
        }
      }
    }
  }

  function selectEntry(idx, entries) {
    var entry = entries[idx];
    if (!entry) return;
    selectedEntry = entry;

    var rows = document.querySelectorAll('#reentrant-table-body .proc-row');
    for (var i = 0; i < rows.length; i++) {
      rows[i].classList.remove('proc-selected');
      if (parseInt(rows[i].getAttribute('data-reent-idx')) === idx) {
        rows[i].classList.add('proc-selected');
      }
    }

    var panel = document.getElementById('reentrant-detail-panel');
    if (!panel) return;
    panel.style.display = 'block';

    // Find matching segment entry from our cached segment map
    var seg = cachedSegmentMap[entry.segno] || null;
    // Also check getLastSegments() in case Segment Table is open
    if (!seg && typeof window.getLastSegments === 'function') {
      var segs = window.getLastSegments();
      for (var j = 0; j < segs.length; j++) {
        if (segs[j].segNum === entry.segno) { seg = segs[j]; break; }
      }
    }

    var html = '<div class="seg-detail-title">' + escHtml(entry.name) + '</div>';
    html += '<div class="seg-detail-field"><span class="seg-detail-field-name">Segment</span>' +
      '<span class="seg-detail-field-value">' + entry.segno.toString(8).padStart(3, '0') + '</span></div>';
    html += '<div class="seg-detail-field"><span class="seg-detail-field-name">Start</span>' +
      '<span class="seg-detail-field-value">' + entry.stadr.toString(8).padStart(6, '0') + '</span></div>';
    html += '<div class="seg-detail-field"><span class="seg-detail-field-name">Restart</span>' +
      '<span class="seg-detail-field-value">' + entry.restart.toString(8).padStart(6, '0') + '</span></div>';

    if (seg && typeof window.renderSegDetailHtml === 'function') {
      html += '<div class="seg-detail-section">Segment Details</div>';
      html += window.renderSegDetailHtml(seg);  // already includes Open Disassembler button
    } else {
      if (entry.segno) {
        html += '<div class="seg-detail-section" style="color:rgba(160,175,210,0.4);">Segment details not available — open Segment Table first</div>';
      }
      html += '<button class="seg-detail-btn" id="reent-disasm-btn">Open Disassembler</button>';
    }

    panel.innerHTML = html;

    // Kick off async chain walk if segment is in RAM
    if (seg && seg.bpagl && typeof window.loadSegCoreMapChain === 'function') {
      window.loadSegCoreMapChain(seg);
    }

    // Wire "Open Disassembler" button — works whether added by renderSegDetailHtml or by us.
    // querySelectorAll catches both cases; last one wins (renderSegDetailHtml appends at bottom).
    var disasmBtns = panel.querySelectorAll('.seg-detail-btn');
    for (var k = 0; k < disasmBtns.length; k++) {
      var btnEl = disasmBtns[k];
      if (btnEl.textContent.indexOf('Disassembler') < 0) continue;
      (function(btn) {
        btn.onclick = function() {
          if (typeof window.segDisasmOpenWindowForReentrant === 'function') {
            window.segDisasmOpenWindowForReentrant(entry.name, entry.segno, entry.stadr, entry.restart);
          } else if (typeof window.segDisasmOpenWindow === 'function') {
            window.segDisasmOpenWindow(entry.segno);
          }
        };
      })(btnEl);
    }
  }

  // =========================================================
  // Refresh
  // =========================================================

  function refresh() {
    if (!sintranState || !sintranState.detected) return;
    readReentrantTable().then(function(entries) {
      lastEntries = entries;
      renderTable(entries);
    }).catch(function(e) {
      console.error('[reentrant] read error:', e);
      var body = document.getElementById('reentrant-table-body');
      if (body) {
        body.innerHTML = '<div style="padding:12px;color:rgba(255,80,80,0.85);">' +
          '<b>Failed to read reentrant programs</b><br>' +
          '<div style="margin-top:8px;font-family:monospace;font-size:11px;background:rgba(0,0,0,0.3);padding:8px;border-left:3px solid rgba(255,80,80,0.6);white-space:pre-wrap;word-break:break-word;">' +
          escHtml(e.message || String(e)) +
          '</div>' +
          '<div style="margin-top:8px;font-size:10px;color:rgba(160,175,210,0.6);">' +
          'Filter browser console by <b>[reentrant]</b> for details.</div></div>';
      }
    });
  }

  function startAutoRefresh() {
    stopAutoRefresh();
    refreshTimer = setInterval(refresh, 5000);
  }

  function stopAutoRefresh() {
    if (refreshTimer) { clearInterval(refreshTimer); refreshTimer = null; }
  }

  // =========================================================
  // Window show/hide
  // =========================================================

  function showWindow() {
    var win = document.getElementById('reentrant-window');
    if (win) {
      win.style.display = 'flex';
      refresh();
      startAutoRefresh();
    }
  }

  function hideWindow() {
    var win = document.getElementById('reentrant-window');
    if (win) win.style.display = 'none';
    stopAutoRefresh();
  }

  // =========================================================
  // Copy as Markdown
  // =========================================================

  function copyAsMarkdown() {
    if (lastEntries.length === 0) return;

    var md = '| Name | Seg# | Start | Restart |\n';
    md += '|------|------|-------|--------|\n';

    for (var i = 0; i < lastEntries.length; i++) {
      var e = lastEntries[i];
      md += '| ' + e.name +
        ' | ' + e.segno.toString(8).padStart(3, '0') +
        ' | ' + e.stadr.toString(8).padStart(6, '0') +
        ' | ' + e.restart.toString(8).padStart(6, '0') +
        ' |\n';
    }

    navigator.clipboard.writeText(md).then(function() {
      var btn = document.getElementById('reentrant-copy');
      if (btn) {
        btn.classList.add('copied');
        setTimeout(function() { btn.classList.remove('copied'); }, 1200);
      }
    });
  }

  // =========================================================
  // Event handlers
  // =========================================================

  var closeBtn = document.getElementById('reentrant-close');
  if (closeBtn) closeBtn.addEventListener('click', hideWindow);

  var copyBtn = document.getElementById('reentrant-copy');
  if (copyBtn) copyBtn.addEventListener('click', copyAsMarkdown);

  var refreshBtn = document.getElementById('reentrant-refresh');
  if (refreshBtn) refreshBtn.addEventListener('click', refresh);

  // =========================================================
  // Export
  // =========================================================
  window.reentrantShowWindow  = showWindow;
  window.reentrantHideWindow  = hideWindow;
  window.readReentrantTable   = readReentrantTable;
  // Expose discovered SEGFIL0 base so segment-disassembler.js can use it
  // without repeating the discovery if reentrant.js already did it.
  window.getDiscoveredSegfilBase0 = function() {
    return (segfilBases && segfilBases[0]) || 0;
  };
  // Allow segment-disassembler.js to find segment entries even when the
  // Segment Table window hasn't been opened yet.
  window.getReentrantCachedSegment = function(segNum) { return cachedSegmentMap[segNum] || null; };
  // Return the REECOMT program name for a segment number, or '' if not known.
  // Used by sintran-segments.js to show the reentrant name in the segment table.
  window.getREECOMTNameForSeg = function(segNum) {
    for (var i = 0; i < lastEntries.length; i++) {
      if (lastEntries[i].segno === segNum) return lastEntries[i].name;
    }
    return '';
  };

})();
