/*
 * test_printjob.c - Unit tests for the print job manager.
 *
 * Unit tests for the PrintJob manager module.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>

#include "printjob.h"
#include "test_suites.h"

/* Helpers */

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/* Read entire file into malloc'd buffer. Caller frees. Sets *size. */
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

static void feed_string(PrintJob *pj, const char *s)
{
    for (const char *p = s; *p; p++)
    {
        PrintJob_PutChar(pj, *p);
    }
}

/* Tests */

static int test_pj_text_txt(const char *tmpdir)
{
    char outdir[512];
    snprintf(outdir, sizeof(outdir), "%s/text_txt", tmpdir);

    PrintJob *pj = PrintJob_Create(PJ_PRINTER_TEXT, PJ_FORMAT_TXT, outdir);
    assert(pj != NULL);

    feed_string(pj, "Hello\n");
    PrintJob_Flush(pj);

    char path[600];
    snprintf(path, sizeof(path), "%s/print-1.txt", outdir);
    assert(file_exists(path));

    size_t sz;
    char *content = read_file(path, &sz);
    assert(content != NULL);
    assert(strcmp(content, "Hello\n") == 0);
    free(content);

    PrintJob_Destroy(pj);
    return 0;
}

static int test_pj_text_pdf(const char *tmpdir)
{
    char outdir[512];
    snprintf(outdir, sizeof(outdir), "%s/text_pdf", tmpdir);

    PrintJob *pj = PrintJob_Create(PJ_PRINTER_TEXT, PJ_FORMAT_PDF, outdir);
    assert(pj != NULL);

    feed_string(pj, "Hello\nWorld\n");
    PrintJob_Flush(pj);

    char path[600];
    snprintf(path, sizeof(path), "%s/print-1.pdf", outdir);
    assert(file_exists(path));

    /* Verify it's a valid PDF */
    size_t sz;
    char *content = read_file(path, &sz);
    assert(content != NULL);
    assert(sz > 20);
    assert(memcmp(content, "%PDF-1.4", 8) == 0);
    assert(strstr(content + sz - 10, "%%EOF") != NULL);
    free(content);

    PrintJob_Destroy(pj);
    return 0;
}

static int test_pj_escp_txt(const char *tmpdir)
{
    char outdir[512];
    snprintf(outdir, sizeof(outdir), "%s/escp_txt", tmpdir);

    PrintJob *pj = PrintJob_Create(PJ_PRINTER_ESCP, PJ_FORMAT_TXT, outdir);
    assert(pj != NULL);

    /* Feed: ESC E (bold on) + "Bold" + ESC F (bold off) + " text\n" */
    PrintJob_PutChar(pj, 0x1B);
    PrintJob_PutChar(pj, 'E');
    feed_string(pj, "Bold");
    PrintJob_PutChar(pj, 0x1B);
    PrintJob_PutChar(pj, 'F');
    feed_string(pj, " text\n");
    PrintJob_Flush(pj);

    char path[600];
    snprintf(path, sizeof(path), "%s/print-1.txt", outdir);
    assert(file_exists(path));

    /* ESC/P codes should be stripped, leaving plain text */
    size_t sz;
    char *content = read_file(path, &sz);
    assert(content != NULL);
    assert(strcmp(content, "Bold text\n") == 0);
    free(content);

    PrintJob_Destroy(pj);
    return 0;
}

static int test_pj_escp_pdf(const char *tmpdir)
{
    char outdir[512];
    snprintf(outdir, sizeof(outdir), "%s/escp_pdf", tmpdir);

    PrintJob *pj = PrintJob_Create(PJ_PRINTER_ESCP, PJ_FORMAT_PDF, outdir);
    assert(pj != NULL);

    /* Feed: ESC E (bold on) + "Bold" + LF */
    PrintJob_PutChar(pj, 0x1B);
    PrintJob_PutChar(pj, 'E');
    feed_string(pj, "Bold");
    PrintJob_PutChar(pj, 0x0A); /* LF */
    PrintJob_Flush(pj);

    char path[600];
    snprintf(path, sizeof(path), "%s/print-1.pdf", outdir);
    assert(file_exists(path));

    /* Verify it's a PDF */
    size_t sz;
    char *content = read_file(path, &sz);
    assert(content != NULL);
    assert(sz > 20);
    assert(memcmp(content, "%PDF-1.4", 8) == 0);
    free(content);

    PrintJob_Destroy(pj);
    return 0;
}

