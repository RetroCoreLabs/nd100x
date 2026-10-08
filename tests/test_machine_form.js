// test_machine_form.js - the one piece of INI handling that lives in
// JavaScript.
//
// machine-form.js reads a configuration through the C parser (DescribeMachineINI)
// and generates INI text that the C validator checks before it is saved. In
// between sits foreignSections(): the code that carries over every section the
// form does not own, so pressing Save does not delete the settings the form
// cannot show.
//
// [runtime] alone holds the telnet port, the throttle, the charset, the print
// and tape directories, the drum and CDC images and the memory size. If this
// function is wrong, all of it disappears the first time somebody uses the
// form, and nothing says so. Hence a test of its own.
//
//   node tests/test_machine_form.js        (exit 0 = all passed)

'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

// machine-form.js reaches for the DOM only through document.getElementById,
// and the markup it writes is its own (input/select/option with double-quoted
// attributes), so a fake document that scans that markup is enough to render
// a machine and read the form back - no browser needed. The fake is swapped
// in below for the CDC-row tests; the foreignSections tests never touch it.
const sandbox = { console, JSON, document: { getElementById: () => null }, Promise };
sandbox.window = sandbox;
vm.createContext(sandbox);

const modulePath = path.join(__dirname, '..', 'template-glass', 'js', 'machine-form.js');
vm.runInContext(fs.readFileSync(modulePath, 'utf8'), sandbox, { filename: modulePath });

// ---- a fake document ------------------------------------------------------

function unesc(s) {
  return String(s).replace(/&quot;/g, '"').replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&amp;/g, '&');
}

function fakeDocument() {
  const nodes = {};
  function parseAttrs(text) {
    const attrs = {};
    text.replace(/([\w-]+)(?:="([^"]*)")?/g, (_, k, v) => { attrs[k] = v === undefined ? '' : unesc(v); return ''; });
    return attrs;
  }
  function setOptions(n, body) {
    n.options = [];
    let sel = null;
    const ore = /<option value="([^"]*)"( selected)?>([^<]*)<\/option>/g;
    let o;
    while ((o = ore.exec(body))) {
      n.options.push({ value: unesc(o[1]), textContent: unesc(o[3]), selected: !!o[2] });
      if (o[2] && sel === null) sel = unesc(o[1]);
    }
    n.value = sel !== null ? sel : (n.options[0] ? n.options[0].value : '');
  }
  function makeNode(tag, attrs) {
    const n = {
      tagName: tag, id: attrs.id, _attrs: attrs, _listeners: {},
      value: '', checked: false, readOnly: false, disabled: false, type: 'text', options: [],
      addEventListener(ev, fn) { (this._listeners[ev] = this._listeners[ev] || []).push(fn); },
      dispatchEvent(e) { (this._listeners[e.type] || []).forEach((fn) => fn.call(this, e)); return true; },
      getAttribute(k) { return Object.prototype.hasOwnProperty.call(this._attrs, k) ? this._attrs[k] : null; },
      querySelector(q) {
        const m = /^option\[value="([^"]*)"\]$/.exec(q);
        if (!m) return null;
        return this.options.find((x) => x.value === m[1]) || null;
      },
      set innerHTML(html) { if (this.tagName === 'SELECT') { const keep = this.value; setOptions(this, html); if (this.options.some((x) => x.value === keep)) this.value = keep; } },
      get innerHTML() { return ''; }
    };
    return n;
  }
  function parse(html) {
    for (const k in nodes) delete nodes[k];
    const re = /<(input|select)\b([^>]*)>/g;
    let m;
    while ((m = re.exec(html))) {
      const attrs = parseAttrs(m[2]);
      if (!attrs.id) continue;
      const n = makeNode(m[1].toUpperCase(), attrs);
      if (m[1] === 'input') {
        n.type = attrs.type || 'text';
        n.value = attrs.value || '';
        n.checked = Object.prototype.hasOwnProperty.call(attrs, 'checked');
        n.readOnly = Object.prototype.hasOwnProperty.call(attrs, 'readonly');
      } else {
        const end = html.indexOf('</select>', re.lastIndex);
        setOptions(n, html.slice(re.lastIndex, end < 0 ? html.length : end));
      }
      nodes[attrs.id] = n;
    }
  }
  const host = {
    id: 'machine-setup-form', tagName: 'DIV', _html: '', scrollTop: 0, scrollHeight: 0,
    set innerHTML(html) { this._html = html; parse(html); },
    get innerHTML() { return this._html; },
    querySelectorAll() { return Object.keys(nodes).map((k) => nodes[k]); }
  };
  return {
    getElementById(id) { return id === 'machine-setup-form' ? host : (nodes[id] || null); },
    createElement(tag) { return makeNode(tag.toUpperCase(), {}); },
    html() { return host._html; }
  };
}

