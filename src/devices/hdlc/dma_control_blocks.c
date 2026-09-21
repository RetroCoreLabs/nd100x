/*
 * dma_control_blocks.c - HDLC DMA control blocks: TX/RX buffer list handling in memory.
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

#include "dma_control_blocks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#include "dma_param_buf.h"
#include "hdlc_frame.h"
#include "dma_dcb.h"
#include "../devices_types.h"
#include "device_hdlc.h"
#include "dma_enum.h"
#include "hdlc_constants.h"

// Debug flags (convert from C# #define)

// Static function declarations
static void dma_control_blocks_dma_write(DMAControlBlocks *dma_cb, uint32_t address, uint16_t data);
static int dma_control_blocks_dma_read(DMAControlBlocks *dma_cb, uint32_t address);
static void dma_control_blocks_log(DMAControlBlocks *dma_cb, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

void dmacb_init(DMAControlBlocks *dcbs, struct Device *hdlc_device)
{
    if (!dcbs)
    {
        return;
    }

    memset(dcbs, 0, sizeof(DMAControlBlocks));

    // Store the device reference
    dcbs->hdlcDevice = hdlc_device;

    // Initialize outbound buffer
    dcbs->outboundBufferCapacity = HDLC_MAX_FRAME_SIZE + 64;
    dcbs->outboundBuffer = malloc(dcbs->outboundBufferCapacity);
    if (!dcbs->outboundBuffer)
    {
        dcbs->outboundBufferCapacity = 0; /* the transmitter tests it */
    }
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
    if (dcbs->hdlcReceiveFrame)
    {
        hdlc_frame_init(dcbs->hdlcReceiveFrame);
    }

    // Initialize DMA state machine (these constants need to be defined)
    dcbs->dmaSenderState = 0;    // DMA_SENDER_STOPPED
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

void dmacb_destroy(DMAControlBlocks *dma_cb)
{
    if (!dma_cb)
    {
        return;
    }

    // Free outbound buffer
    if (dma_cb->outboundBuffer)
    {
        free(dma_cb->outboundBuffer);
        dma_cb->outboundBuffer = NULL;
    }

    // Free DCBs
    if (dma_cb->txDCB)
    {
        free(dma_cb->txDCB);
        dma_cb->txDCB = NULL;
    }
    if (dma_cb->rxDCB)
    {
        free(dma_cb->rxDCB);
        dma_cb->rxDCB = NULL;
    }

    // Free HDLC frame
    if (dma_cb->hdlcReceiveFrame)
    {
        free(dma_cb->hdlcReceiveFrame);
        dma_cb->hdlcReceiveFrame = NULL;
    }

    memset(dma_cb, 0, sizeof(DMAControlBlocks));
}

void dmacb_clear(DMAControlBlocks *dma_cb)
{
    if (!dma_cb)
    {
        return;
    }

    // Free existing DCBs
    if (dma_cb->txDCB)
    {
        free(dma_cb->txDCB);
        dma_cb->txDCB = NULL;
    }
    if (dma_cb->rxDCB)
    {
        free(dma_cb->rxDCB);
        dma_cb->rxDCB = NULL;
    }

    // Reset list pointers
    dma_cb->txListPointer = 0;
    dma_cb->txListPointerOffset = 0;
    dma_cb->rxListPointer = 0;
    dma_cb->rxListPointerOffset = 0;

    // Clear outbound buffer
    dma_cb->outboundBufferSize = 0;

    // Clear parameters would go here if ParameterBuffer was implemented
}

// TX Functions

void dmacb_set_tx_pointer(DMAControlBlocks *dma_cb, uint32_t list_pointer, int offset)
{
    (void)offset;
    if (!dma_cb)
    {
        return;
    }

    dma_cb->txListPointer = list_pointer;
    dma_cb->txListPointerOffset = 0;

    dmacb_load_tx_buffer(dma_cb);
}

