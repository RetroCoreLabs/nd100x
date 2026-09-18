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
 * Print job manager.
 *
 * Owns the complete pipeline from raw device bytes to output files:
 *
 *   text + txt:  bytes -> .txt file  (direct pass-through)
 *   text + pdf:  bytes -> monospaced text -> PDF pages -> .pdf file
 *   escp + txt:  bytes -> ESC/P strip -> .txt file  (plain text, codes removed)
 *   escp + pdf:  bytes -> ESC/P interpreter -> styled spans -> PDF pages -> .pdf file
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#ifdef _WIN32
#  include <direct.h>    /* _mkdir */
#  define ND_MKDIR(p) _mkdir(p)
#else
#  define ND_MKDIR(p) mkdir((p), 0755)
#endif

#include "printjob.h"
#include "ndlib_types.h"
#include "ndlib_protos.h"

#define DEFAULT_JOB_TIMEOUT 5
#define MAX_PATH 512
#define PDF_FONT_SIZE 10.0f
#define PDF_MARGIN_LEFT 50.0f
#define PDF_MARGIN_TOP 50.0f
#define PDF_CHAR_WIDTH (PDF_FONT_SIZE * 0.6f)    // Courier char width
#define PDF_LINE_HEIGHT (PDF_FONT_SIZE * 1.2f)    // Line spacing
#define PDF_PAGE_HEIGHT 841.89f
#define PDF_LINES_PER_PAGE 66

// Forward declarations
static void flush_job(PrintJob *pj);
static void ensure_directory(const char *path);

PrintJob *PrintJob_Create(PjPrinterType printerType, PjOutputFormat format,
                          const char *outputDir)
{
    PrintJob *pj = calloc(1, sizeof(PrintJob));
    if (!pj) return NULL;

    pj->printerType = printerType;
    pj->outputFormat = format;
    pj->outputDir = strdup(outputDir ? outputDir : "./prints");
    if (!pj->outputDir) {
        free(pj);
        return NULL;
    }
    pj->jobTimeout = DEFAULT_JOB_TIMEOUT;

    if (printerType == PJ_PRINTER_ESCP) {
        pj->escpCtx = Escp_Create();
        if (!pj->escpCtx) {
            free(pj->outputDir);
            free(pj);
            return NULL;
        }
    }

    return pj;
}

static void ensure_directory(const char *path)
{
    if (ND_MKDIR(path) != 0 && errno != EEXIST) {
        fprintf(stderr, "PrintJob: failed to create directory: %s\n", path);
    }
}

// Build the output filename for current job
static void build_filename(PrintJob *pj, char *buf, size_t bufSize)
{
    const char *ext = (pj->outputFormat == PJ_FORMAT_PDF) ? "pdf" : "txt";
    snprintf(buf, bufSize, "%s/print-%d.%s", pj->outputDir, pj->jobNumber, ext);
}

// Start a new job
static void start_new_job(PrintJob *pj)
{
    ensure_directory(pj->outputDir);
    pj->jobNumber++;
    pj->jobActive = true;
    pj->jobStartTime = time(NULL);
    pj->jobByteCount = 0;
    pj->jobLineCount = 0;

    if (pj->outputFormat == PJ_FORMAT_TXT) {
        char filename[MAX_PATH];
        build_filename(pj, filename, sizeof(filename));
        pj->txtFile = fopen(filename, "w");
        if (!pj->txtFile) {
            fprintf(stderr, "PrintJob: failed to open %s\n", filename);
            pj->jobActive = false;
            return;
        }
    } else {
        // PDF mode
        pj->pdfDoc = Pdf_Create();
        if (!pj->pdfDoc) {
            pj->jobActive = false;
            return;
        }
        pj->pdfCurrentPage = Pdf_AddPage(pj->pdfDoc);
        pj->pdfColumn = 0;
        pj->pdfLine = 0;

        if (pj->printerType == PJ_PRINTER_ESCP && pj->escpCtx) {
            Escp_Reset(pj->escpCtx);
        }
    }
}

// --- TEXT + TXT pipeline: direct pass-through ---

