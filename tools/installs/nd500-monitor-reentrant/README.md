# ND-500-MONITOR as a reentrant subsystem (ND 211305, step 2.1)

Makes the ND-500/5000 monitor startable as `@ND-500-MONITOR`. It is the step in ND's
install description for the ND-500/5000 System Package (ND-895030-1A for version L,
ND-895030-2 for version M), section "ND-500/5000 Monitor":

```
@DUMP-PROGRAM-REENTRANT
NAME: ND-500-MONITOR
FILE NAME: (<user-name>)ND-500-MON-J<rev>:PROG,N5MON
```

```sh
# from the repository root, in WSL / Linux
python3 tools/installs/nd500-monitor-reentrant/install.py ND5000-I05.IMG /tmp/ND5000-N5MON.IMG
```

The source pack is only copied. The result appears only when every check below has passed.
No floppy is needed: the monitor program is already on the pack.

## Why it is needed

Every ND product installer that defines an ND-500 standard domain (the Linkage-Loader,
the LINKER, the Backup Manager) starts the monitor as `@ND-500-MONITOR`. On a pack where
it is not reentrant, SINTRAN answers `NO SUCH FILE NAME`, the installer's ND-500 commands
go to SINTRAN instead, and the installer still prints `INSTALLATION - FINISHED` with no
domain copied. nd100x's `ND5000.IMG` had the monitor only as the file
`(SYSTEM)ND-500-MON-J:PROG`.

## What you need

- **A built nd100x**, `build/bin/nd100x` (or `--nd100x PATH`). **Python 3**, standard
  library only.
- **A SINTRAN pack with the monitor program on it.** The default is
  `(SYSTEM)ND-500-MON-J:PROG`, as on `ND5000.IMG`; give another with `--monitor-file`.
  The script stops if the file is not there, and also if `ND-500-MONITOR` is already in
  `@LIST-REENTRANT` (nothing to do).

## What the script does

1. Copies the pack into a work folder, boots it with no floppy, logs in as SYSTEM.
2. Types the three lines above, with `(SYSTEM)ND-500-MON-J:PROG,N5MON`.
3. Checks: `@LIST-REENTRANT` shows `ND-500-MONITOR`, and `@ND-500-MONITOR` prints
   `ND-500/5000 MONITOR Version ...` and the `ND-5000:` prompt.
4. Boots the result again, repeats the checks, and moves it to the result name.

On `ND5000.IMG` after BACKUP-SYSTEM I05 (2026-11-02): PASS, monitor version J04, still
reentrant after the second boot.
