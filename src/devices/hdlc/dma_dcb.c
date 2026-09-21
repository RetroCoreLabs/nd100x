/*
 * dma_dcb.c - HDLC DMA descriptor control block (DCB): key flags and addresses.
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

#include "dma_dcb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "hdlc_constants.h"

void dcb_init(HdlcDCB *dcb)
{
    if (!dcb)
    {
        return;
    }

    memset(dcb, 0, sizeof(HdlcDCB));

    // Initialize to default values
    dcb->bufferAddress = 0;
    dcb->offsetFromLP = 0;
    dcb->keyValue = 0;
    dcb->byteCount = 0;
    dcb->mostAddress = 0;
    dcb->leastAddress = 0;
    dcb->displacement = 0;
    dcb->listPointer = 0;

    // Initialize helper fields for DMA transfer
    dcb->dmaAddress = 0;
    dcb->dmaBytesRead = 0;
    dcb->dmaBytesWritten = 0;
    dcb->dmaReadData = -1; // -1 indicates not read
}

void dcb_clear(HdlcDCB *dcb)
{
    if (!dcb)
    {
        return;
    }

    dcb_init(dcb);
}

KeyFlags dcb_get_key(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return (KeyFlags)(dcb->keyValue & KEYFLAG_MASK_KEY);
}

bool dcb_has_rsom_flag(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return false;
    }

    return (dcb->keyValue & KEYFLAG_RCOST_RSOM) != 0;
}

bool dcb_has_reom_flag(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return false;
    }

    return (dcb->keyValue & KEYFLAG_RCOST_REOM) != 0;
}

uint16_t dcb_get_data_flow_cost(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return (uint16_t)(dcb->keyValue & KEYFLAG_MASK_DATAFLOW_COST);
}

uint32_t dcb_get_data_memory_address(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    // Originally 18 bit memory address.. Lets be a bit more open to potentially bigger memories like 24bit
    return (uint32_t)(dcb->mostAddress & 0x00FF) << 16 | dcb->leastAddress;
}

void dcb_set_data_memory_address(HdlcDCB *dcb, uint32_t address)
{
    if (!dcb)
    {
        return;
    }

    dcb->leastAddress = (uint16_t)(address & 0xFFFF);
    dcb->mostAddress = (uint16_t)((address >> 16) & 0x00FF);
}

void dcb_set_buffer_address(HdlcDCB *dcb, uint32_t address)
{
    if (!dcb)
    {
        return;
    }

    dcb->bufferAddress = address;
}

uint32_t dcb_get_buffer_address(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return dcb->bufferAddress;
}

void dcb_set_offset_from_lp(HdlcDCB *dcb, uint16_t offset)
{
    if (!dcb)
    {
        return;
    }

    dcb->offsetFromLP = offset;
}

uint16_t dcb_get_offset_from_lp(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return dcb->offsetFromLP;
}

void dcb_set_key_value(HdlcDCB *dcb, uint16_t key_value)
{
    if (!dcb)
    {
        return;
    }

    dcb->keyValue = key_value;
}

uint16_t dcb_get_key_value(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return dcb->keyValue;
}

void dcb_set_byte_count(HdlcDCB *dcb, uint16_t byte_count)
{
    if (!dcb)
    {
        return;
    }

    dcb->byteCount = byte_count;
}

uint16_t dcb_get_byte_count(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return dcb->byteCount;
}

void dcb_set_displacement(HdlcDCB *dcb, uint16_t displacement)
{
    if (!dcb)
    {
        return;
    }

    dcb->displacement = displacement;
}

uint16_t dcb_get_displacement(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return dcb->displacement;
}

void dcb_set_list_pointer(HdlcDCB *dcb, uint32_t list_pointer)
{
    if (!dcb)
    {
        return;
    }

    dcb->listPointer = list_pointer;
}

uint32_t dcb_get_list_pointer(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return dcb->listPointer;
}

// DMA helper functions

void dcb_set_dma_address(HdlcDCB *dcb, uint32_t address)
{
    if (!dcb)
    {
        return;
    }

    dcb->dmaAddress = address;
}

uint32_t dcb_get_dma_address(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return dcb->dmaAddress;
}

void dcb_set_dma_bytes_read(HdlcDCB *dcb, int bytes_read)
{
    if (!dcb)
    {
        return;
    }

    dcb->dmaBytesRead = bytes_read;
}

int dcb_get_dma_bytes_read(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return dcb->dmaBytesRead;
}

void dcb_set_dma_bytes_written(HdlcDCB *dcb, int bytes_written)
{
    if (!dcb)
    {
        return;
    }

    dcb->dmaBytesWritten = bytes_written;
}

int dcb_get_dma_bytes_written(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return dcb->dmaBytesWritten;
}

void dcb_set_dma_read_data(HdlcDCB *dcb, int data)
{
    if (!dcb)
    {
        return;
    }

    dcb->dmaReadData = data;
}

int dcb_get_dma_read_data(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return -1;
    }

    return dcb->dmaReadData;
}

bool dcb_is_dma_read_data_valid(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return false;
    }

    return dcb->dmaReadData != -1;
}

void dcb_clear_dma_read_data(HdlcDCB *dcb)
{
    if (!dcb)
    {
        return;
    }

    dcb->dmaReadData = -1;
}

// Debug/utility functions
