# Product installs, scripted

One folder per Norsk Data product. Each one installs the product on a SINTRAN pack
by running **ND's own installer** inside nd100x, from the product's distribution floppy,
answering its questions the way ND's install notes say, and then checks the result.

| Folder | Product | Floppies | Machine |
|---|---|---|---|
| [`backup-system-i05/`](backup-system-i05/README.md) | BACKUP-SYSTEM I05 (ND 210337I05) | 1, from the archive | ND-100 + ND-5000 |

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
