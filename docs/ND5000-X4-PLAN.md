# Four ND-5000 CPUs on one ND-100: plan

Branch `nd5000-x4` in this repository and in the nd500x checkout the build
uses (the sibling `../nd500x`; see `CMakeLists.txt`, "sibling if present").
Every path below is relative to the repository root; nd500x paths are prefixed
`nd500x/`.

Each claim carries its evidence tag: `[READ file:line]` = read in source this
branch, `[MEASURED]` = observed by running something, `[UNVERIFIED]` = not yet
checked. Nothing without a tag is a fact.

## Why four

| Source | Limit |
|---|---|
| ND-5900 sales material (NDInsight History/machines/systems/ND-5900.md) | 2, 3 or 4 CPUs |
| SINTRAN M06 (LN5DEST = 073B) | stations 070B..073B, four |
| SINTRAN L07 (LN5DEST = 077B, NDInsight SINTRAN/CARVING-HANDOFF.md:509) | stations 070B..077B, eight |
| Octobus station table, ND-05.020.01 T329 | 070B..076B, seven slots |

The shipped product stopped at four, so the configuration stops at four
(`src/machine/machine_config.h`, `MC_ND5000_MAX_CPUS 4`). The bus and bridge
keep the hardware's seven slots (`src/machine/mfbus_bridge.c`,
`MFBUS_MAX_ND5000 7`) - that is what the octobus can address, and refusing it
there would be refusing the hardware.

## What is already per CPU and what is not

Done before this branch `[READ]`:

- One shared pool, relaxed-atomic byte accessors on both sides
  (`nd500x/src/ndbus/ndbus_pool.h`, `nd500x/src/machine/io.c:279,429`,
  `src/machine/mfbus_bridge.c:1202,1298`).
- One bus mutex for every LOCK cycle (`nd500x/src/ndbus/ndbus_lock.c`); the
  ND-100 TSET holds it across its pair (`src/cpu/cpu_instr.c:1913`).
- Per-station state in the bridge: machine, CPU, runner thread, servicer,
  context block, process table, trap capture (`MfbusCpuSlot`,
  `src/machine/mfbus_bridge.c:218-287`; `NdbusNd5000.servicer`,
  `nd500x/src/ndbus/ndbus_nd5000.h:184`).
- Station / CPUNO / X5CPU conversion in one place
  (`nd500x/src/ndbus/ndbus_cpunum.h`).
- Per-CPU TLB with cross-CPU shootdown, registry of 8
  (`nd500x/src/cpu/nd500_tlb.c`, `ND500_TLB_MAX_CPUS 8`).
- Mailbox service and pump loops walk every station
  (`src/machine/mfbus_bridge.c:3674,3686`).
- The nd500x `nd500_cpu_lock()` mutex (`nd500x/src/machine/machine.c:84`) is
  used only by nd500x's own shell and DAP adapter, not by the bridge - it does
  not serialize the bridge's CPUs.

Done on this branch, untested `[READ]`:

- Configuration capped at four `[nd5000.1]`..`[nd5000.4]`, stations
  070B..073B (`src/machine/machine_config.h:194-196`).
- The ND-500 TSET instruction holds the same bus mutex across its read and
  write, through a hook pair installed by the bridge
  (`nd500x/src/cpu/cpu.c` `nd500_set_tset_lock`,
  `nd500x/src/cpu/instructions/CONTROL/Tset.c`,
  `src/machine/mfbus_bridge.c` in `mfbus_attach` / `mfbus_detach`).

Still process-wide in the ND-500 core, i.e. shared by four threads `[READ]`:

