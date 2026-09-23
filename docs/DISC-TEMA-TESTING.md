# Testing the SMD controller with DISC-TEMA

DISC-TEMA is the Norsk Data service program for testing disks and disk
controllers. It is File 7 on `FLOPPY.IMG` (`DISC-TEMA-J02:TEST`) and runs
under the TPE monitor, like the other test programs in
`docs/TPE-AUTORUN.md`.

Source for everything in this document:

The manuals live in the NDInsight documentation collection, outside this repo.
Set `NDINSIGHT` to its root to follow the references:

- `$NDINSIGHT/Reference-Manuals/ND-830005.3 EN Test Program Description for ND-100-110.md`,
  chapter 5, lines 2608-3870 (the III version, 94 disc types). This is the
  source for everything below unless stated otherwise.
- `$NDINSIGHT/Installation/Installation-Description/ND-10324E.md` and
  `ND-10324E_Disc-Tema_version_D_info_NO.md` (version D notes).

Anything this document does not state is not stated in those manuals either.
Where the manual contradicts itself, both readings are given rather than one
being picked.

---

## 1. The rule that comes before everything else

**DISC-TEMA writes to the pack. Run it only against a throwaway copy.**

Never point it at `SMD0.IMG` or anything under
`images/`. Copy the image to a scratch directory and
pass the copy to `--smd0=`:

```sh
cp SMD0.IMG "$SCRATCH/dt.img"
build/bin/nd100x --boot=floppy --image=FLOPPY.IMG --smd0="$SCRATCH/dt.img" --pipe
```

This is not the ordinary "booting writes to the pack" drift. `FORMAT` destroys
the pack outright, `FUNCTION` destroys a cylinder every time it runs, and
`COPY` overwrites the destination disk completely. The command table in
section 5 marks every command READS or WRITES; check it before typing
anything.

---

## 2. Why the generic TPE recipe does not work here

`docs/TPE-AUTORUN.md` section 2 documents the pattern
used by `INSTRUCTION-C03`, `MEMORY-D04` and `PAGING-C02`:

```
<program-name>            % load
[ set-para,<args...> ]    % optional
run                       % execute
```

**The load works; the `run` does not.**

**There is no `run` command.** DISC-TEMA has 23 commands of its own and `run`
is not among them, which is why driving it the generic way answers
`*** No such command ***`. Loading was never the problem.

### How to load a TPE program - always use LOAD

```
TPE>LOAD DISC-TEMA <CR>
```

The bare program name also works, but **only while nothing has been loaded
yet**. Once any program is in the monitor, loading another one needs `LOAD`.

Use `LOAD` every time. It is correct in both states, and neither a written
procedure nor a script can be sure the monitor is still fresh - a previous
step, an operator, or a retry may already have loaded something. The bare name
is the special case, not the rule.

It is also not a single test that starts and finishes. It is an interactive
service program: load it, and it sits at its prompt waiting for one of 23
commands, most of which then ask a series of questions.

### Two console conventions that apply here and everywhere in TPE

- **`Command:` is the help filter prompt.** Typing `help` and pressing return
  does not print a list; it asks what to filter the listing by. A blank line
  or a filter string gets the listing. This is the same convention used
  throughout ND software.
- **A comma supplies one blank parameter inline**, so a command never has to
  prompt. `HELP,,,` is HELP with three blank parameters. The number of commas
  must match the number of parameters the command takes.

---

## 3. Starting it, and the disc type

```
TPE>LOAD DISC-TEMA <CR>
```

It then asks for a **DISC TYPE** before accepting any command. There are 94.
A bare `<CR>` takes the default, which is entry 23, `DISC-75MB-1`.

The type answers both the "FROM" and the "TO" disc. `SET-DISC-TYPE` changes
them individually afterwards, and version D additionally accepts the type
`VARYING`, which makes every command ask for its own type.

**For nd100x, the default is the correct answer.** nd100x's SMD presents the
75 MB geometry, and it matches the manual exactly:

| | `src/devices/smd/disk_smd.h:53-58` | manual prompt ranges |
|---|---|---|
| Surfaces | max 5 | `SURFACE NUMBER (DATA HEAD) (0-4 OCT.):` |
| Sectors  | max 18 | `SECTOR NUMBER (0-21 OCT.):` (0-17 dec) |
| Cylinders| 823 (0-822) | `CYLINDER NUMBER (0-1466 OCT.):` (0-822 dec) |
| Total sectors | 220526 octal | `AMOUNT (NO. OF SECTORS) (1-220526 OCT.):` |

