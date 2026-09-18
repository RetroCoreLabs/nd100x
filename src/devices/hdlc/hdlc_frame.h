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

#ifndef HDLC_FRAME_H
#define HDLC_FRAME_H

#include <stdint.h>
#include <stdbool.h>

#define HDLC_FLAG 0x7E
#define HDLC_ESCAPE 0x7D
#define HDLC_ESCAPE_MASK 0x20
#define HDLC_MAX_FRAME_SIZE 1024

typedef enum {
    HDLC_STATE_IDLE,
    HDLC_STATE_RECEIVING,
    HDLC_STATE_ESCAPE,
    HDLC_STATE_ERROR
} HDLCReceiveState;

typedef struct HDLCFrame {
    HDLCReceiveState state;
    uint8_t frameBuffer[HDLC_MAX_FRAME_SIZE];
    uint8_t prevByte; // For detecting consecutive flags

    int frameLength;
    uint16_t crc;
    bool frameComplete;
    bool crcValid;
} HDLCFrame;

// CRC-16-CCITT functions
uint16_t HDLCFrame_CalculateCRC(const uint8_t *data, int length);
uint16_t HDLCFrame_UpdateCRC(uint16_t crc, uint8_t data);

// Frame management
void HDLCFrame_Init(HDLCFrame *frame);
void HDLCFrame_Reset(HDLCFrame *frame);
bool HDLCFrame_AddByte(HDLCFrame *frame, uint8_t data);
bool HDLCFrame_IsFrameComplete(HDLCFrame *frame);
bool HDLCFrame_IsCRCValid(HDLCFrame *frame);
int HDLCFrame_GetFrameLength(HDLCFrame *frame);
const uint8_t* HDLCFrame_GetFrameData(HDLCFrame *frame);
void HDLCFrame_AddBytes(HDLCFrame *frame, const uint8_t *data, int length); // for unit testing

// Frame building
int HDLCFrame_BuildFrame(const uint8_t *data, int dataLength, uint8_t *outputBuffer, int bufferSize);
int HDLCFrame_StuffByte(uint8_t data, uint8_t *outputBuffer, int bufferSize, int *outputIndex);
uint8_t HDLCFrame_DestuffByte(uint8_t data);

#endif // HDLC_FRAME_H