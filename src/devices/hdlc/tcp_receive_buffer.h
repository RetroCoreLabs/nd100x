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

#ifndef TCP_RECEIVE_BUFFER_H
#define TCP_RECEIVE_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

// Default capacity: 2MB - must be large enough to absorb TX bursts
// without dropping data while the emulation thread drains it
#define TCP_RECV_BUF_DEFAULT_CAPACITY (2 * 1024 * 1024)

typedef struct {
    uint8_t *buffer;
    int head;       // Write position
    int tail;       // Read position
    int count;      // Bytes available
    int capacity;
} TcpReceiveBuffer;

// Initialize buffer (caller provides pre-allocated struct)
void TcpReceiveBuffer_Init(TcpReceiveBuffer *buf, int capacity);
void TcpReceiveBuffer_Destroy(TcpReceiveBuffer *buf);

// Enqueue TCP data. Returns number of bytes actually enqueued.
int TcpReceiveBuffer_Enqueue(TcpReceiveBuffer *buf, const uint8_t *data, int length);

// Dequeue a single byte (pull-based). Returns true if byte was read.
bool TcpReceiveBuffer_DequeueByte(TcpReceiveBuffer *buf, uint8_t *out);

// Number of bytes available to read
int TcpReceiveBuffer_Available(TcpReceiveBuffer *buf);

// Clear all data
void TcpReceiveBuffer_Clear(TcpReceiveBuffer *buf);

#endif // TCP_RECEIVE_BUFFER_H
