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

#include <stdlib.h>
#include <string.h>

#include "tcpReceiveBuffer.h"

void TcpReceiveBuffer_Init(TcpReceiveBuffer *buf, int capacity)
{
    if (!buf) return;

    buf->capacity = capacity;
    buf->buffer = malloc((size_t)capacity);
    if (!buf->buffer) buf->capacity = 0;   /* read/write already test buffer */
    buf->head = 0;
    buf->tail = 0;
    buf->count = 0;
}

void TcpReceiveBuffer_Destroy(TcpReceiveBuffer *buf)
{
    if (!buf) return;

    if (buf->buffer) {
        free(buf->buffer);
        buf->buffer = NULL;
    }
    buf->head = 0;
    buf->tail = 0;
    buf->count = 0;
}

int TcpReceiveBuffer_Enqueue(TcpReceiveBuffer *buf, const uint8_t *data, int length)
{
    if (!buf || !buf->buffer || !data || length <= 0) return 0;

    int bytesToWrite = length;
    int freeSpace = buf->capacity - buf->count;
    if (bytesToWrite > freeSpace) {
        bytesToWrite = freeSpace;
    }
    if (bytesToWrite == 0) return 0;

    // Write in two parts if wrapping around
    if (buf->head + bytesToWrite > buf->capacity) {
        int firstPart = buf->capacity - buf->head;
        memcpy(&buf->buffer[buf->head], data, (size_t)firstPart);
        int secondPart = bytesToWrite - firstPart;
        memcpy(&buf->buffer[0], &data[firstPart], (size_t)secondPart);
        buf->head = secondPart;
    } else {
        memcpy(&buf->buffer[buf->head], data, (size_t)bytesToWrite);
        buf->head = (buf->head + bytesToWrite) % buf->capacity;
    }

    buf->count += bytesToWrite;
    return bytesToWrite;
}

bool TcpReceiveBuffer_DequeueByte(TcpReceiveBuffer *buf, uint8_t *out)
{
    if (!buf || !buf->buffer || buf->count == 0) {
        if (out) *out = 0;
        return false;
    }

    *out = buf->buffer[buf->tail];
    buf->tail = (buf->tail + 1) % buf->capacity;
    buf->count--;
    return true;
}

int TcpReceiveBuffer_Available(TcpReceiveBuffer *buf)
{
    if (!buf) return 0;
    return buf->count;
}

void TcpReceiveBuffer_Clear(TcpReceiveBuffer *buf)
{
    if (!buf) return;
    buf->head = 0;
    buf->tail = 0;
    buf->count = 0;
}
