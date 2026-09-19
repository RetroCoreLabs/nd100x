/*
 * ndlib.c - Console raw (cbreak) mode handling for POSIX and Windows.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2006 Per-Olof Astrom
 * Copyright (c) 2006-2008 Roger Abrahamsson
 * Copyright (c) 2008 Zdravko
 * Copyright (c) 2025 Ronny Hansen
 *
 * This file is originated from the nd100em project.
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

#include "ndlib_types.h"
#include "ndlib_protos.h"


/* Terminal raw-mode handling (console "cbreak" mode). */

#ifdef _WIN32
/* ---- Windows: SetConsoleMode on stdin ---- */

#include <windows.h>

static DWORD saved_console_mode = 0;
static HANDLE saved_console_handle = NULL;

void unsetcbreak(void)
{
    if (saved_console_handle)
    {
        SetConsoleMode(saved_console_handle, saved_console_mode);
    }
}

void setcbreak(void)
{
    saved_console_handle = GetStdHandle(STD_INPUT_HANDLE);
    if (!saved_console_handle || saved_console_handle == INVALID_HANDLE_VALUE)
    {
        return;
    }

    DWORD mode = 0;
    if (!GetConsoleMode(saved_console_handle, &mode))
    {
        return;
    }
    saved_console_mode = mode;

    /* Clear line-input and echo so keys arrive one at a time without being
     * echoed. Window/menu events are read and discarded by read_key_event()
     * in keyboard.c. */
    mode &= ~(DWORD)(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    mode |= ENABLE_WINDOW_INPUT;
    SetConsoleMode(saved_console_handle, mode);
}

#else
/* ---- POSIX: termios ---- */

#include <termios.h>
#include <signal.h>

static struct termios s_saved_tty;

void unsetcbreak(void)
{
    tcsetattr(0, TCSADRAIN, &s_saved_tty);
}

void setcbreak(void)
{
    struct termios tty;

    /* Ignore SIGTTOU so tcsetattr() won't stop us when running as a
     * background process (e.g. under timeout(1) or make). */
    signal(SIGTTOU, SIG_IGN);

    tcgetattr(0, &s_saved_tty);
    tcgetattr(0, &tty);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN);
    tty.c_cc[VTIME] = (cc_t)0;
    tty.c_cc[VMIN] = (cc_t)0;
    tcsetattr(0, TCSADRAIN, &tty);
}

#endif
