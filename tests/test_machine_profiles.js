// test_machine_profiles.js - unit tests for the named machine configuration
// store (template-glass/js/machine-profiles.js).
//
// NODE, not a headless browser, unlike test_glass_windows.html next door. That
// harness exists because line-printer.js and paper-tape.js need a real DOM;
// machine-profiles.js touches nothing but `window` and `localStorage`, so a
// browser would only add a dependency that CI may not have. Both are stubbed
// below - localStorage in memory, so a test run can never eat the developer's
// own saved machines.
//
//   node tests/test_machine_profiles.js        (exit 0 = all passed)

'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

// ---- the stubs ------------------------------------------------------------

let store = {};
const localStorage = {
  getItem: (k) => (Object.prototype.hasOwnProperty.call(store, k) ? store[k] : null),
  setItem: (k, v) => { store[k] = String(v); },
  removeItem: (k) => { delete store[k]; },
  clear: () => { store = {}; }
};

const sandbox = { localStorage, console, JSON };
sandbox.window = sandbox;          // the module hangs its API off `window`
vm.createContext(sandbox);

const modulePath = path.join(__dirname, '..', 'template-glass', 'js', 'machine-profiles.js');
vm.runInContext(fs.readFileSync(modulePath, 'utf8'), sandbox, { filename: modulePath });

const mp = sandbox.machineProfiles;
if (!mp) { console.error('FAIL: machine-profiles.js did not define window.machineProfiles'); process.exit(1); }

// ---- the framework --------------------------------------------------------

let passCount = 0, failCount = 0;

function section(name) { console.log('\n=== ' + name + ' ==='); }

function assert(cond, description) {
  if (cond) { passCount++; console.log('  PASS: ' + description); }
  else      { failCount++; console.log('  FAIL: ' + description); }
}

function assertEqual(actual, expected, description) {
  if (actual === expected) { passCount++; console.log('  PASS: ' + description); }
  else {
    failCount++;
    console.log('  FAIL: ' + description +
                ' (expected: ' + JSON.stringify(expected) +
                ', got: ' + JSON.stringify(actual) + ')');
  }
}

function reset() { localStorage.clear(); }

// The built-in machines every store holds, in order.
const SHIPPED = ['ND-100', 'ND-5000', 'BSD 2.11', '500 NDIX-C', 'TSS'];

// The INI the page shipped as "Default" before the built-in machines existed.
const LEGACY_DEFAULT_INI =
  '# nd100x machine configuration\n' +
  '# Toggle a device with "enabled = yes|no". Sections are [type.thumbwheel].\n\n' +
  '[machine]\n' +
  'cpu = 100                 ; 100 | 110 | 120, or a model: ND110CX, ND120CX\n\n' +
  '[controller.floppy.0]\n' +
  'enabled = yes\n' +
  'disk0 = FLOPPY.IMG\n\n' +
  '[controller.smd.0]\n' +
  'enabled = yes\n' +
  'disk0 = SMD0.IMG\n\n' +
  '[controller.scsi.0]\n' +
  'enabled = yes\n' +
  'disk0 = hdd:SCSI0.IMG     ; SCSI ID 0 (media hdd)\n\n' +
  '[terminals]\n' +
  'enabled = 5, 6, 7, 8, 9, 10, 11\n\n' +
  '[boot]\n' +
  'device = smd.0.0          ; <type>.<wheel>.<unit>\n';

// ---- the tests ------------------------------------------------------------