`device_smd.c:996` confirms `DISK_75_MB` is the default type, and the device
is at IOX 1540-1547, ident 17, level 11.

**Open item, not yet resolved:** `SMD0.IMG` is 78,643,200 bytes, which is
76,800 sectors of 1024 bytes. The 75 MB geometry is 823 x 5 x 18 = 74,070
sectors. The image is therefore larger than the geometry describes, by 2,730
sectors. Whether that is padding, a different pack layout, or a mismatch has
not been established, and it should be before any address-range command is
trusted near the end of the disc.

---

## 4. What DISC-TEMA never asks for

There is **no controller-address or device-number prompt anywhere in the
chapter.** The disc *type* implies the controller. So the earlier assumption
that it would ask for an IOX address was wrong.

"Subunit" appears only in the phrase "unit- (subunit-) number" and in a note
about "artificially divided subunits (2-75MB or 3-75MB)". There is no separate
subunit prompt: the subunit is encoded in the type name, e.g. `DISC-2-75MB-1`
against `DISC-2-75MB-2`.

Unit prompts vary in spelling between commands, which matters for any harness
matching on prompt text:

```
Unit (0-1 Oct.):          CLEAR-DEVICE, MATCH, COPY, PARITY-CHECK
Unit (0-3 Oct):           ALIGN
Unit [0-3 Oct.]:          FORMAT           (square brackets)
Unit (0 to 3 oct):        DUMP-FLAW-TABLE, REFRESH
UNIT(S) {0-3 Dec.}:       FUNCTION         (decimal, 1-4 units, space or comma separated)
UNIT NUMBER (0-3 OCT.):   COMPARE, VERIFY, DUMP-DISC-CONTENT, SET-DISC-CONTENT, SEEK
```

Numbers are octal unless the prompt says `Dec.`. A bare `<CR>` takes the
default. Commands may be abbreviated and typed in lower case.

---

## 5. The 23 commands, by whether they write

### Safe - read only

| Command | What it does | Prompts |
|---|---|---|
| `PARITY-CHECK` | Reads one disk area without storing it in memory, to see whether addresses and data read without ECC error. The safest command in the program. | `Unit (0-1 Oct.):` |
| `VERIFY` | Reads two disk areas and compares them word by word in software. Terminates by itself after the given amount. | two address blocks, then `AMOUNT (NO. OF SECTORS) (1-216374 OCT.):` |
| `COMPARE` | Compares memory against disc using the hardware bit-compare on the interface. | two address blocks, `AMOUNT...`, `DISC AREAS OVERLAP EACH OTHER. OK ? (YES/NO):` when they overlap, `BLOCK SIZE (1-132 OCT.):` |
| `MATCH` | COMPARE until a compare error, then VERIFY at that spot. Cannot run under SINTRAN. | FROM unit, TO unit |
| `SEEK` | Consecutive seeks between two addresses. Head motion only, no transfer. **Runs forever until ESCAPE.** | FROM block with unit, TO block without unit |
| `CLEAR-DEVICE` | Return-to-zero seek. No transfer at all. | `Unit (0-1 Oct.):` |
| `DUMP-DISC-CONTENT` | Dumps an area to the printer device; a line identical to the one before prints as `SAME`. Needs `SET-PRINTER-DEVICE-NUMBER` set first. | address block, `AMOUNT (NO. OF SECTORS) (1-22 OCT.):` |
| `DUMP-FLAW-TABLE` | Prints the bad-track/alternative-track table from the last track of the disc. | `Unit (0 to 3 oct):` |
| `TRANSLATE` | Converts between physical, logical and page addresses, and decodes a status word to text. No disc access. | `ADDRESS TYPE:` then cylinder/surface/sector |
| `SET-DISC-TYPE` | Sets the FROM and TO disc types. | `SET NEW "FROM" DISC NAME:`, `SET NEW "TO" DISC NAME:` |
| `SET-PARAMETERS` | The 12 mode settings, section 6. | the 12 prompts |
| `CLEAR-COUNTERS` | Clears the ECC, retry, marginal-recovery and transfer counters. | five `CLEAR ... (YES/NO):` questions |
| `PRIORITY-SELECT` | Dual-port option only; holds a drive until the next command finishes. Not executable under SINTRAN. | not stated |
| `ALIGN` | Head alignment. Needs a removable pack and a special ALIGN pack; errors with `No align cylinder for this disk.` otherwise. | `Unit (0-3 Oct):`, `CYLINDER`, `HEAD`, then single-key sub-commands |
| `ALLOCATE-BUFFERS` | Inspects and alters the memory buffers used for disc DMA. Prompt is `Buffalo:`. Not possible on ST506-type interfaces. | menu 1-4 |

