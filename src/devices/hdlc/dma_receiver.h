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

/**
 * @brief Zero the receiver state, store the COM5025 chip, DMA control block
 *        set and owning HDLC device references, and init the TCP receive
 *        ring buffer at its default capacity.
 * @param receiver Receiver state to initialize.
 * @param com5025 COM5025State chip instance (stored as a raw pointer).
 * @param dmaCB DMA control block set the receiver fills.
 * @param hdlcDevice Owning HDLC device.
 * @return void.
 */
void DMAReceiver_Init(DMAReceiver *receiver, void *com5025, DMAControlBlocks *dma_cb,
                      struct Device *hdlc_device);

/**
 * @brief Free the TCP receive ring buffer and clear the interrupt callback.
 * @param receiver Receiver state to destroy.
 * @return void.
 */
void DMAReceiver_Destroy(DMAReceiver *receiver);

/**
 * @brief Reset the received-byte counter and clear the TCP receive ring
 *        buffer.
 * @param receiver Receiver state to clear.
 * @return void.
 */
void DMAReceiver_Clear(DMAReceiver *receiver);

/**
 * @brief Called every CPU cycle from DMAEngine_Tick: after an adaptive
 *        delay, process one complete HDLC frame's worth of buffered TCP
 *        data into the DMA receive buffers.
 * @param receiver Receiver state to advance.
 * @return void.
 */
void DMAReceiver_Tick(DMAReceiver *receiver);

// State management

/**
 * @brief Called by CommandReceiverStart / CommandReceiverContinue: enable
 *        receiver DMA and the HDLC receiver hardware, clear overrun/empty
 *        error flags, mark the receiver active, and load or find the next
 *        empty receive buffer.
 * @param receiver Receiver state to update.
 * @return void.
 */
void DMAReceiver_SetReceiverState(DMAReceiver *receiver);

// Data processing - TCP ring buffer path (burst mode, always active)

/**
 * @brief Non-blocking enqueue of data arriving from the modem's TCP link
 *        into the receiver's ring buffer; the byte-stuffed HDLC processing
 *        happens later in DMAReceiver_Tick. Bypasses the COM5025 chip.
 * @param receiver Receiver state to enqueue into.
 * @param data Bytes received from the modem.
 * @param length Number of bytes in data.
 * @return void.
 */
void DMAReceiver_ReceiveDataFromModem(DMAReceiver *receiver, const uint8_t *data, int length);


// Buffer management


// Flag and interrupt management

/**
 * @brief Register the callback invoked to raise or clear a receiver
 *        interrupt bit.
 * @param receiver Receiver state to update.
 * @param callback Function to call with the interrupt bit to set.
 * @return void.
 */
void DMAReceiver_SetInterruptCallback(DMAReceiver *receiver,
                                      DMAReceiverSetInterruptCallback callback);

#endif // DMA_RECEIVER_H
