# Disc FIFO over/underrun (status bit 11) - facts, root cause, open questions

This is the background for BUGS.md **S1** (SMD) and **W1** (Winchester). In both,
status bit 11 never sets on an overrun, and DISC-TEMA FUNCTION test 5 reports
"Error after Read (overrun expected), Status Bit 13b remains 0" (and the same for
Write). DISC-TEMA writes status bits in octal, so its "13b" is bit 11.

Marks used below:
- **FACT** means it was read in the source named.
- **INFERRED** means it was worked out from facts and has not been measured.
- **NOT FOUND** means it was looked for and is not in the sources.

## 1. SMD controller (ND-11.020.01, cards 3043/3044)

- **FACT, what bit 11 is.** Sec 2.5 calls bit 11 "DMA channel error" and explains it as
  "FIFO over/underrun or ND-100 Bus error". Sec 3.7.1 makes bit 4 the OR of bits 5-13.
- **FACT, the FIFO.** Sec 3.1 says "An 8Kb FIFO". The card 3044 FIFO sheet (manual p.109)
  shows four IMS1420 RAMs (4K x 4 each) and an 8X60 FIFO controller with EMPTY,
  HALFFULL and FULL. That makes **4096 words** of 16 bits. Sec 3.1 adds that straps can
  set "a lower transfer rate between the FIFO and the main memory". The DMA request
  is picked by a 74LS151 from FASTREQ, DELREQ and HALFFULL (STRAP1/STRAP2).
- **FACT, where bit 11 comes from.** `SETSB11` is made by PAL 20H (347-00, a PAL16L8)
  from M0, EMPTY, FULL, FIFRD, FIFWR, START, REQSTRT, WCZ, SETSI and SETSO.
  **NOT FOUND:** the PAL equations and the default strap setting.
- **FACT, the timing figures.** Figs 10 and 11 give the order of the FIFO signals but no
  time scale.
- **FACT, the data rate.** Sec 1 says the card takes 10 and 15 MHz drives, and the sec 3.2
  servo clock is 9.677 MHz. A sector holds 512 words.
- **INFERRED, word time and slack.** A word passes every 1.65 us (10 MHz) or 1.07 us
  (15 MHz). A full 4096-word FIFO gives about 6.8 ms or 4.4 ms of slack.

## 2. Winchester controller (ND-11.015.01, cards 3041/3038)

- **FACT, the FIFO.** Sec 2.2: "16 words FIFO ... (Fairchild 9403)".
- **FACT, what sets bit 11.** Sec 4.2: on a write the card fills the FIFO from memory and
  stops at 16 words; on a read it requests memory as soon as the FIFO is not empty.
  "If the FIFO goes empty during write disk or full during read, status bit 11
  will be set." Sec 3.5 calls bit 11 "Fifo over/under-run or dma channel error".
- **FACT, the data rate.** Sec 4.3: "The standard ST506 transfer rate is 5 Megabits/second",
  and ESDI is 10 Mbit/s. **NOT FOUND:** the rate of the 8-inch drives on the 3038.
- **INFERRED, word time and slack.** A word passes every 3.2 us (1.6 us on ESDI). The FIFO
  gives about 51 us of slack (26 us on ESDI).

## 3. The bus

- **FACT, ND-100 (ND-06.015.02).**
  - A CPU memory reference holds the bus "typically 450 ns", a DMA transfer
    "typically 550 ns".
  - Sec 4.2: refresh comes first; between the CPU and DMA the priority is toggled.
  - Sec 6.2.3: the bus is given one cycle at a time, and a cycle lasts until BDRY.
  - Sec 6.6.2: an IOX with no BDRY times out after 10 us. Sec 6.2.3 says 15 us, so the
    manual disagrees with itself.
  - Fast DMA devices "should" use multiport memory "to avoid cycle stealing from the CPU".
- **FACT, ND-110 (ND-06.026).** The order is refresh, then the CPU when the last cycle was
  refresh, then DMA, then the CPU. Cycle times are as on the ND-100, and the IOX
  timeout is 15 us.
- **FACT, ND-120 (ND-06.031.1).** The memory on the CPU card "is also accessible to DMA
  Controllers on the same ND-100 bus". **NOT FOUND:** ND-120 arbitration figures.

## 4. DISC-TEMA

- **FACT, note 7.** In the test program description (ND-830005.3): "If the CPU and the
  controller have different DMA-channels (ports), you cannot provoke overrun (as in
  f.ex MPM-5 systems)". In the program text of DISC-TEMA-J02: "If the disc controller
  is NOT placed in the same crate as the CPU, it is not possible to force overrun".
- **FACT, BIGFUNC item 13 (ND-11.020.01 sec 5.4).** "Status bit 11 is checked by doing read
  with the instructions IOX 0 three times in the waiting loop, and by doing write
  with the instruction IOX 0 twice in the waiting loop."
