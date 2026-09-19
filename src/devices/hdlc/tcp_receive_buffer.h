/*
 * tcp_receive_buffer.h - Ring buffer for bytes received over TCP: structure and API.
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

#ifndef TCP_RECEIVE_BUFFER_H
#define TCP_RECEIVE_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

// Default capacity: 2MB - must be large enough to absorb TX bursts
// without dropping data while the emulation thread drains it
#define TCP_RECV_BUF_DEFAULT_CAPACITY (2 * 1024 * 1024)

// clang-format off
typedef struct {
    uint8_t *buffer;
    int head;       // Write position
    int tail;       // Read position
    int count;      // Bytes available
    int capacity;
} TcpReceiveBuffer;
// clang-format on

// Initialize buffer (caller provides pre-allocated struct)

/**
 * @brief Allocate the backing storage for a ring buffer of the given
 *        capacity and reset its head/tail/count to empty. Caller provides
 *        the pre-allocated TcpReceiveBuffer struct.
 * @param buf Buffer struct to initialize.
 * @param capacity Number of bytes to allocate for the ring.
 * @return void.
 */
void TcpReceiveBuffer_Init(TcpReceiveBuffer *buf, int capacity);

/**
 * @brief Free the ring buffer's backing storage and reset head/tail/count.
 * @param buf Buffer to destroy.
 * @return void.
 */
void TcpReceiveBuffer_Destroy(TcpReceiveBuffer *buf);

/**
 * @brief Copy as much of data as fits into the free space of the ring
 *        buffer, wrapping around the end of the backing array as needed.
 * @param buf Buffer to enqueue into.
 * @param data Bytes to enqueue.
 * @param length Number of bytes in data.
 * @return Number of bytes actually enqueued (less than length if the buffer
 *         did not have enough free space), or 0 if buf, buf->buffer or data
 *         is NULL or length <= 0.
 */
int TcpReceiveBuffer_Enqueue(TcpReceiveBuffer *buf, const uint8_t *data, int length);

// Dequeue a single byte (pull-based). Returns true if byte was read.

/**
 * @brief Pop a single byte from the tail of the ring buffer (pull-based
 *        consumption).
 * @param buf Buffer to dequeue from.
 * @param out Receives the dequeued byte (set to 0 if the buffer is empty).
 * @return true if a byte was read, false if buf, buf->buffer is NULL or the
 *         buffer is empty.
 */
bool TcpReceiveBuffer_DequeueByte(TcpReceiveBuffer *buf, uint8_t *out);

// Number of bytes available to read

/**
 * @brief Read how many bytes are currently queued.
 * @param buf Buffer to query.
 * @return Current byte count, or 0 if buf is NULL.
 */
int TcpReceiveBuffer_Available(TcpReceiveBuffer *buf);

// Clear all data

/**
 * @brief Reset head, tail and count to empty without freeing the backing
 *        storage.
 * @param buf Buffer to clear.
 * @return void.
 */
void TcpReceiveBuffer_Clear(TcpReceiveBuffer *buf);

#endif // TCP_RECEIVE_BUFFER_H
