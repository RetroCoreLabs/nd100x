//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// module-init.js - WASM Module configuration, terminal output handler, disk loading
// This must be loaded BEFORE nd100wasm.js

// Define the Module configuration object before loading any Emscripten code
// In Worker mode, Module lives in the Worker - we skip this.
if (typeof USE_WORKER === 'undefined' || !USE_WORKER) {
  // Extract cache-bust version from nd100wasm.js script tag (if present)
  var _wasmCacheBust = (function() {
    var scripts = document.querySelectorAll('script[src*="nd100wasm.js"]');
    if (scripts.length > 0) {
      var m = scripts[0].src.match(/\?v=(\d+)/);
      if (m) return '?v=' + m[1];
    }
    return '';
  })();

  var Module = {
    locateFile: function(path) {
      // Append cache-bust query string to .wasm file
      if (path.endsWith('.wasm')) return path + _wasmCacheBust;
      return path;
    },
    print: function(text) { console.log(text); },
    printErr: function(text) { console.error(text); },
    onRuntimeInitialized: function() {
      console.log("WASM Runtime Initialized");
      console.log("Execution mode: Direct (main thread)");
      // Set up the JavaScript handler for terminal output
      setupTerminalOutputHandler();
      initializeSystem();
    }
  };
}

// Called by emu-proxy-worker.js when Worker WASM is ready
window.onWorkerReady = function() {
  console.log("WASM Runtime Initialized");
  console.log("Execution mode: Web Worker (background thread)");
  setupTerminalOutputHandler();
  initializeSystem();
};

// Global object to store terminal references
var terminals = {};

// Function to handle terminal output from C/WASM
function handleTerminalOutput(identCode, charCode) {
  // Route output to pop-out window if terminal is popped out
  if (typeof window.isPoppedOut === 'function' && window.isPoppedOut(identCode)) {
    window.bufferPopoutOutput(identCode, charCode);

    // Still handle loading overlay hide logic
    if (loadingOverlayVisible && !hasReceivedTerminalOutput) {
      hasReceivedTerminalOutput = true;
      hasEverStartedEmulation = true;
      setTimeout(() => {
        hideLoadingOverlay();
      }, 500);
    }
    return 1;
  }

  if (terminals[identCode] && terminals[identCode].term) {
    terminals[identCode].term.write(String.fromCharCode(charCode));

    // Hide loading overlay when we receive terminal output
    if (loadingOverlayVisible && !hasReceivedTerminalOutput) {
      hasReceivedTerminalOutput = true;
      hasEverStartedEmulation = true;
      setTimeout(() => {
        hideLoadingOverlay();
      }, 500);
    }
  } else {
    console.error(`Terminal with identCode ${identCode} not found or not initialized in handleTerminalOutput`);
  }
  return 1;
}

// Make the terminal output handler available globally for C to call
window.handleTerminalOutputFromC = function(identCode, charCode) {
  return handleTerminalOutput(identCode, charCode);
};

// Setup the terminal output handler by exposing our JS function to C
function setupTerminalOutputHandler() {
  // Prefer ring buffer mode: C writes to a buffer, JS polls after each Step().
  // This eliminates EM_ASM calls and is required for future Web Worker mode.
  if (emu && emu.hasRingBuffer()) {
    if (emu.enableRingBuffer()) {
      console.log("[Phase 1] Terminal ring buffer enabled - C writes to buffer, JS polls after Step()");
      console.log("[Phase 1] EM_ASM terminal callbacks eliminated");
      return true;
    }
  }

  // Fallback: EM_ASM callback (legacy path)
  if (!emu || !emu.hasJSTerminalHandler()) {
    console.warn("[Phase 1] FALLBACK: Using legacy terminal output handler - ring buffer not available");
    return false;
  }

  console.log("[Phase 1] FALLBACK: Using EM_ASM terminal callbacks (ring buffer not available)");
  emu.setJSTerminalOutputHandler(1);
  return true;
}

// Track which disk images loaded successfully
var diskImageStatus = { smd: false };

