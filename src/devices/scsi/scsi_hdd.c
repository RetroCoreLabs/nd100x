/*
 * nd100x - ND-100 emulator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * scsi_hdd.c - SCSI hard disk target (CDB decode + block I/O)
 *
 * Ported from RetroCore:
 *   <RetroCore>\Emulated.HW\Common\SCSI\SCSIHDD.cs
 *   <RetroCore>\Emulated.HW\Common\SCSI\SCSIHDDMicropolis.cs
 *
 * ALL multi-byte SCSI fields are big-endian - use the scsi_get/put_*be helpers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#include "../devices_types.h"
#include "../devices_protos.h"


static void scsihdd_log(SCSIHDDDevice *hdd, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void scsihdd_log(SCSIHDDDevice *hdd, const char *fmt, ...)
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
    Log_Write(LOG_CAT_SCSI, LOG_DEBUG, "HDD%d: %s", hdd->unit, msg);
}


/* Is an image mounted on this unit? Uses the machine's disk-info callback,
 * which fails for an unmounted drive (RetroCore's IsMountedCallback). */
static bool scsihdd_has_media(SCSIHDDDevice *hdd)
{
    if (!hdd->owner || !hdd->owner->blockCallbacks.diskInfoFunc)
    {
        return false;
    }

    size_t image_size = 0;
    bool is_write_protected = false;
    int rc = hdd->owner->blockCallbacks.diskInfoFunc(hdd->owner, &image_size, &is_write_protected,
                                                     hdd->unit);
    return (rc >= 0) && (image_size > 0);
}


/*
 * LUN from the IDENTIFY message, falling back to the CDB's LUN field.
 * (SCSIFullDevice.cs get_lun)
 */
static int scsihdd_get_lun(SCSIHDDDevice *hdd, int def)
{
    if (hdd->target.scsi_identify & 0x80)
    {
        return hdd->target.scsi_identify & 0x07;
    }
    return def;
}


/*
 * Read one block into sectorData.
 *
 * The machine block callback takes a count in BLOCKS and an LBA, and uses
 * owner->blockSizeBytes for the byte maths - so this is the exact equivalent of
 * RetroCore's readBlock(), which computes location = lba * sectorbytes itself.
 */
static bool scsihdd_read_block(SCSIHDDDevice *hdd, int read_lba)
{
    if (!hdd->owner || !hdd->owner->blockCallbacks.readFunc)
    {
        return false;
    }

    if (read_lba < 0 || (uint32_t)read_lba > DiskSCSI_LastLBA(&hdd->hdinfo))
    {
        scsihdd_log(hdd, "HD READ ERROR! LBA=%d out of range (last=%u)", read_lba,
                    DiskSCSI_LastLBA(&hdd->hdinfo));
        return false;
    }

    int blocks_read = hdd->owner->blockCallbacks.readFunc(hdd->owner, hdd->sectorData, 1,
                                                          (uint32_t)read_lba, hdd->unit);
    if (blocks_read != 1)
    {
        scsihdd_log(hdd, "HD READ ERROR! LBA=%d rc=%d", read_lba, blocks_read);
        return false;
    }

    return true;
}


static bool scsihdd_write_block(SCSIHDDDevice *hdd, int write_lba)
{
    if (!hdd->owner || !hdd->owner->blockCallbacks.writeFunc)
    {
        return false;
    }

    if (write_lba < 0 || (uint32_t)write_lba > DiskSCSI_LastLBA(&hdd->hdinfo))
    {
        scsihdd_log(hdd, "HD SEEK ERROR! LBA=%d", write_lba);
        return false;
    }

    int blocks_written = hdd->owner->blockCallbacks.writeFunc(hdd->owner, hdd->sectorData, 1,
                                                              (uint32_t)write_lba, hdd->unit);
    return blocks_written == 1;
}


/*
 * DATA IN byte fetch.
 *
 * Multi-block transfers stream sector by sector: recompute the LBA from the
 * byte position and re-read whenever it moves, then index within the sector.
 */
