# HANDOFF: nd100x interactive shell + :PROG loader (SHIPPED) and dual-arch MON seam (PLAN ONLY)

Date: 2026-07-26
Repo: nd100x (git@github.com:RetroCoreLabs/nd100x.git). Paths below are relative to its root.
Branch of record for shipped work: `feat/prog-loader-and-shell-exec` (commit `763a4f4`, pushed to `origin`)
Shared submodule with nd500x: `external/ndmonlib`
Related nd500x repo (MON regression guard lives here): the sibling `nd500x` repository

TOP-PRIORITY STANDING CONSTRAINT: do NOT break the ND-500 MON emulation. `ndmonlib` is
shared with nd500x. Any ndmonlib change must be gated on the nd500x MON regression suite
staying green (run it BEFORE and AFTER every change).

---------------------------------------------------------------------------
## 1. What is DONE, committed, and pushed (commit 763a4f4)
---------------------------------------------------------------------------

Purpose: the nd100x interactive shell runs BPUN/PROG programs WITHOUT SINTRAN.

### 1a. Shell program execution
- `src/frontend/nd100x/nd100x_shell.c`
  - `cmd_run_program`: `join_path()` (collapses trailing slash), `resolve_program_file()`
    (case-insensitive rank match: rank 0 exact, rank 1 name+.bpun, rank 2 name+.prog),
    fopen pre-check BEFORE arming the CPU (fixes the crash-on-missing-file).
  - `.prog` -> `BOOT_PROG`: enters at `GetLastPROGHeader().startAddress`, always autostarts,
    flags 2-bank.
  - `.bpun` -> `BOOT_BPUN`: enters at `GetLastBPUNHeader().start` (NOT `boot`), honors
    `.action`: action!=0 -> `CPU_STOPPED`, return 0; action==0 -> `CPU_RUNNING`,
    return `SHELL_RESULT_RUN`.
- `src/frontend/nd100x/nd100x_shell.h`
  - `SHELL_RESULT_EXIT 0`, `SHELL_RESULT_ERROR -1`, `SHELL_RESULT_RUN 2`.
- `src/frontend/nd100x/nd100x.c` (~line 1105)
  - Before shell: `unsetcbreak(); setvbuf(stdout,NULL,_IOLBF,0);` (COOKED mode - the nd500x way;
    this is what fixed the instant-exit + broken-tty on a real terminal; root cause was
    `setcbreak()` setting VMIN=0 -> instant EOF).
  - On `SHELL_RESULT_RUN`: `setcbreak(); setvbuf(stdout,NULL,_IONBF,0);` then falls through
    to the machine loop (no return). Otherwise returns `EXIT_SUCCESS`.

### 1b. BPUN header capture
- `src/ndlib/load_bpun.c`
  - Added `static BPUN_Header s_last_bpun_header; static bool s_last_bpun_valid;` and
    `bool GetLastBPUNHeader(BPUN_Header* out)`; captures header before `return bpun.boot`.

### 1c. :PROG loader (NEW)
- `src/ndlib/load_prog.c` (see full file for the exact math)
  - `int LoadPROG(const char* filename, bool verbose)` -> start addr (>=0) or -1.
  - `bool GetLastPROGHeader(PROG_Header* out)`.
  - Header = 512-byte block, 6 big-endian 16-bit words: [0]start [1]restart
    [2]firstBank1 [3]lastBank1 [4]firstBank2 [5]lastBank2.
  - Bank 1 data at file offset 512; loads `(last-first)+1` words at word address `first`.
  - 1-bank sentinel: firstBank2==0xFFFF && lastBank2==0x0000 -> `twoBank=false`.
  - 2-bank (`twoBank=true`): Bank 1 IS loaded, Bank 2 is REFUSED with a warning - it needs the
    ALTERNATIVE page table (enabled at runtime via MON ALTON) which nd100x does not yet map.
    This is a KNOWN gap (see section 3).
  - Declares its externals directly (`WritePhysicalMemory`, `disasm_addword`, `DISASM`) rather
    than including `cpu_protos.h`, to stay dependency-light (mirrors load_bpun.c).
- `src/ndlib/ndlib_types.h`: `PROG_Header` struct
  (startAddress, restartAddress, firstBank1, lastBank1, firstBank2, lastBank2, bool twoBank).
- `src/ndlib/ndlib_protos.h`: AUTO-GENERATED (mkptypes). Do NOT hand-edit
  the LoadPROG/GetLastPROGHeader lines; they come from the CMake mkptypes step for load_prog.c.
- `src/ndlib/CMakeLists.txt`: added the mkptypes line for load_prog.c.
- `src/machine/machine_types.h`: `BOOT_PROG` added to BOOT_TYPE
  (after BOOT_BP, before BOOT_FLOPPY).
- `src/machine/machine.c` (~line 583): `case BOOT_PROG:` calls
  `LoadPROG`, sets `STARTADDR = bootAddress`.

