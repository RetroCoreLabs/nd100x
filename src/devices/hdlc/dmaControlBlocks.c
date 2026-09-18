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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#include "dmaControlBlocks.h"
#include "dmaParamBuf.h"
#include "hdlcFrame.h"
#include "dmaDCB.h"
#include "../devices_types.h"
#include "deviceHDLC.h"
#include "dmaEnum.h"
#include "hdlc_constants.h"

// Debug flags (convert from C# #define)
// #define DEBUG_DETAIL
// #define DEBUG_DETAIL_PLUS_DESCRIPTION
// #define DMA_DEBUG
//#define RX_BLAST_LOGGING

// Static function declarations
static void DMAControlBlocks_DMAWrite(DMAControlBlocks *dmaCB, uint32_t address, uint16_t data);
static int DMAControlBlocks_DMARead(DMAControlBlocks *dmaCB, uint32_t address);
#if defined(DMA_DEBUG) || defined(DEBUG_DETAIL) || defined(RX_BLAST_LOGGING)
static void DMAControlBlocks_Log(DMAControlBlocks *dmaCB, const char *format, ...);
#endif

void DMAControlBlocks_Init(DMAControlBlocks *dcbs, struct Device *hdlcDevice)
{
    if (!dcbs) return;

    memset(dcbs, 0, sizeof(DMAControlBlocks));

    // Store the device reference
    dcbs->hdlcDevice = hdlcDevice;

    // Initialize outbound buffer
    dcbs->outboundBufferCapacity = HDLC_MAX_FRAME_SIZE + 64;
    dcbs->outboundBuffer = malloc(dcbs->outboundBufferCapacity);
    dcbs->outboundBufferSize = 0;

    // Initialize DCBs
    dcbs->txDCB = NULL;
    dcbs->rxDCB = NULL;

    // Initialize list pointers
    dcbs->txListPointer = 0;
    dcbs->txListPointerOffset = 0;
    dcbs->rxListPointer = 0;
    dcbs->rxListPointerOffset = 0;

    // Initialize HDLC frame
    dcbs->hdlcReceiveFrame = malloc(sizeof(HDLCFrame));
    if (dcbs->hdlcReceiveFrame) {
        HDLCFrame_Init(dcbs->hdlcReceiveFrame);
    }

    // Initialize DMA state machine (these constants need to be defined)
    dcbs->dmaSenderState = 0; // DMA_SENDER_STOPPED
    dcbs->dmaSendBlockState = 0; // DMA_BLOCK_IDLE
    dcbs->dmaWaitTicks = -1;
    dcbs->parameters = NULL;

    // Initialize callbacks
    dcbs->onReadDMA = NULL;
    dcbs->onWriteDMA = NULL;
    dcbs->onSendHDLCFrame = NULL;
    dcbs->onSetInterruptBit = NULL;
    dcbs->callbackContext = NULL;
}

void DMAControlBlocks_Destroy(DMAControlBlocks *dmaCB)
{
    if (!dmaCB) return;

    // Free outbound buffer
    if (dmaCB->outboundBuffer) {
        free(dmaCB->outboundBuffer);
        dmaCB->outboundBuffer = NULL;
    }

    // Free DCBs
    if (dmaCB->txDCB) {
        free(dmaCB->txDCB);
        dmaCB->txDCB = NULL;
    }
    if (dmaCB->rxDCB) {
        free(dmaCB->rxDCB);
        dmaCB->rxDCB = NULL;
    }

    // Free HDLC frame
    if (dmaCB->hdlcReceiveFrame) {
        free(dmaCB->hdlcReceiveFrame);
        dmaCB->hdlcReceiveFrame = NULL;
    }

    memset(dmaCB, 0, sizeof(DMAControlBlocks));
}