static uint8_t scsihdd_get_data(SCSITarget *t, SBUF id, int pos)
{
    SCSIHDDDevice *hdd = (SCSIHDDDevice *)t->impl;

    /* Micropolis vendor 0x10 READ SECTOR BUFFER reads the raw sector buffer. */
    if (t->scsi_cmdbuf[0] == SC_READ_SECTOR_BUFFER)
    {
        if (pos < 0 || pos >= SCSI_HDD_MAX_SECTOR_BYTES)
        {
            return 0;
        }
        return hdd->sectorData[pos];
    }

    if (!scsihdd_has_media(hdd))
    {
        return 0x00;
    }

    if (id != SBUF_DATA)
    {
        return SCSITarget_DefaultGetData(t, id, pos);
    }

    int clba = hdd->lba + pos / hdd->hdinfo.sectorbytes;
    if (clba != hdd->cur_lba)
    {
        hdd->cur_lba = clba;
        hdd->sectorDataValid = scsihdd_read_block(hdd, clba);
        if (!hdd->sectorDataValid)
        {
            memset(hdd->sectorData, 0, sizeof(hdd->sectorData));
        }
    }

    if (!hdd->sectorDataValid)
    {
        return 0x00;
    }

    return hdd->sectorData[pos % hdd->hdinfo.sectorbytes];
}


/*
 * DATA OUT byte store. Mirrors scsi_get_data and flushes the sector to the
 * image on the last byte of each block.
 */
static void scsihdd_put_data(SCSITarget *t, SBUF id, int pos, uint8_t data)
{
    SCSIHDDDevice *hdd = (SCSIHDDDevice *)t->impl;
    uint8_t cmd = t->scsi_cmdbuf[0];

    switch (cmd)
    {
    case SC_INIT_DRIVE_PARAMS: /* 0x0C */
        if (pos >= 0 && pos < 8)
        {
            hdd->hdinfo.drive_params[pos] = data;
        }
        return;
    case SC_FORMAT_ALT_TRACK: /* 0x0E - accepted and discarded */
        return;
    case SC_WRITE_SECTOR_BUFFER: /* 0x0F */
        if (pos >= 0 && pos < SCSI_HDD_MAX_SECTOR_BYTES)
        {
            hdd->sectorData[pos] = data;
        }
        return;
    default:
        break;
    }

    if (id != SBUF_DATA)
    {
        SCSITarget_DefaultPutData(t, id, pos, data);
        return;
    }

    if (!scsihdd_has_media(hdd))
    {
        return;
    }

    int offset = pos % hdd->hdinfo.sectorbytes;
    int clba = hdd->lba + pos / hdd->hdinfo.sectorbytes;

    hdd->sectorData[offset] = data;

    /* Flush when the sector is full. */
    if (offset == hdd->hdinfo.sectorbytes - 1)
    {
        hdd->cur_lba = clba;
        scsihdd_write_block(hdd, clba);
    }
}


/* ------------------------------------------------------------------ */
/* Individual commands                                                 */
/* ------------------------------------------------------------------ */

/*
 * READ CAPACITY (0x25) -> 8 bytes:
 *   [0..3] last LBA   (big-endian u32)
 *   [4..7] block size (big-endian u32)
 *
 * For the Micropolis 1375-ND that is 129311 (0x0001F91F) and 1024 (0x00000400).
 *
 * This MUST be the geometry-derived physical last LBA, not a directory-derived
 * one: SINTRAN's carved READ-CAPACITY completion handler ECAPD validates it,
 * and a wrong value stamps DISC TRANSFER ERROR (STATUS 100020B).
 */
static void scsihdd_command_read_capacity(SCSIHDDDevice *hdd)
{
    SCSITarget *t = &hdd->target;
    uint32_t last_lba = DiskSCSI_LastLBA(&hdd->hdinfo);

    scsi_put_u32be(&t->scsi_cmdbuf[0], last_lba);
    scsi_put_u32be(&t->scsi_cmdbuf[4], hdd->hdinfo.sectorbytes);

    scsihdd_log(hdd, "READ CAPACITY -> blockSize=%u lastLBA=%u capacityBytes=%lld",
                hdd->hdinfo.sectorbytes, last_lba,
                (long long)(last_lba + 1) * hdd->hdinfo.sectorbytes);

    /* NOTE: buffer id 0 = SBUF_MAIN - the payload was just written into the
     * command buffer, not the data buffer. */
    SCSITarget_DataIn(t, SBUF_MAIN, 8);
    SCSITarget_StatusComplete(t, SS_GOOD);
}


/*
 * INQUIRY (0x12), page 0 -> up to 56 bytes, returning min(cmd[4], 56).
 *
 * Verified real-hardware exchange: CDB 12 00 00 00 24 00 -> 36 bytes.
 */
