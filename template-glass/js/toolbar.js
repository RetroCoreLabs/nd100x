//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// toolbar.js - Toolbar menus, button wiring, window management, draggable helper

// =========================================================
// Reusable drag helper for glass windows
// =========================================================
// Map window element IDs to localStorage keys for position persistence
var windowStorageKeys = {};

// Save a window's current position to localStorage
function saveWindowPosition(winOrId) {
  var el = (typeof winOrId === 'string') ? document.getElementById(winOrId) : winOrId;
  if (!el) return;
  var key = windowStorageKeys[el.id];
  if (!key) return;
  try {
    localStorage.setItem(key, JSON.stringify({
      left: el.style.left,
      top: el.style.top
    }));
  } catch(e) {}
}

// Restore a window's position from localStorage
function restoreWindowPosition(winOrId) {
  var el = (typeof winOrId === 'string') ? document.getElementById(winOrId) : winOrId;
  if (!el) return;
  var key = windowStorageKeys[el.id];
  if (!key) return;
  try {
    var pos = JSON.parse(localStorage.getItem(key));
    if (pos && pos.left && pos.top) {
      var left = Math.max(0, Math.min(parseInt(pos.left) || 0, window.innerWidth - 200));
      var top = Math.max(49, Math.min(parseInt(pos.top) || 0, window.innerHeight - 100));
      el.style.left = left + 'px';
      el.style.top = top + 'px';
      el.style.bottom = 'auto';
      el.style.right = 'auto';
      el.style.transform = 'none';
    }
  } catch(e) {}
}

function makeDraggable(win, header, storageKey) {
  var dragging = false, ox = 0, oy = 0;
  var toolbarH = 49; // toolbar height (48px + 1px border)

  if (storageKey) windowStorageKeys[win.id] = storageKey;

  header.addEventListener('mousedown', function(e) {
    if (e.target.closest('button') || e.target.closest('select')) return;
    if (win.classList.contains('maximized')) return;
    dragging = true;
    ox = e.clientX - win.getBoundingClientRect().left;
    oy = e.clientY - win.getBoundingClientRect().top;
    e.preventDefault();
  });

  document.addEventListener('mousemove', function(e) {
    if (!dragging) return;
    win.style.left = Math.max(0, Math.min(window.innerWidth - 100, e.clientX - ox)) + 'px';
    win.style.top = Math.max(toolbarH, Math.min(window.innerHeight - 60, e.clientY - oy)) + 'px';
    win.style.bottom = 'auto';
    win.style.right = 'auto';
    win.style.transform = 'none';
  });

  document.addEventListener('mouseup', function() {
    if (dragging) {
      dragging = false;
      saveWindowPosition(win);
    }
  });

  // Restore position
  restoreWindowPosition(win);
}

// =========================================================
// Window Manager - click-to-focus and taskbar
// =========================================================
var windowManager = {
  zCounter: 7000,
  windows: {},
  focusedId: null,

  register: function(id, name) {
    var el = document.getElementById(id);
    if (!el) return;
    this.windows[id] = { name: name, element: el };
    el.style.zIndex = this.zCounter;
    var self = this;
    el.addEventListener('mousedown', function() {
      self.focus(id);
    });
  },

  focus: function(id) {
    var entry = this.windows[id];
    if (!entry) return;
    // Cap at 8899 then reset all to base
    if (this.zCounter >= 8899) {
      this.zCounter = 7000;
      for (var wid in this.windows) {
        this.windows[wid].element.style.zIndex = 7000;
      }
    }
    this.zCounter++;
    entry.element.style.zIndex = this.zCounter;
    this.focusedId = id;
    this.updateTaskbarActive();
  },

  updateTaskbarActive: function() {
    var taskbar = document.getElementById('window-taskbar');
    if (!taskbar) return;
    var btns = taskbar.querySelectorAll('.taskbar-btn');
    for (var i = 0; i < btns.length; i++) {
      var btn = btns[i];
      if (btn.getAttribute('data-win-id') === this.focusedId) {
        btn.classList.add('active');
      } else {
        btn.classList.remove('active');
      }
    }
  },

  updateTaskbar: function() {
    var taskbar = document.getElementById('window-taskbar');
    if (!taskbar) return;

    // Find which window currently has highest z-index among visible
    var topZ = -1;
    var topId = null;
    var visibleWindows = [];

    for (var id in this.windows) {
      var entry = this.windows[id];
      var el = entry.element;
      var style = window.getComputedStyle(el);
      if (style.display !== 'none') {
        visibleWindows.push(id);
        var z = parseInt(el.style.zIndex) || 0;
        if (z > topZ) {
          topZ = z;
          topId = id;
        }
      }
    }

    this.focusedId = topId;

    // Rebuild buttons only if window set changed
    var currentIds = [];
    var existingBtns = taskbar.querySelectorAll('.taskbar-btn');
    for (var i = 0; i < existingBtns.length; i++) {
      currentIds.push(existingBtns[i].getAttribute('data-win-id'));
    }

    var same = (currentIds.length === visibleWindows.length);
    if (same) {
      for (var j = 0; j < visibleWindows.length; j++) {
        if (currentIds.indexOf(visibleWindows[j]) === -1) {
          same = false;
          break;
        }
      }
    }

    if (!same) {
      taskbar.innerHTML = '';
      var self = this;
      for (var k = 0; k < visibleWindows.length; k++) {
        (function(wid) {
          var btn = document.createElement('button');
          btn.className = 'taskbar-btn';
          btn.setAttribute('data-win-id', wid);
          btn.textContent = self.windows[wid].name;
          btn.addEventListener('click', function() {
            var el = self.windows[wid].element;
            if (window.getComputedStyle(el).display === 'none') {
              el.style.display = 'flex';
            }
            // If window is outside visible viewport, move it to top-left
            var rect = el.getBoundingClientRect();
            var vw = window.innerWidth;
            var vh = window.innerHeight;
            if (rect.right < 40 || rect.bottom < 40 || rect.left > vw - 40 || rect.top > vh - 40) {
              el.style.left = '20px';
              el.style.top = '60px';
              saveWindowPosition(el);
            }
            self.focus(wid);
          });
          taskbar.appendChild(btn);
        })(visibleWindows[k]);
      }
    }

    this.updateTaskbarActive();
  }
};

// =========================================================
// Toolbar dropdown menus
// =========================================================
(function() {
  'use strict';

  function closeAllMenus() {
    document.querySelectorAll('.toolbar-menu-container').forEach(function(c) {
      c.classList.remove('open');
    });
  }

  // Toggle menu on trigger click
  document.querySelectorAll('.toolbar-menu-trigger').forEach(function(trigger) {
    trigger.addEventListener('click', function(e) {
      e.stopPropagation();
      var container = trigger.closest('.toolbar-menu-container');
      var wasOpen = container.classList.contains('open');
      closeAllMenus();
      if (!wasOpen) container.classList.add('open');
    });
  });

  // Close on click outside
  document.addEventListener('click', function() {
    closeAllMenus();
  });

  // Close on Escape
  document.addEventListener('keydown', function(e) {
    if (e.key === 'Escape') closeAllMenus();
  });

  // Prevent menu clicks from closing
  document.querySelectorAll('.toolbar-menu').forEach(function(menu) {
    menu.addEventListener('click', function(e) {
      e.stopPropagation();
    });
  });
})();

// =========================================================
// Glass window toggle helpers
// =========================================================
function saveWindowVisibility(windowId, visible) {
  try {
    var state = JSON.parse(localStorage.getItem('window-visibility') || '{}');
    state[windowId] = visible;
    localStorage.setItem('window-visibility', JSON.stringify(state));
  } catch(e) {}
}

function closeWindow(windowId) {
  var win = document.getElementById(windowId);
  if (win) win.style.display = 'none';
  saveWindowVisibility(windowId, false);
}

function openWindow(windowId) {
  var win = document.getElementById(windowId);
  if (win) {
    win.style.display = 'flex';
    windowManager.focus(windowId);
  }
  saveWindowVisibility(windowId, true);
}

function toggleGlassWindow(windowId) {
  var win = document.getElementById(windowId);
  if (!win) return;
  if (win.style.display === 'none' || win.style.display === '') {
    openWindow(windowId);
  } else {
    closeWindow(windowId);
  }
}

// =========================================================
// View menu handlers
// =========================================================
document.getElementById('menu-machine-info').addEventListener('click', function() {
  toggleGlassWindow('machine-window');
  document.querySelector('.toolbar-menu-container').classList.remove('open');
});

// View > Config - opens the same Config window as the taskbar cogwheel
document.getElementById('menu-config').addEventListener('click', function() {
  toggleGlassWindow('config-window');
  document.querySelector('.toolbar-menu-container').classList.remove('open');
});

document.getElementById('menu-cpu-load-graph').addEventListener('click', function() {
  toggleGlassWindow('cpu-load-window');
  document.querySelector('.toolbar-menu-container').classList.remove('open');
});

document.getElementById('menu-floppy-library').addEventListener('click', function() {
  var floppyModal = document.getElementById('floppy-modal');
  if (floppyModal.style.display === 'none' || floppyModal.style.display === '') {
    openFloppyBrowser();
    windowManager.focus('floppy-modal');
  } else {
    closeFloppyBrowser();
  }
  document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
});

document.getElementById('menu-smd-manager').addEventListener('click', function() {
  updateSmdManagerMenuState();   // the persistence notice follows the current setting
  var smdWin = document.getElementById('smd-manager-window');
  if (smdWin.style.display === 'none' || smdWin.style.display === '') {
    if (typeof smdManagerShow === 'function') smdManagerShow();
  } else {
    if (typeof smdManagerHide === 'function') smdManagerHide();
  }
  document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
});

var menuPrinter = document.getElementById('menu-printer');
if (menuPrinter) menuPrinter.addEventListener('click', function() {
  toggleGlassWindow('printer-window');
  document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
});


var menuPaperTape = document.getElementById('menu-paper-tape');
if (menuPaperTape) menuPaperTape.addEventListener('click', function() {
  toggleGlassWindow('paper-tape-window');
  document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
});

