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

#include "hdlcFrame.h"
#include "hdlc_crc.h"

// Standard HDLC FCS: init=0xFFFF, good residue=0xF0B8
#define HDLC_FCS_INIT  0xFFFF
#define HDLC_FCS_GOOD  0xF0B8

void HDLCFrame_Init(HDLCFrame *frame)
{
    if (!frame) return;
    HDLCFrame_Reset(frame);
}

void HDLCFrame_Reset(HDLCFrame *frame)
{
    if (!frame) return;

    frame->state = HDLC_STATE_IDLE;
    frame->frameLength = 0;
    frame->crc = HDLC_FCS_INIT;
    frame->frameComplete = false;
    frame->crcValid = false;
    memset(frame->frameBuffer, 0, sizeof(frame->frameBuffer));
}

uint16_t HDLCFrame_UpdateCRC(uint16_t crc, uint8_t data)
{
    return HDLC_CRC_CalcCCITT(crc, data);
}

uint16_t HDLCFrame_CalculateCRC(const uint8_t *data, int length)
{
    uint16_t crc = HDLC_FCS_INIT;
    if (!data) return crc;
    for (int i = 0; i < length; i++) {
        crc = HDLC_CRC_CalcCCITT(crc, data[i]);
    }
    return crc;
}


void HDLCFrame_AddBytes(HDLCFrame *frame, const uint8_t *data, int length)
{
    for (int i = 0; i < length; i++)
    {
        HDLCFrame_AddByte(frame, data[i]);
    }
}

// -----------------------------------------------------------------------
// Add bytes to a frame buffer.
// DATA/FRAME Bytes must be byte stuffed, i.e., FLAG and ESCAPE bytes must be escaped.
//
// Expects to receive the FCS bytes at the end of the frame.
// Returns true if a complete frame was received (FLAG + data + FCS + FLAG), false otherwise.
// -----------------------------------------------------------------------
bool HDLCFrame_AddByte(HDLCFrame *frame, uint8_t data)
{
    if (!frame) return false;

    switch (frame->state) {
        case HDLC_STATE_IDLE:
            if (data == HDLC_FLAG) {
                HDLCFrame_Reset(frame);
                frame->state = HDLC_STATE_RECEIVING;
            }
            break;

        case HDLC_STATE_RECEIVING:
            if (data == HDLC_FLAG) {
                if (frame->prevByte == HDLC_FLAG)
                {
                    return false; // Ignore consecutive flags - still waiting for frame data
                }

                if (frame->frameLength >= 2) {
                    // Frame complete - check CRC residue
                    // CRC was calculated over all bytes including FCS
                    frame->crcValid = (frame->crc == HDLC_FCS_GOOD);
                    frame->frameComplete = true;
                    frame->state = HDLC_STATE_IDLE;
                    return true;
                } else {
                    // Too short or back-to-back flags, start new frame
                    HDLCFrame_Reset(frame);
                    frame->state = HDLC_STATE_RECEIVING;
                }
            } else if (data == HDLC_ESCAPE) {
                frame->state = HDLC_STATE_ESCAPE;
            } else {
                if (frame->frameLength < HDLC_MAX_FRAME_SIZE) {
                    frame->frameBuffer[frame->frameLength++] = data;
                    frame->crc = HDLC_CRC_CalcCCITT(frame->crc, data);
                } else {
                    frame->state = HDLC_STATE_ERROR;
                }
            }
            break;

        case HDLC_STATE_ESCAPE:
            if (data == HDLC_FLAG) {
                // Abort current frame and start new one
                HDLCFrame_Reset(frame);
                frame->state = HDLC_STATE_RECEIVING;
            } else {
                uint8_t destuffed = data ^ HDLC_ESCAPE_MASK;
                if (frame->frameLength < HDLC_MAX_FRAME_SIZE) {
                    frame->frameBuffer[frame->frameLength++] = destuffed;
                    frame->crc = HDLC_CRC_CalcCCITT(frame->crc, destuffed);
                    frame->state = HDLC_STATE_RECEIVING;
                } else {
                    frame->state = HDLC_STATE_ERROR;
                }
            }
            break;

        case HDLC_STATE_ERROR:
            if (data == HDLC_FLAG) {
                HDLCFrame_Reset(frame);
                frame->state = HDLC_STATE_RECEIVING;
            }
            break;
    }

    frame->prevByte = data;

    return false;
}

bool HDLCFrame_IsFrameComplete(HDLCFrame *frame)
{
    return frame ? frame->frameComplete : false;
}

bool HDLCFrame_IsCRCValid(HDLCFrame *frame)
{
    return frame ? frame->crcValid : false;
}

int HDLCFrame_GetFrameLength(HDLCFrame *frame)
{
    return frame ? frame->frameLength : 0;
}

const uint8_t* HDLCFrame_GetFrameData(HDLCFrame *frame)
{
    return frame ? frame->frameBuffer : NULL;
}

int HDLCFrame_StuffByte(uint8_t data, uint8_t *outputBuffer, int bufferSize, int *outputIndex)
{
    if (!outputBuffer || !outputIndex || *outputIndex >= bufferSize) {
        return -1;
    }

    if (data == HDLC_FLAG || data == HDLC_ESCAPE) {
        if (*outputIndex + 1 >= bufferSize) {
            return -1;
        }
        outputBuffer[(*outputIndex)++] = HDLC_ESCAPE;
        outputBuffer[(*outputIndex)++] = data ^ HDLC_ESCAPE_MASK;
        return 2;
    } else {
        outputBuffer[(*outputIndex)++] = data;
        return 1;
    }
}

uint8_t HDLCFrame_DestuffByte(uint8_t data)
{
    return data ^ HDLC_ESCAPE_MASK;
}

// Build a byte-stuffed HDLC frame: FLAG + stuffed(data + FCS) + FLAG
int HDLCFrame_BuildFrame(const uint8_t *data, int dataLength, uint8_t *outputBuffer, int bufferSize)
{
    if (!data || !outputBuffer || dataLength <= 0 || bufferSize < 4) {
        return -1;
    }

    int outputIndex = 0;

    // Start flag
    if (outputIndex >= bufferSize) return -1;
    outputBuffer[outputIndex++] = HDLC_FLAG;

    // Calculate FCS over data (matching C# CreateFrame: init 0xFFFF, then XOR 0xFFFF)
    uint16_t fcs = HDLC_FCS_INIT;
    for (int i = 0; i < dataLength; i++) {
        fcs = HDLC_CRC_CalcCCITT(fcs, data[i]);
    }
    fcs ^= HDLC_FCS_INIT; // Complement (same as C#: crc16 ^ 0xFFFF)

    // Stuff data bytes
    for (int i = 0; i < dataLength; i++) {
        int stuffed = HDLCFrame_StuffByte(data[i], outputBuffer, bufferSize, &outputIndex);
        if (stuffed < 0) return -1;
    }

    // Stuff FCS bytes (low byte first, matching C#)
    int stuffed = HDLCFrame_StuffByte(fcs & 0xFF, outputBuffer, bufferSize, &outputIndex);
    if (stuffed < 0) return -1;

    stuffed = HDLCFrame_StuffByte((fcs >> 8) & 0xFF, outputBuffer, bufferSize, &outputIndex);
    if (stuffed < 0) return -1;

    // End flag
    if (outputIndex >= bufferSize) return -1;
    outputBuffer[outputIndex++] = HDLC_FLAG;

    return outputIndex;
}