static int test_pj_job_numbering(const char *tmpdir)
{
    char outdir[512];
    snprintf(outdir, sizeof(outdir), "%s/numbering", tmpdir);

    PrintJob *pj = PrintJob_Create(PJ_PRINTER_TEXT, PJ_FORMAT_TXT, outdir);
    assert(pj != NULL);

    /* Job 1 */
    feed_string(pj, "Job1\n");
    PrintJob_Flush(pj);

    /* Job 2 */
    feed_string(pj, "Job2\n");
    PrintJob_Flush(pj);

    /* Job 3 */
    feed_string(pj, "Job3\n");
    PrintJob_Flush(pj);

    char path[600];
    snprintf(path, sizeof(path), "%s/print-1.txt", outdir);
    assert(file_exists(path));
    snprintf(path, sizeof(path), "%s/print-2.txt", outdir);
    assert(file_exists(path));
    snprintf(path, sizeof(path), "%s/print-3.txt", outdir);
    assert(file_exists(path));

    /* Verify content of each */
    size_t sz;
    char *c1 = read_file(path, &sz);
    /* path is print-3.txt here, check it */
    assert(c1 != NULL);
    assert(strcmp(c1, "Job3\n") == 0);
    free(c1);

    snprintf(path, sizeof(path), "%s/print-1.txt", outdir);
    c1 = read_file(path, &sz);
    assert(c1 != NULL);
    assert(strcmp(c1, "Job1\n") == 0);
    free(c1);

    PrintJob_Destroy(pj);
    return 0;
}

static int test_pj_formfeed_split(const char *tmpdir)
{
    char outdir[512];
    snprintf(outdir, sizeof(outdir), "%s/ff_split", tmpdir);

    PrintJob *pj = PrintJob_Create(PJ_PRINTER_TEXT, PJ_FORMAT_TXT, outdir);
    assert(pj != NULL);

    /* Form feed in text+txt mode triggers job flush */
    feed_string(pj, "Page1");
    PrintJob_PutChar(pj, '\f');
    /* That should have flushed job 1 */

    feed_string(pj, "Page2\n");
    PrintJob_Flush(pj);

    char path[600];
    snprintf(path, sizeof(path), "%s/print-1.txt", outdir);
    assert(file_exists(path));
    snprintf(path, sizeof(path), "%s/print-2.txt", outdir);
    assert(file_exists(path));

    /* Job 1 should contain "Page1" + form feed */
    size_t sz;
    snprintf(path, sizeof(path), "%s/print-1.txt", outdir);
    char *c1 = read_file(path, &sz);
    assert(c1 != NULL);
    assert(sz == 6); /* "Page1" + '\f' */
    assert(memcmp(c1, "Page1\f", 6) == 0);
    free(c1);

    /* Job 2 should contain "Page2\n" */
    snprintf(path, sizeof(path), "%s/print-2.txt", outdir);
    char *c2 = read_file(path, &sz);
    assert(c2 != NULL);
    assert(strcmp(c2, "Page2\n") == 0);
    free(c2);

    PrintJob_Destroy(pj);
    return 0;
}

static int test_pj_check_timeout(const char *tmpdir)
{
    char outdir[512];
    snprintf(outdir, sizeof(outdir), "%s/timeout", tmpdir);

    PrintJob *pj = PrintJob_Create(PJ_PRINTER_TEXT, PJ_FORMAT_TXT, outdir);
    assert(pj != NULL);

    feed_string(pj, "Data");

    /* Immediately after writing, timeout should NOT have elapsed */
    bool timed_out = PrintJob_CheckTimeout(pj);
    assert(!timed_out);

    PrintJob_Destroy(pj);
    return 0;
}

static int test_pj_destroy_flushes(const char *tmpdir)
{
    char outdir[512];
    snprintf(outdir, sizeof(outdir), "%s/destroy_flush", tmpdir);

    PrintJob *pj = PrintJob_Create(PJ_PRINTER_TEXT, PJ_FORMAT_TXT, outdir);
    assert(pj != NULL);

    feed_string(pj, "Pending\n");
    /* Don't call Flush - Destroy should do it */
    PrintJob_Destroy(pj);

    char path[600];
    snprintf(path, sizeof(path), "%s/print-1.txt", outdir);
    assert(file_exists(path));

    size_t sz;
    char *content = read_file(path, &sz);
    assert(content != NULL);
    assert(strcmp(content, "Pending\n") == 0);
    free(content);

    return 0;
}

/* Suite runner */

typedef int (*pj_test_fn)(const char *);

int run_printjob_tests(const char *tmpdir)
{
    int passed = 0;
    int failed = 0;
    // clang-format off
    struct { const char *name; pj_test_fn fn; } tests[] = {
        { "pj_text_txt",          test_pj_text_txt },
        { "pj_text_pdf",          test_pj_text_pdf },
        { "pj_escp_txt",          test_pj_escp_txt },
        { "pj_escp_pdf",          test_pj_escp_pdf },
        { "pj_job_numbering",     test_pj_job_numbering },
        { "pj_formfeed_split",    test_pj_formfeed_split },
        { "pj_check_timeout",     test_pj_check_timeout },
        { "pj_destroy_flushes",   test_pj_destroy_flushes },
    };
    // clang-format on

    int n = sizeof(tests) / sizeof(tests[0]);
    for (int i = 0; i < n; i++)
    {
        printf("  %-50s", tests[i].name);
        if (tests[i].fn(tmpdir) == 0)
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

    printf("  printjob: %d passed, %d failed\n", passed, failed);
    return failed;
}