### 1d. Tests (all real - #include the shipped .c and stub externals; NO fake reimplementations)
- `tests/test_shell.c`: #includes real nd100x_shell.c; stubs
  program_load, gReg, STARTADDR, set/get_cpu_run_mode, GetLastBPUNHeader, GetLastPROGHeader;
  `capture_stdout` ALWAYS restores stdout (tty safety). ~54 checks. PASS.
- `tests/test_load_prog.c`: #includes real load_prog.c; stubs
  WritePhysicalMemory/disasm_addword/DISASM; synthetic .prog files. 18 checks. PASS (18/18).
- `tests/CMakeLists.txt`: `test_shell` + `test_load_prog` targets.
- Build/run: from the repository root, `cmake --build build` then
  `cd build && ctest -R 'shell_tests|load_prog_tests' -V`.
  NOTE: build nd100x/ndlib FIRST so the generated `ndlib_protos.h` exists before the test build.

---------------------------------------------------------------------------
## 2. Verified facts (byte/trace evidence - do NOT re-derive)
---------------------------------------------------------------------------
- BPUN entry point = the `start` field, NOT `boot`. `LoadBPUN` returns `bpun.boot` (obsolete
  bootstrap-loader addr, often 0) which is WRONG as a program entry. The `action` field governs
  autostart (0 -> run at start; !=0 -> hold in OPCOM with P=start).
- The normal `--image` boot path in machine.c STILL uses `STARTADDR=boot` for BPUN. Left
  untouched (potential latent bug, not in scope).
- MAC.BPUN = standalone paper-tape assembler. Uses direct IOX paper-tape reader (device 400-403),
  ZERO MON calls (7695 IOX, 0 MON in 37k instrs). Does NOT read operator switches with TRA/TRR/OPR
  (26 distinct executed mnemonics, none of them TRA). Do NOT use MAC.BPUN to test MON emulation.
- MAC.PROG = the SINTRAN version. Loads at 0145000..0177777 (13824 words = exact file size),
  enters at 0177777 (JMP), executes MON 2 (OUTBT), zero IOX. THIS is the MON test binary.
- A MON-heavy small BPUN also exists: DITAP-1880D.BPUN (MON 0,1,2,7,43,50,65,77,143).

---------------------------------------------------------------------------
## 3. Known gaps / not-in-scope (flagged, untouched)
---------------------------------------------------------------------------
- 2-bank :PROG (alternative page table) not supported: Bank 1 loads, Bank 2 refused with warning.
- `cdc_tests` (`tests/test_cdc.c:152/154`, "default surface is 16384
  sectors") FAILS pre-existing and UNRELATED to this work. Do not "fix" as a side effect.
- machine.c `--image` BPUN path uses STARTADDR=boot (see section 2).

---------------------------------------------------------------------------
## 4. NEXT TASK - dual-arch MON seam (PLAN ONLY, explicitly ON HOLD)
---------------------------------------------------------------------------
User's last instruction: "Not yet - hold." Do NOT start coding the seam until the user authorizes.
When authorized, the agreed guard is: "Run nd500x MON tests each step" (regression BEFORE and
AFTER every ndmonlib change; a red ND-500 result blocks progress).

Design doc: `docs/NDMONLIB_DUAL_ARCH_SEAM_PLAN.md`

Key design points (from the plan, values still TO VERIFY against ND-860228.2 - never guess):
- nd100x has NO MON glue yet: `ndfunc_mon` (`src/cpu/cpu_instr.c`) just
  does `interrupt(14)` (SINTRAN trap). KEEP that as the DEFAULT. Add ndmonlib dispatch behind a
  flag that is auto-ON in shell mode.
- The seam must abstract THREE host ops for ND-100: (1) params in/out, (2) memory read/write,
  (3) register update/return. ND-500 currently uses arg_addresses + byte-addressed memory +
  K-flag returns (in `external/ndmonlib/src/core/mon_params.c`).
- Proposed uniform ops: `mon_param_in/out`, `mon_mem_read/write`, `mon_reg_get/set`, plus a
  `ctx->arch` selector and a per-MON ND-100 register-map table.
- ND-500 non-regression: only ADD struct fields (never change existing layout/semantics); land
  the ndmonlib change + BOTH submodule bumps (nd100x and nd500x) together.
- First subset to validate on ND-100: MON 0 (LEAVE), 1 (INBT), 2 (OUTBT), 3 (SetEcho), via a
  purpose-built minimal BPUN in `asm/`, then MAC.PROG (MON 2).

---------------------------------------------------------------------------
## 5. How to verify the shipped state cold
---------------------------------------------------------------------------
```
# from the nd100x repository root
git log --oneline -1            # expect 763a4f4 on feat/prog-loader-and-shell-exec
cmake --build build            # build ndlib first if ndlib_protos.h is missing
cd build && ctest -R 'shell_tests|load_prog_tests' -V   # both PASS
# cdc_tests fails - pre-existing, unrelated, leave it.
```
