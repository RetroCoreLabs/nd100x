# ND Linkage-Loader H02 (ND 210319H02)

Installs ND's Linkage-Loader (NLL), the ND-500/5000 loader, as the standard domain
`LINKAGE-LOADER`, with ND's own installer `IN-NLL-XX-H02:PROG`.

```sh
# from the repository root, in WSL / Linux
python3 tools/installs/linkage-loader-h02/install.py ND5000-N5MON.IMG /tmp/ND5000-NLL.IMG
```

The source pack is only copied. The result appears only when every check below has passed.

## What you need

- **A built nd100x** (`build/bin/nd100x` or `--nd100x PATH`) and **Python 3**.
- **On the pack, in `@LIST-REENTRANT`:** `BACKUP-SYSTEM` (the installer copies with it,
  [`backup-system-i05`](../backup-system-i05/README.md)) and `ND-500-MONITOR`
  ([`nd500-monitor-reentrant`](../nd500-monitor-reentrant/README.md)). The script stops if
  either is missing.
- **Users `DOMAIN-USER` and `UTILITY` with free pages.** The installer puts the domain on
  DOMAIN-USER and checks for room itself. On `ND5000.IMG` DOMAIN-USER has 2000 pages;
  after this install it used 168.
- **The floppy** `210319H02-XX-01D`, md5 `016307c989e20c2997e68723f0298637`, downloaded from
  the RetroCoreLabs software archive and checked (`~/.cache/nd100x/images/`).

## What the script does

1. Copies the pack and the floppy into a work folder, boots, logs in as SYSTEM.
2. **Starts the ND-5000 as SYSTEM**, with the commands ND puts in `(SYSTEM)LOAD-MODE:MODE`
   (ND-896058-2, section 14): `@ND-500-MONITOR`, `LOAD-CONTROL-STORE,,,,`, `START-SWAPPER`,
   `EXIT`. `ND5000.IMG` has no LOAD-MODE, so nothing does this at boot. Without it the
   first ND-5000 user after the boot loads the microcode. The installer switches to
   DOMAIN-USER before it runs the Linkage-Loader, and DOMAIN-USER may not read
   `(SYSTEM)CONTROL-STORE:DATA`. The monitor then prints `Error when loading Control Store.
   NOT READ ACCESS`, no domain is copied, and the installer still ends
   `INSTALLATION - FINISHED`.
3. `@ENTER-DIRECTORY` for the floppy, runs `(210319H02-XX-01D:FLOPPY-USER)IN-NLL-XX-H02:PROG`
   and answers it:

   | Installer asks | Answer |
   |---|---|
   | `>> Press "CR" to continue` (after each MESSAGE box) | Return |
   | `>>Which module do you want to execute? (default:'1'):` | 1, 2, 3, 4, 5 in turn (the menu says they must run in that order) |
   | `>>On which user do you want to place the domain? (default:'DOMAIN-USER'):` | Return |
   | `>>On which user do you want to place the utility files? (default:'UTILITY'):` | Return |
   | `>>Do you want to delete the previous version of the product? (default:'Y'):` | N |
   | `>>Do you want to delete the current version of the product (default:'Y'):` | N |
   | `Remove floppy 210319H--01D from station 1 unit 0.` then `>> Press "CR" to continue` | eject, Return |
   | `>>Do you want to copy IN-NLL-XX-H02:INST to floppy number 1? (default:'N'):` | N |

   Anything else stops the run. A `NO MORE PAGES`, `Filename syntax error`,
   `NOT READ ACCESS` or `Error when loading` line anywhere in the output fails it.
4. Checks: `LIST-STANDARD-DOMAINS` shows `LINKAGE-LOADER` initialized on
   `(PACK-ONE:DOMAIN-USER)LINKAGE-LOAD-H02:PSEG`, and running it in the monitor prints
   `ND-Linkage-Loader - H.02` and the `Nll:` prompt.
5. Boots the result again, starts the ND-5000 again, repeats the checks, and moves it to the
   result name.

On the chain from `ND5000.IMG` (2026-11-02): PASS, version H.02 after both boots.

## What the installer says to do by hand

It asks for lines in `(SYSTEM)ND500-HENT:MODE` so the standard domain is defined again
after a cold start. `ND5000.IMG` has no such file and the script does not create one; on
nd100x the standard domain was still there after the second boot.
