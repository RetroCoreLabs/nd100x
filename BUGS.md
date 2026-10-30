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

## Winchester controller (`src/devices/winchester/device_winchester.c`)

The manual is ND-11.015.01 (Winchester controller, cards 3041/3038). Section numbers below are
from it.

### W1. Over/underrun (status bit 11) can never happen

Sec 2.2 and sec 4.2: the data goes through a 16-word FIFO between the drive and memory, and "If
the FIFO goes empty during write disk or full during read, status bit 11 will be set" (bit 11
is also the DMA channel error, sec 3.5). nd100x moves the whole DMA transfer at GO and
only delays the end of the operation, so the FIFO can never fall behind; bit 11 is set only when a
memory cycle fails. Same cause and same fix as S1: move the words at the drive's data rate through
a FIFO of the real size, with the bus taken by the CPU's own I/O cycles. Sec 5.6 names DISC-TEMA as
the checkout for this controller too; it has NOT been run against the Winchester yet, so whether
it tests overrun here is not known.

### W2. Address fields written by Write Format live in memory only

Write Format (M5) writes each sector's two-word address field (sec 3.4.6: word 0 bit 4 bad track
flag, bits 3-0 head; word 1 cylinder << 5 | sector). The image file holds data fields only, so
tracks formatted with other fields than the natural ones are kept in a table in memory
(`WDTrackFormat`, `disk_winchester.h`) and are lost when the emulator stops. Two more things are
not described in the manual and are guessed:

- sec 4.4 phase 5 writes "dummy data" into each formatted sector; what that data is is not said,
  so nd100x leaves the image's data fields as they were;
- a word count giving more fields than the track has sectors: nd100x uses the first track's worth.

### W3. Multi-sector transfers go on to the next head

A read or write of several sectors goes on to the next sector and, at the end of a track, to the
next head, the same order as the image file. The manual does not say whether the card does this.
The SINTRAN driver (WINCHESTER:NPL) splits transfers at the end of a track, so SINTRAN never
depends on it. A transfer that runs off the arm's cylinder ends in address mismatch, because the
card cannot step the arm itself (sec 2.2, sec 5.5).

### W4. The status bit for an operation on a drive that is not there

A unit with no pack is taken as a drive that is switched off: every operation, seeks and return to
zero included, ends with status bit 7 ("Disk fault or missing clocks", sec 3.5). The manual does
not name the bit a seek gives on an absent drive. Not checked on a real controller.

### W5. Stepping past either end of the disk

M4 (relative seek) stepping below cylinder 0 or above the last cylinder: the manual does not say
what the drive does. nd100x stops the arm at the end and reports no error.

### W6. A write whose word count is not a whole number of sectors

What the card writes after the last word of a count that ends inside a sector is not described.
nd100x writes zero for the rest of the sector, as S5 does for the SMD.

### W7. The block address register after an operation is inferred, not documented

nd100x moves the sector field of the block address register one past each sector the operation
dealt with, the failing one included, and a device clear leaves the register alone. This comes from
the SINTRAN driver, not from the manual: its error routine (WINCHESTER:NPL, IERR) gives a device
clear, reads the register (RDAD) and takes "register - 1" as the failing sector. What a real card
holds there after a good transfer, and whether the head or cylinder field ever moves, is not known.

### W8. Read Parity moves the memory address and word count, inferred

Sec 3.4.3: on M2 "No data transfer to the computer memory is performed." nd100x still runs the
memory address and the word count through the sectors, as if the words had gone to memory, and
ends with the address at start + word count and the count at 0. The manual does not say this.
The evidence is DISC-TEMA J02 PARITY-CHECK, the card's checkout (sec 5.6): it reads the
address back after every M2 and reports "Memory address Register not as expected" otherwise.
The SINTRAN driver makes the same check after every transfer it starts (WINCHESTER:NPL,
"MAR=OLDMAR+OLDWORDC ? >>> MEMER", "CORE ADDRESS REGISTER NOT AS EXPECTED"); whether it ever
starts an M2 has not been checked. Not checked on a real card.

### W9. Drive timing comes from the Micropolis 1320 series figures

Every operation keeps the controller active for the drive's time: seeks from the maker's
figures for the 1320 series (6 ms track-to-track, 28 ms average, 62 ms maximum, 3600 rpm), and
transfers for the wait until the first sector comes round plus one sector time per sector. Not
known, and so drawn as an assumption:

- the shape of the seek curve between the three seek figures (two straight lines, as for the SMD);
- whether the CDC Finch types (DISC-14MB, DISC-21MB) have the same figures; they get them;
- how long an operation takes that ends in an error found before any sector passes (empty unit,
  write protect, out of range): it ends after the old flat 10 ticks.
