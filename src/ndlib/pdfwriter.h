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

// Create a new PDF document (A4 page size)
PdfDocument *Pdf_Create(void);

// Add a new page to the document, returns page index
int Pdf_AddPage(PdfDocument *doc);

// Add a text span to a page
void Pdf_AddTextSpan(PdfDocument *doc, int pageIndex, float x, float y, uint8_t style,
                     float fontSize, const char *text);

// Write the PDF document to a file, returns true on success
bool Pdf_WriteToFile(PdfDocument *doc, const char *filename);

// Free the PDF document and all its contents
void Pdf_Destroy(PdfDocument *doc);

#endif /* PDFWRITER_H */
