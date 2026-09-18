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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pdfwriter.h"

// Initial capacities
#define INITIAL_PAGES 8
#define INITIAL_SPANS 64

// Fixed object numbers for fonts
#define OBJ_CATALOG 1
#define OBJ_PAGES   2
#define OBJ_FONT_REGULAR     3
#define OBJ_FONT_BOLD        4
#define OBJ_FONT_OBLIQUE     5
#define OBJ_FONT_BOLDOBLIQUE 6
#define OBJ_FIRST_PAGE       7

// A4 page size in points
#define A4_WIDTH  595.28f
#define A4_HEIGHT 841.89f

PdfDocument *Pdf_Create(void)
{
    PdfDocument *doc = calloc(1, sizeof(PdfDocument));
    if (!doc) return NULL;

    doc->pageWidth = A4_WIDTH;
    doc->pageHeight = A4_HEIGHT;
    doc->pageCapacity = INITIAL_PAGES;
    doc->pages = calloc(doc->pageCapacity, sizeof(PdfPage));
    if (!doc->pages) {
        free(doc);
        return NULL;
    }
    return doc;
}

int Pdf_AddPage(PdfDocument *doc)
{
    if (!doc) return -1;

    if (doc->pageCount >= doc->pageCapacity) {
        int newCap = doc->pageCapacity * 2;
        PdfPage *tmp = realloc(doc->pages, newCap * sizeof(PdfPage));
        if (!tmp) return -1;
        memset(tmp + doc->pageCapacity, 0, (newCap - doc->pageCapacity) * sizeof(PdfPage));
        doc->pages = tmp;
        doc->pageCapacity = newCap;
    }

    PdfPage *page = &doc->pages[doc->pageCount];
    page->spanCapacity = INITIAL_SPANS;
    page->spans = calloc(page->spanCapacity, sizeof(PdfTextSpan));
    if (!page->spans) return -1;
    page->spanCount = 0;

    return doc->pageCount++;
}

void Pdf_AddTextSpan(PdfDocument *doc, int pageIndex,
                     float x, float y, uint8_t style, float fontSize,
                     const char *text)
{
    if (!doc || pageIndex < 0 || pageIndex >= doc->pageCount || !text) return;

    PdfPage *page = &doc->pages[pageIndex];
    if (page->spanCount >= page->spanCapacity) {
        int newCap = page->spanCapacity * 2;
        PdfTextSpan *tmp = realloc(page->spans, newCap * sizeof(PdfTextSpan));
        if (!tmp) return;
        page->spans = tmp;
        page->spanCapacity = newCap;
    }

    PdfTextSpan *span = &page->spans[page->spanCount++];
    span->x = x;
    span->y = y;
    span->style = style;
    span->fontSize = fontSize;
    span->text = strdup(text);
    if (!span->text) {
        page->spanCount--;   /* out of memory: drop this span */
    }
}

// Select font name based on style flags
static const char *pdf_font_name(uint8_t style)
{
    bool bold = (style & PDF_STYLE_BOLD) != 0;
    bool italic = (style & PDF_STYLE_ITALIC) != 0;

    if (bold && italic) return "/F4";
    if (bold) return "/F2";
    if (italic) return "/F3";
    return "/F1";
}

// Build the content stream for one page into a dynamic buffer
static char *build_page_content(PdfPage *page, size_t *outLen)
{
    // Estimate: each span ~100 bytes
    size_t bufSize = 256 + page->spanCount * 128;
    char *buf = malloc(bufSize);
    if (!buf) { *outLen = 0; return NULL; }

    size_t pos = 0;

    pos += snprintf(buf + pos, bufSize - pos, "BT\n");

    for (int i = 0; i < page->spanCount; i++) {
        PdfTextSpan *span = &page->spans[i];

        // Grow buffer if needed
        size_t textLen = span->text ? strlen(span->text) : 0;
        size_t needed = pos + textLen * 2 + 256;
        if (needed > bufSize) {
            bufSize = needed * 2;
            char *tmp = realloc(buf, bufSize);
            if (!tmp) { free(buf); *outLen = 0; return NULL; }
            buf = tmp;
        }

        // Font selection
        pos += snprintf(buf + pos, bufSize - pos, "%s %.1f Tf\n",
                        pdf_font_name(span->style), span->fontSize);

        // Position (Tm sets absolute text matrix, unlike Td which is relative)
        pos += snprintf(buf + pos, bufSize - pos, "1 0 0 1 %.2f %.2f Tm\n",
                        span->x, span->y);

        // Text
        // Escape manually into buf
        buf[pos++] = '(';
        for (const char *p = span->text; p && *p; p++) {
            if (*p == '(' || *p == ')' || *p == '\\') {
                buf[pos++] = '\\';
            }
            buf[pos++] = *p;
        }
        buf[pos++] = ')';
        pos += snprintf(buf + pos, bufSize - pos, " Tj\n");

        // Underline: draw a line under the text
        if (span->style & PDF_STYLE_UNDERLINE) {
            float charWidth = span->fontSize * 0.6f;  // Courier character width
            float lineWidth = charWidth * textLen;
            float lineY = span->y - 2.0f;

            pos += snprintf(buf + pos, bufSize - pos,
                            "ET\n"
                            "0.5 w\n"
                            "%.2f %.2f m %.2f %.2f l S\n"
                            "BT\n",
                            span->x, lineY,
                            span->x + lineWidth, lineY);
        }
    }

    pos += snprintf(buf + pos, bufSize - pos, "ET\n");

    *outLen = pos;
    return buf;
}

