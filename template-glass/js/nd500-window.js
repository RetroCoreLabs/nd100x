//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// HackerCorp Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// nd500-window.js - the ND-500 console.
//
// The ND-500 has run inside this module since M2c, but only from the browser
// console: emu.nd500.create(), loadKernel(), boot(), step() by hand. This is
// the window that does it, and the terminal that shows the result.
//
// WHAT IT IS. A kernel, optionally its pre-split segment files, and up to four
// disc images go in; NDIX boots and you get a login prompt. The ND-500 runs on
// its own here, answering its own front-end calls the way nd500x does natively
// (front_end = synthetic). It is NOT connected to the ND-100 in the same page -
// no shared memory, no 3022 bus interface. That is a later milestone, and
// pretending otherwise in the UI would be a lie about what the machine is.
//
// WHERE THE FILES COME FROM. Two places, and both are honest about themselves:
//   - the local disk library (the same OPFS store the HDD manager uses), which
//     is where a catalog download lands;
//   - a file picker, because a kernel is not in the catalog - there is no
//     vmunix on the server, and the one people actually boot is the one they
//     just built.
//
// WHY IT STEPS ON A TIMER. The page has one thread. nd500x's run() does not
// come back until the guest stops, so the window asks for a slice of
// instructions at a time and gives the browser its thread back in between.