### Destructive - scratch copy only

| Command | What it destroys |
|---|---|
| `FORMAT` | **The whole pack.** "This command will destroy the existing data on the disk." Asks `Do you still want to continue (Yes/No):` and then runs a table editor with `APPEND`, `DELETE`, `LIST-TABLE`, `REMOVE-TABLE`, `START-FORMATTING`. |
| `COPY` | **The destination disk.** "This command will 'destroy' the data on the destination disk." Cannot run under SINTRAN. |
| `FUNCTION` | **One cylinder, every run.** See section 7. |
| `SET-DISC-CONTENT` | Writes a generated pattern over the sectors given. |
| `CHANGE` | Inspects and changes single words on the disc. The manual gives no example and no prompts. |
| `REFRESH` | Reformats one track and writes the data back. Phoenix discs cannot be refreshed in the III version. Warns if the track was already reallocated, and the manual says do not continue in that case. |
| `RANDOM` | Only when `TEST-TYPE` is set to `write-compare`. See section 8. |

### Unclassified

`SCOPE-LOOP` is listed as a command name and **described nowhere** - not in
chapter 5, not in ND-10324E, not in ND-210523G. No description, no prompts, no
statement of whether it writes. Treat it as unknown, not as safe.

---

## 6. SET-PARAMETERS, and the settings a real test needs

Defaults are in angle brackets, as the manual prints them.

| # | Prompt | Default | For a thorough test |
|---|---|---|---|
| 1 | `Directory mode on (YES or NO):` | `<YES>` | YES gives unit-only addressing; NO requires full cylinder/surface/sector and an amount |
| 2 | `Marginal recovery allowed (YES or NO):` | `<YES>` | **NO** - "For a thorough test, marginal recovery should not be allowed." |
| 3 | `ECC correction allowed (YES or NO):` | `<YES>` | **NO** - "For a thorough test, ECC operation should not be allowed." |
| 4 | `Single surface format (YES or NO):` | `<NO>` | leave NO - "Should not be used. Only for debugging HW." |
| 5 | `Override obligatory reallocating while formatting (Yes/No):` | not stated | leave alone - "This is not recommended." |
| 6 | `No of retries on READ (0 to 64 dec):` | `<27>` | **0** - "For a thorough test, READ retries should be 0." |
| 7 | `No of retries on WRITE (0 to 64 dec):` | `<3>` | **0** - "For a thorough test, WRITE retries should be 0." |
| 8 | `Number of test patterns to use during formatting:` | `<3>` | more patterns finds more bad tracks. Range is printed as `(1-3 Dec.)` in the table and `(0 to 3 dec)` in the explanation - the manual contradicts itself |
| 9 | `No of bits to accept before reallocating (1 to 11 dec):` | `<1>` | **1**. No effect if ECC is disallowed |
| 10 | `Skip any test in function (YES or NO):` | `<NO>` | YES makes it ask about each test in FUNCTION individually |
| 11 | `Number of times for function to loop (1 to 65535 dec):` | `<4>` | the FUNCTION loop count; ESCAPE also ends it |
| 12 | `Do you want to release units after operations (YES or NO):` | `<NO>` | only relevant with a disc switch and ECC disc |

The first three matter most: **left at their defaults, DISC-TEMA hides exactly
the errors an emulator test is trying to find.** Marginal recovery retries with
the heads off track, ECC correction repairs the data in software, and 27 read
retries paper over an intermittent failure. All three must be off for a result
to mean anything about the SMD implementation.

---

## 7. FUNCTION - the closest thing to an automated test

This is the command that most resembles `run` in the other TPE programs, and
it is **destructive**, though in a bounded way.

```
TPE>function <CR>

DISC-75MB-1

On this disk type FUNCTION will "destroy" data in the last cylinder in the
spare track buffer pool.
Most new disks have an area reserved for function test. The data pool and the
flaw-table are not affected by the function test.

DISC-TEMA will give a warning ONLY when data may be lost.

Do you still want to continue (Yes/No):Y <CR>

Specify from 1 to 4 unit numbers, terminated by CR. Separate the numbers with
a space or a comma (,). The units specified must be turned ON and ready. The
units NOT specified must be turned OFF, or nonexistent.
UNIT(S) {0-3 Dec.}: 0 1 2 3 <CR>
TPE>
```

