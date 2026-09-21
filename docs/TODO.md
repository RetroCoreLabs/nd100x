# nd100x TODO

Single list of everything outstanding, written 20-SEP-2026.
Counts come from `docs/house-audit/counts.json` (baseline at commit 7a295c2).
The rules themselves and the gate are in `docs/HOUSE_STANDARD_FULL_CLEANUP_PLAN.md`;
this file is the checklist, not a second copy of the plan.

Every item marked GATE must pass G1-G14 (`tools/house/gate.sh`) before its commit.

---

## A. House standard cleanup - remaining phases

Total findings still open: **35,376** (of which 30,300 are empty REVIEW cells).
Updated 21-SEP-2026 after phase 3.

### A.1 Phase 3 - naming - DONE except for 99 names that need decisions

Rule 3.1 went 540 -> 19, rule 3.2 222 -> 1, rule 3.4-3.7 1,151 -> 77. Every
module under `src/` and the `tests/` directory has been through the fixers,
each with the full gate and an object comparison. Every rename is traced in
`docs/house-audit/rename_log.tsv` (3,451 rows).

What is left is not sweepable, and each name needs a decision:

- [ ] 3.1 - 19, 3.4-3.7 - 77, 3.2 - 1, 3.3 - 2. These are the names the tools
      correctly refuse:
      - declared in one module and used from others, which a per-module tool
        cannot rewrite - `Log_IsEnabled`, `BOOT_TYPE`, `DRIVE_TYPE`,
        `MC_Runtime`, `MountedDriveInfo_t`, `BPUN_Header`, `PROG_Header`,
        `nd_pollfd_t`, `nd_socklen_t`;
      - the register file indices `_A`, `_B`, `_D`, `_L`, `_P`, `_T`, `_X`,
        `_STS` and the enum constants `Four`, `Sixteen` in `cpu_types.h`;
      - `scsi_debug_enabled`, a non-static global used from the vendored
        `ncr5386.c`, which cannot be edited.
      Each needs either a whole-tree rename of the whole family, or a
      recorded exemption. Renaming half a family is worse than none.
- [ ] Re-check rule 4.6 for `src/cpu/cpu_mms.c` and
      `src/devices/hdlc/dma_control_blocks.c` now that the parameter renames
      are in.
- [ ] `src/cpu/cpu_instr.c`: the registration table at the bottom is still in
      its original order, deliberately - see
      `docs/OPCODE_FILE_ORGANISATION_PLAN.md`. Note before touching it: these
      are `instruction_add_mask` and `instruction_add_range` calls, so
      registration order may decide precedence where masks overlap. That is a
      behavioural question, not a cosmetic one.

### A.2 Phase 5 - types and arithmetic - 3,517 findings

- [ ] 5.3 signed operand in a bitwise operation - 3,102 findings. The big one.
      Decide a per-module approach before touching any of it; a blind cast sweep
      will hide real sign bugs.
- [ ] 5.5 - 194, 5.2 - 183, 5.10 - 31, 5.9 - 7, 5.1/5.4/5.6-5.8 - clean.

### A.3 Phase 6 - statements and expressions - 89 findings

- [ ] 6.7 - 37, 6.2 - 33, 6.6 - 16, 6.1 - 3.

### A.4 Phase 7 - functions - 836 findings

- [ ] 7.3 return value ignored - 760 findings.
- [ ] 7.1 function too long - 64 findings.
- [ ] 7.6 - 12.
- [ ] 2.4 deferred in `src/cpu/cpu_instr.c` and `tests/test-gateway.c` (36
      findings) - blocked until the phase 7 function splits are done.

### A.5 Phase 8 - memory and strings - 3 findings

- [ ] 8.4 - 3.

### A.6 Phase 9 - logging and configuration - 57 findings

