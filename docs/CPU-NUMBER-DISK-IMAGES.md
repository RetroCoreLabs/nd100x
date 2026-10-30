# Making a disk image with its own CPU number

This chapter shows how to take one standard SINTRAN installation image and
make many copies of it, each with its own CPU number, so that machines running
XMSG or COSMOS can talk to each other.

Everything here was run on throwaway copies of a SINTRAN K pack and a SINTRAN
L pack. Where a step was not run, the text says "not verified".

## 1. Why every machine needs its own CPU number

- Every SINTRAN system has a CPU number (the kernel word `SYSNO`). XMSG and
  COSMOS use it to tell "this machine" from "another machine". Two machines
  with the same number look like the same machine, and they cannot be told
  apart on the network.
- The Ethernet card builds its MAC address from the CPU number (see
  [ETHERNET.md](ETHERNET.md), section 10: the COSMOS image with CPU number 100
  has MAC 08:00:26:64:00:00). Copies of one image share a CPU number and so
  share a MAC address.
- A new copy of a standard image has the CPU number of the image it came from.
  So every generated image must be given a new number before its first boot.

## 2. What nd100x already says about the CPU, and what was missing

Already in the docs before this chapter:

- `--cputype=TYPE` and `ND100X_CPUTYPE`: [TPE-AUTORUN.md](TPE-AUTORUN.md),
  section 5, and `nd100x --help`.
- "Directory entered by another system": one paragraph in
  [ND-DOMAIN-GOTCHAS.md](ND-DOMAIN-GOTCHAS.md).
- The MAC address follows the CPU number: [ETHERNET.md](ETHERNET.md).

Missing, and covered here:

- How to set the CPU number (on the disk, or with an environment variable).
- The difference between the CPU number on the pack and the CPU model of the
  emulator (section 3).
- How to change the number on an image and clear the stamp (sections 5 to 7).
- How to make many images with different numbers (section 8).

## 3. Three different things called "CPU"

| Thing | What it is | Where it is set |
|---|---|---|
| CPU model | Which CPU the emulator acts as: `ND100CX`, `ND110CX`, `ND120CX` and so on. Decides which instructions exist and what the test programs (TPE) print. | `--cputype=NAME`, or `ND100X_CPUTYPE`, or `cpu =` in an .ini file |
| CPU number | The SINTRAN `SYSNO` word, the number shown as `CPU NUMBER` on the boot banner. This is what XMSG and COSMOS use. | In the pack (this chapter), or `ND100X_CPU_NUMBER` |
| CPU type (order code) | The SINTRAN word `HWINFO(2)`, shown as `CPU TYPE` on the banner, for example 100, 110 or 9883. SINTRAN never reads it. | In the pack, or `ND100X_SYSTEM_TYPE` |

The numbers on the pack and `--cputype` are not the same thing. `--cputype`
does not change the number or the order code. The value 9883 on a banner is
an order code, not a model name.

### How nd100x is told the CPU model (verified in the source and with `--help`)

- `--cputype=NAME` (case does not matter). Valid names from `--help`: ND1, ND4,
  ND10, ND100, ND100CE, ND100CX, ND110, ND110CE, ND110CX, ND110PCX, ND120CX.
- Without `--cputype` the program starts as `ND100CX`
  (`src/frontend/nd100x/nd100x.c`).
- `[machine] cpu =` in an .ini file accepts 100, 110, 120 or a model name
  (see `config/nd100-cosmos.ini`).
- `ND100X_CPUTYPE` is read inside `cpu_init()` and accepts only ND100,
  ND100CE, ND100CX, ND110, ND110CE, ND110CX and ND110PCX. It does not accept
  `ND120CX` (`cpu_set_type_from_env` in `src/cpu/cpu.c`; `--help` lists the
  same seven). Use `--cputype=ND120CX` for an ND-120.

### What the ND-120 does differently (verified by reading the source)

- The instruction set is the same as the ND-110/CX.
- The VERSN instruction answers as an ND-120 (bit 15 of the T register) and
  reports print number 3202, release D (`src/cpu/cpu_instr.c`).
- Not verified: what else of the ND-120 the TPE tests read.

### Why the model matters for the CPU number

SINTRAN reads the identity PROM (instruction VERSN) only when it sees an
ND-110 or ND-120. On the default `ND100CX` model SINTRAN never looks at it. So:

- Start SINTRAN images with `--cputype=ND120CX` (or an ND110 model) if you
  want the identity to be read at all.
- The default PROM in nd100x has no valid signature, so SINTRAN keeps the
  number written on the pack. I started a K pack with `--cputype=ND120CX` and
  it printed the number I had written on the disk.