Points that decide whether it can be automated:

- **What it tests:** "Function is mainly a test of the disk interface, and the
  disk drive and the disk pack is only partially tested." The individual
  subtests are never named in the manual.
- **What a pass looks like:** the example simply returns to `TPE>` with no
  output. There is no "end of test" banner and no pass marker. A failure shows
  up as one of the error messages in section 9.
- **This breaks the usual harness classification.** `tools/tpe_autorun.py`
  decides PASS/FAIL/INCONCLUSIVE by scanning for markers. FUNCTION has none,
  so silence is success and any error text is failure - the inverse of how the
  other programs are read.
- **Unit requirement:** "The units specified must be turned ON and ready. The
  units NOT specified must be turned OFF, or nonexistent." For nd100x that
  means only configured `--smd0..3` units may be listed.
- **Loop count** comes from SET-PARAMETERS item 11, default 4. ESCAPE ends it.
- **Version C bug, fixed in D:** on a 38 MB disc, FUNCTION used cylinder 0 for
  all tests and destroyed it. Not relevant at 75 MB, but it shows the "bounded"
  area is version- and type-dependent and should not be assumed.

---

## 8. RANDOM - a media and servo stress test

`RANDOM` enters its own sub-prompt, `RND:`, with commands `RUN-TEST`,
`ADDRESS-MODE`, `TEST-TYPE`, `LIMIT-ADDRESSES`, `AMOUNT-SET`, `WRITE-PROTECT`,
`LIST-PARAMETERS`, `SET-PARAMETERS`, `EXIT`.

The safety statement is explicit: "The DATA on the disk media will NOT normally
be ruined by D-T random test, unless WRITE is specified. To enable write, you
must bypass write protect." The write-protect flag starts ON and makes the
program ask before any write operation.

`TEST-TYPE` decides everything:

1. `read parity` - checks parity at the calculated address, no transfer
2. `read only` - transfers to memory
3. `read-compare` - the interface compares the memory buffer against the disc
4. `write-compare` - **writes to the disc**, then compares
5. `seek-only` - seek to the calculated track, finished on ON CYLINDER

Default is read and compare, so **RANDOM at its defaults is read only**.

`ADDRESS-MODE` chooses `INCREMENT-DECREMENT` or `RANDOM`; the manual notes the
random pattern repeats identically on a new RUN unless the test area size
changes, which makes it reproducible. `EXIT` resets every RANDOM parameter.

Worth knowing: RANDOM's own initial values are the **opposite** of the global
SET-PARAMETERS defaults. "Initially ECC correction and marginal recovery are
not allowed. Initially no read or write retries are allowed." That is the
strict setting the global parameters have to be talked into.

`RUN-TEST` prints a list of the operations and the commands accepted during a
run without interrupting it. Neither that text nor the termination condition
is stated in the manual, so **how a RANDOM run ends is unknown** and must be
established by experiment before it is automated.

---

## 9. Error messages

An error report is three parts: the message text, a numbered print note, and a
16-bit controller status code that `TRANSLATE` decodes.

### Status word bits

| Bit | Meaning |
|---|---|
| 0 | Controller not active interrupt enabled |
| 1 | Error interrupt enabled |
| 2 | Controller active |
| 3 | Controller finished with a device operation |
| 4 | Inclusive OR of errors (bits 5-13) |
| 5 | Illegal load, i.e. load while the unit is not on cylinder |
| 6 | Timeout |
| 7 | Hardware error (disk fault + missing read clocks + missing servo clocks + ECC parity error) |
| 8 | Address mismatch |
| 9 | CRC error |
| 10 | Compare error |
| 11 | DMA channel error |
| 12 | 0 |
| 13 | Disc unit not ready |
| 14 | On cylinder |
| 15 | 0, used to distinguish from 10 Mb |

The manual says "Abnormal completion" decodes bit 12, while the table gives
bit 12 as 0. One of the two is wrong and it is not clear which.

### Print notes

1. Controller did not respond to the IOX instruction. Error in the controller,
   wrong device number (switch setting), or controller missing.
