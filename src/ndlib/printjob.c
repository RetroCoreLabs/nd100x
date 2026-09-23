/*
 * printjob.c - Print job manager: device bytes to .txt or .pdf output files.
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
 * Print job manager.
 *
 * Owns the complete pipeline from raw device bytes to output files:
 *
 *   text + txt:  bytes -> .txt file  (direct pass-through)
 *   text + pdf:  bytes -> monospaced text -> PDF pages -> .pdf file
 *   escp + txt:  bytes -> ESC/P strip -> .txt file  (plain text, codes removed)
 *   escp + pdf:  bytes -> ESC/P interpreter -> styled spans -> PDF pages -> .pdf file
 */

#include "printjob.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h> /* _mkdir */
#define ND_MKDIR(p) _mkdir(p)
#else
#define ND_MKDIR(p) mkdir((p), 0755)
#endif

#include "log.h"
#include "nd_format.h"
#include "ndlib_types.h"
#include "ndlib_protos.h"

#define DEFAULT_JOB_TIMEOUT 5
#define MAX_PATH            512
#define PDF_FONT_SIZE       10.0f
#define PDF_MARGIN_LEFT     50.0f
#define PDF_MARGIN_TOP      50.0f
#define PDF_CHAR_WIDTH      (PDF_FONT_SIZE * 0.6f) // Courier char width
#define PDF_LINE_HEIGHT     (PDF_FONT_SIZE * 1.2f) // Line spacing
#define PDF_PAGE_HEIGHT     841.89f
#define PDF_LINES_PER_PAGE  66

// Forward declarations
static void flush_job(PrintJob *pj);
static void ensure_directory(const char *path);

PrintJob *pj_create(PjPrinterType printer_type, PjOutputFormat format, const char *output_dir)
{
    PrintJob *pj = calloc(1, sizeof(PrintJob));
    if (!pj)
    {
        return NULL;
    }

    pj->printerType = printer_type;
    pj->outputFormat = format;
    pj->outputDir = strdup(output_dir ? output_dir : "./prints");
    if (!pj->outputDir)
    {
        free(pj);
        return NULL;
    }
    pj->jobTimeout = DEFAULT_JOB_TIMEOUT;

    if (printer_type == PJ_PRINTER_ESCP)
    {
        pj->escpCtx = escp_create();
        if (!pj->escpCtx)
        {
            free(pj->outputDir);
            free(pj);
            return NULL;
        }
    }

    return pj;
}

static void ensure_directory(const char *path)
{
    if (ND_MKDIR(path) != 0 && errno != EEXIST)
    {
        LOG(LOG_CAT_PRINTER, LOG_ERROR, "failed to create directory: %s", path);
    }
}

/**
 * @brief Build the output filename for the current job.
 * @details A truncated name would open, and later overwrite, a file other than
 *          the one meant, so the caller must not use buf when this fails.
 * @return true if the whole path fitted in buf.
 */
static bool build_filename(PrintJob *pj, char *buf, size_t buf_size)
{
    const char *ext = (pj->outputFormat == PJ_FORMAT_PDF) ? "pdf" : "txt";
    return nd_format_checked(buf, buf_size, "%s/print-%d.%s", pj->outputDir, pj->jobNumber, ext);
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

    if (pj->outputFormat == PJ_FORMAT_TXT)
    {
        char filename[MAX_PATH];
        if (!build_filename(pj, filename, sizeof(filename)))
        {
            LOG(LOG_CAT_PRINTER, LOG_ERROR,
                "printer job %d: output path does not fit; the job is dropped", pj->jobNumber);
            pj->jobActive = false;
            return;
        }
        pj->txtFile = fopen(filename, "w");
        if (!pj->txtFile)
        {
            LOG(LOG_CAT_PRINTER, LOG_ERROR, "failed to open %s", filename);
            pj->jobActive = false;
            return;
        }
    }
    else
    {
        // PDF mode
        pj->pdfDoc = pdf_create();
        if (!pj->pdfDoc)
        {
            pj->jobActive = false;
            return;
        }
        pj->pdfCurrentPage = pdf_add_page(pj->pdfDoc);
        pj->pdfColumn = 0;
        pj->pdfLine = 0;

        if (pj->printerType == PJ_PRINTER_ESCP && pj->escpCtx)
        {
            escp_reset(pj->escpCtx);
        }
    }
}

// --- TEXT + TXT pipeline: direct pass-through ---

static void text_txt_putchar(PrintJob *pj, char c)
{
    if (!pj->txtFile)
    {
        return;
    }
    fputc(c, pj->txtFile);
    fflush(pj->txtFile);
}

