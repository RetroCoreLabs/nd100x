/*
 * pdfwriter.c - Minimal PDF 1.4 writer for monospaced Courier text pages.
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

/*
 * Minimal PDF 1.4 generator for monospaced text output.
 *
 * Uses the 4 standard Courier Type1 fonts (built into every PDF reader,
 * no embedding required):
 *   Courier, Courier-Bold, Courier-Oblique, Courier-BoldOblique
 *
 * Structure:
 *   obj 1: Catalog
 *   obj 2: Pages
 *   obj 3: Courier
 *   obj 4: Courier-Bold
 *   obj 5: Courier-Oblique
 *   obj 6: Courier-BoldOblique
 *   obj 7..N: Page objects
 *   obj N+1..2N-6: Page content streams
 *   xref table
 *   trailer
 */

#include "pdfwriter.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Initial capacities
#define INITIAL_PAGES 8
#define INITIAL_SPANS 64

// Fixed object numbers for fonts
#define OBJ_CATALOG          1
#define OBJ_PAGES            2
#define OBJ_FONT_REGULAR     3
#define OBJ_FONT_BOLD        4
#define OBJ_FONT_OBLIQUE     5
#define OBJ_FONT_BOLDOBLIQUE 6
#define OBJ_FIRST_PAGE       7

// A4 page size in points
#define A4_WIDTH  595.28f
#define A4_HEIGHT 841.89f

PdfDocument *pdf_create(void)
{
    PdfDocument *doc = calloc(1, sizeof(PdfDocument));
    if (!doc)
    {
        return NULL;
    }

    doc->pageWidth = A4_WIDTH;
    doc->pageHeight = A4_HEIGHT;
    doc->pageCapacity = INITIAL_PAGES;
    doc->pages = calloc(doc->pageCapacity, sizeof(PdfPage));
    if (!doc->pages)
    {
        free(doc);
        return NULL;
    }
    return doc;
}

int pdf_add_page(PdfDocument *doc)
{
    if (!doc)
    {
        return -1;
    }

    if (doc->pageCount >= doc->pageCapacity)
    {
        int new_cap = doc->pageCapacity * 2;
        PdfPage *tmp = realloc(doc->pages, new_cap * sizeof(PdfPage));
        if (!tmp)
        {
            return -1;
        }
        memset(tmp + doc->pageCapacity, 0, (new_cap - doc->pageCapacity) * sizeof(PdfPage));
        doc->pages = tmp;
        doc->pageCapacity = new_cap;
    }

    PdfPage *page = &doc->pages[doc->pageCount];
    page->spanCapacity = INITIAL_SPANS;
    page->spans = calloc(page->spanCapacity, sizeof(PdfTextSpan));
    if (!page->spans)
    {
        return -1;
    }
    page->spanCount = 0;

    return doc->pageCount++;
}

void pdf_add_text_span(PdfDocument *doc, int page_index, float x, float y, uint8_t style,
                       float font_size, const char *text)
{
    if (!doc || page_index < 0 || page_index >= doc->pageCount || !text)
    {
        return;
    }

    PdfPage *page = &doc->pages[page_index];
    if (page->spanCount >= page->spanCapacity)
    {
        int new_cap = page->spanCapacity * 2;
        PdfTextSpan *tmp = realloc(page->spans, new_cap * sizeof(PdfTextSpan));
        if (!tmp)
        {
            return;
        }
        page->spans = tmp;
        page->spanCapacity = new_cap;
    }

    PdfTextSpan *span = &page->spans[page->spanCount++];
    span->x = x;
    span->y = y;
    span->style = style;
    span->fontSize = font_size;
    span->text = strdup(text);
    if (!span->text)
    {
        page->spanCount--; /* out of memory: drop this span */
    }
}

// Select font name based on style flags
static const char *pdf_font_name(uint8_t style)
{
    bool bold = (style & PDF_STYLE_BOLD) != 0;
    bool italic = (style & PDF_STYLE_ITALIC) != 0;

    if (bold && italic)
    {
        return "/F4";
    }
    if (bold)
    {
        return "/F2";
    }
    if (italic)
    {
        return "/F3";
    }
    return "/F1";
}

// Build the content stream for one page into a dynamic buffer
static char *build_page_content(PdfPage *page, size_t *out_len)
{
    // Estimate: each span ~100 bytes
    size_t buf_size = 256 + page->spanCount * 128;
    char *buf = malloc(buf_size);
    if (!buf)
    {
        *out_len = 0;
        return NULL;
    }

    size_t pos = 0;

    pos += snprintf(buf + pos, buf_size - pos, "BT\n");

    for (int i = 0; i < page->spanCount; i++)
    {
        PdfTextSpan *span = &page->spans[i];

        // Grow buffer if needed
        size_t text_len = span->text ? strlen(span->text) : 0;
        size_t needed = pos + text_len * 2 + 256;
        if (needed > buf_size)
        {
            buf_size = needed * 2;
            char *tmp = realloc(buf, buf_size);
            if (!tmp)
            {
                free(buf);
                *out_len = 0;
                return NULL;
            }
            buf = tmp;
        }

        // Font selection
        pos += snprintf(buf + pos, buf_size - pos, "%s %.1f Tf\n", pdf_font_name(span->style),
                        span->fontSize);

        // Position (Tm sets absolute text matrix, unlike Td which is relative)
        pos += snprintf(buf + pos, buf_size - pos, "1 0 0 1 %.2f %.2f Tm\n", span->x, span->y);

        // Text
        // Escape manually into buf
        buf[pos++] = '(';
        for (const char *p = span->text; p && *p; p++)
        {
            if (*p == '(' || *p == ')' || *p == '\\')
            {
                buf[pos++] = '\\';
            }
            buf[pos++] = *p;
        }
        buf[pos++] = ')';
        pos += snprintf(buf + pos, buf_size - pos, " Tj\n");

        // Underline: draw a line under the text
        if (span->style & PDF_STYLE_UNDERLINE)
        {
            float char_width = span->fontSize * 0.6f; // Courier character width
            float line_width = char_width * text_len;
            float line_y = span->y - 2.0f;

            pos += snprintf(buf + pos, buf_size - pos,
                            "ET\n"
                            "0.5 w\n"
                            "%.2f %.2f m %.2f %.2f l S\n"
                            "BT\n",
                            span->x, line_y, span->x + line_width, line_y);
        }
    }

    pos += snprintf(buf + pos, buf_size - pos, "ET\n");

    *out_len = pos;
    return buf;
}

