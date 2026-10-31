/*
 * test_scsi_hdd.c - the SCSI hard disk target's READ BUFFER and WRITE BUFFER.
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

int main(void)
{
    printf("SCSI hard disk READ BUFFER / WRITE BUFFER tests\n");
    printf("===============================================\n\n");

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