section('First run: the built-in machines');
reset();
assertEqual(mp.list().join(','), SHIPPED.join(','), 'starts with the five built-in machines');
assertEqual(mp.activeName(), 'ND-100', 'and the plain ND-100 is the active one');
assert(mp.ini().indexOf('device = smd.0.0') >= 0, 'booting SINTRAN from SMD unit 0');
assert(mp.ini().indexOf('[controller.scsi') < 0, 'with no SCSI controller - floppy and SMD only');
assertEqual(mp.kind('ND-5000'), 'nd100', 'ND-5000 is an ND-100 machine...');
assert(mp.ini('ND-5000').indexOf('[nd5000.1]') >= 0, '...with an ND-5000 CPU section');
assert(mp.ini('ND-5000').indexOf('[controller.octobus.0]') >= 0, 'and the octobus card');
assert(mp.ini('BSD 2.11').indexOf('device = wd.0.0') >= 0, 'BSD 2.11 boots from the Winchester');
assert(mp.ini('BSD 2.11').indexOf('BSD211-WD0.IMG') >= 0, 'the BSD Winchester image');
assertEqual(mp.kind('500 NDIX-C'), 'nd500-ndix', '500 NDIX-C is a standalone ND-500');
assertEqual(mp.ndix('500 NDIX-C').diskUrl, 'NDIX.IMG', 'on the served NDIX root disc');
assertEqual(mp.ndix('500 NDIX-C').writable, true, 'with the disc writable - it only ever lives in memory');
SHIPPED.forEach((n) => assert(mp.isShipped(n), n + ' is built in'));

section('Built-in machines are read-only');
reset();
assertEqual(mp.write('[machine]\ncpu = 110\n', 'ND-100'), false, 'write() into a built-in is refused');
assert(mp.ini('ND-100').indexOf('cpu = 100') >= 0, 'and it is untouched');
assert(mp.rename('ND-100', 'Mine') !== '', 'rename is refused');
assert(mp.remove('ND-100') !== '', 'delete is refused');
assertEqual(mp.list().length, SHIPPED.length, 'nothing changed');
assert(mp.writeNdix({ memoryMb: 32 }, '500 NDIX-C') !== '', 'writeNdix into a built-in is refused');
assertEqual(mp.writeTerminal({ backend: 'xterm' }, 'ND-100'), false, 'writeTerminal into a built-in is refused');
assert(mp.create('ND-100', 'x') !== '', 'a built-in name cannot be taken by a new machine');

section('Clone: the way a built-in is changed');
reset();
assertEqual(mp.clone('ND-100', 'Mine'), '', 'cloning a built-in succeeds');
assertEqual(mp.activeName(), 'Mine', 'the copy is active');
assertEqual(mp.list().length, SHIPPED.length + 1, 'and listed');
assert(mp.ini('Mine').indexOf('device = smd.0.0') >= 0, 'with the same INI');
assertEqual(mp.write('[machine]\ncpu = 110\n', 'Mine'), true, 'the copy can be written');
assert(mp.ini('ND-100').indexOf('cpu = 100') >= 0, 'while the original is untouched');
assert(mp.clone('ND-100', 'Mine') !== '', 'a second copy under the same name is refused');
assert(mp.clone('ND-100', 'ND-5000') !== '', 'a built-in name is refused for a copy');
assert(mp.clone('nope', 'x') !== '', 'cloning a missing machine is refused');
assertEqual(mp.clone('500 NDIX-C', 'My Unix'), '', 'an NDIX machine clones too');
assertEqual(mp.kind('My Unix'), 'nd500-ndix', 'as an NDIX machine');
assertEqual(mp.ndix('My Unix').diskUrl, 'NDIX.IMG', 'with its settings');

section('The old single key is no longer a machine');
reset();
localStorage.setItem('nd100x-machine-ini', '[machine]\ncpu = 110\n');
assertEqual(mp.list().join(','), SHIPPED.join(','), 'an old single-key config makes no machine - the stock machine is the built-in ND-100');
assertEqual(mp.activeName(), 'ND-100', 'which is active');