void DMAControlBlocks_Clear(DMAControlBlocks *dmaCB)
{
    if (!dmaCB) return;

    // Free existing DCBs
    if (dmaCB->txDCB) {
        free(dmaCB->txDCB);
        dmaCB->txDCB = NULL;
    }
    if (dmaCB->rxDCB) {
        free(dmaCB->rxDCB);
        dmaCB->rxDCB = NULL;
    }

    // Reset list pointers
    dmaCB->txListPointer = 0;
    dmaCB->txListPointerOffset = 0;
    dmaCB->rxListPointer = 0;
    dmaCB->rxListPointerOffset = 0;

    // Clear outbound buffer
    dmaCB->outboundBufferSize = 0;

    // Clear parameters would go here if ParameterBuffer was implemented
    // dmaCB->parameters.Clear();
}

// TX Functions

void DMAControlBlocks_SetTXPointer(DMAControlBlocks *dmaCB, uint32_t listPointer, int offset)
{
    (void)offset;
    if (!dmaCB) return;

    dmaCB->txListPointer = listPointer;
    dmaCB->txListPointerOffset = 0;

    DMAControlBlocks_LoadTXBuffer(dmaCB);
}

void DMAControlBlocks_DebugTXFrames(DMAControlBlocks *dmaCB)
{
#ifdef DMA_DEBUG
    if (!dmaCB) return;

    for (int i = 0; i < 100; i++) {
        uint32_t addr = dmaCB->txListPointer + (uint32_t)(i * 4);

        uint16_t keyValue = (uint16_t)DMAControlBlocks_DMARead(dmaCB, addr);
        KeyFlags key = (KeyFlags)(keyValue & 0xFF00);

        DMAControlBlocks_Log(dmaCB, "TXAnalyse: Offset=%d LP=0x%06X Key=0x%04X", i, addr, keyValue);

        if ((keyValue == 0) || (key == KEYFLAG_NEW_LIST_POINTER)) {
            DMAControlBlocks_Log(dmaCB, "TXAnalyse: End of frames at offset=%d", i);
            return;
        }
    }
#else
    (void)dmaCB;
#endif
}

void DMAControlBlocks_LoadTXBuffer(DMAControlBlocks *dmaCB)
{
    if (!dmaCB) return;

    if (dmaCB->txDCB) {
        free(dmaCB->txDCB);
    }

    dmaCB->txDCB = DMAControlBlocks_LoadBufferDescription(dmaCB, dmaCB->txListPointer, dmaCB->txListPointerOffset, false);
}

bool DMAControlBlocks_LoadNextTXBuffer(DMAControlBlocks *dmaCB)
{
    if (!dmaCB) return false;

    dmaCB->txListPointerOffset++;
    DMAControlBlocks_LoadTXBuffer(dmaCB);

    return (dmaCB->txDCB && DCB_GetKey(dmaCB->txDCB) == KEYFLAG_BLOCK_TO_BE_TRANSMITTED);
}

void DMAControlBlocks_MarkBufferSent(DMAControlBlocks *dmaCB)
{
    if (!dmaCB || !dmaCB->txDCB) return;

    if (DCB_GetKey(dmaCB->txDCB) == KEYFLAG_BLOCK_TO_BE_TRANSMITTED) {
        if (dmaCB->hdlcDevice && dmaCB->hdlcDevice->deviceData) {
            ((HDLCData *)dmaCB->hdlcDevice->deviceData)->dcbTxMarked++;
        }
        uint16_t data = (uint16_t)(DCB_GetDataFlowCost(dmaCB->txDCB) | (uint16_t)(KEYFLAG_ALREADY_TRANSMITTED_BLOCK));
        DCB_SetKeyValue(dmaCB->txDCB, data);

#ifdef DMA_DEBUG
        DMAControlBlocks_Log(dmaCB, "MarkBufferSent: Status Word = 0x%04X", data);
#endif
        DMAControlBlocks_DMAWrite(dmaCB, DCB_GetBufferAddress(dmaCB->txDCB), data);
    }
}

// RX Functions