// ?image=basename.img — sanitized basename or null (forces HTTP demo load + optional auto SMD boot)
function sanitizeUrlImageQuery(raw) {
  if (raw == null || typeof raw !== 'string') return null;
  var s = raw.trim();
  if (!s) return null;
  var base = s.replace(/\\/g, '/').split('/').pop();
  if (base.indexOf('..') >= 0) return null;
  if (!/^[A-Za-z0-9._-]+\.(img|IMG)$/.test(base)) return null;
  return base;
}

(function initUrlImageFromSearch() {
  try {
    var q = new URLSearchParams(window.location.search).get('image');
    window.__nd100xUrlImage = sanitizeUrlImageQuery(q);
  } catch (e) {
    window.__nd100xUrlImage = null;
  }
})();

// Persistence mode: opt-in via Config toggle
var SMD_PERSIST_KEY = 'nd100x-smd-persist';

function isSmdPersistenceEnabled() {
  return localStorage.getItem(SMD_PERSIST_KEY) === 'true';
}

// Expected minimum sizes (sanity check - reject truncated / error pages)
var DISK_MIN_SIZES = {
  'SMD0.IMG':   1024,         // SMD must be at least 1 block (1KB)
  'FLOPPY.IMG': 512           // Floppy must be at least 1 sector
};

// Format byte count for display
function formatBytes(bytes) {
  if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + ' MB';
  if (bytes >= 1024)    return (bytes / 1024).toFixed(0) + ' KB';
  return bytes + ' B';
}

// Load a single disk image into MEMFS using XHR (supports progress).
// Returns a promise that resolves to true on success, false on failure.
function loadDiskImage(url, memfsPath) {
  var minSize = DISK_MIN_SIZES[url] || 512;

  return new Promise(function(resolve) {
    var statusEl = document.getElementById('status');
    var xhr = new XMLHttpRequest();
    xhr.open('GET', url, true);
    xhr.responseType = 'arraybuffer';

    xhr.onprogress = function(e) {
      if (e.lengthComputable && statusEl) {
        var pct = Math.round((e.loaded / e.total) * 100);
        statusEl.textContent = 'Loading ' + url + '... ' + pct + '%';
      } else if (statusEl) {
        statusEl.textContent = 'Loading ' + url + '... ' + formatBytes(e.loaded);
      }
    };

    xhr.onload = function() {
      if (xhr.status < 200 || xhr.status >= 300) {
        console.error(url + ': HTTP ' + xhr.status + ' ' + xhr.statusText);
        resolve(false);
        return;
      }

      var buf = xhr.response;
      if (!buf) {
        console.error(url + ': empty response body');
        resolve(false);
        return;
      }

      if (buf.byteLength < minSize) {
        console.error(url + ': file too small (' + formatBytes(buf.byteLength) +
          ', expected at least ' + formatBytes(minSize) + ') - not a valid disk image');
        resolve(false);
        return;
      }

      // Check for HTML error pages served as the image
      var header = new Uint8Array(buf, 0, Math.min(16, buf.byteLength));
      var headerStr = String.fromCharCode.apply(null, header);
      if (headerStr.indexOf('<!DOC') === 0 || headerStr.indexOf('<html') === 0 ||
          headerStr.indexOf('<HTML') === 0) {
        console.error(url + ': received HTML instead of a disk image (server returned an error page)');
        resolve(false);
        return;
      }

      // In Worker mode, transfer ArrayBuffer to Worker
      if (typeof USE_WORKER !== 'undefined' && USE_WORKER) {
        emu.workerLoadDisk(memfsPath, buf);
        console.log(url + ' -> ' + memfsPath + ' (' + formatBytes(buf.byteLength) + ') OK [Worker]');
        resolve(true);
        return;
      }

      // Write to MEMFS (direct mode)
      try {
        emu.fsWriteFile(memfsPath, new Uint8Array(buf));
      } catch (e) {
        console.error(url + ': FS.writeFile failed: ' + e.message);
        resolve(false);
        return;
      }

      // Set permissions
      try { emu.fsChmod(memfsPath, 0o666); } catch (e) { /* ignore */ }

      // Verify readback
      try {
        var stat = emu.fsStat(memfsPath);
        if (stat.size !== buf.byteLength) {
          console.error(url + ': MEMFS size mismatch (wrote ' + buf.byteLength +
            ', got ' + stat.size + ')');
          resolve(false);
          return;
        }
      } catch (e) {
        console.error(url + ': FS.stat failed after write: ' + e.message);
        resolve(false);
        return;
      }

      console.log(url + ' -> ' + memfsPath + ' (' + formatBytes(buf.byteLength) + ') OK');
      resolve(true);
    };

    xhr.onerror = function() {
      console.error(url + ': network error (server unreachable or CORS blocked)');
      resolve(false);
    };

    xhr.ontimeout = function() {
      console.error(url + ': request timed out');
      resolve(false);
    };

    xhr.timeout = 120000; // 2 minute timeout for large images
    xhr.send();
  });
}