void dmacb_debug_tx_frames(DMAControlBlocks *dma_cb)
{
    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
    {
        if (!dma_cb)
        {
            return;
        }

        for (int i = 0; i < 100; i++)
        {
            uint32_t addr = dma_cb->txListPointer + (uint32_t)(i * 4);

            uint16_t key_value = (uint16_t)dma_control_blocks_dma_read(dma_cb, addr);
            KeyFlags key = (KeyFlags)(key_value & 0xFF00);

            dma_control_blocks_log(dma_cb, "TXAnalyse: Offset=%d LP=0x%06X Key=0x%04X", i, addr,
                                   key_value);

            if ((key_value == 0) || (key == KEYFLAG_NEW_LIST_POINTER))
            {
                dma_control_blocks_log(dma_cb, "TXAnalyse: End of frames at offset=%d", i);
                return;
            }
        }
    }
}

void dmacb_load_tx_buffer(DMAControlBlocks *dma_cb)
{
    if (!dma_cb)
    {
        return;
    }

    if (dma_cb->txDCB)
    {
        free(dma_cb->txDCB);
    }

    dma_cb->txDCB = dmacb_load_buffer_description(dma_cb, dma_cb->txListPointer,
                                                  dma_cb->txListPointerOffset, false);
}

bool dmacb_load_next_tx_buffer(DMAControlBlocks *dma_cb)
{
    if (!dma_cb)
    {
        return false;
    }

    dma_cb->txListPointerOffset++;
    dmacb_load_tx_buffer(dma_cb);

    return (dma_cb->txDCB && dcb_get_key(dma_cb->txDCB) == KEYFLAG_BLOCK_TO_BE_TRANSMITTED);
}

void dmacb_mark_buffer_sent(DMAControlBlocks *dma_cb)
{
    if (!dma_cb || !dma_cb->txDCB)
    {
        return;
    }

    if (dcb_get_key(dma_cb->txDCB) == KEYFLAG_BLOCK_TO_BE_TRANSMITTED)
    {
        if (dma_cb->hdlcDevice && dma_cb->hdlcDevice->deviceData)
        {
            ((HDLCData *)dma_cb->hdlcDevice->deviceData)->dcbTxMarked++;
        }
        uint16_t data = (uint16_t)(dcb_get_data_flow_cost(dma_cb->txDCB) |
                                   (uint16_t)(KEYFLAG_ALREADY_TRANSMITTED_BLOCK));
        dcb_set_key_value(dma_cb->txDCB, data);

        if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
        {
            dma_control_blocks_log(dma_cb, "MarkBufferSent: Status Word = 0x%04X", data);
        }
        dma_control_blocks_dma_write(dma_cb, dcb_get_buffer_address(dma_cb->txDCB), data);
    }
}

// RX Functions

void dmacb_set_rx_pointer(DMAControlBlocks *dma_cb, uint32_t list_pointer, int offset)
{
    if (!dma_cb)
    {
        return;
    }

    dma_cb->rxListPointer = list_pointer;
    dma_cb->rxListPointerOffset = (uint16_t)offset;

    dmacb_load_rx_buffer(dma_cb);
}

void dmacb_load_rx_buffer(DMAControlBlocks *dma_cb)
{
    if (!dma_cb)
    {
        return;
    }

    if (dma_cb->rxDCB)
    {
        free(dma_cb->rxDCB);
    }

    dma_cb->rxDCB = dmacb_load_buffer_description(dma_cb, dma_cb->rxListPointer,
                                                  dma_cb->rxListPointerOffset, true);
}

