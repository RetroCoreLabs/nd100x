# nd100x TODO

Single list of everything outstanding, written 20-SEP-2026.
Counts come from `docs/house-audit/counts.json` (baseline at commit 7a295c2).
The rules themselves and the gate are in `docs/HOUSE_STANDARD_FULL_CLEANUP_PLAN.md`;
this file is the checklist, not a second copy of the plan.

Every item marked GATE must pass G1-G14 (`tools/house/gate.sh`) before its commit.

---

## A. House standard cleanup - remaining phases

Total findings still open: 37,194 (of which 30,300 are empty REVIEW cells).

### A.1 Phase 3 - naming (GATE per module) - 1,915 findings

- [ ] 3.1 non-static functions to `module_verb` lower snake - 540 findings,
      487 of them in the 61 files listed in `docs/house-audit/prefixes.tsv`.
      Exempt: `Dbg_*`, `Nd500_*`, `Init`, `Boot`, `SendKeyToTerminal` (JavaScript
      calls these by name).
      Work module by module, one commit each. Feed the rename map to
      `tools/house/objcompare.sh` so G9 can still compare objects.
      Test stand-ins in `tests/` follow the real function's new name.
- [ ] 3.2 static functions to lower snake - 222 findings.
- [ ] 3.4-3.7 parameters and locals out of camelCase - 1,151 findings.
- [ ] 3.3 (2), 3.8 (3), 3.9 (1) - leftovers, fold into the module they live in.
- [ ] After 3.4-3.7: re-check rule 4.6 for `src/cpu/cpu_mms.c` and
      `src/devices/hdlc/dma_control_blocks.c` (deferred, needs the parameter
      renames first).

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
