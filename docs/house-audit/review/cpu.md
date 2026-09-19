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
| calcIIC | 356 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| recalcInternalInterruptBits | 380 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| calcPK | 399 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| interrupt | 426 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| device_interrupt | 481 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PhysMemWrite | 506 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PhysMemRead | 515 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_watchpoint_triggered | 526 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MemoryWrite | 557 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MemoryRead | 577 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MemoryFetch | 589 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| checkAndSwitch | 596 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| trace_before_exec | 647 | n/a | n/a | unsigned short b (rule 5.2 finding) | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok --trace/BSD diag to stderr by design (profile: traces stay) | n/a CPU thread only | n/a | ok | n/a |
| private_cpu_tick | 675 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| sleep_ms | 728 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_instruction_is_jump | 762 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ring_record | 853 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ring_dump | 872 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_run | 926 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_init | 1141 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| init_cpu_debugger | 1189 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_reset | 1203 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cleanup_cpu | 1234 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_debugger_request_pause | 1250 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_debugger_request_pause | 1266 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_debugger_control_granted | 1287 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_debugger_control_granted | 1303 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_cpu_stop_reason | 1323 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_cpu_stop_reason | 1337 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_cpu_run_mode | 1356 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_cpu_run_mode | 1372 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_throttle_set_enabled | 1391 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_throttle_get_enabled | 1404 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_throttle_set_mhz | 1409 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_throttle_get_mhz | 1423 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

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
| regop_arith | 3804 | n/a | n/a | int tmp from do_add (kept from regop) | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a CPU thread only | n/a | ok | n/a |
| regop | 3844 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoMCL | 3897 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoMST | 3936 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoTRA | 3974 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoEXR | 4080 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoWAIT | 4113 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_halt | 4154 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lwcs | 4164 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoTRR | 4191 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoSRB | 4275 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoLRB | 4325 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| IsSkip | 4353 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_bops | 4432 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ShiftReg | 4509 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ShiftDoubleReg | 4552 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoIDENT | 4595 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoRDUS | 4640 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoTSET | 4661 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoMOVEW | 4693 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoMOVB | 4816 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoMOVBF | 4918 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_A_mem | 5007 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| doMoveBytes | 5050 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_movb | 5189 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_movbf | 5198 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| sub_A_mem | 5204 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rdiv_org | 5244 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rdiv | 5287 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rmpy_org | 5355 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rmpy | 5393 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mpy | 5445 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetBCD | 5473 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| StoreBCD | 5479 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Instruction_Add | 5490 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Instruction_Add_Range | 5500 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Instruction_Add_Mask | 5515 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Setup_Instructions | 5541 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

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
| PT_Write | 167 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PT_Read | 213 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetPageTableEntry | 231 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetPageTableEntryForDebugger | 276 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| UpdatePageTableEntry | 315 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SetPageUsed | 342 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SetPageWritten | 359 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetPageTableEntryDebugInfo | 387 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mapVirtualToPhysical | 447 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| UpdatePGS | 644 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| page_fault_diag | 673 | n/a | n/a | static long pf_calls (rule 5.2 finding) | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok diag to trace file | n/a CPU thread only | n/a | ok | n/a |
| checkPageProtection | 702 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| IsAddressShadowMemory | 788 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ReadVirtualMemory | 841 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ReadIndirectVirtualMemory | 857 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FetchVirtualMemory | 868 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| WriteVirtualMemory | 879 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetPhysicalMemoryType | 903 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd_ecc_write_latch | 937 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd_ecc_read_detect | 985 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ReadPhysicalMemory | 1080 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| WritePhysicalMemory | 1113 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| WritePhysicalMemoryWM | 1120 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HandleMemoryOutOfRange | 1190 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HandleMPV | 1200 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HandlePF | 1234 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_ReadPhysicalMemory | 1253 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_WritePhysicalMemory | 1262 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_MapVirtualToPhysical | 1284 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_ReadVirtualMemoryISpace_PIL | 1372 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_ReadVirtualMemoryDSpace_PIL | 1382 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_WriteVirtualMemoryISpace_PIL | 1392 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_WriteVirtualMemoryDSpace_PIL | 1402 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_ReadVirtualMemoryISpace | 1413 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_ReadVirtualMemoryDSpace | 1418 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_WriteVirtualMemoryISpace | 1423 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_WriteVirtualMemoryDSpace | 1428 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

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
