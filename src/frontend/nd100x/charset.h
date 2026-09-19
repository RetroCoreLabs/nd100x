/*
 * charset.h - National 7-bit ISO 646 charset translation: API.
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

/*
 * National 7-bit charset translation for the LOCAL console only.
 *
 * The emulated terminal speaks 7-bit ISO 646. National variants reuse the
 * ASCII positions [ \ ] ^ { | } ~ for accented letters. This module maps
 * those positions to/from UTF-8 (and Latin-1 input) so a modern UTF-8
 * terminal shows the national letters and the national keyboard keys reach
 * the ND as the correct 7-bit code.
 *
 * IMPORTANT: This is a presentation layer for the native local console
 * (stdout/keyboard) and serial-via-console only. The telnet/TCP path is
 * NEVER touched - those bytes stay raw 7-bit.
 */

#ifndef CHARSET_H
#define CHARSET_H

#include <stdint.h>
#include <stdbool.h>

// clang-format off
typedef enum {
    CHARSET_OFF = 0,    /* ASCII passthrough (default) - { | } [ \ ] stay literal */
    CHARSET_NORWEGIAN,  /* NS 4551    (Norwegian / Danish) */
    CHARSET_SWEDISH,    /* SEN 850200 (Swedish / Finnish)  */
    CHARSET_GERMAN,     /* DIN 66003  (German)             */
    CHARSET_COUNT
} CharsetVariant;
// clang-format on

/* Active variant (local console only). */
void charset_set(CharsetVariant v);
CharsetVariant charset_get(void);

/* Human and CLI names. */
const char *charset_name(CharsetVariant v);  /* "Norwegian" / "Off" */
const char *charset_short(CharsetVariant v); /* "no" / "off"        */

/* Parse a CLI/menu name. Accepts short codes and full names
 * (off, none, no, norwegian, dk, danish, se, swedish, fi, finnish,
 *  de, german). Returns false on no match. */
bool charset_from_name(const char *s, CharsetVariant *out);

/* OUTPUT: write one emulated 7-bit byte to host stdout, translating the
 * national positions to UTF-8 when a variant is active. */
void charset_emit_host(char c);

/* INPUT: translate a raw host key byte sequence (possibly UTF-8 or Latin-1)
 * into emulated 7-bit bytes. Returns the number of bytes written to out
 * (<= outmax). When CHARSET_OFF, the sequence is copied verbatim. */
int charset_translate_input(const char *seq, int len, char *out, int outmax);

/* For the F12 detail view: enumerate the mappings of a variant.
 * On success fills *byte (the 7-bit code), *glyph (ASCII glyph, e.g. "{")
 * and *utf8 (the national letter). Returns false when i is out of range. */
int charset_mapping_count(CharsetVariant v);
bool charset_mapping_at(CharsetVariant v, int i, uint8_t *byte, const char **glyph,
                        const char **utf8);

#endif /* CHARSET_H */
