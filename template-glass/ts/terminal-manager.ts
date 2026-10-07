//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs -- https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// terminal-manager.ts - Main page terminal orchestration.
// Replaces terminal.js. Uses terminal-core.ts for all shared logic.
//
// Manages:
//   - Console terminal (identCode=1) in main window
//   - Floating glass windows for additional terminals
//   - Tab switching, active terminal tracking
//   - Float mode toggle
//   - Terminal submenu updates
//
// Globals preserved for other JS modules:
//   terminals, activeTerminalId, terminalContainers, terminalDisplayNames,
//   initializeTerminals, registerTerminalCallbacks, sendKey,
//   getTerminalSettings, saveTerminalSettings, applySettingsToTerminal,
//   switchTerminalMode, updateTerminalSubmenu

// Is the active machine profile an ND-500 standalone (NDIX) machine
// (ndix-machine.js)? Decides the terminal emulator, the extra tty terminals
// and where keys go. ndix-machine.js loads after this file, so the lookup is
// made when asked, never at load.
function isNdixMachineSelected(): boolean {
  return typeof ndixMachine !== 'undefined' && !!ndixMachine && ndixMachine.isSelected();
}

// The ND-500's ttys 1-3 as terminals 2-4; tty 0 is terminal 1, the console.
function createNdixTtyTerminals(): void {
  if (!isNdixMachineSelected()) return;
  var ttys = ndixMachine.ttyTerminals();
  for (var t = 0; t < ttys.length; t++) {
    createTerminal(ttys[t].identCode, ttys[t].name);
  }
}

// Store all terminals and their containers
var terminalContainers: { [identCode: number]: HTMLElement } = {};
var activeTerminalId = 1;
var isInitialized = false;

// Track floating terminal windows for cleanup
var floatingTerminalWindows: { [identCode: number]: HTMLElement } = {};
var floatingTerminalCount = 0;

// Map identCode -> display name
var terminalDisplayNames: { [identCode: number]: string } = {};

// Defaults
var defaultFontFamily = "monospace";
var defaultColorTheme = "green";

// Check if float mode is enabled (default: true — "Group" toggle OFF)
function isFloatMode(): boolean {
  var val = localStorage.getItem('terminal-float-mode');
  if (val === null) return true;
  return val === 'true';
}

// Check if auto pop-out is enabled (default: false)
function isAutoPopout(): boolean {
  return localStorage.getItem('terminal-auto-popout') === 'true';
}

// ---- Floating terminal window creation ----

