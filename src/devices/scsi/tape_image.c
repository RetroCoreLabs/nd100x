/*
 * nd100x - ND-100 emulator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * tape_image.c - a tape held in a SIMH .tap image file
 *
 * Port of MAME src/lib/util/simh_tape_file.cpp; see tape_image.h for the
 * format and for what differs from MAME and why.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../devices_types.h"
#include "../devices_protos.h"


/* SIMH markers (MAME simh_marker). */
#define SIMH_TAPE_MARK 0x00000000u
#define SIMH_ERASE_GAP 0xfffffffeu
#define SIMH_EOM       0xffffffffu

/* Marker classes, the top four bits (MAME simh_marker_class). */
#define SIMH_CLASS_GOOD_DATA       0x0
#define SIMH_CLASS_PRIVATE_MARKER  0x7
#define SIMH_CLASS_RESERVED_MARKER 0xf


/* An erase gap marker half overwritten, met while reading forward. */
static bool simh_is_half_gap_forward(uint32_t marker)
{
    return marker == 0xfffeffffu;
}


/* The same met while reading in reverse. */
static bool simh_is_half_gap_reverse(uint32_t marker)
{
    return marker >= 0xffff0000u && marker <= 0xfffffffdu;
}


/* Markers that end the recorded data, reading forward. */
static bool simh_is_eod_forward(uint32_t marker)
{
    return marker == SIMH_ERASE_GAP || simh_is_half_gap_forward(marker) || marker == SIMH_EOM;
}


/* Markers that end the recorded data, reading in reverse. */
static bool simh_is_eod_reverse(uint32_t marker)
{
    return marker == SIMH_ERASE_GAP || simh_is_half_gap_reverse(marker) || marker == SIMH_EOM;
}


/* Whole length of a record whose header word is marker: header, data, the
 * pad byte for an odd length, trailer. */
static uint64_t simh_record_bytes(uint32_t marker)
{
    uint32_t block_len = marker & 0x0fffffffu;
    return 4u + (uint64_t)block_len + (block_len % 2u) + 4u;
}


/* ------------------------------------------------------------------ */
/* Byte access through the block callbacks                             */
/* ------------------------------------------------------------------ */

bool tape_image_flush(TapeImage *ti)
{
    if (!ti->cache_dirty || ti->cache_block < 0)
    {
        return true;
    }

    if (!ti->owner || !ti->owner->blockCallbacks.writeFunc)
    {
        ti->io_error = true;
        return false;
    }

    int rc = ti->owner->blockCallbacks.writeFunc(ti->owner, ti->cache, 1,
                                                 (uint32_t)ti->cache_block, ti->unit);
    if (rc != 1)
    {
        LOG(LOG_CAT_SCSI, LOG_ERROR, "TAPE%d: image write of block %lld failed (rc=%d)\n",
            ti->unit, (long long)ti->cache_block, rc);
        ti->io_error = true;
        return false;
    }

    ti->cache_dirty = false;
    return true;
}


void tape_image_drop_cache(TapeImage *ti)
{
    tape_image_flush(ti);
    ti->cache_block = -1;
    ti->cache_dirty = false;
}


/* Make the image block holding byte offset pos the cached one. */
static bool tape_image_load(TapeImage *ti, uint64_t pos)
{
    int64_t block = (int64_t)(pos / TAPE_IMAGE_BLOCK_BYTES);
    if (block == ti->cache_block)
    {
        return true;
    }

    if (!tape_image_flush(ti))
    {
        return false;
    }

    if (!ti->owner || !ti->owner->blockCallbacks.readFunc)
    {
        ti->io_error = true;
        return false;
    }

    int rc =
        ti->owner->blockCallbacks.readFunc(ti->owner, ti->cache, 1, (uint32_t)block, ti->unit);
    if (rc != 1)
    {
        LOG(LOG_CAT_SCSI, LOG_ERROR, "TAPE%d: image read of block %lld failed (rc=%d)\n", ti->unit,
            (long long)block, rc);
        ti->cache_block = -1;
        ti->io_error = true;
        return false;
    }

    ti->cache_block = block;
    return true;
}