// The shipped TSS machine's INI text, from the store itself rather than a
// copy that could drift from it.
const profilesPath = path.join(__dirname, '..', 'template-glass', 'js', 'machine-profiles.js');
let store = {};
sandbox.localStorage = {
  getItem: (k) => (Object.prototype.hasOwnProperty.call(store, k) ? store[k] : null),
  setItem: (k, v) => { store[k] = String(v); },
  removeItem: (k) => { delete store[k]; },
  clear: () => { store = {}; }
};
vm.runInContext(fs.readFileSync(profilesPath, 'utf8'), sandbox, { filename: profilesPath });
const TSS_INI = sandbox.machineProfiles.ini('TSS');

// What DescribeMachineINI says about the TSS machine, HAND-BUILT here with
// the key names mc_to_json (src/machine/machine_config_json.c) writes. The
// real parser is not available to node; the headless browser run is where
// the form meets it. If mc_to_json renames a key, this copy is stale.
function tssDescription() {
  const disks = [];
  for (let j = 0; j < 8; j++) disks.push({ slot: j, present: false, media: 'hdd', image: '' });
  return {
    machine: { cpu: 'ND100', cpuDisplay: 'ND-100', cpuNumber: 100, fpp: 48, rtc: 'ticks', mms: 1 },
    runtime: { cdc: 'TSS-CDC.IMG', drum: 'TSS-DRUM.IMG' },
    cpuModels: ['ND100', 'ND110', 'ND120'],
    controllerTypes: [{ type: 'floppy', minWheel: 0, maxWheel: 0, diskSlots: 3, isDisc: true, bootable: true }],
    controllers: [{ type: 'floppy', wheel: 0, enabled: false, isDisc: true, bootable: true, diskSlots: 3,
                    disks, hdlcMode: 'server', hdlcHost: '', hdlcPort: 0 }],
    terminals: [5, 6, 7, 8],
    peripherals: { papertapeReader: false, papertapePunch: false, linePrinter: false },
    boot: { none: false, cdc: true, isDisc: false, type: 'floppy', wheel: 0, unit: 0, file: '' },
    nd500: { enabled: false, memoryMb: 0, kernel: '', pseg: '', dseg: '', disks: [] },
    octobus: { enabled: false }, nd5000: []
  };
}

const mf = sandbox.machineForm;
if (!mf || !mf._foreignSections) {
  console.error('FAIL: machine-form.js did not expose _foreignSections');
  process.exit(1);
}

let passCount = 0, failCount = 0;
function section(n) { console.log('\n=== ' + n + ' ==='); }
function assert(c, d) { if (c) { passCount++; console.log('  PASS: ' + d); } else { failCount++; console.log('  FAIL: ' + d); } }
function assertEqual(a, e, d) {
  if (a === e) { passCount++; console.log('  PASS: ' + d); }
  else { failCount++; console.log('  FAIL: ' + d + ' (expected: ' + JSON.stringify(e) + ', got: ' + JSON.stringify(a) + ')'); }
}

