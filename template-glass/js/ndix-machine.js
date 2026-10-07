//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// HackerCorp Labs - https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// ndix-machine.js - a standalone ND-500 running NDIX, as a machine kind.
//
// A machine profile (machine-profiles.js) is either an ND-100 or an
// 'nd500-ndix' machine; the Power button in toolbar.js asks machineProfiles
// which one is active and hands an NDIX machine to this module. There is no
// ND-100 in that machine at all - no Init(), no MFbus, no octobus - and
// there is no window of its own either: the ND-500's tty 0 is terminal 1,
// the same console the ND-100 would have used, and ttys 1-3 are terminals
// 2-4. One console, one set of terminals, whichever machine is running.
//
// WHAT BOOTS. The root disc out of the local disk library (an image tagged
// "nd500" - the catalog's NDIX root disk), and nothing else: Nd500_Boot()
// takes /vmunix out of that disc itself, the same order nd500x uses
// natively. No kernel, .pseg or .dseg to pick, and no file picker - the
// library is the only source.
//
// WHY IT STEPS ON A TIMER. The page has one thread and nd500x's run() does
// not come back until the guest stops, so in Direct mode this asks for a
// slice of instructions at a time. In Worker mode the Worker steps the
// ND-500 in its own loop (emu-worker.js) and this only drains the console.

