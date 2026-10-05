/*
 * gunzip.c - In-memory gzip (RFC 1952) decompression.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * The floppy archive stores every image as <name>.img.gz. This decoder lets the
 * emulator mount such an image straight from a download buffer without a zlib
 * dependency (the Windows, WebAssembly and RISC-V builds do not carry one).
 * The DEFLATE part is a canonical-Huffman decoder in the style of zlib's
 * contrib/puff.c: small, bounds-checked, bit-at-a-time. Speed is not a concern
 * for images of about 1.4 MB.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2 or (at your option) any
 * later version. See COPYING.
 */
#include "gunzip.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GZ_MAX_OUT       (500UL * 1024UL * 1024UL) /* same cap as dl_download_file() */
#define GZ_MAX_BITS      15
#define GZ_MAX_LIT_CODES 288
#define GZ_MAX_DIST      30
#define GZ_FLAG_HCRC     0x02
#define GZ_FLAG_EXTRA    0x04
#define GZ_FLAG_NAME     0x08
#define GZ_FLAG_COMMENT  0x10
#define GZ_FLAG_RESERVED 0xE0

typedef struct
{
    const unsigned char *in;
    size_t in_len;
    size_t in_pos;
    uint32_t bit_buf;
    int bit_cnt;
    unsigned char *out;
    size_t out_cap;
    size_t out_pos;
} GzState;

typedef struct
{
    short count[GZ_MAX_BITS + 1]; /* number of codes of each length */
    short symbol[GZ_MAX_LIT_CODES]; /* symbols ordered by code */
} GzHuff;

/* Read n (0..16) bits, least significant first. Returns -1 at end of input. */
static int gz_bits(GzState *s, int n)
{
    uint32_t val = s->bit_buf;
    while (s->bit_cnt < n)
    {
        if (s->in_pos >= s->in_len)
        {
            return -1;
        }
        val |= (uint32_t)s->in[s->in_pos++] << s->bit_cnt;
        s->bit_cnt += 8;
    }
    s->bit_buf = val >> n;
    s->bit_cnt -= n;
    return (int)(val & ((1UL << n) - 1UL));
}

/* Build a decoding table from n code lengths. Returns 0 on success, negative
 * for an over-subscribed set, positive for an incomplete set. */
static int gz_construct(GzHuff *h, const short *length, int n)
{
    short offs[GZ_MAX_BITS + 1];
    int left = 1;

    for (int len = 0; len <= GZ_MAX_BITS; len++)
    {
        h->count[len] = 0;
    }
    for (int sym = 0; sym < n; sym++)
    {
        h->count[length[sym]]++;
    }
    if (h->count[0] == n)
    {
        return 0; /* no codes: complete, decoding any symbol will fail */
    }
    for (int len = 1; len <= GZ_MAX_BITS; len++)
    {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
        {
            return left;
        }
    }
    offs[1] = 0;
    for (int len = 1; len < GZ_MAX_BITS; len++)
    {
        offs[len + 1] = (short)(offs[len] + h->count[len]);
    }
    for (int sym = 0; sym < n; sym++)
    {
        if (length[sym] != 0)
        {
            h->symbol[offs[length[sym]]++] = (short)sym;
        }
    }
    return left;
}

