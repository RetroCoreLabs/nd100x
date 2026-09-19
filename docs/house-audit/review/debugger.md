# Review: debugger

One row per function. A cell is empty until reviewed; write `ok`, or
`n/a`, or `fixed <commit>`. Rules: see the plan, section 3 (REVIEW).

## src/debugger/debugger.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| resolve_source_path | 162 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| debugger_signal_handler | 227 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| debugger_thread | 235 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| sleep_ms | 273 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| start_debugger | 288 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndx_server_terminate | 297 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| stop_debugger_thread | 306 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpuStopReasonToString | 341 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_wait_for_debugger | 368 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_release_debugger | 402 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_check_cpu_events | 414 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ensure_cpu_running | 465 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| is_procedure_call | 483 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dbg_read_data | 516 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dbg_write_data | 522 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_jpl_target_address | 536 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| find_stack_return_address | 574 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| debugger_update_jpl_entrypoint | 590 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| debugger_build_stack_trace | 604 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| step_cpu | 682 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| str_ends_with | 1076 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| free_source_references | 1089 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_next | 1111 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_step_in | 1125 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_step_out | 1138 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_continue | 1143 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_scopes | 1173 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_variable_to_array | 1324 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_local_variables | 1379 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_level_variables | 1463 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_pil_register_variables | 1540 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_register_variables | 1580 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_internal_registers_read_variables | 1718 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_internal_registers_write_variables | 1781 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_status_flag_variables | 1840 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetPageTableEntryInfo | 1916 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetPageTableMemoryRange | 1966 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_page_mms_entries | 1975 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_page_table_entries | 2048 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_variables | 2109 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| update_stack_frame | 2220 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| frame_line_entry_in_range | 2311 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rebuild_stack_from_b_chain | 2344 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_stack_trace | 2446 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| on_set_exception_breakpoints | 2700 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_set_breakpoints | 2724 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_set_instruction_breakpoints | 2853 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_data_breakpoint_info | 2908 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_set_data_breakpoints | 3027 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| free_symbol_table | 3152 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| init_symbol_support | 3191 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_launch_callback | 3263 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_configuration_done | 3563 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_pause | 3576 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_restart | 3587 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_disconnect | 3628 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_terminate | 3667 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_set_variable | 3693 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_evaluate | 3762 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_set_function_breakpoints | 3792 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_read_memory | 3864 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| debugger_base64_decode | 3951 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_write_memory | 3984 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_source | 4039 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_disassemble | 4074 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| debugger_console_output | 4170 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| flush_console_output | 4194 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_console_enable | 4247 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_console_write | 4302 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| symbol_type_to_dap_string | 4344 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_symbol_list | 4356 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndx_server_init | 4447 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndx_server_stop | 4558 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| debugger_kbd_input | 4571 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| find_symbol_by_address | 4900 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_symbol_for_address | 4922 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_source_location | 4934 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_default_dap_capabilities | 4954 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
