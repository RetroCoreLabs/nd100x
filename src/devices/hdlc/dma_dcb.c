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

#include "dma_dcb.h"
#include "hdlc_constants.h"

void DCB_Init(HdlcDCB *dcb)
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

void DCB_Clear(HdlcDCB *dcb)
{
    if (!dcb)
    {
        return;
    }

    DCB_Init(dcb);
}

KeyFlags DCB_GetKey(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return (KeyFlags)(dcb->keyValue & KEYFLAG_MASK_KEY);
}

bool DCB_HasRSOMFlag(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return false;
    }

    return (dcb->keyValue & KEYFLAG_RCOST_RSOM) != 0;
}

bool DCB_HasREOMFlag(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return false;
    }

    return (dcb->keyValue & KEYFLAG_RCOST_REOM) != 0;
}

uint16_t DCB_GetDataFlowCost(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return (uint16_t)(dcb->keyValue & KEYFLAG_MASK_DATAFLOW_COST);
}

uint32_t DCB_GetDataMemoryAddress(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    // Originally 18 bit memory address.. Lets be a bit more open to potentially bigger memories like 24bit
    return (uint32_t)(dcb->mostAddress & 0x00FF) << 16 | dcb->leastAddress;
}

void DCB_SetDataMemoryAddress(HdlcDCB *dcb, uint32_t address)
{
    if (!dcb)
    {
        return;
    }

    dcb->leastAddress = (uint16_t)(address & 0xFFFF);
    dcb->mostAddress = (uint16_t)((address >> 16) & 0x00FF);
}

void DCB_SetBufferAddress(HdlcDCB *dcb, uint32_t address)
{
    if (!dcb)
    {
        return;
    }

    dcb->bufferAddress = address;
}

uint32_t DCB_GetBufferAddress(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return dcb->bufferAddress;
}

void DCB_SetOffsetFromLP(HdlcDCB *dcb, uint16_t offset)
{
    if (!dcb)
    {
        return;
    }

    dcb->offsetFromLP = offset;
}

uint16_t DCB_GetOffsetFromLP(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return dcb->offsetFromLP;
}

void DCB_SetKeyValue(HdlcDCB *dcb, uint16_t keyValue)
{
    if (!dcb)
    {
        return;
    }

    dcb->keyValue = keyValue;
}

uint16_t DCB_GetKeyValue(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return dcb->keyValue;
}

void DCB_SetByteCount(HdlcDCB *dcb, uint16_t byteCount)
{
    if (!dcb)
    {
        return;
    }

    dcb->byteCount = byteCount;
}

uint16_t DCB_GetByteCount(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return dcb->byteCount;
}

void DCB_SetDisplacement(HdlcDCB *dcb, uint16_t displacement)
{
    if (!dcb)
    {
        return;
    }

    dcb->displacement = displacement;
}

uint16_t DCB_GetDisplacement(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return dcb->displacement;
}

void DCB_SetListPointer(HdlcDCB *dcb, uint32_t listPointer)
{
    if (!dcb)
    {
        return;
    }

    dcb->listPointer = listPointer;
}

uint32_t DCB_GetListPointer(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return dcb->listPointer;
}

// DMA helper functions

void DCB_SetDMAAddress(HdlcDCB *dcb, uint32_t address)
{
    if (!dcb)
    {
        return;
    }

    dcb->dmaAddress = address;
}

uint32_t DCB_GetDMAAddress(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return dcb->dmaAddress;
}

void DCB_SetDMABytesRead(HdlcDCB *dcb, int bytesRead)
{
    if (!dcb)
    {
        return;
    }

    dcb->dmaBytesRead = bytesRead;
}

int DCB_GetDMABytesRead(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return dcb->dmaBytesRead;
}

void DCB_SetDMABytesWritten(HdlcDCB *dcb, int bytesWritten)
{
    if (!dcb)
    {
        return;
    }

    dcb->dmaBytesWritten = bytesWritten;
}

int DCB_GetDMABytesWritten(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return 0;
    }

    return dcb->dmaBytesWritten;
}

void DCB_SetDMAReadData(HdlcDCB *dcb, int data)
{
    if (!dcb)
    {
        return;
    }

    dcb->dmaReadData = data;
}

int DCB_GetDMAReadData(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return -1;
    }

    return dcb->dmaReadData;
}

bool DCB_IsDMAReadDataValid(const HdlcDCB *dcb)
{
    if (!dcb)
    {
        return false;
    }

    return dcb->dmaReadData != -1;
}

void DCB_ClearDMAReadData(HdlcDCB *dcb)
{
    if (!dcb)
    {
        return;
    }

    dcb->dmaReadData = -1;
}

// Debug/utility functions