function createFloatingTerminalWindow(identCode: number, name: string): HTMLElement {
  var winId = 'float-term-' + identCode;

  var win = document.createElement('div');
  win.id = winId;
  win.className = 'glass-window';
  win.style.display = 'none';
  win.style.width = '820px';
  win.style.height = '520px';
  win.style.minWidth = '400px';
  win.style.minHeight = '250px';
  win.style.flexDirection = 'column';

  // Stagger position using sequential counter
  var offset = floatingTerminalCount * 30;
  floatingTerminalCount++;
  win.style.top = (120 + offset) + 'px';
  win.style.left = (100 + offset) + 'px';

  // Header
  var header = document.createElement('div');
  header.className = 'glass-window-header';
  header.id = winId + '-header';
  var emuLabel = window.getEmulatorTypeLabel();
  var titleText = 'Terminal ' + name + (emuLabel ? ' (' + emuLabel + ')' : '');

  header.innerHTML =
    '<span class="glass-window-title" id="' + winId + '-title">' + titleText + '</span>' +
    '<div class="float-term-header-controls">' +
      '<span class="term-font-size-badge" id="' + winId + '-fontsize">16px</span>' +
      window.buildFontSelectHTML(identCode) +
      window.buildColorSelectHTML(identCode) +
      '<button class="terminal-window-btn float-vk-toggle" id="float-term-' + identCode + '-vk" title="Virtual Keyboard" style="display:none">' +
        '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">' +
        '<rect x="2" y="4" width="20" height="16" rx="2"/>' +
        '<line x1="6" y1="8" x2="6" y2="8"/><line x1="10" y1="8" x2="10" y2="8"/><line x1="14" y1="8" x2="14" y2="8"/><line x1="18" y1="8" x2="18" y2="8"/>' +
        '<line x1="6" y1="12" x2="6" y2="12"/><line x1="10" y1="12" x2="10" y2="12"/><line x1="14" y1="12" x2="14" y2="12"/><line x1="18" y1="12" x2="18" y2="12"/>' +
        '<line x1="8" y1="16" x2="16" y2="16"/>' +
        '</svg>' +
      '</button>' +
      '<button class="terminal-window-btn" id="' + winId + '-popout" title="Pop out to separate window">' +
        '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">' +
        '<polyline points="15,3 21,3 21,9"/>' +
        '<line x1="21" y1="3" x2="14" y2="10"/>' +
        '<path d="M21 14v5a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5"/>' +
        '</svg>' +
      '</button>' +
      '<button class="terminal-window-btn terminal-window-btn-close" id="' + winId + '-close">' +
        '<svg width="12" height="12" viewBox="0 0 12 12" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round">' +
        '<line x1="2" y1="2" x2="10" y2="10"/><line x1="10" y1="2" x2="2" y2="10"/></svg>' +
      '</button>' +
    '</div>';

  // Resize handle
  var resizeHandle = document.createElement('div');
  resizeHandle.className = 'glass-resize-handle';
  resizeHandle.id = winId + '-resize';
  resizeHandle.innerHTML =
    '<svg class="glass-resize-grip" viewBox="0 0 12 12" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round">' +
    '<line x1="11" y1="1" x2="1" y2="11"/><line x1="11" y1="5" x2="5" y2="11"/><line x1="11" y1="9" x2="9" y2="11"/></svg>';

  // Body
  var body = document.createElement('div');
  body.className = 'float-terminal-body';

  // Terminal container
  var container = document.createElement('div');
  container.id = 'terminal-container-' + identCode;
  container.className = 'terminal-container active';
  body.appendChild(container);

  win.appendChild(header);
  win.appendChild(body);
  win.appendChild(resizeHandle);
  document.body.appendChild(win);

  // VK toggle button (floating window)
  var floatVkBtn = document.getElementById('float-term-' + identCode + '-vk');
  if (floatVkBtn) {
    floatVkBtn.addEventListener('click', function() {
      toggleTerminalVK(identCode);
    });
  }

  // Pop-out button
  document.getElementById(winId + '-popout')!.addEventListener('click', function() {
    if (typeof window.popOutTerminal === 'function') {
      window.popOutTerminal(identCode);
    }
  });

  // Close button
  document.getElementById(winId + '-close')!.addEventListener('click', function() {
    closeWindow(winId);
    if (typeof emu !== 'undefined') {
      emu.setTerminalCarrier(1, identCode);
    }
    updateTerminalSubmenu();
  });

  // Font/color selects
  var selects = header.querySelectorAll('.term-header-select');
  var fontSel = selects[0] as HTMLSelectElement;
  var colorSel = selects[1] as HTMLSelectElement;

  fontSel.addEventListener('change', function(e) {
    var s = window.getTerminalSettings(identCode);
    s.fontFamily = (e.target as HTMLSelectElement).value;
    window.saveTerminalSettings();
    applySettingsToTerminal(identCode);
  });

  colorSel.addEventListener('change', function(e) {
    var s = window.getTerminalSettings(identCode);
    s.colorTheme = (e.target as HTMLSelectElement).value;
    window.saveTerminalSettings();
    applySettingsToTerminal(identCode);
  });

  // Hide font dropdown in RetroTerm mode (bitmap fonts only)
  if (retroTermNow()) {
    fontSel.style.display = 'none';
  }

  // Click to focus
  win.addEventListener('mousedown', function() {
    activeTerminalId = identCode;
    var mainFont = document.getElementById('font-family-select') as HTMLSelectElement | null;
    var mainColor = document.getElementById('color-theme-select') as HTMLSelectElement | null;
    var settings = window.getTerminalSettings(identCode);
    if (mainFont) mainFont.value = settings.fontFamily;
    if (mainColor) mainColor.value = settings.colorTheme;
  });

  // Make draggable and resizable (aspect-ratio locked via computeHeight callback)
  makeDraggable(win, header, 'float-term-' + identCode + '-pos');
  makeResizable(win, resizeHandle, 'float-term-' + identCode + '-size', 400, 250,
    function(w: number) { return computeTerminalHeight(identCode, w); });

  // Register with window manager
  windowManager.register(winId, 'Term ' + name);

  // Restore visibility
  try {
    var vis = JSON.parse(localStorage.getItem('window-visibility') || '{}');
    if (vis[winId] === true) {
      win.style.display = 'flex';
    } else {
      win.style.display = 'none';
    }
  } catch(e) {
    win.style.display = 'none';
  }

  floatingTerminalWindows[identCode] = win;
  return container;
}

// Remove all floating terminal windows from DOM
function cleanupFloatingTerminals(): void {
  Object.keys(floatingTerminalWindows).forEach(function(id) {
    var win = floatingTerminalWindows[parseInt(id)];
    if (win && win.parentNode) {
      win.parentNode.removeChild(win);
    }
  });
  floatingTerminalWindows = {};
}

// ---- Terminal creation ----

