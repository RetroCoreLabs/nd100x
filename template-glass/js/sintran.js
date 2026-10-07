//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// sintran.js - SINTRAN OS detection and System Information window

// =========================================================
// SINTRAN state
// =========================================================
var sintranState = {
  detected: false,
  versionLetter: '',
  osType: -1,
  pollTimer: null
};

// =========================================================
// Lookup tables
//
// Source: PH-P2-OPPSTART.NPL lines 3414-3452 (SYSEVAL table)
// =========================================================
var cpuTypeNames = {
  0: 'NORD-10 (48-bit FP)',
  1: 'NORD-10 (32-bit FP)',
  2: 'ND-100 (48-bit FP)',
  3: 'ND-100 (32-bit FP)',
  4: 'ND-110 (48-bit FP)',
  5: 'ND-110 (32-bit FP)',
  6: 'ND-120 (48-bit FP)',
  7: 'ND-120 (32-bit FP)'
};

// Instruction set values from SYSEVAL table (PH-P2-OPPSTART.NPL:3424-3436)
// Note: values 10-12 octal = 8-10 decimal
var instrSetNames = {
  0: 'Standard (NORD-10/ND-100)',
  1: 'Commercial/CE',
  2: 'ND-100/CX',
  3: 'ND-110 PCX',
  4: 'ND-120 PCX',
  8: 'ND-120/CX',
  9: 'ND-110/CX (3095)',
  10: 'ND-110/CX (3090)'
};

var osTypeNames = {
  0: 'VS',
  1: 'VSE',
  2: 'VSE/500',
  3: 'RTP',
  4: 'VSX',
  5: 'VSX/500'
};

var monthNames = [
  '', 'JANUARY', 'FEBRUARY', 'MARCH', 'APRIL', 'MAY', 'JUNE',
  'JULY', 'AUGUST', 'SEPTEMBER', 'OCTOBER', 'NOVEMBER', 'DECEMBER'
];

// =========================================================
// SYSEVAL address table
//
// All addresses are ND-100 word addresses in hex.
// Source: SINTRAN-STRUCTURES.md section 10, verified from
// SYMBOL-1-LIST across K03/L07/M06.
// =========================================================
var SYSEVAL = {
  SYSNO:    0x829,  // 004051 octal
  HWINFO0:  0x82A,  // 004052 octal
  HWINFO1:  0x82B,  // 004053 octal
  HWINFO2:  0x82C,  // 004054 octal
  SINVER0:  0x82D,  // 004055 octal
  SINVER1:  0x82E,  // 004056 octal
  REVLEV:   0x82F,  // 004057 octal
  GENDAT0:  0x830,  // 004060 octal - minutes
  GENDAT1:  0x831,  // 004061 octal - hours
  GENDAT2:  0x832,  // 004062 octal - day
  GENDAT3:  0x833,  // 004063 octal - month
  GENDAT4:  0x834,  // 004064 octal - year
  UNAFLAG:  0x847   // 004107 octal - system availability
};

// =========================================================
// Helpers
// =========================================================

// Format a 16-bit value as 6-digit octal
function toOctal(val) {
  return (val & 0xFFFF).toString(8).padStart(6, '0');
}

// Extract CPU type (0-7) from HWINFO(0).
//
// SYSEVAL (PH-P2-OPPSTART.NPL:3481) computes a 3-bit CPU type value
// and shifts it left by 8 positions (SH 10 octal = SH 8 decimal),
// placing it at bits 10-8. Additional bits may be set by the
// instruction probing sequence, so we mask to just the 3-bit field.
function extractCpuType(hwinfo0) {
  return (hwinfo0 >> 8) & 0x07;
}

// Extract instruction set code from HWINFO(0).
//
// SYSEVAL increments the instruction set code (low byte) for each
// detected instruction. Documented values are 0-4 and 10-12 octal
// (8-10 decimal). The probing may set additional bits, so try the
// full byte first, fall back to lower 4 bits.
function extractInstrSet(hwinfo0) {
  var full = hwinfo0 & 0xFF;
  if (instrSetNames[full] !== undefined) return full;
  var low = hwinfo0 & 0x0F;
  if (instrSetNames[low] !== undefined) return low;
  return full;
}

// Extract OS type (0-5) from SINVER(0).
//
// SYSEVAL (PH-P2-OPPSTART.NPL:3517-3520) sets A:=4 (or 5 for /500),
// shifts left by 8, and adds the version character. The documented
// encoding places OS type 0-5 in the high byte. However, some SINTRAN
// binaries have additional bits set in the high byte (parity, flags).
//
// Strategy: try extracting from bits 10-8 first (matches the SH 10
// algorithm). If out of range, try bits 14-12 as fallback.
function extractOsType(sinver0) {
  var osType = (sinver0 >> 8) & 0x07;
  if (osType <= 5) return osType;
  osType = (sinver0 >> 12) & 0x07;
  if (osType <= 5) return osType;
  return -1;
}

