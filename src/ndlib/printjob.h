/*
 * printjob.h - Print job manager: printer type, output format and API.
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

#ifndef PRINTJOB_H
#define PRINTJOB_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include "escp.h"
#include "pdfwriter.h"

// Printer emulation type
typedef enum PjPrinterType
{
    PJ_PRINTER_TEXT, // Simple line printer (plain ASCII pass-through)
    PJ_PRINTER_ESCP  // Epson ESC/P interpreter
} PjPrinterType;

// Output format
typedef enum PjOutputFormat
{
    PJ_FORMAT_TXT, // Plain text (.txt)
    PJ_FORMAT_PDF  // PDF (.pdf)
} PjOutputFormat;

// Print job manager context
typedef struct PrintJob
{
    // Configuration (set once at creation)
    PjPrinterType printerType;
    PjOutputFormat outputFormat;
    char *outputDir; // Output directory (owned, strdup'd)
    int jobTimeout;  // Seconds of silence = end of job (default 5)

    // Job state
    int jobNumber;         // Monotonic job counter
    bool jobActive;        // Currently accumulating output
    time_t lastOutputTime; // Timestamp of last character received

    // Active job metadata
    time_t jobStartTime; // When current job started
    int jobByteCount;    // Bytes received in active job
    int jobLineCount;    // Lines (LF) in active job

    // Last completed job metadata
    int lastCompletedJob;    // Job# of most recently flushed job
    time_t lastJobStartTime; // Start time of last completed job
    time_t lastJobEndTime;   // End time of last completed job
    int lastJobBytes;        // Byte count of last completed job
    int lastJobLines;        // Line count of last completed job

    // Text mode state (PJ_FORMAT_TXT)
    FILE *txtFile; // Current open .txt file (NULL if no active job)

    // PDF mode state (PJ_FORMAT_PDF)
    PdfDocument *pdfDoc; // Current PDF document being built
    int pdfCurrentPage;  // Current page index in pdfDoc
    int pdfColumn;       // Current column on page
    int pdfLine;         // Current line on page

    // ESC/P interpreter (used when printerType == PJ_PRINTER_ESCP)
    EscpContext *escpCtx;
} PrintJob;

// Create a print job manager
PrintJob *PrintJob_Create(PjPrinterType printerType, PjOutputFormat format, const char *outputDir);

// Feed a single character from the device into the print job
void PrintJob_PutChar(PrintJob *pj, char c);

// Check for job timeout (call periodically from main loop)
// Returns true if a job was flushed
bool PrintJob_CheckTimeout(PrintJob *pj);

// Flush and close the current job (call on shutdown)
void PrintJob_Flush(PrintJob *pj);

// Destroy the print job manager and free all resources
void PrintJob_Destroy(PrintJob *pj);

#endif /* PRINTJOB_H */
