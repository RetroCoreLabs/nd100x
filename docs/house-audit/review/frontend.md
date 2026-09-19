# Review: frontend

One row per function. A cell is empty until reviewed; write `ok`, or
`n/a`, or `fixed <commit>`. Rules: see the plan, section 3 (REVIEW).

## src/frontend/nd100x/charset.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| charset_set | 96 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_get | 104 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_name | 109 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_short | 118 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_from_name | 127 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_mapping_count | 164 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_mapping_at | 173 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_emit_host | 200 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| decode_one | 221 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| input_byte_for_cp | 279 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| charset_translate_input | 308 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/config.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Config_Init | 110 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parseBootSpec | 217 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parseHDLCConfig | 314 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parseWatchConfig | 411 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Config_ParseCommandLine | 481 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Config_PrintHelp | 1212 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/menu.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| build_image_url | 77 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| detect_drive_type | 175 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| count_directory_lines | 233 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| free_directory_pages | 259 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| build_directory_pages | 271 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| build_cache_path | 344 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ensure_cache_dir | 363 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| save_to_cache | 387 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| load_from_cache | 414 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cache_age_seconds | 452 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_cached_or_download_json | 474 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| json_str_or_empty | 522 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a single thread | n/a | ok | n/a |
| free_floppy_contents | 528 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | ok | n/a | ok | n/a single thread | n/a | ok | n/a |
| parse_one_floppy | 542 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | ok strdup checked by caller path | ok freed by free_floppy_contents / menu exit | n/a | ok | n/a single thread | n/a | ok | n/a |
| parse_floppies_json | 601 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| init_curses | 674 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cleanup_curses | 714 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_search_box | 724 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_floppy_list | 737 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| print_truncated | 800 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| safe_print_line | 811 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| safe_print_line_no_scroll | 854 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| print_directory_page | 880 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | ok strdup checked | ok freed here | n/a | ok | n/a single thread | n/a | ok | n/a |
| draw_floppy_details | 925 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_toolbar | 1010 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| filter_floppies | 1055 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| handle_search_input | 1147 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| show_mount_popup_for_floppy | 1195 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| unmount_floppy | 1202 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| init_mount_popup | 1210 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| show_mount_popup | 1225 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hide_mount_popup | 1242 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_mount_popup | 1249 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| handle_mount_popup_input | 1332 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| init_unmount_popup | 1394 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| show_unmount_popup | 1409 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hide_unmount_popup | 1417 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| format_file_size | 1424 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_unmount_popup | 1446 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| handle_unmount_popup_input | 1531 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_loop | 1579 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| load_catalog | 1764 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | ok json_data freed on every path | n/a | ok progress text to the user's terminal, not diagnostics | n/a single thread | n/a | ok | n/a |
| show_floppy_menu | 1805 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/nd100x.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| boot_type_for_ctrl | 104 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_terminal_carrier | 126 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| pipe_handle_control | 160 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| pipe_control_feed | 230 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| handle_sigint | 256 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| register_signals | 285 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dump_stats | 317 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| apply_cputype_override | 358 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| initialize | 378 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cleanup | 610 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ensure_directory | 647 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_tape_dir | 656 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| VScreenOutputHandler | 664 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PrinterOutputHandler | 692 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTapeWriterOutputHandler | 726 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| flush_tape_writer | 738 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| LogScreenHandler | 769 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| make_terminal_name | 794 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| findScreenForDevice | 801 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| load_paper_tape_file | 814 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| main | 861 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/nd100x_shell.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cmd_matches | 79 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_tokens | 120 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| join_path | 154 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_list_files | 171 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| resolve_program_file | 251 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_run_program | 327 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_show_regs | 444 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_help | 471 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_exit | 492 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| execute_command | 506 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| read_line | 539 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| execute_script | 563 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd100x_shell_run | 617 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/screenmenu.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| format_bytes | 53 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_set_mode | 69 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_show_message | 116 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_cpu_speed | 134 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_about | 183 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_charset | 207 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_panel_switches | 257 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_f12 | 315 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| tx_sender_state_name | 330 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_hdlc_status | 347 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| telnet_screen_stats | 539 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | ok TelnetServer_* accessors lock internally (not re-verified) | n/a | ok | n/a |
| screen_status_text | 559 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | ok buf_size from caller's array | ok | n/a single thread | n/a | ok | n/a |
| draw_screen_select | 625 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_release_prompt | 685 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_pending_list | 692 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_init | 735 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_enter | 744 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_tick | 753 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_process_key | 801 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/screenmenu.h

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| menu_is_active | 57 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/vscreen.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| VScreen_Init | 19 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| VScreen_Write | 58 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| VScreen_Redraw | 101 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| VScreen_Destroy | 155 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
