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

Added 12-OCT-2026. G.1 to G.3 done 12-OCT-2026. G.4 and G.5 open.

### G.1 What was measured before

`perf` on the ND-100 thread, 15 seconds, SINTRAN III VSX/500 L idle after
CPU-STAT on four ND-5000 CPUs, binary at commit `fa4d043`:

| Where | Share of the ND-100 thread |
|---|---|
| `IO_Tick` (`src/machine/io.c`), called from `cpu_run` after every instruction | 62% |
| `private_cpu_tick` - fetching and executing the instruction | 34% |

Fifteen devices have a `Tick` routine and all of them were called after every
instruction. Ten of the fifteen do nothing in `Tick` except
`dev_tick_io_delay()`, and the terminal only counts.

### G.2 What was changed

`devmgr_tick()` (`src/devices/devicemanager.c`) calls a device's `Tick` only
when the device has work:

- `tickAlways` devices (RTC, SCSI, HDLC, Ethernet, octobus): every instruction,
  as before. Their `Tick` counts instructions or clocks a state machine.
- every other device: only while it has a queued I/O delay
  (`ioDelayCount > 0`) or its `TickNeeded` says so. The terminal's
  `TickNeeded` is "a character waits in the input queue".
- the OR of the interrupt bits is kept and computed again only after
  `dev_irq_changed()`, which every write to `interruptBits` now calls.

A device that is called is called on the same instructions and in the same
order as before, so no I/O delay changed length. The terminal's
`checkInputQueueTick` count stands still while its input queue is empty; a
character still reaches the UART within `MAX_TICKS` + 1 instructions.

This is NOT the larger design first written here (one call per deadline with an
elapsed count). That design changes every counter in fifteen devices; this one
changes none and takes most of the gain.

### G.3 What was measured after

Same host, debug build (`-g`, no optimisation), old binary = `684b0b2`:

| Measurement | Before | After |
|---|---|---|
| CPU time for 300 000 000 instructions, SMD boot of the ND-5000 pack, `--rtc=ticks` | 47.4 s and 49.7 s (two runs) | 23.9 s |
| `IO_Tick` share of the ND-100 thread (`perf`, 15 s, four LED sessions) | 62% | 33% |
| `private_cpu_tick` share | 34% | 57% |
| Host load of the ND-100 thread, SINTRAN idle | 64.5% | 49.9% |

Proof that nothing else changed - the complete `--trace` output (every
instruction with all registers), old binary against new, `--rtc=ticks`:

| Boot | Instructions | Result |
|---|---|---|
| SMD, ND-5000 pack | 100 000 000 | traces identical (md5), disc images identical |
| Floppy, TPE monitor | 40 000 000 | traces identical, image identical - BUT see below |
| Winchester, `WD0-SINTRAN-M` | 60 000 000 | traces identical, written image identical |

The floppy row proves nothing. `[MEASURED 14-OCT-2026]` A floppy (TPE) trace
is not repeatable even with one binary: the TPE monitor reads the panel status
register (`TRA PANS`, written in `src/devices/panel/panel.c`), and two runs of
the same binary first differed at line 1 119 424. The two runs compared here
happened to read the same value. The SMD and winchester rows hold: their sums
were the same in every run.

Also run on the new binary:

- `ctest` 90/90, with the new `tests/test_devmgr_tick.c` (delays of 0, 1, 2, 3,
  63, 64, 65 and 1000 fire on the same call as before; two devices; a callback
  that queues a delay on an earlier and on a later device; the interrupt OR;
  terminal input after an idle stretch).
- `tools/tpe_autorun.py`, old and new binary side by side: INSTRUCTION-C03,
  MEMORY-D04, PAGING-C02 PASS and CONFIGURATIO-D05 INCONCLUSIVE in both.
- Four ND-5000 CPUs, LED on each from its own telnet terminal, `WHO` shows cpu
  1 to 4. An 88-character paste into LED gives the same screen on the old and
  the new binary.
- WebAssembly glass build: compiles, and in a headless browser the built-in
  ND-5000 machine boots SINTRAN and runs CPU-STAT on the ND-5000.

Not run: SCSI boot (the SCSI controller is `tickAlways`, so its path did not
change), DISC-TEMA, the floppy stream test.

### G.4 The idle rule - done 12-OCT-2026

`private_cpu_tick` (`src/cpu/cpu.c`) slept 1 ms after 10000 consecutive
level-0 instructions, whatever ran there.

`[MEASURED 12-OCT-2026]` Idle SINTRAN runs five instructions at 011302-011306
on level 0, every one with the interrupt system and paging on (196534 of the
last 200000 traced instructions). The stand-alone TPE monitor runs on level 0
with paging off: 39 million instructions, none with paging on, 5% with
interrupts on. Nothing breaks the level-0 stretches up, so the host load was
the rule itself: 10000 instructions take 0.8 ms and the sleep 1.1 ms.

Now: on level 0 with paging and interrupts both on, the sleep comes after
`--idle-count` instructions (`[runtime] idle_count`, default 2000, 0 = off).
Everything else keeps 10000.

The default depends on what the machine boots from
(`set_default_idle_for_boot` in `src/frontend/nd100x/nd100x.c`): 2000 for
smd, scsi, wd and cdc; 50000 for floppy and directly loaded programs. The
reason is TPE MEMORY-D04, which works on level 0 with paging and interrupts
on: its "ADDRESSES IN ADDRESSES" pass took 28.3 s at 2000, 8.3 s with the
rule off and 5.6 s at 50000. PAGING-C02 took 6.3-6.5 s at all three. The
SINTRAN boot from SMD reached "RUNNING" after 10.5-11.1 s at 2000 against
10.4-10.6 s with the rule off (three runs each).

| Idle SINTRAN, ND-100 thread, 10 s | Host load |
|---|---|
| `--idle-count=0` (the old rule) | 45.7% of a core |
| default 2000 | 15.1% of a core |

The `--rtc=ticks` instruction trace of the first 100 000 000 instructions of
the SMD boot is unchanged (same md5), `ctest` 90/90, four users with LED on
four CPUs still start.

Not measured: the run time of a stand-alone test that turns paging on at level
0 (TPE PAGING-C02), and the effect on a loaded SINTRAN, where each early sleep
can delay an ND-5000 answer by up to 1 ms.

### G.5 What is left in IO_Tick

From the `perf` run after the change, shares of the ND-100 thread:

| Function | Share |
|---|---|
| `devmgr_tick` own time | 10% |
| `mfbus_service_nd5000_mailboxes` (called while a work flag is set) | 6% |
| `scsi_tick`, `scsi_bus_clock`, `NCR_Clock`, `scsi_target_clock`, `scsi_step_go_state` | 7.7% |
| `rtc_tick` | 3.2% |
| `octobus_tick` | 1.9% |

- [ ] The SCSI bus and chip are clocked on every instruction even with no
      command in flight. Whether an idle bus can be skipped has to be read out
      of the NCR-5386 port, which is vendored code.
- [ ] `rtc_tick` in ticks mode counts one per call; it could count down to its
      deadline instead.
