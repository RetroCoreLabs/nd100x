/*
 * test_escp.c - Unit tests for the ESC/P interpreter.
 *
 * Unit tests for the ESC/P interpreter module.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "escp.h"
#include "test_suites.h"

/* Helpers */

#define POINTS_PER_INCH 72.0f

static void feed_string(EscpContext *ctx, const char *s)
{
    for (const char *p = s; *p; p++)
    {
        escp_put_char(ctx, (uint8_t)*p);
    }
}

/* Tests */

static int test_escp_plain_text(void)
{
    EscpContext *ctx = escp_create();
    assert(ctx != NULL);

    feed_string(ctx, "Hello");

    int count = 0;
    const EscpSpan *spans = escp_get_spans(ctx, &count);
    assert(count == 1);
    assert(strcmp(spans[0].text, "Hello") == 0);
    assert(spans[0].column == 0);
    assert(spans[0].line == 0);
    assert(spans[0].page == 0);
    assert(spans[0].attrs == 0);

    escp_destroy(ctx);
    return 0;
}

static int test_escp_cr_lf_ff(void)
{
    EscpContext *ctx = escp_create();

    /* Feed text, CR, more text - CR resets column */
    feed_string(ctx, "ABC");
    escp_put_char(ctx, 0x0D); /* CR */
    assert(ctx->column == 0);

    /* LF advances line */
    escp_put_char(ctx, 0x0A); /* LF */
    assert(ctx->line == 1);
    assert(ctx->column == 0);

    /* FF advances page */
    feed_string(ctx, "X");
    escp_put_char(ctx, 0x0C); /* FF */
    assert(ctx->page == 1);
    assert(ctx->line == 0);
    assert(ctx->column == 0);

    escp_destroy(ctx);
    return 0;
}

static int test_escp_bold(void)
{
    EscpContext *ctx = escp_create();

    /* ESC E = bold on */
    escp_put_char(ctx, 0x1B);
    escp_put_char(ctx, 'E');
    feed_string(ctx, "Bold");
    /* ESC F = bold off */
    escp_put_char(ctx, 0x1B);
    escp_put_char(ctx, 'F');
    feed_string(ctx, "Normal");

    int count = 0;
    const EscpSpan *spans = escp_get_spans(ctx, &count);
    assert(count >= 2);
    assert(spans[0].attrs & ESCP_ATTR_BOLD);
    assert(strcmp(spans[0].text, "Bold") == 0);
    assert(!(spans[1].attrs & ESCP_ATTR_BOLD));
    assert(strcmp(spans[1].text, "Normal") == 0);

    escp_destroy(ctx);
    return 0;
}

static int test_escp_italic(void)
{
    EscpContext *ctx = escp_create();

    /* ESC 4 = italic on */
    escp_put_char(ctx, 0x1B);
    escp_put_char(ctx, '4');
    feed_string(ctx, "Italic");
    /* ESC 5 = italic off */
    escp_put_char(ctx, 0x1B);
    escp_put_char(ctx, '5');
    feed_string(ctx, "Normal");

    int count = 0;
    const EscpSpan *spans = escp_get_spans(ctx, &count);
    assert(count >= 2);
    assert(spans[0].attrs & ESCP_ATTR_ITALIC);
    assert(!(spans[1].attrs & ESCP_ATTR_ITALIC));

    escp_destroy(ctx);
    return 0;
}

static int test_escp_underline(void)
{
    EscpContext *ctx = escp_create();

    /* ESC - 1 = underline on */
    escp_put_char(ctx, 0x1B);
    escp_put_char(ctx, '-');
    escp_put_char(ctx, 1);
    feed_string(ctx, "Under");
    /* ESC - 0 = underline off */
    escp_put_char(ctx, 0x1B);
    escp_put_char(ctx, '-');
    escp_put_char(ctx, 0);
    feed_string(ctx, "Normal");

    int count = 0;
    const EscpSpan *spans = escp_get_spans(ctx, &count);
    assert(count >= 2);
    assert(spans[0].attrs & ESCP_ATTR_UNDERLINE);
    assert(!(spans[1].attrs & ESCP_ATTR_UNDERLINE));

    escp_destroy(ctx);
    return 0;
}

static int test_escp_pitch_elite(void)
{
    EscpContext *ctx = escp_create();

    /* Default is 10 cpi = 7.2 pt char width */
    float default_width = POINTS_PER_INCH / 10.0f;

    /* ESC M = 12 cpi (Elite) */
    escp_put_char(ctx, 0x1B);
    escp_put_char(ctx, 'M');
    feed_string(ctx, "Elite");

    int count = 0;
    const EscpSpan *spans = escp_get_spans(ctx, &count);
    assert(count == 1);
    float expected = POINTS_PER_INCH / 12.0f;
    assert(fabsf(spans[0].charWidth - expected) < 0.01f);
    /* Verify it's different from default */
    assert(fabsf(spans[0].charWidth - default_width) > 0.1f);

    escp_destroy(ctx);
    return 0;
}

