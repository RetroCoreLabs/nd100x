// machine-setup.js - Machine Setup window for the Glass UI.
//
// Edits the machine configuration as an INI (the same format the native binary
// reads as nd100x.ini). Validation calls the native MachineConfig parser via
// the ValidateMachineINI WASM export, so the browser gets identical, friendly
// error messages. Configurations are kept BY NAME (machine-profiles.js) and can
// be downloaded to run the same machine with the native binary. The selected
// profile is the one toolbar.js hands to emu.init() when the emulator starts.

(function () {
  'use strict';

  function el(id) { return document.getElementById(id); }

  // ---- the profile dropdown ----------------------------------------------
  // Rebuilt from the store on every show and after every change, rather than
  // patched in place: the store is the truth and a list that drifts from it is
  // how you end up saving into the wrong machine.
  function refreshProfiles() {
    var sel = el('machine-setup-profile');
    if (!sel) return;
    var names = machineProfiles.list();
    var active = machineProfiles.activeName();
    sel.innerHTML = '';
    for (var i = 0; i < names.length; i++) {
      var o = document.createElement('option');
      o.value = names[i];
      o.textContent = names[i];
      if (names[i] === active) o.selected = true;
      sel.appendChild(o);
    }
  }

  // Switching machines DISCARDS whatever is in the textarea. Say so, rather
  // than quietly saving it into the machine being left - which is exactly the
  // kind of silent write that loses work.
  function selectProfile() {
    var sel = el('machine-setup-profile');
    if (!sel) return;
    machineProfiles.setActive(sel.value);
    var ta = el('machine-setup-ini');
    if (ta) ta.value = machineProfiles.ini();
    if (window.machineForm) machineForm.load(machineProfiles.ini(), machineProfiles.floppies(), machineProfiles.library()).then(applyLock);
    applyKindPanels();
    setResult('Showing "' + sel.value + '". Unsaved edits to the previous machine were discarded.', '');
  }

  // ---- machine kind: ND-100 vs. standalone ND-500 (NDIX) -------------------
  //
  // A machine's kind is fixed at creation - there is no "convert this ND-100
  // into an NDIX machine" operation, because they do not share a shape (one
  // is an INI, the other is a disc + memory size). Make another machine
  // instead. This just decides which panel is visible and keeps the NDIX
  // panel in sync with the active profile's saved settings.
  // ---- terminal settings: renderer, emulator, keyboard - per machine --------
  //
  // Saved the moment they change and applied at once (the console is rebuilt
  // while the machine is not powered on; the keyboard language is live), so
  // there is no Save button and no reload. An NDIX machine's console has to
  // be a VT100 (terminal-manager forces xterm for it), so for that kind the
  // renderer is shown locked.
  function applyTerminalPanel() {
    var t = machineProfiles.terminal();
    var kind = machineProfiles.kind();
    // One block of markup, shown inside whichever panel is up - above that
    // panel's buttons, with the rest of the machine's settings.
    var section = el('machine-setup-terminal-section');
    var slot = el(kind === 'nd500-ndix' ? 'machine-setup-ndix-terminal-slot' : 'machine-setup-nd100-terminal-slot');
    if (section && slot && section.parentNode !== slot) slot.appendChild(section);
    var backend = el('machine-setup-term-backend');
    var emulator = el('machine-setup-term-emulator');
    var language = el('machine-setup-term-language');
    var emuRow = el('machine-setup-term-emulator-row');
    var langRow = el('machine-setup-term-language-row');
    var note = el('machine-setup-term-note');
    if (!backend || !emulator || !language) return;
    backend.value = t.backend;
    emulator.value = t.emulator;
    language.value = t.language;
    var retro = (t.backend === 'retroterm');
    var ndix = (kind === 'nd500-ndix');
    backend.disabled = ndix;
    if (emuRow) emuRow.style.display = (retro && !ndix) ? 'flex' : 'none';
    if (langRow) langRow.style.display = (retro && !ndix && t.emulator !== 'vt100') ? 'flex' : 'none';
    if (note) note.textContent = ndix
      ? 'NDIX drives its console with ANSI/VT100 escapes: always the xterm.js VT100.'
      : (retro ? 'RetroTerm draws the TDV2200/2215 with its bitmap font; the keyboard language sets the ISO 646 national variant for keys and display.'
               : 'xterm.js is a VT100: no national variant, fonts and colours from the console header.');
  }

  function saveTerminalFromPanel() {
    var backend = el('machine-setup-term-backend');
    var emulator = el('machine-setup-term-emulator');
    var language = el('machine-setup-term-language');
    if (!backend || !emulator || !language) return;
    machineProfiles.writeTerminal({ backend: backend.value, emulator: emulator.value, language: language.value });
    applyTerminalPanel();
    // Rebuilds the console with the renderer and applies the keyboard.
    if (typeof window.applyMachineKindToToolbar === 'function') window.applyMachineKindToToolbar();
    setResult('Terminal settings saved to "' + machineProfiles.activeName() + '".', 'ok');
  }

  // Built-in machines are read-only: every control that would change one is
  // disabled and the note says to clone. Everything is re-enabled for a
  // machine of the user's own.
  function applyLock() {
    var locked = machineProfiles.isShipped(machineProfiles.activeName());
    var note = el('machine-setup-lock-note');
    if (note) note.style.display = locked ? '' : 'none';
    var win = el('machine-setup-window');
    if (!win) return;
    var keep = { 'machine-setup-profile': 1, 'machine-setup-new': 1, 'machine-setup-clone': 1, 'machine-setup-close': 1,
                 'machine-setup-advanced-toggle': 1, 'machine-setup-validate': 1, 'machine-setup-download': 1,
                 'machine-setup-new-name': 1, 'machine-setup-new-kind': 1, 'machine-setup-new-confirm': 1, 'machine-setup-new-cancel': 1 };
    var nodes = win.querySelectorAll('input, select, button, textarea');
    for (var i = 0; i < nodes.length; i++) {
      var n = nodes[i];
      if (keep[n.id]) continue;
      if (n.tagName === 'TEXTAREA') { n.readOnly = locked; continue; }
      n.disabled = locked;
    }
  }

  function cloneProfile() {
    var cur = machineProfiles.activeName();
    var name = prompt('Name for the copy of "' + cur + '":', cur + ' copy');
    if (name === null) return;
    var err = machineProfiles.clone(cur, name);
    if (err) { setResult(err, 'err'); return; }
    refreshProfiles();
    var ta = el('machine-setup-ini');
    if (ta) ta.value = machineProfiles.ini();
    if (window.machineForm) machineForm.load(machineProfiles.ini(), machineProfiles.floppies(), machineProfiles.library()).then(applyLock);
    applyKindPanels();
    setResult('Created "' + name + '" as a copy of "' + cur + '".', 'ok');
  }

  function applyKindPanels() {
    var kind = machineProfiles.kind();
    applyTerminalPanel();
    applyLock();
    // The toolbar shows Boot controls for an ND-100 and none for an NDIX
    // machine, and the active machine can change right here.
    if (typeof window.applyMachineKindToToolbar === 'function') window.applyMachineKindToToolbar();
    var nd100Panel = el('machine-setup-nd100-panel');
    var ndixPanel = el('machine-setup-ndix-panel');
    var label = el('machine-setup-kind-label');
    if (kind === 'nd500-ndix') {
      if (nd100Panel) nd100Panel.style.display = 'none';
      if (ndixPanel) ndixPanel.style.display = '';
      if (label) label.textContent = 'Type: ND-500 standalone (NDIX)';
      var s = machineProfiles.ndix();
      refreshNdixDiskOptions().then(function () {
        var disk = el('machine-setup-ndix-disk');
        if (disk && ndixLibraryMode()) disk.value = s.diskUuid ? ('lib:' + s.diskUuid) : (s.diskUrl ? 'demo' : '');
      });
      var mem = el('machine-setup-ndix-memory');
      if (mem) mem.value = String(s.memoryMb);
      var wr = el('machine-setup-ndix-writable');
      if (wr) wr.checked = !!s.writable;
    } else {
      if (nd100Panel) nd100Panel.style.display = '';
      if (ndixPanel) ndixPanel.style.display = 'none';
      if (label) label.textContent = 'Type: ND-100';
    }
  }

  // Where an NDIX root disc comes from depends on one thing: whether
  // persistent storage (the local disk library, a Worker-mode feature) is on.
  //   Off  - demo mode. The root disc IS the served demo image from the
  //          catalog (disk-catalog.json, diskType "nd500"); there is nothing
  //          to choose, so nothing is offered. It is loaded into memory at
  //          power-on, the same way the ND-100 demo fetches SMD0.IMG.
  //   On   - any image in the local library tagged "nd500". Anything not
  //          tagged nd500 is an ND-100 disc and would boot in a confusing way
  //          rather than an obvious one, so it is not offered.
  function ndixLibraryMode() {
    return typeof isSmdPersistenceEnabled === 'function' && isSmdPersistenceEnabled() &&
           typeof smdStorage !== 'undefined' && smdStorage.isAvailable();
  }

  function ndixCatalogEntries() {
    if (typeof smdEnsureCatalog !== 'function') return Promise.resolve([]);
    return smdEnsureCatalog().then(function (cat) {
      return (cat || []).filter(function (e) { return e.diskType === 'nd500' && e.url; });
    });
  }

  function refreshNdixDiskOptions() {
    var sel = el('machine-setup-ndix-disk');
    var row = el('machine-setup-ndix-disk-row');
    var demo = el('machine-setup-ndix-disk-demo');
    var note = el('machine-setup-ndix-disk-note');
    if (!sel) return Promise.resolve();
    var fmt = (typeof smdStorage !== 'undefined') ? smdStorage.formatSize : function (n) { return n + ' bytes'; };

    if (!ndixLibraryMode()) {
      if (row) row.style.display = 'none';
      if (note) note.style.display = 'none';
      return ndixCatalogEntries().then(function (entries) {
        var e = entries.length ? entries[0] : null;
        if (demo) {
          demo.style.display = '';
          demo.innerHTML = e
            ? 'Root disc: <b>' + e.name + '</b> (' + fmt(e.size) + ') - the server\'s demo image, loaded into memory at power-on. ' +
              '<span class="smd-image-meta" style="opacity:.6;">Turn on persistent disk storage (Config) to choose a library image instead.</span>'
            : '<span style="color:#FF8A8A;">The server catalog has no NDIX root disc.</span>';
        }
      });
    }

    if (demo) demo.style.display = 'none';
    if (row) row.style.display = '';
    var current = sel.value;
    sel.innerHTML = '<option value="">(none)</option>';
    // A profile naming a served image (the built-in 500 NDIX-C) boots that
    // one with the library on too; the list says so instead of "(none)".
    var cur = machineProfiles.ndix();
    if (cur.diskUrl) {
      var dm = document.createElement('option');
      dm.value = 'demo';
      dm.textContent = 'demo: ' + cur.diskUrl + ' (the server\'s image, loaded into memory at power-on)';
      sel.appendChild(dm);
    }
    var imgs = smdStorage.listImages(), n = 0;
    for (var j = 0; j < imgs.length; j++) {
      if ((imgs[j].diskType || '') !== 'nd500') continue;
      var l = document.createElement('option');
      l.value = 'lib:' + imgs[j].uuid;
      l.textContent = imgs[j].name + ' (' + fmt(imgs[j].size) + ')';
      sel.appendChild(l);
      n++;
    }
    sel.value = current;
    if (note) {
      note.style.display = n ? 'none' : '';
      note.textContent = 'No image tagged ND-500 / NDIX root in the local library. Copy the NDIX root disk in from the HDD Disk Manager\'s Server Catalog, or import one and tag it.';
    }
    return Promise.resolve();
  }

  // The dropdown's value, split into what the profile stores.
  function ndixDiskFromSelect(disk) {
    var v = disk ? disk.value : '';
    var name = (disk && disk.selectedIndex > 0) ? disk.options[disk.selectedIndex].textContent : '';
    if (v.indexOf('lib:') === 0) return { diskUuid: v.slice(4), diskName: name, demo: false };
    return { diskUuid: '', diskName: '', demo: v === 'demo' };
  }

  function saveNdix() {
    var name = machineProfiles.activeName();
    var mem = el('machine-setup-ndix-memory');
    var wr = el('machine-setup-ndix-writable');
    // In demo mode the root disc is the served image, kept as diskUrl; with
    // the library on, the chosen library image (diskUuid) and no URL -
    // unless the served image stays chosen ("demo:"), then diskUrl stays.
    var prev = machineProfiles.ndix(name);
    var d = ndixLibraryMode()
      ? ndixDiskFromSelect(el('machine-setup-ndix-disk'))
      : { diskUuid: '', diskName: prev.diskName, demo: true };
    var err = machineProfiles.writeNdix({
      diskUuid: d.diskUuid,
      diskUrl: d.demo ? (prev.diskUrl || '') : '',
      diskName: d.diskName,
      memoryMb: parseInt(mem && mem.value, 10) || 16,
      writable: !!(wr && wr.checked)
    }, name);
    if (err) setResult(err, 'err');
    else setResult('Saved to "' + name + '".', 'ok');
  }

  // "New" picks the kind up front via a small dialog, rather than prompt(),
  // so the choice is a deliberate selection and not a typed guess.
  function newProfile() {
    var dlg = el('machine-setup-new-dialog');
    var nameInput = el('machine-setup-new-name');
    var kindSelect = el('machine-setup-new-kind');
    if (!dlg || !nameInput || !kindSelect) return;
    nameInput.value = '';
    kindSelect.value = 'nd100';
    dlg.style.display = '';
    nameInput.focus();
  }

  function newProfileConfirm() {
    var dlg = el('machine-setup-new-dialog');
    var nameInput = el('machine-setup-new-name');
    var kindSelect = el('machine-setup-new-kind');
    if (!dlg || !nameInput || !kindSelect) return;
    var name = nameInput.value;
    var kind = kindSelect.value;
    // Start an ND-100 machine from what is on screen: "New" almost always
    // means "like this one, but ...", and starting from the default would
    // throw that away. A new NDIX machine has nothing on screen to start
    // from - normalizeNdix()'s defaults are the whole starting point.
    var ta = el('machine-setup-ini');
    if (kind === 'nd500-ndix') {
      // A new NDIX machine starts on the catalog's first served NDIX disc,
      // so it boots in demo mode as made - the way the default ND-100
      // machine starts on SMD0.IMG.
      ndixCatalogEntries().then(function (entries) {
        var first = entries.length ? entries[0] : null;
        finishCreate(machineProfiles.createNdix(name, first
          ? { diskUrl: first.url, diskName: first.name + ' (server)' } : null));
      });
      return;
    }
    finishCreate(machineProfiles.create(name, ta ? ta.value : null));

    function finishCreate(err) {
      if (err) { setResult(err, 'err'); return; }
      dlg.style.display = 'none';
      refreshProfiles();
      if (ta) ta.value = machineProfiles.ini();
      if (window.machineForm) machineForm.load(machineProfiles.ini(), machineProfiles.floppies(), machineProfiles.library()).then(applyLock);
      applyKindPanels();
      setResult('Created "' + name + '".', 'ok');
    }
  }

  function newProfileCancel() {
    var dlg = el('machine-setup-new-dialog');
    if (dlg) dlg.style.display = 'none';
  }

  function renameProfile() {
    var cur = machineProfiles.activeName();
    var name = prompt('Rename "' + cur + '" to:', cur);
    if (name === null) return;
    var err = machineProfiles.rename(cur, name);
    if (err) { setResult(err, 'err'); return; }
    refreshProfiles();
    setResult('Renamed to "' + name + '".', 'ok');
  }

  function deleteProfile() {
    var cur = machineProfiles.activeName();
    if (!confirm('Delete the machine "' + cur + '"? This cannot be undone.')) return;
    var err = machineProfiles.remove(cur);
    if (err) { setResult(err, 'err'); return; }
    refreshProfiles();
    var ta = el('machine-setup-ini');
    if (ta) ta.value = machineProfiles.ini();
    if (window.machineForm) machineForm.load(machineProfiles.ini(), machineProfiles.floppies(), machineProfiles.library()).then(applyLock);
    // Whatever is active now may be the other kind of machine.
    applyKindPanels();
    setResult('Deleted "' + cur + '".', 'ok');
  }

  function machineSetupShow() {
    var win = el('machine-setup-window');
    if (!win) return;
    var ta = el('machine-setup-ini');
    refreshProfiles();
    if (ta) ta.value = machineProfiles.ini();
    // Render the form too. Without this the window opened EMPTY and only filled
    // in once you toggled Advanced (INI) and back, because "Hide INI" was the
    // only path that ever called machineForm.load().
    if (window.machineForm) machineForm.load(machineProfiles.ini(), machineProfiles.floppies(), machineProfiles.library()).then(applyLock);
    applyKindPanels();
    setResult('', '');
    win.style.display = 'flex';
    if (typeof windowManager !== 'undefined') windowManager.focus('machine-setup-window');
  }

  function machineSetupHide() {
    var win = el('machine-setup-window');
    if (win) win.style.display = 'none';
  }

  function setResult(text, kind) {
    var r = el('machine-setup-result');
    if (!r) return;
    r.textContent = text;
    r.style.color = (kind === 'ok') ? '#7CFC7C' : (kind === 'err') ? '#FF8A8A' : '';
  }

  // Validate via the native parser (through the emu proxy). Returns a Promise.
  function validate() {
    var ta = el('machine-setup-ini');
    if (!ta) return Promise.resolve(false);
    syncFormToTextarea();
    if (typeof emu === 'undefined' || !emu.validateMachineINI) {
      setResult('Validation unavailable (emulator not ready).', 'err');
      return Promise.resolve(false);
    }
    setResult('Validating...', '');
    return Promise.resolve(emu.validateMachineINI(ta.value)).then(function (msg) {
      if (!msg) { setResult('Configuration is valid.', 'ok'); return true; }
      setResult(msg, 'err');
      return false;
    }).catch(function (e) {
      setResult('Validation error: ' + (e && e.message ? e.message : e), 'err');
      return false;
    });
  }

  // Which view the user is looking at decides what gets saved. Taking the
  // form's answer while the INI is on screen (or the reverse) would throw away
  // whichever one they had just been editing.
  function syncFormToTextarea() {
    var ta = el('machine-setup-ini');
    if (!ta || !window.machineForm || !machineForm.ready() || iniVisible()) return;
    var out = machineForm.toINI(ta.value);
    if (out !== null) ta.value = out;
  }

  function iniVisible() {
    var adv = el('machine-setup-advanced');
    return !!(adv && adv.style.display !== 'none');
  }

  function toggleAdvanced() {
    var adv = el('machine-setup-advanced');
    var btn = el('machine-setup-advanced-toggle');
    if (!adv) return;
    var showing = adv.style.display !== 'none';
    if (!showing) {
      // Going to the INI view: show what the form currently says, so the two
      // views never disagree about the machine in front of you.
      syncFormToTextarea();
      adv.style.display = '';
      if (btn) btn.textContent = 'Hide INI';
    } else {
      adv.style.display = 'none';
      if (btn) btn.textContent = 'Advanced (INI)';
      // Coming back from the INI view: the text is the truth now.
      var ta = el('machine-setup-ini');
      if (ta && window.machineForm) machineForm.load(ta.value);
    }
  }

  function save() {
    var ta = el('machine-setup-ini');
    if (!ta) return;
    syncFormToTextarea();
    // Only persist a valid config so a broken INI can't wedge the machine.
    validate().then(function (ok) {
      if (!ok) { setResult(el('machine-setup-result').textContent + '  (not saved)', 'err'); return; }
      var name = machineProfiles.activeName();
      if (machineProfiles.write(ta.value, name)) {
        // The catalog floppies the form's Library... buttons chose, kept
        // beside the INI (the INI names only the file).
        if (window.machineForm && machineForm.ready()) {
          machineProfiles.writeFloppies(machineForm.floppyPicks(), name);
          machineProfiles.writeLibrary(machineForm.libraryPicks(), name);
        }
        setResult('Saved to "' + name + '".', 'ok');
      }
      else setResult('Save failed (browser storage refused).', 'err');
    });
  }

  function reset() {
    var ta = el('machine-setup-ini');
    if (ta) ta.value = machineProfiles.DEFAULT_INI;
    if (window.machineForm) machineForm.load(machineProfiles.DEFAULT_INI);
    setResult('Reset to default (not yet saved).', '');
  }

  function download() {
    var ta = el('machine-setup-ini');
    if (!ta) return;
    syncFormToTextarea();
    var blob = new Blob([ta.value], { type: 'text/plain' });
    var url = URL.createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url;
    // Named after the machine, so a folder of downloads is still readable.
    a.download = machineProfiles.activeName().replace(/[^\w.-]+/g, '_') + '.ini';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
  }

  function wire() {
    var menu = el('menu-machine-setup');
    if (menu) menu.addEventListener('click', machineSetupShow);
    var close = el('machine-setup-close');
    if (close) close.addEventListener('click', machineSetupHide);
    var v = el('machine-setup-validate'); if (v) v.addEventListener('click', validate);
    var s = el('machine-setup-save');     if (s) s.addEventListener('click', save);
    var r = el('machine-setup-reset');    if (r) r.addEventListener('click', reset);
    var d = el('machine-setup-download'); if (d) d.addEventListener('click', download);
    var p = el('machine-setup-profile'); if (p) p.addEventListener('change', selectProfile);
    var n = el('machine-setup-new');     if (n) n.addEventListener('click', newProfile);
    var rn = el('machine-setup-rename'); if (rn) rn.addEventListener('click', renameProfile);
    var dl = el('machine-setup-delete'); if (dl) dl.addEventListener('click', deleteProfile);
    var adv = el('machine-setup-advanced-toggle'); if (adv) adv.addEventListener('click', toggleAdvanced);
    var nc = el('machine-setup-new-confirm'); if (nc) nc.addEventListener('click', newProfileConfirm);
    var nx = el('machine-setup-new-cancel');  if (nx) nx.addEventListener('click', newProfileCancel);
    var ns = el('machine-setup-ndix-save');   if (ns) ns.addEventListener('click', saveNdix);
    var cl = el('machine-setup-clone');       if (cl) cl.addEventListener('click', cloneProfile);
    ['machine-setup-term-backend', 'machine-setup-term-emulator', 'machine-setup-term-language'].forEach(function (id) {
      var s = el(id); if (s) s.addEventListener('change', saveTerminalFromPanel);
    });
    if (typeof makeDraggable === 'function') {
      var hdr = el('machine-setup-header');
      if (hdr) makeDraggable(el('machine-setup-window'), hdr, 'machine-setup-pos');
    }
    // Resizable, and the size is remembered - this window holds a machine's
    // whole hardware list, which is more than any fixed height suits. Same
    // shared helper the NDFS viewer, printer and paper tape use.
    if (typeof makeResizable === 'function') {
      var rz = el('machine-setup-resize');
      if (rz) makeResizable(el('machine-setup-window'), rz, 'machine-setup-size', 420, 320);
    }
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', wire);
  } else {
    wire();
  }

  // Expose for other modules / tests.
  window.machineSetupShow = machineSetupShow;
  window.machineSetupHide = machineSetupHide;
  window.machineSetupValidate = validate;
})();
