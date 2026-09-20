# Review: devices

One row per function. A cell is empty until reviewed; write `ok`, or
`n/a`, or `fixed <commit>`. Rules: see the plan, section 3 (REVIEW).

## src/devices/cdc/device_cdc.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CdcDevice_SetBackingFile | 50 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cdc_lba_to_sector | 70 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cdc_effective_core | 82 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_EnsureSurface | 92 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_AttachBacking | 115 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_Read | 170 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_Write | 224 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_ExecuteGO | 295 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_End | 434 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_Tick | 481 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_Ident | 491 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_Reset | 517 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_Destroy | 539 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_Boot | 576 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_IotOp | 642 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
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
| CreateDevice | 196 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
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
| Drum_AttachBacking | 57 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Drum_Read | 95 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Drum_Write | 116 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Drum_ExecuteGO | 160 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Drum_End | 240 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Drum_Tick | 261 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Drum_Ident | 271 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Drum_Reset | 285 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Drum_Destroy | 301 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateDrumDevice | 329 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/floppy/device_floppy_dma.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| FloppyDMA_Reset | 84 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CalculateOrOfErrors | 109 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CalculateHardwareStatusWord | 117 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CalculateStatusWord1 | 135 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyDMA_Read | 150 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyDMA_Write | 188 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyDMA_Tick | 258 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyDMA_Ident | 269 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DmaAutoloadErrorImage | 325 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ExecuteAutoload | 359 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ExecuteTest | 391 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ExecuteFloppyGo | 403 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ReadEnd | 828 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| AutoLoadEnd | 849 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateFloppyDMADevice | 866 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/floppy/device_floppy_pio.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| FloppyPIO_Reset | 69 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_Tick | 84 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_Read | 94 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_Write | 180 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_Ident | 331 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_ReadEnd | 348 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_RecalibrateEnd | 372 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_SeekEnd | 388 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_ClearAllErrorFlags | 404 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SetSectorAsDeleted | 416 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SectorIsDeleted | 428 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| floppy_pio_execute_go | 441 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateFloppyPIODevice | 784 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/chip_com5025.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| COM5025_Init | 52 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_Reset | 70 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_master_reset | 96 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ReadByte | 111 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetOutputPin | 124 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetReceiverStatus | 130 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_WriteByte | 166 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ReadWord | 226 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_WriteWord | 264 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetInputPin | 310 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_GetInputPin | 366 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_GetOutputPin | 375 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ClockReceiver | 384 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ClockTransmitter | 395 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_MoveTDBtoTSR | 410 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| com5025_process_bit | 476 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ReceiveData | 527 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_TransmitData | 541 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ClearAllInputPins | 574 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetTransmitterBufferEmpty | 586 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ClearTransmitterBufferEmpty | 596 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_WriteTransmitterDataBuffer | 606 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_WriteDataToTSR | 634 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_TransmitByteOutput | 651 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SendOneByte | 688 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_TSR_Empty | 708 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_MapBits2CharLen | 717 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
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
| HDLC_Reset | 176 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_Tick | 241 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_Read | 289 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_Write | 387 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_Ident | 613 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_Destroy | 647 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateHDLCDevice | 684 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_BridgeInjectRx | 818 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnModemReceivedData | 842 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnModemRingIndicator | 866 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnModemDataSetReady | 890 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnModemSignalDetector | 914 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnModemClearToSend | 938 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnModemRequestToSend | 963 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnModemDataTerminalReady | 981 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnDMAWriteDMA | 1001 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnDMAReadDMA | 1010 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnDMASetInterruptBit | 1021 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnDMASendHDLCFrame | 1046 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnDMAUpdateReceiverStatus | 1067 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnDMAClearCommand | 1085 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnCOM5025TransmitterOutput | 1104 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnCOM5025PinValueChanged | 1121 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CheckTriggerIRQ12 | 1198 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CheckTriggerIRQ13 | 1231 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CheckTriggerInterrupt | 1272 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_UpdateRQTS | 1329 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_GetRxFrameStatus | 1379 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/dma_control_blocks.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DMAControlBlocks_Log | 49 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
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
| DMAControlBlocks_DMARead | 426 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| read_dcb_words | 470 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| pick_displacement | 492 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| DMAControlBlocks_LoadBufferDescription | 503 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_ReadNextByteDMA | 603 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_WriteNextByteDMA | 670 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_SetReadDMACallback | 765 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_SetWriteDMACallback | 776 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_SetSendHDLCFrameCallback | 787 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_SetInterruptCallback | 799 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_DMAWrite | 813 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

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
| DMAEngine_CBReadDMA | 60 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CBWriteDMA | 68 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_TXSendFrame | 75 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_TXInterrupt | 85 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_RXInterrupt | 92 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
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
| StopReceiver | 144 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
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
| initializeParityTables | 68 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
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
| LinePrinter_Reset | 43 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| LinePrinter_Tick | 59 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| LinePrinter_Read | 68 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| LinePrinter_Write | 96 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| LinePrinter_Ident | 168 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
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
| PaperTape_Reset | 43 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTape_Tick | 59 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTape_Read | 69 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTape_Write | 97 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTape_Ident | 191 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTape_Destroy | 208 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTape_LoadTape | 223 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreatePaperTapeDevice | 260 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/papertapewriter/device_paper_tape_writer.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| PaperTapeWriter_Reset | 45 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTapeWriter_Tick | 59 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTapeWriter_Read | 69 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTapeWriter_GrowBuffer | 101 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTapeWriter_Write | 131 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTapeWriter_Ident | 220 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PunchEnd | 237 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTapeWriter_Destroy | 256 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
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
| SCSI_Log | 82 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_GetMAR | 99 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_IncrementMAR | 104 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_ParseUnitType | 112 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_UnitTypeName | 140 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_SetUnitType | 159 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_OnNCRInterrupt | 198 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_OnNCRDataRequest | 214 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_ReadNextByteDMA | 243 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_WriteNextByteDMA | 263 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_StepGoState | 298 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_Reset | 354 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ncr_read_register | 414 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok -1 = not an NCR register | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| scsi_status_word | 446 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| SCSI_Read | 496 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_write_control | 578 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| SCSI_Write | 653 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_Tick | 725 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_Ident | 751 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_Boot | 787 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_destroy | 886 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateSCSIDevice | 898 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/scsi/disk_scsi.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DiskSCSI_LastLBA | 40 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DiskSCSI_SetField | 55 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DiskSCSI_SetDiskType | 69 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/scsi/scsi_bus.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SCSIBus_PhaseName | 27 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_Init | 33 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_AddDevice | 43 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_Clock | 67 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_RegenData | 86 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_RegenCtrl | 102 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
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
| SCSITarget_Log | 72 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_AdjustTimer | 91 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_BufControlPush | 102 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_BufControlPop | 122 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DefaultGetData | 143 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DefaultPutData | 166 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_GetData | 189 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_PutData | 199 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DataIn | 215 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DataOut | 226 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_StatusComplete | 236 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_SetSenseData | 256 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_Sense | 293 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_ReportBadCmd | 312 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_ReportBadLun | 319 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_RecvByte | 329 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_SendByte | 338 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_SendBufferByte | 348 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_CommandDone | 358 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_Message | 393 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_Step | 408 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_Clock | 646 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_CtrlChanged | 664 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DeviceReset | 674 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_Init | 701 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/scsi/scsi_hdd.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SCSIHDD_Log | 27 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_HasMedia | 48 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_GetLun | 67 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_ReadBlock | 84 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_WriteBlock | 110 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_GetData | 135 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_PutData | 183 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_CommandReadCapacity | 248 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_CommandInquiry | 272 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_CommandTestUnitReady | 322 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_CommandModeSense | 350 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_Command | 537 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsihdd_device_reset | 789 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_Init | 807 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/smd/device_smd.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SMD_OpName | 66 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_RegReadName | 95 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_RegWriteName | 112 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_ErrorName | 129 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Reset | 156 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Read | 177 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Write | 428 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HandleError | 467 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Tick | 801 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Ident | 813 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Boot | 835 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ExecuteGO | 938 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ConvertCHStoLBA | 1045 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FinishOperation | 1114 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMDReadEnd | 1471 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMDTimeoutEnd | 1539 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| LoadIsIllegal | 1587 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| UnitAttached | 1612 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ClearFlipFlops | 1671 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ClearErrors | 1700 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SetSelectedUnit | 1717 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| IncrementCoreAddress | 1805 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DecrementWordCounter | 1818 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateSMDDevice | 1831 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| smd_destroy | 2007 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/smd/disk_smd.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DiskSMD_SetDiskType | 37 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/terminal/device_terminal.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Terminal_Reset | 114 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Terminal_Tick | 134 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Terminal_Read | 210 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| terminal_write_input_control | 265 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| Terminal_Write | 297 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Terminal_Ident | 388 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| WriteEnd | 412 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Terminal_QueueKeyCode | 438 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Terminal_InputFunction | 465 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateTerminalDevice | 475 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/winchester/device_winchester.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Wd_OpName | 65 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_ClearFlipFlops | 97 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| WdUnitAttached | 108 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_FinishOperation | 137 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_ClearErrors | 155 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_DeviceClear | 167 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_ReadStatus | 184 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_Read | 221 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_Write | 281 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| WdTransferEnd | 433 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_ExecuteGO | 456 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_Tick | 681 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_Ident | 687 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_Reset | 717 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_Boot | 761 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_Destroy | 848 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateWinchesterDevice | 866 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/winchester/disk_winchester.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DiskWinchester_SetDiskType | 40 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DiskWinchester_ChsToLba | 109 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
