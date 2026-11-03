
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

### P2. Backup Manager B02 COPY to the streamer: "No files copied", nothing reaches the tape

**Found by:** running BM-OPERATOR on a pack made by the `tools/installs/` chain (BACKUP-SYSTEM
I05, ND-500 monitor J04, LINKAGE-LOADER H02, LINKER B01, Backup Manager B02), with the SCSI
streamer set to 1.1 in S3-CONFIG, `SET-PERIPHERAL-FILE "STREAMER-1" 2226B`, and a tape loaded and
rewound, 2026-11-02.

**Shows up as:** the line-mode COPY of user area `RONNY` (one file, `TEST:SYMB`) from
`PACK-ONE` to volume `BMTEST-1` on `STREAMER-1`, unit 0, mode OV, `STOP-ON-ERROR`, ends with
`No files copied` and no SCSI command reaches the drive. A COPY of the same user area to another
directory on `PACK-ONE` works ("File(s) copied: 1").

**How to run it:** machine `ND120CX` with `fpp = 32`; XMSG first (`START-XMSG` in
SINTRAN-SERVICE-PROGRAM), then `@START-SERVERS`, then the ND-5000. In any other order the
swapper takes the multiport memory and the NUCLEUS servers find no room for their LAMUs. ND
gives the same order (ND-895061-1A, the LOAD-MODE step). `--log=mms:info` logs every ND-5000
monitor call with its answer.

**Fixed on the way (2026-11-02 and 2026-11-03):**

- CNREK cleared the page-table slots of the wrong logical pages, so the ND-500 monitor never
  answered on any ND-110/ND-120 model (`src/cpu/cpu_instr.c`, vector `CNREK/bit_0_logad_100`).
- NKSERV needs CPU type 5 (ND120CX with `fpp = 32`); with it NUCLEUS starts and error 101025B
  "Nucleus not started" is gone.
- The 3RMICV reply (nd500x `ndbus_servicer`) carried CPU parameter 0x03E1 (001741B). The B30
  microcode puts the ACCP's model answer in that cell, 0x0038 on an ND-5800 (CPU_READ @017137;
  001741B is the CPUMOD08 constant, which goes to a different cell). The NUCLEUS server reads
  bits 0-3 as the ND-5000 model and refused model 1 with 101035B "Not correct hardware
  configuration" on the first NUCLEUS call of every COPY. Fixed in nd500x; now the first three
  NUCLEUS calls answer 0.

**What still goes wrong (measured 2026-11-03, every ND-5000 monitor call logged):**

- The second COPY in a BM-OPERATOR run reads the user and the file (GetUserEntry,
  GetFileIndexes, GetObjectEntry), walks the directory entries, reads GetSystemInfo, makes six
  XMSG calls, then four NUCLEUS calls. The fourth is `nkOpenPort` of a name that starts with
  "DP-NAM" - `DP-NAME-SERVER`, the DOMINO SCSI name server (the string is in
  `BM-FILERE-B02:DOM` next to `DP-100-SERVER`). It answers 101012B `nke_NAMENOTFOUND`
  (ND-820026 Appendix C). BM-OPERATOR then prints `No files copied` and makes no further call.
- There is no DOMINO SCSI on this machine, so the name not being there is correct. BM-INST
  (ND-895055-1A, section 8) says Backup Manager recognises an ND-100 tape drive by its
  peripheral file (`Streamer-1` = 2226B), and needs no peripheral file for a DOMINO device. So
  Backup Manager did not take `STREAMER-1` as an ND-100 drive, although `(SYSTEM)STREAMER-1`
  exists. Why not is **not known**. No monitor call in the sequence names the peripheral file.
- The FIRST COPY in every BM-OPERATOR run stops much earlier: GetDirNameIndex, one GetDirEntry,
  then `No files copied`. Cause found: the 42 bytes of directory entry that GetDirEntry returns
  were thrown away by the bridge because that page of the process was not in memory - see P3.
  The second COPY makes the same call when the page is present and goes on.
- `Error 33626 (octal)` on `(SYSTEM)SWAP-FILE-0:SWAP` at BM-OPERATOR start comes from the
  `IMAGE-NO-COMPARE-FILE` line in `BM-PARAMETER-B02:DATA`; the file does not exist on this pack.
- PROMAN still reports "No domino controllers found in system" (correct), "Not read, write and
  common access" and "Unable to log events to ring buffer file" at `@START-SERVERS`. MTSERV
  reports "MTAD server initiated and started".

**Ruled out:** the four NUCLEUS instructions that ND microcoded on the ND-5000 (`SEND`,
`RECVE`, `GETINF`, hole move) are not in nd500x, but no illegal-instruction trap happens in
these runs (281 traps, all page faults), so they are not reached yet.

**Next:** find what Backup Manager checks to decide that `STREAMER-1` is an ND-100 drive -
decode the six XMSG calls before the NUCLEUS calls.

### P3. ND-5000 monitor-call answer data is dropped when the target page is not in memory

**Found by:** the full ND-5000 monitor-call log of a BM-OPERATOR COPY (P2), 2026-11-03.

**Shows up as:** `MFbus: ND-5000 at 70 3WMONCO answer data: 42 byte(s) from pool 0x0E4800 ->
logical 0x0805F39C, 0 written, 42 skipped as unmapped`. The call (MON 244B GetDirEntry) answers
FUNCV 0 with no error, so the program goes on with a buffer SINTRAN never filled. Backup Manager
then copies nothing and says only `No files copied` (P2, the first COPY of every run). One drop in
a run of 298 answer-data copies; which one depends on what the swapper has paged out.

**Where:** `src/machine/mfbus_bridge.c`, the 26B answer-data block copy after a 3WMONCO
restart. It writes each byte through `nd500_mmu_peek()` and skips any byte whose page is not
present, by design (the comment says a fault there would be reported as the process's own).

**What the hardware does:** the B30 microcode copies this block in `MSG_CONWR`
(@015752-016004) with ordinary `RD,POF` / `WRITE` accesses through the process's own address
space (ND5000UC `docs/ND5000-DOM-LOAD-MONCALL-MICROCODE-REFERENCE-2026-08-20.md` section 3.4). A
missing page is therefore a normal ND-5000 page fault. **Not known:** what the microcode does
with a page fault inside `MSG_CONWR` - which trap it raises, what it reports to the swapper,
and how the copy and the restart continue once the page is in. That has to be read out of the
microcode (the WRITE fault path from @016000/@016004) before this can be fixed; skipping the
bytes is not what the machine does.

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
