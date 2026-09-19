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
void DMATransmitter_Init(DMATransmitter *transmitter, void *com5025, DMAControlBlocks *dmaCB,
                         struct Device *hdlcDevice);
void DMATransmitter_Destroy(DMATransmitter *transmitter);
void DMATransmitter_Clear(DMATransmitter *transmitter);
void DMATransmitter_Tick(DMATransmitter *transmitter);

// State management
void DMATransmitter_SetSenderState(DMATransmitter *transmitter, int senderState);
void DMATransmitter_SetEngineSenderState(DMATransmitter *transmitter, int state);

// Data transmission (burst mode)
bool DMATransmitter_SendAllBuffers(DMATransmitter *transmitter);
void DMATransmitter_SetTXDMAFlag(DMATransmitter *transmitter, uint16_t flag);

// Callback setup
void DMATransmitter_SetSendFrameCallback(DMATransmitter *transmitter,
                                         DMATransmitterSendFrameCallback callback);
void DMATransmitter_SetInterruptCallback(DMATransmitter *transmitter,
                                         DMATransmitterSetInterruptCallback callback);

#endif // DMA_TRANSMITTER_H