- **FACT, the overrun item in DISC-TEMA-J02:TEST.** This is the file on FLOPPY.IMG,
  disassembled; addresses are word offsets in the file:
  ```
  036235 IOX 5            control word
  036237 IOX 7            word count, upper part 0
  036241 IOX 7            word count lower = 177000 octal (65024 words)
  036254 IOX 5            GO
  036255..036300 IOX 0    x 20
  036301 IOX 4            read status
  036302 BSKP ZRO 30 DA   finished?  -> leave
  036306 JMP 036255       back to the IOX 0 block
  ```
  The file has IOX 4, 5 and 7 as offsets that are added to the controller's base at
  run time.
- **FACT, measured: the IOX 0 go to device address 0, where nothing answers.**
  The measurement ran DISC-TEMA FUNCTION in nd100x on a copy of SMD0.IMG, unit 0,
  twice:
  - run 1 with `--log=smd:debug`;
  - run 2 with `--log=smd:debug,device:debug`.

  The SMD log for the read half shows, in order:
  - IOX 7 word count 0 then 177000 (65024 words), IOX 1 core address 2 then 0;
  - IOX 3 block address II = 052525, IOX 3 block address I = 125252;
  - IOX 5 control word 000014, the GO;
  - then **no IOX to register 0 (1540) at all**, only two status reads (IOX 4), both
    041030.

  The write half is the same with control word 004014. nd100x logs every read of
  register 0 (run 1 has 131104 of them, from other items), so the loop's `IOX 0` did
  not reach the SMD controller. Run 2 logs, straight after each of the two GOs,
  exactly 20 "No device found for READ address: 0", 40 in the whole run. So the
  `IOX 0` stay device address 0, and on a real ND-100 each one holds the bus until
  the IOX timeout (10 or 15 us, section 3). nd100x ends the transfer at the GO, so
  the loop ran once.

  An earlier reading of the file guessed that `IOX 0` was offset 0, made into base + 0
  like the other register offsets. The measurement shows that guess was wrong.
- **FACT, the overrun item runs in TEST MODE.** Control words 000014 (read) and 004014
  (write) are bit 2 (activate) plus bit 3 (test mode) (ND-11.020.01 sec 2.5).
  Sec 4.4 says that in test mode the data comes from a "test mode generator", not the
  disc, clocked by "an internal clock oscillator" (sec 3.3.2). Sec 4.4.1 says "the
  drive address is set to a special pattern used only in test mode", and that pattern
  is the 052525 / 125252 the item loads. So no drive turns during this item; the
  test-mode clock is what fills or drains the FIFO.
