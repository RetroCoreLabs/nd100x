//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// HackerCorp Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// emulation.js - Emulation loop, start/stop, loading overlay

// =========================================================
// Reusable Level Bars Component
// Populates a container with 16 interrupt-level indicator bars.
// Usage: createLevelBars('container-id')
// =========================================================
var LEVEL_DEFS = [
  { lev: 15, color: '#ff6b6b', desc: 'Fast user int' },
  { lev: 14, color: '#ff8a5c', desc: 'Internal int' },
  { lev: 13, color: '#ffaa44', desc: 'RTC/HDLC' },
  { lev: 12, color: '#ffd03b', desc: 'Terminal in' },
  { lev: 11, color: '#c8e04a', desc: 'Mass storage' },
  { lev: 10, color: '#88d860', desc: 'Terminal out' },
  { lev: 9,  color: '#50d080', desc: 'Direct Task 4' },
  { lev: 8,  color: '#40c8a0', desc: 'Direct Task 3' },
  { lev: 7,  color: '#38b8c0', desc: 'Direct Task 2' },
  { lev: 6,  color: '#40a8e0', desc: 'Direct Task 1' },
  { lev: 5,  color: '#5898f0', desc: 'XMSG' },
  { lev: 4,  color: '#7088e8', desc: 'I/O Monitor' },
  { lev: 3,  color: '#9078e0', desc: 'Segment Admin' },
  { lev: 2,  color: '#b068d0', desc: 'Monitor' },
  { lev: 1,  color: '#c860b8', desc: 'RT/Background' },
  { lev: 0,  color: '#808898', desc: 'Idle (DUMMY)' }
];

function createLevelBars(containerId) {
  var el = document.getElementById(containerId);
  if (!el) return;
  LEVEL_DEFS.forEach(function(d) {
    var span = document.createElement('span');
    span.className = 'lv-bar';
    span.setAttribute('data-level', d.lev);
    span.title = d.lev + ': ' + d.desc;
    span.style.background = d.color;
    el.appendChild(span);
  });
}

// Populate level-bar container in Machine Info (exists at this point in DOM)
createLevelBars('machine-level-bars');

let emulationRunning = false;
let loopId = null;
const instructionsPerFrame = 10000;
let continuousStepMode = false;
let loadingOverlayVisible = false;
let hasReceivedTerminalOutput = false;
let hasEverStartedEmulation = false;

// =========================================================
// CPU Load Meter
// Samples PIL every frame. Level 0 = idle, anything else = busy.
// Only meaningful when IONI (interrupt system) is enabled.
// =========================================================
const CPU_LOAD_WINDOW = 120;         // ~2 seconds at 60fps
const CPU_LOAD_DOM_INTERVAL = 15;    // update DOM every 15 frames (~4x/sec)
let cpuLoadSamples = new Uint8Array(CPU_LOAD_WINDOW);
let cpuLoadIndex = 0;
let cpuLoadFrameCount = 0;
let cpuLoadIoniSeen = false;         // have we ever seen IONI=1?
var levelLastHit = new Array(16).fill(0);

function sampleCpuLoad() {
  if (!emu || !emu.isReady()) return;

  var pil = emu.getPIL();
  cpuLoadSamples[cpuLoadIndex] = (pil !== 0) ? 1 : 0;
  cpuLoadIndex = (cpuLoadIndex + 1) % CPU_LOAD_WINDOW;
  levelLastHit[pil] = Date.now();

  cpuLoadFrameCount++;
  if (cpuLoadFrameCount % CPU_LOAD_DOM_INTERVAL === 0) {
    updateCpuLoadDisplay();
  }
}

function updateCpuLoadDisplay() {
  // Check IONI (bit 15 of STS)
  var sts = emu.isReady() ? emu.getSTS() : 0;
  var ioni = (sts >> 15) & 1;

  if (!ioni) {
    // If we previously saw IONI, keep showing last value (boot might briefly toggle it)
    return;
  }

  cpuLoadIoniSeen = true;

  // Count busy samples
  var busy = 0;
  for (var i = 0; i < CPU_LOAD_WINDOW; i++) {
    busy += cpuLoadSamples[i];
  }
  var load = Math.round((busy / CPU_LOAD_WINDOW) * 100);

  // Update Machine Info CPU load (% only)
  var machLoad = document.getElementById('machine-cpu-load');
  if (machLoad) {
    machLoad.textContent = load + '%';
  }

  // Push sample to CPU Load graph window
  if (typeof window.cpuLoadGraphPush === 'function') {
    window.cpuLoadGraphPush(load);
  }

  // Update level activity bars (all instances)
  var now = Date.now();
  var allBars = document.querySelectorAll('.lv-bar');
  allBars.forEach(function(bar) {
    var lev = parseInt(bar.getAttribute('data-level'));
    bar.classList.toggle('active', (now - levelLastHit[lev]) < 250);
  });
}

