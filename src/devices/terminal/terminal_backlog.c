/*
 * terminal_backlog.c - Keys waiting at the source until the terminal's input queue has room.
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

#include <stdlib.h>
#include <string.h>

#include "terminal_backlog.h"
#include "device_terminal.h"

#define TERMINAL_BACKLOG_FIRST_CAP 1024u

bool terminal_backlog_push(TerminalBacklog *backlog, uint8_t keycode)
{
    if (!backlog)
    {
        return false;
    }

    if (backlog->count == backlog->cap)
    {
        // Full: double the ring and lay the waiting keys out from index 0 again.
        size_t newCap = (backlog->cap == 0) ? TERMINAL_BACKLOG_FIRST_CAP : (backlog->cap * 2u);
        uint8_t *newBuf = (uint8_t *)malloc(newCap);
        if (!newBuf)
        {
            return false;
        }
        size_t first = backlog->cap - backlog->head; // keys from head to the end of the old ring
        if (first > backlog->count)
        {
            first = backlog->count;
        }
        if (backlog->count > 0)
        {
            memcpy(newBuf, backlog->buf + backlog->head, first);
            memcpy(newBuf + first, backlog->buf, backlog->count - first);
        }
        free(backlog->buf);
        backlog->buf = newBuf;
        backlog->cap = newCap;
        backlog->head = 0;
    }

    backlog->buf[(backlog->head + backlog->count) % backlog->cap] = keycode;
    backlog->count++;
    return true;
}

size_t terminal_backlog_drain(TerminalBacklog *backlog, Device *terminal)
{
    if (!backlog || !terminal)
    {
        return 0;
    }

    size_t room = terminal_input_room(terminal);
    size_t moved = 0;
    while ((backlog->count > 0) && (moved < room))
    {
        terminal_queue_key_code(terminal, backlog->buf[backlog->head]);
        backlog->head = (backlog->head + 1) % backlog->cap;
        backlog->count--;
        moved++;
    }
    if (backlog->count == 0)
    {
        backlog->head = 0;
    }
    return moved;
}

void terminal_backlog_clear(TerminalBacklog *backlog)
{
    if (!backlog)
    {
        return;
    }
    free(backlog->buf);
    backlog->buf = NULL;
    backlog->cap = 0;
    backlog->head = 0;
    backlog->count = 0;
}
