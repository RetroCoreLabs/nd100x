/*
 * nd100x - ND-100 emulator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * tape_image.h - a tape held in a SIMH .tap image file
 *
 * Ported from MAME src/lib/util/simh_tape_file.cpp (BSD-3-Clause, Mietek Bak),
 * which follows the SIMH magtape format (simh_magtape.pdf, rev 17 Jan 2022).
 *
 * The image file is the whole tape, start to physical end:
 *   - a data record is a 4-byte little-endian length, the data, one pad byte
 *     when the length is odd, then the same 4-byte length again;
 *   - a 4-byte 00000000 is a tape mark (filemark);
 *   - FFFFFFFF is the end of recorded data, FFFFFFFE an erase gap.
 * A blank tape is a file full of FF bytes, so its first word already says
 * "end of recorded data". The length of the file is the length of the tape:
 * a write that would run past it reports physical end of tape.
 *
 * Differences from the MAME class, each one deliberate:
 *   - Bytes are reached through the owning Device's block callbacks
 *     (machine_block_read / machine_block_write, 1024-byte blocks), never
 *     through a FILE*, so the same code works for a local file, OPFS in the
 *     browser and a remote image. Byte access is a read-modify-write of the
 *     1024-byte block that holds the byte (one block is cached).
 *   - MAME throws on a broken image; here every operation returns
 *     TAPE_STATUS_ERROR and the caller reports a medium error.
 *   - write_block and write_filemarks also write an FFFFFFFF marker after
 *     what they wrote, when there is room. On a streamer the end of the last
 *     write IS the end of recorded data: whatever an older recording left
 *     further down the tape cannot be read past it. MAME leaves the old bytes
 *     in place, so rewriting a used tape from the start would let a later read
 *     run on into the old recording.
 *   - Blocks are counted with filemarks included where a block address is
 *     asked for; see tape_image_block_address.
 */

#ifndef TAPE_IMAGE_H
#define TAPE_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct Device;

/* What a tape operation ran into (MAME tape_file_interface.h tape_status,
 * minus the setmark states SIMH does not define). */
// clang-format off
typedef enum {
    TAPE_STATUS_OK = 0,      /* operation done */
    TAPE_STATUS_BOM,         /* beginning of medium reached (reverse motion) */
    TAPE_STATUS_EW,          /* done, but now past the early warning point */
    TAPE_STATUS_FILEMARK,    /* stopped after a filemark */
    TAPE_STATUS_FILEMARK_EW, /* the same, past early warning */
    TAPE_STATUS_EOD,         /* reached the end of recorded data */
    TAPE_STATUS_EOD_EW,      /* the same, past early warning */
    TAPE_STATUS_EOM,         /* reached the physical end of the tape */
    TAPE_STATUS_ERROR        /* the image is broken, or the image I/O failed */
} TapeStatus;
// clang-format on

/* The image is reached in blocks of this size (the SCSI controller's
 * blockSizeBytes). */
#define TAPE_IMAGE_BLOCK_BYTES 1024

/* Early warning, as MAME: 32 KB before the physical end. MAME notes that ANSI
 * puts it 10 feet before the end whatever the density; the TDC 3600 manual
 * does not give the distance for QIC cartridges. */
#define TAPE_IMAGE_EARLY_WARNING_BYTES 32768

// clang-format off
typedef struct {
    struct Device *owner;    /* block callbacks of the controller */
    int      unit;           /* unit number for the callbacks */
    uint64_t size;           /* tape length in bytes (image length, whole blocks) */
    bool     read_only;      /* image is write protected */
    uint64_t pos;            /* tape position, a byte offset, always even */

    /* One cached 1024-byte block of the image. */
    uint8_t  cache[TAPE_IMAGE_BLOCK_BYTES];
    int64_t  cache_block;    /* block number held, -1 = none */
    bool     cache_dirty;    /* written to, not yet stored */
    bool     io_error;       /* a block callback failed */
} TapeImage;
// clang-format on

/**
 * @brief Attach a tape image: read its size and write protection through the
 *        owner's disk-info callback and put the position at the start.
 * @param ti Tape state to fill in.
 * @param owner Device whose block callbacks reach the image.
 * @param unit Unit number passed to the callbacks.
 * @return true if an image is mounted on the unit and is at least one block
 *         long, false if there is no tape.
 */
bool tape_image_open(TapeImage *ti, struct Device *owner, int unit);

/**
 * @brief Write the cached block back to the image if it was changed.
 * @param ti Tape state.
 * @return true on success.
 */
bool tape_image_flush(TapeImage *ti);

/**
 * @brief Forget the cached block, so the next access reads the image again.
 * @param ti Tape state.
 */
void tape_image_drop_cache(TapeImage *ti);

/**
 * @brief Is the position past the early warning point?
 * @param ti Tape state.
 * @return true when fewer than TAPE_IMAGE_EARLY_WARNING_BYTES remain.
 */
bool tape_image_is_ew(const TapeImage *ti);

/**
 * @brief Is the position at the beginning of the tape?
 * @param ti Tape state.
 * @return true at byte 0.
 */
bool tape_image_is_bot(const TapeImage *ti);

/**
 * @brief Is the position at the end of recorded data - the next word on the
 *        tape an end-of-data marker, or the physical end reached?
 * @param ti Tape state.
 * @param at_eod Receives the answer.
 * @return TAPE_STATUS_OK, or TAPE_STATUS_ERROR if the image could not be read.
 */
