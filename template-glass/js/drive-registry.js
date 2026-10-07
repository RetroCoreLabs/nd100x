//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// drive-registry.js - Unified disk mount state registry
//
// Single source of truth for all drive mount state in the UI.
// Replaces scattered state trackers (_gatewayMountedUnits, currentMountedFloppy,
// localStorage OPFS, DOM text, diskImageStatus) with one coherent registry.
//
// All JS modules use driveRegistry.mount()/eject()/get() instead of
// maintaining their own state. The registry fires onChange callbacks
// so the Machine Info window updates automatically.
//
// Must load BEFORE smd-manager.js and floppy-browser.js.

(function() {
  'use strict';

  // =========================================================
  // Internal state
  // =========================================================
  // Keys: 'smd-0' through 'smd-3', 'floppy-0', 'floppy-1'
  var _drives = {};
  var _listeners = [];

  function makeKey(type, unit) {
    return type + '-' + unit;
  }

  function emptyEntry() {
    return {
      mounted: false,
      source: null,     // 'demo' | 'opfs' | 'gateway' | 'library' | null
      name: '',
      fileName: null,
      imageSize: 0
    };
  }

  // Initialize all slots
  var i;
  for (i = 0; i < 4; i++) {
    _drives[makeKey('smd', i)] = emptyEntry();
  }
  for (i = 0; i < 2; i++) {
    _drives[makeKey('floppy', i)] = emptyEntry();
  }

  // =========================================================
  // Change notification
  // =========================================================
  function notifyListeners(type, unit, action) {
    for (var j = 0; j < _listeners.length; j++) {
      try {
        _listeners[j](type, unit, action);
      } catch (e) {
        console.error('[DriveRegistry] Listener error:', e);
      }
    }
  }

  // =========================================================
  // Public API
  // =========================================================
  var registry = {

    /**
     * Get mount state copy for a specific drive.
     * @param {string} type - 'smd' or 'floppy'
     * @param {number} unit - unit number
     * @returns {object} copy of mount entry
     */
    get: function(type, unit) {
      var key = makeKey(type, unit);
      var entry = _drives[key];
      if (!entry) return emptyEntry();
      return {
        type: type,
        unit: unit,
        mounted: entry.mounted,
        source: entry.source,
        name: entry.name,
        fileName: entry.fileName,
        imageSize: entry.imageSize
      };
    },

    /**
     * Get all drives as a flat array.
     * @returns {Array} array of drive entries with type and unit
     */
    getAll: function() {
      var result = [];
      var u;
      for (u = 0; u < 4; u++) {
        result.push(registry.get('smd', u));
      }
      for (u = 0; u < 2; u++) {
        result.push(registry.get('floppy', u));
      }
      return result;
    },

    /**
     * Quick check if a drive slot is occupied.
     * @returns {boolean}
     */
    isOccupied: function(type, unit) {
      var key = makeKey(type, unit);
      var entry = _drives[key];
      return entry ? entry.mounted : false;
    },

    /**
     * Find unit number where a fileName is already mounted.
     * @param {string} type - 'smd' or 'floppy'
     * @param {string} fileName - file name to search for
     * @returns {number} unit number or -1 if not found
     */
    findByFileName: function(type, fileName) {
      if (!fileName) return -1;
      var maxUnit = (type === 'smd') ? 4 : 2;
      for (var u = 0; u < maxUnit; u++) {
        var entry = _drives[makeKey(type, u)];
        if (entry && entry.mounted && entry.fileName === fileName) {
          return u;
        }
      }
      return -1;
    },

    /**
     * Mount a drive. Handles conflict resolution:
     * - If same fileName already on another unit -> eject that unit first
     * - If target unit occupied -> eject it first
     *
     * @param {string} type - 'smd' or 'floppy'
     * @param {number} unit - unit number
     * @param {string} source - 'demo' | 'opfs' | 'gateway' | 'library'
     * @param {string} name - display name
     * @param {string|null} fileName - file name (for duplicate detection)
     * @param {number} imageSize - image size in bytes
     */
    mount: function(type, unit, source, name, fileName, imageSize) {
      var key = makeKey(type, unit);
      if (!_drives[key]) return;

      // Auto-eject same fileName from another unit (prevent duplicates)
      if (fileName) {
        var existingUnit = registry.findByFileName(type, fileName);
        if (existingUnit >= 0 && existingUnit !== unit) {
          console.log('[DriveRegistry] Auto-ejecting ' + fileName + ' from ' + type + ' unit ' + existingUnit);
          registry.eject(type, existingUnit);
        }
      }

      // Eject current occupant if different
      var current = _drives[key];
      if (current.mounted) {
        registry.eject(type, unit);
      }

      // Set new mount state
      _drives[key] = {
        mounted: true,
        source: source || 'demo',
        name: name || '',
        fileName: fileName || null,
        imageSize: imageSize || 0
      };

      notifyListeners(type, unit, 'mount');
    },

    /**
     * Eject a drive. Clears mount state and notifies listeners.
     * Does NOT call emu.unmount* - the caller is responsible for that.
     *
     * @param {string} type - 'smd' or 'floppy'
     * @param {number} unit - unit number
     */
    eject: function(type, unit) {
      var key = makeKey(type, unit);
      if (!_drives[key]) return;

      var wasMounted = _drives[key].mounted;
      _drives[key] = emptyEntry();

      if (wasMounted) {
        notifyListeners(type, unit, 'eject');
      }
    },

    /**
     * Subscribe to mount state changes.
     * Callback signature: function(type, unit, action)
     * where action is 'mount' or 'eject'.
     *
     * @param {function} callback
     */
    onChange: function(callback) {
      if (typeof callback === 'function') {
        _listeners.push(callback);
      }
    },

    /**
     * Sync registry from C backend state via emu.getDriveInfo().
     * Called after boot completes to pick up demo-mode mounts.
     */
    syncFromBackend: function() {
      if (typeof emu === 'undefined' || !emu.isReady || !emu.isReady()) return;

      var result = emu.getDriveInfo();
      // Handle both sync (direct mode returns array) and async (worker mode returns Promise)
      if (result && typeof result.then === 'function') {
        result.then(function(drives) {
          applySyncData(drives);
        });
      } else if (Array.isArray(result)) {
        applySyncData(result);
      }
    }
  };

  function applySyncData(drives) {
    if (!Array.isArray(drives)) return;

    for (var d = 0; d < drives.length; d++) {
      var info = drives[d];
      var key = makeKey(info.type, info.unit);
      var current = _drives[key];
      if (!current) continue;

      if (info.mounted && !current.mounted) {
        // Backend has a mount we don't know about (e.g., demo boot mount)
        var source = 'demo';
        if (info.opfs) source = 'opfs';
        else if (info.gateway) source = 'gateway';

        _drives[key] = {
          mounted: true,
          source: source,
          name: info.name || '',
          fileName: null,
          imageSize: info.size || 0
        };
        notifyListeners(info.type, info.unit, 'mount');
      } else if (!info.mounted && current.mounted) {
        // Backend says not mounted but we think it is
        _drives[key] = emptyEntry();
        notifyListeners(info.type, info.unit, 'eject');
      }
    }
  }

  // =========================================================
  // Machine Info auto-update listener
  // =========================================================
  registry.onChange(function(type, unit, action) {
    var entry = registry.get(type, unit);
    var nameEl = document.getElementById('drive-name-' + type + '-' + unit);
    var badgeEl = document.getElementById('drive-badge-' + type + '-' + unit);

    if (nameEl) {
      nameEl.textContent = entry.mounted ? entry.name : (type === 'floppy' ? 'Empty' : 'Not mounted');
    }

    if (badgeEl) {
      if (entry.mounted && entry.source) {
        var labels = { demo: 'Demo', opfs: 'OPFS', gateway: 'Gateway', library: 'Library' };
        badgeEl.textContent = labels[entry.source] || entry.source;
        badgeEl.className = 'drive-source-badge badge-' + entry.source;
        badgeEl.style.display = '';
      } else {
        badgeEl.style.display = 'none';
      }
    }

    // Update floppy mount/eject button visibility
    if (type === 'floppy') {
      var driveNum = unit + 1;
      var mountBtn = document.getElementById('floppy-drive-' + driveNum + '-mount');
      var ejectBtn = document.getElementById('floppy-drive-' + driveNum + '-eject');
      if (mountBtn) mountBtn.style.display = entry.mounted ? 'none' : '';
      if (ejectBtn) ejectBtn.style.display = entry.mounted ? '' : 'none';
    }
  });

  // =========================================================
  // Expose globally
  // =========================================================
  window.driveRegistry = registry;

})();
