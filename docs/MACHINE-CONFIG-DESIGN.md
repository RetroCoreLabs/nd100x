# Machine Configuration Design (nd100x / nd110x / nd120x)

Status: DESIGN DRAFT for review. No code committed against this yet.
Author target: /path/to/nd100x

This document proposes replacing the ever-growing per-device command line with a
single, user-editable **machine configuration** expressed as an INI file, plus a
controller registry that both the native binary and the WASM Glass UI consume.

It supersedes the ad-hoc plan in the earlier SCSI/HDD chat and folds that work in
as the *first client* of the new model.

---

## 1. Goals and hard requirements (from the user)

1. **INI, text-only, easy to hand-edit.** Not JSON, not TOML. Plain `[section]` +
   `key = value`.
2. **User-friendly validation errors.** Every rejection names the file, the
   section/line, what was wrong, and how to fix it.
3. **Binary-name autoload.** The binary looks for `<argv0-basename>.ini` in the
   current directory. `nd100x` -> `nd100x.ini`; a rename/symlink to `nd110x`
   -> `nd110x.ini`. An explicit `--config FILE` overrides.
4. **Controllers are (type + thumbwheel) instances.** No duplicate
   `type + thumbwheel`. Duplicate detection is natural: it is a duplicate INI
   section.
5. **Ident code is NOT part of config at all.** It lives in each controller's
   source and is derived from the thumbwheel by the device itself, purely for
   the machine's own use. The config layer never sets, stores, or displays it.
6. **Web config window reads and writes the SAME INI.** It parses the INI to
   build the UI, and serializes the UI back to INI. It also offers to
   **download the .ini** file so the user can run the native binary with the
   exact same machine.
7. **CPU type is live now.** `[machine] cpu = 100|110|120`, default 100. Stored
   in the machine model and selectable from the web window. (Behavioral
   differences between 100/110/120 are minimal today per project goals - this
   sets the selected CPU mode; instruction-set effects land as they are
   implemented.)