function resetCpuLoad() {
  cpuLoadSamples.fill(0);
  cpuLoadIndex = 0;
  cpuLoadFrameCount = 0;
  cpuLoadIoniSeen = false;
  levelLastHit.fill(0);
  var machLoad = document.getElementById('machine-cpu-load');
  if (machLoad) machLoad.textContent = '-';
  var bars = document.querySelectorAll('.lv-bar');
  bars.forEach(function(bar) { bar.classList.remove('active'); });
  if (typeof window.cpuLoadGraphReset === 'function') {
    window.cpuLoadGraphReset();
  }
}

// Loading overlay functions
function showLoadingOverlay(bootDevice) {
  const overlay = document.getElementById('loading-overlay');
  if (overlay) {
    // Named by the caller: the machine's [boot] device decides what boots,
    // there is no selector on the toolbar to read it off any more.
    const bootDeviceText = document.getElementById('loading-boot-device');
    if (bootDeviceText) {
      bootDeviceText.textContent = `Booting from ${bootDevice || 'the configured drive'}`;
    }

    overlay.style.display = 'flex';
    loadingOverlayVisible = true;
    hasReceivedTerminalOutput = false;

    document.addEventListener('keydown', handleLoadingKeys);
    document.getElementById('loading-ok-btn').addEventListener('click', dismissLoadingOverlay);
    document.getElementById('loading-cancel-btn').addEventListener('click', cancelFromLoadingOverlay);
  }
}

function hideLoadingOverlay() {
  const overlay = document.getElementById('loading-overlay');
  if (overlay) {
    overlay.style.display = 'none';
    loadingOverlayVisible = false;
    document.removeEventListener('keydown', handleLoadingKeys);
  }
}

function dismissLoadingOverlay() {
  if (loadingOverlayVisible) {
    hideLoadingOverlay();
  }
}

function cancelFromLoadingOverlay() {
  if (loadingOverlayVisible) {
    console.log('User cancelled boot sequence');
    hideLoadingOverlay();
    stopEmulation();

    if (terminals[activeTerminalId] && terminals[activeTerminalId].term) {
      terminals[activeTerminalId].term.writeln('\r\n\x1b[31mBoot sequence cancelled by user\x1b[0m');
    }
  }
}

function handleLoadingKeys(event) {
  if (!loadingOverlayVisible) return;
  if (event.key === 'Escape') {
    cancelFromLoadingOverlay();
  } else if (event.key === 'Enter') {
    dismissLoadingOverlay();
  }
}

function handleBreakpointHit() {
  emulationRunning = false;
  continuousStepMode = false;
  if (loopId !== null) {
    cancelAnimationFrame(loopId);
    loopId = null;
  }
  resetCpuLoad();
  if (loadingOverlayVisible) hideLoadingOverlay();

  var reason = emu.getStopReason();
  /* 2=BREAKPOINT, 8=DATA_BREAKPOINT */
  if (reason === 8) {
    document.getElementById('status').textContent = 'Watchpoint hit';
  } else {
    document.getElementById('status').textContent = 'Breakpoint hit';
  }

  /* Activate debugger window */
  if (typeof window.dbgActivateBreakpoint === 'function') {
    window.dbgActivateBreakpoint();
  }
  /* Refresh breakpoints window */
  if (typeof window.bpRefresh === 'function') {
    window.bpRefresh();
  }
}