static void scsihdd_command_inquiry(SCSIHDDDevice *hdd)
{
    SCSITarget *t = &hdd->target;
    int page = t->scsi_cmdbuf[2];
    int size = t->scsi_cmdbuf[4];

    if (page != 0)
    {
        /* RetroCore only implements page 0; other pages fall through with
         * whatever status the media check produces below. */
        scsihdd_log(hdd, "INQUIRY page %d not supported", page);
    }
    else
    {
        /* Zero the header area, then blank-fill the identity fields. */
        memset(&t->scsi_cmdbuf[0], 0, 148);
        memset(&t->scsi_cmdbuf[8], 0x20, 28);

        t->scsi_cmdbuf[0] = 0x00; /* device type = direct-access disk */
        t->scsi_cmdbuf[1] = 0x00; /* media is not removable */
        t->scsi_cmdbuf[2] = 0x05; /* complies with SPC-3 */
        t->scsi_cmdbuf[3] = 0x01; /* response data format = CCS */
        t->scsi_cmdbuf[4] = 52;   /* additional length */

        /* Identity for the ND drive: NDMICROP / 1375 / B0C. The generic
         * SEAGATE ST225N default in SCSIHDD.cs is only used when no drive
         * metadata is set, which never happens on the ND path. */
        memcpy(&t->scsi_cmdbuf[8], hdd->hdinfo.vendor, 8);
        memcpy(&t->scsi_cmdbuf[16], hdd->hdinfo.product, 16);
        memcpy(&t->scsi_cmdbuf[32], hdd->hdinfo.revision, 4);

        if (size > SCSI_HDD_INQUIRY_SIZE)
        {
            size = SCSI_HDD_INQUIRY_SIZE;
        }

        SCSITarget_DataIn(t, SBUF_MAIN, size);
    }

    if (scsihdd_has_media(hdd))
    {
        SCSITarget_StatusComplete(t, SS_GOOD);
    }
    else
    {
        SCSITarget_StatusComplete(t, SS_CHECK_CONDITION);
    }
}


static void scsihdd_command_test_unit_ready(SCSIHDDDevice *hdd)
{
    scsihdd_log(hdd, "command TEST UNIT READY");

    if (scsihdd_has_media(hdd))
    {
        SCSITarget_StatusComplete(&hdd->target, SS_GOOD);
    }
    else
    {
        SCSITarget_StatusComplete(&hdd->target, SS_CHECK_CONDITION);
    }
}


/*
 * MODE SENSE(6) (0x1A) - ported 2026-07-17 from RetroCore
 * SCSIHDD.CommandModeSense (SCSIHDD.cs), byte-for-byte page layouts.
 *
 * Reply: 4-byte mode parameter header + 8-byte block descriptor + the
 * requested page(s). page 0x3F returns all implemented pages, iterated
 * DOWNWARDS from 0x3e to 0x00 exactly like the C# loop. An unimplemented
 * single page (other than 0x3f) fails with CHECK CONDITION / ILLEGAL REQUEST
 * / invalid field in CDB (0x24).
 *
 * LUN note: the C# checks the LUN inside CommandModeSense (bad_lun); here the
 * generic lun != 0 rejection in SCSIHDD_Command's preamble already covers it.
 */
