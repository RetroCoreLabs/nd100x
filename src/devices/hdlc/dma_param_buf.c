/*
 * dma_param_buf.c - HDLC DMA parameter buffer: control, sync and block-length fields.
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

#include "dma_param_buf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

void ParameterBuffer_Init(ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return;
    }

    memset(param_buf, 0, sizeof(ParameterBuffer));
}

void ParameterBuffer_Clear(ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return;
    }

    param_buf->parameterControlRegister = 0;
    param_buf->syncAddressRegister = 0;
    param_buf->characterLength = 0;
    param_buf->displacement1 = 0;
    param_buf->displacement2 = 0;
    param_buf->maxReceiverBlockLength = 0;
    param_buf->receiverStatusReg = 0;
    param_buf->transmitterStatusReg = 0;
    param_buf->dmaBankBits = 0;
}

// Getter functions

int ParameterBuffer_GetParameterControlRegister(const ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return 0;
    }
    return param_buf->parameterControlRegister;
}

int ParameterBuffer_GetSyncAddressRegister(const ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return 0;
    }
    return param_buf->syncAddressRegister;
}

int ParameterBuffer_GetCharacterLength(const ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return 0;
    }
    return param_buf->characterLength;
}

int ParameterBuffer_GetDisplacement1(const ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return 0;
    }
    return param_buf->displacement1;
}

int ParameterBuffer_GetDisplacement2(const ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return 0;
    }
    return param_buf->displacement2;
}

int ParameterBuffer_GetMaxReceiverBlockLength(const ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return 0;
    }
    return param_buf->maxReceiverBlockLength;
}

int ParameterBuffer_GetReceiverStatusReg(const ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return 0;
    }
    return param_buf->receiverStatusReg;
}

int ParameterBuffer_GetTransmitterStatusReg(const ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return 0;
    }
    return param_buf->transmitterStatusReg;
}

int ParameterBuffer_GetDmaBankBits(const ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return 0;
    }
    return param_buf->dmaBankBits;
}

// Setter functions

void ParameterBuffer_SetParameterControlRegister(ParameterBuffer *param_buf, int value)
{
    if (!param_buf)
    {
        return;
    }
    param_buf->parameterControlRegister = value;
}

void ParameterBuffer_SetSyncAddressRegister(ParameterBuffer *param_buf, int value)
{
    if (!param_buf)
    {
        return;
    }
    param_buf->syncAddressRegister = value;
}

void ParameterBuffer_SetCharacterLength(ParameterBuffer *param_buf, int value)
{
    if (!param_buf)
    {
        return;
    }
    param_buf->characterLength = value;
}

void ParameterBuffer_SetDisplacement1(ParameterBuffer *param_buf, int value)
{
    if (!param_buf)
    {
        return;
    }
    param_buf->displacement1 = value;
}

void ParameterBuffer_SetDisplacement2(ParameterBuffer *param_buf, int value)
{
    if (!param_buf)
    {
        return;
    }
    param_buf->displacement2 = value;
}

void ParameterBuffer_SetMaxReceiverBlockLength(ParameterBuffer *param_buf, int value)
{
    if (!param_buf)
    {
        return;
    }
    param_buf->maxReceiverBlockLength = value;
}

void ParameterBuffer_SetReceiverStatusReg(ParameterBuffer *param_buf, int value)
{
    if (!param_buf)
    {
        return;
    }
    param_buf->receiverStatusReg = value;
}

void ParameterBuffer_SetTransmitterStatusReg(ParameterBuffer *param_buf, int value)
{
    if (!param_buf)
    {
        return;
    }
    param_buf->transmitterStatusReg = value;
}

void ParameterBuffer_SetDmaBankBits(ParameterBuffer *param_buf, int value)
{
    if (!param_buf)
    {
        return;
    }
    param_buf->dmaBankBits = value;
}

// Debug functions