function executeInstructionBatch() {
  if (!emulationRunning && !continuousStepMode) {
    return;
  }

  if (!emu || !emu.isReady()) {
    console.error("Step function not available");
    return;
  }

  try {
    /* CPURunMode enum values from cpu_types.h:
       0=CPU_UNKNOWN_STATE, 1=CPU_RUNNING, 2=CPU_BREAKPOINT,
       3=CPU_PAUSED, 4=CPU_STOPPED, 5=CPU_SHUTDOWN */

    if (emu.isWorkerMode()) {
      // Worker runs autonomously. Main thread just samples cached state.
      sampleCpuLoad();
      var mode = emu.getRunMode();
      if (mode === 2) { /* CPU_BREAKPOINT */
        handleBreakpointHit();
        return;
      }
      /* Refresh debugger window from RAF loop (~4x/sec) */
      if (cpuLoadFrameCount % CPU_LOAD_DOM_INTERVAL === 0 &&
          typeof window.dbgRefreshIfVisible === 'function') {
        window.dbgRefreshIfVisible();
      }
      if (emulationRunning || continuousStepMode) {
        loopId = requestAnimationFrame(executeInstructionBatch);
      }
      return;
    }

    // Direct mode: existing code
    emu.step(instructionsPerFrame);
    emu.flushTerminalOutput();
    sampleCpuLoad();

    /* Refresh debugger window directly from RAF loop (~4x/sec) */
    if (cpuLoadFrameCount % CPU_LOAD_DOM_INTERVAL === 0 &&
        typeof window.dbgRefreshIfVisible === 'function') {
      window.dbgRefreshIfVisible();
    }

    /* Check if CPU hit a breakpoint or watchpoint during execution */
    {
      var mode = emu.getRunMode();
      if (mode === 2) { /* CPU_BREAKPOINT */
        handleBreakpointHit();
        return;
      }
    }

    if (emulationRunning || continuousStepMode) {
      loopId = requestAnimationFrame(executeInstructionBatch);
    }
  } catch (e) {
    console.error("Error executing instructions:", e);
    terminals[activeTerminalId].term.writeln(`\r\nError: ${e.message}`);
    stopEmulation();
  }
}

function startEmulation(bootDevice) {
  if (emulationRunning) return;

  emulationRunning = true;
  document.getElementById('status').textContent = 'Emulation running...';
  resetCpuLoad();
  terminals[activeTerminalId].term.writeln('\r\nBooting from ' + (bootDevice || 'unknown'));

  // Mark SMD units dirty for Direct mode auto-save (Worker persists immediately)
  if (typeof isSmdPersistenceEnabled === 'function' && isSmdPersistenceEnabled() &&
      typeof smdStorage !== 'undefined' && emu && !emu.isWorkerMode()) {
    var units = smdStorage.getUnitAssignments();
    for (var u = 0; u < 4; u++) {
      if (units[u]) smdStorage.markDirty(u);
    }
  }

  if (!hasEverStartedEmulation) {
    showLoadingOverlay(bootDevice);
  } else {
    hasReceivedTerminalOutput = false;
  }

  // In Worker mode, tell the Worker to start its autonomous loop
  if (emu.isWorkerMode()) {
    emu.workerStart();
  }

  loopId = requestAnimationFrame(executeInstructionBatch);
}

function stopEmulation() {
  if (!emulationRunning && !continuousStepMode) return;

  emulationRunning = false;
  continuousStepMode = false;

  if (loopId !== null) {
    cancelAnimationFrame(loopId);
    loopId = null;
  }

  // In Worker mode, tell the Worker to stop its loop
  if (emu.isWorkerMode()) {
    emu.workerStop();
  }

  document.getElementById('status').textContent = 'Emulation stopped';
  terminals[activeTerminalId].term.writeln('\r\nEmulation stopped');
  resetCpuLoad();

  if (loadingOverlayVisible) {
    hideLoadingOverlay();
  }

  // Auto-save dirty SMD units to OPFS (Direct mode only)
  if (typeof isSmdPersistenceEnabled === 'function' && isSmdPersistenceEnabled() &&
      typeof smdStorage !== 'undefined' && emu && !emu.isWorkerMode()) {
    var dirtyUnits = smdStorage.getDirtyUnits();
    dirtyUnits.forEach(function(unit) {
      if (typeof smdSaveUnit === 'function') smdSaveUnit(unit);
    });
  }
}

// =========================================================
// Worker mode callbacks
// =========================================================
if (typeof USE_WORKER !== 'undefined' && USE_WORKER) {
  // Wire up breakpoint callback from Worker
  // (set after emu-proxy-worker.js creates window.emu)
  window.addEventListener('DOMContentLoaded', function() {
    if (emu && emu.isWorkerMode()) {
      emu.onBreakpoint = function(msg) {
        handleBreakpointHit();
      };
      emu.onStopped = function(msg) {
        emulationRunning = false;
        continuousStepMode = false;
        if (loopId !== null) {
          cancelAnimationFrame(loopId);
          loopId = null;
        }
        resetCpuLoad();
        document.getElementById('status').textContent = 'Emulation stopped';
        if (loadingOverlayVisible) hideLoadingOverlay();
      };
    }
  });
}
