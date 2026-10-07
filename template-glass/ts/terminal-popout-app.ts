//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs -- https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// terminal-popout-app.ts - Pop-out terminal window application.
// Replaces the ~240 lines of inline JS in terminal-popout.html.
// Uses terminal-core.ts for factory, keyboard, settings, and theme logic.

(function() {
  'use strict';

  // Parse URL params
  var params = new URLSearchParams(window.location.search);
  var identCode = parseInt(params.get('id') || '0') || 0;
  var displayName = params.get('name') || identCode.toString();

  if (!identCode) {
    document.body.innerHTML = '<p style="color:#ff4444;padding:20px;">Error: No terminal ID specified</p>';
    return;
  }

  var emuLabel = window.getEmulatorTypeLabel ? window.getEmulatorTypeLabel() : '';
  var titleSuffix = emuLabel ? ' (' + emuLabel + ')' : '';
  document.title = 'Terminal ' + displayName + titleSuffix + ' - ND100X';
  document.getElementById('popout-title')!.textContent = 'Terminal ' + displayName + titleSuffix;

  // Apply saved theme
  var theme = 'dark';
  try {
    var savedTheme = localStorage.getItem('nd100x-theme');
    if (savedTheme) theme = savedTheme;
  } catch(e) {}
  if (theme !== 'dark') {
    document.body.setAttribute('data-theme', theme);
  }

  // Load terminal settings via core
  var settings = window.getTerminalSettings(identCode);
  var fontFamily = settings.fontFamily;
  var colorTheme = settings.colorTheme;

  // Populate font/color selects from canonical option lists
  var fontSelect = document.getElementById('popout-font-select') as HTMLSelectElement;
  var colorSelect = document.getElementById('popout-color-select') as HTMLSelectElement;

  // Build font select options from canonical list
  fontSelect.innerHTML = '';
  window.terminalFontOptions.forEach(function(o: FontOption) {
    var opt = document.createElement('option');
    opt.value = o.val;
    opt.textContent = o.label;
    if (o.val === fontFamily) opt.selected = true;
    fontSelect.appendChild(opt);
  });

  // Build color select options from canonical list
  colorSelect.innerHTML = '';
  window.terminalColorOptions.forEach(function(o: ColorOption) {
    var opt = document.createElement('option');
    opt.value = o.val;
    opt.textContent = o.label;
    if (o.val === colorTheme) opt.selected = true;
    colorSelect.appendChild(opt);
  });

  // Create terminal using shared factory from terminal-core
  var container = document.getElementById('popout-terminal')!;
  var inst = window.createScaledTerminal(container, {
    fontFamily: fontFamily,
    colorTheme: colorTheme,
    sizeDisplay: document.getElementById('popout-fontsize')
  });
  var term = inst.term;

  // Override theme to use opaque background for pop-out
  term.options.theme = window.getOpaqueTheme(colorTheme);

  // Debounced resize
  var resizeTimeout: number | null = null;
  function resizeTerminal() {
    inst.resizeTerminal();
  }

  window.addEventListener('resize', function() {
    if (resizeTimeout) clearTimeout(resizeTimeout);
    resizeTimeout = window.setTimeout(resizeTerminal, 50);
  });

  // Open BroadcastChannel
  var channelName = 'nd100x-term-' + identCode;
  var channel = new BroadcastChannel(channelName);

  // Hide font select for RetroTerm (bitmap fonts only)
  var isRetroTerm = (typeof TERMINAL_BACKEND !== 'undefined' && TERMINAL_BACKEND === 'retroterm');
  if (isRetroTerm) {
    fontSelect.style.display = 'none';
  }

  // Apply font/color locally and save + notify main tab
  function applySettings(font: string, color: string): void {
    fontFamily = font;
    colorTheme = color;
    if (!isRetroTerm) {
      term.options.fontFamily = font;
    }
    term.options.theme = window.getOpaqueTheme(color);
    setTimeout(resizeTerminal, 50);

    // Save to localStorage
    try {
      var all = JSON.parse(localStorage.getItem('terminal-settings') || '{}');
      if (!all[identCode]) all[identCode] = {};
      all[identCode].fontFamily = font;
      all[identCode].colorTheme = color;
      localStorage.setItem('terminal-settings', JSON.stringify(all));
    } catch(e) {}

    // Notify main tab
    channel.postMessage({
      type: 'settings-changed-from-popout',
      fontFamily: font,
      colorTheme: color
    });
  }

  // Font/color select handlers
  fontSelect.addEventListener('change', function() {
    applySettings(fontSelect.value, colorSelect.value);
  });
  colorSelect.addEventListener('change', function() {
    applySettings(fontSelect.value, colorSelect.value);
  });

  // Maximize toggle
  var isMaximized = false;
  var savedBounds: { left: number; top: number; width: number; height: number } | null = null;
  var maxBtn = document.getElementById('popout-maximize')!;
  var maxIcon = document.getElementById('popout-maximize-icon')!;
  var expandSVG = '<polyline points="9,1 13,1 13,5"/><line x1="13" y1="1" x2="8" y2="6"/><polyline points="5,13 1,13 1,9"/><line x1="1" y1="13" x2="6" y2="8"/>';
  var collapseSVG = '<polyline points="5,1 1,1 1,5"/><line x1="1" y1="1" x2="6" y2="6"/><polyline points="9,13 13,13 13,9"/><line x1="13" y1="13" x2="8" y2="8"/>';

  maxBtn.addEventListener('click', function() {
    if (!isMaximized) {
      savedBounds = {
        left: window.screenX,
        top: window.screenY,
        width: window.outerWidth,
        height: window.outerHeight
      };
      window.moveTo((screen as any).availLeft || 0, (screen as any).availTop || 0);
      window.resizeTo(screen.availWidth, screen.availHeight);
      maxIcon.innerHTML = collapseSVG;
      maxBtn.title = 'Restore size';
      isMaximized = true;
    } else {
      if (savedBounds) {
        window.moveTo(savedBounds.left, savedBounds.top);
        window.resizeTo(savedBounds.width, savedBounds.height);
      }
      maxIcon.innerHTML = expandSVG;
      maxBtn.title = 'Maximize';
      isMaximized = false;
    }
    setTimeout(resizeTerminal, 100);
  });

  // Pop-back / restore button
  document.getElementById('popout-restore')!.addEventListener('click', function() {
    channel.postMessage({ type: 'popout-closed' });
    channel.close();
    window.close();
  });

  // Handle messages from main tab
  channel.onmessage = function(ev: MessageEvent) {
    var msg = ev.data;
    if (!msg || !msg.type) return;

    switch (msg.type) {
      case 'output':
        if (msg.chars && msg.chars.length > 0) {
          var str = '';
          for (var i = 0; i < msg.chars.length; i++) {
            str += String.fromCharCode(msg.chars[i]);
          }
          term.write(str);
        }
        break;

      case 'settings-changed':
        if (msg.fontFamily) {
          fontFamily = msg.fontFamily;
          fontSelect.value = msg.fontFamily;
          if (!isRetroTerm) {
            term.options.fontFamily = msg.fontFamily;
          }
        }
        if (msg.colorTheme) {
          colorTheme = msg.colorTheme;
          colorSelect.value = msg.colorTheme;
          term.options.theme = window.getOpaqueTheme(msg.colorTheme);
        }
        setTimeout(resizeTerminal, 50);
        break;

      case 'theme-changed':
        if (msg.theme && msg.theme !== 'dark') {
          document.body.setAttribute('data-theme', msg.theme);
        } else {
          document.body.removeAttribute('data-theme');
        }
        break;

      case 'main-closing':
        document.getElementById('disconnected-overlay')!.classList.add('visible');
        break;
    }
  };

  // Virtual keyboard for RetroTerm popouts
  if (isRetroTerm && typeof RetroTerm.VirtualKeyboard !== 'undefined') {
    var vkContainer = document.getElementById('popout-vk-container');
    if (vkContainer) {
      try {
        var vk = new RetroTerm.VirtualKeyboard(vkContainer);
        vk.attachTerminal(term, 'Terminal ' + displayName);
        var vkBtn = document.getElementById('popout-vk-toggle');
        if (vkBtn) {
          vkBtn.style.display = '';
          vkBtn.addEventListener('click', function() {
            vkContainer!.style.display = vkContainer!.style.display === 'none' ? '' : 'none';
          });
        }
      } catch(e) {
        console.warn('VirtualKeyboard init failed:', e);
      }
    }
  }

  // Keyboard input via core factory - sends to main tab via channel
  window.setupTerminalKeyHandler(term, function(keyCode: number) {
    channel.postMessage({ type: 'key', keyCode: keyCode });
  });

  // Notify main tab that we're ready
  channel.postMessage({ type: 'popout-ready' });

  // Notify main tab on close
  window.addEventListener('beforeunload', function() {
    channel.postMessage({ type: 'popout-closed' });
    channel.close();
  });

  // Focus terminal
  term.focus();
})();
