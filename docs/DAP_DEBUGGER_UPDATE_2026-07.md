# DAP Debugger Update — July 2026

Three fixes to the DAP debugger, all in `src/debugger/debugger.c`, found while
debugging the ND-100 BSD kernel (`-g`/srcmap, split-I/D `PTM=1`). All on
`main`.

| Commit | Fix |
|--------|-----|
| `658ae43` | `stackTrace`/`variables` no longer crash the emulator on a corrupted stack |
| `a2fb4a2` | Frame source file/line bounded to the containing function |
| `a180a33` | `step_over` no longer jumps into a foreign function on an overlay line-table match |

---

## 1. `stackTrace` crash — cross-context `longjmp` (`658ae43`)

**Symptom:** `stackTrace` (or the follow-up `variables`) while stopped on a
crashed/looping kernel aborted the whole emulator with `*** stack smashing
detected ***` (or a segfault). Only these commands crashed; everything else
worked.

**Root cause (not a buffer overflow):** the stack unwinder, locals reader,
step-out heuristic, JPL-target resolver and set-variable handler read/wrote
emulated memory through the normal `ReadVirtualMemory`/`WriteVirtualMemory`
path. On an unmapped/protected address — exactly what a corrupted B-chain
produces — that path runs `checkPageProtection`, which raises an *emulated* CPU
fault: `HandlePF`/`HandleMPV` → `interrupt(14)` → `longjmp(s_cpu_jmp_buf)`
(`cpu.c:380`). Fired from inside a DAP handler, that `longjmp` unwinds back to
`cpurun()`'s `setjmp` — a different (and in the threaded server, different-
thread) stack context — and trips the stack protector.

**Fix:** all six debugger data accesses now go through trap-free `Dbg_*`
accessors via helpers `dbg_read_data`/`dbg_write_data`, which mirror the CPU's
own data mapping — split-I/D (`STS_PTM`) → D-space, else the normal page table
— matching `ReadVirtualMemory(addr, UseAPT=true)` in every mode but returning
`-1` on a bad page and raising no fault. Instruction reads already used the
trap-free I-space accessor and are unchanged. This also corrected a latent
split-I/D bug where stack *data* was read via the I-space mapping.

**Validation:** isolated mechanism test (trapping reads `longjmp`; all `Dbg_*`
accessors return `-1` without trapping, both PTM branches, read+write);
end-to-end DAP smoke test; field-validated by the BSD team on the real srcmap
kernel (locals resolved to correct u-area addresses).

## 2. Frame source line/file bleed (`a2fb4a2`)

**Symptom:** a frame's file/line was resolved by a *global* nearest-match, so a
PC in an inter-function gap or a garbage frame was labelled with an adjacent
compilation unit's line (real `syscall` frame flickering to `kern_prot.c`,
garbage frames showing `tty.c:1210`).

**Fix:** when C debug info is present, `cmd_stack_trace` resolves file/line only
within the containing function's `[start_address, end_address]` (new
`frame_line_entry_in_range()` helper), and shows no source line for a PC outside
every known C function. The no-debug-info path (assembly / SINTRAN) keeps the
original MAP → STABS → AOUT nearest-match, unchanged.

**Validation:** bounded-lookup algorithm unit-tested (cross-function no-bleed +
stray-below-range rejection); no-debug-info path regression-checked via DAP.
Field validation on the real srcmap kernel recommended.

## 3. `step_over` overlay/foreign-function jump (`a180a33`)

**Symptom:** a `step_over` at a statement jumped into a monitor/overlay routine
(observed: `syscall@126026` → `mon3_load+2454 @025701`). Session stayed alive;
stack/frames stayed correct.

**Root cause:** `symbols_get_next_line_address()`
(`external/libsymbols/src/symbols.c:662`) matches the next source line by
**filename only**, unbounded across the global line table. In an overlaid kernel
one file has line entries at several disjoint address ranges, so the "next line"
can resolve to a different overlay copy of the same file far from the current PC.

**Fix:** guard the STEP_OVER next-line target — when C debug info is present and
the computed target leaves the current function's `[start_address, end_address]`,
discard it and fall back to a single instruction step. Contained to
`debugger.c`; no effect without C debug info. Rather than change the shared
`libsymbols` next-line lookup (which touches all stepping), the guard sits at
the DAP handler.

**Validation:** build + no-debug-info regression check via DAP. Not reproduced
against the exact srcmap-kernel case (needs `make debug-kernel`); this is a
guard against the confirmed latent bug. Field check: confirm the `syscall`
step no longer lands in `mon3_load`, and normal in-function line stepping is
unaffected.

---

### Related notes
- General rule: DAP command handlers must access emulated memory only through
  the trap-free `Dbg_*` accessors, never `ReadVirtualMemory`/`WriteVirtualMemory`
  (which fault → `longjmp`). Same `longjmp` family as the earlier `pause` fix
  (`fefb1e9`).
- K&R parameters sharing one frame address (also seen during validation) is a
  srcmap/PCC dynamic-frame representational limit, not a debugger bug — not
  addressed here.