document.getElementById('menu-debugger').addEventListener('click', function() {
  var dbgWin = document.getElementById('debugger-window');
  if (dbgWin.style.display === 'none' || dbgWin.style.display === '') {
    dbgWin.style.display = 'flex';
    if (typeof dbgShowWindow === 'function') dbgShowWindow();
    windowManager.focus('debugger-window');
    saveWindowVisibility('debugger-window', true);
  } else {
    dbgWin.style.display = 'none';
    if (typeof dbgHideWindow === 'function') dbgHideWindow();
    saveWindowVisibility('debugger-window', false);
  }
  document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
});

document.getElementById('menu-disassembly').addEventListener('click', function() {
  var disasmWin = document.getElementById('disasm-window');
  if (disasmWin.style.display === 'none' || disasmWin.style.display === '') {
    if (typeof disasmShowWindow === 'function') disasmShowWindow();
    windowManager.focus('disasm-window');
  } else {
    if (typeof disasmHideWindow === 'function') disasmHideWindow();
  }
  document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
});

document.getElementById('disasm-window-close').addEventListener('click', function() {
  if (typeof disasmHideWindow === 'function') disasmHideWindow();
});

document.getElementById('menu-breakpoints').addEventListener('click', function() {
  var bpWin = document.getElementById('breakpoints-window');
  if (bpWin.style.display === 'none' || bpWin.style.display === '') {
    if (typeof bpShowWindow === 'function') bpShowWindow();
    windowManager.focus('breakpoints-window');
  } else {
    if (typeof bpHideWindow === 'function') bpHideWindow();
  }
  document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
});

document.getElementById('breakpoints-window-close').addEventListener('click', function() {
  if (typeof bpHideWindow === 'function') bpHideWindow();
});

// =========================================================
// Help menu handlers
// =========================================================
document.getElementById('menu-sintran-help').addEventListener('click', function() {
  openHelpWindow();
  windowManager.focus('help-window');
  document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
});

document.getElementById('menu-about').addEventListener('click', function() {
  openWindow('about-window');
  document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
});

// Help > PDF manuals. Native browser <iframe> rendering; the iframe src is
// set lazily from data-src on first open so the PDF isn't fetched at page load.
function openPdfWindow(windowId, frameId) {
  var frame = document.getElementById(frameId);
  if (frame && !frame.getAttribute('src')) {
    var src = frame.getAttribute('data-src');
    if (src) frame.setAttribute('src', encodeURI(src));
  }
  openWindow(windowId);
  document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
}

document.getElementById('menu-pdf-handbok').addEventListener('click', function() {
  openPdfWindow('pdf-handbok-window', 'pdf-handbok-frame');
});

document.getElementById('menu-pdf-supervisor').addEventListener('click', function() {
  openPdfWindow('pdf-supervisor-window', 'pdf-supervisor-frame');
});

document.getElementById('pdf-handbok-close').addEventListener('click', function() {
  closeWindow('pdf-handbok-window');
});

document.getElementById('pdf-supervisor-close').addEventListener('click', function() {
  closeWindow('pdf-supervisor-window');
});

// Pop out a PDF into its own browser window (native full PDF viewer), then
// close the inline glass window so it isn't shown in two places at once.
function popOutPdfWindow(windowId, frameId, title) {
  var frame = document.getElementById(frameId);
  var src = frame ? frame.getAttribute('data-src') : null;
  if (!src) return;
  var win = window.open(encodeURI(src), title.replace(/\s+/g, '_'),
    'width=900,height=760,menubar=no,toolbar=no,location=no,status=no');
  if (win) closeWindow(windowId);
}

document.getElementById('pdf-handbok-popout').addEventListener('click', function() {
  popOutPdfWindow('pdf-handbok-window', 'pdf-handbok-frame', 'Handbok');
});

document.getElementById('pdf-supervisor-popout').addEventListener('click', function() {
  popOutPdfWindow('pdf-supervisor-window', 'pdf-supervisor-frame', 'System_Supervisor');
});

// About window close
document.getElementById('about-close').addEventListener('click', function() {
  closeWindow('about-window');
});

// Config window close
document.getElementById('config-window-close').addEventListener('click', function() {
  closeWindow('config-window');
});

// Gateway Statistics window close
document.getElementById('gateway-stats-close').addEventListener('click', function() {
  closeWindow('gateway-stats-window');
});

// Taskbar cogwheel - toggle Config window (taskbar element is below scripts in DOM)
document.addEventListener('DOMContentLoaded', function() {
  var settingsBtn = document.getElementById('taskbar-settings');
  if (settingsBtn) {
    settingsBtn.addEventListener('click', function() {
      toggleGlassWindow('config-window');
    });
  }
  // Taskbar WebSocket indicator - click opens Gateway Statistics window
  var wsInd = document.getElementById('taskbar-ws-indicator');
  if (wsInd) {
    wsInd.addEventListener('click', function() {
      toggleGlassWindow('gateway-stats-window');
    });
  }
});

// =========================================================
// SMD Manager window close + event wiring
// =========================================================
document.getElementById('smd-manager-close').addEventListener('click', function() {
  if (typeof smdManagerHide === 'function') smdManagerHide();
});

// SMD Manager menu item visibility (depends on persistence state)
// The HDD Disk Manager is always openable: it shows the running machine's
// drives and the server catalog in every mode. Only the local library needs
// persistent storage, and the window says so itself (hdd-persist-notice).
function updateSmdManagerMenuState() {
  var menuItem = document.getElementById('menu-smd-manager');
  if (menuItem) {
    menuItem.classList.remove('disabled');
    menuItem.style.opacity = '';
    menuItem.style.pointerEvents = '';
  }
  var notice = document.getElementById('hdd-persist-notice');
  if (notice) notice.style.display = isSmdPersistenceEnabled() ? 'none' : '';
}
updateSmdManagerMenuState();


// Import button
(function() {
  var importBtn = document.getElementById('smd-import-btn');
  if (importBtn) {
    importBtn.addEventListener('click', function() {
      if (typeof smdImportFromFile === 'function') smdImportFromFile();
    });
  }
})();

// =========================================================
// Config: Float terminals toggle
// =========================================================
(function() {
  var groupToggle = document.getElementById('config-group-terminals');
  var popoutToggle = document.getElementById('config-auto-popout');
  var popoutRow = document.getElementById('config-auto-popout-row');
  if (!groupToggle) return;

  function updatePopoutState() {
    var grouped = groupToggle.checked;
    if (popoutRow) popoutRow.style.opacity = grouped ? '0.4' : '1';
    if (popoutToggle) popoutToggle.disabled = grouped;
  }

  // Initialize state - "Group" ON means float-mode OFF (inverted)
  groupToggle.checked = (localStorage.getItem('terminal-float-mode') === 'false');
  if (popoutToggle) {
    popoutToggle.checked = (localStorage.getItem('terminal-auto-popout') === 'true');
  }
  updatePopoutState();

  groupToggle.addEventListener('change', function() {
    // Inverted: group ON = float OFF
    localStorage.setItem('terminal-float-mode', groupToggle.checked ? 'false' : 'true');
    updatePopoutState();
    if (typeof switchTerminalMode === 'function') {
      switchTerminalMode();
    }
  });

  if (popoutToggle) {
    popoutToggle.addEventListener('change', function() {
      localStorage.setItem('terminal-auto-popout', popoutToggle.checked ? 'true' : 'false');
    });
  }
})();

// =========================================================
// Config: Worker mode toggle
// =========================================================
(function() {
  var toggle = document.getElementById('config-worker-mode');
  if (!toggle) return;

  // Initialize from localStorage (same key used by USE_WORKER detection)
  toggle.checked = (localStorage.getItem('nd100x-worker') === 'true');

  toggle.addEventListener('change', function() {
    // Worker mode requires page reload to take effect
    if (confirm('Background execution change requires a page reload. Reload now?')) {
      localStorage.setItem('nd100x-worker', toggle.checked ? 'true' : 'false');
      location.reload();
    } else {
      // Revert checkbox - don't change setting without reload
      toggle.checked = !toggle.checked;
    }
  });
})();

// =========================================================
// Config: Persistent disk storage toggle
// =========================================================
(function() {
  var persistToggle = document.getElementById('config-smd-persist');
  var persistRow = document.getElementById('config-persist-row');
  if (!persistToggle) return;

  // Disable when not in Worker mode
  if (!USE_WORKER) {
    persistToggle.disabled = true;
    if (persistRow) persistRow.style.opacity = '0.4';
    persistToggle.checked = false;
  } else {
    persistToggle.checked = isSmdPersistenceEnabled();
  }

  persistToggle.addEventListener('change', function() {
    if (typeof smdHandlePersistToggle === 'function') {
      smdHandlePersistToggle(persistToggle.checked);
    }
    updateSmdManagerMenuState();
    // Close SMD Manager if persistence was turned off
    if (!persistToggle.checked) {
      if (typeof smdManagerHide === 'function') smdManagerHide();
    }
  });
})();

