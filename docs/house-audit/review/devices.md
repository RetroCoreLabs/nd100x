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
| Device_GetOddParity | 52 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Init | 58 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Destroy | 107 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Reset | 133 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Tick | 142 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Boot | 153 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_IsInAddress | 167 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_RegisterAddress | 176 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Read | 185 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Write | 194 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Ident | 203 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_QueueIODelay | 212 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_TickIODelay | 243 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_ClearInterrupt | 271 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_GenerateInterrupt | 287 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_SetInterruptStatus | 303 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_IO_Seek | 320 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_IO_ReadWord | 331 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_IO_BufferReadWord | 356 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_IO_WriteWord | 384 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_IO_BufferWriteWord | 407 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_DMAWrite | 429 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_DMARead | 436 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_SetCharacterOutput | 447 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_SetCharacterInput | 458 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_OutputCharacter | 469 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_InputCharacter | 480 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_SetBlockRead | 493 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_SetBlockWrite | 505 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_SetBlockDiskInfo | 520 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_ReadBlock | 534 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_WriteBlock | 546 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

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
| FloppyPIO_Reset | 68 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_Tick | 83 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_Read | 93 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_Write | 179 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_Ident | 330 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_ReadEnd | 347 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_RecalibrateEnd | 371 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_SeekEnd | 387 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_ClearAllErrorFlags | 403 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SetSectorAsDeleted | 415 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SectorIsDeleted | 427 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_ExecuteGo | 440 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateFloppyPIODevice | 783 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/chip_com5025.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| COM5025_Init | 50 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_Reset | 68 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_MasterReset | 94 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ReadByte | 109 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetOutputPin | 122 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetReceiverStatus | 128 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_WriteByte | 164 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ReadWord | 224 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_WriteWord | 262 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetInputPin | 308 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_GetInputPin | 364 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_GetOutputPin | 373 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ClockReceiver | 382 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ClockTransmitter | 393 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_MoveTDBtoTSR | 408 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ProcessBit | 474 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ReceiveData | 525 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_TransmitData | 539 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ClearAllInputPins | 572 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetTransmitterBufferEmpty | 584 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ClearTransmitterBufferEmpty | 594 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_WriteTransmitterDataBuffer | 604 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_WriteDataToTSR | 632 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_TransmitByteOutput | 649 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SendOneByte | 686 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_TSR_Empty | 706 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_MapBits2CharLen | 715 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetTransmitterOutputCallback | 751 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetPinValueChangedCallback | 763 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/chip_com5025_registers.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| COM5025Registers_Init | 35 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_Clear | 49 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_Destroy | 96 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_SetReceiverStatus | 112 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_GetReceiverCharacterLen | 121 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_GetTransmitterCharacterLen | 130 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_SetModeControl | 139 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_IsProtocolModeCCP | 161 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_SetClockSpeed | 170 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_Clock | 180 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_AdjustTimer | 190 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_QueueReceivedData | 209 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_DataReceived | 265 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_IsNextByteSync | 285 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_MarkDataAsReceived | 320 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_CalcCRC | 337 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_CalcRXCrc | 373 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_ClearRXCRC | 382 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_IsRxCrcEqual | 399 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_AggregateTXCrc | 408 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_ClearTXCRC | 417 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_CalcFinalTxCrc | 434 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_IsTxCrcEqual | 444 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025IOTimer_Init | 454 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025IOTimer_Clear | 463 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025IOTimer_SetCallback | 474 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025IOTimer_SetClockSpeed | 485 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025IOTimer_AdjustTimer | 494 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025IOTimer_Clock | 505 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

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
| DMAEngine_CBReadDMA | 54 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CBWriteDMA | 62 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_TXSendFrame | 69 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_TXInterrupt | 79 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_RXInterrupt | 86 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_Init | 92 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_Destroy | 161 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_Clear | 190 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_Tick | 214 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_DMARead | 236 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_DMAWrite | 248 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_OnSetInterruptBit | 260 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_OnWriteDMA | 270 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_OnReadDMA | 280 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_ExecuteCommand | 296 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandDeviceClear | 322 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandInitialize | 346 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_Log | 413 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandReceiverStart | 439 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandReceiverContinue | 465 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandTransmitterStart | 491 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandDumpDataModule | 518 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandDumpRegisters | 582 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandLoadRegisters | 649 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetDMAAddress | 706 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_ClearDMACommand | 715 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_GetBufferKeyVault | 729 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_ScanNextTXBuffer | 741 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetWriteDMACallback | 777 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetReadDMACallback | 786 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetInterruptCallback | 795 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetSendFrameCallback | 804 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetUpdateReceiverStatusCallback | 813 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetClearCommandCallback | 823 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

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
| DMAReceiver_Init | 41 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_Destroy | 66 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_Clear | 77 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_Tick | 93 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| StopReceiver | 137 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_SetReceiverState | 154 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_ReceiveDataFromModem | 195 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_ProcessBufferedData | 228 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_ProcessCompleteFrame | 311 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_SetRXDMAFlag | 389 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_ClearReceiveFrameState | 404 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_SetInterruptCallback | 414 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_FindNextReceiveBuffer | 427 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_ReceiveDataBufferByte | 458 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_EnableHDLCReceiver | 563 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/dma_transmitter.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DMATransmitter_Init | 44 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_Destroy | 63 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_Clear | 73 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_Tick | 87 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_SetTXDMAFlag | 143 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| record_tx_history | 186 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | ok copyLen bounded by HDLC_TX_HISTORY_DATA_SIZE | ok | n/a emulation thread only | n/a | ok | n/a |
| send_outbound_frame | 208 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | ok frameLength <= sizeof(frameBuffer) by HDLCFrame_BuildFrame | ok | n/a emulation thread only | n/a | ok | n/a |
| DMATransmitter_SendAllBuffers | 233 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_SetEngineSenderState | 336 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_SetSenderState | 365 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_SetSendFrameCallback | 376 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_SetInterruptCallback | 386 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/hdlc_crc.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| initializeParityTables | 65 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CRC_CalculateCRC16Buffer | 80 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CRC_CalcCrc16 | 94 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CRC_CalcCCITT | 103 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CRC_CalculateParityBit | 110 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CRC_AddParityBit | 137 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CRC_CheckParity | 145 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

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
| setup_pap | 44 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ProcessMessageControl | 59 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ProcessTerminalPanc | 93 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ProcessTerminalLamp | 238 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| UpdateMachineTime | 249 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

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
| RTC_Reset | 76 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| RTC_ClearClockTicks | 94 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| RTC_Tick | 114 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| RTC_Read | 183 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| RTC_Write | 218 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| RTC_Ident | 280 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateRTCDevice | 310 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/scsi/device_scsi.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SCSI_Log | 81 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_GetMAR | 98 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_IncrementMAR | 103 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_ParseUnitType | 111 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_UnitTypeName | 139 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_SetUnitType | 158 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_OnNCRInterrupt | 197 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_OnNCRDataRequest | 213 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_ReadNextByteDMA | 242 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_WriteNextByteDMA | 262 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_StepGoState | 297 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_Reset | 353 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ncr_read_register | 413 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok -1 = not an NCR register | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| scsi_status_word | 445 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| SCSI_Read | 495 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_write_control | 577 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| SCSI_Write | 652 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_Tick | 724 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_Ident | 750 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_Boot | 786 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_Destroy | 885 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateSCSIDevice | 897 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

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
| scsi_put_u16be | 32 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_put_u24be | 38 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_put_u32be | 45 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_get_u16be | 53 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_get_u24be | 58 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_get_u32be | 63 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_Log | 69 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_AdjustTimer | 88 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_BufControlPush | 99 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_BufControlPop | 119 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DefaultGetData | 140 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DefaultPutData | 163 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_GetData | 186 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_PutData | 196 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DataIn | 212 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DataOut | 223 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_StatusComplete | 233 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_SetSenseData | 253 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_Sense | 290 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_ReportCondition | 301 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_ReportBadCmd | 309 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_ReportBadLun | 316 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_RecvByte | 326 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_SendByte | 335 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_SendBufferByte | 345 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_CommandDone | 355 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_Message | 390 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_Step | 405 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_Clock | 643 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_CtrlChanged | 661 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DeviceReset | 671 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_Init | 698 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

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
| SCSIHDD_DeviceReset | 789 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_Init | 807 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/smd/device_smd.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SMD_OpName | 64 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_RegReadName | 93 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_RegWriteName | 110 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_ErrorName | 127 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Reset | 154 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Read | 175 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Write | 426 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HandleError | 465 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Tick | 799 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Ident | 811 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Boot | 833 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ExecuteGO | 936 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ConvertCHStoLBA | 1043 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FinishOperation | 1112 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMDReadEnd | 1469 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMDTimeoutEnd | 1537 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| LoadIsIllegal | 1585 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| UnitAttached | 1610 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ClearFlipFlops | 1669 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ClearErrors | 1698 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SetSelectedUnit | 1715 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| IncrementCoreAddress | 1803 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DecrementWordCounter | 1816 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateSMDDevice | 1829 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Destroy | 2005 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

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
