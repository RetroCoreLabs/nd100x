# Code quality cleanup - remaining work

Status of the house C coding standard cleanup, as of 30-SEP-2026.

The house standard is BARR-C:2018 plus SEI CERT C. The rule engine that
measures it is `tools/house/audit.py`; the validation gate that must pass
after every change is `tools/house/gate.sh`.

## Baseline

The last full gate run passed with:

```
TOTAL       1445  in 159 files
REVIEW     30300  unfilled manual-checklist cells (1 function = 20 cells) - NOT in the total
```

The REVIEW number is a manual checklist, 20 cells per function. It is printed
separately and is deliberately excluded from the total, because it measures
how much has been hand-reviewed, not how many defects exist.

The gate has 15 steps: clean Debug, Release, WASM-glass and WASM-standard
builds with zero warnings, binary freshness, the ctest suite, an SMD boot,
a golden comparison of console output plus image and input hashes, the rule
count ratchet, and a DAP attach check.

## How to run the measurements

```bash
# full gate - this is the only thing that proves a change is safe
tools/house/gate.sh

# rule counts only
python3 tools/house/audit.py

# every finding for one rule, with file and line
python3 tools/house/audit.py --list 7.3
```

Do NOT trust `--no-tidy` for a quick local check. It cannot measure rules
6.2, 7.1, 7.3 or 3.4-3.7 at all, so it reports clean on exactly the rules a
newly added macro is most likely to break.

Do NOT edit `tools/house/gate.sh` while a gate run is in progress. The
running shell re-reads the file and the results become meaningless.

## Batch 1 - mechanical, about 205 findings

No judgement required. Each edit is local and the gate proves it.
Estimated 1-2 sessions.

| Count | What |
|-------|------|
| 37 | `#if 0` and commented-out code blocks - delete or restore |
| 36 | multiple declarations on one line |
| 33 | missing brackets on a single-statement `if` or `while` |
| 12 | `else` following a `return` |
| 14 | naming violations |
| 7 | pointer parameters that should be `const` |
| 6 | missing or malformed file headers |
| 3 | banned library functions |
| 3 | assignments inside an `if` condition |
| 3 | filenames |
| 2 | globals |
| 1 | include guard |
| 1 | static name |

The 51 unowned `TODO` and `FIXME` comments are catalogued separately in
[TODO-INVENTORY.md](TODO-INVENTORY.md) and are for Ronny to judge one at a
time, not for a sweep.

## Batch 2 - needs care, 1 session

128 file-scope symbols that the audit says should be `static`.

Some of these are WebAssembly exports, reached only from JavaScript in
`template-glass/` and `template/`. To the audit they look like functions
nobody calls. Every one must be checked against the `EXPORTED_FUNCTIONS`
list in `src/frontend/nd100wasm/CMakeLists.txt` and against the JavaScript
modules before the keyword goes on. Adding `static` to a live export breaks
the browser build silently at link time, or worse, at run time.

## Batch 3 - decided but not started, 37 findings

**15 `ND100X_*` environment variables.** The project convention in
`CLAUDE.md` is that every machine or config option comes as a CLI flag plus
an INI key, with the CLI winning. So each of the 15 needs both added, plus
the `*Set` bool in the config struct. Never an INI-only or CLI-only option.

**22 platform `#ifdef` blocks.** Two groups:

- A time shim. Mechanical once written.
- The `sleep_ms` browser-blocking guards. These need a ruling from Ronny on
  what the WebAssembly build should do instead of blocking, because a
  blocking sleep in the browser freezes the page. Not a mechanical change.

## Batch 4 - judgement, 476 findings

Real risk of introducing bugs. Each change needs reasoning about the actual
value ranges involved, not a type substitution. Estimated 4-6 sessions.

| Count | What |
|-------|------|
| 194 | signed and unsigned mismatches |
| 183 | `short` and `long` that should be exact-width types |
| 64 | oversized functions |
| 19 | `volatile` |
| 16 | recursion |

The 183 width-type findings are the ones to be most careful with. The
emulator's guest values are 16-bit by definition and a great deal of the
arithmetic relies on defined wraparound.

## Skipped on purpose - 582 findings

**420 remaining ignored return values.** These are `printf` calls that write
display text, and `fclose` on streams opened read-only. The return value
genuinely does not matter. The ones that did matter have already been fixed,
including every disk-image sector write in `src/machine/machine.c`, where a
short write used to be silent.

**162 `../` includes.** Recorded as blocked on the ND-500 library work.
**This needs re-checking.** That work has moved since the number was taken:
`../nd500x/CMakeLists.txt` now defines an `nd500_headers` INTERFACE target,
and `src/frontend/nd100wasm/CMakeLists.txt` was rewritten to use
`nd500::headers nd500::machine nd500::cpu nd500::ndlib`. Whether the 162 can
now be fixed is unverified - nobody has read the current nd500x targets to
find out. This is the cheapest next task and it either unblocks the largest
single skip group or closes the question.

## Blocked - 5 findings

Five include-order findings sit behind `src/devices/devices_types.h:663`,
which uses `SCSIUnitType` from `src/devices/scsi/device_scsi.h:60`.

This is not a live bug. It is not a simple reorder either, because
`devices_protos.h` is generated by mkptypes and references the type too.
Auto-generated `*_protos.h` files must never be edited or created by hand -
generation is timestamp-gated, so a hand-made file makes CMake skip
regeneration and the build silently uses a stale header.

## Open questions for Ronny

1. **A Windows build.** The crash class fixed in `1deddb4` - `fopen` on a
   path that does not exist on Windows, its NULL return unchecked - is
   invisible to every step of the gate, because the gate only builds for
   Linux and WebAssembly. Nothing catches the next one either.
2. **A DISC-TEMA session transcript**, following
   [DISC-TEMA-TESTING.md](DISC-TEMA-TESTING.md). Only against a scratch copy
   of the image - DISC-TEMA formats and writes.
3. **Policy on rules 5.5 and 5.2.**
4. **Whether the SMD boot gate step should hold its own pristine pack copy.**
   Until it does, G7 keeps reporting a golden input mismatch after any manual
   boot, because booting SINTRAN mutates the pack. That is expected
   behaviour, not a fault, but it makes the gate step noisy.
5. **The SMD0.IMG size gap.** The image is 78,643,200 bytes, which is 76,800
   sectors of 1024 bytes. The DISC-75MB-1 geometry in
   `src/devices/disk_smd.h:53-58` gives 823 x 5 x 18 = 74,070 sectors. The
   2,730 sector difference is not explained yet.

## What this exercise actually produced

Worth recording, because it argues for how to spend the next session. Two
real defects were found and fixed during the cleanup:

- `src/cpu/cpu_disasm.c` opened `/dev/stdout`, which does not exist on
  Windows. `fopen` returned NULL, all 17 `fprintf` calls were handed that
  NULL, and the closing `fclose(NULL)` finished it. `--disasm` had never
  worked outside Linux. Fixed in `1deddb4` by writing to `stdout` directly.
- `src/machine/machine.c` wrote disk-image sectors without checking the
  `fwrite` return value. A short write - a full disk, a failing drive - was
  silent, and the image was quietly corrupted.

Neither was found by chasing a rule count. Both came from reading the code
around a finding. Separately, four rules turned out to be measuring the
wrong thing, and correcting the measurement removed more findings than the
code fixes did: 301 console prints counted as unchecked returns, 21
conditional includes counted as misordered, and the REVIEW checklist
multiplying itself into the headline total.
