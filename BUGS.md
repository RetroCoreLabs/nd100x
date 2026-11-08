
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



**Measured:** see `docs/DISC-FIFO-OVERRUN.md`. The DISC-TEMA item runs the controller in test mode

(control words 000014 and 004014, the card's own ring oscillator of about 15 MHz), and its loop does

20 IOX to device address 0, where nothing answers. On real hardware each of those holds the bus for

the 10-15 us IOX timeout. The fix also needs a CPU time per instruction, which no manual gives in

full; the document lists the figures that are known and where they come from.



**Decision:** left open. The fix needs a new time base in the CPU loop that all device timing would

depend on, and it changes nothing but this one diagnostic item. It stays red until that trade is

made differently.



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



### S6. Control bit 1, "enable interrupt on errors", is never looked at



**Found by:** reading the code while porting it to NDDeviceCore. Not yet shown by a run.



**What the hardware does:** ND-11.020.01 sec 3.7, Fig. 14: "Error interrupt is enabled by

control word bit 1" - the line is (end of operation and bit 0) or (SB4 and bit 1).



**What nd100x does:** `smd_read_end`, `smd_timeout_end` and `finish_operation` raise the

interrupt only when bit 0 (`interruptEnabled`) is set. An illegal load sets b5 and b12 in

`smd_write` and raises no interrupt at all. The Winchester has the same gap (W11).



### S7. A compare mismatch stops the transfer and sets bit 12



**Found by:** code reading (NDDeviceCore port).



**Shows up as:** DISC-TEMA COMPARE, unit 0 against a copy on unit 1 with one word changed

(docs/DISC-TEMA-TESTING.md section 4.9). The word is found at the right track, but the report

has bit 12 and a second line the Winchester report of the same change does not have:



```

            Hardware Status: 052031b

            Controller finished

            Compare error

            Abnormal completion

            Additional Status: 100000b

            Controller not active after activate

```



Whether "Controller not active after activate" comes from this defect has not been established.



**What the hardware does:** ND-11.020.01, mode M3: "A mismatch causes compare error to be set".

Sec 3.7.2 and Fig. 14 list the abnormal ends as SB13, SB8 (when not formatting), SB7, SB6 and SB5.

A compare error is not one of them, so it should neither end the transfer nor set b12.



**What nd100x does:** `execute_go`, `DEVICE_OP_COMPARE_TRANSFER`: `handle_error(COMPARER_ERROR)`

sets b10 and b12 and `finish_operation` ends the transfer at the first differing word, leaving

the address and word counters on it. In test mode b9 is set as well.



### S8. A failed memory cycle never sets bit 11



**Found by:** code reading (NDDeviceCore port). Not yet shown by a run.



**What the hardware does:** ND-11.020.01 sec 2.5, status bit 11 "FIFO over/underrun or ND-100 Bus

error".



**What nd100x does:**

- **M0 and test M0:** the result of `dev_dma_write` is not checked. The word is lost, the

  counters step on, and nothing is reported.

- **M1:** a `dev_dma_read` below 0 runs `handle_error(READ_ERROR)` and `finish_operation`, and

  only b12 shows (see S11).

- **M3:** the failed cycle counts as a compare error (b10, b12).

- **M5:** the word is taken as 0 and the format goes on.



b11 is set in no mode.



### S9. The drive type chosen from the image size misses three drives



**Found by:** code reading (NDDeviceCore port). Not yet shown by a run.



**What the hardware does:** ND-11.020.01 sec 2.4: the cylinder table lists the Fujitsu 474 Mb

Eagle with 842 cylinders and the CDC 825 Mb XMD with 1024.



**What nd100x does:** `smd_disk_type_for_size` never picks the Eagle or the 515 MB drive. It picks

the XMD only for files of 0x33900000 bytes or more, which is larger than an XMD image (738,197,504

bytes). So an Eagle-sized image gets DT288 (19/18/823), and an XMD-sized image gets DT450 (24/26/711).



### S10. Operation codes 10-15 leave the controller active for ever



**Found by:** code reading (NDDeviceCore port). Not yet shown by a run.



**What nd100x does:** an activate with an operation code from 10 to 15 sets active and

nothing ever ends it. What the real card does with those codes is not in ND-11.020.01; a

controller that never answers again is unlikely to be it.



### S11. The status read throws away bit 13 set by an error



**Found by:** code reading (NDDeviceCore port). Not yet shown by a run.



**What nd100x does:** `handle_error` sets b13 in a copy of the status word, and the status read

then builds the word again without it. So read, write and bus errors show only b12, and a media

fault shows b12 where the drive should report "unit not ready" (b13).


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



Measured cause and decision as in S1: see `docs/DISC-FIFO-OVERRUN.md`. Left open.



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



### W10. Status bit 12 on the 3038 (8 inch) card is not traced



**Shows up as:** nothing yet; no test or program has shown it.



**What the hardware does:** on the 3041 (ST506) bit 12 is the selected drive's Track 000 line,

and nd100x does that (`wd_read_status`). On the 3038, ND-11.015.01 sec 7.4 shows bit 12 coming

from an SB12 set/reset latch (11E, a 74LS279, reset by CE), and the signal that sets it is on

another sheet that has NOT been traced. Sec 3.5 calls the bit "0 (Not used)".



**What nd100x does:** bit 12 reads 0 on the 3038.



**What fixing it takes:** find SB12 on the 3038 sheets (sec 7.1-7.6).



### W11. Control bit 1, "enable interrupt on errors", is never looked at



**Found by:** code reading (NDDeviceCore port). Not yet shown by a run.



**What the hardware does:** ND-11.015.01 sec 3.4 lists control bit 1 as "Enable interrupt on

errors".



**What nd100x does:** `wd_transfer_end` raises the interrupt only when bit 0

(`interruptEnabled`) is set. Same gap as S6.



### W12. A failed memory write in M0 is not reported



**Found by:** code reading (NDDeviceCore port). Not yet shown by a run.



**What nd100x does:** the M0 loop does not check the result of `dev_dma_write`. The word is

lost, and the transfer goes on as if it had been stored. Sec 3.5 bit 11 is the DMA channel

error, so the card would report it.



### W13. A control word without activate sets bit 3 while an operation runs



**Found by:** code reading (NDDeviceCore port). Not yet shown by a run.



**What nd100x does:** a non-activating control word sets "finished" (b3) even when the

controller is active, so the status can read active and finished at once. The ND-120 RTL

guards this case (`ND_WINCHESTER.v`, "ONLY WHEN IT REALLY IS IDLE").



### W14. The decoded cylinder and sector do not follow the block address advance



**Found by:** code reading (NDDeviceCore port). Not yet shown by a run.



**What nd100x does:** `wd_advance_block_address` (W7) moves the block address register, but

the cylinder and sector fields decoded from it are not updated, so a later use of them

reads the old address.



### W15. The Micropolis 1325 is sized at 1022 cylinders but given 1024



**Found by:** code reading (NDDeviceCore port). Not yet shown by a run.



**What nd100x does:** the 74 MB format (DT074) is 1022 cylinders in the SINTRAN layout table

(`disk_winchester.h`), but the drive entry has 1024, so the range check lets cylinders 1022

and 1023 through.


## SCSI controller (`src/devices/scsi/`)

Found by running the Norsk Data SCSI test programs from the TPE floppy 210523G02-XX-02D
(SCSI-TV-B00 and DISK-MM-B00, under TPE-MON-100-B00), on copies of `SCSI0.IMG` at SCSI IDs 0, 1
and 2. The manual is ND-210523G (Test Programs for ND-100/110/120), the SCSI-TV and DISK-MM
chapters. What passes: adapter tests 1 to 6; device tests 1 to 14 on ID 0, and 4 and 5 on ID 1
as well; DISK-MM SELECT-DEVICE, LIST-DISK-INFORMATION, LIST-REASSIGN-TABLE, LIST-REFRESH-TABLE,
VERIFY and FORMAT-INITIALIZE.

### C5. Mode pages and FORMAT UNIT: what still differs from the drive

**Found by:** DISK-MM FORMAT-INITIALIZE on a copy at ID 3, with `--scsi-debug`. It used to stop
with "(DF) Must be formatted", and after that was fixed with "(CS) Error in consistency of
partition block". Both are fixed (2026-10-31): the target now keeps current and saved values of
mode pages 1 to 4, takes MODE SELECT, keeps the current values as the saved ones on FORMAT UNIT,
loads the saved ones on reset, and answers MODE SENSE page 3Fh with the 1370's four pages in
ascending order. FORMAT-INITIALIZE ends with "-Ok- Directory size 125 Mb" and writes a partition
block with entry 7 at 129107, size 162 = (8 heads + 1) x 18 sectors per track. How DISK-MM checks
a format, with the trace addresses, is in the comments at `scsihdd_command_mode_sense` and
`SC_FORMAT_UNIT` in `scsi_hdd.c`, and in the NDInsight SCSI storage chapter. Tests:
`tests/test_scsi_hdd.c`, `test_mode_pages`.

**What is still not as the drive:**

- FORMAT UNIT keeps the current mode values as the saved ones. That the 1370 does this is
  inferred from DISK-MM's check; Micropolis document 110011 (SCSI implementation), which would say,
  has not been found.
- FORMAT UNIT does not touch the image. A real format leaves every block empty; here the old data
  stays. The defect descriptors after the 4-byte defect list header are not taken (an error is
  logged when the header says there are some).
- The saved values are kept in memory only, by choice: every run starts with the defaults. On a
  real drive they outlive power-off. Whether DISK-MM or SINTRAN looks at the saved values outside
  FORMAT-INITIALIZE has not been checked.
- MODE SELECT refuses nothing. The drive would refuse a field that cannot be changed set to a
  different value, and a skew larger than the sectors per track (Appendix B). The block descriptor
  is not taken: the block length stays at the image's 1024.
- Page 3 and page 4 default values follow the image, not Appendix B. Sectors per track, bytes per
  sector, cylinders and heads describe the image (18 x 1024, 898 cylinders, 8 heads), where the
  drive's defaults are 36 x 512 and 1024 cylinders. Tracks per zone is cylinders x heads (the drive:
  1), alternate sectors per zone 0 (1), alternate tracks per volume 0 (3 x data heads), interleave
  0 (1), cylinder skew 0 (12), and the HSEC bit in byte 20 is 0 (1). The original `SCSI0.IMG`
  partition block has entry 7 sized 171 = 9 x 19, which fits a drive formatted with 19 sectors per
  track, the Appendix A example; the image itself is 18 sectors per track.