static void scsihdd_command_mode_sense(SCSIHDDDevice *hdd)
{
    SCSITarget *t = &hdd->target;
    int page = t->scsi_cmdbuf[2] & 0x3f;
    int size = t->scsi_cmdbuf[4];
    int pos = 1;
    uint32_t dsize;
    int pmax;
    int pmin;
    int p;
    bool fail = false;

    scsihdd_log(hdd, "command MODE SENSE(6) page=0x%02X alloc=0x%02X link=0x%02X", page, size,
                t->scsi_cmdbuf[5]);

    t->scsi_cmdbuf[pos++] = 0x00; /* medium type */
    t->scsi_cmdbuf[pos++] = 0x00; /* WP, cache */

    /* Block descriptor. NOTE: the "number of blocks" field is filled from
     * DiskSCSI_LastLBA() because the C# uses hdinfo.DiskSizeInBlocks here and
     * that field actually holds the LAST LBA (count-1) - copied verbatim, do
     * not "correct" without changing both sides. */
    dsize = DiskSCSI_LastLBA(&hdd->hdinfo);
    t->scsi_cmdbuf[pos++] = 0x08; /* block descriptor length */
    t->scsi_cmdbuf[pos++] = 0x00;
    scsi_put_u24be(&t->scsi_cmdbuf[pos], dsize);
    pos += 3;
    t->scsi_cmdbuf[pos++] = 0x00;
    scsi_put_u24be(&t->scsi_cmdbuf[pos], hdd->hdinfo.sectorbytes);
    pos += 3;

    pmax = (page == 0x3f) ? 0x3e : page;
    pmin = (page == 0x3f) ? 0x00 : page;

    for (p = pmax; p >= pmin; p--)
    {
        switch (p)
        {
        case 0x00:                        /* Unit attention parameters page (weird) */
            t->scsi_cmdbuf[pos++] = 0x80; /* PS, page id */
            t->scsi_cmdbuf[pos++] = 0x02; /* Page length */
            t->scsi_cmdbuf[pos++] = 0x00; /* Meh */
            t->scsi_cmdbuf[pos++] = 0x00; /* Double meh */
            break;

        case 0x01:                        /* read-write error recovery page */
            t->scsi_cmdbuf[pos++] = 0x01; /* !PS, page id */
            t->scsi_cmdbuf[pos++] = 0x0a; /* page length */
            t->scsi_cmdbuf[pos++] = 0;    /* various bits */
            t->scsi_cmdbuf[pos++] = 0;    /* read retry count */
            t->scsi_cmdbuf[pos++] = 0;    /* correction span */
            t->scsi_cmdbuf[pos++] = 0;    /* head offset count */
            t->scsi_cmdbuf[pos++] = 0;    /* data strobe offset count */
            t->scsi_cmdbuf[pos++] = 0;    /* reserved */
            t->scsi_cmdbuf[pos++] = 0;    /* write retry count */
            t->scsi_cmdbuf[pos++] = 0;    /* reserved */
            t->scsi_cmdbuf[pos++] = 0;    /* recovery time limit (msb) */
            t->scsi_cmdbuf[pos++] = 0;    /* recovery time limit (lsb) */
            break;

        case 0x02:                        /* disconnect-reconnect page */
            t->scsi_cmdbuf[pos++] = 0x02; /* !PS, page id */
            t->scsi_cmdbuf[pos++] = 0x0e; /* page length */
            t->scsi_cmdbuf[pos++] = 0;    /* buffer full ratio */
            t->scsi_cmdbuf[pos++] = 0;    /* buffer empty ratio */
            t->scsi_cmdbuf[pos++] = 0;    /* bus inactivity limit (msb) */
            t->scsi_cmdbuf[pos++] = 0;    /* bus inactivity limit (lsb) */
            t->scsi_cmdbuf[pos++] = 0;    /* disconnect time limit (msb) */
            t->scsi_cmdbuf[pos++] = 0;    /* disconnect time limit (lsb) */
            t->scsi_cmdbuf[pos++] = 0;    /* connect time limit (msb) */
            t->scsi_cmdbuf[pos++] = 0;    /* connect time limit (lsb) */
            t->scsi_cmdbuf[pos++] = 0;    /* maximum burst size (msb) */
            t->scsi_cmdbuf[pos++] = 0;    /* maximum burst size (lsb) */
            t->scsi_cmdbuf[pos++] = 0;    /* reserved */
            t->scsi_cmdbuf[pos++] = 0;    /* reserved */
            t->scsi_cmdbuf[pos++] = 0;    /* reserved */
            t->scsi_cmdbuf[pos++] = 0;    /* reserved */
            break;

        case 0x03:                        /* Format parameters page */
            t->scsi_cmdbuf[pos++] = 0x83; /* PS, page id */
            t->scsi_cmdbuf[pos++] = 0x16; /* Page length */
            scsi_put_u16be(&t->scsi_cmdbuf[pos],
                           (uint16_t)(hdd->hdinfo.cylinders * hdd->hdinfo.heads)); /* Track/zone */
            pos += 2;
            t->scsi_cmdbuf[pos++] = 0x00;                              /* Alt sect/zone */
            t->scsi_cmdbuf[pos++] = 0x00;                              /* Alt sect/zone */
            t->scsi_cmdbuf[pos++] = 0x00;                              /* Alt track/zone */
            t->scsi_cmdbuf[pos++] = 0x00;                              /* Alt track/zone */
            t->scsi_cmdbuf[pos++] = 0x00;                              /* Alt track/volume */
            t->scsi_cmdbuf[pos++] = 0x00;                              /* Alt track/volume */
            scsi_put_u16be(&t->scsi_cmdbuf[pos], hdd->hdinfo.sectors); /* Sectors/track */
            pos += 2;
            scsi_put_u16be(&t->scsi_cmdbuf[pos], hdd->hdinfo.sectorbytes); /* Bytes/sector */
            pos += 2;
            t->scsi_cmdbuf[pos++] = 0x00; /* Interleave */
            t->scsi_cmdbuf[pos++] = 0x00; /* Interleave */
            t->scsi_cmdbuf[pos++] = 0x00; /* Track skew */
            t->scsi_cmdbuf[pos++] = 0x00; /* Track skew */
            t->scsi_cmdbuf[pos++] = 0x00; /* Cylinder skew */
            t->scsi_cmdbuf[pos++] = 0x00; /* Cylinder skew */
            t->scsi_cmdbuf[pos++] = 0x00; /* Sectoring type */
            t->scsi_cmdbuf[pos++] = 0x00; /* Reserved */
            t->scsi_cmdbuf[pos++] = 0x00; /* Reserved */
            t->scsi_cmdbuf[pos++] = 0x00; /* Reserved */
            break;

        case 0x04:                        /* Rigid drive geometry page */
            t->scsi_cmdbuf[pos++] = 0x84; /* PS, page id */
            t->scsi_cmdbuf[pos++] = 0x16; /* Page length */
            scsi_put_u24be(&t->scsi_cmdbuf[pos], hdd->hdinfo.cylinders); /* Cylinders */
            pos += 3;
            t->scsi_cmdbuf[pos++] = hdd->hdinfo.heads; /* Heads */
            t->scsi_cmdbuf[pos++] = 0x00;              /* Starting cylinder - write precomp */
            t->scsi_cmdbuf[pos++] = 0x00;              /* Starting cylinder - write precomp */
            t->scsi_cmdbuf[pos++] = 0x00;              /* Starting cylinder - write precomp */
            t->scsi_cmdbuf[pos++] = 0x00; /* Starting cylinder - reduced write current */
            t->scsi_cmdbuf[pos++] = 0x00; /* Starting cylinder - reduced write current */
            t->scsi_cmdbuf[pos++] = 0x00; /* Starting cylinder - reduced write current */
            t->scsi_cmdbuf[pos++] = 0x00; /* Drive step rate */
            t->scsi_cmdbuf[pos++] = 0x00; /* Drive step rate */
            t->scsi_cmdbuf[pos++] = 0x00; /* Landing zone cylinder */
            t->scsi_cmdbuf[pos++] = 0x00; /* Landing zone cylinder */
            t->scsi_cmdbuf[pos++] = 0x00; /* Landing zone cylinder */
            t->scsi_cmdbuf[pos++] = 0x00; /* RPL */
            t->scsi_cmdbuf[pos++] = 0x00; /* Rotational offset */
            t->scsi_cmdbuf[pos++] = 0x00; /* Reserved */
            scsi_put_u16be(&t->scsi_cmdbuf[pos], 10000); /* Medium rotation rate */
            pos += 2;
            t->scsi_cmdbuf[pos++] = 0x00; /* Reserved */
            t->scsi_cmdbuf[pos++] = 0x00; /* Reserved */
            break;

        case 0x08:                        /* caching page */
            t->scsi_cmdbuf[pos++] = 0x08; /* !PS, page id */
            t->scsi_cmdbuf[pos++] = 0x0a; /* page length */
            t->scsi_cmdbuf[pos++] = 0;
            t->scsi_cmdbuf[pos++] = 0;
            t->scsi_cmdbuf[pos++] = 0;
            t->scsi_cmdbuf[pos++] = 0;
            t->scsi_cmdbuf[pos++] = 0;
            t->scsi_cmdbuf[pos++] = 0;
            t->scsi_cmdbuf[pos++] = 0;
            t->scsi_cmdbuf[pos++] = 0;
            t->scsi_cmdbuf[pos++] = 0;
            t->scsi_cmdbuf[pos++] = 0;
            break;

        case 0x30:                        /* Apple firmware ID page (kept for C# parity) */
            t->scsi_cmdbuf[pos++] = 0xb0; /* cPS, page id */
            t->scsi_cmdbuf[pos++] = 0x16; /* Page length */
            memcpy(&t->scsi_cmdbuf[pos], "APPLE COMPUTER, INC   ", 22);
            pos += 22;
            break;

        default:
            if (page != 0x3f)
            {
                scsihdd_log(hdd, "mode sense page 0x%02X unhandled", page);
                fail = true;
            }
            break;
        }
    }

    if (!fail)
    {
        t->scsi_cmdbuf[0] = (uint8_t)pos; /* mode data length */
        if (pos > size)
        {
            pos = size;
        }

        SCSITarget_DataIn(t, SBUF_MAIN, pos);
        SCSITarget_StatusComplete(t, SS_GOOD);
    }
    else
    {
        SCSITarget_StatusComplete(t, SS_CHECK_CONDITION);
        SCSITarget_Sense(t, false, SK_ILLEGAL_REQUEST, 0x24, 0x00);
    }
}