(function () {
  'use strict';

  // Instructions per tick, and how often. 300k at ~4.5M/s is ~65 ms of work,
  // too long for one frame, so the interval is longer than a frame and the
  // page stays responsive while the guest runs at a useful speed.
  var SLICE = 300000;
  var TICK_MS = 40;

  // tty 0 is terminal 1 - the console - and tty u is terminal u+1. Terminals
  // for ttys 1-3 are made at power-on with the console; any further tty gets
  // one the first time it produces output (ensureTerminalForUnit). The
  // emulator's console queue carries a unit byte, so up to 255 ttys, with
  // 255 itself being the boot log.
  var TTY_COUNT = 4;
  var ND500_TTY_LIMIT = 255;
  var LOG_UNIT = 255;        // the emulator's own boot log, not the guest

  var created = false;       // emu.nd500.create() has been called
  var booted = false;        // Nd500_Boot() returned 0 and the CPU is stepped
  var timer = null;
  var warnedUnits = {};

  function workerMode() {
    return !!(window.emu && emu.isWorkerMode && emu.isWorkerMode());
  }

  function isSelected() {
    return typeof machineProfiles !== 'undefined' && machineProfiles.kind() === 'nd500-ndix';
  }

  function setStatus(text) {
    var s = document.getElementById('status');
    if (s) s.textContent = text;
  }

  // ---- terminals ------------------------------------------------------------

  function identCodeOfUnit(unit) { return unit + 1; }
  function unitOfIdentCode(identCode) { return identCode - 1; }

  // Terminals 2-4 for ttys 1-3; initializeTerminals() (terminal-manager)
  // creates these alongside the console when this machine is selected.
  function ttyTerminals() {
    var out = [];
    for (var u = 1; u < TTY_COUNT; u++) out.push({ identCode: identCodeOfUnit(u), name: 'tty' + u });
    return out;
  }

  // Guest output is a byte stream and goes to the terminal untouched - the
  // escape sequences in it are the point. A popped-out terminal takes it a
  // byte at a time through the bridge, the way ND-100 output reaches it.
  function writeTo(identCode, text) {
    if (typeof window.isPoppedOut === 'function' && window.isPoppedOut(identCode)) {
      for (var i = 0; i < text.length; i++) window.bufferPopoutOutput(identCode, text.charCodeAt(i));
      return true;
    }
    var t = (typeof terminals !== 'undefined') ? terminals[identCode] : null;
    if (!t || !t.term) return false;
    t.term.write(text);
    return true;
  }

  // The emulator's own lines end in a bare LF, and a terminal in the state
  // NDIX leaves it in does not do an implicit carriage return. Prefixed "| "
  // so a boot-log line is never mistaken for the guest talking.
  function writeLog(text) {
    writeTo(1, text.replace(/^/gm, '| ').replace(/\r?\n/g, '\r\n'));
  }

  // A tty beyond the initial set gets its terminal the first time it speaks.
  // How many gettys an NDIX image runs is the image's business (the one
  // shipped runs them on ttys 0-8); a fixed count here would either drop
  // output or open windows for ttys that never say anything.
  function ensureTerminalForUnit(unit) {
    var id = identCodeOfUnit(unit);
    if (typeof terminals !== 'undefined' && terminals[id]) return true;
    if (typeof createTerminal !== 'function' || unit >= ND500_TTY_LIMIT) return false;
    createTerminal(id, 'tty' + unit);
    if (typeof updateTerminalSubmenu === 'function') updateTerminalSubmenu();
    return !!(terminals[id]);
  }

  function drain() {
    if (!window.emu || !emu.nd500) return;
    var chunks = emu.nd500.pollConsole();
    for (var i = 0; i < chunks.length; i++) {
      var c = chunks[i];
      if (c.unit === LOG_UNIT) { writeLog(c.text); continue; }
      var id = identCodeOfUnit(c.unit);
      if (!terminals[id] && !ensureTerminalForUnit(c.unit) && !warnedUnits[c.unit]) {
        warnedUnits[c.unit] = true;
        console.warn('[NDIX] output on tty ' + c.unit + ' has no terminal ' + id + ' - dropped');
        continue;
      }
      writeTo(id, c.text);
    }
  }

  // Keys arrive one code at a time from terminal-manager's key handler, the
  // same path the ND-100 uses - escape sequences come as their bytes in
  // order, so arrows and control keys reach the guest intact.
  function sendKey(identCode, keyCode) {
    if (!booted || !window.emu || !emu.nd500) return false;
    emu.nd500.sendInput(unitOfIdentCode(identCode), String.fromCharCode(keyCode));
    return true;
  }

  // ---- the run loop ----------------------------------------------------------

  function tick() {
    if (!booted) return;
    try {
      // Worker mode: the Worker steps the guest itself; the slice was sent
      // once at boot. Stepping here too would run the guest twice per tick.
      if (!workerMode()) emu.nd500.step(SLICE);
    } catch (e) {
      stop();
      writeLog('ND-500 stopped: ' + (e && e.message ? e.message : e) + '\n');
      setStatus('NDIX stopped');
      return;
    }
    drain();
    // The run FLAG, not the stop reason. NDIX takes page faults constantly -
    // that is what demand paging is - and each one leaves a stop reason
    // behind while the machine carries on. Only the flag going away means
    // the CPU actually stopped.
    if (!emu.nd500.isRunning()) {
      stop();
      writeLog('ND-500 stopped: ' + emu.nd500.stopReason() + '\n');
      setStatus('NDIX stopped: ' + emu.nd500.stopReason());
    }
  }

  function start() {
    if (timer) return;
    timer = setInterval(tick, TICK_MS);
  }

  function stop() {
    if (timer) { clearInterval(timer); timer = null; }
  }

  // ---- power on = create, mount the root disc, boot, run ----------------------
  //
  // One step, not Power then Boot like the ND-100: this machine has one disc
  // type and boots from it, so there is no boot choice to offer. Resolves
  // true when the guest is running; false means the console says why.
  function fail(text) {
    writeLog(text + '\n');
    setStatus('NDIX: ' + text);
    console.error('[NDIX] ' + text);
    return false;
  }

  // The root disc's bytes: out of the local disk library when the profile
  // names an image there, otherwise fetched from the server - the catalog's
  // own file, which is all demo mode has (the library is a Worker-mode
  // thing). Same two sources the ND-100's SMD unit 0 has.
  // Persistent storage on: the library image the profile names - that is
  // the only choice there is in that mode. Off (demo mode): the served
  // demo image, the catalog's NDIX root disc; the profile's diskUrl, or the
  // catalog's first nd500 entry when the profile has none.
  function libraryMode() {
    return typeof isSmdPersistenceEnabled === 'function' && isSmdPersistenceEnabled() &&
           typeof smdStorage !== 'undefined' && smdStorage.isAvailable();
  }

  function demoDiscUrl(s) {
    if (s.diskUrl) return Promise.resolve(s.diskUrl);
    if (typeof smdEnsureCatalog !== 'function') return Promise.resolve('');
    return smdEnsureCatalog().then(function (cat) {
      for (var i = 0; i < (cat || []).length; i++) {
        if (cat[i].diskType === 'nd500' && cat[i].url) return cat[i].url;
      }
      return '';
    });
  }

  function loadRootDisc(s) {
    if (libraryMode()) {
      if (!s.diskUuid) {
        return Promise.reject(new Error('no root disc chosen - pick a library image tagged NDIX in Machine Setup'));
      }
      setStatus('NDIX: reading the root disc out of the library...');
      return smdStorage.retrieveImage(s.diskUuid);
    }
    return demoDiscUrl(s).then(function (url) {
      if (!url) throw new Error('the server catalog has no NDIX root disc to boot');
      setStatus('NDIX: downloading the root disc ' + url + '...');
      return fetch(url).then(function (resp) {
        if (!resp.ok) throw new Error('root disc ' + url + ' is not on the server (HTTP ' + resp.status + ')');
        return resp.arrayBuffer();
      });
    }).then(function (buf) { return new Uint8Array(buf); });
  }

  function powerOn() {
    if (!window.emu || !emu.nd500 || !emu.nd500.available()) {
      // Terminals first, so there is somewhere for the message to go.
      initializeTerminals();
      return Promise.resolve(fail('this build has no ND-500 (built without an nd500x checkout)'));
    }
    if (created) {
      return Promise.resolve(fail('already powered on - reload the page to boot again'));
    }
    var s = machineProfiles.ndix();

    // The console and the tty terminals. initializeTerminals() asks this
    // module (isSelected) which extra terminals to make and that they are
    // VT100s - NDIX drives its console with one.
    initializeTerminals();
    if (typeof switchTerminal === 'function') switchTerminal(1);

    writeLog('ND-500 standalone, NDIX. Memory ' + s.memoryMb + ' MB, ' +
             (workerMode() ? 'Worker' : 'Direct') + ' mode.\n');

    return loadRootDisc(s).then(function (bytes) {
      if (!bytes || !bytes.length) {
        return fail('root disc "' + (s.diskName || s.diskUuid || s.diskUrl) + '" could not be read');
      }
      writeLog('root disc: ' + (s.diskName || s.diskUuid) + ', ' + bytes.length + ' bytes, ' +
               (s.writable ? 'WRITABLE' : 'read-only') + '\n');

      created = true;
      setStatus('NDIX: creating the machine...');
      if (emu.nd500.create(s.memoryMb * 1024 * 1024) !== 0) {
        return fail('could not create the ND-500');
      }
      if (emu.nd500.mountDisk(0, bytes, !!s.writable) !== 0) {
        return fail('could not mount the root disc');
      }

      setStatus('NDIX: booting...');
      // Direct mode answers with the return code; Worker mode with a promise
      // of the nd500Result message, whose rc is the real one.
      return Promise.resolve(emu.nd500.boot()).then(function (r) {
        var rc = (typeof r === 'number') ? r : (r && typeof r.rc === 'number' ? r.rc : -1);
        drain();
        if (rc !== 0) return fail('boot failed (rc ' + rc + ') - see the log above');
        booted = true;
        if (workerMode()) emu.nd500.step(SLICE);   // the slice, once; the Worker steps
        start();
        setStatus('NDIX running');
        if (typeof terminals !== 'undefined' && terminals[1]) terminals[1].term.focus();
        return true;
      });
    }).catch(function (e) {
      return fail('error: ' + (e && e.message ? e.message : e));
    });
  }

  window.ndixMachine = {
    isSelected: isSelected,
    isActive: function () { return created; },
    isBooted: function () { return booted; },
    ttyTerminals: ttyTerminals,
    sendKey: sendKey,
    consoleTitle: function () { return 'Console (VT100) - NDIX'; },
    powerOn: powerOn,
    stop: stop
  };
})();
