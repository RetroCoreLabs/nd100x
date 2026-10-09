# Ethernet II differential trace format (plan rule R6)

Both the RetroCore C# card and the nd100x C card write this format, so
`tools/ethport/trace_diff.py` can compare them line by line.

## Why event order and not time

RetroCore steps its 68000 one clock cycle per ND-100 tick
(`Emulated.HW/ND/CPU/NDBUS/NDBusEthernetII.cs:1044`,
`CPU68K_CYCLES_PER_ND100_TICK = 1`, loop at :1065-1077). Musashi executes
whole instructions (`m68k_execute(cycles)` runs until the cycle budget is
spent, finishing the current instruction). The tick at which a 68000 bus
cycle happens therefore cannot match between the two. The comparison is
done on the ORDER of events in separate streams, with the tick kept only
as information.

## Line format

ASCII, one event per line, fields separated by one space:

    <stream> <seq> <tick> <EVENT> <field>=<value> ...

- `stream`: `ND` (events seen at the ND-100 side of the card) or `M68K`
  (events on the 68000 side) or `NET` (frames at the host backend).
- `seq`: decimal, increments by 1 per line within the stream, from 0.
- `tick`: decimal ND-100 tick count since card creation (informational,
  NOT compared by default).
- `EVENT` and fields: from the table below; numbers are lower-case hex
  without `0x`, fixed field order as listed.

| Stream | EVENT | Fields | Emitted when (C# location) |
|---|---|---|---|
| ND | IOX_R | reg=<offset> val=<16-bit> | end of `NDBusEthernetII.Read` |
| ND | IOX_W | reg=<offset> val=<16-bit> | start of `NDBusEthernetII.Write` |
| ND | IDENT | lvl=<level> code=<returned code, 0 = not this card> | end of `NDBusEthernetII.IDENT` |
| ND | INT | lvl=<level> on=<0/1> | the card's ND-100 interrupt bit differs from the previous tick; sampled once at the end of every `Clock()` |
| M68K | RESET | (none) | every `cpu.SetIrqFlag(IrqType.RESET)` (card creation and reset falling edge) |
| M68K | R8 | a=<address as passed> v=<byte> fc=<function code> | every byte read through `NDEthernetMemory.ReadMemory` (68000 accesses and the F8xxxx window) |
| M68K | W8 | a=<address as passed> v=<byte> fc=<function code> | every byte write through `NDEthernetMemory.WriteMemory`, emitted before the write |
| M68K | BERR | a=<address> rw=<r/w> fc=<function code> | `MemoryMap_OnBusError` |
| M68K | IRQ | lvl=<1-7> on=<0/1> | every call that sets or clears a 68000 interrupt level (`CpuSetIrq`) |
| M68K | IACK | lvl=<level> type=<AV/SP/VEC> vec=<vector number field> | end of `Cpu_OnInterruptAck` |
| M68K | DMA_R | a=<address & 0xFFFFFF> v=<16-bit> ok=<0/1> | LANCE word read through the card's `DmaIn` lambda (ok=0: out of DRAM, MERR raised) |
| M68K | DMA_W | a=<address & 0xFFFFFF> v=<16-bit> ok=<0/1> | LANCE word write through the card's `DmaOut` lambda, emitted before the write |
| NET | TX / RX | len=<n> sha1=<first 12 hex of SHA-1> | not yet emitted (Phase 6) |

Word and long 68000 accesses appear as their byte accesses, in the order
RetroCore makes them (`MachineMemory.cs:405-421`: byte at `address`, then
`address+1`; long = word at `address`, then word at `address+2`).
LANCE and MFP register accesses are memory-mapped and therefore already
appear as R8/W8 lines; there are no separate chip events.

Not emitted yet: INSN (per-instruction PC). Instruction fetches are
included in R8 (RetroCore reads opcodes through the same `ReadMemory`;
whether it does so for every fetch is unverified) - fc separates program
(2/6) from data (1/5) if `cpu.regs.FunctionCode` is accurate at each
access, which is unverified.

## Comparison rules (`trace_diff.py`)

1. Each stream is compared separately, in seq order, ignoring `tick`.
2. `INSN` lines are compared only with `--insn`.
3. The first differing line is reported with 20 lines of context from both
   files, plus its tick on both sides.
4. Any difference is a failure. There is no tolerance setting; a known,
   accepted difference must be written down as a rule in this file, with
   Ronny's approval, and implemented in `trace_diff.py` as a named filter.

Accepted differences: none yet.

## Predicted problem (not yet measured)

Because the two 68000 models run at different speeds relative to the
ND-100, a firmware polling loop (68000 reading a mailbox until the ND-100
writes it, or the ND-100 polling STATUS) will run a different number of
times on each side. The streams will then differ in the number of
identical repeated reads even when both cards behave correctly. If the
first lockstep runs show this, the proposed rule is: collapse a run of
identical consecutive R/IOX_R lines (same address, same value) into one
line with a count, and do not compare the count. That rule is NOT active
until Ronny approves it.
