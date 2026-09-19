# Review: cpu

One row per function. A cell is empty until reviewed; write `ok`, or
`n/a`, or `fixed <commit>`. Rules: see the plan, section 3 (REVIEW).

## src/cpu/bcd.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| bcd_byte_aligned_nibbles | 134 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_mem_words | 149 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_get_nibble | 197 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_set_nibble | 203 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_get_byte | 216 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_set_byte | 224 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_get_operand | 254 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_store_operand | 304 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_decode_sign | 338 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_extract_magnitude | 377 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_add_magnitude | 401 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_sub_magnitude | 429 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_cmp_magnitude | 452 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_pack_magnitude_to_field | 487 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_add_sub | 556 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_addd | 665 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_subd | 697 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_compare | 735 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_comd | 808 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_shde | 883 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_shde | 1032 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_report_error | 1070 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_ascii_layout | 1085 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_convert_to_packed | 1122 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_pack | 1282 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| unpacked_digit_byte | 1328 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a CPU thread only | n/a | ok | n/a |
| bcd_convert_to_unpacked | 1354 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_unpack | 1451 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| throttle_get_ns | 49 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_set_type_from_env | 69 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_trace_nd110_set | 224 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_op | 240 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| New_GetEffectiveAddr | 286 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| calcIIC | 362 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| recalc_internal_interrupt_bits | 386 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| calc_pk | 405 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| interrupt | 432 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| device_interrupt | 487 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PhysMemWrite | 512 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PhysMemRead | 521 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_watchpoint_triggered | 532 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MemoryWrite | 563 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MemoryRead | 583 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| memory_fetch | 595 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| check_and_switch | 602 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| trace_before_exec | 653 | n/a | n/a | unsigned short b (rule 5.2 finding) | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok --trace/BSD diag to stderr by design (profile: traces stay) | n/a CPU thread only | n/a | ok | n/a |
| private_cpu_tick | 681 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| sleep_ms | 734 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_instruction_is_jump | 768 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ring_record | 859 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ring_dump | 878 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_run | 932 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_init | 1147 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| init_cpu_debugger | 1195 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_reset | 1209 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cleanup_cpu | 1240 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_debugger_request_pause | 1256 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_debugger_request_pause | 1272 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_debugger_control_granted | 1293 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_debugger_control_granted | 1309 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_cpu_stop_reason | 1329 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_cpu_stop_reason | 1343 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_cpu_run_mode | 1362 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_cpu_run_mode | 1378 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_throttle_set_enabled | 1397 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_throttle_get_enabled | 1410 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_throttle_set_mhz | 1415 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_throttle_get_mhz | 1429 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu_bkpt.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| breakpoint_bitmap_rebuild | 43 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hash_address | 64 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| breakpoint_manager_init | 72 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| breakpoint_manager_cleanup | 86 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| breakpoint_manager_step_one | 98 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| breakpoint_manager_add | 110 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| breakpoint_manager_remove | 154 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| breakpoint_manager_clear | 192 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| breakpoint_manager_clear_type | 214 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| breakpoint_manager_check | 258 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| check_for_breakpoint | 326 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| stop_reason_from_breakpoint | 418 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| breakpoint_manager_get_last_hit | 438 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| watchpoint_bitmap_rebuild | 482 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| watchpoint_add | 501 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| watchpoint_remove | 529 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| watchpoint_check_slow | 549 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| watchpoint_check | 593 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| watchpoint_clear | 599 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| watchpoint_get_count | 610 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| watchpoint_get | 616 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| phys_watchpoint_pagemap_rebuild | 638 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| phys_watchpoint_add | 653 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| phys_watchpoint_remove | 680 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| phys_watchpoint_check | 696 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| phys_watchpoint_clear | 731 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| phys_watchpoint_get_count | 742 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| phys_watchpoint_get | 748 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu_disasm.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| OpToStr | 72 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_allocate | 767 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_instr | 778 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_exr | 799 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_addword | 811 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_init | 825 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_setlbl | 835 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_set_isdata | 844 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_userel | 852 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_dump | 877 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| extract_opcode | 967 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| decode_140k | 998 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| decode_150k | 1137 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu_instr.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cpu_set_ring_at_clpt | 43 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_versn | 48 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| check_priv | 72 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| signExtend | 95 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_add | 109 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| calc_el | 140 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| read_el | 151 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| write_el | 157 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| illegal_instr | 165 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| unimplemented_instr | 177 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_aaa | 194 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_aab | 204 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_aat | 214 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_aax | 224 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_mon | 235 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_saa | 262 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sab | 269 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sat | 276 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sax | 283 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_shifts | 290 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_nlz | 317 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_dnz | 331 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_srb | 345 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lrb | 358 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cjp | 381 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jap | 418 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jan | 431 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jaz | 444 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jaf | 467 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jpc | 480 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jnc | 495 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jxn | 508 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jxz | 520 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jpl | 527 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_skp | 566 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_bfill_new | 587 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_bfill | 617 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_stz | 643 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sta | 651 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_stt | 660 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_stx | 668 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_std | 676 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_stf | 695 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lda | 705 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ldt | 713 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ldx | 721 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ldd | 729 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ldf | 748 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_stztx | 760 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_statx | 774 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_stdtx | 788 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ldatx | 813 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ldxtx | 837 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lddtx | 861 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ldbtx | 889 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_min | 917 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_add | 933 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sub | 943 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_and | 952 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ora | 960 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_fad | 974 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_fsb | 1009 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_fmu | 1044 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_fdv | 1079 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_jmp | 1122 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_geco | 1137 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| versn_load_default_prom | 1258 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_versn_reset | 1296 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| versn_set_word | 1316 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| versn_identity_is_skip | 1330 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| versn_hex_digit | 1344 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| versn_parse_number | 1373 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| versn_parse_microcode_version | 1453 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| versn_env_number | 1489 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_versn_set_identity_from_env | 1531 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| versn_is_nd120 | 1696 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| update_memory_io | 1771 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_iot | 1806 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_iox | 1861 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ioxt | 1876 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ident | 1896 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_opcom | 1927 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_irw | 1943 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_irr | 1982 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_exam | 2004 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_depo | 2020 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_pof | 2034 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_piof | 2047 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_pon | 2062 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_pion | 2070 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_iof | 2081 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_ion | 2097 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_rex | 2107 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sex | 2120 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd110_seg_phys | 2158 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd110_bankgroup_phys | 2173 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_setpt | 2187 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_clept | 2263 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_clnreent | 2387 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_chreent_pages | 2501 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| clepu_mark_working_set | 2607 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a CPU thread only | n/a | ok | n/a |
| ndfunc_clepu | 2619 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_wglob | 2747 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_rglob | 2767 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_inspl | 2791 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_rempl | 2855 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_cnrek | 2925 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_clpt | 3005 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd110_enter_page_table | 3119 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_enpt | 3168 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_rept | 3186 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lbit | 3206 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lbitp | 3234 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sbit | 3266 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sbitp | 3301 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lbytp | 3341 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sbytp | 3376 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_tsetp | 3416 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_rdusp | 3442 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lasb | 3468 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sasb | 3479 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lacb | 3490 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sacb | 3501 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lxsb | 3512 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lxcb | 3523 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_szsb | 3534 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_szcb | 3545 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_init | 3599 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_entr | 3636 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_leave | 3659 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_eleav | 3668 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lbyt | 3692 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_sbyt | 3720 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_mix3 | 3753 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| regop_logical | 3763 | n/a | n/a | ok | ok 16-bit results masked by uint16_t casts | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a CPU thread only | n/a | ok | n/a |
| regop_arith | 3814 | n/a | n/a | int tmp from do_add (kept from regop) | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a CPU thread only | n/a | ok | n/a |
| regop | 3854 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_mcl | 3907 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_mst | 3946 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_tra | 3984 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_exr | 4090 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_wait | 4123 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_halt | 4164 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_lwcs | 4174 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_trr | 4201 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_srb | 4285 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_lrb | 4335 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| is_skip | 4363 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_bops | 4442 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| shift_reg | 4519 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| shift_double_reg | 4562 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_ident | 4605 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_rdus | 4650 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_tset | 4671 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_movew | 4703 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_movb | 4826 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_movbf | 4928 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_A_mem | 5017 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_move_bytes | 5060 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_movb | 5199 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndfunc_movbf | 5208 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| sub_A_mem | 5214 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rdiv_org | 5254 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rdiv | 5297 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rmpy_org | 5365 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rmpy | 5403 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mpy | 5455 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetBCD | 5483 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| StoreBCD | 5489 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| instruction_add | 5500 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| instruction_add_range | 5510 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| instruction_add_mask | 5525 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Setup_Instructions | 5551 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu_mms.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cpu_set_ring_at_pf | 46 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreatePagingTables | 54 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ConvertFrom16BitPTE | 83 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ConvertTo16BitPTE | 89 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CalcPageTableAddress | 98 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DestroyPagingTables | 124 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetPTShadowAddress | 134 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PT_Write | 171 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PT_Read | 217 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetPageTableEntry | 235 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetPageTableEntryForDebugger | 280 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| UpdatePageTableEntry | 319 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SetPageUsed | 346 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SetPageWritten | 363 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetPageTableEntryDebugInfo | 391 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mapVirtualToPhysical | 451 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| UpdatePGS | 648 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| page_fault_diag | 677 | n/a | n/a | static long pf_calls (rule 5.2 finding) | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok diag to trace file | n/a CPU thread only | n/a | ok | n/a |
| checkPageProtection | 706 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| IsAddressShadowMemory | 792 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ReadVirtualMemory | 845 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ReadIndirectVirtualMemory | 861 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FetchVirtualMemory | 872 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| WriteVirtualMemory | 883 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetPhysicalMemoryType | 907 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd_ecc_write_latch | 941 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd_ecc_read_detect | 989 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ReadPhysicalMemory | 1084 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| WritePhysicalMemory | 1117 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| WritePhysicalMemoryWM | 1124 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HandleMemoryOutOfRange | 1194 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HandleMPV | 1204 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HandlePF | 1238 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_ReadPhysicalMemory | 1257 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_WritePhysicalMemory | 1266 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_MapVirtualToPhysical | 1288 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_ReadVirtualMemoryISpace_PIL | 1376 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_ReadVirtualMemoryDSpace_PIL | 1386 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_WriteVirtualMemoryISpace_PIL | 1396 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_WriteVirtualMemoryDSpace_PIL | 1406 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_ReadVirtualMemoryISpace | 1417 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_ReadVirtualMemoryDSpace | 1422 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_WriteVirtualMemoryISpace | 1427 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_WriteVirtualMemoryDSpace | 1432 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu_model.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CpuModel_FromName | 47 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CpuModel_Name | 67 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CpuModel_DisplayName | 83 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CpuModel_Count | 114 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CpuModel_NameByIndex | 119 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu_regs.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| setPIL | 38 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| setPEA | 57 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| setPES | 68 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| setPGS | 79 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| setreg | 93 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| getbit | 105 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| clrbit | 122 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| setbit_STS_MSB | 134 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| setbit | 151 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| AdjustSTS | 179 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu_types.h

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| phys_watchpoint_page_armed | 739 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/expr_eval.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| skip_ws | 57 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| has_error | 66 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_error | 72 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| match2 | 81 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| match1 | 93 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| match_keyword | 105 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| match_char | 119 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| lookup_internal_register | 133 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a CPU thread only | n/a | ok | n/a |
| lookup_register | 223 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_number | 315 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_identifier | 354 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_primary | 383 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_unary | 437 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_multiplicative | 471 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_additive | 519 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_comparison | 551 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_equality | 591 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_bitwise_and | 627 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_bitwise_xor | 656 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_bitwise_or | 684 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_logic_and | 713 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_logic_or | 747 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_expr | 781 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| expr_eval_value | 790 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| expr_eval_condition | 825 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/float.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| mkfp48 | 75 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_core | 90 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add48 | 130 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| sub_core | 156 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| sub48 | 218 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| NDFloat_Add | 245 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| NDFloat_Sub | 271 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| NDFloat_Mul | 300 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| NDFloat_Div | 341 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoNLZ | 401 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoDNZ | 449 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mkfp32 | 527 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| pack32 | 550 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| NDFloat_Add32 | 601 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| NDFloat_Sub32 | 647 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| NDFloat_Mul32 | 695 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| NDFloat_Div32 | 737 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoNLZ32 | 797 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DoDNZ32 | 852 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
