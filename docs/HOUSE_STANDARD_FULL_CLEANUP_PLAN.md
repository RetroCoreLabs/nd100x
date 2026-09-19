# House C standard: full cleanup plan for nd100x

Status: PLAN, not started. Written 19-SEP-2026.
Standard: the house C standard skill `c-coding-standard`, `SKILL.md` (rules 1.1 - 12.3).
All paths below are relative to the repo root.

## 0. What "done" means

Every rule of the standard holds for every line of project C code. Not "when
touched", not "estimated", not "mostly". This plan overrides section 0 of the
standard ("existing code: only the lines being changed") for nd100x.

Done = all four are true on the final commit:

1. `tools/house/audit.py` reports **0** for every rule in section 3 below, on
   every project file.
2. Every manual-review rule (section 3, column "How checked" = REVIEW) has a
   signed-off row per function in `docs/house-audit/review/<module>.md`.
3. `docs/house-audit/exemptions.md` lists every remaining deviation, each
   one with rule, file:line, reason and "approved by Ronny <date>". Nothing
   else deviates. An exemption without Ronny's approval is a failure.
4. The gate (section 2) passes.

## 1. Scope

In scope: every tracked `*.c` / `*.h` under `src/`, `tests/`, `tools/reth-tap/`
(about 170 files, 65,279 lines at `ecd19ab`).

Out of scope, never edited (vendored or generated):
`external/`, `template-glass/external/`, `tools/mkptypes/`,
`src/devices/scsi/ncr5386.c`, `src/devices/scsi/ncr5386.h`, every
`*_protos.h`, the build-dir `nd100x_version.h`, and `../nd500x`.

## 2. The gate: run after EVERY step, before EVERY commit

`tools/house/gate.sh` (built in phase 0) runs all of this and stops on the
first failure. A step is not committed until the gate prints PASS.

| # | Check | Pass condition |
|---|---|---|
| G1 | Native Debug build (`build/`) | 0 warnings from project files |
| G2 | Native Release build (`build_release/`) | 0 warnings from project files (Release finds maybe-uninitialized / clobbered that Debug misses) |
| G3 | WASM glass build (`build_wasm_glass/`) | 0 warnings from project files |
| G4 | Binary freshness | `build/bin/nd100x --version` hash == `git rev-parse --short HEAD` of the step, no `-dirty` (gate builds after a temporary commit, or checks the working-tree hash) |
| G5 | Unit tests | `ctest` in `build/`: all pass, same count as baseline or more |
| G6 | SMD boot | Fresh scratch copy of `SMD0.IMG`; `--boot=smd --pipe --max-instr=300000000`; exit 0; console contains `SINTRAN III RUNNING`; console contains no `WARNING` and no `ERROR`; stderr has no `[ERROR]` and no `[WARN]` |
| G7 | SMD boot golden | Console output byte-identical to the golden file captured in phase 0 (the ticks RTC makes the run deterministic - phase 0 proves that first) |
| G8 | Rule counts | `tools/house/audit.py` count for the step's rule(s) in the step's scope is 0, and NO other rule count went up vs the previous commit |
| G9 | Behaviour-neutral proof (formatting / rename / comment steps only) | Release objects built with `-g0`: disassembly of every project `.o` identical to the previous commit, symbol names excepted for rename steps (names mapped through the rename table) |

Extra gates for the modules they guard, run after any step that touches them:

| # | Check | When |
|---|---|---|
| G10 | TPE `INSTRUCTION-C03` and `PAGING-C02` via `tools/tpe_autorun.py` on a scratch `FLOPPY.IMG` | any step touching `src/cpu/` or `src/machine/` |
| G11 | Winchester boot (scratch `WD0.IMG`) to `SINTRAN III RUNNING` | any step touching `src/devices/` |
| G12 | Floppy boot to `TPE Monitor` | any step touching `src/devices/floppy/` |
| G13 | Puppeteer: `verify-disasm-worker.js`, `test-gateway-browser.js`, `test-hdd-manager-browser.js` (the Winchester-tab check fixed first, see phase 0) | any step touching `src/frontend/nd100wasm/` or exported `Dbg_*` |
| G14 | DAP attach + pause confirmed stopped, port 6661 | any step touching `src/debugger/` |

Rules for running the gate: scratch copies only, never the repo images;
never kill a process not started by the gate; DAP never on 4711.

## 3. Every rule, and how it is checked

MECH = `audit.py` counts it by text/AST scan. TIDY = a clang-tidy check,
counted by `audit.py`. CC = compiler flag, counted by the build. REVIEW =
needs a person to read the code; tracked per function in the review file.

