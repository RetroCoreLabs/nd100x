//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// test_symbol_tables.js - Unit tests for SINTRAN version detection and
// symbol-table-driven RT program name resolution.
//
// Tests the real production modules (js/sintran.js, js/sintran-symbols.js,
// js/sintran-rt-names.js) against the real shipped symbol tables under
// data/symbols/{K03,L07,M06}/.
//
// Run from the template-glass directory:
//   node js/tests/test_symbol_tables.js

'use strict';

var fs = require('fs');
var path = require('path');
var vm = require('vm');

var GLASS_ROOT = path.join(__dirname, '..', '..');

// ---------------------------------------------------------------
// Minimal browser environment stubs
// ---------------------------------------------------------------
global.window = global;
global.emu = null;
global.document = {
  getElementById: function() { return null; },
  querySelectorAll: function() { return []; }
};
global.fetch = function(url) {
  var p = path.join(GLASS_ROOT, url);
  return Promise.resolve({
    ok: true,
    text: function() { return Promise.resolve(fs.readFileSync(p, 'latin1')); }
  });
};

// Load production modules (plain scripts, evaluated in global context)
function loadScript(rel) {
  var p = path.join(GLASS_ROOT, rel);
  vm.runInThisContext(fs.readFileSync(p, 'utf8'), { filename: p });
}
loadScript('js/sintran.js');
loadScript('js/sintran-symbols.js');
loadScript('js/sintran-rt-names.js');
loadScript('js/sintran-seg-names.js');

// ---------------------------------------------------------------
// Tiny test harness
// ---------------------------------------------------------------
var failures = 0, passes = 0;
function check(desc, got, want) {
  if (got === want) {
    passes++;
    console.log('  PASS ' + desc);
  } else {
    failures++;
    console.log('  FAIL ' + desc + '  (want ' + JSON.stringify(want) +
                ', got ' + JSON.stringify(got) + ')');
  }
}

// ---------------------------------------------------------------
// 1. Version detection from SINVER0 raw memory values
//    SINVER0 low byte = ASCII letter (bit 7 may hold even parity),
//    bits 8-10 = OS type (4=VSX, 5=VSX/500).
// ---------------------------------------------------------------
console.log('--- SINVER0 decoding (sintran.js) ---');
check('L07 VSX/500: 0x05CC -> letter L', extractVersionLetter(0x05CC), 'L');
check('L07 VSX/500: 0x05CC -> osType 5', extractOsType(0x05CC), 5);
check('parity bit stripped: 0x04CB -> K', extractVersionLetter(0x04CB), 'K');
check('VSX: 0x044D -> letter M', extractVersionLetter(0x044D), 'M');
check('VSX: 0x044D -> osType 4', extractOsType(0x044D), 4);
check('non-letter low byte -> empty', extractVersionLetter(0x0430), '');
check('zero word -> empty (SINTRAN not booted)', extractVersionLetter(0), '');

// ---------------------------------------------------------------
// 2. FIXED root pointers (identical across K03/L07/M06)
// ---------------------------------------------------------------
console.log('--- Root pointer cells (sintran-symbols.js) ---');
var FX = sintranSymbols.FIXED;
check('SINVER = 004055', FX.SINVER, 0o4055);
check('RTSTA  = 004020', FX.RTSTA, 0o4020);
check('RTEND  = 004323', FX.RTEND, 0o4323);
check('SGMAX  = 004015', FX.SGMAX, 0o4015);
check('SEGTB  = 004320', FX.SEGTB, 0o4320);
check('SEGST  = 004321', FX.SEGST, 0o4321);
check('RT stride = 22 words (5RTSI=26 octal)', sintranSymbols.RT_DESC.SIZE, 22);

// ---------------------------------------------------------------
// 3. Symbol table loading + RT name resolution per version.
//    RT slot n lives at read(RTSTA) + n*22; its name is the symbol
//    with that exact address. Table bases below are the values the
//    RTSTA cell holds on each version (from the symbol tables:
//    first entry DUMMY).
// ---------------------------------------------------------------
var VERSION_CASES = [
  { letter: 'L', sinver0: 0x05CC, base: 0o012071,
    slots: [[0, 'DUMMY'], [1, 'STSIN'], [3, '1SWAP'], [6, 'DIMWD'],
            [67, 'RTBES'], [217, 'BAK01'], [316, 'BK100'],
            [338, 'BCH01'], [347, 'BCH10']] },
  { letter: 'K', sinver0: 0x04CB, base: 0o057360,
    slots: [[0, 'DUMMY'], [1, 'STSIN'], [6, 'BPTMP'],
            [61, 'RTBES'], [171, 'BAK01'], [288, 'BCH01']] },
  { letter: 'M', sinver0: 0x044D, base: 0o012146,
    slots: [[0, 'DUMMY'], [1, 'STSIN'], [6, 'DIMWD'],
            [69, 'RTBES'], [249, 'BAK01'], [250, 'BAK02']] }
];

