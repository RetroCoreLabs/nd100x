//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// sintran-processes.js - Process List and Process Detail windows
// Shows SINTRAN III RT-Description table entries with status, registers, etc.

(function() {
  'use strict';

  var sym = window.sintranSymbols;
  var FX = sym.FIXED;
  var RT = sym.RT_DESC;
  var SB = sym.STATU_BITS;
  var RS = sym.REG_SAVE;

  var refreshTimer = null;
  var autoRefresh = true;
  var selectedRtNum = -1;
  var lastProcesses = [];

  // =========================================================
  // Status filter
  // =========================================================

  function getActiveFilters() {
    var filters = [];
    var bar = document.getElementById('proc-filter-bar');
    if (!bar) return filters;
    var cbs = bar.querySelectorAll('input[type="checkbox"]');
    for (var i = 0; i < cbs.length; i++) {
      if (cbs[i].checked) filters.push(cbs[i].value);
    }
    return filters;
  }

  function applyFilter(processes) {
    var filters = getActiveFilters();
    // No checkboxes checked = no filter, show all
    if (filters.length === 0) return processes;
    var result = [];
    for (var i = 0; i < processes.length; i++) {
      if (filters.indexOf(processes[i].state) !== -1) {
        result.push(processes[i]);
      }
    }
    return result;
  }

  // Field descriptions for hover tooltips
  var FIELD_TIPS = {
    TLINK: 'Time queue link - pointer to next RT in time queue',
    STATU: 'Status word with flag bits',
    INPRI: 'Initial priority (assigned at creation)',
    PRITY: 'Type/Priority/Ring - bits 15-8: priority, bits 2-0: ring level',
    DTIM1: 'Delay timer high word (32-bit DTIME upper half)',
    DTIM2: 'Delay timer low word (32-bit DTIME lower half)',
    DTIN1: 'Delay timer interval high word (repeat period upper)',
    DTIN2: 'Delay timer interval low word (repeat period lower)',
    STADR: 'Start address - code entry point for this RT program',
    SEGM1: 'Program (code) segment number',
    SEGM2: 'Data segment number',
    WLINK: 'Wait/execution queue link - pointer to next RT in exec queue',
    ACT1S: 'Currently active program segment',
    ACT2S: 'Currently active data segment',
    INIPR: 'Initial priority register value',
    ACTPR: 'Active PCR (Paging Control Register) value',
    BRESL: 'Reservation chain head - linked list of reserved I/O datafields',
    RSEGM: 'Reentrant segment number',
    BUFWI: 'Buffer window page (segment for DMA buffers)',
    TRMWI: 'Terminal window page (segment for terminal I/O)',
    N5WIN: 'ND-500 window page (for dual-CPU ND-100/500 systems)',
    RTDLG: 'Pointer to register save block (separate from RT-Desc)'
  };

  var STATUS_TIPS = {
    BACK: 'Bit 0 - Background process flag',
    USED: 'Bit 1 - RT entry is in use',
    TSLI: 'Bit 2 - Timesliced execution (CPU quanta)',
    ESCF: 'Bit 3 - Escape priority flag',
    BRKF: 'Bit 4 - Break flag (received interrupt)',
    SWWA: 'Bit 8 - Swap wait (being swapped to/from disk)',
    RTOF: 'Bit 9 - RT program OFF/inhibited',
    TMOU: 'Bit 10 - Timeout occurred',
    INT:  'Bit 12 - Internal process',
    RWAI: 'Bit 13 - Resource wait (waiting for shared resource)',
    WAIT: 'Bit 15 - I/O wait (waiting for device completion)'
  };

  var REG_TIPS = {
    P: 'Program counter - next instruction address',
    X: 'Index register',
    T: 'Scratch/temporary register',
    A: 'Accumulator - primary arithmetic register',
    D: 'Double register (A,D form 32-bit pair)',
    L: 'Link register (subroutine return address)',
    S: 'Stack pointer',
    B: 'Base register'
  };

  // =========================================================
  // Data reading
  // =========================================================

  // Returns Promise<Array>
  function readProcessList() {
    // Invalidate cache so we get fresh values
    sym.invalidateRtCache();

    return sym.discoverRtTable().then(function(rtInfo) {
      var tableBase = rtInfo.base;
      var rtCount = rtInfo.count;
      if (tableBase === 0 || rtCount === 0) return [];

      // Bulk read entire RT table + current running process + exec queue head
      return Promise.all([
        sym.readBlock(tableBase, rtCount * RT.SIZE),
        sym.readWord(FX.RTREF),
        buildExecQueueSet()
      ]).then(function(results) {
        var tableData = results[0];
        var rtrefVal = results[1];
        var execSet = results[2];

        if (tableData.length === 0) return [];

        var processes = [];
        for (var i = 0; i < rtCount; i++) {
          var base = i * RT.SIZE;
          var statu = tableData[base + RT.STATU];

          // Only show USED entries
          if (!sym.testBit(statu, SB.USED)) continue;

          var rtAddr = tableBase + base;
          var entry = {
            rtNum: i,
            rtAddr: rtAddr,
            tlink:  tableData[base + RT.TLINK],
            statu:  statu,
            inpri:  tableData[base + RT.INPRI],
            prity:  tableData[base + RT.PRITY],
            dtim1:  tableData[base + RT.DTIM1],
            dtim2:  tableData[base + RT.DTIM2],
            dtin1:  tableData[base + RT.DTIN1],
            dtin2:  tableData[base + RT.DTIN2],
            stadr:  tableData[base + RT.STADR],
            segm1:  tableData[base + RT.SEGM1],
            segm2:  tableData[base + RT.SEGM2],
            wlink:  tableData[base + RT.WLINK],
            act1s:  tableData[base + RT.ACT1S],
            act2s:  tableData[base + RT.ACT2S],
            inipr:  tableData[base + RT.INIPR],
            actpr:  tableData[base + RT.ACTPR],
            bresl:  tableData[base + RT.BRESL],
            rsegm:  tableData[base + RT.RSEGM],
            bufwi:  tableData[base + RT.BUFWI],
            trmwi:  tableData[base + RT.TRMWI],
            n5win:  tableData[base + RT.N5WIN],
            rtdlg:  tableData[base + RT.RTDLG],
            isCurrent: (rtAddr === rtrefVal),
            name: ''
          };

          // Derive state
          entry.state = deriveState(entry, rtrefVal, execSet);
          entry.name = (typeof window.resolveProcessName === 'function')
            ? window.resolveProcessName(i) : 'RT #' + i;

          processes.push(entry);
        }

        return processes;
      });
    });
  }

  // Returns Promise<Object> (set of addresses in exec queue)
  function buildExecQueueSet() {
    return sym.readWord(FX.BEXQU).then(function(head) {
      if (head === 0) return {};

      var set = {};
      var first = head;

      function walkExec(addr, count) {
        if (addr === 0 || count > 50) return Promise.resolve(set);
        set[addr] = true;
        return sym.readWord(addr + RT.WLINK).then(function(next) {
          if (next === first) return set;
          return walkExec(next, count + 1);
        });
      }

      return walkExec(head, 0);
    });
  }

  function deriveState(entry, rtrefVal, execSet) {
    if (entry.isCurrent) return 'Running';
    if (sym.testBit(entry.statu, SB.WAIT)) return 'I/O Wait';
    if (sym.testBit(entry.statu, SB.RWAI)) return 'Res Wait';
    if (sym.testBit(entry.statu, SB.SWWA)) return 'Swap Wait';
    if (sym.testBit(entry.statu, SB.RTOF)) return 'Off';
    if (execSet[entry.rtAddr]) return 'Ready';
    return 'Idle';
  }

  // Name resolution delegated to sintran-rt-names.js (window.resolveProcessName)
  // Uses reverse lookup from SYMBOL-2-LIST symbol tables, not in-memory strings.

  // =========================================================
  // State color mapping
  // =========================================================

  function stateClass(state) {
    switch (state) {
      case 'Running':   return 'proc-running';
      case 'Ready':     return 'proc-ready';
      case 'I/O Wait':
      case 'Res Wait':
      case 'Swap Wait': return 'proc-waiting';
      case 'Off':       return 'proc-off';
      default:          return 'proc-idle';
    }
  }

  // =========================================================
  // Rendering - Process List Table
  // =========================================================

  function renderProcessList(processes) {
    var body = document.getElementById('proc-list-body');
    if (!body) return;

    if (processes.length === 0) {
      body.innerHTML = '<div style="padding:12px;color:rgba(160,175,210,0.5);font-style:italic;">No processes found</div>';
      return;
    }

    var html = '<table class="proc-table"><thead><tr>' +
      '<th>RT#</th><th>Name</th><th>Status</th><th>Priority</th>' +
      '<th>Start</th><th>Code Seg</th><th>Data Seg</th>' +
      '</tr></thead><tbody>';

    for (var i = 0; i < processes.length; i++) {
      var p = processes[i];
      var cls = stateClass(p.state);
      var rowCls = p.isCurrent ? ' proc-row-current' : '';

      html += '<tr class="proc-row ' + cls + rowCls + '" data-rt="' + p.rtNum + '">' +
        '<td>' + p.rtNum + '</td>' +
        '<td>' + escHtml(p.name) + '</td>' +
        '<td><span class="proc-status-badge ' + cls + '">' + p.state + '</span></td>' +
        '<td>' + sym.toOctal(p.prity) + '</td>' +
        '<td>' + sym.toOctal(p.stadr) + '</td>' +
        '<td>' + sym.toOctal(p.segm1) + '</td>' +
        '<td>' + sym.toOctal(p.segm2) + '</td>' +
        '</tr>';
    }

    html += '</tbody></table>';
    body.innerHTML = html;

    // Attach click handlers
    var rows = body.querySelectorAll('.proc-row');
    for (var j = 0; j < rows.length; j++) {
      rows[j].addEventListener('click', function() {
        var rtNum = parseInt(this.getAttribute('data-rt'));
        selectProcess(rtNum, processes);
      });
    }

    // Re-select previously selected
    if (selectedRtNum >= 0) {
      selectProcess(selectedRtNum, processes);
    }
  }

  function escHtml(str) {
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  function escAttr(str) {
    return str.replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  // =========================================================
  // Rendering - Process Detail Panel
  // =========================================================

  function selectProcess(rtNum, processes) {
    selectedRtNum = rtNum;

    // Highlight row
    var rows = document.querySelectorAll('#proc-list-body .proc-row');
    for (var i = 0; i < rows.length; i++) {
      rows[i].classList.remove('proc-selected');
      if (parseInt(rows[i].getAttribute('data-rt')) === rtNum) {
        rows[i].classList.add('proc-selected');
      }
    }

    // Find process in provided list, fall back to unfiltered list
    var proc = null;
    for (var j = 0; j < processes.length; j++) {
      if (processes[j].rtNum === rtNum) { proc = processes[j]; break; }
    }
    if (!proc) {
      for (var k = 0; k < lastProcesses.length; k++) {
        if (lastProcesses[k].rtNum === rtNum) { proc = lastProcesses[k]; break; }
      }
    }

    var detail = document.getElementById('proc-detail-panel');
    if (!detail || !proc) {
      if (detail) detail.style.display = 'none';
      return;
    }

    detail.style.display = 'block';

    var html = '<div class="proc-detail-header">' +
      '<span class="proc-detail-title">' + escHtml(proc.name) + ' (RT #' + proc.rtNum + ')</span>' +
      '<span class="proc-status-badge ' + stateClass(proc.state) + '">' + proc.state + '</span>' +
      '</div>';

    // RT program description
    var desc = (typeof window.resolveProcessDescription === 'function')
      ? window.resolveProcessDescription(proc.rtNum) : null;
    if (desc) {
      html += '<div class="proc-detail-desc">' + escHtml(desc) + '</div>';
    }

    // All RT-Desc fields
    html += '<div class="proc-detail-section"><div class="proc-detail-section-title">RT Description</div>';
    var fields = [
      ['TLINK', proc.tlink], ['STATU', proc.statu], ['INPRI', proc.inpri],
      ['PRITY', proc.prity], ['DTIM1', proc.dtim1], ['DTIM2', proc.dtim2],
      ['DTIN1', proc.dtin1], ['DTIN2', proc.dtin2], ['STADR', proc.stadr],
      ['SEGM1', proc.segm1], ['SEGM2', proc.segm2], ['WLINK', proc.wlink],
      ['ACT1S', proc.act1s], ['ACT2S', proc.act2s], ['INIPR', proc.inipr],
      ['ACTPR', proc.actpr], ['BRESL', proc.bresl], ['RSEGM', proc.rsegm],
      ['BUFWI', proc.bufwi], ['TRMWI', proc.trmwi], ['N5WIN', proc.n5win],
      ['RTDLG', proc.rtdlg]
    ];
    html += '<div class="proc-fields-grid">';
    for (var f = 0; f < fields.length; f++) {
      var tip = FIELD_TIPS[fields[f][0]] || '';
      html += '<div class="proc-field" title="' + escAttr(tip) + '"><span class="proc-field-name">' + fields[f][0] +
        '</span><span class="proc-field-value">' + sym.toOctal(fields[f][1]) + '</span></div>';
    }
    html += '</div></div>';

    // STATU decoded bits
    html += '<div class="proc-detail-section"><div class="proc-detail-section-title">Status Flags</div>';
    html += '<div class="proc-status-pills">';
    var bitNames = Object.keys(SB);
    for (var b = 0; b < bitNames.length; b++) {
      var bitName = bitNames[b];
      var bitNum = SB[bitName];
      var isSet = sym.testBit(proc.statu, bitNum);
      var sTip = STATUS_TIPS[bitName] || '';
      html += '<span class="proc-pill' + (isSet ? ' proc-pill-active' : '') + '" title="' + escAttr(sTip) + '">' + bitName + '</span>';
    }
    html += '</div></div>';

    // Build remaining sections asynchronously (registers + reservation chain)
    var asyncHtml = Promise.resolve('');

    // Saved registers (via RTDLG pointer)
    if (proc.rtdlg !== 0) {
      asyncHtml = sym.readBlock(proc.rtdlg, 16).then(function(regData) {
        var rhtml = '';
        if (regData.length >= 8) {
          rhtml += '<div class="proc-detail-section"><div class="proc-detail-section-title">Saved Registers</div>';
          var regNames = ['P', 'X', 'T', 'A', 'D', 'L', 'S', 'B'];
          rhtml += '<div class="proc-regs-grid">';
          for (var r = 0; r < 8; r++) {
            var rTip = REG_TIPS[regNames[r]] || '';
            rhtml += '<div class="proc-reg" title="' + escAttr(rTip) + '"><span class="proc-reg-name">' + regNames[r] +
              '</span><span class="proc-reg-value">' + sym.toOctal(regData[r]) + '</span></div>';
          }
          rhtml += '</div>';

          // Page bitmap (8 words = 128 bits)
          if (regData.length >= 16) {
            var bitmapTip = 'Page Bitmap - 128 bits tracking which virtual pages this process has modified.\n' +
              'Each bit corresponds to one of the 128 logical pages (64 per PIT).\n' +
              'A set bit (1) means the page was written to and must be saved on swap-out.\n' +
              'Used by the segment manager to know which pages are dirty.';
            rhtml += '<div class="proc-detail-section-title" style="margin-top:6px;" title="' + escAttr(bitmapTip) + '">Page Bitmap</div>';
            var setBits = 0;
            rhtml += '<div class="proc-bitmap-grid">';
            for (var bw = 8; bw <= 15; bw++) {
              for (var bn = 0; bn < 16; bn++) {
                var pageNum = (bw - 8) * 16 + bn;
                var dirty = !!(regData[bw] & (1 << bn));
                if (dirty) setBits++;
                var boxTip = 'Page ' + pageNum + (dirty ? ' - modified (dirty)' : ' - clean');
                rhtml += '<span class="bm-box' + (dirty ? ' bm-set' : '') + '" title="' + escAttr(boxTip) + '"></span>';
              }
            }
            rhtml += '</div>';
            rhtml += '<div class="proc-bitmap-summary">' + setBits + ' of 128 pages dirty</div>';
          }
          rhtml += '</div>';
        }
        return rhtml;
      });
    }

    // Reservation chain (after registers)
    if (proc.bresl !== 0) {
      asyncHtml = asyncHtml.then(function(prevHtml) {
        var rtInfo = sym.discoverRtTableSync();
        var resChainTip = 'Reservation Chain - I/O devices reserved by this process.\n' +
          'When a program opens a device (terminal, disk, tape, etc.),\n' +
          'the device\'s I/O datafield is linked into this chain via BRESL.\n' +
          'The chain is circular and returns to the owning RT program.\n' +
          'A reserved device cannot be used by other programs until released.';
        var chtml = '<div class="proc-detail-section"><div class="proc-detail-section-title" title="' + escAttr(resChainTip) + '">Reservation Chain</div>';
        chtml += '<div class="proc-res-chain">';

        function walkResChain(addr, count, visited) {
          if (addr === 0 || addr === 0xFFFF || count >= 20 || visited[addr]) {
            return Promise.resolve('');
          }
          // Stop if chain loops back into RT table area
          if (rtInfo.base > 0 && addr >= rtInfo.base &&
              addr < rtInfo.base + rtInfo.count * RT.SIZE) {
            return Promise.resolve('');
          }
          visited[addr] = true;
          return Promise.all([
            sym.readWord(addr + sym.IO_DF.ISTAT),
            sym.readWord(addr + sym.IO_DF.RTRES),
            sym.readWord(addr + sym.IO_DF.RESLI)
          ]).then(function(vals) {
            var istat = vals[0], rtres = vals[1], nextAddr = vals[2];
            var devName = identifyDevice(addr, istat);
            var resTip = 'I/O Datafield at ' + sym.toOctal(addr) + '\n' +
              'Device: ' + devName + '\n' +
              'ISTAT: ' + sym.toOctal(istat) + '\n' +
              'Owner RT: ' + sym.toOctal(rtres) + '\n' +
              'This device is reserved (locked) by this RT program.';
            var itemHtml = '<div class="proc-res-item" title="' + escAttr(resTip) + '">' +
              '<span class="proc-res-addr">' + sym.toOctal(addr) + '</span>' +
              '<span class="proc-res-type">' + devName + '</span>' +
              '</div>';
            return walkResChain(nextAddr, count + 1, visited).then(function(rest) {
              return itemHtml + rest;
            });
          });
        }

        return walkResChain(proc.bresl, 0, {}).then(function(chainItems) {
          chtml += chainItems + '</div></div>';
          return prevHtml + chtml;
        });
      });
    }

    asyncHtml.then(function(extraHtml) {
      detail.innerHTML = html + extraHtml;
    });
  }

  // Identify device by ISTAT bits and known datafield address ranges.
  // Terminal DFs come in read/write pairs 13 octal (11 decimal) words apart.
  // DT01R address is version-dependent; terminals are sequential from there.
  function identifyDevice(dfAddr, istat) {
    var vsym = sym.getVersionSymbols();

    // Check ISTAT terminal bit first (most common)
    if (sym.testBit(istat, sym.ISTAT_BITS.TERM)) {
      var termNum = identifyTerminalNum(dfAddr, vsym);
      if (termNum >= 0) {
        var rw = isTerminalWrite(dfAddr, vsym) ? 'W' : 'R';
        return 'Terminal ' + termNum + ' (' + rw + ')';
      }
      return 'Terminal';
    }

    // Floppy
    if (sym.testBit(istat, sym.ISTAT_BITS.FLOP)) return 'Floppy Disc';

    // Magnetic tape
    if (sym.testBit(istat, sym.ISTAT_BITS.MT)) return 'Magnetic Tape';

    // Bad device flag
    if (sym.testBit(istat, sym.ISTAT_BITS.BAD)) return 'Bad Device';

    // No ISTAT type bits set - likely a disk or other mass storage.
    // ISTAT=0 is typical for SMD/Winchester disk datafields.
    if (istat === 0) return 'Disk/Mass Storage';

    // Fallback: show raw ISTAT so user can investigate
    return 'Device (' + sym.toOctal(istat) + ')';
  }

  // Try to identify terminal number from datafield address.
  // Terminals start at DT01R and are spaced 13 octal (11 decimal) words apart
  // for read DFs; write DFs are at DTnnR + 13 octal.
  function identifyTerminalNum(dfAddr, vsym) {
    if (!vsym || !vsym.DT01R) return -1;
    var dt01r = vsym.DT01R;
    var spacing = 22; // 2 DFs per terminal: read + write, each 11 words = 22 total
    var offset = dfAddr - dt01r;
    if (offset < 0) return -1;
    var termNum = Math.floor(offset / spacing) + 1;
    if (termNum > 64) return -1;
    return termNum;
  }

  function isTerminalWrite(dfAddr, vsym) {
    if (!vsym || !vsym.DT01R) return false;
    var dt01r = vsym.DT01R;
    var offset = dfAddr - dt01r;
    if (offset < 0) return false;
    // Write DF is 11 words after read DF within each 22-word pair
    return (offset % 22) >= 11;
  }

  // =========================================================
  // Refresh
  // =========================================================

  function refreshProcessList() {
    if (!sintranState || !sintranState.detected) return;
    readProcessList().then(function(processes) {
      lastProcesses = processes;
      var filtered = applyFilter(processes);
      renderProcessList(filtered);
    });
  }

  function startAutoRefresh() {
    stopAutoRefresh();
    if (autoRefresh) {
      refreshTimer = setInterval(refreshProcessList, 2000);
    }
  }

  function stopAutoRefresh() {
    if (refreshTimer) {
      clearInterval(refreshTimer);
      refreshTimer = null;
    }
  }

  // =========================================================
  // Window show/hide
  // =========================================================

  function showWindow() {
    var win = document.getElementById('process-list-window');
    if (win) {
      win.style.display = 'flex';
      refreshProcessList();
      startAutoRefresh();
    }
  }

  function hideWindow() {
    var win = document.getElementById('process-list-window');
    if (win) win.style.display = 'none';
    stopAutoRefresh();
  }

  // =========================================================
  // Copy to markdown
  // =========================================================

  function copyAsMarkdown() {
    var filtered = applyFilter(lastProcesses);
    if (filtered.length === 0) return;

    var filters = getActiveFilters();
    var md = '';
    if (filters.length > 0) {
      md += 'Filter: ' + filters.join(', ') + '\n\n';
    }

    md += '| RT# | Name | Status | Priority | Start | Code Seg | Data Seg |\n';
    md += '|-----|------|--------|----------|-------|----------|----------|\n';

    for (var i = 0; i < filtered.length; i++) {
      var p = filtered[i];
      md += '| ' + p.rtNum +
        ' | ' + p.name +
        ' | ' + p.state +
        ' | ' + sym.toOctal(p.prity) +
        ' | ' + sym.toOctal(p.stadr) +
        ' | ' + sym.toOctal(p.segm1) +
        ' | ' + sym.toOctal(p.segm2) +
        ' |\n';
    }

    navigator.clipboard.writeText(md).then(function() {
      var btn = document.getElementById('proc-list-copy');
      if (btn) {
        btn.classList.add('copied');
        setTimeout(function() { btn.classList.remove('copied'); }, 1200);
      }
    });
  }

  // =========================================================
  // Event handlers
  // =========================================================

  var closeBtn = document.getElementById('proc-list-close');
  if (closeBtn) {
    closeBtn.addEventListener('click', hideWindow);
  }

  var copyBtn = document.getElementById('proc-list-copy');
  if (copyBtn) {
    copyBtn.addEventListener('click', copyAsMarkdown);
  }

  var refreshBtn = document.getElementById('proc-list-refresh');
  if (refreshBtn) {
    refreshBtn.addEventListener('click', refreshProcessList);
  }

  var autoRefreshCb = document.getElementById('proc-auto-refresh');
  if (autoRefreshCb) {
    autoRefreshCb.addEventListener('change', function() {
      autoRefresh = this.checked;
      if (autoRefresh) startAutoRefresh();
      else stopAutoRefresh();
    });
  }

  // Filter checkboxes: re-render from cached data (no re-read needed)
  var filterBar = document.getElementById('proc-filter-bar');
  if (filterBar) {
    filterBar.addEventListener('change', function() {
      var filtered = applyFilter(lastProcesses);
      renderProcessList(filtered);
    });
  }

  // =========================================================
  // Navigate to a specific RT program (called from other windows)
  // =========================================================
  function navigateToProcess(rtNum) {
    selectedRtNum = rtNum;
    var win = document.getElementById('process-list-window');
    if (!win || win.style.display === 'none' || win.style.display === '') {
      showWindow();
    } else {
      refreshProcessList();
    }
    if (typeof windowManager !== 'undefined') {
      windowManager.focus('process-list-window');
    }
  }

  // =========================================================
  // Export
  // =========================================================
  window.procListShowWindow = showWindow;
  window.procListHideWindow = hideWindow;
  window.procListNavigateToRT = navigateToProcess;
})();
