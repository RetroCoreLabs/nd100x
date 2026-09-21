/*
 * vscreen.c - Virtual screen buffers: write, redraw and destroy.
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

#include "vscreen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "charset.h"

void vscreen_init(VScreen *vs, const char *name, Device *dev, int cols, bool input_capable)
{
    if (!vs)
    {
        return;
    }

    memset(vs, 0, sizeof(VScreen));
    snprintf(vs->name, sizeof(vs->name), "%s", name);
    vs->device = dev;
    vs->cols = (cols > 0) ? cols : 80;
    vs->isInputCapable = input_capable;
    vs->localActive = true;
    vs->lineCount = 0;
    vs->currentLine = 0;
    vs->curCol = 0;

    // Allocate line buffer
    vs->lines = calloc(VSCREEN_BUF_LINES, sizeof(char *));
    if (vs->lines)
    {
        for (int i = 0; i < VSCREEN_BUF_LINES; i++)
        {
            vs->lines[i] = calloc(vs->cols + 1, sizeof(char));
            if (!vs->lines[i])
            {
                // Free previously allocated lines on failure
                for (int j = 0; j < i; j++)
                {
                    free(vs->lines[j]);
                }
                free(vs->lines);
                vs->lines = NULL;
                break;
            }
        }
    }
}

void vscreen_write(VScreen *vs, char c)
{
    if (!vs || !vs->lines)
    {
        return;
    }

    switch (c)
    {
    case '\n': // Line feed - move to next line
        vs->currentLine = (vs->currentLine + 1) % VSCREEN_BUF_LINES;
        if (vs->lineCount < VSCREEN_BUF_LINES)
        {
            vs->lineCount++;
        }
        memset(vs->lines[vs->currentLine], 0, vs->cols + 1);
        vs->curCol = 0;
        break;

    case '\r': // Carriage return
        vs->curCol = 0;
        break;

    case '\f': // Form feed
        vs->currentLine = (vs->currentLine + 1) % VSCREEN_BUF_LINES;
        if (vs->lineCount < VSCREEN_BUF_LINES)
        {
            vs->lineCount++;
        }
        memset(vs->lines[vs->currentLine], 0, vs->cols + 1);
        vs->curCol = 0;
        break;

    default:
        if (vs->curCol < vs->cols)
        {
            vs->lines[vs->currentLine][vs->curCol] = c;
            vs->curCol++;
        }
        break;
    }
}

void vscreen_redraw(VScreen *vs)
{
    if (!vs || !vs->lines)
    {
        return;
    }

    // Clear the physical terminal
    printf("\033[2J\033[H");

    // Print header
    printf("=== %s ===\n", vs->name);

    // Determine the starting line (oldest line in the buffer)
    int num_lines = (vs->lineCount < VSCREEN_BUF_LINES) ? vs->lineCount : VSCREEN_BUF_LINES;
    int start_line;
    if (num_lines == 0)
    {
        return;
    }

    if (vs->lineCount < VSCREEN_BUF_LINES)
    {
        start_line = 0;
    }
    else
    {
        start_line = (vs->currentLine + 1) % VSCREEN_BUF_LINES;
    }

    // Print all buffered lines. Terminal screens get national 7-bit charset
    // translation (per-char) so a redraw matches live output; non-terminal
    // screens (printer, tape, log) are emitted raw.
    for (int i = 0; i < num_lines; i++)
    {
        int line_idx = (start_line + i) % VSCREEN_BUF_LINES;
        const char *line = vs->lines[line_idx];
        if (vs->isInputCapable && charset_get() != CHARSET_OFF)
        {
            for (const char *p = line; *p; p++)
            {
                charset_emit_host(*p);
            }
            putchar('\n');
        }
        else
        {
            printf("%s\n", line);
        }
    }

    fflush(stdout);
}

void vscreen_destroy(VScreen *vs)
{
    if (!vs || !vs->lines)
    {
        return;
    }

    for (int i = 0; i < VSCREEN_BUF_LINES; i++)
    {
        if (vs->lines[i])
        {
            free(vs->lines[i]);
        }
    }
    free(vs->lines);
    vs->lines = NULL;
}
