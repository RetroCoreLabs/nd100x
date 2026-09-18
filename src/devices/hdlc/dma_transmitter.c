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

#include "dma_transmitter.h"
#include "dma_control_blocks.h"
#include "chip_com5025.h"
#include "dma_engine.h"
#include "dma_enum.h"
#include "device_hdlc.h"
#include "hdlc_frame.h"
#include "../devices_types.h"


void DMATransmitter_Init(DMATransmitter *transmitter, void *com5025, DMAControlBlocks *dmaCB, struct Device *hdlcDevice)
{
    if (!transmitter) return;
    memset(transmitter, 0, sizeof(DMATransmitter));

    transmitter->com5025 = (COM5025State *)com5025;
    transmitter->dmaCB = dmaCB;
    transmitter->hdlcDevice = hdlcDevice;
    transmitter->active = false;
    transmitter->bytesSent = 0;
    transmitter->onSendHDLCFrame = NULL;
    transmitter->onSetInterruptBit = NULL;
    transmitter->callbackContext = NULL;
}

void DMATransmitter_Destroy(DMATransmitter *transmitter)
{
    if (!transmitter) return;
    transmitter->onSendHDLCFrame = NULL;
    transmitter->onSetInterruptBit = NULL;
}

void DMATransmitter_Clear(DMATransmitter *transmitter)
{
    if (!transmitter) return;
    transmitter->active = false;
    transmitter->bytesSent = 0;
}

