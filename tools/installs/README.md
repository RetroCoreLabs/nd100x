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

The scripts with an ND-500 product need the ND-5000 running after each boot; the reason is
in [`linkage-loader-h02`](linkage-loader-h02/README.md). `start_nd5000()` asks the monitor's
`VERSION` first; if the swapper and the microprogram are not loaded it runs the commands ND
puts in `(SYSTEM)LOAD-MODE:MODE` (`LOAD-CONTROL-STORE`, `START-SWAPPER`), reads every
`ND-500(0) error:` line, and checks `VERSION` again. A pack's own boot batch may be starting
it at that moment (`ND5000.IMG` does, about 20 s after log-in): then the monitor answers
"reserved for special use" or "Other user(s) already logged on", and the script leaves the
monitor and looks again 5 s later. Code shared by all of them is in `install_common.py`.

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
  log in, `eject()` / `mount()` for floppy changes. No hand-written console code, and never
  the `0xFF` control byte by hand.
- **The installer is answered from a dialogue table** (below), never by a guess. A known
  question in the wrong place, or a failure line, stops the run at once; a question the
  table does not know is never answered, and the state's timeout stops the run with the
  end of the screen printed.
- **The check boots the result a second time**, so a pack that only works until the next
  boot is not handed out as installed.
- **Python 3 standard library only.** Run from WSL / Linux with a built nd100x
  (`build/bin/nd100x`), or point `--nd100x` at another build.

## Dialogue tables

Each script holds its installer's dialogue as a list of states (`dialogue.py`, run by
`run_dialogue()`), written from a real transcript of the installer, question by question
in the order it asks them:

```python
DIALOGUE = [
    State("delete-old", [When(r"delete previous version.*\(Y/N\) ?:", "Y", "user")]),
    State("user",       [When(r"\(default: UTILITY\) ?:", "", "reentrant")]),
    ...
    State("end",        [When(r"<< Installation finished >>\s*@ ?", done=True)]),
]
run_dialogue(vm, DIALOGUE, fail=INSTALL_FAILED, silence=a.silence, total=a.total)
```

- A state waits for a **set** of valid responses. Each `When` has its own reply and its own
  next state (`goto`); a state may go to itself, for a box that comes several times. When
  the installer can answer in more than one way, each way is a `When` with its own `goto`.
- **All** output after the previous match is searched, CR left out, so a server's
  console report printed after a question does not hide it. The question is answered
  once the screen has been quiet for a second.
- A question is matched by its **text**, without `$`: a console report that the
  installer's output cut in two goes on right after the question, on its own line
  (`(default: UTILITY) : tarted`, seen 2026-11-04). Only the bare `@` prompt keeps `$`.
- Of the state's `When`s, the one matching **earliest** on the screen wins; on a tie, the
  first listed.
- A line matching `INSTALL_FAILED` stops the run at once. A question that belongs to
  **another** state, coming before anything this state waits for, stops it as "out of
  order" and is not answered. Everything else is ignored, and kept in the transcript.
- Three timeouts, each naming the state and what it waited for: per state (`State(...,
  timeout=)`, 5 minutes unless a slow step sets more), silence (`--silence`, no output at
  all for 10 minutes: the machine has hung) and the whole dialogue (`--total`, 2 hours).
- Every answer is a line in the transcript:
  `>>> [  17.4 s] [delete-old] answer 'Y' to '...(Y/N): ' -> user`.

`tests/test_install_dialogue.py` (CTest `install_dialogue_unit`) runs the tables against a
stand-in machine that replays the real transcripts.
