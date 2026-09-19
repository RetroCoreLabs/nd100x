/*
 * Unit tests for the ESC/P interpreter module.
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
        Escp_PutChar(ctx, (uint8_t)*p);
    }
}

/* Tests */

static int test_escp_plain_text(void)
{
    EscpContext *ctx = Escp_Create();
    assert(ctx != NULL);

    feed_string(ctx, "Hello");

    int count = 0;
    const EscpSpan *spans = Escp_GetSpans(ctx, &count);
    assert(count == 1);
    assert(strcmp(spans[0].text, "Hello") == 0);
    assert(spans[0].column == 0);
    assert(spans[0].line == 0);
    assert(spans[0].page == 0);
    assert(spans[0].attrs == 0);

    Escp_Destroy(ctx);
    return 0;
}

static int test_escp_cr_lf_ff(void)
{
    EscpContext *ctx = Escp_Create();

    /* Feed text, CR, more text - CR resets column */
    feed_string(ctx, "ABC");
    Escp_PutChar(ctx, 0x0D); /* CR */
    assert(ctx->column == 0);

    /* LF advances line */
    Escp_PutChar(ctx, 0x0A); /* LF */
    assert(ctx->line == 1);
    assert(ctx->column == 0);

    /* FF advances page */
    feed_string(ctx, "X");
    Escp_PutChar(ctx, 0x0C); /* FF */
    assert(ctx->page == 1);
    assert(ctx->line == 0);
    assert(ctx->column == 0);

    Escp_Destroy(ctx);
    return 0;
}

static int test_escp_bold(void)
{
    EscpContext *ctx = Escp_Create();

    /* ESC E = bold on */
    Escp_PutChar(ctx, 0x1B);
    Escp_PutChar(ctx, 'E');
    feed_string(ctx, "Bold");
    /* ESC F = bold off */
    Escp_PutChar(ctx, 0x1B);
    Escp_PutChar(ctx, 'F');
    feed_string(ctx, "Normal");

    int count = 0;
    const EscpSpan *spans = Escp_GetSpans(ctx, &count);
    assert(count >= 2);
    assert(spans[0].attrs & ESCP_ATTR_BOLD);
    assert(strcmp(spans[0].text, "Bold") == 0);
    assert(!(spans[1].attrs & ESCP_ATTR_BOLD));
    assert(strcmp(spans[1].text, "Normal") == 0);

    Escp_Destroy(ctx);
    return 0;
}

static int test_escp_italic(void)
{
    EscpContext *ctx = Escp_Create();

    /* ESC 4 = italic on */
    Escp_PutChar(ctx, 0x1B);
    Escp_PutChar(ctx, '4');
    feed_string(ctx, "Italic");
    /* ESC 5 = italic off */
    Escp_PutChar(ctx, 0x1B);
    Escp_PutChar(ctx, '5');
    feed_string(ctx, "Normal");

    int count = 0;
    const EscpSpan *spans = Escp_GetSpans(ctx, &count);
    assert(count >= 2);
    assert(spans[0].attrs & ESCP_ATTR_ITALIC);
    assert(!(spans[1].attrs & ESCP_ATTR_ITALIC));

    Escp_Destroy(ctx);
    return 0;
}

static int test_escp_underline(void)
{
    EscpContext *ctx = Escp_Create();

    /* ESC - 1 = underline on */
    Escp_PutChar(ctx, 0x1B);
    Escp_PutChar(ctx, '-');
    Escp_PutChar(ctx, 1);
    feed_string(ctx, "Under");
    /* ESC - 0 = underline off */
    Escp_PutChar(ctx, 0x1B);
    Escp_PutChar(ctx, '-');
    Escp_PutChar(ctx, 0);
    feed_string(ctx, "Normal");

    int count = 0;
    const EscpSpan *spans = Escp_GetSpans(ctx, &count);
    assert(count >= 2);
    assert(spans[0].attrs & ESCP_ATTR_UNDERLINE);
    assert(!(spans[1].attrs & ESCP_ATTR_UNDERLINE));

    Escp_Destroy(ctx);
    return 0;
}