/* Decode one symbol. Returns the symbol, or -1 on bad code / end of input. */
static int gz_decode(GzState *s, const GzHuff *h)
{
    int code = 0;
    int first = 0;
    int index = 0;

    for (int len = 1; len <= GZ_MAX_BITS; len++)
    {
        int bit = gz_bits(s, 1);
        if (bit < 0)
        {
            return -1;
        }
        code |= bit;
        int count = h->count[len];
        if (code - count < first)
        {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const short gz_len_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,
                                      15, 17, 19, 23, 27, 31, 35, 43, 51,  59,
                                      67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short gz_len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                       2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const short gz_dist_base[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                       33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                       1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const short gz_dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                        6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/* Decode literal/length + distance codes until end-of-block. 0 = ok. */
static int gz_codes(GzState *s, const GzHuff *lencode, const GzHuff *distcode)
{
    for (;;)
    {
        int sym = gz_decode(s, lencode);
        if (sym < 0)
        {
            return -1;
        }
        if (sym < 256)
        {
            if (s->out_pos >= s->out_cap)
            {
                return -1;
            }
            s->out[s->out_pos++] = (unsigned char)sym;
        }
        else if (sym == 256)
        {
            return 0;
        }
        else
        {
            sym -= 257;
            if (sym >= 29)
            {
                return -1;
            }
            int extra = gz_bits(s, gz_len_extra[sym]);
            if (extra < 0)
            {
                return -1;
            }
            size_t len = (size_t)(gz_len_base[sym] + extra);

            int dsym = gz_decode(s, distcode);
            if (dsym < 0 || dsym >= GZ_MAX_DIST)
            {
                return -1;
            }
            extra = gz_bits(s, gz_dist_extra[dsym]);
            if (extra < 0)
            {
                return -1;
            }
            size_t dist = (size_t)(gz_dist_base[dsym] + extra);

            if (dist > s->out_pos || len > s->out_cap - s->out_pos)
            {
                return -1;
            }
            for (size_t i = 0; i < len; i++)
            {
                s->out[s->out_pos] = s->out[s->out_pos - dist];
                s->out_pos++;
            }
        }
    }
}

static int gz_stored(GzState *s)
{
    s->bit_buf = 0; /* discard the rest of the current byte */
    s->bit_cnt = 0;
    if (s->in_len - s->in_pos < 4)
    {
        return -1;
    }
    unsigned len = s->in[s->in_pos] | ((unsigned)s->in[s->in_pos + 1] << 8);
    unsigned nlen = s->in[s->in_pos + 2] | ((unsigned)s->in[s->in_pos + 3] << 8);
    s->in_pos += 4;
    if (len != (~nlen & 0xFFFFU) || len > s->in_len - s->in_pos || len > s->out_cap - s->out_pos)
    {
        return -1;
    }
    memcpy(s->out + s->out_pos, s->in + s->in_pos, len);
    s->in_pos += len;
    s->out_pos += len;
    return 0;
}

static int gz_fixed(GzState *s)
{
    static GzHuff lencode;
    static GzHuff distcode;
    short lengths[GZ_MAX_LIT_CODES];
    int sym = 0;

    for (; sym < 144; sym++)
    {
        lengths[sym] = 8;
    }
    for (; sym < 256; sym++)
    {
        lengths[sym] = 9;
    }
    for (; sym < 280; sym++)
    {
        lengths[sym] = 7;
    }
    for (; sym < GZ_MAX_LIT_CODES; sym++)
    {
        lengths[sym] = 8;
    }
    gz_construct(&lencode, lengths, GZ_MAX_LIT_CODES);
    for (sym = 0; sym < GZ_MAX_DIST; sym++)
    {
        lengths[sym] = 5;
    }
    gz_construct(&distcode, lengths, GZ_MAX_DIST);
    return gz_codes(s, &lencode, &distcode);
}

static int gz_dynamic(GzState *s)
{
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[GZ_MAX_LIT_CODES + GZ_MAX_DIST];
    GzHuff lencode;
    GzHuff distcode;

    int nlen = gz_bits(s, 5);
    int ndist = gz_bits(s, 5);
    int ncode = gz_bits(s, 4);
    if (nlen < 0 || ndist < 0 || ncode < 0)
    {
        return -1;
    }
    nlen += 257;
    ndist += 1;
    ncode += 4;
    if (nlen > 286 || ndist > GZ_MAX_DIST)
    {
        return -1;
    }

    int idx = 0;
    for (; idx < ncode; idx++)
    {
        int v = gz_bits(s, 3);
        if (v < 0)
        {
            return -1;
        }
        lengths[order[idx]] = (short)v;
    }
    for (; idx < 19; idx++)
    {
        lengths[order[idx]] = 0;
    }
    if (gz_construct(&lencode, lengths, 19) != 0)
    {
        return -1; /* the code-length code must be complete */
    }

    idx = 0;
    while (idx < nlen + ndist)
    {
        int sym = gz_decode(s, &lencode);
        if (sym < 0)
        {
            return -1;
        }
        if (sym < 16)
        {
            lengths[idx++] = (short)sym;
            continue;
        }
        int prev = 0;
        int rep;
        if (sym == 16)
        {
            if (idx == 0)
            {
                return -1;
            }
            prev = lengths[idx - 1];
            rep = gz_bits(s, 2);
            rep += 3;
        }
        else if (sym == 17)
        {
            rep = gz_bits(s, 3);
            rep += 3;
        }
        else
        {
            rep = gz_bits(s, 7);
            rep += 11;
        }
        if (rep < 3 || idx + rep > nlen + ndist)
        {
            return -1;
        }
        while (rep-- > 0)
        {
            lengths[idx++] = (short)prev;
        }
    }
    if (lengths[256] == 0)
    {
        return -1; /* no end-of-block code */
    }

    int err = gz_construct(&lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1))
    {
        return -1;
    }
    err = gz_construct(&distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1))
    {
        return -1;
    }
    return gz_codes(s, &lencode, &distcode);
}

static uint32_t gz_crc32(const unsigned char *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFUL;
    while (n-- > 0)
    {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
        {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

static uint32_t gz_le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Skip the gzip header. Returns the offset of the DEFLATE data, or 0 if bad. */
static size_t gz_header(const unsigned char *in, size_t in_len)
{
    if (in_len < 18 || in[0] != 0x1F || in[1] != 0x8B || in[2] != 8 || (in[3] & GZ_FLAG_RESERVED))
    {
        return 0;
    }
    size_t pos = 10;
    unsigned flags = in[3];

    if (flags & GZ_FLAG_EXTRA)
    {
        if (pos + 2 > in_len)
        {
            return 0;
        }
        pos += 2 + (size_t)(in[pos] | ((unsigned)in[pos + 1] << 8));
    }
    if (flags & GZ_FLAG_NAME)
    {
        while (pos < in_len && in[pos] != 0)
        {
            pos++;
        }
        pos++;
    }
    if (flags & GZ_FLAG_COMMENT)
    {
        while (pos < in_len && in[pos] != 0)
        {
            pos++;
        }
        pos++;
    }
    if (flags & GZ_FLAG_HCRC)
    {
        pos += 2;
    }
    if (pos + 8 > in_len)
    {
        return 0;
    }
    return pos;
}

unsigned char *nd_gunzip(const unsigned char *in, size_t in_len, size_t *out_len)
{
    if (out_len)
    {
        *out_len = 0;
    }
    if (!in)
    {
        return NULL;
    }
    size_t start = gz_header(in, in_len);
    if (start == 0)
    {
        return NULL;
    }

    /* The trailer holds CRC-32 and the uncompressed size modulo 2^32. */
    uint32_t want_crc = gz_le32(in + in_len - 8);
    uint32_t want_size = gz_le32(in + in_len - 4);
    if (want_size > GZ_MAX_OUT)
    {
        return NULL;
    }

    GzState s;
    memset(&s, 0, sizeof(s));
    s.in = in;
    s.in_len = in_len - 8;
    s.in_pos = start;
    s.out_cap = want_size;
    s.out = (unsigned char *)malloc((size_t)want_size + 1);
    if (!s.out)
    {
        return NULL;
    }

    int last;
    int err = 0;
    do
    {
        last = gz_bits(&s, 1);
        int type = gz_bits(&s, 2);
        if (last < 0 || type < 0)
        {
            err = -1;
            break;
        }
        switch (type)
        {
        case 0:
            err = gz_stored(&s);
            break;
        case 1:
            err = gz_fixed(&s);
            break;
        case 2:
            err = gz_dynamic(&s);
            break;
        default:
            err = -1;
            break;
        }
    } while (!last && !err);

    if (err || s.out_pos != want_size || gz_crc32(s.out, s.out_pos) != want_crc)
    {
        free(s.out);
        return NULL;
    }
    s.out[s.out_pos] = '\0';
    if (out_len)
    {
        *out_len = s.out_pos;
    }
    return s.out;
}
