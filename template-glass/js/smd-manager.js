//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// HackerCorp Labs -- https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// smd-manager.js - SMD Disk Manager window UI
//
// Provides a glass window for managing persistent SMD disk images:
// - Drive configuration (4 units, boot drive selection)
// - Local disk library (list, import, rename, delete, copy from catalog)
// - Server catalog browsing with "Copy to local disk library" dialog
// - Storage quota display

'use strict';

// =========================================================
// Show / Hide
// =========================================================

// Opens in every mode. The local library needs persistent storage and the
// window says so (hdd-persist-notice, toolbar.js); the running machine's
// drives and the server catalog are there regardless.
function smdManagerShow() {
  var win = document.getElementById('smd-manager-window');
  if (!win) return;
  win.style.display = 'flex';
  windowManager.focus('smd-manager-window');
  saveWindowVisibility('smd-manager-window', true);
  smdRefreshAll();
}

function smdManagerHide() {
  var win = document.getElementById('smd-manager-window');
  if (win) win.style.display = 'none';
  saveWindowVisibility('smd-manager-window', false);
}

// =========================================================
// Refresh all sections
// =========================================================

// Names in the local library are unique: a second "SINTRAN K" next to the
// first is exactly the confusion a name is there to prevent. Case-insensitive,
// trimmed; <exceptUuid> is the image being renamed.
function smdNameInUse(name, exceptUuid) {
  var want = String(name || '').trim().toLowerCase();
  if (!want || typeof smdStorage === 'undefined') return false;
  var imgs = smdStorage.listImages();
  for (var i = 0; i < imgs.length; i++) {
    if (imgs[i].uuid === exceptUuid) continue;
    if (String(imgs[i].name || '').trim().toLowerCase() === want) return true;
  }
  return false;
}

// A unique name for an import, which brings no name of its own: the file
// name, then "name (2)", "name (3)", ...
function smdUniqueImportName(name) {
  var base = String(name || 'image').trim() || 'image';
  if (!smdNameInUse(base)) return base;
  for (var n = 2; n < 1000; n++) {
    if (!smdNameInUse(base + ' (' + n + ')')) return base + ' (' + n + ')';
  }
  return base + ' (' + Date.now() + ')';
}

// The image counts on the HDD manager's tabs: "Winchester (2)".
function hddUpdateTabCounts() {
  if (typeof smdStorage === 'undefined') return;
  var counts = {};
  smdStorage.listImages().forEach(function(i) { var t = i.diskType || 'smd'; counts[t] = (counts[t] || 0) + 1; });
  var labels = { smd: 'SMD', scsi: 'SCSI', winchester: 'Winchester', ndix: 'NDIX' };
  var btns = document.querySelectorAll('#hdd-tabs .hdd-tab');
  for (var b = 0; b < btns.length; b++) {
    var tab = btns[b].getAttribute('data-hdd-tab');
    var type = HDD_TAB_DISK_TYPE[tab] || tab;
    btns[b].textContent = (labels[tab] || tab) + ' (' + (counts[type] || 0) + ')';
  }
}

// Where a freshly copied image can be found, by its type.
function copiedWhere(diskType) {
  if (diskType === 'scsi') return ' - SCSI tab';
  if (diskType === 'winchester') return ' - Winchester tab';
  if (diskType === 'floppy') return ' - mount it from the Floppy Library';
  if (diskType === 'nd500') return ' - NDIX tab; choose it in Machine Setup as the root disc';
  return '';
}

function smdRefreshAll() {
  // Ensure smdStorage is initialized before reading data.
  // init() loads metadata from localStorage; returns immediately if already initialized.
  smdStorage.init().then(function() {
    smdRefreshUnitDisplay();
    smdRefreshInstalledList();
    smdRefreshCatalogList();
    smdUpdateStorageInfo();
    // The other type tabs' libraries too: a copy made from this tab may
    // belong on one of them.
    if (typeof hddTypeRefresh === 'function') { hddTypeRefresh('scsi'); hddTypeRefresh('winchester'); hddTypeRefresh('nd500'); }
    hddUpdateTabCounts();
  });
}

// =========================================================
// Unit display
// =========================================================

function smdRefreshUnitDisplay() {
  var units = smdStorage.getUnitAssignments();

  for (var u = 0; u < 4; u++) {
    var nameEl = document.getElementById('smd-unit-' + u + '-name');
    var ejectBtn = document.getElementById('smd-unit-' + u + '-eject');

    if (!nameEl) continue;

    // Check registry for current mount state (covers all sources)
    var regEntry = (typeof driveRegistry !== 'undefined') ? driveRegistry.get('smd', u) : null;

    if (regEntry && regEntry.mounted) {
      var displayName = regEntry.name || 'Mounted';
      if (regEntry.source === 'gateway') displayName += ' (Gateway)';
      nameEl.textContent = displayName;
      nameEl.classList.remove('empty');
      if (ejectBtn) ejectBtn.style.display = '';
    } else if (units[u]) {
      // Show the image name from metadata instead of UUID
      var meta = smdStorage.getMetadata(units[u]);
      nameEl.textContent = meta ? meta.name : units[u];
      nameEl.classList.remove('empty');
      if (ejectBtn) ejectBtn.style.display = '';
    } else {
      nameEl.textContent = 'Not assigned';
      nameEl.classList.add('empty');
      if (ejectBtn) ejectBtn.style.display = 'none';
    }
  }
}

// =========================================================
// Installed list (Local Disk Library)
// =========================================================

function smdRefreshInstalledList() {
  var container = document.getElementById('smd-installed-list');
  if (!container) return;

  // Hard constraint: the SMD tab lists only SMD-type images (diskType defaults
  // to 'smd' for images stored before disk-type tagging existed).
  var images = smdStorage.listImages().filter(function(i) { return (i.diskType || 'smd') === 'smd'; });

  if (images.length === 0) {
    container.innerHTML = '<div class="smd-empty-msg">No SMD disk images stored. Copy one in from the Server Catalog below, or import one.</div>';
    return;
  }

  // Build reverse map: uuid -> unit number (or -1 if not assigned)
  var units = smdStorage.getUnitAssignments();
  var uuidToUnit = {};
  for (var u = 0; u < 4; u++) {
    if (units[u]) uuidToUnit[units[u]] = u;
  }

  var html = '';
  images.forEach(function(img) {
    var uuid = img.uuid;
    var assignedUnit = (uuidToUnit[uuid] !== undefined) ? uuidToUnit[uuid] : -1;
    html += '<div class="smd-image-card" data-uuid="' + escapeHtml(uuid) + '">';
    html += '<div class="smd-image-info">';
    html += '<span class="smd-image-name">' + escapeHtml(img.name) + '</span>' + smdTypeBadgeHTML(img.diskType);
    html += '<span class="smd-image-meta">' + smdStorage.formatSize(img.size) + ' &middot; ' + (img.date || '');
    if (assignedUnit >= 0) {
      html += ' &middot; Unit ' + assignedUnit;
    }
    html += '</span>';
    if (img.sourceName) {
      html += '<span class="smd-source-label">Source: ' + escapeHtml(img.sourceName) + '</span>';
    }
    if (img.description) {
      html += '<span class="smd-image-meta">' + escapeHtml(img.description) + '</span>';
    }
    html += '</div>';
    // No Assign here: a drive gets its image in Machine Setup (the machine
    // is the master of its drives). This window keeps the library itself.
    html += '<div class="smd-image-actions">';
    html += '<button class="smd-rename-btn" data-uuid="' + escapeHtml(uuid) + '" title="Rename">Rename</button>';
    html += '<button class="smd-export-btn" data-uuid="' + escapeHtml(uuid) + '" title="Export to local file">Export</button>';
    html += '<button class="smd-ndfs-btn" data-uuid="' + escapeHtml(uuid) + '" title="Browse the ND filesystem">NDFS</button>';
    html += '<button class="smd-delete-btn" data-uuid="' + escapeHtml(uuid) + '" title="Delete">Del</button>';
    html += '</div>';
    html += '</div>';
  });

  container.innerHTML = html;

  // Wire up rename buttons
  container.querySelectorAll('.smd-rename-btn').forEach(function(btn) {
    btn.addEventListener('click', function() {
      smdShowRenameInline(this.getAttribute('data-uuid'));
    });
  });

  // Wire up export buttons
  container.querySelectorAll('.smd-ndfs-btn').forEach(function(btn) {
    btn.addEventListener('click', function(e) {
      e.stopPropagation();
      if (typeof openNdfsViewer !== 'function') { alert('NDFS viewer not loaded'); return; }
      var uuid = this.getAttribute('data-uuid');
      var meta = smdStorage.getMetadata(uuid);
      var self = this;
      var orig = self.textContent;
      self.disabled = true; self.textContent = '...';
      smdStorage.retrieveImage(uuid).then(function(data) {
        self.disabled = false; self.textContent = orig;
        if (!data) { alert('No data for this image'); return; }
        openNdfsViewer(new Uint8Array(data), meta ? meta.name : 'SMD image');
      }).catch(function(err) {
        self.disabled = false; self.textContent = orig;
        alert('NDFS: ' + (err && err.message ? err.message : err));
      });
    });
  });

  container.querySelectorAll('.smd-export-btn').forEach(function(btn) {
    btn.addEventListener('click', function() {
      smdExportImage(this.getAttribute('data-uuid'));
    });
  });

  // Wire up delete buttons
  container.querySelectorAll('.smd-delete-btn').forEach(function(btn) {
    btn.addEventListener('click', function() {
      var uuid = this.getAttribute('data-uuid');
      var meta = smdStorage.getMetadata(uuid);
      var displayName = meta ? meta.name : uuid;
      if (confirm('Delete "' + displayName + '" from persistent storage?')) {
        smdDeleteImage(uuid);
      }
    });
  });
}

