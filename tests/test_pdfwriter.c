/*
 * test_pdfwriter.c - Unit tests for the PDF writer data model and rendered content.
 *
 * Unit tests for pdfwriter module.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Tests verify both the in-memory data model AND the rendered PDF content
 * stream - the actual PDF operators that control font selection, text
 * positioning, text output, underline drawing, and special-char escaping.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pdfwriter.h"
#include "test_suites.h"

/* ---- Helpers ---- */

static char *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        *size = 0;
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    if (!buf)
    {
        fclose(f);
        *size = 0;
        return NULL;
    }
    *size = fread(buf, 1, sz, f);
    buf[*size] = '\0';
    fclose(f);
    return buf;
}

/* Count non-overlapping occurrences of needle in haystack */
static int count_occurrences(const char *haystack, const char *needle)
{
    int count = 0;
    size_t nlen = strlen(needle);
    const char *p = haystack;
    while ((p = strstr(p, needle)) != NULL)
    {
        count++;
        p += nlen;
    }
    return count;
}

/* ---- Data model tests ---- */

static int test_pdf_create(void)
{
    PdfDocument *doc = Pdf_Create();
    assert(doc != NULL);
    assert(doc->pageCount == 0);
    assert(doc->pageWidth > 500);  /* A4 = 595.28 */
    assert(doc->pageHeight > 800); /* A4 = 841.89 */
    Pdf_Destroy(doc);
    return 0;
}

static int test_pdf_add_pages(void)
{
    PdfDocument *doc = Pdf_Create();

    int p0 = Pdf_AddPage(doc);
    assert(p0 == 0);
    assert(doc->pageCount == 1);

    int p1 = Pdf_AddPage(doc);
    assert(p1 == 1);
    assert(doc->pageCount == 2);

    int p2 = Pdf_AddPage(doc);
    assert(p2 == 2);
    assert(doc->pageCount == 3);

    Pdf_Destroy(doc);
    return 0;
}

static int test_pdf_add_text_spans(void)
{
    PdfDocument *doc = Pdf_Create();
    int page = Pdf_AddPage(doc);

    Pdf_AddTextSpan(doc, page, 50.0f, 700.0f, PDF_STYLE_NORMAL, 10.0f, "Hello");
    assert(doc->pages[page].spanCount == 1);
    assert(doc->pages[page].spans[0].style == PDF_STYLE_NORMAL);
    assert(strcmp(doc->pages[page].spans[0].text, "Hello") == 0);

    Pdf_AddTextSpan(doc, page, 50.0f, 688.0f, PDF_STYLE_BOLD | PDF_STYLE_ITALIC, 12.0f, "World");
    assert(doc->pages[page].spanCount == 2);
    assert(doc->pages[page].spans[1].style == (PDF_STYLE_BOLD | PDF_STYLE_ITALIC));
    assert(doc->pages[page].spans[1].fontSize == 12.0f);

    Pdf_Destroy(doc);
    return 0;
}

static int test_pdf_empty_document(void)
{
    PdfDocument *doc = Pdf_Create();
    bool ok = Pdf_WriteToFile(doc, "/tmp/should_not_exist.pdf");
    assert(!ok);
    Pdf_Destroy(doc);
    return 0;
}

static int test_pdf_destroy_null(void)
{
    Pdf_Destroy(NULL);
    return 0;
}

/* ---- Content stream / rendering tests ---- */

