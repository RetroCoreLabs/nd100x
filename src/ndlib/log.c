/*
 * log.c - Logging: per-category levels, level parsing and output sink.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * This file is originated from the nd100x project and the RetroCore project
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

#include "log.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef __EMSCRIPTEN__
#include <pthread.h>
#endif

/* Indexed by LogCategory; these are the names a level specification uses. */
static const char *const s_category_names[LOG_CAT_COUNT] = {
    [LOG_CAT_GENERAL] = "general", [LOG_CAT_CPU] = "cpu",       [LOG_CAT_MMS] = "mms",
    [LOG_CAT_MMSMAP] = "mmsmap",   [LOG_CAT_TRAP] = "trap",     [LOG_CAT_PKSWITCH] = "pkswitch",
    [LOG_CAT_DEVICE] = "device",   [LOG_CAT_SMD] = "smd",       [LOG_CAT_FLOPPY] = "floppy",
    [LOG_CAT_WD] = "wd",           [LOG_CAT_SCSI] = "scsi",     [LOG_CAT_CDC] = "cdc",
    [LOG_CAT_DRUM] = "drum",       [LOG_CAT_HDLC] = "hdlc",     [LOG_CAT_RTC] = "rtc",
    [LOG_CAT_TERM] = "term",       [LOG_CAT_PANEL] = "panel",   [LOG_CAT_TAPE] = "tape",
    [LOG_CAT_PRINTER] = "printer", [LOG_CAT_NET] = "net",       [LOG_CAT_DAP] = "dap",
    [LOG_CAT_MACHINE] = "machine", [LOG_CAT_LOADER] = "loader", [LOG_CAT_CONFIG] = "config",
};

/* Indexed by LogLevel. */
static const char *const s_level_names[] = {"ERROR", "WARN", "INFO", "DEBUG", "TRACE"};

#define LOG_LEVEL_COUNT ((int)(sizeof(s_level_names) / sizeof(s_level_names[0])))

/* Minimum level per category. Written at start-up (and from a debugger
 * session), read by every LOG(); a torn read can only show the old or the new
 * level, both of which are valid. */
LogLevel g_log_min_level[LOG_CAT_COUNT] = {
    [0 ... LOG_CAT_COUNT - 1] = LOG_INFO,
};

static LogSinkFunc s_sink;
static void *s_sink_ctx;

#ifndef __EMSCRIPTEN__
/* One line at a time: telnet, DAP and modem threads log too. */
static pthread_mutex_t s_log_lock = PTHREAD_MUTEX_INITIALIZER;
#endif


void Log_SetLevel(LogCategory cat, LogLevel lvl)
{
    if ((unsigned)cat < (unsigned)LOG_CAT_COUNT)
    {
        g_log_min_level[cat] = lvl;
    }
}

void Log_SetAllLevels(LogLevel lvl)
{
    for (int i = 0; i < LOG_CAT_COUNT; i++)
    {
        g_log_min_level[i] = lvl;
    }
}

void Log_SetSink(LogSinkFunc sink, void *ctx)
{
#ifndef __EMSCRIPTEN__
    pthread_mutex_lock(&s_log_lock);
#endif
    s_sink = sink;
    s_sink_ctx = ctx;
#ifndef __EMSCRIPTEN__
    pthread_mutex_unlock(&s_log_lock);
#endif
}

const char *Log_CategoryName(LogCategory cat)
{
    if ((unsigned)cat >= (unsigned)LOG_CAT_COUNT)
    {
        return "?";
    }
    return s_category_names[cat];
}

const char *Log_LevelName(LogLevel lvl)
{
    if ((int)lvl < 0 || (int)lvl >= LOG_LEVEL_COUNT)
    {
        return "?";
    }
    return s_level_names[lvl];
}

void Log_Write(LogCategory cat, LogLevel lvl, const char *fmt, ...)
{
    char line[1024];
    int head = snprintf(line, sizeof(line), "[%s] %s: ", Log_LevelName(lvl), Log_CategoryName(cat));
    if (head < 0)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(line + head, sizeof(line) - (size_t)head, fmt, args);
    va_end(args);

    /* Exactly one newline, whether or not the caller supplied one. A message
     * that filled the buffer is cut, and still ends the line. */
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
    {
        len--;
    }
    if (len > sizeof(line) - 2)
    {
        len = sizeof(line) - 2;
    }
    line[len] = '\n';
    line[len + 1] = '\0';

#ifndef __EMSCRIPTEN__
    pthread_mutex_lock(&s_log_lock);
#endif
    if (s_sink)
    {
        s_sink(cat, lvl, line, s_sink_ctx);
    }
    else
    {
#ifdef __EMSCRIPTEN__
        /* stdout reaches the browser console as console.log; stderr would
         * turn every INFO line into console.error. */
        fputs(line, stdout);
#else
        /* On native builds stdout carries the guest terminal. */
        fputs(line, stderr);
#endif
    }
#ifndef __EMSCRIPTEN__
    pthread_mutex_unlock(&s_log_lock);
#endif
}

/* Case-insensitive compare of a NUL-terminated name against [s, s + n). */
static bool name_matches(const char *name, const char *s, size_t n)
{
    if (strlen(name) != n)
    {
        return false;
    }
    for (size_t i = 0; i < n; i++)
    {
        if (tolower((unsigned char)name[i]) != tolower((unsigned char)s[i]))
        {
            return false;
        }
    }
    return true;
}

int Log_ParseSpec(const char *spec)
{
    if (!spec)
    {
        return -1;
    }

    const char *p = spec;
    while (*p)
    {
        const char *end = strchr(p, ',');
        size_t item_len = end ? (size_t)(end - p) : strlen(p);
        const char *colon = memchr(p, ':', item_len);
        if (!colon)
        {
            return -1;
        }

        size_t cat_len = (size_t)(colon - p);
        const char *lvl_s = colon + 1;
        size_t lvl_len = item_len - cat_len - 1;

        int lvl = -1;
        for (int i = 0; i < LOG_LEVEL_COUNT; i++)
        {
            if (name_matches(s_level_names[i], lvl_s, lvl_len))
            {
                lvl = i;
                break;
            }
        }
        if (lvl < 0)
        {
            return -1;
        }

        if (name_matches("*", p, cat_len) || name_matches("all", p, cat_len))
        {
            Log_SetAllLevels((LogLevel)lvl);
        }
        else
        {
            int cat = -1;
            for (int i = 0; i < LOG_CAT_COUNT; i++)
            {
                if (name_matches(s_category_names[i], p, cat_len))
                {
                    cat = i;
                    break;
                }
            }
            if (cat < 0)
            {
                return -1;
            }
            Log_SetLevel((LogCategory)cat, (LogLevel)lvl);
        }

        p = end ? end + 1 : p + item_len;
    }
    return 0;
}