// =========================================================
// Config: WebSocket terminal bridge toggle
// =========================================================
(function() {
  var toggle = document.getElementById('config-ws-bridge');
  var urlRow = document.getElementById('config-ws-url-row');
  var statusRow = document.getElementById('config-ws-status-row');
  var urlInput = document.getElementById('config-ws-url');
  var statusEl = document.getElementById('config-ws-status');
  var networkTitle = document.getElementById('config-network-title');
  var bridgeRow = document.getElementById('config-ws-bridge-row');
  if (!toggle) return;

  // WebSocket status callback - MUST be defined before any early return
  // so the taskbar indicator works regardless of Worker mode
  window.onWsStatusChange = function(connected, error) {
    if (statusEl) {
      if (connected) {
        statusEl.textContent = 'Connected';
        statusEl.style.color = 'var(--accent-color, #00c8b4)';
      } else {
        statusEl.textContent = error ? ('Error: ' + error) : 'Disconnected';
        statusEl.style.color = '';
      }
    }
    // Update taskbar lightning indicator
    var wsInd = document.getElementById('taskbar-ws-indicator');
    if (wsInd) {
      wsInd.style.display = connected ? 'flex' : 'none';
      wsInd.title = connected ? 'Gateway connected' : 'Gateway disconnected';
    }
    // Auto-show Gateway Statistics window on connect, hide on disconnect
    var gwStatsWin = document.getElementById('gateway-stats-window');
    if (gwStatsWin) {
      if (connected) {
        gwStatsWin.style.display = '';
      } else {
        gwStatsWin.style.display = 'none';
      }
    }
  };

  // Gateway stats update callback
  window.onWsStatsUpdate = function(stats) {
    function fmtBytes(n) {
      if (n < 1024) return n + ' B';
      if (n < 1048576) return (n / 1024).toFixed(1) + ' KB';
      return (n / 1048576).toFixed(1) + ' MB';
    }
    function fmtUptime(since) {
      if (!since) return '-';
      var sec = Math.floor((Date.now() - since) / 1000);
      var h = Math.floor(sec / 3600);
      var m = Math.floor((sec % 3600) / 60);
      var s = sec % 60;
      return (h < 10 ? '0' : '') + h + ':' + (m < 10 ? '0' : '') + m + ':' + (s < 10 ? '0' : '') + s;
    }
    function setEl(id, txt) {
      var el = document.getElementById(id);
      if (el) el.textContent = txt;
    }
    setEl('gw-stat-uptime', fmtUptime(stats.connectedSince));
    setEl('gw-stat-term-in', stats.termIn.frames + ' frames / ' + fmtBytes(stats.termIn.bytes));
    setEl('gw-stat-term-out', stats.termOut.frames + ' frames / ' + fmtBytes(stats.termOut.bytes));
    setEl('gw-stat-disk-read', stats.diskRead.ops + ' ops / ' + fmtBytes(stats.diskRead.bytes));
    setEl('gw-stat-disk-write', stats.diskWrite.ops + ' ops / ' + fmtBytes(stats.diskWrite.bytes));
    setEl('gw-stat-hdlc-rx', stats.hdlcRx.frames + ' frames / ' + fmtBytes(stats.hdlcRx.bytes));
    setEl('gw-stat-hdlc-tx', stats.hdlcTx.frames + ' frames / ' + fmtBytes(stats.hdlcTx.bytes));
    var active = stats.clientConnects - stats.clientDisconnects;
    setEl('gw-stat-clients', active + ' active / ' + stats.clientConnects + ' total');
  };

  // Client connection callback
  window.onWsClientChange = function(action, identCode, clientAddr) {
    if (action === 'connected') {
      console.log('[Gateway] Client', clientAddr, 'connected to identCode', identCode);
    } else {
      console.log('[Gateway] Client disconnected from identCode', identCode);
    }
  };

  // The bridge needs Worker mode (the Worker owns the WebSocket). Without
  // it the section stays ON SCREEN, disabled, saying what it needs - the same
  // way the persistent-storage row above does. Hiding it left people
  // looking for a setting that had vanished.
  var isWorker = (typeof USE_WORKER !== 'undefined' && USE_WORKER);
  if (!isWorker) {
    toggle.checked = false;
    toggle.disabled = true;
    if (bridgeRow) {
      bridgeRow.style.opacity = '0.4';
      bridgeRow.title = 'Needs Background execution (Web Worker) - turn it on above and reload';
      var why = document.createElement('span');
      why.className = 'config-toggle-label';
      why.style.cssText = 'opacity:0.7; font-size:10px; margin-left:4px';
      why.textContent = 'needs Background execution';
      bridgeRow.appendChild(why);
    }
    if (urlRow) urlRow.style.display = 'none';
    if (statusRow) statusRow.style.display = 'none';
    return;
  }

  // Initialize from localStorage
  var savedEnabled = (localStorage.getItem('nd100x-ws-bridge') === 'true');
  var savedUrl = localStorage.getItem('nd100x-ws-url') || 'ws://localhost:8765';
  toggle.checked = savedEnabled;
  if (urlInput) urlInput.value = savedUrl;

  function updateVisibility() {
    var on = toggle.checked;
    if (urlRow) urlRow.style.display = on ? 'flex' : 'none';
    if (statusRow) statusRow.style.display = on ? 'flex' : 'none';
  }
  updateVisibility();

  function connectBridge() {
    var url = (urlInput && urlInput.value) ? urlInput.value.trim() : 'ws://localhost:8765';
    localStorage.setItem('nd100x-ws-bridge', 'true');
    localStorage.setItem('nd100x-ws-url', url);
    if (statusEl) {
      statusEl.textContent = 'Connecting...';
      statusEl.style.color = '';
    }
    // Enable remote terminals first, then connect WebSocket
    emu.enableRemoteTerminals().then(function(result) {
      emu.wsConnect(url);
    });
  }

  function disconnectBridge() {
    localStorage.setItem('nd100x-ws-bridge', 'false');
    emu.wsDisconnect();
    if (statusEl) {
      statusEl.textContent = 'Disconnected';
      statusEl.style.color = '';
    }
  }

  toggle.addEventListener('change', function() {
    updateVisibility();
    if (toggle.checked) {
      connectBridge();
    } else {
      disconnectBridge();
    }
  });

  // URL input: reconnect on Enter
  if (urlInput) {
    urlInput.addEventListener('keydown', function(e) {
      if (e.key === 'Enter' && toggle.checked) {
        disconnectBridge();
        toggle.checked = true;
        updateVisibility();
        connectBridge();
      }
    });
  }

  // Auto-connect on page load if previously enabled and emulator is ready
  if (savedEnabled) {
    // Wait for emulator to be initialized before connecting
    var _autoConnectInterval = setInterval(function() {
      if (typeof emu !== 'undefined' && emu.isReady && emu.isReady()) {
        clearInterval(_autoConnectInterval);
        connectBridge();
      }
    }, 1000);
    // Give up after 30 seconds
    setTimeout(function() { clearInterval(_autoConnectInterval); }, 30000);
  }
})();

// =========================================================
// SINTRAN menu handlers - debug windows
// =========================================================
document.getElementById('menu-process-list').addEventListener('click', function() {
  var win = document.getElementById('process-list-window');
  if (win.style.display === 'none' || win.style.display === '') {
    if (typeof procListShowWindow === 'function') procListShowWindow();
    windowManager.focus('process-list-window');
  } else {
    if (typeof procListHideWindow === 'function') procListHideWindow();
  }
  document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
});

document.getElementById('menu-queue-viewer').addEventListener('click', function() {
  var win = document.getElementById('queue-viewer-window');
  if (win.style.display === 'none' || win.style.display === '') {
    if (typeof queueViewerShowWindow === 'function') queueViewerShowWindow();
    windowManager.focus('queue-viewer-window');
  } else {
    if (typeof queueViewerHideWindow === 'function') queueViewerHideWindow();
  }
  document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
});

document.getElementById('menu-segment-table').addEventListener('click', function() {
  var win = document.getElementById('segment-table-window');
  if (win.style.display === 'none' || win.style.display === '') {
    if (typeof segTableShowWindow === 'function') segTableShowWindow();
    windowManager.focus('segment-table-window');
  } else {
    if (typeof segTableHideWindow === 'function') segTableHideWindow();
  }
  document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
});

document.getElementById('menu-reentrant').addEventListener('click', function() {
  var win = document.getElementById('reentrant-window');
  if (win.style.display === 'none' || win.style.display === '') {
    if (typeof reentrantShowWindow === 'function') reentrantShowWindow();
    windowManager.focus('reentrant-window');
  } else {
    if (typeof reentrantHideWindow === 'function') reentrantHideWindow();
  }
  document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
});

document.getElementById('menu-io-devices').addEventListener('click', function() {
  var win = document.getElementById('io-devices-window');
  if (win.style.display === 'none' || win.style.display === '') {
    if (typeof ioDevicesShowWindow === 'function') ioDevicesShowWindow();
    windowManager.focus('io-devices-window');
  } else {
    if (typeof ioDevicesHideWindow === 'function') ioDevicesHideWindow();
  }
  document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
});

document.getElementById('menu-page-tables').addEventListener('click', function() {
  var win = document.getElementById('page-table-window');
  if (win.style.display === 'none' || win.style.display === '') {
    if (typeof pageTableShowWindow === 'function') pageTableShowWindow();
    windowManager.focus('page-table-window');
  } else {
    if (typeof pageTableHideWindow === 'function') pageTableHideWindow();
  }
  document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
});

// =========================================================
// Copy-to-clipboard button handlers
// =========================================================
document.getElementById('proc-list-copy').addEventListener('click', function() {
  copyTableToClipboard('proc-list-body', 'Process List');
});

document.getElementById('queue-viewer-copy').addEventListener('click', function() {
  // Copy the active tab's table
  var activeContent = document.querySelector('#queue-viewer-window .queue-tab-content.active');
  if (activeContent) {
    var tabName = activeContent.getAttribute('data-tab');
    var titles = { exec: 'Execution Queue', time: 'Time Queue', mon: 'Monitor Queue' };
    copyTableToClipboard(activeContent.id, titles[tabName] || 'Queue');
  }
});

document.getElementById('segment-table-copy').addEventListener('click', function() {
  copyTableToClipboard('segment-table-body', 'Segment Table');
});

document.getElementById('io-devices-copy').addEventListener('click', function() {
  copyTableToClipboard('io-devices-body', 'I/O Devices');
});

document.getElementById('page-table-copy').addEventListener('click', function() {
  var ptSelect = document.getElementById('pt-select');
  var ptLabel = ptSelect ? 'Page Table ' + ptSelect.options[ptSelect.selectedIndex].text : 'Page Table';
  copyTableToClipboard('pt-table-body', ptLabel);
});