function createTerminal(identCode: number, name: string): void {
  terminalDisplayNames[identCode] = name;

  var useFloat = (identCode !== 1) && isFloatMode();
  var container: HTMLElement;

  if (useFloat) {
    container = createFloatingTerminalWindow(identCode, name);
  } else {
    container = document.createElement('div');
    container.id = 'terminal-container-' + identCode;
    // Start first container as active so it has layout dimensions when the
    // terminal renderer initialises (bitmap font canvas sizing needs non-zero size).
    container.className = 'terminal-container' + (identCode === 1 ? ' active' : '');
    var tabs = document.querySelector('.terminal-tabs') as HTMLElement;
    if (tabs.nextSibling) {
      tabs.parentNode!.insertBefore(container, tabs.nextSibling);
    } else {
      tabs.parentNode!.appendChild(container);
    }

    // Only create tabs when in grouped (non-float) mode
    if (!isFloatMode()) {
      var tab = document.createElement('div');
      tab.className = 'terminal-tab';
      tab.dataset.terminal = identCode.toString();
      tab.textContent = name;
      document.querySelector('.terminal-tabs')!.appendChild(tab);

      tab.addEventListener('click', function() {
        switchTerminal(identCode);
      });
    }

    container.addEventListener('mousedown', function() {
      activeTerminalId = identCode;
    });
  }

  var settings = window.getTerminalSettings(identCode);

  var fontSizeDisplay = useFloat
    ? document.getElementById('float-term-' + identCode + '-fontsize')
    : document.getElementById('console-fontsize');

  var parentWin = useFloat ? floatingTerminalWindows[identCode]
                           : document.getElementById('terminal-window');

  // Create a wrapper div inside the container for the canvas.
  // VK sits as a sibling below the wrapper so flex layout gives it space.
  var wrapper = document.createElement('div');
  wrapper.className = 'terminal-canvas-wrapper';
  container.appendChild(wrapper);

  // An ND-500 standalone (NDIX) machine drives this terminal with a VT100 -
  // RetroTerm only ships the TDV2200, SINTRAN's terminal - so when that is
  // the selected machine the console is xterm whatever the renderer setting
  // says. Same container, same factory; only the emulator differs.
  var forNdix = isNdixMachineSelected();

  // Create terminal using shared factory from terminal-core
  var inst = window.createScaledTerminal(wrapper, {
    fontFamily: settings.fontFamily,
    colorTheme: settings.colorTheme,
    sizeDisplay: fontSizeDisplay,
    observeResize: parentWin,
    forceXterm: forNdix
  });
  var term = inst.term;
  var fitAddon = inst.fitAddon;
  var resizeTerminal = inst.resizeTerminal;

  terminals[identCode] = {
    term: term,
    fitAddon: fitAddon,
    container: container,
    wrapper: wrapper,
    resizeTerminal: resizeTerminal
  };

  // Create per-terminal VK (RetroTerm only)
  createTerminalVK(identCode, container, term);

  // Apply the selected national keyboard language (input mapper + display
  // variant + VK labels) to this freshly created terminal.
  var kbLang = getCurrentKeyboardLanguage();
  if (window.setTerminalKeyboardLanguage) window.setTerminalKeyboardLanguage(kbLang);
  applyKeyboardLanguageToTerminal(identCode, kbLang);

  console.log('Terminal ' + identCode + ' (' + name + ') created' + (useFloat ? ' [floating]' : ' [tab]'));
  terminalContainers[identCode] = container;

  // Keyboard handler using core factory
  window.setupTerminalKeyHandler(term, function(keyCode: number) {
    sendKey(keyCode);
  }, function() {
    return identCode === activeTerminalId;
  });

  setTimeout(resizeTerminal, 0);

  // Fit console window height to match canvas aspect ratio on first creation
  if (!useFloat && identCode === 1) {
    setTimeout(function() { fitTerminalWindowHeight(); }, 50);
  }

  // Fit floating window height to terminal content (avoid empty space below canvas)
  if (useFloat) {
    setTimeout(function() {
      var floatWin = floatingTerminalWindows[identCode];
      if (floatWin && floatWin.style.display !== 'none') {
        var w = floatWin.getBoundingClientRect().width;
        if (w > 0) {
          floatWin.style.height = computeTerminalHeight(identCode, w) + 'px';
        }
      }
    }, 100);
  }

  // Auto pop-out floating terminals if enabled (skip console terminal)
  if (useFloat && isAutoPopout() && identCode !== 1) {
    setTimeout(function() {
      if (typeof window.popOutTerminal === 'function') {
        window.popOutTerminal(identCode);
      }
    }, 300);
  }
}

// ---- Initialize all available terminals ----

function initializeTerminals(): void {
  cleanupFloatingTerminals();

  if (!emu || !emu.isReady()) {
    (document.querySelector('.terminal-tabs') as HTMLElement).innerHTML = '';
    document.querySelectorAll('#terminal-window-body > .terminal-container').forEach(function(el) { el.remove(); });
    // Dispose what was there, as the branch below does. Removing a container
    // out from under a live xterm leaves its canvas renderer drawing into
    // nothing ("Cannot read properties of undefined (reading
    // 'getRasterizedGlyph')" on every frame) - seen when an NDIX machine
    // re-made terminal 1 in Worker mode, where isReady() is still false.
    Object.keys(terminals).forEach(function(identCode) {
      try { terminals[parseInt(identCode)].term.dispose(); } catch (e) { /* already gone */ }
    });
    terminals = {};
    terminalContainers = {};
    terminalDisplayNames = {};
    floatingTerminalCount = 0;
    createTerminal(1, '1');
    // In Worker mode isReady() stays false until the ND-100 is initialised,
    // which an NDIX machine never is - so its tty terminals are made here
    // as well as below, or Worker mode would have the console alone.
    createNdixTtyTerminals();

    // Hide tab bar in float mode (no tabs to show)
    var earlyTabs = document.querySelector('.terminal-tabs') as HTMLElement;
    if (earlyTabs && isFloatMode()) {
      earlyTabs.style.display = 'none';
    }

    if (terminals[1]) {
      terminals[1].term.focus();
    }

    switchTerminal(1);
    updateTerminalSubmenu();
    updateConsoleTitle();
    return;
  }

  // Remove tab-based containers
  document.querySelectorAll('#terminal-window-body > .terminal-container').forEach(function(el) { el.remove(); });
  (document.querySelector('.terminal-tabs') as HTMLElement).innerHTML = '';

  // Dispose all existing xterm instances
  Object.keys(terminals).forEach(function(identCode) {
    terminals[parseInt(identCode)].term.dispose();
  });
  terminals = {};
  terminalContainers = {};
  terminalDisplayNames = {};
  floatingTerminalCount = 0;

  createTerminal(1, '1');

  if (isNdixMachineSelected()) {
    // isInitialized is the ND-100's flag and stays false for this machine -
    // there is no ND-100 to ask for terminal addresses.
    createNdixTtyTerminals();
  } else if (isInitialized) {
    for (var i = 0; i < 16; i++) {
      var address = emu.getTerminalAddress(i);
      if (address !== -1) {
        var identCode = emu.getTerminalIdentCode(i);
        if (identCode !== -1 && identCode !== 1) {
          var logDev = emu.getTerminalLogicalDevice(i);
          var displayName = (logDev !== -1) ? logDev.toString() : identCode.toString();
          createTerminal(identCode, displayName);
        }
      }
    }
  }

  // In float mode, hide tab bar if only console exists
  var tabs = document.querySelector('.terminal-tabs') as HTMLElement;
  var tabCount = tabs ? tabs.children.length : 0;
  if (isFloatMode() && tabCount <= 1) {
    tabs.style.display = 'none';
  } else {
    tabs.style.display = '';
  }

  // Click anywhere on main terminal window reclaims focus for console
  var termWin = document.getElementById('terminal-window') as any;
  if (termWin && !termWin._floatFocusWired) {
    termWin.addEventListener('mousedown', function() {
      var activeTab = document.querySelector('.terminal-tab.active') as HTMLElement | null;
      if (activeTab) {
        activeTerminalId = parseInt(activeTab.dataset.terminal!);
      } else {
        activeTerminalId = 1;
      }
    });
    termWin._floatFocusWired = true;
  }

  switchTerminal(1);
  updateTerminalSubmenu();
  updateConsoleTitle();
  fitTerminalWindowHeight();
}

