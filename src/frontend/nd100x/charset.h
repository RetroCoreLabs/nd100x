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

/**
 * @brief Select the active national charset variant for the local console.
 *        Values outside CHARSET_OFF..CHARSET_COUNT-1 are ignored.
 * @param v The variant to activate.
 */
void charset_set(CharsetVariant v);

/**
 * @brief Return the currently active national charset variant.
 * @return The active CharsetVariant (CHARSET_OFF when translation is disabled).
 */
CharsetVariant charset_get(void);

/* Human and CLI names. */

/**
 * @brief Return the human-readable name of a variant, e.g. "Norwegian" or "Off".
 * @param v The variant to name.
 * @return The name, or "?" when v is out of range.
 */
const char *charset_name(CharsetVariant v); /* "Norwegian" / "Off" */

/**
 * @brief Return the short CLI code of a variant, e.g. "no" or "off".
 * @param v The variant to name.
 * @return The short code, or "?" when v is out of range.
 */
const char *charset_short(CharsetVariant v); /* "no" / "off"        */

/* Parse a CLI/menu name. Accepts short codes and full names
 * (off, none, no, norwegian, dk, danish, se, swedish, fi, finnish,
 *  de, german). Returns false on no match. */

/**
 * @brief Match a CLI or menu name against the alias table (off, none, ascii, no,
 *        norwegian, norsk, dk, danish, se, swedish, fi, finnish, de, german),
 *        ignoring case.
 * @param s The name to match; NULL yields false.
 * @param out Receives the matched variant; NULL yields false.
 * @return true when the name matched, false otherwise.
 */
bool charset_from_name(const char *s, CharsetVariant *out);

/* OUTPUT: write one emulated 7-bit byte to host stdout, translating the
 * national positions to UTF-8 when a variant is active. */

/**
 * @brief Write one emulated 7-bit byte to stdout, replacing a national position
 *        with its UTF-8 letter when the active variant maps that byte.
 * @param c The 7-bit byte from the emulated terminal.
 */
void charset_emit_host(char c);

/* INPUT: translate a raw host key byte sequence (possibly UTF-8 or Latin-1)
 * into emulated 7-bit bytes. Returns the number of bytes written to out
 * (<= outmax). When CHARSET_OFF, the sequence is copied verbatim. */

/**
 * @brief Translate a raw host key byte sequence into emulated 7-bit bytes,
 *        decoding UTF-8/Latin-1 code points to their national 7-bit positions.
 *        ASCII bytes (including ESC sequences) pass through; unmapped non-ASCII
 *        code points are dropped. With CHARSET_OFF the sequence is copied verbatim.
 * @param seq The raw host bytes; NULL yields 0.
 * @param len Number of bytes in seq.
 * @param out Buffer receiving the emulated bytes; NULL yields 0.
 * @param outmax Capacity of out; must be > 0.
 * @return Number of bytes written to out (<= outmax).
 */
int charset_translate_input(const char *seq, int len, char *out, int outmax);

/* For the F12 detail view: enumerate the mappings of a variant.
 * On success fills *byte (the 7-bit code), *glyph (ASCII glyph, e.g. "{")
 * and *utf8 (the national letter). Returns false when i is out of range. */

/**
 * @brief Return how many 7-bit positions a variant remaps.
 * @param v The variant to count.
 * @return The number of mappings, or 0 when v is out of range or CHARSET_OFF.
 */
int charset_mapping_count(CharsetVariant v);

/**
 * @brief Fetch one mapping of a variant for the F12 detail view.
 * @param v The variant to read.
 * @param i Mapping index, 0 .. charset_mapping_count(v) - 1.
 * @param byte Receives the 7-bit code; may be NULL.
 * @param glyph Receives the ASCII glyph, e.g. "{"; may be NULL.
 * @param utf8 Receives the national letter in UTF-8; may be NULL.
 * @return true when the mapping was returned, false when v or i is out of range.
 */
bool charset_mapping_at(CharsetVariant v, int i, uint8_t *byte, const char **glyph,
                        const char **utf8);

#endif /* CHARSET_H */
