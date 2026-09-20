/*
 * dma_param_buf.h - HDLC DMA parameter buffer: structure and accessors.
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

#ifndef DMA_PARAM_BUF_H
#define DMA_PARAM_BUF_H

#include <stdint.h>
#include <stdbool.h>

/**
 * DMA Module Parameter Buffer
 *
 * This structure contains the parameter buffer used by the DMA module
 * for HDLC communication configuration and status tracking.
 */
typedef struct
{
    /**
     * PCR (PCRH/High byte and PCRL/low byte)
     * Parameter Control Register (8 least significant bits)
     */
    int parameterControlRegister;

    /**
     * Sync/Address Register (8 least significant bits)
     */
    int syncAddressRegister;

    /**
     * Character Length (8 least significant bits)
     */
    int characterLength;

    /**
     * No. of bytes, first block in frame
     * Displacement 1 is the number of free bytes reserved at the beginning
     * of each buffer containing the start of a message (Frame).
     */
    int displacement1;

    /**
     * No. of bytes, other blocks in frame
     * Displacement 2 is the number of free bytes reserved at the beginning
     * of each buffer which _do not_ contain the start of a message (Frame).
     */
    int displacement2;

    /**
     * No. of bytes, including displacement
     * Max Receiver Block Length is the total number of bytes in a receiver buffer,
     * including displacement. Long frames may be divided into blocks and stored
     * in two or more buffers.
     */
    int maxReceiverBlockLength;

    /**
     * Receiver Status Register
     * 8 least significant bits, not accumulate
     */
    int receiverStatusReg;

    /**
     * Transmitter Status Register
     * 8 least significant bits, not accumulated
     */
    int transmitterStatusReg;

    /**
     * DMA Bank Bits
     */
    int dmaBankBits;

} ParameterBuffer;

// Function declarations

/**
 * @brief Zero the whole ParameterBuffer structure.
 * @param paramBuf Buffer to initialize.
 */
void ParameterBuffer_Init(ParameterBuffer *param_buf);

/**
 * @brief Reset every field of the ParameterBuffer to 0 (same effect as Init,
 * field by field).
 * @param paramBuf Buffer to clear.
 */
void ParameterBuffer_Clear(ParameterBuffer *param_buf);

// Getter functions

/**
 * @brief Get the Parameter Control Register (PCR) value.
 * @param paramBuf Buffer to read.
 * @return parameterControlRegister field, or 0 if paramBuf is NULL.
 */
int ParameterBuffer_GetParameterControlRegister(const ParameterBuffer *param_buf);

/**
 * @brief Get the Sync/Address Register value.
 * @param paramBuf Buffer to read.
 * @return syncAddressRegister field, or 0 if paramBuf is NULL.
 */
int ParameterBuffer_GetSyncAddressRegister(const ParameterBuffer *param_buf);

/**
 * @brief Get the Character Length value.
 * @param paramBuf Buffer to read.
 * @return characterLength field, or 0 if paramBuf is NULL.
 */
int ParameterBuffer_GetCharacterLength(const ParameterBuffer *param_buf);

/**
 * @brief Get Displacement1 (free bytes reserved at the start of a buffer
 * that begins a frame).
 * @param paramBuf Buffer to read.
 * @return displacement1 field, or 0 if paramBuf is NULL.
 */
int ParameterBuffer_GetDisplacement1(const ParameterBuffer *param_buf);

/**
 * @brief Get Displacement2 (free bytes reserved at the start of a buffer
 * that does not begin a frame).
 * @param paramBuf Buffer to read.
 * @return displacement2 field, or 0 if paramBuf is NULL.
 */
int ParameterBuffer_GetDisplacement2(const ParameterBuffer *param_buf);

/**
 * @brief Get the maximum receiver block length, including displacement.
 * @param paramBuf Buffer to read.
 * @return maxReceiverBlockLength field, or 0 if paramBuf is NULL.
 */
int ParameterBuffer_GetMaxReceiverBlockLength(const ParameterBuffer *param_buf);

/**
 * @brief Get the Receiver Status Register value.
 * @param paramBuf Buffer to read.
 * @return receiverStatusReg field, or 0 if paramBuf is NULL.
 */
int ParameterBuffer_GetReceiverStatusReg(const ParameterBuffer *param_buf);

/**
 * @brief Get the Transmitter Status Register value.
 * @param paramBuf Buffer to read.
 * @return transmitterStatusReg field, or 0 if paramBuf is NULL.
 */
int ParameterBuffer_GetTransmitterStatusReg(const ParameterBuffer *param_buf);

/**
 * @brief Get the DMA Bank Bits value.
 * @param paramBuf Buffer to read.
 * @return dmaBankBits field, or 0 if paramBuf is NULL.
 */
int ParameterBuffer_GetDmaBankBits(const ParameterBuffer *param_buf);

// Setter functions

/**
 * @brief Set the Parameter Control Register (PCR) value.
 * @param paramBuf Buffer to modify.
 * @param value Value to store.
 */
void ParameterBuffer_SetParameterControlRegister(ParameterBuffer *param_buf, int value);

/**
 * @brief Set the Sync/Address Register value.
 * @param paramBuf Buffer to modify.
 * @param value Value to store.
 */
void ParameterBuffer_SetSyncAddressRegister(ParameterBuffer *param_buf, int value);

/**
 * @brief Set the Character Length value.
 * @param paramBuf Buffer to modify.
 * @param value Value to store.
 */
void ParameterBuffer_SetCharacterLength(ParameterBuffer *param_buf, int value);

/**
 * @brief Set Displacement1 (free bytes reserved at the start of a buffer
 * that begins a frame).
 * @param paramBuf Buffer to modify.
 * @param value Value to store.
 */
void ParameterBuffer_SetDisplacement1(ParameterBuffer *param_buf, int value);

/**
 * @brief Set Displacement2 (free bytes reserved at the start of a buffer
 * that does not begin a frame).
 * @param paramBuf Buffer to modify.
 * @param value Value to store.
 */
void ParameterBuffer_SetDisplacement2(ParameterBuffer *param_buf, int value);

/**
 * @brief Set the maximum receiver block length, including displacement.
 * @param paramBuf Buffer to modify.
 * @param value Value to store.
 */
void ParameterBuffer_SetMaxReceiverBlockLength(ParameterBuffer *param_buf, int value);

/**
 * @brief Set the Receiver Status Register value.
 * @param paramBuf Buffer to modify.
 * @param value Value to store.
 */
void ParameterBuffer_SetReceiverStatusReg(ParameterBuffer *param_buf, int value);

/**
 * @brief Set the Transmitter Status Register value.
 * @param paramBuf Buffer to modify.
 * @param value Value to store.
 */
void ParameterBuffer_SetTransmitterStatusReg(ParameterBuffer *param_buf, int value);

/**
 * @brief Set the DMA Bank Bits value.
 * @param paramBuf Buffer to modify.
 * @param value Value to store.
 */
void ParameterBuffer_SetDmaBankBits(ParameterBuffer *param_buf, int value);


#endif // DMA_PARAM_BUF_H
