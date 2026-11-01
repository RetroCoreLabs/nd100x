/*
 * test_scsi_tape.c - the SCSI streamer target (Tandberg TDC 3600) and its
 * SIMH .tap tape layer.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Every expected value comes from the TDC 3620/3640/3660 Reference Manual
 * (chapter 9 and Appendix A; the section is named at each check) or from the
 * SIMH magtape format (simh_magtape.pdf) for the bytes in the image.
 *
 * scsi_tape.c and tape_image.c are INCLUDED rather than linked, so the
 * target's command decoder and its byte fetch and store can be called the way
 * the SCSI bus calls them, without a controller or the phase engine. The tape
 * image is a buffer in memory behind block callbacks of the same shape as
 * machine_block_read / machine_block_write, 1024-byte blocks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "scsi/tape_image.c"
#include "scsi/scsi_tape.c"

static int s_failed = 0;
static int s_checks = 0;

#define CHECK(cond, msg)                                                                 \
    do                                                                                   \
    {                                                                                    \
        s_checks++;                                                                      \
        if (!(cond))                                                                     \
        {                                                                                \
            printf("  FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);                   \
            s_failed++;                                                                  \
        }                                                                                \
    } while (0)

/* ------------------------------------------------------------------ */
/* The tape image: a memory buffer behind block callbacks             */
/* ------------------------------------------------------------------ */

#define IMG_BLOCK 1024
#define IMG_MAX   (1024 * 1024)

static uint8_t s_img[IMG_MAX];
static size_t s_img_size;
static bool s_img_wp;
static bool s_img_mounted;

static int img_read(Device *dev, uint8_t *buf, size_t count, uint32_t block, int unit)
{
    (void)dev;
    (void)unit;
    if (!s_img_mounted)
    {
        return -1;
    }
    for (size_t i = 0; i < count; i++)
    {
        size_t at = ((size_t)block + i) * IMG_BLOCK;
        for (size_t j = 0; j < IMG_BLOCK; j++)
        {
            buf[i * IMG_BLOCK + j] = (at + j < s_img_size) ? s_img[at + j] : 0;
        }
    }
    return (int)count;
}

static int img_write(Device *dev, const uint8_t *buf, size_t count, uint32_t block, int unit)
{
    (void)dev;
    (void)unit;
    if (!s_img_mounted || s_img_wp)
    {
        return -1;
    }
    size_t at = (size_t)block * IMG_BLOCK;
    if (at + count * IMG_BLOCK > s_img_size)
    {
        return -1;
    }
    memcpy(&s_img[at], buf, count * IMG_BLOCK);
    return (int)count;
}

static int img_info(Device *dev, size_t *size, bool *wp, int unit)
{
    (void)dev;
    (void)unit;
    if (!s_img_mounted)
    {
        return -1;
    }
    *size = s_img_size;
    *wp = s_img_wp;
    return 0;
}

static Device s_owner;

