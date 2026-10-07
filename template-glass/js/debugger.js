//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// debugger.js - ND-100 Debugger Panel
// Organized into sections:
//   1. State
//   2. Utilities
//   3. Button / status management
//   4. Window management (toggle, close, drag, position)
//   5. Tab switching
//   6. Execution control (step helper, run, pause, exec loop)
//   7. Auto-refresh timer
//   8. View refresh (dispatch + individual renderers)
//   9. Keyboard shortcuts
//  10. Memory dump
//  11. Main-page integration hooks
//  12. Init

(function() {
  'use strict';

  /* --------------------------------------------------
     1. State
     -------------------------------------------------- */
  var visible       = false;
  var paused        = true;
  var active        = false;
  var prevRegs      = {};
  window.dbgBreakpoints = window.dbgBreakpoints || new Set();
  var breakpoints   = window.dbgBreakpoints;
  var refreshTimer  = null;
  var execLoopId    = null;
  var prevVariables = {};
  var expandedScopes = {};
  var levelDesc = [
    'Idle (DUMMY)',    // 0
    'RT/Background',   // 1
    'Monitor',         // 2
    'Segment Admin',   // 3
    'I/O Monitor',     // 4
    'XMSG',            // 5
    'Direct Task 1',   // 6
    'Direct Task 2',   // 7
    'Direct Task 3',   // 8
    'Direct Task 4',   // 9
    'Terminal out',    // 10
    'Mass storage',    // 11
    'Terminal in',     // 12
    'RTC/HDLC',        // 13
    'Internal int',    // 14
    'Fast user int'    // 15
  ];

  /* --------------------------------------------------
     2. Utilities
     -------------------------------------------------- */
  function oct(val) {
    return ('000000' + ((val >>> 0) & 0xFFFF).toString(8)).slice(-6);
  }

  function parseOctal(str) {
    return parseInt(str, 8) || 0;
  }

  /* Register setter mapping (key = short name used in dbg-r-<key> IDs) */
  var regSetters = {
    p:   'setPC',
    a:   'setRegA',
    d:   'setRegD',
    b:   'setRegB',
    t:   'setRegT',
    l:   'setRegL',
    x:   'setRegX',
    sts: 'setSTS'
    /* ea is read-only */
  };
  var activeEdit = null; /* regKey currently being edited, or null */

  function wasmReady() {
    return emu && emu.isReady();
  }

  function wasmCall(fnName) {
    return emu && emu.isReady();
  }

  function formatCount(n) {
    if (n >= 1000000) return (n / 1000000).toFixed(1) + 'M';
    if (n >= 1000)    return (n / 1000).toFixed(1) + 'K';
    return n.toString();
  }

  /* --------------------------------------------------
     2b. Register inline editing
     -------------------------------------------------- */
  function startRegEdit(regKey) {
    if (activeEdit) cancelRegEdit();
    if (!regSetters[regKey]) return; /* read-only */

    var valEl = document.getElementById('dbg-r-' + regKey);
    if (!valEl) return;

    activeEdit = regKey;
    var curVal = valEl.textContent;

    var input = document.createElement('input');
    input.type = 'text';
    input.className = 'reg-edit-input';
    input.value = curVal;
    input.maxLength = 6;
    input.setAttribute('spellcheck', 'false');

    input.addEventListener('keydown', function(e) {
      if (e.key === 'Enter') { e.preventDefault(); commitRegEdit(); }
      else if (e.key === 'Escape') { e.preventDefault(); cancelRegEdit(); }
      e.stopPropagation(); /* don't trigger debugger shortcuts */
    });
    input.addEventListener('blur', function() {
      /* small delay so click-away works cleanly */
      setTimeout(function() { if (activeEdit === regKey) commitRegEdit(); }, 80);
    });

    valEl.textContent = '';
    valEl.appendChild(input);
    input.focus();
    input.select();
  }

  function commitRegEdit() {
    if (!activeEdit) return;
    var regKey = activeEdit;
    var valEl = document.getElementById('dbg-r-' + regKey);
    var input = valEl ? valEl.querySelector('.reg-edit-input') : null;

    if (input) {
      var newVal = parseOctal(input.value.trim());
      var setterName = regSetters[regKey];
      if (setterName && emu[setterName]) {
        emu[setterName](newVal & 0xFFFF);
      }
    }
    activeEdit = null;
    renderRegisters();
    renderFlags();
  }

  function cancelRegEdit() {
    if (!activeEdit) return;
    activeEdit = null;
    renderRegisters();
  }

  /* --------------------------------------------------
     3. Central state sync
     -------------------------------------------------- */
  var dbgBtnIds = ['dbg-run','dbg-step','dbg-step10','dbg-step-over','dbg-step-out'];

  function syncState() {
    var running = (active && !paused) ||
                  (typeof emulationRunning !== 'undefined' && emulationRunning);

    document.getElementById('dbg-pause').disabled = !running;
    for (var i = 0; i < dbgBtnIds.length; i++) {
      document.getElementById(dbgBtnIds[i]).disabled = running;
    }

    var el = document.getElementById('dbg-status');
    el.textContent = running ? 'RUNNING' : (active ? 'PAUSED' : 'IDLE');
    el.className = 'debugger-header-status ' + (running ? 'running' : 'paused');

  }

  /* --------------------------------------------------
     4. Window management
     -------------------------------------------------- */
  // Expose show/hide for toolbar.js
  window.dbgShowWindow = showWindow;
  window.dbgHideWindow = hideWindow;

  function showWindow() {
    visible = true;
    document.getElementById('debugger-window').style.display = 'flex';
    restorePosition();
    refreshView();
    startRefreshTimer();
  }

  function hideWindow() {
    visible = false;
    document.getElementById('debugger-window').style.display = 'none';
    stopRefreshTimer();
  }

  function savePosition() {
    if (typeof saveWindowPosition === 'function') saveWindowPosition('debugger-window');
  }

  function restorePosition() {
    if (typeof restoreWindowPosition === 'function') restoreWindowPosition('debugger-window');
  }

  // Close button
  document.getElementById('dbg-close').addEventListener('click', hideWindow);

  // Draggable header
  (function() {
    var win = document.getElementById('debugger-window');
    var dragging = false, ox = 0, oy = 0;

    document.getElementById('debugger-header').addEventListener('mousedown', function(e) {
      if (e.target.closest('.debugger-header-close')) return;
      dragging = true;
      ox = e.clientX - win.getBoundingClientRect().left;
      oy = e.clientY - win.getBoundingClientRect().top;
      e.preventDefault();
    });
    document.addEventListener('mousemove', function(e) {
      if (!dragging) return;
      win.style.left  = Math.max(0, Math.min(window.innerWidth - 100, e.clientX - ox)) + 'px';
      win.style.top   = Math.max(49, Math.min(window.innerHeight - 60, e.clientY - oy)) + 'px';
      win.style.right = 'auto';
    });
    document.addEventListener('mouseup', function() {
      if (dragging) { dragging = false; savePosition(); }
    });
  })();

  /* --------------------------------------------------
     5. Tab switching
     -------------------------------------------------- */
  document.querySelectorAll('.debugger-tab').forEach(function(tab) {
    tab.addEventListener('click', function() {
      document.querySelectorAll('.debugger-tab').forEach(function(t) { t.classList.remove('active'); });
      document.querySelectorAll('.debugger-tab-content').forEach(function(c) { c.classList.remove('active'); });
      tab.classList.add('active');
      var target = tab.getAttribute('data-tab');
      document.querySelector('.debugger-tab-content[data-tab="' + target + '"]').classList.add('active');
      refreshView();
    });
  });

  /* --------------------------------------------------
     6. Execution control
     -------------------------------------------------- */
  function takeControl() {
    if (typeof stopEmulation === 'function' &&
        typeof emulationRunning !== 'undefined' && emulationRunning) {
      stopEmulation();
    }
    active = true;
  }

  function syncFromCpu() {
    if (!active) return;
    if (wasmReady()) {
      try { paused = !!emu.isPaused(); } catch(e) {}
    }
  }

  /* Map old WASM function names to emu proxy methods */
  var stepMethodMap = {
    '_Dbg_StepOne':  'stepOne',
    '_Dbg_StepOver': 'stepOver',
    '_Dbg_StepOut':  'stepOut'
  };

  function doStep(wasmFn, count) {
    if (!wasmReady()) return;
    var method = stepMethodMap[wasmFn];
    if (!method || !emu[method]) return;
    takeControl();
    paused = true;

    if (emu.isWorkerMode()) {
      // Worker mode: step is async via postMessage
      emu.onStepDone = function(msg) {
        syncFromCpu();
        syncState();
        refreshView();
        emu.onStepDone = null;
      };
      // stepOne/stepOver/stepOut return Promises in Worker mode
      emu[method]();
      syncState();
      return;
    }

    // Direct mode: synchronous
    if (count && count > 1) {
      for (var i = 0; i < count; i++) {
        emu[method]();
        // Stop early if we landed on a user breakpoint
        if (breakpoints.has(emu.getPC())) break;
      }
    } else {
      emu[method]();
    }
    emu.flushTerminalOutput();
    syncFromCpu();
    syncState();
    refreshView();
  }

  document.getElementById('dbg-step').addEventListener('click', function() {
    doStep('_Dbg_StepOne');
  });

  document.getElementById('dbg-step10').addEventListener('click', function() {
    doStep('_Dbg_StepOne', 10);
  });

  document.getElementById('dbg-step-over').addEventListener('click', function() {
    doStep('_Dbg_StepOver');
  });

  document.getElementById('dbg-step-out').addEventListener('click', function() {
    doStep('_Dbg_StepOut');
  });

  document.getElementById('dbg-run').addEventListener('click', function() {
    if (!wasmReady()) return;
    takeControl();
    paused = false;
    emu.setPaused(0);
    syncState();
    if (emu.isWorkerMode()) {
      // Worker mode: tell Worker to start autonomous loop
      emu.workerStart();
    }
    startExecLoop();
  });

  document.getElementById('dbg-pause').addEventListener('click', function() {
    if (!wasmReady()) return;
    takeControl();
    paused = true;
    emu.setPaused(1);
    if (emu.isWorkerMode()) {
      // Worker mode: stop Worker's autonomous loop and request a fresh snapshot
      emu.workerStop();
      emu.requestSnapshot();
    }
    stopExecLoop();
    syncState();
    refreshView();
  });

  function startExecLoop() {
    if (execLoopId) return;
    execFrame();
  }

  function stopExecLoop() {
    if (execLoopId) { cancelAnimationFrame(execLoopId); execLoopId = null; }
  }

  var execFrameCount = 0;
  function execFrame() {
    if (paused || !wasmReady()) {
      execLoopId = null;
      return;
    }

    if (emu.isWorkerMode()) {
      // Worker runs autonomously - just check cached state
      var mode = emu.getRunMode();
      if (mode === 2 || mode === 3 || mode === 4 || mode === 5) {
        syncFromCpu();
        paused = true;
        stopExecLoop();
        syncState();
        refreshView();
        return;
      }
      execFrameCount++;
      if (execFrameCount % 15 === 0) refreshView();
      execLoopId = requestAnimationFrame(execFrame);
      return;
    }

    // Direct mode
    var remaining = emu.runWithBreakpoints(10000);
    emu.flushTerminalOutput();
    if (remaining > 0) {
      syncFromCpu();
      paused = true;
      stopExecLoop();
      syncState();
      refreshView();
      return;
    }
    execFrameCount++;
    if (execFrameCount % 15 === 0) refreshView();
    execLoopId = requestAnimationFrame(execFrame);
  }

  /* --------------------------------------------------
     7. Auto-refresh timer
     -------------------------------------------------- */
  function startRefreshTimer() {
    if (refreshTimer) return;
    refreshTimer = setInterval(function() {
      if (visible) refreshView();
    }, 250);
  }

  function stopRefreshTimer() {
    if (refreshTimer) { clearInterval(refreshTimer); refreshTimer = null; }
  }

  /* --------------------------------------------------
     8. View refresh
     -------------------------------------------------- */
  function refreshView() {
    if (!wasmReady()) return;
    if (!emu.isInitialized()) return;
    try { emu.getPC(); } catch(e) { return; }

    try { renderRegisters(); } catch(e) { /* prevent cascade */ }
    try { renderFlags(); } catch(e) { /* prevent cascade */ }
    try { renderInstrCount(); } catch(e) { /* prevent cascade */ }

    var tab = document.querySelector('.debugger-tab.active');
    if (!tab) return;
    var tabRenderers = {
      'variables': renderVariables,
      'stack':     renderStackTrace,
      'levels':    renderLevels,
      'sysregs':   renderSystemRegs
    };
    var fn = tabRenderers[tab.getAttribute('data-tab')];
    if (fn) try { fn(); } catch(e) { /* prevent cascade */ }
  }

  function renderRegisters() {
    var regs = {
      sts: emu.getSTS(), p: emu.getPC(),
      a: emu.getRegA(),  d: emu.getRegD(),
      b: emu.getRegB(),  l: emu.getRegL(),
      t: emu.getRegT(),  x: emu.getRegX(),
      ea: emu.getEA()
    };

    document.getElementById('dbg-pil-display').textContent = emu.getPIL();

    ['sts','p','a','d','b','l','t','x','ea'].forEach(function(r) {
      var el = document.getElementById('dbg-r-' + r);
      if (!el) return;
      if (activeEdit === r) return; /* don't overwrite active edit input */
      el.textContent = oct(regs[r]);

      var changed = prevRegs[r] !== undefined && prevRegs[r] !== regs[r];
      var item = el.closest('.reg-item');
      if (changed) {
        el.classList.add('changed');
        if (item) item.classList.add('changed');
        setTimeout(function() {
          el.classList.remove('changed');
          if (item) item.classList.remove('changed');
        }, 600);
      }
    });
    prevRegs = regs;
  }

  function renderFlags() {
    var sts = emu.getSTS();

    [['dbg-f-ptm',0],['dbg-f-tg',1],['dbg-f-k',2],['dbg-f-z',3],
     ['dbg-f-q',4],['dbg-f-o',5],['dbg-f-c',6],['dbg-f-m',7]
    ].forEach(function(f) {
      var el = document.getElementById(f[0]);
      if (el) el.classList.toggle('active', ((sts >> f[1]) & 1) === 1);
    });

    document.getElementById('dbg-f-n100').classList.toggle('active', ((sts >> 12) & 1) === 1);
    document.getElementById('dbg-f-sexi').classList.toggle('active', ((sts >> 13) & 1) === 1);
    document.getElementById('dbg-f-poni').classList.toggle('active', ((sts >> 14) & 1) === 1);
    document.getElementById('dbg-f-ioni').classList.toggle('active', ((sts >> 15) & 1) === 1);

    document.getElementById('dbg-f-pil').textContent = 'PIL: ' + emu.getPIL();
  }

  function renderInstrCount() {
    if (!wasmReady()) return;
    document.getElementById('dbg-instr-count').textContent =
      'IC: ' + formatCount(emu.getInstrCount());
  }

  /* renderDisassembly removed - now handled by standalone disassembly.js window */

  function renderVariables() {
    var view = document.getElementById('dbg-variables-view');
    if (!view || !wasmReady()) return;

    safeJsonCall('Dbg_GetScopes', []).then(function(scopes) {
      if (!scopes) {
        view.innerHTML = '<div style="color:rgba(160,175,210,0.5);padding:8px">Scopes not available</div>';
        return;
      }

      var html = '';
      scopes.forEach(function(scope) {
        var sid = scope.variablesReference;
        var expanded = expandedScopes[sid] || false;

        html += '<div class="var-scope">';
        html += '<div class="var-scope-header" data-scope-id="' + sid + '">';
        html += '<span class="var-scope-arrow' + (expanded ? ' expanded' : '') + '">&#9654;</span>';
        html += '<span>' + escapeHtml(scope.name) + '</span>';
        html += '</div>';
        html += '<div class="var-scope-body' + (expanded ? ' expanded' : '') + '" id="var-scope-body-' + sid + '">';
        html += '</div></div>';
      });
      view.innerHTML = html;

      // Fill expanded scopes async
      scopes.forEach(function(scope) {
        var sid = scope.variablesReference;
        if (expandedScopes[sid]) {
          buildVariablesHtml(sid).then(function(innerHtml) {
            var body = document.getElementById('var-scope-body-' + sid);
            if (body) body.innerHTML = innerHtml;
          });
        }
      });

      view.querySelectorAll('.var-scope-header').forEach(function(hdr) {
        hdr.addEventListener('click', function() {
          var sid = parseInt(hdr.getAttribute('data-scope-id'));
          expandedScopes[sid] = !expandedScopes[sid];
          hdr.querySelector('.var-scope-arrow').classList.toggle('expanded', expandedScopes[sid]);
          var body = document.getElementById('var-scope-body-' + sid);
          body.classList.toggle('expanded', expandedScopes[sid]);
          if (expandedScopes[sid]) {
            buildVariablesHtml(sid).then(function(innerHtml) {
              body.innerHTML = innerHtml;
            });
          } else {
            body.innerHTML = '';
          }
        });
      });
    });
  }

  function buildVariablesHtml(scopeId) {
    return safeJsonCall('Dbg_GetVariables', ['number'], [scopeId]).then(function(vars) {
      if (!vars) return '';
      if (!prevVariables[scopeId]) prevVariables[scopeId] = {};

      var html = '';
      vars.forEach(function(v) {
        var changed = prevVariables[scopeId][v.name] !== undefined &&
                      prevVariables[scopeId][v.name] !== v.value;
        html += '<div class="var-item">';
        html += '<span class="var-name">' + escapeHtml(v.name) + '</span>';
        html += '<span class="var-value' + (changed ? ' changed' : '') + '">' + escapeHtml(v.value) + '</span>';
        html += '<span class="var-type">' + escapeHtml(v.type || '') + '</span>';
        html += '</div>';
        prevVariables[scopeId][v.name] = v.value;
      });
      return html;
    });
  }

  function safeJsonCall(name, argTypes, argValues) {
    try {
      var result = emu.ccall(name, 'string', argTypes || [], argValues || []);
      return Promise.resolve(result).then(function(json) {
        return json ? JSON.parse(json) : null;
      });
    } catch(e) { return Promise.resolve(null); }
  }

  function renderStackTrace() {
    var view = document.getElementById('dbg-stacktrace-view');
    if (!view || !wasmReady()) return;

    safeJsonCall('Dbg_GetStackTrace', []).then(function(frames) {
      if (!frames || frames.length === 0) {
        view.innerHTML = '<div style="color:rgba(160,175,210,0.5);padding:8px">No stack frames</div>';
        return;
      }

      var html = '';
      frames.forEach(function(frame, idx) {
        html += '<div class="stack-frame' + (idx === 0 ? ' current' : '') + '" data-addr="' + frame.id + '">';
        html += '<span class="stack-addr">' + escapeHtml(frame.instructionPointerReference || '') + '</span>';
        html += '<span class="stack-name">' + escapeHtml(frame.name || 'unknown') + '</span>';
        if (frame.source) html += '<span class="stack-source">' + escapeHtml(frame.source) + '</span>';
        html += '</div>';
      });
      view.innerHTML = html;

      view.querySelectorAll('.stack-frame').forEach(function(el) {
        el.addEventListener('click', function() {
          var addr = parseInt(el.getAttribute('data-addr'));
          if (isNaN(addr)) return;
          if (typeof window.disasmShowWindow === 'function') window.disasmShowWindow();
        });
      });
    });
  }

  function renderLevels() {
    var grid = document.getElementById('dbg-levels-grid');
    if (!wasmReady()) return;

    var pil = emu.getPIL();
    document.getElementById('dbg-level-pil').textContent = pil;
    document.getElementById('dbg-level-pvl').textContent = oct(emu.getPVL());

    Promise.resolve(emu.getLevelInfo()).then(function(raw) {
      var lines = (raw || '').trim().split('\n');
      var html = '';
      lines.forEach(function(line) {
        var parts = line.split(/\s+/);
        if (parts.length < 6) return;
        var lev = parseInt(parts[0]);
        html += '<div class="level-item' + (lev === pil ? ' current-level' : '') + '">';
        html += '<span class="level-num" title="Interrupt level (0-15)">' + lev + '</span>';
        html += '<span class="level-pc" title="Program counter - next instruction address">P:' + parts[1] + '</span>';
        html += '<span class="level-sts" title="Status register low byte (STS LSB) - condition flags">S:' + parts[2] + '</span>';
        html += '<span class="level-info">';
        html += '<span title="Protection ring (0=kernel, 1-2=system, 3=user)">' + escapeHtml(parts[3]) + '</span> ';
        html += '<span title="Normal Page Table - used for instruction fetch">' + escapeHtml(parts[4]) + '</span> ';
        html += '<span title="Alternative Page Table - used for data access">' + escapeHtml(parts[5]) + '</span>';
        html += '</span>';
        html += '<span class="level-desc">' + (levelDesc[lev] || '') + '</span>';
        html += '</div>';
      });
      grid.innerHTML = html;
    });
  }

  function renderSystemRegs() {
    var roGrid = document.getElementById('dbg-sysregs-ro');
    var woGrid = document.getElementById('dbg-sysregs-wo');

    function renderRegList(regs) {
      var html = '';
      regs.forEach(function(r) {
        var val = emu[r.fn] ? emu[r.fn]() : 0;
        html += '<div class="sysreg-item"><span class="sysreg-name">' + r.name +
                '</span><span class="sysreg-value">' + oct(val) + '</span></div>';
      });
      return html;
    }

    roGrid.innerHTML = renderRegList([
      { name: 'PANS', fn: 'getPANS' }, { name: 'OPR',  fn: 'getOPR' },
      { name: 'PGS',  fn: 'getPGS' },  { name: 'PVL',  fn: 'getPVL' },
      { name: 'IIC',  fn: 'getIIC' },   { name: 'IID',  fn: 'getIID' },
      { name: 'PID',  fn: 'getPID' },   { name: 'PIE',  fn: 'getPIE' },
      { name: 'CSR',  fn: 'getCSR' },   { name: 'ALD',  fn: 'getALD' },
      { name: 'PES',  fn: 'getPES' },   { name: 'PGC',  fn: 'getPGC' },
      { name: 'PEA',  fn: 'getPEA' }
    ]);

    var woHtml = renderRegList([
      { name: 'PANC', fn: 'getPANC' }, { name: 'LMP',  fn: 'getLMP' },
      { name: 'IIE',  fn: 'getIIE' },  { name: 'CCL',  fn: 'getCCL' },
      { name: 'LCIL', fn: 'getLCIL' }, { name: 'UCIL', fn: 'getUCIL' },
      { name: 'ECCR', fn: 'getECCR' }
    ]);

    var pil = emu.getPIL();
    woHtml += '<div class="sysreg-item"><span class="sysreg-name">PCR[' + pil +
              ']</span><span class="sysreg-value">' + oct(emu.getPCR(pil)) + '</span></div>';
    woGrid.innerHTML = woHtml;
  }

  function switchToTab(name) {
    document.querySelectorAll('.debugger-tab').forEach(function(t) { t.classList.remove('active'); });
    document.querySelectorAll('.debugger-tab-content').forEach(function(c) { c.classList.remove('active'); });
    var tab = document.querySelector('.debugger-tab[data-tab="' + name + '"]');
    var content = document.querySelector('.debugger-tab-content[data-tab="' + name + '"]');
    if (tab) tab.classList.add('active');
    if (content) content.classList.add('active');
  }

  /* --------------------------------------------------
     9. Keyboard shortcuts
     -------------------------------------------------- */
  document.addEventListener('keydown', function(e) {
    if (!visible || e.ctrlKey || e.altKey) return;

    if (e.key === 'F5' && !e.shiftKey) {
      e.preventDefault();
      var btn = document.getElementById(paused ? 'dbg-run' : 'dbg-pause');
      if (btn && !btn.disabled) btn.click();
      return;
    }

    if (e.key === 'F9' && !e.shiftKey) {
      e.preventDefault();
      toggleBreakpointAtPC();
      return;
    }

    if (e.key === 'F10' && !e.shiftKey) {
      e.preventDefault();
      clickIfEnabled('dbg-step-over');
    } else if (e.key === 'F11' && e.shiftKey) {
      e.preventDefault();
      clickIfEnabled('dbg-step-out');
    } else if (e.key === 'F11' && !e.shiftKey) {
      e.preventDefault();
      clickIfEnabled('dbg-step');
    }
  });

  function clickIfEnabled(id) {
    var btn = document.getElementById(id);
    if (btn && !btn.disabled) btn.click();
  }

  function toggleBreakpointAtPC() {
    if (!wasmReady()) return;
    var pc = emu.getPC();
    if (breakpoints.has(pc)) {
      breakpoints.delete(pc);
      emu.removeBreakpoint(pc);
    } else {
      breakpoints.add(pc);
      emu.addBreakpoint(pc);
    }
    if (typeof window.dbgRefreshDisasm === 'function') window.dbgRefreshDisasm();
  }

  /* --------------------------------------------------
     10. Memory dump
     -------------------------------------------------- */
  document.getElementById('dbg-mem-go').addEventListener('click', renderMemoryDump);
  document.getElementById('dbg-mem-addr').addEventListener('keydown', function(e) {
    if (e.key === 'Enter') renderMemoryDump();
  });

  function renderMemoryDump() {
    if (!wasmReady()) return;
    var addr  = parseOctal(document.getElementById('dbg-mem-addr').value.trim());
    var count = Math.min(parseInt(document.getElementById('dbg-mem-count').value.trim()) || 64, 256);

    Promise.resolve(emu.readMemoryBlock(addr, count)).then(function(values) {
      var text = '';
      for (var i = 0; i < count; i += 8) {
        var lineAddr = (addr + i) & 0xFFFF;
        text += oct(lineAddr) + ':';
        for (var j = 0; j < 8 && (i + j) < count; j++) {
          var val = (values && values[i + j] !== undefined) ? values[i + j] : 0;
          text += ' ' + oct(val);
        }
        text += '\n';
      }
      document.getElementById('dbg-mem-dump').textContent = text;
    });
  }

  /* --------------------------------------------------
     11. Main-page integration hooks
     -------------------------------------------------- */
  if (typeof startEmulation === 'function') {
    var origStart = startEmulation;
    window.startEmulation = startEmulation = function(bootDevice) {
      if (active) return;
      origStart(bootDevice);
      paused = false;
      syncState();
      if (visible && emu.isWorkerMode()) {
        startExecLoop();
      }
    };
  }

  if (typeof stopEmulation === 'function') {
    var origStop = stopEmulation;
    window.stopEmulation = stopEmulation = function() {
      if (active) {
        paused = true;
        if (wasmReady()) emu.setPaused(1);
        stopExecLoop();
        active = false;
      }
      origStop();
      syncState();
      if (visible) refreshView();
    };
  }

  /* --------------------------------------------------
     12. Init
     -------------------------------------------------- */
  window.dbgRefreshDisasm = function() {}; /* standalone disassembly window handles its own rendering */

  /* Called by emulation.js RAF loop to ensure debugger updates while running */
  window.dbgRefreshIfVisible = function() {
    if (visible) refreshView();
  };

  /* Called by emulation.js when a breakpoint is hit during normal execution */
  window.dbgActivateBreakpoint = function() {
    takeControl();
    active = true;
    paused = true;
    syncFromCpu();
    syncState();
    showWindow();
    refreshView();
  };

  /* Wire click-to-edit on register items */
  document.querySelectorAll('.reg-item').forEach(function(item) {
    var valEl = item.querySelector('.reg-value');
    if (!valEl) return;
    var id = valEl.id; /* e.g. "dbg-r-p" */
    if (!id) return;
    var regKey = id.replace('dbg-r-', '');
    if (!regSetters[regKey]) return; /* skip read-only (ea) */
    item.style.cursor = 'pointer';
    item.addEventListener('click', function(e) {
      if (e.target.classList.contains('reg-edit-input')) return; /* don't re-trigger on input click */
      startRegEdit(regKey);
    });
  });

  syncState();

  // If the debugger window was restored as visible by toolbar.js (before this script loaded),
  // initialize the visible flag and start the refresh timer
  (function() {
    var win = document.getElementById('debugger-window');
    if (win && win.style.display !== 'none') {
      visible = true;
      startRefreshTimer();
    }
  })();

})();