bool dmacb_load_next_rx_buffer(DMAControlBlocks *dma_cb)
{
    if (!dma_cb)
    {
        return false;
    }

    dma_cb->rxListPointerOffset++;

    // Assuming max 128 buffers in the list (for safefty), wrap around to 0 if we exceed
    if (dma_cb->rxListPointerOffset > 128)
    {
        dma_cb->rxListPointerOffset = 0;
    }

    dmacb_load_rx_buffer(dma_cb);

    if (!dma_cb->rxDCB)
    {
        return false;
    }

    // Handle NewListPointer - loop back to offset 0 of the buffer ring
    if (dcb_get_key(dma_cb->rxDCB) == KEYFLAG_NEW_LIST_POINTER)
    {
        if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
        {
            dma_control_blocks_log(dma_cb, "NewListPointer at offset %d, looping back to offset 0",
                                   dma_cb->rxListPointerOffset);
        }
        dma_cb->rxListPointerOffset = 0;
        dmacb_load_rx_buffer(dma_cb);

        if (!dma_cb->rxDCB)
        {
            return false;
        }

        // If first buffer after wrap is not empty, list is exhausted
        if (dcb_get_key(dma_cb->rxDCB) != KEYFLAG_EMPTY_RECEIVER_BLOCK)
        {
            if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
            {
                dma_control_blocks_log(dma_cb,
                                       "Buffer after NewListPointer is not empty - list exhausted");
            }
            return false;
        }
    }

    return (dcb_get_key(dma_cb->rxDCB) == KEYFLAG_EMPTY_RECEIVER_BLOCK);
}

bool dmacb_is_next_r_xbuf_valid(DMAControlBlocks *dma_cb)
{
    if (!dma_cb)
    {
        return false;
    }

    uint32_t listpointer =
        dma_cb->rxListPointer + (uint32_t)((dma_cb->rxListPointerOffset + 1) * 4);
    uint16_t key_value = (uint16_t)dma_control_blocks_dma_read(dma_cb, listpointer);
    KeyFlags key = (KeyFlags)(key_value & KEYFLAG_MASK_KEY);

    return (key == KEYFLAG_EMPTY_RECEIVER_BLOCK);
}

