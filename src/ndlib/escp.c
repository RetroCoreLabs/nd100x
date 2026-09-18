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

/*
 * ESC/P (Epson Standard Code for Printers) interpreter.
 *
 * Processes the byte stream from SINTRAN's EPSON-LX86 driver and produces
 * structured text spans with position and style attributes.
 *
 * Supported commands:
 *   CR (0x0D)        Carriage return
 *   LF (0x0A)        Line feed
 *   FF (0x0C)        Form feed
 *   BS (0x08)        Backspace (overstrike / bold simulation)
 *   SI (0x0F)        Condensed mode on
 *   DC2 (0x12)       Condensed mode off
 *   ESC @            Initialize / reset printer
 *   ESC E            Bold on
 *   ESC F            Bold off
 *   ESC 4            Italic on
 *   ESC 5            Italic off
 *   ESC - n          Underline on (n=1) / off (n=0)
 *   ESC P            Select 10 cpi (Pica)
 *   ESC M            Select 12 cpi (Elite)
 *   ESC W n          Expanded mode on (n=1) / off (n=0)
 *   ESC 0            Set line spacing to 1/8"
 *   ESC 2            Set line spacing to 1/6"
 *   ESC 3 n          Set line spacing to n/216"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "escp.h"

#define INITIAL_SPANS    256
#define INITIAL_LINEBUF  256

// Default line spacing: 1/6 inch = 36/216 inch
#define DEFAULT_LINE_SPACING_216 36
// 1/8 inch = 27/216 inch
#define LINE_SPACING_8TH_216 27

// Default page: 66 lines at 1/6" (11-inch paper)
#define DEFAULT_PAGE_LINES 66

// Points per inch
#define POINTS_PER_INCH 72.0f

// Flush the current line buffer as a span
static void flush_line_buffer(EscpContext *ctx)
{
    if (ctx->lineBufLen == 0) return;

    // Grow spans array if needed
    if (ctx->spanCount >= ctx->spanCapacity) {
        int newCap = ctx->spanCapacity * 2;
        EscpSpan *tmp = realloc(ctx->spans, newCap * sizeof(EscpSpan));
        if (!tmp) return;
        ctx->spans = tmp;
        ctx->spanCapacity = newCap;
    }

    // Calculate character width in points based on pitch
    int effectiveCpi = ctx->cpi;
    if (ctx->condensed) {
        // Condensed roughly multiplies cpi by ~1.7
        effectiveCpi = (effectiveCpi == 10) ? 17 : 20;
    }
    float charWidth = POINTS_PER_INCH / (float)effectiveCpi;
    if (ctx->expanded) {
        charWidth *= 2.0f;
    }

    // Line height in points
    float lineHeight = (ctx->lineSpacing216 / 216.0f) * POINTS_PER_INCH;

    // Null-terminate the line buffer
    ctx->lineBuf[ctx->lineBufLen] = '\0';

    EscpSpan *span = &ctx->spans[ctx->spanCount++];
    span->column = ctx->column - ctx->lineBufLen;  // Start column of this run
    if (span->column < 0) span->column = 0;
    span->line = ctx->line;
    span->page = ctx->page;
    span->attrs = ctx->attrs;
    span->charWidth = charWidth;
    span->lineHeight = lineHeight;
    span->text = strdup(ctx->lineBuf);
    if (!span->text) {
        ctx->spanCount--;   /* out of memory: drop this run of text */
    }

    ctx->lineBufLen = 0;
}

// Add a character to the line buffer
static void buffer_char(EscpContext *ctx, char c)
{
    if (ctx->lineBufLen + 1 >= ctx->lineBufCap) {
        int newCap = ctx->lineBufCap * 2;
        char *tmp = realloc(ctx->lineBuf, newCap);
        if (!tmp) return;
        ctx->lineBuf = tmp;
        ctx->lineBufCap = newCap;
    }
    ctx->lineBuf[ctx->lineBufLen++] = c;
}

// Advance to next line
static void line_feed(EscpContext *ctx)
{
    flush_line_buffer(ctx);
    ctx->line++;
    ctx->column = 0;

    // Check for page overflow
    if (ctx->line >= ctx->pageLines) {
        ctx->page++;
        ctx->line = 0;
    }
}

// Form feed: advance to next page
static void form_feed(EscpContext *ctx)
{
    flush_line_buffer(ctx);
    ctx->page++;
    ctx->line = 0;
    ctx->column = 0;
}

