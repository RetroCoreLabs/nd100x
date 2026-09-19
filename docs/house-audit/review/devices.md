# Review: devices

One row per function. A cell is empty until reviewed; write `ok`, or
`n/a`, or `fixed <commit>`. Rules: see the plan, section 3 (REVIEW).

## src/devices/cdc/device_cdc.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CdcDevice_SetBackingFile | 44 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cdc_lba_to_sector | 64 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| cdc_effective_core | 76 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_EnsureSurface | 86 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_AttachBacking | 109 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_Read | 164 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_Write | 218 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_ExecuteGO | 289 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_End | 428 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_Tick | 475 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_Ident | 485 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_Reset | 511 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_Destroy | 533 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_Boot | 570 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Cdc_IotOp | 636 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateCdcDevice | 729 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/device.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Device_GetOddParity | 48 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Init | 54 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Destroy | 103 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Reset | 129 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Tick | 138 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Boot | 149 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_IsInAddress | 163 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_RegisterAddress | 172 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Read | 181 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Write | 190 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_Ident | 199 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_QueueIODelay | 208 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_TickIODelay | 239 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_ClearInterrupt | 267 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_GenerateInterrupt | 283 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_SetInterruptStatus | 299 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_IO_Seek | 316 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_IO_ReadWord | 327 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_IO_BufferReadWord | 352 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_IO_WriteWord | 380 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_IO_BufferWriteWord | 403 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_DMAWrite | 425 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_DMARead | 432 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_SetCharacterOutput | 443 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_SetCharacterInput | 454 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_OutputCharacter | 465 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_InputCharacter | 476 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_SetBlockRead | 489 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_SetBlockWrite | 501 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_SetBlockDiskInfo | 516 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_ReadBlock | 530 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Device_WriteBlock | 542 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/devicemanager.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DeviceManager_Init | 47 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_Destroy | 66 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_AddAllDevices | 89 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_AddSCSIDevice_WithConfig | 142 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_AddHDLCDevice_WithConfig | 165 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateDevice | 192 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_MasterClear | 320 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_AddDevice | 331 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_Read | 391 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_Write | 409 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_Ident | 431 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_Tick | 462 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_GetDeviceByAddress | 476 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_GetDeviceCount | 490 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_GetDeviceByIndex | 495 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_IotOp | 516 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DeviceManager_BootFrom | 535 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/drum/device_drum.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DrumDevice_SetBackingFile | 35 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Drum_AttachBacking | 51 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Drum_Read | 89 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Drum_Write | 110 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Drum_ExecuteGO | 154 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Drum_End | 234 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Drum_Tick | 255 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Drum_Ident | 265 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Drum_Reset | 279 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Drum_Destroy | 295 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateDrumDevice | 323 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/floppy/device_floppy_dma.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| FloppyDMA_Reset | 78 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CalculateOrOfErrors | 103 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CalculateHardwareStatusWord | 111 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CalculateStatusWord1 | 129 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyDMA_Read | 144 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyDMA_Write | 182 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyDMA_Tick | 252 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyDMA_Ident | 263 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DmaAutoloadErrorImage | 319 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ExecuteAutoload | 353 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ExecuteTest | 385 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ExecuteFloppyGo | 397 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ReadEnd | 822 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| AutoLoadEnd | 843 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateFloppyDMADevice | 860 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/floppy/device_floppy_pio.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| FloppyPIO_Reset | 62 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_Tick | 77 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_Read | 87 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_Write | 173 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_Ident | 324 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_ReadEnd | 341 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_RecalibrateEnd | 365 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_SeekEnd | 381 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_ClearAllErrorFlags | 397 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SetSectorAsDeleted | 409 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SectorIsDeleted | 421 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FloppyPIO_ExecuteGo | 434 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateFloppyPIODevice | 777 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/chip_com5025.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| COM5025_Init | 44 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_Reset | 62 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_MasterReset | 88 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ReadByte | 103 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetOutputPin | 116 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetReceiverStatus | 122 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_WriteByte | 158 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ReadWord | 218 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_WriteWord | 256 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetInputPin | 302 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_GetInputPin | 358 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_GetOutputPin | 367 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ClockReceiver | 376 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ClockTransmitter | 387 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_MoveTDBtoTSR | 402 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ProcessBit | 468 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ReceiveData | 519 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_TransmitData | 533 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ClearAllInputPins | 566 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetTransmitterBufferEmpty | 578 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_ClearTransmitterBufferEmpty | 588 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_WriteTransmitterDataBuffer | 598 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_WriteDataToTSR | 626 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_TransmitByteOutput | 643 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SendOneByte | 680 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_TSR_Empty | 700 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_MapBits2CharLen | 709 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetTransmitterOutputCallback | 745 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025_SetPinValueChangedCallback | 757 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/chip_com5025_registers.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| COM5025Registers_Init | 29 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_Clear | 43 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_Destroy | 90 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_SetReceiverStatus | 106 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_GetReceiverCharacterLen | 115 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_GetTransmitterCharacterLen | 124 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_SetModeControl | 133 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_IsProtocolModeCCP | 155 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_SetClockSpeed | 164 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_Clock | 174 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_AdjustTimer | 184 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_QueueReceivedData | 203 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_DataReceived | 259 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_IsNextByteSync | 279 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_MarkDataAsReceived | 314 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_CalcCRC | 331 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_CalcRXCrc | 367 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_ClearRXCRC | 376 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_IsRxCrcEqual | 393 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_AggregateTXCrc | 402 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_ClearTXCRC | 411 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_CalcFinalTxCrc | 428 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025Registers_IsTxCrcEqual | 438 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025IOTimer_Init | 448 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025IOTimer_Clear | 457 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025IOTimer_SetCallback | 468 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025IOTimer_SetClockSpeed | 479 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025IOTimer_AdjustTimer | 488 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| COM5025IOTimer_Clock | 499 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/device_hdlc.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| hdlc_log_bits | 69 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_Reset | 172 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_Tick | 237 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_Read | 285 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_Write | 383 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_Ident | 609 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_Destroy | 643 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateHDLCDevice | 680 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_BridgeInjectRx | 814 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnModemReceivedData | 838 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnModemRingIndicator | 862 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnModemDataSetReady | 886 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnModemSignalDetector | 910 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnModemClearToSend | 934 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnModemRequestToSend | 959 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnModemDataTerminalReady | 977 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnDMAWriteDMA | 997 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnDMAReadDMA | 1006 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnDMASetInterruptBit | 1017 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnDMASendHDLCFrame | 1042 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnDMAUpdateReceiverStatus | 1063 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnDMAClearCommand | 1081 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnCOM5025TransmitterOutput | 1100 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_OnCOM5025PinValueChanged | 1117 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CheckTriggerIRQ12 | 1194 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CheckTriggerIRQ13 | 1227 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CheckTriggerInterrupt | 1268 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_UpdateRQTS | 1325 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_GetRxFrameStatus | 1375 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/dma_control_blocks.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DMAControlBlocks_Log | 44 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_Init | 47 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_Destroy | 99 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_Clear | 135 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_SetTXPointer | 168 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_DebugTXFrames | 182 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_LoadTXBuffer | 210 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_LoadNextTXBuffer | 226 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_MarkBufferSent | 239 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_SetRXPointer | 266 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_LoadRXBuffer | 279 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_LoadNextRXBuffer | 295 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_IsNextRXbufValid | 348 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_MarkBufferReceived | 367 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_DMARead | 421 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| read_dcb_words | 465 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| pick_displacement | 487 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| DMAControlBlocks_LoadBufferDescription | 498 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_ReadNextByteDMA | 598 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_WriteNextByteDMA | 665 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_SetReadDMACallback | 760 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_SetWriteDMACallback | 771 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_SetSendHDLCFrameCallback | 782 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_SetInterruptCallback | 794 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAControlBlocks_DMAWrite | 808 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/dma_dcb.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DCB_Init | 32 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_Clear | 58 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetKey | 68 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_HasRSOMFlag | 78 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_HasREOMFlag | 88 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetDataFlowCost | 98 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetDataMemoryAddress | 108 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetDataMemoryAddress | 119 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetBufferAddress | 130 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetBufferAddress | 140 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetOffsetFromLP | 150 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetOffsetFromLP | 160 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetKeyValue | 170 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetKeyValue | 180 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetByteCount | 190 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetByteCount | 200 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetDisplacement | 210 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetDisplacement | 220 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetListPointer | 230 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetListPointer | 240 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetDMAAddress | 252 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetDMAAddress | 262 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetDMABytesRead | 272 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetDMABytesRead | 282 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetDMABytesWritten | 292 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetDMABytesWritten | 302 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_SetDMAReadData | 312 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_GetDMAReadData | 322 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_IsDMAReadDataValid | 332 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DCB_ClearDMAReadData | 342 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/dma_engine.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DMAEngine_CBReadDMA | 49 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CBWriteDMA | 57 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_TXSendFrame | 64 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_TXInterrupt | 74 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_RXInterrupt | 81 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_Init | 87 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_Destroy | 156 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_Clear | 185 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_Tick | 209 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_DMARead | 231 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_DMAWrite | 243 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_OnSetInterruptBit | 255 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_OnWriteDMA | 265 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_OnReadDMA | 275 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_ExecuteCommand | 291 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandDeviceClear | 317 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandInitialize | 341 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_Log | 408 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandReceiverStart | 434 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandReceiverContinue | 460 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandTransmitterStart | 486 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandDumpDataModule | 513 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandDumpRegisters | 577 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_CommandLoadRegisters | 644 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetDMAAddress | 701 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_ClearDMACommand | 710 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_GetBufferKeyVault | 724 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_ScanNextTXBuffer | 736 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetWriteDMACallback | 772 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetReadDMACallback | 781 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetInterruptCallback | 790 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetSendFrameCallback | 799 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetUpdateReceiverStatusCallback | 808 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAEngine_SetClearCommandCallback | 818 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/dma_param_buf.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ParameterBuffer_Init | 31 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_Clear | 41 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_GetParameterControlRegister | 61 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_GetSyncAddressRegister | 70 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_GetCharacterLength | 79 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_GetDisplacement1 | 88 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_GetDisplacement2 | 97 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_GetMaxReceiverBlockLength | 106 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_GetReceiverStatusReg | 115 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_GetTransmitterStatusReg | 124 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_GetDmaBankBits | 133 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_SetParameterControlRegister | 144 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_SetSyncAddressRegister | 153 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_SetCharacterLength | 162 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_SetDisplacement1 | 171 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_SetDisplacement2 | 180 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_SetMaxReceiverBlockLength | 189 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_SetReceiverStatusReg | 198 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_SetTransmitterStatusReg | 207 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ParameterBuffer_SetDmaBankBits | 216 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/dma_receiver.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DMAReceiver_Init | 36 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_Destroy | 61 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_Clear | 72 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_Tick | 88 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| StopReceiver | 132 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_SetReceiverState | 149 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_ReceiveDataFromModem | 190 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_ProcessBufferedData | 223 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_ProcessCompleteFrame | 306 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_SetRXDMAFlag | 384 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_ClearReceiveFrameState | 399 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_SetInterruptCallback | 409 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_FindNextReceiveBuffer | 422 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_ReceiveDataBufferByte | 453 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMAReceiver_EnableHDLCReceiver | 558 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/dma_transmitter.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DMATransmitter_Init | 39 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_Destroy | 58 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_Clear | 68 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_Tick | 82 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_SetTXDMAFlag | 138 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| record_tx_history | 181 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | ok copyLen bounded by HDLC_TX_HISTORY_DATA_SIZE | ok | n/a emulation thread only | n/a | ok | n/a |
| send_outbound_frame | 203 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | ok frameLength <= sizeof(frameBuffer) by HDLCFrame_BuildFrame | ok | n/a emulation thread only | n/a | ok | n/a |
| DMATransmitter_SendAllBuffers | 228 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_SetEngineSenderState | 331 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_SetSenderState | 360 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_SetSendFrameCallback | 371 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DMATransmitter_SetInterruptCallback | 381 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/hdlc_crc.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| initializeParityTables | 61 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CRC_CalculateCRC16Buffer | 76 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CRC_CalcCrc16 | 90 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CRC_CalcCCITT | 99 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CRC_CalculateParityBit | 106 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CRC_AddParityBit | 133 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLC_CRC_CheckParity | 141 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/hdlc_frame.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| HDLCFrame_Init | 38 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_Reset | 47 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_UpdateCRC | 62 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_CalculateCRC | 67 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_AddBytes | 82 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_AddByte | 97 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_IsFrameComplete | 195 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_IsCRCValid | 200 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_GetFrameLength | 205 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_GetFrameData | 210 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_StuffByte | 215 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_DestuffByte | 239 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HDLCFrame_BuildFrame | 245 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/modem.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| sleep_ms | 41 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd_set_nonblocking | 56 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| nd_connect_in_progress | 81 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| queue_init | 97 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| queue_destroy | 104 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| queue_write | 110 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| queue_write_all | 137 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| queue_read | 163 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| queue_has_data | 183 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| set_nodelay | 196 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| resolve_address | 208 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| try_connect | 232 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| sleep_check_shutdown | 294 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| server_worker | 308 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| service_connection | 446 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | ok modem worker thread; queues are the locked hand-off, atomics for flags | n/a | ok | n/a |
| client_worker | 504 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_Init | 575 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_Destroy | 596 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_StartModem | 620 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_Tick | 682 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SendByte | 705 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SendBytes | 726 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetDTR | 746 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetRTS | 760 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetDSR | 774 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetCTS | 787 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetReceivedDataCallback | 804 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetRingIndicatorCallback | 811 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetDataSetReadyCallback | 818 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetSignalDetectorCallback | 825 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetClearToSendCallback | 832 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetRequestToSendCallback | 839 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Modem_SetDataTerminalReadyCallback | 846 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/hdlc/tcp_receive_buffer.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| TcpReceiveBuffer_Init | 28 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| TcpReceiveBuffer_Destroy | 46 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| TcpReceiveBuffer_Enqueue | 63 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| TcpReceiveBuffer_DequeueByte | 100 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| TcpReceiveBuffer_Available | 117 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| TcpReceiveBuffer_Clear | 126 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/lineprinter/device_line_printer.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| LinePrinter_Reset | 39 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| LinePrinter_Tick | 55 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| LinePrinter_Read | 64 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| LinePrinter_Write | 92 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| LinePrinter_Ident | 164 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateLinePrinterDevice | 181 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/panel/panel.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| setup_pap | 43 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ProcessMessageControl | 58 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ProcessTerminalPanc | 92 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ProcessTerminalLamp | 237 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| UpdateMachineTime | 248 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/papertape/device_paper_tape.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| PaperTape_Reset | 39 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTape_Tick | 55 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTape_Read | 65 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTape_Write | 93 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTape_Ident | 187 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTape_Destroy | 204 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTape_LoadTape | 219 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreatePaperTapeDevice | 256 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/papertapewriter/device_paper_tape_writer.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| PaperTapeWriter_Reset | 41 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTapeWriter_Tick | 55 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTapeWriter_Read | 65 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTapeWriter_GrowBuffer | 97 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTapeWriter_Write | 127 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTapeWriter_Ident | 216 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PunchEnd | 233 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTapeWriter_Destroy | 252 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| PaperTapeWriter_GetTapeData | 267 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreatePaperTapeWriterDevice | 285 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/rtc/device_rtc.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| RTC_SetWallClockMode | 49 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| rtc_now_ns | 54 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| RTC_Reset | 72 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| RTC_ClearClockTicks | 90 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| RTC_Tick | 110 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| RTC_Read | 179 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| RTC_Write | 214 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| RTC_Ident | 276 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateRTCDevice | 306 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/scsi/device_scsi.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SCSI_Log | 78 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_GetMAR | 95 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_IncrementMAR | 100 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_ParseUnitType | 108 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_UnitTypeName | 136 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_SetUnitType | 155 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_OnNCRInterrupt | 194 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_OnNCRDataRequest | 210 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_ReadNextByteDMA | 239 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_WriteNextByteDMA | 259 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_StepGoState | 294 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_Reset | 350 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ncr_read_register | 410 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok -1 = not an NCR register | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| scsi_status_word | 442 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| SCSI_Read | 492 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_write_control | 574 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| SCSI_Write | 649 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_Tick | 721 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_Ident | 747 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_Boot | 783 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSI_Destroy | 882 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateSCSIDevice | 894 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/scsi/disk_scsi.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DiskSCSI_LastLBA | 37 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DiskSCSI_SetField | 52 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DiskSCSI_SetDiskType | 66 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/scsi/scsi_bus.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SCSIBus_PhaseName | 24 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_Init | 30 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_AddDevice | 40 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_Clock | 64 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_RegenData | 83 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_RegenCtrl | 99 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_ControlRead | 136 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_ControlWait | 147 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_ControlWrite | 163 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_DataRead | 182 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIBus_DataWrite | 192 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/scsi/scsi_device.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| scsi_put_u16be | 29 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_put_u24be | 35 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_put_u32be | 42 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_get_u16be | 50 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_get_u24be | 55 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| scsi_get_u32be | 60 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_Log | 66 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_AdjustTimer | 85 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_BufControlPush | 96 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_BufControlPop | 116 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DefaultGetData | 137 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DefaultPutData | 160 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_GetData | 183 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_PutData | 193 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DataIn | 209 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DataOut | 220 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_StatusComplete | 230 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_SetSenseData | 250 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_Sense | 287 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_ReportCondition | 298 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_ReportBadCmd | 306 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_ReportBadLun | 313 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_RecvByte | 323 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_SendByte | 332 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_SendBufferByte | 342 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_CommandDone | 352 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_Message | 387 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_Step | 402 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_Clock | 640 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_CtrlChanged | 658 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_DeviceReset | 668 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSITarget_Init | 695 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/scsi/scsi_hdd.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SCSIHDD_Log | 24 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_HasMedia | 45 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_GetLun | 64 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_ReadBlock | 81 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_WriteBlock | 107 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_GetData | 132 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_PutData | 180 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_CommandReadCapacity | 245 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_CommandInquiry | 269 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_CommandTestUnitReady | 319 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_CommandModeSense | 347 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_Command | 534 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_DeviceReset | 786 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SCSIHDD_Init | 804 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/smd/device_smd.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SMD_OpName | 59 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_RegReadName | 88 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_RegWriteName | 105 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_ErrorName | 122 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Reset | 149 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Read | 170 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Write | 421 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| HandleError | 460 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Tick | 794 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Ident | 806 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Boot | 828 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ExecuteGO | 931 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ConvertCHStoLBA | 1038 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| FinishOperation | 1107 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMDReadEnd | 1464 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMDTimeoutEnd | 1532 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| LoadIsIllegal | 1580 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| UnitAttached | 1605 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ClearFlipFlops | 1664 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| ClearErrors | 1693 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SetSelectedUnit | 1710 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| IncrementCoreAddress | 1798 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DecrementWordCounter | 1811 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateSMDDevice | 1824 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| SMD_Destroy | 2000 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/smd/disk_smd.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DiskSMD_SetDiskType | 33 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/terminal/device_terminal.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Terminal_Reset | 110 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Terminal_Tick | 130 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Terminal_Read | 206 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| terminal_write_input_control | 261 | n/a | n/a | ok | ok | n/a | n/a | n/a | ok | n/a | ok | n/a static | ok | n/a | n/a | n/a | ok | n/a emulation thread only | n/a | ok | n/a |
| Terminal_Write | 293 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Terminal_Ident | 384 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| WriteEnd | 408 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Terminal_QueueKeyCode | 434 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Terminal_InputFunction | 461 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateTerminalDevice | 471 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/winchester/device_winchester.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Wd_OpName | 61 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_ClearFlipFlops | 93 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| WdUnitAttached | 104 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_FinishOperation | 133 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_ClearErrors | 151 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_DeviceClear | 163 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_ReadStatus | 180 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_Read | 217 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_Write | 277 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| WdTransferEnd | 429 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_ExecuteGO | 452 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_Tick | 677 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_Ident | 683 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_Reset | 713 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_Boot | 757 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| Wd_Destroy | 844 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| CreateWinchesterDevice | 862 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

## src/devices/winchester/disk_winchester.c

| Function | Line | 2.8 | 3.10 | 5.1 | 5.4 | 5.7 | 5.8 | 5.10 | 6.3 | 6.8 | 7.2 | 7.5 | 7.6 | 8.1 | 8.2 | 8.5 | 9.5 | 10.1 | 10.2 | 11.3 | 12.3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DiskWinchester_SetDiskType | 36 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| DiskWinchester_ChsToLba | 105 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