document.getElementById('menu-dump-memory').addEventListener('click', function() {
  document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
  if (typeof emu === 'undefined' || !emu.isReady()) {
    alert('Emulator not ready');
    return;
  }

  // Dump full physical memory — size comes from WASM (Dbg_GetPhysMemWords).
  // Check console for "[toolbar] dump:" to verify the computed size.
  var dumpWords = emu.getPhysMemWords ? emu.getPhysMemWords() : 2 * 1024 * 1024;
  var dumpBytes = dumpWords * 2;
  console.log('[toolbar] dump: ' + dumpWords + ' words = ' + (dumpBytes / (1024*1024)).toFixed(2) +
    ' MB (' + (emu.getPhysMemWords ? 'WASM' : 'fallback 2MW') + ')');
  var sizeLabelMB = (dumpBytes / (1024 * 1024)).toFixed(0);
  var sizeLabelKB = (dumpBytes / 1024).toFixed(0);
  var sizeLabel = (dumpBytes >= 1024 * 1024) ? sizeLabelMB + 'MB' : sizeLabelKB + 'KB';

  var rc = emu.dumpPhysicalMemory(dumpWords);
  if (rc !== 0) { alert('Memory dump failed'); return; }
  var data = emu.fsReadFile('/nd100_physmem.bin');
  var blob = new Blob([data], { type: 'application/octet-stream' });
  var url = URL.createObjectURL(blob);
  var a = document.createElement('a');
  a.href = url;
  a.download = 'nd100_physmem_' + sizeLabel + '.bin';
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
  try { emu.fsUnlink('/nd100_physmem.bin'); } catch(e) {}
});

// =========================================================
// Machine Info window close
// =========================================================
document.getElementById('machine-window-close').addEventListener('click', function() {
  closeWindow('machine-window');
});

// =========================================================
// CPU Load graph window close
// =========================================================
document.getElementById('cpu-load-window-close').addEventListener('click', function() {
  closeWindow('cpu-load-window');
});

// =========================================================
// Power / Boot buttons
// =========================================================
let isInitializedBtn = false;

function completePowerOn(btn) {
  isInitialized = true;
  initializeTerminals();
  if (registerTerminalCallbacks()) {
    console.log("Terminal callbacks registered successfully");
  } else {
    console.error("Failed to register terminal callbacks");
  }

  // Update machine info status
  var machStatus = document.getElementById('machine-status');
  if (machStatus) machStatus.textContent = 'Initialized';

  // Update execution mode display
  var execMode = document.getElementById('machine-exec-mode');
  if (execMode) execMode.textContent = (typeof USE_WORKER !== 'undefined' && USE_WORKER) ? 'Web Worker' : 'Direct';

  // Update status
  document.getElementById('status').textContent = 'Initialized';

  // Update power button appearance
  btn.classList.add('initialized');
  btn.title = 'Power off';
  isInitializedBtn = true;
  refreshMachineSelect();   // locks the machine selector

  // Start SINTRAN detection polling
  if (typeof sintranStartDetection === 'function') sintranStartDetection();

  // Apply persisted printer driver setting
  if (typeof applyPersistedPrinterDriver === 'function') applyPersistedPrinterDriver();
}

// The active machine changed (Machine Setup, or the toolbar's own selector):
// everything on screen that names it follows.
function applyMachineKindToToolbar() {
  // The machine's terminal settings (Machine Setup > Terminal) decide the
  // renderer and emulator: while nothing is powered on, rebuild the console
  // so it shows the selected machine's terminal at once. A powered-on
  // machine keeps its terminals - they are rebuilt at the next power-on.
  var power = document.getElementById('toolbar-power');
  var poweredOn = !!(power && power.classList.contains('initialized'));
  if (!poweredOn && typeof terminals !== 'undefined' && terminals[1] && typeof initializeTerminals === 'function') {
    initializeTerminals();
  }
  if (typeof applyKeyboardLanguage === 'function' && typeof getCurrentKeyboardLanguage === 'function') {
    applyKeyboardLanguage(getCurrentKeyboardLanguage());
  }
  // The console window's title names the machine it serves.
  if (typeof updateConsoleTitle === 'function') updateConsoleTitle();
  refreshMachineSelect();
}
window.applyMachineKindToToolbar = applyMachineKindToToolbar;

// The toolbar's machine selector: the Machine Setup profiles by name, the
// active one selected. Rebuilt from the store whenever Machine Setup changes
// it (applyMachineKindToToolbar is called from there), and locked once the
// machine is powered on - what is running is no longer a choice.
function refreshMachineSelect() {
  var sel = document.getElementById('toolbar-machine-select');
  if (!sel || typeof machineProfiles === 'undefined') return;
  var names = machineProfiles.list();
  var active = machineProfiles.activeName();
  sel.innerHTML = '';
  for (var i = 0; i < names.length; i++) {
    var o = document.createElement('option');
    o.value = names[i];
    var kind = machineProfiles.kind(names[i]);
    o.textContent = names[i] + (kind === 'nd500-ndix' ? '  (ND-500 NDIX)' : '  (ND-100)') + (machineProfiles.isShipped(names[i]) ? ' built-in' : '');
    if (names[i] === active) o.selected = true;
    sel.appendChild(o);
  }
  // Read the lock off the button, not isInitializedBtn: that is a `let`
  // further down this file, and this can run before it is initialised.
  var power = document.getElementById('toolbar-power');
  sel.disabled = !!(power && power.classList.contains('initialized'));
}
window.refreshMachineSelect = refreshMachineSelect;

(function() {
  var sel = document.getElementById('toolbar-machine-select');
  if (!sel) return;
  sel.addEventListener('change', function() {
    if (typeof machineProfiles === 'undefined') return;
    machineProfiles.setActive(sel.value);
    applyMachineKindToToolbar();
    // Keep an open Machine Setup window looking at the same machine.
    var win = document.getElementById('machine-setup-window');
    if (win && win.style.display !== 'none' && typeof machineSetupShow === 'function') machineSetupShow();
  });
})();

// machine-profiles.js and ndix-machine.js load after this file.
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', applyMachineKindToToolbar);
} else {
  applyMachineKindToToolbar();
}

document.getElementById('toolbar-power').addEventListener('click', function() {
  var btn = this;
  if (!isInitializedBtn) {
    // --- POWER ON: an ND-500 standalone (NDIX) machine boots right here,
    // there is no separate Boot step for it (ndix-machine.js). ---
    if (window.ndixMachine && ndixMachine.isSelected()) {
      console.log("Powering on the ND-500 (NDIX)");
      applyMachineKindToToolbar();
      ndixMachine.powerOn().then(function(ok) {
        if (!ok) return;
        btn.classList.add('initialized');
        btn.title = 'Power off';
        isInitializedBtn = true;
        refreshMachineSelect();   // locks the machine selector
        var machStatus = document.getElementById('machine-status');
        if (machStatus) machStatus.textContent = 'NDIX running (ND-500 standalone)';
        var execMode = document.getElementById('machine-exec-mode');
        if (execMode) execMode.textContent = (typeof USE_WORKER !== 'undefined' && USE_WORKER) ? 'Web Worker' : 'Direct';
      });
      return;
    }

    // --- POWER ON: build the machine from its INI, then boot its [boot]
    // device. One step: the machine says what it boots from. ---
    console.log("Calling Init");
    var ini = machineProfiles.ini();

    preloadBootImage(ini).then(function() {
      if (emu.isWorkerMode()) {
        // Worker mode: init is async, terminal info arrives via callback.
        // With persistent storage the manager re-mounts the SMD units after
        // Init and signals smd-mounts-ready; the boot waits for THAT signal,
        // not the page-load one.
        if (typeof isSmdPersistenceEnabled === 'function' && isSmdPersistenceEnabled()) window._smdMountsReady = false;
        emu.onInitialized = function(msg) {
          completePowerOn(btn);
          remoteTerminalsBeforeBoot().then(bootConfiguredMachine);
        };
        emu.init(ini);
      } else {
        // Direct mode: synchronous
        emu.init(ini);
        completePowerOn(btn);
        bootConfiguredMachine();
      }
    });
  } else {
    // --- POWER OFF (triggers page reload to reset all state) ---
    console.log("Powering off");
    if (typeof emulationRunning !== 'undefined' && emulationRunning) {
      stopEmulation();
    }
    if (emu) emu.stop();

    btn.classList.remove('initialized');
    btn.title = 'Power on';
    isInitializedBtn = false;
    isInitialized = false;

    var machStatus = document.getElementById('machine-status');
    if (machStatus) machStatus.textContent = 'Not initialized';

    var statusEl = document.getElementById('status');
    if (statusEl) statusEl.textContent = 'Restarting...';

    setTimeout(function() {
      window.location.reload();
    }, 500);
  }
});

// Log which image is actually on each SMD unit at boot time
function logBootDriveInfo() {
  console.log('--- SMD Boot Drive Info ---');

  // Check persistence mode
  var persist = (typeof isSmdPersistenceEnabled === 'function') && isSmdPersistenceEnabled();
  console.log('  Persistence: ' + (persist ? 'ON' : 'OFF (demo mode)'));
  console.log('  Worker mode: ' + (emu && emu.isWorkerMode ? emu.isWorkerMode() : false));

  if (persist && typeof smdStorage !== 'undefined') {
    var units = smdStorage.getUnitAssignments();
    for (var u = 0; u < 4; u++) {
      var uuid = units[u];
      if (uuid) {
        var meta = smdStorage.getMetadata(uuid);
        var name = meta ? meta.name : '(no metadata)';
        var src = meta && meta.sourceName ? ' [source: ' + meta.sourceName + ']' : '';
        console.log('  Unit ' + u + ': "' + name + '" (uuid=' + uuid.substring(0, 8) + '...)' + src);
      } else {
        console.log('  Unit ' + u + ': (empty)');
      }
    }
  }

  // Check drive registry
  if (typeof driveRegistry !== 'undefined') {
    for (var u2 = 0; u2 < 4; u2++) {
      var reg = driveRegistry.get('smd', u2);
      if (reg.mounted) {
        console.log('  Registry unit ' + u2 + ': mounted as "' + reg.name + '" source=' + reg.source +
          (reg.fileName ? ' file=' + reg.fileName.substring(0, 8) + '...' : ''));
      }
    }
  }

  // Demo mode: check diskImageStatus
  if (!persist && typeof diskImageStatus !== 'undefined') {
    console.log('  Demo SMD0: ' + (diskImageStatus.smd ? 'loaded' : 'NOT loaded'));
  }

  console.log('--------------------------');
}