// Reset printer to initial state (ESC @)
static void reset_printer(EscpContext *ctx)
{
    ctx->attrs = 0;
    ctx->expanded = false;
    ctx->cpi = 10;
    ctx->condensed = false;
    ctx->lineSpacing216 = DEFAULT_LINE_SPACING_216;
    ctx->pageLines = DEFAULT_PAGE_LINES;
}

// Handle an ESC command that takes no parameters
static void handle_esc_no_param(EscpContext *ctx, uint8_t cmd)
{
    switch (cmd) {
        case '@':  // Initialize printer
            flush_line_buffer(ctx);
            reset_printer(ctx);
            break;

        case 'E':  // Bold on
            flush_line_buffer(ctx);
            ctx->attrs |= ESCP_ATTR_BOLD;
            break;

        case 'F':  // Bold off
            flush_line_buffer(ctx);
            ctx->attrs &= ~ESCP_ATTR_BOLD;
            break;

        case '4':  // Italic on
            flush_line_buffer(ctx);
            ctx->attrs |= ESCP_ATTR_ITALIC;
            break;

        case '5':  // Italic off
            flush_line_buffer(ctx);
            ctx->attrs &= ~ESCP_ATTR_ITALIC;
            break;

        case 'P':  // 10 cpi (Pica)
            flush_line_buffer(ctx);
            ctx->cpi = 10;
            break;

        case 'M':  // 12 cpi (Elite)
            flush_line_buffer(ctx);
            ctx->cpi = 12;
            break;

        case '0':  // Line spacing 1/8"
            ctx->lineSpacing216 = LINE_SPACING_8TH_216;
            ctx->pageLines = (int)(11.0f * 8.0f);  // 88 lines/page at 1/8"
            break;

        case '2':  // Line spacing 1/6"
            ctx->lineSpacing216 = DEFAULT_LINE_SPACING_216;
            ctx->pageLines = DEFAULT_PAGE_LINES;
            break;
    }
}

// Handle an ESC command that takes 1 parameter byte
static void handle_esc_with_param(EscpContext *ctx, uint8_t cmd, uint8_t param)
{
    switch (cmd) {
        case '-':  // Underline on/off
            flush_line_buffer(ctx);
            if (param & 0x01) {
                ctx->attrs |= ESCP_ATTR_UNDERLINE;
            } else {
                ctx->attrs &= ~ESCP_ATTR_UNDERLINE;
            }
            break;

        case 'W':  // Expanded mode on/off
            flush_line_buffer(ctx);
            ctx->expanded = (param & 0x01) != 0;
            break;

        case '3':  // Line spacing = n/216"
            ctx->lineSpacing216 = param;
            if (param > 0) {
                ctx->pageLines = (int)((11.0f * 216.0f) / (float)param);
            }
            break;
    }
}

// Determine how many parameter bytes a given ESC command needs
static int params_for_command(uint8_t cmd)
{
    switch (cmd) {
        // 1-parameter commands
        case '-':   // Underline
        case 'W':   // Expanded
        case '3':   // n/216" spacing
            return 1;

        // No-parameter commands
        case '@': case 'E': case 'F': case '4': case '5':
        case 'P': case 'M': case '0': case '2':
            return 0;

        default:
            return 0;  // Unknown command: consume no params, ignore
    }
}

EscpContext *Escp_Create(void)
{
    EscpContext *ctx = calloc(1, sizeof(EscpContext));
    if (!ctx) return NULL;

    ctx->spanCapacity = INITIAL_SPANS;
    ctx->spans = calloc(ctx->spanCapacity, sizeof(EscpSpan));
    if (!ctx->spans) {
        free(ctx);
        return NULL;
    }

    ctx->lineBufCap = INITIAL_LINEBUF;
    ctx->lineBuf = malloc(ctx->lineBufCap);
    if (!ctx->lineBuf) {
        free(ctx->spans);
        free(ctx);
        return NULL;
    }

    reset_printer(ctx);
    return ctx;
}

