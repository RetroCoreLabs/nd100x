# Several ND-5000 CPUs on one ND-100

How nd100x runs up to four ND-5000 CPUs against one ND-100, what is shared,
what is per CPU, and how it was tested. Paths are relative to the repository
root; `nd500x/` is the nd500x checkout the build uses.

Evidence tags: `[MANUAL]` quoted from a Norsk Data manual, `[SOURCE]` read in
the source named, `[MEASURED]` observed by running the test named,
`[UNVERIFIED]` not checked.

## The limit: four

| Source | Limit |
|---|---|
| ND-5900 sales material (NDInsight `History/machines/systems/ND-5900.md`) | Models 2, 3 and 4 = two, three, four CPUs |
| SINTRAN M06, `LN5DEST = 073B` (NDInsight `SINTRAN/ND5000/CARVE-ANSWER-ND5000-ACTIVATION-WORKFLAG.md`) | stations 070B..073B |
| SINTRAN L07, `LN5DEST = 077B` (NDInsight `SINTRAN/CARVING-HANDOFF.md`) | stations 070B..077B |
| Octobus station table, ND-05.020.01 T329 | 070B..076B, seven slots |

The configuration accepts `[nd5000.1]` to `[nd5000.4]`, stations 070B..073B
(`src/machine/machine_config.h`, `MC_ND5000_MAX_CPUS`). The bridge and the bus
library keep the octobus's seven slots (`src/machine/mfbus_bridge.c`,
`MFBUS_MAX_ND5000`); the cap is a configuration rule, not a bus rule.

## Configuration

```ini
[mfbus]
size      = 8
base_page = 04100B

[controller.octobus.0]
enabled = yes

[nd5000.1]
enabled = yes          ; station defaults to 070B + (slot - 1)

[nd5000.2]
enabled = yes          ; 071B

[nd5000.3]
enabled = yes          ; 072B

[nd5000.4]
enabled = yes          ; 073B
```

Rules `[SOURCE src/machine/machine_config.c]`: slot 1..4; a duplicate slot or a
duplicate station is an error; a station outside 070B..073B is an error;
`base_page` must be the same for every CPU and for `[mfbus]`; an `[nd5000.N]`
without `[mfbus]` is refused.

## Three numbers per CPU

`nd500x/src/ndbus/ndbus_cpunum.h` is the only place they are converted.

| Number | Range for four CPUs | What it indexes |
|---|---|---|
| Station | 070B..073B (56..59) | the octobus address |
| CPUNO | 1..4 | the mailbox extension block |
| X5CPU (station form) | 0..3 | the context block of a bare start |

In a mailbox MESSAGE, `X5CPU` is the PROCESS number, not the station
(`src/machine/mfbus_bridge.c`, comment "X5CPU IS THE PROCESS, NOT THE
STATION"). How SINTRAN numbers processes across several CPUs is `[UNVERIFIED]`.

## What is shared and what is per CPU

Shared by the ND-100 and every ND-5000:

- the MFbus pool - one byte array, no private RAM per CPU;
- the bus mutex (`ndbus_lock`), taken by every test-and-set and by the mailbox
  and octobus queue code;
- the servicer engine lock (`ndbus_engine_lock`);
- the bitmap of pages a translation walk has read (`g_nd500_xlat_bm`);
- read-only settings (`nd500_settings()`).

Per CPU:

- machine, registers, runner thread, servicer, context block, process table
  (`MfbusCpuSlot` in `src/machine/mfbus_bridge.c`);
- the translation cache, with a cross-CPU invalidation flag
  (`nd500x/src/cpu/nd500_tlb.h`);
- the trap store - bound to the stepping thread at the top of every step
  (`nd500_trap_bind_thread_state`, `nd500x/src/cpu/cpu.c`);
- the diagnostic PC ring and the runaway-trap detector (thread-local);
- the TSET spin damper.

## Memory access rules

Ordinary loads and stores take no lock on either side. Bytes go through
relaxed-atomic accessors. A naturally aligned 16- or 32-bit store is ONE host
store (`ndbus_pool_w16` / `_w32` in `nd500x/src/ndbus/ndbus_pool.h`); an
unaligned one is a byte at a time and may tear, as on the hardware
(ND-10.004.01 T21-T22).

### Test-and-set

`[MANUAL]` ND-500 Reference Manual ND-05.009.4, section 16.3: "lock / read
operand and set status bits / set operand to all ones / unlock", "performs the
two necessary memory accesses uninterruptible by other processors".

A read followed by a write under a mutex does not meet that: a semaphore is
released with an ordinary store, which takes no mutex. `[MEASURED]` with four
CPUs on one byte semaphore the guarded counter stopped at 35 of 80004 and three
CPUs spun forever, the cell set with no owner.

So the test-and-set is one host atomic exchange:

- ND-500 `TSET` (`nd500x/src/cpu/instructions/CONTROL/Tset.c`): its store runs
  in the bus layer's exchange mode (`nd500_bus_exchange_begin` / `_end`,
  `nd500x/src/machine/io.c`) and the status bits come from the value the
  exchange returned.