static int test_escp_pitch_elite(void)
{
    EscpContext *ctx = Escp_Create();

    /* Default is 10 cpi = 7.2 pt char width */
    float defaultWidth = POINTS_PER_INCH / 10.0f;

    /* ESC M = 12 cpi (Elite) */
    Escp_PutChar(ctx, 0x1B);
    Escp_PutChar(ctx, 'M');
    feed_string(ctx, "Elite");

    int count = 0;
    const EscpSpan *spans = Escp_GetSpans(ctx, &count);
    assert(count == 1);
    float expected = POINTS_PER_INCH / 12.0f;
    assert(fabsf(spans[0].charWidth - expected) < 0.01f);
    /* Verify it's different from default */
    assert(fabsf(spans[0].charWidth - defaultWidth) > 0.1f);

    Escp_Destroy(ctx);
    return 0;
}

static int test_escp_condensed(void)
{
    EscpContext *ctx = Escp_Create();

    /* SI (0x0F) = condensed on.  10 cpi -> 17 cpi effective */
    Escp_PutChar(ctx, 0x0F);
    feed_string(ctx, "Tiny");

    int count = 0;
    const EscpSpan *spans = Escp_GetSpans(ctx, &count);
    assert(count == 1);
    float expected = POINTS_PER_INCH / 17.0f;
    assert(fabsf(spans[0].charWidth - expected) < 0.01f);

    Escp_Reset(ctx);

    /* DC2 (0x12) = condensed off after condensed on */
    Escp_PutChar(ctx, 0x0F); /* condensed on */
    Escp_PutChar(ctx, 0x12); /* condensed off */
    feed_string(ctx, "Normal");

    spans = Escp_GetSpans(ctx, &count);
    assert(count == 1);
    float normal = POINTS_PER_INCH / 10.0f;
    assert(fabsf(spans[0].charWidth - normal) < 0.01f);

    Escp_Destroy(ctx);
    return 0;
}

static int test_escp_expanded(void)
{
    EscpContext *ctx = Escp_Create();

    /* ESC W 1 = expanded on (double width) */
    Escp_PutChar(ctx, 0x1B);
    Escp_PutChar(ctx, 'W');
    Escp_PutChar(ctx, 1);
    feed_string(ctx, "Wide");

    int count = 0;
    const EscpSpan *spans = Escp_GetSpans(ctx, &count);
    assert(count == 1);
    float expected = (POINTS_PER_INCH / 10.0f) * 2.0f;
    assert(fabsf(spans[0].charWidth - expected) < 0.01f);

    Escp_Reset(ctx);

    /* ESC W 0 = expanded off */
    Escp_PutChar(ctx, 0x1B);
    Escp_PutChar(ctx, 'W');
    Escp_PutChar(ctx, 1);
    Escp_PutChar(ctx, 0x1B);
    Escp_PutChar(ctx, 'W');
    Escp_PutChar(ctx, 0);
    feed_string(ctx, "Norm");

    spans = Escp_GetSpans(ctx, &count);
    assert(count == 1);
    float normal = POINTS_PER_INCH / 10.0f;
    assert(fabsf(spans[0].charWidth - normal) < 0.01f);

    Escp_Destroy(ctx);
    return 0;
}

static int test_escp_line_spacing(void)
{
    EscpContext *ctx = Escp_Create();

    /* Default: 1/6" = 36/216" */
    float default_lh = (36.0f / 216.0f) * POINTS_PER_INCH;

    /* ESC 0 = 1/8" spacing */
    Escp_PutChar(ctx, 0x1B);
    Escp_PutChar(ctx, '0');
    feed_string(ctx, "A");

    int count = 0;
    const EscpSpan *spans = Escp_GetSpans(ctx, &count);
    assert(count == 1);
    float eighth_lh = (27.0f / 216.0f) * POINTS_PER_INCH;
    assert(fabsf(spans[0].lineHeight - eighth_lh) < 0.01f);

    Escp_Reset(ctx);

    /* ESC 2 = 1/6" spacing (back to default) */
    Escp_PutChar(ctx, 0x1B);
    Escp_PutChar(ctx, '2');
    feed_string(ctx, "B");

    spans = Escp_GetSpans(ctx, &count);
    assert(count == 1);
    assert(fabsf(spans[0].lineHeight - default_lh) < 0.01f);

    Escp_Reset(ctx);

    /* ESC 3 n = n/216" spacing.  n=24 -> 24/216" */
    Escp_PutChar(ctx, 0x1B);
    Escp_PutChar(ctx, '3');
    Escp_PutChar(ctx, 24);
    feed_string(ctx, "C");

    spans = Escp_GetSpans(ctx, &count);
    assert(count == 1);
    float custom_lh = (24.0f / 216.0f) * POINTS_PER_INCH;
    assert(fabsf(spans[0].lineHeight - custom_lh) < 0.01f);

    Escp_Destroy(ctx);
    return 0;
}