- Setting `ND100X_CPU_NUMBER`, `ND100X_SYSTEM_TYPE` or `ND100X_LEGAL_USERS`
  switches the signature on. SINTRAN L and later then take the number from the
  environment variable at every start and ignore the pack. I ran an L pack
  with `ND100X_CPU_NUMBER=777` and the banner said `CPU NUMBER: 777`, and
  `CPU TYPE: 100` (nd100x default for the type).
- SINTRAN K stops before the banner when the signature is on (documented in
  the NDInsight chapter; in my run with `ND100X_CPU_NUMBER=777` on a K pack no
  banner appeared in 12 seconds, which fits, but I did not look deeper).
  Do not set the variables for K packs.

The environment variable is the quick way to give a machine a number when the
pack is an L pack, and it needs no change to the image. The disk way (below)
works for K and L and also fixes the directory stamp. There is no .ini key and
no command line option for the CPU number (I found none in
`src/frontend/nd100x/config.c` or `src/machine/machine_config.c`).

## 4. What lives on the pack

This is a summary. The full measured description is chapter 28,
"CPU number and type on a SINTRAN pack" (file
`28-CPU-NUMBER-AND-TYPE-ON-DISK.md`, folder `SINTRAN/OS`), in the NDInsight
repository.

- The CPU number is stored in the SINTRAN system data page, word `SYSNO`, and
  a second copy `FCPUN`. The CPU type is the word `HWINFO(2)`.
- The page exists in three copies: a live copy in memory (not on disk), an
  image copy and a save copy. Both disk copies must be changed. A cold start
  copies save to image to memory. A warm start uses the image copy.
- On a K pack the save copy is in the file `SINTRAN:DATA`. On an L pack both
  copies are in `SEGFIL0:DATA`. The tool finds them itself.
- The address of `FCPUN` is different in each SINTRAN build, so the tool reads
  it from the symbol lists on the pack. A pack without those lists is refused.
- The generation numbers `33CPN` and `33CPU` in `SYMBOL-2-LIST:SYMB` are
  changed too, so a later patch run does not put the old number back.

### The "entered" stamp

Page 0 of the pack holds a flag "directory entered" and the number of the
system that entered it. When SINTRAN enters the directory and the flag is
set with a number that differs from its own CPU number, it refuses with:

```
DIRECTORY ENTERED BY ANOTHER SYSTEM
NO MAIN DIRECTORY
```

and every terminal says SYSTEM UNAVAILABLE. The pack is not damaged; the
stamp only has to be cleared. A pack that was running when the emulator was
stopped keeps the stamp. A normal boot sets the stamp to the pack's own
number.

I saw this refusal: I changed a booted copy to number 303 with `--keep-stamp`
and started it again; the console printed `DIRECTORY ENTERED BY ANOTHER SYSTEM`
and `NO MAIN DIRECTORY`. The same copy booted after the stamp was cleared
(chapter 28 shows this too, with `ndtool --release`).

## 5. The tool: ndtool

The tool is `ndtool` from the norskdata-ndfs repository (folder
`ndfs-c/tools/ndtool`, version 0.0.8 when this was written). Build it from
`ndfs-c` in that repository. It is also built in the browser (section 9).

Commands (from `ndtool` usage text and its README):

| Command | What it does |
|---|---|
| `ndtool --cpu-identity IMAGE` | Show the CPU number and type in both disk copies, `33CPN`/`33CPU` and the stamp. Changes nothing. |
| `ndtool --set-cpu NUMBER[:TYPE] IMAGE` | Set the number (and type) in both copies and in `33CPN`/`33CPU`, and clear the stamp. |
| `ndtool --keep-stamp --set-cpu ...` | The same, but leave the stamp as it was. Only for tests. |
| `ndtool -n --set-cpu ...` | Show what would change. Writes nothing. |
| `ndtool --release IMAGE` | Only clear the stamp. |
| `ndtool -n --release IMAGE` | Show what would be cleared. |

Numbers are decimal. The type is optional; without it the type is not touched.

The same read and write also exist in the libraries that live in the
norskdata-ndfs repository next to ndtool (chapter 28, section 8.2a; I did not
run these myself):