- ND-100 `TSET` and `TSETP` (`src/cpu/cpu_instr.c`): a word write into a shared
  bank while the test-and-set lock is held is the bank's exchange
  (`mms_tset_take_old`, `src/cpu/cpu_mms.c`; `mfbus_bank_exchange`,
  `src/machine/mfbus_bridge.c`). Local ND-100 RAM is unchanged.

Limits, stated:

- An H or W cell that is NOT naturally aligned is exchanged a byte at a time.
  It is exact against other test-and-sets (the mutex) but not against a store
  in flight. Whether real software does this is `[UNVERIFIED]`.
- The manual's format line says `BY TSET`; its instruction index lists TSET
  under `W`. The emulator accepts BY, H and W. Which is right is `[UNVERIFIED]`.
- If the store of a TSET does not reach memory with exactly the operand's
  width (a fault, or the MMU writing a page-table bit through the same path),
  the instruction falls back to the value of its earlier read.

### Spinning guests

A guest waiting on a semaphore burns a host core. After 64 consecutive failed
TSETs at the same instruction address the CPU's host thread yields
(`mfbus_tset_result`, `NDBUS_SPIN_YIELD_THRESHOLD`).

## Tests

| Test | What it shows |
|---|---|
| `tests/test_machine_config.c` | four sections load on 070B..073B; slot 5 and station 074B are refused |
| `tests/test_mfbus_bridge.c`, "FOUR ND-5000 CPUs" | four real ND-500 CPUs on four host threads, one TSET semaphore, a guarded `W INCR`: the counter is exactly four times one CPU's count |
| `tests/test_mfbus_bridge.c`, "THE ND-100 TSET'S WRITE" | a word write into the window during a test-and-set is an exchange and returns the old word |
| `nd500x/test/test_ndbus.c` layers 3 and 7 | the bus helpers under four threads, two mock CPUs on threads |
| `tests/test_mfbus_bridge.c`, "A MESSAGE FOR A BUSY CPU WAITS" | a 3START (23B) and a 3MONCO (24B) for process 1 arrive while the CPU executes another process: the message reads WAITING for 50 ticks, and is served under its own function code once the CPU has stopped |
| `nd500x/test/test_wext_clock.c` | `WEXT`, `REXT`, `CLINIT`, `CLREAD` through `nd500_cpu_step()`: all four registers, K for a refused word, for devices 1, 8, 9 and for no device installed, the privilege trap, the microsecond count, one clock per CPU |

`[MEASURED 11-OCT-2026]` nd100x `ctest`: 89 of 89. nd500x `ctest`: 44 of 44,
including the instruction conformance corpus. The four-CPU test under
ThreadSanitizer (`-fsanitize=thread`, run with `setarch -R` because the
sanitizer rejects the default address-space randomisation): 245 checks, 0
failed, 0 warnings.

The four-CPU test program uses only encodings that are rows of
`nd500x/test/nd500-conformance.json`. It measures how many passes one CPU makes
before it trusts the four-CPU total.

## SINTRAN boot

`[MEASURED 11-OCT-2026]` SINTRAN III VSX/500 L on scratch copies of
`ND5000.IMG`, `SCSI0.IMG` and `FLOPPY.IMG`, binary `nd100x 1.0.22
(dc401d1-dirty)`. Driven over `--pipe` with `tools/nd100x_expect.py`: log in as
SYSTEM, `ND-500`, `PLACE-DOMAIN CPU-STAT`, `RUN`, `LIST-ACTIVE-PROCESSES`.

One CPU (`[nd5000.1]`): CPU-STAT printed its fields (CPU number 100, CPU type
2, Instruction set 2, Microprog vers. 0, System type 110, Operating system 5,
Version L, Revision 0) and reached `MON 0B`. No lock timeout.

Four CPUs (`[nd5000.1]`..`[nd5000.4]`): the same session gave the same result.
All four stations were on the bus and each took an octobus emergency
`241B MASTER CLEAR` / `242B CONTINUE ACCP` before SINTRAN reported RUNNING.
After that every control-store load, swapper load, mailbox message and monitor
call (366 of them) was on station 070B. Stations 071B, 072B and 073B received
nothing further and executed nothing.

`[MEASURED]` with the per-destination frame log in `mfbus_card_transmit`
(`src/machine/mfbus_bridge.c`): SINTRAN addresses all four. Each of 070B..073B
receives the same 13 frames - two at boot (`0x..A1`, `0x..A2`), then, when the
ND-500 monitor is entered, one multibyte message (`0x..33`, payload bytes
03 07 0E 01 03 00 00 00 00) and one request (`0x..23`) that the station answers
with five frames. The four exchanges are identical apart from the station
number. Station 070B then gets further messages (control-store load onward);
071B..073B get none. SINTRAN also probes stations 2B..6B, where nothing answers.
What the 13 frames mean was not decoded here.

