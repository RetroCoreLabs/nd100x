# Review: debugger

One row per function. A cell is empty until reviewed; write `ok`, or
`n/a`, or `fixed <commit>`. Rules: see the plan, section 3 (REVIEW).

## src/debugger/debugger.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| resolve_source_path | 261 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| debugger_signal_handler | 340 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| debugger_thread | 348 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| sleep_ms | 388 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| start_debugger | 403 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndx_server_terminate | 412 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| stop_debugger_thread | 421 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cpuStopReasonToString | 456 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_wait_for_debugger | 483 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_release_debugger | 519 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_check_cpu_events | 531 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ensure_cpu_running | 587 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| is_procedure_call | 606 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dbg_read_data | 640 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dbg_write_data | 645 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_jpl_target_address | 663 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| find_stack_return_address | 709 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| debugger_update_jpl_entrypoint | 725 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| debugger_build_stack_trace | 739 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| step_cpu | 819 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| str_ends_with | 1263 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| free_source_references | 1282 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_next | 1314 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_step_in | 1328 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_step_out | 1341 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_continue | 1346 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_scopes | 1376 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_variable_to_array | 1530 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_local_variables | 1582 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_level_variables | 1654 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_pil_register_variables | 1731 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_register_variables | 1770 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_internal_registers_read_variables | 1898 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_internal_registers_write_variables | 1960 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_status_flag_variables | 2018 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetPageTableEntryInfo | 2092 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| GetPageTableMemoryRange | 2158 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_page_mms_entries | 2167 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| add_page_table_entries | 2240 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_variables | 2303 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| update_stack_frame | 2418 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| frame_line_entry_in_range | 2511 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rebuild_stack_from_b_chain | 2554 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_stack_trace | 2664 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| on_set_exception_breakpoints | 2931 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_set_breakpoints | 2956 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_set_instruction_breakpoints | 3089 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_data_breakpoint_info | 3143 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_set_data_breakpoints | 3290 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| free_symbol_table | 3424 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| init_symbol_support | 3463 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_launch_callback | 3536 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_configuration_done | 3856 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_pause | 3869 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_restart | 3880 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_disconnect | 3921 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_terminate | 3960 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_set_variable | 3986 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_evaluate | 4092 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_set_function_breakpoints | 4125 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_read_memory | 4217 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| debugger_base64_decode | 4312 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_write_memory | 4364 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_source | 4438 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| read_disasm_word | 4480 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | ok DAP thread; trap-free Dbg_* reads | n/a | ok | n/a |
| format_disasm_text | 4503 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | ok text_size from caller's array | ok | n/a single thread | n/a | ok | n/a |
| cmd_disassemble | 4522 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| debugger_console_output | 4600 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| flush_console_output | 4628 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_console_enable | 4689 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_console_write | 4757 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| symbol_type_to_dap_string | 4812 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| count_symbols | 4831 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | ok DAP thread | n/a | ok | n/a |
| add_debug_info_symbols | 4857 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | strdup results unchecked (phase 8) | n/a | n/a | ok | ok DAP thread | n/a | ok | n/a |
| cmd_symbol_list | 4886 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndx_server_init | 4958 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ndx_server_stop | 5073 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| debugger_kbd_input | 5086 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| find_symbol_by_address | 5478 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_symbol_for_address | 5500 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_source_location | 5512 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_default_dap_capabilities | 5532 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
