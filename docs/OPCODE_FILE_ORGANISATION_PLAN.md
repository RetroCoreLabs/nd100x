# Plan: sorting the opcode functions in src/cpu/cpu_instr.c

Decided by Ronny, 21-SEP-2026: **group by what the instruction does, ascending
octal opcode inside each group.** Written after the rename to
`opcode_<mnemonic>_<what it does>` passed the gate (commit 78ef468: G10 TPE
INSTRUCTION-C03 and PAGING-C02 pass, G9 compares 117 objects identical).

## Why this order

The grouping is not an opinion. `docs/cpu_documentation.md` carries a
`Category` for each instruction, and 114 of the 119 functions have one. Octal
order inside a group matches the ND-100 manual's tables and the registration
list at the bottom of the file, so an octal code read from a trace or a
disassembly leads to a predictable place.

## The groups

Consolidated from the 32 raw categories in the reference. The order below is
the order they will appear in the file.

| # | Group | Source categories | Count |
|---|-------|-------------------|-------|
| 1 | Argument and register set | Argument Instruction, Register Operations | 9 |
| 2 | Memory transfer - load | Memory Transfer - Load Instruction, ...Double word | 5 |
| 3 | Memory transfer - store | Memory Transfer - Store Instruction | 5 |
| 4 | Memory transfer - general | Memory Transfer Instructions | 7 |
| 5 | Arithmetic and logical | Arithmetic and Logical | 4 |
| 6 | Floating point | Standard Floating Instructions, Floating Conversion | 8 |
| 7 | Decimal | Decimal Instructions | 5 |
| 8 | Byte and word block | Byte Instructions, Word Block Instructions | 6 |
| 9 | Sequencing | Sequencing Instructions, Skip Instruction | 11 |
| 10 | Stack | Stack Operations | 4 |
| 11 | Input and output | Input and Output | 3 |
| 12 | Interrupt control | Interrupt Control Instructions, Inter-level | 7 |
| 13 | Register block | Register Block Instructions | 2 |
| 14 | Paging control | Memory Management Instructions | 5 |
| 15 | Page tables (140300-140304) | Control Instructions, octal sub-block | 5 |
| 16 | Page list and segments (140500-140507) | Control Instructions, octal sub-block | 8 |
| 17 | Bit, byte and physical memory (140510-140517) | Control Instructions, octal sub-block | 8 |
| 18 | Bank registers (140700-140707) | Control Instructions, octal sub-block | 8 |
| 19 | Monitor, examine and CPU information | Monitor Calls, Memory Examine and Test, System/CPU Information, privileged, Undocumented | 6 |

"Control Instructions" holds 28 entries in the reference, which is too coarse
to help a reader. It is the whole `140xxx` privileged block and splits by
octal sub-block into groups 15-18 above - page table control, page list and
segment control, privileged bit/byte/physical access, and the segment-table
and core-map bank registers.

Five functions have no category in the reference and need one assigned by
hand before the sort runs:

- `opcode_shift_group` - dispatches SHT/SHD/SHA/SAD; belongs with Shift.
- `opcode_chreent_pages` - registered at octal 140303, so it sits in group 15
  by its opcode.
- `opcode_halt` - no reference entry at all.
- `opcode_unpack_convert_from_decimal` - group 7 by its mnemonic.
- `opcode_bfill_new_byte_fill` - group 8 by its mnemonic.

## How it will be done

1. Build the order from `docs/house-audit/opcode_names.tsv` joined with the
   category and opcode of `docs/cpu_documentation.md`; assign the five above
   by hand. Write the result to `docs/house-audit/opcode_order.tsv` so the
   intended order is reviewable before a line of source moves.
2. Move whole function bodies with their Doxygen blocks. Nothing inside a
   function changes; no declaration, no registration line, no static
   prototype moves.
3. A banner comment per group, in the style already used in this repo.
4. The registration table at the bottom of the file is NOT reordered in the
   same step. It is a separate change, and mixing the two would make the diff
   unreadable.

## How it will be verified

An earlier version of this plan said G9 must report every object identical.
**That was wrong, and it cost a gate cycle.** Moving a function changes its
address, so branch targets and RIP-relative displacements change with it. The
object differs although the behaviour does not; G9 compares whole objects and
correctly reports `cpu_instr.c.o: code or data differs`.

The right check compares **function by function**, ignoring where each one
landed. Disassemble both objects, split on the symbol, drop objdump's
resolved-address note after `#`, and rewrite `<hex> <symbol+0xNN>` to
`<symbol+0xNN>` - the symbol-relative form is the meaning, the absolute
address is just where it landed. Then assert the same set of functions and
identical instruction text for each.

Measured on the real move: 148 functions, same set, 147 identical, and the
only difference is `Setup_Instructions` - the registration table, which holds
RIP-relative displacements to every function it registers, so its distances
must change. Confirm the differences there are only `lea -0x17e8(%rip)` style
displacements with no instruction added, removed or altered.

Normalise carefully. A first attempt stripped hex runs of four digits or
more, which hid real differences while leaving short addresses visible, and
reported ten functions differing when the answer was one. Look at a diff of
one differing function before believing any count.

The full gate still runs with `--extra=cpu` so G10 drives the TPE instruction
verifier over the reordered file.

The full gate runs with `--extra=cpu` so G10 drives the TPE instruction
verifier over the reordered file.

A function moved by accident into the wrong group is not a build error, so
the generated order file is checked against the source after the move: every
function present, none duplicated, each one inside the group it was assigned.

## Functions that exist but are never called

Separate from the sort, and needing manual analysis (Ronny, 21-SEP-2026).

Checked on 21-SEP-2026: **all 109 opcode functions are registered** in the
dispatch table and reachable, including `opcode_chreent_pages` at octal
140303. An earlier count of 30 unregistered functions was wrong - the check
missed registrations whose function name sits on a continuation line. There
is no `opcode_exit`.

The real list is `docs/house-audit/unused_functions.txt`, 82 functions that
nothing calls. The CPU ones look like superseded implementations:
`rmpy_org`, `rdiv_org`, `sub_A_mem`, `update_stack_frame`. Two opcode
registrations are commented out rather than absent - `opcode_movb_move_byte`
at 140131 and `opcode_movbf_move_bytes_forward` at 140132 - so those
instructions are implemented but deliberately not dispatched; why is not
recorded anywhere and needs Ronny.

Each of the 82 needs a decision: delete, or keep with a comment saying why it
stays. That is open decision C.1 in `docs/TODO.md`. None of them may be
deleted on a guess - `_removed_MOVB_AND_MOVBF_` in this same file shows the
project keeps deliberate markers.