void DMAControlBlocks_SetRXPointer(DMAControlBlocks *dmaCB, uint32_t listPointer, int offset)
{
    if (!dmaCB) return;

    dmaCB->rxListPointer = listPointer;
    dmaCB->rxListPointerOffset = (uint16_t)offset;

    DMAControlBlocks_LoadRXBuffer(dmaCB);
}

void DMAControlBlocks_LoadRXBuffer(DMAControlBlocks *dmaCB)
{
    if (!dmaCB) return;

    if (dmaCB->rxDCB) {
        free(dmaCB->rxDCB);
    }

    dmaCB->rxDCB = DMAControlBlocks_LoadBufferDescription(dmaCB, dmaCB->rxListPointer, dmaCB->rxListPointerOffset, true);
}

bool DMAControlBlocks_LoadNextRXBuffer(DMAControlBlocks *dmaCB)
{
    if (!dmaCB) return false;

    dmaCB->rxListPointerOffset++;

    // Assuming max 128 buffers in the list (for safefty), wrap around to 0 if we exceed
    if (dmaCB->rxListPointerOffset > 128) {
        dmaCB->rxListPointerOffset = 0;
    }

    DMAControlBlocks_LoadRXBuffer(dmaCB);

    if (!dmaCB->rxDCB) return false;

    // Handle NewListPointer - loop back to offset 0 of the buffer ring
    if (DCB_GetKey(dmaCB->rxDCB) == KEYFLAG_NEW_LIST_POINTER) {
#ifdef DMA_DEBUG
        DMAControlBlocks_Log(dmaCB, "NewListPointer at offset %d, looping back to offset 0",
                           dmaCB->rxListPointerOffset);
#endif
        dmaCB->rxListPointerOffset = 0;
        DMAControlBlocks_LoadRXBuffer(dmaCB);

        if (!dmaCB->rxDCB) return false;

        // If first buffer after wrap is not empty, list is exhausted
        if (DCB_GetKey(dmaCB->rxDCB) != KEYFLAG_EMPTY_RECEIVER_BLOCK) {
#ifdef DMA_DEBUG
            DMAControlBlocks_Log(dmaCB, "Buffer after NewListPointer is not empty - list exhausted");
#endif
            return false;
        }
    }

    return (DCB_GetKey(dmaCB->rxDCB) == KEYFLAG_EMPTY_RECEIVER_BLOCK);
}

bool DMAControlBlocks_IsNextRXbufValid(DMAControlBlocks *dmaCB)
{
    if (!dmaCB) return false;

    uint32_t listpointer = dmaCB->rxListPointer + (uint32_t)((dmaCB->rxListPointerOffset + 1) * 4);
    uint16_t keyValue = (uint16_t)DMAControlBlocks_DMARead(dmaCB, listpointer);
    KeyFlags key = (KeyFlags)(keyValue & KEYFLAG_MASK_KEY);

    return (key == KEYFLAG_EMPTY_RECEIVER_BLOCK);
}