// =========================================================
// Booting the machine's [boot] device
// =========================================================
// The Boot type numbers nd100wasm.c's BootFrom() takes, by the INI's
// controller type name. No bpun/aout here: a file boot names a path on
// the native host, which a browser cannot open - a BPUN comes in through
// View > Boot BPUN file... instead.
var BOOT_TYPE_OF_CTRL = { floppy: 0, smd: 1, scsi: 3, wd: 4 };
var BOOT_LABEL_OF_CTRL = { floppy: 'FLOPPY', smd: 'SMD', scsi: 'SCSI', wd: 'WINCHESTER' };

// What the active machine's INI says it boots from, as DescribeMachineINI
// (the C parser) reads it - never a second parser here. Resolves to
// {boot, image} where image is the boot unit's own disk image, or null.
function describeBootDevice(ini) {
  if (typeof emu === 'undefined' || !emu.describeMachineINI) return Promise.resolve(null);
  return Promise.resolve(emu.describeMachineINI(ini)).then(function(json) {
    var d;
    try { d = JSON.parse(json); } catch (e) { return null; }
    if (!d || d.error || !d.boot) return null;
    var b = d.boot, image = null;
    if (b.isDisc) {
      for (var i = 0; i < d.controllers.length; i++) {
        var c = d.controllers[i];
        if (c.type !== b.type || c.wheel !== b.wheel) continue;
        var k = c.disks && c.disks[b.unit];
        if (k && k.present) image = k.image;
      }
    }
    return { boot: b, image: image };
  });
}

// Demo mode fetches the boot unit's image into MEMFS before Init(), so the
// controller mounts it the way it mounts SMD0.IMG - BSD's WD0.IMG, a SCSI
// pack, a second SMD unit. With persistent storage on, SMD and SCSI units
// come out of the library instead (module-init.js) and are not fetched.
var _preloadedImages = {};

// Put <bytes> into MEMFS as /<file> in either mode, so the controller mounts
// it at Init the way it mounts the demo SMD pack.
function stageImageBytes(file, bytes) {
  if (emu.isWorkerMode()) {
    var buf = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
    emu.workerLoadDisk('/' + file, buf);
  } else {
    emu.fsWriteFile('/' + file, bytes);
    try { emu.fsChmod('/' + file, 0o666); } catch (e) { /* ignore */ }
  }
  _preloadedImages[file] = true;
}

// The catalog floppies the machine's floppy drives were given in Machine
// Setup (machineProfiles.floppies): fetched from the Norsk Data software
// archive and staged under the INI's disk<n> file name, so [controller.
// floppy.0] mounts them at Init. A slot whose INI file no longer matches its
// choice is a local file and is left alone.
function preloadArchiveFloppies(ini) {
  if (typeof machineProfiles === 'undefined' || typeof fetchArchiveImage !== 'function') return Promise.resolve();
  var picks = machineProfiles.floppies();
  var slots = Object.keys(picks);
  if (!slots.length) return Promise.resolve();
  return Promise.resolve(emu.describeMachineINI(ini)).then(function(json) {
    var d; try { d = JSON.parse(json); } catch (e) { return; }
    if (!d || d.error) return;
    var fl = null;
    for (var i = 0; i < d.controllers.length; i++) {
      if (d.controllers[i].type === 'floppy' && d.controllers[i].wheel === 0 && d.controllers[i].enabled) fl = d.controllers[i];
    }
    if (!fl) return;
    var statusEl = document.getElementById('status');
    var chain = Promise.resolve();
    slots.forEach(function(k) {
      var slot = parseInt(k, 10), pk = picks[k], disk = fl.disks && fl.disks[slot];
      if (!disk || !disk.present || disk.image !== pk.file || _preloadedImages[pk.file]) return;
      chain = chain.then(function() {
        if (statusEl) statusEl.textContent = 'Fetching floppy ' + pk.name + ' from the archive...';
        return fetchArchiveImage(pk.imageUrl).then(function(bytes) {
          stageImageBytes(pk.file, bytes);
          if (typeof driveRegistry !== 'undefined') driveRegistry.mount('floppy', slot, 'archive', pk.name, pk.file, bytes.length);
          console.log('[Boot] floppy unit ' + slot + ': ' + pk.name + ' -> /' + pk.file + ' (' + bytes.length + ' bytes) from the archive');
        }).catch(function(e) {
          console.warn('[Boot] floppy unit ' + slot + ': could not fetch ' + pk.name + ' - ' + (e && e.message ? e.message : e));
        });
      });
    });
    return chain;
  });
}

// The library images the machine's drives were given in Machine Setup
// (machineProfiles.library, "type.wheel.slot" -> {uuid, file, name}): mounted
// before Init so the machine comes up with exactly what its configuration
// says is in its drives - the machine is the master of its drives, the HDD
// Disk Manager is a tool for a running one. Per type:
//   smd, scsi  - the library's own mount paths (OPFS SyncAccessHandle in
//                Worker mode, a buffer in Direct mode), so writes persist
//                the way the HDD manager's own assignments do.
//   wd, floppy - the image is staged into MEMFS under the INI's file name
//                and the controller mounts it at Init; writes stay in memory.
// A slot whose INI file no longer names its choice is left alone.
function preloadLibraryDisks(ini) {
  if (typeof machineProfiles === 'undefined' || typeof smdStorage === 'undefined') return Promise.resolve();
  if (typeof isSmdPersistenceEnabled !== 'function' || !isSmdPersistenceEnabled()) return Promise.resolve();
  var picks = machineProfiles.library();
  var keys = Object.keys(picks);
  if (!keys.length) return Promise.resolve();
  return smdStorage.init().then(function() {
    return Promise.resolve(emu.describeMachineINI(ini));
  }).then(function(json) {
    var d; try { d = JSON.parse(json); } catch (e) { return; }
    if (!d || d.error) return;
    var statusEl = document.getElementById('status');
    var chain = Promise.resolve();
    keys.forEach(function(key) {
      var m = /^(\w+)\.(\d+)\.(\d+)$/.exec(key);
      if (!m) return;
      var type = m[1], wheel = parseInt(m[2], 10), slot = parseInt(m[3], 10), pk = picks[key];
      var ctrl = null;
      for (var i = 0; i < d.controllers.length; i++) {
        if (d.controllers[i].type === type && d.controllers[i].wheel === wheel && d.controllers[i].enabled) ctrl = d.controllers[i];
      }
      var disk = ctrl && ctrl.disks && ctrl.disks[slot];
      if (!disk || !disk.present || disk.image !== pk.file) return;
      chain = chain.then(function() {
        if (statusEl) statusEl.textContent = 'Mounting ' + pk.name + ' from the library...';
        return mountLibraryImage(type, slot, pk).catch(function(e) {
          console.warn('[Boot] ' + type + ' unit ' + slot + ': could not mount ' + pk.name + ' from the library - ' + (e && e.message ? e.message : e));
        });
      });
    });
    return chain;
  });
}

// One library image onto one unit, before Init. Resolves when mounted.
function mountLibraryImage(type, unit, pk) {
  var regType = (type === 'wd') ? 'winchester' : type;
  var worker = emu.isWorkerMode();
  function registered(size, source) {
    if (typeof driveRegistry !== 'undefined') driveRegistry.mount(regType, unit, source, pk.name, pk.uuid, size || 0);
    console.log('[Boot] ' + regType + ' unit ' + unit + ': ' + pk.name + ' from the library (' + source + ')');
  }
  if (type === 'smd') {
    // The same bookkeeping the HDD manager does for an assignment, so the
    // two never disagree about unit 0..3 - including the Direct-mode
    // save-back of a dirty buffer at eject. The MOUNT itself happens after
    // Init: in Worker mode the manager's post-init sync mounts every assigned
    // unit from OPFS (and the boot waits for its 'smd-mounts-ready'); in
    // Direct mode mountLibraryDisksAfterInit() loads the buffer. Mounting
    // here, before Init, had the sync re-open the OPFS handle under a boot
    // that was reading sector 0.
    if (typeof smdEjectUnit === 'function' && typeof driveRegistry !== 'undefined') {
      var cur = driveRegistry.get('smd', unit);
      if (cur && cur.mounted && cur.fileName !== pk.uuid) smdEjectUnit(unit);
    }
    smdStorage.setUnitAssignment(unit, pk.uuid);
    if (typeof driveRegistry !== 'undefined') driveRegistry.mount('smd', unit, 'opfs', pk.name, pk.uuid, 0);
    return Promise.resolve();
  }
  if (type === 'scsi') {
    // Mounted after Init (mountLibraryDisksAfterInit), like SMD.
    return Promise.resolve();
  }
  // wd, floppy: staged as the INI's file; the controller mounts it at Init.
  return smdStorage.retrieveImage(pk.uuid).then(function(data) {
    if (!data) throw new Error('not in the library');
    stageImageBytes(pk.file, data);
    registered(data.byteLength, 'library');
  });
}