// ---------------------------------------------------------------------------
// DMA Back to ND machine with a new KEY telling that this buffer has been received
//
// Update KEY = FullReceiverBlock
// -----------------------------------------------------------------------------
void dmacb_mark_buffer_received(DMAControlBlocks *dma_cb, uint8_t rx_status)
{
    if (!dma_cb || !dma_cb->rxDCB)
    {
        return;
    }

    if (dcb_get_key(dma_cb->rxDCB) == KEYFLAG_EMPTY_RECEIVER_BLOCK)
    {
        if (dma_cb->hdlcDevice && dma_cb->hdlcDevice->deviceData)
        {
            ((HDLCData *)dma_cb->hdlcDevice->deviceData)->dcbRxMarked++;
        }

        // Update the key value with the RX status and mark as DONE
        uint16_t key_value = dcb_get_key_value(dma_cb->rxDCB);

        // Set RCOST in the low 8 bits, its actually the ReceiverStatusRegister
        key_value = (key_value & 0xFF00) | rx_status;

        // Mark block as DONE (ie filled up)
        key_value |= (uint16_t)(KEYFLAG_BLOCK_DONE_BIT);
        dcb_set_key_value(dma_cb->rxDCB, key_value);

        // Write back the updated key value to memory
        dma_control_blocks_dma_write(dma_cb, dcb_get_buffer_address(dma_cb->rxDCB), key_value);

        if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
        {
            const char *flags = "";
            char flags_buffer[64] = "";
            if (dcb_has_rsom_flag(dma_cb->rxDCB))
            {
                snprintf(flags_buffer + strlen(flags_buffer),
                         sizeof(flags_buffer) - strlen(flags_buffer), "%s", "RSOM ");
            }
            if (dcb_has_reom_flag(dma_cb->rxDCB))
            {
                snprintf(flags_buffer + strlen(flags_buffer),
                         sizeof(flags_buffer) - strlen(flags_buffer), "%s", "REOM ");
            }
            flags = flags_buffer;

            dma_control_blocks_log(
                dma_cb,
                "--------------------------------------------------------------------------");
            dma_control_blocks_log(dma_cb, "DMA Buffer received           : 0x%06X Flags: %s",
                                   dcb_get_buffer_address(dma_cb->rxDCB), flags);
            dma_control_blocks_log(dma_cb,
                                   "ListPointer                   : 0x%06X  lp[0x%06X] offset[%d]",
                                   dcb_get_list_pointer(dma_cb->rxDCB), dma_cb->rxListPointer,
                                   dma_cb->rxListPointerOffset);
            dma_control_blocks_log(dma_cb, "%s", "");
            dma_control_blocks_log(
                dma_cb, "KeyValue                      : 0x%04X %s [MarkBufferReceived]",
                dma_control_blocks_dma_read(dma_cb, dcb_get_buffer_address(dma_cb->rxDCB) + 0),
                ""); // TODO: key name
            dma_control_blocks_log(
                dma_cb, "ByteCount                     : 0x%04X",
                dma_control_blocks_dma_read(dma_cb, dcb_get_buffer_address(dma_cb->rxDCB) + 1));
            dma_control_blocks_log(
                dma_cb, "MostAddress                   : 0x%04X",
                dma_control_blocks_dma_read(dma_cb, dcb_get_buffer_address(dma_cb->rxDCB) + 2));
            dma_control_blocks_log(
                dma_cb, "LeastAddress                  : 0x%04X",
                dma_control_blocks_dma_read(dma_cb, dcb_get_buffer_address(dma_cb->rxDCB) + 3));
            dma_control_blocks_log(dma_cb, "DMA bytes written             : %d",
                                   dcb_get_dma_bytes_written(dma_cb->rxDCB));

            if (dcb_get_dma_bytes_written(dma_cb->rxDCB) > 0)
            {
                dcb_set_dma_address(dma_cb->rxDCB, dcb_get_data_memory_address(dma_cb->rxDCB));

                char bytes[512] = "";
                char temp[16];
                for (int i = 0; i < dcb_get_dma_bytes_written(dma_cb->rxDCB); i++)
                {
                    snprintf(temp, sizeof(temp), "0x%02X ", dmacb_read_next_byte_dma(dma_cb, true));
                    snprintf(bytes + strlen(bytes), sizeof(bytes) - strlen(bytes), "%s", temp);
                }

                dma_control_blocks_log(
                    dma_cb, "Received block [%06X:%d]: %s [RSOM:%d] [REOM:%d]",
                    dcb_get_buffer_address(dma_cb->rxDCB), dma_cb->rxListPointerOffset, bytes,
                    dcb_has_rsom_flag(dma_cb->rxDCB), dcb_has_reom_flag(dma_cb->rxDCB));
            }
            dma_control_blocks_log(
                dma_cb,
                "--------------------------------------------------------------------------");
        }
    }
}

// Buffer Description Loading

/* Read the buffer description words at address (key value, and for a
 * non-zero key the byte count and the two data-address words) into
 * description. Returns the key value. */
static uint16_t read_dcb_words(DMAControlBlocks *dma_cb, HdlcDCB *description, uint32_t address)
{
    uint16_t key_value = (uint16_t)dma_control_blocks_dma_read(dma_cb, address++);
    dcb_set_key_value(description, key_value);

    if (key_value != 0)
    {
        uint16_t byte_count = (uint16_t)dma_control_blocks_dma_read(dma_cb, address++);
        uint16_t most_address = (uint16_t)dma_control_blocks_dma_read(dma_cb, address++);
        uint16_t least_address = (uint16_t)dma_control_blocks_dma_read(dma_cb, address++);

        dcb_set_byte_count(description, byte_count);

        // Set the data memory address using the most and least address parts
        uint32_t data_memory_addr = ((uint32_t)(most_address & 0x00FF) << 16) | least_address;
        dcb_set_data_memory_address(description, data_memory_addr);
    }
    return key_value;
}

/* Displacement for the buffer at offset: the first buffer in the list uses
 * Displacement1, all others Displacement2 (0 without parameters). */
