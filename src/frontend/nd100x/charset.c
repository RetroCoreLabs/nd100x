/*
 * charset.c - National 7-bit ISO 646 charset translation for the local console.
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
 */

#include "charset.h"

#include <stdio.h>
#include <string.h>

/*
 * One mapped position in an ISO 646 national variant.
 *   byte  - the 7-bit code the ND uses (e.g. 0x7B for '{')
 *   cp    - Unicode code point of the national letter (Latin-1 range)
 *   glyph - the ASCII glyph that byte normally shows ("{")
 *   utf8  - UTF-8 encoding of the national letter
 *
 * All national letters here live in the Latin-1 range (cp < 0x100), so a
 * bare Latin-1 input byte equals its code point - that is what lets us
 * accept both UTF-8 and Latin-1 keyboard input cheaply.
 */
// clang-format off
typedef struct {
    uint8_t      byte;
    uint32_t     cp;
    const char  *glyph;
    const char  *utf8;
} CharsetEntry;
// clang-format on

/* Norwegian / Danish - NS 4551 */
// clang-format off
static const CharsetEntry norwegian[] = {
    { 0x5B, 0x00C6, "[",  "\xC3\x86" }, /* AE */
    { 0x5C, 0x00D8, "\\", "\xC3\x98" }, /* O/ */
    { 0x5D, 0x00C5, "]",  "\xC3\x85" }, /* Aa */
    { 0x7B, 0x00E6, "{",  "\xC3\xA6" }, /* ae */
    { 0x7C, 0x00F8, "|",  "\xC3\xB8" }, /* o/ */
    { 0x7D, 0x00E5, "}",  "\xC3\xA5" }, /* aa */
};
// clang-format on

/* Swedish / Finnish - SEN 850200 */
// clang-format off
static const CharsetEntry swedish[] = {
    { 0x5B, 0x00C4, "[",  "\xC3\x84" }, /* AE-dots */
    { 0x5C, 0x00D6, "\\", "\xC3\x96" },
    { 0x5D, 0x00C5, "]",  "\xC3\x85" },
    { 0x5E, 0x00DC, "^",  "\xC3\x9C" }, /* U-dots */
    { 0x7B, 0x00E4, "{",  "\xC3\xA4" },
    { 0x7C, 0x00F6, "|",  "\xC3\xB6" },
    { 0x7D, 0x00E5, "}",  "\xC3\xA5" },
    { 0x7E, 0x00FC, "~",  "\xC3\xBC" }, /* u-dots */
};
// clang-format on

/* German - DIN 66003 */
// clang-format off
static const CharsetEntry german[] = {
    { 0x5B, 0x00C4, "[",  "\xC3\x84" },
    { 0x5C, 0x00D6, "\\", "\xC3\x96" },
    { 0x5D, 0x00DC, "]",  "\xC3\x9C" },
    { 0x7B, 0x00E4, "{",  "\xC3\xA4" },
    { 0x7C, 0x00F6, "|",  "\xC3\xB6" },
    { 0x7D, 0x00FC, "}",  "\xC3\xBC" },
    { 0x7E, 0x00DF, "~",  "\xC3\x9F" }, /* sharp s */
};
// clang-format on

// clang-format off
typedef struct {
    const char         *name;
    const char         *shortName;
    const CharsetEntry *entries;
    int                 count;
} CharsetDef;
// clang-format on

// clang-format off
static const CharsetDef variants[CHARSET_COUNT] = {
    [CHARSET_OFF]       = { "Off",       "off", NULL,      0 },
    [CHARSET_NORWEGIAN] = { "Norwegian", "no",  norwegian, (int)(sizeof(norwegian)/sizeof(norwegian[0])) },
    [CHARSET_SWEDISH]   = { "Swedish",   "se",  swedish,   (int)(sizeof(swedish)/sizeof(swedish[0])) },
    [CHARSET_GERMAN]    = { "German",    "de",  german,    (int)(sizeof(german)/sizeof(german[0])) },
};
// clang-format on

static CharsetVariant g_active = CHARSET_OFF;

void charset_set(CharsetVariant v)
{
    if (v >= 0 && v < CHARSET_COUNT)
    {
        g_active = v;
    }
}

CharsetVariant charset_get(void)
{
    return g_active;
}

const char *charset_name(CharsetVariant v)
{
    if (v < 0 || v >= CHARSET_COUNT)
    {
        return "?";
    }
    return variants[v].name;
}

const char *charset_short(CharsetVariant v)
{
    if (v < 0 || v >= CHARSET_COUNT)
    {
        return "?";
    }
    return variants[v].shortName;
}

