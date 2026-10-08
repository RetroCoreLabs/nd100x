//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs - https://github.com/RetroCoreLabs
// Emulating yesterday's technology with today's code
//

// welcome.js - the Welcome window (first visit, and Help > Welcome) and the
// Help > Machines pages. Both are filled from data/machines.json so the four
// machines are described in ONE place: a short card in Welcome, a full page
// under Help. "Show this every time" is the localStorage key below; every
// setting of the page lives in localStorage (there is no cookie).

(function () {
  'use strict';

  var SHOW_KEY = 'nd100x-welcome-show';   // 'false' = do not open on load; anything else = open
  // Fetched with the page's ?v= build token (read off this script's own tag)
  // so a new build never shows a cached older machines.json.
  var DATA_URL = 'data/machines.json' + (function () {
    var s = document.querySelector('script[src*="welcome.js"]');
    var m = s && s.src.match(/\?v=(\d+)/);
    return m ? '?v=' + m[1] : '?t=' + Date.now();
  })();
  var _data = null;        // parsed machines.json
  var _loading = null;     // promise while fetching

  function el(id) { return document.getElementById(id); }
  function esc(s) {
    return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  function showOnLoad() {
    try { return localStorage.getItem(SHOW_KEY) !== 'false'; } catch (e) { return true; }
  }
  function setShowOnLoad(on) {
    try { localStorage.setItem(SHOW_KEY, on ? 'true' : 'false'); } catch (e) {}
  }

  function loadData() {
    if (_data) return Promise.resolve(_data);
    if (_loading) return _loading;
    _loading = fetch(DATA_URL).then(function (r) {
      if (!r.ok) throw new Error(DATA_URL + ': HTTP ' + r.status);
      return r.json();
    }).then(function (d) {
      _data = d;
      return d;
    }).catch(function (e) {
      console.error('[Welcome] ' + (e && e.message ? e.message : e));
      _data = { machines: [] };
      return _data;
    });
    return _loading;
  }

  // Tags in the JSON: [sN] names a source URL in the machine's 'sources' list
  // and renders as a small superscript link; [eN] (a repo file) and [mN] (a
  // measurement) are bookkeeping for the writer and are not shown; the
  // [unverified: ...] review mark IS shown, until Ronny confirms or deletes
  // the sentence. Cards show no tags at all.
  function stripTags(s) {
    return String(s || '').replace(/\s*\[(s|e|m)\d+\]/g, '').replace(/\s*\[unverified[^\]]*\]/g, '');
  }
  function richText(m, s) {
    var out = esc(String(s || '').replace(/\s*\[(e|m)\d+\]/g, ''));
    out = out.replace(/\s*\[(s\d+)\]/g, function (_, id) {
      var src = null;
      for (var i = 0; i < (m.sources || []).length; i++) if (m.sources[i].id === id) src = m.sources[i];
      if (!src) return '';
      return '<sup><a class="machine-page-src" href="' + esc(src.url) + '" target="_blank" rel="noopener noreferrer" title="' + esc(src.title) + '">' + esc(id.slice(1)) + '</a></sup>';
    });
    return out.replace(/\[unverified([^\]]*)\]/g, function (_, rest) {
      return ' <span class="machine-page-unverified" title="Not yet confirmed - review before relying on it">[unverified' + esc(rest) + ']</span>';
    });
  }

  // ---- Welcome window --------------------------------------------------------

  function renderCards(d) {
    var box = el('welcome-cards');
    if (!box) return;
    var running = machineRunning();
    var h = '';
    for (var i = 0; i < d.machines.length; i++) {
      var m = d.machines[i];
      h += '<div class="welcome-card">' +
           '<div class="welcome-card-name">' + esc(m.name) + '<span class="welcome-card-tag">' + esc(m.tag) + '</span></div>' +
           '<div class="welcome-card-text">' + esc(stripTags(m.card)) + '</div>' +
           '<div class="welcome-card-after"><b>After Power:</b> ' + esc(stripTags(m.afterPower)) + '</div>' +
           '<div class="welcome-card-actions">' +
           '<button class="smd-action-btn welcome-btn-start welcome-card-start" data-machine="' + esc(m.name) + '"' +
           (running ? ' disabled title="A machine is running - power it off first"' : ' title="Select this machine and press Power"') +
           '>Select and start</button>' +
           '<button class="smd-action-btn welcome-btn-more welcome-card-more" data-machine="' + esc(m.name) + '">Read more</button>' +
           '</div></div>';
    }
    box.innerHTML = h;
  }

  // A machine is running when the Power button is lit (an ND-100 after
  // Init) or the standalone ND-500 has been created.
  function machineRunning() {
    var btn = el('toolbar-power');
    if (btn && btn.classList.contains('initialized')) return true;
    return !!(window.ndixMachine && ndixMachine.isActive && ndixMachine.isActive());
  }

  // "Select and start": pick the machine in the toolbar's selector, then
  // press its Power button - the same two steps the window tells the user.
  function selectAndStart(name) {
    if (machineRunning()) return;
    selectMachine(name);
    closeWelcome();
    var btn = el('toolbar-power');
    if (btn) setTimeout(function () { btn.click(); }, 50);
  }

  function openWelcome() {
    var ov = el('welcome-overlay');
    if (!ov) return;
    var cb = el('welcome-show-always');
    if (cb) cb.checked = showOnLoad();
    loadData().then(renderCards);
    ov.style.display = 'flex';
  }

  function closeWelcome() {
    var ov = el('welcome-overlay');
    if (ov) ov.style.display = 'none';
  }

  // ---- Machine pages (Help > Machines) ------------------------------------------

  function machineByName(d, name) {
    for (var i = 0; i < d.machines.length; i++) if (d.machines[i].name === name) return d.machines[i];
    return null;
  }

  function renderPage(m, fromWelcome) {
    var h = '<div class="machine-page-tag">' + esc(m.tag) + '</div>';
    h += '<p class="machine-page-lead">' + esc(stripTags(m.card)) + '</p>';
    for (var i = 0; i < (m.sections || []).length; i++) {
      var sec = m.sections[i];
      h += '<h3>' + esc(sec.title) + '</h3>';
      for (var j = 0; j < (sec.paragraphs || []).length; j++) h += '<p>' + richText(m, sec.paragraphs[j]) + '</p>';
    }
    if (m.sources && m.sources.length) {
      h += '<h3>Sources</h3><ol class="machine-page-sources">';
      for (var k = 0; k < m.sources.length; k++) {
        h += '<li value="' + esc(String(m.sources[k].id).replace(/^s/, '')) + '"><a href="' + esc(m.sources[k].url) + '" target="_blank" rel="noopener noreferrer">' + esc(m.sources[k].title) + '</a></li>';
      }
      h += '</ol>';
    }
    h += '<div class="machine-page-actions">' +
         '<button class="smd-action-btn machine-page-select" data-machine="' + esc(m.name) + '">Select this machine</button>' +
         (fromWelcome ? ' <button class="smd-action-btn machine-page-back">Back to Welcome</button>' : '') +
         '</div>';
    return h;
  }

  // The Welcome overlay sits above every glass window (z-index 10000 against
  // the window manager's 7000-8899), so a page opened from a card would be
  // hidden behind it: "Read more" closes the Welcome first, and the page
  // then carries a "Back to Welcome" button.
  function openMachinePage(name, fromWelcome) {
    loadData().then(function (d) {
      var m = machineByName(d, name);
      var win = el('machine-page-window');
      if (!m || !win) return;
      if (fromWelcome) closeWelcome();
      el('machine-page-title').textContent = m.name + ' - ' + m.tag;
      el('machine-page-body').innerHTML = renderPage(m, fromWelcome);
      if (typeof openWindow === 'function') openWindow('machine-page-window');
      else win.style.display = 'flex';
    });
  }

  function closeMachinePage() {
    if (typeof closeWindow === 'function') closeWindow('machine-page-window');
    else el('machine-page-window').style.display = 'none';
  }

  // The toolbar's menus stop click propagation (so a click inside a menu does
  // not close it), so every menu entry gets its own listener.
  function fillMachinesSubmenu(d) {
    var sub = el('machines-submenu');
    if (!sub) return;
    var h = '';
    for (var i = 0; i < d.machines.length; i++) {
      h += '<button class="toolbar-menu-item machines-submenu-item" data-machine="' + esc(d.machines[i].name) + '">' + esc(d.machines[i].name) + '</button>';
    }
    sub.innerHTML = h || '<div class="toolbar-menu-item disabled">No machine pages</div>';
    var items = sub.querySelectorAll('.machines-submenu-item');
    for (var j = 0; j < items.length; j++) {
      items[j].addEventListener('click', function () { openMachinePage(this.getAttribute('data-machine')); });
    }
  }

  // Selecting a machine from a card or page: the toolbar's own selector does
  // the work, so the choice is saved and the console follows it.
  function selectMachine(name) {
    var sel = el('toolbar-machine-select');
    if (!sel || sel.disabled) return;
    sel.value = name;
    sel.dispatchEvent(new Event('change', { bubbles: true }));
  }

  // ---- wiring -------------------------------------------------------------------

  // Buttons inside the Welcome panel and the machine page: delegated.
  document.addEventListener('click', function (e) {
    var t = e.target && e.target.closest ? e.target.closest('button') : null;
    if (!t) return;
    if (t.id === 'welcome-close' || t.id === 'welcome-close-btn') { closeWelcome(); return; }
    if (t.id === 'machine-page-close') { closeMachinePage(); return; }
    if (t.classList.contains('welcome-card-more')) { openMachinePage(t.getAttribute('data-machine'), true); return; }
    if (t.classList.contains('machine-page-back')) { closeMachinePage(); openWelcome(); return; }
    if (t.classList.contains('welcome-card-start')) { selectAndStart(t.getAttribute('data-machine')); return; }
    if (t.classList.contains('machine-page-select')) { selectMachine(t.getAttribute('data-machine')); closeMachinePage(); return; }
  });
  var mw = el('menu-welcome');
  if (mw) mw.addEventListener('click', function () { openWelcome(); });

  var cb = el('welcome-show-always');
  if (cb) cb.addEventListener('change', function () { setShowOnLoad(cb.checked); });

  // Only the Close buttons close the window - a click on the dark backdrop
  // does nothing (Ronny, 08-OCT-2026).

  if (typeof windowManager !== 'undefined' && windowManager.register) windowManager.register('machine-page-window', 'Machine');
  if (typeof makeDraggable === 'function' && el('machine-page-window') && el('machine-page-header')) {
    makeDraggable(el('machine-page-window'), el('machine-page-header'), 'machine-page-pos');
  }

  loadData().then(fillMachinesSubmenu);

  // First visit (or "show every time" left on): open once the page is up.
  // ?nowelcome in the URL keeps it closed, for scripted runs.
  if (showOnLoad() && !/[?&]nowelcome\b/.test(location.search)) {
    window.addEventListener('load', function () { setTimeout(openWelcome, 400); });
  }

  window.welcomeWindow = { open: openWelcome, close: closeWelcome, openMachinePage: openMachinePage };
})();