// ---------------------------------------------------------------------------
// DMA Back to ND machine with a new KEY telling that this buffer has been received
//
// Update KEY = FullReceiverBlock
// -----------------------------------------------------------------------------
void DMAControlBlocks_MarkBufferReceived(DMAControlBlocks *dmaCB, uint8_t rxStatus)
{
    if (!dmaCB || !dmaCB->rxDCB) return;

    if (DCB_GetKey(dmaCB->rxDCB) == KEYFLAG_EMPTY_RECEIVER_BLOCK) {
        if (dmaCB->hdlcDevice && dmaCB->hdlcDevice->deviceData) {
            ((HDLCData *)dmaCB->hdlcDevice->deviceData)->dcbRxMarked++;
        }

        // Update the key value with the RX status and mark as DONE
        uint16_t keyValue = DCB_GetKeyValue(dmaCB->rxDCB);

        // Set RCOST in the low 8 bits, its actually the ReceiverStatusRegister
        keyValue = (keyValue & 0xFF00) | rxStatus;

        // Mark block as DONE (ie filled up)
        keyValue |= (uint16_t)(KEYFLAG_BLOCK_DONE_BIT);
        DCB_SetKeyValue(dmaCB->rxDCB, keyValue);

        // Write back the updated key value to memory
        DMAControlBlocks_DMAWrite(dmaCB, DCB_GetBufferAddress(dmaCB->rxDCB), keyValue);

#ifdef DMA_DEBUG
        const char *flags = "";
        char flagsBuffer[64] = "";
        if (DCB_HasRSOMFlag(dmaCB->rxDCB)) strcat(flagsBuffer, "RSOM ");
        if (DCB_HasREOMFlag(dmaCB->rxDCB)) strcat(flagsBuffer, "REOM ");
        flags = flagsBuffer;

        DMAControlBlocks_Log(dmaCB, "--------------------------------------------------------------------------");
        DMAControlBlocks_Log(dmaCB, "DMA Buffer received           : 0x%06X Flags: %s", DCB_GetBufferAddress(dmaCB->rxDCB), flags);
        DMAControlBlocks_Log(dmaCB, "ListPointer                   : 0x%06X  lp[0x%06X] offset[%d]",
                           DCB_GetListPointer(dmaCB->rxDCB), dmaCB->rxListPointer, dmaCB->rxListPointerOffset);
        DMAControlBlocks_Log(dmaCB, "");
        DMAControlBlocks_Log(dmaCB, "KeyValue                      : 0x%04X %s [MarkBufferReceived]",
                           DMAControlBlocks_DMARead(dmaCB, DCB_GetBufferAddress(dmaCB->rxDCB) + 0), ""); // TODO: key name
        DMAControlBlocks_Log(dmaCB, "ByteCount                     : 0x%04X",
                           DMAControlBlocks_DMARead(dmaCB, DCB_GetBufferAddress(dmaCB->rxDCB) + 1));
        DMAControlBlocks_Log(dmaCB, "MostAddress                   : 0x%04X",
                           DMAControlBlocks_DMARead(dmaCB, DCB_GetBufferAddress(dmaCB->rxDCB) + 2));
        DMAControlBlocks_Log(dmaCB, "LeastAddress                  : 0x%04X",
                           DMAControlBlocks_DMARead(dmaCB, DCB_GetBufferAddress(dmaCB->rxDCB) + 3));
        DMAControlBlocks_Log(dmaCB, "DMA bytes written             : %d", DCB_GetDMABytesWritten(dmaCB->rxDCB));

        if (DCB_GetDMABytesWritten(dmaCB->rxDCB) > 0) {
            DCB_SetDMAAddress(dmaCB->rxDCB, DCB_GetDataMemoryAddress(dmaCB->rxDCB));

            char bytes[512] = "";
            char temp[16];
            for (int i = 0; i < DCB_GetDMABytesWritten(dmaCB->rxDCB); i++) {
                snprintf(temp, sizeof(temp), "0x%02X ", DMAControlBlocks_ReadNextByteDMA(dmaCB, true));
                strcat(bytes, temp);
            }

            DMAControlBlocks_Log(dmaCB, "Received block [%06X:%d]: %s [RSOM:%d] [REOM:%d]",
                               DCB_GetBufferAddress(dmaCB->rxDCB), dmaCB->rxListPointerOffset, bytes,
                               DCB_HasRSOMFlag(dmaCB->rxDCB), DCB_HasREOMFlag(dmaCB->rxDCB));
        }
        DMAControlBlocks_Log(dmaCB, "--------------------------------------------------------------------------");
#endif
    }
}

// Buffer Description Loading