// --- TEXT + PDF pipeline: monospaced text accumulation ---

// Line buffer for text+pdf mode
static char pdf_line_buf[1024];
static int pdf_line_buf_len = 0;

static void text_pdf_flush_line(PrintJob *pj)
{
    if (pdf_line_buf_len == 0 || !pj->pdfDoc)
    {
        return;
    }

    pdf_line_buf[pdf_line_buf_len] = '\0';

    float x = PDF_MARGIN_LEFT;
    float y = PDF_PAGE_HEIGHT - PDF_MARGIN_TOP - (pj->pdfLine * PDF_LINE_HEIGHT);

    pdf_add_text_span(pj->pdfDoc, pj->pdfCurrentPage, x, y, PDF_STYLE_NORMAL, PDF_FONT_SIZE,
                      pdf_line_buf);
    pdf_line_buf_len = 0;
}

static void text_pdf_putchar(PrintJob *pj, char c)
{
    if (!pj->pdfDoc)
    {
        return;
    }

    switch (c)
    {
    case '\r':
        // CR: just reset column
        text_pdf_flush_line(pj);
        pj->pdfColumn = 0;
        break;

    case '\n':
        text_pdf_flush_line(pj);
        pj->pdfLine++;
        pj->pdfColumn = 0;
        if (pj->pdfLine >= PDF_LINES_PER_PAGE)
        {
            pj->pdfCurrentPage = pdf_add_page(pj->pdfDoc);
            pj->pdfLine = 0;
        }
        break;

    case '\f':
        text_pdf_flush_line(pj);
        pj->pdfCurrentPage = pdf_add_page(pj->pdfDoc);
        pj->pdfLine = 0;
        pj->pdfColumn = 0;
        break;

    default:
        if (c >= 0x20 && c <= 0x7E)
        {
            if (pdf_line_buf_len < (int)sizeof(pdf_line_buf) - 1)
            {
                pdf_line_buf[pdf_line_buf_len++] = c;
            }
            pj->pdfColumn++;
        }
        break;
    }
}

// --- ESCP + TXT pipeline: strip codes, write plain text ---

static void escp_txt_putchar(PrintJob *pj, char c)
{
    if (!pj->txtFile || !pj->escpCtx)
    {
        return;
    }

    char plain = escp_strip_to_plain_char(pj->escpCtx, (uint8_t)c);
    if (plain)
    {
        fputc(plain, pj->txtFile);
        fflush(pj->txtFile);
    }
}

// --- ESCP + PDF pipeline: full styled output ---

static void escp_pdf_putchar(PrintJob *pj, char c)
{
    if (!pj->escpCtx)
    {
        return;
    }
    escp_put_char(pj->escpCtx, (uint8_t)c);
}

// Convert ESC/P spans to PDF spans and write
static void escp_pdf_flush(PrintJob *pj)
{
    if (!pj->escpCtx || !pj->pdfDoc)
    {
        return;
    }

    int span_count = 0;
    const EscpSpan *spans = escp_get_spans(pj->escpCtx, &span_count);
    if (!spans || span_count == 0)
    {
        return;
    }

    // Ensure we have enough pages
    int max_page = 0;
    for (int i = 0; i < span_count; i++)
    {
        if (spans[i].page > max_page)
        {
            max_page = spans[i].page;
        }
    }
    while (pj->pdfDoc->pageCount <= max_page)
    {
        pdf_add_page(pj->pdfDoc);
    }

    // Convert each ESC/P span to a PDF text span
    for (int i = 0; i < span_count; i++)
    {
        const EscpSpan *sp = &spans[i];

        float x = PDF_MARGIN_LEFT + sp->column * sp->charWidth;
        float line_h = sp->lineHeight > 0 ? sp->lineHeight : PDF_LINE_HEIGHT;
        float y = PDF_PAGE_HEIGHT - PDF_MARGIN_TOP - (sp->line * line_h);

        // Map ESC/P attrs to PDF style flags
        uint8_t style = PDF_STYLE_NORMAL;
        if (sp->attrs & ESCP_ATTR_BOLD)
        {
            style |= PDF_STYLE_BOLD;
        }
        if (sp->attrs & ESCP_ATTR_ITALIC)
        {
            style |= PDF_STYLE_ITALIC;
        }
        if (sp->attrs & ESCP_ATTR_UNDERLINE)
        {
            style |= PDF_STYLE_UNDERLINE;
        }

        pdf_add_text_span(pj->pdfDoc, sp->page, x, y, style, PDF_FONT_SIZE, sp->text);
    }
}

// --- Flush and close current job ---

