/*
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * log.h - host diagnostics: one logger, a category per subsystem, a minimum
 * level per category chosen at run time (--log=SPEC or [runtime] log = SPEC).
 *
 *   LOG(LOG_CAT_SMD, LOG_DEBUG, "IOX %06o read -> %06o", addr, val);
 *
 * LOG() tests the level BEFORE any argument is evaluated, so a disabled
 * message costs one table read. The logger adds the level, the category and
 * the newline; call sites pass only the message.
 *
 * NOT for emulated-machine output. Terminal, printer, panel and guest console
 * bytes go through the device output callbacks, never through this logger.
 */

#ifndef LOG_H
#define LOG_H

#include <stdbool.h>

/// @brief Subsystem a message belongs to. Each has its own minimum level.
typedef enum
{
    LOG_CAT_GENERAL,
    LOG_CAT_CPU,
    LOG_CAT_MMS,      /* page tables, protection (hot path: needs ND100X_HOT_TRACE) */
    LOG_CAT_MMSMAP,   /* every virtual-to-physical translation (ND100X_HOT_TRACE) */
    LOG_CAT_TRAP,     /* internal-interrupt traps (ND100X_HOT_TRACE) */
    LOG_CAT_PKSWITCH, /* interrupt-level switches (ND100X_HOT_TRACE) */
    LOG_CAT_DEVICE,   /* device manager, IOX dispatch */
    LOG_CAT_SMD,
    LOG_CAT_FLOPPY,
    LOG_CAT_WD,
    LOG_CAT_SCSI,
    LOG_CAT_CDC,
    LOG_CAT_DRUM,
    LOG_CAT_HDLC,
    LOG_CAT_RTC,
    LOG_CAT_TERM,
    LOG_CAT_PANEL,
    LOG_CAT_TAPE,
    LOG_CAT_PRINTER,
    LOG_CAT_NET, /* telnet server, modem sockets, gateway */
    LOG_CAT_DAP,
    LOG_CAT_MACHINE,
    LOG_CAT_LOADER,
    LOG_CAT_CONFIG,
    LOG_CAT_COUNT
} LogCategory;

/// @brief Message level. A message is written when its level is at or below
///        the category's minimum (ERROR is always the lowest number).
typedef enum
{
    LOG_ERROR,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG,
    LOG_TRACE
} LogLevel;

/**
 * @brief Where finished lines go. Called with the log lock held, so a sink
 *        never sees two lines interleaved.
 * @param cat  Category of the message.
 * @param lvl  Level of the message.
 * @param line Complete line: "[LEVEL] category: message\n".
 * @param ctx  The pointer given to Log_SetSink().
 */
typedef void (*LogSinkFunc)(LogCategory cat, LogLevel lvl, const char *line, void *ctx);

/* Minimum level per category (defined in log.c). Read by Log_IsEnabled(). */
extern LogLevel g_log_min_level[LOG_CAT_COUNT];

/**
 * @brief Whether a message of this category and level would be written.
 * @details Inline: one table load and a compare. As an out-of-line call it
 *          cost 1.9% more host instructions at the MMS trace sites (measured).
 * @return true if lvl is at or below the category's minimum level.
 */
static inline bool Log_IsEnabled(LogCategory cat, LogLevel lvl)
{
    return (unsigned)cat < (unsigned)LOG_CAT_COUNT && lvl <= g_log_min_level[cat];
}

/**
 * @brief Format and write one message. Use LOG() instead, which skips the
 *        call entirely when the level is disabled.
 * @param cat Category of the message.
 * @param lvl Level of the message.
 * @param fmt printf-style format; a trailing newline is optional.
 * @param ... Arguments consumed by the conversions in fmt.
 */
void log_write(LogCategory cat, LogLevel lvl, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/**
 * @brief Set the minimum level of one category.
 * @param cat Category to change; out-of-range values are ignored.
 * @param lvl New minimum level for that category.
 */
void log_set_level(LogCategory cat, LogLevel lvl);

/**
 * @brief Set the minimum level of every category.
 */


/**
 * @brief Apply a level specification such as "smd:debug,hdlc:trace,*:warn".
 * @details Comma-separated "category:level" pairs, applied left to right.
 *          "*" or "all" names every category. Levels: error, warn, info,
 *          debug, trace. Category and level names are case-insensitive.
 * @param spec The specification string; NULL is treated as an error.
 * @return 0 on success; -1 on the first unknown category or level, in which
 *         case the pairs before it have already been applied.
 */
int log_parse_spec(const char *spec);

/**
 * @brief Route finished lines to a sink instead of the default stream
 *        (stderr on native builds, stdout on WebAssembly). NULL restores
 *        the default.
 * @param sink Function called with each finished line, or NULL.
 * @param ctx  Opaque pointer passed back to sink as its last argument.
 */
void log_set_sink(LogSinkFunc sink, void *ctx);

/**
 * @brief Name of a category as used in a level specification ("smd").
 * @param cat Category to name.
 * @return Static string with the category name, or "?" if cat is out of range.
 */
const char *log_category_name(LogCategory cat);

/**
 * @brief Name of a level ("DEBUG").
 */


/// @brief Write a message if its category is enabled at that level. The
///        arguments are not evaluated when it is not.
#define LOG(cat, lvl, ...)                                                                         \
    do                                                                                             \
    {                                                                                              \
        if (Log_IsEnabled((cat), (lvl)))                                                           \
        {                                                                                          \
            log_write((cat), (lvl), __VA_ARGS__);                                                  \
        }                                                                                          \
    } while (0)

#endif /* LOG_H */
