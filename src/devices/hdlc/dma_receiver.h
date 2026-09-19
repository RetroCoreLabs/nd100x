/*
 * dma_receiver.h - HDLC DMA receiver: state structure, callbacks and API.
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

#ifndef DMA_RECEIVER_H
#define DMA_RECEIVER_H

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
struct Device;

#include "chip_com5025.h"
#include "dma_control_blocks.h"
#include "tcp_receive_buffer.h"

// DMA Receiver callback function types (with context for forwarding)
typedef void (*DMAReceiverSetInterruptCallback)(void *context, uint8_t bit);

// DMA Receiver status enumeration
typedef enum
{
    DMA_RECEIVE_OK,
    DMA_RECEIVE_FAILED,
    DMA_RECEIVE_BUFFER_FULL,
    DMA_RECEIVE_NO_BUFFER
} DMAReceiveStatus;

// DMA Receiver state structure
typedef struct DMAReceiver
{
    // Total bytes received across all buffers (for statistics)
    int bytesReceived;

    // Tick-based delay for rate-limiting frame processing
    int processTcpBufDelay;

    // Ring buffer for TCP receive data (pull-based architecture)
    TcpReceiveBuffer tcpReceiveBuffer;

    // Hardware references
    COM5025State *com5025;
    DMAControlBlocks *dmaCB;
    struct Device *hdlcDevice;

    // Callbacks (with context)
    DMAReceiverSetInterruptCallback onSetInterruptBit;
    void *callbackContext;

} DMAReceiver;

// Core functions
void DMAReceiver_Init(DMAReceiver *receiver, void *com5025, DMAControlBlocks *dmaCB,
                      struct Device *hdlcDevice);
void DMAReceiver_Destroy(DMAReceiver *receiver);
void DMAReceiver_Clear(DMAReceiver *receiver);
void DMAReceiver_Tick(DMAReceiver *receiver);

// State management
void DMAReceiver_SetReceiverState(DMAReceiver *receiver);

// Data processing - TCP ring buffer path (burst mode, always active)
void DMAReceiver_ReceiveDataFromModem(DMAReceiver *receiver, const uint8_t *data, int length);
int DMAReceiver_ProcessBufferedData(DMAReceiver *receiver);
bool DMAReceiver_ProcessCompleteFrame(DMAReceiver *receiver);
void DMAReceiver_ClearReceiveFrameState(DMAReceiver *receiver);

// Buffer management
bool DMAReceiver_FindNextReceiveBuffer(DMAReceiver *receiver);
DMAReceiveStatus DMAReceiver_ReceiveDataBufferByte(DMAReceiver *receiver, uint8_t data);

// Flag and interrupt management
void DMAReceiver_SetRXDMAFlag(DMAReceiver *receiver, uint16_t flag);
void DMAReceiver_EnableHDLCReceiver(DMAReceiver *receiver, bool enable);
void DMAReceiver_SetInterruptCallback(DMAReceiver *receiver,
                                      DMAReceiverSetInterruptCallback callback);

#endif // DMA_RECEIVER_H