HdlcDCB* DMAControlBlocks_LoadBufferDescription(DMAControlBlocks *dmaCB, uint32_t listPointer, uint16_t offset, bool isRX)
{
    (void)isRX;
    if (!dmaCB || listPointer == 0) return NULL;

    uint32_t actualListPointer = listPointer + (uint32_t)(offset * 4);

    HdlcDCB *description = malloc(sizeof(HdlcDCB));
    if (!description) return NULL;

    DCB_Init(description);
    DCB_SetListPointer(description, actualListPointer);
    DCB_SetOffsetFromLP(description, offset);
    DCB_SetBufferAddress(description, actualListPointer);

    uint32_t address = actualListPointer;
    uint16_t keyValue = (uint16_t)DMAControlBlocks_DMARead(dmaCB, address++);
    DCB_SetKeyValue(description, keyValue);

    if (keyValue != 0) {
        uint16_t byteCount = (uint16_t)DMAControlBlocks_DMARead(dmaCB, address++);
        uint16_t mostAddress = (uint16_t)DMAControlBlocks_DMARead(dmaCB, address++);
        uint16_t leastAddress = (uint16_t)DMAControlBlocks_DMARead(dmaCB, address++);

        DCB_SetByteCount(description, byteCount);

        // Set the data memory address using the most and least address parts
        uint32_t dataMemoryAddr = ((uint32_t)(mostAddress & 0x00FF) << 16) | leastAddress;
        DCB_SetDataMemoryAddress(description, dataMemoryAddr);
    }

    // Set displacement based on offset
    uint16_t displacement;
    if (offset == 0) {
        // First buffer in the list uses Displacement1
        displacement = dmaCB->parameters ? (uint16_t)dmaCB->parameters->displacement1 : 0;
    } else {
        // All other buffers use Displacement2
        displacement = dmaCB->parameters ? (uint16_t)dmaCB->parameters->displacement2 : 0;
    }
    DCB_SetDisplacement(description, displacement);

#ifdef DMA_DEBUG
    const char *mode = isRX ? "RX" : "TX";
    DMAControlBlocks_Log(dmaCB, "--------------------------------------------------------------------------");
    DMAControlBlocks_Log(dmaCB, "LoadBufferDescription %s      : 0x%06X lp[0x%06X] offset[%d]",
                       mode, actualListPointer, listPointer, offset);
    DMAControlBlocks_Log(dmaCB, "");
    DMAControlBlocks_Log(dmaCB, "KeyValue                      : 0x%04X %s", keyValue, ""); // TODO: key name

    if (DCB_GetKey(description) != KEYFLAG_EMPTY_RECEIVER_BLOCK) {
        DMAControlBlocks_Log(dmaCB, "ByteCount                     : 0x%04X", DCB_GetByteCount(description));
    }
    DMAControlBlocks_Log(dmaCB, "MostAddress                   : 0x%04X", (DCB_GetDataMemoryAddress(description) >> 16) & 0xFF);
    DMAControlBlocks_Log(dmaCB, "LeastAddress                  : 0x%04X", DCB_GetDataMemoryAddress(description) & 0xFFFF);
    DMAControlBlocks_Log(dmaCB, "Displacement                  : %d : Use Displacement%s", displacement, disp);

    if ((DCB_GetByteCount(description) > 0) && (DCB_GetKey(description) == KEYFLAG_BLOCK_TO_BE_TRANSMITTED)) {
        DCB_SetDMAAddress(description, DCB_GetDataMemoryAddress(description));

        char bytes[512] = "";
        char temp[16];
        for (int i = 0; i < DCB_GetByteCount(description); i++) {
            snprintf(temp, sizeof(temp), "0x%02X ", DMAControlBlocks_ReadNextByteDMA(dmaCB, isRX));
            strcat(bytes, temp);
        }

        DMAControlBlocks_Log(dmaCB, "DATA: %s", bytes);
    }
    DMAControlBlocks_Log(dmaCB, "--------------------------------------------------------------------------");
#endif

#ifdef DEBUG_DETAIL
    if (DCB_GetKey(description) == KEYFLAG_EMPTY_RECEIVER_BLOCK) {
        DMAControlBlocks_Log(dmaCB, "Loading Buffer from 0x%08X Key=%s", listPointer, "EmptyReceiverBlock");
    } else {
        DMAControlBlocks_Log(dmaCB, "Loading Buffer from 0x%08X Key=%s DataFlowCost=0x%08X ByteCount=%d RSOMFlag=%d REOMFlag=%d",
                           listPointer, "", DCB_GetDataFlowCost(description), DCB_GetByteCount(description),
                           DCB_HasRSOMFlag(description), DCB_HasREOMFlag(description));
    }
#endif

    // Set up the DMA helpers
    DCB_SetDMAAddress(description, DCB_GetDataMemoryAddress(description));
    DCB_SetDMAReadData(description, -1);
    DCB_SetDMABytesWritten(description, 0);
    DCB_SetDMABytesRead(description, 0);

    return description;
}

