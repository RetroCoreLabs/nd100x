/*
 * escp.h - ESC/P interpreter: span, attribute and context definitions.
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

/**
 * @brief Allocate an ESC/P interpreter context with an empty span array and
 *        line buffer, and set the printer defaults (10 cpi, 1/6 inch line
 *        spacing, 66 lines per page, no attributes).
 * @return New context the caller frees with Escp_Destroy(), or NULL if any
 *         allocation failed.
 */
EscpContext *Escp_Create(void);

/**
 * @brief Feed one byte to the interpreter state machine, which appends
 *        printable characters to the current line and applies ESC commands
 *        (attributes, pitch, line spacing, page geometry, positioning).
 * @param ctx Interpreter context; NULL is ignored.
 * @param c   The byte received from the emulated printer port.
 */
void Escp_PutChar(EscpContext *ctx, uint8_t c);

/**
 * @brief Flush the pending line and return the accumulated styled spans.
 * @details The spans stay owned by ctx and are freed by Escp_Reset() or
 *          Escp_Destroy(); the caller must not free them.
 * @param ctx   Interpreter context; NULL yields NULL and a count of 0.
 * @param count Receives the number of spans; may be NULL.
 * @return Pointer to the first span, or NULL when ctx is NULL.
 */
const EscpSpan *Escp_GetSpans(EscpContext *ctx, int *count);

/**
 * @brief Number of pages produced so far.
 * @param ctx Interpreter context; NULL yields 0.
 * @return Current 0-based page index plus one, or 0 when nothing has been
 *         printed yet.
 */
int Escp_GetPageCount(EscpContext *ctx);

/**
 * @brief Free all span text, drop the line buffer and return the parser,
 *        column, line, page and printer settings to their defaults.
 * @param ctx Interpreter context; NULL is ignored.
 */
void Escp_Reset(EscpContext *ctx);

/**
 * @brief Free the span text, the span array, the line buffer and the context.
 * @param ctx Interpreter context; NULL is ignored.
 */
void Escp_Destroy(EscpContext *ctx);

/**
 * @brief Run one byte through the ESC command state machine for text-only
 *        output, returning just the characters that should appear in a
 *        plain text file.
 * @details Passes through printable ASCII 0x20-0x7E plus CR, LF, FF and TAB;
 *          ESC sequences and their parameter bytes, and all other control
 *          characters, are swallowed.
 * @param ctx Interpreter context; NULL yields 0.
 * @param c   The byte received from the emulated printer port.
 * @return The character to emit, or 0 when the byte was consumed.
 */
char Escp_StripToPlainChar(EscpContext *ctx, uint8_t c);

#endif /* ESCP_H */
