# BACKUP-SYSTEM I05 (ND 210337I05)

Installs ND's BACKUP-SYSTEM, version I05 of 1988-11-07, on a SINTRAN pack with ND's own
installer `INST-BASY-I05:PROG`. I05 is the version the ND-500/5000 Backup Manager
(ND 211226) asks for.

```sh
# from the repository root, in WSL / Linux
python3 tools/installs/backup-system-i05/install.py ND5000.IMG /tmp/ND5000-I05.IMG
```

`ND5000.IMG` is only copied. The installed pack appears as `/tmp/ND5000-I05.IMG` when, and
only when, every check below has passed. The run takes about two minutes.

## What you need

- **A built nd100x**, `build/bin/nd100x` (or `--nd100x PATH`).
- **Python 3.** Standard library only.
- **A SINTRAN pack** that boots with ND-500/5000 support, where `SYSTEM` logs in with an empty
  password and the user `UTILITY` exists. The installer checks for room on SYSTEM and
  UTILITY before it starts. Tested on nd100x's `ND5000.IMG` (SINTRAN III VSX/500 L), which
  had I02 installed before.
- **The floppy**, `210337I05-XX-01D`, md5 `395982991324b9425ecceaf71552f266`. The script
  downloads it from the RetroCoreLabs software archive into `~/.cache/nd100x/images/` and
  checks the md5. Offline, give a local copy with `--floppy PATH`; it is checked the same way.
  The archive also holds a second image under the same directory name with a different md5
  (640 pages instead of 616), which is not the one this script was tested with.

## What the script does

1. Copies the pack and the floppy into a work folder (`--work DIR`, default a new temp
   folder) and writes the machine INI there. That INI is `config/ND5000.ini` (an ND-100 with
   MFbus memory, the octobus and one ND-5000 at station 070B) without the telnet server.
2. Boots, logs in as SYSTEM, `@ENTER-DIRECTORY` for `FLOPPY-DISC-1` unit 0, and runs
   `@(210337I05-XX-01D:FLOPPY-USER)INST-BASY-I05:PROG`.
3. Answers the installer. These are the questions as it asked them on 2026-11-02:

   | Installer asks | Answer |
   |---|---|
   | `Do you want to delete previous version of the BACKUP-SYSTEM if it exist ? User SYSTEM, UTILITY and BPUN will be checked (Y/N):` | `Y` |
   | `Please specify which user you want the files BACKUP-SYS-I:PROG and BACKUP-SERV-I:PROG copied to (default: UTILITY) :` | Return (UTILITY) |
   | `Should the BACKUP-SYSTEM be dumped reentrant (Y/N) ?` | `Y` |
   | `Remove floppy 210337I05-XX-01D from station 1 unit 0 and type "CR"` | eject the floppy, then Return |
   | `Press "RETURN" to continue.` (after the note on termination handling) | Return |

   Anything else stops the run and prints the end of the screen.
4. Checks: `@LIST-REENTRANT` must show `BACKUP-SYSTEM-I`, and `@BACKUP-SYSTEM` must print
   `BACKUP-SYSTEM ver I05 ...` and give the `Ba-sy:` prompt.
5. Boots the result **again** and repeats both checks, then moves it to the result name.

The work folder keeps `install-console.log` and `reboot-console.log` (the screen, plus one
`>>>` line per step) and the emulator logs.

## What it changes on the pack

Measured by comparing every file on `ND5000.IMG` before and after:

| File | Change |
|---|---|
| `(UTILITY)BACKUP-SYS-I02:PROG`, `(UTILITY)BACKUP-SERV-I02:PROG` | deleted (the earlier version) |
| `(UTILITY)BACKUP-SYS-I05:PROG`, `(UTILITY)BACKUP-SERV-I05:PROG` | added |
| `(SYSTEM)BACKUP-LOAD-I:MODE` | rewritten. Loads the DMA server onto segment `DMASEG`; I05 adds "patch 2", which lets the server put its buffers in low memory |
| `(SYSTEM)BACKUP-LOAD-I:OUT` | output of that job. Check it for errors; they do not show on the terminal |
| `(SYSTEM)SEGFIL0:DATA` | the reentrant copy `BACKUP-SYSTEM-I` (segment 130B) and `DMASEG` (segment 131) |

The installer also **resets termination handling to blanks**, and says so on the screen.
To get the old setting back: `@DEFINE-TERMINATION-HANDLING`, `B`, `NO TERMINATION DEFINED!`.

## What the installer says to do by hand, and what was seen

At the end the installer asks for two lines to be added so the install survives a cold start:

```
append to the DUMP-REENTRANT file on user SYSTEM:
    @DUMP-PROGRAM-REENTRANT BACKUP-SYSTEM-I, (UTILITY)BACKUP-SYS-I05:PROG
append (SYSTEM)BACKUP-LOAD-I:MODE to the HENT-MODE file on user SYSTEM
```

`ND5000.IMG` has neither a DUMP-REENTRANT file nor a HENT-MODE file, and the script does
not create them. On nd100x the reentrant entry and the `@BACKUP-SYSTEM` banner were still
there after the second boot (step 5). Whether an nd100x boot is what SINTRAN calls a
cold start has not been established, so this is a measurement of nd100x, not proof
that the two lines are never needed.

## Sources

- The installer's own screen output (the table above), from runs on 2026-11-02.
- NDInsight `Installation/INSTALL-ND-LINKAGE-LOADER-AND-BACKUP-SYSTEM.md`: the I04
  install on RetroCore, with the same questions and the same two manual lines.
- NDInsight `SINTRAN/Devices/SCSI/SCSI-STORAGE-UNDER-SINTRAN.md` section 10.3: why I05 is
  needed (the Backup Manager).