| Rule | What | How checked |
|---|---|---|
| 1.1 gnu11 pinned | per target | MECH (CMake scan: every project target calls `nd100x_apply_house_standard`) |
| 1.2 no threads.h / Annex K | | MECH |
| 1.3 zero Tier A warnings | | CC (G1-G3) |
| 1.4 no `#pragma once` | | MECH |
| 1.5 ASCII, LF, no tabs, no trailing ws, final newline | | MECH |
| 2.1-2.6 formatting | indent, Allman, braces always, 100 cols, one stmt/decl per line, spacing, pointer star | MECH: `clang-format --dry-run` over each file = 0 diffs; plus `readability-braces-around-statements` (TIDY), `readability-isolate-declaration` (TIDY) |
| 2.7 switch: default + fall-through comment | | CC (`-Wswitch-default`, `-Wimplicit-fallthrough`) |
| 2.8 tables in clang-format off/on | | REVIEW |
| 3.1 `Module_PascalVerb` non-static functions | | MECH (symbol list from `nm` of project objects vs the module prefix table) |
| 3.2 static functions snake_case | | MECH |
| 3.3 globals `g_`, extern only in owning header | | MECH |
| 3.4 statics `s_` / snake, locals snake | | TIDY (`readability-identifier-naming`) |
| 3.5 no Hungarian prefixes | | TIDY |
| 3.6 macros / enum constants UPPER_SNAKE | | TIDY |
| 3.7 types PascalCase, no `_t` | | TIDY |
| 3.8 file names snake_case, one header per .c | | MECH |
| 3.9 guards | | MECH |
| 3.10 boolean names read as a question | | REVIEW |
| 4.1 banner (purpose, SPDX, copyright, licence) | | MECH |
| 4.2 self-contained headers | | MECH (compile each header alone) |
| 4.3 no storage / non-inline definitions in headers | | MECH |
| 4.4 include order | | MECH (`llvm-include-order` style check in audit.py) |
| 4.5 includes through module root | | MECH (no `"../` in `#include`) |
| 4.6 non-API symbols static | | MECH (every non-static symbol must be referenced from another object: `nm` cross-reference) |
| 5.1/5.2 types | | MECH (no `short`/`long`/`unsigned char` as data types) + REVIEW (width chosen correctly) |
| 5.3 no bitwise on signed | | TIDY (`hicpp-signed-bitwise`) |
| 5.4 no signed overflow | | CC (UBSan in G5/G6 run on the sanitize build) + REVIEW |
| 5.5 no mixed signed/unsigned | | CC (`-Wsign-compare -Wsign-conversion`) |
| 5.6 char widening | | TIDY (`bugprone-signed-char-misuse`) |
| 5.7 no punning | | TIDY (`bugprone-casting-through-void`, cast-align `-Wcast-align=strict`) + REVIEW |
| 5.8 layout structs static-asserted | | REVIEW |
| 5.9 const | | TIDY (`misc-const-correctness`, `readability-non-const-parameter`) |
| 5.10 volatile only where allowed | | MECH (list every `volatile`) + REVIEW |
| 6.1 no assignment in condition / argument | | TIDY (`bugprone-assignment-in-if-condition`) + CC (`-Wparentheses`) |
| 6.2 parenthesise mixed operators | | TIDY (`readability-math-missing-parentheses`) |
| 6.3 no side effects in boolean expressions | | REVIEW |
| 6.4 goto only forward to cleanup | | MECH (every `goto` listed, label must be later in the function) |
| 6.5 no VLA / alloca | | CC (`-Wvla`) + MECH |
| 6.6 no recursion in hot paths | | TIDY (`misc-no-recursion`) |
| 6.7 no `#if 0`, no commented-out code | | MECH (heuristic: comment lines that parse as C) + REVIEW of hits |
| 6.8 macro hygiene | | TIDY (`bugprone-macro-parentheses`, `bugprone-macro-repeated-side-effects`) + REVIEW |
| 6.9 `(void)` | | CC (`-Wstrict-prototypes`) |
| 6.10 prototypes | | CC (`-Wmissing-prototypes`) |
| 7.1 <= 100 lines, <= 6 params | | TIDY (`readability-function-size`: LineThreshold 100, ParameterThreshold 6) |
| 7.2 return conventions | | REVIEW |
| 7.3 return values checked | | TIDY (`bugprone-unused-return-value`, `cert-err33-c`) |
| 7.4 no exit/abort in library code | | MECH |
| 7.5 parameter validation in non-static functions | | REVIEW |
| 7.6 early return | | TIDY (`readability-else-after-return`) + REVIEW |
| 8.1 allocation results checked | | MECH (every alloc call site listed) + REVIEW |
| 8.2 one owner, no alloc in per-tick path | | REVIEW |
| 8.3 realloc pattern | | MECH |
| 8.4 banned functions | | MECH |
| 8.5 snprintf size from a real array | | TIDY (`bugprone-sizeof-expression`) + REVIEW |
| 8.6 literal format strings | | CC (`-Wformat=2`, `-Wformat-nonliteral`) |
| 8.7 memset/memcpy size from the object | | TIDY (`bugprone-sizeof-expression`) |
| 9.1-9.7 logging | | MECH (no `printf`/`fprintf`/`puts`/`perror` in library code; no `getenv` switches; no `#ifdef DEBUG*`) + REVIEW |
| 10.1 shared data locked | | REVIEW (per thread: telnet, DAP, modem, gateway) + TSan run of G6 |
| 10.2 signal handlers | | REVIEW |
| 10.3 setjmp confined, volatile locals | | MECH (files with setjmp vs the profile list) + CC (`-Wclobbered`) |
| 11.1 ASCII comments | | MECH (1.5) |
| 11.2 Doxygen on every non-static function | | MECH (`-Wdocumentation` with clang + audit.py: every declaration in a header has `@brief`, one `@param` per parameter, `@return` unless void) |
| 11.3 comments say why | | REVIEW |
| 11.4 TODO(name), no FIXME | | MECH |
| 12.1 one platform macro | | MECH |
| 12.2 platform code isolated | | MECH (no platform `#if` in `src/cpu/`, `src/devices/`) |
| 12.3 no endianness assumption | | TIDY (cast-align) + REVIEW |

