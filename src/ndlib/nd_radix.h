/*
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2026 Ronny Hansen
 *
 * nd_radix.h - the radix whole numbers are shown in, and their text. One set of forms for the
 * debugger (src/debugger/debugger.c) and the console shell (src/frontend/nd100x/nd100x_shell.c).
 *
 * The real ND tools print numbers in octal (measured: the ND-100 prints "PC=037613", a word as 6
 * digits with leading zeros), so octal is the default. The forms:
 *   octal    the digits with the B suffix, filled with zeros to the width of the value: 6 digits
 *            for a word (177773B), 3 for a byte (141B)
 *   decimal  plain, signed where the value is signed (an INTEGER: -5)
 *   hex      the digits with the H suffix (ND style), filled to the width (4 digits for a word),
 *            and a 0 in front when the first digit is a letter, as the ND assemblers need it
 *            (0FFFBH)
 */

#ifndef ND_RADIX_H
#define ND_RADIX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <strings.h>

typedef enum
{
    ND_RADIX_OCTAL = 0,
    ND_RADIX_DECIMAL,
    ND_RADIX_HEX
} nd_radix;

/* "octal", "decimal" or "hex" (any case) to *out; returns 0, or -1 for another word */
static inline int nd_radix_from_word(const char *w, nd_radix *out)
{
    if (!w)
    {
        return -1;
    }
    if (strcasecmp(w, "octal") == 0)
    {
        *out = ND_RADIX_OCTAL;
        return 0;
    }
    if (strcasecmp(w, "decimal") == 0)
    {
        *out = ND_RADIX_DECIMAL;
        return 0;
    }
    if (strcasecmp(w, "hex") == 0)
    {
        *out = ND_RADIX_HEX;
        return 0;
    }
    return -1;
}

static inline const char *nd_radix_word(nd_radix r)
{
    return r == ND_RADIX_DECIMAL ? "decimal" : r == ND_RADIX_HEX ? "hex" : "octal";
}

/* The text of a whole number of  bits (1 to 32) in radix r; with is_signed, decimal shows
 * the top bit as the sign. */
static inline void nd_num_text(char *out, size_t cap, uint32_t v, int bits, bool is_signed,
                               nd_radix r)
{
    uint32_t mask = bits >= 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    v &= mask;
    switch (r)
    {
    case ND_RADIX_DECIMAL:
        if (is_signed && ((v >> (bits - 1)) & 1u) != 0)
        {
            snprintf(out, cap, "%lld", (long long)v - (1LL << bits));
        }
        else
        {
            snprintf(out, cap, "%lu", (unsigned long)v);
        }
        break;
    case ND_RADIX_HEX:
    {
        char d[16];
        snprintf(d, sizeof d, "%0*lX", (bits + 3) / 4, (unsigned long)v);
        snprintf(out, cap, "%s%sH", d[0] >= 'A' ? "0" : "", d);
        break;
    }
    default:
        snprintf(out, cap, "%0*loB", (bits + 2) / 3, (unsigned long)v);
        break;
    }
}

#endif /* ND_RADIX_H */