/* Header, catalog (object 1), page tree (object 2) and the four fonts
 * (objects 3-6); records each object's file offset in offsets[]. */
static void write_head_objects(FILE *f, int64_t *offsets, int num_pages)
{
    // Header
    fprintf(f, "%%PDF-1.4\n");

    // Object 1: Catalog
    offsets[1] = ftell(f);
    fprintf(f, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

    // Object 2: Pages
    offsets[2] = ftell(f);
    fprintf(f, "2 0 obj\n<< /Type /Pages /Kids [");
    for (int i = 0; i < num_pages; i++)
    {
        fprintf(f, " %d 0 R", OBJ_FIRST_PAGE + i);
    }
    fprintf(f, " ] /Count %d >>\nendobj\n", num_pages);

    // Objects 3-6: Fonts
    static const char *font_names[] = {"Courier", "Courier-Bold", "Courier-Oblique",
                                       "Courier-BoldOblique"};
    for (int i = 0; i < 4; i++)
    {
        int obj_num = OBJ_FONT_REGULAR + i;
        offsets[obj_num] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /%s >>\nendobj\n", obj_num,
                font_names[i]);
    }
}

/* Cross-reference table and trailer for objects 1..total_objects. */
static void write_xref_and_trailer(FILE *f, const int64_t *offsets, int total_objects)
{
    // Cross-reference table
    int64_t xref_offset = ftell(f);
    fprintf(f, "xref\n0 %d\n", total_objects + 1);
    fprintf(f, "0000000000 65535 f \n");
    for (int i = 1; i <= total_objects; i++)
    {
        fprintf(f, "%010" PRId64 " 00000 n \n", offsets[i]);
    }

    // Trailer
    fprintf(f, "trailer\n<< /Size %d /Root 1 0 R >>\n", total_objects + 1);
    fprintf(f, "startxref\n%" PRId64 "\n%%%%EOF\n", xref_offset);
}

bool pdf_write_to_file(PdfDocument *doc, const char *filename)
{
    if (!doc || !filename || doc->pageCount == 0)
    {
        return false;
    }

    FILE *f = fopen(filename, "wb");
    if (!f)
    {
        return false;
    }

    int num_pages = doc->pageCount;
    // Total objects: catalog + pages + 4 fonts + numPages page objs + numPages content objs
    int total_objects = 6 + num_pages * 2;

    // Track byte offsets for xref
    int64_t *offsets = calloc(total_objects + 1, sizeof(int64_t));
    if (!offsets)
    {
        fclose(f);
        return false;
    }

    write_head_objects(f, offsets, num_pages);

    // Build content streams first so we know their lengths
    char **content_bufs = calloc(num_pages, sizeof(char *));
    size_t *content_lens = calloc(num_pages, sizeof(size_t));

    for (int i = 0; i < num_pages; i++)
    {
        content_bufs[i] = build_page_content(&doc->pages[i], &content_lens[i]);
    }

    // Page objects (OBJ_FIRST_PAGE .. OBJ_FIRST_PAGE + numPages - 1)
    int content_obj_base = OBJ_FIRST_PAGE + num_pages;
    for (int i = 0; i < num_pages; i++)
    {
        int page_obj = OBJ_FIRST_PAGE + i;
        int cont_obj = content_obj_base + i;
        offsets[page_obj] = ftell(f);
        fprintf(f,
                "%d 0 obj\n"
                "<< /Type /Page /Parent 2 0 R\n"
                "   /MediaBox [0 0 %.2f %.2f]\n"
                "   /Contents %d 0 R\n"
                "   /Resources << /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R /F4 6 0 R >> >>\n"
                ">>\nendobj\n",
                page_obj, doc->pageWidth, doc->pageHeight, cont_obj);
    }

    // Content stream objects
    for (int i = 0; i < num_pages; i++)
    {
        int cont_obj = content_obj_base + i;
        offsets[cont_obj] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Length %zu >>\nstream\n", cont_obj, content_lens[i]);
        if (content_bufs[i] && content_lens[i] > 0)
        {
            fwrite(content_bufs[i], 1, content_lens[i], f);
        }
        fprintf(f, "endstream\nendobj\n");
    }

    write_xref_and_trailer(f, offsets, total_objects);

    // Cleanup
    for (int i = 0; i < num_pages; i++)
    {
        free(content_bufs[i]);
    }
    free(content_bufs);
    free(content_lens);
    free(offsets);
    fclose(f);
    return true;
}

void pdf_destroy(PdfDocument *doc)
{
    if (!doc)
    {
        return;
    }

    for (int i = 0; i < doc->pageCount; i++)
    {
        PdfPage *page = &doc->pages[i];
        for (int j = 0; j < page->spanCount; j++)
        {
            free(page->spans[j].text);
        }
        free(page->spans);
    }
    free(doc->pages);
    free(doc);
}