static bool tape_image_read_bytes(TapeImage *ti, uint64_t pos, uint8_t *buf, uint32_t len)
{
    while (len > 0)
    {
        if (!tape_image_load(ti, pos))
        {
            return false;
        }
        uint32_t at = (uint32_t)(pos % TAPE_IMAGE_BLOCK_BYTES);
        uint32_t n = TAPE_IMAGE_BLOCK_BYTES - at;
        if (n > len)
        {
            n = len;
        }
        memcpy(buf, &ti->cache[at], n);
        buf += n;
        pos += n;
        len -= n;
    }
    return true;
}


static bool tape_image_write_bytes(TapeImage *ti, uint64_t pos, const uint8_t *buf, uint32_t len)
{
    while (len > 0)
    {
        if (!tape_image_load(ti, pos))
        {
            return false;
        }
        uint32_t at = (uint32_t)(pos % TAPE_IMAGE_BLOCK_BYTES);
        uint32_t n = TAPE_IMAGE_BLOCK_BYTES - at;
        if (n > len)
        {
            n = len;
        }
        memcpy(&ti->cache[at], buf, n);
        ti->cache_dirty = true;
        buf += n;
        pos += n;
        len -= n;
    }
    return true;
}


static bool tape_image_write_repeat(TapeImage *ti, uint64_t pos, uint8_t value, uint64_t len)
{
    while (len > 0)
    {
        if (!tape_image_load(ti, pos))
        {
            return false;
        }
        uint32_t at = (uint32_t)(pos % TAPE_IMAGE_BLOCK_BYTES);
        uint64_t n = TAPE_IMAGE_BLOCK_BYTES - at;
        if (n > len)
        {
            n = len;
        }
        memset(&ti->cache[at], value, (size_t)n);
        ti->cache_dirty = true;
        pos += n;
        len -= n;
    }
    return true;
}


/* Little-endian 32-bit word at pos. */
static bool tape_image_read_word(TapeImage *ti, uint64_t pos, uint32_t *word)
{
    uint8_t b[4];
    if (!tape_image_read_bytes(ti, pos, b, 4))
    {
        return false;
    }
    *word = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}


static bool tape_image_write_word(TapeImage *ti, uint64_t pos, uint32_t word)
{
    uint8_t b[4] = {(uint8_t)word, (uint8_t)(word >> 8), (uint8_t)(word >> 16),
                    (uint8_t)(word >> 24)};
    return tape_image_write_bytes(ti, pos, b, 4);
}


/* ------------------------------------------------------------------ */
/* Open and position-only queries                                      */
/* ------------------------------------------------------------------ */

bool tape_image_open(TapeImage *ti, struct Device *owner, int unit)
{
    memset(ti, 0, sizeof(*ti));
    ti->owner = owner;
    ti->unit = unit;
    ti->cache_block = -1;

    if (!owner || !owner->blockCallbacks.diskInfoFunc)
    {
        return false;
    }

    size_t image_size = 0;
    bool wp = false;
    if (owner->blockCallbacks.diskInfoFunc(owner, &image_size, &wp, unit) < 0)
    {
        return false;
    }

    /* Only whole image blocks are used: a byte past the last whole block
     * would need a read-modify-write of a block the image does not fully
     * hold, and storing that block would make the file longer. */
    uint64_t usable = (uint64_t)image_size - ((uint64_t)image_size % TAPE_IMAGE_BLOCK_BYTES);
    if (usable != (uint64_t)image_size)
    {
        LOG(LOG_CAT_SCSI, LOG_WARN,
            "TAPE%d: image is %zu bytes, not a multiple of %d; the last %zu bytes are not used\n",
            unit, image_size, TAPE_IMAGE_BLOCK_BYTES, (size_t)(image_size - usable));
    }
    if (usable < TAPE_IMAGE_BLOCK_BYTES)
    {
        return false;
    }

    ti->size = usable;
    ti->read_only = wp;
    return true;
}


