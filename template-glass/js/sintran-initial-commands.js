//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// sintran-initial-commands.js - Initial Commands window
//
// Shows the SINTRAN III *initial-command buffer* - the commands that run
// first at every restart, exactly as "@LIST-INITIAL-COMMANDS" prints them
// (e.g. ENTER-DIR,,DI-75-1,0 / SET-AVAIL / APPEND-BATCH ... LOAD-MODE).
//
// The buffer has no file form: it lives at kernel symbol INIBU inside the
// SINTRAN command segment, whose disk image is in (SYSTEM)SEGFIL0:DATA.
// It is located WITHOUT byte-searching, purely from the symbol value plus
// the segment table read off the same disk image:
//
//   1. find the segment table page in SEGFIL0 (self-validating: the page
//      contains an entry whose MADR equals that page number)
//   2. find the segment whose virtual range contains INIBU:
//        segBase = (LOGAD & 0x3F) * 1024        [words]
//        segBase <= INIBU < segBase + SEGLE*1024
//   3. byteOffset = MADR*2048 + (INIBU - segBase)*2
//
// Buffer format (verified on live L07, incl. an offline write + boot):
//   command := 7-bit ASCII text + 0x27 ("'"), zero-padded to a word boundary
//   end     := a lone 0x27 (an empty command), then 0x00 fill
//   length  := a word at INIBU+130 words = the text byte count (excl. terminator).
//              SINTRAN reads exactly that many bytes; a writer MUST update it.
//
// Full derivation and evidence: NDInsight SINTRAN/OS/26-INITIAL-COMMAND-BUFFER-ON-DISK.md