/* A blank cartridge: the whole image FF, the SIMH end-of-medium marker. */
static void blank_tape(size_t size)
{
    memset(s_img, 0xff, size);
    s_img_size = size;
    s_img_wp = false;
    s_img_mounted = true;
    memset(&s_owner, 0, sizeof(s_owner));
    s_owner.blockCallbacks.readFunc = img_read;
    s_owner.blockCallbacks.writeFunc = img_write;
    s_owner.blockCallbacks.diskInfoFunc = img_info;
    s_owner.blockSizeBytes = IMG_BLOCK;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* ------------------------------------------------------------------ */
/* Driving the target the way the bus does                            */
/* ------------------------------------------------------------------ */

static SCSIBus s_bus;
static SCSITapeDevice s_tape;

/* Send a six-byte CDB through the target's own store hook into SBUF_MAIN,
 * then run the decoder with an empty phase queue. */
static void cdb6(uint8_t op, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5)
{
    SCSITarget *t = &s_tape.target;
    uint8_t cdb[6] = {op, b1, b2, b3, b4, b5};
    for (int i = 0; i < 6; i++)
    {
        tdc_put_data(t, SBUF_MAIN, i, cdb[i]);
    }
    t->scsi_cmdsize = 6;
    t->buf_control_rpos = 0;
    t->buf_control_wpos = 0;
    tdc_command(t);
}

static void cdb10(uint8_t op, uint16_t len)
{
    SCSITarget *t = &s_tape.target;
    uint8_t cdb[10] = {op, 0, 0, 0, 0, 0, 0, (uint8_t)(len >> 8), (uint8_t)len, 0};
    for (int i = 0; i < 10; i++)
    {
        tdc_put_data(t, SBUF_MAIN, i, cdb[i]);
    }
    t->scsi_cmdsize = 10;
    t->buf_control_rpos = 0;
    t->buf_control_wpos = 0;
    tdc_command(t);
}

/* The status byte queued for the command, or -1 if none is queued yet. */
static int status_of(void)
{
    SCSITarget *t = &s_tape.target;
    for (int i = 0; i < t->buf_control_wpos; i++)
    {
        if (t->buf_control[i].action == BC_STATUS)
        {
            return t->buf_control[i].param1;
        }
    }
    return -1;
}

/* Size of the queued data phase of the given kind, or -1. */
static int phase_size(SCSIBufControlAction kind)
{
    SCSITarget *t = &s_tape.target;
    for (int i = 0; i < t->buf_control_wpos; i++)
    {
        if (t->buf_control[i].action == kind)
        {
            return t->buf_control[i].param2;
        }
    }
    return -1;
}

/* Run the queued DATA IN phase byte by byte into buf. Stops when the target
 * queues its status before the last byte - what scsi_device_end_data_phase
 * does on the bus. A command that queued its status with the data phase
 * (INQUIRY, MODE SENSE) always runs to the end. Returns the number of bytes moved. */
static int data_in(uint8_t *buf, int max)
{
    int size = phase_size(BC_DATA_IN);
    bool queued_before = status_of() >= 0;
    int n = 0;
    for (int pos = 0; pos < size; pos++)
    {
        uint8_t b = tdc_get_data(&s_tape.target, SBUF_DATA, pos);
        if (n < max)
        {
            buf[n] = b;
        }
        n++;
        if (!queued_before && status_of() >= 0 && pos < size - 1)
        {
            break;
        }
    }
    return n;
}

/* Run the queued DATA OUT phase from buf, the same way. */
static int data_out(const uint8_t *buf, int len)
{
    int size = phase_size(BC_DATA_OUT);
    bool queued_before = status_of() >= 0;
    int n = 0;
    for (int pos = 0; pos < size && pos < len; pos++)
    {
        tdc_put_data(&s_tape.target, SBUF_DATA, pos, buf[pos]);
        n++;
        if (!queued_before && status_of() >= 0 && pos < size - 1)
        {
            break;
        }
    }
    return n;
}

/* REQUEST SENSE with allocation length alloc into buf; returns bytes. */
static int request_sense(uint8_t *buf, uint8_t alloc)
{
    cdb6(TDC_REQUEST_SENSE, 0, 0, 0, alloc, 0);
    return data_in(buf, 32);
}

/* Extended sense of the last command: key, flags, info, error code, EXERCD. */
typedef struct
{
    uint8_t key;
    bool fm;
    bool eom;
    bool vadd;
    uint32_t info;
    uint8_t code;
    uint8_t exercd;
} Sense;

static Sense sense(void)
{
    uint8_t b[32];
    memset(b, 0, sizeof(b));
    request_sense(b, 25);
    Sense s;
    s.key = b[2] & 0x0f;
    s.fm = (b[2] & 0x80) != 0;
    s.eom = (b[2] & 0x40) != 0;
    s.vadd = (b[0] & 0x80) != 0;
    s.info = scsi_device_scsi_get_u32_be(&b[3]);
    s.code = b[14];
    s.exercd = b[15];
    return s;
}

/* A fresh drive on a fresh tape, with the power-on Unit Attention taken. */
static void fresh(size_t tape_bytes)
{
    blank_tape(tape_bytes);
    scsi_bus_init(&s_bus);
    scsi_tape_init(&s_tape, &s_bus, 1, &s_owner, 1);
    cdb6(TDC_TEST_UNIT_READY, 0, 0, 0, 0, 0);
    (void)sense();
}

static void fill_block(uint8_t *b, int seed)
{
    for (int i = 0; i < TDC_BLOCK_BYTES; i++)
    {
        b[i] = (uint8_t)(seed * 7 + i);
    }
}

/* WRITE n blocks, block k filled with fill_block(seed + k). */
static int write_blocks(int n, int seed)
{
    static uint8_t buf[64 * TDC_BLOCK_BYTES];
    for (int k = 0; k < n; k++)
    {
        fill_block(&buf[k * TDC_BLOCK_BYTES], seed + k);
    }
    cdb6(TDC_WRITE, 0x01, (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n, 0);
    if (phase_size(BC_DATA_OUT) > 0)
    {
        data_out(buf, n * TDC_BLOCK_BYTES);
    }
    return status_of();
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

/* Section 8.6.2: the first command after power-on gets CHECK CONDITION / UNIT
 * ATTENTION and is not performed; INQUIRY does not clear it (9.3.3); sense
 * key 6, error code 30, EXERCD 1C E$UATT (Appendix A). */
static void test_unit_attention(void)
{
    printf("unit attention\n");
    blank_tape(64 * 1024);
    scsi_bus_init(&s_bus);
    scsi_tape_init(&s_tape, &s_bus, 1, &s_owner, 1);

    uint8_t b[64];
    cdb6(TDC_INQUIRY, 0, 0, 0, 51, 0);
    CHECK(status_of() == SS_GOOD, "INQUIRY is GOOD with Unit Attention pending");
    CHECK(s_tape.unitAttention, "INQUIRY does not clear Unit Attention");

    cdb6(TDC_TEST_UNIT_READY, 0, 0, 0, 0, 0);
    CHECK(status_of() == SS_CHECK_CONDITION, "first TEST UNIT READY: CHECK CONDITION");
    Sense s = sense();
    CHECK(s.key == SK_UNIT_ATTENTION, "sense key UNIT ATTENTION");
    CHECK(s.code == 0x30 && s.exercd == 0x1C, "error code 30, EXERCD 1C");

    cdb6(TDC_TEST_UNIT_READY, 0, 0, 0, 0, 0);
    CHECK(status_of() == SS_GOOD, "second TEST UNIT READY: GOOD");

    /* A bus reset posts it again (section 8.6.2). */
    s_tape.target.scsi_bus_reset(&s_tape.target);
    cdb6(TDC_TEST_UNIT_READY, 0, 0, 0, 0, 0);
    CHECK(status_of() == SS_CHECK_CONDITION, "after a bus reset: CHECK CONDITION again");
    request_sense(b, 25);
    CHECK((b[2] & 0x0f) == SK_UNIT_ATTENTION, "after a bus reset: UNIT ATTENTION");
}

/* Section 9.3.3: 51 bytes, type 01, RMB, additional length 46, "TANDBERG",
 * " TDC 3600", "CREATED" at 38. */
static void test_inquiry(void)
{
    printf("inquiry\n");
    fresh(64 * 1024);
    uint8_t b[64];
    cdb6(TDC_INQUIRY, 0, 0, 0, 255, 0);
    int n = data_in(b, sizeof(b));
    CHECK(n == 51, "INQUIRY sends 51 bytes for an allocation of 255");
    CHECK(b[0] == 0x01 && b[1] == 0x80, "sequential access, removable");
    CHECK(b[4] == 46, "additional length 46");
    CHECK(memcmp(&b[8], "TANDBERG", 8) == 0, "vendor TANDBERG");
    CHECK(memcmp(&b[16], " TDC 3600", 9) == 0, "product ' TDC 3600'");
    CHECK(memcmp(&b[38], "CREATED", 7) == 0, "software identification CREATED");

    cdb6(TDC_INQUIRY, 0, 0, 0, 0, 0);
    CHECK(phase_size(BC_DATA_IN) == -1 && status_of() == SS_GOOD,
          "allocation 0: no data, GOOD");
}

/* Section 9.3.14: allocation 0 gives the 4-byte standard block; above the
 * normal sense threshold (default 0) the extended block, 25 bytes with
 * additional sense length 17. */
static void test_request_sense_formats(void)
{
    printf("request sense formats\n");
    fresh(64 * 1024);
    uint8_t b[32];

    /* A read on a blank tape: BLANK CHECK, code 34, residue 1. */
    cdb6(TDC_READ, 0x01, 0, 0, 1, 0);
    int n = request_sense(b, 0);
    CHECK(n == 4, "allocation 0: four bytes of standard sense");
    CHECK(b[0] == (0x80 | 0x34), "standard byte 0: VADD and error code 34");
    CHECK(b[1] == 0 && b[2] == 0 && b[3] == 1, "standard bytes 1-3: residue 1");

    cdb6(TDC_READ, 0x01, 0, 0, 1, 0);
    n = request_sense(b, 255);
    CHECK(n == 25, "extended sense is 25 bytes");
    CHECK(b[0] == 0xF0, "extended byte 0: VADD | 70h");
    CHECK(b[7] == 17, "additional sense length 17");
    CHECK((b[2] & 0x0f) == SK_BLANK_CHECK, "BLANK CHECK");
    CHECK(b[14] == 0x34 && b[15] == 0x17, "code 34, EXERCD 17 E$REOL");

    /* Sense data is cleared once read. */
    n = request_sense(b, 255);
    CHECK((b[2] & 0x0f) == SK_NO_SENSE && b[14] == 0, "second REQUEST SENSE: no sense");
}

/* Section 9.3.9: maximum and minimum block length both 200h. */
static void test_read_block_limits(void)
{
    printf("read block limits\n");
    fresh(64 * 1024);
    uint8_t b[8];
    cdb6(TDC_READ_BLOCK_LIMITS, 0, 0, 0, 0, 0);
    int n = data_in(b, sizeof(b));
    CHECK(n == 6, "six bytes");
    CHECK(b[0] == 0 && b[1] == 0 && b[2] == 0x02 && b[3] == 0x00, "maximum 000200h");
    CHECK(b[4] == 0x02 && b[5] == 0x00, "minimum 0200h");
}

/* Write, filemark, rewind, read back - and the SIMH bytes on the image. */
static void test_write_read_filemark(void)
{
    printf("write, filemark, read back\n");
    fresh(256 * 1024);
    uint8_t b[4 * TDC_BLOCK_BYTES];
    uint8_t want[TDC_BLOCK_BYTES];

    CHECK(write_blocks(3, 10) == SS_GOOD, "WRITE 3 blocks at BOT: GOOD");
    cdb6(TDC_WRITE_FILEMARK, 0, 0, 0, 1, 0);
    CHECK(status_of() == SS_GOOD, "WRITE FILE MARK 1: GOOD");

    /* SIMH: length, data, length; then 00000000 for the tape mark; then the
     * end-of-data marker written after the last write. */
    CHECK(le32(&s_img[0]) == 512 && le32(&s_img[4 + 512]) == 512, "record 1 framed by 512");
    fill_block(want, 10);
    CHECK(memcmp(&s_img[4], want, 512) == 0, "record 1 holds block 1's data");
    CHECK(le32(&s_img[3 * 520]) == 0, "tape mark after three records");
    CHECK(le32(&s_img[3 * 520 + 4]) == 0xffffffffu, "end of data after the tape mark");

    /* Section 9.3.8.1: a Read after a Write with no rewind is ILLEGAL
     * REQUEST, code 34 (E$RDWR 16). */
    cdb6(TDC_READ, 0x01, 0, 0, 1, 0);
    CHECK(status_of() == SS_CHECK_CONDITION, "READ right after a write: CHECK");
    Sense s = sense();
    CHECK(s.key == SK_ILLEGAL_REQUEST && s.code == 0x34 && s.exercd == 0x16,
          "ILLEGAL REQUEST, code 34, E$RDWR");

    cdb6(TDC_REWIND, 0, 0, 0, 0, 0);
    CHECK(status_of() == SS_GOOD, "REWIND: GOOD");

    cdb6(TDC_READ, 0x01, 0, 0, 2, 0);
    int n = data_in(b, sizeof(b));
    CHECK(n == 1024 && status_of() == SS_GOOD, "READ 2: 1024 bytes, GOOD");
    fill_block(want, 11);
    CHECK(memcmp(&b[512], want, 512) == 0, "second block read back");

    /* Section 9.3.8: a filemark before the count ends the read: CHECK, FM,
     * NO SENSE, VADD, residue; code 1C, E$FIMK 0B. */
    cdb6(TDC_READ, 0x01, 0, 0, 3, 0);
    n = data_in(b, sizeof(b));
    CHECK(n == 512, "READ 3 over the filemark: one block moved");
    CHECK(status_of() == SS_CHECK_CONDITION, "then CHECK CONDITION");
    s = sense();
    CHECK(s.key == SK_NO_SENSE && s.fm && s.vadd && s.info == 2, "NO SENSE, FM, residue 2");
    CHECK(s.code == 0x1C && s.exercd == 0x0B, "code 1C, E$FIMK");

    /* The tape is after the filemark, at the end of data: BLANK CHECK. */
    cdb6(TDC_READ, 0x01, 0, 0, 1, 0);
    CHECK(phase_size(BC_DATA_IN) == -1 && status_of() == SS_CHECK_CONDITION,
          "READ at end of data: no data, CHECK");
    s = sense();
    CHECK(s.key == SK_BLANK_CHECK && s.code == 0x34 && s.info == 1, "BLANK CHECK, 34, residue 1");

    /* The fixed bit must be set (sections 9.3.8 and 9.3.22). */
    cdb6(TDC_READ, 0x00, 0, 0, 1, 0);
    s = sense();
    CHECK(s.key == SK_ILLEGAL_REQUEST, "READ without the fixed bit: ILLEGAL REQUEST");
}

/* Section 9.3.22 error table: append error 33 when writing other than at BOT
 * or the end of recorded data (E$APPN 01). */
static void test_append_rule(void)
{
    printf("append rule\n");
    fresh(256 * 1024);
    CHECK(write_blocks(3, 0) == SS_GOOD, "WRITE 3: GOOD");
    cdb6(TDC_REWIND, 0, 0, 0, 0, 0);
    cdb6(TDC_SPACE, 0x00, 0, 0, 1, 0);
    CHECK(status_of() == SS_GOOD, "SPACE 1 block: GOOD");
    CHECK(write_blocks(1, 50) == SS_CHECK_CONDITION, "WRITE in the middle: CHECK");
    Sense s = sense();
    CHECK(s.key == SK_ILLEGAL_REQUEST && s.code == 0x33 && s.exercd == 0x01,
          "ILLEGAL REQUEST, code 33, E$APPN");

    cdb6(TDC_SPACE, 0x03, 0, 0, 0, 0);
    CHECK(status_of() == SS_GOOD, "SPACE to end of data: GOOD");
    CHECK(write_blocks(1, 60) == SS_GOOD, "WRITE at end of data: GOOD");

    /* Writing again at BOT records from the start; what was after is gone. */
    cdb6(TDC_REWIND, 0, 0, 0, 0, 0);
    CHECK(write_blocks(1, 70) == SS_GOOD, "WRITE at BOT over an old recording: GOOD");
    cdb6(TDC_REWIND, 0, 0, 0, 0, 0);
    cdb6(TDC_SPACE, 0x00, 0, 0, 2, 0);
    s = sense();
    CHECK(s.key == SK_BLANK_CHECK && s.info == 1, "only one block left on the tape");
}

/* Section 9.3.13: block addresses start at 1. Section 9.3.17: Seek Block. */
static void test_block_address_and_seek(void)
{
    printf("request block address, seek block\n");
    fresh(256 * 1024);
    uint8_t b[TDC_BLOCK_BYTES];
    uint8_t want[TDC_BLOCK_BYTES];

    cdb6(TDC_REQUEST_BLOCK_ADDR, 0, 0, 0, 0, 0);
    int n = data_in(b, sizeof(b));
    CHECK(n == 3, "allocation 0: three bytes");
    CHECK(b[0] == 0 && b[1] == 0 && b[2] == 1, "address 1 at BOT");

    write_blocks(4, 100);
    cdb6(TDC_REQUEST_BLOCK_ADDR, 0, 0, 0, 3, 0);
    data_in(b, sizeof(b));
    CHECK(b[2] == 5, "address 5 after four blocks");

    cdb6(TDC_SEEK_BLOCK, 0, 0, 0, 3, 0);
    CHECK(status_of() == SS_GOOD, "SEEK BLOCK 3: GOOD");
    cdb6(TDC_REWIND, 0, 0, 0, 0, 0); /* clears the write for the read below */
    cdb6(TDC_SEEK_BLOCK, 0, 0, 0, 3, 0);
    cdb6(TDC_READ, 0x01, 0, 0, 1, 0);
    data_in(b, sizeof(b));
    fill_block(want, 102);
    CHECK(memcmp(b, want, 512) == 0, "READ after SEEK BLOCK 3 gives the third block");

    cdb6(TDC_SEEK_BLOCK, 0, 0, 0, 0, 0);
    Sense s = sense();
    CHECK(s.key == SK_ILLEGAL_REQUEST && s.code == 0x20, "SEEK BLOCK 0: ILLEGAL REQUEST, 20");

    cdb6(TDC_SEEK_BLOCK, 0, 0, 0, 99, 0);
    s = sense();
    CHECK(s.key == SK_BLANK_CHECK && s.code == 0x14, "SEEK BLOCK 99: BLANK CHECK, 14");
}

/* Section 9.3.19: Space. */
static void test_space(void)
{
    printf("space\n");
    fresh(256 * 1024);
    write_blocks(2, 0);
    cdb6(TDC_WRITE_FILEMARK, 0, 0, 0, 1, 0);
    write_blocks(2, 10);
    cdb6(TDC_WRITE_FILEMARK, 0, 0, 0, 2, 0);
    cdb6(TDC_REWIND, 0, 0, 0, 0, 0);

    /* Over blocks into a filemark: FM, VADD, residue, after the mark. */
    cdb6(TDC_SPACE, 0x00, 0, 0, 5, 0);
    Sense s = sense();
    CHECK(s.fm && s.vadd && s.info == 3, "SPACE 5 blocks: FM after 2, residue 3");

    cdb6(TDC_SPACE, 0x01, 0, 0, 1, 0);
    CHECK(status_of() == SS_GOOD, "SPACE 1 filemark: GOOD");

    /* Reverse over 2 filemarks back to just before the first one. */
    cdb6(TDC_SPACE, 0x01, 0xff, 0xff, 0xfe, 0);
    CHECK(status_of() == SS_GOOD, "SPACE -2 filemarks: GOOD");
    cdb6(TDC_REQUEST_BLOCK_ADDR, 0, 0, 0, 3, 0);
    uint8_t b[4];
    data_in(b, sizeof(b));
    CHECK(b[2] == 3, "before the first filemark: address 3");

    /* Reverse into BOT: EOM bit, NO SENSE, residue. */
    cdb6(TDC_SPACE, 0x00, 0xff, 0xff, 0xf6, 0); /* -10 */
    s = sense();
    CHECK(s.key == SK_NO_SENSE && s.eom && s.vadd && s.info == 8, "SPACE -10 blocks: BOM, residue 8");

    /* Sequential filemarks: two in a row. */
    cdb6(TDC_SPACE, 0x02, 0, 0, 2, 0);
    CHECK(status_of() == SS_GOOD, "SPACE 2 sequential filemarks: GOOD");
    cdb6(TDC_REQUEST_BLOCK_ADDR, 0, 0, 0, 3, 0);
    data_in(b, sizeof(b));
    CHECK(b[2] == 8, "after the pair at the end: address 8");

    cdb6(TDC_SPACE, 0x02, 0xff, 0xff, 0xff, 0);
    s = sense();
    CHECK(s.key == SK_ILLEGAL_REQUEST, "SPACE sequential filemarks reverse: ILLEGAL REQUEST");
}

/* Appendix A: E$WPRO 1F, code 17, DATA PROTECT. */
static void test_write_protect(void)
{
    printf("write protect\n");
    fresh(64 * 1024);
    s_img_wp = true;
    s_tape.tape.read_only = true;
    CHECK(write_blocks(1, 0) == SS_CHECK_CONDITION, "WRITE on a protected cartridge: CHECK");
    Sense s = sense();
    CHECK(s.key == SK_DATA_PROTECT && s.code == 0x17 && s.exercd == 0x1F,
          "DATA PROTECT, code 17, E$WPRO");

    cdb6(TDC_MODE_SENSE, 0, 0, 0, 24, 0);
    uint8_t b[24];
    data_in(b, sizeof(b));
    CHECK((b[2] & 0x80) != 0, "MODE SENSE: WP bit set");
}

/* Section 9.3.22: past Pseudo Early Warning the write stops, CHECK, NO SENSE,
 * EOM, VADD, code 0A, residue; the host may then write on to the physical
 * end, where it is VOLUME OVERFLOW. */
static void test_end_of_tape(void)
{
    printf("end of tape\n");
    /* 220 KB tape: PEW (200 KB before the end) is 20 KB in. */
    fresh(220 * 1024);
    int st = write_blocks(64, 0);
    CHECK(st == SS_CHECK_CONDITION, "WRITE 64 blocks across PEW: CHECK");
    Sense s = sense();
    CHECK(s.key == SK_NO_SENSE && s.eom && s.vadd, "NO SENSE, EOM, VADD");
    CHECK(s.code == 0x0A && s.exercd == 0x1D, "code 0A, E$WEOW");
    /* 20 KB / 520 bytes a record: the 40th record crosses it. */
    CHECK(s.info == 64 - 40, "residue: 24 blocks not taken");

    /* Write on until the physical end. */
    int rounds = 0;
    do
    {
        st = write_blocks(64, 0);
        rounds++;
    } while (st == SS_GOOD && rounds < 20);
    CHECK(st == SS_CHECK_CONDITION, "writing on reaches the physical end");
    s = sense();
    CHECK(s.key == TDC_SK_VOLUME_OVERFLOW && s.eom && s.code == 0x0A && s.exercd == 0x1E,
          "VOLUME OVERFLOW, EOM, code 0A, E$WEOM");
    CHECK(s.info > 0 && s.info <= 64, "residue reported");
}

/* Section 9.3.2: ERASE needs LONG, erases the whole tape, ends at BOT. */
static void test_erase(void)
{
    printf("erase\n");
    fresh(64 * 1024);
    write_blocks(3, 0);
    cdb6(TDC_ERASE, 0x00, 0, 0, 0, 0);
    Sense s = sense();
    CHECK(s.key == SK_ILLEGAL_REQUEST, "ERASE without LONG: ILLEGAL REQUEST");

    cdb6(TDC_ERASE, 0x01, 0, 0, 0, 0);
    CHECK(status_of() == SS_GOOD, "ERASE LONG: GOOD");
    bool all_ff = true;
    for (size_t i = 0; i < s_img_size; i++)
    {
        if (s_img[i] != 0xff)
        {
            all_ff = false;
            break;
        }
    }
    CHECK(all_ff, "the whole image is FF afterwards");
    CHECK(tape_image_is_bot(&s_tape.tape), "at BOT afterwards");
}

/* Sections 9.3.5 and 9.3.6: the 24-byte parameter list. */
static void test_mode_select_sense(void)
{
    printf("mode select, mode sense\n");
    fresh(64 * 1024);
    uint8_t b[32];

    cdb6(TDC_MODE_SENSE, 0, 0, 0, 255, 0);
    int n = data_in(b, sizeof(b));
    CHECK(n == 24, "MODE SENSE sends the 24-byte list");
    CHECK(b[0] == 23, "sense data length 23");
    CHECK(b[2] == 0x10, "BM set by default, not protected");
    CHECK(b[3] == 8, "block descriptor length 8");
    CHECK(b[10] == 0x02 && b[11] == 0x00, "block size 512");
    CHECK(b[12] == 0x00 && b[13] == 2 && b[14] == 2 && b[15] == 120, "vendor defaults");
    CHECK(b[18] == 8 && b[19] == 8 && b[20] == 0 && b[21] == 4, "vendor defaults, continued");

    /* Density 05 (QIC-24) is not valid for the TDC 3640: bad parameter. */
    uint8_t list[24];
    memcpy(list, b, 24);
    list[0] = 0;
    list[2] = 0x10;
    list[4] = 0x05;
    cdb6(TDC_MODE_SELECT, 0, 0, 0, 24, 0);
    data_out(list, 24);
    CHECK(status_of() == SS_CHECK_CONDITION, "MODE SELECT density 05: CHECK");
    Sense s = sense();
    CHECK(s.key == SK_ILLEGAL_REQUEST && s.exercd == 0x03, "ILLEGAL REQUEST, E$BADP");

    /* Normal sense threshold 4: an allocation of 4 now gets standard sense. */
    list[4] = 0x0F;
    list[12 + TDC_VENDOR_NORMAL_SENSE_THRESHOLD] = 4;
    list[2] = 0x00;
    cdb6(TDC_MODE_SELECT, 0, 0, 0, 24, 0);
    data_out(list, 24);
    CHECK(status_of() == SS_GOOD, "MODE SELECT density 0F, threshold 4: GOOD");
    cdb6(TDC_MODE_SENSE, 0, 0, 0, 24, 0);
    data_in(b, sizeof(b));
    CHECK(b[2] == 0x00 && b[4] == 0x0F && b[20] == 4, "the new values read back");
    cdb6(TDC_READ, 0x01, 0, 0, 1, 0);
    n = request_sense(b, 4);
    CHECK(n == 4 && b[0] == (0x80 | 0x34), "allocation 4 <= threshold: standard sense");

    /* Not saved: a reset brings the defaults back. */
    s_tape.target.scsi_bus_reset(&s_tape.target);
    cdb6(TDC_TEST_UNIT_READY, 0, 0, 0, 0, 0);
    (void)sense();
    cdb6(TDC_MODE_SENSE, 0, 0, 0, 24, 0);
    data_in(b, sizeof(b));
    CHECK(b[2] == 0x10 && b[4] == 0x00 && b[20] == 0, "after reset: defaults again");
}

/* Section 9.3.4 and Appendix A E$NLOD / E$NCAR. */
static void test_load_unload(void)
{
    printf("load, unload, no cartridge\n");
    fresh(64 * 1024);
    cdb6(TDC_LOAD_UNLOAD, 0, 0, 0, 0x00, 0);
    CHECK(status_of() == SS_GOOD, "UNLOAD: GOOD");
    cdb6(TDC_TEST_UNIT_READY, 0, 0, 0, 0, 0);
    Sense s = sense();
    CHECK(s.key == SK_NOT_READY && s.code == 0x09 && s.exercd == 0x13,
          "unloaded: NOT READY, 09, E$NLOD");
    cdb6(TDC_LOAD_UNLOAD, 0, 0, 0, 0x01, 0);
    cdb6(TDC_TEST_UNIT_READY, 0, 0, 0, 0, 0);
    CHECK(status_of() == SS_GOOD, "LOAD, then TEST UNIT READY: GOOD");

    s_img_mounted = false;
    cdb6(TDC_TEST_UNIT_READY, 0, 0, 0, 0, 0);
    s = sense();
    CHECK(s.key == SK_NOT_READY && s.exercd == 0x12, "no cartridge: NOT READY, E$NCAR");

    /* A cartridge put in: Unit Attention first. */
    s_img_mounted = true;
    cdb6(TDC_TEST_UNIT_READY, 0, 0, 0, 0, 0);
    s = sense();
    CHECK(s.key == SK_UNIT_ATTENTION, "cartridge in: UNIT ATTENTION");
}

/* Section 9.3.18: SEND DIAGNOSTIC. Selftest 2 (STest + both offline bits) is
 * "no operation" on the TDC 3600 per ND-210523G 3.5; that is what SCSI-TV
 * device test 3 sends (1D 07 00 00 00 00). It must not move the tape. */
static void test_send_diagnostic(void)
{
    printf("send diagnostic\n");
    fresh(64 * 1024);
    cdb6(TDC_SEND_DIAGNOSTIC, 0x04, 0, 0, 0, 0);
    CHECK(status_of() == SS_GOOD, "Selftest 1: GOOD");

    /* Move off the start so a hidden rewind would show. */
    cdb6(TDC_WRITE_FILEMARK, 0, 0, 0, 1, 0);
    cdb6(TDC_SEND_DIAGNOSTIC, 0x07, 0, 0, 0, 0);
    CHECK(status_of() == SS_GOOD, "Selftest 2 (SCSI-TV test 3, 1D 07): GOOD, no operation");
    cdb6(TDC_WRITE_FILEMARK, 0, 0, 0, 1, 0);
    cdb6(TDC_REWIND, 0, 0, 0, 0, 0);
    cdb6(TDC_SPACE, 0x01, 0, 0, 2, 0);
    CHECK(status_of() == SS_GOOD, "two filemarks after Selftest 2: tape did not move");

    cdb6(TDC_SEND_DIAGNOSTIC, 0x05, 0, 0, 0, 0);
    CHECK(status_of() == SS_GOOD, "Selftest 1 with one offline bit: GOOD");
    cdb6(TDC_SEND_DIAGNOSTIC, 0x04, 0, 0, 8, 0);
    CHECK(sense().key == SK_ILLEGAL_REQUEST, "self test with a parameter list: ILLEGAL");
    cdb6(TDC_SEND_DIAGNOSTIC, 0x00, 0, 0, 0, 0);
    CHECK(sense().key == SK_ILLEGAL_REQUEST, "no STest and no offline bits: ILLEGAL");
}

/* Section 9.3.21: Verify with byte compare. */
static void test_verify(void)
{
    printf("verify\n");
    /* Bigger than the 200 KB Pseudo Early Warning zone, so the writes below
     * are not stopped by it. */
    fresh(256 * 1024);
    write_blocks(2, 5);
    cdb6(TDC_REWIND, 0, 0, 0, 0, 0);

    uint8_t buf[2 * TDC_BLOCK_BYTES];
    fill_block(buf, 5);
    fill_block(&buf[512], 6);
    cdb6(TDC_VERIFY, 0x03, 0, 0, 2, 0);
    data_out(buf, sizeof(buf));
    CHECK(status_of() == SS_GOOD, "VERIFY BC of the same data: GOOD");

    cdb6(TDC_REWIND, 0, 0, 0, 0, 0);
    buf[600] ^= 0xff;
    cdb6(TDC_VERIFY, 0x03, 0, 0, 2, 0);
    data_out(buf, sizeof(buf));
    Sense s = sense();
    CHECK(s.key == TDC_SK_MISCOMPARE && s.code == 0x1D && s.exercd == 0x08 && s.info == 1,
          "a changed byte in block 2: MISCOMPARE, 1D, E$COMP, 1 unverified");

    cdb6(TDC_REWIND, 0, 0, 0, 0, 0);
    cdb6(TDC_VERIFY, 0x01, 0, 0, 3, 0);
    s = sense();
    CHECK(s.key == SK_BLANK_CHECK && s.info == 1, "VERIFY 3 without BC: end of data, residue 1");
}

/* A ten-byte CDB with byte 1 (mode) and the three-byte length in bytes 6-8,
 * the SCSI-2 READ BUFFER / WRITE BUFFER layout. */
static void cdb10_buffer(uint8_t op, uint8_t mode, uint32_t len)
{
    SCSITarget *t = &s_tape.target;
    uint8_t cdb[10] = {op, mode, 0, 0, 0, 0, (uint8_t)(len >> 16), (uint8_t)(len >> 8), (uint8_t)len, 0};
    for (int i = 0; i < 10; i++)
    {
        tdc_put_data(t, SBUF_MAIN, i, cdb[i]);
    }
    t->scsi_cmdsize = 10;
    t->buf_control_rpos = 0;
    t->buf_control_wpos = 0;
    tdc_command(t);
}

/* READ BUFFER 3Ch and WRITE BUFFER 3Bh, the standard SCSI commands (T10 list,
 * SCSI-2 7.2.12 / 7.2.17), with the data layout of Tandberg sections 9.3.10
 * and 9.3.23. */
static void test_data_buffer(void)
{
    printf("read and write data buffer\n");
    fresh(64 * 1024);

    CHECK(TDC_WRITE_DATA_BUFFER == 0x3B && TDC_READ_DATA_BUFFER == 0x3C,
          "standard opcodes: WRITE BUFFER 3Bh, READ BUFFER 3Ch");

    /* SCSI-TV device test 4 asks for the four-byte header first. */
    uint8_t hdr[4];
    cdb10(TDC_READ_DATA_BUFFER, 4);
    CHECK(phase_size(BC_DATA_IN) == 4, "READ BUFFER 4: a DATA IN of 4 bytes");
    CHECK(data_in(hdr, 4) == 4 && hdr[0] == 0 && hdr[1] == 0 && hdr[2] == 0xFF && hdr[3] == 0xFB,
          "header: available length 65531");
    CHECK(status_of() == SS_GOOD, "GOOD");

    uint8_t out[4 + 100];
    memset(out, 0, 4);
    for (int i = 0; i < 100; i++)
    {
        out[4 + i] = (uint8_t)(0xA0 + i);
    }
    cdb10(TDC_WRITE_DATA_BUFFER, sizeof(out));
    CHECK(phase_size(BC_DATA_OUT) == (int)sizeof(out), "WRITE DATA BUFFER takes 104 bytes");
    data_out(out, sizeof(out));
    CHECK(status_of() == SS_GOOD, "GOOD");

    uint8_t in[4 + 100];
    cdb10(TDC_READ_DATA_BUFFER, sizeof(in));
    int n = data_in(in, sizeof(in));
    CHECK(n == (int)sizeof(in), "READ DATA BUFFER sends the allocation length");
    CHECK(in[0] == 0 && in[1] == 0 && in[2] == 0xFF && in[3] == 0xFB, "available length 65531");
    CHECK(memcmp(&in[4], &out[4], 100) == 0, "the data written comes back");

    /* The largest length the CDB can hold, FFFFh, is exactly header plus
     * 65531, so it is taken. */
    cdb10(TDC_WRITE_DATA_BUFFER, 0xFFFF);
    CHECK(phase_size(BC_DATA_OUT) == 0xFFFF, "WRITE DATA BUFFER FFFFh: all of it taken");

    /* The length has three bytes; 10000h is more than header + 65531. */
    cdb10_buffer(TDC_WRITE_DATA_BUFFER, 0, 0x10000);
    CHECK(status_of() == SS_CHECK_CONDITION && sense().key == SK_ILLEGAL_REQUEST,
          "WRITE BUFFER 10000h: ILLEGAL REQUEST");
    cdb10_buffer(TDC_READ_DATA_BUFFER, 0, 0x10000);
    CHECK(phase_size(BC_DATA_IN) == 4 + 65531, "READ BUFFER 10000h: cut to header + 65531");

    /* Only mode 000b (header and data) is known for this drive. */
    cdb10_buffer(TDC_WRITE_DATA_BUFFER, 0x05, 4);
    CHECK(status_of() == SS_CHECK_CONDITION && sense().key == SK_ILLEGAL_REQUEST,
          "WRITE BUFFER mode 5 (download microcode): ILLEGAL REQUEST");
    cdb10_buffer(TDC_READ_DATA_BUFFER, 0x02, 4);
    CHECK(status_of() == SS_CHECK_CONDITION && sense().key == SK_ILLEGAL_REQUEST,
          "READ BUFFER mode 2 (data only): ILLEGAL REQUEST");
}

/* scsi_device_end_data_phase: ends the data phase after the byte on the bus,
 * keeping the handshake substate. */
static void test_end_data_phase(void)
{
    printf("end data phase early\n");
    SCSITarget t;
    memset(&t, 0, sizeof(t));
    t.scsi_state = TS_TARGET_WAIT_DATA_OUT_BYTE | TS_RECV_BYTE_T_WAIT_ACK_1;
    scsi_device_end_data_phase(&t);
    CHECK(t.scsi_state == (TS_TARGET_NEXT_CONTROL | TS_RECV_BYTE_T_WAIT_ACK_1),
          "DATA OUT: next control, substate kept");
    t.scsi_state = TS_TARGET_WAIT_DATA_IN_BYTE;
    scsi_device_end_data_phase(&t);
    CHECK(t.scsi_state == TS_TARGET_NEXT_CONTROL, "DATA IN: next control");
    t.scsi_state = TS_TARGET_WAIT_CMD_BYTE;
    scsi_device_end_data_phase(&t);
    CHECK(t.scsi_state == TS_TARGET_WAIT_CMD_BYTE, "not in a data phase: left alone");
}

int main(void)
{
    test_unit_attention();
    test_inquiry();
    test_request_sense_formats();
    test_read_block_limits();
    test_write_read_filemark();
    test_append_rule();
    test_block_address_and_seek();
    test_space();
    test_write_protect();
    test_end_of_tape();
    test_erase();
    test_mode_select_sense();
    test_load_unload();
    test_send_diagnostic();
    test_verify();
    test_data_buffer();
    test_end_data_phase();

    printf("%d checks, %d failed\n", s_checks, s_failed);
    return s_failed == 0 ? 0 : 1;
}
