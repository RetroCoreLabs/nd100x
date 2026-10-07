//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs -- https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// smd-storage.js - OPFS-based persistent storage for SMD disk images
//
// Provides browser-local persistent storage so SMD images survive across sessions.
// Worker mode uses SyncAccessHandle for per-block persistence.
// Direct mode uses async OPFS API with auto-save on stop.
// Falls back gracefully when OPFS is unavailable.
//
// Storage is UUID-keyed: each image in OPFS has a UUID filename.
// Metadata (name, description, source) is stored in localStorage.

var smdStorage = (function() {
  'use strict';

  // =========================================================
  // State
  // =========================================================
  var _initialized = false;
  var _available = false;        // Is OPFS usable?
  var _opfsRoot = null;          // OPFS root directory handle
  var _imagesDir = null;         // 'smd-images' directory handle
  var _metadata = {};            // uuid -> { uuid, name, description, size, date, sourceName }
  var _dirty = {};               // unit -> true (Direct mode dirty tracking)

  // localStorage keys
  var UNITS_KEY = 'nd100x-smd-units';
  var BOOT_KEY = 'nd100x-smd-boot-unit';
  var META_KEY = 'nd100x-smd-metadata';  // fallback if OPFS metadata unavailable

  // =========================================================
  // UUID generation
  // =========================================================
  function generateUUID() {
    return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function(c) {
      var r = Math.random() * 16 | 0;
      return (c === 'x' ? r : (r & 0x3 | 0x8)).toString(16);
    });
  }

  var UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;

  // =========================================================
  // Initialization
  // =========================================================
  async function init() {
    if (_initialized) return _available;

    // Try OPFS
    try {
      if (typeof navigator.storage !== 'undefined' &&
          typeof navigator.storage.getDirectory === 'function') {
        _opfsRoot = await navigator.storage.getDirectory();
        _imagesDir = await _opfsRoot.getDirectoryHandle('smd-images', { create: true });
        _available = true;
        console.log('[HDD Library] OPFS available');
      }
    } catch (e) {
      console.warn('[HDD Library] OPFS not available:', e.message);
      _available = false;
    }

    // Load metadata from localStorage (always, as backup)
    try {
      var saved = localStorage.getItem(META_KEY);
      if (saved) _metadata = JSON.parse(saved);
    } catch (e) {}

    // Migrate old filename-keyed entries to UUID-keyed
    if (_available) {
      await _migrateIfNeeded();
    }

    _initialized = true;
    return _available;
  }

  function isAvailable() {
    return _available;
  }

  // =========================================================
  // Metadata persistence
  // =========================================================
  function _saveMetadata() {
    try {
      localStorage.setItem(META_KEY, JSON.stringify(_metadata));
    } catch (e) {}
  }

  // =========================================================
  // Migration: old filename keys -> UUID keys
  // =========================================================
  async function _migrateIfNeeded() {
    var needsMigration = Object.keys(_metadata).some(function(k) { return !UUID_RE.test(k); });
    if (!needsMigration) return;

    console.log('[HDD Library] Migrating old filename-keyed entries to UUID format...');

    var oldKeys = Object.keys(_metadata).filter(function(k) { return !UUID_RE.test(k); });
    var units = getUnitAssignments();

    for (var i = 0; i < oldKeys.length; i++) {
      var oldKey = oldKeys[i];
      var oldMeta = _metadata[oldKey];
      var uuid = generateUUID();

      // Rename OPFS file: old filename -> UUID
      try {
        var oldHandle = await _imagesDir.getFileHandle(oldKey);
        var file = await oldHandle.getFile();
        var data = await file.arrayBuffer();

        // Create new file with UUID name
        var newHandle = await _imagesDir.getFileHandle(uuid, { create: true });
        var writable = await newHandle.createWritable();
        await writable.write(data);
        await writable.close();

        // Remove old file
        await _imagesDir.removeEntry(oldKey);
      } catch (e) {
        console.warn('[HDD Library] Migration: could not rename OPFS file ' + oldKey + ':', e.message);
        // If the OPFS file doesn't exist, still migrate the metadata
      }

      // Migrate metadata
      _metadata[uuid] = {
        uuid: uuid,
        name: oldMeta.name || oldKey,
        description: oldMeta.description || '',
        size: oldMeta.size || 0,
        date: oldMeta.date || new Date().toISOString().split('T')[0],
        sourceName: oldMeta.sourceName || ''
      };
      delete _metadata[oldKey];

      // Update unit assignments that referenced the old filename
      for (var u = 0; u < 4; u++) {
        if (units[u] === oldKey) {
          units[u] = uuid;
        }
      }

      console.log('[HDD Library] Migrated ' + oldKey + ' -> ' + uuid);
    }

    // Save updated unit assignments and metadata
    try {
      localStorage.setItem(UNITS_KEY, JSON.stringify(units));
    } catch (e) {}
    _saveMetadata();
    console.log('[HDD Library] Migration complete');
  }

  // =========================================================
  // Image CRUD
  // =========================================================

  // Store an image to OPFS
  // uuid: string (caller-generated), data: Uint8Array, meta: { name, description, sourceName }
  async function storeImage(uuid, data, meta) {
    if (!_available || !_imagesDir) {
      throw new Error('OPFS not available');
    }

    // Check available quota before writing
    try {
      var est = await getStorageEstimate();
      if (est.quota > 0) {
        var available = est.quota - est.usage;
        if (data.byteLength > available) {
          throw new Error('Insufficient storage: need ' + formatSize(data.byteLength) +
            ' but only ' + formatSize(available) + ' available');
        }
      }
    } catch (e) {
      // If it's our own quota error, re-throw; otherwise ignore estimate failures
      if (e.message.indexOf('Insufficient storage') === 0) throw e;
    }

    console.log('[HDD Library] Writing ' + uuid + ': ' + formatSize(data.byteLength) +
      ', first 8 bytes: ' + Array.from(data.slice(0, Math.min(8, data.byteLength)))
        .map(function(b) { return ('0' + b.toString(16)).slice(-2); }).join(' '));

    var fileHandle = await _imagesDir.getFileHandle(uuid, { create: true });
    var writable = await fileHandle.createWritable();
    await writable.write(data);
    await writable.close();

    // Verify the file was written correctly
    var verifyFile = await fileHandle.getFile();
    var verifySize = verifyFile.size;
    if (verifySize !== data.byteLength) {
      throw new Error('OPFS write verification failed: wrote ' + formatSize(data.byteLength) +
        ' but file is ' + formatSize(verifySize));
    }
    console.log('[HDD Library] OPFS file size verified: ' + formatSize(verifySize));

    _metadata[uuid] = {
      uuid: uuid,
      name: (meta && meta.name) || uuid,
      description: (meta && meta.description) || '',
      size: data.byteLength,
      date: new Date().toISOString().split('T')[0],
      sourceName: (meta && meta.sourceName) || '',
      // Disk type this image is for: 'smd' (default), 'scsi', or 'winchester'.
      // Used as a hard mount constraint - a SCSI ID accepts only 'scsi' images.
      diskType: (meta && meta.diskType) || 'smd'
    };
    _saveMetadata();

    console.log('[HDD Library] Stored ' + uuid + ' (' + formatSize(data.byteLength) + ') OK');
    return true;
  }

  // Retrieve an image from OPFS as Uint8Array
  async function retrieveImage(uuid) {
    if (!_available || !_imagesDir) return null;

    try {
      var fileHandle = await _imagesDir.getFileHandle(uuid);
      var file = await fileHandle.getFile();
      var buffer = await file.arrayBuffer();
      return new Uint8Array(buffer);
    } catch (e) {
      console.warn('[HDD Library] Could not retrieve ' + uuid + ':', e.message);
      return null;
    }
  }

  // Delete an image from OPFS
  // Returns false if the image is currently mounted (caller should eject first)
  async function deleteImage(uuid) {
    if (!_available || !_imagesDir) return false;

    // Check if the image is assigned to any unit - caller must eject first
    var units = getUnitAssignments();
    for (var u = 0; u < 4; u++) {
      if (units[u] === uuid) {
        throw new Error('Cannot delete: image is assigned to unit ' + u + '. Eject it first.');
      }
    }

    try {
      await _imagesDir.removeEntry(uuid);
    } catch (e) {
      // File may not exist or may be locked
      console.warn('[HDD Library] Could not remove OPFS file ' + uuid + ':', e.message);
    }

    delete _metadata[uuid];
    _saveMetadata();

    console.log('[HDD Library] Deleted ' + uuid);
    return true;
  }

  // List all stored images (returns array of metadata objects)
  function listImages() {
    var result = [];
    for (var key in _metadata) {
      result.push(_metadata[key]);
    }
    return result;
  }

  // Quick existence check by UUID
  function imageExists(uuid) {
    return !!_metadata[uuid];
  }

  // Update metadata (name/description) without re-copying data
  function updateMetadata(uuid, name, description) {
    if (!_metadata[uuid]) return;
    _metadata[uuid].name = name;
    _metadata[uuid].description = description;
    _saveMetadata();
  }

  // Get metadata for a specific UUID
  function getMetadata(uuid) {
    return _metadata[uuid] || null;
  }

  // Refresh metadata by scanning OPFS directory
  async function refreshMetadata() {
    if (!_available || !_imagesDir) return;

    var found = {};
    for await (var entry of _imagesDir.values()) {
      if (entry.kind === 'file') {
        found[entry.name] = true;
        if (!_metadata[entry.name]) {
          // File exists in OPFS but not in metadata - add basic entry
          try {
            var file = await entry.getFile();
            _metadata[entry.name] = {
              uuid: entry.name,
              name: entry.name,
              description: '',
              size: file.size,
              date: new Date().toISOString().split('T')[0],
              sourceName: ''
            };
          } catch (e) {}
        }
      }
    }

    // Remove metadata for files that no longer exist
    for (var key in _metadata) {
      if (!found[key]) {
        delete _metadata[key];
      }
    }

    _saveMetadata();
  }

  // =========================================================
  // Unit assignments (persisted in localStorage)
  // Now stores UUIDs instead of filenames
  // =========================================================

  function getUnitAssignments() {
    try {
      var saved = localStorage.getItem(UNITS_KEY);
      if (saved) return JSON.parse(saved);
    } catch (e) {}
    return { 0: null, 1: null, 2: null, 3: null };
  }

  // Get display name for a unit assignment
  function getUnitAssignment(unit) {
    var units = getUnitAssignments();
    return units[unit] || null;
  }

  function setUnitAssignment(unit, uuid) {
    if (unit < 0 || unit > 3) return;
    var units = getUnitAssignments();

    // Prevent assigning the same image to multiple units (OPFS SyncAccessHandle is exclusive)
    if (uuid) {
      for (var u = 0; u < 4; u++) {
        if (u !== unit && units[u] === uuid) {
          console.warn('[HDD Library] ' + uuid + ' already assigned to unit ' + u + ', clearing it first');
          units[u] = null;
        }
      }
    }

    units[unit] = uuid;
    try {
      localStorage.setItem(UNITS_KEY, JSON.stringify(units));
    } catch (e) {}
  }

  function clearUnitAssignment(unit) {
    setUnitAssignment(unit, null);
  }

  // =========================================================
  // Boot drive (persisted in localStorage)
  // =========================================================

  function getBootUnit() {
    try {
      var saved = localStorage.getItem(BOOT_KEY);
      if (saved !== null) {
        var u = parseInt(saved);
        if (u >= 0 && u <= 3) return u;
      }
    } catch (e) {}
    return 0;  // default: unit 0
  }

  function setBootUnit(unit) {
    if (unit < 0 || unit > 3) return;
    try {
      localStorage.setItem(BOOT_KEY, '' + unit);
    } catch (e) {}
  }

  // =========================================================
  // Dirty tracking (session-scoped, Direct mode auto-save)
  // =========================================================

  function markDirty(unit) {
    _dirty[unit] = true;
  }

  function isDirty(unit) {
    return !!_dirty[unit];
  }

  function clearDirty(unit) {
    delete _dirty[unit];
  }

  function getDirtyUnits() {
    var result = [];
    for (var u in _dirty) {
      if (_dirty[u]) result.push(parseInt(u));
    }
    return result;
  }

  // =========================================================
  // Storage info
  // =========================================================

  async function getStorageEstimate() {
    try {
      if (navigator.storage && navigator.storage.estimate) {
        return await navigator.storage.estimate();
      }
    } catch (e) {}
    return { usage: 0, quota: 0 };
  }

  async function requestPersistence() {
    try {
      if (navigator.storage && navigator.storage.persist) {
        var granted = await navigator.storage.persist();
        console.log('[HDD Library] Persistence ' + (granted ? 'granted' : 'denied'));
        return granted;
      }
    } catch (e) {}
    return false;
  }

  // =========================================================
  // OPFS file handle access (for Worker SyncAccessHandle)
  // =========================================================

  // Get the raw file handle for a stored image (for Worker to open SyncAccessHandle)
  async function getFileHandle(uuid) {
    if (!_available || !_imagesDir) return null;
    try {
      return await _imagesDir.getFileHandle(uuid);
    } catch (e) {
      return null;
    }
  }

  // Get the images directory handle (for Worker)
  function getImagesDir() {
    return _imagesDir;
  }

  // =========================================================
  // Disk image validation
  // =========================================================

  // Validate a disk image before storing
  // Returns { valid: true } or { valid: false, error: 'reason' }
  function validateDiskImage(data) {
    if (!data || data.byteLength === 0) {
      return { valid: false, error: 'File is empty (0 bytes)' };
    }

    if (data.byteLength < 4096) {
      return { valid: false, error: 'File too small (' + formatSize(data.byteLength) + '). SMD images must be at least 4 KB.' };
    }

    // Check for HTML/XML error pages served as disk images
    var headerStr = String.fromCharCode.apply(null, data.slice(0, Math.min(16, data.byteLength)));
    if (headerStr.indexOf('<!DOC') === 0 || headerStr.indexOf('<html') === 0 ||
        headerStr.indexOf('<HTML') === 0 || headerStr.indexOf('<?xml') === 0) {
      return { valid: false, error: 'Data is HTML/XML, not a disk image. The server returned an error page.' };
    }

    return { valid: true };
  }

  // Check if boot sector (first 1024 bytes = 1 SMD block) is all zeros
  function isBootSectorEmpty(data) {
    if (!data || data.byteLength < 4) return true;
    var checkLen = Math.min(1024, data.byteLength);
    for (var i = 0; i < checkLen; i++) {
      if (data[i] !== 0) return false;
    }
    return true;
  }

  // =========================================================
  // Helpers
  // =========================================================

  function formatSize(bytes) {
    if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + ' MB';
    if (bytes >= 1024) return (bytes / 1024).toFixed(0) + ' KB';
    return bytes + ' B';
  }

  // =========================================================
  // Public API
  // =========================================================

  return {
    init: init,
    isAvailable: isAvailable,

    // UUID generation
    generateUUID: generateUUID,

    // Image CRUD
    storeImage: storeImage,
    retrieveImage: retrieveImage,
    deleteImage: deleteImage,
    listImages: listImages,
    imageExists: imageExists,
    updateMetadata: updateMetadata,
    getMetadata: getMetadata,
    refreshMetadata: refreshMetadata,

    // Unit assignments
    getUnitAssignments: getUnitAssignments,
    getUnitAssignment: getUnitAssignment,
    setUnitAssignment: setUnitAssignment,
    clearUnitAssignment: clearUnitAssignment,

    // Boot drive
    getBootUnit: getBootUnit,
    setBootUnit: setBootUnit,

    // Dirty tracking
    markDirty: markDirty,
    isDirty: isDirty,
    clearDirty: clearDirty,
    getDirtyUnits: getDirtyUnits,

    // Storage info
    getStorageEstimate: getStorageEstimate,
    requestPersistence: requestPersistence,

    // OPFS handles (for Worker integration)
    getFileHandle: getFileHandle,
    getImagesDir: getImagesDir,

    // Validation
    validateDiskImage: validateDiskImage,
    isBootSectorEmpty: isBootSectorEmpty,

    // Helpers
    formatSize: formatSize
  };

})();
