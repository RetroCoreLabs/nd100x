# Review: frontend

One row per function. A cell is empty until reviewed; write `ok`, or
`n/a`, or `fixed <commit>`. Rules: see the plan, section 3 (REVIEW).

## src/frontend/nd100x/charset.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| charset_set | 100 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_get | 108 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_name | 113 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_short | 122 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_from_name | 131 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_mapping_count | 168 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_mapping_at | 177 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_emit_host | 204 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| decode_one | 225 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| input_byte_for_cp | 283 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_translate_input | 312 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/config.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Config_Init | 114 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parseBootSpec | 221 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parseHDLCConfig | 318 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parseWatchConfig | 415 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Config_ParseCommandLine | 485 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Config_PrintHelp | 1216 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/menu.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| build_image_url | 82 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| detect_drive_type | 180 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| count_directory_lines | 238 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| free_directory_pages | 264 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| build_directory_pages | 276 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| build_cache_path | 349 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ensure_cache_dir | 368 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| save_to_cache | 392 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| load_from_cache | 419 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cache_age_seconds | 457 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_cached_or_download_json | 479 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| json_str_or_empty | 527 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a single thread | n/a | ok | n/a |
| free_floppy_contents | 533 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | ok | n/a | ok | n/a single thread | n/a | ok | n/a |
| parse_one_floppy | 547 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | ok strdup checked by caller path | ok freed by free_floppy_contents / menu exit | n/a | ok | n/a single thread | n/a | ok | n/a |
| parse_floppies_json | 606 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| init_curses | 679 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cleanup_curses | 719 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_search_box | 729 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_floppy_list | 742 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| print_truncated | 805 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| safe_print_line | 816 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| safe_print_line_no_scroll | 859 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| print_directory_page | 885 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | ok strdup checked | ok freed here | n/a | ok | n/a single thread | n/a | ok | n/a |
| draw_floppy_details | 930 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_toolbar | 1015 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| filter_floppies | 1060 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| handle_search_input | 1152 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| show_mount_popup_for_floppy | 1200 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| unmount_floppy | 1207 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| init_mount_popup | 1215 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| show_mount_popup | 1230 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hide_mount_popup | 1247 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_mount_popup | 1254 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| handle_mount_popup_input | 1337 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| init_unmount_popup | 1401 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| show_unmount_popup | 1416 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hide_unmount_popup | 1424 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| format_file_size | 1431 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_unmount_popup | 1453 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| handle_unmount_popup_input | 1538 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_loop | 1588 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| load_catalog | 1773 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | ok json_data freed on every path | n/a | ok progress text to the user's terminal, not diagnostics | n/a single thread | n/a | ok | n/a |
| show_floppy_menu | 1814 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/nd100x.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| boot_type_for_ctrl | 109 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_terminal_carrier | 131 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| pipe_handle_control | 165 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| pipe_control_feed | 235 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| handle_sigint | 261 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| register_signals | 290 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dump_stats | 322 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| apply_cputype_override | 363 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| initialize | 383 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cleanup | 615 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ensure_directory | 652 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_tape_dir | 661 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| VScreenOutputHandler | 669 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PrinterOutputHandler | 697 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTapeWriterOutputHandler | 731 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| flush_tape_writer | 743 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| LogScreenHandler | 774 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| make_terminal_name | 799 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| findScreenForDevice | 806 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| load_paper_tape_file | 819 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| main | 866 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/nd100x_shell.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cmd_matches | 85 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_tokens | 126 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| join_path | 160 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_list_files | 177 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| resolve_program_file | 257 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_run_program | 333 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_show_regs | 450 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_help | 477 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_exit | 498 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| execute_command | 512 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| read_line | 545 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| execute_script | 569 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd100x_shell_run | 623 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/screenmenu.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| format_bytes | 58 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_set_mode | 74 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_show_message | 121 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_cpu_speed | 139 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_about | 188 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_charset | 212 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_panel_switches | 262 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_f12 | 320 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| tx_sender_state_name | 335 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_hdlc_status | 352 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| telnet_screen_stats | 546 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | ok TelnetServer_* accessors lock internally (not re-verified) | n/a | ok | n/a |
| screen_status_text | 566 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | ok buf_size from caller's array | ok | n/a single thread | n/a | ok | n/a |
| draw_screen_select | 632 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_release_prompt | 692 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_pending_list | 699 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_init | 742 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_enter | 751 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_tick | 760 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_process_key | 808 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/screenmenu.h

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| menu_is_active | 61 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/vscreen.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| VScreen_Init | 24 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| VScreen_Write | 63 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| VScreen_Redraw | 106 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| VScreen_Destroy | 160 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
