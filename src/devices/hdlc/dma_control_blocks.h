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

#ifndef DMA_CONTROL_BLOCKS_H
#define DMA_CONTROL_BLOCKS_H

#include <stdint.h>
#include <stdbool.h>
#include "dma_dcb.h"
#include "dma_enum.h"

// Forward declarations
struct Device;

// Include HDLCFrame definition
#include "hdlc_frame.h"

// Callback function types (match those used in actual implementation)
typedef uint16_t (*DMAControlBlocks_ReadCallback)(void *context, uint32_t address);
typedef void (*DMAControlBlocks_WriteCallback)(void *context, uint32_t address, uint16_t data);
typedef void (*DMAControlBlocks_SendFrameCallback)(void *context, void *frame);
typedef void (*DMAControlBlocks_InterruptCallback)(void *context, uint8_t bit);

#include "dma_param_buf.h"

// DMA Control Blocks structure
typedef struct DMAControlBlocks {
    // Buffer pointers
    uint8_t *outboundBuffer;
    int outboundBufferSize;
    int outboundBufferCapacity;

    // DCB pointers
    HdlcDCB *txDCB;
    HdlcDCB *rxDCB;

    // List pointer management
    uint32_t txListPointer;
    int txListPointerOffset;
    uint32_t rxListPointer;
    int rxListPointerOffset;

    // HDLC frame for receiving
    HDLCFrame *hdlcReceiveFrame;

    // Parameter buffer reference (owned by DMAEngine, pointed to here)
    ParameterBuffer *parameters;

    // DMA state machine
    int dmaSenderState;
    int dmaSendBlockState;
    int dmaWaitTicks;

    // Callbacks
    DMAControlBlocks_ReadCallback onReadDMA;
    DMAControlBlocks_WriteCallback onWriteDMA;
    DMAControlBlocks_SendFrameCallback onSendHDLCFrame;
    DMAControlBlocks_InterruptCallback onSetInterruptBit;
    void *callbackContext;

    // Associated device
    struct Device *hdlcDevice;

} DMAControlBlocks;

// Function prototypes (match the actual C implementation)
void DMAControlBlocks_Init(DMAControlBlocks *dcbs, struct Device *hdlcDevice);
void DMAControlBlocks_Destroy(DMAControlBlocks *dmaCB);
void DMAControlBlocks_Clear(DMAControlBlocks *dmaCB);

void DMAControlBlocks_SetTXPointer(DMAControlBlocks *dmaCB, uint32_t listPointer, int offset);
void DMAControlBlocks_DebugTXFrames(DMAControlBlocks *dmaCB);
void DMAControlBlocks_LoadTXBuffer(DMAControlBlocks *dmaCB);
bool DMAControlBlocks_LoadNextTXBuffer(DMAControlBlocks *dmaCB);
void DMAControlBlocks_MarkBufferSent(DMAControlBlocks *dmaCB);
void DMAControlBlocks_SetRXPointer(DMAControlBlocks *dmaCB, uint32_t listPointer, int offset);
void DMAControlBlocks_LoadRXBuffer(DMAControlBlocks *dmaCB);
bool DMAControlBlocks_LoadNextRXBuffer(DMAControlBlocks *dmaCB);
bool DMAControlBlocks_IsNextRXbufValid(DMAControlBlocks *dmaCB);
void DMAControlBlocks_MarkBufferReceived(DMAControlBlocks *dmaCB, uint8_t rxStatus);
HdlcDCB *DMAControlBlocks_LoadBufferDescription(DMAControlBlocks *dmaCB, uint32_t listPointer, uint16_t offset, bool isRX);
uint8_t DMAControlBlocks_ReadNextByteDMA(DMAControlBlocks *dmaCB, bool isRx);
void DMAControlBlocks_WriteNextByteDMA(DMAControlBlocks *dmaCB, uint8_t data, bool isRx);
void DMAControlBlocks_SetReadDMACallback(DMAControlBlocks *dmaCB, DMAControlBlocks_ReadCallback callback, void *context);
void DMAControlBlocks_SetWriteDMACallback(DMAControlBlocks *dmaCB, DMAControlBlocks_WriteCallback callback, void *context);
void DMAControlBlocks_SetSendHDLCFrameCallback(DMAControlBlocks *dmaCB, DMAControlBlocks_SendFrameCallback callback, void *context);
void DMAControlBlocks_SetInterruptCallback(DMAControlBlocks *dmaCB, DMAControlBlocks_InterruptCallback callback, void *context);

#endif // DMA_CONTROL_BLOCKS_H