// =========================================================
// Inline rename
// =========================================================

function smdShowRenameInline(uuid) {
  var card = document.querySelector('.smd-image-card[data-uuid="' + uuid + '"]');
  if (!card) return;
  var meta = smdStorage.getMetadata(uuid);
  if (!meta) return;

  var infoEl = card.querySelector('.smd-image-info');
  if (!infoEl) return;

  // Replace info content with editable fields
  infoEl.innerHTML =
    '<div class="smd-rename-fields">' +
    '<input type="text" class="smd-rename-input" id="smd-rename-name" value="' + escapeHtml(meta.name) + '" placeholder="Name" maxlength="80">' +
    '</div>' +
    '<div class="smd-rename-fields">' +
    '<input type="text" class="smd-rename-input" id="smd-rename-desc" value="' + escapeHtml(meta.description) + '" placeholder="Description" maxlength="200">' +
    '<button class="smd-rename-save" id="smd-rename-save">Save</button>' +
    '<button class="smd-rename-cancel" id="smd-rename-cancel">Cancel</button>' +
    '</div>';

  var saveBtn = infoEl.querySelector('#smd-rename-save');
  var cancelBtn = infoEl.querySelector('#smd-rename-cancel');
  var nameInput = infoEl.querySelector('#smd-rename-name');
  var descInput = infoEl.querySelector('#smd-rename-desc');

  function doSave() {
    var newName = nameInput.value.trim() || meta.name;
    var newDesc = descInput.value.trim();
    if (smdNameInUse(newName, uuid)) {
      alert('There is already an image called "' + newName + '" in the library - names are unique.');
      nameInput.focus();
      return;
    }
    smdStorage.updateMetadata(uuid, newName, newDesc);
    smdRefreshAll();
  }

  saveBtn.addEventListener('click', doSave);
  cancelBtn.addEventListener('click', function() { smdRefreshAll(); });
  nameInput.addEventListener('keydown', function(e) {
    if (e.key === 'Enter') doSave();
    if (e.key === 'Escape') smdRefreshAll();
  });
  descInput.addEventListener('keydown', function(e) {
    if (e.key === 'Enter') doSave();
    if (e.key === 'Escape') smdRefreshAll();
  });
  nameInput.focus();
  nameInput.select();
}

// =========================================================
// Actions
// =========================================================

function smdAssignToUnit(uuid, unit) {
  if (unit < 0 || unit > 3) return;

  // For unit 0 (boot drive), warn if the image has an empty boot sector
  if (unit === 0) {
    smdStorage.retrieveImage(uuid).then(function(data) {
      if (data && smdStorage.isBootSectorEmpty(data)) {
        alert('Warning: This disk image has an empty boot sector (all zeros).\n\n' +
          'Booting from this image on unit 0 will not load an operating system.\n' +
          'This is normal for blank data disks.');
      }
    });
  }

  var currentUnits = smdStorage.getUnitAssignments();

  // If this image is already assigned to another unit, eject it first
  // (OPFS SyncAccessHandle is exclusive -- only one handle per file)
  for (var u = 0; u < 4; u++) {
    if (u !== unit && currentUnits[u] === uuid) {
      console.log('[HDD Manager] Ejecting ' + uuid + ' from unit ' + u + ' (moving to unit ' + unit + ')');
      smdEjectUnit(u);
    }
  }

  // Eject the CURRENT occupant of the target unit (close its SyncAccessHandle)
  if (currentUnits[unit] && currentUnits[unit] !== uuid) {
    console.log('[HDD Manager] Ejecting current image from unit ' + unit + ' to make room');
    smdEjectUnit(unit);
  }

  smdStorage.setUnitAssignment(unit, uuid);

  // Resolve display name early so it's available for all branches
  var meta = smdStorage.getMetadata(uuid);
  var displayName = meta ? meta.name : uuid;

  // Update registry immediately so Machine Info reflects the assignment
  if (typeof driveRegistry !== 'undefined') {
    driveRegistry.mount('smd', unit, 'opfs', displayName, uuid, meta ? meta.size || 0 : 0);
  }

  // Mount live if persistence is on.
  // No isReady() gate — opfsMountSMD works as soon as the worker is running.
  if (emu && isSmdPersistenceEnabled()) {
    console.log('[HDD Manager] Mounting ' + displayName + ' on unit ' + unit +
      ' (isReady=' + (emu.isReady ? emu.isReady() : 'N/A') + ')');
    if (emu.isWorkerMode()) {
      emu.opfsMountSMD(unit, uuid).then(function(r) {
        // Guard: if the assignment changed while the async mount was in flight, skip
        if (smdStorage.getUnitAssignment(unit) !== uuid) {
          console.log('[HDD Manager] Unit ' + unit + ' assignment changed during mount, skipping registry update');
          return;
        }
        if (r.ok) {
          console.log('[HDD Manager] Unit ' + unit + ' mounted: ' + displayName + ' (' + smdStorage.formatSize(r.size || 0) + ')');
          if (typeof driveRegistry !== 'undefined') {
            driveRegistry.mount('smd', unit, 'opfs', displayName, uuid, r.size || 0);
          }
        } else {
          console.error('[HDD Manager] Unit ' + unit + ' mount FAILED: ' + displayName);
          alert('Failed to mount "' + displayName + '" on unit ' + unit + '.\n\nThe image file may be corrupted or inaccessible.');
          smdStorage.clearUnitAssignment(unit);
          if (typeof driveRegistry !== 'undefined') driveRegistry.eject('smd', unit);
          smdRefreshAll();
        }
      });
    } else {
      smdStorage.retrieveImage(uuid).then(function(data) {
        // Guard: if the assignment changed while retrieving, skip
        if (smdStorage.getUnitAssignment(unit) !== uuid) {
          console.log('[HDD Manager] Unit ' + unit + ' assignment changed during retrieve, skipping');
          return;
        }
        if (data) {
          var rc = emu.mountSMDFromBuffer(unit, data);
          if (rc === 0) {
            console.log('[HDD Manager] Unit ' + unit + ' mounted from buffer: ' + displayName + ' (' + smdStorage.formatSize(data.byteLength) + ')');
            if (typeof driveRegistry !== 'undefined') {
              driveRegistry.mount('smd', unit, 'opfs', displayName, uuid, data.byteLength);
            }
          } else {
            console.error('[HDD Manager] Unit ' + unit + ' mount from buffer FAILED (rc=' + rc + ')');
            alert('Failed to mount "' + displayName + '" on unit ' + unit + '.');
            smdStorage.clearUnitAssignment(unit);
            if (typeof driveRegistry !== 'undefined') driveRegistry.eject('smd', unit);
            smdRefreshAll();
          }
        } else {
          console.error('[HDD Manager] Unit ' + unit + ' - image not found in OPFS: ' + uuid);
          alert('Image "' + displayName + '" not found in storage. It may have been deleted.');
          smdStorage.clearUnitAssignment(unit);
          if (typeof driveRegistry !== 'undefined') driveRegistry.eject('smd', unit);
          smdRefreshAll();
        }
      });
    }
  }

  smdRefreshAll();
}

function smdEjectUnit(unit) {
  if (unit < 0 || unit > 3) return;

  // Save dirty buffer first if Direct mode
  if (!emu.isWorkerMode() && smdStorage.isDirty(unit)) {
    smdSaveUnit(unit);
  }

  smdStorage.clearUnitAssignment(unit);

  // No isReady() gate — unmount works as soon as the worker is running.
  if (emu) {
    console.log('[HDD Manager] Ejecting unit ' + unit +
      ' (isReady=' + (emu.isReady ? emu.isReady() : 'N/A') + ')');
    if (emu.isWorkerMode()) {
      emu.opfsUnmountSMD(unit);
    } else {
      emu.unmountSMD(unit);
    }
  }

  if (typeof driveRegistry !== 'undefined') {
    driveRegistry.eject('smd', unit);
  }

  smdRefreshAll();
}

function smdSaveUnit(unit) {
  if (emu.isWorkerMode()) return; // Worker mode: SyncAccessHandle persists immediately

  var units = smdStorage.getUnitAssignments();
  var uuid = units[unit];
  if (!uuid) return;

  // Read buffer from WASM heap
  var ptr = emu.getSMDBuffer(unit);
  var size = emu.getSMDBufferSize(unit);
  if (!ptr || !size) return;

  var data = new Uint8Array(Module.HEAPU8.buffer, ptr, size);
  // Copy before async operation (WASM heap may grow)
  var copy = new Uint8Array(data);

  var meta = smdStorage.getMetadata(uuid);
  smdStorage.storeImage(uuid, copy, {
    name: meta ? meta.name : uuid,
    description: meta ? meta.description : 'Saved from emulator',
    sourceName: meta ? meta.sourceName : ''
  }).then(function() {
    smdStorage.clearDirty(unit);
    console.log('[HDD Manager] Unit ' + unit + ' saved to OPFS');
  }).catch(function(err) {
    console.error('[HDD Manager] Save failed for unit ' + unit + ':', err);
  });
}

