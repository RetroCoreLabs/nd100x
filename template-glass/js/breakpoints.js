//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// breakpoints.js - Breakpoints Glass Window
// Lists execution breakpoints and memory watchpoints,
// allows adding/removing both types.

(function() {
  'use strict';

  var visible = false;
  var refreshTimer = null;
  var execList = document.getElementById('bp-exec-list');
  var watchList = document.getElementById('bp-watch-list');
  var addBtn = document.getElementById('bp-add-btn');
  var addrInput = document.getElementById('bp-add-addr');
  var typeSelect = document.getElementById('bp-add-type');

  /* --- Utilities --- */

  function oct(val) {
    return ('000000' + ((val >>> 0) & 0xFFFF).toString(8)).slice(-6);
  }

  function wasmReady() {
    return typeof emu !== 'undefined' && emu && emu.isReady();
  }

  function escapeHtml(str) {
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  /* Breakpoint type names from C enum */
  var bpTypeNames = {
    0: 'User',
    1: 'Function',
    2: 'Data',
    3: 'Instruction',
    4: 'Temporary'
  };

  var watchTypeNames = {
    1: 'Read',
    2: 'Write',
    3: 'Read/Write'
  };

  /* --- Window show / hide --- */

  function show() {
    visible = true;
    document.getElementById('breakpoints-window').style.display = 'flex';
    render();
    startRefreshTimer();
  }

  function hide() {
    visible = false;
    document.getElementById('breakpoints-window').style.display = 'none';
    stopRefreshTimer();
  }

  /* --- Auto-refresh timer --- */

  function startRefreshTimer() {
    if (refreshTimer) return;
    refreshTimer = setInterval(function() {
      if (visible) render();
    }, 500);
  }

  function stopRefreshTimer() {
    if (refreshTimer) { clearInterval(refreshTimer); refreshTimer = null; }
  }

  /* --- Render breakpoint lists --- */

  function render() {
    if (emu && !emu.isInitialized()) return;
    renderExecBreakpoints();
    renderWatchpoints();
  }

  function renderExecBreakpoints() {
    if (!execList) return;

    /* Gather from JS-side shared set */
    var breakpoints = window.dbgBreakpoints || new Set();

    /* Also gather from C-side list if available */
    var bpPromise = Promise.resolve('');
    if (wasmReady()) {
      try {
        bpPromise = Promise.resolve(emu.getBreakpointList());
      } catch(e) { /* ignore */ }
    }

    bpPromise.then(function(rawStr) {
      var cBpMap = {};
      var raw = (rawStr || '').trim();
      if (raw.length > 0) {
        raw.split('\n').forEach(function(line) {
          var parts = line.split(' ');
          if (parts.length >= 3) {
            var addr = parseInt(parts[0], 10);
            var type = parseInt(parts[1], 10);
            var hits = parseInt(parts[2], 10);
            /* Skip temporary breakpoints (type 4) */
            if (type !== 4) {
              cBpMap[addr] = { type: type, hits: hits };
            }
          }
        });
      }

      /* Merge JS set and C list */
      var allAddrs = new Set();
      breakpoints.forEach(function(a) { allAddrs.add(a); });
      Object.keys(cBpMap).forEach(function(a) { allAddrs.add(parseInt(a, 10)); });

      if (allAddrs.size === 0) {
        execList.innerHTML = '<div class="bp-empty">No execution breakpoints set</div>';
        return;
      }

      var sorted = Array.from(allAddrs).sort(function(a, b) { return a - b; });
      var html = '';
      sorted.forEach(function(addr) {
        var info = cBpMap[addr] || { type: 0, hits: 0 };
        var typeName = bpTypeNames[info.type] || 'User';
        html += '<div class="bp-item" data-addr="' + addr + '">';
        html += '<span class="bp-item-dot exec"></span>';
        html += '<span class="bp-item-addr">' + oct(addr) + '</span>';
        html += '<span class="bp-item-type">' + escapeHtml(typeName) + '</span>';
        if (info.hits > 0) {
          html += '<span class="bp-item-hits">hits: ' + info.hits + '</span>';
        }
        html += '<button class="bp-item-remove" data-addr="' + addr + '" data-kind="exec">x</button>';
        html += '</div>';
      });
      execList.innerHTML = html;

      /* Attach remove handlers */
      execList.querySelectorAll('.bp-item-remove').forEach(function(btn) {
        btn.addEventListener('click', function(e) {
          e.stopPropagation();
          var addr = parseInt(btn.getAttribute('data-addr'), 10);
          removeExecBreakpoint(addr);
        });
      });
    });
  }

  function renderWatchpoints() {
    if (!watchList || !wasmReady()) {
      if (watchList) watchList.innerHTML = '<div class="bp-empty">No watchpoints set</div>';
      return;
    }

    Promise.resolve(emu.getWatchpointCount()).then(function(count) {
      if (count === 0) {
        watchList.innerHTML = '<div class="bp-empty">No watchpoints set</div>';
        return;
      }

      // Collect addr+type for each watchpoint
      var promises = [];
      for (var i = 0; i < count; i++) {
        (function(idx) {
          promises.push(Promise.all([
            Promise.resolve(emu.getWatchpointAddr(idx)),
            Promise.resolve(emu.getWatchpointType(idx))
          ]));
        })(i);
      }

      Promise.all(promises).then(function(wps) {
        var html = '';
        wps.forEach(function(wp) {
          var addr = wp[0];
          var type = wp[1];
          if (addr < 0) return;
          var typeName = watchTypeNames[type] || 'Unknown';
          html += '<div class="bp-item" data-addr="' + addr + '">';
          html += '<span class="bp-item-dot watch"></span>';
          html += '<span class="bp-item-addr">' + oct(addr) + '</span>';
          html += '<span class="bp-item-type">' + escapeHtml(typeName) + '</span>';
          html += '<button class="bp-item-remove" data-addr="' + addr + '" data-kind="watch">x</button>';
          html += '</div>';
        });
        watchList.innerHTML = html;

        /* Attach remove handlers */
        watchList.querySelectorAll('.bp-item-remove').forEach(function(btn) {
          btn.addEventListener('click', function(e) {
            e.stopPropagation();
            var addr = parseInt(btn.getAttribute('data-addr'), 10);
            removeWatchpoint(addr);
          });
        });
      }); // end Promise.all(promises).then
    }); // end Promise.resolve(getWatchpointCount).then
  }

  /* --- Add / Remove --- */

  function addBreakpoint() {
    if (!wasmReady()) return;
    var addrStr = addrInput.value.trim();
    if (!addrStr) return;

    var addr = parseInt(addrStr, 8); /* parse as octal */
    if (isNaN(addr) || addr < 0 || addr > 0xFFFF) {
      addrInput.style.borderColor = 'rgba(229, 57, 53, 0.6)';
      setTimeout(function() { addrInput.style.borderColor = ''; }, 1000);
      return;
    }

    var type = typeSelect.value;

    if (type === 'exec') {
      /* Execution breakpoint */
      emu.addBreakpoint(addr);
      var breakpoints = window.dbgBreakpoints || new Set();
      breakpoints.add(addr);
      /* Sync disassembly views */
      if (typeof window.dbgRefreshDisasm === 'function') {
        try { window.dbgRefreshDisasm(); } catch(e) {}
      }
    } else {
      /* Watchpoint: read=1, write=2, readwrite=3 */
      var watchType = 3;
      if (type === 'read') watchType = 1;
      else if (type === 'write') watchType = 2;
      emu.addWatchpoint(addr, watchType);
    }

    addrInput.value = '';
    render();
  }

  function removeExecBreakpoint(addr) {
    if (!wasmReady()) return;
    emu.removeBreakpoint(addr);
    var breakpoints = window.dbgBreakpoints;
    if (breakpoints) breakpoints.delete(addr);
    /* Sync disassembly views */
    if (typeof window.dbgRefreshDisasm === 'function') {
      try { window.dbgRefreshDisasm(); } catch(e) {}
    }
    render();
  }

  function removeWatchpoint(addr) {
    if (!wasmReady()) return;
    emu.removeWatchpoint(addr);
    render();
  }

  /* --- Event handlers --- */

  if (addBtn) {
    addBtn.addEventListener('click', function() {
      addBreakpoint();
    });
  }

  if (addrInput) {
    addrInput.addEventListener('keydown', function(e) {
      if (e.key === 'Enter') {
        addBreakpoint();
      }
    });
  }

  /* --- Expose for toolbar.js --- */
  window.bpShowWindow = show;
  window.bpHideWindow = hide;
  window.bpRefresh = render;

})();