// ---- Terminal submenu ----

function updateTerminalSubmenu(): void {
  var submenu = document.getElementById('terminal-submenu');
  if (!submenu) return;

  submenu.innerHTML = '';

  var hasAdditional = false;
  var floatMode = isFloatMode();

  Object.keys(terminals).forEach(function(id) {
    var identCode = parseInt(id);
    if (identCode === 1) return;
    hasAdditional = true;

    var item = document.createElement('button');
    item.className = 'toolbar-menu-item';
    var decName = terminalDisplayNames[identCode] || identCode.toString();

    if (floatMode) {
      var winId = 'float-term-' + identCode;
      var win = document.getElementById(winId);
      var visible = win && win.style.display !== 'none';
      item.innerHTML = '<span class="submenu-check">' + (visible ? '&#10003;' : '') + '</span>Terminal ' + decName;
      item.addEventListener('mousedown', function(e) { e.stopPropagation(); });
      item.addEventListener('click', (function(ic: number) {
        return function(e: Event) {
          e.stopPropagation();
          toggleFloatingTerminal(ic);
          document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
        };
      })(identCode));
    } else {
      item.innerHTML = '<span class="submenu-check">' + (activeTerminalId === identCode ? '&#10003;' : '') + '</span>Terminal ' + decName;
      item.addEventListener('click', (function(ic: number) {
        return function() {
          switchTerminal(ic);
          document.querySelectorAll('.toolbar-menu-container').forEach(function(c) { c.classList.remove('open'); });
        };
      })(identCode));
    }

    submenu.appendChild(item);
  });

  if (!hasAdditional) {
    var empty = document.createElement('div');
    empty.className = 'toolbar-menu-item disabled';
    empty.id = 'terminal-submenu-empty';
    empty.textContent = 'No additional terminals';
    submenu.appendChild(empty);
  }
}

// ---- Toggle floating terminal ----

function toggleFloatingTerminal(identCode: number): void {
  var winId = 'float-term-' + identCode;
  var win = document.getElementById(winId);
  if (!win) return;

  if (win.style.display === 'none') {
    openWindow(winId);
    if (typeof emu !== 'undefined') {
      emu.setTerminalCarrier(0, identCode);
    }
    activeTerminalId = identCode;
    setTimeout(function() {
      windowManager.focus(winId);
      windowManager.updateTaskbar();
      if (terminals[identCode]) {
        // Compute correct height from width to eliminate empty space below terminal
        var w = win!.getBoundingClientRect().width;
        if (w > 0) {
          win!.style.height = computeTerminalHeight(identCode, w) + 'px';
        }
        terminals[identCode].term.focus();
        terminals[identCode].resizeTerminal();
      }
    }, 50);
  } else {
    closeWindow(winId);
    if (typeof emu !== 'undefined') {
      emu.setTerminalCarrier(1, identCode);
    }
  }
  updateTerminalSubmenu();
}

// ---- Mode switching ----

function switchTerminalMode(): void {
  if (!isInitialized) return;
  initializeTerminals();
  registerTerminalCallbacks();
}

// ---- Window resize ----

window.addEventListener('resize', function() {
  Object.values(terminals).forEach(function(terminal: TerminalEntry) {
    terminal.resizeTerminal();
  });
});

// ---- Tab switching ----

