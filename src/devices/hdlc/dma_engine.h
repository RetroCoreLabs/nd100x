/*
 * nd100x - ND100 Virtual Machine
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * This file is originated from the nd100x project and the RetroCore project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the nd100em
 * distribution in the file COPYING); if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DMA_ENGINE_H
#define DMA_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "dma_param_buf.h"

// Forward declarations
struct Device;
struct COM5025State;
struct ModemState;

// Forward declarations for types that are defined in other headers
struct DMAControlBlocks;
struct DMATransmitter;
struct DMAReceiver;
struct HDLCFrame;

// DMA engine callback function types
typedef void (*DMAWriteCallback)(struct Device *device, uint32_t address, uint16_t data);
typedef void (*DMAReadCallback)(struct Device *device, uint32_t address, int *data);
typedef void (*DMASetInterruptCallback)(struct Device *device, uint8_t bit);
typedef void (*DMASendFrameCallback)(struct Device *device, struct HDLCFrame *frame);
typedef void (*DMAUpdateReceiverStatusCallback)(struct Device *device, uint16_t status);
typedef void (*DMAClearCommandCallback)(struct Device *device);

// DMA Engine state enums are defined in dma_enum.h

// DMA Engine state structure
typedef struct DMAEngine {
    // DMA Control Blocks - main coordination structure
    struct DMAControlBlocks *dmaCB;

    // DMA components
    struct DMATransmitter *transmitter;
    struct DMAReceiver *receiver;

    // Hardware references
    struct COM5025State *com5025;
    struct ModemState *modem;
    struct Device *hdlcDevice;

    // State management
    bool enabled;

    // DMA register array (256 16-bit registers)
    uint16_t dmaRegisters[256];

    // Current DMA address for operations
    uint32_t currentDMAAddress;

    // Parameter buffer for DMA operations
    ParameterBuffer parameterBuffer;

    // Callbacks to HDLC device
    DMAWriteCallback onWriteDMA;
    DMAReadCallback onReadDMA;
    DMASetInterruptCallback onSetInterruptBit;
    DMASendFrameCallback onSendHDLCFrame;
    DMAUpdateReceiverStatusCallback onUpdateReceiverStatus;
    DMAClearCommandCallback onClearCommand;

} DMAEngine;

// Core DMA Engine functions
void DMAEngine_Init(DMAEngine *dma, bool burstMode, struct Device *hdlcDevice, void *modem, void *com5025);
void DMAEngine_Destroy(DMAEngine *dma);
void DMAEngine_Clear(DMAEngine *dma);
void DMAEngine_Tick(DMAEngine *dma);

// DMA Command execution
void DMAEngine_ExecuteCommand(DMAEngine *dma);

// DMA Command implementations (8 commands total)
void DMAEngine_CommandDeviceClear(DMAEngine *dma);
void DMAEngine_CommandInitialize(DMAEngine *dma);
void DMAEngine_CommandReceiverStart(DMAEngine *dma);
void DMAEngine_CommandReceiverContinue(DMAEngine *dma);
void DMAEngine_CommandTransmitterStart(DMAEngine *dma);
void DMAEngine_CommandDumpDataModule(DMAEngine *dma);
void DMAEngine_CommandDumpRegisters(DMAEngine *dma);
void DMAEngine_CommandLoadRegisters(DMAEngine *dma);

// Memory access functions
int DMAEngine_DMARead(DMAEngine *dma, uint32_t address);
void DMAEngine_DMAWrite(DMAEngine *dma, uint32_t address, uint16_t data);

// Utility functions
void DMAEngine_ClearDMACommand(DMAEngine *dma);
void DMAEngine_SetDMAAddress(DMAEngine *dma, uint32_t address);
uint16_t DMAEngine_GetBufferKeyVault(DMAEngine *dma, uint32_t listPointer, uint16_t offset);
uint32_t DMAEngine_ScanNextTXBuffer(DMAEngine *dma, uint32_t start);

// Event handling functions
void DMAEngine_OnSetInterruptBit(DMAEngine *dma, uint8_t bit);
void DMAEngine_OnWriteDMA(DMAEngine *dma, uint32_t address, uint16_t data);
void DMAEngine_OnReadDMA(DMAEngine *dma, uint32_t address, int *data);

// Callback setup functions
void DMAEngine_SetWriteDMACallback(DMAEngine *dma, DMAWriteCallback callback);
void DMAEngine_SetReadDMACallback(DMAEngine *dma, DMAReadCallback callback);
void DMAEngine_SetInterruptCallback(DMAEngine *dma, DMASetInterruptCallback callback);
void DMAEngine_SetSendFrameCallback(DMAEngine *dma, DMASendFrameCallback callback);
void DMAEngine_SetUpdateReceiverStatusCallback(DMAEngine *dma, DMAUpdateReceiverStatusCallback callback);
void DMAEngine_SetClearCommandCallback(DMAEngine *dma, DMAClearCommandCallback callback);

// Debug functions
void DMAEngine_Log(DMAEngine *dma, const char *format, ...) __attribute__((format(printf, 2, 3)));

#endif // DMA_ENGINE_H