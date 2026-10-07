//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// sintran-segments.js - Segment Table Viewer
// Displays SINTRAN III segment table entries with flags and cross-references
// Algorithm from SINTRAN-STRUCTURES.md Section 6.17:
//   1. Read SGMAX, SEGTB, SEGST via DPIT
//   2. Physical base = (SEGTB << 16) + SEGST
//   3. For each segment 0..SGMAX, read 8-word entry at physBase + segNum*8
//   4. Skip all-zero entries
//   5. Cross-reference: RT SEGM1/SEGM2 contain segment NUMBERS (not addresses)

(function() {
  'use strict';

  var sym = window.sintranSymbols;
  var FX = sym.FIXED;
  var SE = sym.SEG_ENTRY;
  var RT = sym.RT_DESC;

  var refreshTimer = null;
  var autoRefresh = true;
  var lastSegments = [];
  var selectedSegNum = -1;

  // =========================================================
  // Segment FLAG bit names (word offset 5)
  // Source: SINTRAN-STRUCTURES.md Section 6.5
  // =========================================================
  var FLAG_BITS = {
    0: 'OK',        // 5OK - segment valid
    1: 'INHIB',     // 5INHB - inhibited (swapped out / not loaded)
    3: 'PROT',      // PROTECT - write protected
    4: 'SHARED',    // 5SREE - shared/reentrant
    5: 'FIXED',     // 5FIXC - fixed in core (never swapped)
    6: 'DEMAND'     // demand-loaded
  };

  // =========================================================
  // Segment STATUS (SGSTA) bit definitions (word offset 6)
  // Source: SINTRAN-STRUCTURES.md Section 6.6
  // =========================================================
  // Bit 15: WPM  (write permit)
  // Bit 14: RPM  (read permit)
  // Bit 13: FPM  (fetch permit)
  // Bits 10-9: Ring (0-3)
  // Bit 0: NOCLEAR (do not clear on load)

  function decodeSgsta(sgsta) {
    var parts = [];
    if (sgsta & (1 << 15)) parts.push('WPM');
    if (sgsta & (1 << 14)) parts.push('RPM');
    if (sgsta & (1 << 13)) parts.push('FPM');
    var ring = (sgsta >> 9) & 0x03;
    parts.push('R' + ring);
    if (sgsta & 1) parts.push('NOCLEAR');
    return parts.join(' ');
  }

  // =========================================================
  // Data reading - Section 6.17 algorithm
  // =========================================================

  // Returns Promise<Array>
  function readSegmentTable() {
    // Step 1: Read root pointers via DPIT
    return Promise.all([sym.readWord(FX.SGMAX), sym.readWord(FX.SEGTB), sym.readWord(FX.SEGST)])
    .then(function(vals) {
      var sgmax = vals[0], segtb = vals[1], segst = vals[2];

      if (sgmax === 0 || sgmax > 4096) return [];

      // Step 2: Compute physical base address
      var physBase = (segtb << 16) + segst;

      // Step 3: Read all segment entries via physical memory
      // Read entire block at once for efficiency
      var totalWords = (sgmax + 1) * SE.SIZE;
      return sym.readBlockPhysical(physBase, totalWords).then(function(allData) {
        var segments = [];
        for (var segNum = 0; segNum <= sgmax; segNum++) {
          var offset = segNum * SE.SIZE;
          if (offset + SE.SIZE > allData.length) continue;

          // Step 4: Skip all-zero entries (unused segments)
          var allZero = true;
          for (var w = 0; w < SE.SIZE; w++) {
            if (allData[offset + w] !== 0) { allZero = false; break; }
          }
          if (allZero) continue;

          segments.push({
            segNum: segNum,
            segli:  allData[offset + SE.SEGLI],
            prese:  allData[offset + SE.PRESE],
            logad:  allData[offset + SE.LOGAD],
            segle:  allData[offset + SE.SEGLE],
            madr:   allData[offset + SE.MADR],
            flag:   allData[offset + SE.FLAG],
            sgsta:  allData[offset + SE.SGSTA],
            bpagl:  allData[offset + SE.BPAGL],
            usedBy: []
          });
        }

        // Step 5: Cross-reference with RT descriptions
        return crossReferenceSegments(segments).then(function() {
          return segments;
        });
      });
    });
  }

  // Returns Promise<void>
  function crossReferenceSegments(segments) {
    return sym.discoverRtTable().then(function(rtInfo) {
      if (rtInfo.base === 0 || rtInfo.count === 0) return;

      var rtCount = rtInfo.count;
      return sym.readBlock(rtInfo.base, rtCount * RT.SIZE).then(function(tableData) {
        if (tableData.length === 0) return;

        // Build a map of segment NUMBER to segment entry for fast lookup
        var segByNum = {};
        for (var s = 0; s < segments.length; s++) {
          segByNum[segments[s].segNum] = segments[s];
        }

        for (var i = 0; i < rtCount; i++) {
          var base = i * RT.SIZE;
          var statu = tableData[base + RT.STATU];
          if (!sym.testBit(statu, sym.STATU_BITS.USED)) continue;

          // RT SEGM1/SEGM2/ACT1S/ACT2S contain segment NUMBERS
          var segFields = [
            tableData[base + RT.SEGM1],
            tableData[base + RT.SEGM2],
            tableData[base + RT.ACT1S],
            tableData[base + RT.ACT2S]
          ];

          for (var f = 0; f < segFields.length; f++) {
            var segNum = segFields[f];
            if (segNum !== 0 && segByNum[segNum]) {
              var label = (typeof window.resolveProcessName === 'function')
                ? window.resolveProcessName(i) : 'RT #' + i;
              if (segByNum[segNum].usedBy.indexOf(label) === -1) {
                segByNum[segNum].usedBy.push(label);
              }
            }
          }
        }
      }); // end readBlock.then
    }); // end discoverRtTable.then
  }

  // =========================================================
  // Flag decoding
  // =========================================================

  var FLAG_TIPS = {
    0: 'OK - segment is valid and loaded in memory',
    1: 'INHIB - inhibited: segment is swapped out to disk or not yet loaded',
    3: 'PROT - write-protected: any write attempt causes a trap',
    4: 'SHARED - shared/reentrant: multiple RT programs share this segment',
    5: 'FIXED - permanently resident in physical memory, never swapped',
    6: 'DEMAND - demand-loaded: pages loaded from disk only when accessed'
  };

  function decodeFlags(flag) {
    var parts = [];
    for (var bit in FLAG_BITS) {
      if (sym.testBit(flag, parseInt(bit))) {
        parts.push(FLAG_BITS[bit]);
      }
    }
    return parts.length > 0 ? parts.join(', ') : '-';
  }

  function flagsTip(flag) {
    var lines = [];
    for (var bit in FLAG_BITS) {
      var b = parseInt(bit);
      var set = sym.testBit(flag, b);
      lines.push((set ? '[X] ' : '[ ] ') + FLAG_TIPS[bit]);
    }
    return lines.join('\n');
  }

  function sgstaTip(sgsta) {
    var wpm = !!(sgsta & (1 << 15));
    var rpm = !!(sgsta & (1 << 14));
    var fpm = !!(sgsta & (1 << 13));
    var ring = (sgsta >> 9) & 0x03;
    var noclear = !!(sgsta & 1);
    var lines = [
      'Memory protection and access control bits:',
      (wpm ? '[X]' : '[ ]') + ' WPM - write permit: processes can write to this segment',
      (rpm ? '[X]' : '[ ]') + ' RPM - read permit: processes can read this segment',
      (fpm ? '[X]' : '[ ]') + ' FPM - fetch permit: CPU can execute code from this segment',
      'Ring ' + ring + ' - protection ring (0=kernel, 1-2=system, 3=user)',
      (noclear ? '[X]' : '[ ]') + ' NOCLEAR - do not zero-fill pages when loaded from disk'
    ];
    return lines.join('\n');
  }

  // =========================================================
  // Rendering
  // =========================================================

  function escAttr(str) {
    return str.replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  // Column header tooltips
  var COL_TIPS = {
    segNum: 'Unique index in the segment table',
    name:   'System segment name from SINTRAN release documentation.\nS3xxx = active, S3Sxxx = save copy, S3Ixxx = image copy.\nUser segments (136-255) show the RT programs that use them.',
    logad:  'Virtual base page number (VPN).\nFor user segments: first word address = LOGAD × 1024.\nFor system segments: LOGAD is an absolute VPN index (may exceed 64KW user space).',
    pages:  'Segment length in pages (octal).\nEach SINTRAN page = 512 words = 1 disk sector.\nTotal data = pages × 512 words.',
    madr:   'Disk swap location.\nWhere this segment is stored on disk when swapped out.',
    bpagl:  'First physical page in the core map chain.\nLinks to the physical memory pages allocated to this segment.',
    flags:  'Loading, sharing and protection behavior.\nHover over cells for details on each flag.',
    status: 'Memory protection bits and ring level.\nControls read/write/execute permissions.\nHover over cells for details.',
    usedBy: 'Which RT programs reference this segment.\nShows processes using this as code, data, or active segment.'
  };

  function renderSegmentTable(segments) {
    var body = document.getElementById('segment-table-body');
    if (!body) return;

    if (segments.length === 0) {
      body.innerHTML = '<div style="padding:12px;color:rgba(160,175,210,0.5);font-style:italic;">No segments found</div>';
      return;
    }

    var html = '<table class="proc-table"><thead><tr>' +
      '<th title="' + escAttr(COL_TIPS.segNum) + '">Seg#</th>' +
      '<th title="' + escAttr(COL_TIPS.name) + '">Name</th>' +
      '<th title="' + escAttr(COL_TIPS.logad) + '">Virtual Page</th>' +
      '<th title="' + escAttr(COL_TIPS.pages) + '">Size</th>' +
      '<th title="' + escAttr(COL_TIPS.madr) + '">Disk Location</th>' +
      '<th title="' + escAttr(COL_TIPS.bpagl) + '">Core Map Link</th>' +
      '<th title="' + escAttr(COL_TIPS.flags) + '">Flags</th>' +
      '<th title="' + escAttr(COL_TIPS.status) + '">Protection</th>' +
      '<th title="' + escAttr(COL_TIPS.usedBy) + '">Used By</th>' +
      '</tr></thead><tbody>';

    for (var i = 0; i < segments.length; i++) {
      var s = segments[i];
      var pages = s.segle & 0x3FF;
      var pagesStr = pages.toString(8);
      var pagesTip = pages + ' pages × 512 words = ' + (pages * 512) + ' words on disk';
      var localVpn = s.logad & 0x3F;
      var logadTip = 'VPN ' + sym.toOctal(s.logad) +
        ' → load word addr ' + (s.logad << 10).toString(8) +
        ' (local ' + sym.toOctal(localVpn << 10) + ')';
      var usedByStr = s.usedBy.length > 0 ? s.usedBy.join(', ') : '-';

      // Resolve segment name and description
      var segName = (typeof window.resolveSegmentName === 'function')
        ? window.resolveSegmentName(s.segNum) : '';
      var segDesc = (typeof window.resolveSegmentDescription === 'function')
        ? window.resolveSegmentDescription(s.segNum) : null;
      var segCat = (typeof window.resolveSegmentCategory === 'function')
        ? window.resolveSegmentCategory(s.segNum) : null;
      var reentName = (typeof window.getREECOMTNameForSeg === 'function')
        ? window.getREECOMTNameForSeg(s.segNum) : '';
      var nameTip = '';
      if (segDesc) {
        nameTip = segDesc;
        if (reentName) nameTip += '\nReentrant: ' + reentName;
        if (segCat) nameTip += '\nCategory: ' + segCat;
      } else if (reentName) {
        nameTip = 'Reentrant program: ' + reentName;
      } else if (s.usedBy.length > 0) {
        nameTip = 'User segment used by: ' + s.usedBy.join(', ');
      }
      // Prefer REECOMT program name; fall back to static name then RT cross-reference
      var nameDisplay = reentName || segName || (s.usedBy.length > 0 ? s.usedBy[0] : '');

      var segOct = s.segNum.toString(8);
      html += '<tr class="proc-row" data-segnum="' + s.segNum + '">' +
        '<td title="Segment ' + segOct + ' octal (' + s.segNum + ' decimal)">' + segOct + '</td>' +
        '<td title="' + escAttr(nameTip) + '">' + nameDisplay + '</td>' +
        '<td title="' + escAttr(logadTip) + '">' + sym.toOctal(s.logad) + '</td>' +
        '<td title="' + escAttr(pagesTip) + '">' + pagesStr + '</td>' +
        '<td title="Disk swap address (octal)">' + sym.toOctal(s.madr) + '</td>' +
        '<td title="First physical page in core map chain">' + sym.toOctal(s.bpagl) + '</td>' +
        '<td title="' + escAttr(flagsTip(s.flag)) + '">' + decodeFlags(s.flag) + '</td>' +
        '<td title="' + escAttr(sgstaTip(s.sgsta)) + '">' + decodeSgsta(s.sgsta) + '</td>' +
        '<td title="' + escAttr(usedByStr) + '">' + usedByStr + '</td>' +
        '</tr>';
    }

    html += '</tbody></table>';
    body.innerHTML = html;

    // Attach click handlers
    var rows = body.querySelectorAll('.proc-row');
    for (var ri = 0; ri < rows.length; ri++) {
      rows[ri].addEventListener('click', function() {
        var sn = parseInt(this.getAttribute('data-segnum'));
        var seg = null;
        for (var k = 0; k < segments.length; k++) {
          if (segments[k].segNum === sn) { seg = segments[k]; break; }
        }
        selectSegment(sn, seg);
      });
    }

    // Re-select previously selected segment
    if (selectedSegNum >= 0) {
      var prevSeg = null;
      for (var pi = 0; pi < segments.length; pi++) {
        if (segments[pi].segNum === selectedSegNum) { prevSeg = segments[pi]; break; }
      }
      if (prevSeg) selectSegment(selectedSegNum, prevSeg);
    }
  }

  // =========================================================
  // Segment detail pane
  // =========================================================

  function selectSegment(segNum, seg) {
    selectedSegNum = segNum;

    // Highlight row
    var rows = document.querySelectorAll('#segment-table-body .proc-row');
    for (var i = 0; i < rows.length; i++) {
      rows[i].classList.remove('proc-selected');
      if (parseInt(rows[i].getAttribute('data-segnum')) === segNum) {
        rows[i].classList.add('proc-selected');
      }
    }

    var panel = document.getElementById('segment-detail-panel');
    if (!panel || !seg) return;
    panel.style.display = 'block';
    panel.innerHTML = renderSegDetailHtml(seg);

    // Async: fill core map chain
    if (seg.bpagl) {
      loadCoreMapChain(seg);
    }
  }

  function loadCoreMapChain(seg) {
    sym.readWord(sym.FIXED.CORMB).then(function(cormb) {
      var cormBase = (cormb & 0xFF) << 16;
      var limit = 32;
      var entries = [];

      function walk(addr) {
        if (addr === 0 || limit-- <= 0) return Promise.resolve();
        return sym.readBlockPhysical(cormBase + addr, 4).then(function(cme) {
          if (!cme || cme.length < 4) return Promise.resolve();
          entries.push({
            physPage: (addr / 4) | 0,
            prote:    cme[2],
            logpa:    cme[3],
            relPage:  cme[3] - seg.logad
          });
          return walk(cme[0]);
        });
      }

      return walk(seg.bpagl).then(function() {
        var el = document.getElementById('seg-cmap-chain-' + seg.segNum);
        if (!el) return;
        if (entries.length === 0) { el.textContent = 'no entries'; return; }
        var html = '<table class="seg-cmap-table"><tr><th>physPage</th><th>rel</th><th>LOGPA</th><th>prot</th></tr>';
        for (var i = 0; i < entries.length; i++) {
          var e = entries[i];
          var wpm = (e.prote >> 15) & 1, rpm = (e.prote >> 14) & 1, fpm = (e.prote >> 13) & 1;
          var prot = (fpm ? 'X' : '-') + (rpm ? 'R' : '-') + (wpm ? 'W' : '-');
          html += '<tr>' +
            '<td>' + sym.toOctal(e.physPage) + '</td>' +
            '<td>' + e.relPage + '</td>' +
            '<td>' + sym.toOctal(e.logpa) + '</td>' +
            '<td>' + prot + '</td>' +
            '</tr>';
        }
        html += '</table>';
        if (limit <= 0) html += '<div style="font-size:10px;color:rgba(160,175,210,0.5);">chain truncated at 32</div>';
        el.innerHTML = html;
      });
    }).catch(function(e) {
      console.warn('[seg] chain walk failed for seg ' + seg.segNum + ':', e.message || e);
      var el = document.getElementById('seg-cmap-chain-' + seg.segNum);
      if (el) el.innerHTML = '<span style="color:rgba(255,120,120,0.7);">chain walk failed: ' +
        escAttr(e.message || String(e)) + '</span>';
    });
  }

  function renderSegDetailHtml(seg) {
    var segOct = seg.segNum.toString(8).padStart(3, '0');
    var segName = (typeof window.resolveSegmentName === 'function')
      ? window.resolveSegmentName(seg.segNum) : '';
    var title = segName ? segName + ' (' + segOct + ')' : 'Segment ' + segOct;

    var html = '<div class="seg-detail-title">' + escAttr(title) + '</div>';

    // LRU links
    html += '<div class="seg-detail-section">LRU Links</div>';
    html += detailField('SEGLI', sym.toOctal(seg.segli), 'Forward LRU link');
    html += detailField('PRESE', sym.toOctal(seg.prese), 'Backward LRU link');

    // Addressing
    html += '<div class="seg-detail-section">Addressing</div>';
    var localVpnBase = seg.logad & 0x3F;
    html += detailField('LOGAD', sym.toOctal(seg.logad),
      'VPN ' + sym.toOctal(seg.logad) +
      ' → load word addr ' + (seg.logad << 10).toString(8) +
      ' (local ' + sym.toOctal(localVpnBase << 10) + ')');
    var pages = seg.segle & 0x3FF;
    html += detailField('SEGLE', sym.toOctal(pages) + ' (' + pages + ' pages)', pages + ' pages × 512 words = ' + (pages * 512) + ' words');
    html += detailField('MADR',  sym.toOctal(seg.madr), 'Disk sector offset from SEGFIL base');

    // Flags
    var segfilN = (seg.flag >> 13) & 0x7;
    html += '<div class="seg-detail-section">FLAG (SEGFIL ' + segfilN + ')</div>';
    var flagBits = [
      { bit: 0, name: 'OK' }, { bit: 1, name: 'INHIB' }, { bit: 3, name: 'PROT' },
      { bit: 4, name: 'SHARED' }, { bit: 5, name: 'FIXED' }, { bit: 6, name: 'DEMAND' }
    ];
    html += '<div class="seg-detail-bits">';
    for (var fb = 0; fb < flagBits.length; fb++) {
      var active = sym.testBit(seg.flag, flagBits[fb].bit) ? ' active' : '';
      html += '<span class="seg-detail-bit' + active + '">' + flagBits[fb].name + '</span>';
    }
    html += '</div>';
    html += detailField('FLAG', sym.toOctal(seg.flag), 'Raw flag word');

    // SGSTA
    html += '<div class="seg-detail-section">SGSTA (Protection)</div>';
    var sgstaBits = [
      { mask: 1 << 15, name: 'WPM' }, { mask: 1 << 14, name: 'RPM' }, { mask: 1 << 13, name: 'FPM' },
      { mask: 1, name: 'NOCLEAR' }
    ];
    var ring = (seg.sgsta >> 9) & 0x03;
    html += '<div class="seg-detail-bits">';
    for (var sb = 0; sb < sgstaBits.length; sb++) {
      var sact = (seg.sgsta & sgstaBits[sb].mask) ? ' active' : '';
      html += '<span class="seg-detail-bit' + sact + '">' + sgstaBits[sb].name + '</span>';
    }
    html += '<span class="seg-detail-bit active">R' + ring + '</span>';
    html += '</div>';
    html += detailField('SGSTA', sym.toOctal(seg.sgsta), decodeSgsta(seg.sgsta));

    // Disk range
    var segfilBase0 = (typeof window.getDiscoveredSegfilBase0 === 'function') ? window.getDiscoveredSegfilBase0() : 0;
    var diskBase = segfilN === 4 ? 0 : (segfilBase0 || null);
    html += '<div class="seg-detail-section">Disk (SEGFIL ' + segfilN + ')</div>';
    if (diskBase !== null) {
      var firstLBA = diskBase + seg.madr;
      var lastLBA  = firstLBA + pages - 1;
      html += detailField('LBA range', firstLBA + ' – ' + lastLBA,
        'Sectors ' + firstLBA + ' to ' + lastLBA + ' (' + pages + ' × 512 words)');
    } else {
      html += detailField('MADR offset', sym.toOctal(seg.madr), 'Relative to SEGFIL base (run SINTRAN to discover base)');
    }

    // Core map
    html += '<div class="seg-detail-section">Core Map (' +
      (seg.bpagl ? 'in RAM' : 'not loaded') + ')</div>';
    if (seg.bpagl) {
      html += detailField('BPAGL', sym.toOctal(seg.bpagl), 'Head page (physPage ' + ((seg.bpagl / 4) | 0) + ')');
      html += '<div id="seg-cmap-chain-' + seg.segNum + '" class="seg-detail-coremap" ' +
        'style="font-size:10px;font-family:monospace;">' +
        '<span style="color:rgba(160,175,210,0.4);">loading…</span></div>';
    } else {
      html += '<div class="seg-detail-coremap" style="color:rgba(160,175,210,0.4);">BPAGL=0 — segment not in RAM</div>';
    }

    // Used By
    if (seg.usedBy && seg.usedBy.length > 0) {
      html += '<div class="seg-detail-section">Used By</div>';
      html += '<div class="seg-detail-coremap">' + seg.usedBy.join(', ') + '</div>';
    }

    // Disassembler button
    html += '<button class="seg-detail-btn" onclick="segDisasmOpenWindow(' + seg.segNum + ')">Open Disassembler</button>';

    return html;
  }

  function detailField(name, value, tip) {
    var tipAttr = tip ? ' title="' + escAttr(tip) + '"' : '';
    return '<div class="seg-detail-field"' + tipAttr + '>' +
      '<span class="seg-detail-field-name">' + name + '</span>' +
      '<span class="seg-detail-field-value">' + value + '</span>' +
      '</div>';
  }

  // =========================================================
  // Refresh
  // =========================================================

  function refreshSegments() {
    if (!sintranState || !sintranState.detected) return;
    sym.invalidateRtCache();
    readSegmentTable().then(function(segments) {
      lastSegments = segments;
      renderSegmentTable(segments);
    });
  }

  function startAutoRefresh() {
    stopAutoRefresh();
    if (autoRefresh) {
      refreshTimer = setInterval(refreshSegments, 2000);
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
    var win = document.getElementById('segment-table-window');
    if (win) {
      win.style.display = 'flex';
      refreshSegments();
      startAutoRefresh();
    }
  }

  function hideWindow() {
    var win = document.getElementById('segment-table-window');
    if (win) win.style.display = 'none';
    stopAutoRefresh();
  }

  // =========================================================
  // Copy to markdown
  // =========================================================

  function copyAsMarkdown() {
    if (lastSegments.length === 0) return;

    var md = '| Seg# | Name | Virtual Page | Size | Disk Location | Core Map Link | Flags | Protection | Used By |\n';
    md += '|------|------|-------------|------|---------------|---------------|-------|------------|---------|\n';

    for (var i = 0; i < lastSegments.length; i++) {
      var s = lastSegments[i];
      var pagesStr = (s.segle & 0x3FF).toString(8);
      var segName = (typeof window.resolveSegmentName === 'function')
        ? window.resolveSegmentName(s.segNum) : '';
      var nameDisplay = segName || (s.usedBy.length > 0 ? s.usedBy[0] : '');
      md += '| ' + s.segNum.toString(8) +
        ' | ' + nameDisplay +
        ' | ' + sym.toOctal(s.logad) +
        ' | ' + pagesStr +
        ' | ' + sym.toOctal(s.madr) +
        ' | ' + sym.toOctal(s.bpagl) +
        ' | ' + decodeFlags(s.flag) +
        ' | ' + decodeSgsta(s.sgsta) +
        ' | ' + (s.usedBy.length > 0 ? s.usedBy.join(', ') : '-') +
        ' |\n';
    }

    navigator.clipboard.writeText(md).then(function() {
      var btn = document.getElementById('segment-table-copy');
      if (btn) {
        btn.classList.add('copied');
        setTimeout(function() { btn.classList.remove('copied'); }, 1200);
      }
    });
  }

  // =========================================================
  // Event handlers
  // =========================================================

  var closeBtn = document.getElementById('segment-table-close');
  if (closeBtn) closeBtn.addEventListener('click', hideWindow);

  var copyBtn = document.getElementById('segment-table-copy');
  if (copyBtn) copyBtn.addEventListener('click', copyAsMarkdown);

  var refreshBtn = document.getElementById('segment-table-refresh');
  if (refreshBtn) refreshBtn.addEventListener('click', refreshSegments);

  var autoRefreshCb = document.getElementById('segment-auto-refresh');
  if (autoRefreshCb) {
    autoRefreshCb.addEventListener('change', function() {
      autoRefresh = this.checked;
      if (autoRefresh) startAutoRefresh();
      else stopAutoRefresh();
    });
  }

  // =========================================================
  // Export
  // =========================================================
  window.segTableShowWindow   = showWindow;
  window.segTableHideWindow   = hideWindow;
  window.readSegmentTable     = readSegmentTable;
  window.renderSegDetailHtml  = renderSegDetailHtml;
  window.loadSegCoreMapChain  = loadCoreMapChain;
  window.getLastSegments      = function() { return lastSegments; };
})();