static void text_txt_putchar(PrintJob *pj, char c)
{
    if (!pj->txtFile) return;
    fputc(c, pj->txtFile);
    fflush(pj->txtFile);
}

// --- TEXT + PDF pipeline: monospaced text accumulation ---

// Line buffer for text+pdf mode
static char pdfLineBuf[1024];
static int pdfLineBufLen = 0;

static void text_pdf_flush_line(PrintJob *pj)
{
    if (pdfLineBufLen == 0 || !pj->pdfDoc) return;

    pdfLineBuf[pdfLineBufLen] = '\0';

    float x = PDF_MARGIN_LEFT;
    float y = PDF_PAGE_HEIGHT - PDF_MARGIN_TOP - (pj->pdfLine * PDF_LINE_HEIGHT);

    Pdf_AddTextSpan(pj->pdfDoc, pj->pdfCurrentPage,
                    x, y, PDF_STYLE_NORMAL, PDF_FONT_SIZE, pdfLineBuf);
    pdfLineBufLen = 0;
}

static void text_pdf_putchar(PrintJob *pj, char c)
{
    if (!pj->pdfDoc) return;

    switch (c) {
        case '\r':
            // CR: just reset column
            text_pdf_flush_line(pj);
            pj->pdfColumn = 0;
            break;

        case '\n':
            text_pdf_flush_line(pj);
            pj->pdfLine++;
            pj->pdfColumn = 0;
            if (pj->pdfLine >= PDF_LINES_PER_PAGE) {
                pj->pdfCurrentPage = Pdf_AddPage(pj->pdfDoc);
                pj->pdfLine = 0;
            }
            break;

        case '\f':
            text_pdf_flush_line(pj);
            pj->pdfCurrentPage = Pdf_AddPage(pj->pdfDoc);
            pj->pdfLine = 0;
            pj->pdfColumn = 0;
            break;

        default:
            if (c >= 0x20 && c <= 0x7E) {
                if (pdfLineBufLen < (int)sizeof(pdfLineBuf) - 1) {
                    pdfLineBuf[pdfLineBufLen++] = c;
                }
                pj->pdfColumn++;
            }
            break;
    }
}

// --- ESCP + TXT pipeline: strip codes, write plain text ---

static void escp_txt_putchar(PrintJob *pj, char c)
{
    if (!pj->txtFile || !pj->escpCtx) return;

    char plain = Escp_StripToPlainChar(pj->escpCtx, (uint8_t)c);
    if (plain) {
        fputc(plain, pj->txtFile);
        fflush(pj->txtFile);
    }
}

// --- ESCP + PDF pipeline: full styled output ---

static void escp_pdf_putchar(PrintJob *pj, char c)
{
    if (!pj->escpCtx) return;
    Escp_PutChar(pj->escpCtx, (uint8_t)c);
}

// Convert ESC/P spans to PDF spans and write
static void escp_pdf_flush(PrintJob *pj)
{
    if (!pj->escpCtx || !pj->pdfDoc) return;

    int spanCount = 0;
    const EscpSpan *spans = Escp_GetSpans(pj->escpCtx, &spanCount);
    if (!spans || spanCount == 0) return;

    // Ensure we have enough pages
    int maxPage = 0;
    for (int i = 0; i < spanCount; i++) {
        if (spans[i].page > maxPage) maxPage = spans[i].page;
    }
    while (pj->pdfDoc->pageCount <= maxPage) {
        Pdf_AddPage(pj->pdfDoc);
    }

    // Convert each ESC/P span to a PDF text span
    for (int i = 0; i < spanCount; i++) {
        const EscpSpan *sp = &spans[i];

        float x = PDF_MARGIN_LEFT + sp->column * sp->charWidth;
        float lineH = sp->lineHeight > 0 ? sp->lineHeight : PDF_LINE_HEIGHT;
        float y = PDF_PAGE_HEIGHT - PDF_MARGIN_TOP - (sp->line * lineH);

        // Map ESC/P attrs to PDF style flags
        uint8_t style = PDF_STYLE_NORMAL;
        if (sp->attrs & ESCP_ATTR_BOLD)      style |= PDF_STYLE_BOLD;
        if (sp->attrs & ESCP_ATTR_ITALIC)    style |= PDF_STYLE_ITALIC;
        if (sp->attrs & ESCP_ATTR_UNDERLINE) style |= PDF_STYLE_UNDERLINE;

        Pdf_AddTextSpan(pj->pdfDoc, sp->page,
                        x, y, style, PDF_FONT_SIZE, sp->text);
    }
}