2. An interface gave an interrupt on level 11, which is not legal.
3. Error on the controller.
4. Same as 3.
5. Error on the interrupt system.
6. Timeout without hardware status error from the disk.
7. CPU and controller have different DMA channels, so overrun cannot be
   provoked.
8. The test found a bad track that was not in the flaw table.

### The messages most likely to appear against an emulator

These are the ones that indicate the emulated controller, rather than a real
drive, is at fault:

- `Illegal load` - "Disc-Tema has initialized the controller while the disk was
  active." Directly a controller state-machine issue.
- `Log. block address out of range.` - "Disc-Tema tried to access an address
  outside the disk's area." Relevant given the open geometry question in
  section 3.
- `Address mismatch` - status bit 8; any error in a track's address field.
- `CRC error` - status bit 9, in the data field.
- `Compare error` - status bit 10; memory and disc data differ.
- `Core address register not as specified` - a transfer did not finish.
- `Controller not active after activate` - status bit 2.
- `Disc unit not ready` - status bit 13, the disk has not started.
- `DMA channel error (FIFO over/underrun or ND-100 Bus error)`
- `Data way error` - between controller and memory.
- `Error in Test-Mode`
- `No alternative track table found` - hardware error, the disk was never
  formatted, or the table track is destroyed.
- `No table on this disk type`
- `Not on cylinder.` - status bit 14.
- `Error After READ` / `Error After COMPARE` - "Error after timeout. This
  general HW-error could be caused by almost anything in the system."

---

## 10. Preconditions

- **Version D only:** disk unit 0 must always be spinning. From
  `ND-10324E_Disc-Tema_version_D_info_NO.md`: "Ved bruk av DISC-TEMA, versjon D,
  ma disk-unit 0 ALLTID vaere i gang ('ga rundt')." The note says this applies
  only to version D and was to be corrected in the next version. The floppy
  carries J02, so it probably does not apply, but that has not been confirmed.
- `MATCH`, `COPY` and `PRIORITY-SELECT` cannot run under SINTRAN. Under nd100x
  they are driven from TPE, not SINTRAN, so this does not bite - but it does
  mean they expect exclusive control of the drive.
- `ALIGN` needs a removable pack and a special alignment pack.
- `ALLOCATE-BUFFERS` automatic mode is not possible on ST506-type interfaces.

---

## 11. Suggested order for testing the nd100x SMD

Nothing here has been run yet. This is the order the manual's own safety
statements imply, cheapest and safest first.

1. `TRANSLATE` - no disc access at all. Confirms the program is alive and that
   the geometry it believes in matches section 3.
2. `DUMP-FLAW-TABLE` - reads one track. Tells us whether nd100x presents a
   readable flaw table, or answers `No alternative track table found`.
3. `PARITY-CHECK` - reads an area with no memory transfer. First real exercise
   of the read path.
4. `DUMP-DISC-CONTENT` on a few sectors - needs `SET-PRINTER-DEVICE-NUMBER`
   first, and gives readable output to compare against the image bytes.
5. `SEEK` - head motion only. **Needs ESCAPE to stop**, so any harness must be
   able to send it.
6. `VERIFY` or `COMPARE` over a bounded range - exercises DMA and the
   hardware compare path, and terminates by itself.
7. `RANDOM` at its defaults - read and compare, write protect on.
8. `FUNCTION` - **destroys a cylinder**. Scratch copy only, and only once the
   read paths above are known good.
9. `FORMAT` - **destroys the pack**. Only ever on a copy that can be thrown
   away, and only to test the format path deliberately.

Before any of it, set SET-PARAMETERS items 2, 3, 6 and 7 to NO/NO/0/0, or the
results will not mean anything.

## 12. What the harness needs before this can be automated

`tools/tpe_autorun.py` cannot drive DISC-TEMA as it stands. The gaps:

- Load with `LOAD <name>`. The bare name only works while nothing has been
  loaded, and a script cannot count on that.
- Answer the disc-type question that arrives before any command.
- Drive a command vocabulary instead of `run`, including nested sub-prompts
  (`RND:`, `Buffalo:`, `*:`, `ADDRESS TYPE:`, and ALIGN's single-key `Press:`).
- Match six different spellings of the unit prompt (section 4).
- Send ESCAPE, which `SEEK` requires and `FUNCTION` and `RANDOM` accept.
- Treat silence as success for FUNCTION, which inverts the existing marker
  logic.
- Know which commands write, and refuse to run them against anything but a
  scratch copy.