// Extract version letter from SINVER(0).
//
// Right byte contains ASCII version letter (A-Z). Bit 7 may contain
// even parity, so strip it with & 0x7F.
// Source: PH-P2-OPPSTART.NPL line 3451-3452
function extractVersionLetter(sinver0) {
  var ch = sinver0 & 0x7F;
  if (ch >= 0x41 && ch <= 0x5A) return String.fromCharCode(ch);
  return '';
}

// =========================================================
// Detection
// =========================================================
function sintranStartDetection() {
  if (sintranState.detected || sintranState.pollTimer) return;
  sintranState.pollTimer = setInterval(sintranCheckVersion, 2000);
}

function sintranStopDetection() {
  if (sintranState.pollTimer) {
    clearInterval(sintranState.pollTimer);
    sintranState.pollTimer = null;
  }
}

function sintranCheckVersion() {
  if (!emu || !emu.isReady()) return;
  if (typeof emulationRunning === 'undefined' || !emulationRunning) return;

  try {
    // Read through CPU's current page table context (Dbg_ReadMemory).
    // This works because the CPU's active PIT maps SYSEVAL correctly
    // once SINTRAN has initialized. DPIT is NOT needed here - it's
    // only for kernel data structures (RT tables, queues, segments).
    Promise.resolve(emu.readMemory(SYSEVAL.SINVER0)).then(function(raw) {
      var sinver = raw & 0xFFFF;
      var letter = extractVersionLetter(sinver);
      if (letter !== '') {
        sintranState.detected = true;
        sintranState.versionLetter = letter;
        sintranState.osType = extractOsType(sinver);
        sintranStopDetection();
        sintranOnDetected();
      }
    });
  } catch (e) {
    // Not ready yet, keep polling
  }
}

function sintranOnDetected() {
  var menuContainer = document.getElementById('sintran-menu-container');
  if (menuContainer) {
    menuContainer.style.display = 'flex';
  }
  // Load the detected version's linker symbol table (RT program names
  // are not stored in memory - see sintran-symbols.js) and the
  // LIST-SEGMENT segment-name capture (see sintran-seg-names.js).
  if (window.sintranSymbols && window.sintranSymbols.loadSymbolTable) {
    window.sintranSymbols.loadSymbolTable(sintranState.versionLetter);
  }
  if (window.sintranSegNames && window.sintranSegNames.loadSegmentNames) {
    window.sintranSegNames.loadSegmentNames(sintranState.versionLetter);
  }
  console.log('SINTRAN detected: ' + sintranGetOsString());
}

// =========================================================
// OS string helper
// =========================================================
function sintranGetOsString() {
  var osName = osTypeNames[sintranState.osType];
  if (osName === undefined) osName = '';
  if (osName !== '') osName = osName + ' ';
  return 'SINTRAN III ' + osName + 'version ' + sintranState.versionLetter;
}

// =========================================================
// Read SYSEVAL table
// =========================================================
// Returns Promise<object|null>
function sintranReadSyseval() {
  if (!emu || !emu.isReady()) return Promise.resolve(null);

  // Use DPIT-translated reads when available (post-detection)
  var sym = window.sintranSymbols;
  var read = (sym && sym.readWordDPIT) ? sym.readWordDPIT
    : function(addr) { return Promise.resolve(emu.readMemory(addr)).then(function(v) { return v & 0xFFFF; }); };

  var addrs = [
    SYSEVAL.SYSNO, SYSEVAL.HWINFO0, SYSEVAL.HWINFO1, SYSEVAL.HWINFO2,
    SYSEVAL.SINVER0, SYSEVAL.REVLEV,
    SYSEVAL.GENDAT0, SYSEVAL.GENDAT1, SYSEVAL.GENDAT2, SYSEVAL.GENDAT3, SYSEVAL.GENDAT4,
    SYSEVAL.UNAFLAG
  ];

  return Promise.all(addrs.map(function(a) { return read(a); })).then(function(vals) {
    var sysno = vals[0], hwinfo0 = vals[1], hwinfo1 = vals[2], hwinfo2 = vals[3];
    var sinver0 = vals[4], revlev = vals[5];
    var minutes = vals[6], hours = vals[7], day = vals[8], month = vals[9], year = vals[10];
    var unaflag = vals[11];

    var cpuType = extractCpuType(hwinfo0);
    var instrSet = extractInstrSet(hwinfo0);
    var osType = extractOsType(sinver0);
    var versionLetter = extractVersionLetter(sinver0);

    var cpuName = cpuTypeNames[cpuType];
    if (cpuName === undefined) cpuName = 'Unknown (' + cpuType + ')';

    var instrName = instrSetNames[instrSet];
    if (instrName === undefined) instrName = 'Unknown (' + instrSet + ')';

    var osName = osTypeNames[osType];
    if (osName === undefined) osName = '';
    if (osName !== '') osName = osName + ' ';
    var osString = 'SINTRAN III ' + osName + 'version ' + versionLetter;

    var genDate = '';
    if (minutes >= 0 && minutes <= 59 &&
        hours >= 0 && hours <= 23 &&
        day >= 1 && day <= 31 &&
        month >= 1 && month <= 12 &&
        year >= 1950 && year <= 2100) {
      var hh = hours < 10 ? '0' + hours : '' + hours;
      var mi = minutes < 10 ? '0' + minutes : '' + minutes;
      var mName = monthNames[month] || ('Month ' + month);
      genDate = hh + '.' + mi + '.00 ' + day + ' ' + mName + ' ' + year;
    } else {
      genDate = 'N/A';
    }

    var available = (unaflag & 0x8000) !== 0 ? 'Unavailable' : 'Available';
    var patchStr = (revlev & 0xFFFF).toString(8) + 'B';
    var sysnoStr = '' + (sysno & 0xFFFF);
    var sysTypeStr = '' + (hwinfo2 & 0xFFFF);

    return {
      sysno: sysnoStr,
      cpuType: cpuName,
      instrSet: instrName,
      microprogVersion: hwinfo1,
      systemType: sysTypeStr,
      osString: osString,
      patchLevel: patchStr,
      genDate: genDate,
      available: available
    };
  });
}