bool tape_image_is_ew(const TapeImage *ti)
{
    return ti->pos + TAPE_IMAGE_EARLY_WARNING_BYTES >= ti->size;
}


bool tape_image_is_bot(const TapeImage *ti)
{
    return ti->pos == 0;
}


TapeStatus tape_image_at_eod(TapeImage *ti, bool *at_eod)
{
    *at_eod = false;
    if (ti->pos + 4 > ti->size)
    {
        *at_eod = true;
        return TAPE_STATUS_OK;
    }

    uint32_t marker;
    if (!tape_image_read_word(ti, ti->pos, &marker))
    {
        return TAPE_STATUS_ERROR;
    }
    *at_eod = simh_is_eod_forward(marker);
    return TAPE_STATUS_OK;
}


void tape_image_rewind(TapeImage *ti, bool eom)
{
    ti->pos = eom ? ti->size : 0;
}


/*
 * Records between the start and the position. MAME read_position counts data
 * blocks only; here a filemark counts as a record too. ASSUMPTION, not taken
 * from the drive: on a QIC cartridge a filemark is written as a block of its
 * own (TDC 3600 manual chapter 6, "File Mark Blocks"), and the drive's block
 * address is a count of the blocks on the tape, so a filemark is taken to
 * have an address of its own. The manual does not say so in words.
 */
TapeStatus tape_image_block_address(const TapeImage *ti, uint32_t *count)
{
    /* The scan moves only a local position; the cache is the only state it
     * changes. */
    TapeImage *t = (TapeImage *)ti;
    uint32_t records = 0;
    uint64_t at = 0;

    while (at < ti->pos)
    {
        uint32_t marker;
        if (at + 4 > ti->size || !tape_image_read_word(t, at, &marker))
        {
            return TAPE_STATUS_ERROR;
        }
        if (marker == SIMH_TAPE_MARK)
        {
            at += 4;
            records++;
            continue;
        }
        if (simh_is_eod_forward(marker))
        {
            /* The position is past the end of recorded data. */
            return TAPE_STATUS_ERROR;
        }

        uint32_t cls = marker >> 28;
        if (cls == SIMH_CLASS_PRIVATE_MARKER || cls == SIMH_CLASS_RESERVED_MARKER)
        {
            at += 4;
            continue;
        }

        uint64_t rec = simh_record_bytes(marker);
        if (at + rec > ti->size)
        {
            return TAPE_STATUS_ERROR;
        }
        at += rec;
        if (cls == SIMH_CLASS_GOOD_DATA)
        {
            records++;
        }
    }

    if (at != ti->pos)
    {
        /* Inside a record. */
        return TAPE_STATUS_ERROR;
    }

    *count = records;
    return TAPE_STATUS_OK;
}


/* ------------------------------------------------------------------ */
/* Non-destructive motion                                              */
/* ------------------------------------------------------------------ */

TapeStatus tape_image_locate(TapeImage *ti, uint32_t index)
{
    uint32_t records = 0;
    ti->pos = 0;

    while (ti->pos < ti->size)
    {
        if (records == index)
        {
            return TAPE_STATUS_OK;
        }

        uint32_t marker;
        if (ti->pos + 4 > ti->size || !tape_image_read_word(ti, ti->pos, &marker))
        {
            return TAPE_STATUS_ERROR;
        }
        if (marker == SIMH_TAPE_MARK)
        {
            ti->pos += 4;
            records++;
            continue;
        }
        if (simh_is_eod_forward(marker))
        {
            return tape_image_is_ew(ti) ? TAPE_STATUS_EOD_EW : TAPE_STATUS_EOD;
        }

        uint32_t cls = marker >> 28;
        if (cls == SIMH_CLASS_PRIVATE_MARKER || cls == SIMH_CLASS_RESERVED_MARKER)
        {
            ti->pos += 4;
            continue;
        }

        uint64_t rec = simh_record_bytes(marker);
        if (ti->pos + rec > ti->size)
        {
            return TAPE_STATUS_ERROR;
        }
        ti->pos += rec;
        if (cls == SIMH_CLASS_GOOD_DATA)
        {
            records++;
        }
    }

    return TAPE_STATUS_EOM;
}