const join = (arr) => arr.join('\n---\n');

section('Sections the form owns are dropped');
assertEqual(join(mf._foreignSections('[machine]\ncpu = 100\n')), '', '[machine] is not carried');
assertEqual(join(mf._foreignSections('[terminals]\nenabled = 5\n')), '', '[terminals] is not carried');
assertEqual(join(mf._foreignSections('[boot]\ndevice = smd.0.0\n')), '', '[boot] is not carried');
assertEqual(join(mf._foreignSections('[controller.smd.0]\nenabled = yes\n')), '',
            '[controller.*] is not carried');
assertEqual(join(mf._foreignSections('[peripheral.lineprinter]\nenabled = yes\n')), '',
            '[peripheral.*] is not carried');

section('Sections the form does not own ARE carried');
const withRuntime =
  '[machine]\ncpu = 100\n\n' +
  '[runtime]\ncharset = norwegian\nmemory = 4\ndrum = DRUM.IMG\n\n' +
  '[boot]\ndevice = smd.0.0\n';
const carried = mf._foreignSections(withRuntime);
assertEqual(carried.length, 1, 'exactly one foreign section found');
assert(carried[0].indexOf('[runtime]') >= 0, 'and it is [runtime]');
assert(carried[0].indexOf('charset = norwegian') >= 0, 'with its charset');
assert(carried[0].indexOf('memory = 4') >= 0, 'with its memory size');
assert(carried[0].indexOf('drum = DRUM.IMG') >= 0, 'with its drum image');
assert(carried[0].indexOf('cpu = 100') < 0, 'and nothing from [machine] leaked in');
assert(carried[0].indexOf('device = smd.0.0') < 0, 'and nothing from [boot] either');

section('A section the form has never heard of');
const future = '[machine]\ncpu = 100\n\n[something.new]\nkey = value\n';
const c2 = mf._foreignSections(future);
assertEqual(c2.length, 1, 'an unknown section is carried, not dropped');
assert(c2[0].indexOf('key = value') >= 0, 'with its contents');

section('Several foreign sections');
const many =
  '[runtime]\ncharset = off\n\n' +
  '[machine]\ncpu = 110\n\n' +
  '[future.thing]\na = 1\n';
const c3 = mf._foreignSections(many);
assertEqual(c3.length, 2, 'both are kept');
assert(join(c3).indexOf('charset = off') >= 0, 'the first survives');
assert(join(c3).indexOf('a = 1') >= 0, 'and so does the last');

section('Leading comments before any section');
// Lines before the first [section] belong to no section. They must NOT be
// carried: the generated file writes its own header, and keeping the old one
// would stack a new header on top of it at every save.
const leading = '# my machine\n# second line\n\n[runtime]\ncharset = off\n';
const c4 = mf._foreignSections(leading);
assertEqual(c4.length, 1, 'only the real section is carried');
assert(c4[0].indexOf('# my machine') < 0, 'the leading comment is not carried');

section('Odd but legal input');
assertEqual(mf._foreignSections('').length, 0, 'empty text yields nothing');
assertEqual(mf._foreignSections(null).length, 0, 'null yields nothing');
assertEqual(mf._foreignSections('no sections here\n').length, 0,
            'text with no section header yields nothing');
const spaced = '   [runtime]   \ncharset = off\n';
assertEqual(mf._foreignSections(spaced).length, 1, 'an indented section header is still a section');
const upper = '[RUNTIME]\ncharset = off\n';
assertEqual(mf._foreignSections(upper).length, 1, 'and case does not change ownership');
const upperOwned = '[MACHINE]\ncpu = 100\n';
assertEqual(mf._foreignSections(upperOwned).length, 0, 'including for the sections we own');