static int test_escp_condensed(void)
{
    EscpContext *ctx = escp_create();

    /* SI (0x0F) = condensed on.  10 cpi -> 17 cpi effective */
    escp_put_char(ctx, 0x0F);
    feed_string(ctx, "Tiny");

    int count = 0;
    const EscpSpan *spans = escp_get_spans(ctx, &count);
    assert(count == 1);
    float expected = POINTS_PER_INCH / 17.0f;
    assert(fabsf(spans[0].charWidth - expected) < 0.01f);

    escp_reset(ctx);

    /* DC2 (0x12) = condensed off after condensed on */
    escp_put_char(ctx, 0x0F); /* condensed on */
    escp_put_char(ctx, 0x12); /* condensed off */
    feed_string(ctx, "Normal");

    spans = escp_get_spans(ctx, &count);
    assert(count == 1);
    float normal = POINTS_PER_INCH / 10.0f;
    assert(fabsf(spans[0].charWidth - normal) < 0.01f);

    escp_destroy(ctx);
    return 0;
}

static int test_escp_expanded(void)
{
    EscpContext *ctx = escp_create();

    /* ESC W 1 = expanded on (double width) */
    escp_put_char(ctx, 0x1B);
    escp_put_char(ctx, 'W');
    escp_put_char(ctx, 1);
    feed_string(ctx, "Wide");

    int count = 0;
    const EscpSpan *spans = escp_get_spans(ctx, &count);
    assert(count == 1);
    float expected = (POINTS_PER_INCH / 10.0f) * 2.0f;
    assert(fabsf(spans[0].charWidth - expected) < 0.01f);

    escp_reset(ctx);

    /* ESC W 0 = expanded off */
    escp_put_char(ctx, 0x1B);
    escp_put_char(ctx, 'W');
    escp_put_char(ctx, 1);
    escp_put_char(ctx, 0x1B);
    escp_put_char(ctx, 'W');
    escp_put_char(ctx, 0);
    feed_string(ctx, "Norm");

    spans = escp_get_spans(ctx, &count);
    assert(count == 1);
    float normal = POINTS_PER_INCH / 10.0f;
    assert(fabsf(spans[0].charWidth - normal) < 0.01f);

    escp_destroy(ctx);
    return 0;
}

static int test_escp_line_spacing(void)
{
    EscpContext *ctx = escp_create();

    /* Default: 1/6" = 36/216" */
    float default_lh = (36.0f / 216.0f) * POINTS_PER_INCH;

    /* ESC 0 = 1/8" spacing */
    escp_put_char(ctx, 0x1B);
    escp_put_char(ctx, '0');
    feed_string(ctx, "A");

    int count = 0;
    const EscpSpan *spans = escp_get_spans(ctx, &count);
    assert(count == 1);
    float eighth_lh = (27.0f / 216.0f) * POINTS_PER_INCH;
    assert(fabsf(spans[0].lineHeight - eighth_lh) < 0.01f);

    escp_reset(ctx);

    /* ESC 2 = 1/6" spacing (back to default) */
    escp_put_char(ctx, 0x1B);
    escp_put_char(ctx, '2');
    feed_string(ctx, "B");

    spans = escp_get_spans(ctx, &count);
    assert(count == 1);
    assert(fabsf(spans[0].lineHeight - default_lh) < 0.01f);

    escp_reset(ctx);

    /* ESC 3 n = n/216" spacing.  n=24 -> 24/216" */
    escp_put_char(ctx, 0x1B);
    escp_put_char(ctx, '3');
    escp_put_char(ctx, 24);
    feed_string(ctx, "C");

    spans = escp_get_spans(ctx, &count);
    assert(count == 1);
    float custom_lh = (24.0f / 216.0f) * POINTS_PER_INCH;
    assert(fabsf(spans[0].lineHeight - custom_lh) < 0.01f);

    escp_destroy(ctx);
    return 0;
}

