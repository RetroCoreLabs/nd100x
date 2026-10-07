//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// pagetables.js - ND-100 Hardware Page Table Inspector
// Displays raw PIT entries from shadow RAM with decoded flags and physical addresses

(function() {
  'use strict';

  // =========================================================
  // PTE bit positions (32-bit PTE returned by Dbg_GetPageTableEntryRaw)
  // =========================================================
  var PTE_WPM = 31;   // Write Permit
  var PTE_RPM = 30;   // Read Permit
  var PTE_FPM = 29;   // Fetch Permit
  var PTE_WIP = 28;   // Written In Page
  var PTE_PGU = 27;   // Page Used
  // Bits 25-24: Ring (0-3)
  // Extended mode: bits 13-0 = PPN (14 bits)
  // Normal mode:   bits 8-0  = PPN (9 bits)

  // =========================================================
  // Helpers
  // =========================================================

  function toOctal6(val) {
    return (val & 0xFFFF).toString(8).padStart(6, '0');
  }

  function toOctal8(val) {
    return (val >>> 0).toString(8).padStart(8, '0');
  }

  function bit(val, n) {
    return (val >>> n) & 1;
  }

  // =========================================================
  // Decode a 32-bit PTE into human-readable fields
  // =========================================================
  function decodePTE(pte, extended) {
    var wpm  = bit(pte, PTE_WPM);
    var rpm  = bit(pte, PTE_RPM);
    var fpm  = bit(pte, PTE_FPM);
    var wip  = bit(pte, PTE_WIP);
    var pgu  = bit(pte, PTE_PGU);
    var ring = (pte >>> 24) & 0x03;
    var ppn;

    if (extended) {
      ppn = pte & 0x3FFF;  // 14-bit PPN
    } else {
      ppn = pte & 0x01FF;  // 9-bit PPN
    }

    var flags = [];
    if (wpm) flags.push('WPM');
    if (rpm) flags.push('RPM');
    if (fpm) flags.push('FPM');
    if (wip) flags.push('WIP');
    if (pgu) flags.push('PGU');

    // Physical address range: PPN * 1024 to PPN * 1024 + 1023
    var physStart = ppn * 1024;
    var physEnd   = physStart + 1023;

    var parts = flags.join(' ');
    if (parts.length > 0) parts += ' ';
    parts += 'Ring:' + ring + ' PPN:' + ppn.toString(8).padStart(extended ? 5 : 4, '0');
    parts += ' -> ' + physStart.toString(8).padStart(extended ? 8 : 7, '0') +
             '-' + physEnd.toString(8).padStart(extended ? 8 : 7, '0');

    return parts;
  }

  // =========================================================
  // Build PCR-to-PT mapping (which runlevels use which PT)
  // =========================================================
  function getPCRMapping(ptCount) {
    var map = {};
    for (var i = 0; i < ptCount; i++) map[i] = [];

    if (!emu || !emu.isReady()) return map;

    for (var level = 0; level < 16; level++) {
      var pcr = emu.getPCR(level);
      var ptNum = (pcr >>> 10) & 0x0F;
      if (map[ptNum]) {
        map[ptNum].push(level);
      }
    }
    return map;
  }

  // =========================================================
  // Render the page table
  // =========================================================
  function renderPageTable() {
    var body = document.getElementById('pt-table-body');
    var ptInfo = document.getElementById('pt-info');
    if (!body) return;

    if (!emu || !emu.isReady()) {
      body.innerHTML = '<div style="padding:12px;color:rgba(160,175,210,0.5);font-style:italic;">Emulator not ready</div>';
      return;
    }

    var ptSelect = document.getElementById('pt-select');
    var ptNum = parseInt(ptSelect.value) || 0;
    var ptCount = emu.getPageTableCount();
    var extended = emu.getExtendedMode();

    // Show runlevel mapping
    var pcrMap = getPCRMapping(ptCount);
    var levels = pcrMap[ptNum];
    if (ptInfo) {
      if (levels && levels.length > 0) {
        ptInfo.textContent = 'Used by level: ' + levels.join(', ');
      } else {
        ptInfo.textContent = 'No runlevels using this PT';
      }
      ptInfo.textContent += ' | Mode: ' + (extended ? 'Extended (SEXI)' : 'Normal');
    }

    // Build table rows (batch read all 64 entries at once)
    Promise.resolve(emu.getPageTableMap(ptNum)).then(function(entries) {
    var html = '<table class="proc-table pt-data-table"><thead><tr>' +
      '<th>VPN</th><th>Word 0 (flags)</th><th>Word 1 (PPN)</th><th>Decoded</th>' +
      '</tr></thead><tbody>';

    for (var vpn = 0; vpn < 64; vpn++) {
      var pte = (entries[vpn] || 0) >>> 0;

      var word0 = (pte >>> 16) & 0xFFFF;
      var word1 = pte & 0xFFFF;
      var isEmpty = (word0 === 0 && word1 === 0);
      var rowClass = isEmpty ? 'proc-row pt-empty' : 'proc-row';

      var decoded = isEmpty ? '-' : decodePTE(pte, extended);

      html += '<tr class="' + rowClass + '">' +
        '<td>' + vpn.toString(8).padStart(3, '0') + '</td>' +
        '<td>' + toOctal6(word0) + '</td>' +
        '<td>' + toOctal6(word1) + '</td>' +
        '<td class="pt-decoded">' + decoded + '</td>' +
        '</tr>';
    }

    html += '</tbody></table>';
    body.innerHTML = html;
    }); // end Promise.resolve(getPageTableMap).then
  }

  // =========================================================
  // Populate PT selector dropdown
  // =========================================================
  function populatePTSelector() {
    var ptSelect = document.getElementById('pt-select');
    if (!ptSelect) return;

    var count = 16; // default
    if (emu && emu.isReady()) {
      count = emu.getPageTableCount();
    }

    var currentVal = parseInt(ptSelect.value) || 0;
    ptSelect.innerHTML = '';
    for (var i = 0; i < count; i++) {
      var opt = document.createElement('option');
      opt.value = i;
      opt.textContent = 'PT ' + i.toString(8);
      ptSelect.appendChild(opt);
    }

    if (currentVal < count) {
      ptSelect.value = currentVal;
    }
  }

  // =========================================================
  // Window show/hide
  // =========================================================

  function showWindow() {
    var win = document.getElementById('page-table-window');
    if (win) {
      win.style.display = 'flex';
      populatePTSelector();
      renderPageTable();
    }
  }

  function hideWindow() {
    var win = document.getElementById('page-table-window');
    if (win) win.style.display = 'none';
  }

  // =========================================================
  // Event handlers
  // =========================================================

  var closeBtn = document.getElementById('page-table-close');
  if (closeBtn) closeBtn.addEventListener('click', hideWindow);

  var refreshBtn = document.getElementById('page-table-refresh');
  if (refreshBtn) refreshBtn.addEventListener('click', function() {
    populatePTSelector();
    renderPageTable();
  });

  var ptSelect = document.getElementById('pt-select');
  if (ptSelect) ptSelect.addEventListener('change', renderPageTable);

  // =========================================================
  // Export
  // =========================================================
  window.pageTableShowWindow = showWindow;
  window.pageTableHideWindow = hideWindow;
})();
