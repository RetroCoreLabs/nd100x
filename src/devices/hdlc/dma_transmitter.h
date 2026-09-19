/*
 * dma_transmitter.h - HDLC DMA transmitter: state structure, callbacks and API.
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

#ifndef DMA_TRANSMITTER_H
#define DMA_TRANSMITTER_H

#include <stdint.h>
#include <stdbool.h>

struct Device;

#include "chip_com5025.h"
#include "dma_control_blocks.h"
#include "hdlc_frame.h"

// DMA Transmitter callback function types (with context for forwarding)
typedef void (*DMATransmitterSendFrameCallback)(void *context, HDLCFrame *frame);
typedef void (*DMATransmitterSetInterruptCallback)(void *context, uint8_t bit);

// DMA Transmitter state structure
typedef struct DMATransmitter
{
    bool active;
    int bytesSent;

    // Hardware references
    COM5025State *com5025;
    DMAControlBlocks *dmaCB;
    struct Device *hdlcDevice;

    // Callbacks (with context)
    DMATransmitterSendFrameCallback onSendHDLCFrame;
    DMATransmitterSetInterruptCallback onSetInterruptBit;
    void *callbackContext;

} DMATransmitter;

// Core functions

/**
 * @brief Zero the transmitter state and store the COM5025 chip, DMA control
 *        block set and owning HDLC device references.
 * @param transmitter Transmitter state to initialize.
 * @param com5025 COM5025State chip instance (stored as a raw pointer).
 * @param dmaCB DMA control block set the transmitter drains from.
 * @param hdlcDevice Owning HDLC device.
 * @return void.
 */
void DMATransmitter_Init(DMATransmitter *transmitter, void *com5025, DMAControlBlocks *dmaCB,
                         struct Device *hdlcDevice);

/**
 * @brief Clear the transmitter's callback pointers.
 * @param transmitter Transmitter state to destroy.
 * @return void.
 */
void DMATransmitter_Destroy(DMATransmitter *transmitter);

/**
 * @brief Reset the transmitter to inactive with zero bytes sent.
 * @param transmitter Transmitter state to clear.
 * @return void.
 */
void DMATransmitter_Clear(DMATransmitter *transmitter);

/**
 * @brief Burst-mode transmit state machine tick: while DMA and TX are
 *        enabled and the rate-limit wait has expired, send all ready DMA
 *        buffers as HDLC frames.
 * @param transmitter Transmitter state to advance.
 * @return void.
 */
void DMATransmitter_Tick(DMATransmitter *transmitter);

// State management

/**
 * @brief Set the DMA engine sender state machine state and mirror whether
 *        the transmitter is active (state == DMA_SENDER_BLOCK_READY_TO_SEND).
 * @param transmitter Transmitter state to update.
 * @param senderState New DmaEngineSenderState value.
 * @return void.
 */
void DMATransmitter_SetSenderState(DMATransmitter *transmitter, int senderState);


// Data transmission (burst mode)


// Callback setup

/**
 * @brief Register the callback invoked to hand a completed HDLC frame to the
 *        modem for sending.
 * @param transmitter Transmitter state to update.
 * @param callback Function to call with the frame to send.
 * @return void.
 */
void DMATransmitter_SetSendFrameCallback(DMATransmitter *transmitter,
                                         DMATransmitterSendFrameCallback callback);

/**
 * @brief Register the callback invoked to raise or clear a transmitter
 *        interrupt bit.
 * @param transmitter Transmitter state to update.
 * @param callback Function to call with the interrupt bit to set.
 * @return void.
 */
void DMATransmitter_SetInterruptCallback(DMATransmitter *transmitter,
                                         DMATransmitterSetInterruptCallback callback);

#endif // DMA_TRANSMITTER_H