static uint16_t pick_displacement(const DMAControlBlocks *dma_cb, uint16_t offset)
{
    if (offset == 0)
    {
        // First buffer in the list uses Displacement1
        return dma_cb->parameters ? (uint16_t)dma_cb->parameters->displacement1 : 0;
    }
    // All other buffers use Displacement2
    return dma_cb->parameters ? (uint16_t)dma_cb->parameters->displacement2 : 0;
}

HdlcDCB *dmacb_load_buffer_description(DMAControlBlocks *dma_cb, uint32_t list_pointer,
                                       uint16_t offset, bool is_rx)
{
    (void)is_rx;
    if (!dma_cb || list_pointer == 0)
    {
        return NULL;
    }

    uint32_t actual_list_pointer = list_pointer + (uint32_t)(offset * 4);

    HdlcDCB *description = malloc(sizeof(HdlcDCB));
    if (!description)
    {
        return NULL;
    }

    dcb_init(description);
    dcb_set_list_pointer(description, actual_list_pointer);
    dcb_set_offset_from_lp(description, offset);
    dcb_set_buffer_address(description, actual_list_pointer);

    uint16_t key_value = read_dcb_words(dma_cb, description, actual_list_pointer);

    // Set displacement based on offset
    uint16_t displacement = pick_displacement(dma_cb, offset);
    dcb_set_displacement(description, displacement);

    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
    {
        const char *mode = is_rx ? "RX" : "TX";
        dma_control_blocks_log(
            dma_cb, "--------------------------------------------------------------------------");
        dma_control_blocks_log(dma_cb,
                               "LoadBufferDescription %s      : 0x%06X lp[0x%06X] offset[%d]", mode,
                               actual_list_pointer, list_pointer, offset);
        dma_control_blocks_log(dma_cb, "%s", "");
        dma_control_blocks_log(dma_cb, "KeyValue                      : 0x%04X %s", key_value,
                               ""); // TODO: key name

        if (dcb_get_key(description) != KEYFLAG_EMPTY_RECEIVER_BLOCK)
        {
            dma_control_blocks_log(dma_cb, "ByteCount                     : 0x%04X",
                                   dcb_get_byte_count(description));
        }
        dma_control_blocks_log(dma_cb, "MostAddress                   : 0x%04X",
                               (dcb_get_data_memory_address(description) >> 16) & 0xFF);
        dma_control_blocks_log(dma_cb, "LeastAddress                  : 0x%04X",
                               dcb_get_data_memory_address(description) & 0xFFFF);
        dma_control_blocks_log(dma_cb, "Displacement                  : %d : Use Displacement%s",
                               displacement, offset == 0 ? "1" : "2");

        if ((dcb_get_byte_count(description) > 0) &&
            (dcb_get_key(description) == KEYFLAG_BLOCK_TO_BE_TRANSMITTED))
        {
            dcb_set_dma_address(description, dcb_get_data_memory_address(description));

            char bytes[512] = "";
            char temp[16];
            for (int i = 0; i < dcb_get_byte_count(description); i++)
            {
                snprintf(temp, sizeof(temp), "0x%02X ", dmacb_read_next_byte_dma(dma_cb, is_rx));
                snprintf(bytes + strlen(bytes), sizeof(bytes) - strlen(bytes), "%s", temp);
            }

            dma_control_blocks_log(dma_cb, "DATA: %s", bytes);
        }
        dma_control_blocks_log(
            dma_cb, "--------------------------------------------------------------------------");
    }

    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_TRACE))
    {
        if (dcb_get_key(description) == KEYFLAG_EMPTY_RECEIVER_BLOCK)
        {
            dma_control_blocks_log(dma_cb, "Loading Buffer from 0x%08X Key=%s", list_pointer,
                                   "EmptyReceiverBlock");
        }
        else
        {
            dma_control_blocks_log(dma_cb,
                                   "Loading Buffer from 0x%08X Key=%s DataFlowCost=0x%08X "
                                   "ByteCount=%d RSOMFlag=%d REOMFlag=%d",
                                   list_pointer, "", dcb_get_data_flow_cost(description),
                                   dcb_get_byte_count(description), dcb_has_rsom_flag(description),
                                   dcb_has_reom_flag(description));
        }
    }

    // Set up the DMA helpers
    dcb_set_dma_address(description, dcb_get_data_memory_address(description));
    dcb_set_dma_read_data(description, -1);
    dcb_set_dma_bytes_written(description, 0);
    dcb_set_dma_bytes_read(description, 0);

    return description;
}

