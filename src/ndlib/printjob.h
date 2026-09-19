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

/**
 * @brief Allocate a print job manager, copy the output directory and, for the
 *        ESC/P printer type, create the ESC/P interpreter.
 * @details The job timeout starts at the default 5 seconds of silence. No job
 *          is open until the first character arrives.
 * @param printerType PJ_PRINTER_TEXT for plain ASCII pass-through, or
 *                    PJ_PRINTER_ESCP for the Epson ESC/P interpreter.
 * @param format      PJ_FORMAT_TXT writes .txt files, PJ_FORMAT_PDF .pdf.
 * @param outputDir   Directory for the output files; NULL means "./prints".
 * @return New manager the caller frees with PrintJob_Destroy(), or NULL if an
 *         allocation failed.
 */
PrintJob *PrintJob_Create(PjPrinterType printerType, PjOutputFormat format, const char *outputDir);

/**
 * @brief Feed one character from the printer device into the current job.
 * @details Flushes the open job first when it has been silent for at least
 *          jobTimeout seconds, opens a new job when none is active, counts the
 *          byte and any LF, then routes the character through the text or
 *          ESC/P pipeline for the chosen output format. In text plus .txt
 *          mode a form feed (0x0C) also ends the job at once.
 * @param pj The manager; NULL is ignored.
 * @param c  The character received from the device.
 */
void PrintJob_PutChar(PrintJob *pj, char c);

/**
 * @brief End the open job if no character has arrived for jobTimeout seconds.
 *        Call this periodically from the main loop.
 * @param pj The manager; NULL is ignored.
 * @return true if a job was written out and closed, false otherwise.
 */
bool PrintJob_CheckTimeout(PrintJob *pj);

/**
 * @brief Write out and close the open job immediately, regardless of the
 *        timeout. Call this on shutdown.
 * @param pj The manager; NULL is ignored.
 */
void PrintJob_Flush(PrintJob *pj);

/**
 * @brief Flush the open job, destroy the ESC/P interpreter if present, and
 *        free the output directory string and the manager.
 * @param pj The manager; NULL is ignored.
 */
void PrintJob_Destroy(PrintJob *pj);

#endif /* PRINTJOB_H */
