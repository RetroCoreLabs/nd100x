/*
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * test_log.c - unit tests for the category/level logger (src/ndlib/log.c).
 */

#include "log.h"

#include <stdio.h>
#include <string.h>

static int g_pass;
static int g_fail;

#define CHECK(cond, what)                                   \
    do                                                      \
    {                                                       \
        if (cond)                                           \
        {                                                   \
            g_pass++;                                       \
        }                                                   \
        else                                                \
        {                                                   \
            g_fail++;                                       \
            printf("FAIL: %s (line %d)\n", what, __LINE__); \
        }                                                   \
    } while (0)

/* Test sink: remembers the last line and counts calls. */
static char g_last[1100];
static int g_lines;
static LogCategory g_last_cat;
static LogLevel g_last_lvl;

static void capture_sink(LogCategory cat, LogLevel lvl, const char *line, void *ctx)
{
    (void)ctx;
    g_lines++;
    g_last_cat = cat;
    g_last_lvl = lvl;
    snprintf(g_last, sizeof(g_last), "%s", line);
}

static int g_evaluated;

static int side_effect(void)
{
    g_evaluated++;
    return 42;
}

int main(void)
{
    printf("=== logger tests ===\n");
    Log_SetSink(capture_sink, NULL);

    /* Defaults: every category at INFO. */
    CHECK(Log_IsEnabled(LOG_CAT_SMD, LOG_ERROR), "ERROR enabled by default");
    CHECK(Log_IsEnabled(LOG_CAT_SMD, LOG_INFO), "INFO enabled by default");
    CHECK(!Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG), "DEBUG disabled by default");

    /* A disabled message is neither formatted nor are its arguments evaluated. */
    g_lines = 0;
    g_evaluated = 0;
    LOG(LOG_CAT_SMD, LOG_DEBUG, "value %d", side_effect());
    CHECK(g_lines == 0, "disabled message not written");
    CHECK(g_evaluated == 0, "disabled message's arguments not evaluated");

    /* Line format and a single trailing newline. */
    LOG(LOG_CAT_SMD, LOG_INFO, "hello %d\n", 7);
    CHECK(g_lines == 1, "enabled message written once");
    CHECK(strcmp(g_last, "[INFO] smd: hello 7\n") == 0, "line format with caller newline");
    LOG(LOG_CAT_NET, LOG_WARN, "no newline");
    CHECK(strcmp(g_last, "[WARN] net: no newline\n") == 0, "line format without caller newline");
    CHECK(g_last_cat == LOG_CAT_NET && g_last_lvl == LOG_WARN, "sink gets category and level");

    /* Spec parsing. */
    CHECK(Log_ParseSpec("smd:debug,hdlc:TRACE") == 0, "valid spec accepted");
    CHECK(Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG), "smd raised to debug");
    CHECK(!Log_IsEnabled(LOG_CAT_SMD, LOG_TRACE), "smd not raised to trace");
    CHECK(Log_IsEnabled(LOG_CAT_HDLC, LOG_TRACE), "hdlc raised to trace, level name case-insensitive");
    CHECK(!Log_IsEnabled(LOG_CAT_CPU, LOG_DEBUG), "other categories unchanged");

    CHECK(Log_ParseSpec("*:warn") == 0, "wildcard accepted");
    CHECK(!Log_IsEnabled(LOG_CAT_SMD, LOG_INFO), "wildcard lowered smd to warn");
    CHECK(Log_IsEnabled(LOG_CAT_CPU, LOG_WARN), "wildcard set cpu to warn");

    CHECK(Log_ParseSpec("all:info,SMD:debug") == 0, "all + later override accepted");
    CHECK(Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG), "later pair wins, category case-insensitive");
    CHECK(!Log_IsEnabled(LOG_CAT_CPU, LOG_DEBUG), "all:info applied to cpu");

    CHECK(Log_ParseSpec("nosuch:debug") == -1, "unknown category rejected");
    CHECK(Log_ParseSpec("smd:loud") == -1, "unknown level rejected");
    CHECK(Log_ParseSpec("smd") == -1, "missing level rejected");
    CHECK(Log_ParseSpec(NULL) == -1, "NULL spec rejected");
    CHECK(Log_ParseSpec("") == 0, "empty spec is a no-op");

    /* Out-of-range category never enabled, never crashes. */
    CHECK(!Log_IsEnabled((LogCategory)LOG_CAT_COUNT, LOG_ERROR), "out-of-range category disabled");
    CHECK(strcmp(Log_CategoryName((LogCategory)999), "?") == 0, "out-of-range category name");

    /* A message longer than the line buffer is cut and still ends the line. */
    char big[2000];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    LOG(LOG_CAT_SMD, LOG_ERROR, "%s", big);
    size_t n = strlen(g_last);
    CHECK(n > 0 && n < sizeof(g_last) && g_last[n - 1] == '\n', "long message cut and newline-terminated");

    Log_SetSink(NULL, NULL);
    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