void Escp_PutChar(EscpContext *ctx, uint8_t c)
{
    if (!ctx) return;

    switch (ctx->state) {
        case ESCP_STATE_NORMAL:
            // Control characters
            switch (c) {
                case 0x1B:  // ESC
                    ctx->state = ESCP_STATE_ESC_SEEN;
                    return;

                case 0x0D:  // CR
                    flush_line_buffer(ctx);
                    ctx->column = 0;
                    return;

                case 0x0A:  // LF
                    line_feed(ctx);
                    return;

                case 0x0C:  // FF
                    form_feed(ctx);
                    return;

                case 0x08:  // BS (backspace / overstrike)
                    if (ctx->lineBufLen > 0) {
                        // Remove last char from buffer (overstrike ignored for now)
                        ctx->lineBufLen--;
                    }
                    if (ctx->column > 0) ctx->column--;
                    return;

                case 0x0F:  // SI: condensed on
                    flush_line_buffer(ctx);
                    ctx->condensed = true;
                    return;

                case 0x12:  // DC2: condensed off
                    flush_line_buffer(ctx);
                    ctx->condensed = false;
                    return;

                case 0x07:  // BEL - ignore
                case 0x00:  // NUL - ignore
                    return;
            }

            // Printable character
            if (c >= 0x20 && c <= 0x7E) {
                buffer_char(ctx, (char)c);
                ctx->column++;
            }
            break;

        case ESCP_STATE_ESC_SEEN:
            ctx->currentCommand = c;
            ctx->paramsNeeded = params_for_command(c);
            ctx->paramsReceived = 0;

            if (ctx->paramsNeeded == 0) {
                handle_esc_no_param(ctx, c);
                ctx->state = ESCP_STATE_NORMAL;
            } else {
                ctx->state = ESCP_STATE_PARAM;
            }
            break;

        case ESCP_STATE_PARAM:
            ctx->params[ctx->paramsReceived++] = c;
            if (ctx->paramsReceived >= ctx->paramsNeeded) {
                handle_esc_with_param(ctx, ctx->currentCommand, ctx->params[0]);
                ctx->state = ESCP_STATE_NORMAL;
            }
            break;
    }
}

const EscpSpan *Escp_GetSpans(EscpContext *ctx, int *count)
{
    if (!ctx) { if (count) *count = 0; return NULL; }

    // Flush any pending text
    flush_line_buffer(ctx);

    if (count) *count = ctx->spanCount;
    return ctx->spans;
}

int Escp_GetPageCount(EscpContext *ctx)
{
    if (!ctx) return 0;
    // page is 0-based, so page count = page + 1 (if any content exists)
    if (ctx->spanCount == 0 && ctx->lineBufLen == 0) return 0;
    return ctx->page + 1;
}

void Escp_Reset(EscpContext *ctx)
{
    if (!ctx) return;

    // Free span text
    for (int i = 0; i < ctx->spanCount; i++) {
        free(ctx->spans[i].text);
    }
    ctx->spanCount = 0;
    ctx->lineBufLen = 0;

    ctx->state = ESCP_STATE_NORMAL;
    ctx->column = 0;
    ctx->line = 0;
    ctx->page = 0;

    reset_printer(ctx);
}

void Escp_Destroy(EscpContext *ctx)
{
    if (!ctx) return;

    for (int i = 0; i < ctx->spanCount; i++) {
        free(ctx->spans[i].text);
    }
    free(ctx->spans);
    free(ctx->lineBuf);
    free(ctx);
}

char Escp_StripToPlainChar(EscpContext *ctx, uint8_t c)
{
    if (!ctx) return 0;

    switch (ctx->state) {
        case ESCP_STATE_NORMAL:
            if (c == 0x1B) {
                ctx->state = ESCP_STATE_ESC_SEEN;
                return 0;
            }
            // Pass through printable chars and common control chars
            if (c >= 0x20 && c <= 0x7E) return (char)c;
            if (c == 0x0D || c == 0x0A || c == 0x0C) return (char)c;
            if (c == 0x09) return '\t';
            return 0;  // Swallow other control chars

        case ESCP_STATE_ESC_SEEN:
            ctx->currentCommand = c;
            ctx->paramsNeeded = params_for_command(c);
            ctx->paramsReceived = 0;
            if (ctx->paramsNeeded == 0) {
                ctx->state = ESCP_STATE_NORMAL;
            } else {
                ctx->state = ESCP_STATE_PARAM;
            }
            return 0;

        case ESCP_STATE_PARAM:
            ctx->paramsReceived++;
            if (ctx->paramsReceived >= ctx->paramsNeeded) {
                ctx->state = ESCP_STATE_NORMAL;
            }
            return 0;
    }

    return 0;
}
