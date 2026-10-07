// machine-form.js - the Machine Setup form.
//
// The window used to be a raw INI textarea. Fine if you already know the key
// names; not much use if you just want an ND-110 with two discs.
//
// TWO RULES SHAPE THIS FILE.
//
// 1. READING GOES THROUGH THE C PARSER. The form never interprets INI itself.
//    It calls DescribeMachineINI (src/frontend/nd100wasm), which runs the same
//    MachineConfig_LoadFile the native binary runs and hands back JSON. A
//    second parser written in JavaScript would drift from the first, and then
//    the form would show one machine while the emulator built another.
//
// 2. WRITING PRESERVES WHAT IT DOES NOT UNDERSTAND. The form regenerates only
//    the sections it owns - [machine], [controller.*], [terminals],
//    [peripheral.*], [boot] - and carries every other section over verbatim.
//    [runtime] alone holds the telnet port, throttle, charset, print and tape
//    directories, the drum and CDC images and the memory size. A form that
//    regenerated the file from scratch would delete all of it the first time
//    somebody clicked Save, and nothing would say so.
//
// The generated text still goes through the C validator before it is saved, so
// a form that produces nonsense is caught by the same check a hand-typed file
// gets.

(function () {
  'use strict';

  function el(id) { return document.getElementById(id); }

  // The INI spells the Winchester "wd"; nobody calls it that out loud. These
  // are for the screen only - every value written to the file is the INI name.
  var PRETTY = {
    floppy: 'Floppy',
    smd:    'SMD disc',
    wd:     'Winchester (ST506)',
    scsi:   'SCSI',
    hdlc:   'HDLC'
  };

  function pretty(type) { return PRETTY[type] || type; }

  // ---- INI section surgery ------------------------------------------------
  // Deliberately NOT a parser: it splits on section headers and nothing else.
  // Understanding keys is the C code's job (rule 1); all this needs to know is
  // where one section stops and the next begins.

  // [mfbus], [mfbus.part.N], [controller.octobus.N] and [nd5000.N] are owned
  // too: the ND-5000 checkbox writes them. Before that, [controller.octobus.0]
  // already matched "controller." and so was neither carried over nor
  // written - a machine with an ND-5000 lost it on the first Save.
  var OWNED = /^\[(machine|controller\.|terminals|peripheral\.|boot|mfbus|nd5000\.)/i;

  function foreignSections(ini) {
    var lines = (ini || '').split('\n');
    var out = [], keep = false, cur = [];
    for (var i = 0; i < lines.length; i++) {
      var m = /^\s*\[([^\]]+)\]/.exec(lines[i]);
      if (m) {
        if (keep && cur.length) out.push(cur.join('\n'));
        cur = [];
        keep = !OWNED.test('[' + m[1]);
      }
      if (keep) cur.push(lines[i]);
    }
    if (keep && cur.length) out.push(cur.join('\n'));
    return out;
  }

  // ---- rendering ----------------------------------------------------------

  var current = null;   // the last JSON description, for regenerating

  // Catalog floppies chosen for the floppy drives, by slot: {file, name,
  // imageUrl, md5}. Seeded from the profile by load(), changed by the
  // Library... buttons, and read back by machine-setup.js on Save.
  var picks = {};

  // A MEMFS-safe file name for a catalog floppy, from its name: upper case,
  // letters/digits/-/_ only, .IMG. Written into the INI's disk<n> = line.
  function pickFileName(floppy) {
    var base = String(floppy.Name || 'FLOPPY').toUpperCase().replace(/[^A-Z0-9_-]+/g, '-')
                 .replace(/^-+|-+$/g, '').slice(0, 40) || 'FLOPPY';
    return base + '.IMG';
  }

  // Library images chosen for the drives, by "type.wheel.slot":
  // {uuid, file, name}. Seeded by load(), changed by the slot selects, read
  // back by machine-setup.js on Save; toolbar.js mounts them at power-on.
  var libPicks = {};

  // Persistent storage on and a library to pick from.
  function libraryMode() {
    return typeof isSmdPersistenceEnabled === 'function' && isSmdPersistenceEnabled() &&
           typeof smdStorage !== 'undefined' && smdStorage.isAvailable();
  }

  // The INI controller type's diskType tag in the library.
  var LIBRARY_TYPE_OF = { smd: 'smd', scsi: 'scsi', wd: 'winchester', floppy: 'floppy' };

  function libraryImagesFor(ctrlType) {
    var want = LIBRARY_TYPE_OF[ctrlType];
    if (!want || typeof smdStorage === 'undefined') return [];
    var out = [];
    smdStorage.listImages().forEach(function (img) {
      if ((img.diskType || 'smd') !== want) return;
      out.push({ uuid: img.uuid, name: img.name || img.uuid, sizeText: smdStorage.formatSize(img.size || 0) });
    });
    return out;
  }

  // A MEMFS-safe file name for a library image, from its name (the INI's
  // disk<n> = line), the same way archive floppies are named.
  function libraryFileName(name) {
    var base = String(name || 'DISK').toUpperCase().replace(/[^A-Z0-9_-]+/g, '-')
                 .replace(/^-+|-+$/g, '').slice(0, 40) || 'DISK';
    return base + '.IMG';
  }

  // A slot's library select changed: the hidden file field and the pick
  // follow, the boot drive list is rebuilt.
  function libraryChanged(ctrlIndex, slot, type, wheel) {
    var sel = el('mf-c' + ctrlIndex + '-l' + slot);
    var input = el('mf-c' + ctrlIndex + '-d' + slot);
    if (!sel || !input) return;
    var key = type + '.' + wheel + '.' + slot;
    var v = sel.value;
    if (v.indexOf('lib:') === 0) {
      var uuid = v.slice(4), meta = smdStorage.getMetadata(uuid);
      var name = (meta && meta.name) || uuid;
      var file = libraryFileName(name);
      libPicks[key] = { uuid: uuid, file: file, name: name };
      input.value = file;
      if (type === 'floppy') delete picks[slot];
    } else if (v === '') {
      delete libPicks[key];
      if (type === 'floppy') delete picks[slot];
      input.value = '';
    }
    // 'archive' / 'file' keep what the slot has.
    refreshBootSelect();
  }

  function pickForSlot(ctrlIndex, slot) {
    if (typeof openFloppyBrowser !== 'function') return;
    openFloppyBrowser({ pick: function (floppy) {
      var file = pickFileName(floppy);
      picks[slot] = { file: file, name: floppy.Name || file, imageUrl: floppy.ImageUrl || '', md5: floppy.Md5 || '' };
      delete libPicks['floppy.0.' + slot];
      var input = el('mf-c' + ctrlIndex + '-d' + slot);
      if (input) { input.value = file; input.dispatchEvent(new Event('input')); }
      // In library mode the slot's select shows the archive choice.
      var sel = el('mf-c' + ctrlIndex + '-l' + slot);
      if (sel) {
        var o = sel.querySelector('option[value="archive"]');
        if (!o) { o = document.createElement('option'); o.value = 'archive'; sel.appendChild(o); }
        o.textContent = 'archive: ' + (floppy.Name || file);
        sel.value = 'archive';
      }
      var note = el('mf-c' + ctrlIndex + '-pn' + slot);
      if (note) note.textContent = sel ? '' : 'archive: ' + (floppy.Name || file);
      refreshBootSelect();
    } });
  }

  function opt(value, label, selected) {
    return '<option value="' + value + '"' + (selected ? ' selected' : '') + '>' +
           (label || value) + '</option>';
  }

  function checkbox(id, label, checked) {
    return '<label style="display:inline-flex;align-items:center;gap:4px;margin-right:12px;">' +
           '<input type="checkbox" id="' + id + '"' + (checked ? ' checked' : '') + '> ' +
           label + '</label>';
  }

  function esc(s) {
    return String(s == null ? '' : s)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  function textInput(id, value, width) {
    return '<input type="text" id="' + id + '" value="' + (value || '').replace(/"/g, '&quot;') +
           '" style="width:' + (width || '150px') + ';font-size:12px;padding:2px;">';
  }

  function renderMachine(d) {
    var h = '<div class="smd-section-title">CPU and clock</div><div style="margin-bottom:10px;">';
    h += '<label style="margin-right:12px;">Model ';
    h += '<select id="mf-cpu" style="font-size:12px;padding:2px;">';
    // Straight from the CPU's own table (cpu_model.c), so the list cannot go
    // stale when a model is added to the enum.
    for (var i = 0; i < d.cpuModels.length; i++)
      h += opt(d.cpuModels[i], d.cpuModels[i], d.cpuModels[i] === d.machine.cpu);
    h += '</select></label>';
    h += '<label style="margin-right:12px;">FPP <select id="mf-fpp" style="font-size:12px;padding:2px;">' +
         opt('48', '48-bit', d.machine.fpp === 48) + opt('32', '32-bit', d.machine.fpp === 32) +
         '</select></label>';
    h += '<label>RTC <select id="mf-rtc" style="font-size:12px;padding:2px;">' +
         opt('ticks', 'instruction ticks', d.machine.rtc === 'ticks') +
         opt('wall', 'wall clock 20 ms', d.machine.rtc === 'wall') +
         '</select></label>';
    h += '</div>';

    // The ND-5000 on the octobus. One checkbox for the three sections it
    // takes ([mfbus] + [mfbus.part.0], [controller.octobus.0], [nd5000.1]),
    // because they are one thing: no CPU without the bus and the shared
    // memory, and no point in either without the CPU. The CPU is created at
    // power-on and sits idle until SINTRAN's ND-500 monitor starts it.
    h += '<div class="smd-section-title">ND-5000</div><div style="margin-bottom:10px;">';
    h += checkbox('mf-nd5000', 'ND-5000 CPU on the octobus (8 MB MFbus shared memory, station 070B)', hasNd5000(d));
    h += '</div>';
    return h;
  }

  function hasNd5000(d) {
    if (!d.octobus || !d.octobus.enabled || !d.nd5000 || !d.nd5000.length) return false;
    for (var i = 0; i < d.nd5000.length; i++) if (d.nd5000[i].enabled) return true;
    return false;
  }

  function renderControllers(d) {
    var h = '<div class="smd-section-title">Controllers</div>';
    if (!d.controllers.length)
      h += '<div class="smd-image-meta" style="opacity:.7;">None in this configuration.</div>';
    for (var i = 0; i < d.controllers.length; i++) {
      var c = d.controllers[i];
      var id = 'mf-c' + i;
      h += '<div style="border:1px solid rgba(255,255,255,.12);border-radius:4px;padding:6px;margin-bottom:6px;">';
      h += '<div style="margin-bottom:4px;">' +
           checkbox(id + '-en', '<b>' + pretty(c.type) + '</b> (thumbwheel ' + c.wheel + ')', c.enabled) +
           '</div>';
      if (c.type === 'hdlc') {
        // NO mode/host/port here, on purpose. A browser cannot open a TCP
        // socket, and the emulator does not pretend otherwise: modem.c builds
        // with MODEM_HAS_NETWORKING undefined under __EMSCRIPTEN__, and
        // wasm_bind_hdlc() in nd100wasm.c puts every HDLC channel on the
        // WebSocket gateway bridge whatever the config asked for. Offering a
        // "server (listen)" mode would be offering something that cannot
        // happen. Enable it or don't - the traffic goes over the gateway.
        // The values themselves are still carried into the written INI, since
        // the native binary reads the same file and there they are real.
        h += '<div class="smd-image-meta" style="opacity:.75;">' +
             'All traffic goes over the <b>gateway</b> (WebSocket). ' +
             'The browser cannot listen on or dial a TCP port itself.';
        if (c.hdlcMode === 'client' && c.hdlcHost)
          h += '<br>Kept for a native run: client ' + esc(c.hdlcHost) + ':' + (c.hdlcPort || 0) + '.';
        else if (c.hdlcPort)
          h += '<br>Kept for a native run: ' + esc(c.hdlcMode || 'server') + ' port ' + c.hdlcPort + '.';
        h += '</div>';
      } else if (c.diskSlots > 0) {
        var lib = libraryMode();
        var libImages = lib ? libraryImagesFor(c.type) : [];
        for (var j = 0; j < c.diskSlots && j < c.disks.length; j++) {
          var k = c.disks[j];
          var key = c.type + '.' + c.wheel + '.' + j;
          var lp = libPicks[key];
          var pk = (c.type === 'floppy' && c.wheel === 0) ? picks[j] : null;
          var file = k.present ? k.image : '';
          var fromLib = !!(lp && file && file === lp.file);
          var fromArchive = !!(pk && file && file === pk.file);
          h += '<div style="display:flex;gap:6px;align-items:center;margin-top:3px;">';
          h += '<span style="opacity:.7;width:52px;">disk' + j + '</span>';
          if (lib) {
            // Persistent storage on: the slot is a library image of this
            // controller's type (the machine is the master of its drives).
            // The INI's file name rides along in a hidden field.
            h += '<input type="hidden" id="' + id + '-d' + j + '" value="' + esc(file) + '">';
            h += '<select id="' + id + '-l' + j + '" style="font-size:12px;padding:2px;min-width:220px;">';
            h += opt('', '(empty)', !file);
            for (var li = 0; li < libImages.length; li++) {
              h += opt('lib:' + libImages[li].uuid, libImages[li].name + ' (' + libImages[li].sizeText + ')',
                       fromLib && lp.uuid === libImages[li].uuid);
            }
            if (fromArchive) h += opt('archive', 'archive: ' + pk.name, true);
            else if (file && !fromLib) h += opt('file', 'file: ' + file + ' (not in the library)', true);
            h += '</select>';
          } else {
            // Demo mode: the drives hold the server's demo images, by name,
            // and that is not a choice - there is no library to choose from.
            h += '<input type="text" id="' + id + '-d' + j + '" value="' + esc(file) + '" readonly ' +
                 'title="Demo mode: the server\'s image. Turn on persistent disk storage to choose a library image." ' +
                 'style="width:190px;font-size:12px;padding:2px;opacity:.7;">';
          }
          if (c.type === 'floppy' && c.wheel === 0) {
            // A catalog floppy for this drive: the Floppy Library opens in
            // pick mode and the choice is fetched at power-on (toolbar.js).
            h += '<button class="smd-action-btn" id="' + id + '-p' + j + '" title="Choose a floppy from the Norsk Data software archive">Archive...</button>';
            h += '<span class="smd-image-meta" id="' + id + '-pn' + j + '" style="opacity:.7;">' +
                 ((fromArchive && !lib) ? 'archive: ' + esc(pk.name) : '') + '</span>';
          }
          if (c.type === 'scsi') {
            h += '<select id="' + id + '-m' + j + '" style="font-size:12px;padding:2px;">' +
                 opt('hdd', 'hdd', k.media === 'hdd') +
                 opt('cdrom', 'cdrom', k.media === 'cdrom') +
                 opt('tape', 'tape', k.media === 'tape') +
                 opt('floppy', 'floppy', k.media === 'floppy') + '</select>';
          }
          h += '</div>';
        }
        h += '<div class="smd-image-meta" style="opacity:.6;margin-top:3px;">' +
             (lib ? 'Images come from the local disk library (HDD Disk Manager), by type. Leave a slot empty for no disk.'
                  : 'Demo mode: the server\'s images. Turn on persistent disk storage (Config) to choose library images.') + '</div>';
      }
      h += '</div>';
    }

    // Everything the machine COULD have and does not. Without this the form
    // can only edit controllers the file already named, so a config that never
    // mentioned Winchester could never grow one - which is most of them, since
    // the default machine is floppy + SMD + SCSI.
    var missing = [];
    for (var t = 0; t < (d.controllerTypes || []).length; t++) {
      var td = d.controllerTypes[t];
      for (var w = td.minWheel; w <= td.maxWheel; w++) {
        var have = false;
        for (var k = 0; k < d.controllers.length; k++)
          if (d.controllers[k].type === td.type && d.controllers[k].wheel === w) have = true;
        if (!have) missing.push({ type: td.type, wheel: w });
      }
    }
    if (missing.length) {
      h += '<div style="display:flex;gap:6px;align-items:center;margin-top:4px;">';
      h += '<select id="mf-add" style="font-size:12px;padding:2px;">';
      for (var m = 0; m < missing.length; m++) {
        var lbl = pretty(missing[m].type);
        // Only say "thumbwheel N" where there is a choice of N to make.
        var multi = false;
        for (var q = 0; q < missing.length; q++)
          if (missing[q].type === missing[m].type && missing[q].wheel !== missing[m].wheel) multi = true;
        if (multi) lbl += ' (thumbwheel ' + missing[m].wheel + ')';
        h += opt(missing[m].type + '.' + missing[m].wheel, lbl, false);
      }
      h += '</select>';
      h += '<button class="smd-action-btn" id="mf-add-btn">Add controller</button>';
      h += '</div>';
    }
    return h;
  }

  // Add the selected controller to the machine being edited, then re-render so
  // it gets its image fields. It goes in ENABLED with empty slots: adding a
  // controller you then have to tick on as well is a step with no meaning, and
  // an empty slot is simply a drive with no disk in it.
  //
  // Nothing is written anywhere yet - Save still generates the INI and the C
  // validator still checks it. That matters for the Winchester in particular:
  // it answers IOX 500-507, the same block as the CDC system disc, so a machine
  // can have one or the other. If this config has a CDC image the validator
  // will say so, in its own words, at Save.
  function addController() {
    if (!current) return;
    var sel = el('mf-add');
    if (!sel || !sel.value) return;
    var parts = sel.value.split('.');
    var type = parts[0], wheel = parseInt(parts[1], 10);

    var td = null;
    for (var i = 0; i < (current.controllerTypes || []).length; i++)
      if (current.controllerTypes[i].type === type) td = current.controllerTypes[i];
    if (!td) return;

    var disks = [];
    for (var j = 0; j < 8; j++) disks.push({ slot: j, present: false, media: 'hdd', image: '' });

    current.controllers.push({
      type: type, wheel: wheel, enabled: true,
      isDisc: td.isDisc, bootable: td.bootable, diskSlots: td.diskSlots,
      disks: disks,
      hdlcMode: 'server', hdlcHost: '', hdlcPort: 5000 + wheel
    });

    // Keep the form's current answers: re-rendering from `current` alone would
    // throw away anything typed since it was loaded.
    var host = el('machine-setup-form');
    var keep = readForm();
    render(current);
    applyForm(keep);
    if (host) host.scrollTop = host.scrollHeight;
  }

  // The form's current values, by element id. Used to survive a re-render.
  function readForm() {
    var host = el('machine-setup-form');
    var out = {};
    if (!host) return out;
    var nodes = host.querySelectorAll('input, select');
    for (var i = 0; i < nodes.length; i++) {
      var n = nodes[i];
      if (!n.id) continue;
      out[n.id] = (n.type === 'checkbox') ? n.checked : n.value;
    }
    return out;
  }

  function applyForm(values) {
    for (var id in values) {
      if (!Object.prototype.hasOwnProperty.call(values, id)) continue;
      var n = el(id);
      if (!n) continue;                       // gone after the re-render
      if (n.type === 'checkbox') n.checked = values[id];
      else n.value = values[id];
    }
  }

  function renderRest(d) {
    var h = '<div class="smd-section-title">Terminals</div>';
    h += '<div style="margin-bottom:10px;">' +
         '<label>Thumbwheels ' + textInput('mf-terminals', d.terminals.join(', '), '220px') + '</label>' +
         '<div class="smd-image-meta" style="opacity:.6;">The console is always there; these are the extra lines.</div>' +
         '</div>';

    h += '<div class="smd-section-title">Peripherals</div><div style="margin-bottom:10px;">';
    h += checkbox('mf-ptr', 'Paper tape reader', d.peripherals.papertapeReader);
    h += checkbox('mf-ptp', 'Paper tape punch', d.peripherals.papertapePunch);
    h += checkbox('mf-lpt', 'Line printer', d.peripherals.linePrinter);
    h += '</div>';

    h += '<div class="smd-section-title">Boot drive</div><div style="margin-bottom:6px;">';
    h += '<select id="mf-boot" style="font-size:12px;padding:2px;min-width:220px;">';
    h += bootOptionsHTML(d, bootValueOf(d.boot));
    h += '</select>';
    h += '<div class="smd-image-meta" style="opacity:.6;margin-top:3px;">Only drives on enabled controllers. ' +
         'With no boot drive the machine powers on with nothing loaded.</div>';
    h += '</div>';
    return h;
  }

  // The [boot] device as the select's value: "type.wheel.unit", or "none".
  function bootValueOf(boot) {
    if (!boot || boot.none || !boot.isDisc) return 'none';
    return boot.type + '.' + boot.wheel + '.' + boot.unit;
  }

  // The boot drives on offer: every unit of every ENABLED bootable disc
  // controller, plus "no boot drive". Enabled is read off the form's own
  // checkbox when it is on screen, so unticking a controller takes its
  // drives out of this list at once - a boot device on a disabled controller
  // is what the validator refuses at Save.
  // <fromForm>: read enabled/image off the form's own controls (a refresh
  // after a tick or a keystroke). During render() the controls on screen
  // still belong to the PREVIOUS machine, so the first list is built from
  // the description alone.
  function bootOptionsHTML(d, selected, fromForm) {
    var h = opt('none', '(no boot drive)', selected === 'none');
    var found = (selected === 'none');
    for (var i = 0; i < d.controllers.length; i++) {
      var c = d.controllers[i];
      if (!c.bootable || !c.isDisc) continue;
      var en = fromForm ? el('mf-c' + i + '-en') : null;
      if (en ? !en.checked : !c.enabled) continue;
      for (var j = 0; j < c.diskSlots && j < c.disks.length; j++) {
        var v = c.type + '.' + c.wheel + '.' + j;
        var img = fromForm ? el('mf-c' + i + '-d' + j) : null;
        var image = img ? img.value : (c.disks[j].present ? c.disks[j].image : '');
        var sel = (v === selected);
        if (sel) found = true;
        h += opt(v, pretty(c.type) + ' ' + c.wheel + ' unit ' + j + (image ? '  (' + image + ')' : '  (empty)'), sel);
      }
    }
    // The saved device is on a controller that is now off, or not in this
    // machine at all: say so rather than silently choosing another drive.
    if (!found) h += opt(selected, selected + '  (controller disabled - choose another)', true);
    return h;
  }

  // Rebuild the boot drive list after a controller is ticked on or off, or a
  // disk image typed in, keeping the current choice when it still exists.
  function refreshBootSelect() {
    if (!current) return;
    var sel = el('mf-boot');
    if (!sel) return;
    var selected = sel.value || 'none';
    sel.innerHTML = bootOptionsHTML(current, selected, true);
  }

  function render(d) {
    var host = el('machine-setup-form');
    if (!host) return;
    if (d.error) {
      host.innerHTML = '<div class="smd-image-meta" style="color:#FF8A8A;">' +
        'This configuration cannot be shown as a form: ' + d.error +
        '<br>Fix it in Advanced (INI) below.</div>';
      current = null;
      return;
    }
    current = d;
    host.innerHTML = renderMachine(d) + renderControllers(d) + renderRest(d);
    // innerHTML replaces the nodes, so the handler is attached here rather than
    // once at startup - there is no button to attach to until now.
    var add = el('mf-add-btn');
    if (add) add.addEventListener('click', addController);
    // A controller ticked off takes its drives out of the boot list; an
    // image typed into a slot shows up in it.
    for (var i = 0; i < d.controllers.length; i++) {
      var en = el('mf-c' + i + '-en');
      if (en) en.addEventListener('change', refreshBootSelect);
      for (var j = 0; j < 8; j++) {
        var img = el('mf-c' + i + '-d' + j);
        if (img) img.addEventListener('input', refreshBootSelect);
        var pb = el('mf-c' + i + '-p' + j);
        if (pb) pb.addEventListener('click', (function (ci, sj) { return function () { pickForSlot(ci, sj); }; })(i, j));
        var ls = el('mf-c' + i + '-l' + j);
        if (ls) ls.addEventListener('change', (function (ci, sj, t, w) { return function () { libraryChanged(ci, sj, t, w); }; })(i, j, d.controllers[i].type, d.controllers[i].wheel));
      }
    }
  }

  // ---- form -> INI --------------------------------------------------------

  function val(id, dflt) { var e = el(id); return e ? e.value : (dflt || ''); }
  function chk(id) { var e = el(id); return !!(e && e.checked); }

  function toINI(previousIni) {
    if (!current) return null;
    var d = current, out = [];

    out.push('# nd100x machine configuration');
    out.push('# Written by the Machine Setup form. Sections are [type.thumbwheel].');
    out.push('');
    out.push('[machine]');
    out.push('cpu = ' + val('mf-cpu', 'ND100'));
    out.push('fpp = ' + val('mf-fpp', '48'));
    out.push('rtc = ' + val('mf-rtc', 'ticks'));
    out.push('');

    for (var i = 0; i < d.controllers.length; i++) {
      var c = d.controllers[i], id = 'mf-c' + i;
      out.push('[controller.' + c.type + '.' + c.wheel + ']');
      out.push('enabled = ' + (chk(id + '-en') ? 'yes' : 'no'));
      if (c.type === 'hdlc') {
        // Straight off the parsed config, not off the screen - the form does
        // not show these (see renderControllers) because they are meaningless
        // in a browser. They are written anyway so the same .ini still works
        // when run natively, where the modem really does open a socket.
        out.push('mode = ' + (c.hdlcMode === 'client' ? 'client' : 'server'));
        if (c.hdlcMode === 'client' && c.hdlcHost) out.push('host = ' + c.hdlcHost);
        if (c.hdlcPort) out.push('port = ' + c.hdlcPort);
      } else {
        for (var j = 0; j < c.diskSlots && j < c.disks.length; j++) {
          var img = val(id + '-d' + j, '');
          if (!img) continue;
          // SCSI slots carry a media type; everything else is a plain path.
          if (c.type === 'scsi') out.push('disk' + j + ' = ' + val(id + '-m' + j, 'hdd') + ':' + img);
          else                   out.push('disk' + j + ' = ' + img);
        }
      }
      out.push('');
    }

    out.push('[terminals]');
    out.push('enabled = ' + val('mf-terminals', ''));
    out.push('');

    out.push('[peripheral.papertape-reader]');
    out.push('enabled = ' + (chk('mf-ptr') ? 'yes' : 'no'));
    out.push('');
    out.push('[peripheral.papertape-punch]');
    out.push('enabled = ' + (chk('mf-ptp') ? 'yes' : 'no'));
    out.push('');
    out.push('[peripheral.lineprinter]');
    out.push('enabled = ' + (chk('mf-lpt') ? 'yes' : 'no'));
    out.push('');

    out.push('[boot]');
    out.push('device = ' + (val('mf-boot', 'none') || 'none'));
    out.push('');

    if (chk('mf-nd5000')) {
      // Same sections ND5000.ini ships with. Naming a section is what
      // enables it, so an unticked box writes none of them.
      out.push('[mfbus]');
      out.push('size      = 8');
      out.push('base_page = 04100B');
      out.push('');
      out.push('[mfbus.part.0]');
      out.push('pages   = 4096');
      out.push('nd100   = yes');
      out.push('nd500_p = yes');
      out.push('nd500_d = yes');
      out.push('');
      out.push('[controller.octobus.0]');
      out.push('enabled = yes');
      out.push('');
      out.push('[nd5000.1]');
      out.push('enabled = yes');
      out.push('station = 070B');
      out.push('');
    }

    // Rule 2: everything the form does not own comes across untouched.
    var carried = foreignSections(previousIni);
    if (carried.length) {
      out.push('# Sections below are not edited by the form and are kept as they were.');
      out.push('');
      for (var k = 0; k < carried.length; k++) { out.push(carried[k]); out.push(''); }
    }

    return out.join('\n');
  }

  // ---- public -------------------------------------------------------------

  window.machineForm = {

    /* Show <ini> as a form. Asks the C parser what it means; on a parse error
     * says so and leaves the user with the INI view, which is where a broken
     * config has to be fixed anyway. */
    load: function (ini, floppies, library) {
      picks = {};
      if (floppies && typeof floppies === 'object') {
        for (var k in floppies) if (Object.prototype.hasOwnProperty.call(floppies, k)) picks[k] = floppies[k];
      }
      libPicks = {};
      if (library && typeof library === 'object') {
        for (var lk in library) if (Object.prototype.hasOwnProperty.call(library, lk)) libPicks[lk] = library[lk];
      }
      if (typeof emu === 'undefined' || !emu.describeMachineINI) {
        var host = el('machine-setup-form');
        if (host) host.innerHTML = '<div class="smd-image-meta" style="opacity:.7;">' +
          'The form needs the emulator module (not loaded yet). Use Advanced (INI) below.</div>';
        current = null;
        return Promise.resolve(false);
      }
      return Promise.resolve(emu.describeMachineINI(ini)).then(function (json) {
        var d;
        try { d = JSON.parse(json); }
        catch (e) { d = { error: 'could not read the machine description' }; }
        render(d);
        return !d.error;
      });
    },

    /* The form as INI, with unowned sections carried over from <previousIni>.
     * null when there is nothing rendered - the caller keeps its own text. */
    toINI: toINI,

    /* Is there a rendered form to read? */
    ready: function () { return !!current; },

    /* The library choices still in force: a slot whose disk<n> file no longer
     * names the chosen image is a plain file again. For
     * machineProfiles.writeLibrary(). */
    libraryPicks: function () {
      var out = {};
      if (!current) return libPicks;
      for (var i = 0; i < current.controllers.length; i++) {
        var c = current.controllers[i];
        if (!c.isDisc) continue;
        for (var j = 0; j < c.diskSlots && j < c.disks.length; j++) {
          var key = c.type + '.' + c.wheel + '.' + j, lp = libPicks[key];
          if (!lp) continue;
          var input = el('mf-c' + i + '-d' + j);
          var file = input ? input.value : (c.disks[j].present ? c.disks[j].image : '');
          if (file === lp.file) out[key] = lp;
        }
      }
      return out;
    },

    /* The catalog-floppy choices still in force: a slot whose disk<n> text
     * no longer names the chosen file has been edited by hand and is a local
     * file again. For machineProfiles.writeFloppies(). */
    floppyPicks: function () {
      var out = {};
      if (!current) return picks;
      for (var i = 0; i < current.controllers.length; i++) {
        var c = current.controllers[i];
        if (c.type !== 'floppy' || c.wheel !== 0) continue;
        for (var j = 0; j < 3; j++) {
          var pk = picks[j];
          if (!pk) continue;
          var input = el('mf-c' + i + '-d' + j);
          var file = input ? input.value : (c.disks[j] && c.disks[j].present ? c.disks[j].image : '');
          if (file === pk.file) out[j] = pk;
        }
      }
      return out;
    },

    /* Exposed for tests/test_machine_form.js. This is the one piece of INI
     * handling that lives in JavaScript, and getting it wrong deletes
     * [runtime] - the telnet port, throttle, charset, drum and CDC images and
     * the memory size - the first time somebody presses Save. It is worth a
     * test of its own. */
    _foreignSections: foreignSections
  };
})();