| State | Where | Risk |
|---|---|---|
| `g_trap_state` | `nd500x/src/cpu/cpu.c:26`; 296 call sites of `nd500_trap_occurred()` with no CPU argument | CORRECTNESS. One CPU's trap is seen, or cleared, by another. |
| `g_pc_ring*` diagnostic ring | `nd500x/src/cpu/cpu.c:33-40`, written on every step | data race, diagnostics only; still undefined behaviour |
| `g_table` / `g_meta_index_built` instruction metadata | `nd500x/src/cpu/cpu_instr.c:221,309` | built lazily on first use; two threads building at once is a race `[UNVERIFIED whether it is built before threads start]` |
| `g_mmu_guest_tables`, `mmu_demand_segments` env lazies | `nd500x/src/cpu/nd500_mmu.c:60,181` | read-once from getenv; racy first read, same value either way |
| `ptewatch_*`, `vwatch_*` | `nd500x/src/machine/io.c:31-40` | diagnostics only |
| `g_tlb_flushes` counter | `nd500x/src/cpu/nd500_tlb.c` | statistics only |
| `nd500_settings()` singleton | `nd500x/src/cpu/nd500_settings.c:29` | read-only after init (`[READ]` no `nd500_settings()->x =` writes exist); shared is correct |
| host ops singleton | `nd500x/src/cpu/nd500_host_posix.c:193` | NDIX fecall path only; the bridge never calls fecall (`[READ]` zero hits in `mfbus_bridge.c`) |
| spin damper | `nd500x/src/ndbus/ndbus_lock.c`, defined, called by nobody | a guest spinning on TSET burns a whole host core; four spinners can starve the holder |
| engine lock | `nd500x/src/ndbus/ndbus_lock.c`, one recursive mutex for all servicers | correct, coarse: the ND-100 chain walk and every CPU's answer serialize on it |

## Phases

### Phase 0 - baseline on the branch

- [x] 0.1 Build `build/` incrementally (`cmake --build build`), no clean, no
      flag changes. Record `build/bin/nd100x --version`; the hash must be the
      branch tip, `-dirty`.
- [x] 0.2 Run `ctest --test-dir build` and the nd500x `test_ndbus` /
      `test_ndbus_station` binaries. Record pass/fail per test before any
      further change, so a later failure can be attributed.

### Phase 1 - audit the remaining process-wide state (no code)

- [x] 1.1 Confirm when `g_table` / the meta index is built
      (`nd500x/src/cpu/cpu_instr.c:309`) relative to `mfbus_start_nd5000()`.
- [x] 1.2 Trace the two `nd500_trap_clear()` calls in the bridge
      (`src/machine/mfbus_bridge.c:974` on the ND-5000 thread, `:2987` on the
      ND-100 thread) and the `trap_captured` sink, to decide between the two
      designs in 2.1.
- [ ] 1.3 Resolve the X5CPU naming: `ndbus_cpunum.h` says X5CPU is the
      station's context block (0..6); `ndbus_servicer.h:1007` says X5CPU in a
      message is the PROCESS number. `mfbus_bridge.c:2730` prefers the
      message's value and falls back to the station's. Write down which is
      right for a four-CPU machine, with the NPL line that says so.
- [ ] 1.4 List every bridge static that the ND-5000 thread reads and the
      ND-100 thread writes (or the reverse) and check each has an atomic or
      the engine lock. Known good: `mfbus_tick_work`, `s_armed_mask`,
      `s_pump_mask`, `runner_leaving`, `activation_trap_check`, inbound queue.

### Phase 2 - nd500x core fixes (sibling checkout, branch nd5000-x4)

- [x] 2.1 Make the trap state per CPU. Two candidates; 1.2 decides:
      (a) `_Thread_local Nd500TrapState g_trap_state` - no call-site change,
      each runner thread owns its copy; the ND-100 thread's `nd500_trap_clear()`
      at `:2987` then clears a copy nobody reads, which is harmless only if the
      runner thread clears its own on the next start `[UNVERIFIED]`.
      (b) move `Nd500TrapState` into `Nd500Cpu` and have
      `nd500_trap_occurred()` read a thread-local "current CPU" pointer set by
      the runner at step entry. Correct from any thread, one more pointer.
- [x] 2.2 Make `g_pc_ring*` thread-local or per CPU (same mechanism as 2.1).
- [x] 2.3 Build the instruction meta table eagerly in `nd500_cpu_init`/reset,
      or guard it with `pthread_once`, per 1.1.