TapeStatus tape_image_space_eod(TapeImage *ti)
{
    while (ti->pos < ti->size)
    {
        uint32_t marker;
        if (ti->pos + 4 > ti->size || !tape_image_read_word(ti, ti->pos, &marker))
        {
            return TAPE_STATUS_ERROR;
        }
        if (marker == SIMH_TAPE_MARK)
        {
            ti->pos += 4;
            continue;
        }
        if (simh_is_eod_forward(marker))
        {
            return TAPE_STATUS_OK;
        }

        uint32_t cls = marker >> 28;
        if (cls == SIMH_CLASS_PRIVATE_MARKER || cls == SIMH_CLASS_RESERVED_MARKER)
        {
            ti->pos += 4;
            continue;
        }

        uint64_t rec = simh_record_bytes(marker);
        if (ti->pos + rec > ti->size)
        {
            return TAPE_STATUS_ERROR;
        }
        ti->pos += rec;
    }

    return TAPE_STATUS_EOM;
}


TapeStatus tape_image_space_blocks(TapeImage *ti, uint32_t count, uint32_t *done)
{
    *done = 0;
    while (ti->pos < ti->size)
    {
        uint32_t marker;
        if (ti->pos + 4 > ti->size || !tape_image_read_word(ti, ti->pos, &marker))
        {
            return TAPE_STATUS_ERROR;
        }
        if (marker == SIMH_TAPE_MARK)
        {
            ti->pos += 4;
            return tape_image_is_ew(ti) ? TAPE_STATUS_FILEMARK_EW : TAPE_STATUS_FILEMARK;
        }
        if (simh_is_eod_forward(marker))
        {
            return tape_image_is_ew(ti) ? TAPE_STATUS_EOD_EW : TAPE_STATUS_EOD;
        }

        uint32_t cls = marker >> 28;
        if (cls == SIMH_CLASS_PRIVATE_MARKER || cls == SIMH_CLASS_RESERVED_MARKER)
        {
            ti->pos += 4;
            continue;
        }

        uint64_t rec = simh_record_bytes(marker);
        if (ti->pos + rec > ti->size)
        {
            return TAPE_STATUS_ERROR;
        }
        ti->pos += rec;
        if (cls == SIMH_CLASS_GOOD_DATA)
        {
            (*done)++;
            if (*done == count)
            {
                return TAPE_STATUS_OK;
            }
        }
    }

    return TAPE_STATUS_EOM;
}


TapeStatus tape_image_space_blocks_reverse(TapeImage *ti, uint32_t count, uint32_t *done)
{
    *done = 0;
    while (ti->pos > 0)
    {
        uint32_t marker;
        if (ti->pos < 4 || !tape_image_read_word(ti, ti->pos - 4, &marker))
        {
            return TAPE_STATUS_ERROR;
        }
        if (marker == SIMH_TAPE_MARK)
        {
            ti->pos -= 4;
            return tape_image_is_ew(ti) ? TAPE_STATUS_FILEMARK_EW : TAPE_STATUS_FILEMARK;
        }
        if (simh_is_eod_reverse(marker))
        {
            return tape_image_is_ew(ti) ? TAPE_STATUS_EOD_EW : TAPE_STATUS_EOD;
        }

        uint32_t cls = marker >> 28;
        if (cls == SIMH_CLASS_PRIVATE_MARKER || cls == SIMH_CLASS_RESERVED_MARKER)
        {
            ti->pos -= 4;
            continue;
        }

        uint64_t rec = simh_record_bytes(marker);
        if (ti->pos < rec)
        {
            return TAPE_STATUS_ERROR;
        }
        ti->pos -= rec;
        if (cls == SIMH_CLASS_GOOD_DATA)
        {
            (*done)++;
            if (*done == count)
            {
                return TAPE_STATUS_OK;
            }
        }
    }

    return TAPE_STATUS_BOM;
}


