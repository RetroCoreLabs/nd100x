//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// disassembly.js - Standalone Disassembly Glass Window
// Renders the same disassembly data as the debugger's Disasm tab
// in a separate, draggable/resizable glass window.

(function() {
  'use strict';

  var visible = false;
  var refreshTimer = null;
  var view = document.getElementById('disasm-standalone-view');

  /* --- Utilities (local copies to avoid cross-module deps) --- */

  function oct(val) {
    return ('000000' + ((val >>> 0) & 0xFFFF).toString(8)).slice(-6);
  }

  function escapeHtml(str) {
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  function wasmReady() {
    return typeof emu !== 'undefined' && emu && emu.isReady();
  }

  function wasmCall(fnName) {
    return wasmReady();
  }

  /* --- Window show / hide --- */

  function show() {
    visible = true;
    document.getElementById('disasm-window').style.display = 'flex';
    renderDisasm();
    startRefreshTimer();
  }

  function hide() {
    visible = false;
    document.getElementById('disasm-window').style.display = 'none';
    stopRefreshTimer();
  }

  /* --- Auto-refresh timer --- */

  function startRefreshTimer() {
    if (refreshTimer) return;
    refreshTimer = setInterval(function() {
      if (visible) renderDisasm();
    }, 250);
  }

  function stopRefreshTimer() {
    if (refreshTimer) { clearInterval(refreshTimer); refreshTimer = null; }
  }

  /* --- Render disassembly --- */

  function renderDisasm() {
    if (!view || !wasmReady()) return;
    if (!emu.isInitialized()) return;

    var breakpoints = window.dbgBreakpoints || new Set();
    var pc = emu.getPC();

    Promise.resolve(emu.disassemble((pc - 10) & 0xFFFF, 30)).then(function(raw) {
    var lines = (raw || '').trim().split('\n');

    var html = '';
    lines.forEach(function(line) {
      var parts = line.split(/\s+/);
      if (parts.length < 3) return;
      var addr = parseInt(parts[0], 8);
      var isPC = (addr === pc);
      var hasBP = breakpoints.has(addr);

      html += '<div class="disasm-line' + (isPC ? ' current-pc' : '') + '" data-addr="' + addr + '">';
      html += '<div class="disasm-gutter">';
      html += '<span class="disasm-gutter-bp"><span class="disasm-bp-dot' + (hasBP ? ' active' : '') + '"></span></span>';
      html += '<span class="disasm-gutter-pc">' + (isPC ? '<span class="disasm-pc-arrow">&#9654;</span>' : '') + '</span>';
      html += '</div>';
      html += '<span class="disasm-addr">' + parts[0] + '</span>';
      html += '<span class="disasm-word">' + parts[1] + '</span>';
      html += '<span class="disasm-mnemonic">' + escapeHtml(parts.slice(2).join(' ')) + '</span>';
      html += '</div>';
    });
    view.innerHTML = html;

    var currentLine = view.querySelector('.current-pc');
    if (currentLine) currentLine.scrollIntoView({ block: 'center', behavior: 'auto' });

    /* Breakpoint click handler */
    view.querySelectorAll('.disasm-line').forEach(function(el) {
      el.addEventListener('click', function() {
        var addr = parseInt(el.getAttribute('data-addr'));
        if (isNaN(addr)) return;
        if (breakpoints.has(addr)) {
          breakpoints.delete(addr);
          emu.removeBreakpoint(addr);
        } else {
          breakpoints.add(addr);
          emu.addBreakpoint(addr);
        }
        renderDisasm();
        /* Keep debugger tab in sync */
        if (typeof window.dbgRefreshDisasm === 'function') {
          try { window.dbgRefreshDisasm(); } catch(e) {}
        }
      });
    });
    }); // end Promise.resolve(emu.disassemble...).then
  }

  /* --- Expose for toolbar.js --- */
  window.disasmShowWindow = show;
  window.disasmHideWindow = hide;

})();
