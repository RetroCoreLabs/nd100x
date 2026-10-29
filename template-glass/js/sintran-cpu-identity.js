//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen - RetroCore Labs
//

// sintran-cpu-identity.js - CPU number / CPU type of the SINTRAN pack on SMD
// unit 0, read from the disk image, with a form that changes it.
//
// A SINTRAN pack carries its CPU number twice (SYSNO, FCPUN) and its CPU type
// once (HWINF+2) in the resident data page, stored on disk in the segments
// S3IDPIT (warm start) and S3SDPIT (cold start). Two nodes with the same
// number break COSMOS routing, and a pack that was last entered by another
// system is refused at @ENTER-DIRECTORY (error 3203). The reader and writer
// are NdfsLib.readCpuIdentity / writeCpuIdentity from the norskdata-ndfs
// TypeScript library (built into lib/ndfs/ndfs-browser-bundle.js by
// `make ndfs-build`): addresses come from the pack's own symbol lists, segment
// numbers from the RT-loader name records, pages from the segment table.
// Derivation: NDInsight chapter SINTRAN/OS/28-CPU-NUMBER-AND-TYPE-ON-DISK.md.
//
// The window also lists every segment on the pack (NdfsLib.listSegments):
// name, segment file, disk page. That is the DISK view; the "Segment Table"
// window shows the running kernel's table from memory.
//
// Writing: the running emulator owns the mounted image (an in-memory buffer in
// persistent Direct mode, a MEMFS file read through a FILE handle in demo
// mode, an OPFS handle in Worker mode), so a patch behind its back would be
// overwritten or ignored. Apply therefore ejects unit 0, patches the stored
// image, stores it back and remounts it, after a confirm. Do it with the
// machine stopped or at the boot prompt; SINTRAN picks the new number up at
// the next start.