function switchTerminal(identCode: number): void {
  document.querySelectorAll('.terminal-tab').forEach(function(tab) {
    (tab as HTMLElement).classList.toggle('active', parseInt((tab as HTMLElement).dataset.terminal!) === identCode);
  });

  Object.keys(terminalContainers).forEach(function(termId) {
    var cont = terminalContainers[parseInt(termId)];
    if (cont.closest('#terminal-window-body')) {
      cont.classList.toggle('active', parseInt(termId) === identCode);
    }
  });

  activeTerminalId = identCode;

  var settings = window.getTerminalSettings(identCode);
  var fontSelect = document.getElementById('font-family-select') as HTMLSelectElement | null;
  var colorSelect = document.getElementById('color-theme-select') as HTMLSelectElement | null;
  if (fontSelect) fontSelect.value = settings.fontFamily;
  if (colorSelect) colorSelect.value = settings.colorTheme;
}

// ---- Register terminal callbacks ----

function registerTerminalCallbacks(): boolean {
  if (!emu) {
    console.error("Emu proxy not ready for terminal callback registration");
    return false;
  }

  try {
    console.log("Setting up direct JS terminal callbacks...");

    if (Object.keys(terminals).length === 0) {
      console.error("No terminals initialized");
      return false;
    }

    console.log('Found ' + Object.keys(terminals).length + ' terminals ready for input/output');

    if (emu.hasJSTerminalHandler()) {
      console.log("Using modern direct JS terminal output handler");
      return true;
    }

    console.log("Using legacy terminal callback approach with function pointers");

    if (!emu.hasAddFunction()) {
      console.error("addFunction not available");
      return false;
    }

    var singleCallback: number;
    try {
      singleCallback = emu.addFunction(function(identCode: number, charCode: number) {
        return handleTerminalOutput(identCode, charCode);
      }, 'iii');

      Object.keys(terminals).forEach(function(identCode) {
        console.log('Registering legacy callback for terminal with identCode ' + identCode);
        if (emu.hasSetTerminalOutputCallback()) {
          emu.setTerminalOutputCallback(parseInt(identCode), singleCallback);
        }
      });

      console.log("Legacy callbacks registered successfully");
      return true;
    } catch (e) {
      console.error("Error registering legacy callbacks:", e);
      return false;
    }
  } catch (e) {
    console.error("Error in registerTerminalCallbacks:", e);
    return false;
  }
}

// ---- Send key ----

function sendKey(keyCode: number): boolean {
  if (!emu) {
    console.error("SendKeyToTerminal not available");
    return false;
  }

  try {
    if (!terminals[activeTerminalId]) {
      console.error('Terminal with identCode ' + activeTerminalId + ' not found');
      return false;
    }

    // A powered-on NDIX machine owns the terminals: keys go to its tty, not
    // to an ND-100 that was never initialised.
    if (isNdixMachineSelected() && ndixMachine.isActive()) {
      return ndixMachine.sendKey(activeTerminalId, keyCode);
    }

    var result = emu.sendKey(activeTerminalId, keyCode);
    if (result !== 1) {
      console.error("Failed to send key to terminal with identCode", activeTerminalId);
      return false;
    }
    return true;
  } catch (e) {
    console.error("Error sending key to terminal:", e);
    return false;
  }
}

// ---- Backend detection ----

// Does RetroTerm draw the active machine's terminals? Asked every time -
// the machine selector changes it without a reload (terminal-core.ts).
function retroTermNow(): boolean {
  return !!(window.isRetroTermBackend && window.isRetroTermBackend());
}

// ---- Apply settings ----

function applySettingsToTerminal(identCode: number): void {
  // If popped out, broadcast settings to pop-out window
  if (typeof window.isPoppedOut === 'function' && window.isPoppedOut(identCode)) {
    if (typeof window.broadcastSettingsChange === 'function') {
      window.broadcastSettingsChange(identCode);
    }
    return;
  }

  var t = terminals[identCode];
  if (!t) return;
  var settings = window.getTerminalSettings(identCode);
  var colorThemes = window.terminalColorThemes;

  // Skip font-related assignments for RetroTerm (bitmap fonts only)
  if (!retroTermNow()) {
    t.term.options.fontFamily = settings.fontFamily;
  }

  t.term.options.theme = Object.assign({}, colorThemes[settings.colorTheme], { background: '#0a0e1c' });
  t.term.refresh(0, t.term.rows - 1);

  setTimeout(function() {
    t.term.options.theme = colorThemes[settings.colorTheme];
    t.resizeTerminal();
  }, 50);
}

function applyAllTerminalSettings(): void {
  Object.keys(terminals).forEach(function(identCode) {
    applySettingsToTerminal(parseInt(identCode));
  });
}

// ---- Per-terminal VirtualKeyboard integration (RetroTerm only) ----

/** Auto-size console window height to fit canvas content.
 *  Computes canvas height from aspect ratio (not current DOM measurement)
 *  to avoid measuring a canvas that flex layout has already squeezed. */
