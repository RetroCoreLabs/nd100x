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
| build_image_url | 81 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| detect_drive_type | 179 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| count_directory_lines | 237 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| free_directory_pages | 263 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| build_directory_pages | 275 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| build_cache_path | 348 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ensure_cache_dir | 367 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| save_to_cache | 391 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| load_from_cache | 418 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cache_age_seconds | 456 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_cached_or_download_json | 478 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| json_str_or_empty | 526 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a single thread | n/a | ok | n/a |
| free_floppy_contents | 532 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | ok | n/a | ok | n/a single thread | n/a | ok | n/a |
| parse_one_floppy | 546 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | ok strdup checked by caller path | ok freed by free_floppy_contents / menu exit | n/a | ok | n/a single thread | n/a | ok | n/a |
| parse_floppies_json | 605 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| init_curses | 678 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cleanup_curses | 718 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_search_box | 728 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_floppy_list | 741 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| print_truncated | 804 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| safe_print_line | 815 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| safe_print_line_no_scroll | 858 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| print_directory_page | 884 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | ok strdup checked | ok freed here | n/a | ok | n/a single thread | n/a | ok | n/a |
| draw_floppy_details | 929 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_toolbar | 1014 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| filter_floppies | 1059 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| handle_search_input | 1151 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| show_mount_popup_for_floppy | 1199 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| unmount_floppy | 1206 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| init_mount_popup | 1214 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| show_mount_popup | 1229 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hide_mount_popup | 1246 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_mount_popup | 1253 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| handle_mount_popup_input | 1336 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| init_unmount_popup | 1400 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| show_unmount_popup | 1415 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hide_unmount_popup | 1423 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| format_file_size | 1430 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_unmount_popup | 1452 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| handle_unmount_popup_input | 1537 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_loop | 1587 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| load_catalog | 1772 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | ok json_data freed on every path | n/a | ok progress text to the user's terminal, not diagnostics | n/a single thread | n/a | ok | n/a |
| show_floppy_menu | 1813 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/nd100x.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| boot_type_for_ctrl | 108 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_terminal_carrier | 130 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| pipe_handle_control | 164 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| pipe_control_feed | 234 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| handle_sigint | 260 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| register_signals | 289 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dump_stats | 321 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| apply_cputype_override | 362 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| initialize | 382 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cleanup | 614 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ensure_directory | 651 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| get_tape_dir | 660 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| VScreenOutputHandler | 668 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PrinterOutputHandler | 696 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTapeWriterOutputHandler | 730 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| flush_tape_writer | 742 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| LogScreenHandler | 773 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| make_terminal_name | 798 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| findScreenForDevice | 805 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| load_paper_tape_file | 818 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| main | 865 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/nd100x_shell.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cmd_matches | 83 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_tokens | 124 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| join_path | 158 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_list_files | 175 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| resolve_program_file | 255 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_run_program | 331 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_show_regs | 448 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_help | 475 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cmd_exit | 496 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| execute_command | 510 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| read_line | 543 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| execute_script | 567 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd100x_shell_run | 621 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/screenmenu.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| format_bytes | 57 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_set_mode | 73 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_show_message | 120 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_cpu_speed | 138 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_about | 187 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_charset | 211 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_panel_switches | 261 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_f12 | 319 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| tx_sender_state_name | 334 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_hdlc_status | 351 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| telnet_screen_stats | 545 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | ok TelnetServer_* accessors lock internally (not re-verified) | n/a | ok | n/a |
| screen_status_text | 565 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | ok buf_size from caller's array | ok | n/a single thread | n/a | ok | n/a |
| draw_screen_select | 631 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_release_prompt | 691 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| draw_pending_list | 698 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_init | 741 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_enter | 750 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_tick | 759 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| menu_process_key | 807 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/screenmenu.h

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| menu_is_active | 61 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/frontend/nd100x/vscreen.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| VScreen_Init | 23 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| VScreen_Write | 62 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| VScreen_Redraw | 105 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| VScreen_Destroy | 159 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
