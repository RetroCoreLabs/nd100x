//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs -- https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// terminal-core.ts - Single source of truth for terminal creation, theming,
// settings, keyboard handling, and font scaling.
// Used by terminal-manager.ts (main page) and terminal-popout-app.ts (pop-out).
//
// Exports (on window):
//   terminalColorThemes      - color theme map
//   createScaledTerminal     - factory: Terminal + addons + auto-scaling
//   fitTerminalScaled        - resize an existing terminal with font scaling
//   terminalFontOptions      - canonical font option list
//   terminalColorOptions     - canonical color option list
//   getTerminalSettings      - per-terminal settings accessor
//   saveTerminalSettings     - persist settings to localStorage
//   loadTerminalSettings     - restore settings from localStorage
//   setupTerminalKeyHandler  - keyboard handler factory (RAW_MODE)
//   buildFontSelectHTML      - build <select> HTML for font dropdown
//   buildColorSelectHTML     - build <select> HTML for color dropdown
//   getOpaqueTheme           - return theme with opaque background (for pop-out)

(function() {
  'use strict';

  var TARGET_COLS = 80;
  var TARGET_ROWS = 24;
  var MIN_FONT = 8;
  var MAX_FONT = 120;

  // ---- National 7-bit (ISO 646) keyboard input mapping ----
  // The physical keyboard delivers composed Unicode letters (e.g. 'ae' = U+00E6),
  // but the ND expects the 7-bit ISO 646 *position* (e.g. '{' = 0x7B). This
  // mirrors the native charset.c: map the national letter a host keyboard
  // produces to its 7-bit code. Host-layout independent - the active language
  // wins, other variants fill in - so a Norwegian keyboard can drive a Swedish
  // or German terminal by key position. Output glyphs are handled by the TDV
  // font via the RetroTerm ISO 646 variant, so only input needs remapping here.
  var nationalKeyTables: { [lang: string]: Array<[number, number]> } = {
    // [unicodeCodePoint, 7bitByte]
    no: [[0x00C6,0x5B],[0x00D8,0x5C],[0x00C5,0x5D],[0x00E6,0x7B],[0x00F8,0x7C],[0x00E5,0x7D]],
    se: [[0x00C4,0x5B],[0x00D6,0x5C],[0x00C5,0x5D],[0x00DC,0x5E],[0x00E4,0x7B],[0x00F6,0x7C],[0x00E5,0x7D],[0x00FC,0x7E]],
    de: [[0x00C4,0x5B],[0x00D6,0x5C],[0x00DC,0x5D],[0x00E4,0x7B],[0x00F6,0x7C],[0x00FC,0x7D],[0x00DF,0x7E]]
  };
  var currentKbLanguage = 'off';   // 'off' | 'no' | 'se' | 'de'

  function setTerminalKeyboardLanguage(lang: string): void {
    currentKbLanguage = (lang === 'no' || lang === 'se' || lang === 'de') ? lang : 'off';
  }

  // Map a Unicode code point to its 7-bit ISO 646 position, or -1 if unmapped.
  function mapNationalKey(cp: number): number {
    if (currentKbLanguage === 'off') return -1;
    var act = nationalKeyTables[currentKbLanguage];
    var i: number;
    for (i = 0; i < act.length; i++) { if (act[i][0] === cp) return act[i][1]; }
    var langs = ['no', 'se', 'de'];
    for (var l = 0; l < langs.length; l++) {
      if (langs[l] === currentKbLanguage) continue;
      var t = nationalKeyTables[langs[l]];
      for (i = 0; i < t.length; i++) { if (t[i][0] === cp) return t[i][1]; }
    }
    return -1;
  }

  // ---- Color themes (ONE definition, used everywhere) ----

  var colorThemes: { [name: string]: ColorTheme } = {
    green:      { background: 'transparent', foreground: '#00FF00', cursor: '#00FF00' },
    amber:      { background: 'transparent', foreground: '#FFBF00', cursor: '#FFBF00' },
    white:      { background: 'transparent', foreground: '#F8F8F8', cursor: '#F8F8F8' },
    blue:       { background: 'transparent', foreground: '#00BFFF', cursor: '#00BFFF' },
    paperwhite: { background: '#f0f0f5',     foreground: '#222222', cursor: '#222222' }
  };

  // ---- Canonical font and color option lists ----

  var fontOptions: FontOption[] = [
    { val: "monospace",                      label: "Monospace" },
    { val: "'VT323', monospace",             label: "VT323" },
    { val: "'Fira Mono', monospace",         label: "Fira Mono" },
    { val: "'IBM Plex Mono', monospace",     label: "IBM Plex" },
    { val: "'Cascadia Mono', monospace",     label: "Cascadia" },
    { val: "'Source Code Pro', monospace",   label: "Source Code" }
  ];

  var colorOptions: ColorOption[] = [
    { val: "green",      label: "Green" },
    { val: "amber",      label: "Amber" },
    { val: "white",      label: "White" },
    { val: "blue",       label: "Blue" },
    { val: "paperwhite", label: "Paper" }
  ];

  // ---- Settings management ----

  var defaultFontFamily = "monospace";
  var defaultColorTheme = "green";

  // Per-terminal settings: { [identCode]: { fontFamily, colorTheme } }
  var terminalSettingsMap: { [identCode: number]: TerminalSettings } = {};

  function getTerminalSettings(identCode: number): TerminalSettings {
    if (!terminalSettingsMap[identCode]) {
      terminalSettingsMap[identCode] = {
        fontFamily: defaultFontFamily,
        colorTheme: defaultColorTheme
      };
    }
    return terminalSettingsMap[identCode];
  }

  function saveTerminalSettings(): void {
    try {
      localStorage.setItem('terminal-settings', JSON.stringify(terminalSettingsMap));
    } catch(e) { /* quota or private browsing */ }
  }

  function loadTerminalSettings(): void {
    try {
      var saved = localStorage.getItem('terminal-settings');
      if (saved) {
        terminalSettingsMap = JSON.parse(saved);
      }
    } catch(e) { /* corrupt data */ }
  }

  // Load on script evaluation
  loadTerminalSettings();

  // ---- DOM measurement helpers ----

  var _measureSpan: HTMLSpanElement | null = null;

  function measureCellWidth(fontFamily: string, fontSize: number): number {
    if (!_measureSpan) {
      _measureSpan = document.createElement('span');
      _measureSpan.style.position = 'absolute';
      _measureSpan.style.visibility = 'hidden';
      _measureSpan.style.whiteSpace = 'pre';
      _measureSpan.style.lineHeight = 'normal';
      _measureSpan.style.fontVariant = 'none';
      _measureSpan.textContent = 'WWWWWWWWWW';
      document.body.appendChild(_measureSpan);
    }
    _measureSpan.style.fontFamily = fontFamily;
    _measureSpan.style.fontSize = fontSize + 'px';
    return _measureSpan.getBoundingClientRect().width / 10;
  }

  function measureLineHeight(fontFamily: string, fontSize: number): number {
    if (!_measureSpan) measureCellWidth(fontFamily, fontSize);
    _measureSpan!.style.fontFamily = fontFamily;
    _measureSpan!.style.fontSize = fontSize + 'px';
    return _measureSpan!.getBoundingClientRect().height;
  }

  // ---- Backend detection ----

  // The active machine's terminal settings (Machine Setup > Terminal): which
  // renderer draws the terminals, which terminal it emulates, which national
  // keyboard. machine-profiles.js keeps them per machine; where it is not
  // loaded (the pop-out page) the keys it mirrors for the active machine
  // answer instead. Asked every time, never cached: the machine selector
  // changes them without a reload.
  function currentTerminalSettings(): { backend: string; emulator: string; language: string } {
    var mp: any = (window as any).machineProfiles;
    if (mp && typeof mp.terminal === 'function') return mp.terminal();
    var backend = 'retroterm', emulator = 'tdv2200', language = 'no';
    try {
      backend  = localStorage.getItem('nd100x-terminal-backend') || backend;
      emulator = localStorage.getItem('nd100x-emulator-type') || emulator;
      language = localStorage.getItem('nd100x-keyboard-language') || language;
    } catch (e) {}
    return { backend: backend, emulator: emulator, language: language };
  }

  // RetroTerm draws the active machine's terminals (and is loaded).
  function isRetroTermBackend(): boolean {
    return currentTerminalSettings().backend === 'retroterm' && typeof RetroTerm !== 'undefined';
  }

  // ---- Terminal factory ----

  /**
   * Create a Terminal with FitAddon and auto font-scaling.
   * Backend-aware: xterm.js or RetroTerm, as the active machine's terminal
   * settings say (currentTerminalSettings, Machine Setup > Terminal).
   *
   * @param container   DOM element to host the terminal
   * @param opts        options
   * @returns { term, fitAddon, resizeTerminal }
   */
  function createScaledTerminal(container: HTMLElement, opts?: any) {
    opts = opts || {};
    var fontFamily = opts.fontFamily || 'monospace';
    var theme = colorThemes[opts.colorTheme] || colorThemes.green;
    var sizeDisplay: HTMLElement | null = opts.sizeDisplay || null;

    var term: Terminal;
    var fitAddon: any;

    // opts.forceXterm: this caller needs a VT100 and will not take the TDV.
    // RetroTerm ships one emulator (the TDV2200) and the ND-500's NDIX console
    // speaks ANSI/VT100, so that window asks for xterm outright rather than
    // following the ND-100's backend setting.
    if (isRetroTermBackend() && !opts.forceXterm) {
      // RetroTerm path
      var emulatorType = currentTerminalSettings().emulator;

      term = new RetroTerm.Terminal({
        cursorBlink: true,
        rows: TARGET_ROWS,
        cols: TARGET_COLS,
        theme: theme,
        emulatorType: emulatorType
      });

      fitAddon = new RetroTerm.FitAddon();
      term.loadAddon(fitAddon);
      term.open(container);

      function resizeRetro() {
        fitTerminalRetroTerm(term, fitAddon, sizeDisplay);
      }

      requestAnimationFrame(function() {
        requestAnimationFrame(function() {
          resizeRetro();
        });
      });

      var observeTarget: HTMLElement | null = opts.observeResize || null;
      if (observeTarget) {
        var observer = new ResizeObserver(function() { resizeRetro(); });
        observer.observe(observeTarget);
      }

      return { term: term, fitAddon: fitAddon, resizeTerminal: resizeRetro };
    }

    // xterm.js path (default)
    term = new Terminal({
      cursorBlink: true,
      fontSize: 16,
      fontFamily: fontFamily,
      rows: TARGET_ROWS,
      cols: TARGET_COLS,
      theme: theme
    });

    fitAddon = new FitAddon.FitAddon();
    term.loadAddon(fitAddon);

    // Canvas renderer: precise character positioning (no DOM cell gaps at large fonts)
    if (typeof CanvasAddon !== 'undefined' && CanvasAddon!.CanvasAddon) {
      try { term.loadAddon(new CanvasAddon!.CanvasAddon()); } catch(e) { /* DOM fallback */ }
    }

    term.open(container);
    fitAddon.fit();

    function resizeTerminal() {
      fitTerminalScaled(term, fitAddon, sizeDisplay);
    }

    // Defer initial font scaling so xterm has rendered cell metrics
    setTimeout(resizeTerminal, 50);

    // Observe container or parent for size changes
    var observeXterm: HTMLElement | null = opts.observeResize || null;
    if (observeXterm) {
      var obs = new ResizeObserver(function() { resizeTerminal(); });
      obs.observe(observeXterm);
    }

    return { term: term, fitAddon: fitAddon, resizeTerminal: resizeTerminal };
  }

  // ---- Font scaling ----

  /**
   * Scale terminal font so ~80x24 fills the available space, then fit.
   *
   * Phase 1 - Fast binary search using DOM span measurement.
   * Phase 2 - Verify with xterm's fitAddon.proposeDimensions() and correct
   *           downward if xterm's cell metrics disagree.
   * Phase 3 - After fit, verify the rendered screen doesn't overflow the
   *           container (catches subpixel rounding that clips the last row).
   *
   * CSS zoom handling:
   *   Under CSS zoom, xterm's proposeDimensions() mixes unzoomed container
   *   dimensions (from getComputedStyle) with zoomed cell metrics, producing
   *   inflated col/row counts. Phase 2 verification is skipped when zoom is
   *   detected. fitAddon.fit() still fills the space correctly despite the
   *   coordinate mismatch because the font size (from Phase 1) is correct.
   */
  function fitTerminalScaled(term: Terminal, fitAddon: any, sizeDisplay?: HTMLElement | null): void {
    var el = term.element;
    if (!el || !el.parentElement) {
      fitAddon.fit();
      return;
    }

    // getBoundingClientRect returns actual visible (zoomed) pixels.
    // clientWidth/clientHeight returns CSS pixels (ignores zoom).
    var parentRect = el.parentElement.getBoundingClientRect();
    var containerW = parentRect.width;
    var containerH = parentRect.height;
    if (containerW < 20 || containerH < 20) {
      fitAddon.fit();
      return;
    }

    // Detect CSS zoom: ratio of visible pixels to CSS pixels
    var clientW = el.parentElement.clientWidth;
    var zoomRatio = (clientW > 0) ? containerW / clientW : 1;
    var hasZoom = Math.abs(zoomRatio - 1) > 0.01;

    // Detect zoom mismatch: container may be counter-zoomed (effective zoom ~1.0)
    // while the measurement span in document.body is at page zoom.
    // When this happens, span BCR needs scaling to match container BCR coordinates.
    var pageZoom = parseFloat(document.documentElement.style.zoom as any) || 1;
    var spanScale = 1;
    if (!hasZoom && Math.abs(pageZoom - 1) > 0.01) {
      // Container is counter-zoomed (BCR/clientWidth ≈ 1) but page has zoom.
      // Span measurements (BCR) are at pageZoom; scale to native resolution.
      spanScale = 1 / pageZoom;
    }

    var fontFamily = term.options.fontFamily || 'monospace';

    // Phase 1: Binary search using DOM span measurement (fast estimate).
    var low = MIN_FONT;
    var high = Math.min(MAX_FONT, Math.floor(containerW / 2));
    var bestSize = MIN_FONT;

    while (low <= high) {
      var mid = Math.floor((low + high) / 2);
      var cellW = measureCellWidth(fontFamily, mid) * spanScale;
      var lineH = measureLineHeight(fontFamily, mid) * spanScale;
      var cols = Math.floor(containerW / cellW);
      var rows = Math.floor(containerH / lineH);

      if (cols >= TARGET_COLS && rows >= TARGET_ROWS) {
        bestSize = mid;
        low = mid + 1;
      } else {
        high = mid - 1;
      }
    }

    // Phase 2: Verify with xterm's proposeDimensions (only reliable without zoom).
    if (!hasZoom && typeof fitAddon.proposeDimensions === 'function') {
      term.options.fontSize = bestSize;
      void (el as any).offsetHeight;
      var proposed = fitAddon.proposeDimensions();
      var corrections = 0;
      while (proposed &&
             (proposed.cols < TARGET_COLS || proposed.rows < TARGET_ROWS) &&
             bestSize > MIN_FONT && corrections < 20) {
        bestSize--;
        corrections++;
        term.options.fontSize = bestSize;
        void (el as any).offsetHeight;
        proposed = fitAddon.proposeDimensions();
      }
    }

    // Compute correct cols/rows from binary search measurements (zoom-safe)
    var bsCellW = measureCellWidth(fontFamily, bestSize) * spanScale;
    var bsLineH = measureLineHeight(fontFamily, bestSize) * spanScale;
    var bsCols = Math.floor(containerW / bsCellW);
    var bsRows = Math.min(Math.floor(containerH / bsLineH), TARGET_ROWS);

    // Apply final font size
    term.options.fontSize = bestSize;
    if (typeof term.clearTextureAtlas === 'function') {
      term.clearTextureAtlas!();
    }
    void (el as any).offsetHeight;

    fitAddon.fit();

    // Phase 3: Post-fit clipping check.
    var screen = el.querySelector('.xterm-screen') as HTMLElement | null;
    if (screen) {
      var screenH = screen.getBoundingClientRect().height;
      if (screenH > containerH + 1 && bestSize > MIN_FONT) {
        bestSize--;
        term.options.fontSize = bestSize;
        if (typeof term.clearTextureAtlas === 'function') {
          term.clearTextureAtlas!();
        }
        void (el as any).offsetHeight;
        fitAddon.fit();
        // Recompute after font change
        bsCellW = measureCellWidth(fontFamily, bestSize) * spanScale;
        bsLineH = measureLineHeight(fontFamily, bestSize) * spanScale;
        bsCols = Math.floor(containerW / bsCellW);
        bsRows = Math.min(Math.floor(containerH / bsLineH), TARGET_ROWS);
      }
    }

    // Under CSS zoom, fitAddon.fit() uses unzoomed clientWidth which inflates
    // cols/rows. Force the zoom-correct dimensions from our binary search.
    if (hasZoom || term.rows > TARGET_ROWS || term.cols !== bsCols) {
      term.resize(bsCols, bsRows);
    }

    // Shrink xterm element to match content so glass bg shows through
    var xtermScreen = el.querySelector('.xterm-screen') as HTMLElement | null;
    if (xtermScreen) {
      var renderedH = xtermScreen.getBoundingClientRect().height;
      if (renderedH < containerH - 5) {
        el.style.height = renderedH + 'px';
      } else {
        el.style.height = '';
      }
    }

    term.refresh(0, term.rows - 1);

    if (sizeDisplay) {
      sizeDisplay.style.display = '';
      sizeDisplay.textContent = bestSize + 'px';
    }
  }

  // ---- RetroTerm fit (bitmap fonts — no font scaling needed) ----

  function fitTerminalRetroTerm(term: Terminal, fitAddon: any, sizeDisplay?: HTMLElement | null): void {
    // For bitmap-font terminals, keep fixed 80x24 dimensions.
    // The host system (SINTRAN) expects exactly 80 cols and 24 rows.
    // The CanvasRenderer's _fitCanvasToContainer scales the canvas CSS to
    // fit the container while preserving aspect ratio.
    term.resize(TARGET_COLS, TARGET_ROWS);
    term.refresh(0, term.rows - 1);
    if (sizeDisplay) {
      sizeDisplay.style.display = 'none';
    }
  }

  // ---- Keyboard handler factory ----

  /**
   * Set up RAW_MODE keyboard handling on a terminal.
   * The caller provides a sendCallback to route key codes
   * (main page: emu.sendKey wrapper, pop-out: channel.postMessage).
   * Optional isActiveCheck returns false to suppress input (e.g. wrong terminal focused).
   */
  function setupTerminalKeyHandler(
    term: Terminal,
    sendCallback: (keyCode: number) => void,
    isActiveCheck?: () => boolean
  ): void {
    term.onKey(function(ev: { key: string; domEvent: KeyboardEvent }) {
      var key = ev.key;
      var domEvent = ev.domEvent;
      if (isActiveCheck && !isActiveCheck()) return;
      var charCode = key.charCodeAt(0);

      // Ctrl+key: convert to control code
      if (domEvent.ctrlKey && charCode >= 65 && charCode <= 90) {
        var ctrlCode = charCode - 64;
        sendCallback(ctrlCode);
      } else if (charCode === 10) {
        sendCallback(13);
      } else {
        // Send all bytes of the sequence (escape sequences are multi-byte).
        // National letters are remapped to their 7-bit ISO 646 position when a
        // keyboard language is active; everything else passes through verbatim.
        for (var i = 0; i < key.length; i++) {
          var cc = key.charCodeAt(i);
          var mapped = mapNationalKey(cc);
          sendCallback(mapped >= 0 ? mapped : cc);
        }
      }

    });
  }

  // ---- Select HTML builders ----

  function buildFontSelectHTML(identCode: number): string {
    var settings = getTerminalSettings(identCode);
    var html = '<select class="term-header-select" title="Font">';
    fontOptions.forEach(function(o) {
      var sel = (o.val === settings.fontFamily) ? ' selected' : '';
      html += '<option value="' + o.val + '"' + sel + '>' + o.label + '</option>';
    });
    html += '</select>';
    return html;
  }

  function buildColorSelectHTML(identCode: number): string {
    var settings = getTerminalSettings(identCode);
    var html = '<select class="term-header-select" title="Color">';
    colorOptions.forEach(function(o) {
      var sel = (o.val === settings.colorTheme) ? ' selected' : '';
      html += '<option value="' + o.val + '"' + sel + '>' + o.label + '</option>';
    });
    html += '</select>';
    return html;
  }

  // ---- Opaque theme helper ----

  /**
   * Return a copy of a color theme with opaque background.
   * Pop-out windows need this since there's no glass parent behind them.
   */
  function getOpaqueTheme(themeName: string): ColorTheme {
    var theme = colorThemes[themeName] || colorThemes.green;
    var copy: ColorTheme = { background: theme.background, foreground: theme.foreground, cursor: theme.cursor };
    if (copy.background === 'transparent') {
      copy.background = '#0a0e1c';
    }
    return copy;
  }

  // ---- Emulator type label ----

  function getEmulatorTypeLabel(): string {
    if (!isRetroTermBackend()) return 'VT100';
    var saved = currentTerminalSettings().emulator;
    if (saved === 'tdv2200') return 'TDV 2200';
    if (saved === 'tdv2215') return 'TDV 2215';
    if (saved === 'vt100') return 'VT100';
    return '';
  }

  // ---- Exports ----

  window.getEmulatorTypeLabel = getEmulatorTypeLabel;
  window.currentTerminalSettings = currentTerminalSettings;
  window.isRetroTermBackend = isRetroTermBackend;
  window.terminalColorThemes = colorThemes;
  window.createScaledTerminal = createScaledTerminal;
  window.fitTerminalScaled = fitTerminalScaled;
  window.fitTerminalRetroTerm = fitTerminalRetroTerm;
  window.terminalFontOptions = fontOptions;
  window.terminalColorOptions = colorOptions;
  window.getTerminalSettings = getTerminalSettings;
  window.saveTerminalSettings = saveTerminalSettings;
  window.loadTerminalSettings = loadTerminalSettings;
  window.setupTerminalKeyHandler = setupTerminalKeyHandler;
  window.setTerminalKeyboardLanguage = setTerminalKeyboardLanguage;
  window.buildFontSelectHTML = buildFontSelectHTML;
  window.buildColorSelectHTML = buildColorSelectHTML;
  window.getOpaqueTheme = getOpaqueTheme;

  // Expose the settings map so terminal-manager can access it
  window.terminalSettings = terminalSettingsMap;

})();
