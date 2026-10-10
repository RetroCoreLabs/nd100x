// machine-profiles.js - named machine configurations.
//
// The Machine Setup window used to keep ONE configuration, in a single
// localStorage key. You could describe a machine, but not keep two of them -
// so trying an ND-110 with a different disc set meant editing over the config
// you already had and hoping you could type it back.
//
// This is the store behind "multiple machine configs, saved by name". It is
// deliberately just a store: no UI, no emulator calls, no DOM. machine-setup.js
// drives it and toolbar.js reads the active profile when it boots.
//
// SHAPE ON DISK (one key, so a half-written second key cannot desync it):
//
//   nd100x-machine-profiles = {
//     "v": 1,
//     "active": "<name>",
//     "profiles": [ { "name": "<name>", "ini": "<text>" }, ... ]
//   }
//
// An ARRAY, not an object keyed by name: order is what the user sees in the
// dropdown, and object key order is not something to rely on.
//
// The old single key is still WRITTEN with the active profile's INI on every
// save. Nothing else reads it today, but it is the format the native binary
// takes and it costs one line to keep anything that grew up expecting it
// working.

(function () {
  'use strict';

  var KEY     = 'nd100x-machine-profiles';
  var OLD_KEY = 'nd100x-machine-ini';   // the single-config key this replaces

  // Mirrors the shipped nd100x.ini defaults. cpu takes a family number or a
  // model name now (src/cpu/cpu_model.c), which is worth saying here because
  // the comment is the only place a user finds out.
  var DEFAULT_INI =
    '# nd100x machine configuration\n' +
    '# Toggle a device with "enabled = yes|no". Sections are [type.thumbwheel].\n\n' +
    '[machine]\n' +
    'cpu = 100                 ; 100 | 110 | 120, or a model: ND110CX, ND120CX\n\n' +
    '[controller.floppy.0]\n' +
    'enabled = yes\n' +
    'disk0 = FLOPPY.IMG\n\n' +
    '[controller.smd.0]\n' +
    'enabled = yes\n' +
    'disk0 = SMD0.IMG\n\n' +
    '[terminals]\n' +
    'enabled = 5, 6, 7, 8, 9, 10, 11\n\n' +
    '[boot]\n' +
    'device = smd.0.0          ; <type>.<wheel>.<unit>\n';

  var DEFAULT_NAME = 'ND-100';

  // (The pre-07-OCT-2026 stock INI, with an SCSI controller, lived here as
  // LEGACY_DEFAULT_INI; the upgrade no longer needs to recognise it.)

  // The machines that ship with the page. Each boots as made, in demo mode,
  // off an image served next to the page:
  //   ND-100    - SINTRAN from SMD0.IMG.
  //   ND-5000   - SINTRAN L from ND5000-SMD0.IMG, with an ND-5000 CPU on the octobus (MFbus shared
  //               memory, station 070B), as ND5000.ini does natively. The
  //               mms log category is held at warn: the ND-5000 mailbox/MON
  //               traffic at info floods the console once the CPU starts.
  //   BSD 2.11  - the BSD port, from the Winchester image BSD211-WD0.IMG
  //               (the bsd211_481 build).
  //   500 NDIX-C - a standalone ND-500 booting NDIX from the catalog's root
  //               disc NDIX.IMG (an 'nd500-ndix' profile, see kind below).
  // Every built-in machine's INI is its own complete text. None is derived
  // from another: a change to one machine must never reach another one
  // through a shared string (Ronny, 08-OCT-2026).
  var ND5000_INI =
    '# nd100x machine configuration - ND-100 with an ND-5000 CPU\n' +
    '# SINTRAN III VSX/500 L prepared for the ND-5000 (ND5000-SMD0.IMG); the\n' +
    '# ND-5000 sits on octobus station 070B with MFbus shared memory.\n\n' +
    '[machine]\n' +
    'cpu = 100                 ; 100 | 110 | 120, or a model: ND110CX, ND120CX\n\n' +
    '[controller.floppy.0]\n' +
    'enabled = yes\n' +
    'disk0 = FLOPPY.IMG\n\n' +
    '[controller.smd.0]\n' +
    'enabled = yes\n' +
    'disk0 = ND5000-SMD0.IMG\n\n' +
    '[terminals]\n' +
    'enabled = 5, 6, 7, 8, 9, 10, 11\n\n' +
    '[boot]\n' +
    'device = smd.0.0          ; <type>.<wheel>.<unit>\n\n' +
    '[mfbus]\n' +
    'size      = 8\n' +
    'base_page = 04100B\n\n' +
    '[mfbus.part.0]\n' +
    'pages   = 4096\n' +
    'nd100   = yes\n' +
    'nd500_p = yes\n' +
    'nd500_d = yes\n\n' +
    '[controller.octobus.0]\n' +
    'enabled = yes\n\n' +
    '[nd5000.1]\n' +
    'enabled = yes\n' +
    'station = 070B\n\n' +
    '[runtime]\n' +
    'log = mms:warn\n';

  var BSD_INI =
    '# nd100x machine configuration - BSD 2.11 on the ND-100\n' +
    '# Boots from the Winchester (ST506) image, cards 3041/3038 at IOX 500-507.\n\n' +
    '[machine]\n' +
    'cpu = 100                 ; 100 | 110 | 120, or a model: ND110CX, ND120CX\n\n' +
    '[controller.floppy.0]\n' +
    'enabled = yes\n' +
    'disk0 = FLOPPY.IMG\n\n' +
    '[controller.wd.0]\n' +
    'enabled = yes\n' +
    'disk0 = BSD211-WD0.IMG\n\n' +
    '[terminals]\n' +
    'enabled = 5, 6, 7, 8, 9, 10, 11\n\n' +
    '[boot]\n' +
    'device = wd.0.0           ; <type>.<wheel>.<unit>\n';

  // NORD TSS 3.0 (1973) on a NORD-10-class ND-100: Paging System I (mms = 1 -
  // with MMS2 TSS hangs in its overlay loader), the CDC cartridge system disc
  // at IOX 500 and the swapping drum at IOX 540 from [runtime], no SMD, no
  // Winchester (it shares the 500 block with the CDC). Boots with the disc's
  // LOAD button ("device = cdc"). TSS-CDC.IMG is the TSS repo's TEL10
  // jump-start disc (10 teletypes, NTY=12) padded to 4 MB; TSS-DRUM.IMG is
  // 1 MB of zeros the page writes into MEMFS at power-on (swap space).
  // Terminals 5-10 are TSS's TTY5-TTY10: the same IOX addresses (0340-0370,
  // 01300, 01310) and ident codes (044-051) TSS's N10 device table names.
  // TSS's TTY2-4 want idents 5-7, which the ND-100 cards at 0310-0330 never
  // answer, so terminals 2-4 are left out. A dead teletype wakes on ESC.
  var TSS_INI =
    '# nd100x machine configuration - NORD TSS 3.0\n\n' +
    '[machine]\n' +
    'cpu = 100\n' +
    'mms = 1                   ; Paging System I - NORD TSS needs it\n\n' +
    '[controller.floppy.0]\n' +
    'enabled = no\n\n' +
    '[terminals]\n' +
    'enabled = 5, 6, 7, 8, 9, 10   ; TSS TTY5-TTY10 (press ESC on one to wake it)\n\n' +
    '[runtime]\n' +
    'cdc = TSS-CDC.IMG         ; CDC cartridge system disc @ IOX 500\n' +
    'drum = TSS-DRUM.IMG       ; swapping drum @ IOX 540\n\n' +
    '[boot]\n' +
    'device = cdc              ; the cartridge disc LOAD button\n';

  // SINTRAN M with TCP/IP: ND-100/CX booting the Winchester image
  // WD0-SINTRAN-M.IMG, whose AIP files give the ND 192.168.210.40 (see
  // docs/ETHERNET.md). The Ethernet II card (thumbwheel 0, IOX 140360) sends
  // and receives its frames through the gateway's Ethernet segment 0.
  var TCPIP_INI =
    '# nd100x machine configuration - SINTRAN M with TCP/IP over Ethernet\n' +
    '# Winchester image WD0-SINTRAN-M.IMG; Ethernet II card on gateway segment 0.\n\n' +
    '[machine]\n' +
    'cpu = ND100CX\n' +
    'mms = 2\n' +
    'fpp = 48\n\n' +
    '[controller.floppy.0]\n' +
    'enabled = yes\n' +
    'disk0 = FLOPPY.IMG\n\n' +
    '[controller.wd.0]\n' +
    'enabled = yes\n' +
    'disk0 = WD0-SINTRAN-M.IMG\n\n' +
    '[controller.eth.0]\n' +
    'enabled = yes\n' +
    'net = gateway:0           ; frames go through the gateway, Ethernet segment 0\n\n' +
    '[terminals]\n' +
    'enabled = 5, 6, 7, 8, 9, 10, 11\n\n' +
    '[boot]\n' +
    'device = wd.0.0           ; <type>.<wheel>.<unit>\n\n' +
    '[runtime]\n' +
    'memory = 4\n';

  // COSMOS over Ethernet: ND-100/CX booting SINTRAN from the SMD image
  // BIGDISK0-K-100.IMG. COSMOS uses IEEE 802.3 length framing on the same
  // Ethernet II card and gateway segment 0.
  var COSMOS_INI =
    '# nd100x machine configuration - SINTRAN with COSMOS over Ethernet\n' +
    '# SMD image BIGDISK0-K-100.IMG; Ethernet II card on gateway segment 0.\n\n' +
    '[machine]\n' +
    'cpu = ND100CX\n' +
    'mms = 2\n' +
    'fpp = 48\n\n' +
    '[controller.floppy.0]\n' +
    'enabled = yes\n' +
    'disk0 = FLOPPY.IMG\n\n' +
    '[controller.smd.0]\n' +
    'enabled = yes\n' +
    'disk0 = BIGDISK0-K-100.IMG\n\n' +
    '[controller.eth.0]\n' +
    'enabled = yes\n' +
    'net = gateway:0           ; frames go through the gateway, Ethernet segment 0\n\n' +
    '[terminals]\n' +
    'enabled = 5, 6, 7, 8, 9, 10, 11\n\n' +
    '[boot]\n' +
    'device = smd.0.0          ; <type>.<wheel>.<unit>\n\n' +
    '[runtime]\n' +
    'memory = 4\n';

  function shippedProfiles() {
    return [
      { name: 'ND-100',   kind: 'nd100', ini: DEFAULT_INI },
      { name: 'ND-5000',  kind: 'nd100', ini: ND5000_INI },
      { name: 'BSD 2.11', kind: 'nd100', ini: BSD_INI },
      { name: '500 NDIX-C', kind: 'nd500-ndix',
        ndix: { diskUrl: 'NDIX.IMG', diskName: 'NDIX root disk (server)', memoryMb: 16, writable: true } },
      // A 1973 teletype system: the VT100 (xterm) is the plain terminal here.
      { name: 'TSS',      kind: 'nd100', ini: TSS_INI,
        terminal: { backend: 'xterm', emulator: 'vt100', language: 'no' } },
      { name: 'TCP/IP',   kind: 'nd100', ini: TCPIP_INI },
      { name: 'COSMOS',   kind: 'nd100', ini: COSMOS_INI }
    ];
  }
  // The shipped machines are BUILT IN: read-only, always present, and kept
  // at the current definition by the store upgrade. Clone one to change it.
  var SHIPPED_NAMES = ['ND-100', 'ND-5000', 'BSD 2.11', '500 NDIX-C', 'TSS', 'TCP/IP', 'COSMOS'];
  var OLD_SHIPPED_NAMES = ['NDIX C', 'ND100'];   // shipped under these names once, then renamed
  function isShippedName(name) { return SHIPPED_NAMES.indexOf(name) >= 0; }

  // A user NDIX machine that is the demo one again: no library disc, the
  // served demo root disc (under either of its names). Made before 500
  // NDIX-C shipped; the upgrade drops it as a duplicate.
  function duplicatesShippedNdix(p) {
    if (!p || p.kind !== 'nd500-ndix') return false;
    var n = normalizeNdix(p.ndix, 'nd500-ndix');
    return !n.diskUuid && (!n.diskUrl || n.diskUrl === 'rootfs_full.img' || n.diskUrl === 'NDIX.IMG');
  }

  var STORE_VERSION = 6;   // 2: shipped machines added; 3: built in and first, leftovers dropped; 4: ND-100 name, "Default" gone; 5: TSS shipped; 6: TCP/IP and COSMOS shipped

  // A profile's "kind" decides what boots it and what it is made of:
  //   'nd100'      - the ND-100 INI machine this file has always stored.
  //   'nd500-ndix' - a standalone ND-500 running NDIX. No ND-100, no MFbus,
  //                  no octobus - a different machine entirely, not an
  //                  ND-100 with something extra. Holds `ndix` settings
  //                  instead of `ini`.
  // Every profile saved before this existed has no `kind` field at all;
  // every reader below treats that as 'nd100', so nothing needs a migration
  // pass over the stored JSON.
  // The root disc is one of two things: an image in the local disk library
  // (diskUuid - Worker mode, where the library exists) or an image served
  // next to the page (diskUrl - the catalog's own file, which is all demo
  // mode has, the same way the ND-100 demo fetches SMD0.IMG). diskUuid wins
  // when both are set. Writable by default: the disc is loaded into memory
  // for the session and nothing is written back, so read-only would only
  // stop NDIX doing what it does the moment it boots - write to its root.
  var DEFAULT_NDIX = { diskUuid: '', diskUrl: '', diskName: '', memoryMb: 16, writable: true };

  function normalizeNdix(data) {
    data = data || {};
    return {
      diskUuid: (typeof data.diskUuid === 'string') ? data.diskUuid : '',
      diskUrl:  (typeof data.diskUrl === 'string') ? data.diskUrl : '',
      diskName: (typeof data.diskName === 'string') ? data.diskName : '',
      memoryMb: ([8, 16, 32].indexOf(data.memoryMb) >= 0) ? data.memoryMb : DEFAULT_NDIX.memoryMb,
      writable: (typeof data.writable === 'boolean') ? data.writable : DEFAULT_NDIX.writable
    };
  }

  // The machine's terminals: which renderer draws them, which terminal it
  // emulates, which national keyboard. A SINTRAN machine wants the TDV2200
  // (RetroTerm); NDIX drives its console with ANSI/VT100 escapes and wants
  // xterm - so these belong to the machine, not to the browser. Defaults
  // differ by kind for that reason.
  var TERMINAL_BACKENDS = ['retroterm', 'xterm'];
  var TERMINAL_EMULATORS = ['tdv2200', 'tdv2215', 'vt100'];
  var TERMINAL_LANGUAGES = ['off', 'no', 'se', 'de'];

  function defaultTerminal(kind) {
    return (kind === 'nd500-ndix')
      ? { backend: 'xterm', emulator: 'vt100', language: 'off' }
      : { backend: 'retroterm', emulator: 'tdv2200', language: 'no' };
  }

  function normalizeTerminal(data, kind) {
    var d = defaultTerminal(kind);
    data = data || {};
    return {
      backend:  (TERMINAL_BACKENDS.indexOf(data.backend) >= 0) ? data.backend : d.backend,
      emulator: (TERMINAL_EMULATORS.indexOf(data.emulator) >= 0) ? data.emulator : d.emulator,
      language: (TERMINAL_LANGUAGES.indexOf(data.language) >= 0) ? data.language : d.language
    };
  }

  // The pop-out terminal page has no machine store and reads these keys, as
  // the whole page did before the settings moved to the machine. Kept
  // pointing at the ACTIVE machine's settings so a pop-out matches it.
  function mirrorTerminalKeys(t) {
    try {
      localStorage.setItem('nd100x-terminal-backend', t.backend);
      localStorage.setItem('nd100x-emulator-type', t.emulator);
      localStorage.setItem('nd100x-keyboard-language', t.language);
    } catch (e) {}
  }

  // Library images chosen for the machine's drives, by "type.wheel.slot"
  // (smd.0.0, scsi.0.3, wd.0.0, floppy.0.1): { uuid, name }. The INI names
  // only the FILE (disk<n> = FILE); this says which local-library image
  // that file is, and power-on mounts it from the library. The machine is
  // the master of what is in its drives. 'cdc.0.0' is the NORD TSS
  // cartridge disc, whose file is [runtime] cdc = (not a controller unit).
  var LIBRARY_KEY_RE = /^(smd|scsi|wd|floppy|cdc)\.\d+\.\d+$/;
  function normalizeLibrary(map) {
    var out = {};
    if (!map || typeof map !== 'object') return out;
    for (var k in map) {
      if (!Object.prototype.hasOwnProperty.call(map, k) || !LIBRARY_KEY_RE.test(k)) continue;
      var e = map[k];
      if (!e || typeof e.uuid !== 'string' || !e.uuid || typeof e.file !== 'string' || !e.file) continue;
      out[k] = { uuid: e.uuid, file: e.file, name: (typeof e.name === 'string') ? e.name : e.file };
    }
    return out;
  }

  function normalizeFloppies(map) {
    var out = {};
    if (!map || typeof map !== 'object') return out;
    for (var k in map) {
      if (!Object.prototype.hasOwnProperty.call(map, k)) continue;
      var e = map[k], slot = parseInt(k, 10);
      if (!(slot >= 0 && slot <= 2) || !e || typeof e.file !== 'string' || !e.file ||
          typeof e.imageUrl !== 'string' || !e.imageUrl) continue;
      out[slot] = { file: e.file, name: (typeof e.name === 'string') ? e.name : e.file,
                    imageUrl: e.imageUrl, md5: (typeof e.md5 === 'string') ? e.md5 : '' };
    }
    return out;
  }

  // ---- storage ------------------------------------------------------------
  // Every read goes through load(), so a corrupt or absent key produces a
  // usable store rather than an exception halfway up the UI.

  function load() {
    var raw = null;
    try { raw = localStorage.getItem(KEY); } catch (e) {}

    if (raw) {
      try {
        var s = JSON.parse(raw);
        if (s && s.profiles && s.profiles.length) return upgrade(s);
      } catch (e) {
        // Unparseable: fall through and rebuild. Keeping a broken blob would
        // mean the window never opens again.
      }
    }

    // First run, or a store we could not read: the built-in machines. The old
    // single-key config (OLD_KEY) is no longer carried in as a machine - it
    // was always this page's own stock machine, which is the built-in ND-100
    // now; the key is still written for whatever reads it.
    return { v: STORE_VERSION, active: DEFAULT_NAME, profiles: shippedProfiles() };
  }

  // A store written before the shipped machines existed gets them added, by
  // name, behind whatever the user already has. Nothing of theirs is touched
  // and the active machine stays theirs; the version says it was done, so a
  // shipped machine they then delete stays deleted.
  function upgrade(s) {
    if ((s.v || 1) >= STORE_VERSION) return s;
    var keep = [];
    for (var i = 0; i < s.profiles.length; i++) {
      var p = s.profiles[i];
      // The built-in machines are taken from their current definition (ND100
      // and NDIX C were their earlier names).
      if (isShippedName(p.name) || OLD_SHIPPED_NAMES.indexOf(p.name) >= 0) continue;
      // "Default": the machine this page made for itself before the built-in
      // ones existed. Whatever it has become, the ND-100 is the built-in
      // ND-100 now - it goes, in whatever state.
      if (p.name === 'Default') continue;
      // A copy of the demo NDIX machine made before it shipped.
      if (duplicatesShippedNdix(p)) continue;
      keep.push(p);
    }
    s.profiles = shippedProfiles().concat(keep);
    if (s.active === 'ND100') s.active = 'ND-100';
    if (!find(s, s.active)) s.active = DEFAULT_NAME;
    s.v = STORE_VERSION;
    save(s);
    return s;
  }

  // A deep copy of a profile under a new name (everything but the name).
  function copyProfile(p, newName) {
    var c = JSON.parse(JSON.stringify(p));
    c.name = newName;
    return c;
  }

  function save(s) {
    try {
      localStorage.setItem(KEY, JSON.stringify(s));
      // Keep the single-config key pointing at whatever is active. Only
      // meaningful for an 'nd100' profile - an 'nd500-ndix' one has no `ini`
      // and the native binary has no use for this key either way.
      var p = find(s, s.active);
      if (p && p.ini) localStorage.setItem(OLD_KEY, p.ini);
      return true;
    } catch (e) {
      return false;
    }
  }

  function find(s, name) {
    for (var i = 0; i < s.profiles.length; i++)
      if (s.profiles[i].name === name) return s.profiles[i];
    return null;
  }

  // A name has to be usable as a dropdown label and as a thing you type, so
  // trim it and refuse an empty one. Duplicates are refused rather than
  // silently numbered: two profiles called the same thing is a trap.
  function cleanName(name) {
    return (typeof name === 'string') ? name.replace(/^\s+|\s+$/g, '') : '';
  }

  // ---- the API ------------------------------------------------------------

  window.machineProfiles = {

    DEFAULT_INI: DEFAULT_INI,

    /* Names, in display order. */
    list: function () {
      var s = load(), out = [];
      for (var i = 0; i < s.profiles.length; i++) out.push(s.profiles[i].name);
      return out;
    },

    /* The active profile's name. Always one of list(). */
    activeName: function () {
      var s = load();
      return find(s, s.active) ? s.active : s.profiles[0].name;
    },

    /* The INI of <name>, or of the active profile when <name> is omitted.
     * Falls back to the default rather than returning null: a caller about to
     * boot a machine should get a machine. */
    ini: function (name) {
      var s = load();
      var p = find(s, cleanName(name) || s.active) || s.profiles[0];
      // An 'nd500-ndix' profile has no ini at all. Callers that render the
      // ND-100 form (hidden for that kind) still get a parseable machine
      // rather than undefined.
      return (p && p.ini) ? p.ini : DEFAULT_INI;
    },

    /* Switch the active profile. Returns false for an unknown name. */
    setActive: function (name) {
      var s = load(), n = cleanName(name);
      var p = find(s, n);
      if (!p) return false;
      s.active = n;
      mirrorTerminalKeys(normalizeTerminal(p.terminal, p.kind === 'nd500-ndix' ? 'nd500-ndix' : 'nd100'));
      return save(s);
    },

    /* 'nd100' (the default, including every profile saved before this
     * existed) or 'nd500-ndix'. toolbar.js reads this before deciding how
     * Power/Boot behave. */
    kind: function (name) {
      var s = load();
      var p = find(s, cleanName(name) || s.active) || s.profiles[0];
      return (p && p.kind === 'nd500-ndix') ? 'nd500-ndix' : 'nd100';
    },

    /* The NDIX settings of <name> (or the active profile). Always a usable
     * object - for an 'nd100' profile, or one saved before this existed, that
     * is DEFAULT_NDIX. */
    ndix: function (name) {
      var s = load();
      var p = find(s, cleanName(name) || s.active) || s.profiles[0];
      return normalizeNdix(p && p.ndix);
    },

    /* The terminal settings of <name> (or the active profile):
     * { backend: 'retroterm'|'xterm', emulator: 'tdv2200'|'tdv2215'|'vt100',
     *   language: 'off'|'no'|'se'|'de' }. Always usable; a profile without
     * them answers the defaults of its kind. */
    terminal: function (name) {
      var s = load();
      var p = find(s, cleanName(name) || s.active) || s.profiles[0];
      return normalizeTerminal(p && p.terminal, p && p.kind === 'nd500-ndix' ? 'nd500-ndix' : 'nd100');
    },

    /* Write terminal settings into <name> (or the active profile). */
    writeTerminal: function (settings, name) {
      var s = load(), n = cleanName(name) || s.active;
      var p = find(s, n);
      if (!p || isShippedName(n)) return false;
      p.terminal = normalizeTerminal(settings, p.kind === 'nd500-ndix' ? 'nd500-ndix' : 'nd100');
      if (n === s.active) mirrorTerminalKeys(p.terminal);
      return save(s);
    },

    /* Local-library images chosen for the machine's drives, keyed
     * "type.wheel.slot": { uuid, file, name }. file is what the INI's
     * disk<n> = names; a slot whose INI file no longer matches is a plain
     * file again. Always an object. */
    library: function (name) {
      var s = load();
      var p = find(s, cleanName(name) || s.active) || s.profiles[0];
      return normalizeLibrary(p && p.library);
    },

    /* Replace the library choices of <name> (or the active profile). */
    writeLibrary: function (map, name) {
      var s = load(), n = cleanName(name) || s.active;
      var p = find(s, n);
      if (!p || isShippedName(n)) return false;
      p.library = normalizeLibrary(map);
      return save(s);
    },

    /* Catalog floppies chosen for an ND-100 machine's floppy drives, by slot
     * (0-2 of [controller.floppy.0]): { slot: {file, name, imageUrl, md5} }.
     * The INI names only the FILE (disk<n> = FILE); this says where the page
     * gets that file from at power-on - the Norsk Data software archive. A
     * slot whose INI file no longer matches its entry is simply a local file
     * again. Always an object. */
    floppies: function (name) {
      var s = load();
      var p = find(s, cleanName(name) || s.active) || s.profiles[0];
      return normalizeFloppies(p && p.floppies);
    },

    /* Replace the catalog-floppy choices of <name> (or the active profile). */
    writeFloppies: function (map, name) {
      var s = load(), n = cleanName(name) || s.active;
      var p = find(s, n);
      if (!p || isShippedName(n)) return false;
      p.floppies = normalizeFloppies(map);
      return save(s);
    },

    /* Write NDIX settings into <name> (or the active profile) and mark it
     * 'nd500-ndix'. Mirrors write() for the INI side. */
    writeNdix: function (settings, name) {
      var s = load(), n = cleanName(name) || s.active;
      if (isShippedName(n)) return 'The built-in machine "' + n + '" cannot be changed - clone it first.';
      var p = find(s, n);
      var norm = normalizeNdix(settings);
      if (p) { p.kind = 'nd500-ndix'; p.ndix = norm; }
      else s.profiles.push({ name: n, kind: 'nd500-ndix', ndix: norm });
      s.active = n;
      return save(s) ? '' : 'Could not save (browser storage refused).';
    },

    /* Write <ini> into <name> (or the active profile). Returns false only if
     * localStorage refused. */
    write: function (ini, name) {
      var s = load(), n = cleanName(name) || s.active;
      if (isShippedName(n)) return false;
      var p = find(s, n);
      if (p) p.ini = ini;
      else s.profiles.push({ name: n, ini: ini });
      s.active = n;
      return save(s);
    },

    /* The built-in (shipped) machines: read-only, always present. */
    isShipped: function (name) { return isShippedName(cleanName(name)); },
    shippedNames: function () { return SHIPPED_NAMES.slice(); },

    /* A copy of <name> (or the active profile) as <newName>, made active:
     * the way a built-in machine is changed. Returns an error string, or "". */
    clone: function (name, newName) {
      var s = load(), o = cleanName(name) || s.active, n = cleanName(newName);
      var p = find(s, o);
      if (!p) return 'No machine called "' + o + '".';
      if (!n) return 'Give the copy a name.';
      if (find(s, n) || isShippedName(n)) return 'There is already a machine called "' + n + '".';
      s.profiles.push(copyProfile(p, n));
      s.active = n;
      mirrorTerminalKeys(normalizeTerminal(p.terminal, p.kind === 'nd500-ndix' ? 'nd500-ndix' : 'nd100'));
      return save(s) ? '' : 'Could not save (browser storage refused).';
    },

    /* Create an ND-100 profile from <ini> and make it active. Returns an
     * error string, or "". */
    create: function (name, ini) {
      var s = load(), n = cleanName(name);
      if (!n) return 'Give the machine a name.';
      if (find(s, n) || isShippedName(n)) return 'There is already a machine called "' + n + '".';
      s.profiles.push({ name: n, kind: 'nd100',
        ini: (typeof ini === 'string' && ini.length) ? ini : DEFAULT_INI });
      s.active = n;
      return save(s) ? '' : 'Could not save (browser storage refused).';
    },

    /* Create a standalone ND-500 (NDIX) profile from <settings> (see
     * normalizeNdix) and make it active. Returns an error string, or "". */
    createNdix: function (name, settings) {
      var s = load(), n = cleanName(name);
      if (!n) return 'Give the machine a name.';
      if (find(s, n) || isShippedName(n)) return 'There is already a machine called "' + n + '".';
      s.profiles.push({ name: n, kind: 'nd500-ndix', ndix: normalizeNdix(settings) });
      s.active = n;
      return save(s) ? '' : 'Could not save (browser storage refused).';
    },

    /* Rename, keeping the INI and the active selection. Returns "" or why not. */
    rename: function (oldName, newName) {
      var s = load(), o = cleanName(oldName), n = cleanName(newName);
      if (!n) return 'Give the machine a name.';
      var p = find(s, o);
      if (!p) return 'No machine called "' + o + '".';
      if (isShippedName(o)) return 'The built-in machine "' + o + '" cannot be renamed - clone it instead.';
      if (n !== o && (find(s, n) || isShippedName(n))) return 'There is already a machine called "' + n + '".';
      p.name = n;
      if (s.active === o) s.active = n;
      return save(s) ? '' : 'Could not save (browser storage refused).';
    },

    /* Delete. The last profile is NOT deletable - an empty list would leave
     * the window with nothing to show and the boot with nothing to use. */
    remove: function (name) {
      var s = load(), n = cleanName(name);
      if (s.profiles.length <= 1) return 'This is the only machine - keep at least one.';
      if (isShippedName(n)) return 'The built-in machine "' + n + '" cannot be deleted.';
      var idx = -1;
      for (var i = 0; i < s.profiles.length; i++) if (s.profiles[i].name === n) idx = i;
      if (idx < 0) return 'No machine called "' + n + '".';
      s.profiles.splice(idx, 1);
      if (s.active === n) s.active = s.profiles[0].name;
      return save(s) ? '' : 'Could not save (browser storage refused).';
    }
  };
})();