function fitTerminalWindowHeight(): void {
  var win = document.getElementById('terminal-window');
  if (!win) return;
  // Skip when maximized -- flex layout handles everything
  if (win.classList.contains('maximized')) return;
  var header = win.querySelector('.terminal-window-header') as HTMLElement;
  var body = win.querySelector('.terminal-window-body') as HTMLElement;
  if (!header || !body) return;
  var wrapper = body.querySelector('.terminal-canvas-wrapper') as HTMLElement;
  if (!wrapper) return;
  requestAnimationFrame(function() {
    requestAnimationFrame(function() {
      // Compute canvas height from wrapper width + terminal aspect ratio
      // (wrapper width is stable; height may be squeezed by VK flex)
      var contentW = wrapper.clientWidth;
      var canvasRatio = (24 * 16) / (80 * 9); // fallback
      var t = terminals[activeTerminalId];
      if (t && t.term) {
        var renderer = (t.term as any)._renderer || (t.term as any).renderer;
        if (renderer) {
          var cw = renderer.charWidth || 9;
          var ch = renderer.charHeight || 16;
          canvasRatio = (24 * ch) / (80 * cw);
        }
      }
      var canvasH = contentW * canvasRatio;

      var headerH = header.getBoundingClientRect().height;
      var tabsEl = body.querySelector('.terminal-tabs') as HTMLElement;
      var tabsH = tabsEl ? tabsEl.getBoundingClientRect().height : 0;
      // Account for VK if visible (use SVG viewBox ratio, not DOM measurement)
      var vkH = 0;
      var activeContainer = body.querySelector('.terminal-container.active') as HTMLElement;
      if (activeContainer) {
        var vkEl = activeContainer.querySelector('.terminal-vk-container') as HTMLElement;
        if (vkEl && vkEl.style.display !== 'none') {
          var svg = vkEl.querySelector('svg.retroterm-vk') as SVGSVGElement;
          if (svg) {
            var vb = svg.viewBox.baseVal;
            if (vb && vb.width > 0 && vb.height > 0) {
              vkH = contentW * (vb.height / vb.width);
            }
          }
        }
      }
      var padding = 20; // resize handle + borders
      win!.style.height = Math.ceil(headerH + tabsH + canvasH + vkH + padding) + 'px';
    });
  });
}

// DOM key name -> TDV VK code mapping for special keys
var _domSpecialKeyVK: { [key: string]: number } = {
  'Escape': 27, 'Enter': 13, 'Tab': 9,
  'ArrowUp': 38, 'ArrowDown': 40, 'ArrowLeft': 37, 'ArrowRight': 39,
  'Home': 36, 'Delete': 46, 'Insert': 45,
  'PageUp': 33, 'PageDown': 34,
  'F1': 112, 'F2': 113, 'F3': 114, 'F4': 115,
  'F5': 116, 'F6': 117, 'F7': 118, 'F8': 119
};

/** Highlight a VK key from a DOM keyboard event */
function vkHighlightFromDom(vk: any, ev: KeyboardEvent): void {
  // Modifier keys: map by physical position
  switch (ev.code) {
    case 'ShiftLeft': vk.highlightGridKey('B99'); return;
    case 'ShiftRight': vk.highlightGridKey('B11'); return;
    case 'ControlLeft': case 'ControlRight': vk.highlightGridKey('D0'); return;
  }
  // Space bar: direct grid (avoid KPSPACE VK conflict)
  if (ev.key === ' ') { vk.highlightGridKey('A5'); return; }
  // Backspace: direct grid (E13 NEWPARA)
  if (ev.key === 'Backspace') { vk.highlightGridKey('E13'); return; }
  // Special keys by VK code
  var specialVK = _domSpecialKeyVK[ev.key];
  if (specialVK) { vk.highlightKey(specialVK); return; }
  // Letters and digits: ASCII uppercase matches Windows VK codes
  if (ev.key.length === 1) {
    var ch = ev.key.toUpperCase().charCodeAt(0);
    if ((ch >= 65 && ch <= 90) || (ch >= 48 && ch <= 57)) {
      vk.highlightKey(ch);
    }
  }
}

/** Unhighlight a VK key from a DOM keyboard event */
function vkUnhighlightFromDom(vk: any, ev: KeyboardEvent): void {
  switch (ev.code) {
    case 'ShiftLeft': vk.unhighlightGridKey('B99'); return;
    case 'ShiftRight': vk.unhighlightGridKey('B11'); return;
    case 'ControlLeft': case 'ControlRight': vk.unhighlightGridKey('D0'); return;
  }
  if (ev.key === ' ') { vk.unhighlightGridKey('A5'); return; }
  if (ev.key === 'Backspace') { vk.unhighlightGridKey('E13'); return; }
  var specialVK = _domSpecialKeyVK[ev.key];
  if (specialVK) { vk.unhighlightKey(specialVK); return; }
  if (ev.key.length === 1) {
    var ch = ev.key.toUpperCase().charCodeAt(0);
    if ((ch >= 65 && ch <= 90) || (ch >= 48 && ch <= 57)) {
      vk.unhighlightKey(ch);
    }
  }
}

