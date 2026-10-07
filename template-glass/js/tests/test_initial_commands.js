//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// test_initial_commands.js - Unit tests for the Initial Commands window
// (js/sintran-initial-commands.js).
//
// Verifies the buffer parser and the full symbol+segment-table location
// pipeline against the repo's real SMD images. Reads only; never writes.
//
// Run from the template-glass directory:
//   node js/tests/test_initial_commands.js

'use strict';

var fs = require('fs');
var path = require('path');
var vm = require('vm');

var GLASS_ROOT = path.join(__dirname, '..', '..');
var REPO_ROOT = path.join(GLASS_ROOT, '..');

global.window = global;
global.document = {
  readyState: 'complete',
  getElementById: function() { return null; },
  querySelectorAll: function() { return []; },
  addEventListener: function() {}
};

function loadScript(rel) {
  var p = path.join(GLASS_ROOT, rel);
  vm.runInThisContext(fs.readFileSync(p, 'utf8'), { filename: p });
}
loadScript('lib/ndfs/ndfs-browser-bundle.js');
loadScript('js/sintran-initial-commands.js');

var failures = 0, passes = 0;
function check(desc, got, want) {
  var g = JSON.stringify(got), w = JSON.stringify(want);
  if (g === w) { passes++; console.log('  PASS ' + desc); }
  else { failures++; console.log('  FAIL ' + desc + '  (want ' + w + ', got ' + g + ')'); }
}

var SIC = window.sintranInitialCommands;

// ---------------------------------------------------------------
// 1. Buffer parser: text + 0x27, word-aligned, lone 0x27 terminates
// ---------------------------------------------------------------
console.log('--- parseBuffer ---');
function buf(str) {
  // "|" marks an explicit NUL pad byte in the fixture
  var out = [];
  for (var i = 0; i < str.length; i++) out.push(str[i] === '|' ? 0 : str.charCodeAt(i));
  while (out.length < 64) out.push(0);
  return new Uint8Array(out);
}
// odd-length command -> NUL pad; even-length -> none (as seen on real L07)
check('two commands with pad', SIC.parseBuffer(buf("AB'|CD'|'"), 0, 64), ['AB', 'CD']);
check('even command needs no pad', SIC.parseBuffer(buf("SET-AVAIL'CC HELLO'|'"), 0, 64),
      ['SET-AVAIL', 'CC HELLO']);
check('lone quote terminates immediately', SIC.parseBuffer(buf("'"), 0, 64), []);
check('parity bit stripped', SIC.parseBuffer(new Uint8Array([0xC1, 0xC2, 0x27, 0x00, 0x27]), 0, 5), ['AB']);
check('non-printable payload rejected', SIC.parseBuffer(new Uint8Array([0x01, 0x02, 0x27, 0x27]), 0, 4), null);

// ---------------------------------------------------------------
// 2. Version symbol table
// ---------------------------------------------------------------
console.log('--- INIBU symbols ---');
check('K03 INIBU = 067172 octal', SIC.INIBU.K, 0o67172);
check('L07 INIBU = 074123 octal', SIC.INIBU.L, 0o74123);
check('M06 INIBU = 102327 octal', SIC.INIBU.M, 0o102327);
check('capacity = 131 words (INIBU..INCOM)', SIC.CAP_BYTES, 262);
check('length cell at INIBU+130 words', SIC.LEN_CELL_BYTES, 260);
check('max text = 254 bytes (NEXIN literal 252 -> (252+2)&~1)', SIC.MAX_TEXT_BYTES, 254);
// M06 is bounded conservatively by CPTSL (a real address) until its literal is read
check('per-version writer bound L07 = 254', SIC.MAX_TEXT_BYTES_BY_VERSION.L, 254);
// M06's NEXIN was disassembled: same literal 252 -> CPTSL is not a bound
check('per-version writer bound M06 = 254 (CPTSL resolved)', SIC.MAX_TEXT_BYTES_BY_VERSION.M, 254);

// ---------------------------------------------------------------
// 2b. Encoder: textLen excludes the terminator (this is the length cell).
//     Values below are the ones observed on the real L07 system.
// ---------------------------------------------------------------
console.log('--- encodeBuffer ---');
var THREE = ['ENTER-DIR,,DI-75-1,0', 'ENTER-DIR,,DI-74-1,0', 'SET-AVAIL'];
check('3 real commands -> textLen 54 (matches live 0x36)', SIC.encodeBuffer(THREE).textLen, 54);
check('+ CC HELLO -> textLen 64 (matches live 0x40)',
      SIC.encodeBuffer(THREE.concat(['CC HELLO'])).textLen, 64);
check('+ CC OFFLINE WRITE OK -> textLen 74 (booted OK)',
      SIC.encodeBuffer(THREE.concat(['CC OFFLINE WRITE OK'])).textLen, 74);