So SINTRAN finds all four CPUs and talks to each; it then gives work only to
the first.

### Choosing the CPU: `CHANGE-CPU`

The ND-500 monitor on this pack (Version J04) lists two CPU commands in its
own `HELP` that the Loader Monitor manual ND-60.136.04A does not describe:

```
SET-CPU-STATUS  <CPU Number: >,<Image: >,<Save: >,<Status: >
CHANGE-CPU  <CPU Number: >
```

`[MEASURED]` four CPUs configured, then `ND-500`, `CHANGE-CPU 2`,
`PLACE-DOMAIN CPU-STAT`, `RUN`: the monitor printed "Loading Control Store",
"Loading Swapper", "Allocating memory - 7342B pages", and every one of the 366
monitor calls, including the final `MON 0B`, was on station 071B. Station 070B
executed nothing in that session. CPU-STAT printed the same fields as on CPU 1.

So CPU numbers in `CHANGE-CPU` are 1-based (2 = station 071B), a session
without the command uses CPU 1, and a second ND-5000 runs a real program under
SINTRAN from control-store load to exit.

### Two CPUs at the same time

`[MEASURED 11-OCT-2026]` four CPUs configured; console terminal on CPU 1, a
telnet terminal (Terminal 39) on CPU 2 with `CHANGE-CPU 2`; both run
`PLACE-DOMAIN CPU-STAT` and are sent `RUN` at the same moment, twice.

Result: CPU-STAT printed all nine fields on both terminals in both rounds.
Station 070B reached `MON 0B` three times and station 071B twice, no fatal
error, no refused message. The swapper, on CPU 1, executed `WEXT` 50 times.

THE PROCEDURE MATTERS. The console must start the swapper, run once and `EXIT`
the ND-500 monitor before another terminal enters it; and `SET-AVAILABLE` must
be given before another terminal can log in at all. Without the `EXIT` the
second terminal's work fails in ways that look like emulator faults.

Getting there needed four things, each found by a failing run:

1. `WEXT` / `REXT` (`FFEC`-`FFEF` / `FFE8`-`FFEB`, ND-500 Reference Manual
   ND-05.009.4 sections 16.28-16.29, "'87 extension", "Device = 0 : OCTO-bus /
   ACCP"). The swapper sends an octobus frame to the other CPU with `W3 WEXT`
   as soon as a process is placed there - measured frame `0xB943`, a kick 3
   (CLRKICK) to station 071B. The register's low 16 bits are the frame, as in
   the microcode's own answer interrupt (MICRO-5800-B30 `010571`,
   `D,SPEC,AIB`). `nd500x/src/cpu/instructions/SYSTEM/Wext.c`, bridge hook
   `mfbus_ext_write`. `REXT` answers "nothing read": there is no frame queue
   for the guest, and no run has executed it.
2. `CLINIT` / `CLREAD` (`FF1E` / `FF1F`, sections 15.17-15.18): the CPU's
   local microsecond clock, which the swapper reads right after the `WEXT`.
   `nd500x/src/cpu/instructions/SYSTEM/Clock.c`; the clock is the host's.
3. A message for a process that arrives while the CPU is executing another
   process WAITS (ND-05.017.01 Appendix A.7: the microprogram takes one message
   at a time). The bridge used to refuse it, and SINTRAN reported "Fatal
   internal system error" or "CPU locked". `mfbus_defer_message` and
   `mfbus_release_deferred` in `src/machine/mfbus_bridge.c`. This is needed
   for two users on ONE CPU as well, which now also works.
4. Each station uses its own mailbox extension block: SINTRAN patches control
   store cell 025B (`SAMSON_CPU`) to 1 for station 070B and 2 for 071B
   (measured; logged by `ndbus_nd5000_mailbox_from_control_store`).

Limits: a process that never stops (no trap, no monitor call) keeps its CPU,
and a waiting message waits with it - the preempting kick is not modelled
here. `CHANGE-CPU 3` and `4`, and `SET-CPU-STATUS`, are `[UNVERIFIED]`.
Telnet terminals lose the first character of each line inside the ND-500
monitor (worked around with a leading space; cause not found).

## Not yet done

- RetroCore has WEXT, REXT, CLINIT and CLREAD in its functional `CpuND500`
  with a unit test, but that port has not been run under SINTRAN, and the
  waiting-message rule is not ported there.
- A CPU that stops on a trap nobody can be told about keeps the trap pending
  and does not run again until reset (an ACCP CPURES on the machine).
- About 60 function-local `static` variables remain in
  `nd500x/src/cpu/cpu.c`, `nd500_mmu.c`, `instruction_helpers.c` and
  `nd500x/src/machine/io.c`. Those seen are diagnostics; they have not all been
  read. ThreadSanitizer reports only what a test executes.
- `ADRZERO` is one value for all CPUs in the configuration; whether real
  systems ever differed per CPU is `[UNVERIFIED]`.
- WebAssembly has no host threads: several CPUs are pumped from the main loop.
  That build was not rebuilt or run for this work.