section('Upgrade of a v1/v2 store');
reset();
localStorage.setItem('nd100x-machine-profiles', JSON.stringify({ v: 2, active: 'Mine', profiles: [
  { name: 'Default', kind: 'nd100', ini: LEGACY_DEFAULT_INI + '\n[runtime]\ntelnet = 9000\n' },  // the old stock machine, edited or not: dropped
  { name: '500ndix-c', kind: 'nd500-ndix', ndix: { diskUrl: 'rootfs_full.img', memoryMb: 16, writable: false } }, // a demo-NDIX duplicate: dropped
  { name: 'NDIX C', kind: 'nd500-ndix', ndix: { diskUrl: 'rootfs_full.img' } },  // shipped under its old name: replaced
  { name: 'ND-100', kind: 'nd100', ini: LEGACY_DEFAULT_INI },            // an old copy of a built-in: replaced by the current one
  { name: 'Mine', kind: 'nd100', ini: '[machine]\ncpu = 120\n' },       // the user's own: kept
  { name: 'My Unix', kind: 'nd500-ndix', ndix: { diskUuid: 'abc', diskName: 'lib' } } // an NDIX machine on a library disc: kept
]}));
assertEqual(mp.list().join(','), SHIPPED.concat(['Mine', 'My Unix']).join(','), 'built-ins first, then the user machines, leftovers gone');
assertEqual(mp.activeName(), 'Mine', 'with the user machine still active');
assert(mp.ini('ND-100').indexOf('[controller.scsi') < 0, 'ND-100 is the current definition (no SCSI)');
assertEqual(mp.ndix('My Unix').diskUuid, 'abc', 'the kept NDIX machine keeps its disc');
reset();
localStorage.setItem('nd100x-machine-profiles', JSON.stringify({ v: 2, active: 'Default', profiles: [
  { name: 'Default', kind: 'nd100', ini: LEGACY_DEFAULT_INI } ]}));
assertEqual(mp.activeName(), 'ND-100', 'when the active machine is dropped, ND-100 takes over');
reset();
localStorage.setItem('nd100x-machine-profiles', JSON.stringify({ v: 3, active: 'ND-100', profiles: [
  { name: 'ND-100', kind: 'nd100', ini: '[machine]\ncpu = 100\n' } ]}));
assertEqual(mp.activeName(), 'ND-100', 'a v3 store active on ND100 follows the rename');

section('Create');
reset();
const base = mp.list().length;
assertEqual(mp.create('ND-110 test', '[machine]\ncpu = ND110CX\n'), '', 'creating a machine succeeds');
assertEqual(mp.list().length, base + 1, 'there is now one more');
assertEqual(mp.activeName(), 'ND-110 test', 'the new one is active');
assert(mp.ini().indexOf('ND110CX') >= 0, 'showing its own INI');
assert(mp.create('ND-110 test', 'x') !== '', 'a duplicate name is refused');
assert(mp.create('   ', 'x') !== '', 'an empty name is refused');
assertEqual(mp.list().length, base + 1, 'and neither refusal added anything');

section('Names are trimmed');
reset();
mp.create('  spaced  ', 'x');
assertEqual(mp.activeName(), 'spaced', 'surrounding blanks are dropped');

section('Switching keeps each machine separate');
reset();
mp.create('First', '[machine]\ncpu = 100\n');
mp.create('Second', '[machine]\ncpu = 120\n');
assert(mp.ini().indexOf('cpu = 120') >= 0, 'Second shows its own config');
assert(mp.setActive('First'), 'switching back works');
assert(mp.ini().indexOf('cpu = 100') >= 0, 'First still has its own');
assert(!mp.setActive('nope'), 'switching to an unknown name is refused');
assertEqual(mp.activeName(), 'First', 'and leaves the selection alone');

section('Rename');
reset();
mp.create('old name', 'x');
assertEqual(mp.rename('old name', 'new name'), '', 'renaming succeeds');
assertEqual(mp.activeName(), 'new name', 'the active selection follows it');
assertEqual(mp.ini(), 'x', 'the config comes with it');
assert(mp.rename('new name', 'ND-100') !== '', 'renaming onto a built-in name is refused');
assert(mp.rename('missing', 'whatever') !== '', 'renaming a missing machine is refused');

section('Delete');
reset();
mp.create('doomed', 'x');
assertEqual(mp.remove('doomed'), '', 'deleting works');
assertEqual(mp.list().length, SHIPPED.length, 'back to the built-ins');
assert(mp.list().indexOf(mp.activeName()) >= 0, 'and the selection moved to a machine that exists');