- The mode data length (byte 0 of the MODE SENSE reply) counts itself, as the RetroCore C# sets
  it. What the 1370 sends there has not been checked against a document.

### C6. SCSI tape: what is not as the drive, and the CD-ROM target

The streamer exists since 2026-11-01: `--scsiN=tape:FILE` is a Tandberg TDC 3600 on a SIMH
`.tap` image (`src/devices/scsi/scsi_tape.c`, `tape_image.c`), built from the Tandberg TDC
3620/40/60 Reference Manual. Tests: `tests/test_scsi_tape.c`. On a blank tape at ID 1, both
SCSI-TV B00 (floppy 210523G02-XX-02D) and SCSI-TV C04 (floppy 210523I01-XX-02D) pass device tests
1, 2, 4, 5, 30, 31 and 32 with 0 errors (`RUN-DEVICE-TESTS ALL-TESTS`, 2026-11-01). C04 also
runs LIST-PRESENT-DEVICES (it shows "Seq Rem TANDBERG TDC 3600", block size 512),
PRINT-TAPE-STATISTICS, TAPE-SERVICES REWIND / UNLOAD / LOAD, DUMP-SENSE-DATA and CLEAR-DEVICE
without an error.

READ BUFFER and WRITE BUFFER are the standard SCSI commands (WRITE BUFFER 3Bh, READ BUFFER 3Ch,
T10 operation code list). The scanned Tandberg manual prints them the other way round. Until
2026-11-01 the target followed the manual: C04 device test 4 then failed, and B00 test 4 passed
without moving any data, because it read no buffer header and so wrote 0 bytes. With the standard
codes both versions move real data (C04 24 x 48 KB, B00 24 x 65531 bytes).