function smdDeleteImage(uuid) {
  // Check if assigned to any unit - must eject first
  // Check both unit assignments AND registry (they can be out of sync)
  var units = smdStorage.getUnitAssignments();
  var needsEject = false;
  for (var u = 0; u < 4; u++) {
    var assignedHere = (units[u] === uuid);
    var registryHere = false;
    if (typeof driveRegistry !== 'undefined') {
      var regEntry = driveRegistry.get('smd', u);
      if (regEntry && regEntry.mounted && regEntry.fileName === uuid) {
        registryHere = true;
      }
    }
    if (assignedHere || registryHere) {
      needsEject = true;
      if (confirm('This image is mounted on unit ' + u + '. Eject it before deleting?')) {
        smdEjectUnit(u);
      } else {
        return;
      }
    }
  }

  smdStorage.deleteImage(uuid).then(function() {
    smdRefreshAll();
  }).catch(function(err) {
    alert('Delete failed: ' + err.message);
  });
}

function smdExportImage(uuid) {
  var meta = smdStorage.getMetadata(uuid);
  var exportName = meta ? (meta.name.replace(/[^a-zA-Z0-9._-]/g, '_') + '.IMG') : (uuid + '.IMG');

  smdStorage.retrieveImage(uuid).then(function(data) {
    if (!data) {
      console.error('[HDD Manager] Export failed: no data for ' + uuid);
      return;
    }
    var blob = new Blob([data], { type: 'application/octet-stream' });
    var url = URL.createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url;
    a.download = exportName;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  });
}

// <defaultType>: the tab the Import button was pressed on, preselected in the
// tag dialog (the file itself says nothing about its type).
function smdImportFromFile(defaultType) {
  var input = document.createElement('input');
  input.type = 'file';
  input.accept = '.img,.IMG';
  input.addEventListener('change', function() {
    var file = input.files[0];
    if (!file) return;

    var reader = new FileReader();
    reader.onload = function(ev) {
      var data = new Uint8Array(ev.target.result);

      // Validate disk image before storing
      var validation = smdStorage.validateDiskImage(data);
      if (!validation.valid) {
        alert('Import failed: ' + validation.error);
        console.error('[HDD Manager] Import rejected: ' + validation.error);
        return;
      }

      // A local file carries no type of its own - ask, rather than guess.
      // Defaults to 'smd' only as the dialog's starting selection.
      smdEnsureCatalog().then(function() {
        smdAskImportType(file.name, function(diskType) {
          var uuid = smdStorage.generateUUID();
          var displayName = smdUniqueImportName(file.name);

          smdStorage.storeImage(uuid, data, {
            name: displayName,
            description: 'Imported from local file',
            sourceName: file.name,
            diskType: diskType
          }).then(function() {
            smdRefreshAll();
            console.log('[HDD Manager] Imported ' + displayName + ' as ' + diskType +
              ' (' + smdStorage.formatSize(data.byteLength) + ')');
          }).catch(function(err) {
            alert('Import failed: ' + err.message);
            console.error('[HDD Manager] Import store failed:', err);
          });
        }, defaultType);
      });
    };
    reader.readAsArrayBuffer(file);
  });
  input.click();
}

// Show the "Tag Imported Disk" dialog and call onConfirm(diskType) once the
// user picks one and confirms. Does nothing further on Cancel - the file has
// not been stored yet at this point, so there is no partial state to undo.
function smdAskImportType(fileName, onConfirm, defaultType) {
  var dlg = document.getElementById('smd-import-dialog');
  var sel = document.getElementById('smd-import-type');
  var nameEl = document.getElementById('smd-import-filename');
  if (!dlg || !sel) {
    // Dialog not in this page (shouldn't happen) - fall back to the old
    // silent default rather than lose the import entirely.
    onConfirm('smd');
    return;
  }

  if (nameEl) nameEl.textContent = fileName;
  sel.innerHTML = smdDiskTypeOptionsHTML(defaultType || 'smd');
  dlg.style.display = '';

  var confirmBtn = document.getElementById('smd-import-confirm');
  var cancelBtn = document.getElementById('smd-import-cancel');

  function cleanup() {
    dlg.style.display = 'none';
    confirmBtn.onclick = null;
    cancelBtn.onclick = null;
  }

  confirmBtn.onclick = function() {
    var diskType = sel.value || 'smd';
    cleanup();
    onConfirm(diskType);
  };
  cancelBtn.onclick = function() {
    cleanup();
  };
}

// =========================================================
// Server Catalog browsing
// =========================================================

var _catalogData = null;

// ---------------------------------------------------------------------------
// Disk type tagging
// ---------------------------------------------------------------------------
// The choices offered when tagging an image (local import) come from the same
// disk-catalog.json the Server Catalog list below reads - not a second
// hardcoded list here - so a type the catalog already uses (including nd500,
// the NDIX root disk tag, which is not an ND-100 controller type and so is
// not in disk-types.js) shows up in the picker without a JS change.

function smdEnsureCatalog() {
  if (_catalogData) return Promise.resolve(_catalogData);
  return fetch('disk-catalog.json').then(function(r) { return r.json(); }).then(function(catalog) {
    _catalogData = catalog || [];
    return _catalogData;
  }).catch(function() {
    return [];
  });
}

// nd500 is a tag on the NDIX root disk, read only by the standalone ND-500
// machine (ndix-machine.js, Machine Setup) - it is never assigned to an ND-100
// controller, so disk-types.js (the real DRIVE_TYPE enum) does not carry it.
var EXTRA_DISK_TYPE_LABELS = { nd500: 'ND-500 / NDIX root' };

function smdDiskTypeLabel(t) {
  if (typeof diskTypes !== 'undefined' && diskTypes.DRIVE_TYPE_LABEL[t]) return diskTypes.DRIVE_TYPE_LABEL[t];
  return EXTRA_DISK_TYPE_LABELS[t] || t;
}

// Short form for the badge itself - DRIVE_TYPE_LABEL is a full word ("SMD
// disc", "Winchester (ST506)"), too long for an inline tag.
var DISK_TYPE_ABBR = { smd: 'SMD', scsi: 'SCSI', winchester: 'WD', floppy: 'FD', nd500: 'ND5' };

// A small colored tag naming <type>, for the catalog, local library and
// remote (gateway) image lists - one shared look everywhere a disk's type
// needs to be visible without reading the tab or section it is in.
function smdTypeBadgeHTML(t) {
  t = t || 'smd';
  var abbr = DISK_TYPE_ABBR[t] || t.toUpperCase();
  return '<span class="smd-type-badge smd-type-' + escapeHtml(t) + '" title="' +
    escapeHtml(smdDiskTypeLabel(t)) + '">' + escapeHtml(abbr) + '</span>';
}

// Every type disk-types.js knows as a real controller, plus whatever other
// diskType values the catalog itself uses (nd500 today).
function smdDiskTypeOptionsHTML(selected) {
  var types = (typeof diskTypes !== 'undefined') ? diskTypes.DRIVE_TYPE_NAMES.slice() : ['smd'];
  (_catalogData || []).forEach(function(e) {
    var t = e.diskType || 'smd';
    if (types.indexOf(t) < 0) types.push(t);
  });
  var html = '';
  types.forEach(function(t) {
    html += '<option value="' + t + '"' + (t === selected ? ' selected' : '') + '>' +
            escapeHtml(smdDiskTypeLabel(t)) + '</option>';
  });
  return html;
}

function smdRefreshCatalogList() {
  var container = document.getElementById('smd-catalog-list');
  if (!container) return;

  if (_catalogData) {
    smdRenderCatalog(container, _catalogData);
    return;
  }

  fetch('disk-catalog.json').then(function(r) { return r.json(); }).then(function(catalog) {
    _catalogData = catalog || [];
    smdRenderCatalog(container, _catalogData);
  }).catch(function(err) {
    container.innerHTML = '<div class="smd-empty-msg">Could not load catalog: ' + escapeHtml(err.message) + '</div>';
  });
}

// The HDD manager's active tab decides which disk type the Server Catalog
// and Remote Images sections (shared under every tab) show.
var _hddActiveTab = 'smd';
var HDD_TAB_DISK_TYPE = { smd: 'smd', scsi: 'scsi', winchester: 'winchester', ndix: 'nd500' };
function smdListFilter(selectId) {
  return HDD_TAB_DISK_TYPE[_hddActiveTab] || 'smd';
}

