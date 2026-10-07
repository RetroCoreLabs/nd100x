//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// HackerCorp Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// floppy-browser.js - Floppy browser modal, search, products, mount

let floppyDatabase = [];
let productsDatabase = [];
let filteredFloppies = [];
let selectedProductId = null;
let currentProductFilter = '';

// ---- Norsk Data software archive (GitHub) ----------------------------------
// The catalog and the images come from the RetroCoreLabs/norskdata-software-archive
// repository. raw.githubusercontent.com answers with Access-Control-Allow-Origin: *.
// Every image is a gzip file, images/<md5>/<name>.img.gz.
const ARCHIVE_BASE_URL = 'https://raw.githubusercontent.com/RetroCoreLabs/norskdata-software-archive/main/';
const ARCHIVE_FLOPPIES_URL = ARCHIVE_BASE_URL + 'catalog/floppies.json';
const ARCHIVE_PRODUCTS_URL = ARCHIVE_BASE_URL + 'catalog/products.json';

// URL of an image path from the catalog, percent-encoded (some names hold spaces).
function archiveImageUrl(imagePath) {
  return ARCHIVE_BASE_URL + imagePath.split('/').map(encodeURIComponent).join('/');
}

// Text listing of one archive record (ndfs / dosFiles / backupFiles), one line per file.
function archiveListing(r) {
  const lines = [];
  const pad = (s, n) => String(s == null ? '?' : s).padEnd(n);
  if (r.volumeName) lines.push('Directory name            : ' + r.volumeName);
  if (r.totalPages > 0) lines.push('Filesystem image size     : ' + r.totalPages.toString(8).padStart(6, '0') + ' pages');
  lines.push('Filesystem                : ' + (r.filesystem || 'unknown'));
  if (r.bootFormat && r.bootFormat !== 'none') lines.push('Boot                      : ' + r.bootFormat + ' ' + (r.bootProgram || ''));
  const ndfs = r.ndfs || {};
  (ndfs.users || []).forEach(function(u) {
    lines.push('---- User ' + u.name + ' (' + (u.pagesUsed || 0) + ' pages)');
    (ndfs.files || []).forEach(function(f) {
      if (f.userName && f.userName !== u.name) return;
      lines.push('  ' + pad(f.name, 24) + ' ' + String(f.pages || 0).padStart(5) + ' pages ' +
                 String(f.bytes || 0).padStart(9) + ' bytes  ' + (f.dateCreatedStr || ''));
    });
  });
  (r.dosFiles || []).forEach(function(f) {
    lines.push('  ' + pad(f.path, 32) + ' ' + String(f.bytes || 0).padStart(9) + ' bytes  ' + (f.modified || ''));
  });
  if (r.backupSet) lines.push('---- ' + (r.backupSet.kind || 'backup') + ' set ' + (r.backupSet.name || '?') + '  label ' + (r.backupSet.label || '?'));
  (r.backupFiles || []).forEach(function(f) {
    lines.push('  ' + pad(f.name, 32) + ' ' + String(f.bytes || 0).padStart(9) + ' bytes  ' + (f.created || ''));
  });
  return lines.join('\r\n');
}

// Archive records -> the entry shape the browser code works with
// (Name, Description, Reference, Md5, DirectoryContent, ...). Records without
// an image in the archive are left out.
// The archive holds more than ND floppies. Its `filesystem` field says what
// an image is: ndfs (a SINTRAN floppy), backup (an ND BACKUP-SYSTEM set),
// none (a boot-only or unrecognised ND floppy) - and dos (a PC floppy), tar,
// winch (a Winchester pack, not a floppy at all). Only ND floppies are
// listed; a DOS image mounted here is useless and its NDFS view is an error
// ("Invalid NDFS master block" - a FAT boot sector is not a master block).
const ARCHIVE_SKIP_FILESYSTEMS = { dos: true, tar: true, winch: true };

function archiveToFloppies(records) {
  const out = [];
  records.forEach(function(r) {
    const imagePath = r && r.storage && r.storage.git && r.storage.git.imagePath;
    if (!r || !r.md5 || !imagePath) return;
    if (ARCHIVE_SKIP_FILESYSTEMS[r.filesystem]) return;
    const stem = imagePath.split('/').pop().replace(/\.img\.gz$/, '');
    const disk = r.diskNumber > 0 ? 'disk ' + r.diskNumber + ' of ' + (r.diskTotal || '?') : '';
    // Product, version and disk number when known, then the file stem - and
    // nothing else (no contributor).
    const product = [r.productId || '', r.version || '', disk].filter(Boolean).join(' ');
    out.push({
      Id: out.length + 1,
      Status: 0,
      Name: r.volumeName || (r.backupSet && r.backupSet.name) || stem,
      Description: [product, stem].filter(Boolean).join(' - '),
      Reference: r.productId || '',
      ProductId: r.productId || '',
      Md5: r.md5,
      // ndfs | backup | none - decides whether the NDFS view makes sense.
      Filesystem: r.filesystem || 'none',
      ImageUrl: archiveImageUrl(imagePath),
      DirectoryContent: archiveListing(r)
    });
  });
  return out;
}