/** Create VK container and instance for a single terminal */
function createTerminalVK(identCode: number, container: HTMLElement, term: any): void {
  if (!retroTermNow() || typeof RetroTerm === 'undefined') return;
  if (typeof RetroTerm.VirtualKeyboard === 'undefined') return;
  // The TDV virtual keyboard attaches to a RetroTerm terminal; an NDIX
  // machine's terminals are xterm (see createTerminal) whatever the backend.
  if (isNdixMachineSelected()) return;

  var vkContainer = document.createElement('div');
  vkContainer.className = 'terminal-vk-container';
  vkContainer.style.display = 'none';
  container.appendChild(vkContainer);

  try {
    var vk = new RetroTerm.VirtualKeyboard(vkContainer);
    var name = terminalDisplayNames[identCode] || identCode.toString();
    vk.attachTerminal(term, 'Terminal ' + name);
    terminals[identCode].vk = vk;
    terminals[identCode].vkContainer = vkContainer;

    // PC keyboard -> VK visual sync (capture phase to see all events)
    container.addEventListener('keydown', function(ev: KeyboardEvent) {
      vkHighlightFromDom(vk, ev);
    }, true);
    container.addEventListener('keyup', function(ev: KeyboardEvent) {
      vkUnhighlightFromDom(vk, ev);
    }, true);

    // Show VK toggle button for console window
    if (identCode === 1 || !isFloatMode()) {
      var vkBtn = document.getElementById('term-vk-toggle');
      if (vkBtn) vkBtn.style.display = '';
    }

    // Show VK toggle button for floating windows
    var floatVkBtn = document.getElementById('float-term-' + identCode + '-vk');
    if (floatVkBtn) floatVkBtn.style.display = '';
  } catch(e) {
    console.warn('VirtualKeyboard init failed for terminal ' + identCode + ':', e);
    vkContainer.remove();
  }
}

// ---- National keyboard language / ISO 646 variant (RetroTerm TDV only) ----
// One control drives three things, live (no reload):
//   1. physical-keyboard input mapping (terminal-core: Unicode -> 7-bit)
//   2. the RetroTerm display variant so the TDV font shows national glyphs
//   3. the on-screen VirtualKeyboard labels

// Maps our language code to TDV2200ISO646Variant (Intl=0, No=1, Se=2, De=3).
function kbLangToVariant(lang: string): number {
  if (lang === 'no') return 1;
  if (lang === 'se') return 2;
  if (lang === 'de') return 3;
  return 0;
}

// RetroTerm VirtualKeyboard LANGUAGE_CODES uses 'sv' for Swedish.
function kbLangToVkCode(lang: string): string {
  if (lang === 'no') return 'no';
  if (lang === 'se') return 'sv';
  if (lang === 'de') return 'de';
  return 'us';
}

// Effective language: national variants only exist on the TDV emulators, so
// VT100 / xterm always resolve to 'off' regardless of the saved value.
function getCurrentKeyboardLanguage(): string {
  var t = window.currentTerminalSettings ? window.currentTerminalSettings() : { emulator: 'tdv2200', language: 'no' };
  if (!retroTermNow() || t.emulator === 'vt100') return 'off';
  return t.language;
}

// Push the language to one terminal: display variant (font) + VK labels.
function applyKeyboardLanguageToTerminal(identCode: number, lang: string): void {
  var t = terminals[identCode];
  if (!t || !t.term) return;
  try {
    var emu: any = (t.term as any).getEmulator ? (t.term as any).getEmulator() : null;
    if (emu && emu.iso646Handler && typeof emu.iso646Handler.setVariant === 'function') {
      emu.iso646Handler.setVariant(kbLangToVariant(lang));
    }
  } catch(e) { /* non-TDV emulator: no national variant */ }
  try {
    if (t.vk && typeof t.vk.setLanguage === 'function') {
      t.vk.setLanguage(kbLangToVkCode(lang));
    }
  } catch(e) {}
}

// Apply to the input mapper + every terminal. Live, no reload.
function applyKeyboardLanguage(lang: string): void {
  if (window.setTerminalKeyboardLanguage) window.setTerminalKeyboardLanguage(lang);
  for (var id in terminals) {
    if (terminals.hasOwnProperty(id)) applyKeyboardLanguageToTerminal(parseInt(id), lang);
  }
}

/** Compute correct window height from width, preserving canvas aspect ratio + VK */
function computeTerminalHeight(identCode: number, windowWidth: number): number {
  var headerH = 39;
  var padding = 20;
  var bodyPadding = 0; // float-terminal-body has no padding
  var contentWidth = windowWidth - bodyPadding;

  // Canvas aspect ratio from RetroTerm renderer
  var t = terminals[identCode];
  var canvasRatio = (24 * 16) / (80 * 9); // fallback ~0.533
  if (t && t.term) {
    var renderer = (t.term as any)._renderer || (t.term as any).renderer;
    if (renderer) {
      var cw = renderer.charWidth || 9;
      var ch = renderer.charHeight || 16;
      var cols = 80, rows = 24;
      canvasRatio = (rows * ch) / (cols * cw);
    }
  }

  var canvasH = contentWidth * canvasRatio;

  // VK height from SVG viewBox aspect ratio (if visible)
  var vkH = 0;
  if (t && t.vkContainer && t.vkContainer.style.display !== 'none') {
    var svg = t.vkContainer.querySelector('svg.retroterm-vk') as SVGSVGElement;
    if (svg) {
      var vb = svg.viewBox.baseVal;
      if (vb && vb.width > 0 && vb.height > 0) {
        vkH = contentWidth * (vb.height / vb.width);
      }
    }
  }

  return Math.ceil(headerH + canvasH + vkH + padding);
}

/** Toggle VK visibility for a terminal.
 *  To prevent flicker, the window height is set BEFORE the VK enters
 *  the flow.  The VK is kept out of flow (display:none) while the
 *  target height is computed from computeTerminalHeight (which knows
 *  the VK SVG aspect ratio without it being in the DOM flow). */
