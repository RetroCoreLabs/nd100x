# Ethernet II port - phase log

Evidence for every todo marked done in `docs/ETHERNET-II-PORT-PLAN.md`
(plan rule R1). Paths are relative to the nd100x repo root; RetroCore paths
are relative to `$RETROCORE_DIR`.

RetroCore source used for the port: commit
`935163feb549a7edd3f3595589efa66d70a6e172` (Mon Oct 5 17:21:20 2026 +0200).
`git status` on `Emulated.HW/ND/CPU/NDBUS`, `Emulated.HW/AMD/LANCE`,
`Emulated.HW/Motorola/MFP`, `Emulated.HW/Common/Network` was clean on
09-OCT-2026. Inventory row ids contain C# line numbers, so they are only
valid for this commit; moving RetroCore means regenerating the inventory
and re-checking the matrix.

## Phase 0

### 0.1 C# inventory - DONE (09-OCT-2026)

Tool: `tools/ethport/cs_inventory/` (Roslyn, Microsoft.CodeAnalysis.CSharp
4.14.0, .NET 10.0.112). Script: `tools/ethport/make_inventory.sh`.
Output: `docs/ethernet-port/cs-inventory.csv`, 1306 rows over 19 files.
The tool exits non-zero on any C# parse error; it reported none.

Cross-check of `case` labels (inventory vs
`grep -cE '^\s*case\s[^:]*:|^\s*default\s*:'`):

| File | grep | inventory |
|---|---|---|
| Emulated.HW/ND/CPU/NDBUS/NDBusEthernetII.cs | 55 | 55 |
| Emulated.HW/AMD/LANCE/Am7990/Am2990Lance.cs | 21 | 21 |
| Emulated.HW/Motorola/MFP/MC68901/MC68901MFP.cs | 64 | 64 |

Still open for 0.1: Ronny's hand comparison of 3 random methods.

### 0.2 matrix_check - WRITTEN, not yet exercised on a matrix

`tools/ethport/matrix_check.py`. With no `TRACE-MATRIX.csv` it fails with
"1306 inventory rows unmapped" (expected).

### 0.3 stub_check - DONE

`tools/ethport/stub_check.py`. Self-test on a fixture with five functions
(normal, empty body, `return false;` body, marked trivial body, TODO
comment): reported exactly the empty body, the `return false;` body and
the TODO; accepted the normal and the marked function.

### 0.4 const_check - WRITTEN, not yet exercised (no C constants yet)

`tools/ethport/const_check.py`.

### 0.5 test_parity - DONE

`tools/ethport/test_parity.py`, C# tests listed by Roslyn
(`cs_inventory --tests`), scope in `docs/ethernet-port/TEST-SCOPE.txt`
(22 files; 8 left out with reasons in the file). Snapshot of the list:
`docs/ethernet-port/cs-tests.csv`.

Result: 391 C# test methods. Cross-check against
`grep -cE '^\s*\[(Test|TestCase)\b'` over the same files = 394; the
difference is exactly the 3 extra `[TestCase]` rows on
`TestNDBusEthernetII.Test_Constructor_ThumbwheelSettings` (4 cases).
1 test is `[Ignore]`
(`Nd100EthernetIILanceInterruptAckTests`), 28 are `[Explicit]`
(class-level and method-level).

A first line-based version of the parser found 389 and missed tests with
multi-line attributes (e.g. `Nd100EthernetIILanceInterruptAckTests.cs:97`);
it was replaced by the Roslyn listing.

### 0.6 Trace format - DONE (spec)

`docs/ethernet-port/TRACE-FORMAT.md`. Facts it rests on:
- RetroCore steps the 68000 one CLOCK CYCLE per ND-100 tick
  (`NDBusEthernetII.cs:1044`, loop :1065-1077); Musashi runs whole
  instructions. Streams are compared in event order, tick ignored.
- A 68000 word access in RetroCore is two byte calls, `address` then
  `address+1`; a long is two word accesses
  (`Emulated.HW/Common/Memory/MachineMemory.cs:405-421`). The trace
  therefore records 68000 bus traffic as bytes (R8/W8). The C port must
  split Musashi's 16/32-bit callbacks into byte calls in the same order,
  because I/O registers see each byte access separately.
