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

#include "dma_param_buf.h"

void ParameterBuffer_Init(ParameterBuffer *paramBuf)
{
    if (!paramBuf) return;

    memset(paramBuf, 0, sizeof(ParameterBuffer));
}

void ParameterBuffer_Clear(ParameterBuffer *paramBuf)
{
    if (!paramBuf) return;

    paramBuf->parameterControlRegister = 0;
    paramBuf->syncAddressRegister = 0;
    paramBuf->characterLength = 0;
    paramBuf->displacement1 = 0;
    paramBuf->displacement2 = 0;
    paramBuf->maxReceiverBlockLength = 0;
    paramBuf->receiverStatusReg = 0;
    paramBuf->transmitterStatusReg = 0;
    paramBuf->dmaBankBits = 0;
}

// Getter functions

int ParameterBuffer_GetParameterControlRegister(const ParameterBuffer *paramBuf)
{
    if (!paramBuf) return 0;
    return paramBuf->parameterControlRegister;
}

int ParameterBuffer_GetSyncAddressRegister(const ParameterBuffer *paramBuf)
{
    if (!paramBuf) return 0;
    return paramBuf->syncAddressRegister;
}

int ParameterBuffer_GetCharacterLength(const ParameterBuffer *paramBuf)
{
    if (!paramBuf) return 0;
    return paramBuf->characterLength;
}

int ParameterBuffer_GetDisplacement1(const ParameterBuffer *paramBuf)
{
    if (!paramBuf) return 0;
    return paramBuf->displacement1;
}

int ParameterBuffer_GetDisplacement2(const ParameterBuffer *paramBuf)
{
    if (!paramBuf) return 0;
    return paramBuf->displacement2;
}

int ParameterBuffer_GetMaxReceiverBlockLength(const ParameterBuffer *paramBuf)
{
    if (!paramBuf) return 0;
    return paramBuf->maxReceiverBlockLength;
}

int ParameterBuffer_GetReceiverStatusReg(const ParameterBuffer *paramBuf)
{
    if (!paramBuf) return 0;
    return paramBuf->receiverStatusReg;
}

int ParameterBuffer_GetTransmitterStatusReg(const ParameterBuffer *paramBuf)
{
    if (!paramBuf) return 0;
    return paramBuf->transmitterStatusReg;
}

int ParameterBuffer_GetDmaBankBits(const ParameterBuffer *paramBuf)
{
    if (!paramBuf) return 0;
    return paramBuf->dmaBankBits;
}

// Setter functions

void ParameterBuffer_SetParameterControlRegister(ParameterBuffer *paramBuf, int value)
{
    if (!paramBuf) return;
    paramBuf->parameterControlRegister = value;
}

void ParameterBuffer_SetSyncAddressRegister(ParameterBuffer *paramBuf, int value)
{
    if (!paramBuf) return;
    paramBuf->syncAddressRegister = value;
}

void ParameterBuffer_SetCharacterLength(ParameterBuffer *paramBuf, int value)
{
    if (!paramBuf) return;
    paramBuf->characterLength = value;
}

void ParameterBuffer_SetDisplacement1(ParameterBuffer *paramBuf, int value)
{
    if (!paramBuf) return;
    paramBuf->displacement1 = value;
}

void ParameterBuffer_SetDisplacement2(ParameterBuffer *paramBuf, int value)
{
    if (!paramBuf) return;
    paramBuf->displacement2 = value;
}

void ParameterBuffer_SetMaxReceiverBlockLength(ParameterBuffer *paramBuf, int value)
{
    if (!paramBuf) return;
    paramBuf->maxReceiverBlockLength = value;
}

void ParameterBuffer_SetReceiverStatusReg(ParameterBuffer *paramBuf, int value)
{
    if (!paramBuf) return;
    paramBuf->receiverStatusReg = value;
}

void ParameterBuffer_SetTransmitterStatusReg(ParameterBuffer *paramBuf, int value)
{
    if (!paramBuf) return;
    paramBuf->transmitterStatusReg = value;
}

void ParameterBuffer_SetDmaBankBits(ParameterBuffer *paramBuf, int value)
{
    if (!paramBuf) return;
    paramBuf->dmaBankBits = value;
}

// Debug functions
