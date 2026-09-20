/*
 * hdlc_frame.h - HDLC frame structure and frame/CRC function declarations.
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

#ifndef HDLC_FRAME_H
#define HDLC_FRAME_H

#include <stdint.h>
#include <stdbool.h>

#define HDLC_FLAG           0x7E
#define HDLC_ESCAPE         0x7D
#define HDLC_ESCAPE_MASK    0x20
#define HDLC_MAX_FRAME_SIZE 1024

typedef enum
{
    HDLC_STATE_IDLE,
    HDLC_STATE_RECEIVING,
    HDLC_STATE_ESCAPE,
    HDLC_STATE_ERROR
} HDLCReceiveState;

typedef struct HDLCFrame
{
    HDLCReceiveState state;
    uint8_t frameBuffer[HDLC_MAX_FRAME_SIZE];
    uint8_t prevByte; // For detecting consecutive flags

    int frameLength;
    uint16_t crc;
    bool frameComplete;
    bool crcValid;
} HDLCFrame;

// CRC-16-CCITT functions

/**
 * @brief Compute the CRC-16-CCITT FCS over a whole buffer, starting from
 *        HDLC_FCS_INIT.
 * @param data Bytes to checksum.
 * @param length Number of bytes in data.
 * @return Final CRC value, or HDLC_FCS_INIT if data is NULL.
 */
uint16_t HDLCFrame_CalculateCRC(const uint8_t *data, int length);

/**
 * @brief Fold one more byte into a running CRC-16-CCITT FCS.
 * @param crc Current CRC accumulator value.
 * @param data Next byte to fold in.
 * @return Updated CRC value.
 */
uint16_t HDLCFrame_UpdateCRC(uint16_t crc, uint8_t data);

// Frame management

/**
 * @brief Initialize a frame object; delegates to HDLCFrame_Reset.
 * @param frame Frame to initialize.
 * @return void.
 */
void HDLCFrame_Init(HDLCFrame *frame);

/**
 * @brief Return a frame to the idle state: clear its length, CRC
 *        accumulator, complete/valid flags and frame buffer.
 * @param frame Frame to reset.
 * @return void.
 */
void HDLCFrame_Reset(HDLCFrame *frame);

/**
 * @brief Feed one already de-stuffed byte into the frame assembler,
 *        detecting flag/escape boundaries and marking the frame complete
 *        with its CRC validity once the closing FLAG and FCS are seen.
 * @param frame Frame being assembled.
 * @param data Next raw (unescaped) byte from the wire.
 * @return true once a complete frame (FLAG + data + FCS + FLAG) has been
 *         assembled, false otherwise; unverified for the exact return value
 *         on error paths.
 */
bool HDLCFrame_AddByte(HDLCFrame *frame, uint8_t data);

/**
 * @brief Test whether frame assembly finished (closing FLAG and FCS seen).
 * @param frame Frame to query.
 * @return Current frameComplete flag, or false if frame is NULL.
 */
bool HDLCFrame_IsFrameComplete(HDLCFrame *frame);

/**
 * @brief Test whether the last assembled frame's FCS matched.
 * @param frame Frame to query.
 * @return Current crcValid flag, or false if frame is NULL.
 */
bool HDLCFrame_IsCRCValid(HDLCFrame *frame);

/**
 * @brief Read the number of bytes accumulated in the frame buffer.
 * @param frame Frame to query.
 * @return Current frameLength, or 0 if frame is NULL.
 */
int HDLCFrame_GetFrameLength(HDLCFrame *frame);

/**
 * @brief Get a pointer to the frame's internal data buffer.
 * @param frame Frame to query.
 * @return Pointer to frameBuffer, or NULL if frame is NULL.
 */
const uint8_t *HDLCFrame_GetFrameData(HDLCFrame *frame);

/**
 * @brief Feed a whole block of bytes into the frame assembler one at a time
 *        via HDLCFrame_AddByte; for unit testing.
 * @param frame Frame being assembled.
 * @param data Bytes to feed in.
 * @param length Number of bytes in data.
 * @return void.
 */
void HDLCFrame_AddBytes(HDLCFrame *frame, const uint8_t *data, int length); // for unit testing

// Frame building

/**
 * @brief Build a complete byte-stuffed HDLC frame on the wire: leading FLAG,
 *        bit/byte-stuffed data and FCS, trailing FLAG.
 * @param data Unstuffed payload bytes to frame.
 * @param dataLength Number of bytes in data.
 * @param outputBuffer Buffer to receive the framed, stuffed bytes.
 * @param bufferSize Capacity of outputBuffer.
 * @return Number of bytes written to outputBuffer, or -1 on invalid
 *         arguments or if outputBuffer is too small.
 */
int HDLCFrame_BuildFrame(const uint8_t *data, int data_length, uint8_t *output_buffer,
                         int buffer_size);

/**
 * @brief Append one byte to outputBuffer, escaping it (ESCAPE + byte XOR
 *        HDLC_ESCAPE_MASK) if it equals HDLC_FLAG or HDLC_ESCAPE.
 * @param data Byte to stuff.
 * @param outputBuffer Buffer to append to.
 * @param bufferSize Capacity of outputBuffer.
 * @param outputIndex In/out current write position within outputBuffer.
 * @return Number of bytes written (1 or 2), or -1 if there is no room or an
 *         argument is NULL.
 */
int HDLCFrame_StuffByte(uint8_t data, uint8_t *output_buffer, int buffer_size, int *output_index);

/**
 * @brief Reverse byte stuffing on a single escaped byte (XOR with
 *        HDLC_ESCAPE_MASK).
 * @param data Escaped byte that followed an HDLC_ESCAPE octet.
 * @return De-stuffed byte value.
 */
uint8_t HDLCFrame_DestuffByte(uint8_t data);

#endif // HDLC_FRAME_H
