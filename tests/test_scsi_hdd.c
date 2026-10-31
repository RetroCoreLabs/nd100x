/*
 * test_scsi_hdd.c - the SCSI hard disk target's READ BUFFER and WRITE BUFFER,
 * and its mode pages (MODE SENSE, MODE SELECT, FORMAT UNIT, reset; see
 * test_mode_pages for the sources).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * SCSI-TV-B00 (TPE floppy 210523G02-XX-02D) device tests 4 "SCSI bus data
 * transport" and 5 "SCSI bus stress" send READ BUFFER (3Ch) in combined header
 * and data mode to learn the drive buffer length, then write and read back
 * test patterns with WRITE BUFFER (3Bh). The target answered both with
 * ILLEGAL REQUEST (the former BUGS.md C4).
 *
 * What is checked, and where it comes from:
 *   - the four-byte READ BUFFER header: a reserved byte and the 24-bit buffer
 *     capacity (SCSI-2 X3T9.2/86-109 section 8.2.12.1, table 60);
 *   - the capacity, 16384: the Micropolis 1370 series "16-kilobyte dual-ported
 *     data buffer" (product description 101689 rev D, section 1.1);
 *   - READ BUFFER stops at the allocation length or at the end of header plus
 *     buffer, whichever is less (8.2.12.1);
 *   - WRITE BUFFER stores parameter list length minus four bytes after a
 *     four-byte reserved header, and a length past four plus the capacity is
 *     CHECK CONDITION, ILLEGAL REQUEST (8.2.17.1);
 *   - any mode other than 000b is refused (see the comment at SC_READ_BUFFER).
 *
 * scsi_hdd.c is INCLUDED rather than linked, so the target's command decoder
 * and its byte fetch and store can be called directly, the way the SCSI bus
 * calls them, without a controller, a bus phase engine or a disk image. The
 * buffer commands never touch the medium, so no disk is attached.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "scsi/scsi_hdd.c"

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

/* Send a ten-byte CDB the way the COMMAND phase does: byte by byte through the
 * target's own store hook into SBUF_MAIN (scsi_device.c, TS_TARGET_WAIT_CMD_BYTE
 * -> scsi_target_put_data), then run the decoder with an empty phase queue.
 * Writing scsi_cmdbuf directly would miss a store hook that diverts CDB bytes,
 * which is exactly how bytes 1-9 of WRITE BUFFER once went into the drive
 * buffer and the CDB kept the previous command's length. */
static void run_cdb10(SCSIHDDDevice *hdd, uint8_t op, uint8_t mode, uint32_t length)
{
    SCSITarget *t = &hdd->target;
    uint8_t cdb[10] = {op, mode, 0, 0, 0, 0, (uint8_t)(length >> 16), (uint8_t)(length >> 8),
                       (uint8_t)length, 0};
    for (int i = 0; i < 10; i++)
    {
        scsihdd_put_data(t, SBUF_MAIN, i, cdb[i]);
    }
    t->scsi_cmdsize = 10;
    t->buf_control_rpos = 0;
    t->buf_control_wpos = 0;
    scsihdd_command(t);
}

/* The same for a six-byte CDB (MODE SENSE, MODE SELECT, FORMAT UNIT). */
static void run_cdb6(SCSIHDDDevice *hdd, uint8_t op, uint8_t b1, uint8_t b2, uint8_t b4)
{
    SCSITarget *t = &hdd->target;
    uint8_t cdb[6] = {op, b1, b2, 0, b4, 0};
    for (int i = 0; i < 6; i++)
    {
        scsihdd_put_data(t, SBUF_MAIN, i, cdb[i]);
    }
    t->scsi_cmdsize = 6;
    t->buf_control_rpos = 0;
    t->buf_control_wpos = 0;
    scsihdd_command(t);
}

/* MODE SENSE(6) of one page (or 3Fh) with page control pc; the reply is left in
 * scsi_cmdbuf, the SBUF_MAIN data the DATA IN phase sends. */
static const uint8_t *mode_sense(SCSIHDDDevice *hdd, int pc, int page)
{
    run_cdb6(hdd, SC_MODE_SENSE_6, 0, (uint8_t)((pc << 6) | page), 0xFF);
    return hdd->target.scsi_cmdbuf;
}

/* MODE SELECT(6) of a parameter list: CDB, then the DATA OUT bytes through the
 * target's own store hook. */
static void mode_select(SCSIHDDDevice *hdd, const uint8_t *list, int len)
{
    run_cdb6(hdd, SC_MODE_SELECT_6, 0, 0, (uint8_t)len);
    for (int i = 0; i < len; i++)
    {
        scsihdd_put_data(&hdd->target, SBUF_DATA, i, list[i]);
    }
}