/*
 * Sequential filemarks (SCSI Space code 2) is not in MAME (it asserts
 * !sequential). Here: count filemarks that follow one another with no data
 * block between them, start again from zero at each data block, and stop
 * after the count-th one in a row.
 */
TapeStatus tape_image_space_filemarks(TapeImage *ti, uint32_t count, bool sequential,
                                      uint32_t *done)
{
    *done = 0;
    while (ti->pos < ti->size)
    {
        uint32_t marker;
        if (ti->pos + 4 > ti->size || !tape_image_read_word(ti, ti->pos, &marker))
        {
            return TAPE_STATUS_ERROR;
        }
        if (marker == SIMH_TAPE_MARK)
        {
            ti->pos += 4;
            (*done)++;
            if (*done == count)
            {
                return TAPE_STATUS_OK;
            }
            continue;
        }
        if (simh_is_eod_forward(marker))
        {
            return tape_image_is_ew(ti) ? TAPE_STATUS_EOD_EW : TAPE_STATUS_EOD;
        }

        uint32_t cls = marker >> 28;
        if (cls == SIMH_CLASS_PRIVATE_MARKER || cls == SIMH_CLASS_RESERVED_MARKER)
        {
            ti->pos += 4;
            continue;
        }

        uint64_t rec = simh_record_bytes(marker);
        if (ti->pos + rec > ti->size)
        {
            return TAPE_STATUS_ERROR;
        }
        ti->pos += rec;
        if (sequential)
        {
            *done = 0;
        }
    }

    return TAPE_STATUS_EOM;
}


TapeStatus tape_image_space_filemarks_reverse(TapeImage *ti, uint32_t count, uint32_t *done)
{
    *done = 0;
    while (ti->pos > 0)
    {
        uint32_t marker;
        if (ti->pos < 4 || !tape_image_read_word(ti, ti->pos - 4, &marker))
        {
            return TAPE_STATUS_ERROR;
        }
        if (marker == SIMH_TAPE_MARK)
        {
            ti->pos -= 4;
            (*done)++;
            if (*done == count)
            {
                return TAPE_STATUS_OK;
            }
            continue;
        }
        if (simh_is_eod_reverse(marker))
        {
            return tape_image_is_ew(ti) ? TAPE_STATUS_EOD_EW : TAPE_STATUS_EOD;
        }

        uint32_t cls = marker >> 28;
        if (cls == SIMH_CLASS_PRIVATE_MARKER || cls == SIMH_CLASS_RESERVED_MARKER)
        {
            ti->pos -= 4;
            continue;
        }

        uint64_t rec = simh_record_bytes(marker);
        if (ti->pos < rec)
        {
            return TAPE_STATUS_ERROR;
        }
        ti->pos -= rec;
    }

    return TAPE_STATUS_BOM;
}