/* ------------------------------------------------------------------ */
/* CDB decode                                                          */
/* ------------------------------------------------------------------ */
static void scsihdd_command(SCSITarget *t)
{
    SCSIHDDDevice *hdd = (SCSIHDDDevice *)t->impl;
    uint8_t cmd = t->scsi_cmdbuf[0];
    int lun = scsihdd_get_lun(hdd, t->scsi_cmdbuf[1] >> 5);

    if (Log_IsEnabled(LOG_CAT_SCSI, LOG_DEBUG))
    {
        scsihdd_log(hdd, "CDB op=0x%02X cdb=%02X,%02X,%02X,%02X,%02X lun=%d", cmd,
                    t->scsi_cmdbuf[1], t->scsi_cmdbuf[2], t->scsi_cmdbuf[3], t->scsi_cmdbuf[4],
                    t->scsi_cmdbuf[5], lun);
    }

    /* LUNs other than 0 are rejected, except for the three commands that must
     * always answer (SCSIHDDMicropolis.scsi_command). */
    if (cmd != SC_REQUEST_SENSE && cmd != SC_INQUIRY && cmd != SC_TEST_UNIT_READY)
    {
        if (lun != 0)
        {
            SCSITarget_ReportBadLun(t, cmd, (uint8_t)lun);
            return;
        }
    }

    switch (cmd)
    {
    case SC_TEST_UNIT_READY:
        scsihdd_command_test_unit_ready(hdd);
        break;

    case SC_REQUEST_SENSE:
        /* NOTE: the Micropolis override returns only FOUR bytes of sense, not
         * the 18 the base SCSIHDD returns and not the 18 the real drive returns
         * in the captured trace. This is what RetroCore's ND path does and what
         * SINTRAN is known to work against - do not "correct" it to 18 without
         * re-validating the mount. */
        SCSITarget_DataIn(t, SBUF_SENSE, 4);
        SCSITarget_StatusComplete(t, SS_GOOD);
        break;

    case SC_INQUIRY:
        scsihdd_command_inquiry(hdd);
        break;

    case SC_READ_CAPACITY:
        scsihdd_command_read_capacity(hdd);
        break;

    case SC_READ_6:
        /* 21-bit LBA, and a transfer length where 0 means 256. */
        hdd->lba = (int)(scsi_get_u24be(&t->scsi_cmdbuf[1]) & 0x1fffff);
        hdd->blocks = t->scsi_cmdbuf[4];
        if (hdd->blocks == 0)
        {
            hdd->blocks = 256;
        }

        scsihdd_log(hdd, "command READ(6) lba=%d blocks=%d", hdd->lba, hdd->blocks);

        if ((uint32_t)hdd->lba > DiskSCSI_LastLBA(&hdd->hdinfo))
        {
            hdd->sectorDataValid = false;
        }
        else
        {
            hdd->sectorDataValid = scsihdd_read_block(hdd, hdd->lba);
            hdd->cur_lba = hdd->lba;
        }

        if (hdd->sectorDataValid)
        {
            SCSITarget_DataIn(t, SBUF_DATA, hdd->blocks * hdd->hdinfo.sectorbytes);
            SCSITarget_StatusComplete(t, SS_GOOD);
        }
        else
        {
            SCSITarget_StatusComplete(t, SS_CHECK_CONDITION);
            SCSITarget_Sense(t, false, SK_ILLEGAL_REQUEST, 0x24, 0x00);
        }
        break;

    case SC_WRITE_6:
        hdd->lba = (int)(scsi_get_u24be(&t->scsi_cmdbuf[1]) & 0x1fffff);
        hdd->blocks = t->scsi_cmdbuf[4];
        if (hdd->blocks == 0)
        {
            hdd->blocks = 256;
        }

        scsihdd_log(hdd, "command WRITE(6) lba=%d blocks=%d", hdd->lba, hdd->blocks);

        SCSITarget_DataOut(t, SBUF_DATA, hdd->blocks * hdd->hdinfo.sectorbytes);
        SCSITarget_StatusComplete(t, SS_GOOD);
        break;

    case SC_READ_10:
        hdd->lba = (int)scsi_get_u32be(&t->scsi_cmdbuf[2]);
        hdd->blocks = scsi_get_u16be(&t->scsi_cmdbuf[7]);

        scsihdd_log(hdd, "command READ(10) lba=%d blocks=%d", hdd->lba, hdd->blocks);

        hdd->sectorDataValid = scsihdd_read_block(hdd, hdd->lba);
        hdd->cur_lba = hdd->lba;

        if (hdd->sectorDataValid)
        {
            SCSITarget_DataIn(t, SBUF_DATA, hdd->blocks * hdd->hdinfo.sectorbytes);
            SCSITarget_StatusComplete(t, SS_GOOD);
        }
        else
        {
            SCSITarget_StatusComplete(t, SS_CHECK_CONDITION);
            SCSITarget_Sense(t, false, SK_ILLEGAL_REQUEST, 0x24, 0x00);
        }
        break;

    case SC_WRITE_10:
        hdd->lba = (int)scsi_get_u32be(&t->scsi_cmdbuf[2]);
        hdd->blocks = scsi_get_u16be(&t->scsi_cmdbuf[7]);

        scsihdd_log(hdd, "command WRITE(10) lba=%d blocks=%d", hdd->lba, hdd->blocks);

        SCSITarget_DataOut(t, SBUF_DATA, hdd->blocks * hdd->hdinfo.sectorbytes);
        SCSITarget_StatusComplete(t, SS_GOOD);
        break;

    case SC_SEEK_6:
        hdd->lba = (int)(scsi_get_u24be(&t->scsi_cmdbuf[1]) & 0x1fffff);
        if (((t->scsi_cmdbuf[1] >> 5) != 0) ||
            ((uint32_t)hdd->lba > DiskSCSI_LastLBA(&hdd->hdinfo)))
        {
            /* NOTE: RetroCore reports SS_CONDITION_MET (not CHECK CONDITION) and
             * writes the sense KEY straight into sense byte 0, where the
             * response code (0x70) belongs. Both look wrong; both are copied
             * verbatim because this is the behaviour SINTRAN sees. */
            SCSITarget_StatusComplete(t, SS_CONDITION_MET);
            t->scsi_sense_buffer[0] = SK_HARDWARE_ERROR;
        }
        else
        {
            SCSITarget_StatusComplete(t, SS_GOOD);
        }
        break;

    case SC_RESERVE_6:
        SCSITarget_StatusComplete(t, SS_GOOD);
        break;

    case SC_INIT_DRIVE_PARAMS: /* Micropolis vendor 0x0C */
        SCSITarget_DataOut(t, SBUF_DATA, 8);
        SCSITarget_StatusComplete(t, SS_GOOD);
        break;

    case SC_MODE_SELECT_6:
        /* Parameters are accepted and IGNORED - the sector size is fixed at
         * 1024 and never changes, whatever the block descriptor asks for. */
        scsihdd_log(hdd,
                    "command MODE SELECT(6) paramLen=%d (block descriptor IGNORED, "
                    "sector size fixed at %u)",
                    t->scsi_cmdbuf[4], hdd->hdinfo.sectorbytes);
        if (t->scsi_cmdbuf[4] != 0)
        {
            SCSITarget_DataOut(t, SBUF_DATA, t->scsi_cmdbuf[4]);
        }
        SCSITarget_StatusComplete(t, SS_GOOD);
        break;

    case SC_MODE_SENSE_6:
        /* Ported 2026-07-17 from RetroCore SCSIHDD.CommandModeSense - was
         * previously missing here (fell through to ReportBadCmd). */
        scsihdd_command_mode_sense(hdd);
        break;

    case SC_START_STOP_UNIT:
        scsihdd_log(hdd, "command %s UNIT", (t->scsi_cmdbuf[4] & 0x01) ? "START" : "STOP");
        SCSITarget_StatusComplete(t, SS_GOOD);
        break;

    case SC_VERIFY_10:
        if ((t->scsi_cmdbuf[1] & 0x02) == 0)
        {
            SCSITarget_StatusComplete(t, SS_GOOD);
        }
        else
        {
            SCSITarget_ReportBadCmd(t, cmd);
        }
        break;

    case SC_FORMAT_UNIT:
        /*
         * Reports success WITHOUT touching the image.
         *
         * This deliberately does NOT port RetroCore's format loop, which is
         *     for cyl.. for head.. for sector..
         *         writeBlock(cyl * head * sector, block);
         * That MULTIPLIES the CHS values instead of computing an LBA
         * ((cyl*heads + head)*sectors + sector), so it zero-fills a scattered
         * set of wrong blocks - and block 0 repeatedly, since the product is 0
         * whenever head or sector is 0. It would corrupt the disk image.
         * Returning GOOD is the safe behaviour and matches what the port spec
         * calls for; a real format is not needed to mount or boot.
         */
        scsihdd_log(hdd, "command FORMAT UNIT (accepted, image NOT modified)");
        SCSITarget_StatusComplete(t, SS_GOOD);
        break;

    case SC_RECEIVE_DIAGNOSTIC_RESULTS:
    case SC_SEND_DIAGNOSTIC:
    {
        int size = scsi_get_u16be(&t->scsi_cmdbuf[3]);
        int pos = 0;

        if (cmd == SC_SEND_DIAGNOSTIC && (t->scsi_cmdbuf[1] & 4))
        {
            SCSITarget_StatusComplete(t, SS_GOOD);
            break;
        }

        t->scsi_cmdbuf[pos++] = 0;
        t->scsi_cmdbuf[pos++] = 6;
        t->scsi_cmdbuf[pos++] = 0; /* ROM is OK */
        t->scsi_cmdbuf[pos++] = 0; /* RAM is OK */
        t->scsi_cmdbuf[pos++] = 0; /* Data buffer is OK */
        t->scsi_cmdbuf[pos++] = 0; /* Interface is OK */
        t->scsi_cmdbuf[pos++] = 0;
        if (cmd == SC_SEND_DIAGNOSTIC)
        {
            t->scsi_cmdbuf[pos++] = 0;
        }

        if (size > pos)
        {
            size = pos;
        }
        SCSITarget_DataIn(t, SBUF_MAIN, size);
        SCSITarget_StatusComplete(t, SS_GOOD);
        break;
    }

    case SC_READ_BUFFER:
        SCSITarget_ReportBadCmd(t, cmd);
        break;

    default:
        scsihdd_log(hdd, "command 0x%02X *** UNKNOWN ***", cmd);
        SCSITarget_ReportBadCmd(t, cmd);
        break;
    }
}