function toggleTerminalVK(identCode: number): void {
  var t = terminals[identCode];
  if (!t || !t.vkContainer) return;
  var showing = t.vkContainer.style.display === 'none';

  // Floating terminal
  var floatWin = floatingTerminalWindows[identCode];
  if (floatWin) {
    if (showing) {
      // Grow window FIRST (VK still hidden), then reveal VK
      if (!floatWin.classList.contains('maximized')) {
        // computeTerminalHeight checks vkContainer.style.display — temporarily
        // pretend it's visible so VK height is included in the calculation
        t.vkContainer.style.display = '';
        var newH = computeTerminalHeight(identCode, floatWin.offsetWidth);
        t.vkContainer.style.display = 'none';
        floatWin.style.height = newH + 'px';
        var storageKey = 'float-term-' + identCode + '-size';
        try { localStorage.setItem(storageKey, JSON.stringify({ width: floatWin.style.width, height: floatWin.style.height })); } catch(e) {}
      }
      // Now show VK — window is already the right size
      requestAnimationFrame(function() {
        t.vkContainer.style.display = '';
        t.resizeTerminal();
      });
    } else {
      t.vkContainer.style.display = 'none';
      if (!floatWin.classList.contains('maximized')) {
        var shrunkH = computeTerminalHeight(identCode, floatWin.offsetWidth);
        floatWin.style.height = shrunkH + 'px';
        var storageKey2 = 'float-term-' + identCode + '-size';
        try { localStorage.setItem(storageKey2, JSON.stringify({ width: floatWin.style.width, height: floatWin.style.height })); } catch(e) {}
      }
      setTimeout(function() { t.resizeTerminal(); }, 0);
    }
    return;
  }

  // Console window
  var consoleWin = document.getElementById('terminal-window');
  if (showing) {
    if (consoleWin && !consoleWin.classList.contains('maximized')) {
      // Pre-compute target height with VK included, set it, THEN show VK
      // so the window grows before VK enters flow (no flicker)
      t.vkContainer.style.display = '';
      var preH = computeTerminalHeight(identCode, consoleWin.offsetWidth);
      t.vkContainer.style.display = 'none';
      consoleWin.style.height = preH + 'px';
    }
    requestAnimationFrame(function() {
      t.vkContainer.style.display = '';
      requestAnimationFrame(function() {
        t.resizeTerminal();
      });
    });
  } else {
    t.vkContainer.style.display = 'none';
    if (consoleWin && consoleWin.classList.contains('maximized')) {
      requestAnimationFrame(function() {
        requestAnimationFrame(function() { t.resizeTerminal(); });
      });
    } else {
      fitTerminalWindowHeight();
    }
  }
}

// ---- Update console title based on emulator type ----

function updateConsoleTitle(): void {
  var titleEl = document.getElementById('terminal-window-title');
  if (!titleEl) return;

  if (isNdixMachineSelected()) {
    // Not SINTRAN's console, and not waiting for an ESC.
    titleEl.textContent = ndixMachine.consoleTitle();
    return;
  }

  var emuLabel = window.getEmulatorTypeLabel();
  if (emuLabel) {
    titleEl.textContent = 'Console (' + emuLabel + ') - Push ESC to wake up SINTRAN';
  }

  // Update floating terminal titles too
  Object.keys(floatingTerminalWindows).forEach(function(id) {
    var identCode = parseInt(id);
    var floatTitleEl = document.getElementById('float-term-' + identCode + '-title');
    if (floatTitleEl) {
      var name = terminalDisplayNames[identCode] || identCode.toString();
      floatTitleEl.textContent = 'Terminal ' + name + (emuLabel ? ' (' + emuLabel + ')' : '');
    }
  });
}

function toggleVirtualKeyboard(): void {
  toggleTerminalVK(activeTerminalId);
}

// ---- Initialize dropdowns ----

function initializeDropdowns(): void {
  var settings = window.getTerminalSettings(activeTerminalId);
  var fontSelect = document.getElementById('font-family-select') as HTMLSelectElement | null;
  var colorSelect = document.getElementById('color-theme-select') as HTMLSelectElement | null;

  if (fontSelect) {
    if (retroTermNow()) {
      // Hide font dropdown when RetroTerm is active (bitmap fonts only)
      fontSelect.style.display = 'none';
    } else {
      fontSelect.value = settings.fontFamily;
    }
  }
  if (colorSelect) colorSelect.value = settings.colorTheme;

  var floatToggle = document.getElementById('config-float-terminals') as HTMLInputElement | null;
  if (floatToggle) {
    floatToggle.checked = isFloatMode();
  }

  // The keyboard language of the active machine (Machine Setup > Terminal).
  applyKeyboardLanguage(getCurrentKeyboardLanguage());

  // VK toggle button (console window)
  var vkBtn = document.getElementById('term-vk-toggle');
  if (vkBtn) {
    vkBtn.addEventListener('click', function() {
      toggleTerminalVK(activeTerminalId);
    });
  }
}

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', initializeDropdowns);
} else {
  initializeDropdowns();
}

// Expose globals for other JS modules
window.activeTerminalId = activeTerminalId;
window.terminalDisplayNames = terminalDisplayNames;
window.applySettingsToTerminal = applySettingsToTerminal;
window.toggleVirtualKeyboard = toggleVirtualKeyboard;
