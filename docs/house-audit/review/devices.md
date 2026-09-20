# Review: devices

One row per function. A cell is empty until reviewed; write `ok`, or
`n/a`, or `fixed <commit>`. Rules: see the plan, section 3 (REVIEW).

## src/devices/cdc/device_cdc.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CdcDevice_SetBackingFile | 50 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cdc_lba_to_sector | 70 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cdc_effective_core | 82 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cdc_ensure_surface | 92 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cdc_attach_backing | 115 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cdc_read | 170 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cdc_write | 224 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cdc_execute_go | 295 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cdc_end | 434 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cdc_tick | 481 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cdc_ident | 491 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cdc_reset | 517 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cdc_destroy | 539 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cdc_boot | 576 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cdc_iot_op | 642 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateCdcDevice | 735 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/device.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Device_GetOddParity | 53 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Init | 59 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Destroy | 108 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Reset | 134 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Tick | 143 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Boot | 154 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_IsInAddress | 168 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_RegisterAddress | 177 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Read | 186 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Write | 195 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Ident | 204 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_QueueIODelay | 213 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_TickIODelay | 244 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| device_clear_interrupt | 272 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_GenerateInterrupt | 288 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_SetInterruptStatus | 304 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_IO_Seek | 321 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_IO_ReadWord | 332 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_IO_BufferReadWord | 357 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_IO_WriteWord | 385 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_IO_BufferWriteWord | 408 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_DMAWrite | 430 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_DMARead | 437 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_SetCharacterOutput | 448 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_SetCharacterInput | 459 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_OutputCharacter | 470 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_InputCharacter | 481 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_SetBlockRead | 494 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_SetBlockWrite | 506 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_SetBlockDiskInfo | 521 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_ReadBlock | 535 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_WriteBlock | 547 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/devicemanager.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DeviceManager_Init | 51 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_Destroy | 70 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_AddAllDevices | 93 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_AddSCSIDevice_WithConfig | 146 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_AddHDLCDevice_WithConfig | 169 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| create_device | 196 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_MasterClear | 324 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_AddDevice | 335 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_Read | 395 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_Write | 413 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_Ident | 435 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_Tick | 466 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_GetDeviceByAddress | 480 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_GetDeviceCount | 494 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_GetDeviceByIndex | 499 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_IotOp | 520 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_BootFrom | 539 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/drum/device_drum.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DrumDevice_SetBackingFile | 41 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| drum_attach_backing | 57 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| drum_read | 95 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| drum_write | 116 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| drum_execute_go | 160 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| drum_end | 240 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| drum_tick | 261 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| drum_ident | 271 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| drum_reset | 285 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| drum_destroy | 301 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateDrumDevice | 329 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/floppy/device_floppy_dma.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| floppy_dma_reset | 84 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| calculate_or_of_errors | 109 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| calculate_hardware_status_word | 117 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| calculate_status_word1 | 135 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| floppy_dma_read | 150 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| floppy_dma_write | 188 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| floppy_dma_tick | 258 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| floppy_dma_ident | 269 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_autoload_error_image | 325 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| execute_autoload | 359 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| execute_test | 391 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| execute_floppy_go | 403 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| read_end | 828 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| auto_load_end | 849 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateFloppyDMADevice | 866 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/floppy/device_floppy_pio.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| floppy_pio_reset | 69 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| floppy_pio_tick | 84 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| floppy_pio_read | 94 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| floppy_pio_write | 180 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| floppy_pio_ident | 331 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| floppy_pio_read_end | 348 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| floppy_pio_recalibrate_end | 372 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| floppy_pio_seek_end | 388 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| floppy_pio_clear_all_error_flags | 404 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_sector_as_deleted | 416 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| sector_is_deleted | 428 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| floppy_pio_execute_go | 441 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateFloppyPIODevice | 784 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/chip_com5025.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| COM5025_Init | 52 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_Reset | 70 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_master_reset | 96 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ReadByte | 111 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_set_output_pin | 124 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetReceiverStatus | 130 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_WriteByte | 166 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ReadWord | 226 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_WriteWord | 264 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetInputPin | 310 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_GetInputPin | 366 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_GetOutputPin | 375 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ClockReceiver | 384 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ClockTransmitter | 395 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_move_data_buffer_to_shift_register | 410 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_process_bit | 476 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ReceiveData | 527 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_TransmitData | 541 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_clear_all_input_pins | 574 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_set_transmitter_buffer_empty | 586 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_clear_transmitter_buffer_empty | 596 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_write_transmitter_data_buffer | 606 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_write_data_to_shift_register | 634 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_transmit_byte_output | 651 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_send_one_byte | 688 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_shift_register_empty | 708 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_map_bits_to_character_length | 717 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetTransmitterOutputCallback | 753 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetPinValueChangedCallback | 765 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/chip_com5025_registers.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| COM5025Registers_Init | 43 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_Clear | 57 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_Destroy | 104 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_SetReceiverStatus | 120 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_GetReceiverCharacterLen | 129 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_GetTransmitterCharacterLen | 138 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_SetModeControl | 147 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_IsProtocolModeCCP | 169 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_SetClockSpeed | 178 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_Clock | 188 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_AdjustTimer | 198 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_QueueReceivedData | 217 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_DataReceived | 273 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_IsNextByteSync | 293 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_MarkDataAsReceived | 328 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_registers_calc_crc | 345 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_CalcRXCrc | 381 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_registers_clear_rxcrc | 390 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_IsRxCrcEqual | 407 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_AggregateTXCrc | 416 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_registers_clear_txcrc | 425 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_CalcFinalTxCrc | 442 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_IsTxCrcEqual | 452 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_io_timer_init | 462 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_io_timer_clear | 471 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025IOTimer_SetCallback | 482 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_io_timer_set_clock_speed | 493 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_io_timer_adjust_timer | 502 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_io_timer_clock | 513 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/device_hdlc.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| hdlc_log_bits | 73 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_reset | 176 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_tick | 241 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_read | 289 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_write | 387 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_ident | 613 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_destroy | 647 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateHDLCDevice | 684 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_BridgeInjectRx | 818 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_on_modem_received_data | 842 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_on_modem_ring_indicator | 866 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_on_modem_data_set_ready | 890 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_on_modem_signal_detector | 914 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_on_modem_clear_to_send | 938 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_on_modem_request_to_send | 963 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_on_modem_data_terminal_ready | 981 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_on_dma_write_dma | 1001 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_on_dma_read_dma | 1010 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_on_dma_set_interrupt_bit | 1021 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_on_dma_send_hdlc_frame | 1046 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_on_dma_update_receiver_status | 1067 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_on_dma_clear_command | 1085 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_on_com5025_transmitter_output | 1104 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_on_com5025_pin_value_changed | 1121 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_check_trigger_interrupt_request_12 | 1198 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_check_trigger_interrupt_request_13 | 1231 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_check_trigger_interrupt | 1272 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_update_rqts | 1329 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_GetRxFrameStatus | 1379 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/dma_control_blocks.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| dma_control_blocks_log | 49 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_Init | 52 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_Destroy | 104 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_Clear | 140 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_SetTXPointer | 173 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_DebugTXFrames | 187 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_LoadTXBuffer | 215 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_LoadNextTXBuffer | 231 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_MarkBufferSent | 244 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_SetRXPointer | 271 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_LoadRXBuffer | 284 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_LoadNextRXBuffer | 300 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_IsNextRXbufValid | 353 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_MarkBufferReceived | 372 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_control_blocks_dma_read | 426 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| read_dcb_words | 470 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| pick_displacement | 492 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| DMAControlBlocks_LoadBufferDescription | 503 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_ReadNextByteDMA | 603 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_WriteNextByteDMA | 670 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_SetReadDMACallback | 765 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_SetWriteDMACallback | 776 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_SetSendHDLCFrameCallback | 787 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_SetInterruptCallback | 799 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_control_blocks_dma_write | 813 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/dma_dcb.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DCB_Init | 37 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_Clear | 63 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetKey | 73 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_HasRSOMFlag | 83 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_HasREOMFlag | 93 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetDataFlowCost | 103 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetDataMemoryAddress | 113 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetDataMemoryAddress | 124 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetBufferAddress | 135 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetBufferAddress | 145 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetOffsetFromLP | 155 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetOffsetFromLP | 165 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetKeyValue | 175 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetKeyValue | 185 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetByteCount | 195 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetByteCount | 205 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetDisplacement | 215 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetDisplacement | 225 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetListPointer | 235 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetListPointer | 245 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetDMAAddress | 257 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetDMAAddress | 267 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetDMABytesRead | 277 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetDMABytesRead | 287 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetDMABytesWritten | 297 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetDMABytesWritten | 307 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetDMAReadData | 317 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetDMAReadData | 327 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_IsDMAReadDataValid | 337 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_ClearDMAReadData | 347 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/dma_engine.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| dma_engine_cb_read_dma | 60 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_engine_cb_write_dma | 68 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_engine_tx_send_frame | 75 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_engine_tx_interrupt | 85 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_engine_rx_interrupt | 92 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_Init | 98 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_Destroy | 167 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_engine_clear | 196 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_Tick | 220 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_engine_dma_read | 242 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_engine_dma_write | 254 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_engine_on_set_interrupt_bit | 266 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_OnWriteDMA | 276 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_OnReadDMA | 286 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_ExecuteCommand | 302 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_engine_log | 318 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandDeviceClear | 329 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandInitialize | 353 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandReceiverStart | 446 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandReceiverContinue | 472 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandTransmitterStart | 498 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandDumpDataModule | 525 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandDumpRegisters | 589 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandLoadRegisters | 656 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetDMAAddress | 713 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_engine_clear_dma_command | 722 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_GetBufferKeyVault | 736 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_ScanNextTXBuffer | 748 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetWriteDMACallback | 784 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetReadDMACallback | 793 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetInterruptCallback | 802 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetSendFrameCallback | 811 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetUpdateReceiverStatusCallback | 820 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetClearCommandCallback | 830 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/dma_param_buf.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ParameterBuffer_Init | 35 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_Clear | 45 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_GetParameterControlRegister | 65 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_GetSyncAddressRegister | 74 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_GetCharacterLength | 83 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_GetDisplacement1 | 92 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_GetDisplacement2 | 101 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_GetMaxReceiverBlockLength | 110 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_GetReceiverStatusReg | 119 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_GetTransmitterStatusReg | 128 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_GetDmaBankBits | 137 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_SetParameterControlRegister | 148 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_SetSyncAddressRegister | 157 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_SetCharacterLength | 166 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_SetDisplacement1 | 175 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_SetDisplacement2 | 184 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_SetMaxReceiverBlockLength | 193 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_SetReceiverStatusReg | 202 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_SetTransmitterStatusReg | 211 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_SetDmaBankBits | 220 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/dma_receiver.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DMAReceiver_Init | 48 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_Destroy | 73 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_Clear | 84 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_Tick | 100 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| stop_receiver | 144 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_SetReceiverState | 161 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_ReceiveDataFromModem | 202 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_receiver_process_buffered_data | 235 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_receiver_process_complete_frame | 318 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_receiver_set_rxdma_flag | 396 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_receiver_clear_receive_frame_state | 411 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_SetInterruptCallback | 421 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_receiver_find_next_receive_buffer | 434 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_receiver_receive_data_buffer_byte | 465 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_receiver_enable_hdlc_receiver | 570 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/dma_transmitter.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DMATransmitter_Init | 46 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_Destroy | 65 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_Clear | 75 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_Tick | 89 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_transmitter_set_txdma_flag | 145 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| record_tx_history | 188 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | ok copyLen bounded by HDLC_TX_HISTORY_DATA_SIZE | ok | n/a emulation thread only | n/a | ok | n/a |
| send_outbound_frame | 210 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | ok frameLength <= sizeof(frameBuffer) by HDLCFrame_BuildFrame | ok | n/a emulation thread only | n/a | ok | n/a |
| dma_transmitter_send_all_buffers | 235 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| dma_transmitter_set_engine_sender_state | 338 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_SetSenderState | 367 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_SetSendFrameCallback | 378 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_SetInterruptCallback | 388 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/hdlc_crc.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| initialize_parity_tables | 68 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CRC_CalculateCRC16Buffer | 83 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CRC_CalcCrc16 | 97 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CRC_CalcCCITT | 106 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| hdlc_crc_calculate_parity_bit | 113 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CRC_AddParityBit | 140 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CRC_CheckParity | 148 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/hdlc_frame.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| HDLCFrame_Init | 43 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_Reset | 52 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_UpdateCRC | 67 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_CalculateCRC | 72 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_AddBytes | 87 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_AddByte | 102 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_IsFrameComplete | 200 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_IsCRCValid | 205 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_GetFrameLength | 210 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_GetFrameData | 215 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_StuffByte | 220 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_DestuffByte | 244 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_BuildFrame | 250 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/modem.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| sleep_ms | 46 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd_set_nonblocking | 61 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd_connect_in_progress | 86 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| queue_init | 102 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| queue_destroy | 109 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| queue_write | 115 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| queue_write_all | 142 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| queue_read | 168 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| queue_has_data | 188 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_nodelay | 201 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| resolve_address | 213 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| try_connect | 237 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| sleep_check_shutdown | 299 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| server_worker | 313 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| service_connection | 451 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | ok modem worker thread; queues are the locked hand-off, atomics for flags | n/a | ok | n/a |
| client_worker | 509 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_Init | 580 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_Destroy | 601 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_StartModem | 625 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_Tick | 687 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SendByte | 710 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SendBytes | 731 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetDTR | 751 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetRTS | 765 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetDSR | 779 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetCTS | 792 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetReceivedDataCallback | 809 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetRingIndicatorCallback | 816 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetDataSetReadyCallback | 823 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetSignalDetectorCallback | 830 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetClearToSendCallback | 837 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetRequestToSendCallback | 844 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetDataTerminalReadyCallback | 851 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/tcp_receive_buffer.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| TcpReceiveBuffer_Init | 32 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| TcpReceiveBuffer_Destroy | 50 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| TcpReceiveBuffer_Enqueue | 67 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| TcpReceiveBuffer_DequeueByte | 104 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| TcpReceiveBuffer_Available | 121 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| TcpReceiveBuffer_Clear | 130 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/lineprinter/device_line_printer.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| line_printer_reset | 43 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| line_printer_tick | 59 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| line_printer_read | 68 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| line_printer_write | 96 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| line_printer_ident | 168 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateLinePrinterDevice | 185 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/panel/panel.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| setup_pap | 46 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| process_message_control | 61 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ProcessTerminalPanc | 95 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ProcessTerminalLamp | 240 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| update_machine_time | 251 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/papertape/device_paper_tape.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| paper_tape_reset | 43 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| paper_tape_tick | 59 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| paper_tape_read | 69 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| paper_tape_write | 97 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| paper_tape_ident | 191 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| paper_tape_destroy | 208 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTape_LoadTape | 223 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreatePaperTapeDevice | 260 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/papertapewriter/device_paper_tape_writer.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| paper_tape_writer_reset | 45 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| paper_tape_writer_tick | 59 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| paper_tape_writer_read | 69 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| paper_tape_writer_grow_buffer | 101 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| paper_tape_writer_write | 131 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| paper_tape_writer_ident | 220 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| punch_end | 237 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| paper_tape_writer_destroy | 256 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTapeWriter_GetTapeData | 271 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreatePaperTapeWriterDevice | 289 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/rtc/device_rtc.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| RTC_SetWallClockMode | 53 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rtc_now_ns | 58 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rtc_reset | 76 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rtc_clear_clock_ticks | 94 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rtc_tick | 114 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rtc_read | 183 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rtc_write | 218 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rtc_ident | 280 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateRTCDevice | 310 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/scsi/device_scsi.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| scsi_log | 82 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_get_mar | 99 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_increment_mar | 104 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_ParseUnitType | 112 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_UnitTypeName | 140 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_SetUnitType | 159 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_on_ncr_interrupt | 198 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_on_ncr_data_request | 214 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_read_next_byte_dma | 243 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_write_next_byte_dma | 263 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_step_go_state | 298 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_reset | 354 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ncr_read_register | 414 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok -1 = not an NCR register | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| scsi_status_word | 446 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| scsi_read | 496 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_write_control | 578 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| scsi_write | 653 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_tick | 725 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_ident | 751 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_boot | 787 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_destroy | 886 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateSCSIDevice | 898 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/scsi/disk_scsi.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DiskSCSI_LastLBA | 40 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| disk_scsi_set_field | 55 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DiskSCSI_SetDiskType | 69 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/scsi/scsi_bus.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SCSIBus_PhaseName | 27 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_Init | 33 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_AddDevice | 43 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_Clock | 67 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_bus_regen_data | 86 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_bus_regen_ctrl | 102 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_ControlRead | 139 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_ControlWait | 150 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_ControlWrite | 166 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_DataRead | 185 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_DataWrite | 195 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/scsi/scsi_device.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| scsi_target_report_condition | 26 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_put_u16be | 35 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_put_u24be | 41 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_put_u32be | 48 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_get_u16be | 56 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_get_u24be | 61 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_get_u32be | 66 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_target_log | 72 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_target_adjust_timer | 91 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_target_buf_control_push | 102 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_target_buf_control_pop | 122 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DefaultGetData | 143 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DefaultPutData | 166 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_target_get_data | 189 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_target_put_data | 199 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DataIn | 215 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DataOut | 226 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_StatusComplete | 236 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_target_set_sense_data | 256 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_Sense | 293 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_ReportBadCmd | 312 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_ReportBadLun | 319 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_target_recv_byte | 329 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_target_send_byte | 338 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_target_send_buffer_byte | 348 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_target_command_done | 358 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_target_message | 393 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_target_step | 408 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_target_clock | 646 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_target_ctrl_changed | 664 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DeviceReset | 674 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_Init | 701 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/scsi/scsi_hdd.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| scsihdd_log | 27 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsihdd_has_media | 48 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsihdd_get_lun | 67 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsihdd_read_block | 84 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsihdd_write_block | 110 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsihdd_get_data | 135 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsihdd_put_data | 183 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsihdd_command_read_capacity | 248 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsihdd_command_inquiry | 272 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsihdd_command_test_unit_ready | 322 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsihdd_command_mode_sense | 350 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsihdd_command | 537 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsihdd_device_reset | 789 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_Init | 807 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/smd/device_smd.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| smd_op_name | 66 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| smd_reg_read_name | 95 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| smd_reg_write_name | 112 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| smd_error_name | 129 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| smd_reset | 156 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| smd_read | 177 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| smd_write | 428 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| handle_error | 467 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| smd_tick | 801 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| smd_ident | 813 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| smd_boot | 835 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| execute_go | 938 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| convert_cylinder_head_sector_to_logical_block | 1045 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| finish_operation | 1114 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| smd_read_end | 1471 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| smd_timeout_end | 1539 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| load_is_illegal | 1587 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| unit_attached | 1612 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| clear_flip_flops | 1671 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| clear_errors | 1700 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_selected_unit | 1717 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| increment_core_address | 1805 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| decrement_word_counter | 1818 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateSMDDevice | 1831 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| smd_destroy | 2007 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/smd/disk_smd.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DiskSMD_SetDiskType | 37 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/terminal/device_terminal.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| terminal_reset | 114 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| terminal_tick | 134 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| terminal_read | 210 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| terminal_write_input_control | 265 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| terminal_write | 297 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| terminal_ident | 388 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| write_end | 412 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Terminal_QueueKeyCode | 438 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| terminal_input_function | 465 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateTerminalDevice | 475 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/winchester/device_winchester.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| wd_op_name | 65 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| wd_clear_flip_flops | 97 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| wd_unit_attached | 108 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| wd_finish_operation | 137 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| wd_clear_errors | 155 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| wd_device_clear | 167 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| wd_read_status | 184 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| wd_read | 221 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| wd_write | 281 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| wd_transfer_end | 433 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| wd_execute_go | 456 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| wd_tick | 681 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| wd_ident | 687 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| wd_reset | 717 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| wd_boot | 761 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| wd_destroy | 848 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateWinchesterDevice | 866 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/winchester/disk_winchester.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DiskWinchester_SetDiskType | 40 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DiskWinchester_ChsToLba | 109 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