// ---------------------------------------------------------------------------
// Tick: burst mode transmit state machine.
// Waits for DMA+TX enabled, rate-limits with dmaWaitTicks, sends all buffers.
// ---------------------------------------------------------------------------
void DMATransmitter_Tick(DMATransmitter *transmitter)
{
    if (!transmitter || !transmitter->dmaCB || !transmitter->hdlcDevice) return;

    HDLCData *hdlcData = (HDLCData *)transmitter->hdlcDevice->deviceData;
    if (!hdlcData) return;

    DMAControlBlocks *dmaCB = transmitter->dmaCB;

    switch (dmaCB->dmaSenderState) {
        case DMA_SENDER_STOPPED:
            break;

        case DMA_SENDER_BLOCK_READY_TO_SEND:
            if (hdlcData->txTransferControl.bits.transmitterEnabled &&
                hdlcData->txTransferControl.bits.enableTransmitterDMA) {

                if (dmaCB->dmaWaitTicks > 0) {
                    dmaCB->dmaWaitTicks--;
                }

                if (dmaCB->dmaWaitTicks == 0) {
                    if (DMATransmitter_SendAllBuffers(transmitter)) {
                        dmaCB->dmaWaitTicks = -1; // disable
                    } else {
                        dmaCB->dmaWaitTicks = 50; // Wait before next block
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
void DMATransmitter_SetTXDMAFlag(DMATransmitter *transmitter, uint16_t flag)
{
    if (!transmitter || !transmitter->hdlcDevice) return;

    HDLCData *hdlcData = (HDLCData *)transmitter->hdlcDevice->deviceData;
    if (!hdlcData) return;

    hdlcData->txTransferStatus.raw |= flag;

    if (flag & TTS_TRANSMISSION_FINISHED) {
        hdlcData->txTransferStatus.bits.dmaModuleRequest = 1;
    }
    if (hdlcData->txTransferControl.bits.blockEndIE && hdlcData->txTransferStatus.bits.blockEnd) {
        hdlcData->txTransferStatus.bits.dmaModuleRequest = 1;
    }
    if (hdlcData->txTransferControl.bits.frameEndIE && hdlcData->txTransferStatus.bits.frameEnd) {
        hdlcData->txTransferStatus.bits.dmaModuleRequest = 1;
    }
    if (hdlcData->txTransferControl.bits.listEndIE && hdlcData->txTransferStatus.bits.listEnd) {
        hdlcData->txTransferStatus.bits.dmaModuleRequest = 1;
    }

    if (hdlcData->txTransferControl.bits.dmaModuleIE &&
        hdlcData->txTransferStatus.bits.dmaModuleRequest) {
        if (transmitter->onSetInterruptBit) {
            transmitter->onSetInterruptBit(transmitter->callbackContext, 12);
        }
    }
}

// ---------------------------------------------------------------------------
// SendAllBuffers: read TX DCBs, accumulate frames, send complete HDLC frames.
// Returns true when all buffers have been sent.
// ---------------------------------------------------------------------------
bool DMATransmitter_SendAllBuffers(DMATransmitter *transmitter)
{
    if (!transmitter || !transmitter->dmaCB) return true;

    DMAControlBlocks *dmaCB = transmitter->dmaCB;
    uint16_t tmpTSB = 0;

    if (dmaCB->txListPointer == 0) return true;

    // Track call count
    if (transmitter->hdlcDevice && transmitter->hdlcDevice->deviceData) {
        ((HDLCData *)transmitter->hdlcDevice->deviceData)->txSendCalls++;
    }

    DMAControlBlocks_LoadTXBuffer(dmaCB);

    while (true) {
        if (!dmaCB->txDCB) return true;

        // Skip already transmitted blocks (compare KEY bits 8-10 only, not RCOST low byte)
        while (dmaCB->txDCB &&
               DCB_GetKey(dmaCB->txDCB) == KEYFLAG_ALREADY_TRANSMITTED_BLOCK) {
            if (transmitter->hdlcDevice && transmitter->hdlcDevice->deviceData) {
                ((HDLCData *)transmitter->hdlcDevice->deviceData)->txAlreadySent++;
            }
            DMAControlBlocks_LoadNextTXBuffer(dmaCB);
        }

        if (!dmaCB->txDCB) return true;

        if (DCB_GetKey(dmaCB->txDCB) == KEYFLAG_BLOCK_TO_BE_TRANSMITTED) {
            // Start of new frame: clear outbound buffer
            if (DCB_HasRSOMFlag(dmaCB->txDCB)) {
                dmaCB->outboundBufferSize = 0;
            }

            // Read all bytes from this block into outbound buffer
            uint16_t bytesToSend = dmaCB->txDCB->byteCount + dmaCB->txDCB->displacement;
            while (dmaCB->txDCB->dmaBytesRead < bytesToSend) {
                uint8_t data = DMAControlBlocks_ReadNextByteDMA(dmaCB, false);
                if (dmaCB->outboundBuffer && dmaCB->outboundBufferSize < dmaCB->outboundBufferCapacity) {
                    dmaCB->outboundBuffer[dmaCB->outboundBufferSize++] = data;
                }
            }

            tmpTSB |= TTS_BLOCK_END;

            // End of frame: build HDLC frame and send it
            if (DCB_HasREOMFlag(dmaCB->txDCB)) {
                if (dmaCB->outboundBuffer && dmaCB->outboundBufferSize > 0 &&
                    transmitter->onSendHDLCFrame) {
                    uint8_t frameBuffer[HDLC_MAX_FRAME_SIZE + 10];
                    int frameLength = HDLCFrame_BuildFrame(dmaCB->outboundBuffer,
                                                          dmaCB->outboundBufferSize,
                                                          frameBuffer, sizeof(frameBuffer));
                    if (frameLength > 0) {
                        // Record in TX history
                        if (transmitter->hdlcDevice && transmitter->hdlcDevice->deviceData) {
                            HDLCData *hd = (HDLCData *)transmitter->hdlcDevice->deviceData;
                            int idx = hd->txHistoryIdx % HDLC_TX_HISTORY_SIZE;
                            hd->txHistory[idx].listPtr = DCB_GetBufferAddress(dmaCB->txDCB);
                            hd->txHistory[idx].dataAddr = DCB_GetDataMemoryAddress(dmaCB->txDCB);
                            hd->txHistory[idx].byteCount = dmaCB->txDCB->byteCount;
                            hd->txHistory[idx].keyBefore = DCB_GetKeyValue(dmaCB->txDCB);
                            hd->txHistory[idx].frameSize = (uint16_t)frameLength;
                            int copyLen = frameLength < HDLC_TX_HISTORY_DATA_SIZE ? frameLength : HDLC_TX_HISTORY_DATA_SIZE;
                            memcpy(hd->txHistory[idx].data, frameBuffer, (size_t)copyLen);
                            hd->txHistory[idx].dataLen = (uint8_t)copyLen;
                            hd->txHistoryIdx++;
                        }

                        HDLCFrame callbackFrame;
                        HDLCFrame_Init(&callbackFrame);
                        memcpy(callbackFrame.frameBuffer, frameBuffer, (size_t)frameLength);
                        callbackFrame.frameLength = frameLength;
                        callbackFrame.frameComplete = true;
                        transmitter->onSendHDLCFrame(transmitter->callbackContext, &callbackFrame);
                    }
                }
                dmaCB->outboundBufferSize = 0;
                tmpTSB |= TTS_FRAME_END;
            }

            DMAControlBlocks_MarkBufferSent(dmaCB);

            if (!DMAControlBlocks_LoadNextTXBuffer(dmaCB)) {
                tmpTSB |= TTS_TRANSMISSION_FINISHED | TTS_LIST_END;
                DMATransmitter_SetTXDMAFlag(transmitter, tmpTSB);
                DMATransmitter_SetEngineSenderState(transmitter, DMA_SENDER_STOPPED);
                return true;
            } else {
                DMATransmitter_SetTXDMAFlag(transmitter, tmpTSB);
                tmpTSB = 0;
            }
        } else {
            // End of list
            DMATransmitter_SetTXDMAFlag(transmitter, TTS_TRANSMISSION_FINISHED | TTS_LIST_END);
            DMATransmitter_SetEngineSenderState(transmitter, DMA_SENDER_STOPPED);
            return true;
        }
    }
}

// ---------------------------------------------------------------------------
// State management
// ---------------------------------------------------------------------------
void DMATransmitter_SetEngineSenderState(DMATransmitter *transmitter, int state)
{
    if (!transmitter || !transmitter->dmaCB) return;

    transmitter->dmaCB->dmaSenderState = state;

    if (state == DMA_SENDER_STOPPED) {
        if (transmitter->hdlcDevice) {
            HDLCData *hdlcData = (HDLCData *)transmitter->hdlcDevice->deviceData;
            if (hdlcData) {
                hdlcData->txTransferControl.bits.transmitterEnabled = 0;
            }
        }
        transmitter->dmaCB->txListPointerOffset = 0;
        transmitter->dmaCB->txDCB = NULL;
    }

    if (state == DMA_SENDER_BLOCK_READY_TO_SEND) {
        transmitter->dmaCB->dmaWaitTicks = 10; // matches C# RetroCore
    }
}

void DMATransmitter_SetSenderState(DMATransmitter *transmitter, int senderState)
{
    if (!transmitter) return;
    DMATransmitter_SetEngineSenderState(transmitter, senderState);

    transmitter->active = (senderState == DMA_SENDER_BLOCK_READY_TO_SEND);
}

void DMATransmitter_SetSendFrameCallback(DMATransmitter *transmitter, DMATransmitterSendFrameCallback callback)
{
    if (!transmitter) return;
    transmitter->onSendHDLCFrame = callback;
}

void DMATransmitter_SetInterruptCallback(DMATransmitter *transmitter, DMATransmitterSetInterruptCallback callback)
{
    if (!transmitter) return;
    transmitter->onSetInterruptBit = callback;
}
