# Review: machine

One row per function. A cell is empty until reviewed; write `ok`, or
`n/a`, or `fixed <commit>`. Rules: see the plan, section 3 (REVIEW).

## src/machine/io.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| IO_Init | 39 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| IO_Destroy | 49 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| IO_Read | 54 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| IO_Write | 59 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| IO_Ident | 64 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| IO_Tick | 69 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| io_op | 81 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/machine/machine.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| init_drive_arrays | 73 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cleanup_drive_arrays | 99 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| drives_for_type | 127 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| drive_type_name | 162 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| drive_type_for_device | 181 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| machine_init | 209 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| machine_add_hdlc | 237 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cleanup_machine | 259 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| machine_run | 321 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| sleep_ms | 357 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| machine_stop | 376 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| setdefaultconfig | 393 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| write_memory | 414 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mount_floppy | 425 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mount_drive | 434 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| machine_floppy_swap | 448 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| unmount_drive | 457 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| machine_floppy_mount_catalog | 501 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mount_smd | 598 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mount_winchester | 628 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mount_scsi | 663 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| autoMountDrives | 696 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| read_whole_file | 736 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | ok malloc checked | ok caller frees | n/a | ok | n/a no shared data | n/a | ok | n/a |
| tape_leader_load | 767 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| program_load | 860 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| isMounted | 1012 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| open_local_image | 1043 | n/a | n/a | long from ftell (rule 5.2 finding, phase 5) | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | ok FILE owned by drive, closed in unmount_drive | n/a | ok | n/a no shared data | n/a | ok | n/a |
| list_mount | 1252 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mount_drive_opfs | 1327 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mount_drive_gateway | 1370 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| read_entry_bytes | 1416 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok 1/0/-1 documented | n/a static | ok | n/a | n/a | n/a | ok | n/a no shared data | n/a | ok | n/a |
| log_floppy_read_diag | 1460 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | ok static counter only limits log lines; a race prints one extra line | n/a | ok | n/a |
| machine_block_read | 1477 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| machine_block_write | 1558 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| machine_block_disk_info | 1639 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/machine/machine_config.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| MC_DescriptorForType | 63 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MC_DescriptorForName | 75 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MC_CtrlTypeFromName | 91 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MC_CtrlTypeName | 97 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| str_copy | 106 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| str_trim | 116 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| str_lower | 135 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| str_ieq | 143 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_bool | 162 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MachineConfig_InitBaseline | 178 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MachineConfig_SetDefaults | 222 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MachineConfig_DefaultIniName | 261 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mc_err | 316 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mc_get_controller | 342 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mc_parse_controller_header | 372 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mc_parse_disk_key | 418 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MachineConfig_LoadFile | 486 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ranges_overlap | 1138 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| validate_boot_device | 1149 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a no shared data | n/a | ok | n/a |
| MachineConfig_Validate | 1203 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MachineConfig_Print | 1283 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MachineConfig_WriteFile | 1386 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MachineConfig_CpuTypeForNumber | 1603 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/machine/machine_config_apply.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| MachineConfig_ApplyCpu | 19 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MachineConfig_ApplyDevices | 61 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/machine/machine_config_json.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| put | 45 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| esc | 67 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| kv_str | 105 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| media_name | 113 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MachineConfig_ToJson | 130 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