SCSI-TV device test 3 (Extended drive self test) sends SEND DIAGNOSTIC `1D 07 00 00 00 00`,
Selftest 2. Since 2026-11-01 the target answers GOOD with no operation and no tape motion, as
ND-210523G, "3.5 Known but not Corrected Errors", says the TDC 3600 does ("Device test 3 ...
executed on TANDBERG TDC 3600 will perform no operation"). Ronny chose that. C04 test 3 now ends
with no error. The Tandberg manual (9.3.18) calls Selftest 2 an acceptance test that writes and
reads the tape, without describing it. **Open:** what a real drive answers has not been measured.

Under SINTRAN L the streamer also does a full BACKUP-SYSTEM DEVICE-COPY of a 36945-page disk to
tape and back, both "- OK", and the restored disk is identical to the tape data (NDInsight
`SINTRAN/Devices/SCSI/SCSI-STORAGE-UNDER-SINTRAN.md` sections 9.0 and 10).

**Still red:**

- **COPY (18h) and linked commands** are refused with ILLEGAL REQUEST and an error in the log.
  Nothing on the ND side has been seen to send them.
- **Filemarks count as blocks** for READ POSITION / LOCATE block addresses. The manual does not
  say; it is marked as an assumption at `tape_image_block_address`.
- **Two Unit Attention assumptions** (comment in `tdc_command`): after power-on, reset or a new
  cartridge the tape is taken to be back at the start (the manual says "logically unloaded"), and
  a REQUEST SENSE as the first command returns the Unit Attention sense and clears it (the manual
  does not say).
- **Magtape is not there.** SCSI-TV device tests 20 to 23 are for the HP 88780 / NDSTK 2925
  magtape; `--scsiN=tape:` is always the streamer.

**CD-ROM:** `--scsiN=cdrom:FILE` is parsed but not implemented (`--help` says so). No ND CD-ROM
product has been found, and none of the SCSI-TV versions lists one.

## Terminal (`src/devices/terminal/device_terminal.c`)

### T1. Typed keys reached SINTRAN about ten times faster than a real line carries them (input fixed)

**Found by:** `tools/installs/backup-manager-b02/install.py`, 2026-11-04. The line
`COPY-FILE "(SYSTEM)UE-ERMSG-EN-D05:ERR",(211075C03-EN-01D:FLOPPY-USER)UE-ERMSG-EN-D05:ERR`
(89 characters, sent through `--pipe` in one write, 0.1 s after the `@` that followed
ENTER-DIRECTORY of a floppy) arrived as `...FLOPPY-USER)UESG-EN-D05:ERR`: characters 71-74
(`-ERM`) were gone, and SINTRAN answered NO SUCH FILE NAME.

**Not every time:** the same steps repeated eight times on a copy of the same pack lost
nothing, and lines of up to 120 characters typed at a quiet `@` arrived whole.

**What is known:**

- nd100x does not lose them. Its key queue holds 256 keys and was not full, and the only other
  way a key is dropped, a device clear, loses at most one key, not four in a row.
- SINTRAN does. The terminal input data field DTxxR has a ring buffer of capacity "40 + 40"
  (ND-60.112.01 Appendix A, DTxxR, MAX), 64 characters if that is octal as the listing
  values are (assumed, not checked against the running system). A key that arrives while the
  buffer is full is lost. Losing keys 71-74 fits that: 64 in the buffer plus the few the command
  reader had already taken.
- nd100x hands the next key to the CPU about every 100 instructions once the last one was
  read and output is idle (`terminal_tick`, `MAX_TICKS` = 100). A 9600-baud line carries one
  character per 1.04 ms. So the 89 keys arrive in a small part of the time a real line would
  take, and a command reader that is busy for a moment falls behind where on a real line it
  would not.

**Input fixed 2026-11-04.** A key now reaches the UART when its line time has passed: start
bit, data bits, parity bit and stop bits as the input control word sets them (IOX 303 bits
11-14, ND-06.016.01 B.1), at the line speed, in instructions at the emulator's real-time rate
(`cpu_throttle_get_mhz`, 0.5275 MHz unless `--throttle` sets it, the rate the 20 ms clock is
built on). 8 data bits and 2 stop bits at 9600 baud are 604 instructions. The line speed is
`[terminals] baud` (default 9600, speed switch setting 7; ND-06.016.01 Appendix C.1.2 for the
console, C.2.3 for the 3010 terminal module). The keyboard / `--pipe` reader and the telnet
reader take no more keys than the queue has room for, so a long paste waits at its source and
is not dropped. Tests: `devmgr_tick_tests` (line times for 8N2, 7E1, 5 bits with 1.5 stop bits
and 19200 baud; a 300-key burst arrives whole and in order), `machine_config_tests`. On the
machine: single lines of 40, 89 and 120 characters and three pastes of 426 characters in one
write each reached SINTRAN whole.

**Still not as the hardware:**

- **Output** still takes about 100 instructions a character (`IODELAY_TERMINAL`), about six
  times faster than a 9600-baud line.
- **IOX +1, "data rate selection"**, on the terminals of the 3010 module is still a no
  operation: ND-06.016.01 (B.2) names the register but gives no bit layout, so a program that
  sets a line speed this way is not followed. On the console (IOX 301) it is a no operation on
  the real card too (B.1). SINTRAN's `TSPEED` (DTxxR) was not traced.
- The input waits for output to be idle before the next key, as before; whether the card
  does that was not checked.
- The browser build (`nd100wasm`) queues keys without the room check.

## Interrupt system

### I1. TPE-MON cannot clear IDENT on levels 8 to 11 at start-up

**Found by:** every TPE-MON start in the SCSI-TV runs of 2026-11-01, booting copies of the TPE
floppies 210523G02-XX-02D (TPE-MON-100-B00) and 210523I01-XX-02D (TPE-MON-100-B01) with
`--boot=floppy --pipe`. It shows the same with no SCSI controller, with a SCSI disk, and with the
SCSI streamer, so no SCSI target causes it.

**Shows up as:** before the TPE banner, four times:

```
*** TPE initialization error ***
Impossible to clear IDENT on level  8
```

for levels 8, 9, 10 and 11. The monitor then starts and the test programs run.

**What is known:** inferred from the message only (not traced): TPE-MON reads IDENT on each level
until no device answers, and on these four levels some device keeps answering. Which device, and which IOX sequence TPE-MON uses
to clear it, is not traced. The ND manual for TPE-MON (ND-30.005) has not been checked for what
the test expects.

## Command line (`src/frontend/nd100x/nd100x.c`)

### L1. With `--config`, a `--scsiN` given on the command line is dropped without a word

**Measured 2026-11-08:** `nd100x --pipe --config=machine.ini --scsi1=tape:tape.img`, where the
INI has `[controller.scsi.0] enabled = yes` and no `disk1`. SINTRAN selects SCSI ID 1 for
`@DEVICE-FUNCTION STREAMER-1` and the selection times out every time (`SCSI/NCR: selection:
timed out` in the `--scsi-debug` log); the log has no "unit 1 attached as tape" line and SINTRAN
answers `NO RESPONSE FROM DEVICE`. With `disk1 = tape:tape.img` in the INI instead, the drive
is attached and REWIND / LOAD work.

**Why, from reading the code (not traced):** `apply_cli_to_machine_config()`, whose comment
says "CLI wins over the INI", is only called on the `--show-config` / `--write-config` path
(about line 1267). The `--config` path that builds the machine (about line 1300) loads the INI
and never calls it, so every flag that function copies is left out on a real run - `--scsiN`
measured; `--smdN`, `--wdN`, `--cputype`, `--fpp` and the others it handles not tried. No
message says a flag was ignored.

**Affects:** `tools/nd100x_expect.py` passes `scsi={...}` as `--scsiN`, and
`docs/pipe-automation.md` shows `config=` together with `scsi=`; that combination does not
attach the drive. Workaround used: none in the repo; the Backup Manager check runs of
2026-11-08 wrote the tape into the INI. Fix not made: open.

## ND product installers (`tools/installs/`)

### P1. Backup Manager B02 reinstall: "Filename syntax error" joining BM-OPERATOR

**Found by:** running ND's Backup Manager installer `(211226B02-XX-01D:FLOPPY-USER)IN-BCKMAN-XX-B02:PROG`
a second and a third time on a pack where it was already installed, answering N to
"delete previous version" and "delete current version", 2026-11-02.

**Shows up as:** on floppy 211226B02-XX-04D, after the box "Add the file
(211226B02-XX-04D:FLOPPY-USER)BM-OPERATOR-B02:DO02 to the file (BACKUP-MANAGER)BM-OPERATOR-B02:DOM.":

```
Filename syntax error
1.1594 ^FILE_COPY HE_BUF,SPE_TO_FILE_1
```

The installer goes on and ends "INSTALLATION - FINISHED". The first run on the same pack, with
no Backup Manager on it yet, printed the same box and no error.

**What is known:** the join is the routine `SPE_COPY` in the installer program
`IN-BCKMAN-XX-B02:PROG`: `HE_BUF` is the full name of the floppy file, `SPE_TO_FILE_1` is
`BM-OPERATOR-B02:DOM` (from `IN-BCKMAN-XX-B02:INIT`). Another routine, `COP_SPE_SET`, changes the
type of a target that already exists to `:NEW`, which is the only path that differs on a
reinstall. Not traced: the value of `SPE_TO_FILE_1` when `^FILE_COPY` runs, and which
SINTRAN call returns the error. So it is not known yet whether this is the installer itself
(it would fail the same on a real ND-500) or something nd100x does differently.
`tools/installs/backup-manager-b02/install.py` installs on a pack without the Backup
Manager, which does not take this path, and stops if "Filename syntax error" appears.

### P2. Backup Manager B02 file copy to and from the streamer: what is still open

**State 2026-11-04: backup and restore of several files, and List-volume, work.** A COPY of four
files of user RONNY to `BMTEST-1` and back onto a pack where they were deleted gave four
byte-identical files. Services / Tape-operations / List-volume lists the volume, but only with
terminal type 6 (VT100); see the 036602B point below.

**State 2026-11-03: single-file backup AND restore on `STREAMER-1` work.** Measured on copies
of a pack made by the `tools/installs/` chain (BACKUP-SYSTEM I05, ND-500 monitor J04,
LINKAGE-LOADER H02, LINKER B01, Backup Manager B02), machine `ND120CX` with `fpp = 32`:

- Backup: BM-OPERATOR `COPY` of `(PACK-ONE:RONNY)TEST:SYMB` to volume `BMTEST-1` on
  `STREAMER-1` unit 0, mode OV -> "File(s) copied: 1". The tape holds a Backup Manager volume
  (`MEDH` header, `BMTEST-1`, `PACK-ONE`, `RONNY`) and the start, middle and end of the
  5340-byte file.
- Restore: a fresh copy of the pack with `(RONNY)TEST:SYMB` deleted (`ndtool --rm`), the same
  tape, `COPY` from `VOLUME BMTEST-1 STREAMER-1 0` to `DIRECTORY PACK-ONE RONNY`, `INCLUDE
  TEST:SYMB` -> "File(s) copied: 1"; the restored file is byte-identical to the original
  (`cmp`) and keeps its date 1998-09-22 22:04:39.

The earlier "No files copied" was the set-up, not the emulator. What the working runs did, in
this order (2, 3 and 5 each found by a run that failed without it; 1 by earlier runs; 4 replaced
the name *XM-FIDO and was not tried without since):

1. XMSG (`START-XMSG` in SINTRAN-SERVICE-PROGRAM), then `@START-SERVERS`, then the ND-5000
   (ND-895061-1A, the LOAD-MODE step). In another order the swapper takes the multiport memory
   and the NUCLEUS servers find no room for their LAMUs.
2. `@DEFINE-MASS-STORAGE-UNIT STREAMER-1 0`. With the "Computer name" field blank, B02 looks a
   device up ONLY in SINTRAN's directory and device-name tables (MON 244B GetDirEntry for
   entries 0-45, MON 245B GetNameEntry) - routine `GetS3Device`, 0x10070D39 in
   BM-OPERATOR-B02:DOM. Without the mass-storage unit it finds nothing and prints error 33575
   `D100.STREAMER-1,0`. The peripheral file `(SYSTEM)STREAMER-1` (2226B) is needed as well, for
   `@DEVICE-FUNCTION`.
