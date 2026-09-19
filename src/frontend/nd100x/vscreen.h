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

void VScreen_Init(VScreen *vs, const char *name, Device *dev, int cols, bool inputCapable);
void VScreen_Write(VScreen *vs, char c);
void VScreen_Redraw(VScreen *vs);
void VScreen_Destroy(VScreen *vs);

#endif // VSCREEN_H
