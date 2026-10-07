//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// cpu-load-graph.js - Scrolling CPU utilization graph
// Shared history buffer drives both the full window and the taskbar mini graph.

// Populate taskbar level bars (DOM exists at this point)
createLevelBars('taskbar-level-bars');

(function() {
  // ---- Shared history buffer (1 sample/sec, 60 seconds) ----
  var MAX_SAMPLES = 60;
  var history = new Float32Array(MAX_SAMPLES);
  var histLen = 0;
  var histHead = 0;

  // Down-sampling accumulator (~4 pushes/sec -> 1 sample/sec)
  var accumSum = 0;
  var accumCount = 0;
  var lastSecond = 0;

  function pushSample(load) {
    history[histHead] = load;
    histHead = (histHead + 1) % MAX_SAMPLES;
    if (histLen < MAX_SAMPLES) histLen++;
  }

  function getSample(i) {
    var oldest = (histHead - histLen + MAX_SAMPLES) % MAX_SAMPLES;
    return history[(oldest + i) % MAX_SAMPLES];
  }

  // ---- Colors ----
  var LINE_COLOR = 'rgba(0, 200, 180, 0.9)';
  var FILL_TOP  = 'rgba(0, 200, 180, 0.25)';
  var FILL_BOT  = 'rgba(0, 200, 180, 0.02)';
  var GRID_COLOR   = 'rgba(100, 130, 180, 0.12)';
  var LABEL_COLOR  = 'rgba(140, 165, 210, 0.4)';
  var BORDER_COLOR = 'rgba(100, 130, 180, 0.2)';

  // ---- Draw the graph onto a canvas ----
  function drawGraph(canvas, w, h, opts) {
    if (!canvas || w < 4 || h < 4) return;

    var ctx = canvas.getContext('2d');
    var dpr = window.devicePixelRatio || 1;
    canvas.width = w * dpr;
    canvas.height = h * dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    var pad;
    if (opts.labels) {
      pad = { left: 28, right: 6, top: 6, bottom: 14 };
    } else {
      pad = { left: 1, right: 1, top: 1, bottom: 1 };
    }
    var gw = w - pad.left - pad.right;
    var gh = h - pad.top - pad.bottom;
    if (gw < 2 || gh < 2) return;

    ctx.clearRect(0, 0, w, h);

    // Grid
    if (opts.grid) {
      ctx.strokeStyle = GRID_COLOR;
      ctx.lineWidth = 0.5;
      ctx.font = '9px monospace';
      ctx.fillStyle = LABEL_COLOR;
      ctx.textAlign = 'right';
      ctx.textBaseline = 'middle';
      for (var i = 0; i <= 4; i++) {
        var y = pad.top + gh - (i / 4) * gh;
        ctx.beginPath();
        ctx.moveTo(pad.left, y);
        ctx.lineTo(pad.left + gw, y);
        ctx.stroke();
        if (opts.labels && i > 0) {
          ctx.fillText(Math.round(i / 4 * 100) + '%', pad.left - 3, y);
        }
      }
      for (var x = gw; x > 0; x -= 30) {
        ctx.beginPath();
        ctx.moveTo(pad.left + x, pad.top);
        ctx.lineTo(pad.left + x, pad.top + gh);
        ctx.stroke();
      }
      ctx.strokeStyle = BORDER_COLOR;
      ctx.lineWidth = 1;
      ctx.strokeRect(pad.left, pad.top, gw, gh);
    }

    // Time axis label
    if (opts.timeLabel) {
      ctx.fillStyle = LABEL_COLOR;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'top';
      ctx.font = '9px monospace';
      ctx.fillText('60 seconds', pad.left + gw / 2, pad.top + gh + 2);
    }

    // Draw baseline even with no data
    if (histLen < 2) {
      ctx.beginPath();
      ctx.moveTo(pad.left, pad.top + gh);
      ctx.lineTo(pad.left + gw, pad.top + gh);
      ctx.strokeStyle = LINE_COLOR;
      ctx.lineWidth = opts.lineWidth || 1.5;
      ctx.globalAlpha = 0.3;
      ctx.stroke();
      ctx.globalAlpha = 1;
      return;
    }

    var xStep = gw / (MAX_SAMPLES - 1);
    var startX = gw - (histLen - 1) * xStep;

    // Fill area
    ctx.beginPath();
    ctx.moveTo(pad.left + startX, pad.top + gh);
    for (var i = 0; i < histLen; i++) {
      var val = getSample(i) / 100;
      ctx.lineTo(pad.left + startX + i * xStep, pad.top + gh - val * gh);
    }
    ctx.lineTo(pad.left + startX + (histLen - 1) * xStep, pad.top + gh);
    ctx.closePath();
    var grad = ctx.createLinearGradient(0, pad.top, 0, pad.top + gh);
    grad.addColorStop(0, FILL_TOP);
    grad.addColorStop(1, FILL_BOT);
    ctx.fillStyle = grad;
    ctx.fill();

    // Line
    ctx.beginPath();
    for (var i = 0; i < histLen; i++) {
      var val = getSample(i) / 100;
      var px = pad.left + startX + i * xStep;
      var py = pad.top + gh - val * gh;
      if (i === 0) ctx.moveTo(px, py);
      else ctx.lineTo(px, py);
    }
    ctx.strokeStyle = LINE_COLOR;
    ctx.lineWidth = opts.lineWidth || 1.5;
    ctx.stroke();
  }

  // ---- Canvas references ----
  var mainCanvas = document.getElementById('cpu-load-canvas');
  var miniCanvas = document.getElementById('taskbar-cpu-canvas');

  // ---- Draw both ----
  function drawAll() {
    // Main window (only if visible)
    var win = document.getElementById('cpu-load-window');
    if (win && win.style.display !== 'none' && mainCanvas) {
      var body = mainCanvas.parentElement;
      var rect = body.getBoundingClientRect();
      drawGraph(mainCanvas, Math.floor(rect.width), Math.floor(rect.height),
        { grid: true, labels: true, timeLabel: true, lineWidth: 1.5 });
    }
    // Taskbar mini graph (always draw, fixed 80x24)
    if (miniCanvas) {
      drawGraph(miniCanvas, 80, 24,
        { grid: false, labels: false, timeLabel: false, lineWidth: 1 });
    }
  }

  // ---- Public API (called from emulation.js) ----
  window.cpuLoadGraphPush = function(load) {
    var now = Math.floor(Date.now() / 1000);

    // Update window header percentage
    var pct = document.getElementById('cpu-load-graph-pct');
    if (pct) {
      pct.textContent = load + '%';
      var hue = Math.round(120 * (1 - load / 100));
      pct.style.color = 'hsl(' + hue + ', 65%, 58%)';
    }

    if (lastSecond === 0) lastSecond = now;

    if (now === lastSecond) {
      accumSum += load;
      accumCount++;
    } else {
      if (accumCount > 0) {
        pushSample(Math.round(accumSum / accumCount));
      }
      var gap = now - lastSecond;
      for (var g = 1; g < gap; g++) {
        pushSample(0);
      }
      lastSecond = now;
      accumSum = load;
      accumCount = 1;
    }

    drawAll();
  };

  window.cpuLoadGraphReset = function() {
    history.fill(0);
    histLen = 0;
    histHead = 0;
    accumSum = 0;
    accumCount = 0;
    lastSecond = 0;
    var pct = document.getElementById('cpu-load-graph-pct');
    if (pct) { pct.textContent = '0%'; pct.style.color = ''; }
    drawAll();
  };

  // ---- Resize observer for main canvas ----
  if (mainCanvas && mainCanvas.parentElement) {
    var resizeTimer;
    new ResizeObserver(function() {
      clearTimeout(resizeTimer);
      resizeTimer = setTimeout(drawAll, 50);
    }).observe(mainCanvas.parentElement);
  }

  // ---- Click mini graph to open/focus CPU Load window ----
  function openCpuLoadWindow() {
    var win = document.getElementById('cpu-load-window');
    if (win) {
      if (win.style.display === 'none') {
        win.style.display = 'flex';
      }
      if (typeof windowManager !== 'undefined') {
        windowManager.focus('cpu-load-window');
      }
      // Redraw after making visible
      setTimeout(drawAll, 50);
    }
  }

  var miniContainer = document.getElementById('taskbar-cpu-load');
  if (miniContainer) {
    miniContainer.addEventListener('click', openCpuLoadWindow);
  }

  // Initial draw (deferred to ensure layout is complete)
  setTimeout(drawAll, 100);
})();
