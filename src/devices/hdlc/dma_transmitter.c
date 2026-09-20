/*
 * dma_transmitter.c - HDLC DMA transmitter: sends frames from memory buffers to the modem.
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

#include "dma_transmitter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "dma_control_blocks.h"
#include "chip_com5025.h"
#include "dma_engine.h"
#include "dma_enum.h"
#include "device_hdlc.h"
#include "hdlc_frame.h"
#include "../devices_types.h"
static bool dma_transmitter_send_all_buffers(DMATransmitter *);
static void dma_transmitter_set_engine_sender_state(DMATransmitter *, int);


void DMATransmitter_Init(DMATransmitter *transmitter, void *com5025, DMAControlBlocks *dma_cb,
                         struct Device *hdlc_device)
{
    if (!transmitter)
    {
        return;
    }
    memset(transmitter, 0, sizeof(DMATransmitter));

    transmitter->com5025 = (COM5025State *)com5025;
    transmitter->dmaCB = dma_cb;
    transmitter->hdlcDevice = hdlc_device;
    transmitter->active = false;
    transmitter->bytesSent = 0;
    transmitter->onSendHDLCFrame = NULL;
    transmitter->onSetInterruptBit = NULL;
    transmitter->callbackContext = NULL;
}

void DMATransmitter_Destroy(DMATransmitter *transmitter)
{
    if (!transmitter)
    {
        return;
    }
    transmitter->onSendHDLCFrame = NULL;
    transmitter->onSetInterruptBit = NULL;
}

void DMATransmitter_Clear(DMATransmitter *transmitter)
{
    if (!transmitter)
    {
        return;
    }
    transmitter->active = false;
    transmitter->bytesSent = 0;
}

// ---------------------------------------------------------------------------
// Tick: burst mode transmit state machine.
// Waits for DMA+TX enabled, rate-limits with dmaWaitTicks, sends all buffers.
// ---------------------------------------------------------------------------
void DMATransmitter_Tick(DMATransmitter *transmitter)
{
    if (!transmitter || !transmitter->dmaCB || !transmitter->hdlcDevice)
    {
        return;
    }

    HDLCData *hdlc_data = (HDLCData *)transmitter->hdlcDevice->deviceData;
    if (!hdlc_data)
    {
        return;
    }

    DMAControlBlocks *dma_cb = transmitter->dmaCB;

    switch (dma_cb->dmaSenderState)
    {
    case DMA_SENDER_STOPPED:
        break;

    case DMA_SENDER_BLOCK_READY_TO_SEND:
        if (hdlc_data->txTransferControl.bits.transmitterEnabled &&
            hdlc_data->txTransferControl.bits.enableTransmitterDMA)
        {

            if (dma_cb->dmaWaitTicks > 0)
            {
                dma_cb->dmaWaitTicks--;
            }

            if (dma_cb->dmaWaitTicks == 0)
            {
                if (dma_transmitter_send_all_buffers(transmitter))
                {
                    dma_cb->dmaWaitTicks = -1; // disable
                }
                else
                {
                    dma_cb->dmaWaitTicks = 50; // Wait before next block
                }
            }
        }
        break;

    case DMA_SENDER_SENDING_BLOCK:
    case DMA_SENDER_FRAME_SENT:
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// SetTXDMAFlag: set DMA flags and raise interrupt on level 12
// ---------------------------------------------------------------------------
static void dma_transmitter_set_txdma_flag(DMATransmitter *transmitter, uint16_t flag)
{
    if (!transmitter || !transmitter->hdlcDevice)
    {
        return;
    }

    HDLCData *hdlc_data = (HDLCData *)transmitter->hdlcDevice->deviceData;
    if (!hdlc_data)
    {
        return;
    }

    hdlc_data->txTransferStatus.raw |= flag;

    if (flag & TTS_TRANSMISSION_FINISHED)
    {
        hdlc_data->txTransferStatus.bits.dmaModuleRequest = 1;
    }
    if (hdlc_data->txTransferControl.bits.blockEndIE && hdlc_data->txTransferStatus.bits.blockEnd)
    {
        hdlc_data->txTransferStatus.bits.dmaModuleRequest = 1;
    }
    if (hdlc_data->txTransferControl.bits.frameEndIE && hdlc_data->txTransferStatus.bits.frameEnd)
    {
        hdlc_data->txTransferStatus.bits.dmaModuleRequest = 1;
    }
    if (hdlc_data->txTransferControl.bits.listEndIE && hdlc_data->txTransferStatus.bits.listEnd)
    {
        hdlc_data->txTransferStatus.bits.dmaModuleRequest = 1;
    }

    if (hdlc_data->txTransferControl.bits.dmaModuleIE &&
        hdlc_data->txTransferStatus.bits.dmaModuleRequest)
    {
        if (transmitter->onSetInterruptBit)
        {
            transmitter->onSetInterruptBit(transmitter->callbackContext, 12);
        }
    }
}

// Record a sent frame in the device's TX history ring (diagnostics).
static void record_tx_history(DMATransmitter *transmitter, HdlcDCB *dcb,
                              const uint8_t *frame_buffer, int frame_length)
{
    if (transmitter->hdlcDevice && transmitter->hdlcDevice->deviceData)
    {
        HDLCData *hd = (HDLCData *)transmitter->hdlcDevice->deviceData;
        int idx = hd->txHistoryIdx % HDLC_TX_HISTORY_SIZE;
        hd->txHistory[idx].listPtr = DCB_GetBufferAddress(dcb);
        hd->txHistory[idx].dataAddr = DCB_GetDataMemoryAddress(dcb);
        hd->txHistory[idx].byteCount = dcb->byteCount;
        hd->txHistory[idx].keyBefore = DCB_GetKeyValue(dcb);
        hd->txHistory[idx].frameSize = (uint16_t)frame_length;
        int copy_len =
            frame_length < HDLC_TX_HISTORY_DATA_SIZE ? frame_length : HDLC_TX_HISTORY_DATA_SIZE;
        memcpy(hd->txHistory[idx].data, frame_buffer, (size_t)copy_len);
        hd->txHistory[idx].dataLen = (uint8_t)copy_len;
        hd->txHistoryIdx++;
    }
}

// Build an HDLC frame from the accumulated outbound buffer and hand it to
// the send callback (and the TX history).
static void send_outbound_frame(DMATransmitter *transmitter, DMAControlBlocks *dma_cb)
{
    if (dma_cb->outboundBuffer && dma_cb->outboundBufferSize > 0 && transmitter->onSendHDLCFrame)
    {
        uint8_t frame_buffer[HDLC_MAX_FRAME_SIZE + 10];
        int frame_length = HDLCFrame_BuildFrame(dma_cb->outboundBuffer, dma_cb->outboundBufferSize,
                                                frame_buffer, sizeof(frame_buffer));
        if (frame_length > 0)
        {
            record_tx_history(transmitter, dma_cb->txDCB, frame_buffer, frame_length);

            HDLCFrame callback_frame;
            HDLCFrame_Init(&callback_frame);
            memcpy(callback_frame.frameBuffer, frame_buffer, (size_t)frame_length);
            callback_frame.frameLength = frame_length;
            callback_frame.frameComplete = true;
            transmitter->onSendHDLCFrame(transmitter->callbackContext, &callback_frame);
        }
    }
}

// ---------------------------------------------------------------------------
// SendAllBuffers: read TX DCBs, accumulate frames, send complete HDLC frames.
// Returns true when all buffers have been sent.
// ---------------------------------------------------------------------------
static bool dma_transmitter_send_all_buffers(DMATransmitter *transmitter)
{
    if (!transmitter || !transmitter->dmaCB)
    {
        return true;
    }

    DMAControlBlocks *dma_cb = transmitter->dmaCB;
    uint16_t tmp_tsb = 0;

    if (dma_cb->txListPointer == 0)
    {
        return true;
    }

    // Track call count
    if (transmitter->hdlcDevice && transmitter->hdlcDevice->deviceData)
    {
        ((HDLCData *)transmitter->hdlcDevice->deviceData)->txSendCalls++;
    }

    DMAControlBlocks_LoadTXBuffer(dma_cb);

    while (true)
    {
        if (!dma_cb->txDCB)
        {
            return true;
        }

        // Skip already transmitted blocks (compare KEY bits 8-10 only, not RCOST low byte)
        while (dma_cb->txDCB && DCB_GetKey(dma_cb->txDCB) == KEYFLAG_ALREADY_TRANSMITTED_BLOCK)
        {
            if (transmitter->hdlcDevice && transmitter->hdlcDevice->deviceData)
            {
                ((HDLCData *)transmitter->hdlcDevice->deviceData)->txAlreadySent++;
            }
            DMAControlBlocks_LoadNextTXBuffer(dma_cb);
        }

        if (!dma_cb->txDCB)
        {
            return true;
        }

        if (DCB_GetKey(dma_cb->txDCB) == KEYFLAG_BLOCK_TO_BE_TRANSMITTED)
        {
            // Start of new frame: clear outbound buffer
            if (DCB_HasRSOMFlag(dma_cb->txDCB))
            {
                dma_cb->outboundBufferSize = 0;
            }

            // Read all bytes from this block into outbound buffer
            uint16_t bytes_to_send = dma_cb->txDCB->byteCount + dma_cb->txDCB->displacement;
            while (dma_cb->txDCB->dmaBytesRead < bytes_to_send)
            {
                uint8_t data = DMAControlBlocks_ReadNextByteDMA(dma_cb, false);
                if (dma_cb->outboundBuffer &&
                    dma_cb->outboundBufferSize < dma_cb->outboundBufferCapacity)
                {
                    dma_cb->outboundBuffer[dma_cb->outboundBufferSize++] = data;
                }
            }

            tmp_tsb |= TTS_BLOCK_END;

            // End of frame: build HDLC frame and send it
            if (DCB_HasREOMFlag(dma_cb->txDCB))
            {
                send_outbound_frame(transmitter, dma_cb);
                dma_cb->outboundBufferSize = 0;
                tmp_tsb |= TTS_FRAME_END;
            }

            DMAControlBlocks_MarkBufferSent(dma_cb);

            if (!DMAControlBlocks_LoadNextTXBuffer(dma_cb))
            {
                tmp_tsb |= TTS_TRANSMISSION_FINISHED | TTS_LIST_END;
                dma_transmitter_set_txdma_flag(transmitter, tmp_tsb);
                dma_transmitter_set_engine_sender_state(transmitter, DMA_SENDER_STOPPED);
                return true;
            }
            else
            {
                dma_transmitter_set_txdma_flag(transmitter, tmp_tsb);
                tmp_tsb = 0;
            }
        }
        else
        {
            // End of list
            dma_transmitter_set_txdma_flag(transmitter, TTS_TRANSMISSION_FINISHED | TTS_LIST_END);
            dma_transmitter_set_engine_sender_state(transmitter, DMA_SENDER_STOPPED);
            return true;
        }
    }
}

// ---------------------------------------------------------------------------
// State management
// ---------------------------------------------------------------------------
static void dma_transmitter_set_engine_sender_state(DMATransmitter *transmitter, int state)
{
    if (!transmitter || !transmitter->dmaCB)
    {
        return;
    }

    transmitter->dmaCB->dmaSenderState = state;

    if (state == DMA_SENDER_STOPPED)
    {
        if (transmitter->hdlcDevice)
        {
            HDLCData *hdlc_data = (HDLCData *)transmitter->hdlcDevice->deviceData;
            if (hdlc_data)
            {
                hdlc_data->txTransferControl.bits.transmitterEnabled = 0;
            }
        }
        transmitter->dmaCB->txListPointerOffset = 0;
        transmitter->dmaCB->txDCB = NULL;
    }

    if (state == DMA_SENDER_BLOCK_READY_TO_SEND)
    {
        transmitter->dmaCB->dmaWaitTicks = 10; // matches C# RetroCore
    }
}

void DMATransmitter_SetSenderState(DMATransmitter *transmitter, int sender_state)
{
    if (!transmitter)
    {
        return;
    }
    dma_transmitter_set_engine_sender_state(transmitter, sender_state);

    transmitter->active = (sender_state == DMA_SENDER_BLOCK_READY_TO_SEND);
}

void DMATransmitter_SetSendFrameCallback(DMATransmitter *transmitter,
                                         DMATransmitterSendFrameCallback callback)
{
    if (!transmitter)
    {
        return;
    }
    transmitter->onSendHDLCFrame = callback;
}

void DMATransmitter_SetInterruptCallback(DMATransmitter *transmitter,
                                         DMATransmitterSetInterruptCallback callback)
{
    if (!transmitter)
    {
        return;
    }
    transmitter->onSetInterruptBit = callback;
}
