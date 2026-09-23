/*
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2026 Ronny Hansen
 *
 * nd_format.h - formatting into a fixed-size array, with the intent stated.
 *
 * snprintf returns the length the output WOULD have had, so a caller can tell
 * whether the string was truncated. Whether that matters depends entirely on
 * what the string is for:
 *
 *   - A line of display or log text. Truncation costs a few characters off the
 *     end of a message and nothing else. Use ND_FORMAT.
 *   - A filename, a path, or anything else fed back into the system. Truncation
 *     silently produces a DIFFERENT name, so the program opens, writes or
 *     deletes the wrong thing. Use ND_PATH and act on the result.
 *
 * Both macros take the destination ARRAY and work out its size themselves, so
 * a call site can no longer pass the wrong length, and on gcc and clang
 * passing a pointer instead of an array is a compile error rather than a
 * sizeof that quietly evaluates to 8.
 *
 * A log line is written with ND_FORMAT(line, "PIL=%d PC=%06o", pil, pc) and
 * needs nothing further. A filename is built with ND_PATH(filename, "%s/print
 * -%d.%s", dir, num, ext), which returns false when the name did not fit; the
 * caller must then give up rather than use the truncated name.
 */

#ifndef ND_FORMAT_H
#define ND_FORMAT_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/*
 * Compile-time check that the argument is an array and not a pointer. An array
 * and a pointer to its first element have different types; a pointer compares
 * equal to its own decayed type, an array does not. The result selects between
 * char[1] and char[-1], and the negative size is what fails the build.
 */
#if defined(__GNUC__) || defined(__clang__)
#define ND_REQUIRE_ARRAY(a)                                                                        \
    ((void)sizeof(                                                                                 \
        char[1 - (2 * !!__builtin_types_compatible_p(__typeof__(a), __typeof__(&(a)[0])))]))
#else
#define ND_REQUIRE_ARRAY(a) ((void)0)
#endif

/**
 * @brief Format into a fixed-size array where truncation is acceptable.
 * @details For display and log text. The result is discarded deliberately:
 *          a clipped message is not worth a branch. Use ND_PATH instead when
 *          the string is a filename or is otherwise read back by the program.
 * @param buf Destination array. Must be an array, not a pointer.
 * @param ... printf-style format and its arguments.
 */
#define ND_FORMAT(buf, ...)                                                                        \
    do                                                                                             \
    {                                                                                              \
        ND_REQUIRE_ARRAY(buf);                                                                     \
        (void)snprintf((buf), sizeof(buf), __VA_ARGS__);                                           \
    } while (0)

/**
 * @brief Format into a fixed-size array and report whether it fitted.
 * @details Use for filenames, paths and anything else the program feeds back
 *          into the system, where a truncated string names something other
 *          than what was meant.
 * @param buf Destination array. Must be an array, not a pointer.
 * @param ... printf-style format and its arguments.
 * @return true if the whole string fitted; false if it was truncated or the
 *         encoding failed, in which case buf must not be used.
 */
#define ND_PATH(buf, ...)                                                                          \
    (ND_REQUIRE_ARRAY(buf), nd_format_checked((buf), sizeof(buf), __VA_ARGS__))

/**
 * @brief Format into a buffer and say whether the whole string fitted.
 * @details Call it through ND_PATH, which supplies the size from the array.
 * @param buf  Destination buffer; always terminated when size is non-zero.
 * @param size Size of buf in bytes.
 * @param fmt  printf-style format.
 * @param ...  Arguments consumed by the conversions in fmt.
 * @return true if the whole string fitted, false on truncation or error.
 */
static inline bool nd_format_checked(char *buf, size_t size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static inline bool nd_format_checked(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (buf == NULL || size == 0)
    {
        return false;
    }
    va_start(ap, fmt);
    n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    if (n < 0)
    {
        buf[0] = '\0';
        return false;
    }
    return (size_t)n < size;
}

#endif /* ND_FORMAT_H */
