/*
 * pdfwriter.h - Minimal PDF writer: document, page and text span API.
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

#ifndef PDFWRITER_H
#define PDFWRITER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Text style flags (can be combined)
#define PDF_STYLE_NORMAL    0x00
#define PDF_STYLE_BOLD      0x01
#define PDF_STYLE_ITALIC    0x02
#define PDF_STYLE_UNDERLINE 0x04

// A single text span on a page
typedef struct
{
    float x;        // X position in points
    float y;        // Y position in points
    uint8_t style;  // Combination of PDF_STYLE_* flags
    float fontSize; // Font size in points
    char *text;     // Null-terminated text content
} PdfTextSpan;

// A page is a list of text spans
typedef struct
{
    PdfTextSpan *spans;
    int spanCount;
    int spanCapacity;
} PdfPage;

// The PDF document
typedef struct PdfDocument
{
    PdfPage *pages;
    int pageCount;
    int pageCapacity;
    float pageWidth;  // Points (A4 = 595.28)
    float pageHeight; // Points (A4 = 841.89)
} PdfDocument;

/**
 * @brief Allocate an empty PDF document with A4 pages (595.28 x 841.89
 *        points) and no pages yet.
 * @return New document the caller frees with Pdf_Destroy(), or NULL if
 *         allocation failed.
 */
PdfDocument *Pdf_Create(void);

/**
 * @brief Append an empty page to the document, growing the page array when
 *        needed.
 * @param doc The document.
 * @return 0-based index of the new page, or -1 if doc is NULL or an
 *         allocation failed.
 */
int Pdf_AddPage(PdfDocument *doc);

/**
 * @brief Append one styled text span to a page. The text is copied into the
 *        document.
 * @param doc       The document.
 * @param pageIndex 0-based page index; out-of-range values are ignored.
 * @param x         X position in points from the left edge.
 * @param y         Y position in points, in PDF coordinates (origin bottom
 *                  left).
 * @param style     Combination of PDF_STYLE_* flags selecting the font.
 * @param fontSize  Font size in points.
 * @param text      NUL-terminated text; NULL is ignored.
 */
void Pdf_AddTextSpan(PdfDocument *doc, int page_index, float x, float y, uint8_t style,
                     float font_size, const char *text);

/**
 * @brief Write the whole document as a PDF file: catalog, page tree, the four
 *        standard Helvetica fonts, one page and one content stream object per
 *        page, then the xref table and trailer.
 * @param doc      The document; must hold at least one page.
 * @param filename Path of the file, opened with mode "wb" and overwritten.
 * @return true on success; false if doc or filename is NULL, the document has
 *         no pages, the file could not be opened, or a buffer allocation
 *         failed.
 */
bool Pdf_WriteToFile(PdfDocument *doc, const char *filename);

/**
 * @brief Free every span's text, the span arrays, the page array and the
 *        document.
 * @param doc The document; NULL is ignored.
 */
void Pdf_Destroy(PdfDocument *doc);

#endif /* PDFWRITER_H */
