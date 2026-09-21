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

#include "dma_receiver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "dma_control_blocks.h"
#include "chip_com5025.h"
#include "device_hdlc.h"
#include "../devices_types.h"
static void dma_receiver_clear_receive_frame_state(DMAReceiver *);
static void dma_receiver_enable_hdlc_receiver(DMAReceiver *, bool);
static bool dma_receiver_find_next_receive_buffer(DMAReceiver *);
static int dma_receiver_process_buffered_data(DMAReceiver *);
static bool dma_receiver_process_complete_frame(DMAReceiver *);
static DMAReceiveStatus dma_receiver_receive_data_buffer_byte(DMAReceiver *, uint8_t);
static void dma_receiver_set_rxdma_flag(DMAReceiver *, uint16_t);


void dma_rx_init(DMAReceiver *receiver, void *com5025, DMAControlBlocks *dma_cb,
                 struct Device *hdlc_device)
{
    if (!receiver)
    {
        return;
    }
    memset(receiver, 0, sizeof(DMAReceiver));

    receiver->com5025 = (COM5025State *)com5025;
    receiver->dmaCB = dma_cb;
    receiver->hdlcDevice = hdlc_device;
    receiver->bytesReceived = 0;
    receiver->processTcpBufDelay = 0;
    receiver->onSetInterruptBit = NULL;
    receiver->callbackContext = NULL;

    rxbuf_init(&receiver->tcpReceiveBuffer, TCP_RECV_BUF_DEFAULT_CAPACITY);
}


// -------------------------------------------------------------
// Dispose
// -------------------------------------------------------------

void dma_rx_destroy(DMAReceiver *receiver)
{
    if (!receiver)
    {
        return;
    }
    rxbuf_destroy(&receiver->tcpReceiveBuffer);
    receiver->onSetInterruptBit = NULL;
}


void dma_rx_clear(DMAReceiver *receiver)
{
    if (!receiver)
    {
        return;
    }
    receiver->bytesReceived = 0;
    rxbuf_clear(&receiver->tcpReceiveBuffer);
}

// ---------------------------------------------------------------------------
// Tick: called every CPU cycle from DMAEngine_Tick.
// Processes ONE complete HDLC frame per call.
// Adaptive delay: short delay (50 ticks) when queue is backing up,
// normal delay (500 ticks) when queue is manageable.
// ---------------------------------------------------------------------------
void dma_rx_tick(DMAReceiver *receiver)
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
    int available = rxbuf_available(&receiver->tcpReceiveBuffer);
    if (available > 0)
    {
        dma_receiver_process_buffered_data(receiver);

        // Adaptive delay: process faster when queue has significant backlog
        int avail = rxbuf_available(&receiver->tcpReceiveBuffer);
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
static void stop_receiver(DMAReceiver *receiver)
{
    HDLCData *hdlc_data = (HDLCData *)receiver->hdlcDevice->deviceData;
    if (!hdlc_data)
    {
        return;
    }

    // Clear receiver active flag
    hdlc_data->rxTransferStatus.bits.receiverActive = 0;
}


// ---------------------------------------------------------------------------
// SetReceiverState: called by CommandReceiverStart / CommandReceiverContinue
// Enable the HDLC receiver and ensure DMA is ready for incoming data
// ---------------------------------------------------------------------------
void dma_rx_set_receiver_state(DMAReceiver *receiver)
{
    if (!receiver || !receiver->hdlcDevice)
    {
        return;
    }

    HDLCData *hdlc_data = (HDLCData *)receiver->hdlcDevice->deviceData;
    if (!hdlc_data)
    {
        return;
    }

    // Enable receiver DMA - this allows the receiver to process incoming data
    hdlc_data->rxTransferControl.bits.enableReceiverDMA = 1;

    // Enable the HDLC receiver hardware
    dma_receiver_enable_hdlc_receiver(receiver, true);

    // Clear any previous receiver overrun or error states
    hdlc_data->rxTransferStatus.bits.receiverOverrun = 0;
    hdlc_data->rxTransferStatus.bits.listEmpty = 0;

    // Set receiver as active and ready to receive
    hdlc_data->rxTransferStatus.bits.receiverActive = 1;

    // Find the first empty receive buffer if not already loaded
    if (receiver->dmaCB && !receiver->dmaCB->rxDCB)
    {
        dmacb_load_rx_buffer(receiver->dmaCB);
    }

    // Ensure we have a valid empty buffer to start receiving into
    dma_receiver_find_next_receive_buffer(receiver);
}

// ---------------------------------------------------------------------------
// ReceiveDataFromModem: non-blocking enqueue into ring buffer.
// Called from modem layer when TCP data arrives. Returns immediately.
// (Bypasses COM5025 chip for direct DMA receive in BLAST mode.)
// ---------------------------------------------------------------------------
void dma_rx_receive_data_from_modem(DMAReceiver *receiver, const uint8_t *data, int length)
{
    if (!receiver || !data || length <= 0)
    {
        return;
    }

    // Enqueue raw data for byte-stuffed HDLC processing
    // Data is processed asynchronously via ProcessBufferedData()
    int enqueued = rxbuf_enqueue(&receiver->tcpReceiveBuffer, data, length);

    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_TRACE))
    {
        if (enqueued < length)
        {
            log_write(LOG_CAT_HDLC, LOG_TRACE,
                      "TCP_RX_BUFFER_FULL: Only queued %d/%d bytes - buffer full!\n", enqueued,
                      length);
        }
        else
        {
            log_write(LOG_CAT_HDLC, LOG_TRACE,
                      "TCP_RX_QUEUED: %d bytes enqueued, buffer has %d bytes available\n", enqueued,
                      rxbuf_available(&receiver->tcpReceiveBuffer));
        }
    }
}