// After Init, before boot: the SMD (Direct mode) and SCSI library images.
// Worker-mode SMD is the manager's post-init sync; see mountLibraryImage.
var _libraryMountedAfterInit = false;
function mountLibraryDisksAfterInit() {
  if (_libraryMountedAfterInit) return Promise.resolve();
  _libraryMountedAfterInit = true;
  if (typeof machineProfiles === 'undefined' || typeof smdStorage === 'undefined') return Promise.resolve();
  if (typeof isSmdPersistenceEnabled !== 'function' || !isSmdPersistenceEnabled()) return Promise.resolve();
  var picks = machineProfiles.library();
  var worker = emu.isWorkerMode();
  var chain = Promise.resolve();
  Object.keys(picks).forEach(function(key) {
    var m = /^(smd|scsi)\.(\d+)\.(\d+)$/.exec(key);
    if (!m) return;
    var type = m[1], unit = parseInt(m[3], 10), pk = picks[key];
    if (type === 'smd' && worker) return;   // the post-init sync does it
    if (typeof driveRegistry !== 'undefined') {
      var cur = driveRegistry.get(type, unit);
      if (cur && cur.mounted && cur.fileName === pk.uuid && cur.imageSize > 0 && type === 'scsi') return;
    }
    chain = chain.then(function() {
      var done = function(size) {
        if (unit === 0 && type === 'smd' && typeof diskImageStatus !== 'undefined') diskImageStatus.smd = true;
        if (typeof driveRegistry !== 'undefined') driveRegistry.mount(type, unit, 'opfs', pk.name, pk.uuid, size || 0);
        console.log('[Boot] ' + type + ' unit ' + unit + ': ' + pk.name + ' from the library (opfs)');
      };
      if (type === 'scsi' && worker) {
        return emu.opfsMountSCSI(unit, pk.uuid).then(function(r) { if (r && r.ok) done(r.size); else throw new Error('OPFS mount failed'); });
      }
      return smdStorage.retrieveImage(pk.uuid).then(function(data) {
        if (!data) throw new Error('not in the library');
        var rc = (type === 'smd') ? emu.mountSMDFromBuffer(unit, data) : emu.mountSCSIFromBuffer(unit, data);
        if (rc !== 0) throw new Error('mount failed');
        done(data.byteLength);
      });
    }).catch(function(e) {
      console.warn('[Boot] ' + type + ' unit ' + unit + ': could not mount ' + pk.name + ' from the library - ' + (e && e.message ? e.message : e));
    });
  });
  return chain;
}

// The gateway's remote terminals (TERMINAL 12-19) have to exist BEFORE the
// boot, not be added once the bridge connects. Measured 07-OCT-2026 on
// SINTRAN K in Worker mode: with the terminals created about a second after
// the boot started (the bridge's auto-connect), ESC typed on TERMINAL 12
// through the gateway got no reply at all; created between Init and the
// boot, the same ESC got SINTRAN's answer. So when the bridge is switched
// on, the terminals are created here, before bootConfiguredMachine().
// Connecting the bridge afterwards finds them already there (the worker
// rebuilds its list and re-registers; EnableRemoteTerminals itself is a
// no-op the second time).
function remoteTerminalsBeforeBoot() {
  var on = false;
  try { on = localStorage.getItem('nd100x-ws-bridge') === 'true'; } catch (e) {}
  if (!on || !emu.isWorkerMode()) return Promise.resolve();
  return Promise.resolve(emu.enableRemoteTerminals()).then(function (r) {
    console.log('[Gateway] remote terminals created before boot: ' + ((r && r.count) || 0));
  }, function (e) {
    console.error('[Gateway] remote terminals before boot failed: ' + (e && e.message ? e.message : e));
  });
}

function preloadBootImage(ini) {
  return preloadLibraryDisks(ini).then(function() { return preloadArchiveFloppies(ini); })
    .then(function() { return describeBootDevice(ini); }).then(function(r) {
    if (!r || !r.boot.isDisc || !r.image) return;
    var type = r.boot.type, image = r.image;
    // With persistent storage on, a unit the library already holds (a
    // library pick of this machine, or an HDD-manager assignment) is not
    // fetched over. A unit nothing holds - a built-in machine's demo image,
    // say - is fetched exactly as in demo mode, or there is nothing to boot.
    var regType = (type === 'wd') ? 'winchester' : type;
    if (typeof driveRegistry !== 'undefined' && driveRegistry.isOccupied(regType, r.boot.unit)) return;
    if (_preloadedImages[image]) return;
    if (image === 'SMD0.IMG' && typeof diskImageStatus !== 'undefined' && diskImageStatus.smd) return;
    if (typeof loadDiskImage !== 'function') return;
    return loadDiskImage(image, '/' + image).then(function(ok) {
      if (ok) _preloadedImages[image] = true;
      else console.warn('[Boot] ' + image + ' is not on the server - the ' + type +
                        ' unit ' + r.boot.unit + ' will be empty');
    });
  });
}

// Boot the powered-on ND-100 from the active machine's [boot] device.
var _bootPendingMounts = false;
function bootConfiguredMachine() {
  if (!isInitializedBtn) return;
  if (window.ndixMachine && ndixMachine.isSelected()) return;   // ndix-machine.js boots its own
  var statusEl = document.getElementById('status');
  // Persistent storage: in Worker mode the HDD manager re-mounts every
  // assigned SMD unit after Init and signals 'smd-mounts-ready' when done;
  // booting before that reads sector 0 through a handle being re-opened.
  if (typeof isSmdPersistenceEnabled === 'function' && isSmdPersistenceEnabled() && !window._smdMountsReady) {
    _bootPendingMounts = true;
    if (statusEl) statusEl.textContent = 'Waiting for disk mounts...';
    return;
  }
  mountLibraryDisksAfterInit().then(function() {
    return describeBootDevice(machineProfiles.ini());
  }).then(function(r) {
    if (!r) {
      if (statusEl) statusEl.textContent = 'Powered on - the machine configuration could not be read';
      return;
    }
    var b = r.boot;
    if (b.none) {
      if (statusEl) statusEl.textContent = 'Powered on - no boot drive (set one in Machine Setup)';
      terminals[activeTerminalId].term.writeln('\r\n\x1b[33mPowered on. This machine has no boot drive; nothing was loaded.\x1b[0m');
      return;
    }
    if (!b.isDisc) {
      if (statusEl) statusEl.textContent = 'Powered on - a file boot (' + (b.file || '') + ') is not possible in the browser';
      terminals[activeTerminalId].term.writeln('\r\n\x1b[33mPowered on. The machine boots a file (' + (b.file || '') +
        ') which the browser cannot open - use View > Boot BPUN file... instead.\x1b[0m');
      return;
    }
    var bootType = BOOT_TYPE_OF_CTRL[b.type];
    if (bootType === undefined) {
      if (statusEl) statusEl.textContent = 'Powered on - cannot boot from a ' + b.type + ' controller here';
      return;
    }
    performBoot(bootType, b.unit, r.image, BOOT_LABEL_OF_CTRL[b.type] + ' unit ' + b.unit);
  });
}

// boot_type values: 0=FLOPPY, 1=SMD, 2=BPUN, 3=SCSI, 4=WINCHESTER (nd100wasm.c
// BootFrom); unit is the controller unit; image the MEMFS file for a unit
// that was not mounted at Init (null for the conventional name).
function performBoot(bootType, unit, image, label) {
  var bootNames = ['FLOPPY', 'SMD', 'BPUN', 'SCSI', 'WINCHESTER'];
  unit = unit | 0;
  label = label || bootNames[bootType] || String(bootType);

  console.log("Booting " + label + (image ? ' (' + image + ')' : ''));

  // Show which disk we're booting from
  if (bootType === 1 && typeof driveRegistry !== 'undefined') {
    var bootDrive = driveRegistry.get('smd', 0);
    if (bootDrive && bootDrive.mounted) {
      var statusEl = document.getElementById('status');
      if (statusEl) statusEl.textContent = 'Booting from: ' + (bootDrive.name || 'SMD unit 0') + ' (' + bootDrive.source + ')';
    }
  }

  // Log detailed drive info for SMD boot
  if (bootType === 1) {
    logBootDriveInfo();
  }

  if (emu.isWorkerMode()) {
    // Worker mode: boot is async, result arrives via callback
    emu.onBooted = function(msg) {
      if (msg.result < 0) {
        document.getElementById('status').textContent = 'Boot failed';
        terminals[activeTerminalId].term.writeln(
          '\r\n\x1b[31mBoot failed - could not load from ' +
          (bootNames[bootType] || 'unknown') + '.\x1b[0m' +
          '\r\n\x1b[33mCheck that the image file exists and is valid.\x1b[0m'
        );
        return;
      }

      var pc = emu.getPC();
      console.log("Boot OK - P set to " + pc.toString(8).padStart(6, '0'));

      // Sync drive registry from C backend (picks up demo-mode mounts)
      if (typeof driveRegistry !== 'undefined') driveRegistry.syncFromBackend();

      startEmulation(label);
    };
    emu.boot(bootType, unit, image);
    return;
  }

  // Direct mode: synchronous
  var result = emu.boot(bootType, unit, image);

  // Boot() returns PC on success, -1 on failure
  if (result < 0) {
    document.getElementById('status').textContent = 'Boot failed';
    terminals[activeTerminalId].term.writeln(
      '\r\n\x1b[31mBoot failed - could not load from ' +
      (bootNames[bootType] || 'unknown') + '.\x1b[0m' +
      '\r\n\x1b[33mCheck that the image file exists and is valid.\x1b[0m'
    );
    return;  // Leave boot button enabled for retry
  }

  var pc = emu.getPC();
  console.log("Boot OK - P set to " + pc.toString(8).padStart(6, '0'));

  // Sync drive registry from C backend (picks up demo-mode mounts)
  if (typeof driveRegistry !== 'undefined') driveRegistry.syncFromBackend();

  startEmulation(label);
}

// ?image= — after disk load + terminals, auto Power On and SMD boot (see module-init.js)
var _autoUrlImageBootRan = false;

function scheduleAutoUrlImageBoot() {
  if (_autoUrlImageBootRan) return;
  if (!window.__nd100xUrlImage || typeof diskImageStatus === 'undefined' || !diskImageStatus.smd) return;
  // ?image= names an SMD pack for the ND-100. An NDIX machine has no SMD.
  if (window.ndixMachine && ndixMachine.isSelected()) return;

  var btn = document.getElementById('toolbar-power');
  if (!btn || typeof emu === 'undefined' || !emu) return;

  if (isInitializedBtn) return;

  _autoUrlImageBootRan = true;

  console.log('Auto power-on (?image=' + window.__nd100xUrlImage + ') - the machine boots its own [boot] device');
  // Power does init + boot now; the pack is already in MEMFS as /SMD0.IMG.
  btn.click();
}


// Persistent mount completion: a power-on that was waiting for the
// library's units to mount boots now.
window.addEventListener('smd-mounts-ready', function(e) {
  window._smdMountsReady = true;
  if (_bootPendingMounts && isInitializedBtn) {
    _bootPendingMounts = false;
    bootConfiguredMachine();
  }
});

