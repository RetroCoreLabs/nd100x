# ND-100 / SINTRAN Domain Gotchas

Hard-won facts about the ND-100 and SINTRAN III that are NOT obvious from the
emulator source, and that have each cost real debugging time when guessed
wrong. Read this before diagnosing "impossible" behavior or changing
hardware-conformance code. All paths are relative to the repo root.

## Disk images and packs

- **Booting SINTRAN mutates the pack.** A boot writes swap and segment data
  back to the disk image; even a "quick look" boot changes the file. NEVER
  boot an original image - always boot a scratch copy (see
  docs/VERIFICATION.md for the copy workflow).
- **"DIRECTORY ENTERED BY ANOTHER SYSTEM"** is not corruption. SINTRAN stamps
  the entering CPU's system number into the directory
  (`ext_last_system_number` in the ndfs library's model). If a pack was left
  entered by a different CPU id, SINTRAN refuses it. Fix: clear the
  entered-by mark with the ndfs tooling (set entered cpu to 0), do not
  rebuild or restore the image.
  Step by step: [CPU-NUMBER-DISK-IMAGES.md](CPU-NUMBER-DISK-IMAGES.md)
  (`ndtool --release`, or `ndtool --set-cpu`).
- **Floppies do not boot like SMD/SCSI disks.** SMD/SCSI boot reads a boot
  sector; floppies carry a BPUN-format boot loader ("flomon"). The loader in
  src/ndlib/load_bpun.c (`LoadBPUNStream`) already parses FLOMON - a floppy
  that fails to boot is usually simply a data floppy with no FLOMON, not a
  parser bug. Block 0 full of 0x40 bytes = not bootable.

## CPU semantics

- **The IIC table in src/cpu/cpu.c (search for "Internal interrupt") is the
  authority** for internal-interrupt codes: page fault = IIC 3, memory
  protection violation = IIC 2. Do NOT change HandlePF/HandleMPV routing to
  match what a vendor diagnostic *prints* as expected/found values - the TPE
  INSTRUCTION verifier has printed "Found IIC value: 3" regardless of the
  actual register contents. Trusting that printout once caused a real
  regression (HandlePF changed to HandleMPV, breaking 8 MOVEW sub-tests).
- **`UpdatePGS(..., true)` on an absent page in `checkPageProtection()`
  (src/cpu/cpu_mms.c) is validated-correct behavior.** Setting the PM bit
  for a page-not-present fault looks wrong and has been "fixed" (broken)
  twice. The TPE paging test 6 (permit violation vs ring violation) proves
  the current behavior. There is a guard comment at the site - obey it.
- **`_DEGRADE_` in src/cpu/cpu_types.h is a BEHAVIORAL flag, not a debug
  flag.** It enables ring degradation on instruction fetch (ring 3 fetching
  from a lower-ring page lowers the PCR ring instead of raising MPV). The
  DEGRADE: diagnostic print is separately gated behind DEBUG_MMS.
- **IDENT codes are computed per-device from the thumbwheel setting in the
  device source.** IDENT is never a configuration item and must not appear
  in the INI/config surface.
- **LDF/STF are unconditional 3-word T/A/D movers in BOTH FPP modes.** The
  real RASK/DELILAH microcode has no 32/48-bit mode branch in LDF/STF; with
  `--fpp=32`, store/load 32-bit floats with STD/LDD. This is deliberate and
  documented at the implementation sites - do not "fix" it.

## SINTRAN internals

- **The initial-command buffer (INIBU) and the batch mode files are different
  things.** INIBU lives inside SEGFIL0 on disk (with a length cell at
  INIBU+130 octal that MUST be updated on writes); it holds commands like
  ENTER-DRIVE that set up batch and start HENT-MODE. HENT-MODE / LOAD-MODE
  are ordinary NDFS files. "List the initial commands" means INIBU, not the
  mode files.
- **Segment names are not stored on disk in any byte encoding.** They exist
  only in the RT-Loader's runtime data. The glass UI resolves them from
  shipped LIST-SEGMENT captures keyed by SINVER (template-glass/data/
  segment-names/), not from the pack.
- **SYMBOL-2-LIST symbol tables mix constants and addresses.** A symbol whose
  value falls inside an address range is not necessarily an address (ACLEAR
  is a bitmask constant). Verify against the handler code before deriving
  layouts or bounds from a symbol value.
- **Reads from a booting system return garbage.** Before SINTRAN finishes
  booting, kernel tables (e.g. SGMAX) are not initialized and PIL-dependent
  mappings are not established; a failed or absurd read taken during boot
  proves nothing. Re-test on a fully booted system before writing any
  conclusion down.
- **Memory reads are PIL-dependent.** A paused CPU at the "wrong" PIL may
  have the segment of interest unmapped; a read that returns nonsense may
  just be the mapping, not the data.

## Diagnostics / TPE test programs

- **TPE verifier sub-tests need the drive sequence `instr` ->
  `set-para,...` -> `run`.** Typing a sub-command name directly runs an
  empty test body that reports nothing tested. See docs/TPE-AUTORUN.md and
  tools/tpe_autorun.py.
- **A failing TPE run proves nothing until the binary AND the test image are
  both current.** A stale floppy image once produced an hour-long hunt for a
  "page fault not generated" bug that no longer existed.

## Working practices that follow from the above

- Establish which binary (which build directory) and which image the failure
  was observed on BEFORE reading any code. `nd100x --version` prints the git
  hash, dirty flag and build time precisely so this takes one command.
- When an external handoff document, research note or oracle contradicts the
  emulator, treat the claim as unverified until checked against source,
  microcode listings or a live measurement. Handoff docs have been wrong
  about segment-name storage, INIBU layout, FPP encodings and acceptance
  criteria.