function smdRenderCatalog(container, catalog) {
  if (!catalog || catalog.length === 0) {
    container.innerHTML = '<div class="smd-empty-msg">No images available in server catalog.</div>';
    return;
  }
  var want = smdListFilter('smd-catalog-filter');
  if (want !== 'all') {
    var all = catalog;
    catalog = all.filter(function(e) { return (e.diskType || 'smd') === want; });
    if (catalog.length === 0) {
      container.innerHTML = '<div class="smd-empty-msg">No ' + escapeHtml(smdDiskTypeLabel(want)) + ' images in the server catalog.</div>';
      return;
    }
  }

  var html = '';
  catalog.forEach(function(entry, idx) {
    var isGeneratable = !!entry.generate;
    var isAvailable = entry.available !== false; // static catalog has no 'available' field

    html += '<div class="smd-image-card smd-catalog-card' + (!isAvailable ? ' unavailable' : '') + '">';
    html += '<div class="smd-image-info">';
    html += '<span class="smd-image-name">' + escapeHtml(entry.name) + '</span>' + smdTypeBadgeHTML(entry.diskType);
    html += '<span class="smd-image-meta">' + smdStorage.formatSize(entry.size);
    html += ' &middot; ' + escapeHtml(entry.description);
    html += '</span>';
    html += '</div>';
    html += '<div class="smd-image-actions">';
    if (!isAvailable) {
      html += '<span class="smd-image-meta">Unavailable</span>';
    } else if (isGeneratable) {
      html += '<button class="smd-action-btn smd-catalog-dl-btn" data-idx="' + idx + '" title="Create a blank image in local library">Copy to Library</button>';
    } else {
      html += '<button class="smd-action-btn smd-catalog-dl-btn" data-idx="' + idx + '" title="Copy to local disk library">Copy to Library</button>';
    }
    html += '</div>';
    html += '</div>';
  });

  container.innerHTML = html;

  // Wire up copy buttons
  container.querySelectorAll('.smd-catalog-dl-btn').forEach(function(btn) {
    btn.addEventListener('click', function() {
      var idx = parseInt(this.getAttribute('data-idx'));
      smdShowCopyDialog(catalog[idx]);
    });
  });
}

// =========================================================
// Copy to Library dialog
// =========================================================

function smdShowCopyDialog(entry) {
  var dlg = document.getElementById('smd-copy-dialog');
  if (!dlg) return;

  // For non-generated entries, verify the file exists on the server first
  if (!entry.generate && entry.url) {
    var statusEl = document.getElementById('smd-download-status');
    if (statusEl) statusEl.textContent = 'Checking availability of ' + entry.name + '...';

    fetch(entry.url, { method: 'HEAD' }).then(function(resp) {
      if (statusEl) statusEl.textContent = '';
      if (!resp.ok) {
        console.error('[HDD Manager] Catalog image not found: ' + entry.url + ' (HTTP ' + resp.status + ')');
        alert('Image "' + entry.name + '" is not available on the server (HTTP ' + resp.status + ').\n\n' +
          'The catalog entry may be outdated or the file has not been uploaded yet.');
        return;
      }
      smdShowCopyDialogInner(entry, dlg);
    }).catch(function(err) {
      if (statusEl) statusEl.textContent = '';
      console.error('[HDD Manager] Catalog image check failed: ' + entry.url, err);
      alert('Cannot reach "' + entry.name + '" on the server.\n\n' +
        'Network error: ' + err.message);
    });
  } else {
    smdShowCopyDialogInner(entry, dlg);
  }
}

function smdShowCopyDialogInner(entry, dlg) {
  document.getElementById('smd-copy-name').value = entry.name || '';
  document.getElementById('smd-copy-desc').value = entry.description || '';

  document.getElementById('smd-copy-confirm').onclick = function() {
    var name = document.getElementById('smd-copy-name').value.trim() || entry.name;
    var desc = document.getElementById('smd-copy-desc').value.trim();
    if (smdNameInUse(name)) {
      var st = document.getElementById('smd-download-status');
      if (st) st.textContent = 'There is already an image called "' + name + '" in the library - give this one another name.';
      document.getElementById('smd-copy-name').focus();
      return;
    }
    dlg.style.display = 'none';
    smdDoCopy(smdStorage.generateUUID(), name, desc, entry);
  };

  document.getElementById('smd-copy-cancel').onclick = function() {
    dlg.style.display = 'none';
  };

  dlg.style.display = '';
}

function smdDoCopy(uuid, name, description, entry) {
  var statusEl = document.getElementById('smd-download-status');
  if (statusEl) statusEl.textContent = (entry.generate ? 'Creating ' : 'Copying ') + name + '...';

  console.log('[HDD Copy] Starting copy: "' + name + '" uuid=' + uuid +
    (entry.generate ? ' (GENERATING BLANK DISK ' + smdStorage.formatSize(entry.size) + ' - NOT downloading!)' :
      ' url=' + entry.url + ' expectedSize=' + smdStorage.formatSize(entry.size || 0)));

  var dataPromise;
  if (entry.generate) {
    console.log('[HDD Copy] Creating blank disk image (all zeros) - this is a data disk, not bootable');
    dataPromise = Promise.resolve(new Uint8Array(entry.size));
  } else {
    if (!entry.url) {
      console.error('[HDD Copy] CATALOG ERROR: entry has no URL and generate is not set');
      if (statusEl) statusEl.textContent = 'Failed: catalog entry has no download URL';
      alert('Catalog error: "' + name + '" has no download URL configured.\n\nThe disk-catalog.json entry needs a "url" field pointing to the image file.');
      return;
    }
    dataPromise = downloadImageBuffer(entry.url).then(function(buf) {
      return new Uint8Array(buf);
    });
  }

  var downloadedData = null;

  dataPromise.then(function(data) {
    downloadedData = data;

    console.log('[HDD Copy] Downloaded: ' + smdStorage.formatSize(data.byteLength) +
      ', first 16 bytes: ' + Array.from(data.slice(0, 16))
        .map(function(b) { return ('0' + b.toString(16)).slice(-2); }).join(' '));

    // Validate downloaded data (skip for generated blank disks - they're intentionally all zeros)
    if (!entry.generate) {
      // Check downloaded size matches expected catalog size
      if (entry.size && data.byteLength !== entry.size) {
        throw new Error('Size mismatch: downloaded ' + smdStorage.formatSize(data.byteLength) +
          ' but catalog says ' + smdStorage.formatSize(entry.size) +
          '. The file on the server may be wrong.');
      }

      var validation = smdStorage.validateDiskImage(data);
      if (!validation.valid) {
        throw new Error('Validation failed: ' + validation.error);
      }

      // Check boot sector (first 1024 bytes = 1 block) for all zeros
      var bootAllZero = true;
      var checkLen = Math.min(1024, data.byteLength);
      for (var i = 0; i < checkLen; i++) {
        if (data[i] !== 0) { bootAllZero = false; break; }
      }
      if (bootAllZero) {
        console.warn('[HDD Copy] WARNING: First ' + checkLen + ' bytes are all zeros (empty boot sector)');
      } else {
        console.log('[HDD Copy] Boot sector has data (non-zero within first ' + checkLen + ' bytes)');
      }

      // Check entire image for all zeros (completely blank file)
      var allZero = true;
      for (var j = 0; j < data.byteLength; j++) {
        if (data[j] !== 0) { allZero = false; break; }
      }
      if (allZero) {
        throw new Error('Downloaded file is entirely zeros (' + smdStorage.formatSize(data.byteLength) +
          ' of nothing). The server returned an empty or corrupt file.');
      }
    }

    console.log('[HDD Copy] Validation passed, storing to OPFS...');
    return smdStorage.storeImage(uuid, data, {
      name: name,
      description: description,
      sourceName: entry.name,
      // BUG FIXED 07-OCT-2026: this was missing, so every catalog entry was
      // stored as diskType 'smd' regardless of what the catalog said -
      // the Winchester and NDIX entries went into the library untagged, and
      // their tabs never showed them.
      diskType: entry.diskType || 'smd'
    });
  }).then(function() {
    // Verify: read back from OPFS and compare
    console.log('[HDD Copy] Store complete, verifying readback...');
    return smdStorage.retrieveImage(uuid);
  }).then(function(readback) {
    if (!readback) {
      throw new Error('Readback verification failed: could not read image back from OPFS');
    }
    if (readback.byteLength !== downloadedData.byteLength) {
      throw new Error('Readback size mismatch: wrote ' + smdStorage.formatSize(downloadedData.byteLength) +
        ' but read back ' + smdStorage.formatSize(readback.byteLength));
    }

    // Compare first 4096 bytes (boot area)
    var compareLen = Math.min(4096, readback.byteLength);
    var mismatch = -1;
    for (var i = 0; i < compareLen; i++) {
      if (readback[i] !== downloadedData[i]) { mismatch = i; break; }
    }
    if (mismatch >= 0) {
      console.error('[HDD Copy] READBACK MISMATCH at byte ' + mismatch +
        ': wrote 0x' + downloadedData[mismatch].toString(16) +
        ' but read 0x' + readback[mismatch].toString(16));
      throw new Error('Data corruption: OPFS readback differs from downloaded data at byte ' + mismatch);
    }

    console.log('[HDD Copy] Readback OK: ' + smdStorage.formatSize(readback.byteLength) +
      ', first 16 bytes match: ' + Array.from(readback.slice(0, 16))
        .map(function(b) { return ('0' + b.toString(16)).slice(-2); }).join(' '));

    downloadedData = null; // free memory

    if (statusEl) {
      // Say WHERE it is: the library lists are per type, so an image copied
      // from the SMD tab lands on another tab when it is not an SMD image.
      statusEl.textContent = name + ' added to the local library' + copiedWhere(entry.diskType || 'smd');
      setTimeout(function() {
        if (statusEl && statusEl.textContent.indexOf(name) >= 0) statusEl.textContent = '';
      }, 3000);
    }
    smdRefreshAll();
    // Show the tab the image is on, so it is visible right away.
    var tabOf = { smd: 'smd', scsi: 'scsi', winchester: 'winchester', nd500: 'ndix' };
    if (tabOf[entry.diskType || 'smd'] && typeof hddSelectTab === 'function') hddSelectTab(tabOf[entry.diskType || 'smd']);
  }).catch(function(err) {
    downloadedData = null;
    var errMsg = err.message || String(err);
    if (statusEl) statusEl.textContent = 'Failed: ' + errMsg;
    console.error('[HDD Copy] FAILED: ' + errMsg, err);
    alert('Failed to copy "' + name + '" to library.\n\n' + errMsg);
  });
}

