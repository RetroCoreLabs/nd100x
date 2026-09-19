/*
 * dma_control_blocks.h - HDLC DMA control blocks: structures and buffer list API.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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
typedef struct DMAControlBlocks
{
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

/**
 * @brief Zero a DMAControlBlocks, allocate its outbound frame buffer and its
 * receive HDLCFrame, and store the owning device.
 * @param dcbs Control block structure to initialize.
 * @param hdlcDevice Owning HDLC device, stored for status counters and logging.
 */
void DMAControlBlocks_Init(DMAControlBlocks *dcbs, struct Device *hdlcDevice);

/**
 * @brief Free the outbound buffer, TX/RX DCBs and receive HDLCFrame, then
 * zero the whole structure.
 * @param dmaCB Control block structure to tear down.
 */
void DMAControlBlocks_Destroy(DMAControlBlocks *dmaCB);

/**
 * @brief Free the TX/RX DCBs, reset the list pointers and the outbound
 * buffer size, without freeing the outbound buffer itself.
 * @param dmaCB Control block structure to reset.
 */
void DMAControlBlocks_Clear(DMAControlBlocks *dmaCB);

/**
 * @brief Set the transmit list pointer, reset its offset to 0 and load the
 * buffer description at that address.
 * @param dmaCB Control block structure.
 * @param listPointer Memory address of the start of the TX buffer list.
 * @param offset Unused (kept for interface symmetry with the RX pointer setter); unverified.
 */
void DMAControlBlocks_SetTXPointer(DMAControlBlocks *dmaCB, uint32_t listPointer, int offset);

/**
 * @brief Log up to 100 TX list entries (key value per 4-word block) at
 * LOG_DEBUG until an empty key or a new-list-pointer key is seen.
 * @param dmaCB Control block structure.
 */
void DMAControlBlocks_DebugTXFrames(DMAControlBlocks *dmaCB);

/**
 * @brief Free the current TX DCB and reload it from the current TX list
 * pointer and offset.
 * @param dmaCB Control block structure.
 */
void DMAControlBlocks_LoadTXBuffer(DMAControlBlocks *dmaCB);

/**
 * @brief Advance the TX list offset by one and reload the TX buffer.
 * @param dmaCB Control block structure.
 * @return true if the newly loaded buffer's key is
 * KEYFLAG_BLOCK_TO_BE_TRANSMITTED, false otherwise (including on NULL dmaCB).
 */
bool DMAControlBlocks_LoadNextTXBuffer(DMAControlBlocks *dmaCB);

/**
 * @brief Mark the current TX DCB as sent by OR-ing in
 * KEYFLAG_ALREADY_TRANSMITTED_BLOCK and writing the key value back to
 * memory, bumping the device's dcbTxMarked counter.
 * @param dmaCB Control block structure.
 */
void DMAControlBlocks_MarkBufferSent(DMAControlBlocks *dmaCB);

/**
 * @brief Set the receive list pointer and offset and load the buffer
 * description at that address.
 * @param dmaCB Control block structure.
 * @param listPointer Memory address of the start of the RX buffer list.
 * @param offset Offset (in 4-word blocks) of the first RX buffer to load.
 */
void DMAControlBlocks_SetRXPointer(DMAControlBlocks *dmaCB, uint32_t listPointer, int offset);

/**
 * @brief Free the current RX DCB and reload it from the current RX list
 * pointer and offset.
 * @param dmaCB Control block structure.
 */
void DMAControlBlocks_LoadRXBuffer(DMAControlBlocks *dmaCB);

/**
 * @brief Advance the RX list offset by one (wrapping at 128), reload the RX
 * buffer, and follow a NEW_LIST_POINTER key back to offset 0.
 * @param dmaCB Control block structure.
 * @return true if the resulting buffer's key is KEYFLAG_EMPTY_RECEIVER_BLOCK,
 * false if the list is exhausted or dmaCB/its DCB is NULL.
 */
bool DMAControlBlocks_LoadNextRXBuffer(DMAControlBlocks *dmaCB);

/**
 * @brief Peek at the RX list entry one slot past the current RX offset
 * without changing state.
 * @param dmaCB Control block structure.
 * @return true if that entry's key is KEYFLAG_EMPTY_RECEIVER_BLOCK, false
 * otherwise (including on NULL dmaCB).
 */
