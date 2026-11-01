/*
 * nd100x - ND-100 emulator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * scsi_tape.c - SCSI streamer target: Tandberg TDC 3600 series
 *
 * See scsi_tape.h for the sources. "Section" and "page" below are the TDC
 * 3620/3640/3660 Reference Manual's.
 *
 * How a transfer runs: READ, WRITE, VERIFY and MODE SELECT queue only their
 * data phase when the command arrives. The STATUS phase is queued from the
 * data hooks, once the last byte has moved or when the transfer has to stop
 * early (a filemark, the end of recorded data, the end of the tape); an early
 * stop also ends the data phase (scsi_device_end_data_phase), the way the
 * drive changes phase on a real bus. Every other command queues its data
 * phase and status together, as the disk target does.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#include "../devices_types.h"
#include "../devices_protos.h"


/* Sense keys this drive uses beyond those in scsi_device.h (SCSI-2 table 69;
 * the TDC uses the same values, section 9.3.14.2). */
#define TDC_SK_VOLUME_OVERFLOW 0x0D
#define TDC_SK_MISCOMPARE      0x0E

/* Sense block byte 2 flags. */
#define TDC_SENSE_FM  0x80
#define TDC_SENSE_EOM 0x40

/*
 * Error class / error code (sense byte 14, and byte 0 of the standard sense
 * block) and extended error code EXERCD (sense byte 15). Appendix A gives
 * each EXERCD with its error code and sense key; the ones used here:
 *
 *   mnemonic  EXERCD code key   meaning
 *   E$OK       00     00  00   no error
 *   E$APPN     01     33  05   append error
 *   E$BADC     02     20  05   bad command block
 *   E$BADP     03     20  05   bad parameter block
 *   E$COMP     08     1D  0E   compare error
 *   E$FIMK     0B     1C  80   file mark (NO SENSE + FM bit)
 *   E$ILCM     0D     20  05   illegal command
 *   E$INAP     10     20  05   inappropriate request
 *   E$NCAR     12     09  02   no cartridge
 *   E$NLOD     13     09  02   not loaded
 *   E$QRND     15     14  03   QIC no data detect
 *   E$RDWR     16     34  05   read after write
 *   E$REOL     17     34  08   read EOM logical
 *   E$REOP     18     34  40   read EOM physical (NO SENSE + EOM bit)
 *   E$UATT     1C     30  06   unit attention
 *   E$WEOW     1D     0A  00   write EOM warning
 *   E$WEOM     1E     0A  00   write EOM
 *   E$WPRO     1F     17  07   cartridge write protected
 *   E$16RW     20     11  03   16 rewrite errors
 *   E$24RE     22     19  03   24 rereads, block not found
 *
 * Where a command's own page gives a different sense key, the command page is
 * followed and the call site says so.
 */
// clang-format off
#define TDC_EC_NONE          0x00
#define TDC_EC_NOT_LOADED    0x09
#define TDC_EC_CAPACITY      0x0A
#define TDC_EC_UNCORRECTABLE 0x11
#define TDC_EC_NO_RECORD     0x14
#define TDC_EC_WRITE_PROT    0x17
#define TDC_EC_BAD_BLOCK     0x19
#define TDC_EC_FILEMARK      0x1C
#define TDC_EC_COMPARE       0x1D
#define TDC_EC_INVALID_CMD   0x20
#define TDC_EC_UNIT_ATTN     0x30
#define TDC_EC_APPEND        0x33
#define TDC_EC_READ_EOM      0x34

#define TDC_X_OK    0x00
#define TDC_X_APPN  0x01
#define TDC_X_BADC  0x02
#define TDC_X_BADP  0x03
#define TDC_X_COMP  0x08
#define TDC_X_FIMK  0x0B
#define TDC_X_ILCM  0x0D
#define TDC_X_INAP  0x10
#define TDC_X_NCAR  0x12
#define TDC_X_NLOD  0x13
#define TDC_X_QRND  0x15
#define TDC_X_RDWR  0x16
#define TDC_X_REOL  0x17
#define TDC_X_REOP  0x18
#define TDC_X_UATT  0x1C
#define TDC_X_WEOW  0x1D
#define TDC_X_WEOM  0x1E
#define TDC_X_WPRO  0x1F
#define TDC_X_16RW  0x20
#define TDC_X_24RE  0x22
// clang-format on

/* Vendor unique mode parameters at power-on (section 9.3.5 defaults):
 * page code 00, write threshold 2, read threshold 2, buffer size 120 blocks
 * ("Default for a Drive with 64 KByte buffer = 120"), forced streaming count
 * 0000, bus threshold 8, copy threshold 8, normal sense threshold 0, copy
 * sense allocation 4, load function 00 (auto load), power-up delay 00. */
static const uint8_t tdc_default_vendor[TDC_MODE_VENDOR_BYTES] = {
    0x00, 0x02, 0x02, 120, 0x00, 0x00, 0x08, 0x08, 0x00, 0x04, 0x00, 0x00};

/* The target base counts a data phase in an int. A transfer of more blocks
 * than that can carry (over 4 million, 2 GB - sixteen times the 125 MB
 * cartridge) is refused with an error log; the drive itself would take the
 * count and stop at the end of the tape. */
#define TDC_MAX_PHASE_BLOCKS ((uint32_t)(0x7fffffff / TDC_BLOCK_BYTES))

/* Index of the normal sense threshold in the vendor bytes. */
#define TDC_VENDOR_NORMAL_SENSE_THRESHOLD 8