// =========================================================
// Storage info
// =========================================================

function smdUpdateStorageInfo() {
  var infoEl = document.getElementById('smd-storage-info');
  if (!infoEl) return;

  smdStorage.getStorageEstimate().then(function(est) {
    if (est.quota > 0) {
      infoEl.textContent = 'Storage: ' + smdStorage.formatSize(est.usage) + ' used of ' + smdStorage.formatSize(est.quota);
    } else {
      var images = smdStorage.listImages();
      var total = 0;
      images.forEach(function(img) { total += img.size || 0; });
      infoEl.textContent = 'Storage: ' + smdStorage.formatSize(total) + ' used';
    }
  });
}

// =========================================================
// Helpers
// =========================================================

function escapeHtml(str) {
  if (typeof str !== 'string') str = String(str == null ? '' : str);
  return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

// =========================================================
// Persistence toggle handler
// =========================================================

function smdHandlePersistToggle(enabled) {
  var configToggle = document.getElementById('config-smd-persist');
  localStorage.setItem(SMD_PERSIST_KEY, enabled ? 'true' : 'false');
  if (enabled) {
    // Turning on persistence - need to reload to switch from MEMFS to OPFS
    if (confirm('Enabling persistent storage requires a page reload.\n\nThe current SMD image will be downloaded and stored in your browser.\nAfter this, disk writes will persist across sessions.\n\nReload now?')) {
      location.reload();
    } else {
      // User cancelled - revert toggle
      localStorage.setItem(SMD_PERSIST_KEY, 'false');
      if (configToggle) configToggle.checked = false;
    }
  } else {
    // Turning off persistence - reload to switch back to MEMFS
    if (confirm('Disabling persistent storage requires a page reload.\n\nThe emulator will return to demo mode (SMD image downloaded each visit).\nYour stored images will be kept in browser storage.\n\nReload now?')) {
      location.reload();
    } else {
      localStorage.setItem(SMD_PERSIST_KEY, 'true');
      if (configToggle) configToggle.checked = true;
    }
  }
}

// =========================================================
// Auto-save: periodic timer + page unload (Direct mode only)
// =========================================================

var _autoSaveInterval = null;
var AUTO_SAVE_PERIOD_MS = 60000; // 60 seconds

function smdStartAutoSave() {
  if (_autoSaveInterval) return;
  _autoSaveInterval = setInterval(function() {
    if (!isSmdPersistenceEnabled()) return;
    if (emu && emu.isWorkerMode && emu.isWorkerMode()) return;

    var dirtyUnits = smdStorage.getDirtyUnits();
    dirtyUnits.forEach(function(unit) {
      console.log('[HDD Manager] Auto-saving unit ' + unit);
      smdSaveUnit(unit);
    });
  }, AUTO_SAVE_PERIOD_MS);
}

function smdStopAutoSave() {
  if (_autoSaveInterval) {
    clearInterval(_autoSaveInterval);
    _autoSaveInterval = null;
  }
}

// Start auto-save when persistence is enabled and we're in Direct mode
if (isSmdPersistenceEnabled()) {
  smdStartAutoSave();
}

// Best-effort save on page unload (backup for auto-save)
window.addEventListener('beforeunload', function(e) {
  if (!isSmdPersistenceEnabled()) return;
  if (emu && emu.isWorkerMode && emu.isWorkerMode()) return;

  var dirtyUnits = smdStorage.getDirtyUnits();
  if (dirtyUnits.length > 0) {
    dirtyUnits.forEach(function(unit) {
      smdSaveUnit(unit);
    });
    e.preventDefault();
    e.returnValue = 'Disk changes are being saved...';
  }
});

// =========================================================
// Remote Images (Gateway)
// =========================================================

var _gatewayDiskList = null;      // { smd: [...], floppy: [...] }
var _gatewayBlockIO = false;      // true if SharedArrayBuffer available for sync block I/O

// Called by proxy when disk-list arrives from gateway sub-worker
window.onDiskList = function(data) {
  _gatewayDiskList = data;
  smdRefreshRemoteList();
  window.dispatchEvent(new CustomEvent('gateway-disk-list', { detail: data }));
};

window.onDiskConnected = function(blockIO) {
  _gatewayBlockIO = !!blockIO;
  smdRefreshRemoteList();
};

window.onDiskDisconnected = function() {
  _gatewayDiskList = null;
  _gatewayBlockIO = false;
  // Eject all gateway mounts from the registry
  if (typeof driveRegistry !== 'undefined') {
    driveRegistry.getAll().forEach(function(d) {
      if (d.source === 'gateway') driveRegistry.eject(d.type, d.unit);
    });
  }
  smdRefreshRemoteList();
  window.dispatchEvent(new CustomEvent('gateway-disk-disconnected'));
};

function smdRefreshRemoteList() {
  var container = document.getElementById('smd-remote-list');
  if (!container) return;

  if (!_gatewayDiskList) {
    container.innerHTML = '<div class="smd-empty-msg">No gateway connection. Connect via Network settings to access remote disk images.</div>';
    return;
  }

  var smdImages = _gatewayDiskList.smd || [];
  var floppyImages = _gatewayDiskList.floppy || [];

  // The type filter above the list. The gateway lists SMD and floppy images;
  // any other type simply has nothing to show.
  var wantRemote = smdListFilter('smd-remote-filter');
  if (wantRemote !== 'all') {
    if (wantRemote !== 'smd') smdImages = [];
    if (wantRemote !== 'floppy') floppyImages = [];
    if (smdImages.length === 0 && floppyImages.length === 0) {
      container.innerHTML = '<div class="smd-empty-msg">No ' + escapeHtml(smdDiskTypeLabel(wantRemote)) + ' images on the gateway.</div>';
      return;
    }
  }

  if (smdImages.length === 0 && floppyImages.length === 0) {
    container.innerHTML = '<div class="smd-empty-msg">No disk images configured on gateway server.<br>' +
      '<span style="font-size:0.85em;opacity:0.7">Add image paths to smd.images / floppy.images in gateway.conf.json</span></div>';
    return;
  }

  var html = '';

  if (smdImages.length > 0) {
    html += '<div class="smd-remote-section-title">SMD Images</div>';
    smdImages.forEach(function(img) {
      // Check registry for gateway mount state
      var mounted = false;
      var mountedToLocal = -1;
      if (typeof driveRegistry !== 'undefined') {
        for (var u2 = 0; u2 < 4; u2++) {
          var e = driveRegistry.get('smd', u2);
          if (e.mounted && e.source === 'gateway' && e.name === img.name) {
            mounted = true;
            mountedToLocal = u2;
            break;
          }
        }
      }
      html += '<div class="smd-image-card">';
      html += '<div class="smd-image-info">';
      html += '<span class="smd-image-name">' + escapeHtml(img.name) + '</span>' + smdTypeBadgeHTML('smd');
      html += '<span class="smd-image-meta">' + smdStorage.formatSize(img.size) + ' &middot; Gateway unit ' + img.unit;
      if (mounted && mountedToLocal >= 0) html += ' &rarr; Local unit ' + mountedToLocal;
      html += '</span>';
      html += '</div>';
      html += '<div class="smd-image-actions">';
      if (mounted) {
        html += '<span class="smd-badge-gateway">Gateway</span>';
        html += '<button class="smd-eject-remote-btn" data-type="smd" data-unit="' + mountedToLocal + '">Eject</button>';
      } else {
        html += '<select class="smd-assign-remote-select" data-type="smd" data-remote-unit="' + img.unit + '" data-size="' + img.size + '" data-name="' + escapeHtml(img.name) + '" title="Mount to unit">';
        html += '<option value="">Mount to...</option>';
        for (var su = 0; su < 4; su++) {
          var occupied = (typeof driveRegistry !== 'undefined') ? driveRegistry.isOccupied('smd', su) : false;
          html += '<option value="' + su + '"' + (occupied ? ' disabled' : '') + '>Unit ' + su + (occupied ? ' (in use)' : '') + '</option>';
        }
        html += '</select>';
        var persistOk = isSmdPersistenceEnabled();
        var disabledAttr = persistOk ? '' : ' disabled title="Enable persistent storage first"';
        html += '<button class="smd-copy-gateway-btn smd-action-btn"' + disabledAttr +
                ' data-type="smd" data-remote-unit="' + img.unit + '"' +
                ' data-size="' + img.size + '"' +
                ' data-name="' + escapeHtml(img.name) + '">Copy to Library</button>';
      }
      html += '</div>';
      html += '</div>';
    });
  }

  if (floppyImages.length > 0) {
    html += '<div class="smd-remote-section-title">Floppy Images</div>';
    floppyImages.forEach(function(img) {
      // Check registry for gateway floppy mount state
      var fmounted = false;
      var fmountedToLocal = -1;
      if (typeof driveRegistry !== 'undefined') {
        for (var fu = 0; fu < 2; fu++) {
          var fe = driveRegistry.get('floppy', fu);
          if (fe.mounted && fe.source === 'gateway' && fe.name === img.name) {
            fmounted = true;
            fmountedToLocal = fu;
            break;
          }
        }
      }
      html += '<div class="smd-image-card">';
      html += '<div class="smd-image-info">';
      html += '<span class="smd-image-name">' + escapeHtml(img.name) + '</span>' + smdTypeBadgeHTML('floppy');
      html += '<span class="smd-image-meta">' + smdStorage.formatSize(img.size) + ' &middot; Gateway unit ' + img.unit + '</span>';
      html += '</div>';
      html += '<div class="smd-image-actions">';
      if (fmounted) {
        html += '<span class="smd-badge-gateway">Gateway</span>';
        html += '<button class="smd-eject-remote-btn" data-type="floppy" data-unit="' + fmountedToLocal + '">Eject</button>';
      } else {
        html += '<button class="smd-mount-remote-btn" data-type="floppy" data-remote-unit="' + img.unit + '" data-size="' + img.size + '" data-name="' + escapeHtml(img.name) + '">Mount</button>';
        var fPersistOk = isSmdPersistenceEnabled();
        var fDisabledAttr = fPersistOk ? '' : ' disabled title="Enable persistent storage first"';
        html += '<button class="smd-copy-gateway-btn smd-action-btn"' + fDisabledAttr +
                ' data-type="floppy" data-remote-unit="' + img.unit + '"' +
                ' data-size="' + img.size + '"' +
                ' data-name="' + escapeHtml(img.name) + '">Copy to Library</button>';
      }
      html += '</div>';
      html += '</div>';
    });
  }

  container.innerHTML = html;

  // Wire up mount dropdowns for SMD
  container.querySelectorAll('.smd-assign-remote-select').forEach(function(sel) {
    sel.addEventListener('change', function() {
      if (this.value !== '') {
        var localUnit = parseInt(this.value);
        var remoteUnit = parseInt(this.getAttribute('data-remote-unit'));
        var size = parseInt(this.getAttribute('data-size'));
        var driveType = this.getAttribute('data-type');
        var imgName = this.getAttribute('data-name') || '';
        smdMountRemote(driveType, remoteUnit, localUnit, size, imgName);
        this.value = '';
      }
    });
  });

  // Wire up mount buttons for floppy
  container.querySelectorAll('.smd-mount-remote-btn').forEach(function(btn) {
    btn.addEventListener('click', function() {
      var remoteUnit = parseInt(this.getAttribute('data-remote-unit'));
      var size = parseInt(this.getAttribute('data-size'));
      var imgName = this.getAttribute('data-name') || '';
      smdMountRemote('floppy', remoteUnit, 0, size, imgName);
    });
  });

  // Wire up eject buttons
  container.querySelectorAll('.smd-eject-remote-btn').forEach(function(btn) {
    btn.addEventListener('click', function() {
      var driveType = this.getAttribute('data-type');
      var unit = parseInt(this.getAttribute('data-unit'));
      smdEjectRemote(driveType, unit);
    });
  });

  // Wire up Copy to Library buttons
  container.querySelectorAll('.smd-copy-gateway-btn').forEach(function(btn) {
    btn.addEventListener('click', function() {
      var driveType = this.getAttribute('data-type') === 'floppy' ? 1 : 0;
      var remoteUnit = parseInt(this.getAttribute('data-remote-unit'));
      var size = parseInt(this.getAttribute('data-size'));
      var imgName = this.getAttribute('data-name') || '';
      smdCopyGatewayToLibrary(driveType, remoteUnit, size, imgName, this);
    });
  });
}

function smdMountRemote(driveType, remoteUnit, localUnit, imageSize, imageName) {
  if (!emu || !emu.isWorkerMode || !emu.isWorkerMode()) {
    console.warn('[HDD Manager] Gateway disk mounting requires Worker mode');
    return;
  }

  var mountFn = (driveType === 'smd') ? emu.gatewayMountSMD : emu.gatewayMountFloppy;

  mountFn(localUnit, imageSize).then(function(r) {
    if (r.ok) {
      if (typeof driveRegistry !== 'undefined') {
        driveRegistry.mount(driveType, localUnit, 'gateway', imageName || ('Gateway ' + driveType), null, imageSize);
      }
      console.log('[HDD Manager] Gateway ' + driveType + ' unit ' + remoteUnit + ' mounted to local unit ' + localUnit);
    } else {
      console.error('[HDD Manager] Gateway mount failed for ' + driveType + ' unit ' + remoteUnit);
    }
    smdRefreshRemoteList();
    smdRefreshUnitDisplay();
  });
}

function smdEjectRemote(driveType, unit) {
  var unmountFn = (driveType === 'smd') ? emu.gatewayUnmountSMD : emu.gatewayUnmountFloppy;

  unmountFn(unit);
  if (typeof driveRegistry !== 'undefined') {
    driveRegistry.eject(driveType, unit);
  }
  smdRefreshRemoteList();
  smdRefreshUnitDisplay();
}

// =========================================================
// Copy Gateway Image to Library
// =========================================================

function smdCopyGatewayToLibrary(driveType, remoteUnit, size, name, btnEl) {
  if (!emu || !emu.isWorkerMode || !emu.isWorkerMode()) {
    console.warn('[HDD Manager] Gateway copy requires Worker mode');
    return;
  }
  if (!isSmdPersistenceEnabled()) {
    console.warn('[HDD Manager] Persistent storage must be enabled');
    return;
  }

  // Show the copy dialog for name/description
  var dlg = document.getElementById('smd-copy-dialog');
  if (!dlg) return;

  document.getElementById('smd-copy-name').value = name || '';
  document.getElementById('smd-copy-desc').value = '';

  document.getElementById('smd-copy-confirm').onclick = function() {
    var copyName = document.getElementById('smd-copy-name').value.trim() || name;
    var copyDesc = document.getElementById('smd-copy-desc').value.trim();
    if (smdNameInUse(copyName)) {
      var st = document.getElementById('smd-download-status');
      if (st) st.textContent = 'There is already an image called "' + copyName + '" in the library - give this one another name.';
      document.getElementById('smd-copy-name').focus();
      return;
    }
    dlg.style.display = 'none';
    smdDoGatewayCopy(driveType, remoteUnit, size, copyName, copyDesc, btnEl);
  };

  document.getElementById('smd-copy-cancel').onclick = function() {
    dlg.style.display = 'none';
  };

  dlg.style.display = '';
}

function smdDoGatewayCopy(driveType, remoteUnit, size, name, description, btnEl) {
  // Find the card and replace actions area with a progress bar
  var card = btnEl ? btnEl.closest('.smd-image-card') : null;
  var actionsEl = card ? card.querySelector('.smd-image-actions') : null;

  if (actionsEl) {
    actionsEl.innerHTML =
      '<div class="smd-copy-progress">' +
      '<div class="smd-copy-progress-bar" style="width:0%"></div>' +
      '</div>' +
      '<span class="smd-copy-progress-text">0%</span>';
  }

  // Set up progress callback
  window.onGatewayCopyProgress = function(p) {
    var pct = p.total > 0 ? Math.round((p.done / p.total) * 100) : 0;
    if (actionsEl) {
      var bar = actionsEl.querySelector('.smd-copy-progress-bar');
      var text = actionsEl.querySelector('.smd-copy-progress-text');
      if (bar) bar.style.width = pct + '%';
      if (text) text.textContent = pct + '% (' + smdStorage.formatSize(p.done) + ' / ' + smdStorage.formatSize(p.total) + ')';
    }
  };

  emu.gatewayReadFullImage(driveType, remoteUnit, size).then(function(msg) {
    window.onGatewayCopyProgress = null;

    if (msg.error) {
      console.error('[HDD Manager] Gateway copy failed:', msg.error);
      smdRefreshRemoteList();
      return;
    }

    var uuid = smdStorage.generateUUID();
    var data = new Uint8Array(msg.buffer);

    // Validate received data
    if (size && data.byteLength !== size) {
      console.error('[HDD Manager] Gateway copy size mismatch: got ' + smdStorage.formatSize(data.byteLength) +
        ' but expected ' + smdStorage.formatSize(size));
      if (actionsEl) actionsEl.innerHTML = '<span class="smd-image-meta" style="color:#f44">Copy failed: incomplete transfer</span>';
      smdRefreshRemoteList();
      return;
    }

    var validation = smdStorage.validateDiskImage(data);
    if (!validation.valid) {
      console.error('[HDD Manager] Gateway copy validation failed:', validation.error);
      if (actionsEl) actionsEl.innerHTML = '<span class="smd-image-meta" style="color:#f44">Copy failed: ' + escapeHtml(validation.error) + '</span>';
      smdRefreshRemoteList();
      return;
    }

    smdStorage.storeImage(uuid, data, {
      name: name,
      description: description,
      sourceName: name,
      // BUG FIXED 07-OCT-2026: driveType names the remote unit's own type
      // (smd/scsi/winchester) and was already in scope here, just never
      // passed through - every gateway copy was stored as diskType 'smd'.
      diskType: driveType || 'smd'
    }).then(function() {
      console.log('[HDD Manager] Gateway image copied to library: ' + name + ' (' + smdStorage.formatSize(data.byteLength) + ')');
      smdRefreshRemoteList();
      smdRefreshInstalledList();
      smdUpdateStorageInfo();
    }).catch(function(err) {
      console.error('[HDD Manager] Failed to store image:', err);
      smdRefreshRemoteList();
    });
  }).catch(function(err) {
    window.onGatewayCopyProgress = null;
    console.error('[HDD Manager] Gateway read failed:', err);
    smdRefreshRemoteList();
  });
}

// =========================================================
// Re-sync mounts after Init completes (safety net)
// =========================================================
// If the user assigned disks before Power On, the worker may not have received
// mount commands (pre-Fix: isReady() gate blocked them). After Init, re-apply
// all unit assignments so the C side matches the JS registry.

// Worker mode only: Init() rebuilt the drive tables, so every assigned SMD
// unit is mounted again from OPFS. Signals 'smd-mounts-ready' when ALL of
// them are done - the boot waits for that signal (toolbar.js), because
// reading sector 0 while a SyncAccessHandle is being re-opened fails
// ("Block read failed: got -1 blocks").
function smdSyncMountsToWorker() {
  if (!emu || !isSmdPersistenceEnabled()) return Promise.resolve();
  if (!emu.isWorkerMode || !emu.isWorkerMode()) return Promise.resolve();

  var units = smdStorage.getUnitAssignments();
  var pending = [];
  for (var u = 0; u < 4; u++) {
    if (!units[u]) continue;
    var meta = smdStorage.getMetadata(units[u]);
    var displayName = meta ? meta.name : units[u];
    console.log('[HDD Manager] Post-init sync: mounting ' + displayName + ' on unit ' + u);
    pending.push((function(unit, uuid, name) {
      return emu.opfsMountSMD(unit, uuid).then(function(r) {
        // Guard: if the assignment changed while mount was in flight, skip
        if (smdStorage.getUnitAssignment(unit) !== uuid) {
          console.log('[HDD Manager] Post-init sync: unit ' + unit + ' assignment changed, skipping');
          return false;
        }
        if (r.ok) {
          console.log('[HDD Manager] Post-init sync OK: unit ' + unit + ' = ' + name + ' (' + smdStorage.formatSize(r.size || 0) + ')');
          if (typeof driveRegistry !== 'undefined') {
            driveRegistry.mount('smd', unit, 'opfs', name, uuid, r.size || 0);
          }
          return true;
        }
        console.error('[HDD Manager] Post-init sync FAILED: unit ' + unit + ' = ' + name);
        return false;
      }).catch(function(e) {
        console.error('[HDD Manager] Post-init sync error: unit ' + unit + ' = ' + name, e);
        return false;
      });
    })(u, units[u], displayName));
  }
  return Promise.all(pending).then(function(results) {
    var mounted = results.filter(Boolean).length;
    window.dispatchEvent(new CustomEvent('smd-mounts-ready', { detail: { mounted: mounted, afterInit: true } }));
  });
}

// Listen for the 'emu-initialized' event dispatched by emu-proxy-worker.js
window.addEventListener('emu-initialized', function() {
  console.log('[HDD Manager] emu-initialized event received, syncing mounts');
  smdSyncMountsToWorker();
});

// Expose for toolbar.js
window.smdManagerShow = smdManagerShow;
window.smdManagerHide = smdManagerHide;

// Auto-refresh if the window was restored as visible (toolbar.js loads before us)
(function() {
  var win = document.getElementById('smd-manager-window');
  if (win && win.style.display !== 'none') {
    smdRefreshAll();
  }
})();

// =========================================================
// HDD manager tabs (SMD / SCSI / Winchester) - added for the SCSI controller.
// SMD keeps its existing UI (#smd-manager-body); SCSI uses #hdd-scsi-body.
// =========================================================
function hddSelectTab(tab) {
  // One body per tab, shown by name. Listed rather than derived so a body with
  // no matching tab (or the reverse) shows up as a missing element here instead
  // of as a blank panel.
  var bodies = {
    smd:        'smd-manager-body',
    scsi:       'hdd-scsi-body',
    winchester: 'hdd-winchester-body',
    ndix:       'hdd-ndix-body'
  };
  for (var name in bodies) {
    if (!Object.prototype.hasOwnProperty.call(bodies, name)) continue;
    var b = document.getElementById(bodies[name]);
    if (b) b.style.display = (tab === name) ? '' : 'none';
  }
  var btns = document.querySelectorAll('#hdd-tabs .hdd-tab');
  for (var i = 0; i < btns.length; i++) {
    var active = btns[i].getAttribute('data-hdd-tab') === tab;
    btns[i].classList.toggle('hdd-tab-active', active);
  }
  _hddActiveTab = tab;
  hddUpdateTabCounts();
  if (tab === 'scsi') hddTypeRefresh('scsi');
  if (tab === 'winchester') hddTypeRefresh('winchester');
  if (tab === 'ndix') hddTypeRefresh('nd500');
  // The shared catalog / remote sections follow the tab's type.
  var typeLabel = document.getElementById('hdd-shared-type');
  if (typeLabel) typeLabel.textContent = '- ' + smdDiskTypeLabel(HDD_TAB_DISK_TYPE[tab] || 'smd') + ' images';
  smdRefreshCatalogList();
  smdRefreshRemoteList();
}

// ---------------------------------------------------------------------------
// Disc-manager tabs, for any disk type
// ---------------------------------------------------------------------------
// These four functions were written for SCSI, with 'scsi' and the range 0..6
// hardcoded throughout. Winchester needs exactly the same behaviour over 0..1,
// and a tab list driven by the machine configuration needs it for whatever that
// machine turns out to have. Copying the block per type is how the SMD and SCSI
// paths drifted apart in the first place.
//
// Unit counts come from diskTypes (disk-types.js), which calls itself the single
// source of truth for them and is already kept in step with the C DRIVE_TYPE
// enum. Nothing here repeats a range.

// Per-type wiring: the element id prefix used in index.html, the label a unit
// goes by on screen, and the emu entry points. A type is one row.
var HDD_TYPES = {
  scsi: {
    idPrefix: 'scsi',
    bodyId: 'hdd-scsi-body',
    listId: 'hdd-scsi-installed-list',
    unitWord: 'ID',
    mountBuffer: 'mountSCSIFromBuffer',
    unmount: 'unmountSCSI',
    opfsMount: 'opfsMountSCSI',
    opfsUnmount: 'opfsUnmountSCSI'
  },
  // NDIX root discs: no units - an ND-500 NDIX machine names its root disc in
  // Machine Setup. The tab is the library view for the nd500 tag.
  nd500: {
    idPrefix: 'nd500',
    bodyId: 'hdd-ndix-body',
    listId: 'hdd-ndix-installed-list',
    unitWord: 'Unit',
    mountBuffer: null,
    unmount: null,
    opfsMount: null,
    opfsUnmount: null
  },
  winchester: {
    idPrefix: 'wd',
    bodyId: 'hdd-winchester-body',
    listId: 'hdd-winchester-installed-list',
    unitWord: 'Unit',
    mountBuffer: 'mountWinchesterFromBuffer',
    unmount: 'unmountWinchester',
    // No Worker-mode OPFS channel for Winchester yet: the Worker's OPFS command
    // set is per-type and only SMD and SCSI have one. Left undefined rather than
    // pointed at SCSI's, which would mount the image on the wrong controller.
    opfsMount: null,
    opfsUnmount: null
  }
};

function hddUnitCount(type) {
  return (typeof diskTypes !== 'undefined' && diskTypes.DRIVE_UNIT_COUNT[type]) || 0;
}

// Refresh the unit rows of <type> from the drive registry.
function hddTypeRefresh(type) {
  var cfg = HDD_TYPES[type];
  if (!cfg) return;
  var n = hddUnitCount(type);
  for (var u = 0; u < n; u++) {
    var nameEl = document.getElementById(cfg.idPrefix + '-unit-' + u + '-name');
    var ejectEl = document.getElementById(cfg.idPrefix + '-unit-' + u + '-eject');
    var entry = (typeof driveRegistry !== 'undefined') ? driveRegistry.get(type, u) : null;
    if (entry && entry.name) {
      if (nameEl) nameEl.textContent = entry.name;
      if (ejectEl) ejectEl.style.display = '';
    } else {
      if (nameEl) nameEl.textContent = 'Not assigned';
      if (ejectEl) ejectEl.style.display = 'none';
    }
  }
  hddTypeRefreshLibrary(type);
}

// The local library for <type>: only images tagged with that type. A hard
// constraint, not a filter for tidiness - assigning an SMD image to a SCSI ID
// gives the guest a disk with the wrong geometry.
function hddTypeRefreshLibrary(type) {
  var cfg = HDD_TYPES[type];
  if (!cfg) return;
  var container = document.getElementById(cfg.listId);
  if (!container) return;
  if (typeof smdStorage === 'undefined') return;

  var label = (typeof diskTypes !== 'undefined' && diskTypes.DRIVE_TYPE_LABEL[type]) || type;
  var n = hddUnitCount(type);
  var images = smdStorage.listImages().filter(function (i) {
    return (i.diskType || 'smd') === type;
  });
  if (images.length === 0) {
    container.innerHTML = '<div class="smd-empty-msg">No ' + label + ' disk images stored. ' +
      'Copy one in from the Server Catalog below, or import one and tag it ' + label + '.</div>';
    return;
  }

  // Which UUID is on which unit (from the registry).
  var unitOf = {};
  for (var id = 0; id < n; id++) {
    var e = (typeof driveRegistry !== 'undefined') ? driveRegistry.get(type, id) : null;
    if (e && e.fileName) unitOf[e.fileName] = id;
  }

  var html = '';
  images.forEach(function (img) {
    var uuid = img.uuid;
    var assigned = (unitOf[uuid] !== undefined) ? unitOf[uuid] : -1;
    html += '<div class="smd-image-card" data-uuid="' + escapeHtml(uuid) + '">';
    html += '<div class="smd-image-info">';
    html += '<span class="smd-image-name">' + escapeHtml(img.name) + '</span>' + smdTypeBadgeHTML(img.diskType);
    html += '<span class="smd-image-meta">' + smdStorage.formatSize(img.size) + ' &middot; ' + (img.date || '');
    if (assigned >= 0) html += ' &middot; ' + cfg.unitWord + ' ' + assigned;
    html += '</span>';
    if (img.description) html += '<span class="smd-image-meta">' + escapeHtml(img.description) + '</span>';
    html += '</div>';
    html += '<div class="smd-image-actions">';
    // The same actions as the SMD tab, in the same order - and no Assign on
    // any tab: a drive gets its image in Machine Setup. Only NDIX has no
    // NDFS: its root disc is a Unix filesystem, not an ND one.
    html += '<button class="smd-rename-btn hdd-rename-btn" data-uuid="' + escapeHtml(uuid) + '" title="Rename">Rename</button>';
    html += '<button class="smd-export-btn hdd-export-btn" data-uuid="' + escapeHtml(uuid) + '" title="Export to local file">Export</button>';
    if (type !== 'nd500') {
      html += '<button class="smd-ndfs-btn hdd-ndfs-btn" data-uuid="' + escapeHtml(uuid) + '" title="Browse the ND filesystem">NDFS</button>';
    }
    html += '<button class="smd-delete-btn hdd-delete-btn" data-type="' + type + '" data-uuid="' + escapeHtml(uuid) + '" title="Delete from the library">Del</button>';
    html += '</div></div>';
  });
  container.innerHTML = html;

  container.querySelectorAll('.hdd-rename-btn').forEach(function (btn) {
    btn.addEventListener('click', function () { smdShowRenameInline(this.getAttribute('data-uuid')); });
  });
  container.querySelectorAll('.hdd-export-btn').forEach(function (btn) {
    btn.addEventListener('click', function () { smdExportImage(this.getAttribute('data-uuid')); });
  });
  container.querySelectorAll('.hdd-ndfs-btn').forEach(function (btn) {
    btn.addEventListener('click', function () {
      var uuid = this.getAttribute('data-uuid'), meta = smdStorage.getMetadata(uuid);
      if (typeof openNdfsViewer !== 'function') { alert('NDFS viewer not loaded'); return; }
      smdStorage.retrieveImage(uuid).then(function (bytes) {
        if (!bytes) { alert('Image not found in the library'); return; }
        openNdfsViewer(bytes, (meta && meta.name) || 'Disk');
      });
    });
  });
  container.querySelectorAll('.hdd-delete-btn').forEach(function (btn) {
    btn.addEventListener('click', function () { hddTypeDeleteImage(this.getAttribute('data-type'), this.getAttribute('data-uuid')); });
  });
}

// Delete a library image of <type>: ejected first from any unit of that
// type it is on, then removed from the library. Same shape as
// smdDeleteImage for the SMD tab.
function hddTypeDeleteImage(type, uuid) {
  var meta = smdStorage.getMetadata(uuid);
  var displayName = (meta && meta.name) || uuid;
  if (!confirm('Delete "' + displayName + '" from the local library?')) return;
  var n = hddUnitCount(type);
  for (var u = 0; u < n; u++) {
    var e = (typeof driveRegistry !== 'undefined') ? driveRegistry.get(type, u) : null;
    if (e && e.mounted && e.fileName === uuid) {
      if (!confirm('"' + displayName + '" is on unit ' + u + '. Eject it and delete?')) return;
      hddTypeEjectUnit(type, u);
    }
  }
  smdStorage.deleteImage(uuid).then(function () {
    smdRefreshAll();
  }).catch(function (err) {
    alert('Delete failed: ' + err.message);
  });
}

// Assign an image of <type> to a unit and mount it live.
function hddTypeAssign(type, uuid, id) {
  var cfg = HDD_TYPES[type];
  if (!cfg) return;
  var n = hddUnitCount(type);
  if (id < 0 || id >= n) return;

  var label = (typeof diskTypes !== 'undefined' && diskTypes.DRIVE_TYPE_LABEL[type]) || type;
  var meta = smdStorage.getMetadata(uuid);
  if (!meta || (meta.diskType || 'smd') !== type) {
    alert('Only images tagged as ' + label + ' can be assigned to a ' + label + ' ' + cfg.unitWord.toLowerCase() + '.');
    return;
  }
  var displayName = meta.name || uuid;

  // Eject the same image from any other unit, and the current occupant here.
  for (var i = 0; i < n; i++) {
    var e = (typeof driveRegistry !== 'undefined') ? driveRegistry.get(type, i) : null;
    if (e && e.fileName === uuid && i !== id) hddTypeEjectUnit(type, i);
  }
  var cur = (typeof driveRegistry !== 'undefined') ? driveRegistry.get(type, id) : null;
  if (cur && cur.fileName && cur.fileName !== uuid) hddTypeEjectUnit(type, id);

  if (typeof driveRegistry !== 'undefined') {
    driveRegistry.mount(type, id, 'opfs', displayName, uuid, meta.size || 0);
  }

  if (emu && isSmdPersistenceEnabled()) {
    if (emu.isWorkerMode()) {
      if (cfg.opfsMount && emu[cfg.opfsMount]) {
        emu[cfg.opfsMount](id, uuid).then(function (r) {
          if (r && r.ok && typeof driveRegistry !== 'undefined') {
            driveRegistry.mount(type, id, 'opfs', displayName, uuid, r.size || 0);
          }
        });
      } else {
        // Say so rather than silently registering a mount the emulator never
        // made: the row would show a disk the guest cannot read.
        alert(label + ' images cannot be mounted in Worker mode yet. Switch to direct mode, or use the gateway.');
        if (typeof driveRegistry !== 'undefined') driveRegistry.eject(type, id);
      }
    } else {
      smdStorage.retrieveImage(uuid).then(function (data) {
        if (data && emu[cfg.mountBuffer]) {
          var rc = emu[cfg.mountBuffer](id, data);
          if (rc !== 0) {
            alert('Failed to mount "' + displayName + '" on ' + label + ' ' + cfg.unitWord + ' ' + id + '.');
            if (typeof driveRegistry !== 'undefined') driveRegistry.eject(type, id);
          }
        }
      });
    }
  }
  hddTypeRefresh(type);
}

// Eject a unit of <type>.
function hddTypeEjectUnit(type, unit) {
  var cfg = HDD_TYPES[type];
  if (!cfg) return;
  if (unit < 0 || unit >= hddUnitCount(type)) return;
  if (emu) {
    if (emu.isWorkerMode()) {
      if (cfg.opfsUnmount && emu[cfg.opfsUnmount]) emu[cfg.opfsUnmount](unit);
      else if (emu[cfg.unmount]) emu[cfg.unmount](unit);
    } else if (emu[cfg.unmount]) {
      emu[cfg.unmount](unit);
    }
  }
  if (typeof driveRegistry !== 'undefined') driveRegistry.eject(type, unit);
  hddTypeRefresh(type);
}

// ---- the names index.html and older callers already use --------------------
// Kept as wrappers rather than renamed: the onclick= attributes in the markup
// call these by name, and churning the markup for a rename would risk more than
// it tidies.
function hddScsiRefresh()                { hddTypeRefresh('scsi'); }
function hddScsiRefreshLibrary()         { hddTypeRefreshLibrary('scsi'); }
function hddScsiAssign(uuid, id)         { hddTypeAssign('scsi', uuid, id); }
function hddScsiEjectUnit(unit)          { hddTypeEjectUnit('scsi', unit); }

function hddWinchesterRefresh()          { hddTypeRefresh('winchester'); }
function hddWinchesterEjectUnit(unit)    { hddTypeEjectUnit('winchester', unit); }

// The Import buttons on the SCSI / Winchester / NDIX tabs (the SMD tab's is
// wired in toolbar.js): the tab's type is preselected in the tag dialog.
(function() {
  function init() {
    document.querySelectorAll('.hdd-import-btn').forEach(function(btn) {
      btn.addEventListener('click', function() { smdImportFromFile(this.getAttribute('data-type')); });
    });
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init); else init();
})();
