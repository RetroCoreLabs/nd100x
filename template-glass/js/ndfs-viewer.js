//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen - RetroCore Labs
//
// ndfs-viewer.js - Read-only NDFS (ND filesystem) browser window.
//
// Public entry point:
//   openNdfsViewer(bytes, title)
//     bytes : Uint8Array of a raw (uncompressed) NDFS disk/floppy image
//     title : display name for the window
//
// Ported from the norskdata-software-archive NDFS viewer and re-skinned for
// the Glass UI. Uses the global NdfsLib (built by `make ndfs-build`) and the
// shared window helpers (openWindow/closeWindow/makeDraggable/makeResizable/
// windowManager) from toolbar.js. Read-only: always constructs the filesystem
// with readOnly=true.

(function() {
  'use strict';

  var state = {
    fs: null,
    files: null,
    users: null,
    selectedUser: null,
    selectedFileIdx: null,
    volumeName: ''
  };

  // ---- small local helpers (self-contained) ----
  function esc(s) {
    return String(s == null ? '' : s)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
  }
  function fmtBytes(n) {
    if (n == null) return '-';
    if (n < 1024) return n + ' B';
    if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
    return (n / (1024 * 1024)).toFixed(1) + ' MB';
  }
  function byId(id) { return document.getElementById(id); }

  function setBody(html) {
    var body = byId('ndfs-viewer-body');
    if (body) body.innerHTML = html;
  }

  // ---- public entry ----
  window.openNdfsViewer = function(bytes, title) {
    if (typeof NdfsLib === 'undefined' || !NdfsLib.NdfsFileSystem) {
      alert('NDFS library not loaded.');
      return;
    }
    if (!bytes || !bytes.length) {
      alert('No image data to inspect.');
      return;
    }

    var titleEl = byId('ndfs-viewer-title');
    if (titleEl) titleEl.textContent = 'NDFS Viewer: ' + (title || '');

    // Reset state
    state.fs = null; state.files = null; state.users = null;
    state.selectedUser = null; state.selectedFileIdx = null;
    state.volumeName = title || '';

    if (typeof openWindow === 'function') openWindow('ndfs-viewer-window');
    if (window.windowManager) windowManager.focus('ndfs-viewer-window');

    setBody('<div class="ndfs-loading"><div class="ndfs-spinner"></div><div>Parsing NDFS...</div></div>');

    // Parse on next frame so the "Parsing..." paint lands first.
    requestAnimationFrame(function() {
      try {
        var fs = new NdfsLib.NdfsFileSystem(bytes, true); // readOnly
        state.fs = fs;
        try { state.volumeName = fs.getDirectoryName() || title || ''; } catch (e) {}
        if (titleEl) titleEl.textContent = 'NDFS Viewer: ' + state.volumeName;

        var users = fs.getUsers();
        state.users = users.map(function(u) {
          return { name: u.userName, pagesUsed: u.pagesUsed, pagesReserved: u.pagesReserved };
        });

        var objects = fs.getObjectEntries();
        var allFiles = [];
        objects.forEach(function(oe) {
          if (!oe || !oe.objectName) return;
          var created = oe.dateCreated ? NdfsLib.ndTimeToDate(oe.dateCreated) : null;
          allFiles.push({
            userName: oe.userName,
            fullName: oe.objectName + ':' + (oe.type || ''),
            type: oe.type || '',
            pages: oe.pagesInFile,
            bytes: oe.bytesInFile,
            dateCreated: created ? created.toISOString() : null,
            path: oe.userName + '/' + oe.objectName + ':' + (oe.type || '')
          });
        });
        allFiles.sort(function(a, b) { return a.fullName.localeCompare(b.fullName); });
        state.files = allFiles;

        renderViewer();
      } catch (err) {
        setBody('<div class="ndfs-loading" style="color:#ff9b9b">Not a valid NDFS image: ' +
          esc(String(err && err.message ? err.message : err)) + '</div>');
      }
    });
  };

  function renderViewer() {
    var files = state.files, users = state.users;
    if (!files || !users) return;

    var header =
      '<div class="ndfs-viewer-header">' +
        '<span>Volume: <strong>' + esc(state.volumeName) + '</strong></span>' +
        '<span>' + files.length + ' files</span>' +
        '<span>' + users.length + ' user' + (users.length !== 1 ? 's' : '') + '</span>' +
      '</div>';

    var userHtml = '<div class="ndfs-user-panel"><h4>Users</h4>';
    userHtml += '<div class="ndfs-user-item' + (state.selectedUser === null ? ' ndfs-user-active' : '') +
      '" onclick="ndfsSelectUser(null)">All <span class="ndfs-user-count">(' + files.length + ')</span></div>';
    users.forEach(function(u) {
      var count = 0;
      files.forEach(function(f) { if (f.userName === u.name) count++; });
      var active = state.selectedUser === u.name ? ' ndfs-user-active' : '';
      userHtml += '<div class="ndfs-user-item' + active + '" onclick="ndfsSelectUser(\'' +
        esc(u.name).replace(/'/g, "\\'") + '\')">' +
        esc(u.name) + ' <span class="ndfs-user-count">(' + count + ')</span>' +
        '<div class="ndfs-user-pages">' + u.pagesUsed + '/' + u.pagesReserved + ' pg</div></div>';
    });
    userHtml += '</div>';

    var fileHtml = '<div class="ndfs-file-panel">' + renderFileTable() + '</div>';

    var actions = '<div class="ndfs-file-actions">' +
      '<span class="ndfs-selected-label" id="ndfs-sel-label">Select a file</span>' +
      '<button class="ndfs-btn" id="ndfs-btn-extract" disabled onclick="ndfsExtractFile(false)">Extract</button>' +
      '<button class="ndfs-btn" id="ndfs-btn-extract-strip" disabled onclick="ndfsExtractFile(true)">Extract (strip parity)</button>' +
      '<button class="ndfs-btn" id="ndfs-btn-hex" disabled onclick="ndfsShowHex()">View as hex</button>' +
      '<button class="ndfs-btn" id="ndfs-btn-text" disabled onclick="ndfsShowText()">View as text</button>' +
      '</div>';

    setBody(header +
      '<div class="ndfs-viewer-layout">' + userHtml + fileHtml + '</div>' +
      actions + '<div id="ndfs-file-content" class="ndfs-file-content"></div>');
  }

  function filteredFiles() {
    var files = state.files || [];
    if (state.selectedUser) {
      return files.filter(function(f) { return f.userName === state.selectedUser; });
    }
    return files;
  }

  function renderFileTable() {
    var filtered = filteredFiles();
    if (filtered.length === 0) {
      return '<div class="ndfs-loading" style="padding:1.5rem">No files</div>';
    }
    var html = '<table class="ndfs-file-table"><thead><tr>' +
      '<th>Name</th><th class="num">Pages</th><th class="num">Size</th><th>Created</th>' +
      '</tr></thead><tbody>';
    filtered.forEach(function(f, i) {
      var sel = state.selectedFileIdx === i ? ' ndfs-file-selected' : '';
      var created = f.dateCreated ? String(f.dateCreated).substring(0, 10) : '-';
      var name = '<span class="ndfs-owner">(' + esc(f.userName) + ')</span>' + esc(f.fullName);
      html += '<tr class="' + sel + '" onclick="ndfsSelectFile(' + i + ')">' +
        '<td><code>' + name + '</code></td>' +
        '<td class="num">' + (f.pages || '-') + '</td>' +
        '<td class="num">' + fmtBytes(f.bytes) + '</td>' +
        '<td class="muted">' + created + '</td></tr>';
    });
    return html + '</tbody></table>';
  }

  window.ndfsSelectUser = function(userName) {
    state.selectedUser = userName;
    state.selectedFileIdx = null;
    renderViewer();
  };

  window.ndfsSelectFile = function(index) {
    state.selectedFileIdx = index;
    var file = filteredFiles()[index];
    if (!file) return;
    var rows = document.querySelectorAll('.ndfs-file-table tbody tr');
    for (var i = 0; i < rows.length; i++) {
      rows[i].className = (i === index) ? 'ndfs-file-selected' : '';
    }
    var label = byId('ndfs-sel-label');
    if (label) label.textContent = file.fullName + ' (' + fmtBytes(file.bytes) + ')';
    ['ndfs-btn-extract', 'ndfs-btn-extract-strip', 'ndfs-btn-hex', 'ndfs-btn-text'].forEach(function(id) {
      var b = byId(id); if (b) b.disabled = false;
    });
    var c = byId('ndfs-file-content'); if (c) c.innerHTML = '';
  };

  function selectedFile() {
    if (state.selectedFileIdx == null) return null;
    return filteredFiles()[state.selectedFileIdx] || null;
  }

  window.ndfsExtractFile = function(stripParity) {
    var file = selectedFile();
    if (!file || !state.fs) return;
    try {
      var data = state.fs.readFile(file.path);
      if (!data) { alert('Could not read file'); return; }
      var bytes = new Uint8Array(data);
      if (stripParity) {
        var s = new Uint8Array(bytes.length);
        for (var i = 0; i < bytes.length; i++) s[i] = bytes[i] & 0x7F;
        bytes = s;
      }
      var url = URL.createObjectURL(new Blob([bytes], { type: 'application/octet-stream' }));
      var a = document.createElement('a');
      a.href = url;
      a.download = file.fullName.replace(/:/g, '.');
      document.body.appendChild(a); a.click(); document.body.removeChild(a);
      URL.revokeObjectURL(url);
    } catch (err) { alert('Extract error: ' + (err.message || err)); }
  };

  window.ndfsShowHex = function() {
    var file = selectedFile(); if (!file || !state.fs) return;
    var el = byId('ndfs-file-content'); if (!el) return;
    try {
      var bytes = new Uint8Array(state.fs.readFile(file.path) || []);
      var max = Math.min(bytes.length, 4096), lines = [];
      for (var off = 0; off < max; off += 16) {
        var hex = '', ascii = '';
        for (var j = 0; j < 16; j++) {
          if (off + j < max) {
            var b = bytes[off + j];
            hex += (b < 16 ? '0' : '') + b.toString(16).toUpperCase() + ' ';
            ascii += (b >= 32 && b < 127) ? String.fromCharCode(b) : '.';
          } else { hex += '   '; ascii += ' '; }
        }
        var addr = off.toString(16).toUpperCase();
        while (addr.length < 6) addr = '0' + addr;
        lines.push(addr + '  ' + hex + ' ' + ascii);
      }
      var note = max < bytes.length ? '<div class="ndfs-trunc">Showing first ' + max + ' of ' + bytes.length + ' bytes</div>' : '';
      el.innerHTML = '<strong>Hex: ' + esc(file.fullName) + ' (' + fmtBytes(bytes.length) + ')</strong>' +
        '<div class="ndfs-hex-view">' + esc(lines.join('\n')) + '</div>' + note;
    } catch (err) { el.innerHTML = '<div class="ndfs-err">Hex error: ' + esc(String(err.message || err)) + '</div>'; }
  };

  window.ndfsShowText = function() {
    var file = selectedFile(); if (!file || !state.fs) return;
    var el = byId('ndfs-file-content'); if (!el) return;
    try {
      var bytes = new Uint8Array(state.fs.readFile(file.path) || []);
      var s = new Uint8Array(bytes.length);
      for (var i = 0; i < bytes.length; i++) s[i] = bytes[i] & 0x7F;
      var text = new TextDecoder('ascii').decode(s);
      var note = '';
      if (text.length > 32768) { text = text.substring(0, 32768); note = '<div class="ndfs-trunc">Showing first 32768 chars of ' + bytes.length + ' bytes</div>'; }
      el.innerHTML = '<strong>Text: ' + esc(file.fullName) + ' (' + fmtBytes(bytes.length) + ')</strong>' +
        '<div class="ndfs-text-view">' + esc(text) + '</div>' + note;
    } catch (err) { el.innerHTML = '<div class="ndfs-err">Text error: ' + esc(String(err.message || err)) + '</div>'; }
  };

  // ---- window wiring (runs after toolbar.js has defined the helpers) ----
  function initWindow() {
    var closeBtn = byId('ndfs-viewer-close');
    if (closeBtn && typeof closeWindow === 'function') {
      closeBtn.addEventListener('click', function() { closeWindow('ndfs-viewer-window'); });
    }
    var win = byId('ndfs-viewer-window');
    var header = byId('ndfs-viewer-header-bar');
    var resize = byId('ndfs-viewer-resize');
    if (typeof makeDraggable === 'function' && win && header) makeDraggable(win, header, 'ndfs-viewer-pos');
    if (typeof makeResizable === 'function' && win && resize) makeResizable(win, resize, 'ndfs-viewer-size', 520, 380);
    if (window.windowManager) windowManager.register('ndfs-viewer-window', 'NDFS Viewer');
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initWindow);
  } else {
    initWindow();
  }
})();