// JSON of a floppy for a single-quoted HTML attribute (names may hold ' or &).
function floppyAttr(floppy) {
  return JSON.stringify(floppy).replace(/&/g, '&amp;').replace(/'/g, '&#39;');
}

// Unpack gzip bytes with the browser's own DecompressionStream.
async function gunzipBytes(bytes) {
  if (typeof DecompressionStream === 'undefined') {
    throw new Error('This browser cannot unpack .gz files (no DecompressionStream)');
  }
  const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

// Bytes of a catalog image: uses the .img.gz of the archive and unpacks it.
async function fetchArchiveImage(url) {
  const resp = await fetch(url);
  if (!resp.ok) throw new Error('Download failed: ' + resp.status + ' ' + resp.statusText);
  const bytes = new Uint8Array(await resp.arrayBuffer());
  return /\.gz$/.test(url) ? gunzipBytes(bytes) : bytes;
}

// Close modal handlers
document.getElementById('floppy-modal-close').addEventListener('click', function() {
  closeFloppyBrowser();
});

// Search functionality
document.getElementById('floppy-search').addEventListener('input', function(e) {
  filterFloppies();
});

document.getElementById('search-clear').addEventListener('click', function() {
  document.getElementById('floppy-search').value = '';
  filterFloppies();
});

// Products modal functionality
document.getElementById('select-product-btn').addEventListener('click', function() {
  openProductsModal();
});

document.getElementById('clear-product-btn').addEventListener('click', function() {
  clearProductFilter();
});

document.getElementById('products-modal-close').addEventListener('click', function() {
  closeProductsModal();
});

document.getElementById('products-cancel-btn').addEventListener('click', function() {
  closeProductsModal();
});

document.getElementById('products-ok-btn').addEventListener('click', function() {
  selectProductAndClose();
});

document.getElementById('product-filter').addEventListener('input', function() {
  filterProductsList();
});

document.getElementById('products-modal').addEventListener('click', function(e) {
  if (e.target === this) {
    closeProductsModal();
  }
});

var floppyBrowserDefaultUnit = 0;

// ---- Upload tab state (in-memory only; uploads are lost on reload) ----
var uploadedFloppies = [];   // { id, name, fileName, size, hash, bytes, directoryContent }
var uploadIdSeq = 1;
var uploadUiInited = false;

// Pick mode: Machine Setup opens the library to CHOOSE a catalog floppy for a
// drive slot rather than mount one now. The details pane then shows Select
// instead of the unit/Mount controls, and the chosen record goes to this
// callback; the machine downloads and mounts it at power-on (toolbar.js).
var floppyPickCallback = null;

// openFloppyBrowser(defaultUnit) mounts as before; openFloppyBrowser({pick: fn})
// is pick mode.
function openFloppyBrowser(arg) {
  floppyPickCallback = (arg && typeof arg === 'object' && typeof arg.pick === 'function') ? arg.pick : null;
  if (typeof arg === 'number') floppyBrowserDefaultUnit = arg;
  var sel = document.getElementById('floppy-drive-select');
  if (sel) sel.value = String(floppyBrowserDefaultUnit);
  var title = document.querySelector('#floppy-modal .glass-window-title');
  if (title) title.textContent = floppyPickCallback ? 'Floppy Library - choose a floppy for the drive' : 'Floppy Library';
  document.getElementById('floppy-modal').style.display = 'flex';
  if (typeof windowManager !== 'undefined') windowManager.focus('floppy-modal');
  initFloppyUploadUI();
  loadFloppyDatabase();
}

function closeFloppyBrowser() {
  floppyPickCallback = null;
  document.getElementById('floppy-modal').style.display = 'none';
}

// ============================================================
// Upload tab: drop/pick .img -> validate NDFS -> auto-title -> mount
// ============================================================

function initFloppyUploadUI() {
  if (uploadUiInited) return;
  uploadUiInited = true;

  var libBtn = document.getElementById('floppy-tab-btn-library');
  var upBtn = document.getElementById('floppy-tab-btn-upload');
  if (libBtn) libBtn.addEventListener('click', function() { switchFloppyTab('library'); });
  if (upBtn) upBtn.addEventListener('click', function() { switchFloppyTab('upload'); });

  var pick = document.getElementById('floppy-upload-pick');
  var input = document.getElementById('floppy-upload-input');
  if (pick && input) pick.addEventListener('click', function() { input.click(); });
  if (input) input.addEventListener('change', function() {
    handleFloppyUploadFiles(input.files);
    input.value = '';
  });

  var dz = document.getElementById('floppy-drop-zone');
  if (dz) {
    ['dragenter', 'dragover'].forEach(function(ev) {
      dz.addEventListener(ev, function(e) { e.preventDefault(); e.stopPropagation(); dz.classList.add('dragover'); });
    });
    ['dragleave', 'drop'].forEach(function(ev) {
      dz.addEventListener(ev, function(e) { e.preventDefault(); e.stopPropagation(); dz.classList.remove('dragover'); });
    });
    dz.addEventListener('drop', function(e) {
      if (e.dataTransfer && e.dataTransfer.files) handleFloppyUploadFiles(e.dataTransfer.files);
    });
  }

  renderUploadList();
}

function switchFloppyTab(name) {
  var isUp = (name === 'upload');
  var lib = document.getElementById('floppy-tab-library');
  var up = document.getElementById('floppy-tab-upload');
  var libBtn = document.getElementById('floppy-tab-btn-library');
  var upBtn = document.getElementById('floppy-tab-btn-upload');
  if (lib) lib.style.display = isUp ? 'none' : '';
  if (up) up.style.display = isUp ? '' : 'none';
  if (libBtn) libBtn.classList.toggle('active', !isUp);
  if (upBtn) upBtn.classList.toggle('active', isUp);
}

function handleFloppyUploadFiles(files) {
  if (!files || !files.length) return;
  var file = files[0];
  var reader = new FileReader();
  reader.onload = function(ev) {
    addUploadedFloppyImage(file.name, new Uint8Array(ev.target.result));
  };
  reader.readAsArrayBuffer(file);
}

// Quick content hash for de-duplication (sampled djb2 + length).
function hashFloppyBytes(b) {
  var h = 5381;
  var step = Math.max(1, Math.floor(b.length / 4096));
  for (var i = 0; i < b.length; i += step) { h = (((h << 5) + h) + b[i]) >>> 0; }
  return (h >>> 0).toString(16) + '-' + b.length;
}

function addUploadedFloppyImage(fileName, bytes) {
  if (typeof NdfsLib === 'undefined' || !NdfsLib.NdfsFileSystem) {
    alert('NDFS library not loaded.');
    return;
  }
  // Validate strictly: a non-NDFS image is rejected.
  var fs;
  try {
    fs = new NdfsLib.NdfsFileSystem(bytes, true);
  } catch (err) {
    alert('"' + fileName + '" is not a valid NDFS image:\n' + (err && err.message ? err.message : err));
    return;
  }

  var hash = hashFloppyBytes(bytes);
  var existing = uploadedFloppies.find(function(x) { return x.hash === hash; });
  if (existing) {
    switchFloppyTab('upload');
    selectUploadedFloppy(existing.id);
    return;
  }

  var volume = '';
  try { volume = fs.getDirectoryName(); } catch (e) { volume = ''; }
  if (!volume) volume = fileName.replace(/\.(img|IMG)$/, '');

  var entry = {
    id: uploadIdSeq++,
    name: volume,
    fileName: fileName,
    size: bytes.length,
    hash: hash,
    bytes: bytes,
    directoryContent: generateNdfsListing(fs)
  };
  uploadedFloppies.push(entry);
  switchFloppyTab('upload');
  renderUploadList();
  selectUploadedFloppy(entry.id);
}

function fmtFloppySize(n) {
  if (n < 1024) return n + ' B';
  if (n < 1024 * 1024) return (n / 1024).toFixed(0) + ' KB';
  return (n / (1024 * 1024)).toFixed(1) + ' MB';
}

function renderUploadList() {
  var list = document.getElementById('floppy-upload-list');
  if (!list) return;
  if (!uploadedFloppies.length) {
    list.innerHTML = '<div class="no-selection" style="padding:14px">No uploaded floppies yet. Drop a valid NDFS <code>.img</code> above.</div>';
    return;
  }
  list.innerHTML = uploadedFloppies.map(function(u) {
    return '<div class="ndlib-floppy-item" data-upload-id="' + u.id + '">' +
      '<div class="ndlib-floppy-name">' + escapeHtml(u.name) + '</div>' +
      '<div class="ndlib-floppy-description">' + escapeHtml(u.fileName) + '</div>' +
      '<div class="ndlib-product-badge">' + fmtFloppySize(u.size) + '</div></div>';
  }).join('');
  list.querySelectorAll('.ndlib-floppy-item').forEach(function(el) {
    el.addEventListener('click', function() {
      selectUploadedFloppy(el.getAttribute('data-upload-id'));
    });
  });
}

function selectUploadedFloppy(id) {
  var u = uploadedFloppies.find(function(x) { return String(x.id) === String(id); });
  if (!u) return;
  var list = document.getElementById('floppy-upload-list');
  if (list) {
    list.querySelectorAll('.ndlib-floppy-item').forEach(function(el) {
      el.classList.toggle('active', el.getAttribute('data-upload-id') === String(u.id));
    });
  }
  // Reuse the standard detail pane. __uploadId routes Mount/NDFS to the
  // in-memory bytes instead of a catalog download.
  displayFloppyDetails({
    Name: u.name,
    Description: 'Uploaded file: ' + u.fileName,
    Md5: '(local upload)',
    DirectoryContent: u.directoryContent,
    __uploadId: u.id
  });
}

// Build a SINTRAN-style directory listing from the parsed filesystem.
function generateNdfsListing(fs) {
  var lines = [];
  function pad3(n) { n = String(n); return n.length >= 3 ? n : ('000' + n).slice(-3); }
  try { lines.push('Directory name            : ' + (fs.getDirectoryName() || '')); } catch (e) {}
  try {
    var users = fs.getUsers();
    lines.push('');
    lines.push('Users:');
    users.forEach(function(u) {
      lines.push('[' + pad3(u.userIndex != null ? u.userIndex : '') + '] ' +
        u.userName + '  - ' + u.pagesUsed + ' pages used, ' + u.pagesReserved + ' pages reserved');
    });
    var objs = fs.getObjectEntries();
    lines.push('');
    lines.push('Files:');
    objs.forEach(function(oe) {
      if (!oe || !oe.objectName) return;
      lines.push('(' + oe.userName + ')' + oe.objectName + ':' + (oe.type || '') +
        '  ' + (oe.pagesInFile || 0) + ' pages');
    });
  } catch (e) {}
  return lines.join('\r\n');
}

async function loadFloppyDatabase() {
  const floppyLoading = document.getElementById('floppy-loading');
  const floppyList = document.getElementById('floppy-list');

  floppyLoading.style.display = 'flex';
  floppyList.innerHTML = '';

  try {
    console.log('Loading floppy database...');
    const floppyResponse = await fetch(ARCHIVE_FLOPPIES_URL);
    if (!floppyResponse.ok) {
      throw new Error(`Failed to load floppy database: ${floppyResponse.status}`);
    }
    floppyDatabase = archiveToFloppies(await floppyResponse.json());
    console.log(`Loaded ${floppyDatabase.length} floppies`);

    console.log('Loading products database...');
    const productsResponse = await fetch(ARCHIVE_PRODUCTS_URL);
    if (!productsResponse.ok) {
      throw new Error(`Failed to load products database: ${productsResponse.status}`);
    }
    productsDatabase = await productsResponse.json();
    console.log(`Loaded ${productsDatabase.length} products`);

    document.getElementById('floppy-browser-main').style.display = 'flex';

    filteredFloppies = floppyDatabase.filter(floppy => floppy.Status === 0);
    displayFloppies();

    floppyLoading.style.display = 'none';

  } catch (error) {
    console.error('Error loading floppy database:', error);
    floppyLoading.innerHTML = `
      <div style="color: #e53935; text-align: center;">
        <p><strong>Error loading floppy database</strong></p>
        <p>${error.message}</p>
        <p><small>Please check your internet connection and try again.</small></p>
      </div>
    `;
  }
}

function updateResultsCount() {
  const resultsCount = document.getElementById('floppy-results-count');
  const count = filteredFloppies.length;
  resultsCount.textContent = `${count} floppy${count !== 1 ? 'ies' : ''} found`;
}

function filterFloppies() {
  const searchTerm = document.getElementById('floppy-search').value.toLowerCase();
  const searchClearBtn = document.getElementById('search-clear');

  searchClearBtn.style.display = searchTerm ? 'block' : 'none';

  filteredFloppies = floppyDatabase.filter(floppy => {
    if (floppy.Status !== 0) return false;

    if (selectedProductId) {
      const selectedProduct = productsDatabase.find(p => p.Id === selectedProductId);
      if (!selectedProduct || !floppyMatchesProduct(floppy, selectedProduct)) {
        return false;
      }
    }

    if (!searchTerm) return true;

    const matchesName = floppy.Name && floppy.Name.toLowerCase().includes(searchTerm);
    const matchesDescription = floppy.Description && floppy.Description.toLowerCase().includes(searchTerm);
    const matchesDirectoryContent = floppy.DirectoryContent && floppy.DirectoryContent.toLowerCase().includes(searchTerm);
    const matchesReference = floppy.Reference && floppy.Reference.toLowerCase().includes(searchTerm);
    const matchesFilePath = floppy.FilePath && floppy.FilePath.toLowerCase().includes(searchTerm);

    let matchesProduct = false;
    if (floppy.Product && floppy.Product.Name) {
      matchesProduct = floppy.Product.Name.toLowerCase().includes(searchTerm);
    }

    return matchesName || matchesDescription || matchesDirectoryContent || matchesReference || matchesFilePath || matchesProduct;
  });

  displayFloppies();
  updateResultsCount();
}

function displayFloppies() {
  const floppyList = document.getElementById('floppy-list');
  floppyList.innerHTML = '';

  if (filteredFloppies.length === 0) {
    floppyList.innerHTML = '<div style="text-align: center; color: #666; padding: 40px;">No floppies found matching your search criteria.</div>';
    updateResultsCount();
    return;
  }

  filteredFloppies.forEach(floppy => {
    const floppyItem = createFloppyItem(floppy);
    floppyList.appendChild(floppyItem);
  });

  updateResultsCount();
}

function createFloppyItem(floppy) {
  const item = document.createElement('div');
  item.className = 'ndlib-floppy-item';
  item.onclick = () => selectFloppy(floppy);

  const name = floppy.Name || 'Unnamed Floppy';
  const description = floppy.Description || 'No description available';

  let productName = 'No Product';
  if (floppy.Product && floppy.Product.Name) {
    productName = floppy.Product.Name;
  } else {
    const matchingProduct = productsDatabase.find(product => floppyMatchesProduct(floppy, product));
    if (matchingProduct) {
      productName = matchingProduct.Name;
    }
  }

  item.innerHTML = `
    <div class="ndlib-floppy-name">${escapeHtml(name)}</div>
    <div class="ndlib-floppy-description">${escapeHtml(description)}</div>
    <div class="ndlib-product-badge">${escapeHtml(productName)}</div>
  `;

  return item;
}

function escapeHtml(text) {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

function selectFloppy(floppy) {
  document.querySelectorAll('.ndlib-floppy-item').forEach(item => {
    item.classList.remove('active');
  });

  event.currentTarget.classList.add('active');
  displayFloppyDetails(floppy);
}

function displayFloppyDetails(floppy) {
  const detailsContainer = document.getElementById('floppy-details');

  const name = floppy.Name || 'Unnamed Floppy';
  const description = floppy.Description || 'No description available';
  const md5 = floppy.Md5 || 'N/A';
  const directoryContent = floppy.DirectoryContent || 'No directory content available';

  let productName = 'No Product';
  if (floppy.Product && floppy.Product.Name) {
    productName = floppy.Product.Name;
  } else {
    const matchingProduct = productsDatabase.find(product => floppyMatchesProduct(floppy, product));
    if (matchingProduct) {
      productName = matchingProduct.Name;
    }
  }

  detailsContainer.innerHTML = `
    <div class="floppy-details-content">
      <div class="floppy-details-top">
        <div class="floppy-details-info">
          <h4>${escapeHtml(name)}</h4>
          <div class="floppy-details-row">
            <div class="floppy-details-label">Description:</div>
            <div class="floppy-details-value">${escapeHtml(description)}</div>
          </div>
          <div class="floppy-details-row">
            <div class="floppy-details-label">Product:</div>
            <div class="floppy-details-value">${escapeHtml(productName)}</div>
          </div>
          <div class="floppy-details-row">
            <div class="floppy-details-label">MD5:</div>
            <div class="floppy-details-value">${escapeHtml(md5)}</div>
          </div>
        </div>
        <div class="floppy-details-actions">
          ${floppyPickCallback ? (floppy.__uploadId != null
            ? `<span class="floppy-drive-select-label">An uploaded file cannot be chosen for a machine - only catalog floppies can be fetched again at power-on.</span>`
            : `<button class="floppy-mount-button floppy-pick-button" data-floppy='${floppyAttr(floppy)}'>Select for the drive</button>`)
          : `<span class="floppy-drive-select-label" id="floppy-device-name">Device name: FLOPPY-DISC-1</span>
          <label class="floppy-drive-select-label">Device unit:</label>
          <select id="floppy-drive-select" class="floppy-drive-select">
            <option value="0">Unit 0</option>
            <option value="1">Unit 1</option>
          </select>
          <button class="floppy-mount-button" data-floppy='${floppyAttr(floppy)}'>Mount</button>`}
          ${(floppy.__uploadId != null || floppy.Filesystem === 'ndfs')
            ? `<button class="floppy-ndfs-button" data-floppy='${floppyAttr(floppy)}' title="Browse the ND filesystem">NDFS</button>`
            : `<span class="floppy-drive-select-label" title="This floppy has no NDFS filesystem to browse">${floppy.Filesystem === 'backup' ? 'BACKUP-SYSTEM set' : 'boot-only floppy'}</span>`}
        </div>
      </div>
      <h5>Directory Content:</h5>
      <div class="floppy-directory-content">${escapeHtml(directoryContent)}</div>
    </div>
  `;

  // Set default unit from mount button context
  var driveSel = document.getElementById('floppy-drive-select');
  if (driveSel) driveSel.value = String(floppyBrowserDefaultUnit);

  // Wire up the Select (pick mode) or Mount button
  var pickBtn = detailsContainer.querySelector('.floppy-pick-button');
  if (pickBtn) {
    pickBtn.addEventListener('click', function() {
      var cb = floppyPickCallback;
      closeFloppyBrowser();
      if (cb) cb(floppy);
    });
  } else {
    var mountBtn = detailsContainer.querySelector('.floppy-mount-button');
    if (mountBtn) mountBtn.addEventListener('click', function() { mountFloppy(); });
  }

  // Wire up NDFS browse button
  var ndfsBtn = detailsContainer.querySelector('.floppy-ndfs-button');
  if (ndfsBtn) ndfsBtn.addEventListener('click', function() { browseFloppyNdfs(); });

}

// Open the NDFS viewer for the selected catalog floppy. "Both" behaviour:
// if this exact floppy is currently mounted on the chosen unit, browse the
// in-memory image (no network); otherwise download the catalog image and
// browse it without mounting.
async function browseFloppyNdfs() {
  var ndfsBtn = document.querySelector('.floppy-ndfs-button');
  var driveSelect = document.getElementById('floppy-drive-select');
  if (!ndfsBtn) return;
  var unitNum = driveSelect ? parseInt(driveSelect.value) : 0;

  var floppyData;
  try { floppyData = JSON.parse(ndfsBtn.getAttribute('data-floppy')); }
  catch (e) { return; }

  if (typeof openNdfsViewer !== 'function') { alert('NDFS viewer not loaded'); return; }

  // Uploaded image: browse the in-memory bytes directly.
  if (floppyData.__uploadId != null) {
    var up = uploadedFloppies.find(function(x) { return String(x.id) === String(floppyData.__uploadId); });
    if (up) openNdfsViewer(up.bytes, up.name);
    return;
  }

  var orig = ndfsBtn.textContent;
  ndfsBtn.disabled = true;
  try {
    var bytes = null;

    // Reuse the mounted image if it is this floppy on the chosen unit.
    if (typeof driveRegistry !== 'undefined' && typeof emu !== 'undefined' && emu.fsAvailable()) {
      var entry = driveRegistry.get('floppy', unitNum);
      if (entry && entry.mounted && entry.name === floppyData.Name) {
        try { bytes = emu.fsReadFile('/FLOPPY' + unitNum + '.IMG'); } catch (e) { bytes = null; }
      }
    }

    // Otherwise download the catalog image (browse only, do not mount).
    if (!bytes) {
      ndfsBtn.textContent = 'Loading...';
      if (!floppyData.ImageUrl) throw new Error('No image URL for this floppy');
      bytes = await fetchArchiveImage(floppyData.ImageUrl);
    }

    openNdfsViewer(bytes, floppyData.Name || 'Floppy');
  } catch (err) {
    alert('NDFS: ' + (err && err.message ? err.message : err));
  } finally {
    ndfsBtn.textContent = orig;
    ndfsBtn.disabled = false;
  }
}

async function mountFloppy() {
  var mountButton = document.querySelector('.floppy-mount-button');
  var driveSelect = document.getElementById('floppy-drive-select');
  var progressContainer = document.getElementById('floppy-download-progress');
  var progressFill = document.getElementById('progress-fill');
  var progressText = document.getElementById('progress-text');

  if (!mountButton) return;

  var unitNum = driveSelect ? parseInt(driveSelect.value) : 0;  // hardware unit (0 or 1)
  var driveNum = unitNum + 1;  // UI drive number (1 or 2, matches element IDs)
  var floppyData;

  try {
    floppyData = JSON.parse(mountButton.getAttribute('data-floppy'));
  } catch (e) {
    console.error('No floppy data on mount button');
    return;
  }

  console.log('Mounting floppy to FLOPPY-DISC-1 Unit ' + unitNum + ':', floppyData.Name);

  // Uploaded image: mount directly from the in-memory bytes (no download).
  if (floppyData.__uploadId != null) {
    try {
      var u = uploadedFloppies.find(function(x) { return String(x.id) === String(floppyData.__uploadId); });
      if (!u) throw new Error('Uploaded image no longer available');
      if (typeof emu === 'undefined' || !emu.fsAvailable()) throw new Error('Filesystem not ready');
      var fn = '/FLOPPY' + unitNum + '.IMG';
      emu.fsWriteFile(fn, u.bytes);
      try { emu.fsChmod(fn, 0o666); } catch (e) {}
      if (emu.remountFloppy(unitNum) !== 0) throw new Error('Failed to mount on unit ' + unitNum);
      if (typeof driveRegistry !== 'undefined') {
        driveRegistry.mount('floppy', unitNum, 'upload', u.name, u.fileName, u.bytes.byteLength);
      }
      if (progressContainer) {
        progressContainer.style.display = 'block';
        progressFill.style.width = '100%';
        progressFill.style.backgroundColor = '#4CAF50';
        progressText.textContent = 'Floppy mounted to Unit ' + unitNum + '!';
      }
    } catch (err) {
      alert('Mount: ' + (err && err.message ? err.message : err));
    }
    mountButton.textContent = 'Mount';
    mountButton.disabled = false;
    return;
  }

  try {
    progressContainer.style.display = 'block';
    progressFill.style.width = '0%';
    progressFill.style.backgroundColor = '';
    progressText.textContent = 'Downloading floppy...';

    mountButton.textContent = 'Downloading...';
    mountButton.disabled = true;

    var imageUrl = floppyData.ImageUrl;
    if (!imageUrl) {
      throw new Error('No image URL available for this floppy');
    }
    console.log('Downloading floppy from: ' + imageUrl);

    var response = await fetch(imageUrl);
    if (!response.ok) {
      throw new Error('Failed to download floppy: ' + response.status + ' ' + response.statusText);
    }

    var contentLength = parseInt(response.headers.get('content-length') || '0');
    var reader = response.body.getReader();
    var receivedLength = 0;
    var chunks = [];

    while (true) {
      var result = await reader.read();
      if (result.done) break;

      chunks.push(result.value);
      receivedLength += result.value.length;

      if (contentLength > 0) {
        var progress = (receivedLength / contentLength) * 100;
        progressFill.style.width = progress + '%';
        progressText.textContent = 'Downloading... ' + Math.round(progress) + '%';
      }
    }

    var floppyImageData = new Uint8Array(receivedLength);
    var position = 0;
    for (var i = 0; i < chunks.length; i++) {
      floppyImageData.set(chunks[i], position);
      position += chunks[i].length;
    }
    if (/\.gz$/.test(imageUrl)) {
      progressText.textContent = 'Unpacking...';
      floppyImageData = await gunzipBytes(floppyImageData);
    }

    if (typeof emu !== 'undefined' && emu.fsAvailable()) {
      // Write to unit-specific filename: FLOPPY0.IMG, FLOPPY1.IMG, etc.
      var floppyFileName = '/FLOPPY' + unitNum + '.IMG';
      emu.fsWriteFile(floppyFileName, floppyImageData);

      try {
        emu.fsChmod(floppyFileName, 0o666);
      } catch (e) {
        // chmod not critical on MEMFS
      }

      // Tell the C side to close old FILE* and re-open from MEMFS
      var result = emu.remountFloppy(unitNum);
      if (result !== 0) {
        throw new Error('Failed to mount floppy on unit ' + unitNum);
      }

      if (typeof driveRegistry !== 'undefined') {
        driveRegistry.mount('floppy', unitNum, 'library', floppyData.Name || 'Custom Floppy', floppyData.Name, floppyImageData.byteLength);
      }

      progressText.textContent = 'Floppy mounted to FLOPPY-DISC-1 Unit ' + unitNum + '!';
      progressFill.style.width = '100%';
      progressFill.style.backgroundColor = '#4CAF50';

      mountButton.textContent = 'Mounted!';
      mountButton.disabled = true;

      console.log('Floppy mounted to FLOPPY-DISC-1 Unit ' + unitNum + ':', floppyData.Name);

      setTimeout(function() {
        closeFloppyBrowser();
      }, 2000);

    } else {
      throw new Error('Virtual filesystem not available');
    }

  } catch (error) {
    console.error('Error mounting floppy:', error);
    progressText.textContent = 'Error: ' + error.message;
    progressFill.style.backgroundColor = '#e53935';
    mountButton.textContent = 'Mount';
    mountButton.disabled = false;
  }
}

// Helper: update mount/eject button visibility for a drive
// Note: The drive registry onChange listener also handles this automatically.
function updateFloppyDriveButtons(driveNum) {
  var unitNum = driveNum - 1;
  var nameEl = document.getElementById('drive-name-floppy-' + unitNum);
  var mountBtn = document.getElementById('floppy-drive-' + driveNum + '-mount');
  var ejectBtn = document.getElementById('floppy-drive-' + driveNum + '-eject');
  if (!nameEl || !mountBtn || !ejectBtn) return;
  var mounted = nameEl.textContent !== 'Empty';
  mountBtn.style.display = mounted ? 'none' : '';
  ejectBtn.style.display = mounted ? '' : 'none';
}

// Floppy drive eject handlers
document.getElementById('floppy-drive-1-eject').addEventListener('click', function() {
  emu.unmountFloppy(0);
  if (typeof driveRegistry !== 'undefined') {
    driveRegistry.eject('floppy', 0);
  }
});

document.getElementById('floppy-drive-2-eject').addEventListener('click', function() {
  emu.unmountFloppy(1);
  if (typeof driveRegistry !== 'undefined') {
    driveRegistry.eject('floppy', 1);
  }
});

// Floppy drive mount handlers - open Floppy Library with correct default unit
document.getElementById('floppy-drive-1-mount').addEventListener('click', function() {
  openFloppyBrowser(0);
  if (typeof windowManager !== 'undefined') windowManager.focus('floppy-modal');
});

document.getElementById('floppy-drive-2-mount').addEventListener('click', function() {
  openFloppyBrowser(1);
  if (typeof windowManager !== 'undefined') windowManager.focus('floppy-modal');
});

// Products Modal Functions
function openProductsModal() {
  document.getElementById('products-modal').style.display = 'flex';
  loadProductsWithCounts();
  document.getElementById('product-filter').value = '';
  selectedProductId = null;
  updateProductsOkButton();
}

function closeProductsModal() {
  document.getElementById('products-modal').style.display = 'none';
}

function floppyMatchesProduct(floppy, product) {
  if (floppy.ProductId && floppy.ProductId.toUpperCase() === product.Id.toUpperCase()) return true;

  const floppyName = (floppy.Name || '').toUpperCase();
  const productId = product.Id.toUpperCase();

  if (floppyName.startsWith(productId)) return true;

  if (productId.startsWith('ND-')) {
    const numberPart = productId.substring(3);
    if (floppyName.startsWith(numberPart)) return true;
  }

  const productIdWithoutND = productId.startsWith('ND-') ? productId.substring(3) : productId;
  if (floppyName.includes(productIdWithoutND)) return true;

  return false;
}

function loadProductsWithCounts() {
  const productsWithCounts = productsDatabase.map(product => {
    const matchCount = floppyDatabase.filter(floppy => {
      return floppyMatchesProduct(floppy, product);
    }).length;
    return { ...product, matchCount };
  }).sort((a, b) => {
    if (b.matchCount !== a.matchCount) return b.matchCount - a.matchCount;
    return a.Id.localeCompare(b.Id);
  });

  currentProductFilter = '';
  displayProductsList(productsWithCounts);
}

function displayProductsList(products) {
  const productsList = document.getElementById('products-list');

  const filteredProducts = products.filter(product => {
    if (!currentProductFilter) return product.matchCount > 0;
    const matchesId = product.Id.toLowerCase().includes(currentProductFilter);
    const matchesName = product.Name.toLowerCase().includes(currentProductFilter);
    return (matchesId || matchesName) && product.matchCount > 0;
  });

  if (filteredProducts.length === 0) {
    productsList.innerHTML = '<li style="padding: 20px; text-align: center; color: #666;">No products found matching your criteria.</li>';
    return;
  }

  productsList.innerHTML = filteredProducts.map(product => `
    <li class="product-item" data-id="${escapeHtml(product.Id)}" onclick="selectProduct('${escapeHtml(product.Id)}')">
      <div class="product-item-content">
        <div class="product-item-main">
          <strong>${escapeHtml(product.Id)}</strong>: ${escapeHtml(product.Name)}
        </div>
        <div class="product-item-count">
          <span class="floppy-count">${product.matchCount}</span>
        </div>
      </div>
    </li>
  `).join('');
}

function filterProductsList() {
  currentProductFilter = document.getElementById('product-filter').value.toLowerCase();

  const productCounts = {};
  floppyDatabase.forEach(floppy => {
    const productId = floppy.ProductId;
    if (productId) {
      productCounts[productId] = (productCounts[productId] || 0) + 1;
    }
  });

  const productsWithCounts = productsDatabase.map(product => ({
    ...product,
    matchCount: productCounts[product.Id] || 0
  })).sort((a, b) => b.matchCount - a.matchCount);

  displayProductsList(productsWithCounts);
}

function selectProduct(productId) {
  document.querySelectorAll('.product-item').forEach(item => {
    item.classList.remove('selected');
  });

  const selectedItem = document.querySelector(`.product-item[data-id="${productId}"]`);
  if (selectedItem) {
    selectedItem.classList.add('selected');
    selectedProductId = productId;
  }

  updateProductsOkButton();
}

function updateProductsOkButton() {
  const okButton = document.getElementById('products-ok-btn');
  okButton.disabled = !selectedProductId;
}

function selectProductAndClose() {
  if (!selectedProductId) return;

  const selectedProduct = productsDatabase.find(p => p.Id === selectedProductId);
  const selectProductBtn = document.getElementById('select-product-btn');
  const clearProductBtn = document.getElementById('clear-product-btn');

  if (selectedProduct) {
    selectProductBtn.textContent = `${selectedProduct.Id}: ${selectedProduct.Name}`;
    selectProductBtn.style.backgroundColor = '#4CAF50';
    clearProductBtn.style.display = 'block';
  }

  filterFloppies();
  closeProductsModal();
}

function clearProductFilter() {
  selectedProductId = null;
  const selectProductBtn = document.getElementById('select-product-btn');
  const clearProductBtn = document.getElementById('clear-product-btn');

  selectProductBtn.textContent = 'Select Product';
  selectProductBtn.style.backgroundColor = '#FF9800';
  clearProductBtn.style.display = 'none';

  filterFloppies();
}

window.selectProduct = selectProduct;
window.clearProductFilter = clearProductFilter;

// =========================================================
// Gateway floppy images
// =========================================================
var _gatewayFloppyList = null;

function floppyRefreshGatewaySection() {
  var section = document.getElementById('floppy-gateway-section');
  var container = document.getElementById('floppy-gateway-list');
  if (!section || !container) return;

  if (!_gatewayFloppyList || _gatewayFloppyList.length === 0) {
    section.style.display = 'none';
    return;
  }

  section.style.display = '';
  var html = '';
  _gatewayFloppyList.forEach(function(img) {
    html += '<div class="smd-image-card">';
    html += '<div class="smd-image-info">';
    html += '<span class="smd-image-name">' + escapeHtml(img.name) + '</span>';
    html += '<span class="smd-image-meta">' + Math.round(img.size / 1024) + ' KB &middot; Gateway unit ' + img.unit + '</span>';
    html += '</div>';
    html += '<div class="smd-image-actions">';
    html += '<button class="floppy-drive-mount floppy-gw-mount-btn" data-remote-unit="' + img.unit + '" data-size="' + img.size + '">Mount to Unit 0</button>';
    html += '</div>';
    html += '</div>';
  });
  container.innerHTML = html;

  container.querySelectorAll('.floppy-gw-mount-btn').forEach(function(btn) {
    btn.addEventListener('click', function() {
      var remoteUnit = parseInt(this.getAttribute('data-remote-unit'));
      var size = parseInt(this.getAttribute('data-size'));
      if (emu && emu.gatewayMountFloppy) {
        var imgName = (_gatewayFloppyList && _gatewayFloppyList[remoteUnit]) ? _gatewayFloppyList[remoteUnit].name : 'Gateway Floppy';
        emu.gatewayMountFloppy(0, size).then(function(r) {
          if (r && r.ok) {
            if (typeof driveRegistry !== 'undefined') {
              driveRegistry.mount('floppy', 0, 'gateway', imgName, null, size);
            }
            console.log('[Floppy] Gateway floppy unit ' + remoteUnit + ' mounted');
          } else {
            console.error('[Floppy] Gateway mount failed');
          }
        });
      }
    });
  });
}

function escapeHtml(str) {
  var div = document.createElement('div');
  div.textContent = str;
  return div.innerHTML;
}

// Gateway callbacks are set by smd-manager.js (loads later).
// Register via custom event listeners on window instead of overwriting callbacks.
window.addEventListener('gateway-disk-list', function(e) {
  _gatewayFloppyList = (e.detail && e.detail.floppy) ? e.detail.floppy : null;
  floppyRefreshGatewaySection();
});

window.addEventListener('gateway-disk-disconnected', function() {
  _gatewayFloppyList = null;
  floppyRefreshGatewaySection();
});