// ---------------------------------------------------------------------------
// ProcessBufferedData: pull bytes from ring buffer until ONE complete HDLC
// frame is found, then process it and return.
// Returns bytes processed (0 = incomplete frame, waiting for more data).
// ---------------------------------------------------------------------------
static int dma_receiver_process_buffered_data(DMAReceiver *receiver)
{
    if (!receiver || !receiver->hdlcDevice)
    {
        return 0;
    }

    HDLCData *hdlc_data = (HDLCData *)receiver->hdlcDevice->deviceData;
    if (!hdlc_data)
    {
        return 0;
    }

    // Check if receiver DMA is enabled
    if (!hdlc_data->rxTransferControl.bits.enableReceiverDMA)
    {
        return 0;
    }

    // Check if we have a valid RX buffer (C#: if dmaRegs.RX_DCB is null return 0)
    if (!receiver->dmaCB || !receiver->dmaCB->rxDCB)
    {
        return 0;
    }

    int max_bytes = rxbuf_available(&receiver->tcpReceiveBuffer);
    int bytes_processed = 0;

    // Pull bytes from buffer and feed to HDLC frame state machine
    // Continue until we have a complete frame OR buffer is empty
    while (bytes_processed < max_bytes)
    {

        // Ensure we have a valid DMA buffer
        if (dcb_get_key(receiver->dmaCB->rxDCB) != KEYFLAG_EMPTY_RECEIVER_BLOCK)
        {
            dmacb_load_next_rx_buffer(receiver->dmaCB);
        }

        if (!receiver->dmaCB->rxDCB ||
            dcb_get_key(receiver->dmaCB->rxDCB) != KEYFLAG_EMPTY_RECEIVER_BLOCK)
        {
            // Buffer exhausted - fire LIST_EMPTY, data remains in TCP buffer
            dma_receiver_set_rxdma_flag(receiver, RTS_RECEIVER_OVERRUN | RTS_LIST_EMPTY);
            return bytes_processed;
        }

        // Try to dequeue next byte from TCP receive buffer
        uint8_t data_byte;
        if (!rxbuf_dequeue_byte(&receiver->tcpReceiveBuffer, &data_byte))
        {
            break;
        }

        // Feed byte to HDLC frame state machine
        bool frame_complete = hdlc_frame_add_byte(receiver->dmaCB->hdlcReceiveFrame, data_byte);
        bytes_processed++;

        // Check if we have a complete frame
        if (frame_complete)
        {

            // Process the complete frame
            dma_receiver_process_complete_frame(receiver);

            // Load next RX buffer for next HDLC frame
            if (!dmacb_load_next_rx_buffer(receiver->dmaCB))
            {
                dma_receiver_set_rxdma_flag(receiver, RTS_LIST_EMPTY);
            }

            // Exit - next call will start fresh with new frame
            return bytes_processed;
        }
    }

    // Buffer empty but no complete frame yet
    return 0;
}