3. `DOMINO-MODE = OFF` in `(ND-OPERATIONS)BM-PARAMETER-B02:DATA` (ND's own switch, "Search for
   DOMINO sequential devices"). With ON, every COPY opens the DOMINO name server
   `DP-NAME-SERVER` through NUCLEUS; on a machine without DOMINO that answers 101012B
   `nke_NAMENOTFOUND` (correct: nothing registers the name), and B02 accepts only 101014B there
   (0x100760D7-0x100760E8), so the COPY ends.
4. A local system name in XMSG: `@XMSG-COMMAND`, `DEFINE-REMOTE-NAME,,D100,100`, `EXIT`. Backup
   Manager asks XMSG for it (XROUT XSGNI) and puts it in front of every directory name
   (`D100.PACK-ONE`); without one it got `*XM-FIDO`. XMSG forgets it when XMSG is stopped.
5. Leave the "Computer name" fields EMPTY for local devices. With `D100` typed, B02 takes its
   tape path, opens `D100.STREAMER-1'` through MON 50B, and SINTRAN answers 260B
   "File-access reentrant segments are not loaded": remote file access (COSMOS FAU), which this
   pack does not have.

`@RT DMASRV` was typed before BM-OPERATOR in the measured runs; whether a file copy needs it was
not tried. The run scripts (`nuc4.py`, `run5.sh` backup, `run6.sh` restore) are probes outside
the repository; a `tools/installs`-style script is not written yet.

**Still open:**

- **Error 33504 (octal) during the restore**, printed after the file list and before
  "File(s) copied: 1". Traced 2026-11-03; it is **Backup Manager's own "reached blank tape"
  report, not an emulator fault**:
  - The restore's tape traffic (`--scsi-debug`, MON 144B logged): READ 2 blocks `MEDH`, READ 2
    `VOLH`, READ 2 the `OBJH` header, READ 12 the file's data (blocks 6-17), then READ 12 ->
    filemark at once (residue 12), READ 2 -> the second filemark (residue 2), READ 2 -> BLANK
    CHECK. SINTRAN answers those three reads 3B, 3B and 143B. The tape (SIMH walk) holds exactly
    what the backup wrote: those records, two tape marks, nothing more. The drive answers are
    the ones the SCSI sequential-access rules and the TDC 3600 manual give (`scsi_tape.c`).
  - BM-OPERATOR turns the read status into its own code at 0x100636C9: 3B -> 33505, 143B ->
    33504 when bit 10 of the device's flag bits is set, 100B/146B -> 33503. It stores the code
    in its status word (data 0x08012DFA). The tape routine 0x1000E2C3 sees 33504 there
    (0x1000E762), sets its error flag, and the status-line routine 0x1001CC14 prints 33504
    instead of its progress text; then "Rewinding".
  - So with `INCLUDE`, BM goes on searching for more matching files after the volume, reads
    past both tape marks into blank tape, and reports that. The text is missing because
    `UE-ERMSG-EN-D06` has no texts for BM's 337xx codes. `REEL-END` is not a missing trailer:
    it belongs to `HandleEOT`, the end-of-tape handling for a backup over several tapes.
  - Not verified: that a real machine prints the same line. The same SINTRAN status would come
    back from a real drive at the end of recorded data, so it is expected to.
- **036602B and BM-OPERATOR exits** when Services / Tape-operations is chosen on terminal
  type 2 (Teletype ASR 33), measured 2026-11-04. On type 6 (VT100) the same menu works. The
  same number appeared on VT100 when EXIT was typed into a form field, without the exit.
  036602B is "Invalid parameter value given": group 366B "Backup Manager -
  Operator/Definition", message 2, in `UE-ERMSG-EN-D05:ERR` (a code is group x 64 + message;
  the file layout is in NDInsight's SCSI storage document, 10.3.6a; read 2026-11-07). The
  earlier "not in D05" was a text search for the number, which the file does not hold. So
  the program refused the menu word. Why it refuses the words on terminal type 2 is not
  known; nothing points at the emulator yet.
- **`@RT DMASRV` is not needed; `DEFINE-REMOTE-NAME` is needed for a backup** (step 4),
  measured 2026-11-08 on a pack fresh from the five install scripts. Without the name the
  backup stops with `000021B: Illegal character in parameter` / `*XM-FIDO`, "No files
  copied". With the name and no DMASRV: "File(s) copied: 3", and a restore onto a copy with
  the files deleted brought all three back byte for byte, with and without the name.
- **A second COPY in the same BM-OPERATOR run** after a FAILED one took "COPY" and every answer
  after it as new commands (measured 2026-11-03, before the set-up above). Not tried again
  after a successful COPY.

**Fixed in the install script (2026-11-04), `tools/installs/backup-manager-b02/`:**

- **Error 33626 (octal)** on `(SYSTEM)SWAP-FILE-0:SWAP` at BM-OPERATOR start. The
  `IMAGE-NO-COMPARE-FILE` lines in BM-PARAMETER name files that are not on the pack; this
  machine's ND-500 swap file is `(PACK-ONE:SYSTEM)SWAP-FILE:DATA` (LIST-SWAP-FILE-INFO 0).
  ND's notes say to edit these lines for the machine (ND-895055-3A "Information and Hints",
  ND-895560-2 "Defining Extra Swap-Files"), at most 12 of them (ND-896058-4 item 27). The
  script now asks the ND-500 monitor for the swap files and writes them in
  (`bm_parameter.py`, test `bm_parameter_unit`).
