/*
 * hdlc_frame.c - HDLC frame building, FCS calculation and completeness checks.
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

#include "hdlc_frame.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "hdlc_crc.h"

// Standard HDLC FCS: init=0xFFFF, good residue=0xF0B8
// clang-format off
#define HDLC_FCS_INIT  0xFFFF
#define HDLC_FCS_GOOD  0xF0B8
// clang-format on

void hdlc_frame_init(HDLCFrame *frame)
{
    if (!frame)
    {
        return;
    }
    hdlc_frame_reset(frame);
}

void hdlc_frame_reset(HDLCFrame *frame)
{
    if (!frame)
    {
        return;
    }

    frame->state = HDLC_STATE_IDLE;
    frame->frameLength = 0;
    frame->crc = HDLC_FCS_INIT;
    frame->frameComplete = false;
    frame->crcValid = false;
    memset(frame->frameBuffer, 0, sizeof(frame->frameBuffer));
}

uint16_t hdlc_frame_update_crc(uint16_t crc, uint8_t data)
{
    return hdlc_crc_crc_calc_ccitt(crc, data);
}

uint16_t hdlc_frame_calculate_crc(const uint8_t *data, int length)
{
    uint16_t crc = HDLC_FCS_INIT;
    if (!data)
    {
        return crc;
    }
    for (int i = 0; i < length; i++)
    {
        crc = hdlc_crc_crc_calc_ccitt(crc, data[i]);
    }
    return crc;
}


void hdlc_frame_add_bytes(HDLCFrame *frame, const uint8_t *data, int length)
{
    for (int i = 0; i < length; i++)
    {
        hdlc_frame_add_byte(frame, data[i]);
    }
}

// -----------------------------------------------------------------------
// Add bytes to a frame buffer.
// DATA/FRAME Bytes must be byte stuffed, i.e., FLAG and ESCAPE bytes must be escaped.
//
// Expects to receive the FCS bytes at the end of the frame.
// Returns true if a complete frame was received (FLAG + data + FCS + FLAG), false otherwise.
// -----------------------------------------------------------------------
bool hdlc_frame_add_byte(HDLCFrame *frame, uint8_t data)
{
    if (!frame)
    {
        return false;
    }

    switch (frame->state)
    {
    case HDLC_STATE_IDLE:
        if (data == HDLC_FLAG)
        {
            hdlc_frame_reset(frame);
            frame->state = HDLC_STATE_RECEIVING;
        }
        break;

    case HDLC_STATE_RECEIVING:
        if (data == HDLC_FLAG)
        {
            if (frame->prevByte == HDLC_FLAG)
            {
                return false; // Ignore consecutive flags - still waiting for frame data
            }

            if (frame->frameLength >= 2)
            {
                // Frame complete - check CRC residue
                // CRC was calculated over all bytes including FCS
                frame->crcValid = (frame->crc == HDLC_FCS_GOOD);
                frame->frameComplete = true;
                frame->state = HDLC_STATE_IDLE;
                return true;
            }
            else
            {
                // Too short or back-to-back flags, start new frame
                hdlc_frame_reset(frame);
                frame->state = HDLC_STATE_RECEIVING;
            }
        }
        else if (data == HDLC_ESCAPE)
        {
            frame->state = HDLC_STATE_ESCAPE;
        }
        else
        {
            if (frame->frameLength < HDLC_MAX_FRAME_SIZE)
            {
                frame->frameBuffer[frame->frameLength++] = data;
                frame->crc = hdlc_crc_crc_calc_ccitt(frame->crc, data);
            }
            else
            {
                frame->state = HDLC_STATE_ERROR;
            }
        }
        break;

    case HDLC_STATE_ESCAPE:
        if (data == HDLC_FLAG)
        {
            // Abort current frame and start new one
            hdlc_frame_reset(frame);
            frame->state = HDLC_STATE_RECEIVING;
        }
        else
        {
            uint8_t destuffed = data ^ HDLC_ESCAPE_MASK;
            if (frame->frameLength < HDLC_MAX_FRAME_SIZE)
            {
                frame->frameBuffer[frame->frameLength++] = destuffed;
                frame->crc = hdlc_crc_crc_calc_ccitt(frame->crc, destuffed);
                frame->state = HDLC_STATE_RECEIVING;
            }
            else
            {
                frame->state = HDLC_STATE_ERROR;
            }
        }
        break;

    case HDLC_STATE_ERROR:
        if (data == HDLC_FLAG)
        {
            hdlc_frame_reset(frame);
            frame->state = HDLC_STATE_RECEIVING;
        }
        break;
    default:
        break;
    }

    frame->prevByte = data;

    return false;
}

bool hdlc_frame_is_frame_complete(HDLCFrame *frame)
{
    return frame ? frame->frameComplete : false;
}

bool hdlc_frame_is_crc_valid(HDLCFrame *frame)
{
    return frame ? frame->crcValid : false;
}

int hdlc_frame_get_frame_length(HDLCFrame *frame)
{
    return frame ? frame->frameLength : 0;
}

const uint8_t *hdlc_frame_get_frame_data(HDLCFrame *frame)
{
    return frame ? frame->frameBuffer : NULL;
}

int hdlc_frame_stuff_byte(uint8_t data, uint8_t *output_buffer, int buffer_size, int *output_index)
{
    if (!output_buffer || !output_index || *output_index >= buffer_size)
    {
        return -1;
    }

    if (data == HDLC_FLAG || data == HDLC_ESCAPE)
    {
        if (*output_index + 1 >= buffer_size)
        {
            return -1;
        }
        output_buffer[(*output_index)++] = HDLC_ESCAPE;
        output_buffer[(*output_index)++] = data ^ HDLC_ESCAPE_MASK;
        return 2;
    }
    else
    {
        output_buffer[(*output_index)++] = data;
        return 1;
    }
}

uint8_t hdlc_frame_destuff_byte(uint8_t data)
{
    return data ^ HDLC_ESCAPE_MASK;
}

// Build a byte-stuffed HDLC frame: FLAG + stuffed(data + FCS) + FLAG
int hdlc_frame_build_frame(const uint8_t *data, int data_length, uint8_t *output_buffer,
                           int buffer_size)
{
    if (!data || !output_buffer || data_length <= 0 || buffer_size < 4)
    {
        return -1;
    }

    int output_index = 0;

    // Start flag
    if (output_index >= buffer_size)
    {
        return -1;
    }
    output_buffer[output_index++] = HDLC_FLAG;

    // Calculate FCS over data (matching C# CreateFrame: init 0xFFFF, then XOR 0xFFFF)
    uint16_t fcs = HDLC_FCS_INIT;
    for (int i = 0; i < data_length; i++)
    {
        fcs = hdlc_crc_crc_calc_ccitt(fcs, data[i]);
    }
    fcs ^= HDLC_FCS_INIT; // Complement (same as C#: crc16 ^ 0xFFFF)

    // Stuff data bytes
    for (int i = 0; i < data_length; i++)
    {
        int stuffed = hdlc_frame_stuff_byte(data[i], output_buffer, buffer_size, &output_index);
        if (stuffed < 0)
        {
            return -1;
        }
    }

    // Stuff FCS bytes (low byte first, matching C#)
    int stuffed = hdlc_frame_stuff_byte(fcs & 0xFF, output_buffer, buffer_size, &output_index);
    if (stuffed < 0)
    {
        return -1;
    }

    stuffed = hdlc_frame_stuff_byte((fcs >> 8) & 0xFF, output_buffer, buffer_size, &output_index);
    if (stuffed < 0)
    {
        return -1;
    }

    // End flag
    if (output_index >= buffer_size)
    {
        return -1;
    }
    output_buffer[output_index++] = HDLC_FLAG;

    return output_index;
}