// DMA Read/Write Helper Functions

uint8_t DMAControlBlocks_ReadNextByteDMA(DMAControlBlocks *dmaCB, bool isRx)
{
    if (!dmaCB) return 0;

    HdlcDCB *description;
    if (isRx) {
        description = dmaCB->rxDCB;
    } else {
        description = dmaCB->txDCB;
    }

    if (!description) return 0;

    uint8_t data = 0;
    int bytesRead = DCB_GetDMABytesRead(description);
    uint16_t displacement = DCB_GetDisplacement(description);

    // Adjust for "displacement"
    if ((bytesRead == 0) && (displacement > 0)) {
        // We need to skip "displacement" number of bytes
        bytesRead += displacement;
        DCB_SetDMABytesRead(description, bytesRead);

        // and we need to calculate the new dmaAddress
        uint32_t dmaAddr = DCB_GetDMAAddress(description);
        dmaAddr += (uint32_t)(displacement / 2);
        DCB_SetDMAAddress(description, dmaAddr);
    }

    // Start reading
    if ((bytesRead % 2) == 0) { // 0 ==> is even (High byte), 1 ==> odd (Low byte)
        int readData = DMAControlBlocks_DMARead(dmaCB, DCB_GetDMAAddress(description));
        DCB_SetDMAReadData(description, readData);
        data = (uint8_t)(readData >> 8);
    } else {
        if (DCB_GetDMAReadData(description) == -1) { // data not read (might happen because of displacement)
            int readData = DMAControlBlocks_DMARead(dmaCB, DCB_GetDMAAddress(description));
            DCB_SetDMAReadData(description, readData);
        }

        data = (uint8_t)(DCB_GetDMAReadData(description) & 0xFF);

        uint32_t dmaAddr = DCB_GetDMAAddress(description);
        dmaAddr++;
        DCB_SetDMAAddress(description, dmaAddr);
    }

    bytesRead++;
    DCB_SetDMABytesRead(description, bytesRead);

    return data;
}

