/*
 * dma_receiver.c - HDLC DMA receiver: moves received frames from the modem into memory.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "dma_receiver.h"
#include "dma_control_blocks.h"
#include "chip_com5025.h"
#include "device_hdlc.h"
#include "../devices_types.h"


void DMAReceiver_Init(DMAReceiver *receiver, void *com5025, DMAControlBlocks *dmaCB,
                      struct Device *hdlcDevice)
{
    if (!receiver)
    {
        return;
    }
    memset(receiver, 0, sizeof(DMAReceiver));

    receiver->com5025 = (COM5025State *)com5025;
    receiver->dmaCB = dmaCB;
    receiver->hdlcDevice = hdlcDevice;
    receiver->bytesReceived = 0;
    receiver->processTcpBufDelay = 0;
    receiver->onSetInterruptBit = NULL;
    receiver->callbackContext = NULL;

    TcpReceiveBuffer_Init(&receiver->tcpReceiveBuffer, TCP_RECV_BUF_DEFAULT_CAPACITY);
}


// -------------------------------------------------------------
// Dispose
// -------------------------------------------------------------

void DMAReceiver_Destroy(DMAReceiver *receiver)
{
    if (!receiver)
    {
        return;
    }
    TcpReceiveBuffer_Destroy(&receiver->tcpReceiveBuffer);
    receiver->onSetInterruptBit = NULL;
}


void DMAReceiver_Clear(DMAReceiver *receiver)
{
    if (!receiver)
    {
        return;
    }
    receiver->bytesReceived = 0;
    TcpReceiveBuffer_Clear(&receiver->tcpReceiveBuffer);
}

// ---------------------------------------------------------------------------
// Tick: called every CPU cycle from DMAEngine_Tick.
// Processes ONE complete HDLC frame per call.
// Adaptive delay: short delay (50 ticks) when queue is backing up,
// normal delay (500 ticks) when queue is manageable.
// ---------------------------------------------------------------------------
void DMAReceiver_Tick(DMAReceiver *receiver)
{
    if (!receiver)
    {
        return;
    }

    // Simple delay to avoid busy looping when no data available
    if (receiver->processTcpBufDelay > 0)
    {
        receiver->processTcpBufDelay--;
        return;
    }

    // Pull and process one packet from buffer
    // ProcessBufferedData returns:
    //   0 = no complete packet yet (waiting for more data)
    //   >0 = packet processed successfully
    int available = TcpReceiveBuffer_Available(&receiver->tcpReceiveBuffer);
    if (available > 0)
    {
        DMAReceiver_ProcessBufferedData(receiver);

        // Adaptive delay: process faster when queue has significant backlog
        int avail = TcpReceiveBuffer_Available(&receiver->tcpReceiveBuffer);
        if (avail > 32768)
        {
            receiver->processTcpBufDelay = 50; // >32KB queued: minimal delay
        }
        else if (avail > 8192)
        {
            receiver->processTcpBufDelay = 200; // >8KB queued: reduced delay
        }
        else
        {
            receiver->processTcpBufDelay = 500; // Low queue: normal delay
        }
    }
}


// ---------------------------------------------------------------------------
// Stop the receiver when we have ListEmpty situation
// ---------------------------------------------------------------------------
static void StopReceiver(DMAReceiver *receiver)
{
    HDLCData *hdlcData = (HDLCData *)receiver->hdlcDevice->deviceData;
    if (!hdlcData)
    {
        return;
    }

    // Clear receiver active flag
    hdlcData->rxTransferStatus.bits.receiverActive = 0;
}


// ---------------------------------------------------------------------------
// SetReceiverState: called by CommandReceiverStart / CommandReceiverContinue
// Enable the HDLC receiver and ensure DMA is ready for incoming data
// ---------------------------------------------------------------------------
void DMAReceiver_SetReceiverState(DMAReceiver *receiver)
{
    if (!receiver || !receiver->hdlcDevice)
    {
        return;
    }

    HDLCData *hdlcData = (HDLCData *)receiver->hdlcDevice->deviceData;
    if (!hdlcData)
    {
        return;
    }

    // Enable receiver DMA - this allows the receiver to process incoming data
    hdlcData->rxTransferControl.bits.enableReceiverDMA = 1;

    // Enable the HDLC receiver hardware
    DMAReceiver_EnableHDLCReceiver(receiver, true);

    // Clear any previous receiver overrun or error states
    hdlcData->rxTransferStatus.bits.receiverOverrun = 0;
    hdlcData->rxTransferStatus.bits.listEmpty = 0;

    // Set receiver as active and ready to receive
    hdlcData->rxTransferStatus.bits.receiverActive = 1;

    // Find the first empty receive buffer if not already loaded
    if (receiver->dmaCB && !receiver->dmaCB->rxDCB)
    {
        DMAControlBlocks_LoadRXBuffer(receiver->dmaCB);
    }

    // Ensure we have a valid empty buffer to start receiving into
    DMAReceiver_FindNextReceiveBuffer(receiver);
}

// ---------------------------------------------------------------------------
// ReceiveDataFromModem: non-blocking enqueue into ring buffer.
// Called from modem layer when TCP data arrives. Returns immediately.
// (Bypasses COM5025 chip for direct DMA receive in BLAST mode.)
// ---------------------------------------------------------------------------
void DMAReceiver_ReceiveDataFromModem(DMAReceiver *receiver, const uint8_t *data, int length)
{
    if (!receiver || !data || length <= 0)
    {
        return;
    }

    // Enqueue raw data for byte-stuffed HDLC processing
    // Data is processed asynchronously via ProcessBufferedData()
    int enqueued = TcpReceiveBuffer_Enqueue(&receiver->tcpReceiveBuffer, data, length);

    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_TRACE))
    {
        if (enqueued < length)
        {
            Log_Write(LOG_CAT_HDLC, LOG_TRACE,
                      "TCP_RX_BUFFER_FULL: Only queued %d/%d bytes - buffer full!\n", enqueued,
                      length);
        }
        else
        {
            Log_Write(LOG_CAT_HDLC, LOG_TRACE,
                      "TCP_RX_QUEUED: %d bytes enqueued, buffer has %d bytes available\n", enqueued,
                      TcpReceiveBuffer_Available(&receiver->tcpReceiveBuffer));
        }
    }
}

// ---------------------------------------------------------------------------
// ProcessBufferedData: pull bytes from ring buffer until ONE complete HDLC
// frame is found, then process it and return.
// Returns bytes processed (0 = incomplete frame, waiting for more data).
// ---------------------------------------------------------------------------
int DMAReceiver_ProcessBufferedData(DMAReceiver *receiver)
{
    if (!receiver || !receiver->hdlcDevice)
    {
        return 0;
    }

    HDLCData *hdlcData = (HDLCData *)receiver->hdlcDevice->deviceData;
    if (!hdlcData)
    {
        return 0;
    }

    // Check if receiver DMA is enabled
    if (!hdlcData->rxTransferControl.bits.enableReceiverDMA)
    {
        return 0;
    }

    // Check if we have a valid RX buffer (C#: if dmaRegs.RX_DCB is null return 0)
    if (!receiver->dmaCB || !receiver->dmaCB->rxDCB)
    {
        return 0;
    }

    int maxBytes = TcpReceiveBuffer_Available(&receiver->tcpReceiveBuffer);
    int bytesProcessed = 0;

    // Pull bytes from buffer and feed to HDLC frame state machine
    // Continue until we have a complete frame OR buffer is empty
    while (bytesProcessed < maxBytes)
    {

        // Ensure we have a valid DMA buffer
        if (DCB_GetKey(receiver->dmaCB->rxDCB) != KEYFLAG_EMPTY_RECEIVER_BLOCK)
        {
            DMAControlBlocks_LoadNextRXBuffer(receiver->dmaCB);
        }

        if (!receiver->dmaCB->rxDCB ||
            DCB_GetKey(receiver->dmaCB->rxDCB) != KEYFLAG_EMPTY_RECEIVER_BLOCK)
        {
            // Buffer exhausted - fire LIST_EMPTY, data remains in TCP buffer
            DMAReceiver_SetRXDMAFlag(receiver, RTS_RECEIVER_OVERRUN | RTS_LIST_EMPTY);
            return bytesProcessed;
        }

        // Try to dequeue next byte from TCP receive buffer
        uint8_t dataByte;
        if (!TcpReceiveBuffer_DequeueByte(&receiver->tcpReceiveBuffer, &dataByte))
        {
            break;
        }

        // Feed byte to HDLC frame state machine
        bool frameComplete = HDLCFrame_AddByte(receiver->dmaCB->hdlcReceiveFrame, dataByte);
        bytesProcessed++;

        // Check if we have a complete frame
        if (frameComplete)
        {

            // Process the complete frame
            DMAReceiver_ProcessCompleteFrame(receiver);

            // Load next RX buffer for next HDLC frame
            if (!DMAControlBlocks_LoadNextRXBuffer(receiver->dmaCB))
            {
                DMAReceiver_SetRXDMAFlag(receiver, RTS_LIST_EMPTY);
            }

            // Exit - next call will start fresh with new frame
            return bytesProcessed;
        }
    }

    // Buffer empty but no complete frame yet
    return 0;
}

// ---------------------------------------------------------------------------
// ProcessCompleteFrame: validate FCS, write frame into DMA buffers, set flags.
// ---------------------------------------------------------------------------
bool DMAReceiver_ProcessCompleteFrame(DMAReceiver *receiver)
{
    if (!receiver || !receiver->dmaCB || !receiver->dmaCB->hdlcReceiveFrame)
    {
        return false;
    }

    HDLCFrame *frame = receiver->dmaCB->hdlcReceiveFrame;
    const uint8_t *frameData = HDLCFrame_GetFrameData(frame);
    int frameLength = HDLCFrame_GetFrameLength(frame);

    // Check if frame is complete and CRC is valid before writing to DMA buffers
    if (!HDLCFrame_IsCRCValid(frame))
    {
        // Failed CRC - mark buffer as received with error status and exit
        if (receiver->hdlcDevice && receiver->hdlcDevice->deviceData)
        {
            HDLCData *hdlcData = (HDLCData *)receiver->hdlcDevice->deviceData;
            hdlcData->framesRxErrors++;
        }

        DMAReceiver_ClearReceiveFrameState(receiver);
        return true; // Continue processing next frame
    }

    // GetFrameBytesNoFCS: exclude 2-byte CRC
    int dataLength = frameLength - 2;
    bool writeSuccess = true;
    for (int j = 0; j < dataLength; j++)
    {
        DMAReceiveStatus rstat = DMAReceiver_ReceiveDataBufferByte(receiver, frameData[j]);

        switch (rstat)
        {
        default:
        case DMA_RECEIVE_OK:
            break;

        case DMA_RECEIVE_FAILED:
            writeSuccess = false;
            break;

        case DMA_RECEIVE_BUFFER_FULL:
            // Buffer was full BEFORE write - byte was NOT written
            // Find next buffer and retry this byte
            if (!DMAReceiver_FindNextReceiveBuffer(receiver))
            {
                // Unable to find new empty buffer for receive
                DMAReceiver_SetRXDMAFlag(receiver, RTS_LIST_EMPTY | RTS_RECEIVER_OVERRUN);
                writeSuccess = false;
            }
            j--; // Retry this byte in new buffer
            break;
        case DMA_RECEIVE_NO_BUFFER:
            writeSuccess = false;
            break;
        }

        if (!writeSuccess)
        {
            // Failed to write frame data into DMA buffers - exit processing
            break;
        }
    }

    if (writeSuccess)
    {
        // Update receiver status register RSOM and REOM in COM5025 so SINTRAN can see it
        COM5025_SetReceiverStatus(receiver->com5025,
                                  COM5025_RX_STATUS_RSOM | COM5025_RX_STATUS_REOM);

        // RSOM and REOM
        DMAControlBlocks_MarkBufferReceived(receiver->dmaCB, 0x03);

        // Do NOT include RTS_DATA_AVAILABLE - it's bit 0, never auto-cleared on
        // IOX+10 read, and causes permanent IRQ 13 flood via CheckTriggerInterrupt.
        // COM5025 is not clocked in DMA mode so nothing clears it.
        // SINTRAN uses DMAModuleRequest (bit 4) for DMA frame notification.
        DMAReceiver_SetRXDMAFlag(receiver, RTS_FRAME_END | RTS_BLOCK_END | RTS_DATA_AVAILABLE |
                                               RTS_RECEIVER_ACTIVE | RTS_SYNC_FLAG_RECEIVED);

        // Track frame statistics
        if (receiver->hdlcDevice && receiver->hdlcDevice->deviceData)
        {
            HDLCData *hdlcData = (HDLCData *)receiver->hdlcDevice->deviceData;
            hdlcData->framesRx++;
        }
    }

    DMAReceiver_ClearReceiveFrameState(receiver);
    return writeSuccess;
}

void DMAReceiver_ClearReceiveFrameState(DMAReceiver *receiver)
{
    if (!receiver || !receiver->dmaCB || !receiver->dmaCB->hdlcReceiveFrame)
    {
        return;
    }
    HDLCFrame_Reset(receiver->dmaCB->hdlcReceiveFrame);
}


void DMAReceiver_SetInterruptCallback(DMAReceiver *receiver,
                                      DMAReceiverSetInterruptCallback callback)
{
    if (!receiver)
    {
        return;
    }
    receiver->onSetInterruptBit = callback;
}

// ---------------------------------------------------------------------------
// Buffer management
// ---------------------------------------------------------------------------
bool DMAReceiver_FindNextReceiveBuffer(DMAReceiver *receiver)
{
    if (!receiver || !receiver->dmaCB)
    {
        return false;
    }

    // Do we already have a buffer loaded and ready to receive into?
    if (receiver->dmaCB->rxDCB &&
        DCB_GetKey(receiver->dmaCB->rxDCB) == KEYFLAG_EMPTY_RECEIVER_BLOCK)
    {
        return true;
    }

    // Load the next RX buffer from the list
    if (!DMAControlBlocks_LoadNextRXBuffer(receiver->dmaCB))
    {
        DMAReceiver_SetRXDMAFlag(receiver, RTS_LIST_EMPTY);

        // No more buffers available
        return false; // we are done here, received data will be lost
    }
    return true;
}

// ---------------------------------------------------------------------------
// Receive a single byte via DMA into the current receive buffer.
// Returns BUFFER_FULL if buffer is full BEFORE writing - caller must find new buffer and retry.
//
// returns: OK if written, BUFFER_FULL if no space (byte NOT written), NO_BUFFER if no DCB
// ---------------------------------------------------------------------------
DMAReceiveStatus DMAReceiver_ReceiveDataBufferByte(DMAReceiver *receiver, uint8_t data)
{
    if (!receiver || !receiver->dmaCB)
    {
        return DMA_RECEIVE_NO_BUFFER;
    }

    DMAControlBlocks *dmaCB = receiver->dmaCB;
    if (!dmaCB->rxDCB)
    {
        return DMA_RECEIVE_NO_BUFFER;
    }

    // Check if buffer is full BEFORE writing (matches C#: dma_bytes_written >= MaxReceiverBlockLength)
    if (dmaCB->rxDCB->dmaBytesWritten >= dmaCB->parameters->maxReceiverBlockLength)
    {

        // Buffer is full - mark with RSOM only (frame continues to next buffer)
        DMAControlBlocks_MarkBufferReceived(dmaCB, 0x01);

        // Tell ND that Block has ended (but FRAME is not yet ended)
        DMAReceiver_SetRXDMAFlag(receiver, RTS_BLOCK_END | RTS_RECEIVER_ACTIVE |
                                               RTS_SYNC_FLAG_RECEIVED | RTS_DATA_AVAILABLE);
        dmaCB->rxDCB = NULL;
        return DMA_RECEIVE_BUFFER_FULL; // Caller must find new buffer and retry this byte
    }

    // Buffer has space - write the byte
    if (DCB_GetKey(dmaCB->rxDCB) == KEYFLAG_EMPTY_RECEIVER_BLOCK)
    {
        DMAControlBlocks_WriteNextByteDMA(dmaCB, data, true);
        receiver->bytesReceived++;
    }

    return DMA_RECEIVE_OK;
}


// ---------------------------------------------------------------------------
// SetRXDMAFlag: set flags and raise interrupt on level 13
// ---------------------------------------------------------------------------
void DMAReceiver_SetRXDMAFlag(DMAReceiver *receiver, uint16_t flag)
{
    if (!receiver || !receiver->hdlcDevice)
    {
        return;
    }

    HDLCData *hdlcData = (HDLCData *)receiver->hdlcDevice->deviceData;
    if (!hdlcData)
    {
        return;
    }

    // LIST_EMPTY: stop the DMA receiver completely.
    // No more processing until SINTRAN issues RECEIVER_CONTINUE.
    if (flag & RTS_LIST_EMPTY)
    {
        StopReceiver(receiver);
    }

    // 2. Add SD/DSR flags (burst mode always active)
    flag |= RTS_SIGNAL_DETECTOR | RTS_DATA_SET_READY;

    // 3. Check if next buffer is available; if not, force LIST_EMPTY
    if (receiver->dmaCB && !DMAControlBlocks_IsNextRXbufValid(receiver->dmaCB))
    {
        flag |= RTS_LIST_EMPTY;
    }

    hdlcData->rxTransferStatus.raw |= flag;

    // C# checks the INPUT flag, not the accumulated status register.
    // This prevents stale listEmpty from previous calls triggering DMAModuleRequest.
    if (flag & RTS_LIST_EMPTY)
    {
        // List empty always triggers DMA request
        hdlcData->rxTransferStatus.bits.dmaModuleRequest = 1;
    }
    if (hdlcData->rxTransferControl.bits.blockEndIE && hdlcData->rxTransferStatus.bits.blockEnd)
    {
        hdlcData->rxTransferStatus.bits.dmaModuleRequest = 1;
    }
    if (hdlcData->rxTransferControl.bits.frameEndIE && hdlcData->rxTransferStatus.bits.frameEnd)
    {
        hdlcData->rxTransferStatus.bits.dmaModuleRequest = 1;
    }
    if (hdlcData->rxTransferControl.bits.listEndIE && hdlcData->rxTransferStatus.bits.listEnd)
    {
        hdlcData->rxTransferStatus.bits.dmaModuleRequest = 1;
    }

    if (hdlcData->rxTransferControl.bits.dmaModuleIE &&
        hdlcData->rxTransferStatus.bits.dmaModuleRequest)
    {
        if (receiver->onSetInterruptBit)
        {
            receiver->onSetInterruptBit(receiver->callbackContext, 13);
        }
    }
}

// ---------------------------------------------------------------------------
// Enable or disable the HDLC receiver hardware (RXENA pin)
// ---------------------------------------------------------------------------
void DMAReceiver_EnableHDLCReceiver(DMAReceiver *receiver, bool enable)
{
    if (!receiver || !receiver->hdlcDevice || !receiver->com5025)
    {
        return;
    }

    HDLCData *hdlcData = (HDLCData *)receiver->hdlcDevice->deviceData;
    if (!hdlcData)
    {
        return;
    }

    hdlcData->rxTransferControl.bits.enableReceiver = enable ? 1 : 0;
    COM5025_SetInputPin(receiver->com5025, COM5025_PIN_IN_RXENA, enable);
}