// live L07 accepted 6 commands = 114 bytes: proves the old 88-byte "ACLEA" bound was wrong
check('textLen 114 accepted (live L07 did)',
      SIC.encodeBuffer(THREE.concat(['CC ' + 'A'.repeat(16), 'CC ' + 'B'.repeat(16), 'CC ' + 'C'.repeat(16)])).textLen, 114);
check('encoder uppercases', String.fromCharCode.apply(null, SIC.encodeBuffer(['cc hi']).bytes).slice(0, 5), 'CC HI');
// 254 bytes of text is legal; 256 is not.
check('textLen 254 accepted (at the limit)',
      SIC.encodeBuffer([ 'C'.repeat(253) ]).textLen, 254);   // 253+1 quote = 254, even
check('textLen 256 refused (over the limit)', (function() {
  try { SIC.encodeBuffer([ 'C'.repeat(255) ]); return 'no throw'; }
  catch (e) { return /overflow/.test(e.message) ? 'threw' : e.message; }
})(), 'threw');
check('embedded quote refused', (function() {
  try { SIC.encodeBuffer(["BAD'CMD"]); return 'no throw'; }
  catch (e) { return /invalid/.test(e.message) ? 'threw' : e.message; }
})(), 'threw');
// encode -> decode round trip
var enc = SIC.encodeBuffer(THREE.concat(['CC OFFLINE WRITE OK']));
check('encode->parse round trip', SIC.parseBuffer(enc.bytes, 0, enc.textLen),
      THREE.concat(['CC OFFLINE WRITE OK']));

// ---------------------------------------------------------------
// 3. End-to-end against real images (skipped if absent)
//    Located purely via symbol + segment table - no byte searching.
// ---------------------------------------------------------------
var IMAGES = [
  { file: 'SMD0-L.IMG',   expect: ['ENTER-DIR,,DI-75-1,0', 'ENTER-DIR,,DI-74-1,0', 'SET-AVAIL'] },
  { file: 'SMD0-org.IMG', expect: ['ENTER-DIRECTORY PACK-ONE DISC-75MB-1 0'] }
];

// A real SINTRAN M image, when one is available. Different version, different
// segment-table page and MADR - everything derived, nothing hardcoded.
//
// The path comes from the environment because the image lives OUTSIDE this
// repository, on an archive volume that only one machine has. It used to be
// written in here, which made the check silently dead for everyone else and
// tied a committed test to one developer's drive letters. Set it to run this
// part:
//
//   ND100X_SINTRAN_M_IMAGE=/path/to/BIGDISK0-M.IMG node test_initial_commands.js
var M06_IMAGE = process.env.ND100X_SINTRAN_M_IMAGE || '';
if (M06_IMAGE && fs.existsSync(M06_IMAGE)) {
  console.log('--- BIGDISK0-M.IMG (SINTRAN M, end-to-end) ---');
  var mr = SIC.readInitialCommands(new Uint8Array(fs.readFileSync(M06_IMAGE)));
  check('version auto-detected as M', mr.versionLetter, 'M');
  check('commands', mr.commands, ['ENTER-DIRECTORY PACK-ONE DISC-75MB-1 0']);
  check('command segment 3', mr.segNum, 3);
  check('segment table page 1311 (differs from L07)', mr.segTablePage, 1311);
  check('MADR 1444 (differs from L07)', mr.madr, 1444);
  check('length cell = 40 (38 chars + quote -> pad)', mr.textLen, 40);
  check('length cell offset = byteOffset + 260', mr.lengthCellOffset, mr.byteOffset + 260);
  check('length cell agrees with encoder', mr.textLen, SIC.encodeBuffer(mr.commands).textLen);
} else {
  console.log('--- BIGDISK0-M.IMG not mounted, skipping M06 end-to-end ---');
}

IMAGES.forEach(function(img) {
  var p = path.join(REPO_ROOT, img.file);
  if (!fs.existsSync(p)) {
    console.log('--- ' + img.file + ' not found, skipping ---');
    return;
  }
  console.log('--- ' + img.file + ' (end-to-end) ---');
  var bytes = new Uint8Array(fs.readFileSync(p));
  var r;
  try {
    r = SIC.readInitialCommands(bytes);
  } catch (e) {
    check('readInitialCommands succeeds', String(e.message), '(no error)');
    return;
  }
  check('commands', r.commands, img.expect);
  check('version detected as L', r.versionLetter, 'L');
  check('found in command segment 3', r.segNum, 3);
  check('segment table self-located at page 1275', r.segTablePage, 1275);
  check('byte offset in SEGFIL0', r.byteOffset, 2920614);
  check('length cell offset = byteOffset + 260', r.lengthCellOffset, 2920614 + 260);
  // the length cell must equal what the encoder computes for these commands
  check('length cell agrees with encoder', r.textLen, SIC.encodeBuffer(r.commands).textLen);
});

console.log('');
console.log(passes + ' passed, ' + failures + ' failed');
process.exit(failures === 0 ? 0 : 1);