bool Pdf_WriteToFile(PdfDocument *doc, const char *filename)
{
    if (!doc || !filename || doc->pageCount == 0) return false;

    FILE *f = fopen(filename, "wb");
    if (!f) return false;

    int numPages = doc->pageCount;
    // Total objects: catalog + pages + 4 fonts + numPages page objs + numPages content objs
    int totalObjects = 6 + numPages * 2;

    // Track byte offsets for xref
    long *offsets = calloc(totalObjects + 1, sizeof(long));
    if (!offsets) { fclose(f); return false; }

    // Header
    fprintf(f, "%%PDF-1.4\n");

    // Object 1: Catalog
    offsets[1] = ftell(f);
    fprintf(f, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

    // Object 2: Pages
    offsets[2] = ftell(f);
    fprintf(f, "2 0 obj\n<< /Type /Pages /Kids [");
    for (int i = 0; i < numPages; i++) {
        fprintf(f, " %d 0 R", OBJ_FIRST_PAGE + i);
    }
    fprintf(f, " ] /Count %d >>\nendobj\n", numPages);

    // Objects 3-6: Fonts
    static const char *fontNames[] = {
        "Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique"
    };
    for (int i = 0; i < 4; i++) {
        int objNum = OBJ_FONT_REGULAR + i;
        offsets[objNum] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /%s >>\nendobj\n",
                objNum, fontNames[i]);
    }

    // Build content streams first so we know their lengths
    char **contentBufs = calloc(numPages, sizeof(char *));
    size_t *contentLens = calloc(numPages, sizeof(size_t));

    for (int i = 0; i < numPages; i++) {
        contentBufs[i] = build_page_content(&doc->pages[i], &contentLens[i]);
    }

    // Page objects (OBJ_FIRST_PAGE .. OBJ_FIRST_PAGE + numPages - 1)
    int contentObjBase = OBJ_FIRST_PAGE + numPages;
    for (int i = 0; i < numPages; i++) {
        int pageObj = OBJ_FIRST_PAGE + i;
        int contObj = contentObjBase + i;
        offsets[pageObj] = ftell(f);
        fprintf(f, "%d 0 obj\n"
                "<< /Type /Page /Parent 2 0 R\n"
                "   /MediaBox [0 0 %.2f %.2f]\n"
                "   /Contents %d 0 R\n"
                "   /Resources << /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R /F4 6 0 R >> >>\n"
                ">>\nendobj\n",
                pageObj, doc->pageWidth, doc->pageHeight, contObj);
    }

    // Content stream objects
    for (int i = 0; i < numPages; i++) {
        int contObj = contentObjBase + i;
        offsets[contObj] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Length %zu >>\nstream\n", contObj, contentLens[i]);
        if (contentBufs[i] && contentLens[i] > 0) {
            fwrite(contentBufs[i], 1, contentLens[i], f);
        }
        fprintf(f, "endstream\nendobj\n");
    }

    // Cross-reference table
    long xrefOffset = ftell(f);
    fprintf(f, "xref\n0 %d\n", totalObjects + 1);
    fprintf(f, "0000000000 65535 f \n");
    for (int i = 1; i <= totalObjects; i++) {
        fprintf(f, "%010ld 00000 n \n", offsets[i]);
    }

    // Trailer
    fprintf(f, "trailer\n<< /Size %d /Root 1 0 R >>\n", totalObjects + 1);
    fprintf(f, "startxref\n%ld\n%%%%EOF\n", xrefOffset);

    // Cleanup
    for (int i = 0; i < numPages; i++) {
        free(contentBufs[i]);
    }
    free(contentBufs);
    free(contentLens);
    free(offsets);
    fclose(f);
    return true;
}

void Pdf_Destroy(PdfDocument *doc)
{
    if (!doc) return;

    for (int i = 0; i < doc->pageCount; i++) {
        PdfPage *page = &doc->pages[i];
        for (int j = 0; j < page->spanCount; j++) {
            free(page->spans[j].text);
        }
        free(page->spans);
    }
    free(doc->pages);
    free(doc);
}