| Library | Read | Write |
|---|---|---|
| ndfs-c | `ndfs_read_cpu_identity` | `ndfs_write_cpu_identity` |
| ndfs-py | `ndfs.sintran_identity.read_cpu_identity` | `write_cpu_identity` |
| ndfs-ts | `readCpuIdentity` | `writeCpuIdentity` |
| RetroIO.Storage.Ndfs (C#) | `ReadSintranCpuIdentity` | `WriteSintranCpuIdentity`, `ReleaseDirectory` |

Use one of them when the images are made from a program instead of a shell
script. They follow the same steps as ndtool.

Close the emulator before you run ndtool on an image. It writes straight into
the image file.

## 6. Recipe: one image, one new CPU number

Work on a copy. Never run these commands on your master image.

1. Copy the master.

   ```
   cp BIGDISK0-K-100.IMG my-node-201.IMG
   ```

2. Read the identity.

   ```
   ndtool --cpu-identity my-node-201.IMG
   ```

   Output from a standard K pack (shortened to the lines that matter):

   ```
   SINTRAN version: K (from INIBU 067172)
     33CPN             100  (000144 octal)
     33CPU             110  (000156 octal)
   Segment 44 S3IDPIT (image copy)
     CPU number        100  (000144 octal)
     CPU type          110  (000156 octal)
   Segment 53 S3SDPIT (save copy)
     CPU number        100  (000144 octal)
     CPU type          110  (000156 octal)
   Directory stamp (page 0): entered, system number 100 (144 octal)
   ```

   Here "entered, system number 100" is the stamp that must be cleared.

3. Preview, then set the new number and type.

   ```
   ndtool -n --set-cpu 201:9883 my-node-201.IMG
   ndtool    --set-cpu 201:9883 my-node-201.IMG
   ```

   Real output of the second command:

   ```
   CPU number 100 -> 201, CPU type 110 -> 9883, in SYSTEM/SEGFIL0:DATA page 27
   and SYSTEM/SINTRAN:DATA page 27, 33CPN in SYSTEM/SYMBOL-2-LIST:SYMB,
   directory stamp (system 100) cleared
   ...
   Done. The pack now reports CPU NUMBER 201 and the new CPU TYPE at the next boot.
   ```

   (The tool prints the first sentence on one line; it is wrapped here.)
   Choose the type: ND-110 and ND-120 packs seen here use 110 or 9883. SINTRAN
   never reads the type, so any number boots (chapter 28, section 6).

4. Read it back. `--set-cpu` does this itself, but run it again to be sure.

   ```
   ndtool --cpu-identity my-node-201.IMG
   ```

   All four lines `CPU number` and `CPU type` (two copies) must show the new
   values, and the last line must read:

   ```
   Directory stamp (page 0): not entered, system number 0 (0 octal)
   ```

5. Boot the result as an ND-120.

   ```
   nd100x --boot=smd --smd0=my-node-201.IMG --cputype=ND120CX
   ```

   The banner (real output of a K pack set to 202:9883):

   ```
    SINTRAN III - VSX/500 K
    STANDARD CONFIGURATION:    D
    CPU TYPE:     9883
    CPU NUMBER:    202
   SINTRAN III RUNNING -
   ```

   The proof inside SINTRAN is the command `@LIST-TITLE`, which prints the
   same `CPU TYPE` and `CPU NUMBER` (chapter 28, section 9). I did not run
   `@LIST-TITLE` myself; the banner is what I saw.

6. After the first boot the stamp says "entered, system number 201". That is
   normal. If you later change the number of that image, use `--set-cpu`
   without `--keep-stamp`, or run `--release`.

### Setting only the stamp

To clear the stamp without changing the number (for example a pack that was
left entered by another machine):

```
ndtool --release IMAGE
```

Real output: `Directory released: entered flag cleared, system number 144
(octal) -> 0`. A second run prints `Directory is not marked entered and has no
system number: nothing to do`.

## 7. Where to run ndtool

- Linux and WSL: build `ndfs-c` and run `ndtool`.
- Windows: the same source builds to `ndtool.exe`.
- I ran the WSL build. I did not run the Windows build. Not verified.

## 8. Many images from one master

The script below makes COUNT copies of one master image. Each copy gets the
next number, and the script reads the number back from each image.

Numbering rule: first number plus the position in the list. Pick a first
number nobody else uses. Stay in the range 1 to 32767 (21238 was verified; I
did not test numbers above 32767). Do not use 0 (the stamp check treats it as
"no number") or 65535 (the value -1 means "keep SINTRAN's own value" in the
identity PROM).

```sh
#!/bin/sh
# gen-images.sh MASTER.IMG COUNT [FIRST_NUMBER] [TYPE]
# Makes COUNT copies of MASTER.IMG, each with its own CPU number.
set -e
MASTER=$1; COUNT=$2; FIRST=${3:-201}; TYPE=${4:-9883}
NDTOOL=${NDTOOL:-ndtool}
OUT=${OUT:-./images}
mkdir -p "$OUT"
i=0
while [ "$i" -lt "$COUNT" ]; do
  N=$((FIRST + i))
  F=$OUT/SMD-$N.IMG
  cp "$MASTER" "$F"
  $NDTOOL --set-cpu "$N:$TYPE" "$F" >/dev/null
  # read the number back from both disk copies; both must be N
  GOT=$($NDTOOL --cpu-identity "$F" | awk '/CPU number/ {print $3}' | sort -u | tr '\n' ' ')
  [ "$GOT" = "$N " ] || { echo "FAIL $F reads '$GOT' wanted $N" >&2; exit 1; }
  echo "$F  CPU number $N"
  i=$((i + 1))
done
# every image must hold a different number (the numbers are read from the images)
for F in "$OUT"/SMD-*.IMG; do
  $NDTOOL --cpu-identity "$F" | awk '/CPU number/ {print $3; exit}'
done | sort | uniq -d | { read D && { echo "DUPLICATE number $D"; exit 1; } || echo "all numbers are different"; }
```

Real output with a K master and `NDTOOL=... ./gen-images.sh BIGDISK0-K-100.IMG 3 401`:

```
./images/SMD-401.IMG  CPU number 401
./images/SMD-402.IMG  CPU number 402
./images/SMD-403.IMG  CPU number 403
all numbers are different
```

I also copied image 402 over image 403 by hand and ran the last loop of the
script: it printed `402`, the repeated number, so the check does catch a
duplicate.

Notes:

- Each image is a full copy (75 MB for the pack used here). A copy and a
  `--set-cpu` took a moment each. Delete the images when the test is over.
- If the master is already used by another machine, copy it first and treat
  the copy as the master.
- Give every machine a different IP address too (see [ETHERNET.md](ETHERNET.md),
  section 10). The CPU number and the IP address are separate settings.
- Start each machine with `--cputype=ND120CX` (or an ND110 model) and do not
  set `ND100X_CPU_NUMBER`, so every machine uses the number on its own pack.

## 9. The same in the browser window

The web front end (`template-glass`) has a menu item "CPU Identity (disk)"
(`template-glass/index.html`, code in `template-glass/js/sintran-cpu-identity.js`).
From reading that file:

- It shows the same information as `ndtool --cpu-identity` for the pack on SMD
  unit 0, plus the table of segments.
- It has a form "Change CPU number / type on this pack" with a CPU number
  field, a CPU type field (blank keeps the type) and a checkbox to keep the
  stamp (off by default, so the stamp is cleared).
- Apply ejects unit 0, patches the stored image, reads it back and mounts it
  again. Do this with the machine stopped or at the boot prompt.
- It uses the same library as `ndtool` (a JavaScript port).

I read the code and did not open the page in a browser. The browser result is
not verified.

## 10. Caveats

- Work on copies. Booting SINTRAN writes to the pack, and `ndtool` writes to
  the image file. Stop the emulator before you run `ndtool`.
- ND-100 and ND-120 differ: SINTRAN only reads the identity PROM on an ND-110
  or ND-120. On the default `ND100CX` model it uses the number on the pack and
  never looks at the PROM. The number on the pack is what counts in both cases
  unless you set `ND100X_CPU_NUMBER`.
- `ND100X_CPU_NUMBER` overrides the pack on L and later, but stops a K pack
  (section 3).
- A pack with the "entered" stamp from another number is refused with
  `DIRECTORY ENTERED BY ANOTHER SYSTEM`. `--set-cpu` clears the stamp by
  default. `@ENTER-DIRECTORY` of a second pack on another unit is refused in
  the same way (chapter 28, section 9).
- The text of chapter 28, section 2.3, says that the nd100x default PROM still
  carries the signature. That is out of date: the current source
  (`versn_load_default_prom` in `src/cpu/cpu_instr.c`) leaves the signature
  off by default.
- `tools/nd100x_expect.py` and [pipe-automation.md](pipe-automation.md) show a
  `cpu_number` option that passes `--cpu-number=N`. The program has no such
  option (`nd100x --cpu-number=5` answers `unrecognized option`). Use the
  environment variable `ND100X_CPU_NUMBER` instead.

### Open questions (chapter 28, section 10)

- What reads `FCPUN` is not known. On two L packs it holds a different number
  from `SYSNO` and they still boot and run COSMOS.
- Whether anything else on a pack holds the CPU number is not known (none was
  found in the boot sector, the object entries or the configuration segment).
- Not verified here: two nd100x machines with images from this chapter
  actually talking XMSG or COSMOS to each other. I only checked that each
  image boots and reports its own number.
