# Product installs, scripted

One folder per Norsk Data product. Each one installs the product on a SINTRAN pack
by running **ND's own installer** inside nd100x, from the product's distribution floppy,
answering its questions the way ND's install notes say, and then checks the result.

| Folder | Product | Floppies | Machine |
|---|---|---|---|
| [`backup-system-i05/`](backup-system-i05/README.md) | BACKUP-SYSTEM I05 (ND 210337I05) | 1, from the archive | ND-100 + ND-5000 |
| [`nd500-monitor-reentrant/`](nd500-monitor-reentrant/README.md) | ND-500-MONITOR as a reentrant subsystem (ND 211305, step 2.1) | none | ND-100 + ND-5000 |
| [`linkage-loader-h02/`](linkage-loader-h02/README.md) | Linkage-Loader H02 (ND 210319H02) | 1, from the archive | ND-100 + ND-5000 |
| [`linker-b01/`](linker-b01/README.md) | LINKER B01 (ND 211224B01) | 1, from the archive | ND-100 + ND-5000 |
| [`backup-manager-b02/`](backup-manager-b02/README.md) | Backup Manager B02 (ND 211226B02) | 6, from the archive | ND-100 + ND-5000 |

## The order

Each one needs the ones above it on the pack, and checks for them before it starts:

1. `backup-system-i05`: the Linkage-Loader and LINKER installers copy with BACKUP-SYSTEM.
2. `nd500-monitor-reentrant`: every installer that defines an ND-500 standard domain
   starts the monitor as `@ND-500-MONITOR`.
3. `linkage-loader-h02`
4. `linker-b01`
5. `backup-manager-b02`: needs BACKUP-SYSTEM, the Linkage-Loader and the LINKER.

```sh
python3 tools/installs/backup-system-i05/install.py      ND5000.IMG /tmp/1.IMG
python3 tools/installs/nd500-monitor-reentrant/install.py /tmp/1.IMG /tmp/2.IMG
python3 tools/installs/linkage-loader-h02/install.py     /tmp/2.IMG /tmp/3.IMG
python3 tools/installs/linker-b01/install.py             /tmp/3.IMG /tmp/4.IMG
python3 tools/installs/backup-manager-b02/install.py     /tmp/4.IMG /tmp/5.IMG
```

The scripts with an ND-500 product start the ND-5000 as SYSTEM after each boot, with the
commands ND puts in `(SYSTEM)LOAD-MODE:MODE`; the reason is in
[`linkage-loader-h02`](linkage-loader-h02/README.md). Code shared by all of them is in
`install_common.py`.

## Rules every install script here follows

- **The source pack is never opened by the emulator.** The script copies it into a work
  folder, installs on the copy, and moves the copy to the result name only when every
  check has passed. A failed run leaves no result file; the work folder keeps the
  transcripts and the half-installed copy for a look.
- **Floppies come from the RetroCoreLabs software archive**
  (`retrocorelabs.github.io/norskdata-software-archive`), named by md5 and checked after the
  download (`fetch_catalog_image()` in `tools/nd100x_expect.py`). A local copy can be given
  instead and is checked against the same md5.
- **Everything goes through `tools/nd100x_expect.py`** (see `docs/pipe-automation.md`): boot,
  log in, `settle()` to wait for a question, `eject()` / `mount()` for floppy changes. No
  hand-written console code, and never the `0xFF` control byte by hand.
- **A question the script does not know stops the run** with the end of the screen
  printed. It is never answered by a guess.
- **The check boots the result a second time**, so a pack that only works until the next
  boot is not handed out as installed.
- **Python 3 standard library only.** Run from WSL / Linux with a built nd100x
  (`build/bin/nd100x`), or point `--nd100x` at another build.