8. **Only CPU + RTC + console are permanent.** Everything else - printer, paper
   tape, floppy, SMD, SCSI, HDLC, extra terminals - is a configurable peripheral/
   controller. For backward compatibility the disc + core peripherals default to
   ON when no INI is present (today's behavior).

---

## 2. The controller registry (single source of truth)

One descriptor per controller *type*. This table is the ONLY place that knows a
controller's address map - the CLI parser, INI parser, validator, WASM window,
and gateway all read from it. Adding a controller type is one new row.

Descriptor fields (proposed C struct `ControllerDescriptor`):

| field                | meaning                                                        |
|----------------------|----------------------------------------------------------------|
| `type`               | lowercase name used in INI sections (`smd`, `scsi`, `hdlc`, …) |
| `min_wheel`/`max_wheel` | allowed thumbwheel range                                    |
| `iox_base[wheel]`    | IOX base address for each allowed thumbwheel                   |
| `iox_span`           | number of IOX addresses the card claims (for overlap checks)   |
| `disk_slots`         | number of image slots (0 = not a disc controller)              |
| `slot_media`         | allowed media per slot (hdd/cdrom/tape/floppy) or none         |
| `default_media`      | default media type when unspecified                            |
| `bootable`           | may this controller be a boot device?                          |
| `add_fn`             | fn that instantiates + configures the device (existing adders) |

### Known values to seed the table (VERIFY against device source before coding)

These are read from the current code; the ident derivations MUST be taken from
each device's `Ident` implementation, not invented:

- **smd**  — wheel 0, IOX base `01540`, disc, 4 slots (SMD only), bootable.
  Currently added unconditionally in `DeviceManager_AddAllDevices()`
  (`src/devices/devicemanager.c:122`).
- **floppy** — wheel 0, IOX base `01560` (DMA), disc, 3 slots, bootable.
  (`devicemanager.c:120`).
- **wd** — wheel 0, IOX base `0500`, disc, 2 slots (ST506/8" Winchester,
  cards 3041/3038; the control word carries the unit in one bit, so two units
  is the hardware maximum). Opt-in: NOT added by `DeviceManager_AddAllDevices()`
  because IOX 500-507 is also the CDC cartridge disc - a machine has one card
  or the other (`src/devices/winchester/device_winchester.h`). The device layer
  also supports wheel 1 (IOX 510-517, disk system 2), but the machine layer's
  mount table has a single 2-slot pool, so config exposes wheel 0 only.
- **scsi** — wheel 0-3, IOX bases `{0144300,0144400,0144500,0144600}`
  (`devicemanager.c:147`), disc, 7 slots (IDs 0-6; ID 7 = controller),
  media hdd/cdrom/tape/floppy (only hdd implemented), bootable.
- **hdlc** — wheel 1-4, IOX bases `{01640,01660,01700,01720}`
  (`devicemanager.c:169`), not a disc (network), not bootable.
- **rtc / console** — the ONLY permanent devices besides the CPU. Always added,
  wheel 0, never removable. They stay in the base-machine setup and are listed
  here only so the overlap validator knows their reserved IOX ranges.
- **paper-tape (reader/punch) / line-printer / extra terminals** — configurable
  peripherals. Default ON when no INI is present (compatibility); representable
  in the INI so a user can omit or tune them. Full peripheral config may land in
  a later step; v1 focuses the config UI on disc + hdlc controllers + cpu.

Ident codes are intentionally absent from this table: they are computed inside
each device (from the thumbwheel) and are never surfaced to config.

---

## 3. The machine model

In-memory `MachineConfig` (C), the thing every front end builds and every
consumer reads:

```
MachineConfig {
    int      cpu_type;              // 100 | 110 | 120 (default 100)
    Controller controllers[N];      // each: {type, wheel, disks[], settings}
    BootSpec boot;                  // {controller_type, wheel, unit} or bpun/aout/floppy file
    // plus non-hardware runtime opts that stay CLI-only (see s.7)
}
Controller {
    ControllerType type;
    int wheel;
    Disk disks[max slots];          // {slot, media_type, image_path}
    // type-specific settings (hdlc mode/host/port, ...)
}
```

Validation invariants (all with friendly messages, see s.5):
- Each `(type,wheel)` unique.
- `wheel` within the descriptor's allowed range.
- **No IOX range overlap** across all instantiated controllers + core devices.
  (This is the real invariant; duplicate type+wheel is one way to break it.)
- Boot device must reference a controller that exists and is `bootable`, and a
  slot that has an image (and, for SCSI, media = hdd until others land).
- Disc slot indices within the controller's `disk_slots`.
- Media type allowed by the slot and implemented.

---

## 4. INI schema

Section name carries the identity: `[controller.<type>.<wheel>]`. A duplicate
section is a duplicate controller and is rejected by name. Each device is toggled
with `enabled = yes|no` rather than by deleting its block, so a person can flip a
device on/off without losing its settings. A disabled section is parsed but not
instantiated (and skipped by the overlap validator). See the shipped default
`nd100x.ini` for the full commented template, which also
includes `[terminals]` (enabled = list of terminal numbers; console/0 always on)
and `[peripheral.*]` sections (paper-tape reader/punch, line printer).

```ini
# nd100x.ini - machine configuration
# Lines starting with # or ; are comments. Keys are case-insensitive.

[machine]
cpu = 100                 ; 100 | 110 | 120   (default 100)
fpp = 48                  ; 32 | 48 - installed floating point unit (default 48;
                          ; 32 = optional single-precision FPP, T untouched by NLZ/DNZ)
rtc = ticks               ; ticks | wall - RTC time base (default ticks;
                          ; ticks = one pulse per 10550 executed instructions,
                          ; wall = one pulse per 20 ms of host wall-clock time)

[controller.smd.0]        ; SMD controller on thumbwheel 0
disk0 = SMD0.IMG          ; unit 0 (boot unit)
disk1 = DATA.IMG          ; unit 1

[controller.scsi.0]       ; SCSI (ND-3201/3204) on thumbwheel 0
disk0 = hdd:SCSI-K.image  ; SCSI ID 0, media hdd
disk3 = cdrom:cd.iso      ; SCSI ID 3, media cdrom (when implemented)

[controller.hdlc.1]       ; HDLC on thumbwheel 1 (server)
mode = server
port = 5000

[controller.hdlc.2]       ; HDLC on thumbwheel 2 (client)
mode = client
host = 192.168.1.10
port = 5001

[boot]
device = scsi.0.0         ; <type>.<wheel>.<unit>. Also: floppy.0.0, smd.0.0
# device = bpun:test.bpun ; non-disc boots keep a file form
# device = aout:prog.out

[runtime]                 ; non-hardware options (optional; CLI overrides these)
telnet = 9000             ; enable telnet server on port (omit = off)
throttle = 0.5275         ; MHz real-time throttle (omit = full speed)
charset = norwegian       ; local console charset: off|norwegian|swedish|german
printdir = ./prints
tapedir = ./tapes
# debugger = 6661         ; enable DAP debugger on port
# trace = on
```

Notes:
- `diskN = [media:]path`. `media` optional, defaults to the controller's
  `default_media` (hdd for scsi; the only media for smd/floppy). A bare Windows
  path like `C:\x.img` is still a path (media prefix only honored when the text
  before the first `:` is a known media word) - same rule as today's `--scsiN`.
- Ident never appears here or anywhere in config. `nd100x --show-config` (new)
  prints the resolved machine (controllers, wheels, IOX ranges, disks, boot).

---

## 5. Validation & friendly errors

The parser reports the FILE + line/section + problem + fix. Examples:

```
nd100x.ini:14  [controller.scsi.0]: duplicate controller.
    A 'scsi' controller on thumbwheel 0 is already defined at line 9.
    Each controller+thumbwheel may appear only once.

nd100x.ini:22  [controller.scsi.0] disk7: SCSI ID 7 is the controller itself.
    Valid SCSI IDs are 0-6. Use a different id.

nd100x.ini:31  [controller.hdlc.5]: thumbwheel 5 out of range for hdlc (1-4).

nd100x.ini:40  IOX address clash: 'scsi' on thumbwheel 0 (0144300-0144337)
    overlaps 'scsi' on thumbwheel 0. Move one controller to another thumbwheel.

nd100x.ini:46  [boot] device = scsi.0.4: SCSI ID 4 has no image.
    Add 'disk4 = hdd:FILE' to [controller.scsi.0], or boot a different device.

nd100x.ini:46  [boot] device = scsi.0.0: media is 'cdrom' but only 'hdd' can boot.
```

Implementation: a small INI tokenizer (there is no INI lib in-tree; a ~150-line
hand parser is enough and avoids a new dependency), feeding the descriptor-driven
validator. Shared by native and (compiled to WASM) the browser.

---

## 6. Binary-name autoload

At startup, if no `--config FILE` is given:
1. Derive base name from `argv[0]` (basename, strip directory and any `.exe`).
2. Look for `<base>.ini` in the current working directory.
3. If found, load it. If not found, fall back to built-in defaults (today's
   "SMD at TW0, boot smd" behavior) so a bare `./nd100x` still boots.

`nd100x` -> `nd100x.ini`; symlink/rename to `nd110x` -> `nd110x.ini`. This pairs
naturally with the coming CPU-type selection: an `nd110x` symlink can ship an
`nd110x.ini` whose `[machine] cpu = 110`.

---

## 7. CLI relationship (what stays, what goes)

- **Hardware moves into the INI.** `--smd0..3`, `--scsi0..6`, `--hdlc=...`,
  `--boot=...` become either (a) thin sugar that mutates the in-memory config
  before validation, kept as *deprecated aliases* for one release, or (b)
  removed in favor of `--config`. Recommendation: keep as aliases one release,
  print a deprecation hint suggesting the INI equivalent.
- **Runtime options live in the `[runtime]` INI section AND on the CLI.** The
  INI provides defaults; a CLI flag (`--telnet`, `--throttle`, `--charset`,
  `--printdir/--tapedir`, `--debugger/--port`, `--trace`, `--watch`,
  `--breakpoint`, `--max-instr`, `--ring-dump`, `--verbose`) overrides the INI
  value for that run. CLI wins so one-off debugging never requires editing the
  file.
- **New helper flags**: `--config FILE`, `--show-config` (resolve + print the
  machine, including derived idents), `--write-config FILE` (dump current
  resolved machine as INI - the native equivalent of the web "download .ini").

---

## 8. WASM / Glass UI config window

- A "Machine Setup" window listing **available controller types** (from the same
  descriptor table, compiled to WASM) with Add/Remove. Adding prompts for
  thumbwheel (only free ones offered) and disks; duplicate/overlap is prevented
  in the UI using the same validator, so the user cannot build an invalid
  machine.
- The window's model IS the INI: it stores the INI text in localStorage
  (`nd100x-machine-ini`), parses it to render, serializes on edit.
- **Download .ini** button writes `nd100x.ini` for use with the native binary.
- The disc controllers' disks reuse the HDD Disk Manager (tabbed SMD/SCSI/
  Winchester) from the earlier plan - the manager becomes the "disks" editor for
  a selected controller instance.