(function() {
  'use strict';

  // Kernel symbol INIBU (start of the buffer) per SINTRAN version.
  // Source: SYMBOL-1-LIST.SYMB.TXT of each version.
  var INIBU = { K: 0o67172, L: 0o74123, M: 0o102327 };

  // Region layout (identical in K03/L07/M06):
  //   INIBU+0..+129  command text        (260 bytes)
  //   INIBU+130      length cell         (byte count of the text)
  //   INIBU+131      INCOM               (executable code - never write here)
  var CAP_WORDS = 131;
  var CAP_BYTES = CAP_WORDS * 2;
  var LEN_CELL_WORDS = 130;              // word offset of the length cell
  var LEN_CELL_BYTES = LEN_CELL_WORDS * 2;

  // Maximum text length SINTRAN itself accepts. Read from the NEXIN handler:
  // a character is stored only while its byte offset X <= 252 (literal 0o374),
  // then the length becomes (X+2) rounded down to even => at most 254.
  // The literal 252 was read from BOTH the L07 and M06 handlers (identical
  // instruction sequence). K03's literal has not been read, but its region is
  // laid out the same, so 254 is expected there too.
  var MAX_TEXT_BYTES = 254;

  // Per-version writer bound. M06 was previously capped at 172 because the
  // symbol CPTSL sits at INIBU+86 words; reading M06's NEXIN showed the same
  // limit literal (252), i.e. SINTRAN itself writes straight through CPTSL,
  // so it is NOT a buffer bound. See doc 26 section 2.2.
  var MAX_TEXT_BYTES_BY_VERSION = { K: 254, L: 254, M: 254 };

  var SEG_ENTRY_BYTES = 16;   // 8 words
  var PAGE_BYTES = 2048;      // 1024 words
  var CMD_END = 0x27;         // "'" terminates a command; alone = end of buffer

  function byId(id) { return document.getElementById(id); }
  function esc(s) {
    return String(s == null ? '' : s)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
  }
  function word(b, o) { return (b[o] << 8) | b[o + 1]; }

  function segEntry(seg, page, n) {
    var o = page * PAGE_BYTES + n * SEG_ENTRY_BYTES;
    return {
      logad: word(seg, o + 4),
      segle: word(seg, o + 6) & 0x3FF,
      madr:  word(seg, o + 8),
      flag:  word(seg, o + 10),
      sgsta: word(seg, o + 12)
    };
  }

  // Locate the segment-table page inside SEGFIL0.
  // A real segment-table page: entry 0 is all-zero, it has many plausible
  // entries, and it is self-describing (some entry's MADR == this page).
  function findSegmentTablePage(seg) {
    var pages = Math.floor(seg.length / PAGE_BYTES);
    var best = null;
    for (var p = 0; p < pages; p++) {
      var zero = true;
      for (var i = 0; i < SEG_ENTRY_BYTES; i++) {
        if (seg[p * PAGE_BYTES + i]) { zero = false; break; }
      }
      if (!zero) continue;

      var good = 0, selfRef = false;
      for (var n = 1; n < 128; n++) {
        var e = segEntry(seg, p, n);
        if (!e.segle && !e.madr && !e.logad) continue;
        if (e.segle > 0 && e.segle < 1024 && e.madr > 0 && (e.flag & 1) && (e.sgsta & 0xE000)) {
          good++;
          if (e.madr === p && e.segle <= 32) selfRef = true;
        }
      }
      if (good >= 40 && selfRef && (!best || good > best.good)) best = { page: p, good: good };
    }
    return best ? best.page : -1;
  }

  // Parse the buffer: text + 0x27, word-aligned; lone 0x27 ends it.
  function parseBuffer(seg, off, capBytes) {
    var cmds = [], i = off, end = Math.min(off + capBytes, seg.length);
    while (i < end) {
      if (seg[i] === 0x00) { i++; continue; }        // word-alignment padding
      if (seg[i] === CMD_END) break;                  // empty command = terminator
      var s = '';
      while (i < end && seg[i] !== CMD_END) {
        s += String.fromCharCode(seg[i] & 0x7F);
        i++;
      }
      i++;                                            // consume the "'"
      if (i < end && seg[i] === 0x00 && ((i - off) & 1)) i++;
      if (!/^[\x20-\x7E]+$/.test(s)) return null;     // not a real buffer
      cmds.push(s);
    }
    return cmds;
  }

  // Read the initial-command buffer out of a raw SMD image.
  // Returns {commands, versionLetter, segNum, madr, byteOffset, segTablePage}
  function readInitialCommands(imageBytes) {
    if (typeof NdfsLib === 'undefined' || !NdfsLib.NdfsFileSystem) {
      throw new Error('NDFS library not loaded');
    }
    var fs = new NdfsLib.NdfsFileSystem(imageBytes, true);

    // SEGFIL0 holds the SINTRAN memory image (segment swap area)
    var segfilPath = null;
    fs.getObjectEntries().forEach(function(oe) {
      if (!segfilPath && oe && oe.objectName && /^SEGFIL0?$/i.test(oe.objectName)) {
        segfilPath = oe.userName + '/' + oe.objectName + ':' + (oe.type || '');
      }
    });
    if (!segfilPath) throw new Error('No SEGFIL0 file on this pack - not a SINTRAN system disk');

    var seg = new Uint8Array(fs.readFile(segfilPath));
    var stPage = findSegmentTablePage(seg);
    if (stPage < 0) throw new Error('Could not locate the segment table in ' + segfilPath);

    // Prefer the detected running version; otherwise try each until one parses.
    var order = [];
    if (typeof sintranState !== 'undefined' && sintranState.versionLetter) {
      order.push(sintranState.versionLetter.toUpperCase());
    }
    ['L', 'M', 'K'].forEach(function(v) { if (order.indexOf(v) === -1) order.push(v); });

    // Several segments' virtual ranges contain INIBU (save/image copies share
    // the same LOGAD low bits). Only the running command segment holds a live
    // buffer, so try each in segment order and take the first that yields a
    // sane length cell AND parses to at least one command.
    for (var k = 0; k < order.length; k++) {
      var letter = order[k];
      var inibu = INIBU[letter];
      if (!inibu) continue;

      for (var n = 1; n < 128; n++) {
        var e = segEntry(seg, stPage, n);
        if (!e.madr || !e.segle) continue;
        var base = (e.logad & 0x3F) * 1024;
        if (inibu < base || inibu >= base + e.segle * 1024) continue;

        var off = inibu - base;
        var byteOffset = e.madr * PAGE_BYTES + off * 2;
        if (byteOffset + CAP_BYTES > seg.length) continue;

        // SINTRAN reads exactly `textLen` bytes; use it as the bound.
        var textLen = word(seg, byteOffset + LEN_CELL_BYTES);
        if (textLen === 0 || textLen > LEN_CELL_BYTES) continue;   // implausible

        var cmds = parseBuffer(seg, byteOffset, textLen);
        if (cmds && cmds.length) {
          return {
            commands: cmds, versionLetter: letter, segNum: n,
            madr: e.madr, byteOffset: byteOffset, segTablePage: stPage,
            textLen: textLen, lengthCellOffset: byteOffset + LEN_CELL_BYTES,
            segfil: segfilPath
          };
        }
      }
    }
    throw new Error('Initial-command buffer not found (unrecognised SINTRAN version?)');
  }

  // Encode a command list back into buffer bytes.
  // Returns {bytes, textLen}. `textLen` EXCLUDES the terminator and is what
  // must be stored in the length cell - SINTRAN ignores anything past it.
  // Enforces the same limit the NEXIN handler does: textLen <= 254.
  function encodeBuffer(cmds, maxTextBytes) {
    var limit = maxTextBytes || MAX_TEXT_BYTES;
    var out = [];
    for (var i = 0; i < cmds.length; i++) {
      var c = String(cmds[i]).toUpperCase();
      if (!/^[\x20-\x7E]+$/.test(c) || c.indexOf("'") !== -1) {
        throw new Error('invalid command: ' + cmds[i]);
      }
      for (var j = 0; j < c.length; j++) out.push(c.charCodeAt(j) & 0x7F);
      out.push(CMD_END);
      if (out.length & 1) out.push(0x00);        // word-align
    }
    var textLen = out.length;                    // <- the length cell value
    if (textLen > limit) {
      throw new Error('buffer overflow: text is ' + textLen + ' bytes, max ' + limit);
    }
    out.push(CMD_END);                           // buffer terminator
    if (out.length & 1) out.push(0x00);
    return { bytes: Uint8Array.from(out), textLen: textLen };
  }

  // ---------------------------------------------------------------
  // Rendering
  // ---------------------------------------------------------------
  var lastResult = null;

  function render(r, source) {
    var body = byId('initial-commands-body');
    if (!body) return;

    var html = '<div style="padding:10px 12px;font-size:12px;color:rgba(180,200,210,0.75);">' +
      'From <strong>' + esc(source.name) + '</strong> (' + esc(source.mode) + '), ' +
      'SINTRAN <strong>' + esc(r.versionLetter) + '</strong> &mdash; ' +
      'buffer at INIBU in segment ' + r.segNum.toString(8) + '&#8324; ' +
      '(SEGFIL0 byte 0x' + r.byteOffset.toString(16).toUpperCase() + ').</div>';

    html += '<div style="padding:0 12px 4px;font-size:12px;color:rgba(180,200,210,0.6);">' +
      'These run first at every restart, exactly as <code>@LIST-INITIAL-COMMANDS</code> prints them:</div>';

    html += '<div class="ndfs-text-view" style="margin:4px 12px 10px;max-height:none;">';
    html += r.commands.map(function(c, i) {
      return String(i + 1).padStart(2, ' ') + '  ' + esc(c);
    }).join('\n');
    html += '</div>';

    html += '<div style="padding:4px 12px 12px;font-size:11px;color:rgba(180,200,210,0.5);">' +
      'The buffer has no file form &mdash; it lives at kernel symbol INIBU inside the command ' +
      'segment, whose disk image is in (SYSTEM)SEGFIL0:DATA. It typically enters the directories, ' +
      'then chains into the HENT-MODE / LOAD-MODE files (browse those with the NDFS Viewer).</div>';

    body.innerHTML = html;
  }

  function renderError(msg) {
    var body = byId('initial-commands-body');
    if (body) body.innerHTML = '<div style="padding:16px;color:#ff9b9b;font-size:13px;">' + esc(msg) + '</div>';
  }

  function refresh() {
    var body = byId('initial-commands-body');
    if (body) body.innerHTML = '<div style="padding:16px;color:rgba(180,200,210,0.6);">Reading disk image…</div>';

    if (!window.smdImageSource) { renderError('smd-image-source.js not loaded'); return; }
    window.smdImageSource.getMountedImage(0).then(function(src) {
      var r = readInitialCommands(src.bytes);
      lastResult = { r: r, source: src };
      render(r, src);
    }).catch(function(err) {
      renderError(err && err.message ? err.message : String(err));
    });
  }

  function copyAsMarkdown() {
    if (!lastResult) return;
    var r = lastResult.r;
    var md = '# SINTRAN initial commands (' + lastResult.source.name + ', version ' + r.versionLetter + ')\n\n';
    md += '```\n' + r.commands.join('\n') + '\n```\n';
    navigator.clipboard.writeText(md).then(function() {
      var btn = byId('initial-commands-copy');
      if (btn) { btn.classList.add('copied'); setTimeout(function() { btn.classList.remove('copied'); }, 1200); }
    });
  }

  function showWindow() {
    var win = byId('initial-commands-window');
    if (win) { win.style.display = 'flex'; refresh(); }
  }
  function hideWindow() {
    var win = byId('initial-commands-window');
    if (win) win.style.display = 'none';
  }

  function initWindow() {
    var closeBtn = byId('initial-commands-close');
    if (closeBtn) closeBtn.addEventListener('click', hideWindow);
    var refreshBtn = byId('initial-commands-refresh');
    if (refreshBtn) refreshBtn.addEventListener('click', refresh);
    var copyBtn = byId('initial-commands-copy');
    if (copyBtn) copyBtn.addEventListener('click', copyAsMarkdown);

    var menuBtn = byId('menu-initial-commands');
    if (menuBtn) {
      menuBtn.addEventListener('click', function() {
        var win = byId('initial-commands-window');
        if (win && (win.style.display === 'none' || win.style.display === '')) {
          showWindow();
          if (window.windowManager) windowManager.focus('initial-commands-window');
        } else { hideWindow(); }
        document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
      });
    }

    // "Browse Disk (NDFS)" - works in both persistent and non-persistent mode
    var ndfsBtn = byId('menu-browse-disk');
    if (ndfsBtn) {
      ndfsBtn.addEventListener('click', function() {
        document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
        if (typeof openNdfsViewer !== 'function') { alert('NDFS viewer not loaded'); return; }
        if (!window.smdImageSource) { alert('smd-image-source.js not loaded'); return; }
        window.smdImageSource.getMountedImage(0).then(function(src) {
          openNdfsViewer(src.bytes, src.name);
        }).catch(function(err) {
          alert('NDFS: ' + (err && err.message ? err.message : err));
        });
      });
    }

    var win = byId('initial-commands-window');
    var header = byId('initial-commands-header');
    var resize = byId('initial-commands-resize');
    if (typeof makeDraggable === 'function' && win && header) makeDraggable(win, header, 'initial-commands-pos');
    if (typeof makeResizable === 'function' && win && resize) makeResizable(win, resize, 'initial-commands-size', 420, 260);
    if (window.windowManager) windowManager.register('initial-commands-window', 'Initial Commands');
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initWindow);
  } else {
    initWindow();
  }

  window.initialCommandsShowWindow = showWindow;
  window.initialCommandsHideWindow = hideWindow;
  window.sintranInitialCommands = {
    readInitialCommands: readInitialCommands,
    findSegmentTablePage: findSegmentTablePage,
    parseBuffer: parseBuffer,
    encodeBuffer: encodeBuffer,
    INIBU: INIBU,
    CAP_BYTES: CAP_BYTES,
    LEN_CELL_BYTES: LEN_CELL_BYTES,
    MAX_TEXT_BYTES: MAX_TEXT_BYTES,
    MAX_TEXT_BYTES_BY_VERSION: MAX_TEXT_BYTES_BY_VERSION
  };
})();