static int test_pdf_structure(const char *tmpdir)
{
    /*
     * Verify the PDF file structure: header, trailer, catalog, pages
     * object with correct kid count, font declarations.
     */
    PdfDocument *doc = Pdf_Create();
    int p0 = Pdf_AddPage(doc);
    Pdf_AddTextSpan(doc, p0, 50, 700, PDF_STYLE_NORMAL, 10, "Test");
    int p1 = Pdf_AddPage(doc);
    Pdf_AddTextSpan(doc, p1, 50, 700, PDF_STYLE_NORMAL, 10, "Page2");

    char path[512];
    snprintf(path, sizeof(path), "%s/test_structure.pdf", tmpdir);
    assert(Pdf_WriteToFile(doc, path));

    size_t sz;
    char *raw = read_file(path, &sz);
    assert(raw && sz > 0);

    /* PDF header */
    assert(memcmp(raw, "%PDF-1.4", 8) == 0);

    /* PDF trailer */
    assert(strstr(raw, "%%EOF") != NULL);

    /* Catalog references Pages */
    assert(strstr(raw, "/Type /Catalog /Pages 2 0 R") != NULL);

    /* Pages object lists 2 kids, count = 2 */
    assert(strstr(raw, "/Count 2") != NULL);
    assert(strstr(raw, "7 0 R") != NULL); /* first page obj */
    assert(strstr(raw, "8 0 R") != NULL); /* second page obj */

    /* All 4 Courier font variants declared */
    assert(strstr(raw, "/BaseFont /Courier") != NULL);
    assert(strstr(raw, "/BaseFont /Courier-Bold") != NULL);
    assert(strstr(raw, "/BaseFont /Courier-Oblique") != NULL);
    assert(strstr(raw, "/BaseFont /Courier-BoldOblique") != NULL);

    /* xref table present */
    assert(strstr(raw, "xref\n") != NULL);
    assert(strstr(raw, "startxref\n") != NULL);

    free(raw);
    Pdf_Destroy(doc);
    return 0;
}

static int test_pdf_font_selection(const char *tmpdir)
{
    /*
     * Verify the content stream selects the correct font for each style:
     *   Normal     -> /F1 (Courier)
     *   Bold       -> /F2 (Courier-Bold)
     *   Italic     -> /F3 (Courier-Oblique)
     *   Bold+Ital  -> /F4 (Courier-BoldOblique)
     */
    PdfDocument *doc = Pdf_Create();
    int page = Pdf_AddPage(doc);
    Pdf_AddTextSpan(doc, page, 50, 700, PDF_STYLE_NORMAL, 10, "normal");
    Pdf_AddTextSpan(doc, page, 50, 688, PDF_STYLE_BOLD, 10, "bold");
    Pdf_AddTextSpan(doc, page, 50, 676, PDF_STYLE_ITALIC, 10, "italic");
    Pdf_AddTextSpan(doc, page, 50, 664, PDF_STYLE_BOLD | PDF_STYLE_ITALIC, 10, "bolditalic");

    char path[512];
    snprintf(path, sizeof(path), "%s/test_fonts.pdf", tmpdir);
    assert(Pdf_WriteToFile(doc, path));

    size_t sz;
    char *raw = read_file(path, &sz);
    assert(raw && sz > 0);

    /* Each font+text pair must appear in the content stream */
    assert(strstr(raw, "/F1 10.0 Tf") != NULL); /* Normal */
    assert(strstr(raw, "(normal) Tj") != NULL);

    assert(strstr(raw, "/F2 10.0 Tf") != NULL); /* Bold */
    assert(strstr(raw, "(bold) Tj") != NULL);

    assert(strstr(raw, "/F3 10.0 Tf") != NULL); /* Italic */
    assert(strstr(raw, "(italic) Tj") != NULL);

    assert(strstr(raw, "/F4 10.0 Tf") != NULL); /* Bold+Italic */
    assert(strstr(raw, "(bolditalic) Tj") != NULL);

    free(raw);
    Pdf_Destroy(doc);
    return 0;
}