// ---------------------------------------------------------------------------
// ProcessCompleteFrame: validate FCS, write frame into DMA buffers, set flags.
// ---------------------------------------------------------------------------
static bool dma_receiver_process_complete_frame(DMAReceiver *receiver)
{
    if (!receiver || !receiver->dmaCB || !receiver->dmaCB->hdlcReceiveFrame)
    {
        return false;
    }

    HDLCFrame *frame = receiver->dmaCB->hdlcReceiveFrame;
    const uint8_t *frame_data = hdlc_frame_get_frame_data(frame);
    int frame_length = hdlc_frame_get_frame_length(frame);

    // Check if frame is complete and CRC is valid before writing to DMA buffers
    if (!hdlc_frame_is_crc_valid(frame))
    {
        // Failed CRC - mark buffer as received with error status and exit
        if (receiver->hdlcDevice && receiver->hdlcDevice->deviceData)
        {
            HDLCData *hdlc_data = (HDLCData *)receiver->hdlcDevice->deviceData;
            hdlc_data->framesRxErrors++;
        }

        dma_receiver_clear_receive_frame_state(receiver);
        return true; // Continue processing next frame
    }

    // GetFrameBytesNoFCS: exclude 2-byte CRC
    int data_length = frame_length - 2;
    bool write_success = true;
    for (int j = 0; j < data_length; j++)
    {
        DMAReceiveStatus rstat = dma_receiver_receive_data_buffer_byte(receiver, frame_data[j]);

        switch (rstat)
        {
        default:
        case DMA_RECEIVE_OK:
            break;

        case DMA_RECEIVE_FAILED:
            write_success = false;
            break;

        case DMA_RECEIVE_BUFFER_FULL:
            // Buffer was full BEFORE write - byte was NOT written
            // Find next buffer and retry this byte
            if (!dma_receiver_find_next_receive_buffer(receiver))
            {
                // Unable to find new empty buffer for receive
                dma_receiver_set_rxdma_flag(receiver, RTS_LIST_EMPTY | RTS_RECEIVER_OVERRUN);
                write_success = false;
            }
            j--; // Retry this byte in new buffer
            break;
        case DMA_RECEIVE_NO_BUFFER:
            write_success = false;
            break;
        }

        if (!write_success)
        {
            // Failed to write frame data into DMA buffers - exit processing
            break;
        }
    }

    if (write_success)
    {
        // Update receiver status register RSOM and REOM in COM5025 so SINTRAN can see it
        com5025_set_receiver_status(receiver->com5025,
                                    COM5025_RX_STATUS_RSOM | COM5025_RX_STATUS_REOM);

        // RSOM and REOM
        dmacb_mark_buffer_received(receiver->dmaCB, 0x03);

        // Do NOT include RTS_DATA_AVAILABLE - it's bit 0, never auto-cleared on
        // IOX+10 read, and causes permanent IRQ 13 flood via CheckTriggerInterrupt.
        // COM5025 is not clocked in DMA mode so nothing clears it.
        // SINTRAN uses DMAModuleRequest (bit 4) for DMA frame notification.
        dma_receiver_set_rxdma_flag(receiver, RTS_FRAME_END | RTS_BLOCK_END | RTS_DATA_AVAILABLE |
                                                  RTS_RECEIVER_ACTIVE | RTS_SYNC_FLAG_RECEIVED);

        // Track frame statistics
        if (receiver->hdlcDevice && receiver->hdlcDevice->deviceData)
        {
            HDLCData *hdlc_data = (HDLCData *)receiver->hdlcDevice->deviceData;
            hdlc_data->framesRx++;
        }
    }

    dma_receiver_clear_receive_frame_state(receiver);
    return write_success;
}

static void dma_receiver_clear_receive_frame_state(DMAReceiver *receiver)
{
    if (!receiver || !receiver->dmaCB || !receiver->dmaCB->hdlcReceiveFrame)
    {
        return;
    }
    hdlc_frame_reset(receiver->dmaCB->hdlcReceiveFrame);
}