// DMA Read/Write Helper Functions

uint8_t dmacb_read_next_byte_dma(DMAControlBlocks *dma_cb, bool is_rx)
{
    if (!dma_cb)
    {
        return 0;
    }

    HdlcDCB *description;
    if (is_rx)
    {
        description = dma_cb->rxDCB;
    }
    else
    {
        description = dma_cb->txDCB;
    }

    if (!description)
    {
        return 0;
    }

    uint8_t data = 0;
    int bytes_read = dcb_get_dma_bytes_read(description);
    uint16_t displacement = dcb_get_displacement(description);

    // Adjust for "displacement"
    if ((bytes_read == 0) && (displacement > 0))
    {
        // We need to skip "displacement" number of bytes
        bytes_read += displacement;
        dcb_set_dma_bytes_read(description, bytes_read);

        // and we need to calculate the new dmaAddress
        uint32_t dma_addr = dcb_get_dma_address(description);
        dma_addr += (uint32_t)(displacement / 2);
        dcb_set_dma_address(description, dma_addr);
    }

    // Start reading
    if ((bytes_read % 2) == 0)
    { // 0 ==> is even (High byte), 1 ==> odd (Low byte)
        int read_data = dma_control_blocks_dma_read(dma_cb, dcb_get_dma_address(description));
        dcb_set_dma_read_data(description, read_data);
        data = (uint8_t)(read_data >> 8);
    }
    else
    {
        if (dcb_get_dma_read_data(description) == -1)
        { // data not read (might happen because of displacement)
            int read_data = dma_control_blocks_dma_read(dma_cb, dcb_get_dma_address(description));
            dcb_set_dma_read_data(description, read_data);
        }

        data = (uint8_t)(dcb_get_dma_read_data(description) & 0xFF);

        uint32_t dma_addr = dcb_get_dma_address(description);
        dma_addr++;
        dcb_set_dma_address(description, dma_addr);
    }

    bytes_read++;
    dcb_set_dma_bytes_read(description, bytes_read);

    return data;
}