- **"An internal error has occurred ... Library UE-ERMSG-EN-D' not found"** on the first COPY
  of a fresh pack. B02 needs the message file `(SYSTEM)UE-ERMSG-EN-D..:ERR`, does not ship it
  and does not check for it (ND-895055-1A says B04 fixed this). The script copies
  `UE-ERMSG-EN-D05:ERR` from floppy 211075C03-EN-01D (md5 be8441322f2c65076c48ecc3b4468eda).
  `UE-ERMSG-EN-C05` from BACKUP-SYSTEM is not taken.
- **A fresh pack also needs the streamer set up** (SCSI streamer 1 in S3-CONFIG and
  `@SET-PERIPHERAL-FILE "STREAMER-1" 2226B`); without it DEVICE-FUNCTION answers ILLEGAL
  PARAMETER and DEFINE-MASS-STORAGE-UNIT NO SUCH DEVICE NAME. That is a machine set-up step,
  not part of this install script.

**The install chain on `ND5000.IMG` (2026-11-04):** that pack now runs a boot batch
`(SYSTEM)LOAD-MODE:BATC` (XMSG, START-SERVERS, ND-500-MON LOAD-CONTROL-STORE and
START-SWAPPER) at every boot. The install scripts start the ND-5000 themselves, so on that pack
they get "Other user(s) already logged on ND-500(0)", and PROMAN messages land in the middle of
installer questions. The chain passed all five steps from a copy with that batch file removed.
Not decided: whether the scripts should detect and use a running ND-5000.

