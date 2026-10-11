# nd100x TODO

Single list of everything outstanding, written 20-SEP-2026.
Counts come from `docs/house-audit/counts.json` (baseline at commit 7a295c2).
The rules themselves and the gate are in `docs/HOUSE_STANDARD_FULL_CLEANUP_PLAN.md`;
this file is the checklist, not a second copy of the plan.

Every item marked GATE must pass G1-G14 (`tools/house/gate.sh`) before its commit.

---

## A. House standard cleanup - remaining phases

Total findings still open: **35,376** (of which 30,300 are empty REVIEW cells).
Updated 21-SEP-2026 after phase 3.

### A.1 Phase 3 - naming - DONE except for 99 names that need decisions

Rule 3.1 went 540 -> 19, rule 3.2 222 -> 1, rule 3.4-3.7 1,151 -> 77. Every
module under `src/` and the `tests/` directory has been through the fixers,
each with the full gate and an object comparison. Every rename is traced in
`docs/house-audit/rename_log.tsv` (3,451 rows).

What is left is not sweepable, and each name needs a decision:

- [ ] 3.1 - 19, 3.4-3.7 - 77, 3.2 - 1, 3.3 - 2. These are the names the tools
      correctly refuse:
      - declared in one module and used from others, which a per-module tool
        cannot rewrite - `Log_IsEnabled`, `BOOT_TYPE`, `DRIVE_TYPE`,
        `MC_Runtime`, `MountedDriveInfo_t`, `BPUN_Header`, `PROG_Header`,
        `nd_pollfd_t`, `nd_socklen_t`;
      - the register file indices `_A`, `_B`, `_D`, `_L`, `_P`, `_T`, `_X`,
        `_STS` and the enum constants `Four`, `Sixteen` in `cpu_types.h`;
      - `scsi_debug_enabled`, a non-static global used from the vendored
        `ncr5386.c`, which cannot be edited.
      Each needs either a whole-tree rename of the whole family, or a
      recorded exemption. Renaming half a family is worse than none.
- [ ] Re-check rule 4.6 for `src/cpu/cpu_mms.c` and
      `src/devices/hdlc/dma_control_blocks.c` now that the parameter renames
      are in.
- [ ] `src/cpu/cpu_instr.c`: the registration table at the bottom is still in
      its original order, deliberately - see
      `docs/OPCODE_FILE_ORGANISATION_PLAN.md`. Note before touching it: these
      are `instruction_add_mask` and `instruction_add_range` calls, so
      registration order may decide precedence where masks overlap. That is a
      behavioural question, not a cosmetic one.

### A.2 Phase 5 - types and arithmetic - 3,517 findings

- [ ] 5.3 signed operand in a bitwise operation - 3,102 findings. The big one.
      Decide a per-module approach before touching any of it; a blind cast sweep
      will hide real sign bugs.
- [ ] 5.5 - 194, 5.2 - 183, 5.10 - 31, 5.9 - 7, 5.1/5.4/5.6-5.8 - clean.

### A.3 Phase 6 - statements and expressions - 89 findings

- [ ] 6.7 - 37, 6.2 - 33, 6.6 - 16, 6.1 - 3.

### A.4 Phase 7 - functions - 836 findings

- [ ] 7.3 return value ignored - 760 findings.
- [ ] 7.1 function too long - 64 findings.
- [ ] 7.6 - 12.
- [ ] 2.4 deferred in `src/cpu/cpu_instr.c` and `tests/test-gateway.c` (36
      findings) - blocked until the phase 7 function splits are done.

### A.5 Phase 8 - memory and strings - 3 findings

- [ ] 8.4 - 3.

### A.6 Phase 9 - logging and configuration - 57 findings