bool charset_from_name(const char *s, CharsetVariant *out)
{
    if (!s || !out)
    {
        return false;
    }

    // clang-format off
    struct { const char *alias; CharsetVariant v; } table[] = {
        { "off",       CHARSET_OFF },
        { "none",      CHARSET_OFF },
        { "ascii",     CHARSET_OFF },
        { "no",        CHARSET_NORWEGIAN },
        { "norwegian", CHARSET_NORWEGIAN },
        { "norsk",     CHARSET_NORWEGIAN },
        { "dk",        CHARSET_NORWEGIAN },
        { "danish",    CHARSET_NORWEGIAN },
        { "se",        CHARSET_SWEDISH },
        { "swedish",   CHARSET_SWEDISH },
        { "fi",        CHARSET_SWEDISH },
        { "finnish",   CHARSET_SWEDISH },
        { "de",        CHARSET_GERMAN },
        { "german",    CHARSET_GERMAN },
    };
    // clang-format on

    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
    {
        if (strcasecmp(s, table[i].alias) == 0)
        {
            *out = table[i].v;
            return true;
        }
    }
    return false;
}

int charset_mapping_count(CharsetVariant v)
{
    if (v < 0 || v >= CHARSET_COUNT)
    {
        return 0;
    }
    return variants[v].count;
}

bool charset_mapping_at(CharsetVariant v, int i, uint8_t *byte, const char **glyph,
                        const char **utf8)
{
    if (v < 0 || v >= CHARSET_COUNT)
    {
        return false;
    }
    if (i < 0 || i >= variants[v].count)
    {
        return false;
    }
    const CharsetEntry *e = &variants[v].entries[i];
    if (byte)
    {
        *byte = e->byte;
    }
    if (glyph)
    {
        *glyph = e->glyph;
    }
    if (utf8)
    {
        *utf8 = e->utf8;
    }
    return true;
}

void charset_emit_host(char c)
{
    unsigned char b = (unsigned char)c;
    const CharsetDef *d = &variants[g_active];

    if (d->entries)
    {
        for (int i = 0; i < d->count; i++)
        {
            if (d->entries[i].byte == b)
            {
                fputs(d->entries[i].utf8, stdout);
                return;
            }
        }
    }
    putchar(c);
}

/* Decode one code point from seq[pos..len). Advances *pos.
 * Falls back to treating a stray high byte as Latin-1 (1 byte = 1 cp). */
static uint32_t decode_one(const char *seq, int len, int *pos)
{
    unsigned char b0 = (unsigned char)seq[*pos];

    if (b0 < 0x80)
    {
        (*pos)++;
        return b0;
    }

    int extra;
    uint32_t cp;
    if ((b0 & 0xE0) == 0xC0)
    {
        extra = 1;
        cp = b0 & 0x1F;
    }
    else if ((b0 & 0xF0) == 0xE0)
    {
        extra = 2;
        cp = b0 & 0x0F;
    }
    else if ((b0 & 0xF8) == 0xF0)
    {
        extra = 3;
        cp = b0 & 0x07;
    }
    else
    {
        (*pos)++;
        return b0;
    } /* invalid lead -> Latin-1 */

    if (*pos + extra >= len)
    {
        (*pos)++;
        return b0;
    } /* truncated -> Latin-1 */

    for (int k = 1; k <= extra; k++)
    {
        unsigned char bk = (unsigned char)seq[*pos + k];
        if ((bk & 0xC0) != 0x80)
        {
            (*pos)++;
            return b0;
        } /* bad cont -> Latin-1 */
        cp = (cp << 6) | (bk & 0x3F);
    }
    *pos += extra + 1;
    return cp;
}

/* Map a national letter's code point to its 7-bit position, independent of
 * the host keyboard layout. The active variant wins (resolves cross-layout
 * conflicts such as Swedish vs German 'u-umlaut'); other variants fill in the rest,
 * so a Norwegian keyboard can still drive a Swedish/German emulated charset -
 * the key's *position* is what reaches the ND. Returns -1 if unmapped. */
static int input_byte_for_cp(uint32_t cp)
{
    const CharsetDef *act = &variants[g_active];
    for (int i = 0; i < act->count; i++)
    {
        if (act->entries[i].cp == cp)
        {
            return act->entries[i].byte;
        }
    }

    for (CharsetVariant v = CHARSET_NORWEGIAN; v < CHARSET_COUNT; v++)
    {
        if (v == g_active)
        {
            continue;
        }
        const CharsetDef *d = &variants[v];
        for (int i = 0; i < d->count; i++)
        {
            if (d->entries[i].cp == cp)
            {
                return d->entries[i].byte;
            }
        }
    }
    return -1;
}

int charset_translate_input(const char *seq, int len, char *out, int outmax)
{
    if (!seq || !out || outmax <= 0)
    {
        return 0;
    }

    /* OFF: verbatim copy - preserves multi-byte escape sequences as-is. */
    if (g_active == CHARSET_OFF)
    {
        int n = (len < outmax) ? len : outmax;
        memcpy(out, seq, n);
        return n;
    }

    int pos = 0;
    int n = 0;

    while (pos < len && n < outmax)
    {
        uint32_t cp = decode_one(seq, len, &pos);

        if (cp < 0x80)
        { /* plain ASCII (incl. ESC sequences) */
            out[n++] = (char)cp;
            continue;
        }

        /* National letter -> 7-bit position (host-layout independent). */
        int b = input_byte_for_cp(cp);
        if (b >= 0)
        {
            out[n++] = (char)b;
        }
        /* else: unmapped non-ASCII code point, drop it. */
    }
    return n;
}