- [ ] 9.4 - 42, 9.7 - 15.
- [ ] 9 environment variables `ND100X_*` need the CLI flag + INI key pair the
      project convention demands (see `CLAUDE.md`, "Configuration Options
      Convention"). Decision still open - see C.4.

### A.7 Phase 11/12 - portability and file rules - 437 findings

- [ ] 4.5 include paths - 163 findings. BLOCKED on nd500x becoming a library
      (section B); `../` includes keep their temporary exemption until then.
- [ ] 4.6 - 132 (see A.1), 4.4 - 43, 4.1 - 6.
- [ ] 11.4 - 53, 12.2 - 40.

### A.8 Phase 12 - final audit

- [ ] 30,300 empty REVIEW cells in `docs/house-audit/review/*.md`: 1,500+
      functions x 20 REVIEW rules. These are human judgement rows, not
      mechanical fixes. Needs a decision on who fills them and at what depth.
- [ ] Final full audit run, re-baseline `docs/house-audit/counts.json`, ratchet
      locked at the new numbers.

---

## B. nd500x as a library (planned by Ronny, 20-SEP-2026)

Goal: keep the existing nd500x binary and its behaviour, and additionally build
nd500x as a library that nd100x links.

- [ ] Give nd500x its own include root so its `src/cpu/` and `src/machine/`
      headers stop shadowing the nd100x ones in the WASM build (both are
      compiled into the same target today).
- [ ] CMake option to build nd500x as a static library.
- [ ] Link it into nd100x for the WASM target first.
- [ ] Shared memory + octobus communication between ND-100 and ND-500, following
      what RetroCore does. Wait until the remaining RetroCore bugs are fixed.
- [ ] Only after the include root exists: do house rule 4.5 (A.7).

This is its own piece of work, not part of the cleanup phases.

---

## C. Open decisions (Ronny decides, one at a time)

- [ ] C.1 82 unused functions in `docs/house-audit/unused_functions.txt` -
      delete, or keep and mark?
- [ ] C.2 `tools/reth-tap` licence line - the code came from nd500x under MIT.
- [ ] C.3 `src/ndlib/ndlib_protos_preamble.h` has no include guard.
- [ ] C.4 9 `ND100X_*` environment variables - convert to CLI flag + INI key
      pairs, or document them as an exception?
- [ ] C.5 `_removed_MOVB_AND_MOVBF_` - dead marker, remove or keep as history?
- [ ] C.6 `system()` call in `tests/test_printer_main.c`.

---

## D. Testing

- [ ] SMD / DISC-TEMA automated harness: boot the TPE test floppy, drive the
      disk TEMA tests, assert the result. Asked for by Ronny while chasing the
      "Parallel seek disabled" bug; not built yet. Model it on
      `tools/tpe_autorun.py` and `docs/TPE-AUTORUN.md`.
- [ ] No unit tests exist for `src/devices/smd/device_smd.c` at all. The SMD fix
      (commit ecd19ab) was verified only by a boot comparison.

---

## E. Done (for reference)

- Phase 3 naming, 21-SEP-2026. Rule 3.1 540 -> 19, rule 3.2 222 -> 1, rule
  3.4-3.7 1,151 -> 77. Total 37,194 -> 35,376. Traced in
  docs/house-audit/rename_log.tsv.
- Every opcode function named after the instruction it implements
  (`opcode_jan_jump_if_a_negative`), with a Doxygen block generated from
  docs/cpu_documentation.md carrying opcode, mask, category, privilege and
  bit layout. `tools/house/opcode_docs.py`.
- src/cpu/cpu_instr.c sorted into 20 sections by instruction area, octal
  within each. `docs/OPCODE_FILE_ORGANISATION_PLAN.md`.
- The STS bits named: STS_CARRY, STS_PAGING_ON, and the accessors
  STS_CARRY_IS_SET, so a bit number cannot be confused with a bit value.
- MOVB/MOVBF: the dispatched pair holds the plain names, the kept-but-not-
  dispatched pair is marked _buggy, and why is written in the source.
- Reusable method captured in the `c-refactoring` skill.

- SMD "Parallel seek disabled": Initiate Seek and Return To Zero Seek now use
  `IODELAY_HDD_SMD` (10 ticks), matching the RetroCore C# driver; 11 transfer
  error exits now call `FinishOperation`. Commit ecd19ab.
- SMD operation enums renamed to the manual's names
  (`DEVICE_OP_SEEK_COMPLETE_SEARCH`, `DEVICE_OP_RETURN_TO_ZERO_SEEK`, ...).
- House rule 3.1 changed to `module_verb` lower snake_case. Commit 7a295c2.
- Phases 0, 1, 2 and 4 complete: formatting, file/header structure, static
  functions, banners, Doxygen. Rule 11.2 is at 0.
- Rules the audit now counts as 0: 1.2, 1.4, 1.5, 2.2, 2.7, 2.x, 4.3, 5.6, 5.7,
  6.4, 6.5, 6.8, 7.4, 8.3, 8.5/8.7, 8.6, 10.3, 11.2, 12.1.
  Rules not in this list and not in sections A.1-A.7 are not checked
  mechanically by `tools/house/audit.py` at all - they live in the REVIEW cells.
- Gate G1-G14 built and in use; it has caught several real regressions.

---

## F. Rename trace log

Every identifier renamed by the cleanup is logged, for all phases, so a name
that turns out wrong can be traced back and undone.

- `docs/house-audit/rename_log.tsv` - the trace: commit, date, rules, kind,
  file, line, old, new, count. One row per renamed identifier per file.
- `tools/house/rename_log.py` - rebuilds it from git history. Run after every
  rename commit.
- `docs/house-audit/rename_kinds.tsv` - append-only; `rename_naming.py` writes
  the kind (static function, parameter, local variable) as clang-tidy reported
  it, and the log joins it on. Rows with no recorded kind are classified by
  reading the file, which is an inference: a struct tag can come out as
  "local variable", and a name that appears only in a comment is still listed.

Any new rename mechanism - the clang-rename pass for rule 3.1 above all - must
append to the same trace before it is used on real code.