// View > Boot BPUN file...: load a program into the powered-on ND-100.
(function() {
  var item = document.getElementById('menu-boot-bpun');
  if (!item) return;
  item.addEventListener('click', function() {
    if (!isInitializedBtn || (window.ndixMachine && ndixMachine.isSelected())) {
      document.getElementById('status').textContent = 'Power on an ND-100 machine first, then boot a BPUN';
      return;
    }
    document.getElementById('bpun-file-input').click();
  });
})();

// Handle BPUN file upload
document.getElementById('bpun-file-input').addEventListener('change', function(e) {
  var file = e.target.files[0];
  if (!file) return;

  var reader = new FileReader();
  reader.onload = function(ev) {
    var data = new Uint8Array(ev.target.result);
    // Write uploaded BPUN to MEMFS
    if (emu.isWorkerMode()) {
      emu.workerLoadDisk('/BPUN_UPLOAD.IMG', ev.target.result);
    } else {
      emu.fsWriteFile('/BPUN_UPLOAD.IMG', data);
    }
    console.log("BPUN file uploaded: " + file.name + " (" + data.length + " bytes)");

    // Small delay in Worker mode to let the file transfer complete
    if (emu.isWorkerMode()) {
      setTimeout(function() { performBoot(2, 0, '/BPUN_UPLOAD.IMG', 'BPUN ' + file.name); }, 100);
    } else {
      performBoot(2, 0, '/BPUN_UPLOAD.IMG', 'BPUN ' + file.name);
    }
  };
  reader.readAsArrayBuffer(file);

  // Reset input so same file can be re-selected
  this.value = '';
});

// =========================================================
// Terminal font/color select handlers (now in terminal header)
// =========================================================
document.getElementById('font-family-select').addEventListener('change', function(e) {
  var settings = getTerminalSettings(activeTerminalId);
  settings.fontFamily = e.target.value;
  saveTerminalSettings();
  applySettingsToTerminal(activeTerminalId);
});

document.getElementById('color-theme-select').addEventListener('change', function(e) {
  var settings = getTerminalSettings(activeTerminalId);
  settings.colorTheme = e.target.value;
  saveTerminalSettings();
  applySettingsToTerminal(activeTerminalId);
});

// =========================================================
// Terminal window management (drag, maximize)
// =========================================================
(function() {
  var termWin = document.getElementById('terminal-window');
  var termHeader = document.getElementById('terminal-window-header');
  var termMaxBtn = document.getElementById('term-maximize');
  var termMaxIcon = document.getElementById('term-maximize-icon');

  var expandSVG = '<polyline points="9,1 13,1 13,5"/><line x1="13" y1="1" x2="8" y2="6"/><polyline points="5,13 1,13 1,9"/><line x1="1" y1="13" x2="6" y2="8"/>';
  var collapseSVG = '<polyline points="5,1 1,1 1,5"/><line x1="1" y1="1" x2="6" y2="6"/><polyline points="9,13 13,13 13,9"/><line x1="13" y1="13" x2="8" y2="8"/>';

  function resizeAllTerminals() {
    Object.values(terminals).forEach(function(t) { t.resizeTerminal(); });
  }

  function isMaximized() {
    return termWin.classList.contains('maximized');
  }

  function saveTermPos() {
    try {
      localStorage.setItem('term-pos', JSON.stringify({
        left: termWin.style.left,
        top: termWin.style.top,
        width: termWin.style.width,
        height: termWin.style.height
      }));
    } catch(e) {}
  }

  function restoreTermPos() {
    try {
      var pos = JSON.parse(localStorage.getItem('term-pos'));
      if (pos) {
        if (pos.left) {
          var left = Math.max(0, Math.min(parseInt(pos.left) || 0, window.innerWidth - 200));
          var top = Math.max(49, Math.min(parseInt(pos.top) || 0, window.innerHeight - 100));
          termWin.style.left = left + 'px';
          termWin.style.top = top + 'px';
          termWin.style.bottom = 'auto';
          termWin.style.transform = 'none';
        }
        if (pos.width) {
          var w = parseInt(pos.width) || 860;
          termWin.style.width = Math.min(w, window.innerWidth - 20) + 'px';
        }
        if (pos.height) termWin.style.height = pos.height;
      }
    } catch(e) {}
  }

  // Pop-out button for main terminal (pops out the active tab-based terminal)
  var termPopoutBtn = document.getElementById('term-popout');
  if (termPopoutBtn) {
    termPopoutBtn.addEventListener('click', function() {
      if (typeof window.popOutTerminal === 'function' && typeof activeTerminalId !== 'undefined') {
        window.popOutTerminal(activeTerminalId);
      }
    });
  }

  // Use makeDraggable for terminal (but we handle maximize separately)
  makeDraggable(termWin, termHeader, 'term-pos');

  // Maximize/minimize toggle
  termMaxBtn.addEventListener('click', function() {
    if (isMaximized()) {
      termWin.classList.remove('maximized');
      restoreTermPos();
      termMaxIcon.innerHTML = expandSVG;
      termMaxBtn.title = 'Maximize';
    } else {
      saveTermPos();
      termWin.classList.add('maximized');
      termMaxIcon.innerHTML = collapseSVG;
      termMaxBtn.title = 'Restore';
    }
    try {
      localStorage.setItem('term-maximized', isMaximized() ? '1' : '0');
    } catch(e) {}
    setTimeout(resizeAllTerminals, 50);
  });

  // Restore state on load
  restoreTermPos();
  try {
    if (localStorage.getItem('term-maximized') === '1') {
      termWin.classList.add('maximized');
      termMaxIcon.innerHTML = collapseSVG;
      termMaxBtn.title = 'Restore';
    }
  } catch(e) {}
  setTimeout(resizeAllTerminals, 100);
})();

// =========================================================
// Make Machine Info and Floppy Drives windows draggable
// =========================================================
(function() {
  var machWin = document.getElementById('machine-window');
  var machHeader = document.getElementById('machine-window-header');
  if (machWin && machHeader) makeDraggable(machWin, machHeader, 'machine-pos');

  var cpuLoadWin = document.getElementById('cpu-load-window');
  var cpuLoadHeader = document.getElementById('cpu-load-window-header');
  if (cpuLoadWin && cpuLoadHeader) makeDraggable(cpuLoadWin, cpuLoadHeader, 'cpu-load-pos');

  // Floppy Drives window removed - drives shown in Machine Info

  var helpWin = document.getElementById('help-window');
  var helpHeader = document.getElementById('help-window-header');
  if (helpWin && helpHeader) makeDraggable(helpWin, helpHeader, 'help-window-pos');

  var pdfHbWin = document.getElementById('pdf-handbok-window');
  var pdfHbHeader = document.getElementById('pdf-handbok-header');
  if (pdfHbWin && pdfHbHeader) makeDraggable(pdfHbWin, pdfHbHeader, 'pdf-handbok-pos');

  var pdfSuWin = document.getElementById('pdf-supervisor-window');
  var pdfSuHeader = document.getElementById('pdf-supervisor-header');
  if (pdfSuWin && pdfSuHeader) makeDraggable(pdfSuWin, pdfSuHeader, 'pdf-supervisor-pos');

  var floppyBrowserWin = document.getElementById('floppy-modal');
  var floppyBrowserHeader = document.getElementById('floppy-modal-header');
  if (floppyBrowserWin && floppyBrowserHeader) makeDraggable(floppyBrowserWin, floppyBrowserHeader, 'floppy-browser-pos');

  var disasmWin = document.getElementById('disasm-window');
  var disasmHeader = document.getElementById('disasm-window-header');
  if (disasmWin && disasmHeader) makeDraggable(disasmWin, disasmHeader, 'disasm-pos');

  var bpWin = document.getElementById('breakpoints-window');
  var bpHeader = document.getElementById('breakpoints-window-header');
  if (bpWin && bpHeader) makeDraggable(bpWin, bpHeader, 'breakpoints-pos');

  var sysinfoWin = document.getElementById('sysinfo-window');
  var sysinfoHeader = document.getElementById('sysinfo-window-header');
  if (sysinfoWin && sysinfoHeader) makeDraggable(sysinfoWin, sysinfoHeader, 'sysinfo-pos');

  var procWin = document.getElementById('process-list-window');
  var procHeader = document.getElementById('process-list-header');
  if (procWin && procHeader) makeDraggable(procWin, procHeader, 'proc-list-pos');

  var queueWin = document.getElementById('queue-viewer-window');
  var queueHeader = document.getElementById('queue-viewer-header');
  if (queueWin && queueHeader) makeDraggable(queueWin, queueHeader, 'queue-viewer-pos');

  var segWin = document.getElementById('segment-table-window');
  var segHeader = document.getElementById('segment-table-header');
  if (segWin && segHeader) makeDraggable(segWin, segHeader, 'segment-table-pos');

  var reentWin = document.getElementById('reentrant-window');
  var reentHeader = document.getElementById('reentrant-header');
  if (reentWin && reentHeader) makeDraggable(reentWin, reentHeader, 'reentrant-pos');

  var segDisasmWin = document.getElementById('seg-disasm-window');
  var segDisasmHeader = document.getElementById('seg-disasm-header');
  if (segDisasmWin && segDisasmHeader) makeDraggable(segDisasmWin, segDisasmHeader, 'seg-disasm-pos');

  var ioWin = document.getElementById('io-devices-window');
  var ioHeader = document.getElementById('io-devices-header');
  if (ioWin && ioHeader) makeDraggable(ioWin, ioHeader, 'io-devices-pos');

  var ptWin = document.getElementById('page-table-window');
  var ptHeader = document.getElementById('page-table-header');
  if (ptWin && ptHeader) makeDraggable(ptWin, ptHeader, 'page-table-pos');

  var configWin = document.getElementById('config-window');
  var configHeader = document.getElementById('config-window-header');
  if (configWin && configHeader) makeDraggable(configWin, configHeader, 'config-pos');

  var smdWin = document.getElementById('smd-manager-window');
  var smdHeader = document.getElementById('smd-manager-header');
  if (smdWin && smdHeader) makeDraggable(smdWin, smdHeader, 'smd-manager-pos');

  var gwStatsWin = document.getElementById('gateway-stats-window');
  var gwStatsHeader = document.getElementById('gateway-stats-header');
  if (gwStatsWin && gwStatsHeader) makeDraggable(gwStatsWin, gwStatsHeader, 'gateway-stats-pos');

  // Debugger has its own drag logic but register its storage key for the global helpers
  windowStorageKeys['debugger-window'] = 'dbg-pos';
})();