TapeStatus tape_image_read_block(TapeImage *ti, uint8_t *buf, uint32_t buf_size, uint32_t *len)
{
    *len = 0;
    while (ti->pos < ti->size)
    {
        uint32_t marker;
        if (ti->pos + 4 > ti->size || !tape_image_read_word(ti, ti->pos, &marker))
        {
            return TAPE_STATUS_ERROR;
        }
        if (marker == SIMH_TAPE_MARK)
        {
            ti->pos += 4;
            return tape_image_is_ew(ti) ? TAPE_STATUS_FILEMARK_EW : TAPE_STATUS_FILEMARK;
        }
        if (simh_is_eod_forward(marker))
        {
            return tape_image_is_ew(ti) ? TAPE_STATUS_EOD_EW : TAPE_STATUS_EOD;
        }

        uint32_t cls = marker >> 28;
        if (cls == SIMH_CLASS_PRIVATE_MARKER || cls == SIMH_CLASS_RESERVED_MARKER)
        {
            ti->pos += 4;
            continue;
        }

        uint64_t rec = simh_record_bytes(marker);
        if (ti->pos + rec > ti->size)
        {
            return TAPE_STATUS_ERROR;
        }
        if (cls == SIMH_CLASS_GOOD_DATA)
        {
            uint32_t block_len = marker & 0x0fffffffu;
            if (block_len > buf_size)
            {
                LOG(LOG_CAT_SCSI, LOG_ERROR,
                    "TAPE%d: block of %u bytes at offset %llu is longer than %u\n", ti->unit,
                    block_len, (unsigned long long)ti->pos, buf_size);
                return TAPE_STATUS_ERROR;
            }
            if (!tape_image_read_bytes(ti, ti->pos + 4, buf, block_len))
            {
                return TAPE_STATUS_ERROR;
            }
            ti->pos += rec;
            *len = block_len;
            return TAPE_STATUS_OK;
        }
        ti->pos += rec;
    }

    return TAPE_STATUS_EOM;
}


/* ------------------------------------------------------------------ */
/* Destructive operations                                              */
/* ------------------------------------------------------------------ */

TapeStatus tape_image_erase(TapeImage *ti)
{
    if (ti->read_only)
    {
        return TAPE_STATUS_ERROR;
    }
    if (!tape_image_write_repeat(ti, ti->pos, 0xff, ti->size - ti->pos) || !tape_image_flush(ti))
    {
        return TAPE_STATUS_ERROR;
    }
    return TAPE_STATUS_OK;
}


/* The end of recorded data after a write: an FFFFFFFF word, when it fits. */
static bool tape_image_mark_end(TapeImage *ti)
{
    if (ti->pos + 4 > ti->size)
    {
        return true;
    }
    return tape_image_write_word(ti, ti->pos, SIMH_EOM);
}


TapeStatus tape_image_write_block(TapeImage *ti, const uint8_t *buf, uint32_t len)
{
    if (ti->read_only)
    {
        return TAPE_STATUS_ERROR;
    }

    uint32_t pad = len % 2u;
    uint64_t rec = 4u + (uint64_t)len + pad + 4u;
    if (ti->pos + rec >= ti->size)
    {
        return TAPE_STATUS_EOM;
    }

    static const uint8_t zero = 0;
    if (!tape_image_write_word(ti, ti->pos, len) ||
        !tape_image_write_bytes(ti, ti->pos + 4, buf, len) ||
        (pad && !tape_image_write_bytes(ti, ti->pos + 4 + len, &zero, 1)) ||
        !tape_image_write_word(ti, ti->pos + 4 + len + pad, len))
    {
        return TAPE_STATUS_ERROR;
    }
    ti->pos += rec;

    if (!tape_image_mark_end(ti))
    {
        return TAPE_STATUS_ERROR;
    }
    return tape_image_is_ew(ti) ? TAPE_STATUS_EW : TAPE_STATUS_OK;
}


TapeStatus tape_image_write_filemarks(TapeImage *ti, uint32_t count)
{
    if (ti->read_only)
    {
        return TAPE_STATUS_ERROR;
    }

    uint64_t len = (uint64_t)count * 4u;
    if (ti->pos + len >= ti->size)
    {
        return TAPE_STATUS_EOM;
    }

    for (uint32_t i = 0; i < count; i++)
    {
        if (!tape_image_write_word(ti, ti->pos, SIMH_TAPE_MARK))
        {
            return TAPE_STATUS_ERROR;
        }
        ti->pos += 4;
    }

    if (!tape_image_mark_end(ti))
    {
        return TAPE_STATUS_ERROR;
    }
    return tape_image_is_ew(ti) ? TAPE_STATUS_EW : TAPE_STATUS_OK;
}