static int test_pdf_absolute_positioning(const char *tmpdir)
{
    /*
     * Regression: Td (relative) was used instead of Tm (absolute).
     * Multiple spans on one page ended up off-screen because coordinates
     * accumulated. Verify each span gets its own absolute Tm operator.
     */
    PdfDocument *doc = Pdf_Create();
    int page = Pdf_AddPage(doc);
    Pdf_AddTextSpan(doc, page, 50.0f, 700.0f, PDF_STYLE_NORMAL, 10, "Line1");
    Pdf_AddTextSpan(doc, page, 50.0f, 680.0f, PDF_STYLE_NORMAL, 10, "Line2");
    Pdf_AddTextSpan(doc, page, 100.0f, 660.0f, PDF_STYLE_NORMAL, 10, "Indented");

    char path[512];
    snprintf(path, sizeof(path), "%s/test_positioning.pdf", tmpdir);
    assert(Pdf_WriteToFile(doc, path));

    size_t sz;
    char *raw = read_file(path, &sz);
    assert(raw && sz > 0);

    /* Each span must have absolute Tm coordinates */
    assert(strstr(raw, "1 0 0 1 50.00 700.00 Tm") != NULL);
    assert(strstr(raw, "1 0 0 1 50.00 680.00 Tm") != NULL);
    assert(strstr(raw, "1 0 0 1 100.00 660.00 Tm") != NULL);

    /* No relative Td operators */
    assert(strstr(raw, " Td\n") == NULL);

    /* 3 Tm operators total (one per span) */
    assert(count_occurrences(raw, " Tm\n") == 3);

    free(raw);
    Pdf_Destroy(doc);
    return 0;
}

static int test_pdf_text_rendering(const char *tmpdir)
{
    /*
     * Verify text appears in the content stream as Tj operators with
     * correct content and the right font size.
     */
    PdfDocument *doc = Pdf_Create();
    int page = Pdf_AddPage(doc);
    Pdf_AddTextSpan(doc, page, 50, 700, PDF_STYLE_NORMAL, 12, "Hello World");
    Pdf_AddTextSpan(doc, page, 50, 680, PDF_STYLE_BOLD, 14, "Big Bold");

    char path[512];
    snprintf(path, sizeof(path), "%s/test_text.pdf", tmpdir);
    assert(Pdf_WriteToFile(doc, path));

    size_t sz;
    char *raw = read_file(path, &sz);
    assert(raw && sz > 0);

    /* Text content rendered via Tj */
    assert(strstr(raw, "(Hello World) Tj") != NULL);
    assert(strstr(raw, "(Big Bold) Tj") != NULL);

    /* Font sizes */
    assert(strstr(raw, "/F1 12.0 Tf") != NULL);
    assert(strstr(raw, "/F2 14.0 Tf") != NULL);

    /* Content stream wrapped in BT/ET */
    assert(strstr(raw, "BT\n") != NULL);
    assert(strstr(raw, "ET\n") != NULL);

    free(raw);
    Pdf_Destroy(doc);
    return 0;
}

static int test_pdf_text_escaping(const char *tmpdir)
{
    /*
     * PDF string special chars: ( ) \ must be escaped in the content
     * stream. Verify they appear as \( \) \\ in the Tj operand.
     */
    PdfDocument *doc = Pdf_Create();
    int page = Pdf_AddPage(doc);
    Pdf_AddTextSpan(doc, page, 50, 700, PDF_STYLE_NORMAL, 10, "a(b)c\\d");

    char path[512];
    snprintf(path, sizeof(path), "%s/test_escape.pdf", tmpdir);
    assert(Pdf_WriteToFile(doc, path));

    size_t sz;
    char *raw = read_file(path, &sz);
    assert(raw && sz > 0);

    /* Escaped string in content stream */
    assert(strstr(raw, "(a\\(b\\)c\\\\d) Tj") != NULL);

    free(raw);
    Pdf_Destroy(doc);
    return 0;
}

