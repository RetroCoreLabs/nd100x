/*
 * terminal_backlog.h - Keys waiting at the source until the terminal's input queue has room.
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

#ifndef TERMINAL_BACKLOG_H
#define TERMINAL_BACKLOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../devices_types.h"

/*
 * The terminal's input queue holds TERMINAL_QUEUE_SIZE keys, and a key queued while it is
 * full is lost (terminal_queue_key_code sets the overrun bit). The keyboard, --pipe and
 * telnet readers of the native build take no more from their source than the queue has
 * room for (terminal_input_room), so a long paste waits at the source. The browser build
 * gets its keys pushed in - a paste or a WebSocket frame arrives all at once - so it puts
 * them here and moves them on as the queue empties (BUGS.md T1).
 *
 * The backlog grows as needed; it never drops a key unless memory runs out.
 */
typedef struct
{
    uint8_t *buf; /* ring of cap bytes */
    size_t cap;
    size_t head;  /* index of the oldest waiting key */
    size_t count; /* keys waiting */
} TerminalBacklog;

/**
 * @brief Add one key at the end of the backlog, growing it if it is full.
 * @param backlog The backlog (NULL gives false).
 * @param keycode The key.
 * @return false only if the backlog could not grow (out of memory); the key is then lost.
 */
bool terminal_backlog_push(TerminalBacklog *backlog, uint8_t keycode);

/**
 * @brief Move waiting keys, oldest first, into the terminal's input queue while it has room.
 * @param backlog The backlog (NULL moves nothing).
 * @param terminal The terminal device (NULL moves nothing).
 * @return Number of keys moved.
 */
size_t terminal_backlog_drain(TerminalBacklog *backlog, Device *terminal);

/**
 * @brief Free the backlog's memory and forget every waiting key.
 * @param backlog The backlog (NULL does nothing).
 */
void terminal_backlog_clear(TerminalBacklog *backlog);

#endif /* TERMINAL_BACKLOG_H */
