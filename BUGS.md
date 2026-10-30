# Known defects

Each entry names a behaviour the emulator does not get right yet, the test or program that shows
it, and what is known about the real mechanism. An entry leaves this file when the mechanism is
emulated and the test that showed it passes.

## SMD controller (`src/devices/smd/device_smd.c`)

### S1. Overrun (status bit 11) can never happen

**Shows up as:** DISC-TEMA FUNCTION, test 5 "Status Register bits test", two errors, both marked
`7)` by the program:

```
Error after Read (overrun expected), Status Bit 13b remains 0 !        7)
   Status 041030b, Unit 0
Error after Write (overrun expected), Status Bit 13b remains 0 !        7)
   Status 041030b, Unit 0
```

(DISC-TEMA numbers status bits in octal, so its "Bit 13b" is the manual's bit 11, as its
"Bit 11b" for the parity item is bit 9.)

**What the hardware does:** ND-11.020.01 sec 5.4 (BIGFUNC) item 13: "Status bit 11 is checked by
doing read with the instructions IOX 0 three times in the waiting loop, and by doing write with the
instruction IOX 0 twice in the waiting loop." Status bit 11 is "FIFO over/underrun or ND-100 Bus
error" (sec 2.5, Read Status Register). The data goes through an 8 Kb FIFO between the disk and memory (sec 3, "Functional description",
and figures 10 and 11, FIFO timing). The CPU's I/O cycles in the wait loop keep the controller's DMA off the
bus long enough for the FIFO to overflow on a read or run dry on a write, while the disk keeps
turning.

DISC-TEMA's note 7 says overrun cannot be provoked "when the CPU and the controller have different
DMA channels, as in f.ex MPM-5 systems". Which of the two cases the emulated machine is has NOT
been checked.

**What nd100x does:** the whole DMA transfer is done at GO, in one go, and only the end of the
operation waits for the disk's real time. There is no FIFO and no bus between the CPU and the
controller, so nothing can fall behind.

**What fixing it takes:** move the DMA word by word at the disk's data rate through a FIFO the size
of the real one, with the bus taken by the CPU's I/O cycles. That needs the ND-100 bus cycle times
for IOX and for a DMA cycle, and the answer to the note 7 question for the machine nd100x emulates.

### S2. Long mode: the ECC words may not match the real circuit

ECC control bit 2 ("Long", sec 2.8) makes a read deliver 516 words per sector: 512 data words, then
the 56-bit ECC and the 8-bit end-of-record byte. `smd_long_extra_words` divides the data field by
the polynomial G(X) of sec 3.4.1 as a plain 56-bit shift register, starting from zero (sec 2.8
bit 0: "reset to the zero initial state"). NOT verified: the order the bits go in (taken as each
word's bit 15 first), and whether the real network of Fig. 5 ("ECC Polynomials", three feed points
X1/X2/X3, data fed in differently on write and on read) leaves the same 56 bits as a plain division,
in the same order. DISC-TEMA FUNCTION only checks that the long read ends with data error (status bit 9), which
passes, so it says nothing about these four words. A program that reads them and checks them
(for example the ECC part of DISC-TEMA's other commands, or SINTRAN's error correction) may get
the wrong values.

### S3. Long mode: a write cannot keep the 4 extra words

A long write (M1 with ECC control bit 2) sends 516 words per sector. The image file holds data
fields only, so the 4 words after each 512 are dropped. A later long read of that sector gives the
ECC computed from the data, not the words that were written.

### S4. Address fields written by Write Format live in memory only

Write Format (M5) writes each sector's two-word address field. The image file holds data fields
only, so tracks formatted with fields other than the natural ones (cylinder, head << 8 | sector)
are kept in a table in memory (`SmdTrackFormat`, `disk_smd.h`). The table is lost when the emulator
stops. A real pack keeps its address fields. DISC-TEMA FUNCTION's address mismatch item passes
because it formats and reads back in the same run.

### S5. A write whose word count is not a whole number of sectors

Sec 2.8: "The Word Count is set to an integer multiple of the number of words in a sector when
device operation is M0-M3." What the drive writes after the last word of a shorter count is not
described. nd100x writes zero for the rest of the sector. Not checked against a real controller.