## 4. Phases

Order is chosen so that the mechanical, provably behaviour-neutral work
comes first and gives clean diffs for the semantic work after it. One commit
per step; per module where a step is large. Module order in every phase:
`src/ndlib`, `src/machine`, `src/devices` (one commit per device
directory), `src/debugger`, `src/frontend`, `tests`, `tools/reth-tap`,
`src/cpu` last (largest, guarded by TPE).

### Phase 0 - tooling and baseline (no source change)
0.1 `tools/house/audit.py`: every MECH and TIDY check in section 3, per
    file, per rule; `--json` for the gate; `--baseline` diff mode.
0.2 `.clang-tidy`: the checks named in section 3 with the thresholds.
0.3 `tools/house/gate.sh`: G1-G14 as in section 2.
0.4 Determinism proof for G7: two SMD boots of the same build on two fresh
    scratch copies give byte-identical console output. If not identical,
    G7 is replaced by a documented normalisation - decided with Ronny.
0.5 Golden files: console output of G6 at the starting commit.
0.6 Baseline: full `audit.py` run committed as
    `docs/house-audit/baseline.json`; the per-rule totals go into this plan.
0.7 Fix the out-of-date puppeteer check ("Winchester tab is disabled",
    tab enabled on purpose in `82b838d`) so G13 starts green.
0.8 Review-file skeletons: `docs/house-audit/review/<module>.md`, one row
    per function generated from the source (name, file:line, one column per
    REVIEW rule). A function missing from the file fails the audit.

### Phase 1 - formatting (rules 1.5, 2.1-2.8)
Per module: mark hand-aligned tables `clang-format off` (2.8) first, then
`clang-format -i`. G9 must show identical disassembly. The commit hashes go
into `.git-blame-ignore-revs`. Then add missing `default:` and
fall-through comments (2.7) in a separate commit (not behaviour-neutral:
full gate).

### Phase 2 - files and headers (3.8, 3.9, 4.1-4.6, 1.4)
Banners, include order, module-root includes (needs include-path change
in CMake first), no definitions in headers, make every non-API symbol
static. G9 applies to the banner and include-order commits.

### Phase 3 - naming (3.1-3.7, 3.10)
3a function renames to `Module_PascalVerb`, static functions snake_case -
   one commit per module; a rename table `docs/house-audit/renames.tsv`
   (old, new, file) drives a scripted rename and G9's name mapping.
   Exported `Dbg_*` and WASM exports: the JS callers in `template-glass/`
   are renamed in the same commit, G13 required.
3b variables, macros, enum constants, types.
3c boolean names (REVIEW).

### Phase 4 - documentation (11.2-11.4)
Doxygen block on every non-static function declaration. `@brief` states
what the function does, derived from reading the body - never invented;
where the behaviour is not clear from the code the block says
"unverified" and the question goes to Ronny. Comment review (11.3):
narration removed, hardware facts need a source or "unverified".

