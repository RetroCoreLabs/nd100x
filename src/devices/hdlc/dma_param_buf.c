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

void dma_params_init(ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return;
    }

    memset(param_buf, 0, sizeof(ParameterBuffer));
}

void dma_params_clear(ParameterBuffer *param_buf)
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

int dma_params_get_control_register(const ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return 0;
    }
    return param_buf->parameterControlRegister;
}

int dma_params_get_sync_address_register(const ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return 0;
    }
    return param_buf->syncAddressRegister;
}

int dma_params_get_character_length(const ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return 0;
    }
    return param_buf->characterLength;
}

int dma_params_get_displacement_1(const ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return 0;
    }
    return param_buf->displacement1;
}

int dma_params_get_displacement_2(const ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return 0;
    }
    return param_buf->displacement2;
}

int dma_params_get_max_receiver_block_length(const ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return 0;
    }
    return param_buf->maxReceiverBlockLength;
}

int dma_params_get_receiver_status_reg(const ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return 0;
    }
    return param_buf->receiverStatusReg;
}

int dma_params_get_transmitter_status_reg(const ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return 0;
    }
    return param_buf->transmitterStatusReg;
}

int dma_params_get_dma_bank_bits(const ParameterBuffer *param_buf)
{
    if (!param_buf)
    {
        return 0;
    }
    return param_buf->dmaBankBits;
}

// Setter functions

void dma_params_set_control_register(ParameterBuffer *param_buf, int value)
{
    if (!param_buf)
    {
        return;
    }
    param_buf->parameterControlRegister = value;
}

void dma_params_set_sync_address_register(ParameterBuffer *param_buf, int value)
{
    if (!param_buf)
    {
        return;
    }
    param_buf->syncAddressRegister = value;
}

void dma_params_set_character_length(ParameterBuffer *param_buf, int value)
{
    if (!param_buf)
    {
        return;
    }
    param_buf->characterLength = value;
}

void dma_params_set_displacement_1(ParameterBuffer *param_buf, int value)
{
    if (!param_buf)
    {
        return;
    }
    param_buf->displacement1 = value;
}

void dma_params_set_displacement_2(ParameterBuffer *param_buf, int value)
{
    if (!param_buf)
    {
        return;
    }
    param_buf->displacement2 = value;
}

void dma_params_set_max_receiver_block_length(ParameterBuffer *param_buf, int value)
{
    if (!param_buf)
    {
        return;
    }
    param_buf->maxReceiverBlockLength = value;
}

void dma_params_set_receiver_status_reg(ParameterBuffer *param_buf, int value)
{
    if (!param_buf)
    {
        return;
    }
    param_buf->receiverStatusReg = value;
}

void dma_params_set_transmitter_status_reg(ParameterBuffer *param_buf, int value)
{
    if (!param_buf)
    {
        return;
    }
    param_buf->transmitterStatusReg = value;
}

void dma_params_set_dma_bank_bits(ParameterBuffer *param_buf, int value)
{
    if (!param_buf)
    {
        return;
    }
    param_buf->dmaBankBits = value;
}

// Debug functions