- [x] 2.4 Resolve the two MMU env lazies at init, not on first use.
- [x] 2.5 Wire the spin damper: a third hook `nd500_set_tset_spin_hint`
      alongside the lock hooks; `Tset.c` calls it with the operand address when
      the old value was non-zero and with "succeeded" otherwise; the bridge
      keeps one `NdbusSpinDamper` per slot and yields through the host ops.
- [ ] 2.6 WASM must still build with none of this active: no `-pthread`, the
      hooks NULL, `_Thread_local` degrading to a plain global.

### Phase 3 - bridge checks for four stations

- [ ] 3.1 `mfbus_apply_config` adds every enabled `[nd5000.N]`
      (`src/machine/mfbus_config.c`) - confirm the per-station failure path
      leaves the others running.
- [ ] 3.2 Context block area: one stride per X5CPU past the area base
      (`mfbus_bridge.c:2090,3476`) - confirm four blocks fit and do not overlap
      the mailbox header for the default `[mfbus]` geometry.
- [ ] 3.3 The ACCP bring-up (ECHO, LSYSPAR, LPARP/VPARP, LOCSM, STARTMIC) is
      answered per station from that station's own state, never from slot 0.
- [ ] 3.4 Inbound frame queue to the ND-100 (`MFBUS_ND100_INBOUND_WORDS`) is
      sized for four stations answering at once; a drop is counted and logged.
- [ ] 3.5 Log lines name the station in octal wherever they name a CPU, so a
      four-CPU log can be read.

### Phase 4 - tests

- [x] 4.1 `tests/test_machine_config.c`: four `[nd5000.N]` sections load with
      stations 070B..073B; `[nd5000.5]` is refused; `station = 074B` is refused;
      the error text names 073B.
- [x] 4.2 `tests/test_mfbus_bridge.c`: four stations each get a CPU and a
      thread; all four start and stop cleanly; `mfbus_nd5000_running_mask()`
      reports 0b1111 then 0.
- [x] 4.3 `tests/test_mfbus_bridge.c`: four REAL ND-500 CPUs run a hand-encoded
      program from the pool - TSET on one cell, increment a shared counter,
      release, loop N times, then stop - and the counter equals 4 x N. This is
      the test of the ND-500 TSET lock through the real instruction path. The
      encoding is checked with the nd500x disassembler before it is trusted.
- [ ] 4.4 nd500x `test_ndbus` layer 3 (TSET helpers under threads) and layer 7
      (two mock CPUs on threads) keep passing; raise layer 7 to four runners.
- [x] 4.5 Run 4.3 under `make sanitize` (ThreadSanitizer if the toolchain has
      it; AddressSanitizer at least) - a data race in the pool accessors would
      show here and nowhere else.

### Phase 5 - validation on SINTRAN (needs Ronny's go: long runs)

- [x] 5.1 Scratch copy of `ND5000.IMG`; an INI derived from `ND5000.ini` with
      `[nd5000.1]`..`[nd5000.4]`. Boot to the SINTRAN prompt. ~8 minutes per
      boot `[MEASURED in earlier sessions, per nd500x docs]`.
- [ ] 5.2 Evidence that SINTRAN found four CPUs: the bridge's
      "ND-5000 on the octobus at station %o" line for each station, the ACCP
      bring-up frames per station, and the ND-500 monitor's own report.
      How SINTRAN's ND-500 monitor lists CPUs, and how a domain is placed on a
      given CPU, is `[UNVERIFIED]` - to be found in the ND-500 monitor manual
      before 5.2 runs, not guessed at the prompt.
- [ ] 5.3 Run a program on more than one CPU at once and show the per-station
      instruction counters (`mfbus_nd5000_instructions`) all advancing.
- [ ] 5.4 Pool snapshot at the point of interest (`ndbus_pool_snapshot_save`)
      so the four context blocks and mailbox extension blocks can be examined
      offline.
- [x] 5.5 Negative: with `[nd5000.1]` only, the boot behaves exactly as before
      the branch (no regression for the one-CPU machine).