(function() {
  'use strict';

  var UNIT = 0;
  var lastRead = null;   // { ident, segs, source, error }

  function byId(id) { return document.getElementById(id); }
  function esc(s) {
    return String(s == null ? '' : s)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
  }
  function oct(n) { return n < 0 ? '-' : n.toString(8) + 'B'; }
  function dec(n) { return n < 0 ? '-' : String(n); }

  // ---- read ---------------------------------------------------------------

  // Pure: identity + segment list from raw image bytes. Throws only when the
  // bytes are not an NDFS image; a pack without SINTRAN gives error text.
  function readPack(bytes) {
    if (typeof NdfsLib === 'undefined' || !NdfsLib.NdfsFileSystem) throw new Error('NDFS library not loaded');
    var fs = new NdfsLib.NdfsFileSystem(bytes, true);
    var out = { volume: '', ident: null, error: null, segs: null, segError: null };
    try { out.volume = fs.getDirectoryName(); } catch (e) {}
    try {
      out.ident = NdfsLib.readCpuIdentity(fs);
    } catch (e) {
      out.error = e && e.message ? e.message : String(e);
      if (e && e.identity) out.ident = e.identity;   // what was found before the refusal
    }
    try {
      out.segs = NdfsLib.listSegments(fs);
    } catch (e) {
      out.segError = e && e.message ? e.message : String(e);
    }
    return out;
  }

  // Pure: patch raw image bytes; returns { bytes, after }.
  function patchPack(bytes, cpuNumber, cpuType, keepStamp) {
    var fs = new NdfsLib.NdfsFileSystem(bytes, false);
    var after = NdfsLib.writeCpuIdentity(fs, cpuNumber, cpuType, keepStamp);
    return { bytes: fs.toBuffer(), after: after };
  }

  // ---- render -------------------------------------------------------------

  function copyRows(c) {
    var where = c.present
      ? 'segment file ' + c.segmentFile + ', ' + esc(c.filePath || '(no file)') + ', page ' + c.madr + ', ' + c.segle + ' pages'
      : 'not usable (no pages, no disk address or SYSNO outside the segment)';
    return '<tr><th colspan="3">Segment ' + oct(c.segmentNumber) + ' ' + esc(c.name) + ' (' + c.role + ' copy)</th></tr>' +
      '<tr><td class="cpuid-muted" colspan="3">' + where + (c.present && !c.fileFound ? ' - file not found' : '') + '</td></tr>' +
      '<tr><td>CPU number (SYSNO)</td><td class="num">' + dec(c.sysno) + '</td><td class="num">' + oct(c.sysno) + '</td></tr>' +
      '<tr><td>FCPUN copy</td><td class="num">' + dec(c.fcpun) + '</td><td class="num">' + oct(c.fcpun) + '</td></tr>' +
      '<tr><td>CPU type (HWINF+2)</td><td class="num">' + dec(c.cpuType) + '</td><td class="num">' + oct(c.cpuType) + '</td></tr>' +
      '<tr><td>Patch level (REVLE)</td><td class="num">' + dec(c.revle) + '</td><td class="num">' + oct(c.revle) + '</td></tr>';
  }

  function renderIdentity(r, src) {
    var id = r.ident;
    var html = '<div class="ndfs-viewer-header">' +
      '<span>Volume: <strong>' + esc(r.volume) + '</strong></span>' +
      '<span>' + esc(src.name) + ' (' + esc(src.mode) + ')</span>' +
      (id ? '<span>SINTRAN <strong>' + esc(id.version) + '</strong></span>' : '') +
      '</div>';
    if (r.error) html += '<div class="cpuid-error">' + esc(r.error) + '</div>';
    if (!id) return html;

    html += '<table class="cpuid-table"><tbody>';
    html += '<tr><th colspan="3">Symbols (' + esc(id.symbol1Path) + ', ' + esc(id.symbol2Path) + ')</th></tr>';
    html += '<tr><td>SYSNO / HWINF / REVLE / FCPUN</td><td colspan="2" class="num">' +
      oct(id.sym.sysno) + ' / ' + oct(id.sym.hwinf) + ' / ' + oct(id.sym.revle) + ' / ' + oct(id.sym.fcpun) + '</td></tr>';
    html += '<tr><td>33CPN (generation CPU number)</td><td class="num">' + dec(id.sym.cpn33) + '</td><td class="num">' + oct(id.sym.cpn33) + '</td></tr>';
    html += '<tr><td>33CPU (generation CPU type)</td><td class="num">' + dec(id.sym.cpu33) + '</td><td class="num">' + oct(id.sym.cpu33) + '</td></tr>';
    html += '<tr><td>Segment names from</td><td colspan="2">' + esc(id.namesPath || '-') + '</td></tr>';
    html += '<tr><td>Segment table page</td><td colspan="2" class="num">' + dec(id.segmentTablePage) + '</td></tr>';
    id.copies.forEach(function(c) { html += copyRows(c); });
    html += '<tr><th colspan="3">Directory stamp (page 0)</th></tr>';
    html += '<tr><td>Entered by system</td><td colspan="2">' +
      (id.dirExtValid ? (id.dirEntered ? 'yes, system number ' + id.dirSystemNumber : 'no (system number ' + id.dirSystemNumber + ')')
                      : 'extended info block not valid') + '</td></tr>';
    html += '</tbody></table>';

    var differ = id.copies.some(function(c) { return c.sysno >= 0 && c.fcpun >= 0 && c.sysno !== c.fcpun; });
    if (differ) html += '<div class="cpuid-note">NOTE: SYSNO and FCPUN differ. SINTRAN L and later overwrite SYSNO from the PROM at every start; the banner shows SYSNO.</div>';
    return html;
  }

  function renderForm(r) {
    var id = r.ident;
    var writable = !!(id && !r.error && id.copies.every(function(c) { return c.present && c.fileFound && c.sysnoOffset >= 0 && c.fcpunOffset >= 0; }));
    var curNum = id && id.copies[0].sysno >= 0 ? id.copies[0].sysno : '';
    var curType = id && id.copies[0].cpuType >= 0 ? id.copies[0].cpuType : '';
    var html = '<div class="smd-copy-dialog cpuid-form">' +
      '<div class="smd-copy-dialog-title">Change CPU number / type on this pack</div>' +
      '<div class="cpuid-form-row">' +
      '<label class="smd-copy-label">CPU number (decimal)<input type="text" id="cpuid-number" class="smd-copy-input" value="' + esc(curNum) + '" /></label>' +
      '<label class="smd-copy-label">CPU type (decimal, blank = keep)<input type="text" id="cpuid-type" class="smd-copy-input" value="' + esc(curType) + '" /></label>' +
      '</div>' +
      '<label class="cpuid-check"><input type="checkbox" id="cpuid-keep-stamp" /> keep the "entered by system N" stamp (default: clear it, so the pack is not refused at ENTER-DIRECTORY)</label>' +
      '<div class="cpuid-muted">Unit ' + UNIT + ' is ejected, the stored image is patched (both DPIT copies, 33CPN/33CPU, the stamp), read back and remounted. Do this with the machine stopped or at the boot prompt; the new number shows on the next SINTRAN banner.</div>' +
      '<div class="smd-copy-actions"><button class="smd-action-btn smd-copy-confirm-btn" id="cpuid-apply"' + (writable ? '' : ' disabled title="This pack has no reachable copy to write"') + '>Apply</button></div>' +
      '<div id="cpuid-apply-status" class="cpuid-status"></div>' +
      '</div>';
    return html;
  }

  function renderSegments(r) {
    if (r.segError) return '<div class="cpuid-error">Segments: ' + esc(r.segError) + '</div>';
    var l = r.segs;
    if (!l) return '';
    var html = '<div class="cpuid-section-title">Segments on disk (table page ' + l.segmentTablePage + ' of ' + esc(l.segfilPath) + ', names from ' + esc(l.namesPath || '-') + ')</div>';
    html += '<table class="cpuid-table cpuid-seg-table"><thead><tr><th>sf</th><th>base page</th><th>blocks/page</th><th>file</th><th>source</th></tr></thead><tbody>';
    l.segfiles.forEach(function(sf, n) {
      html += '<tr><td class="num">' + n + '</td>';
      if (!sf.defined) { html += '<td colspan="4" class="cpuid-muted">not defined</td></tr>'; return; }
      html += '<td class="num">' + (sf.onThisDisk ? sf.basePage : 'other unit (MASSNO ' + oct(sf.massno) + ')') + '</td>' +
        '<td class="num">' + sf.blocksPerPage + '</td><td>' + esc(sf.filePath) + '</td><td class="cpuid-muted">' + esc(sf.source) + '</td></tr>';
    });
    html += '</tbody></table>';
    html += '<table class="cpuid-table cpuid-seg-table"><thead><tr><th>seg</th><th>name</th><th>base</th><th>pages</th><th>sf</th><th>MADR</th><th>disk page</th><th>flags</th></tr></thead><tbody>';
    l.segments.forEach(function(s) {
      var flags = (s.ok5 ? '5OK' : '') + (s.memoryOnly ? ' memory' : '');
      html += '<tr><td class="num">' + s.number.toString(8) + '</td><td>' + esc(s.name || '-') + '</td>' +
        '<td class="num">' + s.logad.toString(8).padStart(6, '0') + '</td><td class="num">' + s.segle + '</td>' +
        '<td class="num">' + s.segfile + '</td><td class="num">' + s.madr + '</td>' +
        '<td class="num">' + (s.memoryOnly ? 'memory' : (s.diskPage >= 0 ? s.diskPage : '?')) + '</td><td>' + flags + '</td></tr>';
    });
    html += '</tbody></table>';
    return html;
  }

  function render(r, src) {
    var body = byId('cpu-identity-body');
    if (!body) return;
    body.innerHTML = renderIdentity(r, src) + renderForm(r) + renderSegments(r);
    var apply = byId('cpuid-apply');
    if (apply) apply.addEventListener('click', applyFromForm);
  }

  function renderError(msg) {
    var body = byId('cpu-identity-body');
    if (body) body.innerHTML = '<div class="cpuid-error">' + esc(msg) + '</div>';
  }

  function refresh() {
    var body = byId('cpu-identity-body');
    if (body) body.innerHTML = '<div class="cpuid-muted" style="padding:16px">Reading disk image…</div>';
    if (!window.smdImageSource) { renderError('smd-image-source.js not loaded'); return; }
    window.smdImageSource.getMountedImage(UNIT).then(function(src) {
      var r = readPack(src.bytes);
      lastRead = { r: r, source: src };
      render(r, src);
    }).catch(function(err) {
      renderError(err && err.message ? err.message : String(err));
    });
  }

  // ---- write --------------------------------------------------------------

  function setStatus(msg, isError) {
    var el = byId('cpuid-apply-status');
    if (el) { el.textContent = msg; el.className = 'cpuid-status' + (isError ? ' cpuid-status-error' : ''); }
  }

  function parseField(id, allowBlank) {
    var el = byId(id);
    var s = el ? String(el.value).trim() : '';
    if (s === '') { if (allowBlank) return null; throw new Error('CPU number is required'); }
    if (!/^[0-9]+$/.test(s)) throw new Error('"' + s + '" is not a decimal number');
    var v = parseInt(s, 10);
    if (v < 0 || v > 65535) throw new Error('"' + s + '" is outside 0..65535');
    return v;
  }

  // Where the image on UNIT really lives, and how to replace it.
  //   persistent (OPFS):  eject (saves a dirty Direct-mode buffer), retrieve by
  //                       uuid, patch, storeImage, assign again (remount).
  //   demo (MEMFS):       unmount (closes the FILE handle), read /SMDn.IMG,
  //                       patch, write it back, RemountSMD re-opens it.
  function replaceImage(patchFn) {
    var uuid = (typeof smdStorage !== 'undefined' && smdStorage.getUnitAssignment) ? smdStorage.getUnitAssignment(UNIT) : null;
    if (uuid) {
      var meta = smdStorage.getMetadata ? smdStorage.getMetadata(uuid) : null;
      if (typeof smdEjectUnit === 'function') smdEjectUnit(UNIT);
      return Promise.resolve(smdStorage.retrieveImage(uuid)).then(function(data) {
        if (!data || !data.byteLength) throw new Error('stored image is empty');
        var res = patchFn(new Uint8Array(data));
        return smdStorage.storeImage(uuid, res.bytes, {
          name: meta ? meta.name : uuid,
          description: meta ? meta.description : '',
          sourceName: meta ? meta.sourceName : ''
        }).then(function() {
          if (typeof smdAssignToUnit === 'function') smdAssignToUnit(uuid, UNIT);
          return res.after;
        });
      });
    }
    if (typeof emu === 'undefined' || !emu.fsReadFile) return Promise.reject(new Error('emulator not loaded'));
    var path = '/SMD' + UNIT + '.IMG';
    emu.unmountSMD(UNIT);
    var bytes;
    try { bytes = emu.fsReadFile(path); } catch (e) { emu.remountSMD(UNIT); return Promise.reject(new Error('cannot read ' + path)); }
    var res;
    try {
      res = patchFn(new Uint8Array(bytes));
      emu.fsWriteFile(path, res.bytes);
    } finally {
      emu.remountSMD(UNIT);
    }
    return Promise.resolve(res.after);
  }

  function applyFromForm() {
    var num, type, keep;
    try {
      num = parseField('cpuid-number', false);
      type = parseField('cpuid-type', true);
    } catch (e) { setStatus(e.message, true); return; }
    keep = !!(byId('cpuid-keep-stamp') && byId('cpuid-keep-stamp').checked);

    var what = 'CPU number ' + num + (type === null ? ' (type kept)' : ', CPU type ' + type) + (keep ? ', stamp kept' : ', directory stamp cleared');
    if (!confirm('Change the pack on SMD unit ' + UNIT + ' to ' + what + '?\n\n' +
                 'Unit ' + UNIT + ' is ejected, the stored image is patched and remounted. ' +
                 'Do this with the machine stopped or at the boot prompt.')) return;

    var btn = byId('cpuid-apply');
    if (btn) btn.disabled = true;
    setStatus('Writing…', false);
    replaceImage(function(bytes) { return patchPack(bytes, num, type, keep); }).then(function(after) {
      setStatus('Done: ' + what + ' (read back from the image).', false);
      refresh();
    }).catch(function(err) {
      setStatus('Failed: ' + (err && err.message ? err.message : String(err)) + ' - nothing was changed unless the message says otherwise.', true);
      if (btn) btn.disabled = false;
    });
  }

  // ---- window wiring --------------------------------------------------------

  function showWindow() {
    var win = byId('cpu-identity-window');
    if (win) { win.style.display = 'flex'; refresh(); }
  }
  function hideWindow() {
    var win = byId('cpu-identity-window');
    if (win) win.style.display = 'none';
  }

  function initWindow() {
    var closeBtn = byId('cpu-identity-close');
    if (closeBtn) closeBtn.addEventListener('click', hideWindow);
    var refreshBtn = byId('cpu-identity-refresh');
    if (refreshBtn) refreshBtn.addEventListener('click', refresh);

    var menuBtn = byId('menu-cpu-identity');
    if (menuBtn) {
      menuBtn.addEventListener('click', function() {
        var win = byId('cpu-identity-window');
        if (win && (win.style.display === 'none' || win.style.display === '')) {
          showWindow();
          if (window.windowManager) windowManager.focus('cpu-identity-window');
        } else { hideWindow(); }
        document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
      });
    }

    var win = byId('cpu-identity-window');
    var header = byId('cpu-identity-header');
    var resize = byId('cpu-identity-resize');
    if (typeof makeDraggable === 'function' && win && header) makeDraggable(win, header, 'cpu-identity-pos');
    if (typeof makeResizable === 'function' && win && resize) makeResizable(win, resize, 'cpu-identity-size', 520, 320);
    if (window.windowManager) windowManager.register('cpu-identity-window', 'CPU Identity (disk)');
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initWindow);
  } else {
    initWindow();
  }

  window.cpuIdentityShowWindow = showWindow;
  window.cpuIdentityHideWindow = hideWindow;
  window.sintranCpuIdentity = {
    readPack: readPack,
    patchPack: patchPack,
    renderIdentity: renderIdentity,
    renderSegments: renderSegments
  };
})();
