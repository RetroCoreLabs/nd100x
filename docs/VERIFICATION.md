# Verification Recipes

How to verify a change in each subsystem, and the standing safety rules for
doing so. All paths are relative to the repo root.

## Standing rules

1. **Never open the user's original disk images at runtime.** Booting
   SINTRAN writes to the pack (swap + segment data). Copy the image to a
   scratch location first and run against the copy:
   ```bash
   cp SMD0.IMG /tmp/scratch-SMD0.IMG && build/bin/nd100x --boot=smd --smd0=/tmp/scratch-SMD0.IMG
   ```
2. **Confirm which binary you are testing.** There are multiple build
   directories (build/, build_linux/, build_wasm/, ...) and they diverge.
   `<builddir>/bin/nd100x --version` prints version, git hash (+`-dirty`)
   and build time. If the hash does not match the source state you edited,
   the test proves nothing.
3. **Debug sessions use a non-default DAP port** (e.g. `--port=6661`),
   never 4711 - the default port may be held by a live session.
4. **Never kill a process you did not start** to free a port; pick another
   port instead.

## Unit tests

```bash
make test          # builds debug and runs the CTest suite (tests/)
```
Covers the printer/PDF subsystem (test_escp.c, test_pdfwriter.c,
test_printjob.c) plus machine-config and device tests. Note: `*_protos.h`
CMake dependencies are only exercised by clean parallel builds - a passing
incremental build does not prove the CMake dependency graph is right (CI
builds from a clean tree and will catch it, at ~15 min per round).

## CPU / MMS conformance: TPE test programs

The vendor TPE verifiers (INSTRUCTION, PAGING, ...) are the ground truth for
CPU behavior. See docs/TPE-AUTORUN.md for the full harness.

- Automated: `tools/tpe_autorun.py` drives the verifier end-to-end.
- Manual gotcha: sub-tests need `instr` -> `set-para,...` -> `run`. Typing a
  sub-command name directly executes an EMPTY test body and reports nothing.
- Before believing a failure: check binary freshness (`--version`) AND that
  the test floppy image is current. Both have produced phantom bugs.
- Never re-route fault handlers to satisfy a diagnostic's printed
  expected/found values - see docs/ND-DOMAIN-GOTCHAS.md (IIC table section).

## Boot smoke tests

```bash
make boot-smd      # SMD0_IMAGE=... to override
make boot-wd       # WD0_IMAGE=... to override
make boot-floppy   # FLOPPY_IMAGE=... to override
```
The targets check image existence before launching. Run them against scratch
copies (rule 1). A floppy that does not boot is usually a data floppy with no
FLOMON loader, not an emulator bug.

## ND-5000 / several CPUs

For any change in `src/machine/mfbus_bridge.c` or in nd500x `src/ndbus/`.

```bash
build/bin/test_mfbus_bridge          # the bridge's own checks
ctest --test-dir <nd500x build>      # nd500x has 44 tests of its own
```

Then the live test. One user started alone does NOT find multi-CPU faults:
every fault found on 12-OCT-2026 needed users starting at the same time.

1. Scratch copies of `ND5000.IMG`, `SCSI0.IMG`, `FLOPPY.IMG` and an INI with
   `[nd5000.1]` to `[nd5000.4]` enabled; start with `--telnet=<free port>` and
   `--log mms:info`.
2. Console: log in as SYSTEM, `@SET-AVAILABLE`.
3. Four telnet terminals (8 to 11 are free). On each, without waiting for the
   one before to finish loading: log in, `@SET-T-T,,93`, `@ND-500`,
   `CHANGE-CPU n` (n = 1 to 4), `LED-CONV`.
4. Pass: all four show the LED screen. In the ND-500 monitor on the console,
   `WHO` shows cpu 1 to 4 and `PROCESS-STATUS` shows a CPU time that is not
   0.0 s for each.
5. In the log, every `RECV ... 5CPUN=n ... on CPU m` line must have n = m:
   ```bash
   grep -o '5CPUN=[0-9]* 5MSFL=0x[0-9A-F]* on CPU [0-9]*' session.log \
     | awk '{split($1,a,"="); if (a[2] != $5) n++} END {print n+0}'    # must print 0
   ```
6. Repeat the whole run at least three times; the faults were intermittent.

Failure signs seen: a terminal that stays on `ND-5000: LED-CONV`,
"Illegal physical segment", "Fatal internal system error", "ND-5000 lock
timeout", "CPU locked".

## Device tick or CPU loop changes

For any change in `src/devices/devicemanager.c`, a device `Tick`, or the run
loop in `src/cpu/cpu.c`. The complete `--trace` output must not change:

```bash
# once with the binary before the change, once after; the two sums must be equal
build/bin/nd100x --pipe --boot=smd --config <ini> --rtc=ticks --trace \
    --max-instr=100000000 < /dev/null 2>&1 >/dev/null \
    | grep -av 'time used\|usertime\|cycle time' | md5sum
```

Do the same for a winchester boot (60 000 000) and, if a SCSI pack is at hand,
a SCSI boot, on scratch copies. `--rtc=ticks` is what makes the run
repeatable.

NOT a floppy (TPE) boot: its trace is not repeatable even with the same
binary. The TPE monitor reads the panel status register (`TRA PANS`), which is
written in `src/devices/panel/panel.c`. MEASURED 14-OCT-2026: two runs of one
binary first differ at trace line 1 119 424, `A=172250` against `A=172360`
after `TRA PANS`. Check a floppy-only change with the TPE programs instead.

## WASM / glass UI changes

Do not declare a browser-facing fix done on build success alone - verify it
headlessly with puppeteer. Working examples in `tests/browser/`:

- tests/browser/verify-disasm-worker.js - worker mode + OPFS mount + segment disassembly
- tests/browser/test-gateway-browser.js - gateway / SharedArrayBuffer disk I/O
- tests/browser/test-hdd-manager-browser.js - HDD manager window
- tests/browser/test-nd500-worker-browser.js - ND-500 in Worker mode over the gateway
- tests/browser/test-eth-nd100-browser.js - Ethernet II card through the gateway

The scripts find puppeteer in the repo's node_modules from any folder inside
the repo (Node walks up from the script's own directory); they locate
build_wasm_glass/bin and tools/ from their own location, so the working
directory does not matter. New browser tests go in tests/browser/, never in
the repo root. Typical cycle:
```bash
make wasm-glass
node tests/browser/verify-disasm-worker.js
```

## Symbol-table / SINTRAN inspector changes

```bash
node template-glass/js/tests/test_symbol_tables.js
```
Plus a live check on a booted scratch pack - reads taken before SINTRAN
finishes booting return garbage and must not be used as evidence.

## DAP / debugger changes

libdap lives primarily in a separate repo (see CLAUDE.md); change it there,
then bump the submodule. Verify with a real attach on a non-default port:
```bash
build/bin/nd100x --boot=smd --smd0=/tmp/scratch-SMD0.IMG --debugger --port=6661
```
Flow is connect -> attach -> continue. Remember the DAP single-step tax
(~33x): never benchmark timing-sensitive behavior (RTC, throttle) with the
debugger attached.

## Runnable deliverables (Makefile targets, scripts)

Anything whose purpose is to launch or drive the emulator must be executed
once before being reported as working - source-reading is not verification.
Use scratch copies of images so originals are never touched.
