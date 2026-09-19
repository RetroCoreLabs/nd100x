# Review: machine

One row per function. A cell is empty until reviewed; write `ok`, or
`n/a`, or `fixed <commit>`. Rules: see the plan, section 3 (REVIEW).

## src/machine/io.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| IO_Init | 36 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| IO_Destroy | 44 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| IO_Read | 49 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| IO_Write | 54 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| IO_Ident | 59 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| IO_Tick | 64 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| io_op | 74 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/machine/machine.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| init_drive_arrays | 78 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cleanup_drive_arrays | 98 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| drives_for_type | 121 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| drive_type_name | 153 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| drive_type_for_device | 166 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| machine_init | 192 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| machine_add_hdlc | 218 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cleanup_machine | 234 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| machine_run | 287 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| sleep_ms | 323 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| machine_stop | 339 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| setdefaultconfig | 356 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| write_memory | 378 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mount_floppy | 386 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| machine_floppy_swap | 407 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| unmount_drive | 412 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| machine_floppy_mount_catalog | 450 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mount_smd | 515 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mount_winchester | 544 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mount_drive | 556 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mount_scsi | 574 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| autoMountDrives | 600 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| tape_leader_load | 638 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| program_load | 729 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| isMounted | 874 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| list_mount | 1064 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mount_drive_opfs | 1129 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mount_drive_gateway | 1161 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| machine_block_read | 1193 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| machine_block_write | 1276 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| machine_block_disk_info | 1323 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/machine/machine_config.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| MC_DescriptorForType | 58 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MC_DescriptorForName | 66 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MC_CtrlTypeFromName | 75 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MC_CtrlTypeName | 81 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| str_copy | 90 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| str_trim | 97 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| str_lower | 107 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| str_ieq | 112 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| parse_bool | 123 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MachineConfig_InitBaseline | 135 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MachineConfig_SetDefaults | 174 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MachineConfig_DefaultIniName | 201 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mc_err | 240 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mc_get_controller | 262 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mc_parse_controller_header | 281 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| mc_parse_disk_key | 323 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MachineConfig_LoadFile | 380 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ranges_overlap | 809 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MachineConfig_Validate | 816 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MachineConfig_Print | 905 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MachineConfig_WriteFile | 974 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MachineConfig_CpuTypeForNumber | 1093 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/machine/machine_config_apply.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| MachineConfig_ApplyCpu | 16 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MachineConfig_ApplyDevices | 45 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/machine/machine_config_json.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| put | 36 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| esc | 51 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| kv_str | 68 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| media_name | 76 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| MachineConfig_ToJson | 87 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