// Download an image with XHR and return the ArrayBuffer (for OPFS import)
function downloadImageBuffer(url) {
  return new Promise(function(resolve, reject) {
    var statusEl = document.getElementById('status');
    var xhr = new XMLHttpRequest();
    xhr.open('GET', url, true);
    xhr.responseType = 'arraybuffer';

    xhr.onprogress = function(e) {
      if (e.lengthComputable && statusEl) {
        var pct = Math.round((e.loaded / e.total) * 100);
        statusEl.textContent = 'Downloading ' + url + '... ' + pct + '%';
      } else if (statusEl) {
        statusEl.textContent = 'Downloading ' + url + '... ' + formatBytes(e.loaded);
      }
    };

    xhr.onload = function() {
      if (xhr.status < 200 || xhr.status >= 300) {
        reject(new Error('HTTP ' + xhr.status + ' ' + xhr.statusText + ' for ' + url));
        return;
      }
      var buf = xhr.response;
      if (!buf || buf.byteLength === 0) {
        reject(new Error('Empty response body from ' + url + ' (HTTP ' + xhr.status + ')'));
        return;
      }

      // Reject HTML error pages served with 200 status
      var header = new Uint8Array(buf, 0, Math.min(16, buf.byteLength));
      var headerStr = String.fromCharCode.apply(null, header);
      if (headerStr.indexOf('<!DOC') === 0 || headerStr.indexOf('<html') === 0 ||
          headerStr.indexOf('<HTML') === 0 || headerStr.indexOf('<?xml') === 0) {
        reject(new Error('Server returned an HTML/XML page instead of a disk image for ' + url));
        return;
      }

      console.log('[Download] ' + url + ': ' + formatBytes(buf.byteLength) + ' OK' +
        ', first 8 bytes: ' + Array.from(new Uint8Array(buf, 0, Math.min(8, buf.byteLength)))
          .map(function(b) { return ('0' + b.toString(16)).slice(-2); }).join(' '));
      resolve(buf);
    };

    xhr.onerror = function() { reject(new Error('Network error downloading ' + url)); };
    xhr.ontimeout = function() { reject(new Error('Timeout downloading ' + url)); };
    xhr.timeout = 120000;
    xhr.send();
  });
}

