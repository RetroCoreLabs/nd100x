//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// printer.js - Printer window with PDF generation, preview & download

(function() {
  'use strict';

  var MAX_LINES = 2000;

  // --- DOM references ---
  var win = document.getElementById('printer-window');
  var header = document.getElementById('printer-header');
  var closeBtn = document.getElementById('printer-close');
  var outputEl = document.getElementById('printer-output');
  var driverSelect = document.getElementById('printer-driver-select');
  var jobBadge = document.getElementById('printer-job-badge');
  var activeIndicator = document.getElementById('printer-active-indicator');
  var jobsTbody = document.getElementById('printer-jobs-tbody');
  var pdfViewer = document.getElementById('printer-pdf-viewer');
  var tabs = win ? win.querySelectorAll('.printer-tab') : [];
  var panels = win ? win.querySelectorAll('.printer-tab-panel') : [];

  // Config window printer controls
  var configDriver = document.getElementById('config-printer-driver');
  var configFlushBtn = document.getElementById('config-printer-flush');

  var lineCount = 0;
  var jobs = [];
  var blobUrls = {};  // jobNumber -> blob URL cache

  // --- Close buttons ---
  if (closeBtn) {
    closeBtn.addEventListener('click', function() {
      if (typeof closeWindow === 'function') closeWindow('printer-window');
    });
  }

  // --- Draggable / Resizable ---
  if (typeof makeDraggable === 'function') {
    if (win && header) makeDraggable(win, header, 'printer-pos');
  }
  if (typeof makeResizable === 'function' && win) {
    var resizeHandle = document.getElementById('printer-resize');
    if (resizeHandle) makeResizable(win, resizeHandle, 'printer-size');
  }

  // --- Window manager ---
  if (typeof windowManager !== 'undefined') {
    windowManager.register('printer-window', 'Printer');
  }

  // --- Tab switching ---
  for (var i = 0; i < tabs.length; i++) {
    tabs[i].addEventListener('click', (function(tab) {
      return function() {
        var target = tab.getAttribute('data-tab');
        for (var j = 0; j < tabs.length; j++) {
          var isActive = tabs[j].getAttribute('data-tab') === target;
          tabs[j].style.color = isActive ? 'var(--text-color, #e0e0e0)' : 'var(--text-muted, #999)';
          tabs[j].style.borderBottomColor = isActive ? 'var(--accent-color, #4fc3f7)' : 'transparent';
        }
        for (var k = 0; k < panels.length; k++) {
          panels[k].style.display = panels[k].getAttribute('data-panel') === target ? '' : 'none';
        }
      };
    })(tabs[i]));
  }

  // --- Driver select sync ---
  var DRIVER_STORAGE_KEY = 'nd100x-printer-driver';

  function syncDriverSelects(value, persist) {
    if (driverSelect) driverSelect.value = value;
    if (configDriver) configDriver.value = value;
    if (typeof emu !== 'undefined') emu.printerSetType(parseInt(value));
    if (persist !== false) {
      try { localStorage.setItem(DRIVER_STORAGE_KEY, value); } catch(e) {}
    }
  }

  // Restore persisted driver selection
  try {
    var savedDriver = localStorage.getItem(DRIVER_STORAGE_KEY);
    if (savedDriver !== null) syncDriverSelects(savedDriver, false);
  } catch(e) {}

  // Apply persisted driver to C side — called from completePowerOn() in toolbar.js
  window.applyPersistedPrinterDriver = function() {
    try {
      var saved = localStorage.getItem(DRIVER_STORAGE_KEY);
      if (saved !== null && typeof emu !== 'undefined') {
        emu.printerSetType(parseInt(saved));
      }
    } catch(e) {}
  };

  if (driverSelect) {
    driverSelect.addEventListener('change', function() { syncDriverSelects(this.value); });
  }
  if (configDriver) {
    configDriver.addEventListener('change', function() { syncDriverSelects(this.value); });
  }

  // --- Flush button ---
  if (configFlushBtn) {
    configFlushBtn.addEventListener('click', function() {
      if (typeof emu !== 'undefined') emu.printerFlushJob();
    });
  }

  // --- Text output handler (unchanged core logic) ---
  window.handlePrinterOutput = function(charCode) {
    if (!outputEl) return;

    var c = String.fromCharCode(charCode);
    outputEl.textContent += c;

    if (charCode === 10) {
      lineCount++;
      if (lineCount > MAX_LINES) {
        var text = outputEl.textContent;
        var lines = text.split('\n');
        if (lines.length > MAX_LINES) {
          outputEl.textContent = lines.slice(lines.length - MAX_LINES).join('\n');
          lineCount = MAX_LINES;
        }
      }
    }

    // Auto-scroll
    var body = outputEl.parentElement;
    if (body) body.scrollTop = body.scrollHeight;

    // Update active indicator
    if (activeIndicator) {
      activeIndicator.textContent = 'Printing...';
      activeIndicator.style.color = 'var(--accent-color, #4fc3f7)';
    }
  };

  // --- Job completed callback ---
  function formatTime(epoch) {
    if (!epoch) return '-';
    var d = new Date(epoch * 1000);
    return d.toLocaleTimeString();
  }

  function addJobToTable(job) {
    if (!jobsTbody) return;
    var tr = document.createElement('tr');
    tr.style.borderBottom = '1px solid rgba(255,255,255,0.08)';
    tr.innerHTML =
      '<td style="padding:4px 6px;">' + job.jobNumber + '</td>' +
      '<td style="padding:4px 6px;">' + formatTime(job.startTime) + '</td>' +
      '<td style="padding:4px 6px;">' + formatTime(job.endTime) + '</td>' +
      '<td style="padding:4px 6px;">' + job.lines + '</td>' +
      '<td style="padding:4px 6px;">' + job.bytes + '</td>' +
      '<td style="padding:4px 6px;">' +
        '<button class="pj-preview" data-idx="' + (jobs.length - 1) + '" style="background:none;border:1px solid rgba(255,255,255,0.2);color:var(--text-color,#e0e0e0);padding:1px 6px;border-radius:3px;cursor:pointer;font-size:10px;margin-right:4px;">Preview</button>' +
        '<button class="pj-download" data-idx="' + (jobs.length - 1) + '" style="background:none;border:1px solid rgba(255,255,255,0.2);color:var(--text-color,#e0e0e0);padding:1px 6px;border-radius:3px;cursor:pointer;font-size:10px;">Download</button>' +
      '</td>';
    jobsTbody.appendChild(tr);

    // Wire preview/download buttons
    tr.querySelector('.pj-preview').addEventListener('click', function() {
      previewJob(parseInt(this.getAttribute('data-idx')));
    });
    tr.querySelector('.pj-download').addEventListener('click', function() {
      downloadJob(parseInt(this.getAttribute('data-idx')));
    });
  }

  window.onPrinterJobCompleted = function(workerMsg) {
    var job;

    if (workerMsg && workerMsg.jobNumber) {
      // Worker mode: metadata comes in the message
      job = {
        jobNumber: workerMsg.jobNumber,
        startTime: workerMsg.startTime,
        endTime: workerMsg.endTime,
        bytes: workerMsg.bytes,
        lines: workerMsg.lines
      };
    } else {
      // Direct mode: read metadata from C exports
      job = {
        jobNumber: emu.printerGetLastCompletedJob(),
        startTime: emu.printerGetLastJobStartTime(),
        endTime: emu.printerGetLastJobEndTime(),
        bytes: emu.printerGetLastJobBytes(),
        lines: emu.printerGetLastJobLines()
      };
    }

    jobs.push(job);
    addJobToTable(job);

    // Update badge
    if (jobBadge) jobBadge.textContent = jobs.length;

    // Clear active indicator
    if (activeIndicator) {
      activeIndicator.textContent = 'Job ' + job.jobNumber + ' completed';
      activeIndicator.style.color = 'var(--text-muted, #999)';
    }
  };

  // --- PDF blob URL helper ---
  function getPdfBlobUrl(job, callback) {
    if (blobUrls[job.jobNumber]) {
      callback(blobUrls[job.jobNumber]);
      return;
    }

    var path = '/prints/print-' + job.jobNumber + '.pdf';
    if (typeof emu === 'undefined') return;

    var result = emu.fsReadFile(path);
    if (result && typeof result.then === 'function') {
      // Async (worker mode) — result is a Promise<Uint8Array>
      result.then(function(data) {
        if (!data || data.length === 0) {
          console.error('Printer: empty PDF for job ' + job.jobNumber);
          return;
        }
        var blob = new Blob([data], { type: 'application/pdf' });
        var url = URL.createObjectURL(blob);
        blobUrls[job.jobNumber] = url;
        callback(url);
      });
    } else {
      // Sync (direct mode)
      if (!result || result.length === 0) {
        console.error('Printer: empty PDF for job ' + job.jobNumber);
        return;
      }
      var blob = new Blob([result], { type: 'application/pdf' });
      var url = URL.createObjectURL(blob);
      blobUrls[job.jobNumber] = url;
      callback(url);
    }
  }

  // --- Preview ---
  function previewJob(idx) {
    if (idx < 0 || idx >= jobs.length) return;
    getPdfBlobUrl(jobs[idx], function(url) {
      if (pdfViewer) pdfViewer.src = url;
      // Switch to preview tab
      for (var j = 0; j < tabs.length; j++) {
        var isPreview = tabs[j].getAttribute('data-tab') === 'preview';
        tabs[j].style.color = isPreview ? 'var(--text-color, #e0e0e0)' : 'var(--text-muted, #999)';
        tabs[j].style.borderBottomColor = isPreview ? 'var(--accent-color, #4fc3f7)' : 'transparent';
      }
      for (var k = 0; k < panels.length; k++) {
        panels[k].style.display = panels[k].getAttribute('data-panel') === 'preview' ? '' : 'none';
      }
    });
  }

  // --- Download ---
  function downloadJob(idx) {
    if (idx < 0 || idx >= jobs.length) return;
    getPdfBlobUrl(jobs[idx], function(url) {
      var a = document.createElement('a');
      a.href = url;
      a.download = 'print-' + jobs[idx].jobNumber + '.pdf';
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
    });
  }

})();
