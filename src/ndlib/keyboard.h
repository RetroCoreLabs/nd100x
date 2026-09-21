/*
 * keyboard.h - Keyboard input: non-blocking key event reading API.
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

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "ndlib_types.h" // KeyType, KeyEvent

/**
 * @brief Read the next keyboard event without blocking.
 * @details POSIX: parses xterm-style byte sequences from stdin and classifies
 *          them. Windows: uses ReadConsoleInputW() to read native key events
 *          and classifies them directly from VK_* codes and control-key
 *          modifiers - no synthetic escape sequences.
 * @return The event; its type is KEY_NONE when no input is available.
 */
KeyEvent kbd_read_key_event(void);

/**
 * @brief Enable --pipe mode: read keyboard bytes from a redirected stdin (a
 *        parent process or automation driver) instead of the interactive
 *        console.
 * @details No effect on POSIX, where read_key_event() already polls stdin. On
 *          Windows it switches read_key_event() from ReadConsoleInputW() to a
 *          non-blocking one-byte-per-call read of the stdin pipe or file.
 * @param on true selects pipe mode, false the interactive console.
 */
void kbd_set_pipe_mode(bool on);

#endif // KEYBOARD_H