- **FACT, the test-mode oscillator circuit** (card 3043 logic diagram, sheet 4 "TIMING",
  manual page 87, block "DISK CLOCKS & DATA"):
  - 22D is a 74S00 NAND. Pin 12 is `TEST1`; pin 13 is the 30 ns tap (pin 14) of 20C,
    an HY5010-100 delay line.
  - The NAND output (pin 11) drives the delay line input (pin 1), so the two form a
    ring oscillator that runs only while `TEST1` is high.
  - The same output goes to 25C, a 74S158, at pins 3 and 6. 25C is switched by
    `TEST1`; its other inputs are `WRCM0` (the selected unit's write clock) and `RC0`
    (read clock). So in test mode both `WCL` and `RCL` come from the ring.
  - Sheet 3 ("TIMING", page 85): 25H divides `ICL0` by two to make `CL0`.
- **INFERRED, the test-mode frequency.** A NAND with 30 ns of delay in its loop has a
  half period of 30 ns plus one gate delay. A 74S00 gate typically takes 3 ns (5 ns
  at most), so the period is about 66-70 ns, about 15 MHz. That is the bit rate of a
  15 MHz drive, which makes sense for a loop-back test. The exact figure depends on
  the gate and the delay-line tolerance and was not measured.

## 5. What the emulators do

- **nd100x:** the whole DMA transfer is moved at the GO (S1, W1). An IOX takes no bus time.
- **RetroCore:** the same. See the comments at the bit 11 code in
  `NDBusDiscControllerSMD.cs` and `NDBusDiscControllerWinchester.cs`.
- **ND-120 RTL:**
  - Both controllers store a 1024-word block, then forward it, and the disc side always
    waits for the DMA. So the buffer can never overrun.
  - Bit 11 is set only by a failed memory cycle (`dma_err`).
  - `ND_DMA_MASTER.v` does make a real BREQ/INGRANT cycle per word.

## Root cause

On the real card, the disc moves one word per word time, whatever the bus is doing.
The FIFO is the only slack. All three emulators are missing the same three things:
1. a FIFO of the real depth with FULL and EMPTY;
2. a disc side that moves on its own clock;
3. bus time for the CPU's own I/O cycles.

The disc never has to wait, so it never overruns, and bit 11 cannot set.

What is measured (section 4): DISC-TEMA starts a 65024-word transfer in test mode,
then runs 20 IOX to device address 0, where nothing answers.

What is INFERRED from that and sections 1-3:
- each of those IOX holds the bus for the whole IOX timeout (10 or 15 us);
- with the CPU and DMA taking turns, the DMA gets about one 550 ns cycle per IOX,
  so about one word per 10-15 us;
- the test-mode generator meanwhile fills the FIFO on a read, or drains it on a
  write, at its own clock;
- at the test-mode word time of about 1.07 us (16 bits at about 15 MHz), the FIFO
  gains about one word per us against the DMA. So 4096 words fill in roughly 4-5 ms,
  about 15-25 passes of the loop, well inside the 65024 words.

With multiport memory the controller has its own path to memory, so the CPU cannot
starve it. That is note 7.

## A faithful model needs

1. A FIFO of the real depth: SMD 4096 words (less if the straps say so), Winchester 16.
2. The disc side clocked at the drive's word time, or at the test-mode oscillator when
   control word bit 3 is set (the overrun item uses test mode).
3. The DMA side taking one 550 ns bus cycle per word, using the machine's arbitration.
4. Every CPU bus cycle holding the bus: a memory cycle for about 450 ns, and an IOX until
   BDRY or its timeout.
5. Bit 11 set when the disc writes into a full FIFO or reads from an empty one.

## Building it in nd100x: the time base

**Decision: not built.** The only thing the model would change is this one DISC-TEMA item,
and it needs a new time base that all device timing would depend on. BUGS.md S1 and W1
stay open. The rest of this section is kept as the starting point if that changes.

What nd100x has today:
- The device clock is the instruction counter (`g_instr_counter`), with one tick per
  instruction. `device_smd.c` calibrates it at 0.5275 MIPS (10550 ticks per 20 ms RTC
  period), so one tick is about 1.9 us. All seek, rotation and transfer timing counts
  these ticks.
- An IOX that nothing answers already raises IOX ERROR on level 14 (`devmgr_read` and
  `devmgr_write`). It takes no time.
- Memory goes through `mms_read_physical_memory` and `mms_write_physical_memory`, and
  device DMA through `dev_dma_read` and `dev_dma_write`. So every bus reference the
  emulator makes passes one of four places, and each can be charged its bus time.

Figures the bus clock can use, with their sources:

| What | Figure | Source |
|---|---|---|
| Memory read, BADR to BDRY | 320 ns, 40 ns more on a corrected error | ND-06.015.02 p.149 |
| Memory write, BAPR to BDRY | 200 ns | ND-06.015.02 p.149 |
| CPU memory reference | "typically 450 ns" | ND-06.015.02 p.122 |
| DMA transfer | "typically 550 ns" | ND-06.015.02 p.122 |
| DMA channel, all controllers together | 1.8 M words/s | ND-06.015.02 p.196 |
| Refresh | one cycle every 13 us | ND-06.015.02 p.168 |
| IOX with no answer | 10 us (ND-100), 15 us (ND-110) | ND-06.015.02 sec 6.6.2, ND-06.026 |
| Any bus cycle | aborted at 15 us | ND-06.015.02 sec 6.2.3 |
| Whole instruction, sample | e.g. IOX 302 5.02 us, SAA 0.73 us | ND-06.014.2A appendix A.2 |

**NOT FOUND:** the time of every instruction. A.2 lists about 70 instructions, and the
ND-110 book (ND-06.029.1 sec 3.1.4) gives only the microcycle (6 x 26 ns; 4 x 26 ns on
the ND-110/CX). A bus clock that counts only bus cycles leaves out the CPU's own
microcycles between them, so the disc would turn too slowly against the program.
Two sources could give the real figure per instruction:
- the nd-120 Verilog, which runs the real microcode with real cycle lengths, measured
  per instruction in the simulator;
- NDMicroCpu, which runs the same microcode but does not yet model slow or delayed
  microcycles (`Cpu.cs`, the `SLOW` command is marked TODO).

## Open questions

1. Whether an IOX that times out holds the bus for the whole timeout, and whether the
   DMA gets a cycle between two such IOX. The ND-100 manual's "one cycle at a time,
   until BDRY" (sec 6.2.3) says yes to the first; it does not cover a cycle that
   ends by timeout.
2. The exact frequency of the SMD test-mode ring oscillator. Section 4 works it out to
   about 15 MHz from the 30 ns tap and the gate delay; no manual states it.
3. The PAL 20H equations, and the default STRAP1/STRAP2 setting.
4. Whether a real 3044 always carries all four IMS1420 chips.
5. The ND-100 IOX timeout: 10 us (sec 6.6.2) or 15 us (sec 6.2.3).
6. The ND-120's own bus arbitration, and which machine nd100x claims to be (the note 7 question).
7. The data rate of the 8-inch drives on the 3038.