**Fixed in the emulator on the way (2026-11-02 and 2026-11-03):** CNREK cleared the wrong
page-table slots, so the ND-500 monitor never answered on ND-110/ND-120 (`src/cpu/cpu_instr.c`);
the 3RMICV CPU parameter is 0x0038, the ACCP model answer, not 0x03E1 (nd500x); the 26B
(3WMONCO) answer copy goes through the process's MMU and a missing page is reported as page
fault 46B and the 26B resent (MP-P2-N500.NPL 135641), with MICFU left at 26B until the copy is
done (`MSG_CON10` @015715-015716). nd500x listings decode through the trap-free MMU peek
(6469a3c, test `listing_decode`). `MFBUS_CODE_AT_MON=nnn` logs the code round a monitor call's
resume address and the ENTS frame chain; `--log=mms:info` logs every ND-5000 monitor call, its
answer bytes and every MON 50B file name.

**Ruled out:** the four NUCLEUS instructions that ND microcoded on the ND-5000 (`SEND`, `RECVE`,
`GETINF`, hole move) are not in nd500x, but no illegal-instruction trap happened in the backup
or the restore, so a file copy to the streamer does not use them.

## DAP debugger (`src/debugger/`, `external/libdap`)

### D1. The debugger serves only the first client for the life of the process

**Found by:** connecting a second time to one `--debugger --port=4790` run, 2026-10-31.

**Shows up as:** after the first client disconnects (MCP `debug_disconnect` with
`terminate=false`), a new client's `initialize` gets no answer at all and times out, from both
the MCP tool and a plain socket client. `ss -tnp` then shows the emulator still holding the first
client's socket in `CLOSE-WAIT` (its fd 5), and every later connection sits in the listen
queue, never read.

**What is known:** the server does not notice that its client has gone, so it never closes that
socket or accepts the next one. Whether this is in nd100x's debugger thread
(`debugger_thread`, `dap_server_run`) or in libdap's TCP transport is not traced yet. Until it
is fixed, one debugging session has to do everything over a single connection.
