//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// paper-tape.js - Paper Tape Reader/Writer window management

(function() {
  'use strict';

  var win = document.getElementById('paper-tape-window');
  var header = document.getElementById('paper-tape-header');
  var closeBtn = document.getElementById('paper-tape-close');
  var loadBtn = document.getElementById('paper-tape-load-btn');
  var fileInput = document.getElementById('paper-tape-file-input');
  var readerStatus = document.getElementById('paper-tape-reader-status');
  var downloadBtn = document.getElementById('paper-tape-download-btn');
  var writerStatus = document.getElementById('paper-tape-writer-status');
  var writerOutput = document.getElementById('paper-tape-writer-output');

  var writerByteCount = 0;
  var writerBytes = [];

  // Close button handler
  if (closeBtn) {
    closeBtn.addEventListener('click', function() {
      if (typeof closeWindow === 'function') {
        closeWindow('paper-tape-window');
      }
    });
  }

  // Make draggable and resizable
  if (typeof makeDraggable === 'function' && win && header) {
    makeDraggable(win, header, 'paper-tape-pos');
  }
  if (typeof makeResizable === 'function' && win) {
    var resizeHandle = document.getElementById('paper-tape-resize');
    if (resizeHandle) {
      makeResizable(win, resizeHandle, 'paper-tape-size');
    }
  }

  // Register with window manager
  if (typeof windowManager !== 'undefined') {
    windowManager.register('paper-tape-window', 'Paper Tape');
  }

  // ===== Reader: File upload =====
  if (loadBtn && fileInput) {
    loadBtn.addEventListener('click', function() {
      fileInput.click();
    });

    fileInput.addEventListener('change', function(e) {
      var file = e.target.files[0];
      if (!file) return;

      var reader = new FileReader();
      reader.onload = function(ev) {
        var data = new Uint8Array(ev.target.result);

        if (typeof emu !== 'undefined' && emu.loadPaperTape) {
          emu.loadPaperTape(data);
          if (readerStatus) {
            readerStatus.textContent = file.name + ' (' + data.length + ' bytes)';
          }
        } else {
          console.error('LoadPaperTape not available (emu proxy not initialized)');
          if (readerStatus) {
            readerStatus.textContent = 'Error: emulator not ready';
          }
        }
      };
      reader.readAsArrayBuffer(file);

      // Reset the file input so the same file can be loaded again
      fileInput.value = '';
    });
  }

  /**
   * Handle a single paper tape punch output byte.
   * Called by dispatchTermOutput() when a device-class-2 entry is decoded.
   * @param {number} charCode - 8-bit byte value punched on the tape.
   */
  window.handlePaperTapeWriterOutput = function(charCode) {
    writerByteCount++;
    writerBytes.push(charCode);

    if (writerStatus) {
      writerStatus.textContent = writerByteCount + ' bytes punched';
    }

    // Update hex+ASCII display (show last 256 bytes)
    if (writerOutput) {
      var displayBytes = writerBytes.slice(-256);
      var hex = '';
      var ascii = '';
      for (var i = 0; i < displayBytes.length; i++) {
        hex += ('0' + displayBytes[i].toString(16)).slice(-2) + ' ';
        ascii += (displayBytes[i] >= 32 && displayBytes[i] < 127)
          ? String.fromCharCode(displayBytes[i]) : '.';
        if ((i + 1) % 16 === 0) {
          hex += '  ' + ascii + '\n';
          ascii = '';
        }
      }
      if (ascii) {
        // Pad the last line
        var remaining = 16 - (displayBytes.length % 16);
        if (remaining < 16) {
          for (var j = 0; j < remaining; j++) hex += '   ';
          hex += '  ' + ascii;
        }
        hex += '\n';
      }
      writerOutput.textContent = hex;
      writerOutput.scrollTop = writerOutput.scrollHeight;
    }
  };

  // ===== Writer: Download =====
  if (downloadBtn) {
    downloadBtn.addEventListener('click', function() {
      var data = null;
      var length = 0;

      if (typeof emu !== 'undefined' && emu.getPaperTapeWriterData) {
        var result = emu.getPaperTapeWriterData();
        if (result) {
          data = result;
          length = result.length;
        }
      } else if (typeof Module !== 'undefined' && Module._GetPaperTapeWriterDataLength) {
        length = Module._GetPaperTapeWriterDataLength();
        if (length > 0) {
          var ptr = Module._GetPaperTapeWriterDataPtr();
          if (ptr) {
            data = new Uint8Array(Module.HEAPU8.buffer, ptr, length);
          }
        }
      }

      if (!data || length === 0) {
        alert('No tape data to download');
        return;
      }

      // Create download
      var blob = new Blob([data], { type: 'application/octet-stream' });
      var url = URL.createObjectURL(blob);
      var a = document.createElement('a');
      a.href = url;
      a.download = 'papertape-' + Date.now() + '.bpun';
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);
    });
  }

})();