section('Survives a corrupt store');
reset();
localStorage.setItem('nd100x-machine-profiles', '{ this is not json');
assertEqual(mp.list().length, SHIPPED.length, 'rebuilds (with the built-in machines) instead of throwing');
assert(mp.ini().indexOf('[machine]') >= 0, 'with a usable config');

section('The old key keeps pointing at the active machine');
reset();
mp.create('Live', '[machine]\ncpu = ND120CX\n');
assert(localStorage.getItem('nd100x-machine-ini').indexOf('ND120CX') >= 0,
       'so anything still reading the single key gets the right one');
mp.create('Other', '[machine]\ncpu = 100\n');
assert(localStorage.getItem('nd100x-machine-ini').indexOf('cpu = 100') >= 0,
       'and it follows the selection');

section('Machine kind: ND-100 by default, ND-500 standalone (NDIX) on request');
reset();
assertEqual(mp.kind(), 'nd100', 'the default machine is an ND-100');
mp.create('Plain', '[machine]\ncpu = 100\n');
assertEqual(mp.kind('Plain'), 'nd100', 'create() makes an ND-100');
assertEqual(mp.createNdix('Unix box', { diskUuid: 'abc', diskName: 'NDIX root', memoryMb: 32, writable: true }),
            '', 'createNdix() succeeds');
assertEqual(mp.kind(), 'nd500-ndix', 'and the new machine is active and of that kind');
assertEqual(mp.ndix().diskUuid, 'abc', 'its root disc is kept');
assertEqual(mp.ndix().memoryMb, 32, 'so is its memory size');
assertEqual(mp.ndix().writable, true, 'and the writable flag');
assert(mp.ini().indexOf('[machine]') >= 0, 'ini() still hands back a parseable ND-100 for the hidden form');
assert(mp.createNdix('Unix box', {}) !== '', 'a duplicate NDIX name is refused');
assertEqual(mp.ndix('Plain').memoryMb, 16, 'an ND-100 machine answers the NDIX defaults');
assertEqual(mp.writeNdix({ diskUuid: 'def', memoryMb: 7 }, 'Unix box'), '', 'writeNdix() succeeds');
assertEqual(mp.ndix('Unix box').diskUuid, 'def', 'and replaced the disc');
assertEqual(mp.ndix('Unix box').memoryMb, 16, 'while an unoffered memory size falls back to the default');
assertEqual(mp.createNdix('Demo box', { diskUrl: 'NDIX.IMG', diskName: 'NDIX root disk (server)' }), '',
            'a served (demo-mode) root disc is a valid choice too');
assertEqual(mp.ndix().diskUrl, 'NDIX.IMG', 'and is kept as a URL');
assertEqual(mp.ndix().diskUuid, '', 'with no library image');
mp.createNdix('Fresh', null);
assertEqual(mp.ndix().writable, true, 'a new NDIX machine is writable by default');
mp.writeNdix({ writable: false });
assertEqual(mp.ndix().writable, false, 'while an explicit read-only choice is kept');

section('Terminal settings belong to the machine');
reset();
assertEqual(mp.terminal('ND-100').backend, 'retroterm', 'an ND-100 machine draws with RetroTerm by default');
assertEqual(mp.terminal('ND-100').emulator, 'tdv2200', 'as a TDV 2200');
assertEqual(mp.terminal('ND-100').language, 'no', 'with the Norwegian keyboard');
assertEqual(mp.terminal('500 NDIX-C').backend, 'xterm', 'an NDIX machine draws with xterm');
assertEqual(mp.terminal('500 NDIX-C').emulator, 'vt100', 'as a VT100');
mp.clone('ND-100', 'Mine');
assert(mp.writeTerminal({ backend: 'xterm', emulator: 'bogus', language: 'se' }, 'Mine'), 'writing settings succeeds');
assertEqual(mp.terminal('Mine').backend, 'xterm', 'the renderer was taken');
assertEqual(mp.terminal('Mine').emulator, 'tdv2200', 'an unknown emulator falls back to the default');
assertEqual(mp.terminal('Mine').language, 'se', 'the keyboard was taken');
assertEqual(localStorage.getItem('nd100x-terminal-backend'), 'xterm', 'and the active machine\'s renderer is mirrored for the pop-out page');
mp.setActive('500 NDIX-C');
assertEqual(localStorage.getItem('nd100x-emulator-type'), 'vt100', 'switching machines re-mirrors the keys');
assertEqual(mp.terminal('BSD 2.11').language, 'no', 'another machine is untouched');

