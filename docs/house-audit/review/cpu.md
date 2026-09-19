# Review: cpu

One row per function. A cell is empty until reviewed; write `ok`, or
`n/a`, or `fixed <commit>`. Rules: see the plan, section 3 (REVIEW).

## src/cpu/bcd.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| bcd_byte_aligned_nibbles | 130 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_mem_words | 145 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_get_nibble | 193 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_set_nibble | 199 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_get_byte | 212 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_set_byte | 220 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_get_operand | 250 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_store_operand | 300 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_decode_sign | 334 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_extract_magnitude | 373 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_add_magnitude | 397 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_sub_magnitude | 425 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_cmp_magnitude | 448 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_pack_magnitude_to_field | 483 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_add_sub | 552 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_addd | 661 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_subd | 693 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_compare | 731 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_comd | 804 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_shde | 879 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_shde | 1028 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_report_error | 1066 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_ascii_layout | 1081 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_convert_to_packed | 1118 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_pack | 1278 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| unpacked_digit_byte | 1324 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a CPU thread only | n/a | ok | n/a |
| bcd_convert_to_unpacked | 1350 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_unpack | 1447 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| throttle_get_ns | 45 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_set_type_from_env | 65 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_trace_nd110_set | 219 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_op | 235 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| New_GetEffectiveAddr | 281 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| calcIIC | 354 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| recalcInternalInterruptBits | 378 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| calcPK | 397 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| interrupt | 424 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| device_interrupt | 479 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PhysMemWrite | 504 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PhysMemRead | 513 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_watchpoint_triggered | 524 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MemoryWrite | 555 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MemoryRead | 575 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MemoryFetch | 587 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| checkAndSwitch | 594 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| trace_before_exec | 645 | n/a | n/a | unsigned short b (rule 5.2 finding) | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok --trace/BSD diag to stderr by design (profile: traces stay) | n/a CPU thread only | n/a | ok | n/a |
| private_cpu_tick | 673 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| sleep_ms | 726 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_instruction_is_jump | 760 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ring_record | 851 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ring_dump | 870 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_run | 924 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_init | 1139 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| init_cpu_debugger | 1187 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_reset | 1201 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cleanup_cpu | 1232 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_debugger_request_pause | 1248 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_debugger_request_pause | 1264 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_debugger_control_granted | 1285 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_debugger_control_granted | 1301 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_cpu_stop_reason | 1321 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_cpu_stop_reason | 1335 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_cpu_run_mode | 1354 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_cpu_run_mode | 1370 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_throttle_set_enabled | 1389 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_throttle_get_enabled | 1402 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_throttle_set_mhz | 1407 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_throttle_get_mhz | 1421 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu_bkpt.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| breakpoint_bitmap_rebuild | 38 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hash_address | 59 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| breakpoint_manager_init | 67 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| breakpoint_manager_cleanup | 81 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| breakpoint_manager_step_one | 93 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| breakpoint_manager_add | 105 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| breakpoint_manager_remove | 149 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| breakpoint_manager_clear | 187 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| breakpoint_manager_clear_type | 209 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| breakpoint_manager_check | 253 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| check_for_breakpoint | 321 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| stopReasonFromBreakpoint | 413 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| breakpoint_manager_get_last_hit | 433 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| watchpoint_bitmap_rebuild | 477 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| watchpoint_add | 496 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| watchpoint_remove | 524 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| watchpoint_check_slow | 544 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| watchpoint_check | 588 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| watchpoint_clear | 594 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| watchpoint_get_count | 605 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| watchpoint_get | 611 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| phys_watchpoint_pagemap_rebuild | 633 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| phys_watchpoint_add | 648 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| phys_watchpoint_remove | 675 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| phys_watchpoint_check | 691 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| phys_watchpoint_clear | 726 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| phys_watchpoint_get_count | 737 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| phys_watchpoint_get | 743 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu_disasm.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| OpToStr | 65 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_allocate | 760 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_instr | 771 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_exr | 792 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_addword | 804 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_init | 818 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_setlbl | 828 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_set_isdata | 837 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_userel | 845 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_dump | 870 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| extract_opcode | 960 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| decode_140k | 991 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| decode_150k | 1130 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu_instr.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cpu_set_ring_at_clpt | 39 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_versn | 44 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CheckPriv | 62 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| signExtend | 85 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_add | 99 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| calcEL | 130 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ReadEL | 141 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| WriteEL | 147 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| illegal_instr | 155 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| unimplemented_instr | 167 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_aaa | 184 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_aab | 194 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_aat | 204 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_aax | 214 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_mon | 225 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_saa | 252 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sab | 259 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sat | 266 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sax | 273 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_shifts | 280 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_nlz | 307 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_dnz | 321 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_srb | 335 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lrb | 348 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CJP | 371 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jap | 408 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jan | 421 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jaz | 434 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jaf | 457 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jpc | 470 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jnc | 485 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jxn | 498 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jxz | 510 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jpl | 517 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_skp | 556 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_bfill_new | 577 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_bfill | 607 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_stz | 633 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sta | 641 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_stt | 650 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_stx | 658 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_std | 666 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_stf | 685 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lda | 695 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ldt | 703 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ldx | 711 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ldd | 719 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ldf | 738 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_stztx | 750 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_statx | 764 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_stdtx | 778 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ldatx | 803 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ldxtx | 827 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lddtx | 851 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ldbtx | 879 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_min | 907 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_add | 923 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sub | 933 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_and | 942 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ora | 950 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_fad | 964 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_fsb | 999 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_fmu | 1034 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_fdv | 1069 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jmp | 1112 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_geco | 1127 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| versn_load_default_prom | 1248 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_versn_reset | 1286 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| versn_set_word | 1306 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| versn_identity_is_skip | 1320 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| versn_hex_digit | 1334 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| versn_parse_number | 1363 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| versn_parse_microcode_version | 1443 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| versn_env_number | 1479 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_versn_set_identity_from_env | 1521 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| versn_is_nd120 | 1686 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| UpdateMemoryIO | 1761 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_iot | 1796 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_iox | 1851 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ioxt | 1866 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ident | 1886 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_opcom | 1917 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_irw | 1933 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_irr | 1972 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_exam | 1994 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_depo | 2010 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_pof | 2024 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_piof | 2037 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_pon | 2052 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_pion | 2060 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_iof | 2071 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ion | 2087 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_rex | 2097 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sex | 2110 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd110_seg_phys | 2148 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd110_bankgroup_phys | 2163 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_setpt | 2177 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_clept | 2253 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_clnreent | 2377 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_chreent_pages | 2491 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| clepu_mark_working_set | 2597 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a CPU thread only | n/a | ok | n/a |
| ndfunc_clepu | 2609 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_wglob | 2737 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_rglob | 2757 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_inspl | 2781 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_rempl | 2845 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_cnrek | 2915 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_clpt | 2995 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd110_enter_page_table | 3109 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_enpt | 3158 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_rept | 3176 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lbit | 3196 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lbitp | 3224 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sbit | 3256 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sbitp | 3291 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lbytp | 3331 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sbytp | 3366 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_tsetp | 3406 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_rdusp | 3432 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lasb | 3458 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sasb | 3469 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lacb | 3480 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sacb | 3491 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lxsb | 3502 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lxcb | 3513 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_szsb | 3524 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_szcb | 3535 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_init | 3589 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_entr | 3626 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_leave | 3649 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_eleav | 3658 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lbyt | 3682 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sbyt | 3710 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_mix3 | 3743 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| regop_logical | 3753 | n/a | n/a | ok | ok 16-bit results masked by uint16_t casts | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a CPU thread only | n/a | ok | n/a |
| regop_arith | 3802 | n/a | n/a | int tmp from do_add (kept from regop) | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a CPU thread only | n/a | ok | n/a |
| regop | 3840 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoMCL | 3891 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoMST | 3930 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoTRA | 3968 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoEXR | 4074 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoWAIT | 4107 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_halt | 4148 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lwcs | 4158 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoTRR | 4185 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoSRB | 4267 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoLRB | 4317 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| IsSkip | 4345 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_bops | 4422 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ShiftReg | 4497 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ShiftDoubleReg | 4538 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoIDENT | 4579 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoRDUS | 4624 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoTSET | 4645 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoMOVEW | 4677 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoMOVB | 4798 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoMOVBF | 4900 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_A_mem | 4989 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| doMoveBytes | 5032 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_movb | 5171 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_movbf | 5180 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| sub_A_mem | 5186 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rdiv_org | 5226 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rdiv | 5269 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rmpy_org | 5337 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rmpy | 5375 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mpy | 5427 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetBCD | 5455 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| StoreBCD | 5461 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Instruction_Add | 5472 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Instruction_Add_Range | 5482 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Instruction_Add_Mask | 5497 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Setup_Instructions | 5523 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu_mms.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cpu_set_ring_at_pf | 42 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreatePagingTables | 50 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ConvertFrom16BitPTE | 79 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ConvertTo16BitPTE | 85 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CalcPageTableAddress | 94 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DestroyPagingTables | 120 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetPTShadowAddress | 130 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PT_Write | 165 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PT_Read | 211 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetPageTableEntry | 229 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetPageTableEntryForDebugger | 274 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| UpdatePageTableEntry | 313 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SetPageUsed | 340 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SetPageWritten | 357 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetPageTableEntryDebugInfo | 385 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mapVirtualToPhysical | 445 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| UpdatePGS | 642 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| page_fault_diag | 671 | n/a | n/a | static long pf_calls (rule 5.2 finding) | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok diag to trace file | n/a CPU thread only | n/a | ok | n/a |
| checkPageProtection | 700 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| IsAddressShadowMemory | 786 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ReadVirtualMemory | 839 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ReadIndirectVirtualMemory | 855 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FetchVirtualMemory | 866 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| WriteVirtualMemory | 877 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetPhysicalMemoryType | 901 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd_ecc_write_latch | 935 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd_ecc_read_detect | 983 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ReadPhysicalMemory | 1078 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| WritePhysicalMemory | 1111 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| WritePhysicalMemoryWM | 1118 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HandleMemoryOutOfRange | 1188 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HandleMPV | 1198 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HandlePF | 1232 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_ReadPhysicalMemory | 1251 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_WritePhysicalMemory | 1260 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_MapVirtualToPhysical | 1282 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_ReadVirtualMemoryISpace_PIL | 1370 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_ReadVirtualMemoryDSpace_PIL | 1380 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_WriteVirtualMemoryISpace_PIL | 1390 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_WriteVirtualMemoryDSpace_PIL | 1400 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_ReadVirtualMemoryISpace | 1411 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_ReadVirtualMemoryDSpace | 1416 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_WriteVirtualMemoryISpace | 1421 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_WriteVirtualMemoryDSpace | 1426 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu_model.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CpuModel_FromName | 43 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CpuModel_Name | 63 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CpuModel_DisplayName | 79 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CpuModel_Count | 110 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CpuModel_NameByIndex | 115 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu_regs.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| setPIL | 34 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| setPEA | 53 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| setPES | 64 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| setPGS | 75 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| setreg | 89 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| getbit | 101 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| clrbit | 118 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| setbit_STS_MSB | 130 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| setbit | 147 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| AdjustSTS | 175 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu_types.h

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| phys_watchpoint_page_armed | 735 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/expr_eval.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| skip_ws | 53 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| has_error | 62 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_error | 68 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| match2 | 77 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| match1 | 89 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| match_keyword | 101 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| match_char | 115 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| lookup_internal_register | 129 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a CPU thread only | n/a | ok | n/a |
| lookup_register | 219 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_number | 311 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_identifier | 350 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_primary | 379 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_unary | 433 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_multiplicative | 467 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_additive | 515 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_comparison | 547 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_equality | 587 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_bitwise_and | 623 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_bitwise_xor | 652 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_bitwise_or | 680 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_logic_and | 709 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_logic_or | 743 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_expr | 777 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| expr_eval_value | 786 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| expr_eval_condition | 821 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/float.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| mkfp48 | 71 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_core | 86 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add48 | 126 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| sub_core | 152 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| sub48 | 214 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| NDFloat_Add | 241 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| NDFloat_Sub | 267 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| NDFloat_Mul | 296 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| NDFloat_Div | 337 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoNLZ | 397 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoDNZ | 445 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mkfp32 | 523 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| pack32 | 546 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| NDFloat_Add32 | 597 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| NDFloat_Sub32 | 643 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| NDFloat_Mul32 | 691 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| NDFloat_Div32 | 733 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoNLZ32 | 793 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoDNZ32 | 848 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
