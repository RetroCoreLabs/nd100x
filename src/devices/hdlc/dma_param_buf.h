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
void ParameterBuffer_Init(ParameterBuffer *paramBuf);
void ParameterBuffer_Clear(ParameterBuffer *paramBuf);

// Getter functions
int ParameterBuffer_GetParameterControlRegister(const ParameterBuffer *paramBuf);
int ParameterBuffer_GetSyncAddressRegister(const ParameterBuffer *paramBuf);
int ParameterBuffer_GetCharacterLength(const ParameterBuffer *paramBuf);
int ParameterBuffer_GetDisplacement1(const ParameterBuffer *paramBuf);
int ParameterBuffer_GetDisplacement2(const ParameterBuffer *paramBuf);
int ParameterBuffer_GetMaxReceiverBlockLength(const ParameterBuffer *paramBuf);
int ParameterBuffer_GetReceiverStatusReg(const ParameterBuffer *paramBuf);
int ParameterBuffer_GetTransmitterStatusReg(const ParameterBuffer *paramBuf);
int ParameterBuffer_GetDmaBankBits(const ParameterBuffer *paramBuf);

// Setter functions
void ParameterBuffer_SetParameterControlRegister(ParameterBuffer *paramBuf, int value);
void ParameterBuffer_SetSyncAddressRegister(ParameterBuffer *paramBuf, int value);
void ParameterBuffer_SetCharacterLength(ParameterBuffer *paramBuf, int value);
void ParameterBuffer_SetDisplacement1(ParameterBuffer *paramBuf, int value);
void ParameterBuffer_SetDisplacement2(ParameterBuffer *paramBuf, int value);
void ParameterBuffer_SetMaxReceiverBlockLength(ParameterBuffer *paramBuf, int value);
void ParameterBuffer_SetReceiverStatusReg(ParameterBuffer *paramBuf, int value);
void ParameterBuffer_SetTransmitterStatusReg(ParameterBuffer *paramBuf, int value);
void ParameterBuffer_SetDmaBankBits(ParameterBuffer *paramBuf, int value);


#endif // DMA_PARAM_BUF_H
