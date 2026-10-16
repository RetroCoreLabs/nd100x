# Machine configuration INIs

Each file in this folder describes one complete emulated machine: CPU,
memory, controllers, discs, what to boot and the runtime options. Run one
from the repo root with

```bash
build/bin/nd100x --config config/<file>.ini
build/bin/nd100x --config config/<file>.ini --show-config   # print the resolved machine and exit
```

Disc image names inside an INI are opened relative to the directory nd100x
is started from, not relative to the INI file, so start from the repo root
where the images live.

| File | Machine | Boots | Needs |
|---|---|---|---|
| `tpe.ini` | ND-100 front end with MFbus shared memory, octobus card and one ND-5000 CPU | TPE diagnostic floppy, `floppy.0.0` | `FLOPPY.IMG`, `SMD0.IMG`, `SCSI0.IMG` |
| `nd100-eth.ini` | ND-100/CX, MMS-2, 48-bit FPP, 4 MB, Winchester disc, Ethernet II card on `tap:nd0` | SINTRAN III M with TCP/IP, `wd.0.0` | `WD0-SINTRAN-M.IMG`, a TAP device `nd0` |
| `nd100-cosmos.ini` | Same ND-100/CX and Ethernet II card as `nd100-eth.ini`, SMD disc | SINTRAN III with COSMOS, `smd.0.0` | `BIGDISK0-K-100.IMG`, a TAP device `nd0` |
| `ND5000.ini` | ND-100 with MFbus shared memory (8 MB at page 04100B), octobus card and one ND-5000 CPU, slots 2-4 commented out | SINTRAN with ND-500 support, `smd.0.0` | `ND5000.IMG`, `FLOPPY.IMG`, `SCSI0.IMG` |

One machine INI is not in this folder: `nd100x.ini` in the repo root is the
default machine. The binary loads the INI named after itself from the
current directory when no `--config` is given (`nd100x` reads `nd100x.ini`,
`nd110x` reads `nd110x.ini`), so it has to stay there. It is a floppy-booting
ND-100 with a small ND-5000 (2 MB pool) on top.

Each INI explains its own sections and keys in comments. The full key list
is printed by `nd100x --help` under `--config`, and the design is in
`docs/MACHINE-CONFIG-DESIGN.md`. Three rules that catch new readers:

- Naming a section is what enables the hardware for `[mfbus]`,
  `[controller.octobus.0]`, `[nd5000.N]` and `[nd500]`. Comment the section
  out to remove the hardware; `enabled = no` keeps the section but builds
  nothing.
- Loading any INI starts from a machine with no controllers. A controller
  that is not in the file is not in the machine.
- A command-line flag always wins over the INI key that means the same
  thing.

A shell-mode example that boots no disc is in
`tools/examples/shell/example-shell-config.ini`.
