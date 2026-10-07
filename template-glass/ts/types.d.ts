//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// HackerCorp Labs -- https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// Ambient type declarations for the Glass UI terminal modules.
// These describe globals provided by xterm.js CDN, the emu proxy layer,
// and other Glass JS modules so TypeScript can check cross-file references
// without import/export (module: "none" build).

// ---- xterm.js (loaded from CDN) ----

declare class Terminal {
  constructor(opts?: any);
  options: any;
  element: HTMLElement;
  rows: number;
  cols: number;
  loadAddon(addon: any): void;
  open(container: HTMLElement): void;
  write(data: string): void;
  refresh(start: number, end: number): void;
  focus(): void;
  dispose(): void;
  resize(cols: number, rows: number): void;
  onKey(handler: (ev: { key: string; domEvent: KeyboardEvent }) => void): void;
  clearTextureAtlas?(): void;
}

declare var FitAddon: {
  FitAddon: new () => {
    fit(): void;
    proposeDimensions?(): { cols: number; rows: number } | undefined;
  };
};

declare var CanvasAddon: {
  CanvasAddon: new () => any;
} | undefined;

// ---- RetroTerm (IIFE bundle, global RetroTerm) ----

declare var RetroTerm: {
  Terminal: new (opts?: any) => Terminal;
  FitAddon: new () => {
    activate(terminal: Terminal): void;
    fit(): void;
    proposeDimensions?(): { cols: number; rows: number } | undefined;
    dispose(): void;
  };
  VirtualKeyboard: new (container: HTMLElement) => any;
} | undefined;

declare var TERMINAL_BACKEND: string;

// ---- Emu proxy (emu-proxy.js / emu-proxy-worker.js) ----

interface EmuProxy {
  isReady(): boolean;
  sendKey(identCode: number, keyCode: number): number;
  getTerminalAddress(index: number): number;
  getTerminalIdentCode(index: number): number;
  getTerminalLogicalDevice(index: number): number;
  setTerminalCarrier(state: number, identCode: number): void;
  hasJSTerminalHandler(): boolean;
  hasAddFunction(): boolean;
  addFunction(fn: Function, sig: string): number;
  hasSetTerminalOutputCallback(): boolean;
  setTerminalOutputCallback(identCode: number, cb: number): void;
  hasRingBuffer?(): boolean;
  enableRingBuffer?(): boolean;
}

declare var emu: EmuProxy;

// ---- Shared terminal types ----

interface TerminalEntry {
  term: Terminal;
  fitAddon: any;
  container: HTMLElement;
  wrapper?: HTMLElement;
  resizeTerminal: () => void;
  vk?: any;
  vkContainer?: HTMLElement;
}

interface ColorTheme {
  background: string;
  foreground: string;
  cursor: string;
}

interface TerminalSettings {
  fontFamily: string;
  colorTheme: string;
}

interface FontOption {
  val: string;
  label: string;
}

interface ColorOption {
  val: string;
  label: string;
}

// ---- Globals from module-init.js ----

declare var terminals: { [identCode: number]: TerminalEntry };
declare var loadingOverlayVisible: boolean;
declare var hasReceivedTerminalOutput: boolean;
declare var hasEverStartedEmulation: boolean;
declare function handleTerminalOutput(identCode: number, charCode: number): number;

// ---- Globals from ndix-machine.js (loads after the terminal modules) ----

declare var ndixMachine: {
  isSelected(): boolean;
  isActive(): boolean;
  isBooted(): boolean;
  ttyTerminals(): { identCode: number; name: string }[];
  sendKey(identCode: number, keyCode: number): boolean;
  consoleTitle(): string;
  powerOn(): Promise<boolean>;
  stop(): void;
} | undefined;

// ---- Globals from toolbar.js ----

declare function makeDraggable(win: HTMLElement, header: HTMLElement, storageKey: string): void;
declare function makeResizable(win: HTMLElement, handle: HTMLElement, storageKey: string, minW: number, minH: number, computeHeight?: (width: number) => number): void;
declare function openWindow(winId: string): void;
declare function closeWindow(winId: string): void;

// ---- Window manager (toolbar.js) ----

declare var windowManager: {
  register(id: string, label: string): void;
  focus(id: string): void;
  updateTaskbar(): void;
};

// ---- Globals exported by terminal-core.ts ----

interface Window {
  terminalColorThemes: { [name: string]: ColorTheme };
  createScaledTerminal: (container: HTMLElement, opts?: any) => {
    term: Terminal;
    fitAddon: any;
    resizeTerminal: () => void;
  };
  fitTerminalScaled: (term: Terminal, fitAddon: any, sizeDisplay?: HTMLElement) => void;
  terminalFontOptions: FontOption[];
  terminalColorOptions: ColorOption[];
  getTerminalSettings: (identCode: number) => TerminalSettings;
  saveTerminalSettings: () => void;
  loadTerminalSettings: () => void;
  setupTerminalKeyHandler: (term: Terminal, sendCallback: (keyCode: number) => void, isActiveCheck?: () => boolean) => void;
  setTerminalKeyboardLanguage: (lang: string) => void;
  buildFontSelectHTML: (identCode: number) => string;
  buildColorSelectHTML: (identCode: number) => string;
  getOpaqueTheme: (themeName: string) => ColorTheme;
  getEmulatorTypeLabel: () => string;
  currentTerminalSettings: () => { backend: string; emulator: string; language: string };
  isRetroTermBackend: () => boolean;

  // terminal-manager.ts globals
  activeTerminalId: number;
  terminalDisplayNames: { [identCode: number]: string };
  terminalSettings: { [identCode: number]: TerminalSettings };
  applySettingsToTerminal: (identCode: number) => void;

  // terminal-manager.ts RetroTerm globals
  virtualKeyboard: any;
  toggleVirtualKeyboard: () => void;
  fitTerminalRetroTerm: (term: Terminal, fitAddon: any, sizeDisplay?: HTMLElement | null) => void;

  // terminal-bridge.ts globals
  popOutTerminal: (identCode: number) => void;
  popInTerminal: (identCode: number) => void;
  isPoppedOut: (identCode: number) => boolean;
  bufferPopoutOutput: (identCode: number, charCode: number) => void;
  broadcastThemeChange: (themeName: string) => void;
  broadcastSettingsChange: (identCode: number) => void;

  _uiZoom: number;
}