section('Catalog floppies chosen for the floppy drives');
reset();
mp.clone('ND-100', 'Mine');
assertEqual(Object.keys(mp.floppies()).length, 0, 'none to begin with');
assert(mp.writeFloppies({ 0: { file: 'SINTRAN-K.IMG', name: 'SINTRAN K', imageUrl: 'https://x/a.img.gz', md5: 'abc' },
                          2: { file: 'X.IMG', imageUrl: '' },           // no URL: dropped
                          5: { file: 'Y.IMG', imageUrl: 'https://x/y' } // no such slot: dropped
                        }), 'writing choices succeeds');
assertEqual(Object.keys(mp.floppies()).join(','), '0', 'only a slot 0-2 with a file and a URL is kept');
assertEqual(mp.floppies()[0].name, 'SINTRAN K', 'with its name');
assertEqual(mp.floppies()[0].file, 'SINTRAN-K.IMG', 'and the file the INI names');
mp.create('Other', '[machine]\ncpu = 100\n');
assertEqual(Object.keys(mp.floppies()).length, 0, 'another machine has its own (empty) choices');
assertEqual(mp.floppies('Mine')[0].file, 'SINTRAN-K.IMG', 'while the first keeps hers');
assertEqual(mp.writeFloppies({ 0: { file: 'A.IMG', imageUrl: 'https://x' } }, 'ND-100'), false, 'a built-in takes none');

section('Library images chosen for the drives');
reset();
mp.clone('ND-100', 'Mine');
assertEqual(Object.keys(mp.library()).length, 0, 'none to begin with');
assert(mp.writeLibrary({ 'smd.0.0': { uuid: 'u1', file: 'SINTRAN-K.IMG', name: 'SINTRAN K' },
                         'wd.0.1':  { uuid: 'u2', file: 'BSD.IMG' },
                         'cdc.0.0': { uuid: 'u3', file: 'X.IMG' },          // the TSS cartridge disc ([runtime] cdc =): kept
                         'hdlc.0.0': { uuid: 'u5', file: 'Y.IMG' },         // not a library type: dropped
                         'smd.0.9': { uuid: 'u4' }                           // no file: dropped
                       }), 'writing choices succeeds');
assertEqual(Object.keys(mp.library()).sort().join(','), 'cdc.0.0,smd.0.0,wd.0.1', 'smd/scsi/wd/floppy/cdc slots with uuid and file are kept');
assertEqual(mp.library()['wd.0.1'].name, 'BSD.IMG', 'a missing name falls back to the file');
mp.create('Other', '[machine]\ncpu = 100\n');
assertEqual(Object.keys(mp.library()).length, 0, 'another machine has its own (empty) choices');
assertEqual(mp.clone('Mine', 'Mine 2'), '', 'cloning a machine with choices works');
assertEqual(mp.library('Mine 2')['smd.0.0'].uuid, 'u1', 'and the copy has them too');

section('Profiles saved before kinds existed are ND-100s');
reset();
localStorage.setItem('nd100x-machine-profiles',
  JSON.stringify({ v: 1, active: 'Old', profiles: [{ name: 'Old', ini: '[machine]\ncpu = 110\n' }] }));
assertEqual(mp.kind('Old'), 'nd100', 'a profile with no kind field is an ND-100');
assert(mp.ini('Old').indexOf('cpu = 110') >= 0, 'and its INI is untouched');

// ---- summary --------------------------------------------------------------

console.log('\n===============================');
console.log('Results: ' + passCount + ' passed, ' + failCount + ' failed');
if (failCount === 0) { console.log('All tests passed.'); process.exit(0); }
process.exit(1);