### Phase 5 - types and arithmetic (5.1-5.10)
Behaviour-changing. Per module, full gate plus G10 for cpu. Signed
bitwise and signed/unsigned mixing fixed by making the operands unsigned,
never by casts that hide the problem. Every change in `src/cpu/` gets a
before/after differential test where the function is pure (as done for
`ShiftDoubleReg` in step 4 of the first cleanup).

### Phase 6 - statements and expressions (6.1-6.10)

### Phase 7 - functions (7.1-7.6)
Split every function over 100 lines / 6 parameters, except the ones marked
`size exemption: opcode table` - and each of those needs Ronny's approval
in `exemptions.md`. Return-value checks, parameter validation, early
return.

### Phase 8 - memory and strings (8.1-8.7)
Includes the ~40 `strdup` calls in `src/debugger/debugger.c` that the
first cleanup left unchecked.

### Phase 9 - logging (9.1-9.7)
Includes the 9 `ND100X_*` machine-configuration environment variables:
each becomes a CLI flag + INI key pair (project rule).

### Phase 10 - concurrency, signals, setjmp (10.1-10.3)
Thread inventory in the review file; ThreadSanitizer build of G6.

### Phase 11 - portability (12.1-12.3)

### Phase 12 - final audit
Full `audit.py` = 0 on every rule; every review file complete; every
exemption approved; gate PASS; profile
nd100x profile of the `c-coding-standard` skill updated with the
final state and commit list.

## 5. Rules for doing the work

- No step is reported done without the gate output pasted in the report.
- A count that does not reach 0 stops the phase; the reason goes to Ronny.
  No silent skipping, no "estimated", no "rest when touched".
- Every behaviour change found on the way (a real bug) is its own commit
  with the evidence in the message; it is not hidden inside a cleanup
  commit.
- Commit messages never name the AI tool. Stage explicit files, never
  `git add -A`. Nothing is pushed without Ronny.
- Vendored files are never edited; a rule that fails only because of a
  vendored file is excluded by path in `audit.py`, not by exemption.

## 6. Decisions Ronny must make before or during the work

Asked one at a time, when the phase needs them:

1. Phase 0.4: what to do if the SMD boot is not deterministic.
2. Phase 3a: the module prefix for each file (proposed table first).
3. Phase 3a: whether the WASM/JS-visible `Dbg_*` names keep their names
   (they already match `Module_Verb`).
4. Phase 7: each `size exemption` candidate.
5. Phase 9: flag and INI key names for the 9 environment variables.

## 7. Size - measured baseline

Measured by `tools/house/audit.py` at `ecd19ab` plus the phase-0 tools
(19-SEP-2026), stored in `docs/house-audit/counts.json`. 157 files.

| Rule | Count | | Rule | Count |
|---|---|---|---|---|
| 1.5 tabs / no final newline | 5789 | | 5.3 bitwise on signed | 3102 |
| 2.x clang-format changes | 23916 | | 5.5 signed/unsigned | 194 |
| 2.2 missing braces | 1909 | | 5.9 const parameter | 7 |
| 2.4 multi-declarations | 96 | | 5.10 volatile | 31 |
| 2.7 switch default / fall-through | 46 | | 6.1 assignment in condition | 3 |
| 3.1 function names | 459 | | 6.2 parentheses | 33 |
| 3.2 static function names | 222 | | 6.4 goto | 1 |
| 3.3 globals | 5 | | 6.6 recursion | 16 |
| 3.4-3.7 identifier naming | 1152 | | 6.7 commented-out code (heuristic) | 37 |
| 3.8 file names | 3 | | 7.1 function size / parameters | 64 |
| 3.9 guards | 7 | | 7.3 return values | 760 |
| 4.1 banners | 326 | | 7.6 else after return | 12 |
| 4.4 include order | 107 | | 8.4 banned functions | 3 |
| 4.5 `"../"` includes | 163 | | 9.4 printf in library code | 43 |
| 4.6 should be static | 345 | | 9.7 getenv / DEBUG gates | 15 |
| 5.2 short / long / unsigned char | 202 | | 11.2 Doxygen | 853 |
| | | | 11.4 FIXME / TODO | 53 |
| | | | 12.2 platform #if in cpu/devices | 40 |
| REVIEW (empty cells, 1515 functions x 20 rules) | 30300 | | TOTAL | 70314 |

Rules at 0 already: 1.2, 1.4, 4.3, 5.6, 5.7, 6.5, 6.8, 7.4, 8.3, 8.5/8.7,
8.6, 10.3, 12.1.

Known limits of the measurement (each is REVIEW territory, not a 0):
6.7 is a heuristic and flags microcode listings in comments; 5.5 uses gcc,
which reports fewer sign conversions than clang; clang-tidy identifier
naming does not check struct members (the standard does not settle their
case).