// Load disk images - respects persistence mode
// Demo mode: XHR to MEMFS (current behavior)
// Persistent mode: mount from OPFS (no XHR if images exist)
function loadDiskImages() {
  if (!emu || !emu.fsAvailable()) {
    console.error("ERROR: Module.FS is not available!");
    var statusElement = document.getElementById('status');
    if (statusElement) {
      statusElement.textContent = 'Error: FS API not available. Check console.';
    }
    return Promise.resolve();
  }

  // ?image= forces same-origin HTTP load into /SMD0.IMG (bypasses OPFS persistence this session)
  if (window.__nd100xUrlImage) {
    var urlBase = window.__nd100xUrlImage;
    return loadDiskImage(urlBase, '/SMD0.IMG').then(function(smdOk) {
      diskImageStatus.smd = smdOk;
      if (smdOk && typeof driveRegistry !== 'undefined') {
        driveRegistry.mount('smd', 0, 'demo', urlBase, urlBase, 0);
      }

      var statusEl = document.getElementById('status');
      if (statusEl) {
        if (diskImageStatus.smd) {
          statusEl.textContent = 'WebAssembly module loaded! (' + urlBase + ')';
        } else {
          statusEl.textContent = 'Warning: Failed to load ' + urlBase + '. Check console.';
          console.warn(urlBase + ' not found. Serve the file next to the WASM page.');
        }
      }

      window.dispatchEvent(new CustomEvent('smd-mounts-ready', { detail: { mounted: smdOk ? 1 : 0 } }));
    });
  }

  // Check if persistence is enabled
  if (isSmdPersistenceEnabled()) {
    return loadDiskImagesPersistent();
  }

  // Demo mode: load SMD from server into MEMFS (original behavior)
  return loadDiskImage('SMD0.IMG', '/SMD0.IMG').then(function(smdOk) {
    diskImageStatus.smd = smdOk;
    if (smdOk && typeof driveRegistry !== 'undefined') {
      driveRegistry.mount('smd', 0, 'demo', 'SINTRAN K (Demo)', 'SMD0.IMG', 0);
    }

    var statusElement = document.getElementById('status');
    if (!statusElement) return;

    if (diskImageStatus.smd) {
      statusElement.textContent = 'WebAssembly module loaded!';
    } else {
      statusElement.textContent = 'Warning: Failed to load SMD0.IMG. Check console.';
      console.warn('SMD0.IMG not found. Ensure disk image is served alongside the WASM files.');
    }

    // Signal mounts ready (demo mode)
    window.dispatchEvent(new CustomEvent('smd-mounts-ready', { detail: { mounted: smdOk ? 1 : 0 } }));
  });
}

// Persistent mode: mount SMD units from OPFS
function loadDiskImagesPersistent() {
  var statusEl = document.getElementById('status');

  return smdStorage.init().then(function(available) {
    if (!available) {
      console.warn('[Persist] OPFS not available, falling back to demo mode');
      if (statusEl) statusEl.textContent = 'OPFS unavailable - using demo mode';
      return loadDiskImage('SMD0.IMG', '/SMD0.IMG').then(function(ok) {
        diskImageStatus.smd = ok;
      });
    }

    return smdStorage.refreshMetadata().then(function() {
      return mountPersistentUnits();
    }).then(function() {
      // If no disk on unit 0 after mounting, show a helpful message
      if (!smdStorage.getUnitAssignment(0)) {
        if (statusEl) statusEl.textContent = 'No disk on Unit 0. Open HDD Disk Manager to assign an image.';
      }
    });
  });
}