(function () {
  'use strict';

  // How much CPU to spend per tick, and how often. 300k instructions at ~4.5M/s
  // measured under node is ~65 ms of work - too long for one frame, so the
  // interval is deliberately longer than a frame and the page stays responsive
  // while the guest runs at a useful speed. Both are adjustable on screen.
  var SLICE_DEFAULT = 300000;
  var TICK_MS = 40;

  var win, header, closeBtn, resizeHandle;
  var consoleEl, statusEl, bootBtn, stopBtn, sliceInput;
  var kernelSel, kernelFile, psegFile, dsegFile, diskSel, diskFile, memSel;
  var writableBox;

  var timer = null;
  var booted = false;
  var pendingKernel = null;    // Uint8Array
  var pendingPseg = null, pendingDseg = null;
  var pendingDisk = null;
  var term = null;          // xterm.js Terminal for the guest console
  var resizeTerm = null;    // terminal-core's re-fit for it
  var pendingOut = '';      // output that arrived before the terminal existed
  var lastSlice = -1;       // last slice sent to the Worker, so it is sent once

  function workerMode() {
    return !!(window.emu && emu.isWorkerMode && emu.isWorkerMode());
  }

  function el(id) { return document.getElementById(id); }

  function setStatus(text, kind) {
    if (!statusEl) return;
    statusEl.textContent = text;
    statusEl.style.color = (kind === 'err') ? '#FF8A8A'
                         : (kind === 'ok') ? '#7CFC7C' : '';
  }

  // ---- console ------------------------------------------------------------

  // A real VT100, because NDIX drives this console with one. vi, more, stty and
  // the login prompt all address the cursor; the plain text node this window
  // used before turned every escape sequence into visible garbage and could
  // only ever append. xterm.js is the VT100 - RetroTerm, the other terminal in
  // this page, ships only the TDV2200 (lib/retroterm/emulators/ has "tdv" and
  // nothing else), which is SINTRAN's terminal, not NDIX's. So this window
  // asks terminal-core for xterm outright with forceXterm and does not follow
  // the ND-100's backend setting.
  //
  // Created on the first write or the first open, never at init: xterm needs a
  // laid-out container to measure a cell, and this window starts hidden.
  function ensureTerm() {
    if (term || !consoleEl) return term;
    if (typeof window.createScaledTerminal !== 'function' || typeof Terminal === 'undefined')
      return null;
    var inst = window.createScaledTerminal(consoleEl, {
      forceXterm: true,
      fontFamily: "'Courier Prime','Fira Mono',monospace",
      colorTheme: 'green',
      observeResize: consoleEl
    });
    term = inst.term;
    resizeTerm = inst.resizeTerminal;
    // Typing goes straight to guest tty unit 0. onData is the byte stream the
    // terminal itself produces, so control keys, arrows and the escape
    // sequences a VT100 sends arrive already encoded - which the hand-rolled
    // keydown handler this replaces could not do.
    term.onData(function (data) {
      if (!booted || !window.emu || !emu.nd500) return;
      emu.nd500.sendInput(0, data);
    });
    if (pendingOut) { term.write(pendingOut); pendingOut = ''; }
    return term;
  }

  // Guest output is a byte stream and goes to the terminal untouched - the
  // escape sequences in it are the point. Output that arrives before the
  // terminal exists is held, not dropped, so the boot log is never lost.
  function write(text) {
    var t = ensureTerm();
    if (t) { t.write(text); return; }

    pendingOut += text;
    // Held output is only held while a terminal is still possible. If xterm
    // never loaded - a blocked CDN is the realistic case - the window would
    // otherwise go permanently blank, which is worse than losing the escape
    // sequences. Fall back to plain text so a boot failure is still readable.
    if (typeof Terminal === 'undefined' && document.readyState === 'complete' && consoleEl) {
      consoleEl.style.whiteSpace = 'pre-wrap';
      consoleEl.style.overflow = 'auto';
      consoleEl.style.font = "12px 'Courier Prime','Fira Mono',monospace";
      consoleEl.textContent = pendingOut.replace(/\x1b\[[0-9;?]*[A-Za-z]/g, '');
      consoleEl.scrollTop = consoleEl.scrollHeight;
    }
  }

  // The emulator's own log lines end in a bare LF. A terminal in the state
  // NDIX leaves it in does not do an implicit carriage return, so without this
  // every "| " line would start where the last one ended.
  function writeLog(text) {
    write(text.replace(/\r?\n/g, '\r\n'));
  }

  function drain() {
    if (!window.emu || !emu.nd500) return;
    var chunks = emu.nd500.pollConsole();
    for (var i = 0; i < chunks.length; i++) {
      var c = chunks[i];
      // Unit 255 is the emulator's own boot log, not the guest talking. It is
      // shown, because when a boot dies the last line printed is how you know
      // which step it died in - but marked, so it is never mistaken for NDIX.
      if (c.unit === 255) writeLog(c.text.replace(/^/gm, '| '));
      else write(c.text);
    }
  }

  // ---- the run loop -------------------------------------------------------

  function tick() {
    if (!booted) return;
    var slice = parseInt(sliceInput && sliceInput.value, 10) || SLICE_DEFAULT;
    try {
      // In Worker mode the Worker owns the stepping - both CPUs share one wasm
      // module, so a second stepper here would run the guest twice per frame.
      // step() there only carries the slice, so send it when it CHANGES rather
      // than 25 times a second. Draining the console still happens every tick.
      if (workerMode()) {
        if (slice !== lastSlice) { lastSlice = slice; emu.nd500.step(slice); }
      } else {
        emu.nd500.step(slice);
      }
    } catch (e) {
      stop();
      setStatus('stopped: ' + (e && e.message ? e.message : e), 'err');
      return;
    }
    drain();
    // The run FLAG, not the stop reason. NDIX takes page faults constantly -
    // that is what demand paging is - and each one leaves a stop reason behind
    // while the machine carries on perfectly happily. Only the flag going away
    // means the CPU actually stopped.
    if (!emu.nd500.isRunning()) {
      stop();
      setStatus('stopped: ' + emu.nd500.stopReason(), 'err');
    }
  }

  function start() {
    if (timer) return;
    timer = setInterval(tick, TICK_MS);
  }

  function stop() {
    if (timer) { clearInterval(timer); timer = null; }
    if (stopBtn) stopBtn.textContent = 'Run';
  }

  // ---- loading ------------------------------------------------------------

  function readFile(input) {
    return new Promise(function (resolve, reject) {
      var f = input && input.files && input.files[0];
      if (!f) { resolve(null); return; }
      var r = new FileReader();
      r.onload = function () { resolve(new Uint8Array(r.result)); };
      r.onerror = function () { reject(r.error); };
      r.readAsArrayBuffer(f);
    });
  }

  // Images the local library holds that make sense for an ND-500. The catalog
  // tags an NDIX root image diskType "nd500"; anything untagged is an ND-100
  // disc and is not offered here, because mounting an SMD image as an NDIX root
  // produces a boot that fails in a confusing way rather than an obvious one.
  function refreshLibrary() {
    if (!diskSel) return;
    diskSel.innerHTML = '<option value="">(none - or choose a file below)</option>';
    if (typeof smdStorage === 'undefined' || !smdStorage.isAvailable()) return;
    var imgs = smdStorage.listImages();
    for (var i = 0; i < imgs.length; i++) {
      if ((imgs[i].diskType || '') !== 'nd500') continue;
      var o = document.createElement('option');
      o.value = imgs[i].uuid;
      o.textContent = imgs[i].name + ' (' + smdStorage.formatSize(imgs[i].size) + ')';
      diskSel.appendChild(o);
    }
  }

  async function boot() {
    if (!window.emu || !emu.nd500 || !emu.nd500.available()) {
      setStatus('This build has no ND-500.', 'err');
      return;
    }
    if (emu.isWorkerMode && emu.isWorkerMode()) {
      setStatus('The ND-500 needs direct mode; Worker mode does not forward it yet.', 'err');
      return;
    }
    if (booted) {
      setStatus('Already booted. Reload the page to boot a different kernel.', 'err');
      return;
    }

    try {
      setStatus('reading files...');
      pendingKernel = await readFile(kernelFile);
      pendingPseg = await readFile(psegFile);
      pendingDseg = await readFile(dsegFile);

      pendingDisk = await readFile(diskFile);
      if (!pendingDisk && diskSel && diskSel.value) {
        setStatus('reading the root disc out of the library...');
        pendingDisk = await smdStorage.retrieveImage(diskSel.value);
      }

      if (!pendingKernel && !pendingDisk) {
        setStatus('Choose a kernel or a root disc (or both).', 'err');
        return;
      }

      setStatus('creating the machine...');
      var mb = parseInt(memSel && memSel.value, 10) || 16;
      if (emu.nd500.create(mb * 1024 * 1024) !== 0) {
        setStatus('could not create the ND-500', 'err');
        return;
      }

      if (pendingKernel) {
        if (emu.nd500.loadKernel(pendingKernel) !== 0) {
          setStatus('could not stage the kernel', 'err');
          return;
        }
        // BOTH or NEITHER. With an incomplete pair the library derives the sizes
        // from the a.out header instead, which is what a kernel taken out of a
        // disc image has to do anyway.
        if (pendingPseg && pendingDseg) emu.nd500.loadSegments(pendingPseg, pendingDseg);
        writeLog('| kernel: ' + pendingKernel.length + ' bytes\n');
      } else {
        writeLog('| no kernel provided - will attempt to boot from disc\n');
      }

      if (pendingDisk) {
        var wr = !!(writableBox && writableBox.checked);
        emu.nd500.mountDisk(0, pendingDisk, wr);
        writeLog('| root disc: ' + pendingDisk.length + ' bytes, ' +
              (wr ? 'WRITABLE' : 'read-only') + '\n');
      } else {
        writeLog('| no root disc\n');
      }

      setStatus('booting...');
      var rc = emu.nd500.boot();
      drain();
      if (rc !== 0) { setStatus('boot failed - see the log above', 'err'); return; }

      booted = true;
      if (bootBtn) bootBtn.disabled = true;
      if (stopBtn) { stopBtn.disabled = false; stopBtn.textContent = 'Pause'; }
      setStatus('running', 'ok');
      start();
    } catch (e) {
      setStatus('error: ' + (e && e.message ? e.message : e), 'err');
    }
  }

  // ---- wiring -------------------------------------------------------------

  function init() {
    win          = el('nd500-window');
    if (!win) return;
    header       = el('nd500-header');
    closeBtn     = el('nd500-close');
    resizeHandle = el('nd500-resize');
    consoleEl    = el('nd500-console');
    statusEl     = el('nd500-status');
    bootBtn      = el('nd500-boot');
    stopBtn      = el('nd500-stop');
    sliceInput   = el('nd500-slice');
    kernelFile   = el('nd500-kernel-file');
    psegFile     = el('nd500-pseg-file');
    dsegFile     = el('nd500-dseg-file');
    diskSel      = el('nd500-disk-select');
    diskFile     = el('nd500-disk-file');
    memSel       = el('nd500-memory');
    writableBox  = el('nd500-writable');

    if (typeof makeDraggable === 'function' && header) makeDraggable(win, header, 'nd500-pos');
    if (typeof makeResizable === 'function' && resizeHandle)
      makeResizable(win, resizeHandle, 'nd500-size', 520, 340);

    if (closeBtn) closeBtn.addEventListener('click', function () {
      // Closing hides the window; it does NOT stop the machine. A guest that
      // vanishes because a window was closed is not what anyone means by close.
      if (typeof closeWindow === 'function') closeWindow('nd500-window');
    });

    var menu = el('menu-nd500');
    if (menu) menu.addEventListener('click', function () {
      if (typeof openWindow === 'function') openWindow('nd500-window');
      // Only now does the container have a size to measure.
      setTimeout(function () { ensureTerm(); if (resizeTerm) resizeTerm(); }, 0);
      refreshLibrary();
      report();
      // After refreshLibrary, so the <select> already holds the options that
      // applyConfig is about to pick from.
      if (!booted) applyConfig();
    });

    if (bootBtn) bootBtn.addEventListener('click', boot);
    if (stopBtn) stopBtn.addEventListener('click', function () {
      if (timer) { stop(); setStatus('paused'); }
      else if (booted) { start(); stopBtn.textContent = 'Pause'; setStatus('running', 'ok'); }
    });

    // The terminal takes its own keyboard through term.onData; clicking the
    // window just puts the focus where xterm expects it.
    if (consoleEl) {
      consoleEl.addEventListener('click', function () {
        var t = ensureTerm();
        if (t) t.focus();
      });
    }
  }

  // ---- what the selected machine says -------------------------------------

  // Pre-fill from the active profile's [nd500] section, so the window and the
  // machine configuration cannot quietly disagree about the same machine.
  //
  // Paths are the awkward part and are handled honestly: a browser cannot open
  // one, so an image named in the config is matched against the local library
  // BY NAME. No match means the window SAYS which image the machine asked for
  // rather than booting without it and letting the guest fail later.
  function applyConfig() {
    if (typeof machineProfiles === 'undefined' || !window.emu || !emu.describeMachineINI)
      return Promise.resolve();
    return Promise.resolve(emu.describeMachineINI(machineProfiles.ini()))
      .then(function (json) {
        var d;
        try { d = JSON.parse(json); } catch (e) { return; }
        var n = d && d.nd500;
        if (!n || !n.enabled) {
          writeLog('| the selected machine "' + machineProfiles.activeName() +
                '" has no [nd500] section - nothing pre-filled\n');
          return;
        }
        writeLog('| from the machine configuration "' + machineProfiles.activeName() + '":\n');
        if (n.memoryMb && memSel) {
          // Only offered sizes. A config asking for something not in the list
          // is worth saying out loud rather than silently rounding.
          var found = false;
          for (var i = 0; i < memSel.options.length; i++)
            if (parseInt(memSel.options[i].value, 10) === n.memoryMb) { found = true; break; }
          if (found) { memSel.value = String(n.memoryMb); writeLog('|   memory ' + n.memoryMb + ' MB\n'); }
          else writeLog('|   memory ' + n.memoryMb + ' MB is not one of the sizes here - left alone\n');
        }
        if (n.kernel)
          writeLog('|   kernel "' + n.kernel + '" - choose the file yourself; ' +
                'the page cannot open a path\n');

        var root = (n.disks && n.disks.length) ? n.disks[0] : null;
        if (root) {
          if (writableBox) writableBox.checked = !!root.writable;
          var matched = selectLibraryByName(root.image);
          writeLog('|   root disc "' + root.image + '" (' +
                (root.writable ? 'writable' : 'read-only') + ')' +
                (matched ? ' - found in the library\n'
                         : ' - NOT in the local library; download it or pick a file\n'));
        }
      })
      .catch(function () { /* a broken profile is the config window's problem */ });
  }

  // Match a configured image name against the library. Exact name first, then
  // a case-insensitive compare, because a catalog entry and an .ini written by
  // hand disagree about capitals more often than they disagree about the file.
  function selectLibraryByName(name) {
    if (!name || !diskSel || typeof smdStorage === 'undefined' || !smdStorage.isAvailable())
      return false;
    var imgs = smdStorage.listImages();
    var want = String(name).toLowerCase();
    for (var i = 0; i < imgs.length; i++) {
      if ((imgs[i].diskType || '') !== 'nd500') continue;
      if (String(imgs[i].name).toLowerCase() !== want) continue;
      diskSel.value = imgs[i].uuid;
      return diskSel.value === imgs[i].uuid;
    }
    return false;
  }

  // Say up front whether this build even has an ND-500, rather than letting
  // Boot fail with it later.
  function report() {
    if (!window.emu || !emu.nd500) { setStatus('the emulator is not ready yet'); return; }
    if (!emu.nd500.available()) {
      setStatus('This build has no ND-500 (built without an nd500x checkout).', 'err');
      if (bootBtn) bootBtn.disabled = true;
      return;
    }
    setStatus('ready - choose a kernel and press Boot');
  }

  if (document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', init);
  else
    init();

  window.nd500Window = { refreshLibrary: refreshLibrary, report: report,
                       applyConfig: applyConfig };
})();