static void scsihdd_device_reset(SCSIHDDDevice *hdd)
{
    if (!hdd)
    {
        return;
    }

    SCSITarget_DeviceReset(&hdd->target);

    hdd->cur_lba = -1;
    hdd->sectorDataValid = false;

    /* After a reset the drive posts UNIT ATTENTION / power-on-or-reset.
     * Verified trace: 70 00 06 00 00 00 00 0A 00 00 00 00 29 00 00 00 00 00 */
    SCSITarget_Sense(&hdd->target, false, SK_UNIT_ATTENTION, 0x29, 0x00);
}


void SCSIHDD_Init(SCSIHDDDevice *hdd, SCSIBus *bus, uint8_t scsi_id, struct Device *owner, int unit,
                  SCSIDiskType disk_type)
{
    memset(hdd, 0, sizeof(SCSIHDDDevice));

    hdd->owner = owner;
    hdd->unit = unit;
    hdd->cur_lba = -1;

    DiskSCSI_SetDiskType(&hdd->hdinfo, disk_type);

    /* Hooks must be set before SCSITarget_Init - it calls DeviceReset, which
     * touches the sense buffer through them. */
    hdd->target.impl = hdd;
    hdd->target.scsi_command = scsihdd_command;
    hdd->target.scsi_get_data = scsihdd_get_data;
    hdd->target.scsi_put_data = scsihdd_put_data;

    SCSITarget_Init(&hdd->target, bus, scsi_id, "SCSI-HDD");

    scsihdd_device_reset(hdd);
}
