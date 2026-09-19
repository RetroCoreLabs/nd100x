# Review: frontend

One row per function. A cell is empty until reviewed; write `ok`, or
`n/a`, or `fixed <commit>`. Rules: see the plan, section 3 (REVIEW).

## src/frontend/nd100x/charset.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| charset_set | 84 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_get | 90 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_name | 95 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_short | 101 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_from_name | 107 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_mapping_count | 137 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_mapping_at | 143 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_emit_host | 155 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| decode_one | 173 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| input_byte_for_cp | 205 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_translate_input | 220 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/config.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Config_Init | 108 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parseBootSpec | 196 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parseHDLCConfig | 267 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parseWatchConfig | 346 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Config_ParseCommandLine | 395 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Config_PrintHelp | 981 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/menu.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| build_image_url | 75 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| detect_drive_type | 158 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| count_directory_lines | 201 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| free_directory_pages | 219 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| build_directory_pages | 229 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| build_cache_path | 291 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ensure_cache_dir | 303 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| save_to_cache | 322 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| load_from_cache | 338 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cache_age_seconds | 361 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_cached_or_download_json | 376 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| json_str_or_empty | 409 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a single thread | n/a | ok | n/a |
| free_floppy_contents | 415 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | ok | n/a | ok | n/a single thread | n/a | ok | n/a |
| parse_one_floppy | 429 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | ok strdup checked by caller path | ok freed by free_floppy_contents / menu exit | n/a | ok | n/a single thread | n/a | ok | n/a |
| parse_floppies_json | 488 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| init_curses | 552 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cleanup_curses | 588 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_search_box | 597 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_floppy_list | 609 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| print_truncated | 660 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| safe_print_line | 669 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| safe_print_line_no_scroll | 701 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| print_directory_page | 720 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | ok strdup checked | ok freed here | n/a | ok | n/a single thread | n/a | ok | n/a |
| draw_floppy_details | 765 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_toolbar | 842 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| filter_floppies | 875 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| handle_search_input | 955 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| show_mount_popup_for_floppy | 996 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| unmount_floppy | 1002 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| init_mount_popup | 1009 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| show_mount_popup | 1023 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hide_mount_popup | 1036 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_mount_popup | 1042 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| handle_mount_popup_input | 1111 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| init_unmount_popup | 1162 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| show_unmount_popup | 1176 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hide_unmount_popup | 1183 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| format_file_size | 1189 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_unmount_popup | 1205 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| handle_unmount_popup_input | 1278 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_loop | 1319 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| load_catalog | 1474 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | ok json_data freed on every path | n/a | ok progress text to the user's terminal, not diagnostics | n/a single thread | n/a | ok | n/a |
| show_floppy_menu | 1510 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/nd100x.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| boot_type_for_ctrl | 104 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_terminal_carrier | 120 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| pipe_handle_control | 147 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| pipe_control_feed | 195 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| handle_sigint | 212 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| register_signals | 241 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dump_stats | 270 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| apply_cputype_override | 304 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| initialize | 319 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cleanup | 524 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ensure_directory | 562 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_tape_dir | 570 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| VScreenOutputHandler | 578 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PrinterOutputHandler | 599 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTapeWriterOutputHandler | 626 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| flush_tape_writer | 635 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| LogScreenHandler | 660 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| make_terminal_name | 678 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| findScreenForDevice | 685 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| load_paper_tape_file | 694 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| main | 734 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/nd100x_shell.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cmd_matches | 76 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_tokens | 105 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| join_path | 125 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_list_files | 138 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| resolve_program_file | 203 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_run_program | 257 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_show_regs | 364 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_help | 389 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_exit | 408 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| execute_command | 421 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| read_line | 445 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| execute_script | 466 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd100x_shell_run | 508 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/screenmenu.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| format_bytes | 53 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_set_mode | 63 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_show_message | 109 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_cpu_speed | 127 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_about | 173 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_charset | 197 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_panel_switches | 243 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_f12 | 286 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| tx_sender_state_name | 301 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_hdlc_status | 312 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| telnet_screen_stats | 467 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | ok TelnetServer_* accessors lock internally (not re-verified) | n/a | ok | n/a |
| screen_status_text | 487 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | ok buf_size from caller's array | ok | n/a single thread | n/a | ok | n/a |
| draw_screen_select | 551 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_release_prompt | 600 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_pending_list | 608 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_init | 646 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_enter | 655 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_tick | 664 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_process_key | 705 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/screenmenu.h

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| menu_is_active | 54 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/vscreen.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| VScreen_Init | 19 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| VScreen_Write | 51 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| VScreen_Redraw | 83 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| VScreen_Destroy | 125 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