### Phase 6 - documentation

- [x] 6.1 `docs/ND5000-MULTI-CPU.md`: the limit and its sources, the INI
      schema for four CPUs, the three numberings, the concurrency model as
      built, what is per CPU and what is shared, how to validate, open items.
- [x] 6.2 `ND5000.ini`: a commented four-CPU example (sections 2-4 present,
      disabled).
- [x] 6.3 `template-glass/data/machines.json:72` still says "up to seven" -
      change to four.
- [ ] 6.4 `CLAUDE.md`: `[nd5000.<n>]` n = 1..4 in the options list.
- [ ] 6.5 Outside the repo: the `nd5000-octobus` skill's status line says
      "researched, NOT built"; it is stale and is Ronny's to update.

### Phase 7 - commits (only when Ronny asks)

- [ ] 7.1 nd500x sibling first, then bump `external/nd500x`, then nd100x.
      Explicit file lists, no `git add -A`.

## Open questions

1. Which SINTRAN build is on `ND5000.IMG` - M06 (four stations) or L07
   (eight)? Decides whether 5.2 can even see station 073B. `[UNVERIFIED]`
2. Does the ND-500 monitor on that pack load microcode and the swapper for
   every CPU it finds, or only CPU 1 unless told otherwise?
   `[MEASURED 12-OCT-2026]` A CPU is brought up when a terminal gives
   `CHANGE-CPU n`. There is one swapper (process 0); its message is not CPU
   bound and was served by station 070B in every nd100x run.
3. Is `ADRZERO` ever per CPU in practice? The configuration forces one
   `base_page` for all (`src/machine/machine_config.c:1813-1847`); the carved
   `DEFMC` stores it per CPU datafield. `[UNVERIFIED]`

## Progress log

11-OCT-2026. Done: phases 0, 2, 4 (except 4.4 "raise layer 7 to four runners")
and 6.1-6.3. Results and the as-built design are in `docs/ND5000-MULTI-CPU.md`.

Changed from the plan:

- 2.1 was built as a per-CPU store bound to the stepping thread, with the
  process-wide `g_trap_state` kept as the default for standalone nd500x.
- The mutex around TSET was not enough. The four-CPU test deadlocked on a lost
  release; TSET is now one atomic exchange on both CPUs, and aligned 16/32-bit
  stores are single host stores. Decided by Ronny after the ND-500 Reference
  Manual section 16.3 was checked.
- ThreadSanitizer found the runaway-trap counter in `raise_trap` shared between
  CPUs; it is thread-local now.

Open: 1.3, 1.4, 2.6 (WASM build not rebuilt), phase 3, 4.4, phase 5 (SINTRAN),
6.4 (`CLAUDE.md` has no ND-5000 section to amend), 6.5, phase 7.

11-OCT-2026, later. Phase 5.1 and 5.5 run: one-CPU and four-CPU SINTRAN boots on
scratch copies both run CPU-STAT to `MON 0B`. With four CPUs SINTRAN used only
station 070B. 5.2 and 5.3 are blocked on finding out how SINTRAN is told to
start more than one ND-5000.

12-OCT-2026. The 11-OCT entry above is superseded: SINTRAN uses all four
stations once a terminal gives `CHANGE-CPU n`, so 5.2 and 5.3 are no longer
blocked. Done and measured, details in `docs/ND5000-MULTI-CPU.md`:

- `X5PRO`, real CPU time in `PROCESS-STATUS`, and the kick that takes a running
  process off its CPU, from the ND-100 and from another ND-5000 (`684b0b2`,
  `b3007e6`).
- A CPU takes only messages assigned to it (`5CPUN`); a queue walk ends when a
  message has started a process; a process's state outside the context block
  is kept per process for all CPUs (`ee16986`, nd500x `d309ee5`).
- Four users starting LED on four CPUs at once: 5 runs, 20 of 20.

Still open: the microprogram's take-over of another CPU's message
(MICRO-5800-B30 015155-015174) is not modelled; open questions 1 and 3 above.