- Predicted (unmeasured) problem with polling loops recorded in the spec;
  no filter is active.

### 0.7 Trace emitter in RetroCore + trace_diff - DONE (09-OCT-2026)

RetroCore changes (approved D3, NOT committed in RetroCore - Ronny's call):
- new `Emulated.HW/ND/CPU/NDBUS/EthernetII/EthIITrace.cs`
- `Emulated.HW/ND/CPU/NDBUS/NDBusEthernetII.cs`: `Trace` property; hooks
  at IOX read/write, IDENT, ND INT line (sampled once per Clock), 68000
  byte reads/writes (`ReadMemory`/`WriteMemory` wrap
  `ReadMemoryUntraced`/`WriteMemoryUntraced`), bus error, every 68000
  IRQ level change (all 5 `cpu.InterruptControllerSetInterrupt` call
  sites go through `CpuSetIrq`), IACK result, RESET, LANCE DMA. Own tick
  counter `_traceTick` (one per `Clock()`): RetroCore's `_tick` only
  advances while Device logging is on (`Watch68KPc` returns at its first
  line otherwise), so the first reference trace had tick 0 everywhere.
- new `Emulated.Tests/NDBusDevices/EthIITraceTests.cs`: 3 tests checking
  exact trace lines (IOX write+read, IDENT, R8/W8) - all pass
  (`dotnet.exe test --filter FullyQualifiedName~EthIITraceTests`:
  Passed 3, Failed 0); 1 `[Explicit]` test writing a reference boot trace.

Snapshot rule: the port tools read `build/ethport-cs-snapshot/` made by
`tools/ethport/snapshot_retrocore.sh` from commit 935163f, never the
RetroCore working tree (which now carries the hooks and shifted line
numbers). Checked: all 47 snapshot files are identical to the working tree
except `NDBusEthernetII.cs`, and the snapshot of that file is identical to
the copy saved before the hooks were added. The inventory regenerated from
the snapshot has the same 1306 ids as the first one.
(First snapshot run extracted only one file: `git.exe` inside a
`while read` loop consumed the file list from stdin. Fixed; the script now
fails on any empty extraction.)

First reference trace (diagnostic firmware `pioc-ether-68k-code.srec`,
control writes 0x30, 0x20 after 10 ticks, 0x01 after 20, then 200 000
ticks): 800 365 lines - 3 IOX_W, 1 RESET, 400 359 R8, 400 002 W8.
Reset vectors fetched at tick 10 (reset released), fc=6. Measured: about
4 byte accesses per tick with `CPU68K_CYCLES_PER_ND100_TICK = 1`, i.e.
RetroCore's 68000 does far more bus traffic per clock than a real 68000
(4 clocks per bus cycle). Relevant to D5; meaning not yet analysed.

`tools/ethport/trace_diff.py`: self-test with three fixture traces -
identical content with different ticks -> exit 0; one changed value and
one missing line -> both reported, exit 1.

### 0.8 C test harness - DONE (09-OCT-2026)

`tests/ethernet/eth_test.h`, `eth_test_main.c`, `CMakeLists.txt` (added
from `tests/CMakeLists.txt` when `ND100X_ENABLE_ETHERNET`). Tests are
`ETH_TEST(CsClass, CsMethod)`; each registers itself through a constructor
function, the runner fails a test that makes no checks, and `--list`
prints the registered set (test_parity `--binary=` compares it with the
source). Built in a separate tree `build_eth/` (Ronny's `build/` untouched).
Self-tests: `eth_runner_pass` passes; `eth_runner_must_fail` (one failing
CHECK_EQ, one test without checks) reports both and exits 1 - registered
`WILL_FAIL`. `ctest -R eth_runner`: 2/2 passed. Musashi (`musashi`
target) compiled for the first time here without errors.
The fake ND-100 bus for device tests is NOT written yet: what the card
needs from it (DMA? direct DRAM window?) is todo 5.3.

### 0.9 mutate.py - DONE

`tools/ethport/mutate.py`. Self-test on `selftest_must_pass.c`: 6 sites,
6 mutants, 6 killed.
Two bugs found and fixed during the self-test:
1. octal literal `052163` was mutated to decimal `52164` (now `052164`);
2. after restoring the original file, its timestamp was older than the
   mutant's object file, so the final rebuild did nothing and the mutant
   binary (`1 + 1 != 2`) was left in `build_eth/bin` - the next
   `eth_runner_pass` run failed. Fix: touch the restored file, rebuild,
   and fail the script if the unmutated test does not pass afterwards.
   Re-run: 6/6 killed and the binary passes afterwards.
`stub_check.py` also missed empty `ETH_TEST` bodies (its function regex
needs a return type); fixed, and checked on a copy of
`selftest_must_fail.c` (flagged). `selftest_must_fail.c` itself is
exempt, with the reason in `EXEMPT`.

### 0.10 eth-check - DONE

`make eth-check` -> `tools/ethport/eth_check.sh`. Current result: build
OK, runner self-tests OK, stub_check OK, const_check OK (0 constants),
matrix_check FAILED (no matrix yet), test_parity FAILED (391 C# tests,
0 ported). Both failures are the expected state before Phase 1.

### Phase 0 status

Done except the 0.1 hand check by Ronny (3 random methods in
`docs/ethernet-port/cs-inventory.csv` against the C# source).

## Phase 1

### Scope correction (09-OCT-2026)

`NDBusEthernetIIShared.cs` removed from the port scope: the 68000 card
derives from `NDBusDeviceBase` (`NDBusEthernetII.cs:483`); a grep over the
whole RetroCore tree finds `NDBusEthernetIIShared` only in itself, the HLE
card, 3 HLE test files and `ND100Machine.cs`. Inventory 1306 -> 1217 rows
(the 89 Shared rows). `NDBusDeviceBase.cs` added to the snapshot as the
reference for base IDENT / interrupt-bit behaviour.

### 1.2 / 1.3 Matrix - DONE (planned targets)

`tools/ethport/plan_matrix.py` gives every inventory row a PLANNED C file
and symbol by a fixed naming rule (docstring). 1217 rows, 0 unmapped.
`matrix_check.py`: OK; with `--final`: 1217 errors (all still PLANNED).

### 1.6 RetroCore's own results - PARTLY DONE

Run 09-OCT-2026 on RetroCore 935163f + trace hooks, filter = the 22 test
classes of `docs/ethernet-port/cs-tests.csv`, saved in
`docs/ethernet-port/RETROCORE-RESULTS.csv`:
- `Emulated.Tests`: 178 test cases = 197 in-scope methods + 3 extra
  `[TestCase]` rows - 22 `[Explicit]`. 177 Passed, 1 NotExecuted
  (`InterruptAck_LeavesTheCpuMatchingWhatTheLanceHolds`, the `[Ignore]`
  "NOT SOLVED" test).
- `Emulated.Tests.Chips`: first run could not load the assembly
  (`FileNotFoundException: Microsoft.Extensions.DependencyModel,
  Version=8.0.0.2`; the DLL was absent from its bin, and the copy in
  `Emulated.Tests/bin` is not in that project's deps.json - a stray file,
  assembly version 8.0.0.2). Ronny's decision: add the package reference.
  `Emulated.Tests.Chips.csproj` now has `Microsoft.Extensions.DependencyModel`
  8.0.2 (uncommitted in RetroCore). Re-run: 188 test cases (194 methods -
  6 `[Explicit]` pcap tests), 186 Passed, 2 FAILED in RetroCore itself:
  - `MC68901MFPTests.Test_Timer_TDR_NotReloadedInEventCountMode`:
    "TMC must NOT reload when TDR written in event count mode",
    expected 3, was 10.
  - `Am7990LanceTests.Test_SunOS_RxAndInterrupt`: "MCNT should be 64",
    was 68.
  The C port copies RetroCore's behaviour, so the ported versions of these
  two tests are expected to fail the same way; fixing the behaviour is a
  separate decision for Ronny.
Total recorded: 366 test cases (178 + 188): 363 Passed, 2 Failed,
1 NotExecuted. Parsed with an XML parser (a first regex version captured
only 6 of the 188 chip results because passing results are self-closing
elements).
- `[Explicit]` harnesses (28) not run: they need disk images / pcap.

## Phase 2

### 2.2 Musashi test suite - DONE, with findings (09-OCT-2026)

Musashi's driver `external/Musashi/test/test_driver.c:380` sets
`M68K_CPU_TYPE_68040` for every test, including the 68000 suite. Built out
of tree in the scratchpad (submodule untouched) twice: as shipped, and from a
scratchpad copy with the CPU type changed to 68000.
- 68040 core: 60/60 tests of `test/mc68000` pass.
- 68000 core: 59/60 pass. `move` fails: at PPC 0x10160 the test executes
  `cmpi.b #-71,(d16,PC)` (`m68k-elf-objdump` of `test/mc68000/move.bin`),
  which Musashi accepts only on 68EC020+ (`m68kops.c:11273`
  `CPU_TYPE_IS_EC020_PLUS`), so it raises illegal instruction and runs
  into the 0xDEADBEEF vectors. The test is not 68000-only code; this is
  not a Musashi 68000 defect.

### 2.4 Bus error - MEASURED, DECISION NEEDED

- Musashi `m68ki_exception_bus_error` (`external/Musashi/m68kcpu.h:1935-1968`)
  always pushes a 68010 format $8 frame (`m68ki_stack_frame_1000`; its own
  comment: "This is implemented for 68010 only!"), for every CPU type, then
  `longjmp(m68ki_bus_error_jmp_buf, 1)` out of the memory callback.
- RetroCore for `CpuType.MC68000` pushes a 7-word frame: PC(32), SR, IR,
  access address(32), information word (`Instructionset.Helpers.cs:792-803`,
  `995-1012`), with a PC value its own comment calls "approximate".
- The diagnostic firmware (`pioc-ether-68k-code.srec`) routes every vector
  to `trap #1` stubs at 0x2000 (vector 2 -> 0x2000); the trap #1 handler at
  0x2274 pops 4 words of the bus/address-error frame (0x2322-0x2334) and
  copies them plus `sp@(2)`, `sp@(4)` into the mailbox at 0x440+0x16+n,
  which the ND-100 reads. Frame layout differences are therefore visible to
  the ND-100.
- Musashi's double-fault path reads 0x00FFFF01 and halts
  (`m68kcpu.h:1946-1951`); a callback that pulses another bus error during
  that read recurses until the host stack overflows (seen with Musashi's
  own driver on the 68000 run). On the card 0xFFFF01 is in the F80000-FFFFFF
  DRAM alias (`NDBusEthernetII.cs` FindMemoryBank), so it would not fault there.

### 2.4 Bus error - DECIDED AND DONE (09-OCT-2026)

Ronny's decision: replace upstream Musashi with the fork
https://github.com/RetroCoreLabs/Musashi and patch the fork.
- Fork commit `328a672` (on the fork's master, parent = upstream 313ebf1):
  `m68ki_exception_bus_error` pushes the 68000 group 0 frame with
  Musashi's existing `m68ki_stack_frame_buserr` when the CPU type is 68000;
  68010+ unchanged. New API `m68k_set_bus_error_info(address, write, fc)`.
- `.gitmodules` url -> the fork, branch master. `tools/gen_musashi_ops.sh`
  re-run: `m68kops.c` byte-identical (the patch does not touch
  `m68k_in.c`); `MUSASHI_COMMIT.txt` now names 328a672.
- Test `tests/ethernet/test_musashi_buserr.c` (CTest `eth_musashi`):
  `move.b $800000,d0` on a 68000 -> SP drops 14, frame info 0x0015,
  address 0x00800000, IR 0x1039, PC 0x00000406, handler at 0x500 reached;
  on a 68010 SP drops 58 (format $8 unchanged). Both pass.
  The same program against unpatched Musashi was measured before the
  patch: 68010 frame on a 68000 (see 2.2/2.4 above).
- Open: the stacked PC. Patched Musashi stacks REG_PC (here 0x406, the
  address after the whole instruction); RetroCore stacks an approximation
  (`framePC = AccessAddress == PC ? PC - 4 : PC - 2`,
  `Instructionset.Helpers.cs:801`). Whether they agree for the firmware's
  real faults will show in the differential trace (W8 of the frame).

Musashi's suite is now in CTest: `eth_musashi_suite_<test>` for the 60
`test/mc68000` binaries with Musashi's own driver (68040 core, as shipped).
`ctest -R eth_musashi`: 61/61 passed.

### 2.1 Project Musashi configuration - DONE (09-OCT-2026)

`src/devices/ethernet/m68k/eth_m68kconf.h`, selected through
`MUSASHI_CONFIG_HEADER` -> `MUSASHI_CNF` (root `CMakeLists.txt`, ethernet
block). 68000 only (010-040 off), INT_ACK / FC / INSTRUCTION_HOOK with
card callbacks, TRACE on, ADDRESS_ERROR on (RetroCore raises AddressError,
`Instructionset.Helpers.cs:643-699`), RESET callback off (RetroCore leaves
`OnReset` unconnected, `NDBusEthernetII.cs:672-676`), SIMULATE_PD_WRITES
off (RetroCore writes every long high word first, `MachineMemory.cs:500-516`),
PREFETCH off (RetroCore prefetch model unverified). Every setting carries
its reason in the header.
Check: `nm libmusashi.a` shows `U eth_m68k_int_ack`, `U eth_m68k_set_fc`,
`U eth_m68k_instr_hook` and `T m68k_set_bus_error_info` - the header is in
effect. The compile command carries
`-DMUSASHI_CNF=\"/.../eth_m68kconf.h\"`.
Unverified: whether RetroCore's Cpu68K masks addresses to 24 bits (Musashi
does for a 68000). In the reference boot trace all 800 361 byte accesses
are below 0x1000000, so the question did not arise there.

### 2.5 Clock model (D5) - MEASURED FROM SOURCE

RetroCore (`Cpu68K.cs:515-526, 612-616, 757-795`): at `regs.Cycles == 0`
it checks interrupts, then executes one whole instruction and sets
`regs.Cycles = Instruction.Cycles`; every later tick only decrements.
`Instruction.Cycles` is the constructor argument `cycles`, default 0
(`Instruction.cs:364`); the only instruction given a value is NOP,
`cycles: 4` (`Instructionset.cs:342`, the single `cycles:` in the file);
nothing adds cycles at run time (the two `regs.Cycles +=` hits in
`Cpu68K.cs` are a comment and a commented-out line). With one 68000 clock
per ND-100 tick (`CPU68K_CYCLES_PER_ND100_TICK = 1`), the card therefore
runs ONE INSTRUCTION PER ND-100 TICK, NOP taking 4 ticks. This matches the
measured ~4 byte accesses per tick in the reference trace (a clear loop
writes 4 bytes per instruction).
A trapping instruction: the frame is built in the same tick
(`HandleTrap` in the catch, `Cpu68K.cs:537-575`); the handler's first
instruction runs on the next tick.
Port model (follows the C# exactly, no decision needed beyond "port as
is"): per tick, if not halted/reset and not stalled, call
`m68k_execute(1)` (one instruction) and stall 3 more ticks after a NOP.
To verify in Phase 3/5: that `m68k_execute(1)` runs exactly one
instruction, including when an interrupt is taken first and when the
instruction bus-errors.

## Phase 3 - 68000 interrupt glue (09-OCT-2026)

### Sources read
RetroCore `Cpu68K.Interrupts.cs` (controller: `pending_irq` mask,
`nmi_pending`, `_InterruptControllerEnabled`, `GetHighestPendingInterruptLevel`,
`CheckAndHandleInterrupts`), `Cpu68K.cs:515-604` (clock step, trap path),
`Cpu68K.cs:831-855` (RESET), `Registers.cs` `Clear()`,
`Instructionset.Helpers.cs:1493-1532` (`JumpToISR`). Musashi
`m68ki_exception_interrupt`, `m68k_execute`, `m68k_set_irq`.

### Fork changes made during Phase 3 (RetroCoreLabs/Musashi master)
- `32a04b5` Bus error: do not re-enter the execute loop without cycles.
  Before: after a bus error `m68k_execute(1)` also ran the handler's first
  instruction (the bus-error `setjmp` return fell into the do-while; the
  address-error return already checked). RetroCore runs it on the next
  tick. Test `Port__Musashi_BusError_HandlerStartsInNextExecuteCall`
  FAILS on the fork without this commit (PC 0x502, D1 1) and passes with it.
- `4188dc5` `m68k_is_halted()` / `m68k_is_stopped()` public queries.
`m68kops.c` regenerated after each: byte-identical.

### Code
- `src/devices/ethernet/eth_irq.c/.h` - clean-room controller.
- `src/devices/ethernet/eth_m68k.c/.h` - Musashi glue: per-tick step (one
  instruction per tick, NOP 4 ticks, interrupt check only when no
  instruction is in progress, also while halted), delivery through
  `m68k_set_irq` + int_ack callback, byte-split memory callbacks (word =
  A then A+1, long = word A then A+2), reset as RetroCore (gate on, D/A/USP
  cleared, PC 0 -> stopped, requests and NOP stall kept), bus error via
  `m68k_set_bus_error_info` + `m68k_pulse_bus_error`.
- `m68kdasm.c` dropped from the `musashi` library (unused; the core only
  references the disassembler inside disabled log macros - link verified).

### Tests: `tests/ethernet/test_eth_irq.c`, 34 tests, 110 checks, all pass
Behaviour ids I0-I13 (I3 source wiring belongs to the card, Phase 5),
NMI cases, halt line, bus error frame contents and timing, long read/write
order, argument checks.

### R8 mutation (all 113 sites of eth_irq.c + eth_m68k.c)
First run (sample of 60): 17 survived -> 6 real test gaps found and closed
(init argument check/return, bus error R/W bit and fc, long read high
word, out-of-range level, NMI only for level 7, clearing another level
keeps a queued NMI) plus a redundant static flag replaced by a local.
Full run after that: 13 survived -> 3 more gaps closed (USP reset, Musashi
must not retake a level on its own, select(NULL)).
Final full run: 113 mutants, 103 killed, 10 survived. The 10 survivors
make no observable difference, each checked by hand:
- `eth_m68k.c` reset `m68k_execute(0)` -> `(1)`: both only spend Musashi's
  reset cycles (RESET_CYCLES >= 1, `m68kcpu.c` m68k_execute start);
- `m68k_execute(1)` -> `(2)`: every 68000 instruction costs >= 4 cycles,
  so both run exactly one instruction;
- `int level = 0` -> `1`: only causes an extra `m68k_set_irq(0)`;
- pre-clear `m68k_set_irq(0)` -> `(1)` before delivering: Musashi still
  sees a change to the new level, and 1 -> 7 still makes level 7 an NMI
  (`m68kcpu.c:1061`, old level != 7);
- bus error write flag 1 -> 2: any nonzero value means write;
- `uint8_t vector = 0` -> `1`: the value is used only when the card answers
  VECTORED, and then the card sets it;
- `eth_irq.c` level bound `> 7` -> `> 8` and selection loop start 7 -> 8:
  bit 8 does not exist in the uint8_t mask.
(Two more survivors from the first full run, the disassembler read
functions, went away with `m68kdasm.c`.)

### R7 coverage (gcc --coverage, scratchpad build)
`eth_irq.c` 100% of 37 lines; `eth_m68k.c` 95% of 120 before the
disassembler functions were removed. Uncovered and why: the int_ack
callback's `s_cur == NULL` return and `eth_m68k_set_fc`'s NULL return -
Musashi calls them only after `eth_m68k_init` has set `s_cur`.

### Not done in Phase 3 (moved)
- 3.3 differential trace: needs the card (Phase 5) to produce comparable
  streams; the IRQ/IACK lines will be compared then.
- I14 RetroCore's runaway guard (100 consecutive traps -> CRASHED,
  `Cpu68K.cs:553-578`) is not modelled; Musashi has no such guard. Matrix
  row / decision for Ronny.

### House-rule note for Ronny
`eth_m68k_bus_error` lets Musashi `longjmp` out of the card's memory
callback back into `m68k_execute` (the only way Musashi aborts an
instruction). The C standard's rule 10.3 confines setjmp/longjmp to files
the project profile names and forbids longjmp out of a callback invoked by
another subsystem. The frames skipped hold no locks or allocations. Ronny
decides whether to list `eth_m68k.c` / the card memory files in the nd100x
profile as a documented deviation.

## Phase 5 notes (10-OCT-2026)

- Dead code in the C# card: `Am7990Lance` only calls `DmaIn`/`DmaOut`
  (Am2990Lance.cs:1020 on); it never raises the IOMemoryBase events
  `OnReadDMA`/`OnWriteDMA`, so `NDEthernetMemory.Lance_OnReadDMA`,
  `Lance_OnWriteDMA` and `RaiseLanceMemoryError` (the "out-of-range DMA ->
  MFP I5 / vector 107" path) never run. An out-of-range LANCE DMA goes
  through the `DmaIn`/`DmaOut` lambdas and calls `lance.SimulateMemoryError()`
  (CSR0 MERR). The earlier analysis claim "out-of-range DMA pulses MFP I5"
  was wrong. Matrix rows for those three methods: candidates for NOT_PORTED
  (dead code) - Ronny decides.
- The C# trace's DMA hooks were in that dead path (0 DMA lines in the first
  reference trace); moved into the `DmaIn`/`DmaOut` lambdas. RetroCore
  builds (0 errors).
- KMPIOC: SINTRAN K03 symbol lists give KMPM3=000001, KMPM4=000002,
  KMPM5=000004, KMECC=000010, KMPIO=000020 (e.g.
  SYMBOLS/K03/SYMBOL-1-LIST.SYMB.TXT:344-348; same in FILSYS, N500, RTLO
  symbol lists). RetroCore's card DRAM type 0x10 is right. nd100x had
  ND_MEM_PIOC = 0x02 (that is KMPM4); corrected to 0x10 in
  src/cpu/cpu_types.h (no other user). ND_MEM_MPM3 = 0x05 and ND_MEM_MPM4 =
  0x06 are also wrong (should be 0x01 / 0x02) but unused and outside this
  task - reported to Ronny, not changed. (The NDInsight note citing these
  values gives wrong line numbers for its source.)
- ND-100 side of the card DRAM (RetroCore): physical WORD address W is byte
  address W<<1 (CpuND100.MMS.cs ReadPhysicalMemory, ReadMemory16(pa << 1));
  the ND-100 bus is big-endian (CpuND100.cs:568), so word W = DRAM bytes
  2*(W - start) (high) and +1 (low) - the layout the 68000 sees. Window:
  bank*0x40*2048 bytes, bank = 16 + 4*thumbwheel (0x200000 for thumbwheel 0),
  512 KB. nd100x has backed memory banks (mms_memory_bank_register_backed,
  cpu_mms.c:1272) to plug the window in.
- MFP mutation run (all 482 sites of the 4 MFP files): 177 killed, 281
  survived, 24 no-build. RetroCore's MFP tests leave large parts untested
  (USART TX/RX state machines, timer pulse modes, ...). Survivors list:
  <scratchpad>/mutate_mfp.log. To be closed with Port tests after the TPE
  run (Ronny, 10-OCT-2026: TPE run first). One mutant (`while (b != 1u)`
  in mfp_is_bit_count_odd) hung the tests; Ronny approved ending that test
  process; mutate.py now has a 120 s per-run timeout (hang = killed).

### 5.x Card wired into nd100x (10-OCT-2026)

Code: `src/devices/ethernet/eth_iospace.c` (ETH_IOMem), `eth_memory.c`
(NDEthernetMemory), `device_ethernet.c` (NDBusEthernetII), all compiled
into the devices library when ND100X_ENABLE_ETHERNET; DEVICE_TYPE_ETHERNET;
CLI `--eth0=SPEC` (INI pair still to do). `RAM.Reset()` in RetroCore is
empty (RAM.cs:107), so a card reset keeps the DRAM.

DRAM window (Ronny, 10-OCT-2026: "make sure the memory read and write
actually use the shared memory in the ethernet controller and doesnt read
the local memory directly"):
- The ND-100 reaches the card DRAM only through a backed memory bank
  (mms_memory_bank_register_backed, ND_MEM_PIOC) whose callbacks read/write
  `EthCard.mem.dram`; the 68000 uses the same array. mms checks banks before
  local RAM (cpu_mms.c:1272-1284 read, 1343-1350 write).
- nd100x refuses overlapping banks. With the default 4 MB of local RAM the
  thumbwheel-0 window (byte 0x200000) lies inside local RAM; the first build
  only logged that and kept the card, so the ND-100 would have used local
  RAM at the window. Now the card is NOT created and the error names the
  range and says local memory must end below 0x200000 (--memory=2).
  RetroCore instead lets the card shadow local RAM (FindMemoryBank checks
  the card first); the real ND-110 run in the C# comment shows the card in
  bank 30B, i.e. separate from local memory.
- Test `tests/ethernet/test_eth_window.c` (CTest eth_window), through the
  real cpu/machine/devices libraries: ND-100 write -> card DRAM and visible
  to the 68000; 68000 write -> ND-100 read; MSB/LSB byte writes; last local
  word does not go through the window (window write counter = 1); 4 MB
  local RAM -> card refused. 5/5 pass.

### LANCE MCNT test fixed in RetroCore (Ronny, 10-OCT-2026)
`Emulated.Tests.Chips/Am7990LanceTests.cs` Test_SunOS_RxAndInterrupt now
expects MCNT = 64 + 4: the injected 64 bytes carry no FCS and the chip
counts the CRC in MCNT (Am79C90 data, Receive: "including the CRC bytes
(4 bytes)"; RMD3 MCNT = received message length). The code was right.
RetroCore run: Am7990LanceTests 54/54 passed. RETROCORE-RESULTS.csv updated
to Passed; the C port of this test must PASS (no expected-fail marker).
Uncommitted in RetroCore.

## 10-OCT-2026 - Phase 6: host networking, first live traffic

- Backends in src/devices/ethernet/net/eth_net.c: none, udp (RetroCore wire format),
  tcp listen/connect (RetroCore RETH framing), tap:IFNAME (Linux, nd100x addition).
  Checksum repair (eth_ipcsum.c) and the card glue (own-echo drop by source MAC,
  60-byte runt padding) as RetroCore NDBusEthernetII.AttachNetwork.
- Tests: test_ethernet (IpChecksumRepairTests 10/10), test_eth_net (Null 4 + 1 ignored
  pcap, Tcp 5, Udp 4, TAP 2), all on real sockets of the build host.
- WD0-SINTRAN-M.IMG: (SYSTEM)AIP-CONFIG:SYMB and AIP-HOSTS:SYMB moved from
  192.168.199.x (Windows ND-Loopback, RetroCore) to 192.168.210.x with ndtool --put;
  fsck 0 errors, same 6 warnings as before; original kept as WD0-SINTRAN-M.IMG.orig.
- Measured with nd100x --config nd100-eth.ini (net = tap:nd0, host 192.168.210.1/24):
  SINTRAN console "TELNET and TCP/IP in Ethernet II with Internet address
  192.168.210.40 started"; Linux ARP 192.168.210.40 -> 08:00:26:d2:00:00;
  ping 4/4, rtt 0.78-1.87 ms; TCP port 23 answered "Telnet Server D02 on c3
  (192.168.210.040) available." followed by ENTER / PASSWORD:.
