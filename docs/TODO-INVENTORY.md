# TODO and FIXME inventory

Every `TODO` and `FIXME` in the source that carries no owner - 51 of them, in 16 files. They are listed so each can be judged on its own and either acted on, given an owner, or deleted.

This file is generated from the audit's rule 11.4 listing (`python3 tools/house/audit.py --list 11.4`). Regenerate it after changing any of them, or the line numbers drift.

## How to read the Checked column

- **Still open** - the code was read and the comment is accurate.
- **Half done** - part of the comment describes work already finished.
- *(blank)* - **not checked.** Claude did not verify it either way. A blank is not a claim that the TODO is valid; it means no evidence was gathered.

Only entries backed by reading the surrounding code carry a note. Nothing here is inferred from the wording of the comment itself.

## src/cpu/cpu.c

| Line | Function | Comment | Checked |
|---|---|---|---|
| 720 | `private_cpu_tick` | `g_reg->myreg_PFB = memory_fetch(gPC, false); //TODO: Remove this  step?` |  |

## src/cpu/cpu_disasm.c

| Line | Function | Comment | Checked |
|---|---|---|---|
| 611 | `disasm_op_to_str` | `(void)snprintf(opstr, BUFSTRSIZE, "WAIT"); /* TODO:: number??*/` |  |
| 740 | `disasm_op_to_str` | `if (!(operand & 0x0007)) /* STS reg bits handling FIXME:: what if it is bit >7 & STS??*/` |  |
| 1139 | `decode_140k` | `/* TODO: Check if we should return NOOP, or create our own internal illegal instruction code and trap that ...` |  |
| 1194 | `decode_150k` | `/* TODO: Check if we should return NOOP, or create our own internal illegal instruction code and trap that ...` |  |

## src/cpu/cpu_instr.c

| Line | Function | Comment | Checked |
|---|---|---|---|
| 62 | `cpu_set_ring_at_clpt` | `/*********** TODO ***********/` |  |
| 1199 | `do_mcl` | `/* :TODO: Check if we need to do illegal instruction handling */` |  |
| 1238 | `do_mst` | `/* :TODO: Check if we need to do illegal instruction handling */` |  |
| 1273 | `do_tra` | `/* TODO:: Check that this also is supposed to clear the PGS as it "unlocks" it */` |  |
| 1348 | `do_tra` | `default: /* These registers dont exist, so just return 0 for now FIXME: Check correct behaviour.*/` |  |
| 1372 | `do_exr` | `cpu_setbit(_STS, STS_ERROR_INDICATOR, 1); //: TODO: activate CPU trap on level 14!!!` |  |
| 1428 | `do_wait` | `//TODO: Make these into callbacks` |  |
| 1486 | `do_trr` | `// TODO:? according to manual it can only set bit 15,13-12-11` |  |
| 2090 | `cpu_add_a_mem` | `// FIXME - ADD FLAG HANDLING CORRECTLY FOR C,O,Q FLAGS (CHECK AGAIN THINK WE MIGHT HAVE SUBTLE BUGS)` |  |
| 3599 | `opcode_movb_move_byte` | `{ /* :TODO: check bytes to determine direction, or if no need to copy exist */` |  |
| 4347 | `opcode_init_initialize_stack` | `/*:TODO:  Flag */` |  |
| 4636 | `opcode_ident_identify_interrupting_device` | `cpu_illegal_instr(operand); /* Assume this is how we should hanle it.. TODO: Check!!! */` |  |
| 5421 | `opcode_clepu_clear_page_tables_and_collect_page_used` | `// TODO: Implement` |  |
| 6850 | `opcode_mon_monitor_call` | `// TODO:MAYBE, add emulation layer here` |  |
| 6950 | `cpu_sub_a_mem` | `/*` |  |
| 6989 | `cpu_rdiv_org` | `/* :TODO: Apparently Carry can be set too. CHECK that... Might be RAD=1??? */` |  |
| 7097 | `cpu_rmpy_org` | `/* :TODO: Apparently Carry can be set too. CHECK that... Might be RAD=1??? */` |  |
| 7109 | `cpu_rmpy_org` | `; //: TODO: Carry???;` |  |

## src/cpu/cpu_types.h

| Line | Function | Comment | Checked |
|---|---|---|---|
| 417 | `sleep_ms` | `/* :TODO: Check if PEA and PES should have a common lock */` |  |

## src/debugger/debugger.c

| Line | Function | Comment | Checked |
|---|---|---|---|
| 2953 | `on_set_exception_breakpoints` | `// TODO: Implement exception breakpoint handling` |  |
| 4458 | `cmd_source` | `/* TODO: Reimplement when libdap adds source context` |  |