function testVersion(vc) {
  console.log('--- Version ' + vc.letter + ': detect + resolve ---');

  // Simulate detection exactly as sintranCheckVersion does
  var letter = extractVersionLetter(vc.sinver0);
  check('SINVER0 ' + vc.sinver0.toString(16) + ' detects ' + vc.letter,
        letter, vc.letter);
  sintranState.detected = true;
  sintranState.versionLetter = letter;

  // Simulate the RT table discovered from RTSTA/RTEND in memory
  sintranSymbols.discoverRtTableSync = function() {
    return { base: vc.base, count: 400 };
  };

  return sintranSymbols.loadSymbolTable(letter).then(function(table) {
    check('symbol table loaded for ' + vc.letter, !!table, true);
    if (!table) return;
    check('table has >3000 symbols', table.count > 3000, true);
    for (var i = 0; i < vc.slots.length; i++) {
      var n = vc.slots[i][0], want = vc.slots[i][1];
      check('slot ' + n + ' -> ' + want, resolveProcessName(n), want);
    }
    // Every named system RT should have a description
    check('description for slot 0 exists',
          typeof resolveProcessDescription(0) === 'string', true);
  });
}

// ---------------------------------------------------------------
// 4. Segment names from the LIST-SEGMENT capture (version-keyed)
//    Names come from a real "@RT-LOADER LIST-SEGMENT" run, NOT from
//    the symbol tables (S3xxx names are absent there - verified).
// ---------------------------------------------------------------
function testSegmentNames() {
  console.log('--- Segment names (LIST-SEGMENT capture) ---');
  sintranState.detected = true;
  sintranState.versionLetter = 'L';
  return sintranSegNames.loadSegmentNames('L').then(function(map) {
    check('L07 segment-name map loaded', !!map, true);
    if (!map) return;
    // Numbers in the capture are octal; API takes decimal
    check('seg 3 (003) -> S3CP', resolveSegmentName(3), 'S3CP');
    check('seg 6 (006) -> S3FS', resolveSegmentName(6), 'S3FS');
    check('seg 19 (023) -> S3DPIT', resolveSegmentName(0o23), 'S3DPIT');
    check('seg 26 (032) -> S3RTACC', resolveSegmentName(0o32), 'S3RTACC');
    check('seg 87 (127) -> S3IMTSE', resolveSegmentName(0o127), 'S3IMTSE');
    // Duplicate number 76: RT-loader lists both S3XMSGP and S3XMK
    check('seg 62 (076) joins duplicate names',
          resolveSegmentName(0o76), 'S3XMSGP/S3XMK');
    // Derived category from the S3 naming convention
    check('S3SFS (012) category Save copy',
          resolveSegmentCategory(0o12), 'Save copy');
    check('S3IMPIT (026) category Image copy',
          resolveSegmentCategory(0o26), 'Image copy');
    check('S3CP category System', resolveSegmentCategory(3), 'System');
    // Unknown segment -> empty
    check('unknown seg 200 -> empty', resolveSegmentName(200), '');
    // Unknown version -> empty
    sintranState.versionLetter = 'Z';
    check('version Z -> empty name', resolveSegmentName(3), '');
    sintranState.versionLetter = 'L';
  });
}

// ---------------------------------------------------------------
// 5. Parser edge cases: marker collisions and fallbacks
// ---------------------------------------------------------------
function testEdgeCases() {
  console.log('--- Edge cases ---');

  // L07: 9LTBP, 2THSS and BCH01 all sit at 030505; the letter-initial
  // name must win over digit-initial markers.
  sintranState.versionLetter = 'L';
  check('collision 030505 resolves to BCH01',
        sintranSymbols.lookupSymbolName(0o030505), 'BCH01');
  // Digit-initial REAL names must survive (1SWAP, 5SWAP)
  check('digit-initial name 1SWAP kept',
        sintranSymbols.lookupSymbolName(0o012173), '1SWAP');
  check('digit-initial name 5SWAP kept',
        sintranSymbols.lookupSymbolName(0o012453), '5SWAP');

  // Unknown version letter -> no table, graceful fallback
  sintranState.versionLetter = 'Z';
  return sintranSymbols.loadSymbolTable('Z').then(function(t) {
    check('unknown version Z -> null table', t, null);
    check('unresolved slot falls back to RT #5', resolveProcessName(5), 'RT #5');
    check('unresolved description is null', resolveProcessDescription(5), null);
  });
}

// ---------------------------------------------------------------
// Run
// ---------------------------------------------------------------
VERSION_CASES.reduce(function(p, vc) {
  return p.then(function() { return testVersion(vc); });
}, Promise.resolve())
.then(testSegmentNames)
.then(testEdgeCases)
.then(function() {
  console.log('');
  console.log(passes + ' passed, ' + failures + ' failed');
  process.exit(failures === 0 ? 0 : 1);
});