TapeStatus tape_image_at_eod(TapeImage *ti, bool *at_eod);

/**
 * @brief Move to the beginning (or the physical end) of the tape.
 * @param ti Tape state.
 * @param eom true to go to the physical end instead of the beginning.
 */
void tape_image_rewind(TapeImage *ti, bool eom);

/**
 * @brief Count the blocks and filemarks between the beginning and the
 *        position (MAME read_position, with filemarks counted).
 * @param ti Tape state.
 * @param count Receives the count.
 * @return TAPE_STATUS_OK when the position is on a record boundary, else
 *         TAPE_STATUS_ERROR.
 */
TapeStatus tape_image_block_address(const TapeImage *ti, uint32_t *count);

/**
 * @brief Go to the record with the given number counted from the start of the
 *        tape, filemarks included (MAME locate_block, with filemarks counted).
 * @param ti Tape state.
 * @param index Number of records (blocks and filemarks) to leave behind.
 * @return TAPE_STATUS_OK, TAPE_STATUS_EOD(_EW) if the recorded data ends
 *         first, TAPE_STATUS_EOM, or TAPE_STATUS_ERROR.
 */
TapeStatus tape_image_locate(TapeImage *ti, uint32_t index);

/**
 * @brief Go forward to the end of recorded data (MAME space_eod).
 * @param ti Tape state.
 * @return TAPE_STATUS_OK, TAPE_STATUS_EOM or TAPE_STATUS_ERROR.
 */
TapeStatus tape_image_space_eod(TapeImage *ti);

/**
 * @brief Space forward over data blocks (MAME space_blocks).
 * @param ti Tape state.
 * @param count Blocks to space over, at least 1.
 * @param done Receives the number of blocks spaced over.
 * @return TAPE_STATUS_OK, FILEMARK(_EW) (stopped after a filemark), EOD(_EW),
 *         EOM or ERROR.
 */
TapeStatus tape_image_space_blocks(TapeImage *ti, uint32_t count, uint32_t *done);

/**
 * @brief Space backward over data blocks (MAME space_blocks_reverse).
 * @param ti Tape state.
 * @param count Blocks to space over, at least 1.
 * @param done Receives the number of blocks spaced over.
 * @return TAPE_STATUS_OK, FILEMARK(_EW) (stopped before a filemark), EOD(_EW),
 *         BOM or ERROR.
 */
TapeStatus tape_image_space_blocks_reverse(TapeImage *ti, uint32_t count, uint32_t *done);

/**
 * @brief Space forward over filemarks (MAME space_filemarks).
 * @param ti Tape state.
 * @param count Filemarks to space over, at least 1.
 * @param sequential true to look for count filemarks in a row with no block
 *        between them (SCSI Space code 2).
 * @param done Receives the number of filemarks spaced over.
 * @return TAPE_STATUS_OK, EOD(_EW), EOM or ERROR.
 */
TapeStatus tape_image_space_filemarks(TapeImage *ti, uint32_t count, bool sequential,
                                      uint32_t *done);

/**
 * @brief Space backward over filemarks (MAME space_filemarks_reverse).
 * @param ti Tape state.
 * @param count Filemarks to space over, at least 1.
 * @param done Receives the number of filemarks spaced over.
 * @return TAPE_STATUS_OK, EOD(_EW), BOM or ERROR.
 */
TapeStatus tape_image_space_filemarks_reverse(TapeImage *ti, uint32_t count, uint32_t *done);

/**
 * @brief Read the next data block (MAME read_block).
 * @param ti Tape state.
 * @param buf Receives the data.
 * @param buf_size Room in buf.
 * @param len Receives the length of the block read.
 * @return TAPE_STATUS_OK, FILEMARK(_EW) (the position is then after the
 *         filemark), EOD(_EW), EOM, or ERROR (also for a block longer than
 *         buf_size).
 */
TapeStatus tape_image_read_block(TapeImage *ti, uint8_t *buf, uint32_t buf_size, uint32_t *len);

/**
 * @brief Erase from the position to the physical end (MAME erase). The
 *        position does not move; the caller rewinds if the drive does.
 * @param ti Tape state.
 * @return TAPE_STATUS_OK or TAPE_STATUS_ERROR.
 */
TapeStatus tape_image_erase(TapeImage *ti);

/**
 * @brief Write one data block at the position (MAME write_block) and mark the
 *        end of recorded data after it.
 * @param ti Tape state.
 * @param buf Block data.
 * @param len Block length in bytes.
 * @return TAPE_STATUS_OK, TAPE_STATUS_EW (written, past early warning),
 *         TAPE_STATUS_EOM (no room, nothing written) or TAPE_STATUS_ERROR.
 */
TapeStatus tape_image_write_block(TapeImage *ti, const uint8_t *buf, uint32_t len);

/**
 * @brief Write filemarks at the position (MAME write_filemarks) and mark the
 *        end of recorded data after them.
 * @param ti Tape state.
 * @param count Number of filemarks.
 * @return TAPE_STATUS_OK, TAPE_STATUS_EW, TAPE_STATUS_EOM or TAPE_STATUS_ERROR.
 */
TapeStatus tape_image_write_filemarks(TapeImage *ti, uint32_t count);

#endif // TAPE_IMAGE_H