- CPU-type dropdown (100/110/120) wired to `[machine] cpu` (disabled until CPU
  selection lands).

---

## 9. How the in-flight SCSI/HDD work folds in

Already done and still valid under this model (drive types are independent of the
config mechanism):
- `DRIVE_WINCHESTER` enum stub (C) + `drive_type_name`.
- `template-glass/js/disk-types.js` shared constants.
- OPFS block I/O threaded with `driveType` (C `machine.c` + `emu-worker.js`) so
  SCSI unit 0 does not alias SMD unit 0.

WIP / to reconcile:
- `emu-worker.js` currently references `Module._MountSCSIFromOPFS` /
  `_UnmountSCSI`, which are NOT yet added in `nd100wasm.c` (tree does not link as
  WASM right now). Either add those exports (Phase 1 of the old plan) or gate the
  references until the config work reaches the mount layer.

Recommended integration order once this design is approved:
1. Controller registry + `MachineConfig` + INI parser + validator (native C).
2. Binary-name autoload + `--config`/`--show-config`/`--write-config`.
3. Port existing devices (smd/floppy/scsi/hdlc) onto the registry; old flags
   become aliases.
4. Finish SCSI mount exports + HDD tabbed manager as the disc editor.
5. WASM "Machine Setup" window + INI save/download.
6. Gateway: per-controller disc images keyed by (type,wheel,unit); driveType
   already on the wire.
7. Docs + version bump + tag.

---

## 10. Open items to confirm before coding

1. RESOLVED: ident is machine-internal only (in each device's source), never in
   config - not stored, not displayed. No ident table needed.
2. RESOLVED: only CPU + RTC + console are permanent. Printer, paper tape, floppy,
   SMD, SCSI, HDLC, extra terminals are all configurable (default ON when no INI).
3. RESOLVED: `[runtime]` INI section is in; CLI flags override it per run.
4. RESOLVED (default): keep old hardware flags one release as aliases that feed
   the same model, with a deprecation hint. Revisit removal later.
5. RESOLVED: CPU-type is live now - `[machine] cpu` selects 100/110/120 and is
   stored in the model + web window; instruction-set effects land incrementally.

All open items resolved. Ready to implement in the order listed in section 9.
