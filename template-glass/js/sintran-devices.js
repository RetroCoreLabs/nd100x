//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// sintran-devices.js - I/O Device Inspector
// 4-phase discovery: symbol enumeration, BRESL chains, MQUEU chain, merge.
// GDEVTY classification from TYPRI bits. R/W pair merging. Tabbed category view.
// Hover tooltips on all fields. Copy-to-markdown exports all categories.

(function() {
  'use strict';

  var sym = window.sintranSymbols;
  var FX = sym.FIXED;
  var RT = sym.RT_DESC;
  var DF = sym.IO_DF;
  var TB = sym.TYPRI_BITS;

  var refreshTimer = null;
  var autoRefresh = true;
  var filterLevel = 'initialized';  // all | configured | initialized | active
  var activeTab = null;       // currently selected category name
  var cachedGroups = null;    // last computed groups (for copy-to-markdown)

  // =========================================================
  // Logical device description lookup (loaded from JSON)
  // =========================================================
  var logDevLookup = null;       // loaded JSON: octal-key -> {desc, group, cat}
  var logDevLookupLoading = false;
  var logDevLookupFailed = false;

  function loadLogDevLookup(callback) {
    if (logDevLookup) { if (callback) callback(); return; }
    if (logDevLookupLoading) return;
    if (logDevLookupFailed) { if (callback) callback(); return; }
    logDevLookupLoading = true;
    fetch('data/logical-device-numbers.json')
      .then(function(resp) {
        if (!resp.ok) throw new Error('HTTP ' + resp.status);
        return resp.json();
      })
      .then(function(data) {
        logDevLookup = data;
        logDevLookupLoading = false;
        console.log('[LogDev] Loaded device description lookup (' + Object.keys(data).length + ' entries)');
        if (callback) callback();
      })
      .catch(function(err) {
        logDevLookupLoading = false;
        logDevLookupFailed = true;
        console.warn('[LogDev] Failed to load logical-device-numbers.json:', err.message);
        if (callback) callback();
      });
  }

  // Look up a logical device number (decimal integer) in the JSON table.
  // Returns {desc, group, cat} or null.
  function lookupLogDev(logDevNo) {
    if (logDevNo < 0 || !logDevLookup) return null;
    var key = logDevNo.toString(8);  // decimal -> octal string, no leading zeros
    return logDevLookup[key] || null;
  }

  // Build a rich tooltip for a device row from its logDevNo
  function buildLogDevTooltip(logDevNo) {
    var entry = lookupLogDev(logDevNo);
    if (!entry) return null;
    var octal = logDevNo.toString(8);
    var parts = [
      'LogDev ' + octal + ' (octal) / ' + logDevNo + ' (decimal)',
      'Description: ' + entry.desc,
      'LDNT Group: ' + entry.group,
      'Category: ' + entry.cat
    ];
    return parts.join('\n');
  }

  // =========================================================
  // TYPRI bit labels for tooltip display (indexed by bit position)
  // =========================================================
  var TYPRI_BIT_LABELS = [
    null, null, 'CLDV', 'NORE', 'BAD/TAD', 'TERM', 'IBDV', 'HDMA',
    'FLOP', 'MT', 'M144', 'SPLI', 'ISET', 'CONC', 'RFIL', 'IOBT'
  ];

  var TYPRI_BIT_DESCS = {
    IOBT: 'Block transfer capable',
    RFIL: 'Remote file access',
    CONC: 'Concurrent I/O',
    ISET: 'I/O initialized',
    SPLI: 'Split datafield (R/W halves)',
    M144: '144-byte block format',
    MT:   'Magnetic tape',
    FLOP: 'Floppy disk',
    HDMA: 'HDMA / X.21',
    IBDV: 'Indexed block device (disk)',
    TERM: 'Terminal',
    'BAD/TAD': 'Terminal Adapter (TAD)',
    NORE: 'No reservation required',
    CLDV: 'Closable device'
  };

  // Column header tooltips
  var COL_TIPS = {
    Name:     'Device symbol name from SYMBOL-2-LIST',
    Address:  'DPIT logical address (page table #7)',
    LogDevNo: 'Logical device number (octal) from LOGDBANK table\nUsed by monitor calls to identify devices',
    Desc:     'Official device description from SINTRAN III manual\nLooked up via logical device number',
    DevCat:   'Device category from SINTRAN III device classification\n(terminal, disk, tape, printer, etc.)',
    Type:     'Primary device type from GDEVTY classification (TYPRI bits)',
    Attr:     'Device attribute flags from TYPRI word',
    Owner:    'RT program that has reserved this device',
    Driver:   'Monitor function entry point (driver code address)',
    ISTAT:    'I/O status word (device-specific operational state)',
    Waiting:  'First RT program waiting to reserve this device'
  };

  // =========================================================
  // 4-Phase Device Discovery
  // =========================================================

  // Returns Promise<Array>
  function discoverDevices() {
    var deviceMap = {};  // keyed by DF address

    // Phase 1: Symbol enumeration (primary, most reliable)
    var phase1;
    if (window.sintranDevNames && window.sintranDevNames.getAllKnownDevices) {
      var known = window.sintranDevNames.getAllKnownDevices();
      var headerPromises = known.map(function(dev) {
        return sym.readBlock(dev.addr, 7).then(function(header) {
          return { dev: dev, header: header };
        });
      });
      phase1 = Promise.all(headerPromises).then(function(results) {
        for (var k = 0; k < results.length; k++) {
          var dev = results[k].dev;
          var header = results[k].header;
          var isZero = true;
          for (var z = 0; z < 7; z++) {
            if (header[z] !== 0) { isZero = false; break; }
          }
          deviceMap[dev.addr] = {
            dfAddr: dev.addr,
            name: dev.name,
            cat: dev.cat,
            desc: dev.desc,
            pairAddr: dev.pairAddr,
            resli: header[DF.RESLI],
            rtres: header[DF.RTRES],
            bwlin: header[DF.BWLIN],
            typri: header[DF.TYPRI],
            istat: header[DF.ISTAT],
            mlink: header[DF.MLINK],
            mfunc: header[DF.MFUNC],
            active: !isZero,
            sources: ['Symbol']
          };
        }
      });
    } else {
      phase1 = Promise.resolve();
    }

    return phase1.then(function() {
      // Phase 2: Walk each used RT-Desc's reservation chain (BRESL -> RESLI)
      var rtInfo = sym.discoverRtTableSync();
      if (rtInfo.base === 0 || rtInfo.count === 0) return;

      var vSym = sym.getVersionSymbols();
      var fbpr = vSym ? vSym.FBPR : 0;
      var lbpr = vSym ? vSym.LBPR : 0;

      return sym.readBlock(rtInfo.base, rtInfo.count * RT.SIZE).then(function(tableData) {
        // Collect all BRESL chain heads
        var chains = [];
        for (var i = 0; i < rtInfo.count; i++) {
          var base = i * RT.SIZE;
          var statu = tableData[base + RT.STATU];
          if (!sym.testBit(statu, sym.STATU_BITS.USED)) continue;
          var bresl = tableData[base + RT.BRESL];
          if (bresl === 0 || bresl === 0xFFFF) continue;
          chains.push({
            ownerName: resolveOwner(rtInfo.base + i * RT.SIZE),
            startAddr: bresl
          });
        }

        // Walk each chain sequentially
        var p = Promise.resolve();
        chains.forEach(function(chain) {
          p = p.then(function() {
            return walkResChain(chain.startAddr, chain.ownerName, rtInfo, fbpr, lbpr, deviceMap);
          });
        });
        return p;
      });
    }).then(function() {
      // Phase 3: Walk Monitor Queue (MQUEU -> MLINK chain)
      return sym.readWord(FX.MQUEU).then(function(mqHead) {
        if (mqHead === 0 || mqHead === 0xFFFF) return;

        function walkMon(addr, count, visited) {
          if (addr === 0 || addr === 0xFFFF || count >= 100 || visited[addr]) return Promise.resolve();
          visited[addr] = true;
          enrichIfKnown(deviceMap, addr, 'MQUEU', null);
          return sym.readWord(addr + DF.MLINK).then(function(next) {
            return walkMon(next, count + 1, visited);
          });
        }

        return walkMon(mqHead, 0, {});
      });
    }).then(function() {
      // Build logical device number reverse map
      return buildDynamicLogDevNoMap().then(function(dynResult) {
        if (dynResult.ok) {
          cachedLogDevMap = dynResult.map;
          logDevMapSource = 'dynamic';
        } else {
          cachedLogDevMap = buildHardcodedLogDevNoMap(deviceMap);
          logDevMapSource = 'hardcoded';
        }
      });
    }).then(function() {
      // Phase 4: Merge and convert to array
      var devices = [];
      for (var addr in deviceMap) {
        if (deviceMap.hasOwnProperty(addr)) {
          var d = deviceMap[addr];
          var gdevty = classifyDevice(d.typri);
          d.devType = d.cat || gdevty;
          d.attrs = collectAttrs(d.typri);
          d.ownerStr = d.ownerStr || resolveOwner(d.rtres);
          d.waitStr = resolveWaiter(d.bwlin, d.dfAddr);
          d.logDevNo = getLogDevNo(d.dfAddr);
          devices.push(d);
        }
      }
      devices.sort(function(a, b) { return a.dfAddr - b.dfAddr; });
      return devices;
    });
  }

  // Walk a reservation chain (BRESL -> RESLI) recursively
  function walkResChain(addr, ownerName, rtInfo, fbpr, lbpr, deviceMap) {
    function step(resAddr, count, visited) {
      if (resAddr === 0 || resAddr === 0xFFFF || count >= 50 || visited[resAddr]) {
        return Promise.resolve();
      }
      // Stop if address is in RT table range (circular chain back to owner)
      if (resAddr >= rtInfo.base && resAddr < rtInfo.base + rtInfo.count * RT.SIZE) {
        return Promise.resolve();
      }
      // Stop if address is in background program range
      if (fbpr && lbpr && resAddr >= fbpr && resAddr < lbpr) {
        return Promise.resolve();
      }
      visited[resAddr] = true;
      enrichIfKnown(deviceMap, resAddr, 'BRESL', ownerName);
      return sym.readWord(resAddr + DF.RESLI).then(function(next) {
        return step(next, count + 1, visited);
      });
    }
    return step(addr, 0, {});
  }

  // Enrich-only: Phase 2/3 chain walks only add owner info to devices already
  // found in Phase 1 (symbol enumeration). This prevents false positives from
  // chain-walked addresses that aren't real I/O datafields (BUG 2).
  function enrichIfKnown(map, dfAddr, source, ownerName) {
    if (!map[dfAddr]) return;  // not a known device — skip
    if (map[dfAddr].sources.indexOf(source) === -1) {
      map[dfAddr].sources.push(source);
    }
    if (ownerName && !map[dfAddr].ownerStr) {
      map[dfAddr].ownerStr = ownerName;
    }
  }

  // =========================================================
  // GDEVTY classification (kernel priority order)
  // =========================================================

  function classifyDevice(typri) {
    if (sym.testBit(typri, TB.TERM)) return 'Terminal';
    if (sym.testBit(typri, TB.BAD))  return 'TAD';
    if (sym.testBit(typri, TB.IBDV)) return 'Disk';
    if (sym.testBit(typri, TB.FLOP)) return 'Floppy';
    if (sym.testBit(typri, TB.MT))   return 'Tape';
    if (sym.testBit(typri, TB.RFIL)) return 'Remote File';
    if (typri !== 0) return 'Other';
    return '';
  }

  function collectAttrs(typri) {
    var attrs = [];
    if (sym.testBit(typri, TB.IOBT)) attrs.push('IOBT');
    if (sym.testBit(typri, TB.CONC)) attrs.push('CONC');
    if (sym.testBit(typri, TB.ISET)) attrs.push('ISET');
    if (sym.testBit(typri, TB.SPLI)) attrs.push('SPLI');
    if (sym.testBit(typri, TB.M144)) attrs.push('M144');
    if (sym.testBit(typri, TB.NORE)) attrs.push('NORE');
    if (sym.testBit(typri, TB.CLDV)) attrs.push('CLDV');
    if (sym.testBit(typri, TB.HDMA)) attrs.push('HDMA');
    return attrs;
  }

  // =========================================================
  // Filter levels
  // =========================================================
  //   all         - show every known address
  //   configured  - TYPRI != 0 OR MFUNC != 0
  //   initialized - MFUNC != 0 AND ISET bit (bit 12) set in TYPRI
  //   active      - MFUNC != 0 AND RTRES != 0 (reserved by a program)

  // Test a single raw device entry against the current filter level
  function devicePassesFilter(d) {
    if (filterLevel === 'all') return true;
    if (filterLevel === 'configured') return d.typri !== 0 || d.mfunc !== 0;
    if (filterLevel === 'initialized') return d.mfunc !== 0 && sym.testBit(d.typri, TB.ISET);
    if (filterLevel === 'active') return d.mfunc !== 0 && d.rtres !== 0;
    return d.active;
  }

  // Filter merged entries: for R/W pairs, pass if either half qualifies
  function applyMergedFilter(mergedDevices) {
    if (filterLevel === 'all') return mergedDevices;
    return mergedDevices.filter(function(m) {
      if (devicePassesFilter(m.rDev)) return true;
      if (m.wDev && devicePassesFilter(m.wDev)) return true;
      return false;
    });
  }

  // =========================================================
  // Logical Device Number reverse map (Section 19, SINTRAN-STRUCTURES)
  // Primary: reads LOGDBANK via CNVRT array + LGTFPHPAGE.
  // Fallback: hardcoded name-to-devno table from Section 19.6.
  // =========================================================

  var cachedLogDevMap = null;
  var logDevMapSource = '';   // 'dynamic' or 'hardcoded' (for diagnostics)
  var logDevDiagDone = false; // only log DPIT diagnostics once

  // Hardcoded logical device numbers from SINTRAN startup tables
  // (Section 19.6: PIOCS, BDISTABLE, MTDITABLE, XMTTABLE, FLOP, CX21TABLE)
  // Keyed by device symbol name -> logical device number (decimal).
  // Only covers statically-configured devices, not dynamic assignments.
  var HARDCODED_DEVNO = {
    // Tape units: controller 1 (STMT1 at 560oct)
    MTDI1: 0o40,  MTDO1: 0o40,
    MTDI2: 0o41,  MTDO2: 0o41,
    MTDI3: 0o25,  MTDO3: 0o25,
    MTDI4: 0o33,  MTDO4: 0o33,
    // Tape units: controller 2 (STMT2 at 1111oct)
    M2DI1: 0o32,  M2DO1: 0o32,
    M2DI2: 0o34,  M2DO2: 0o34,
    M2DI3: 0o563, M2DO3: 0o563,
    M2DI4: 0o564, M2DO4: 0o564,
    // Tape units: controller 3 (STMT3/WIGD2 at 1231oct)
    M3DI1: 0o1232, M3DO1: 0o1232,
    M3DI2: 0o1233, M3DO2: 0o1233,
    M3DI3: 0o1234, M3DO3: 0o1234,
    M3DI4: 0o1235, M3DO4: 0o1235,
    // Tape units: controller 4 (STMT4/WIGDI at 1224oct)
    M4DI1: 0o1225, M4DO1: 0o1225,
    M4DI2: 0o1226, M4DO2: 0o1226,
    M4DI3: 0o1227, M4DO3: 0o1227,
    M4DI4: 0o1230, M4DO4: 0o1230,
    // Floppy controllers
    FDID1: 0o1145, FDID2: 0o1156,
    SFDD1: 0o1145, SFDD2: 0o1156,
    // Ethernet terminals
    ETRN1: 0o2240, ETRN2: 0o2241, ETRN3: 0o2242
  };

  // Read the LDNT (Logical Device Number Table) from physical memory.
  // Algorithm (verified against real 256KW memory dump, 542 entries):
  //   Step 1: LGTFPHPAGE at PHYSICAL 0o170223 -> first page of LDNT (direct read, NO DPIT)
  //   Step 2: CNVRT[32] at DPIT logical 0o4327 -> bank-relative group offsets
  //   Step 3: physical_addr = bankBase + CNVRT[group] -> count + entry pairs
  // Returns {map, ok} where map is dfAddr -> logical device number.
  // Returns Promise<{map, ok}>
  function buildDynamicLogDevNoMap() {
    var result = { map: {}, ok: false };
    var LGTFPHPAGE_ADDR = 0o170223;

    // Step 1: Read LGTFPHPAGE
    return sym.readWordPhysical(LGTFPHPAGE_ADDR).then(function(lgtfphpage) {
      if (lgtfphpage === 0 || lgtfphpage === 0xFFFF) {
        if (!logDevDiagDone) console.log('[LogDev] LGTFPHPAGE is zero or FFFF (LDNT not allocated)');
        logDevDiagDone = true;
        return result;
      }
      if (lgtfphpage > 16384) {
        if (!logDevDiagDone) console.log('[LogDev] LGTFPHPAGE=' + lgtfphpage + ' exceeds max (16384 pages)');
        logDevDiagDone = true;
        return result;
      }

      var tablePhysStart = lgtfphpage * 1024;
      var bank = Math.floor(tablePhysStart / 65536);
      var bankBase = bank * 65536;

      // Step 2: Read CNVRT[32] via DPIT
      var CNVRT_COUNT = 32;
      return sym.readBlock(FX.CNVRT, CNVRT_COUNT).then(function(cnvrt) {
        if (!cnvrt || cnvrt.length === 0) {
          if (!logDevDiagDone) console.log('[LogDev] Failed to read CNVRT array via DPIT');
          logDevDiagDone = true;
          return result;
        }

        if (cnvrt[26] !== 0) {
          if (!logDevDiagDone) {
            console.log('[LogDev] CNVRT validation FAILED: CNVRT[26]=' + sym.toOctal(cnvrt[26]) +
              ' (expected 0, RDLNO reserved group)');
            logDevDiagDone = true;
          }
          return result;
        }

        if (cnvrt[0] !== 0 && (bankBase + cnvrt[0]) !== tablePhysStart) {
          if (!logDevDiagDone) {
            console.log('[LogDev] Cross-check warning: bankBase(' + bankBase + ')+CNVRT[0](' +
              cnvrt[0] + ')=' + (bankBase + cnvrt[0]) + ' != tablePhysStart(' + tablePhysStart + ')');
          }
        }

        // Step 3: Read device entries for each group sequentially
        var map = {};
        var totalEntries = 0;

        // Collect non-zero groups
        var groups = [];
        for (var gi = 0; gi < CNVRT_COUNT; gi++) {
          if (cnvrt[gi] !== 0) groups.push({ index: gi, physAddr: bankBase + cnvrt[gi] });
        }

        // Read each group (chain sequentially to build map)
        var p = Promise.resolve();
        groups.forEach(function(grp) {
          p = p.then(function() {
            return sym.readWordPhysical(grp.physAddr).then(function(count) {
              if (count === 0 || count > 512) return;
              return sym.readBlockPhysical(grp.physAddr, 1 + count * 2).then(function(groupData) {
                for (var ei = 0; ei < count; ei++) {
                  var word0 = groupData[1 + ei * 2];
                  var word1 = groupData[1 + ei * 2 + 1];
                  var logDevNo = grp.index * 64 + ei;
                  if (word0 !== 0) { map[word0] = logDevNo; totalEntries++; }
                  if (word1 !== 0 && word1 !== word0) { map[word1] = logDevNo; totalEntries++; }
                }
              });
            });
          });
        });

        return p.then(function() {
          if (totalEntries < 5) {
            if (!logDevDiagDone) console.log('[LogDev] Too few entries (' + totalEntries + '), dynamic map may be invalid');
            logDevDiagDone = true;
            return result;
          }

          if (!logDevDiagDone) {
            console.log('[LogDev] Dynamic LOGDBANK map: ' + totalEntries + ' entries, LGTFPHPAGE=' +
              sym.toOctal(lgtfphpage) + ' (page ' + lgtfphpage + '), bankBase=' + bankBase);
            logDevDiagDone = true;
          }

          result.map = map;
          result.ok = true;
          return result;
        });
      });
    });
  }

  // Fallback: build map from hardcoded name-to-devno table.
  // Takes the deviceMap from Phase 1 (keyed by address, with .name).
  // Returns dfAddr -> logical devno for devices with known names.
  function buildHardcodedLogDevNoMap(deviceMap) {
    var map = {};
    for (var addr in deviceMap) {
      if (deviceMap.hasOwnProperty(addr)) {
        var name = deviceMap[addr].name;
        if (name && HARDCODED_DEVNO.hasOwnProperty(name)) {
          map[addr] = HARDCODED_DEVNO[name];
        }
      }
    }
    console.log('[LogDev] Hardcoded fallback: ' + Object.keys(map).length + ' devices matched by name');
    return map;
  }

  function getLogDevNo(dfAddr) {
    if (!cachedLogDevMap) return -1;
    var devno = cachedLogDevMap[dfAddr];
    return (devno !== undefined) ? devno : -1;
  }

  // =========================================================
  // Name resolution helpers
  // =========================================================

  function resolveOwner(rtres) {
    if (!rtres || rtres === 0) return '-';
    var rtNum = sym.rtAddrToNumberSync(rtres);
    if (rtNum >= 0 && typeof window.resolveProcessName === 'function') {
      return window.resolveProcessName(rtNum);
    }
    if (rtNum >= 0) return 'RT #' + rtNum;
    return sym.toOctal(rtres);
  }

  // Resolve BWLIN to a waiting process name.
  // dfAddr is the device's own address — BWLIN == dfAddr is a self-pointer
  // sentinel meaning "no waiter" (empty circular queue head).
  function resolveWaiter(bwlin, dfAddr) {
    if (!bwlin || bwlin === 0) return '-';
    if (dfAddr && bwlin === dfAddr) return '-';  // self-pointer sentinel
    var rtNum = sym.rtAddrToNumberSync(bwlin);
    if (rtNum >= 0 && typeof window.resolveProcessName === 'function') {
      return window.resolveProcessName(rtNum);
    }
    if (rtNum >= 0) return 'RT #' + rtNum;
    return sym.toOctal(bwlin);
  }

  // =========================================================
  // TYPRI tooltip: 16-bit binary with labeled bits
  // =========================================================

  function typriTooltip(typri) {
    if (typri === 0) return 'TYPRI: 000000 (no type bits set)';
    var binary = (typri & 0xFFFF).toString(2).padStart(16, '0');
    var parts = ['TYPRI: ' + sym.toOctal(typri) + ' (' + binary + ')'];
    var setBits = [];
    for (var b = 15; b >= 0; b--) {
      if ((typri & (1 << b)) !== 0) {
        var label = TYPRI_BIT_LABELS[b];
        if (label) setBits.push('bit ' + b + ': ' + label);
      }
    }
    if (setBits.length > 0) parts.push(setBits.join(', '));
    return parts.join('\n');
  }

  // =========================================================
  // R/W pair merging
  // =========================================================

  function mergeRWPairs(devices) {
    var merged = [];
    var used = {};

    for (var i = 0; i < devices.length; i++) {
      var d = devices[i];
      if (used[d.dfAddr]) continue;

      // Check if this device has a pair
      if (d.pairAddr !== null && !used[d.pairAddr]) {
        var pair = null;
        for (var j = 0; j < devices.length; j++) {
          if (devices[j].dfAddr === d.pairAddr) { pair = devices[j]; break; }
        }
        if (pair) {
          var rDev, wDev;
          if (d.name && d.name.charAt(d.name.length - 1) === 'R') {
            rDev = d; wDev = pair;
          } else if (d.name && d.name.charAt(d.name.length - 1) === 'W') {
            rDev = pair; wDev = d;
          } else if (d.dfAddr < pair.dfAddr) {
            rDev = d; wDev = pair;
          } else {
            rDev = pair; wDev = d;
          }
          used[rDev.dfAddr] = true;
          used[wDev.dfAddr] = true;

          var mName = rDev.name ? rDev.name.replace(/R$/, '') : sym.toOctal(rDev.dfAddr);
          var mDesc = rDev.desc ? rDev.desc.replace(/ read half$/, '') : null;

          // Logical device number: both halves normally map to same devno
          var mLogDev = (rDev.logDevNo >= 0) ? rDev.logDevNo : wDev.logDevNo;

          merged.push({
            merged: true,
            name: mName,
            desc: mDesc,
            cat: rDev.cat || wDev.cat,
            rDev: rDev,
            wDev: wDev,
            active: rDev.active || wDev.active,
            devType: rDev.devType || wDev.devType,
            attrs: rDev.attrs.length >= wDev.attrs.length ? rDev.attrs : wDev.attrs,
            typri: rDev.typri || wDev.typri,
            mfunc: rDev.mfunc || wDev.mfunc,
            logDevNo: mLogDev,
            sources: rDev.sources.concat(wDev.sources)
          });
          continue;
        }
      }

      used[d.dfAddr] = true;
      merged.push({
        merged: false,
        name: d.name || sym.toOctal(d.dfAddr),
        desc: d.desc,
        cat: d.cat,
        rDev: d,
        wDev: null,
        active: d.active,
        devType: d.devType,
        attrs: d.attrs,
        typri: d.typri,
        mfunc: d.mfunc,
        logDevNo: d.logDevNo,
        sources: d.sources
      });
    }

    return merged;
  }

  // =========================================================
  // Grouping by category
  // =========================================================

  function groupByCategory(mergedDevices) {
    var catOrder = (window.sintranDevNames && window.sintranDevNames.CATEGORY_ORDER)
      ? window.sintranDevNames.CATEGORY_ORDER
      : ['Disk', 'SCSI', 'Tape', 'Floppy', 'HDLC', 'ND-500', 'Multi-Net', 'CDF', 'Terminal', 'Block Device', 'Other'];

    var groups = {};
    for (var i = 0; i < mergedDevices.length; i++) {
      var d = mergedDevices[i];
      var cat = d.cat || d.devType || 'Other';
      if (!groups[cat]) groups[cat] = [];
      groups[cat].push(d);
    }

    var result = [];
    for (var c = 0; c < catOrder.length; c++) {
      var catName = catOrder[c];
      if (groups[catName] && groups[catName].length > 0) {
        result.push({ category: catName, devices: groups[catName] });
        delete groups[catName];
      }
    }
    for (var rem in groups) {
      if (groups.hasOwnProperty(rem) && groups[rem].length > 0) {
        result.push({ category: rem, devices: groups[rem] });
      }
    }
    return result;
  }

  // =========================================================
  // Rendering: tabs + table
  // =========================================================

  function renderDeviceTable(devices) {
    var tabBar = document.getElementById('io-dev-tabs');
    var body = document.getElementById('io-devices-body');
    if (!tabBar || !body) return;

    // Merge R/W pairs first, then filter (so either half passing keeps the pair)
    var merged = mergeRWPairs(devices);
    var filtered = applyMergedFilter(merged);
    var groups = groupByCategory(filtered);
    cachedGroups = groups;  // store for copy-to-markdown

    if (groups.length === 0) {
      tabBar.innerHTML = '';
      body.innerHTML = '<div style="padding:12px;color:rgba(160,175,210,0.5);font-style:italic;">No I/O devices found</div>';
      return;
    }

    // If active tab no longer exists in groups, reset to first
    var tabExists = false;
    for (var t = 0; t < groups.length; t++) {
      if (groups[t].category === activeTab) { tabExists = true; break; }
    }
    if (!tabExists) activeTab = groups[0].category;

    // Render tab bar
    var tabHtml = '';
    for (var g = 0; g < groups.length; g++) {
      var grp = groups[g];
      var isActive = (grp.category === activeTab) ? ' active' : '';
      tabHtml += '<button class="io-dev-tab' + isActive + '" data-cat="' + escAttr(grp.category) + '">' +
        escHtml(grp.category) + '<span class="io-tab-count">' + grp.devices.length + '</span></button>';
    }
    tabBar.innerHTML = tabHtml;

    // Attach tab click handlers
    var tabs = tabBar.querySelectorAll('.io-dev-tab');
    for (var ti = 0; ti < tabs.length; ti++) {
      tabs[ti].addEventListener('click', onTabClick);
    }

    // Render active tab content
    var activeGroup = null;
    for (var ag = 0; ag < groups.length; ag++) {
      if (groups[ag].category === activeTab) { activeGroup = groups[ag]; break; }
    }

    if (activeGroup) {
      body.innerHTML = buildTableHtml(activeGroup.devices);
    } else {
      body.innerHTML = '';
    }
  }

  function onTabClick(e) {
    var cat = e.currentTarget.getAttribute('data-cat');
    if (!cat || cat === activeTab) return;
    activeTab = cat;

    // Update tab active states
    var tabBar = document.getElementById('io-dev-tabs');
    if (tabBar) {
      var tabs = tabBar.querySelectorAll('.io-dev-tab');
      for (var i = 0; i < tabs.length; i++) {
        if (tabs[i].getAttribute('data-cat') === cat) {
          tabs[i].classList.add('active');
        } else {
          tabs[i].classList.remove('active');
        }
      }
    }

    // Render the selected group's table
    var body = document.getElementById('io-devices-body');
    if (!body || !cachedGroups) return;
    for (var g = 0; g < cachedGroups.length; g++) {
      if (cachedGroups[g].category === cat) {
        body.innerHTML = buildTableHtml(cachedGroups[g].devices);
        return;
      }
    }
    body.innerHTML = '';
  }

  function buildTableHtml(devList) {
    var html = '<table class="proc-table"><thead><tr>' +
      '<th title="' + escAttr(COL_TIPS.Name) + '">Name</th>' +
      '<th title="' + escAttr(COL_TIPS.Address) + '">Address</th>' +
      '<th title="' + escAttr(COL_TIPS.LogDevNo) + '">LogDev</th>' +
      '<th title="' + escAttr(COL_TIPS.Desc) + '">Description</th>' +
      '<th title="' + escAttr(COL_TIPS.DevCat) + '">Category</th>' +
      '<th title="' + escAttr(COL_TIPS.Type) + '">Type</th>' +
      '<th title="' + escAttr(COL_TIPS.Attr) + '">Attributes</th>' +
      '<th title="' + escAttr(COL_TIPS.Owner) + '">Owner</th>' +
      '<th title="' + escAttr(COL_TIPS.Driver) + '">Driver</th>' +
      '<th title="' + escAttr(COL_TIPS.ISTAT) + '">ISTAT</th>' +
      '<th title="' + escAttr(COL_TIPS.Waiting) + '">Waiting</th>' +
      '</tr></thead><tbody>';

    for (var i = 0; i < devList.length; i++) {
      html += buildDeviceRow(devList[i]);
    }

    html += '</tbody></table>';
    return html;
  }

  // Category CSS class mapping for row left-border color coding
  var CAT_CSS_CLASS = {
    'terminal':      'io-cat-terminal',
    'disk':          'io-cat-disk',
    'tape':          'io-cat-tape',
    'printer':       'io-cat-printer',
    'communication': 'io-cat-comm',
    'network':       'io-cat-network',
    'semaphore':     'io-cat-semaphore',
    'process':       'io-cat-process',
    'modem':         'io-cat-comm',
    'tad':           'io-cat-terminal',
    'spooling':      'io-cat-printer',
    'internal':      'io-cat-system',
    'system':        'io-cat-system',
    'batch':         'io-cat-process',
    'character':     'io-cat-char',
    'file':          'io-cat-disk',
    'user':          'io-cat-char'
  };

  function buildDeviceRow(d) {
    var inactiveClass = d.active ? '' : ' io-inactive';

    // Lookup device description from JSON (by logDevNo)
    var ldEntry = lookupLogDev(d.logDevNo);
    var rowTooltip = buildLogDevTooltip(d.logDevNo);

    // Category CSS class for left-border color coding
    var catClass = '';
    if (ldEntry && ldEntry.cat) {
      catClass = ' ' + (CAT_CSS_CLASS[ldEntry.cat] || '');
    }

    // Name cell with tooltip
    var nameTitle = d.desc || d.name || '';
    if (!d.active) nameTitle += nameTitle ? ' (not configured)' : 'Not configured';
    var nameHtml = '<td class="io-name-cell' + inactiveClass + '" title="' + escAttr(nameTitle) + '">' + escHtml(d.name) + '</td>';

    // Address cell
    var addrHtml;
    if (d.merged) {
      addrHtml = '<td title="DPIT logical addresses (page table #7)&#10;Read half:  ' +
        sym.toOctal(d.rDev.dfAddr) + '&#10;Write half: ' + sym.toOctal(d.wDev.dfAddr) + '">' +
        sym.toOctal(d.rDev.dfAddr) + '</td>';
    } else {
      addrHtml = '<td title="DPIT logical address (page table #7): ' +
        sym.toOctal(d.rDev.dfAddr) + '">' + sym.toOctal(d.rDev.dfAddr) + '</td>';
    }

    // Logical Device Number cell
    var logDevStr = (d.logDevNo >= 0) ? sym.toOctal(d.logDevNo) : '-';
    var logDevTip = (d.logDevNo >= 0)
      ? 'Logical device number ' + sym.toOctal(d.logDevNo) + ' (octal)\nUsed by SINTRAN monitor calls (RESRV, OPEN, etc.)'
      : 'Not found in LOGDBANK table';
    if (rowTooltip) logDevTip += '\n\n' + rowTooltip;
    var logDevHtml = '<td title="' + escAttr(logDevTip) + '">' + logDevStr + '</td>';

    // Description cell (from JSON lookup)
    var descStr = ldEntry ? ldEntry.desc : '';
    var descTip = ldEntry
      ? ldEntry.desc + '\nLDNT Group: ' + ldEntry.group
      : 'No description available (device not in lookup table)';
    var descHtml = '<td class="io-desc-cell" title="' + escAttr(descTip) + '">' + escHtml(descStr) + '</td>';

    // Category cell (from JSON lookup)
    var devCatStr = ldEntry ? ldEntry.cat : '';
    var devCatDisplay = devCatStr ? (devCatStr.charAt(0).toUpperCase() + devCatStr.slice(1)) : '';
    var devCatTip = ldEntry
      ? 'SINTRAN device category: ' + ldEntry.cat + '\nLDNT Group: ' + ldEntry.group
      : '';
    var devCatCss = devCatStr ? (CAT_CSS_CLASS[devCatStr] || '') : '';
    var devCatHtml = '<td title="' + escAttr(devCatTip) + '">' +
      (devCatDisplay ? '<span class="io-cat-badge ' + devCatCss + '">' + escHtml(devCatDisplay) + '</span>' : '') +
      '</td>';

    // Type cell with TYPRI tooltip
    var typeTitle = escAttr(typriTooltip(d.typri));
    var typeHtml = '<td title="' + typeTitle + '">' + escHtml(d.devType) + '</td>';

    // Attributes as badges
    var attrHtml = '<td>';
    for (var a = 0; a < d.attrs.length; a++) {
      var attrName = d.attrs[a];
      var attrDesc = TYPRI_BIT_DESCS[attrName] || attrName;
      attrHtml += '<span class="io-attr-badge" title="' + escAttr(attrDesc) + '">' + attrName + '</span>';
    }
    attrHtml += '</td>';

    // Owner
    var ownerR = d.rDev ? (d.rDev.ownerStr || resolveOwner(d.rDev.rtres)) : '-';
    var ownerW = d.wDev ? (d.wDev.ownerStr || resolveOwner(d.wDev.rtres)) : null;
    var ownerStr;
    if (d.merged && ownerW && ownerR !== ownerW && ownerW !== '-') {
      ownerStr = ownerR + ' / ' + ownerW;
    } else {
      ownerStr = ownerR;
    }
    var ownerTip = ownerStr === '-'
      ? 'No process has reserved this device'
      : 'Reserved by RT program: ' + ownerStr;
    if (d.merged) {
      var orR = d.rDev ? (d.rDev.ownerStr || resolveOwner(d.rDev.rtres)) : '-';
      var orW = d.wDev ? (d.wDev.ownerStr || resolveOwner(d.wDev.rtres)) : '-';
      ownerTip += '\nRead half owner: ' + orR + '\nWrite half owner: ' + orW;
    }
    var ownerHtml = '<td title="' + escAttr(ownerTip) + '">' + escHtml(ownerStr) + '</td>';

    // Driver (MFUNC)
    var mfuncStr = d.mfunc ? sym.toOctal(d.mfunc) : '-';
    var driverTip = d.mfunc
      ? 'Monitor function (driver) at address ' + sym.toOctal(d.mfunc)
      : 'No driver attached (MFUNC = 0)';
    var driverHtml = '<td title="' + escAttr(driverTip) + '">' + mfuncStr + '</td>';

    // ISTAT
    var istatR = d.rDev ? d.rDev.istat : 0;
    var istatW = d.wDev ? d.wDev.istat : 0;
    var istatStr;
    if (d.merged && istatW && istatR !== istatW) {
      istatStr = sym.toOctal(istatR) + '/' + sym.toOctal(istatW);
    } else {
      istatStr = istatR ? sym.toOctal(istatR) : '-';
    }
    var istatTip = 'I/O status word (device-specific operational state)';
    if (d.merged) {
      istatTip += '\nRead half ISTAT: ' + (istatR ? sym.toOctal(istatR) : '0');
      istatTip += '\nWrite half ISTAT: ' + (istatW ? sym.toOctal(istatW) : '0');
    } else if (istatR) {
      istatTip += ': ' + sym.toOctal(istatR);
    }
    var istatHtml = '<td title="' + escAttr(istatTip) + '">' + istatStr + '</td>';

    // Waiting
    var waitR = d.rDev ? resolveWaiter(d.rDev.bwlin, d.rDev.dfAddr) : '-';
    var waitW = d.wDev ? resolveWaiter(d.wDev.bwlin, d.wDev.dfAddr) : null;
    var waitStr;
    if (d.merged && waitW && waitR !== waitW && waitW !== '-') {
      waitStr = waitR + ' / ' + waitW;
    } else {
      waitStr = waitR;
    }
    var waitTip = waitStr === '-'
      ? 'No process waiting to reserve this device'
      : 'Waiting to reserve: ' + waitStr;
    if (d.merged && waitW) {
      waitTip += '\nRead half waiter: ' + waitR + '\nWrite half waiter: ' + waitW;
    }
    var waitHtml = '<td title="' + escAttr(waitTip) + '">' + escHtml(waitStr) + '</td>';

    return '<tr class="proc-row' + inactiveClass + catClass + '">' +
      nameHtml + addrHtml + logDevHtml + descHtml + devCatHtml + typeHtml + attrHtml +
      ownerHtml + driverHtml + istatHtml + waitHtml +
      '</tr>';
  }

  function escHtml(s) {
    if (!s) return '';
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  function escAttr(s) {
    if (!s) return '';
    return s.replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/\n/g, '&#10;');
  }

  // =========================================================
  // Copy to Markdown (all categories, respecting show-all filter)
  // =========================================================

  function copyAsMarkdown() {
    if (!cachedGroups || cachedGroups.length === 0) return;

    var lines = [];
    lines.push('| Name | Address | LogDev | Description | Category | Type | Attributes | Owner | Driver | ISTAT | Waiting |');
    lines.push('|------|---------|--------|-------------|----------|------|------------|-------|--------|-------|---------|');

    for (var g = 0; g < cachedGroups.length; g++) {
      var grp = cachedGroups[g];
      lines.push('| **' + grp.category + ' (' + grp.devices.length + ')** ||||||||||');
      for (var i = 0; i < grp.devices.length; i++) {
        var d = grp.devices[i];
        var addr = d.rDev ? sym.toOctal(d.rDev.dfAddr) : '-';
        var logDev = (d.logDevNo >= 0) ? sym.toOctal(d.logDevNo) : '-';
        var ldEntry = lookupLogDev(d.logDevNo);
        var descStr = ldEntry ? ldEntry.desc : '-';
        var devCatStr = ldEntry ? ldEntry.cat : '-';
        var attrStr = d.attrs.length > 0 ? d.attrs.join(' ') : '-';
        var ownerR = d.rDev ? (d.rDev.ownerStr || resolveOwner(d.rDev.rtres)) : '-';
        var ownerW = d.wDev ? (d.wDev.ownerStr || resolveOwner(d.wDev.rtres)) : null;
        var owner = (d.merged && ownerW && ownerR !== ownerW && ownerW !== '-')
          ? ownerR + ' / ' + ownerW : ownerR;
        var mfunc = d.mfunc ? sym.toOctal(d.mfunc) : '-';
        var istatR = d.rDev ? d.rDev.istat : 0;
        var istatW = d.wDev ? d.wDev.istat : 0;
        var istat = (d.merged && istatW && istatR !== istatW)
          ? sym.toOctal(istatR) + '/' + sym.toOctal(istatW)
          : (istatR ? sym.toOctal(istatR) : '-');
        var waitR = d.rDev ? resolveWaiter(d.rDev.bwlin, d.rDev.dfAddr) : '-';
        var waitW = d.wDev ? resolveWaiter(d.wDev.bwlin, d.wDev.dfAddr) : null;
        var wait = (d.merged && waitW && waitR !== waitW && waitW !== '-')
          ? waitR + ' / ' + waitW : waitR;
        lines.push('| ' + (d.name || '-') + ' | ' + addr + ' | ' + logDev + ' | ' + descStr +
          ' | ' + devCatStr + ' | ' + (d.devType || '-') +
          ' | ' + attrStr + ' | ' + owner + ' | ' + mfunc + ' | ' + istat + ' | ' + wait + ' |');
      }
    }

    var text = lines.join('\n');
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(function() {
        showCopyFeedback();
      });
    }
  }

  function showCopyFeedback() {
    var btn = document.getElementById('io-devices-copy');
    if (!btn) return;
    var origTitle = btn.title;
    btn.title = 'Copied!';
    btn.style.color = '#4fc3f7';
    setTimeout(function() {
      btn.title = origTitle;
      btn.style.color = '';
    }, 1500);
  }

  // =========================================================
  // Refresh
  // =========================================================

  function refreshDevices() {
    if (!sintranState || !sintranState.detected) return;
    sym.invalidateRtCache();
    // Populate RT table cache first, then discover devices
    sym.discoverRtTable().then(function() {
      return discoverDevices();
    }).then(function(devices) {
      renderDeviceTable(devices);
    });
  }

  function startAutoRefresh() {
    stopAutoRefresh();
    if (autoRefresh) {
      refreshTimer = setInterval(refreshDevices, 2000);
    }
  }

  function stopAutoRefresh() {
    if (refreshTimer) {
      clearInterval(refreshTimer);
      refreshTimer = null;
    }
  }

  // =========================================================
  // Window show/hide
  // =========================================================

  function showWindow() {
    var win = document.getElementById('io-devices-window');
    if (win) {
      win.style.display = 'flex';
      logDevDiagDone = false;  // reset so we get fresh diagnostics on reopen
      // Load device description JSON on first open, then refresh
      loadLogDevLookup(function() {
        refreshDevices();
        startAutoRefresh();
      });
    }
  }

  function hideWindow() {
    var win = document.getElementById('io-devices-window');
    if (win) win.style.display = 'none';
    stopAutoRefresh();
  }

  // =========================================================
  // Event handlers
  // =========================================================

  var closeBtn = document.getElementById('io-devices-close');
  if (closeBtn) closeBtn.addEventListener('click', hideWindow);

  var refreshBtn = document.getElementById('io-devices-refresh');
  if (refreshBtn) refreshBtn.addEventListener('click', refreshDevices);

  var copyBtn = document.getElementById('io-devices-copy');
  if (copyBtn) copyBtn.addEventListener('click', copyAsMarkdown);

  var autoRefreshCb = document.getElementById('io-auto-refresh');
  if (autoRefreshCb) {
    autoRefreshCb.addEventListener('change', function() {
      autoRefresh = this.checked;
      if (autoRefresh) startAutoRefresh();
      else stopAutoRefresh();
    });
  }

  var filterSelect = document.getElementById('io-filter-level');
  if (filterSelect) {
    filterSelect.addEventListener('change', function() {
      filterLevel = this.value;
      refreshDevices();
    });
  }

  // =========================================================
  // Export
  // =========================================================
  window.ioDevicesShowWindow = showWindow;
  window.ioDevicesHideWindow = hideWindow;
})();