// ---- [nd500] must survive a Save -----------------------------------------
// The form does not own the section, so it is carried over untouched. That is
// true by construction today; it is asserted here because a later change to
// OWNED would break it silently, and the cost is a Save that deletes a
// machine's whole ND-500 with nothing on screen to say so.
const withNd500 =
  '[machine]\ncpu = 110\n\n' +
  '[nd500]\nmemory = 32\nkernel = vmunix\ndisk0 = rootfs_full.img\n\n' +
  '[runtime]\ncharset = off\n';
const keptNd = mf._foreignSections(withNd500);
assertEqual(keptNd.length, 2, '[nd500] and [runtime] are both carried over');
assert(keptNd.join('\n').indexOf('[nd500]') >= 0, 'the ND-500 section is one of them');
assert(keptNd.join('\n').indexOf('disk0 = rootfs_full.img') >= 0,
       'and it keeps its disc, not just its header');

// ---- [runtime] cdc = : the one key the form owns ---------------------------
section('setRuntimeCdc replaces one value and nothing else');
const rt = ['[runtime]\ncdc = TSS-CDC.IMG         ; CDC cartridge system disc @ IOX 500\ndrum = TSS-DRUM.IMG       ; swapping drum @ IOX 540\n'];
const rt2 = mf._setRuntimeCdc(rt, 'NEW.IMG');
assertEqual(rt2.length, 1, 'still one section');
assert(rt2[0].indexOf('cdc = NEW.IMG         ; CDC cartridge system disc @ IOX 500') >= 0,
       'the cdc value is replaced and its comment kept');
assert(rt2[0].indexOf('drum = TSS-DRUM.IMG       ; swapping drum @ IOX 540') >= 0, 'the drum line is untouched');
assertEqual(rt[0].indexOf('NEW.IMG'), -1, 'the input array is not modified');
const rt3 = mf._setRuntimeCdc(['[runtime]\ncharset = off\n\n', '[nd500]\nmemory = 32\n'], 'X.IMG');
assertEqual(rt3[0], '[runtime]\ncharset = off\ncdc = X.IMG\n\n', 'a [runtime] without the line gets one after its last key');
assertEqual(rt3[1], '[nd500]\nmemory = 32\n', 'another section is not touched');
const rt4 = mf._setRuntimeCdc(['[nd500]\nmemory = 32\n'], 'X.IMG');
assertEqual(rt4.length, 2, 'no [runtime] at all: a new section is added');
assertEqual(rt4[1], '[runtime]\ncdc = X.IMG', 'holding the cdc line');
assertEqual(mf._setRuntimeCdc(rt, '').length, 1, 'an empty file name changes nothing');
assertEqual(mf._setRuntimeCdc(rt, '')[0], rt[0], 'not even the text');

// ---- the CDC row ------------------------------------------------------------
// The emulator module is faked: describeMachineINI answers the hand-built TSS
// description. Demo mode first (no library), then library mode.
const doc = fakeDocument();
sandbox.document = doc;
sandbox.emu = { describeMachineINI: () => JSON.stringify(tssDescription()) };