static int test_escp_reset(void)
{
    EscpContext *ctx = escp_create();

    /* Set various attributes */
    escp_put_char(ctx, 0x1B);
    escp_put_char(ctx, 'E'); /* bold on */
    escp_put_char(ctx, 0x1B);
    escp_put_char(ctx, '4'); /* italic on */
    escp_put_char(ctx, 0x1B);
    escp_put_char(ctx, 'M'); /* 12 cpi */
    feed_string(ctx, "Styled");

    /* ESC @ = reset */
    escp_put_char(ctx, 0x1B);
    escp_put_char(ctx, '@');
    feed_string(ctx, "Reset");

    int count = 0;
    const EscpSpan *spans = escp_get_spans(ctx, &count);
    assert(count >= 2);

    /* First span should have bold+italic */
    assert(spans[0].attrs & ESCP_ATTR_BOLD);
    assert(spans[0].attrs & ESCP_ATTR_ITALIC);

    /* Last span should have no attributes and default pitch */
    const EscpSpan *last = &spans[count - 1];
    assert(last->attrs == 0);
    float default_width = POINTS_PER_INCH / 10.0f;
    assert(fabsf(last->charWidth - default_width) < 0.01f);

    escp_destroy(ctx);
    return 0;
}

static int test_escp_strip_mode(void)
{
    EscpContext *ctx = escp_create();

    /* Printable chars pass through */
    assert(escp_strip_to_plain_char(ctx, 'A') == 'A');
    assert(escp_strip_to_plain_char(ctx, ' ') == ' ');
    assert(escp_strip_to_plain_char(ctx, '~') == '~');

    /* CR, LF, FF pass through */
    assert(escp_strip_to_plain_char(ctx, 0x0D) == '\r');
    assert(escp_strip_to_plain_char(ctx, 0x0A) == '\n');
    assert(escp_strip_to_plain_char(ctx, 0x0C) == '\f');

    /* ESC sequence is swallowed: ESC E (bold on) */
    assert(escp_strip_to_plain_char(ctx, 0x1B) == 0);
    assert(escp_strip_to_plain_char(ctx, 'E') == 0);

    /* After ESC sequence, printable chars pass through again */
    assert(escp_strip_to_plain_char(ctx, 'B') == 'B');

    /* ESC with param: ESC - 1 (underline on) */
    assert(escp_strip_to_plain_char(ctx, 0x1B) == 0);
    assert(escp_strip_to_plain_char(ctx, '-') == 0);
    assert(escp_strip_to_plain_char(ctx, 1) == 0);

    /* Still works after */
    assert(escp_strip_to_plain_char(ctx, 'C') == 'C');

    escp_destroy(ctx);
    return 0;
}

static int test_escp_page_count(void)
{
    EscpContext *ctx = escp_create();

    /* No content = 0 pages */
    assert(escp_get_page_count(ctx) == 0);

    /* Some content = 1 page */
    feed_string(ctx, "Hello");
    assert(escp_get_page_count(ctx) == 1);

    /* Form feed = 2 pages */
    escp_put_char(ctx, 0x0C);
    feed_string(ctx, "World");
    assert(escp_get_page_count(ctx) == 2);

    /* Another form feed = 3 pages */
    escp_put_char(ctx, 0x0C);
    feed_string(ctx, "!");
    assert(escp_get_page_count(ctx) == 3);

    escp_destroy(ctx);
    return 0;
}

static int test_escp_backspace(void)
{
    EscpContext *ctx = escp_create();

    feed_string(ctx, "AB");
    assert(ctx->column == 2);

    /* Backspace */
    escp_put_char(ctx, 0x08);
    assert(ctx->column == 1);

    /* Another backspace */
    escp_put_char(ctx, 0x08);
    assert(ctx->column == 0);

    /* Backspace at column 0 stays at 0 */
    escp_put_char(ctx, 0x08);
    assert(ctx->column == 0);

    escp_destroy(ctx);
    return 0;
}

/* Suite runner */

typedef int (*EscpTestFn)(void);

int run_escp_tests(void)
{
    int passed = 0;
    int failed = 0;
    // clang-format off
    struct { const char *name; EscpTestFn fn; } tests[] = {
        { "escp_plain_text",     test_escp_plain_text },
        { "escp_cr_lf_ff",      test_escp_cr_lf_ff },
        { "escp_bold",          test_escp_bold },
        { "escp_italic",        test_escp_italic },
        { "escp_underline",     test_escp_underline },
        { "escp_pitch_elite",   test_escp_pitch_elite },
        { "escp_condensed",     test_escp_condensed },
        { "escp_expanded",      test_escp_expanded },
        { "escp_line_spacing",  test_escp_line_spacing },
        { "escp_reset",         test_escp_reset },
        { "escp_strip_mode",    test_escp_strip_mode },
        { "escp_page_count",    test_escp_page_count },
        { "escp_backspace",     test_escp_backspace },
    };
    // clang-format on

    int n = sizeof(tests) / sizeof(tests[0]);
    for (int i = 0; i < n; i++)
    {
        printf("  %-50s", tests[i].name);
        if (tests[i].fn() == 0)
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

    printf("  escp: %d passed, %d failed\n", passed, failed);
    return failed;
}
