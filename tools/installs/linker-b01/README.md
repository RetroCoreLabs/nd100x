# ND LINKER B01 (ND 211224B01)

Installs ND's LINKER for the ND-500/5000 as the standard domain `LINKER`, with ND's own
installer `IN-LINK-XX-B01:PROG`. The Backup Manager installer links BM-Operator with it.

```sh
# from the repository root, in WSL / Linux
python3 tools/installs/linker-b01/install.py ND5000-NLL.IMG /tmp/ND5000-LINKER.IMG
```

The source pack is only copied. The result appears only when every check below has passed.

## What you need

- **A built nd100x** (`build/bin/nd100x` or `--nd100x PATH`) and **Python 3**.
- **On the pack, in `@LIST-REENTRANT`:** `BACKUP-SYSTEM` and `ND-500-MONITOR` (see
  [the order](../README.md)). The script stops if either is missing.
- **Users `DOMAIN-USER` and `UTILITY` with free pages** (the installer checks for room).
- **The floppy** `211224B01-XX-01D`, md5 `1097cc3b230ef5b97582962d842e361d`, downloaded from
  the RetroCoreLabs software archive and checked (`~/.cache/nd100x/images/`).

## What the script does

1. Copies the pack and the floppy into a work folder, boots, logs in as SYSTEM.
2. Starts the ND-5000 as SYSTEM with ND's LOAD-MODE commands, for the reason given in
   [`linkage-loader-h02`](../linkage-loader-h02/README.md).
3. `@ENTER-DIRECTORY` for the floppy, runs `(211224B01-XX-01D:FLOPPY-USER)IN-LINK-XX-B01:PROG`
   and answers it with the same table as the Linkage-Loader (the same ND installer
   framework). Two wordings differ, and both are in the table in `install_common.py`:
   the delete questions have no default and end in ` : `.
4. Checks: `LIST-STANDARD-DOMAINS` shows `LINKER` initialized on
   `(PACK-ONE:DOMAIN-USER)LINKER-B01:DOM`, and `@LINKER` prints `ND LINKER, Version B01`
   and its prompt, then `EXIT`.
   - The console has no terminal type set, so the LINKER first prints
     `Terminal type 000 is unknown.`, a list, and `What is your terminal type?`. The check
     answers `2` (Teletype ASR 33): the script's console is plain text with no screen
     control.
   - The LINKER then runs `SET-ADVANCED-MODE` by itself, and the prompt is `NDL(ADV):`
     instead of `NDL:`.
5. Boots the result again, repeats the checks, and moves it to the result name.

On the chain from `ND5000.IMG` (2026-11-02): PASS, version B01 after both boots.

## What the installer says to do by hand

It says to define LINKER as a standard domain in the HENT-MODE file, and to check that
`UE-ERMSG-EN-C:ERR` and `DDBTABLES-G:VTM` are on the pack. `ND5000.IMG` has no HENT-MODE
and the script does not create one; the standard domain was still there after the
second boot. The two files have not been checked.