static int test_pdf_underline_rendering(const char *tmpdir)
{
    /*
     * Underlined text should produce:
     *   1. Normal text Tj
     *   2. ET (exit text mode)
     *   3. Line width + moveto + lineto + stroke (the underline)
     *   4. BT (re-enter text mode)
     */
    PdfDocument *doc = Pdf_Create();
    int page = Pdf_AddPage(doc);
    Pdf_AddTextSpan(doc, page, 50.0f, 700.0f, PDF_STYLE_UNDERLINE, 10.0f, "Underlined");

    char path[512];
    snprintf(path, sizeof(path), "%s/test_underline.pdf", tmpdir);
    assert(Pdf_WriteToFile(doc, path));

    size_t sz;
    char *raw = read_file(path, &sz);
    assert(raw && sz > 0);

    /* Text is rendered */
    assert(strstr(raw, "(Underlined) Tj") != NULL);

    /* Underline stroke operators: line width, moveto, lineto, stroke */
    assert(strstr(raw, "0.5 w") != NULL);          /* line width */
    assert(strstr(raw, "50.00 698.00 m") != NULL); /* moveto at y-2 */
    assert(strstr(raw, " l S") != NULL);           /* lineto + stroke */

    /* Underline line length: 10 chars * 10pt * 0.6 = 60pt */
    /* End x = 50 + 60 = 110 */
    assert(strstr(raw, "110.00 698.00 l S") != NULL);

    /* ET/BT pair wrapping the stroke (exits text mode for drawing) */
    char *tj_pos = strstr(raw, "(Underlined) Tj");
    assert(tj_pos != NULL);
    char *et_pos = strstr(tj_pos, "ET\n");
    assert(et_pos != NULL);
    char *stroke_pos = strstr(et_pos, " l S");
    assert(stroke_pos != NULL);
    char *bt_pos = strstr(stroke_pos, "BT\n");
    assert(bt_pos != NULL);

    free(raw);
    Pdf_Destroy(doc);
    return 0;
}

static int test_pdf_non_underlined_no_stroke(const char *tmpdir)
{
    /*
     * Non-underlined text must NOT produce stroke operators.
     */
    PdfDocument *doc = Pdf_Create();
    int page = Pdf_AddPage(doc);
    Pdf_AddTextSpan(doc, page, 50, 700, PDF_STYLE_NORMAL, 10, "Plain");
    Pdf_AddTextSpan(doc, page, 50, 688, PDF_STYLE_BOLD, 10, "Bold");

    char path[512];
    snprintf(path, sizeof(path), "%s/test_no_stroke.pdf", tmpdir);
    assert(Pdf_WriteToFile(doc, path));

    size_t sz;
    char *raw = read_file(path, &sz);
    assert(raw && sz > 0);

    /* No stroke operators */
    assert(strstr(raw, "0.5 w") == NULL);
    assert(strstr(raw, " l S") == NULL);

    /* Only one BT/ET pair (no mid-stream ET for underline) */
    assert(count_occurrences(raw, "BT\n") == 1);
    assert(count_occurrences(raw, "ET\n") == 1);

    free(raw);
    Pdf_Destroy(doc);
    return 0;
}

static int test_pdf_multipage_content(const char *tmpdir)
{
    /*
     * Each page gets its own content stream with its own text.
     * Verify both pages' text appears in the file and each page
     * references its own content stream object.
     */
    PdfDocument *doc = Pdf_Create();
    int p0 = Pdf_AddPage(doc);
    Pdf_AddTextSpan(doc, p0, 50, 700, PDF_STYLE_NORMAL, 10, "FirstPage");
    int p1 = Pdf_AddPage(doc);
    Pdf_AddTextSpan(doc, p1, 50, 700, PDF_STYLE_BOLD, 10, "SecondPage");

    char path[512];
    snprintf(path, sizeof(path), "%s/test_multipage.pdf", tmpdir);
    assert(Pdf_WriteToFile(doc, path));

    size_t sz;
    char *raw = read_file(path, &sz);
    assert(raw && sz > 0);

    /* Both pages' text present */
    assert(strstr(raw, "(FirstPage) Tj") != NULL);
    assert(strstr(raw, "(SecondPage) Tj") != NULL);

    /* Page 1 uses /F1 (normal), page 2 uses /F2 (bold) */
    char *first = strstr(raw, "(FirstPage) Tj");
    char *second = strstr(raw, "(SecondPage) Tj");
    assert(first != NULL && second != NULL);

    /* They should be in separate stream objects (second comes later) */
    assert(second > first);

    /* Two separate content stream objects */
    assert(count_occurrences(raw, "endstream") == 2);

    /* Pages object has /Count 2 */
    assert(strstr(raw, "/Count 2") != NULL);

    free(raw);
    Pdf_Destroy(doc);
    return 0;
}