void DMAControlBlocks_WriteNextByteDMA(DMAControlBlocks *dmaCB, uint8_t data, bool isRx)
{
    if (!dmaCB) return;

    HdlcDCB *description;
    if (isRx) {
        description = dmaCB->rxDCB;
    } else {
        description = dmaCB->txDCB;
    }

    if (!description) return;


    int bytesWritten = DCB_GetDMABytesWritten(description);
    uint16_t displacement = DCB_GetDisplacement(description);

#ifdef RX_BLAST_LOGGING
    DMAControlBlocks_Log(dmaCB, " DMA_WRITE_ENTRY: data=0x%02X, bytes_written=%d, displacement=%d, dmaAddress=0x%08X",
                       data, bytesWritten, displacement, DCB_GetDMAAddress(description));
#endif

#ifdef DEBUG_DETAIL
    DMAControlBlocks_Log(dmaCB, "DMA Write: 0x%02X  #%d", data, bytesWritten);
#endif

    // Adjust for "displacement"
    if ((bytesWritten == 0) && (displacement > 0)) {
        // We need to skip "displacement" number of bytes
        bytesWritten += displacement;
        DCB_SetDMABytesWritten(description, bytesWritten);

        // and we need to calculate the new dmaAddress
        uint32_t dmaAddr = DCB_GetDMAAddress(description);
        dmaAddr += (uint32_t)(displacement / 2);
        DCB_SetDMAAddress(description, dmaAddr);

#ifdef RX_BLAST_LOGGING
        DMAControlBlocks_Log(dmaCB, "DMA_WRITE_DISPLACEMENT: skipped %d bytes, new_bytes_written=%d, new_dmaAddress=0x%08X",
                           displacement, bytesWritten, DCB_GetDMAAddress(description));
#endif
    }

    uint16_t dmaWriteData;
    uint32_t dmaAddr = DCB_GetDMAAddress(description);

    // Start writing
    if ((bytesWritten % 2) == 0) { // ==0 is even, 1 ==odd
        int memData = DMAControlBlocks_DMARead(dmaCB, dmaAddr);
        dmaWriteData = (uint16_t)((memData & 0x00FF) | ((data & 0xFF) << 8));
        DMAControlBlocks_DMAWrite(dmaCB, dmaAddr, dmaWriteData);
    } else {
        int memData = DMAControlBlocks_DMARead(dmaCB, dmaAddr);
        dmaWriteData = (uint16_t)((memData & 0xFF00) | (data & 0xFF));
        DMAControlBlocks_DMAWrite(dmaCB, dmaAddr, dmaWriteData);

        dmaAddr++;
        DCB_SetDMAAddress(description, dmaAddr);
    }

    bytesWritten++;
    DCB_SetDMABytesWritten(description, bytesWritten);

    // Update DCB with bytes written as "ByteCount"
    DMAControlBlocks_DMAWrite(dmaCB, DCB_GetListPointer(description) + 1, (uint16_t)bytesWritten);

#ifdef RX_BLAST_LOGGING
    DMAControlBlocks_Log(dmaCB, "DMA_WRITE_EXIT: bytes_written=%d, final_dmaAddress=0x%08X", bytesWritten, DCB_GetDMAAddress(description));
#endif
}

// Callback Setup Functions

void DMAControlBlocks_SetReadDMACallback(DMAControlBlocks *dmaCB, DMAControlBlocks_ReadCallback callback, void *context)
{
    if (!dmaCB) return;
    dmaCB->onReadDMA = callback;
    dmaCB->callbackContext = context;
}

void DMAControlBlocks_SetWriteDMACallback(DMAControlBlocks *dmaCB, DMAControlBlocks_WriteCallback callback, void *context)
{
    if (!dmaCB) return;
    dmaCB->onWriteDMA = callback;
    dmaCB->callbackContext = context;
}

void DMAControlBlocks_SetSendHDLCFrameCallback(DMAControlBlocks *dmaCB, DMAControlBlocks_SendFrameCallback callback, void *context)
{
    if (!dmaCB) return;
    dmaCB->onSendHDLCFrame = callback;
    dmaCB->callbackContext = context;
}

void DMAControlBlocks_SetInterruptCallback(DMAControlBlocks *dmaCB, DMAControlBlocks_InterruptCallback callback, void *context)
{
    if (!dmaCB) return;
    dmaCB->onSetInterruptBit = callback;
    dmaCB->callbackContext = context;
}

// Private Helper Functions

static void DMAControlBlocks_DMAWrite(DMAControlBlocks *dmaCB, uint32_t address, uint16_t data)
{
    if (!dmaCB || !dmaCB->onWriteDMA) return;
    dmaCB->onWriteDMA(dmaCB->callbackContext, address, data);
}

static int DMAControlBlocks_DMARead(DMAControlBlocks *dmaCB, uint32_t address)
{
    if (!dmaCB || !dmaCB->onReadDMA) return 0;
    return dmaCB->onReadDMA(dmaCB->callbackContext, address);
}

#if defined(DMA_DEBUG) || defined(DEBUG_DETAIL) || defined(RX_BLAST_LOGGING)
static void DMAControlBlocks_Log(DMAControlBlocks *dmaCB, const char *format, ...)
{
    char buffer[512];
    char finalBuffer[600];

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    snprintf(finalBuffer, sizeof(finalBuffer), "DMACB: %s", buffer);

    // TODO: Replace with actual logging function
    printf("%s\n", finalBuffer);
}
#endif