## src/debugger/debugger.h

| Line | Function | Comment | Checked |
|---|---|---|---|
| 25 | `(file scope)` | `/// TODO: Not yet implemented and supported by the DAP` |  |

## src/devices/floppy/device_floppy_dma.c

| Line | Function | Comment | Checked |
|---|---|---|---|
| 164 | `floppy_dma_read` | `value = 0x1; // TODO: What is the correct value?` |  |
| 235 | `floppy_dma_write` | `// TODO: Implement streamer` |  |
| 378 | `execute_autoload` | `* the status word. TODO: implement the real BPUN autoload (read track 0, scan for '!',` |  |
| 395 | `execute_test` | `// TODO: Implement` |  |

## src/devices/floppy/device_floppy_dma.h

| Line | Function | Comment | Checked |
|---|---|---|---|
| 375 | `(file scope)` | `// TODO: Refactor to support multiple units` |  |

## src/devices/floppy/device_floppy_pio.c

| Line | Function | Comment | Checked |
|---|---|---|---|
| 804 | `floppy_pio_create_device` | `data->floppyName = "FLOPPY.IMG"; //TODO: Make this configurable` |  |

## src/devices/hdlc/dma_control_blocks.c

| Line | Function | Comment | Checked |
|---|---|---|---|
| 429 | `dmacb_mark_buffer_received` | `""); // TODO: key name` |  |
| 542 | `dmacb_load_buffer_description` | `""); // TODO: key name` |  |

## src/devices/panel/panel.c

| Line | Function | Comment | Checked |
|---|---|---|---|
| 128 | `panel_process_terminal_panc` | `// TODO: Implement!` | **Still open.** The `case` contains only `break;` - the panel function is not implemented. |
| 131 | `panel_process_terminal_panc` | `// TODO: Implement!` | **Still open.** The `case` contains only `break;` - the panel function is not implemented. |
| 215 | `panel_process_terminal_panc` | `// TODO: Implement!` | **Still open.** The `case` contains only `break;` - the panel function is not implemented. |
| 218 | `panel_process_terminal_panc` | `// TODO: Implement!` | **Still open.** The `case` contains only `break;` - the panel function is not implemented. |
| 221 | `panel_process_terminal_panc` | `// TODO: Implement!` | **Still open.** The `case` contains only `break;` - the panel function is not implemented. |
| 224 | `panel_process_terminal_panc` | `// TODO: Implement!` | **Still open.** The `case` contains only `break;` - the panel function is not implemented. |
| 227 | `panel_process_terminal_panc` | `// TODO: Implement!` | **Still open.** The `case` contains only `break;` - the panel function is not implemented. |
| 230 | `panel_process_terminal_panc` | `// TODO: Implement!` | **Still open.** The `case` contains only `break;` - the panel function is not implemented. |

## src/devices/scsi/device_scsi.c

| Line | Function | Comment | Checked |
|---|---|---|---|
| 823 | `scsi_boot` | `* TODO(phase 6): confirm the real ND SCSI boot load length against the` |  |

## src/devices/smd/device_smd.c

| Line | Function | Comment | Checked |
|---|---|---|---|
| 735 | `smd_write` | `// TODO: WHat does this mean in practice - what now ?` |  |
| 749 | `smd_write` | `// TODO: WHat does this mean` |  |
| 994 | `execute_go` | `// TODO: Set disk type based on size of SMD image file (TODO: Add more disk sizes)` | **Half done.** The lines directly below DO set the type from `diskFileSize` (150 MB, 288 MB, 825 MB, default 75 MB), so the outer TODO is already implemented. The inner `(TODO: Add more disk sizes)` is still open: `disk_smd.h` defines 7 types and only 4 are selected here. |
| 1453 | `execute_go` | `// TODO: Implement ECC operation` |  |

## src/devices/smd/disk_smd.c

| Line | Function | Comment | Checked |
|---|---|---|---|
| 42 | `smd_disk_set_type` | `4095; // TODO: Find the correct MAX - is it different pr disk or controller?` |  |

## src/devices/smd/disk_smd.h

| Line | Function | Comment | Checked |
|---|---|---|---|
| 117 | `(file scope)` | `int maxWordCount; // TODO: Find the correct MAX - is it different pr disk or controller?` |  |

## src/frontend/nd100wasm/nd100wasm.c

| Line | Function | Comment | Checked |
|---|---|---|---|
| 44 | `(file scope)` | `// TODO: Create proper nd100wasm_types.h or share types with nd100x` |  |