- [ ] 9.4 - 42, 9.7 - 15.
- [ ] 9 environment variables `ND100X_*` need the CLI flag + INI key pair the
      project convention demands (see `CLAUDE.md`, "Configuration Options
      Convention"). Decision still open - see C.4.

### A.7 Phase 11/12 - portability and file rules - 437 findings

- [ ] 4.5 include paths - 163 findings. BLOCKED on nd500x becoming a library
      (section B); `../` includes keep their temporary exemption until then.
- [ ] 4.6 - 132 (see A.1), 4.4 - 43, 4.1 - 6.
- [ ] 11.4 - 53, 12.2 - 40.

### A.8 Phase 12 - final audit

- [ ] 30,300 empty REVIEW cells in `docs/house-audit/review/*.md`: 1,500+
      functions x 20 REVIEW rules. These are human judgement rows, not
      mechanical fixes. Needs a decision on who fills them and at what depth.
- [ ] Final full audit run, re-baseline `docs/house-audit/counts.json`, ratchet
      locked at the new numbers.

---

## B. nd500x as a library (planned by Ronny, 20-SEP-2026)

Goal: keep the existing nd500x binary and its behaviour, and additionally build
nd500x as a library that nd100x links.

- [ ] Give nd500x its own include root so its `src/cpu/` and `src/machine/`
      headers stop shadowing the nd100x ones in the WASM build (both are
      compiled into the same target today).
- [ ] CMake option to build nd500x as a static library.
- [ ] Link it into nd100x for the WASM target first.
- [ ] Shared memory + octobus communication between ND-100 and ND-500, following
      what RetroCore does. Wait until the remaining RetroCore bugs are fixed.
- [ ] Only after the include root exists: do house rule 4.5 (A.7).

This is its own piece of work, not part of the cleanup phases.

---

## C. Open decisions (Ronny decides, one at a time)

- [ ] C.1 82 unused functions in `docs/house-audit/unused_functions.txt` -
      delete, or keep and mark?
- [ ] C.2 `tools/reth-tap` licence line - the code came from nd500x under MIT.
- [ ] C.3 `src/ndlib/ndlib_protos_preamble.h` has no include guard.
- [ ] C.4 9 `ND100X_*` environment variables - convert to CLI flag + INI key
      pairs, or document them as an exception?
- [ ] C.5 `_removed_MOVB_AND_MOVBF_` - dead marker, remove or keep as history?
- [ ] C.6 `system()` call in `tests/test_printer_main.c`.

---

## D. Testing

- [ ] SMD / DISC-TEMA automated harness: boot the TPE test floppy, drive the
      disk TEMA tests, assert the result. Asked for by Ronny while chasing the
      "Parallel seek disabled" bug; not built yet. Model it on
      `tools/tpe_autorun.py` and `docs/TPE-AUTORUN.md`.
- [ ] No unit tests exist for `src/devices/smd/device_smd.c` at all. The SMD fix
      (commit ecd19ab) was verified only by a boot comparison.

---

## E. Done (for reference)

- Phase 3 naming, 21-SEP-2026. Rule 3.1 540 -> 19, rule 3.2 222 -> 1, rule
  3.4-3.7 1,151 -> 77. Total 37,194 -> 35,376. Traced in
  docs/house-audit/rename_log.tsv.
- Every opcode function named after the instruction it implements
  (`opcode_jan_jump_if_a_negative`), with a Doxygen block generated from
  docs/cpu_documentation.md carrying opcode, mask, category, privilege and
  bit layout. `tools/house/opcode_docs.py`.
- src/cpu/cpu_instr.c sorted into 20 sections by instruction area, octal
  within each. `docs/OPCODE_FILE_ORGANISATION_PLAN.md`.
- The STS bits named: STS_CARRY, STS_PAGING_ON, and the accessors
  STS_CARRY_IS_SET, so a bit number cannot be confused with a bit value.
- MOVB/MOVBF: the dispatched pair holds the plain names, the kept-but-not-
  dispatched pair is marked _buggy, and why is written in the source.
- Reusable method captured in the `c-refactoring` skill.

- SMD "Parallel seek disabled": Initiate Seek and Return To Zero Seek now use
  `IODELAY_HDD_SMD` (10 ticks), matching the RetroCore C# driver; 11 transfer
  error exits now call `FinishOperation`. Commit ecd19ab.
- SMD operation enums renamed to the manual's names
  (`DEVICE_OP_SEEK_COMPLETE_SEARCH`, `DEVICE_OP_RETURN_TO_ZERO_SEEK`, ...).
- House rule 3.1 changed to `module_verb` lower snake_case. Commit 7a295c2.
- Phases 0, 1, 2 and 4 complete: formatting, file/header structure, static
  functions, banners, Doxygen. Rule 11.2 is at 0.
- Rules the audit now counts as 0: 1.2, 1.4, 1.5, 2.2, 2.7, 2.x, 4.3, 5.6, 5.7,
  6.4, 6.5, 6.8, 7.4, 8.3, 8.5/8.7, 8.6, 10.3, 11.2, 12.1.
  Rules not in this list and not in sections A.1-A.7 are not checked
  mechanically by `tools/house/audit.py` at all - they live in the REVIEW cells.
- Gate G1-G14 built and in use; it has caught several real regressions.

---

## F. Rename trace log

Every identifier renamed by the cleanup is logged, for all phases, so a name
that turns out wrong can be traced back and undone.

- `docs/house-audit/rename_log.tsv` - the trace: commit, date, rules, kind,
  file, line, old, new, count. One row per renamed identifier per file.
- `tools/house/rename_log.py` - rebuilds it from git history. Run after every
  rename commit.
- `docs/house-audit/rename_kinds.tsv` - append-only; `rename_naming.py` writes
  the kind (static function, parameter, local variable) as clang-tidy reported
  it, and the log joins it on. Rows with no recorded kind are classified by
  reading the file, which is an inference: a struct tag can come out as
  "local variable", and a name that appears only in a comment is still listed.

Any new rename mechanism - the clang-rename pass for rule 3.1 above all - must
append to the same trace before it is used on real code.

---

## G. Performance - stop ticking every device on every ND-100 instruction

Added 12-OCT-2026. Not started.

### G.1 What was measured

`perf` on the ND-100 thread, 15 seconds, SINTRAN III VSX/500 L idle after
CPU-STAT on four ND-5000 CPUs, binary at commit `fa4d043`:

| Where | Share of the ND-100 thread |
|---|---|
| `IO_Tick` (`src/machine/io.c`), called from `cpu_run` after every instruction | 62% |
| `private_cpu_tick` - fetching and executing the instruction | 34% |

Inside `IO_Tick` (own time, not including callees):

| Function | Share |
|---|---|
| `devmgr_tick` (`src/devices/devicemanager.c`) - the loop over all devices | 19% |
| `dev_tick` (`src/devices/device.c`) - the indirect call per device | 10% |
| `terminal_tick` | 9% |
| `dev_tick_io_delay` | 6% |
| `scsi_bus_clock`, `NCR_Clock`, `scsi_target_clock` | 4.5% |
| `mfbus_service_nd5000_mailboxes` | 3.5% |
| `rtc_tick` | 2% |

So ticking the devices costs about 1.8 times as much as executing the
instruction. Removing most of it would let the ND-100 run two to three times
as many instructions per second on the same host core, and under `--throttle`
it would cut the host load by the same factor. That is the value. It is an
estimate from the shares above, not a measurement.

It does NOT by itself make the ND-100 thread less busy when SINTRAN is idle:
that load is set by the idle rule in `private_cpu_tick` (`src/cpu/cpu.c`,
1 ms sleep after 10000 instructions in a row on level 0). See G.4.

### G.2 Why "call IO_Tick every N instructions" alone is wrong

Fifteen devices have a `Tick` routine, and they count calls as time:

- `dev_tick_io_delay` decrements `delayTicks` by one per call. Ten device
  files queue such delays (floppy DMA 14 places, SMD 9, floppy PIO 7,
  winchester 4, CDC 3, drum 2, SCSI 2, terminal 1, paper tape writer 1).
  With a divider of N every disc and floppy timeout becomes N times longer.
- `rtc_tick` in ticks mode decrements `rtcCounter` once per call: 10550
  calls per 20 ms pulse. This mode is the deterministic one the unit tests
  and the TPE runs select.
- `terminal_tick` counts `checkInputQueueTick` up to `MAX_TICKS` (100)
  before it moves one character from the input queue.
- `scsi_tick` clocks the SCSI bus state machine one step per call.
- `octobus_tick` counts down the card's delayed operation.
- A device interrupt reaches the CPU only through the value `IO_Tick`
  returns, so with a divider an interrupt raised by an IOX instruction is
  seen up to N - 1 instructions late.

### G.3 The change

- [ ] Give every `Tick` routine the number of ND-100 instructions that have
      passed since its last call (a parameter, or one field the device
      manager sets), and make each of the six counters in G.2 subtract or
      add that number instead of one. `dev_tick_io_delay` must fire every
      delay that has run out, in order, when several run out in one call.
- [ ] Let each device say how many instructions it can go without a call:
      the shortest queued delay, the remaining `rtcCounter`, the remaining
      `checkInputQueueTick`, 1 while a SCSI transfer or an octobus
      operation is in flight, "nothing pending" otherwise.
- [ ] `cpu_run` counts down to the smallest of those and calls `IO_Tick`
      only then, with the elapsed count. This keeps every timeout exact:
      the tick happens on the same instruction as today, there are just no
      ticks in between.
- [ ] Force a tick right after every IOX/IOXT instruction and after
      anything else that can queue a delay or raise an interrupt line
      (IDENT, a telnet or keyboard byte arriving, a frame on the octobus),
      so the next deadline is recomputed and interrupt latency stays at one
      instruction.
- [ ] Cap the distance between ticks (64 instructions) for the two things
      that depend on host time and not on a counter: the wall-clock RTC and
      the ND-5000 doorbell poll.
- [ ] The WebAssembly build runs the ND-5000 CPUs from this tick
      (`s_pump_mask` in `src/machine/mfbus_bridge.c`); while a CPU is being
      pumped the deadline must stay at 1.

### G.4 Separate, smaller item: the idle rule

- [ ] `private_cpu_tick` sleeps 1 ms only after 10000 consecutive level-0
      instructions. Measured on an idle SINTRAN with four ND-5000s: about
      250 to 310 sleeps per second and 65 to 70% of a host core. Find which
      interrupt levels break up the level-0 stretches before changing the
      threshold.

### G.5 Validation - all of it, before the commit

- [ ] `ctest`: all tests, including `tests/test_rtc.c` in both ticks and
      wall mode. Add a test that queues delays of 1, N - 1, N and N + 1
      ticks and checks each fires on the same instruction count as before
      the change.
- [ ] RTC ticks mode: pulse count over a fixed instruction count is
      unchanged (exactly one per 10550).
- [ ] TPE instruction verifier (`docs/TPE-AUTORUN.md`) - same result as
      before.
- [ ] Boot SINTRAN from SMD, from floppy, from SCSI and from winchester,
      on scratch copies of the images (`docs/VERIFICATION.md`).
- [ ] DISC-TEMA and the floppy tests on scratch copies only: these exercise
      the controller timeouts that G.2 lists.
- [ ] Terminal input: paste a long line into a telnet terminal and into the
      console; no lost or reordered characters.
- [ ] ND-5000: four CPUs, one LED per CPU, a key in each - the 12-OCT-2026
      procedure in `docs/ND5000-MULTI-CPU.md`.
- [ ] `make wasm-glass` builds and boots in the browser.
- [ ] `perf` before and after on the same idle state, and instructions per
      second at full speed before and after. Record both here.