static void flush_job(PrintJob *pj)
{
    if (!pj->jobActive)
    {
        return;
    }

    // Snapshot metadata for last-completed-job queries
    pj->lastCompletedJob = pj->jobNumber;
    pj->lastJobStartTime = pj->jobStartTime;
    pj->lastJobEndTime = time(NULL);
    pj->lastJobBytes = pj->jobByteCount;
    pj->lastJobLines = pj->jobLineCount;

    char filename[MAX_PATH];
    bool have_name = build_filename(pj, filename, sizeof(filename));
    if (!have_name)
    {
        /* Cannot happen once a job has started, since start_new_job builds
         * the same name and drops the job when it does not fit. Reported
         * rather than assumed, and nothing is written under a cut-off name. */
        LOG(LOG_CAT_PRINTER, LOG_ERROR, "printer job %d: output path does not fit", pj->jobNumber);
    }

    if (pj->outputFormat == PJ_FORMAT_TXT)
    {
        if (pj->txtFile)
        {
            /* The error flag is sticky, so this catches any failed write
             * during the job as well as the final flush in fclose. */
            bool lost = (ferror(pj->txtFile) != 0);
            if (fclose(pj->txtFile) != 0)
            {
                lost = true;
            }
            pj->txtFile = NULL;
            if (lost)
            {
                LOG(LOG_CAT_PRINTER, LOG_ERROR, "printer job %d: %s is incomplete", pj->jobNumber,
                    filename);
            }
            else
            {
                LOG(LOG_CAT_PRINTER, LOG_INFO, "Printer job %d saved to %s", pj->jobNumber,
                    filename);
            }
        }
    }
    else
    {
        // PDF mode: finalize and write
        if (pj->printerType == PJ_PRINTER_ESCP)
        {
            escp_pdf_flush(pj);
        }
        else
        {
            text_pdf_flush_line(pj);
        }

        if (pj->pdfDoc)
        {
            if (have_name && pdf_write_to_file(pj->pdfDoc, filename))
            {
                LOG(LOG_CAT_PRINTER, LOG_INFO, "Printer job %d saved to %s\n", pj->jobNumber,
                    filename);
            }
            else
            {
                LOG(LOG_CAT_PRINTER, LOG_ERROR, "PrintJob: failed to write %s\n", filename);
            }
            pdf_destroy(pj->pdfDoc);
            pj->pdfDoc = NULL;
        }
    }

    pj->jobActive = false;
}

// --- Public API ---

void pj_put_char(PrintJob *pj, char c)
{
    if (!pj)
    {
        return;
    }

    time_t now = time(NULL);

    // Check for job timeout (start new job if previous timed out)
    if (pj->jobActive && pj->lastOutputTime > 0 && (now - pj->lastOutputTime) >= pj->jobTimeout)
    {
        flush_job(pj);
    }

    // Start new job if needed
    if (!pj->jobActive)
    {
        start_new_job(pj);
        if (!pj->jobActive)
        {
            return; // Failed to start
        }
    }

    pj->lastOutputTime = now;
    pj->jobByteCount++;
    if (c == '\n')
    {
        pj->jobLineCount++;
    }

    // Route to appropriate pipeline
    if (pj->printerType == PJ_PRINTER_TEXT)
    {
        if (pj->outputFormat == PJ_FORMAT_TXT)
        {
            text_txt_putchar(pj, c);
        }
        else
        {
            text_pdf_putchar(pj, c);
        }
    }
    else
    {
        if (pj->outputFormat == PJ_FORMAT_TXT)
        {
            escp_txt_putchar(pj, c);
        }
        else
        {
            escp_pdf_putchar(pj, c);
        }
    }

    // Form feed triggers immediate job flush in text+txt mode (legacy behavior)
    if (c == '\f' && pj->printerType == PJ_PRINTER_TEXT && pj->outputFormat == PJ_FORMAT_TXT)
    {
        flush_job(pj);
    }
}

bool pj_check_timeout(PrintJob *pj)
{
    if (!pj || !pj->jobActive)
    {
        return false;
    }

    time_t now = time(NULL);
    if (pj->lastOutputTime > 0 && (now - pj->lastOutputTime) >= pj->jobTimeout)
    {
        flush_job(pj);
        return true;
    }
    return false;
}

void pj_flush(PrintJob *pj)
{
    if (!pj)
    {
        return;
    }
    flush_job(pj);
}

void pj_destroy(PrintJob *pj)
{
    if (!pj)
    {
        return;
    }

    flush_job(pj);

    if (pj->escpCtx)
    {
        escp_destroy(pj->escpCtx);
    }
    free(pj->outputDir);
    free(pj);
}
