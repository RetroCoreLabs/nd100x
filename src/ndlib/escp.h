/*
 * nd100x - ND100 Virtual Machine
 *
 * Copyright (c) 2025 Ronny Hansen
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

#ifndef ESCP_H
#define ESCP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Text attribute flags
#define ESCP_ATTR_BOLD      0x01
#define ESCP_ATTR_ITALIC    0x02
#define ESCP_ATTR_UNDERLINE 0x04

// A styled text span produced by the ESC/P interpreter
typedef struct EscpSpan
{
    int column;       // Column position (0-based)
    int line;         // Line number on current page (0-based)
    int page;         // Page number (0-based)
    uint8_t attrs;    // Combination of ESCP_ATTR_* flags
    float charWidth;  // Character width in points (based on pitch)
    float lineHeight; // Line height in points
    char *text;       // Text content (allocated)
} EscpSpan;

// Parser state machine states
typedef enum
{
    ESCP_STATE_NORMAL,
    ESCP_STATE_ESC_SEEN,
    ESCP_STATE_PARAM
} EscpState;

// The ESC/P interpreter context
typedef struct EscpContext
{
    // Parser state
    EscpState state;
    uint8_t currentCommand; // ESC command byte being processed
    int paramsNeeded;       // Number of parameter bytes expected
    int paramsReceived;     // Number of parameter bytes received
    uint8_t params[4];      // Parameter buffer

    // Position tracking
    int column; // Current column (0-based)
    int line;   // Current line on page (0-based)
    int page;   // Current page (0-based)

    // Text attributes
    uint8_t attrs; // Active ESCP_ATTR_* flags
    bool expanded; // Double-width mode

    // Pitch: characters per inch (default 10 = Pica)
    int cpi;
    bool condensed;

    // Line spacing in 1/216 inch units (default: 1/6" = 36/216)
    int lineSpacing216;

    // Page geometry (in lines at current spacing)
    int pageLines; // Lines per page (default 66 for 11" paper at 1/6")

    // Output: accumulated spans
    EscpSpan *spans;
    int spanCount;
    int spanCapacity;

    // Current line buffer (accumulated until newline/formfeed)
    char *lineBuf;
    int lineBufLen;
    int lineBufCap;
} EscpContext;

// Create a new ESC/P interpreter context
EscpContext *Escp_Create(void);

// Feed a single byte to the interpreter
void Escp_PutChar(EscpContext *ctx, uint8_t c);

// Get accumulated spans (caller does NOT free these; they are owned by ctx)
const EscpSpan *Escp_GetSpans(EscpContext *ctx, int *count);

// Get current page count
int Escp_GetPageCount(EscpContext *ctx);

// Reset the interpreter (clear all spans and state)
void Escp_Reset(EscpContext *ctx);

// Free the interpreter context and all its data
void Escp_Destroy(EscpContext *ctx);

// Strip ESC/P codes from a byte, returns the printable char or 0 if consumed
// (Convenience for text-only output: feed bytes through this to get plain text)
char Escp_StripToPlainChar(EscpContext *ctx, uint8_t c);

#endif /* ESCP_H */