// --- Flush and close current job ---

static void flush_job(PrintJob *pj)
{
    if (!pj->jobActive) return;

    // Snapshot metadata for last-completed-job queries
    pj->lastCompletedJob = pj->jobNumber;
    pj->lastJobStartTime = pj->jobStartTime;
    pj->lastJobEndTime = time(NULL);
    pj->lastJobBytes = pj->jobByteCount;
    pj->lastJobLines = pj->jobLineCount;

    char filename[MAX_PATH];
    build_filename(pj, filename, sizeof(filename));

    if (pj->outputFormat == PJ_FORMAT_TXT) {
        if (pj->txtFile) {
            fclose(pj->txtFile);
            pj->txtFile = NULL;
            Log(LOG_INFO, "Printer job %d saved to %s\n", pj->jobNumber, filename);
        }
    } else {
        // PDF mode: finalize and write
        if (pj->printerType == PJ_PRINTER_ESCP) {
            escp_pdf_flush(pj);
        } else {
            text_pdf_flush_line(pj);
        }

        if (pj->pdfDoc) {
            if (Pdf_WriteToFile(pj->pdfDoc, filename)) {
                Log(LOG_INFO, "Printer job %d saved to %s\n", pj->jobNumber, filename);
            } else {
                Log(LOG_ERROR, "PrintJob: failed to write %s\n", filename);
            }
            Pdf_Destroy(pj->pdfDoc);
            pj->pdfDoc = NULL;
        }
    }

    pj->jobActive = false;
}

// --- Public API ---

void PrintJob_PutChar(PrintJob *pj, char c)
{
    if (!pj) return;

    time_t now = time(NULL);

    // Check for job timeout (start new job if previous timed out)
    if (pj->jobActive && pj->lastOutputTime > 0 &&
        (now - pj->lastOutputTime) >= pj->jobTimeout) {
        flush_job(pj);
    }

    // Start new job if needed
    if (!pj->jobActive) {
        start_new_job(pj);
        if (!pj->jobActive) return;  // Failed to start
    }

    pj->lastOutputTime = now;
    pj->jobByteCount++;
    if (c == '\n') pj->jobLineCount++;

    // Route to appropriate pipeline
    if (pj->printerType == PJ_PRINTER_TEXT) {
        if (pj->outputFormat == PJ_FORMAT_TXT) {
            text_txt_putchar(pj, c);
        } else {
            text_pdf_putchar(pj, c);
        }
    } else {
        if (pj->outputFormat == PJ_FORMAT_TXT) {
            escp_txt_putchar(pj, c);
        } else {
            escp_pdf_putchar(pj, c);
        }
    }

    // Form feed triggers immediate job flush in text+txt mode (legacy behavior)
    if (c == '\f' && pj->printerType == PJ_PRINTER_TEXT &&
        pj->outputFormat == PJ_FORMAT_TXT) {
        flush_job(pj);
    }
}

bool PrintJob_CheckTimeout(PrintJob *pj)
{
    if (!pj || !pj->jobActive) return false;

    time_t now = time(NULL);
    if (pj->lastOutputTime > 0 &&
        (now - pj->lastOutputTime) >= pj->jobTimeout) {
        flush_job(pj);
        return true;
    }
    return false;
}

void PrintJob_Flush(PrintJob *pj)
{
    if (!pj) return;
    flush_job(pj);
}

void PrintJob_Destroy(PrintJob *pj)
{
    if (!pj) return;

    flush_job(pj);

    if (pj->escpCtx) {
        Escp_Destroy(pj->escpCtx);
    }
    free(pj->outputDir);
    free(pj);
}