(async () => {
  section('A TSS machine in demo mode shows the CDC row read-only');
  assertEqual(await mf.load(TSS_INI, {}, {}), true, 'the TSS description renders');
  assert(doc.html().indexOf('CDC cartridge disc') >= 0, 'the row has its title');
  const demo = doc.getElementById('mf-cdc-d');
  assert(!!demo, 'mf-cdc-d exists');
  assertEqual(demo && demo.type, 'text', 'as a text field');
  assertEqual(demo && demo.readOnly, true, 'read-only');
  assertEqual(demo && demo.value, 'TSS-CDC.IMG', 'holding the INI file');
  assertEqual(doc.getElementById('mf-cdc-l'), null, 'and no library select');
  assertEqual(doc.getElementById('mf-boot').value, 'cdc', 'the boot drive is the cartridge disc');
  let ini = mf.toINI(TSS_INI);
  assert(/^cdc = TSS-CDC\.IMG\s+; CDC cartridge system disc @ IOX 500$/m.test(ini), 'toINI keeps [runtime] cdc = TSS-CDC.IMG with its comment');
  assert(/^drum = TSS-DRUM\.IMG/m.test(ini), 'and drum = TSS-DRUM.IMG');
  assert(/^\[runtime\]/m.test(ini), 'inside a [runtime] section');
  assertEqual((ini.match(/^\[runtime\]/mg) || []).length, 1, 'exactly one [runtime] section');
  assert(/^device = cdc$/m.test(ini), 'and [boot] device = cdc');
  assertEqual(Object.keys(mf.libraryPicks()).length, 0, 'no library pick');

  section('A machine without the disc has no CDC row');
  const noCdc = tssDescription();
  noCdc.runtime = { cdc: '', drum: '' };
  noCdc.boot = { none: true, cdc: false, isDisc: false, type: 'floppy', wheel: 0, unit: 0, file: '' };
  sandbox.emu.describeMachineINI = () => JSON.stringify(noCdc);
  assertEqual(await mf.load('[machine]\ncpu = 100\n\n[runtime]\ncharset = off\n', {}, {}), true, 'renders');
  assertEqual(doc.getElementById('mf-cdc-d'), null, 'no mf-cdc-d');
  assert(doc.html().indexOf('CDC cartridge disc') < 0, 'no CDC title');
  ini = mf.toINI('[machine]\ncpu = 100\n\n[runtime]\ncharset = off\n');
  assert(ini.indexOf('cdc =') < 0, 'and toINI adds no cdc line');
  assert(ini.indexOf('charset = off') >= 0, 'while [runtime] is still carried');
  sandbox.emu.describeMachineINI = () => JSON.stringify(tssDescription());

  section('Library mode: a CDC-tagged library image can be chosen');
  const images = [
    { uuid: 'u-cdc', name: 'NORD TSS 3.0 cartridge disc', size: 4194304, diskType: 'cdc' },
    { uuid: 'u-smd', name: 'SINTRAN K', size: 75000000, diskType: 'smd' },
    { uuid: 'u-cdc2', name: 'My TSS pack', size: 4194304, diskType: 'cdc' }
  ];
  sandbox.isSmdPersistenceEnabled = () => true;
  sandbox.smdStorage = {
    isAvailable: () => true,
    listImages: () => images.slice(),
    formatSize: (n) => Math.round(n / 1048576) + ' MB',
    getMetadata: (uuid) => images.find((x) => x.uuid === uuid) || null
  };
  assertEqual(await mf.load(TSS_INI, {}, {}), true, 'renders in library mode');
  const hidden = doc.getElementById('mf-cdc-d');
  const sel = doc.getElementById('mf-cdc-l');
  assert(!!hidden && hidden.type === 'hidden', 'mf-cdc-d is a hidden field');
  assertEqual(hidden && hidden.value, 'TSS-CDC.IMG', 'holding the INI file');
  assert(!!sel, 'mf-cdc-l exists');
  assertEqual(sel && sel.options.map((o) => o.value).join(','), 'lib:u-cdc,lib:u-cdc2,file',
              'it lists the cdc-tagged images only, plus the INI file');
  assertEqual(sel && sel.value, 'file', 'the INI file is selected');
  assertEqual(sel && sel.options[2].textContent, 'file: TSS-CDC.IMG (not in the library)', 'with its label');
  assertEqual(sel && sel.getAttribute('data-file'), 'TSS-CDC.IMG', 'and remembered on the select');

  sel.value = 'lib:u-cdc';
  sel.dispatchEvent({ type: 'change' });
  assertEqual(hidden.value, 'NORD-TSS-3-0-CARTRIDGE-DISC.IMG', 'choosing a library image sets the MEMFS-safe file name');
  const picks = mf.libraryPicks();
  assert(!!picks['cdc.0.0'], 'libraryPicks() has cdc.0.0');
  assertEqual(picks['cdc.0.0'] && picks['cdc.0.0'].uuid, 'u-cdc', 'with the uuid');
  assertEqual(picks['cdc.0.0'] && picks['cdc.0.0'].file, 'NORD-TSS-3-0-CARTRIDGE-DISC.IMG', 'the file');
  assertEqual(picks['cdc.0.0'] && picks['cdc.0.0'].name, 'NORD TSS 3.0 cartridge disc', 'and the name');
  assertEqual(Object.keys(picks).join(','), 'cdc.0.0', 'and nothing else');
  const bootOpt = doc.getElementById('mf-boot').options.find((o) => o.value === 'cdc');
  assert(bootOpt && bootOpt.textContent.indexOf('NORD-TSS-3-0-CARTRIDGE-DISC.IMG') >= 0, 'the boot list names the new file');
  assertEqual(doc.getElementById('mf-boot').value, 'cdc', 'and still boots from the disc');
  ini = mf.toINI(TSS_INI);
  assert(/^cdc = NORD-TSS-3-0-CARTRIDGE-DISC\.IMG\s+; CDC cartridge system disc @ IOX 500$/m.test(ini),
         'toINI writes [runtime] cdc = <chosen file>, comment kept');
  assert(/^drum = TSS-DRUM\.IMG/m.test(ini), 'drum = TSS-DRUM.IMG survives');
  assertEqual((ini.match(/^cdc = /mg) || []).length, 1, 'one cdc line');
  assert(/^device = cdc$/m.test(ini), '[boot] device = cdc');

  sel.value = 'file';
  sel.dispatchEvent({ type: 'change' });
  assertEqual(hidden.value, 'TSS-CDC.IMG', "choosing 'file' puts the INI's own file back");
  assertEqual(Object.keys(mf.libraryPicks()).length, 0, 'and drops the pick');

  section('A saved pick is shown selected when the INI still names its file');
  const savedIni = TSS_INI.replace('cdc = TSS-CDC.IMG', 'cdc = MY-TSS-PACK.IMG');
  const savedDesc = tssDescription();
  savedDesc.runtime.cdc = 'MY-TSS-PACK.IMG';
  sandbox.emu.describeMachineINI = () => JSON.stringify(savedDesc);
  assertEqual(await mf.load(savedIni, {}, { 'cdc.0.0': { uuid: 'u-cdc2', file: 'MY-TSS-PACK.IMG', name: 'My TSS pack' } }), true, 'renders');
  assertEqual(doc.getElementById('mf-cdc-l').value, 'lib:u-cdc2', 'the library image is selected');
  assertEqual(doc.getElementById('mf-cdc-l').options.map((o) => o.value).join(','), 'lib:u-cdc,lib:u-cdc2',
              "and there is no 'file' entry - the file IS the library image");
  assertEqual(mf.libraryPicks()['cdc.0.0'].uuid, 'u-cdc2', 'libraryPicks() still returns it');
  assert(/^cdc = MY-TSS-PACK\.IMG/m.test(mf.toINI(savedIni)), 'and toINI keeps the file');

  section('A saved pick whose INI file was edited by hand is a plain file again');
  assertEqual(await mf.load(savedIni, {}, { 'cdc.0.0': { uuid: 'u-cdc2', file: 'OTHER.IMG', name: 'My TSS pack' } }), true, 'renders');
  assertEqual(doc.getElementById('mf-cdc-l').value, 'file', "the 'file' entry is selected");
  assertEqual(Object.keys(mf.libraryPicks()).length, 0, 'and the pick is not returned');

  console.log('\n===============================');
  console.log('Results: ' + passCount + ' passed, ' + failCount + ' failed');
  if (failCount === 0) { console.log('All tests passed.'); process.exit(0); }
  process.exit(1);
})().catch((e) => { console.error('FAIL: ' + (e && e.stack || e)); process.exit(1); });