// =========================================================
// Reusable resize handle helper
// =========================================================
function makeResizable(win, handle, storageKey, minW, minH, computeHeight) {
  if (!win || !handle) return;

  var resizing = false;
  var startX, startY, startW, startH;

  handle.addEventListener('mousedown', function(e) {
    e.preventDefault();
    e.stopPropagation();
    resizing = true;
    startX = e.clientX;
    startY = e.clientY;
    startW = win.offsetWidth;
    startH = win.offsetHeight;
    // Clear centering transform on first resize
    win.style.transform = 'none';
    if (!win.style.left || win.style.left === '') {
      var rect = win.getBoundingClientRect();
      win.style.left = rect.left + 'px';
      win.style.top = rect.top + 'px';
    }
  });

  document.addEventListener('mousemove', function(e) {
    if (!resizing) return;
    var newW = Math.max(minW, startW + (e.clientX - startX));
    win.style.width = newW + 'px';
    if (typeof computeHeight === 'function') {
      win.style.height = Math.max(minH, computeHeight(newW)) + 'px';
    } else {
      win.style.height = Math.max(minH, startH + (e.clientY - startY)) + 'px';
    }
  });

  document.addEventListener('mouseup', function() {
    if (resizing) {
      resizing = false;
      if (storageKey) {
        try {
          localStorage.setItem(storageKey, JSON.stringify({
            width: win.style.width,
            height: win.style.height
          }));
        } catch(e) {}
      }
    }
  });

  // Restore saved size
  if (storageKey) {
    try {
      var saved = JSON.parse(localStorage.getItem(storageKey));
      if (saved && saved.width && saved.height) {
        win.style.width = saved.width;
        win.style.height = saved.height;
      }
    } catch(e) {}
  }
}

// =========================================================
// Copy table as Markdown to clipboard
// =========================================================
function copyTableToClipboard(containerId, windowTitle) {
  var container = document.getElementById(containerId);
  if (!container) return;

  var table = container.querySelector('table');
  if (!table) return;

  // Extract headers
  var headers = [];
  var ths = table.querySelectorAll('thead th');
  for (var i = 0; i < ths.length; i++) {
    headers.push(ths[i].textContent.trim());
  }
  if (headers.length === 0) return;

  // Extract rows
  var rows = [];
  var trs = table.querySelectorAll('tbody tr');
  for (var r = 0; r < trs.length; r++) {
    var cells = [];
    var tds = trs[r].querySelectorAll('td');
    for (var c = 0; c < tds.length; c++) {
      // Replace pipe chars in cell text to avoid breaking markdown
      cells.push(tds[c].textContent.trim().replace(/\|/g, '/'));
    }
    rows.push(cells);
  }

  // Build markdown
  var md = '# ' + windowTitle + '\n\n';
  md += '| ' + headers.join(' | ') + ' |\n';
  md += '|';
  for (var h = 0; h < headers.length; h++) md += ' --- |';
  md += '\n';
  for (var j = 0; j < rows.length; j++) {
    md += '| ' + rows[j].join(' | ') + ' |\n';
  }

  navigator.clipboard.writeText(md).then(function() {
    // Brief visual feedback on the copy button
    var btn = container.closest('.glass-window').querySelector('.copy-table-btn');
    if (btn) {
      btn.classList.add('copied');
      setTimeout(function() { btn.classList.remove('copied'); }, 1200);
    }
  });
}

// Apply resize to terminal, floppy library, and help windows
makeResizable(
  document.getElementById('terminal-window'),
  document.getElementById('terminal-window-resize'),
  'term-size', 500, 350
);
makeResizable(
  document.getElementById('floppy-modal'),
  document.getElementById('floppy-modal-resize'),
  'floppy-browser-size', 500, 350
);
makeResizable(
  document.getElementById('help-window'),
  document.getElementById('help-window-resize'),
  'help-window-size', 400, 300
);
makeResizable(
  document.getElementById('disasm-window'),
  document.getElementById('disasm-window-resize'),
  'disasm-size', 350, 250
);
makeResizable(
  document.getElementById('pdf-handbok-window'),
  document.getElementById('pdf-handbok-resize'),
  'pdf-handbok-size', 480, 360
);
makeResizable(
  document.getElementById('pdf-supervisor-window'),
  document.getElementById('pdf-supervisor-resize'),
  'pdf-supervisor-size', 480, 360
);
// While dragging/resizing a PDF window, disable the iframe's pointer events so
// the mousemove/mouseup reach the document (an iframe otherwise swallows them,
// freezing the drag when the cursor passes over the PDF).
['pdf-handbok', 'pdf-supervisor'].forEach(function(base) {
  var frame = document.getElementById(base + '-frame');
  var header = document.getElementById(base + '-header');
  var handle = document.getElementById(base + '-resize');
  function disableFrame() { if (frame) frame.style.pointerEvents = 'none'; }
  function enableFrame() { if (frame) frame.style.pointerEvents = ''; }
  if (header) header.addEventListener('mousedown', disableFrame);
  if (handle) handle.addEventListener('mousedown', disableFrame);
  document.addEventListener('mouseup', enableFrame);
});
makeResizable(
  document.getElementById('breakpoints-window'),
  document.getElementById('breakpoints-window-resize'),
  'breakpoints-size', 320, 250
);
makeResizable(
  document.getElementById('process-list-window'),
  document.getElementById('process-list-resize'),
  'proc-list-size', 600, 300
);
makeResizable(
  document.getElementById('queue-viewer-window'),
  document.getElementById('queue-viewer-resize'),
  'queue-viewer-size', 500, 300
);
makeResizable(
  document.getElementById('segment-table-window'),
  document.getElementById('segment-table-resize'),
  'segment-table-size', 550, 300
);
makeResizable(
  document.getElementById('reentrant-window'),
  document.getElementById('reentrant-resize'),
  'reentrant-size', 480, 280
);
makeResizable(
  document.getElementById('seg-disasm-window'),
  document.getElementById('seg-disasm-resize'),
  'seg-disasm-size', 500, 350
);
makeResizable(
  document.getElementById('io-devices-window'),
  document.getElementById('io-devices-resize'),
  'io-devices-size', 500, 300
);
makeResizable(
  document.getElementById('page-table-window'),
  document.getElementById('page-table-resize'),
  'page-table-size', 550, 350
);
makeResizable(
  document.getElementById('cpu-load-window'),
  document.getElementById('cpu-load-window-resize'),
  'cpu-load-size', 240, 140
);
makeResizable(
  document.getElementById('smd-manager-window'),
  document.getElementById('smd-manager-resize'),
  'smd-manager-size-2', 560, 420
);

// =========================================================
// Register all windows with the window manager
// =========================================================
windowManager.register('machine-window', 'Machine Info');
windowManager.register('cpu-load-window', 'CPU Load');
windowManager.register('about-window', 'About');
windowManager.register('floppy-modal', 'Floppy Library');
windowManager.register('help-window', 'SINTRAN Help');
windowManager.register('terminal-window', 'Console');
windowManager.register('debugger-window', 'Debugger');
windowManager.register('disasm-window', 'Disassembly');
windowManager.register('breakpoints-window', 'Breakpoints');
windowManager.register('sysinfo-window', 'System Info');
windowManager.register('process-list-window', 'RT Descriptions');
windowManager.register('queue-viewer-window', 'Queues');
windowManager.register('segment-table-window', 'Segments');
windowManager.register('reentrant-window', 'Reentrant');
windowManager.register('seg-disasm-window', 'Disassembler');
windowManager.register('io-devices-window', 'I/O Devices');
windowManager.register('page-table-window', 'Page Tables');
windowManager.register('config-window', 'Config');
windowManager.register('smd-manager-window', 'HDD Manager');
windowManager.register('machine-setup-window', 'Machine Setup');
windowManager.register('gateway-stats-window', 'Gateway');
windowManager.register('pdf-handbok-window', 'Håndbok');
windowManager.register('pdf-supervisor-window', 'System Supervisor');

// Restore window visibility from localStorage
(function() {
  try {
    var state = JSON.parse(localStorage.getItem('window-visibility') || '{}');
    for (var id in state) {
      var win = document.getElementById(id);
      if (!win) continue;
      if (state[id]) {
        win.style.display = 'flex';
        // Lazily load PDF iframes that were restored open
        var frame = win.querySelector('iframe[data-src]');
        if (frame && !frame.getAttribute('src')) {
          frame.setAttribute('src', encodeURI(frame.getAttribute('data-src')));
        }
      } else {
        win.style.display = 'none';
      }
    }
  } catch(e) {}
  // Remove early-restore style tag - toolbar.js now owns all positions/visibility
  var earlyStyle = document.getElementById('early-restore');
  if (earlyStyle) earlyStyle.remove();
})();

// Periodically update taskbar to reflect visible windows
setInterval(function() { windowManager.updateTaskbar(); }, 500);
// Initial update
windowManager.updateTaskbar();

// Populate the toolbar version tag and the About window from version.js
// (generated at build time from the CMake PROJECT_VERSION). When version.js is
// absent - e.g. opening the template directly in dev - the UI shows "(dev)".
(function() {
  var ver = (typeof window.ND100X_VERSION === 'string') ? window.ND100X_VERSION : '';
  var build = (typeof window.ND100X_BUILD === 'string') ? window.ND100X_BUILD : '';
  var tv = document.getElementById('toolbar-version');
  if (tv && ver) tv.textContent = '(v' + ver + ')';
  var av = document.getElementById('about-version');
  if (av) {
    av.innerHTML = ver
      ? ('Version ' + ver + (build ? ' · built ' + build : ''))
      : 'Version (dev build)';
  }
})();