static int test_escp_reset(void)
{
    EscpContext *ctx = Escp_Create();

    /* Set various attributes */
    Escp_PutChar(ctx, 0x1B);
    Escp_PutChar(ctx, 'E'); /* bold on */
    Escp_PutChar(ctx, 0x1B);
    Escp_PutChar(ctx, '4'); /* italic on */
    Escp_PutChar(ctx, 0x1B);
    Escp_PutChar(ctx, 'M'); /* 12 cpi */
    feed_string(ctx, "Styled");

    /* ESC @ = reset */
    Escp_PutChar(ctx, 0x1B);
    Escp_PutChar(ctx, '@');
    feed_string(ctx, "Reset");

    int count = 0;
    const EscpSpan *spans = Escp_GetSpans(ctx, &count);
    assert(count >= 2);

    /* First span should have bold+italic */
    assert(spans[0].attrs & ESCP_ATTR_BOLD);
    assert(spans[0].attrs & ESCP_ATTR_ITALIC);

    /* Last span should have no attributes and default pitch */
    const EscpSpan *last = &spans[count - 1];
    assert(last->attrs == 0);
    float defaultWidth = POINTS_PER_INCH / 10.0f;
    assert(fabsf(last->charWidth - defaultWidth) < 0.01f);

    Escp_Destroy(ctx);
    return 0;
}

static int test_escp_strip_mode(void)
{
    EscpContext *ctx = Escp_Create();

    /* Printable chars pass through */
    assert(Escp_StripToPlainChar(ctx, 'A') == 'A');
    assert(Escp_StripToPlainChar(ctx, ' ') == ' ');
    assert(Escp_StripToPlainChar(ctx, '~') == '~');

    /* CR, LF, FF pass through */
    assert(Escp_StripToPlainChar(ctx, 0x0D) == '\r');
    assert(Escp_StripToPlainChar(ctx, 0x0A) == '\n');
    assert(Escp_StripToPlainChar(ctx, 0x0C) == '\f');

    /* ESC sequence is swallowed: ESC E (bold on) */
    assert(Escp_StripToPlainChar(ctx, 0x1B) == 0);
    assert(Escp_StripToPlainChar(ctx, 'E') == 0);

    /* After ESC sequence, printable chars pass through again */
    assert(Escp_StripToPlainChar(ctx, 'B') == 'B');

    /* ESC with param: ESC - 1 (underline on) */
    assert(Escp_StripToPlainChar(ctx, 0x1B) == 0);
    assert(Escp_StripToPlainChar(ctx, '-') == 0);
    assert(Escp_StripToPlainChar(ctx, 1) == 0);

    /* Still works after */
    assert(Escp_StripToPlainChar(ctx, 'C') == 'C');

    Escp_Destroy(ctx);
    return 0;
}

static int test_escp_page_count(void)
{
    EscpContext *ctx = Escp_Create();

    /* No content = 0 pages */
    assert(Escp_GetPageCount(ctx) == 0);

    /* Some content = 1 page */
    feed_string(ctx, "Hello");
    assert(Escp_GetPageCount(ctx) == 1);

    /* Form feed = 2 pages */
    Escp_PutChar(ctx, 0x0C);
    feed_string(ctx, "World");
    assert(Escp_GetPageCount(ctx) == 2);

    /* Another form feed = 3 pages */
    Escp_PutChar(ctx, 0x0C);
    feed_string(ctx, "!");
    assert(Escp_GetPageCount(ctx) == 3);

    Escp_Destroy(ctx);
    return 0;
}

static int test_escp_backspace(void)
{
    EscpContext *ctx = Escp_Create();

    feed_string(ctx, "AB");
    assert(ctx->column == 2);

    /* Backspace */
    Escp_PutChar(ctx, 0x08);
    assert(ctx->column == 1);

    /* Another backspace */
    Escp_PutChar(ctx, 0x08);
    assert(ctx->column == 0);

    /* Backspace at column 0 stays at 0 */
    Escp_PutChar(ctx, 0x08);
    assert(ctx->column == 0);

    Escp_Destroy(ctx);
    return 0;
}

/* Suite runner */

typedef int (*escp_test_fn)(void);

int run_escp_tests(void)
{
    int passed = 0;
    int failed = 0;
    // clang-format off
    struct { const char *name; escp_test_fn fn; } tests[] = {
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