static int test_pdf_font_size_in_stream(const char *tmpdir)
{
    /*
     * Verify different font sizes appear correctly in the Tf operator.
     */
    PdfDocument *doc = Pdf_Create();
    int page = Pdf_AddPage(doc);
    Pdf_AddTextSpan(doc, page, 50, 780, PDF_STYLE_NORMAL, 8, "Small");
    Pdf_AddTextSpan(doc, page, 50, 760, PDF_STYLE_NORMAL, 14, "Large");
    Pdf_AddTextSpan(doc, page, 50, 730, PDF_STYLE_BOLD, 20, "Huge");

    char path[512];
    snprintf(path, sizeof(path), "%s/test_fontsizes.pdf", tmpdir);
    assert(Pdf_WriteToFile(doc, path));

    size_t sz;
    char *raw = read_file(path, &sz);
    assert(raw && sz > 0);

    assert(strstr(raw, "/F1 8.0 Tf") != NULL);
    assert(strstr(raw, "/F1 14.0 Tf") != NULL);
    assert(strstr(raw, "/F2 20.0 Tf") != NULL);

    free(raw);
    Pdf_Destroy(doc);
    return 0;
}

static int test_pdf_page_mediabox(const char *tmpdir)
{
    /*
     * Verify each page object declares A4 MediaBox.
     */
    PdfDocument *doc = Pdf_Create();
    Pdf_AddPage(doc);
    Pdf_AddTextSpan(doc, 0, 50, 700, PDF_STYLE_NORMAL, 10, "A4");

    char path[512];
    snprintf(path, sizeof(path), "%s/test_mediabox.pdf", tmpdir);
    assert(Pdf_WriteToFile(doc, path));

    size_t sz;
    char *raw = read_file(path, &sz);
    assert(raw && sz > 0);

    assert(strstr(raw, "/MediaBox [0 0 595.28 841.89]") != NULL);

    free(raw);
    Pdf_Destroy(doc);
    return 0;
}

/* ---- Suite runner ---- */

typedef int (*PdfTestFn)(void);
typedef int (*PdfTestFnDir)(const char *);

int run_pdfwriter_tests(const char *tmpdir)
{
    int passed = 0;
    int failed = 0;

    // clang-format off
    struct { const char *name; PdfTestFn fn; } basic_tests[] = {
        { "pdf_create",             test_pdf_create },
        { "pdf_add_pages",          test_pdf_add_pages },
        { "pdf_add_text_spans",     test_pdf_add_text_spans },
        { "pdf_empty_document",     test_pdf_empty_document },
        { "pdf_destroy_null",       test_pdf_destroy_null },
    };
    // clang-format on
    // clang-format off
    struct { const char *name; PdfTestFnDir fn; } render_tests[] = {
        { "pdf_structure",              test_pdf_structure },
        { "pdf_font_selection",         test_pdf_font_selection },
        { "pdf_absolute_positioning",   test_pdf_absolute_positioning },
        { "pdf_text_rendering",         test_pdf_text_rendering },
        { "pdf_text_escaping",          test_pdf_text_escaping },
        { "pdf_underline_rendering",    test_pdf_underline_rendering },
        { "pdf_no_stroke_without_underline", test_pdf_non_underlined_no_stroke },
        { "pdf_multipage_content",      test_pdf_multipage_content },
        { "pdf_font_size_in_stream",    test_pdf_font_size_in_stream },
        { "pdf_page_mediabox",          test_pdf_page_mediabox },
    };
    // clang-format on

    int n = sizeof(basic_tests) / sizeof(basic_tests[0]);
    for (int i = 0; i < n; i++)
    {
        printf("  %-50s", basic_tests[i].name);
        if (basic_tests[i].fn() == 0)
        {
            printf("PASS\n");
            passed++;
        }
        else
        {
            printf("FAIL\n");
            failed++;
        }
    }

    n = sizeof(render_tests) / sizeof(render_tests[0]);
    for (int i = 0; i < n; i++)
    {
        printf("  %-50s", render_tests[i].name);
        if (render_tests[i].fn(tmpdir) == 0)
        {
            printf("PASS\n");
            passed++;
        }
        else
        {
            printf("FAIL\n");
            failed++;
        }
    }

    printf("  pdfwriter: %d passed, %d failed\n", passed, failed);
    return failed;
}
