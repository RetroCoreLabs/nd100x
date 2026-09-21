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
| opcode_addd_add_two_decimal_operands | 665 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_subd_subtract_two_decimal_operands | 697 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_compare | 735 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_comd_compare_two_decimal_operands | 808 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_shde | 883 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_shde_decimal_shift | 1032 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_report_error | 1070 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_ascii_layout | 1085 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bcd_convert_to_packed | 1122 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_pack_convert_to_decimal | 1282 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| unpacked_digit_byte | 1328 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a CPU thread only | n/a | ok | n/a |
| bcd_convert_to_unpacked | 1354 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_unpack_convert_from_decimal | 1451 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| throttle_get_ns | 49 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| line_append | 99 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | n/a | ok | ok | n/a | n/a | ok | n/a | n/a | n/a | ok | n/a |
| cpu_set_type_from_env | 69 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_trace_nd110_set | 224 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_do_op | 240 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_get_effective_addr | 286 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_calc_iic | 362 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| recalc_internal_interrupt_bits | 386 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| calc_pk | 405 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_interrupt | 432 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_device_interrupt | 487 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_phys_mem_write | 512 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_phys_mem_read | 521 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_watchpoint_triggered | 532 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_memory_write | 563 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_memory_read | 583 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| memory_fetch | 595 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| check_and_switch | 602 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| trace_before_exec | 653 | n/a | n/a | unsigned short b (rule 5.2 finding) | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok --trace/BSD diag to stderr by design (profile: traces stay) | n/a CPU thread only | n/a | ok | n/a |
| private_cpu_tick | 681 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| sleep_ms | 734 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_instruction_is_jump | 768 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ring_record | 859 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_ring_dump | 878 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_run | 932 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_init | 1147 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_init_debugger | 1195 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_reset | 1209 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_cleanup | 1240 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_set_debugger_request_pause | 1256 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_get_debugger_request_pause | 1272 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_set_debugger_control_granted | 1293 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_get_debugger_control_granted | 1309 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_set_stop_reason | 1329 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_get_stop_reason | 1343 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_set_run_mode | 1362 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_get_run_mode | 1378 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_throttle_set_enabled | 1397 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_throttle_get_enabled | 1410 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_throttle_set_mhz | 1415 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_throttle_get_mhz | 1429 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu_bkpt.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| breakpoint_bitmap_rebuild | 43 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hash_address | 64 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_manager_init | 72 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_manager_cleanup | 86 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_manager_step_one | 98 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_manager_add | 110 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_manager_remove | 154 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_manager_clear | 192 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_manager_clear_type | 214 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| breakpoint_manager_check | 258 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_check_hit | 326 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| stop_reason_from_breakpoint | 418 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_manager_get_last_hit | 438 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| watchpoint_bitmap_rebuild | 482 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_watchpoint_add | 501 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_watchpoint_remove | 529 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_watchpoint_check_slow | 549 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_watchpoint_check | 593 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_watchpoint_clear | 599 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_watchpoint_get_count | 610 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_watchpoint_get | 616 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| phys_watchpoint_pagemap_rebuild | 638 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_phys_watchpoint_add | 653 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_phys_watchpoint_remove | 680 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_phys_watchpoint_check | 696 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_phys_watchpoint_clear | 731 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_phys_watchpoint_get_count | 742 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| bkpt_phys_watchpoint_get | 748 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu_disasm.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| disasm_op_to_str | 72 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_allocate | 767 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_instr | 778 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_exr | 799 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_addword | 811 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_init | 825 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_setlbl | 835 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_set_isdata | 844 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_userel | 852 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_dump | 877 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disasm_extract_opcode | 967 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| decode_140k | 998 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| decode_150k | 1137 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu_instr.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cpu_set_ring_at_clpt | 43 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_versn_read_cpu_version | 48 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| check_priv | 72 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_sign_extend | 95 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_add | 109 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| calc_el | 140 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| read_el | 151 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| write_el | 157 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_illegal_instr | 165 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| unimplemented_instr | 177 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_aaa_add_argument_to_a | 194 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_aab_add_argument_to_b | 204 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_aat_add_argument_to_t | 214 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_aax_add_argument_to_x | 224 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_mon_monitor_call | 235 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_saa_set_argument_to_a | 262 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_sab_set_argument_to_b | 269 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_sat_set_argument_to_t | 276 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_sax_set_argument_to_x | 283 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_shift_group | 290 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_nlz_normalize_floating_accumulator | 317 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_dnz_denormalize_to_fixed_point | 331 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_srb_store_register_block | 345 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_lrb_load_register_block | 358 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cjp | 381 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_jap_jump_if_a_positive | 418 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_jan_jump_if_a_negative | 431 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_jaz_jump_if_a_zero | 444 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_jaf_jump_if_a_not_zero | 467 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_jpc_increment_x_and_jump_if_x_positive | 480 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_jnc_increment_x_and_jump_if_x_negative | 495 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_jxn_jump_if_x_negative | 508 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_jxz_jump_if_x_zero | 520 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_jpl_jump_if_last_result_positive | 527 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_skp_skip_next_if_condition | 566 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_bfill_new_byte_fill | 587 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_bfill_byte_fill | 617 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_stz_store_zero | 643 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_sta_store_a_register | 651 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_stt_store_t_register | 660 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_stx_store_x_register | 668 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_std_store_double_word | 676 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_stf_store_floating_accumulator | 695 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_lda_load_a_register | 705 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_ldt_load_t_register | 713 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_ldx_load_x_register | 721 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_ldd_load_double_word | 729 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_ldf_load_floating_accumulator | 748 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_stztx_store_zero_t_x_relative | 760 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_statx_store_a_register_t_x_relative | 774 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_stdtx_store_double_word_t_x_relative | 788 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_ldatx_load_a_register_t_x_relative | 813 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_ldxtx_load_x_register_t_x_relative | 837 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_lddtx_load_double_word_t_x_relative | 861 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_ldbtx_load_b_register_t_x_relative | 889 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_min_memory_increment_and_skip_if_zero | 917 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_add_add_to_a_register | 933 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_sub_subtract_from_a_register | 943 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_and_logical_and_to_a_register | 952 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_ora_logical_or_to_a_register | 960 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_fad_add_to_floating_accumulator | 974 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_fsb_subtract_from_floating_accumulator | 1009 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_fmu_multiply_floating_accumulator | 1044 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_fdv_divide_floating_accumulator | 1079 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_jmp_jump_unconditional | 1122 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_geco_customer_specified_instruction | 1137 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
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
| opcode_iot_nord_1_legacy_do_not_use | 1806 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_iox_exchange_with_io_system | 1861 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_ioxt_exchange_with_io_system_t_addressed | 1876 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_ident_identify_interrupting_device | 1896 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_opcom_operator_communication | 1927 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_irw_inter_register_write | 1943 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_irr_inter_register_read | 1982 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_exam_examine_memory | 2004 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_depo_deposit_memory | 2020 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_pof_paging_off | 2034 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_piof_paging_and_interrupt_off | 2047 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_pon_paging_on | 2062 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_pion_paging_and_interrupt_on | 2070 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_iof_interrupt_off | 2081 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_ion_interrupt_on | 2097 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_rex_reset_extended_address_mode | 2107 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_sex_set_extended_address_mode | 2120 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd110_seg_phys | 2158 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd110_bankgroup_phys | 2173 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_setpt_set_page_tables | 2187 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_clept_clear_page_tables | 2263 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_clnreent_clear_non_reentrant_pages | 2387 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_chreent_pages | 2501 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| clepu_mark_working_set | 2607 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a CPU thread only | n/a | ok | n/a |
| opcode_clepu_clear_page_tables_and_collect_page_used | 2619 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_wglob_initialize_global_pointers | 2747 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_rglob_examine_global_pointers | 2767 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_inspl_insert_page_in_page_list | 2791 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_rempl_remove_page_from_page_list | 2855 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_cnrek_clear_non_reentrant_pages_sintran_k | 2925 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_clpt_clear_segment_from_page_tables | 3005 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd110_enter_page_table | 3119 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_enpt_enter_segment_in_page_tables | 3168 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_rept_enter_reentrant_segment_in_page_tables | 3186 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_lbit_load_bit_accumulator_from_logical_memory | 3206 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_lbitp_load_bit_accumulator_from_physical_memory | 3234 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_sbit_store_bit_accumulator_to_logical_memory | 3266 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_sbitp_store_bit_accumulator_to_physical_memory | 3301 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_lbytp_load_byte_from_physical_memory | 3341 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_sbytp_store_byte_in_physical_memory | 3376 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_tsetp_test_and_set_physical_word | 3416 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_rdusp_read_physical_word_bypassing_cache | 3442 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_lasb_load_a_from_segment_table_bank | 3468 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_sasb_store_a_in_segment_table_bank | 3479 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_lacb_load_a_from_core_map_bank | 3490 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_sacb_store_a_in_core_map_bank | 3501 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_lxsb_load_x_from_segment_table_bank | 3512 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_lxcb_load_x_from_core_map_bank | 3523 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_szsb_store_zero_in_segment_table_bank | 3534 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_szcb_store_zero_in_core_map_bank | 3545 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_init_initialize_stack | 3599 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_entr_enter_stack | 3636 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_leave_leave_stack | 3659 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_eleav_error_leave_stack | 3668 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_lbyt_load_byte_to_a_register | 3692 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_sbyt_store_byte_from_a_register | 3720 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_mix3_multiply_index_by_three | 3753 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| regop_logical | 3763 | n/a | n/a | ok | ok 16-bit results masked by uint16_t casts | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a CPU thread only | n/a | ok | n/a |
| regop_arith | 3814 | n/a | n/a | int tmp from do_add (kept from regop) | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a CPU thread only | n/a | ok | n/a |
| regop | 3854 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_mcl | 3907 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_mst | 3946 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_tra | 3984 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_exr | 4090 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_wait | 4123 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_halt | 4164 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_lwcs_load_writable_control_store | 4174 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
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
| opcode_movb_move_byte | 4826 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_movbf_move_bytes_forward | 4928 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_add_a_mem | 5017 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| do_move_bytes | 5060 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_movb_move_byte_buggy | 5199 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| opcode_movbf_move_bytes_forward_buggy | 5208 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_sub_a_mem | 5214 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_rdiv_org | 5254 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rdiv | 5297 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_rmpy_org | 5365 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rmpy | 5403 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mpy | 5455 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_get_bcd | 5483 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_store_bcd | 5489 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| instruction_add | 5500 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| instruction_add_range | 5510 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| instruction_add_mask | 5525 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_instructions | 5551 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu_mms.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| mms_cpu_set_ring_at_pf | 46 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_create_paging_tables | 54 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| convert_from_16_bit_pte | 83 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| convert_to_16_bit_pte | 89 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| calc_page_table_address | 98 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_destroy_paging_tables | 124 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_get_pt_shadow_address | 134 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_write | 171 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_read | 217 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_get_page_table_entry | 235 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_get_page_table_entry_for_debugger | 280 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_update_page_table_entry | 319 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_set_page_used | 346 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_set_page_written | 363 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_get_page_table_entry_debug_info | 391 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_map_virtual_to_physical | 451 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_update_pgs | 648 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| page_fault_diag | 677 | n/a | n/a | static long pf_calls (rule 5.2 finding) | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok diag to trace file | n/a CPU thread only | n/a | ok | n/a |
| mms_check_page_protection | 706 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_is_address_shadow_memory | 792 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_read_virtual_memory | 845 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_read_indirect_virtual_memory | 861 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_fetch_virtual_memory | 872 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_write_virtual_memory | 883 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_get_physical_memory_type | 907 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd_ecc_write_latch | 941 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd_ecc_read_detect | 989 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_read_physical_memory | 1084 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_write_physical_memory | 1117 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_write_physical_memory_wm | 1124 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_handle_memory_out_of_range | 1194 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_handle_mpv | 1204 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mms_handle_pf | 1238 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_ReadPhysicalMemory | 1257 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Dbg_WritePhysicalMemory | 1266 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dbg_map_virtual_to_physical | 1288 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
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
| cpumodel_from_name | 47 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpumodel_name | 67 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpumodel_display_name | 83 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpumodel_count | 114 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpumodel_name_by_index | 119 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/cpu/cpu_regs.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cpu_set_pil | 38 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_set_pea | 57 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_set_pes | 68 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_set_pgs | 79 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_setreg | 93 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_getbit | 105 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_clrbit | 122 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_setbit_sts_msb | 134 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_setbit | 151 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpu_adjust_sts | 179 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

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
| float_add | 245 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| float_sub | 271 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| float_mul | 300 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| float_div | 341 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| float_do_nlz | 401 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| float_do_dnz | 449 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mkfp32 | 527 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| pack32 | 550 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| float_add_32 | 601 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| float_sub_32 | 647 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| float_mul_32 | 695 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| float_div_32 | 737 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| float_do_nlz32 | 797 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| float_do_dnz32 | 852 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
