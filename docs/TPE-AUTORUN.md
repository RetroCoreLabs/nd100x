# Auto-running the ND TPE diagnostic test programs

This describes how to drive the **TPE Monitor** diagnostic floppy (the "ND-100 and
instruction verifier" test suite) unattended under the `nd100x` emulator, and provides
a ready-to-run auto-runner:

- Driver script: `tools/tpe_autorun.py` (paths below are relative to the repo root)
- Test floppy:   `FLOPPY.IMG` in the repo root
  (equivalent copy: `images/Nd-210523I01-XX-01D.img`)

Everything below was **verified** against `nd100x` at commit `763a4f4` on 23-JUL-2026.
Anything not verified is labelled as such.

---

## 1. How the TPE floppy is structured

Boot the floppy and you land in the TPE Monitor:

```bash
build/bin/nd100x --boot=floppy --image=FLOPPY.IMG
...
    TPE Monitor, ND-100 series - Version: B01 - 1988-10-07
TPE>
```

The monitor itself has only a handful of commands. The useful ones:

| Monitor command | Purpose |
|-----------------|---------|
| `LIST-FILE`     | List the test files on the floppy (default arg `All-file-names`) |
| `LOAD-FILE`     | Load a file without executing |
| `PLACE-FILE`    | Place a file |
| `HELP`          | Interactive; prompts `Command:` for a name to explain |
| *`<program-name>`* | **Typing a test file's name loads AND enters that test program** |

`LIST-FILE` on the shipped floppy shows **34 files** (File 0 is the monitor BPUN itself):

```
File  0 : TPE-MON-100-B01:BPUN     <- the monitor, not a test
File  1 : CACHE-1X0-A00:TEST
File  2 : CACHE-100-A00:NEXT
File  3 : CACHE-110-A00:NEXT
File  4 : CACHE-120-A00:NEXT
File  5 : COLOUR-TERM-A00:TEST
File  6 : CONFIGURATIO-D05:TEST
File  7 : DISC-TEMA-J02:TEST
File  8 : FLOPPY-STREA-C03:TEST
File  9 : GRAPHIC-TERM-B00:TEST
File 10 : GRAPHIC-TERM-B00:FONT
File 11 : HDLC-MEGALIN-D00:TEST
File 12 : INSTRUCTION-C03:TEST
File 13 : LP-TEST-E01:TEST
File 14 : MAGTAPE-B00:TEST
File 15 : MEMORY-D04:TEST
File 16 : NET-ONE-A00:TEST
File 17 : OCTOBUS-B00:TEST
File 18 : PAGING-C02:TEST
File 19 : PIOC-ETHER-B01:TEST
File 20 : PIOC-ETHER-B01:NEXT
File 21 : POWER-FAIL-A02:TEST
File 22 : PRINTERS-B00:TEST
File 23..30 : PRINTERS-00x-B00:NEXT
File 31 : SYNC-MODEM-B00:TEST
File 32 : TERMINAL-ASY-F01:TEST
File 33 : UNIVERS-DMA-C02:TEST
```

## 2. The generic drive pattern

Every test is driven the same three-step way at the `TPE>` prompt:

```
<program-name>            % loads the program, prints its identity banner, returns to TPE>
[ set-para,<args...> ]    % OPTIONAL: SET-PARAMETERS selects which subtests / loop / abort mode
run                       % executes the program's tests
```

Key facts (verified):

- **Name matching accepts abbreviations** — `instr` loads `INSTRUCTION-C03`, `int`
  selects its `INTERNAL-INTERRUPTS` subcommand. Ambiguous/no-match prints
  `NO SUCH FILE NAME` or `*** No such command ***`.
- **`run` is the common convention** to execute a loaded program's tests.
  `INSTRUCTION-C03` and `MEMORY-D04` both begin testing on `run`.
- The instruction verifier needs a **`SET-PARAMETERS` line first** to select the full
  suite. The working line captured from a real session is:
  ```
  instr
  set-para,N,N,Y,N,Y
  run
  ```
  which runs all instruction categories + internal interrupts across PIL levels 1..9
  and finishes with `=== End of run ===`.

> **PITFALL (cost a long investigation):** typing a subcommand name directly at `TPE>`
> (e.g. `INTERNAL-INTERRUPTS`) runs a **bare/empty variant** — it prints the test header
> and `=== End of test ===` with no roster and never actually provokes anything. Always
> go through `set-para` (where required) + `run`. "Body is empty, can't reproduce" under
> automation almost always means the wrong invocation, not an emulator bug.

## 3. Which tests can run fully unattended

Only the **CPU / memory / MMS-class** tests run to completion with no real peripherals:

| Program | Class | Unattended? |
|---------|-------|-------------|
| `INSTRUCTION-C03` | CPU instruction verifier | **Yes** (`set-para,N,N,Y,N,Y` + `run`) |
| `MEMORY-D04`      | Main memory | **Yes** (`run`) |
| `PAGING-C02`      | MMS / paging | **Yes** (`run`, then answer `Test number(s) (1 to 11 dec):` with `1-11`) |
| `CONFIGURATIO-D05`| Config report | Yes, but informational — prints the HW config table and has no pass/fail marker, so the runner reports it `INCONCLUSIVE` (expected, not a failure) |
| `CACHE-*`         | Cache | **No** — refuses: `*** The CACHE MANUAL DISABLE switch is ON ***` |
| `*-TERM`, `TERMINAL-ASY`, `LP-TEST`, `PRINTERS-*` | Terminal/printer devices | Need the emulated device + interactive params |
| `DISC-TEMA`, `FLOPPY-STREA`, `MAGTAPE`, `UNIVERS-DMA` | Block/DMA devices | Need the device; often destructive |
| `NET-ONE`, `PIOC-ETHER`, `SYNC-MODEM`, `HDLC-MEGALIN`, `OCTOBUS` | Comms devices | Need the device |
| `POWER-FAIL` | Power-fail interrupt | Special |

The device tests are not "broken" under automation — they legitimately require hardware
that the headless emulator does not present, and most prompt for device numbers / speeds.

## 4. The auto-runner

`tools/tpe_autorun.py` boots the floppy **once per test** (so a
looping/hung test can't poison the next), types `name` → optional `set-para` → `run`,
captures the console for a time-box, scans for markers, and prints a PASS/FAIL/SKIP summary.

Run it:

```bash
# default set: INSTRUCTION-C03, MEMORY-D04, PAGING-C02, CONFIGURATIO-D05
python3 tools/tpe_autorun.py \
    --image FLOPPY.IMG \
    --cputype ND110CX \
    --outdir /tmp/tpe_logs

# only specific programs
python3 tools/tpe_autorun.py --only INSTRUCTION-C03,MEMORY-D04
```

Per-test console logs are written to `--outdir` (`<PROGRAM>.log`). Exit code is non-zero
if any test is classified `FAIL`.

Classification markers (edit `RECIPES` / `FAIL` / `REFUSE` in the script to extend):

- **FAIL**: `*** ERROR`, `No interrupt generated`, `FATAL`, `FAILED`, `MISMATCH`
- **SKIP**: `MANUAL DISABLE switch is ON`, `No such command`, `NO SUCH FILE NAME`
- **PASS**: reached `End of run` / `End of test`

### Adding a program to the runner

Each entry in `RECIPES` is
`name -> (setparams_or_None, run_cmd, timebox_seconds, post_replies)`:

- `setparams`   — a full command line typed at `TPE>` before `run` (or `None`)
- `run_cmd`     — the command that starts the tests (usually `run`)
- `timebox`     — seconds to let the tests run before moving on
- `post_replies`— list of answers sent, in order, to parameter prompts that end in
  `:` **after** `run` (e.g. PAGING's `Test number(s) (1 to 11 dec):` -> `["1-11"]`)

To automate another test, discover its command interface interactively first
(load it, then answer its `HELP`/`Command:` prompt), capture the exact `SET-PARAMETERS`
line and any post-`run` prompts, and add a row. Do **not** guess parameter strings —
they differ per program.

## 5. CPU identity note

The verifier banners are CPU-identity-aware. Under the current default `--cputype=ND110CX`
the banner reads `ND-110/CX`; under `ND100`/`ND100CE`/`ND100CX` it reads
`ND-100/CX upgraded for 16 PITs`. Pass `--cputype=...` (or `ND100X_CPUTYPE=...`) to the
runner to sweep identities. See `src/cpu/cpu.c` (`g_current_cpu_type`,
`cpu_set_type_from_env`).

## 6. Manual driving (no script)

For expect-style piping without the runner, `--pipe` makes the emulator read the keyboard
from stdin and unbuffer stdout:

```bash
printf '\rinstr\rset-para,N,N,Y,N,Y\rrun\r' | \
    build/bin/nd100x --boot=floppy --image=FLOPPY.IMG --pipe --cputype ND100CX
```

(Timing matters — the emulator must reach `TPE>` before each line is consumed; the Python
runner waits for the prompt instead of relying on `printf` timing, which is why it is more
reliable.)

## 6. DISC-TEMA: what is known so far (21-SEP-2026)

Probed while starting the SMD test harness Ronny asked for. Recorded here so
the next attempt does not repeat it.

- `DISC-TEMA-J02` **loads and starts** under automation with an SMD attached:

  ```
  nd100x --boot=floppy --image=<scratch tpe.img> --smd0=<scratch smd.img> --pipe
  TPE>disc        ->  "DISC-TEMA - VERSION: J02 - 1990-04-02", back at TPE>
  ```

- It does **not** use `run`. That answers `*** No such command ***`. Unlike the
  CPU/memory tests, this program has its own command set, so the RECIPES table
  in `tools/house/../tpe_autorun.py` - which assumes
  `name` -> optional `set-para` -> `run` - cannot express it as it stands.
- `help` answers with a `Command:` prompt and the word `All-commands`. Sending
  a bare carriage return there produces nothing further, so the command list
  needs some other interaction (a word to type, or a different key). That is
  where the next attempt should start.

**Always give it a scratch copy of the disk.** These device tests write to the
pack; section 3 marks them "often destructive". Never point `--smd0` at the
real `SMD0.IMG`.
