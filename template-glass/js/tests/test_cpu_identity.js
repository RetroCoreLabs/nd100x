//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen - RetroCore Labs
//

// test_cpu_identity.js - Unit tests for the CPU Identity (disk) window
// (js/sintran-cpu-identity.js) on top of the ndfs browser bundle.
//
// Reads the repo's real SMD images and checks what the window would show
// against values measured with `ndtool --cpu-identity` on 2026-10-29. The
// write path is exercised on an in-memory COPY of the image bytes only; no
// file on disk is ever written.
//
// Run from the template-glass directory:
//   node js/tests/test_cpu_identity.js

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
loadScript('js/sintran-cpu-identity.js');

var failures = 0, passes = 0;
function check(desc, got, want) {
  var g = JSON.stringify(got), w = JSON.stringify(want);
  if (g === w) { passes++; console.log('  PASS ' + desc); }
  else { failures++; console.log('  FAIL ' + desc + '  (want ' + w + ', got ' + g + ')'); }
}

var CI = window.sintranCpuIdentity;

// ---------------------------------------------------------------
// 1. The bundle carries the identity and segment API
// ---------------------------------------------------------------
console.log('--- bundle exports ---');
check('readCpuIdentity exported', typeof NdfsLib.readCpuIdentity, 'function');
check('writeCpuIdentity exported', typeof NdfsLib.writeCpuIdentity, 'function');
check('listSegments exported', typeof NdfsLib.listSegments, 'function');
check('window API present', typeof CI.readPack, 'function');

// ---------------------------------------------------------------
// 2. SMD0.IMG (SINTRAN L, the demo pack): values from ndtool --cpu-identity
// ---------------------------------------------------------------
var IMAGES = [
  { file: 'SMD0.IMG', version: 'L', sysno: 102, fcpun: 21238, type: 9883, cpn33: 0,
    entered: true, dirSystem: 102, tablePage: 1275, imageMadr: 1019, saveMadr: 64, fcpunSym: 0o4414 }
];

IMAGES.forEach(function(img) {
  var p = path.join(REPO_ROOT, img.file);
  if (!fs.existsSync(p)) { console.log('--- ' + img.file + ' not found, skipping ---'); return; }
  console.log('--- ' + img.file + ' (read) ---');
  var bytes = new Uint8Array(fs.readFileSync(p));
  var r = CI.readPack(bytes);
  check('no error', r.error, null);
  check('version', r.ident.version, img.version);
  check('FCPUN symbol from the pack', r.ident.sym.fcpun, img.fcpunSym);
  check('segment table page', r.ident.segmentTablePage, img.tablePage);
  check('names from RTFIL:DATA', r.ident.namesPath, 'SYSTEM/RTFIL:DATA');
  check('image copy segment 44B', r.ident.copies[0].segmentNumber, 0o44);
  check('save copy segment 53B', r.ident.copies[1].segmentNumber, 0o53);
  check('image copy MADR', r.ident.copies[0].madr, img.imageMadr);
  check('save copy MADR', r.ident.copies[1].madr, img.saveMadr);
  check('SYSNO (image copy)', r.ident.copies[0].sysno, img.sysno);
  check('FCPUN (image copy)', r.ident.copies[0].fcpun, img.fcpun);
  check('CPU type (image copy)', r.ident.copies[0].cpuType, img.type);
  check('SYSNO (save copy)', r.ident.copies[1].sysno, img.sysno);
  check('33CPN', r.ident.sym.cpn33, img.cpn33);
  check('directory entered', r.ident.dirEntered, img.entered);
  check('directory system number', r.ident.dirSystemNumber, img.dirSystem);
  check('segments listed', r.segs !== null && r.segs.segments.length > 40, true);
  var i = NdfsLib.findSegment(r.segs, 'S3SDPIT');
  check('S3SDPIT disk page = SEGFIL0 base + MADR', r.segs.segments[i].diskPage, r.segs.segfiles[0].basePage + img.saveMadr);
  var html = CI.renderIdentity(r, { name: img.file, mode: 'test' }) + CI.renderSegments(r);
  check('rendered HTML names the save copy', html.indexOf('S3SDPIT') >= 0, true);

  // ---- write on an in-memory copy; the file is never touched ----
  console.log('--- ' + img.file + ' (write, in memory) ---');
  var copy = new Uint8Array(bytes);        // a copy: patchPack works on its own buffer anyway
  var res = CI.patchPack(copy, 21238, 9883, false);
  check('read-back SYSNO (image copy)', res.after.copies[0].sysno, 21238);
  check('read-back FCPUN (save copy)', res.after.copies[1].fcpun, 21238);
  check('read-back CPU type', res.after.copies[1].cpuType, 9883);
  check('read-back 33CPN', res.after.sym.cpn33, 21238);
  check('stamp cleared', res.after.dirEntered, false);
  check('stamp system number 0', res.after.dirSystemNumber, 0);
  check('patched bytes same length', res.bytes.length, bytes.length);
  var again = CI.readPack(res.bytes);
  check('a fresh read of the patched bytes agrees', again.ident.copies[0].sysno, 21238);
  check('source bytes untouched', bytes[2000] === fs.readFileSync(p)[2000] && CI.readPack(bytes).ident.copies[0].sysno, img.sysno);

  // keep-stamp and type-kept
  var res2 = CI.patchPack(bytes, 7, null, true);
  check('type kept when blank', res2.after.copies[0].cpuType, img.type);
  check('stamp kept on request', res2.after.dirEntered, img.entered);

  // bad values refused before anything is written
  check('65536 refused', (function() { try { CI.patchPack(bytes, 65536, null, false); return 'no throw'; } catch (e) { return 'threw'; } })(), 'threw');
});

// ---------------------------------------------------------------
// 3. A pack without SINTRAN: refusal with text, no crash
// ---------------------------------------------------------------
var floppy = path.join(REPO_ROOT, 'FLOPPY.IMG');
if (fs.existsSync(floppy)) {
  console.log('--- FLOPPY.IMG (not a system pack) ---');
  var fr = CI.readPack(new Uint8Array(fs.readFileSync(floppy)));
  check('error text present', typeof fr.error === 'string' && fr.error.length > 0, true);
  check('render does not throw', typeof CI.renderIdentity(fr, { name: 'FLOPPY.IMG', mode: 'test' }), 'string');
}

console.log('');
console.log(passes + ' passed, ' + failures + ' failed');
process.exit(failures === 0 ? 0 : 1);