// =========================================================
// System Information window
// =========================================================
function sintranOpenSysInfo() {
  var win = document.getElementById('sysinfo-window');
  if (!win) return;
  win.style.display = 'flex';
  sintranRefreshSysInfo();
}

function sintranCloseSysInfo() {
  var win = document.getElementById('sysinfo-window');
  if (!win) return;
  win.style.display = 'none';
}

function sintranRefreshSysInfo() {
  sintranReadSyseval().then(function(data) {
    if (!data) return;
    sintranRenderSysinfo(data);
  });
}

function sintranRenderSysinfo(data) {
  var body = document.getElementById('sysinfo-body');
  if (!body) return;

  var rows = [
    { label: 'Operating System',    value: data.osString },
    { label: 'System Status',       value: data.available },
    { label: 'CPU Type',            value: data.cpuType },
    { label: 'Instruction Set',     value: data.instrSet },
    { label: 'Microprog Version',   value: toOctal(data.microprogVersion) },
    { label: 'Generation Date',     value: data.genDate },
    { label: 'Revision',            value: data.patchLevel },
    { label: 'System Number',       value: data.sysno },
    { label: 'System Type',         value: data.systemType }
  ];

  var html = '';
  for (var i = 0; i < rows.length; i++) {
    html += '<div class="machine-info-row">';
    html += '<span class="machine-info-label">' + rows[i].label + '</span>';
    html += '<span class="machine-info-value">' + rows[i].value + '</span>';
    html += '</div>';
  }

  body.innerHTML = html;
}

function sintranCopySysInfo() {
  sintranReadSyseval().then(function(data) {
    if (!data) return;

    var rows = [
      ['Operating System',  data.osString],
      ['System Status',     data.available],
      ['CPU Type',          data.cpuType],
      ['Instruction Set',   data.instrSet],
      ['Microprog Version', toOctal(data.microprogVersion)],
      ['Generation Date',   data.genDate],
      ['Revision',          data.patchLevel],
      ['System Number',     data.sysno],
      ['System Type',       data.systemType]
    ];

    var md = '| Field | Value |\n|---|---|\n';
    for (var i = 0; i < rows.length; i++) {
      md += '| ' + rows[i][0] + ' | ' + rows[i][1] + ' |\n';
    }

    navigator.clipboard.writeText(md).then(function() {
      var btn = document.getElementById('sysinfo-copy');
      if (btn) {
        btn.classList.add('copied');
        setTimeout(function() { btn.classList.remove('copied'); }, 1200);
      }
    });
  });
}

// =========================================================
// Event handlers (attached after DOM ready)
// =========================================================
(function() {
  var closeBtn = document.getElementById('sysinfo-window-close');
  if (closeBtn) {
    closeBtn.addEventListener('click', sintranCloseSysInfo);
  }

  var copyBtn = document.getElementById('sysinfo-copy');
  if (copyBtn) {
    copyBtn.addEventListener('click', sintranCopySysInfo);
  }

  var refreshBtn = document.getElementById('sysinfo-refresh');
  if (refreshBtn) {
    refreshBtn.addEventListener('click', sintranRefreshSysInfo);
  }

  var menuItem = document.getElementById('menu-system-info');
  if (menuItem) {
    menuItem.addEventListener('click', function() {
      var win = document.getElementById('sysinfo-window');
      if (win.style.display === 'none' || win.style.display === '') {
        sintranOpenSysInfo();
        windowManager.focus('sysinfo-window');
      } else {
        sintranCloseSysInfo();
      }
      document.querySelectorAll('.toolbar-menu-container').forEach(function(c) {
        c.classList.remove('open');
      });
    });
  }
})();