bool DMAControlBlocks_IsNextRXbufValid(DMAControlBlocks *dmaCB);

/**
 * @brief Mark the current RX DCB as filled: OR the caller-supplied status
 * into the low 8 bits of the key value, set KEYFLAG_BLOCK_DONE_BIT, and
 * write the key value back to memory. Bumps the device's dcbRxMarked
 * counter and, at LOG_DEBUG, dumps the received block bytes.
 * @param dmaCB Control block structure.
 * @param rxStatus Receiver status byte (RCOST) to store in the key value's low 8 bits.
 */
void DMAControlBlocks_MarkBufferReceived(DMAControlBlocks *dmaCB, uint8_t rxStatus);

/**
 * @brief Allocate and fill a HdlcDCB by reading the key value and, if
 * non-zero, the byte count and 24-bit data address at listPointer +
 * offset*4, plus the Displacement1/Displacement2 parameter for that offset.
 * @param dmaCB Control block structure (supplies the parameter buffer and DMA read callback).
 * @param listPointer Base memory address of the buffer list.
 * @param offset Offset (in 4-word blocks) of the buffer to load.
 * @param isRX true if loading an RX buffer, false for TX; used only for logging.
 * @return Newly allocated HdlcDCB (caller-owned), or NULL if dmaCB is NULL,
 * listPointer is 0, or allocation fails.
 */
HdlcDCB *DMAControlBlocks_LoadBufferDescription(DMAControlBlocks *dmaCB, uint32_t listPointer,
                                                uint16_t offset, bool isRX);

/**
 * @brief Read the next data byte of the active TX or RX DCB, skipping the
 * DCB's displacement bytes on the first call and advancing the DMA address
 * every two bytes read.
 * @param dmaCB Control block structure.
 * @param isRx true to read from the RX DCB, false for the TX DCB.
 * @return Next data byte, or 0 if dmaCB or the selected DCB is NULL.
 */
uint8_t DMAControlBlocks_ReadNextByteDMA(DMAControlBlocks *dmaCB, bool isRx);

/**
 * @brief Write the next data byte into the active TX or RX DCB's buffer,
 * skipping the DCB's displacement bytes on the first call, merging into the
 * existing 16-bit memory word, and updating the DCB's byte count in memory.
 * @param dmaCB Control block structure.
 * @param data Byte to write.
 * @param isRx true to write into the RX DCB, false for the TX DCB.
 */
void DMAControlBlocks_WriteNextByteDMA(DMAControlBlocks *dmaCB, uint8_t data, bool isRx);

/**
 * @brief Install the DMA memory-read callback and its context.
 * @param dmaCB Control block structure.
 * @param callback Function to call for a DMA memory read.
 * @param context Opaque context passed back to the callback.
 */
void DMAControlBlocks_SetReadDMACallback(DMAControlBlocks *dmaCB,
                                         DMAControlBlocks_ReadCallback callback, void *context);

/**
 * @brief Install the DMA memory-write callback and its context.
 * @param dmaCB Control block structure.
 * @param callback Function to call for a DMA memory write.
 * @param context Opaque context passed back to the callback.
 */
void DMAControlBlocks_SetWriteDMACallback(DMAControlBlocks *dmaCB,
                                          DMAControlBlocks_WriteCallback callback, void *context);

/**
 * @brief Install the callback used to hand a completed HDLC frame onward.
 * @param dmaCB Control block structure.
 * @param callback Function to call with the assembled frame.
 * @param context Opaque context passed back to the callback.
 */
void DMAControlBlocks_SetSendHDLCFrameCallback(DMAControlBlocks *dmaCB,
                                               DMAControlBlocks_SendFrameCallback callback,
                                               void *context);

/**
 * @brief Install the callback used to raise an interrupt bit.
 * @param dmaCB Control block structure.
 * @param callback Function to call with the interrupt bit number.
 * @param context Opaque context passed back to the callback.
 */
void DMAControlBlocks_SetInterruptCallback(DMAControlBlocks *dmaCB,
                                           DMAControlBlocks_InterruptCallback callback,
                                           void *context);

#endif // DMA_CONTROL_BLOCKS_H