static void tdc_log(SCSITapeDevice *tape, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void tdc_log(SCSITapeDevice *tape, const char *fmt, ...)
{
    if (!Log_IsEnabled(LOG_CAT_SCSI, LOG_DEBUG))
    {
        return;
    }

    char msg[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    log_write(LOG_CAT_SCSI, LOG_DEBUG, "TAPE%d: %s", tape->unit, msg);
}


/* ------------------------------------------------------------------ */
/* Sense and status                                                    */
/* ------------------------------------------------------------------ */

/*
 * Fill the extended sense block (section 9.3.14.2):
 *   0      VADD | 70h
 *   1      segment number (Copy only) = 0
 *   2      FM | EOM | sense key
 *   3-6    information bytes (residue), MSB first
 *   7      additional sense length = 17
 *   8-9    source / destination sense pointer (Copy only) = 0
 *   10-11  reserved
 *   12-13  number of recoverable errors = 0 (no retries on an image)
 *   14     error class / error code
 *   15     EXERCD
 *   16-18  block counter
 *   19-20  filemark counter
 *   21-22  underrun counter = 0 (the image never runs dry)
 *   23     marginal counter = 0
 *   24     remaining counter = 0
 */
static void tdc_set_sense(SCSITapeDevice *tape, uint8_t key, uint8_t flags, uint8_t code,
                          uint8_t exercd, bool vadd, uint32_t info)
{
    uint8_t *s = tape->sense;
    memset(s, 0, TDC_SENSE_BYTES);
    s[0] = (uint8_t)((vadd ? 0x80 : 0x00) | 0x70);
    s[2] = (uint8_t)(flags | (key & 0x0f));
    if (vadd)
    {
        scsi_device_scsi_put_u32_be(&s[3], info);
    }
    s[7] = TDC_SENSE_ADD_LEN;
    s[14] = code;
    s[15] = exercd;
    scsi_device_scsi_put_u24_be(&s[16], tape->blockCounter);
    scsi_device_scsi_put_u16_be(&s[19], tape->filemarkCounter);
}


static void tdc_clear_sense(SCSITapeDevice *tape)
{
    tdc_set_sense(tape, SK_NO_SENSE, 0, TDC_EC_NONE, TDC_X_OK, false, 0);
}


/* The image block cache goes back to the image before any status, so a
 * command's writes are on the image when the host sees it end. */
static void tdc_status(SCSITapeDevice *tape, uint8_t status)
{
    tape_image_flush(&tape->tape);
    scsi_device_status_complete(&tape->target, status);
}


static void tdc_good(SCSITapeDevice *tape)
{
    tdc_status(tape, SS_GOOD);
}


static void tdc_check(SCSITapeDevice *tape, uint8_t key, uint8_t flags, uint8_t code,
                      uint8_t exercd, bool vadd, uint32_t info)
{
    tdc_set_sense(tape, key, flags, code, exercd, vadd, info);
    tdc_log(tape, "CHECK CONDITION key=%X fm=%d eom=%d code=%02X exercd=%02X info=%u", key,
            (flags & TDC_SENSE_FM) != 0, (flags & TDC_SENSE_EOM) != 0, code, exercd,
            vadd ? info : 0);
    tdc_status(tape, SS_CHECK_CONDITION);
}


static void tdc_illegal(SCSITapeDevice *tape, uint8_t exercd)
{
    tdc_check(tape, SK_ILLEGAL_REQUEST, 0, TDC_EC_INVALID_CMD, exercd, false, 0);
}


/* Block and filemark counters restart when the drive goes from one mode to
 * Read or Write (section 9.3.14.2). */
static void tdc_enter_mode(SCSITapeDevice *tape, TDCOperationMode mode)
{
    if (mode != tape->opMode && (mode == TDC_MODE_READ || mode == TDC_MODE_WRITE))
    {
        tape->blockCounter = 0;
        tape->filemarkCounter = 0;
    }
    tape->opMode = mode;
}


/* ------------------------------------------------------------------ */
/* Cartridge state                                                     */
/* ------------------------------------------------------------------ */

/* Back to BOT with no write pending - what a rewind, a load, an erase and a
 * Unit Attention all leave behind. */
static void tdc_to_bot(SCSITapeDevice *tape)
{
    tape_image_rewind(&tape->tape, false);
    tape->lastWasWrite = false;
    tape->pewReported = false;
    tape->opMode = TDC_MODE_POSITION;
}


/*
 * Follow the image: an image that appears is a cartridge put in (auto load,
 * the default Load Function 00, and Unit Attention: section 8.6.2 "If a
 * cartridge is then loaded, the first command sent by all Initiators will
 * cause ... UNIT ATTENTION"); one that goes away is a cartridge taken out.
 */
static void tdc_refresh_media(SCSITapeDevice *tape)
{
    if (!tape->present)
    {
        if (tape_image_open(&tape->tape, tape->owner, tape->unit))
        {
            tape->present = true;
            tape->loaded = true;
            tape->unitAttention = true;
            tape->writeError = false;
            tdc_to_bot(tape);
            tdc_log(tape, "cartridge in: %llu bytes%s", (unsigned long long)tape->tape.size,
                    tape->tape.read_only ? ", write protected" : "");
        }
        return;
    }

    size_t image_size = 0;
    bool wp = false;
    if (!tape->owner->blockCallbacks.diskInfoFunc ||
        tape->owner->blockCallbacks.diskInfoFunc(tape->owner, &image_size, &wp, tape->unit) < 0 ||
        image_size == 0)
    {
        tape->present = false;
        tape->loaded = false;
        tdc_log(tape, "cartridge out");
    }
}


/* For the commands that move or read the tape: CHECK CONDITION / NOT READY
 * when there is no cartridge or it was unloaded (section 8.6.2, Appendix A
 * E$NCAR and E$NLOD). */
static bool tdc_ready(SCSITapeDevice *tape)
{
    if (!tape->present)
    {
        tdc_check(tape, SK_NOT_READY, 0, TDC_EC_NOT_LOADED, TDC_X_NCAR, false, 0);
        return false;
    }
    if (!tape->loaded)
    {
        tdc_check(tape, SK_NOT_READY, 0, TDC_EC_NOT_LOADED, TDC_X_NLOD, false, 0);
        return false;
    }
    return true;
}


/* Writes must not be refused by write protection. */
static bool tdc_writable(SCSITapeDevice *tape)
{
    if (tape->tape.read_only)
    {
        tdc_check(tape, SK_DATA_PROTECT, 0, TDC_EC_WRITE_PROT, TDC_X_WPRO, false, 0);
        return false;
    }
    return true;
}


/*
 * Append rule (section 9.3.22 table, "Append Error (33): An attempt to append
 * data occurred when the tape was not at the End-Of-Recorded-Media"; Appendix
 * A E$APPN): writing starts at BOT or where the recorded data ends.
 */
static bool tdc_may_append(SCSITapeDevice *tape)
{
    if (tape_image_is_bot(&tape->tape))
    {
        return true;
    }

    bool at_eod = false;
    if (tape_image_at_eod(&tape->tape, &at_eod) != TAPE_STATUS_OK)
    {
        tdc_check(tape, SK_MEDIUM_ERROR, 0, TDC_EC_BAD_BLOCK, TDC_X_24RE, false, 0);
        return false;
    }
    if (!at_eod)
    {
        tdc_check(tape, SK_ILLEGAL_REQUEST, 0, TDC_EC_APPEND, TDC_X_APPN, false, 0);
        return false;
    }
    return true;
}


/* ------------------------------------------------------------------ */
/* Reading                                                             */
/* ------------------------------------------------------------------ */

/* Read the next tape block into tape->block. A data record that is not 512
 * bytes long cannot come from this drive, so it is a bad block here. */
static TapeStatus tdc_read_next(SCSITapeDevice *tape)
{
    uint32_t len = 0;
    TapeStatus st = tape_image_read_block(&tape->tape, tape->block, TDC_BLOCK_BYTES, &len);
    if (st == TAPE_STATUS_OK && len != TDC_BLOCK_BYTES)
    {
        LOG(LOG_CAT_SCSI, LOG_ERROR, "TAPE%d: tape record of %u bytes, the drive has only %d\n",
            tape->unit, len, TDC_BLOCK_BYTES);
        return TAPE_STATUS_ERROR;
    }
    return st;
}


/*
 * A Read or Verify stopped before the last block (section 9.3.8 and the table
 * in 9.3.8.1; Appendix A). residue = blocks not transferred.
 *   filemark      NO SENSE, FM, code 1C, E$FIMK; the tape is after the mark
 *   end of data   BLANK CHECK, code 34, E$REOL
 *   physical end  NO SENSE, EOM, code 34, E$REOP (the 9.3.8.1 table and
 *                 Appendix A; the running text of 9.3.8 says MEDIA ERROR)
 *   bad image     MEDIA ERROR, code 19, E$24RE
 */
static void tdc_report_read_stop(SCSITapeDevice *tape, TapeStatus st, uint32_t residue)
{
    switch (st)
    {
    case TAPE_STATUS_FILEMARK:
    case TAPE_STATUS_FILEMARK_EW:
        tape->filemarkCounter++;
        tdc_check(tape, SK_NO_SENSE, TDC_SENSE_FM, TDC_EC_FILEMARK, TDC_X_FIMK, true, residue);
        break;
    case TAPE_STATUS_EOD:
    case TAPE_STATUS_EOD_EW:
        tdc_check(tape, SK_BLANK_CHECK, 0, TDC_EC_READ_EOM, TDC_X_REOL, true, residue);
        break;
    case TAPE_STATUS_EOM:
        tdc_check(tape, SK_NO_SENSE, TDC_SENSE_EOM, TDC_EC_READ_EOM, TDC_X_REOP, true, residue);
        break;
    default:
        tdc_check(tape, SK_MEDIUM_ERROR, 0, TDC_EC_BAD_BLOCK, TDC_X_24RE, true, residue);
        break;
    }
}


/* ------------------------------------------------------------------ */
/* Data phase hooks                                                    */
/* ------------------------------------------------------------------ */

static uint8_t tdc_get_data(SCSITarget *t, SBUF id, int pos)
{
    SCSITapeDevice *tape = (SCSITapeDevice *)t->impl;

    if (id != SBUF_DATA)
    {
        return scsi_device_default_get_data(t, id, pos);
    }

    switch (t->scsi_cmdbuf[0])
    {
    case TDC_READ:
    {
        int at = pos % TDC_BLOCK_BYTES;
        uint8_t value = tape->block[at];
        if (at == TDC_BLOCK_BYTES - 1)
        {
            /* Last byte of a block: count it, then fetch the next block now,
             * so a filemark or the end of data ends the phase right here. */
            tape->xferDone++;
            tape->blockCounter++;
            if (tape->xferDone == tape->xferBlocks)
            {
                tdc_good(tape);
            }
            else
            {
                TapeStatus st = tdc_read_next(tape);
                if (st != TAPE_STATUS_OK)
                {
                    scsi_device_end_data_phase(t);
                    tdc_report_read_stop(tape, st, tape->xferBlocks - tape->xferDone);
                }
            }
        }
        return value;
    }

    case TDC_READ_DATA_BUFFER:
        /* Header: two reserved bytes, then the available length (section
         * 9.3.10). The length is not cut down when the allocation length is
         * shorter. */
        if (pos < TDC_DATA_BUFFER_HEADER)
        {
            static const uint8_t header[TDC_DATA_BUFFER_HEADER] = {
                0x00, 0x00, (uint8_t)(TDC_DATA_BUFFER_AVAILABLE >> 8),
                (uint8_t)TDC_DATA_BUFFER_AVAILABLE};
            return pos < 0 ? 0 : header[pos];
        }
        pos -= TDC_DATA_BUFFER_HEADER;
        return pos < TDC_DATA_BUFFER_BYTES ? tape->dataBuffer[pos] : 0;

    default:
        return (pos >= 0 && pos < (int)sizeof(tape->reply)) ? tape->reply[pos] : 0;
    }
}


static void tdc_apply_mode_select(SCSITapeDevice *tape);


static void tdc_put_data(SCSITarget *t, SBUF id, int pos, uint8_t data)
{
    SCSITapeDevice *tape = (SCSITapeDevice *)t->impl;

    /* The COMMAND phase stores the CDB through this same hook into SBUF_MAIN;
     * only the DATA OUT phase uses SBUF_DATA. */
    if (id != SBUF_DATA)
    {
        scsi_device_default_put_data(t, id, pos, data);
        return;
    }

    switch (t->scsi_cmdbuf[0])
    {
    case TDC_WRITE:
    {
        int at = pos % TDC_BLOCK_BYTES;
        tape->block[at] = data;
        if (at != TDC_BLOCK_BYTES - 1)
        {
            return;
        }

        TapeStatus st = tape_image_write_block(&tape->tape, tape->block, TDC_BLOCK_BYTES);
        if (st == TAPE_STATUS_OK || st == TAPE_STATUS_EW)
        {
            tape->xferDone++;
            tape->blockCounter++;
            uint32_t residue = tape->xferBlocks - tape->xferDone;

            /* Section 9.3.22: past the Pseudo Early Warning "the Drive stops
             * transferring data and reports CHECK CONDITION. All received
             * data will be written to the tape. ... EOM and VADD bits will be
             * set, the Sense Key will be set to NO SENSE, and the Sense Code
             * to INSUFFICIENT CAPACITY", residue in the information bytes.
             * Reported once; after it the host may write on to the physical
             * end. */
            if (!tape->pewReported && tape->tape.size - tape->tape.pos <= TDC_PEW_BYTES)
            {
                tape->pewReported = true;
                scsi_device_end_data_phase(t);
                tdc_check(tape, SK_NO_SENSE, TDC_SENSE_EOM, TDC_EC_CAPACITY, TDC_X_WEOW, true,
                          residue);
                return;
            }
            if (residue == 0)
            {
                tdc_good(tape);
            }
            return;
        }

        scsi_device_end_data_phase(t);
        uint32_t residue = tape->xferBlocks - tape->xferDone;
        if (st == TAPE_STATUS_EOM)
        {
            /* Physical end: VOLUME OVERFLOW, code 0A (section 9.3.22 and its
             * error table). Appendix A lists E$WEOM with sense key 00; the
             * command page is followed. */
            tdc_check(tape, TDC_SK_VOLUME_OVERFLOW, TDC_SENSE_EOM, TDC_EC_CAPACITY, TDC_X_WEOM,
                      true, residue);
        }
        else
        {
            tape->writeError = true;
            tdc_check(tape, SK_MEDIUM_ERROR, 0, TDC_EC_UNCORRECTABLE, TDC_X_16RW, true, residue);
        }
        return;
    }

    case TDC_VERIFY:
    {
        /* Byte compare (BC = 1): the host's data against the block read from
         * the tape (section 9.3.21). */
        int at = pos % TDC_BLOCK_BYTES;
        if (data != tape->block[at])
        {
            tape->verifyMismatch = true;
        }
        if (at != TDC_BLOCK_BYTES - 1)
        {
            return;
        }

        if (tape->verifyMismatch)
        {
            /* "The tape will stop after the erroneous block"; MISCOMPARE,
             * code 1D, VADD, the number of unverified blocks. */
            scsi_device_end_data_phase(t);
            tdc_check(tape, TDC_SK_MISCOMPARE, 0, TDC_EC_COMPARE, TDC_X_COMP, true,
                      tape->xferBlocks - tape->xferDone);
            return;
        }

        tape->xferDone++;
        tape->blockCounter++;
        if (tape->xferDone == tape->xferBlocks)
        {
            tdc_good(tape);
            return;
        }

        TapeStatus st = tdc_read_next(tape);
        if (st != TAPE_STATUS_OK)
        {
            scsi_device_end_data_phase(t);
            tdc_report_read_stop(tape, st, tape->xferBlocks - tape->xferDone);
        }
        return;
    }

    case TDC_MODE_SELECT:
        if (pos >= 0 && pos < tape->modeSelectLength)
        {
            tape->modeSelect[pos] = data;
            if (pos == tape->modeSelectLength - 1)
            {
                tdc_apply_mode_select(tape);
            }
        }
        return;

    case TDC_WRITE_DATA_BUFFER:
    {
        /* The four header bytes are reserved (section 9.3.23). */
        int at = pos - TDC_DATA_BUFFER_HEADER;
        if (at >= 0 && at < TDC_DATA_BUFFER_BYTES)
        {
            tape->dataBuffer[at] = data;
        }
        return;
    }

    default:
        return;
    }
}


/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */

/*
 * INQUIRY (section 9.3.3). Bytes 0-4 header, 8-15 vendor, 16-31 product, 32-35
 * revision, 36-37 option level, 38-44 "CREATED", 45-50 month/day/year ASCII.
 *
 * NOT KNOWN: the ISO/ECMA/ANSI version byte, the revision level and the date
 * of the drive ND shipped; the manual does not print them and no INQUIRY
 * capture of a real drive has been found. Byte 2 is set to 01h (ANSI
 * X3.131-1986, the standard the manual names in section 8); the revision and
 * the date are left as blanks rather than made up. The option level is "00",
 * the manual's value for standard firmware.
 */
static void tdc_command_inquiry(SCSITapeDevice *tape)
{
    uint8_t *r = tape->reply;
    memset(r, 0, TDC_INQUIRY_BYTES);
    r[0] = 0x01; /* sequential-access device */
    r[1] = 0x80; /* RMB: removable medium */
    r[2] = 0x01;
    r[4] = TDC_INQUIRY_BYTES - 5; /* additional length = 46 */
    memcpy(&r[8], "TANDBERG", 8);
    memcpy(&r[16], " TDC 3600       ", 16);
    memcpy(&r[32], "    ", 4);
    memcpy(&r[36], "00", 2);
    memcpy(&r[38], "CREATED", 7);
    memcpy(&r[45], "      ", 6);

    /* Allocation length 0 means no data and is not an error. */
    int size = tape->target.scsi_cmdbuf[4];
    if (size > TDC_INQUIRY_BYTES)
    {
        size = TDC_INQUIRY_BYTES;
    }
    if (size > 0)
    {
        scsi_device_data_in(&tape->target, SBUF_DATA, size);
    }
    /* "A Check Condition status will never be reported for this command." */
    tdc_good(tape);
}


/*
 * REQUEST SENSE (section 9.3.14). Allocation length above the normal sense
 * threshold (mode parameter, default 0) gives the extended block, otherwise
 * the 4-byte standard block - and length 0 gives the 4-byte standard block.
 * The sense data stays until it is read or the next command arrives.
 */
static void tdc_command_request_sense(SCSITapeDevice *tape)
{
    int alloc = tape->target.scsi_cmdbuf[4];
    int threshold = tape->vendor[TDC_VENDOR_NORMAL_SENSE_THRESHOLD];
    uint8_t *r = tape->reply;
    int size;

    if (alloc > threshold)
    {
        memcpy(r, tape->sense, TDC_SENSE_BYTES);
        size = alloc < TDC_SENSE_BYTES ? alloc : TDC_SENSE_BYTES;
    }
    else
    {
        /* Standard sense block (section 9.3.14.1): VADD, error class and
         * code in byte 0, then three information bytes with the top three
         * bits of byte 1 zero. */
        bool vadd = (tape->sense[0] & 0x80) != 0;
        uint32_t info = scsi_device_scsi_get_u32_be(&tape->sense[3]);
        memset(r, 0, TDC_NORMAL_SENSE_BYTES);
        r[0] = (uint8_t)((vadd ? 0x80 : 0x00) | (tape->sense[14] & 0x7f));
        scsi_device_scsi_put_u24_be(&r[1], info & 0x1fffff);
        size = (alloc == 0 || alloc > TDC_NORMAL_SENSE_BYTES) ? TDC_NORMAL_SENSE_BYTES : alloc;
    }

    scsi_device_data_in(&tape->target, SBUF_DATA, size);
    tdc_clear_sense(tape);
    tdc_good(tape);
}


/* MODE SENSE (section 9.3.6): the 24-byte list. Byte 0 is the length of the
 * data after it. MDTY (byte 1 bit 0) is 0 for a 600 ft cartridge, the 125 MB
 * one. The density code is "the tape format that has been found on the last
 * tape read. If ... the last tape operation was NOT a read operation, the
 * configured format is reported"; an image records no format, so the
 * configured code is the answer either way. */
static void tdc_command_mode_sense(SCSITapeDevice *tape)
{
    uint8_t *r = tape->reply;
    memset(r, 0, TDC_MODE_LIST_BYTES);
    r[0] = TDC_MODE_LIST_BYTES - 1;
    r[1] = 0x00;
    r[2] = (uint8_t)(((tape->present && tape->tape.read_only) ? 0x80 : 0x00) |
                     (tape->bufferedMode ? 0x10 : 0x00));
    r[3] = TDC_MODE_DESC_BYTES;
    r[4] = tape->density;
    scsi_device_scsi_put_u16_be(&r[10], TDC_BLOCK_BYTES);
    memcpy(&r[TDC_MODE_HEADER_BYTES + TDC_MODE_DESC_BYTES], tape->vendor, TDC_MODE_VENDOR_BYTES);

    int size = tape->target.scsi_cmdbuf[4];
    if (size > TDC_MODE_LIST_BYTES)
    {
        size = TDC_MODE_LIST_BYTES;
    }
    if (size > 0)
    {
        scsi_device_data_in(&tape->target, SBUF_DATA, size);
    }
    tdc_good(tape);
}


/*
 * MODE SELECT parameter list (section 9.3.5), applied when its last byte has
 * arrived. The header's BM bit sets buffered mode; a block descriptor (length
 * 8 or 0) carries the density code - 00 or 0F for the TDC 3640, which writes
 * only QIC-120 - and the block size, which can only be 512; the 12 vendor
 * bytes follow, page code 00. Anything else is a bad parameter block
 * (Appendix A E$BADP: ILLEGAL REQUEST, code 20).
 */
static void tdc_apply_mode_select(SCSITapeDevice *tape)
{
    const uint8_t *p = tape->modeSelect;
    int len = tape->modeSelectLength;

    if (len < TDC_MODE_HEADER_BYTES)
    {
        tdc_illegal(tape, TDC_X_BADP);
        return;
    }

    int desc_len = p[3];
    if (desc_len != 0 && desc_len != TDC_MODE_DESC_BYTES)
    {
        tdc_illegal(tape, TDC_X_BADP);
        return;
    }

    uint8_t density = tape->density;
    if (desc_len == TDC_MODE_DESC_BYTES)
    {
        if (len < TDC_MODE_HEADER_BYTES + TDC_MODE_DESC_BYTES)
        {
            tdc_illegal(tape, TDC_X_BADP);
            return;
        }
        const uint8_t *d = &p[TDC_MODE_HEADER_BYTES];
        if (d[0] != 0x00 && d[0] != 0x0F)
        {
            tdc_illegal(tape, TDC_X_BADP);
            return;
        }
        if (scsi_device_scsi_get_u16_be(&d[6]) != TDC_BLOCK_BYTES)
        {
            tdc_illegal(tape, TDC_X_BADP);
            return;
        }
        density = d[0];
    }

    uint8_t vendor[TDC_MODE_VENDOR_BYTES];
    memcpy(vendor, tape->vendor, sizeof(vendor));
    int vstart = TDC_MODE_HEADER_BYTES + desc_len;
    int vlen = len - vstart;
    if (vlen > 0)
    {
        if (p[vstart] != 0x00)
        {
            tdc_illegal(tape, TDC_X_BADP);
            return;
        }
        if (vlen > TDC_MODE_VENDOR_BYTES)
        {
            vlen = TDC_MODE_VENDOR_BYTES;
        }
        memcpy(vendor, &p[vstart], (size_t)vlen);
    }

    tape->bufferedMode = (p[2] & 0x10) != 0;
    tape->density = density;
    memcpy(tape->vendor, vendor, sizeof(vendor));
    if (tape->modeSelectSave)
    {
        tape->savedBufferedMode = tape->bufferedMode;
        tape->savedDensity = tape->density;
        memcpy(tape->savedVendor, tape->vendor, sizeof(tape->vendor));
    }

    tdc_log(tape, "MODE SELECT bm=%d density=%02X buffer=%u nst=%u%s", tape->bufferedMode,
            tape->density, tape->vendor[3], tape->vendor[TDC_VENDOR_NORMAL_SENSE_THRESHOLD],
            tape->modeSelectSave ? " (saved)" : "");
    tdc_good(tape);
}


static void tdc_command_read(SCSITapeDevice *tape)
{
    SCSITarget *t = &tape->target;

    /* Byte 1 bit 0 is the fixed-block bit and must be 1 (section 9.3.8). */
    if (!(t->scsi_cmdbuf[1] & 0x01))
    {
        tdc_illegal(tape, TDC_X_BADC);
        return;
    }
    if (!tdc_ready(tape))
    {
        return;
    }

    /* "A Read command was issued after a Write command, but no intervening
     * rewind operation occurred": ILLEGAL REQUEST, code 34 (9.3.8.1;
     * E$RDWR). */
    if (tape->lastWasWrite)
    {
        tdc_check(tape, SK_ILLEGAL_REQUEST, 0, TDC_EC_READ_EOM, TDC_X_RDWR, false, 0);
        return;
    }

    tape->xferBlocks = scsi_device_scsi_get_u24_be(&t->scsi_cmdbuf[2]);
    tape->xferDone = 0;
    tdc_log(tape, "READ %u blocks", tape->xferBlocks);
    if (tape->xferBlocks == 0)
    {
        tdc_good(tape);
        return;
    }

    if (tape->xferBlocks > TDC_MAX_PHASE_BLOCKS)
    {
        LOG(LOG_CAT_SCSI, LOG_ERROR, "TAPE%d: READ of %u blocks is too long for one data phase\n",
            tape->unit, tape->xferBlocks);
        tdc_illegal(tape, TDC_X_BADC);
        return;
    }

    tdc_enter_mode(tape, TDC_MODE_READ);
    TapeStatus st = tdc_read_next(tape);
    if (st != TAPE_STATUS_OK)
    {
        tdc_report_read_stop(tape, st, tape->xferBlocks);
        return;
    }
    /* Status is queued by tdc_get_data. */
    scsi_device_data_in(t, SBUF_DATA, (int)(tape->xferBlocks * TDC_BLOCK_BYTES));
}


static void tdc_command_write(SCSITapeDevice *tape)
{
    SCSITarget *t = &tape->target;

    if (!(t->scsi_cmdbuf[1] & 0x01))
    {
        tdc_illegal(tape, TDC_X_BADC);
        return;
    }
    if (!tdc_ready(tape) || !tdc_writable(tape))
    {
        return;
    }

    /* Section 9.3.22: "If an additional Write command is issued after a
     * write error has occurred ... UNRECOVERABLE ERROR ... MEDIA ERROR". */
    if (tape->writeError)
    {
        tdc_check(tape, SK_MEDIUM_ERROR, 0, TDC_EC_UNCORRECTABLE, TDC_X_16RW, false, 0);
        return;
    }

    tape->xferBlocks = scsi_device_scsi_get_u24_be(&t->scsi_cmdbuf[2]);
    tape->xferDone = 0;
    tdc_log(tape, "WRITE %u blocks at %llu", tape->xferBlocks,
            (unsigned long long)tape->tape.pos);
    if (tape->xferBlocks == 0)
    {
        tdc_good(tape);
        return;
    }
    if (tape->xferBlocks > TDC_MAX_PHASE_BLOCKS)
    {
        LOG(LOG_CAT_SCSI, LOG_ERROR, "TAPE%d: WRITE of %u blocks is too long for one data phase\n",
            tape->unit, tape->xferBlocks);
        tdc_illegal(tape, TDC_X_BADC);
        return;
    }
    if (!tdc_may_append(tape))
    {
        return;
    }

    tdc_enter_mode(tape, TDC_MODE_WRITE);
    tape->lastWasWrite = true;
    /* Status is queued by tdc_put_data. */
    scsi_device_data_out(t, SBUF_DATA, (int)(tape->xferBlocks * TDC_BLOCK_BYTES));
}


/* WRITE FILE MARK (section 9.3.24). Count 0 writes no filemark and only
 * empties the drive buffer, which the image never holds data in. */
static void tdc_command_write_filemark(SCSITapeDevice *tape)
{
    SCSITarget *t = &tape->target;
    uint32_t count = scsi_device_scsi_get_u24_be(&t->scsi_cmdbuf[2]);

    if (!tdc_ready(tape) || !tdc_writable(tape))
    {
        return;
    }
    tdc_log(tape, "WRITE FILE MARK %u", count);
    if (count == 0)
    {
        tdc_good(tape);
        return;
    }
    if (!tdc_may_append(tape))
    {
        return;
    }

    tdc_enter_mode(tape, TDC_MODE_WRITE);
    tape->lastWasWrite = true;
    TapeStatus st = tape_image_write_filemarks(&tape->tape, count);
    switch (st)
    {
    case TAPE_STATUS_OK:
    case TAPE_STATUS_EW:
        tape->filemarkCounter = (uint16_t)(tape->filemarkCounter + count);
        tdc_good(tape);
        break;
    case TAPE_STATUS_EOM:
        tdc_check(tape, TDC_SK_VOLUME_OVERFLOW, TDC_SENSE_EOM, TDC_EC_CAPACITY, TDC_X_WEOM, true,
                  count);
        break;
    default:
        tape->writeError = true;
        tdc_check(tape, SK_MEDIUM_ERROR, 0, TDC_EC_UNCORRECTABLE, TDC_X_16RW, true, count);
        break;
    }
}


/* VERIFY (section 9.3.21). BC = 1: the host sends the blocks and they are
 * compared byte by byte. BC = 0: the blocks are read and checked (a CRC check
 * on the drive; an image block either reads or does not). */
static void tdc_command_verify(SCSITapeDevice *tape)
{
    SCSITarget *t = &tape->target;
    bool bc = (t->scsi_cmdbuf[1] & 0x02) != 0;

    if (!(t->scsi_cmdbuf[1] & 0x01))
    {
        tdc_illegal(tape, TDC_X_BADC);
        return;
    }
    if (!tdc_ready(tape))
    {
        return;
    }
    /* 9.3.21.1: ILLEGAL REQUEST, Invalid Command when "the last command
     * issued was a Write command". */
    if (tape->lastWasWrite)
    {
        tdc_illegal(tape, TDC_X_INAP);
        return;
    }

    tape->xferBlocks = scsi_device_scsi_get_u24_be(&t->scsi_cmdbuf[2]);
    tape->xferDone = 0;
    tape->verifyMismatch = false;
    tdc_log(tape, "VERIFY %u blocks bc=%d", tape->xferBlocks, bc);
    if (tape->xferBlocks == 0)
    {
        tdc_good(tape);
        return;
    }

    tdc_enter_mode(tape, TDC_MODE_READ);
    if (bc)
    {
        if (tape->xferBlocks > TDC_MAX_PHASE_BLOCKS)
        {
            LOG(LOG_CAT_SCSI, LOG_ERROR, "TAPE%d: VERIFY of %u blocks is too long for one data phase\n",
                tape->unit, tape->xferBlocks);
            tdc_illegal(tape, TDC_X_BADC);
            return;
        }
        TapeStatus st = tdc_read_next(tape);
        if (st != TAPE_STATUS_OK)
        {
            tdc_report_read_stop(tape, st, tape->xferBlocks);
            return;
        }
        scsi_device_data_out(t, SBUF_DATA, (int)(tape->xferBlocks * TDC_BLOCK_BYTES));
        return;
    }

    while (tape->xferDone < tape->xferBlocks)
    {
        TapeStatus st = tdc_read_next(tape);
        if (st != TAPE_STATUS_OK)
        {
            tdc_report_read_stop(tape, st, tape->xferBlocks - tape->xferDone);
            return;
        }
        tape->xferDone++;
        tape->blockCounter++;
    }
    tdc_good(tape);
}


/* SPACE (section 9.3.19). Code in byte 1 bits 0-1, count bytes 2-4 as a
 * 24-bit two's complement number. */
static void tdc_command_space(SCSITapeDevice *tape)
{
    SCSITarget *t = &tape->target;
    int code = t->scsi_cmdbuf[1] & 0x03;
    int32_t count = (int32_t)scsi_device_scsi_get_u24_be(&t->scsi_cmdbuf[2]);
    if (count & 0x800000)
    {
        count -= 0x1000000;
    }

    if (!tdc_ready(tape))
    {
        return;
    }
    tdc_log(tape, "SPACE code=%d count=%d", code, count);
    tdc_enter_mode(tape, TDC_MODE_POSITION);

    uint32_t done = 0;
    TapeStatus st;

    if (code == 3)
    {
        /* Logical end of media: the count is ignored. Physical end: EOM bit,
         * READ EOM, NO SENSE. */
        st = tape_image_space_eod(&tape->tape);
        if (st == TAPE_STATUS_OK)
        {
            tdc_good(tape);
        }
        else if (st == TAPE_STATUS_EOM)
        {
            tdc_check(tape, SK_NO_SENSE, TDC_SENSE_EOM, TDC_EC_READ_EOM, TDC_X_REOP, false, 0);
        }
        else
        {
            tdc_check(tape, SK_MEDIUM_ERROR, 0, TDC_EC_BAD_BLOCK, TDC_X_24RE, false, 0);
        }
        return;
    }

    if (count == 0)
    {
        tdc_good(tape);
        return;
    }

    if (code == 2)
    {
        /* Sequential filemarks: forward only; logical end -> EOM bit, READ
         * EOM, BLANK CHECK; physical end -> EOM bit, READ EOM, NO SENSE. */
        if (count < 0)
        {
            tdc_illegal(tape, TDC_X_BADC);
            return;
        }
        st = tape_image_space_filemarks(&tape->tape, (uint32_t)count, true, &done);
        if (st == TAPE_STATUS_OK)
        {
            tdc_good(tape);
        }
        else if (st == TAPE_STATUS_EOD || st == TAPE_STATUS_EOD_EW)
        {
            tdc_check(tape, SK_BLANK_CHECK, TDC_SENSE_EOM, TDC_EC_READ_EOM, TDC_X_REOL, false, 0);
        }
        else if (st == TAPE_STATUS_EOM)
        {
            tdc_check(tape, SK_NO_SENSE, TDC_SENSE_EOM, TDC_EC_READ_EOM, TDC_X_REOP, false, 0);
        }
        else
        {
            tdc_check(tape, SK_MEDIUM_ERROR, 0, TDC_EC_BAD_BLOCK, TDC_X_24RE, false, 0);
        }
        return;
    }

    uint32_t want = (uint32_t)(count < 0 ? -count : count);
    if (count > 0)
    {
        st = code == 0 ? tape_image_space_blocks(&tape->tape, want, &done)
                       : tape_image_space_filemarks(&tape->tape, want, false, &done);
    }
    else
    {
        st = code == 0 ? tape_image_space_blocks_reverse(&tape->tape, want, &done)
                       : tape_image_space_filemarks_reverse(&tape->tape, want, &done);
    }
    uint32_t residue = want - done;

    switch (st)
    {
    case TAPE_STATUS_OK:
        tdc_good(tape);
        break;
    case TAPE_STATUS_FILEMARK:
    case TAPE_STATUS_FILEMARK_EW:
        /* Spacing over blocks met a filemark: FM and VADD, residue not
         * counting the filemark; forward the tape is after it, reverse
         * before it. */
        tdc_check(tape, SK_NO_SENSE, TDC_SENSE_FM, TDC_EC_FILEMARK, TDC_X_FIMK, true, residue);
        break;
    case TAPE_STATUS_EOD:
    case TAPE_STATUS_EOD_EW:
        /* End of recorded data: BLANK CHECK, VADD. */
        tdc_check(tape, SK_BLANK_CHECK, 0, TDC_EC_READ_EOM, TDC_X_REOL, true, residue);
        break;
    case TAPE_STATUS_BOM:
        /* Reverse into the beginning of the medium: EOM bit, NO SENSE. */
        tdc_check(tape, SK_NO_SENSE, TDC_SENSE_EOM, TDC_EC_NONE, TDC_X_OK, true, residue);
        break;
    case TAPE_STATUS_EOM:
        /* Forward into the physical end: EOM bit, MEDIA ERROR. */
        tdc_check(tape, SK_MEDIUM_ERROR, TDC_SENSE_EOM, TDC_EC_READ_EOM, TDC_X_REOP, true,
                  residue);
        break;
    default:
        tdc_check(tape, SK_MEDIUM_ERROR, 0, TDC_EC_BAD_BLOCK, TDC_X_24RE, true, residue);
        break;
    }
}


/* REQUEST BLOCK ADDRESS (section 9.3.13): up to 3 bytes, allocation length 0
 * meaning the default 3. "Block addresses begin with block number 1." */
static void tdc_command_request_block_address(SCSITapeDevice *tape)
{
    if (!tdc_ready(tape))
    {
        return;
    }

    uint32_t records = 0;
    if (tape_image_block_address(&tape->tape, &records) != TAPE_STATUS_OK)
    {
        tdc_check(tape, SK_MEDIUM_ERROR, 0, TDC_EC_BAD_BLOCK, TDC_X_24RE, false, 0);
        return;
    }

    scsi_device_scsi_put_u24_be(tape->reply, records + 1);
    int size = tape->target.scsi_cmdbuf[4];
    if (size == 0 || size > 3)
    {
        size = 3;
    }
    scsi_device_data_in(&tape->target, SBUF_DATA, size);
    tdc_good(tape);
}


/* SEEK BLOCK (section 9.3.17): address in bytes 2-4. Address 0 is ILLEGAL
 * REQUEST / Invalid Command; an address not on the tape is BLANK CHECK / No
 * Record Found (code 14). */
static void tdc_command_seek_block(SCSITapeDevice *tape)
{
    uint32_t address = scsi_device_scsi_get_u24_be(&tape->target.scsi_cmdbuf[2]);

    if (address == 0)
    {
        tdc_illegal(tape, TDC_X_ILCM);
        return;
    }
    if (!tdc_ready(tape))
    {
        return;
    }

    tdc_log(tape, "SEEK BLOCK %u", address);
    tdc_enter_mode(tape, TDC_MODE_POSITION);
    TapeStatus st = tape_image_locate(&tape->tape, address - 1);
    if (st == TAPE_STATUS_OK)
    {
        tdc_good(tape);
    }
    else if (st == TAPE_STATUS_ERROR)
    {
        tdc_check(tape, SK_MEDIUM_ERROR, 0, TDC_EC_BAD_BLOCK, TDC_X_24RE, false, 0);
    }
    else
    {
        tdc_check(tape, SK_BLANK_CHECK, 0, TDC_EC_NO_RECORD, TDC_X_QRND, false, 0);
    }
}


/* LOAD/UNLOAD (section 9.3.4): byte 4 bit 0 Load, bit 1 Retension; byte 5
 * bit 7 EOT (unload to the end of the tape). A retension pass runs the tape
 * to the end and back, which leaves the image as it was. */
static void tdc_command_load_unload(SCSITapeDevice *tape)
{
    SCSITarget *t = &tape->target;
    bool load = (t->scsi_cmdbuf[4] & 0x01) != 0;
    bool eot = (t->scsi_cmdbuf[5] & 0x80) != 0;

    if (!tape->present)
    {
        tdc_check(tape, SK_NOT_READY, 0, TDC_EC_NOT_LOADED, TDC_X_NCAR, false, 0);
        return;
    }

    tdc_log(tape, "%s%s", load ? "LOAD" : "UNLOAD", (!load && eot) ? " to EOT" : "");
    tdc_to_bot(tape);
    if (load)
    {
        tape->loaded = true;
    }
    else
    {
        tape->loaded = false;
        tape_image_rewind(&tape->tape, eot);
    }
    tdc_good(tape);
}


/* ERASE (section 9.3.2): the LONG bit (byte 1 bit 0) must be set; the whole
 * tape is erased and left at BOT. */
static void tdc_command_erase(SCSITapeDevice *tape)
{
    if (!(tape->target.scsi_cmdbuf[1] & 0x01))
    {
        tdc_illegal(tape, TDC_X_BADC);
        return;
    }
    if (!tdc_ready(tape) || !tdc_writable(tape))
    {
        return;
    }

    tdc_log(tape, "ERASE");
    tdc_to_bot(tape);
    if (tape_image_erase(&tape->tape) != TAPE_STATUS_OK)
    {
        tdc_check(tape, SK_MEDIUM_ERROR, 0, TDC_EC_UNCORRECTABLE, TDC_X_16RW, false, 0);
        return;
    }
    tape->writeError = false;
    tdc_good(tape);
}


/*
 * SEND DIAGNOSTIC (section 9.3.18). Byte 1: bit 2 STest, bit 1 DevOfl, bit 0
 * UniOfl; parameter list length in bytes 3-4.
 *   STest = 1, not both offline bits:  Selftest 1, no tape motion.
 *   STest = 1, both offline bits:      Selftest 2, the acceptance test, which
 *                                      writes and reads the cartridge.
 *   STest = 0: both offline bits must be set; list length 0 is "no operation",
 *              8 is a Selftest 2 parameter block.
 * Selftest 1 checks the drive's own hardware, which has nothing that can fail
 * here, so it passes. Selftest 2 is answered GOOD with no tape motion and
 * nothing written. That follows ND, not Tandberg: ND-210523G, "3.5 Known but
 * not Corrected Errors", says SCSI-TV device test 3 (which sends Selftest 2,
 * 1D 07) "executed on TANDBERG TDC 3600 will perform no operation". The
 * Tandberg manual calls Selftest 2 an acceptance test that writes and reads
 * the cartridge, but does not say what it writes or reports (section
 * 10.1.2). Chosen by Ronny, 2026-11-01. nd100x BUGS.md C6 keeps the open
 * question of what a real drive answers.
 */
static void tdc_command_send_diagnostic(SCSITapeDevice *tape)
{
    SCSITarget *t = &tape->target;
    bool stest = (t->scsi_cmdbuf[1] & 0x04) != 0;
    bool both_offline = (t->scsi_cmdbuf[1] & 0x03) == 0x03;
    uint16_t list_len = scsi_device_scsi_get_u16_be(&t->scsi_cmdbuf[3]);

    tdc_log(tape, "SEND DIAGNOSTIC stest=%d devofl/uniofl=%d len=%u", stest,
            t->scsi_cmdbuf[1] & 0x03, list_len);

    if (stest)
    {
        if (list_len != 0)
        {
            tdc_illegal(tape, TDC_X_BADC);
            return;
        }
        if (!both_offline)
        {
            tdc_good(tape);
            return;
        }
    }
    else
    {
        if (!both_offline || (list_len != 0 && list_len != 8))
        {
            tdc_illegal(tape, TDC_X_BADC);
            return;
        }
        if (list_len == 0)
        {
            tdc_good(tape);
            return;
        }
    }

    tdc_log(tape, "SEND DIAGNOSTIC Selftest 2: no operation (ND-210523G 3.5)");
    tdc_good(tape);
}


/* RECOVER BUFFERED DATA (section 9.3.11): returns blocks the drive took but
 * did not get onto the tape. Every block written here is on the image before
 * the command ends, so the buffer is always empty: any count above 0 is
 * "the buffer contains less than the specified number of blocks" - CHECK
 * CONDITION with EOM and VADD and the difference. The manual gives no sense
 * key or error code for this case, so both are left 0. */
static void tdc_command_recover_buffered(SCSITapeDevice *tape)
{
    SCSITarget *t = &tape->target;
    uint32_t count = scsi_device_scsi_get_u24_be(&t->scsi_cmdbuf[2]);

    if (!(t->scsi_cmdbuf[1] & 0x01))
    {
        tdc_illegal(tape, TDC_X_BADC);
        return;
    }
    if (!tdc_ready(tape))
    {
        return;
    }
    if (count == 0)
    {
        tdc_good(tape);
        return;
    }
    tdc_check(tape, SK_NO_SENSE, TDC_SENSE_EOM, TDC_EC_NONE, TDC_X_OK, true, count);
}


static void tdc_command(SCSITarget *t)
{
    SCSITapeDevice *tape = (SCSITapeDevice *)t->impl;
    uint8_t cmd = t->scsi_cmdbuf[0];
    int lun = (t->scsi_identify & 0x80) ? (t->scsi_identify & 0x07) : (t->scsi_cmdbuf[1] >> 5);
    /* Control byte: the last CDB byte. Bit 0 is Link. */
    uint8_t control = t->scsi_cmdbuf[t->scsi_cmdsize > 0 ? t->scsi_cmdsize - 1 : 5];

    if (Log_IsEnabled(LOG_CAT_SCSI, LOG_DEBUG))
    {
        char bytes[3 * 16 + 1];
        int n = t->scsi_cmdsize < 16 ? t->scsi_cmdsize : 16;
        int w = 0;
        for (int i = 1; i < n; i++)
        {
            w += snprintf(bytes + w, sizeof(bytes) - (size_t)w, i == 1 ? "%02X" : ",%02X",
                          t->scsi_cmdbuf[i]);
        }
        bytes[w] = '\0';
        tdc_log(tape, "CDB op=0x%02X cdb=%s lun=%d", cmd, bytes, lun);
    }

    tdc_refresh_media(tape);

    /* INQUIRY "will perform the Inquiry command and will not clear the Unit
     * Attention Condition" (section 9.3.3). */
    if (cmd == TDC_INQUIRY)
    {
        tdc_command_inquiry(tape);
        return;
    }

    /*
     * Unit Attention: the command is not performed (Appendix B / sense
     * code 30: "The requested command is then not performed. This condition
     * is cleared when the next I/O is issued by the same Initiator"), and
     * "the tape cartridge is logically unloaded" (section 8.6.2).
     * ASSUMPTION: "logically unloaded" is taken to mean the drive forgets the
     * tape position and starts again from BOT (section 9.3.8: a Read after
     * reset first searches the reference burst and reads from BOT) - not that
     * a LOAD command is needed, since the default Load Function is auto load.
     * ASSUMPTION: a REQUEST SENSE as the first command returns the Unit
     * Attention sense and clears it (the usual SCSI-1 behaviour; the manual
     * does not say).
     */
    if (tape->unitAttention && tape->present)
    {
        tape->unitAttention = false;
        tdc_to_bot(tape);
        if (cmd == TDC_REQUEST_SENSE)
        {
            tdc_set_sense(tape, SK_UNIT_ATTENTION, 0, TDC_EC_UNIT_ATTN, TDC_X_UATT, false, 0);
            tdc_command_request_sense(tape);
            return;
        }
        tdc_check(tape, SK_UNIT_ATTENTION, 0, TDC_EC_UNIT_ATTN, TDC_X_UATT, false, 0);
        return;
    }

    if (cmd == TDC_REQUEST_SENSE)
    {
        tdc_command_request_sense(tape);
        return;
    }

    /* "Sense Data will be cleared upon receipt of any subsequent command"
     * (section 9.3.14). */
    tdc_clear_sense(tape);

    if (lun != 0)
    {
        tdc_illegal(tape, TDC_X_BADC);
        return;
    }

    /* Linked commands (Link bit) answer with INTERMEDIATE status and run the
     * next CDB; that is not modelled. Refused loudly; nd100x BUGS.md names
     * it. */
    if (control & 0x01)
    {
        LOG(LOG_CAT_SCSI, LOG_ERROR, "TAPE%d: linked command 0x%02X is not supported\n",
            tape->unit, cmd);
        tdc_illegal(tape, TDC_X_BADC);
        return;
    }

    switch (cmd)
    {
    case TDC_TEST_UNIT_READY:
        if (tdc_ready(tape))
        {
            tdc_good(tape);
        }
        break;

    case TDC_REWIND:
        if (tdc_ready(tape))
        {
            tdc_log(tape, "REWIND");
            tdc_to_bot(tape);
            tdc_good(tape);
        }
        break;

    case TDC_REQUEST_BLOCK_ADDR:
        tdc_command_request_block_address(tape);
        break;

    case TDC_READ_BLOCK_LIMITS:
        /* Section 9.3.9: maximum and minimum block length both 200h. */
        memset(tape->reply, 0, 6);
        scsi_device_scsi_put_u24_be(&tape->reply[1], TDC_BLOCK_BYTES);
        scsi_device_scsi_put_u16_be(&tape->reply[4], TDC_BLOCK_BYTES);
        scsi_device_data_in(t, SBUF_DATA, 6);
        tdc_good(tape);
        break;

    case TDC_READ:
        tdc_command_read(tape);
        break;

    case TDC_WRITE:
        tdc_command_write(tape);
        break;

    case TDC_SEEK_BLOCK:
        tdc_command_seek_block(tape);
        break;

    case TDC_WRITE_FILEMARK:
        tdc_command_write_filemark(tape);
        break;

    case TDC_SPACE:
        tdc_command_space(tape);
        break;

    case TDC_VERIFY:
        tdc_command_verify(tape);
        break;

    case TDC_RECOVER_BUFFERED:
        tdc_command_recover_buffered(tape);
        break;

    case TDC_MODE_SELECT:
        tape->modeSelectLength = t->scsi_cmdbuf[4];
        tape->modeSelectSave = (t->scsi_cmdbuf[5] & 0x40) != 0;
        if (tape->modeSelectLength > TDC_MODE_LIST_BYTES)
        {
            /* The list is 24 bytes in all (section 9.3.5). */
            tdc_illegal(tape, TDC_X_BADP);
            break;
        }
        if (tape->modeSelectLength == 0)
        {
            tdc_good(tape);
            break;
        }
        /* Applied, and status queued, when the last byte arrives. */
        scsi_device_data_out(t, SBUF_DATA, tape->modeSelectLength);
        break;

    case TDC_MODE_SENSE:
        tdc_command_mode_sense(tape);
        break;

    case TDC_RESERVE_UNIT:
    case TDC_RELEASE_UNIT:
        /* Reservation keeps other initiators out (RESERVATION CONFLICT).
         * The ND-100 is the only initiator on this bus, and the target base
         * does not record which initiator selected it, so there is nobody to
         * keep out: both commands complete. */
        tdc_good(tape);
        break;

    case TDC_ERASE:
        tdc_command_erase(tape);
        break;

    case TDC_LOAD_UNLOAD:
        tdc_command_load_unload(tape);
        break;

    case TDC_SEND_DIAGNOSTIC:
        tdc_command_send_diagnostic(tape);
        break;

    case TDC_PREVENT_ALLOW:
        /* Only the front LED (section 9.3.7). */
        tdc_good(tape);
        break;

    case TDC_READ_DATA_BUFFER:
    {
        /* SCSI-2 READ BUFFER: mode in byte 1 bits 0-2, allocation length in
         * bytes 6-8. Only mode 000b (header and data) is known for this drive:
         * the Tandberg manual describes exactly that layout (section 9.3.10:
         * four-byte header, then the buffer data). Any other mode is refused
         * loudly. Length 0 is no data and not an error; never more than
         * header + available length. */
        if ((t->scsi_cmdbuf[1] & 0x07) != 0)
        {
            LOG(LOG_CAT_SCSI, LOG_ERROR, "TAPE%d: READ BUFFER mode %d is not supported\n",
                tape->unit, t->scsi_cmdbuf[1] & 0x07);
            tdc_illegal(tape, TDC_X_BADC);
            break;
        }
        int alloc = ((int)t->scsi_cmdbuf[6] << 16) | scsi_device_scsi_get_u16_be(&t->scsi_cmdbuf[7]);
        int max = TDC_DATA_BUFFER_HEADER + TDC_DATA_BUFFER_AVAILABLE;
        if (alloc > max)
        {
            alloc = max;
        }
        if (alloc > 0)
        {
            scsi_device_data_in(t, SBUF_DATA, alloc);
        }
        tdc_good(tape);
        break;
    }

    case TDC_WRITE_DATA_BUFFER:
    {
        /* SCSI-2 WRITE BUFFER: mode in byte 1 bits 0-2, transfer length in
         * bytes 6-8. Only mode 000b (header and data, section 9.3.23 of the
         * Tandberg manual) is known; other modes, among them download
         * microcode, are refused loudly. A length above header + available
         * length is ILLEGAL REQUEST and no data is taken (section 9.3.23). */
        if ((t->scsi_cmdbuf[1] & 0x07) != 0)
        {
            LOG(LOG_CAT_SCSI, LOG_ERROR, "TAPE%d: WRITE BUFFER mode %d is not supported\n",
                tape->unit, t->scsi_cmdbuf[1] & 0x07);
            tdc_illegal(tape, TDC_X_BADC);
            break;
        }
        int len = ((int)t->scsi_cmdbuf[6] << 16) | scsi_device_scsi_get_u16_be(&t->scsi_cmdbuf[7]);
        if (len > TDC_DATA_BUFFER_HEADER + TDC_DATA_BUFFER_AVAILABLE)
        {
            tdc_illegal(tape, TDC_X_BADC);
            break;
        }
        if (len > 0)
        {
            scsi_device_data_out(t, SBUF_DATA, len);
        }
        tdc_good(tape);
        break;
    }

    case TDC_COPY:
        /* Copy (section 9.3.1) makes the drive an initiator that reads a disk
         * on the same bus. Not modelled; refused loudly, nd100x BUGS.md. */
        LOG(LOG_CAT_SCSI, LOG_ERROR, "TAPE%d: COPY is not supported\n", tape->unit);
        tdc_illegal(tape, TDC_X_ILCM);
        break;

    default:
        tdc_log(tape, "command 0x%02X *** UNKNOWN ***", cmd);
        tdc_illegal(tape, TDC_X_ILCM);
        break;
    }
}


/* ------------------------------------------------------------------ */
/* Reset and init                                                      */
/* ------------------------------------------------------------------ */

/* Power-on and SCSI bus reset: saved mode parameters back in force, Unit
 * Attention posted (section 8.6.2: "The first command sent by each Initiator
 * to the Drive after a SCSI-bus reset condition causes ... UNIT
 * ATTENTION"). */
static void tdc_reset_state(SCSITapeDevice *tape)
{
    tape_image_flush(&tape->tape);
    tape->density = tape->savedDensity;
    tape->bufferedMode = tape->savedBufferedMode;
    memcpy(tape->vendor, tape->savedVendor, sizeof(tape->vendor));
    tape->unitAttention = true;
    tape->writeError = false;
    tape->blockCounter = 0;
    tape->filemarkCounter = 0;
    tdc_clear_sense(tape);
}


static void tdc_bus_reset(SCSITarget *t)
{
    tdc_reset_state((SCSITapeDevice *)t->impl);
}


void scsi_tape_flush(SCSITapeDevice *tape)
{
    if (tape)
    {
        tape_image_flush(&tape->tape);
    }
}


void scsi_tape_init(SCSITapeDevice *tape, SCSIBus *bus, uint8_t scsi_id, struct Device *owner,
                    int unit)
{
    memset(tape, 0, sizeof(SCSITapeDevice));

    tape->owner = owner;
    tape->unit = unit;
    tape->tape.cache_block = -1;

    /* Mode parameter defaults (section 9.3.5): BM = 1, density 00. */
    tape->savedBufferedMode = true;
    tape->savedDensity = 0x00;
    memcpy(tape->savedVendor, tdc_default_vendor, sizeof(tape->savedVendor));

    /* Hooks before scsi_device_init: it resets the target. */
    tape->target.impl = tape;
    tape->target.scsi_command = tdc_command;
    tape->target.scsi_get_data = tdc_get_data;
    tape->target.scsi_put_data = tdc_put_data;
    tape->target.scsi_bus_reset = tdc_bus_reset;

    scsi_device_init(&tape->target, bus, scsi_id, "SCSI-TAPE");

    tdc_reset_state(tape);
    tdc_refresh_media(tape);
}