/*
 * Mode pages, current / changeable / default / saved (BUGS.md C5).
 *
 * Sources: Micropolis 1370 Series product description 101689 rev D,
 * Appendix B (four pages, their lengths, defaults and CHANGEABLE column), and
 * DISK-MM-B00 FORMAT-INITIALIZE as traced in nd100x: it reads MODE SENSE page
 * 3Fh and takes the sectors per track from byte 42 and the heads from byte 61,
 * then MODE SELECTs pages 3 and 4 with new skews, formats, and compares the
 * saved values (page control 3) with the default ones (page control 2).
 */
static void test_mode_pages(void)
{
    static SCSIBus bus;
    static SCSIHDDDevice hdd;
    scsi_bus_init(&bus);
    scsi_hdd_init(&hdd, &bus, 0, NULL, 0, SCSI_DISK_MICROPOLIS_1375_ND);
    SCSITarget *t = &hdd.target;
    const uint8_t *r;

    /* ---- Page 3Fh: pages 1, 2, 3, 4 in that order, with the 1370 lengths ---- */
    r = mode_sense(&hdd, 0, 0x3F);
    CHECK(t->buf_control[0].action == BC_DATA_IN, "MODE SENSE 3Fh: a DATA IN phase");
    CHECK(r[3] == 8, "block descriptor length 8");
    CHECK(r[12] == 0x01 && r[13] == 0x06, "page 1 at byte 12, length 06h");
    CHECK(r[20] == 0x02 && r[21] == 0x0A, "page 2 at byte 20, length 0Ah");
    CHECK(r[32] == 0x03 && r[33] == 0x16, "page 3 at byte 32, length 16h");
    CHECK(r[56] == 0x04 && r[57] == 0x12, "page 4 at byte 56, length 12h");
    CHECK(t->buf_control[0].param2 == 76, "MODE SENSE 3Fh: 4 + 8 + 8 + 12 + 24 + 20 bytes");
    CHECK(r[0] == 76, "mode data length byte as the C# sets it");

    /* The two fixed offsets DISK-MM reads (FORMAT-INITIALIZE, partition entry 7). */
    CHECK(((r[42] << 8) | r[43]) == 18, "byte 42: page 3 sectors per track, 18");
    CHECK(r[61] == 8, "byte 61: page 4 heads, 8");

    /* Appendix B defaults of pages 1 and 2. */
    CHECK(r[14] == 0x20 && r[15] == 0x0A && r[16] == 0x0B,
          "page 1 defaults: TB 1, retry count 10, correction span 11");
    CHECK(r[24] == 0x00 && r[25] == 0x05, "page 2 default bus inactivity limit 5");

    /* A page the drive does not have. */
    run_cdb6(&hdd, SC_MODE_SENSE_6, 0, 0x08, 0xFF);
    CHECK(t->buf_control[0].action == BC_STATUS && t->buf_control[0].param1 == SS_CHECK_CONDITION,
          "MODE SENSE page 08h: CHECK CONDITION");
    CHECK((t->scsi_sense_buffer[2] & 0x0F) == SK_ILLEGAL_REQUEST && t->scsi_sense_buffer[12] == 0x24,
          "page 08h: ILLEGAL REQUEST, invalid field in CDB");

    /* ---- Changeable values (page control 1) ---- */
    r = mode_sense(&hdd, 1, 0x03);
    CHECK(r[12] == 0x03 && r[13] == 0x16, "changeable page 3 keeps code and length");
    CHECK(r[14] == 0 && r[15] == 0, "page 3 tracks per zone is not changeable");
    CHECK(r[22] == 0xFF && r[23] == 0xFF, "page 3 sectors per track is changeable");
    CHECK(r[28] == 0xFF && r[31] == 0xFF, "page 3 track and cylinder skew are changeable");
    CHECK(r[26] == 0 && r[27] == 0, "page 3 interleave is not changeable");
    r = mode_sense(&hdd, 1, 0x01);
    CHECK(r[14] == 0x3F, "page 1: TB, RC, EER, PER, DTE, DCR changeable; AWRE, ARRE not");

    /* ---- MODE SELECT changes the current values, not the saved ones ---- */
    /* DISK-MM's page 3 list, as logged on 2026-10-31: track skew 0, cylinder
     * skew 6, and an interleave of 1 that the drive must not take. */
    static const uint8_t page3_list[] = {
        0x00, 0x00, 0x00, 0x08, 0x00, 0x01, 0xF9, 0x1F, 0x00, 0x00, 0x04, 0x00,
        0x03, 0x16, 0x1C, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12,
        0x04, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t before[SCSI_HDD_MAX_SECTOR_BYTES];
    memcpy(before, hdd.sectorData, sizeof(before));
    mode_select(&hdd, page3_list, (int)sizeof(page3_list));
    CHECK(memcmp(before, hdd.sectorData, sizeof(before)) == 0,
          "MODE SELECT data does not land in the sector buffer");

    r = mode_sense(&hdd, 0, 0x03);
    CHECK(((r[30] << 8) | r[31]) == 6, "current page 3 cylinder skew is now 6");
    CHECK(r[26] == 0 && r[27] == 0, "current page 3 interleave stays (not changeable)");
    CHECK(r[14] == 0x1C && r[15] == 0x10, "current tracks per zone stays (not changeable)");
    r = mode_sense(&hdd, 3, 0x03);
    CHECK(((r[30] << 8) | r[31]) == 0, "saved page 3 cylinder skew still 0 before FORMAT UNIT");
    r = mode_sense(&hdd, 2, 0x03);
    CHECK(((r[30] << 8) | r[31]) == 0, "default page 3 cylinder skew is never changed");

    /* A page 1 MODE SELECT is kept too, only in its changeable bits. */
    static const uint8_t page1_list[] = {0x00, 0x00, 0x00, 0x00, 0x01, 0x06,
                                         0xFF, 0x03, 0x05, 0x77, 0x00, 0x00};
    mode_select(&hdd, page1_list, (int)sizeof(page1_list));
    r = mode_sense(&hdd, 0, 0x01);
    CHECK(r[14] == 0x3F, "page 1 byte 2: the changeable bits only");
    CHECK(r[15] == 0x03 && r[16] == 0x05, "page 1 retry count and correction span taken");
    CHECK(r[17] == 0x00, "page 1 head offset count is not changeable");

    /* ---- FORMAT UNIT keeps the current values as the saved ones ---- */
    run_cdb6(&hdd, SC_FORMAT_UNIT, 0x10, 0, 0);
    CHECK(t->buf_control[0].action == BC_DATA_OUT && t->buf_control[0].param2 == 4,
          "FORMAT UNIT with FmtData: a four-byte defect list header");
    for (int i = 0; i < 4; i++)
    {
        scsihdd_put_data(t, SBUF_DATA, i, 0);
    }
    r = mode_sense(&hdd, 3, 0x03);
    CHECK(((r[30] << 8) | r[31]) == 6, "saved page 3 cylinder skew 6 after FORMAT UNIT");

    /* ---- A reset loads the saved values into the current ones ---- */
    mode_select(&hdd, page3_list, 12); /* header and block descriptor only: no change */
    static const uint8_t skew9[] = {0x00, 0x00, 0x00, 0x00, 0x03, 0x16, 0, 0, 0, 0, 0, 0, 0, 0,
                                    0x00, 0x12, 0x04, 0x00, 0, 0, 0, 0, 0x00, 0x09, 0, 0, 0, 0};
    mode_select(&hdd, skew9, (int)sizeof(skew9));
    r = mode_sense(&hdd, 0, 0x03);
    CHECK(((r[30] << 8) | r[31]) == 9, "current cylinder skew 9 before the reset");
    scsihdd_device_reset(&hdd);
    r = mode_sense(&hdd, 0, 0x03);
    CHECK(t->buf_control[0].action == BC_DATA_IN, "MODE SENSE after the reset sends data");
    CHECK(((r[30] << 8) | r[31]) == 6, "after a reset the current skew is the saved 6 again");
}

int main(void)
{
    printf("SCSI hard disk mode page and READ BUFFER / WRITE BUFFER tests\n");
    printf("==============================================================\n\n");

    test_mode_pages();

    static SCSIBus bus;
    static SCSIHDDDevice hdd;
    scsi_bus_init(&bus);
    scsi_hdd_init(&hdd, &bus, 0, NULL, 0, SCSI_DISK_MICROPOLIS_1375_ND);
    SCSITarget *t = &hdd.target;

    /* ---- READ BUFFER, header only: the capacity ---- */
    run_cdb10(&hdd, SC_READ_BUFFER, 0, 4);
    CHECK(t->buf_control_wpos == 4, "READ BUFFER queues data in, status, message, bus free");
    CHECK(t->buf_control[0].action == BC_DATA_IN, "READ BUFFER: a DATA IN phase");
    CHECK(t->buf_control[0].param2 == 4, "READ BUFFER, allocation 4: four bytes");
    CHECK(t->buf_control[1].action == BC_STATUS && t->buf_control[1].param1 == SS_GOOD,
          "READ BUFFER: GOOD status");
    CHECK(scsihdd_get_data(t, SBUF_DATA, 0) == 0x00, "header byte 0 is reserved, 0");
    CHECK(scsihdd_get_data(t, SBUF_DATA, 1) == 0x00, "capacity MSB");
    CHECK(scsihdd_get_data(t, SBUF_DATA, 2) == 0x40, "capacity middle byte: 16384 = 004000h");
    CHECK(scsihdd_get_data(t, SBUF_DATA, 3) == 0x00, "capacity LSB");

    /* ---- WRITE BUFFER: header plus eight bytes into the drive buffer ---- */
    run_cdb10(&hdd, SC_WRITE_BUFFER, 0, 12);
    CHECK(t->buf_control[0].action == BC_DATA_OUT, "WRITE BUFFER: a DATA OUT phase");
    CHECK(t->buf_control[0].param2 == 12, "WRITE BUFFER: parameter list length bytes");
    CHECK(t->buf_control[1].action == BC_STATUS && t->buf_control[1].param1 == SS_GOOD,
          "WRITE BUFFER: GOOD status");
    for (int i = 0; i < 4; i++)
    {
        scsihdd_put_data(t, SBUF_DATA, i, 0xEE); /* reserved header bytes */
    }
    for (int i = 0; i < 8; i++)
    {
        scsihdd_put_data(t, SBUF_DATA, 4 + i, (uint8_t)(0x11 * (i + 1)));
    }

    /* ---- READ BUFFER gives the header, then the bytes written ---- */
    run_cdb10(&hdd, SC_READ_BUFFER, 0, 12);
    CHECK(t->buf_control[0].param2 == 12, "READ BUFFER, allocation 12: twelve bytes");
    CHECK(scsihdd_get_data(t, SBUF_DATA, 2) == 0x40, "the header comes first again");
    bool same = true;
    for (int i = 0; i < 8; i++)
    {
        if (scsihdd_get_data(t, SBUF_DATA, 4 + i) != (uint8_t)(0x11 * (i + 1)))
        {
            same = false;
        }
    }
    CHECK(same, "READ BUFFER returns what WRITE BUFFER stored, header bytes not stored");

    /* A large allocation is cut at header plus buffer. */
    run_cdb10(&hdd, SC_READ_BUFFER, 0, 0x100000);
    CHECK(t->buf_control[0].param2 == 4 + 16384, "READ BUFFER stops at header plus capacity");

    /* The whole buffer can be written; one byte more is refused. */
    run_cdb10(&hdd, SC_WRITE_BUFFER, 0, 4 + 16384);
    CHECK(t->buf_control[0].action == BC_DATA_OUT && t->buf_control[0].param2 == 4 + 16384,
          "WRITE BUFFER of header plus the full capacity is accepted");
    scsihdd_put_data(t, SBUF_DATA, 4 + 16383, 0x5A);
    run_cdb10(&hdd, SC_READ_BUFFER, 0, 4 + 16384);
    CHECK(scsihdd_get_data(t, SBUF_DATA, 4 + 16383) == 0x5A, "the last buffer byte round-trips");

    run_cdb10(&hdd, SC_WRITE_BUFFER, 0, 4 + 16384 + 1);
    CHECK(t->buf_control[0].action == BC_STATUS && t->buf_control[0].param1 == SS_CHECK_CONDITION,
          "WRITE BUFFER past the capacity: CHECK CONDITION, no data phase");
    CHECK((t->scsi_sense_buffer[2] & 0x0F) == SK_ILLEGAL_REQUEST, "with sense key ILLEGAL REQUEST");
    CHECK(t->scsi_sense_buffer[12] == 0x24, "and additional sense code 24h");

    /* Modes other than 000b are refused, in both commands. */
    run_cdb10(&hdd, SC_READ_BUFFER, 2, 16);
    CHECK(t->buf_control[0].action == BC_STATUS && t->buf_control[0].param1 == SS_CHECK_CONDITION,
          "READ BUFFER data mode (010b) is refused");
    run_cdb10(&hdd, SC_WRITE_BUFFER, 4, 16);
    CHECK(t->buf_control[0].action == BC_STATUS && t->buf_control[0].param1 == SS_CHECK_CONDITION,
          "WRITE BUFFER download microcode mode (100b) is refused");

    printf("\n%d checks, %d failed\n", s_checks, s_failed);
    return s_failed == 0 ? 0 : 1;
}