void dmacb_write_next_byte_dma(DMAControlBlocks *dma_cb, uint8_t data, bool is_rx)
{
    if (!dma_cb)
    {
        return;
    }

    HdlcDCB *description;
    if (is_rx)
    {
        description = dma_cb->rxDCB;
    }
    else
    {
        description = dma_cb->txDCB;
    }

    if (!description)
    {
        return;
    }


    int bytes_written = dcb_get_dma_bytes_written(description);
    uint16_t displacement = dcb_get_displacement(description);

    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_TRACE))
    {
        dma_control_blocks_log(
            dma_cb,
            " DMA_WRITE_ENTRY: data=0x%02X, bytes_written=%d, displacement=%d, dmaAddress=0x%08X",
            data, bytes_written, displacement, dcb_get_dma_address(description));
    }

    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_TRACE))
    {
        dma_control_blocks_log(dma_cb, "DMA Write: 0x%02X  #%d", data, bytes_written);
    }

    // Adjust for "displacement"
    if ((bytes_written == 0) && (displacement > 0))
    {
        // We need to skip "displacement" number of bytes
        bytes_written += displacement;
        dcb_set_dma_bytes_written(description, bytes_written);

        // and we need to calculate the new dmaAddress
        uint32_t dma_addr = dcb_get_dma_address(description);
        dma_addr += (uint32_t)(displacement / 2);
        dcb_set_dma_address(description, dma_addr);

        if (Log_IsEnabled(LOG_CAT_HDLC, LOG_TRACE))
        {
            dma_control_blocks_log(
                dma_cb,
                "DMA_WRITE_DISPLACEMENT: skipped %d bytes, new_bytes_written=%d, "
                "new_dmaAddress=0x%08X",
                displacement, bytes_written, dcb_get_dma_address(description));
        }
    }

    uint16_t dma_write_data;
    uint32_t dma_addr = dcb_get_dma_address(description);

    // Start writing
    if ((bytes_written % 2) == 0)
    { // ==0 is even, 1 ==odd
        int mem_data = dma_control_blocks_dma_read(dma_cb, dma_addr);
        dma_write_data = (uint16_t)((mem_data & 0x00FF) | ((data & 0xFF) << 8));
        dma_control_blocks_dma_write(dma_cb, dma_addr, dma_write_data);
    }
    else
    {
        int mem_data = dma_control_blocks_dma_read(dma_cb, dma_addr);
        dma_write_data = (uint16_t)((mem_data & 0xFF00) | (data & 0xFF));
        dma_control_blocks_dma_write(dma_cb, dma_addr, dma_write_data);

        dma_addr++;
        dcb_set_dma_address(description, dma_addr);
    }

    bytes_written++;
    dcb_set_dma_bytes_written(description, bytes_written);

    // Update DCB with bytes written as "ByteCount"
    dma_control_blocks_dma_write(dma_cb, dcb_get_list_pointer(description) + 1,
                                 (uint16_t)bytes_written);

    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_TRACE))
    {
        dma_control_blocks_log(dma_cb, "DMA_WRITE_EXIT: bytes_written=%d, final_dmaAddress=0x%08X",
                               bytes_written, dcb_get_dma_address(description));
    }
}

// Callback Setup Functions

void dmacb_set_read_dma_callback(DMAControlBlocks *dma_cb, DmaControlBlocksReadCallback callback,
                                 void *context)
{
    if (!dma_cb)
    {
        return;
    }
    dma_cb->onReadDMA = callback;
    dma_cb->callbackContext = context;
}

void dmacb_set_write_dma_callback(DMAControlBlocks *dma_cb, DmaControlBlocksWriteCallback callback,
                                  void *context)
{
    if (!dma_cb)
    {
        return;
    }
    dma_cb->onWriteDMA = callback;
    dma_cb->callbackContext = context;
}

void DMAControlBlocks_SetSendHDLCFrameCallback(DMAControlBlocks *dma_cb,
                                               DmaControlBlocksSendFrameCallback callback,
                                               void *context)
{
    if (!dma_cb)
    {
        return;
    }
    dma_cb->onSendHDLCFrame = callback;
    dma_cb->callbackContext = context;
}

void DMAControlBlocks_SetInterruptCallback(DMAControlBlocks *dma_cb,
                                           DmaControlBlocksInterruptCallback callback,
                                           void *context)
{
    if (!dma_cb)
    {
        return;
    }
    dma_cb->onSetInterruptBit = callback;
    dma_cb->callbackContext = context;
}

// Private Helper Functions

static void dma_control_blocks_dma_write(DMAControlBlocks *dma_cb, uint32_t address, uint16_t data)
{
    if (!dma_cb || !dma_cb->onWriteDMA)
    {
        return;
    }
    dma_cb->onWriteDMA(dma_cb->callbackContext, address, data);
}

static int dma_control_blocks_dma_read(DMAControlBlocks *dma_cb, uint32_t address)
{
    if (!dma_cb || !dma_cb->onReadDMA)
    {
        return 0;
    }
    return dma_cb->onReadDMA(dma_cb->callbackContext, address);
}

static void dma_control_blocks_log(DMAControlBlocks *dma_cb, const char *format, ...)
{
    (void)dma_cb;
    if (!Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
    {
        return;
    }

    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    log_write(LOG_CAT_HDLC, LOG_DEBUG, "DMACB: %s", buffer);
}
