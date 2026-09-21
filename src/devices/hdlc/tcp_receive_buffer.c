/*
 * tcp_receive_buffer.c - Ring buffer for bytes received over TCP by the HDLC modem.
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

#include "tcp_receive_buffer.h"

#include <stdlib.h>
#include <string.h>

void rxbuf_init(TcpReceiveBuffer *buf, int capacity)
{
    if (!buf)
    {
        return;
    }

    buf->capacity = capacity;
    buf->buffer = malloc((size_t)capacity);
    if (!buf->buffer)
    {
        buf->capacity = 0; /* read/write already test buffer */
    }
    buf->head = 0;
    buf->tail = 0;
    buf->count = 0;
}

void rxbuf_destroy(TcpReceiveBuffer *buf)
{
    if (!buf)
    {
        return;
    }

    if (buf->buffer)
    {
        free(buf->buffer);
        buf->buffer = NULL;
    }
    buf->head = 0;
    buf->tail = 0;
    buf->count = 0;
}

int rxbuf_enqueue(TcpReceiveBuffer *buf, const uint8_t *data, int length)
{
    if (!buf || !buf->buffer || !data || length <= 0)
    {
        return 0;
    }

    int bytes_to_write = length;
    int free_space = buf->capacity - buf->count;
    if (bytes_to_write > free_space)
    {
        bytes_to_write = free_space;
    }
    if (bytes_to_write == 0)
    {
        return 0;
    }

    // Write in two parts if wrapping around
    if (buf->head + bytes_to_write > buf->capacity)
    {
        int first_part = buf->capacity - buf->head;
        memcpy(&buf->buffer[buf->head], data, (size_t)first_part);
        int second_part = bytes_to_write - first_part;
        memcpy(&buf->buffer[0], &data[first_part], (size_t)second_part);
        buf->head = second_part;
    }
    else
    {
        memcpy(&buf->buffer[buf->head], data, (size_t)bytes_to_write);
        buf->head = (buf->head + bytes_to_write) % buf->capacity;
    }

    buf->count += bytes_to_write;
    return bytes_to_write;
}

bool rxbuf_dequeue_byte(TcpReceiveBuffer *buf, uint8_t *out)
{
    if (!buf || !buf->buffer || buf->count == 0)
    {
        if (out)
        {
            *out = 0;
        }
        return false;
    }

    *out = buf->buffer[buf->tail];
    buf->tail = (buf->tail + 1) % buf->capacity;
    buf->count--;
    return true;
}

int rxbuf_available(TcpReceiveBuffer *buf)
{
    if (!buf)
    {
        return 0;
    }
    return buf->count;
}

void rxbuf_clear(TcpReceiveBuffer *buf)
{
    if (!buf)
    {
        return;
    }
    buf->head = 0;
    buf->tail = 0;
    buf->count = 0;
}