// Mount all assigned units from OPFS
function mountPersistentUnits() {
  var units = smdStorage.getUnitAssignments();
  var promises = [];
  var mountedFiles = {};  // Track files already opened (OPFS allows only one SyncAccessHandle per file)

  for (var u = 0; u < 4; u++) {
    if (!units[u]) continue;

    // Skip duplicate file assignments - OPFS SyncAccessHandle is exclusive per file
    if (mountedFiles[units[u]]) {
      console.warn('[Persist] Unit ' + u + ' skipped: ' + units[u] + ' already mounted on unit ' + mountedFiles[units[u]]);
      continue;
    }
    mountedFiles[units[u]] = u;

    (function(unit, fileName) {
      var meta = smdStorage.getMetadata(fileName);
      var displayName = meta ? meta.name : fileName;
      console.log('[Persist] Mounting unit ' + unit + ': "' + displayName + '" (uuid=' + fileName + ')');

      if (emu.isWorkerMode()) {
        // Worker mode: use SyncAccessHandle (true per-block persistence)
        promises.push(
          emu.opfsMountSMD(unit, fileName).then(function(result) {
            if (result.ok) {
              console.log('[Persist] Unit ' + unit + ' mounted from OPFS: ' + displayName + ' (' + formatBytes(result.size || 0) + ')');
              if (unit === 0) diskImageStatus.smd = true;
              if (typeof driveRegistry !== 'undefined') {
                driveRegistry.mount('smd', unit, 'opfs', displayName, fileName, result.size || 0);
              }
              return { ok: true, unit: unit };
            }
            console.error('[Persist] Failed to mount unit ' + unit + ': ' + displayName);
            return { ok: false, unit: unit, error: 'OPFS mount failed', name: displayName };
          }).catch(function(err) {
            console.error('[Persist] Mount error unit ' + unit + ':', err);
            return { ok: false, unit: unit, error: err.message, name: displayName };
          })
        );
      } else {
        // Direct mode: load entire image into buffer
        promises.push(
          smdStorage.retrieveImage(fileName).then(function(data) {
            if (!data) {
              console.error('[Persist] Image not found in OPFS: ' + displayName + ' (' + fileName + ')');
              return { ok: false, unit: unit, error: 'Image not found in OPFS', name: displayName };
            }
            var rc = emu.mountSMDFromBuffer(unit, data);
            if (rc === 0) {
              console.log('[Persist] Unit ' + unit + ' mounted from buffer: ' + displayName + ' (' + formatBytes(data.byteLength) + ')');
              if (unit === 0) diskImageStatus.smd = true;
              if (typeof driveRegistry !== 'undefined') {
                driveRegistry.mount('smd', unit, 'opfs', displayName, fileName, data.byteLength || 0);
              }
              return { ok: true, unit: unit };
            }
            console.error('[Persist] mountSMDFromBuffer failed for unit ' + unit);
            return { ok: false, unit: unit, error: 'Buffer mount failed', name: displayName };
          })
        );
      }
    })(u, units[u]);
  }

  return Promise.all(promises).then(function(results) {
    var statusEl = document.getElementById('status');
    var mounted = results.filter(function(r) { return r && r.ok; }).length;
    var failed = results.filter(function(r) { return r && !r.ok; });

    if (statusEl) {
      if (failed.length > 0) {
        var failMsg = failed.map(function(f) { return 'Unit ' + f.unit + ' ("' + f.name + '"): ' + f.error; }).join('; ');
        statusEl.textContent = 'Mount failed: ' + failMsg;
        console.error('[Persist] Mount failures:', failMsg);
      } else if (mounted > 0) {
        statusEl.textContent = 'Persistent storage: ' + mounted + ' drive(s) mounted';
      } else {
        statusEl.textContent = 'No drives mounted from persistent storage';
      }
    }

    // Signal that all persistent mounts are complete (toolbar listens for this)
    window.dispatchEvent(new CustomEvent('smd-mounts-ready', { detail: { mounted: mounted, failed: failed.length } }));
  });
}

// Main initialization function called from Module.onRuntimeInitialized
function initializeSystem() {
  console.log("System initialization starting...");
  document.getElementById('toolbar-power').disabled = false;

  loadDiskImages()
    .then(() => {
      return document.fonts.ready;
    })
    .then(() => {
      // initializeTerminals is defined in terminal-manager.js which loads later in the page.
      // If WASM + disk images resolve from cache before the parser reaches that script,
      // the function won't exist yet. Wait for DOMContentLoaded in that case.
      return new Promise(function(resolve) {
        function runTerminals() {
          initializeTerminals();
          console.log("Terminals initialized");
          resolve();
        }
        if (typeof initializeTerminals === 'function') {
          runTerminals();
        } else {
          document.addEventListener('DOMContentLoaded', function() {
            initializeTerminals();
            console.log("Terminals initialized (deferred)");
            resolve();
          });
        }
      });
    })
    .then(() => {
      if (typeof scheduleAutoUrlImageBoot === 'function') {
        scheduleAutoUrlImageBoot();
      }
    })
    .catch(error => {
      console.error("Error during initialization:", error);
      document.getElementById('status').textContent = 'Error during initialization: ' + error.message;
    });
}