void dma_rx_set_interrupt_callback(DMAReceiver *receiver, DMAReceiverSetInterruptCallback callback)
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
static bool dma_receiver_find_next_receive_buffer(DMAReceiver *receiver)
{
    if (!receiver || !receiver->dmaCB)
    {
        return false;
    }

    // Do we already have a buffer loaded and ready to receive into?
    if (receiver->dmaCB->rxDCB &&
        dcb_get_key(receiver->dmaCB->rxDCB) == KEYFLAG_EMPTY_RECEIVER_BLOCK)
    {
        return true;
    }

    // Load the next RX buffer from the list
    if (!dmacb_load_next_rx_buffer(receiver->dmaCB))
    {
        dma_receiver_set_rxdma_flag(receiver, RTS_LIST_EMPTY);

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
static DMAReceiveStatus dma_receiver_receive_data_buffer_byte(DMAReceiver *receiver, uint8_t data)
{
    if (!receiver || !receiver->dmaCB)
    {
        return DMA_RECEIVE_NO_BUFFER;
    }

    DMAControlBlocks *dma_cb = receiver->dmaCB;
    if (!dma_cb->rxDCB)
    {
        return DMA_RECEIVE_NO_BUFFER;
    }

    // Check if buffer is full BEFORE writing (matches C#: dma_bytes_written >= MaxReceiverBlockLength)
    if (dma_cb->rxDCB->dmaBytesWritten >= dma_cb->parameters->maxReceiverBlockLength)
    {

        // Buffer is full - mark with RSOM only (frame continues to next buffer)
        dmacb_mark_buffer_received(dma_cb, 0x01);

        // Tell ND that Block has ended (but FRAME is not yet ended)
        dma_receiver_set_rxdma_flag(receiver, RTS_BLOCK_END | RTS_RECEIVER_ACTIVE |
                                                  RTS_SYNC_FLAG_RECEIVED | RTS_DATA_AVAILABLE);
        dma_cb->rxDCB = NULL;
        return DMA_RECEIVE_BUFFER_FULL; // Caller must find new buffer and retry this byte
    }

    // Buffer has space - write the byte
    if (dcb_get_key(dma_cb->rxDCB) == KEYFLAG_EMPTY_RECEIVER_BLOCK)
    {
        dmacb_write_next_byte_dma(dma_cb, data, true);
        receiver->bytesReceived++;
    }

    return DMA_RECEIVE_OK;
}


// ---------------------------------------------------------------------------
// SetRXDMAFlag: set flags and raise interrupt on level 13
// ---------------------------------------------------------------------------
static void dma_receiver_set_rxdma_flag(DMAReceiver *receiver, uint16_t flag)
{
    if (!receiver || !receiver->hdlcDevice)
    {
        return;
    }

    HDLCData *hdlc_data = (HDLCData *)receiver->hdlcDevice->deviceData;
    if (!hdlc_data)
    {
        return;
    }

    // LIST_EMPTY: stop the DMA receiver completely.
    // No more processing until SINTRAN issues RECEIVER_CONTINUE.
    if (flag & RTS_LIST_EMPTY)
    {
        stop_receiver(receiver);
    }

    // 2. Add SD/DSR flags (burst mode always active)
    flag |= RTS_SIGNAL_DETECTOR | RTS_DATA_SET_READY;

    // 3. Check if next buffer is available; if not, force LIST_EMPTY
    if (receiver->dmaCB && !dmacb_is_next_r_xbuf_valid(receiver->dmaCB))
    {
        flag |= RTS_LIST_EMPTY;
    }

    hdlc_data->rxTransferStatus.raw |= flag;

    // C# checks the INPUT flag, not the accumulated status register.
    // This prevents stale listEmpty from previous calls triggering DMAModuleRequest.
    if (flag & RTS_LIST_EMPTY)
    {
        // List empty always triggers DMA request
        hdlc_data->rxTransferStatus.bits.dmaModuleRequest = 1;
    }
    if (hdlc_data->rxTransferControl.bits.blockEndIE && hdlc_data->rxTransferStatus.bits.blockEnd)
    {
        hdlc_data->rxTransferStatus.bits.dmaModuleRequest = 1;
    }
    if (hdlc_data->rxTransferControl.bits.frameEndIE && hdlc_data->rxTransferStatus.bits.frameEnd)
    {
        hdlc_data->rxTransferStatus.bits.dmaModuleRequest = 1;
    }
    if (hdlc_data->rxTransferControl.bits.listEndIE && hdlc_data->rxTransferStatus.bits.listEnd)
    {
        hdlc_data->rxTransferStatus.bits.dmaModuleRequest = 1;
    }

    if (hdlc_data->rxTransferControl.bits.dmaModuleIE &&
        hdlc_data->rxTransferStatus.bits.dmaModuleRequest)
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
static void dma_receiver_enable_hdlc_receiver(DMAReceiver *receiver, bool enable)
{
    if (!receiver || !receiver->hdlcDevice || !receiver->com5025)
    {
        return;
    }

    HDLCData *hdlc_data = (HDLCData *)receiver->hdlcDevice->deviceData;
    if (!hdlc_data)
    {
        return;
    }

    hdlc_data->rxTransferControl.bits.enableReceiver = enable ? 1 : 0;
    com5025_set_input_pin(receiver->com5025, COM5025_PIN_IN_RXENA, enable);
}
