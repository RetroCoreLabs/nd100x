/*
 * vscreen.h - Virtual screen buffers: structure and API.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef VSCREEN_H
#define VSCREEN_H

#include <stdbool.h>
#include "../../devices/devices_types.h"

#define VSCREEN_MAX       12
#define VSCREEN_BUF_LINES 200
#define VSCREEN_BUF_COLS  132 // Line printer is 132 columns

// clang-format off
typedef struct {
    char name[32];              // Display name ("Console", "Line Printer", etc.)
    Device *device;             // Associated device
    char **lines;               // Ring buffer of line strings
    int lineCount;              // Total lines stored
    int currentLine;            // Current write position in ring buffer
    int cols;                   // Max columns per line
    int curCol;                 // Current column position within current line
    bool isInputCapable;        // Can receive keyboard input
    bool localActive;           // Terminal is claimed for local console use
} VScreen;
// clang-format on

/**
 * @brief Clear a VScreen and allocate its ring buffer of VSCREEN_BUF_LINES lines.
 *        On allocation failure the already allocated lines are freed and lines is
 *        left NULL, which makes the other calls no-ops.
 * @param vs The screen to initialize; NULL is ignored.
 * @param name Display name copied into vs->name.
 * @param dev Device this screen shows output for; may be NULL.
 * @param cols Columns per line; values <= 0 become 80.
 * @param inputCapable true when the screen can receive keyboard input.
 */
void VScreen_Init(VScreen *vs, const char *name, Device *dev, int cols, bool inputCapable);

/**
 * @brief Store one character in the screen's ring buffer. LF and FF advance to a
 *        new cleared line, CR resets the column, any other character is written at
 *        the current column and dropped once the line is full.
 * @param vs The screen to write to; NULL or an uninitialized screen is ignored.
 * @param c The character to store.
 */
void VScreen_Write(VScreen *vs, char c);

/**
 * @brief Clear the real terminal and reprint the screen's header and buffered
 *        lines, oldest first. Input-capable screens are emitted through
 *        charset_emit_host() when a national charset is active; other screens
 *        print raw.
 * @param vs The screen to redraw; NULL or an uninitialized screen is ignored.
 */
void VScreen_Redraw(VScreen *vs);

/**
 * @brief Free the screen's line ring buffer and set lines to NULL.
 * @param vs The screen to destroy; NULL or an uninitialized screen is ignored.
 */
void VScreen_Destroy(VScreen *vs);

#endif // VSCREEN_H
