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
    log_write(LOG_CAT_SCSI, LOG_DEBUG, "HDD%d: %s", hdd->unit, msg);
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

    if (read_lba < 0 || (uint32_t)read_lba > scsi_disk_last_lba(&hdd->hdinfo))
    {
        scsihdd_log(hdd, "HD READ ERROR! LBA=%d out of range (last=%u)", read_lba,
                    scsi_disk_last_lba(&hdd->hdinfo));
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

    if (write_lba < 0 || (uint32_t)write_lba > scsi_disk_last_lba(&hdd->hdinfo))
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
    /* As in scsihdd_put_data: the command-specific sources are for the DATA IN
     * phase (SBUF_DATA) only. */
    if (id == SBUF_DATA && t->scsi_cmdbuf[0] == SC_READ_SECTOR_BUFFER)
    {
        if (pos < 0 || pos >= SCSI_HDD_MAX_SECTOR_BYTES)
        {
            return 0;
        }
        return hdd->sectorData[pos];
    }

    /* READ BUFFER, combined header and data mode: the four-byte header is a
     * reserved byte and the 24-bit buffer capacity, then the buffer itself
     * (SCSI-2 section 8.2.12.1, table 60). The drive buffer is not the medium,
     * so this needs no disk. */
    if (id == SBUF_DATA && t->scsi_cmdbuf[0] == SC_READ_BUFFER)
    {
        if (pos < SCSI_HDD_BUFFER_HEADER_BYTES)
        {
            static const uint8_t header[SCSI_HDD_BUFFER_HEADER_BYTES] = {
                0x00, (uint8_t)(SCSI_HDD_DATA_BUFFER_BYTES >> 16),
                (uint8_t)(SCSI_HDD_DATA_BUFFER_BYTES >> 8), (uint8_t)SCSI_HDD_DATA_BUFFER_BYTES};
            return pos < 0 ? 0 : header[pos];
        }
        int at = pos - SCSI_HDD_BUFFER_HEADER_BYTES;
        return at < SCSI_HDD_DATA_BUFFER_BYTES ? hdd->dataBuffer[at] : 0;
    }

    if (!scsihdd_has_media(hdd))
    {
        return 0x00;
    }

    if (id != SBUF_DATA)
    {
        return scsi_device_default_get_data(t, id, pos);
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


static void scsihdd_apply_mode_select(SCSIHDDDevice *hdd);


/*
 * DATA OUT byte store. Mirrors scsi_get_data and flushes the sector to the
 * image on the last byte of each block.
 */
static void scsihdd_put_data(SCSITarget *t, SBUF id, int pos, uint8_t data)
{
    SCSIHDDDevice *hdd = (SCSIHDDDevice *)t->impl;
    uint8_t cmd = t->scsi_cmdbuf[0];

    /* The command-specific stores below are for the DATA OUT phase only
     * (every one of those commands queues its data phase on SBUF_DATA). The
     * COMMAND phase stores the CDB through this same hook into SBUF_MAIN,
     * and from the first byte on scsi_cmdbuf[0] already holds the new opcode:
     * without this test, bytes 1-9 of a WRITE BUFFER CDB went into the drive
     * buffer, the CDB kept the previous command's length bytes, and SCSI-TV
     * device test 4 sent 16388 bytes against a parameter list length of 4.
     * The vendor cases 0Ch, 0Eh and 0Fh lost their CDB bytes 1-5 the same way. */
    switch (id == SBUF_DATA ? cmd : -1)
    {
    case SC_INIT_DRIVE_PARAMS: /* 0x0C */
        if (pos >= 0 && pos < 8)
        {
            hdd->hdinfo.drive_params[pos] = data;
        }
        return;
    case SC_FORMAT_ALT_TRACK: /* 0x0E - accepted and discarded */
        return;
    case SC_MODE_SELECT_6: /* 0x15 - the parameter list, applied on its last byte */
        /* These bytes used to fall through to the block store below and
         * overwrite the cached sector in sectorData. */
        if (pos >= 0 && pos < hdd->modeSelectLength)
        {
            hdd->modeSelectData[pos] = data;
            if (pos == hdd->modeSelectLength - 1)
            {
                scsihdd_apply_mode_select(hdd);
            }
        }
        return;
    case SC_FORMAT_UNIT: /* 0x04 - the defect list header (FmtData = 1) */
        if (pos >= 0 && pos < SCSI_HDD_DEFECT_HEADER_BYTES)
        {
            hdd->defectHeader[pos] = data;
            if (pos == SCSI_HDD_DEFECT_HEADER_BYTES - 1)
            {
                uint16_t list = scsi_device_scsi_get_u16_be(&hdd->defectHeader[2]);
                scsihdd_log(hdd, "FORMAT UNIT defect list header %02X %02X %02X %02X",
                            hdd->defectHeader[0], hdd->defectHeader[1], hdd->defectHeader[2],
                            hdd->defectHeader[3]);
                if (list != 0)
                {
                    /* The defect descriptors that would follow are not taken:
                     * the DATA OUT phase was queued for the header only. */
                    LOG(LOG_CAT_SCSI, LOG_ERROR,
                        "HDD%d: FORMAT UNIT defect list of %u bytes is not supported", hdd->unit,
                        list);
                }
            }
        }
        return;
    case SC_WRITE_SECTOR_BUFFER: /* 0x0F */
        if (pos >= 0 && pos < SCSI_HDD_MAX_SECTOR_BYTES)
        {
            hdd->sectorData[pos] = data;
        }
        return;
    case SC_WRITE_BUFFER: /* 0x3B, combined header and data mode */
    {
        /* The four header bytes are all reserved (SCSI-2 section 8.2.17.1);
         * the data after them goes into the drive buffer, never the medium. */
        int at = pos - SCSI_HDD_BUFFER_HEADER_BYTES;
        if (at >= 0 && at < SCSI_HDD_DATA_BUFFER_BYTES)
        {
            hdd->dataBuffer[at] = data;
        }
        return;
    }
    default:
        break;
    }

    if (id != SBUF_DATA)
    {
        scsi_device_default_put_data(t, id, pos, data);
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
    uint32_t last_lba = scsi_disk_last_lba(&hdd->hdinfo);

    scsi_device_scsi_put_u32_be(&t->scsi_cmdbuf[0], last_lba);
    scsi_device_scsi_put_u32_be(&t->scsi_cmdbuf[4], hdd->hdinfo.sectorbytes);

    scsihdd_log(hdd, "READ CAPACITY -> blockSize=%u lastLBA=%u capacityBytes=%lld",
                hdd->hdinfo.sectorbytes, last_lba,
                (long long)(last_lba + 1) * hdd->hdinfo.sectorbytes);

    /* NOTE: buffer id 0 = SBUF_MAIN - the payload was just written into the
     * command buffer, not the data buffer. */
    scsi_device_data_in(t, SBUF_MAIN, 8);
    scsi_device_status_complete(t, SS_GOOD);
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

        scsi_device_data_in(t, SBUF_MAIN, size);
    }

    if (scsihdd_has_media(hdd))
    {
        scsi_device_status_complete(t, SS_GOOD);
    }
    else
    {
        scsi_device_status_complete(t, SS_CHECK_CONDITION);
    }
}


static void scsihdd_command_test_unit_ready(SCSIHDDDevice *hdd)
{
    scsihdd_log(hdd, "command TEST UNIT READY");

    if (scsihdd_has_media(hdd))
    {
        scsi_device_status_complete(&hdd->target, SS_GOOD);
    }
    else
    {
        scsi_device_status_complete(&hdd->target, SS_CHECK_CONDITION);
    }
}


/* ------------------------------------------------------------------ */
/* Mode pages 1 to 4: current, changeable, default and saved values    */
/* ------------------------------------------------------------------ */

/* MODE SENSE page control field, CDB byte 2 bits 7-6 (SCSI-2 section 8.2.10). */
// clang-format off
#define MODE_PC_CURRENT    0
#define MODE_PC_CHANGEABLE 1
#define MODE_PC_DEFAULT    2
#define MODE_PC_SAVED      3
// clang-format on

/* Page length byte of pages 1 to 4 (Appendix B of 101689 rev D, "Page Length
 * (maximum)"). Index = page code - 1. */
static const uint8_t scsihdd_mode_page_length[SCSI_HDD_MODE_PAGES] = {0x06, 0x0A, 0x16, 0x12};

/* Index into modeCurrent / modeSaved for pages 1 to 4, or -1 for any other page. */
static int scsihdd_mode_page_index(int page)
{
    if (page >= 0x01 && page <= SCSI_HDD_MODE_PAGES)
    {
        return page - 1;
    }
    return -1;
}


/*
 * The default values of page 1, 2, 3 or 4, as MODE SENSE sends them.
 *
 * Byte 0 is the page code alone: Appendix B shows bits 7-6 of byte 0 as
 * "Reserved" on all four pages, so the PS bit is not set.
 *
 * Pages 1 and 2 are the Appendix B defaults. Pages 3 and 4 describe the disk
 * image (sectors per track, bytes per sector, cylinders and heads from
 * hdinfo) instead of the drive's own 36 x 512-byte default format, because a
 * block number maps to a file offset through that geometry. The other page 3
 * fields are the layout ported 2026-07-17 from RetroCore
 * SCSIHDD.CommandModeSense; where they differ from Appendix B is listed in
 * BUGS.md C5.
 */
static void scsihdd_default_mode_page(SCSIHDDDevice *hdd, int page, uint8_t *out)
{
    int pos = 0;

    memset(out, 0, SCSI_HDD_MODE_PAGE_BYTES);

    if (page == 0x01) /* Error recovery parameters page */
    {
        out[pos++] = 0x01; /* Page code */
        out[pos++] = 0x06; /* Page length */
        out[pos++] = 0x20; /* AWRE 0, ARRE 0, TB 1, RC 0, EER 0, PER 0, DTE 0, DCR 0 */
        out[pos++] = 0x0A; /* Retry count: 10 */
        out[pos++] = 0x0B; /* Correction span: 11 */
        out[pos++] = 0x00; /* Head offset count (not supported) */
        out[pos++] = 0x00; /* Data strobe offset count (not supported) */
        out[pos++] = 0x00; /* Recovery time limit (not supported) */
    }
    else if (page == 0x02) /* Disconnect/reconnect parameters page */
    {
        out[pos++] = 0x02; /* Page code */
        out[pos++] = 0x0A; /* Page length */
        out[pos++] = 0x00; /* Buffer full ratio (not supported) */
        out[pos++] = 0x00; /* Buffer empty ratio (not supported) */
        scsi_device_scsi_put_u16_be(&out[pos], 5); /* Bus inactivity limit: 5 */
        pos += 2;
        out[pos++] = 0x00; /* Disconnect time limit (not supported) */
        out[pos++] = 0x00; /* Disconnect time limit */
        out[pos++] = 0x00; /* Connect time limit (not supported) */
        out[pos++] = 0x00; /* Connect time limit */
        out[pos++] = 0x00; /* Reserved */
        out[pos++] = 0x00; /* Reserved */
    }
    else if (page == 0x03) /* Format parameters page */
    {
        out[pos++] = 0x03; /* Page code */
        out[pos++] = 0x16; /* Page length */
        scsi_device_scsi_put_u16_be(
            &out[pos], (uint16_t)(hdd->hdinfo.cylinders * hdd->hdinfo.heads)); /* Track/zone */
        pos += 2;
        out[pos++] = 0x00; /* Alt sect/zone */
        out[pos++] = 0x00; /* Alt sect/zone */
        out[pos++] = 0x00; /* Alt track/zone */
        out[pos++] = 0x00; /* Alt track/zone */
        out[pos++] = 0x00; /* Alt track/volume */
        out[pos++] = 0x00; /* Alt track/volume */
        scsi_device_scsi_put_u16_be(&out[pos], hdd->hdinfo.sectors); /* Sectors/track */
        pos += 2;
        scsi_device_scsi_put_u16_be(&out[pos], hdd->hdinfo.sectorbytes); /* Bytes/sector */
        pos += 2;
        out[pos++] = 0x00; /* Interleave */
        out[pos++] = 0x00; /* Interleave */
        out[pos++] = 0x00; /* Track skew */
        out[pos++] = 0x00; /* Track skew */
        out[pos++] = 0x00; /* Cylinder skew */
        out[pos++] = 0x00; /* Cylinder skew */
        out[pos++] = 0x00; /* Sectoring type */
        out[pos++] = 0x00; /* Reserved */
        out[pos++] = 0x00; /* Reserved */
        out[pos++] = 0x00; /* Reserved */
    }
    else if (page == 0x04) /* Disk drive geometry page */
    {
        out[pos++] = 0x04;                                         /* Page code */
        out[pos++] = 0x12;                                         /* Page length */
        scsi_device_scsi_put_u24_be(&out[pos], hdd->hdinfo.cylinders); /* Cylinders */
        pos += 3;
        out[pos++] = hdd->hdinfo.heads; /* Heads */
        out[pos++] = 0x00;              /* Starting cylinder - write precomp */
        out[pos++] = 0x00;              /* Starting cylinder - write precomp */
        out[pos++] = 0x00;              /* Starting cylinder - write precomp */
        out[pos++] = 0x00;              /* Starting cylinder - reduced write current */
        out[pos++] = 0x00;              /* Starting cylinder - reduced write current */
        out[pos++] = 0x00;              /* Starting cylinder - reduced write current */
        out[pos++] = 0x00;              /* Drive step rate */
        out[pos++] = 0x00;              /* Drive step rate */
        out[pos++] = 0x00;              /* Landing zone cylinder */
        out[pos++] = 0x00;              /* Landing zone cylinder */
        out[pos++] = 0x00;              /* Landing zone cylinder */
        out[pos++] = 0x00;              /* Reserved */
        out[pos++] = 0x00;              /* Reserved */
        out[pos++] = 0x00;              /* Reserved */
        /* The RetroCore layout went on with the SCSI-2 RPL, rotational offset
         * and medium rotation rate (10000) fields here. The 1370 page ends at
         * byte 19 (Appendix B, page length 12h), so they are not sent. */
    }
}


/*
 * Which bytes of a page the host may change with MODE SELECT: 0xFF (or the
 * changeable bits) in a changeable byte, 0 elsewhere, with the page code and
 * length in bytes 0-1 as MODE SENSE sends them for page control 1. From the
 * CHANGEABLE column of Appendix B of the Micropolis 1370 Series product
 * description (101689 rev D):
 *   page 1: TB, RC, EER, PER, DTE and DCR (byte 2 bits 5-0), retry count,
 *           correction span (AWRE, ARRE, head offset count, data strobe offset
 *           count and recovery time limit are "N");
 *   page 2: bus inactivity limit (the rest is "N");
 *   page 3: alternate sectors per zone, alternate tracks per volume, sectors
 *           per track, data bytes per physical sector, track skew, cylinder
 *           skew (tracks per zone, alternate tracks per zone and interleave
 *           are "N");
 *   page 4: maximum number of cylinders and maximum number of heads ("Y*",
 *           "normally left at the default value").
 */
static void scsihdd_changeable_mode_page(int page, uint8_t *out)
{
    memset(out, 0, SCSI_HDD_MODE_PAGE_BYTES);
    if (scsihdd_mode_page_index(page) < 0)
    {
        return;
    }
    out[0] = (uint8_t)page;
    out[1] = scsihdd_mode_page_length[page - 1];

    if (page == 0x01)
    {
        out[2] = 0x3F; /* TB, RC, EER, PER, DTE, DCR */
        out[3] = 0xFF; /* Retry count */
        out[4] = 0xFF; /* Correction span */
    }
    else if (page == 0x02)
    {
        out[4] = out[5] = 0xFF; /* Bus inactivity limit */
    }
    else if (page == 0x03)
    {
        out[4] = out[5] = 0xFF;   /* Alt sect/zone */
        out[8] = out[9] = 0xFF;   /* Alt track/volume */
        out[10] = out[11] = 0xFF; /* Sectors/track */
        out[12] = out[13] = 0xFF; /* Bytes/sector */
        out[16] = out[17] = 0xFF; /* Track skew */
        out[18] = out[19] = 0xFF; /* Cylinder skew */
    }
    else if (page == 0x04)
    {
        out[2] = out[3] = out[4] = 0xFF; /* Cylinders */
        out[5] = 0xFF;                   /* Heads */
    }
}


/* Fill out[] with page 1, 2, 3 or 4 for the given page control; returns its
 * length (2 + the page length byte). */
static int scsihdd_mode_page(SCSIHDDDevice *hdd, int page, int pc, uint8_t *out)
{
    int i = scsihdd_mode_page_index(page);

    switch (pc)
    {
    case MODE_PC_CHANGEABLE:
        scsihdd_changeable_mode_page(page, out);
        break;
    case MODE_PC_DEFAULT:
        scsihdd_default_mode_page(hdd, page, out);
        break;
    case MODE_PC_SAVED:
        memcpy(out, hdd->modeSaved[i], SCSI_HDD_MODE_PAGE_BYTES);
        break;
    default:
        memcpy(out, hdd->modeCurrent[i], SCSI_HDD_MODE_PAGE_BYTES);
        break;
    }
    return 2 + scsihdd_mode_page_length[i];
}


/* Current and saved values start as the defaults. */
static void scsihdd_mode_pages_init(SCSIHDDDevice *hdd)
{
    for (int i = 0; i < SCSI_HDD_MODE_PAGES; i++)
    {
        scsihdd_default_mode_page(hdd, i + 1, hdd->modeCurrent[i]);
    }
    memcpy(hdd->modeSaved, hdd->modeCurrent, sizeof(hdd->modeSaved));
}


/*
 * Apply a whole MODE SELECT parameter list (SCSI-2 section 8.2.8): a 4-byte
 * header whose byte 3 is the block descriptor length, the block descriptors,
 * then pages of page code + page length + fields. For pages 1 to 4, every
 * byte (or bit) the drive lets the host change is copied into the current
 * values; the other bytes keep their value.
 *
 * Not done here (BUGS.md C5): the block descriptor's block length is not
 * taken, and a value that the drive would refuse (a non-changeable field that
 * differs, or a skew larger than the sectors per track, Appendix B) is not
 * refused.
 */
static void scsihdd_apply_mode_select(SCSIHDDDevice *hdd)
{
    const uint8_t *d = hdd->modeSelectData;
    int len = hdd->modeSelectLength;
    char hex[3 * SCSI_HDD_MODE_SELECT_MAX + 1];
    int h = 0;

    for (int k = 0; k < len; k++)
    {
        h += snprintf(&hex[h], sizeof(hex) - (size_t)h, "%02X ", d[k]);
    }
    scsihdd_log(hdd, "MODE SELECT parameter list (%d bytes): %s", len, hex);

    if (len < 4)
    {
        return;
    }

    int at = 4 + d[3]; /* skip the header and the block descriptors */
    while (at + 2 <= len)
    {
        int page = d[at] & 0x3f;
        int plen = d[at + 1];
        int i = scsihdd_mode_page_index(page);

        if (i >= 0)
        {
            uint8_t mask[SCSI_HDD_MODE_PAGE_BYTES];
            scsihdd_changeable_mode_page(page, mask);

            int end = 2 + plen;
            if (end > 2 + scsihdd_mode_page_length[i])
            {
                end = 2 + scsihdd_mode_page_length[i];
            }
            for (int k = 2; k < end && at + k < len; k++)
            {
                hdd->modeCurrent[i][k] =
                    (uint8_t)((hdd->modeCurrent[i][k] & ~mask[k]) | (d[at + k] & mask[k]));
            }
            if (page == 0x03)
            {
                scsihdd_log(hdd, "MODE SELECT page 3: track skew %u, cylinder skew %u",
                            scsi_device_scsi_get_u16_be(&hdd->modeCurrent[i][16]),
                            scsi_device_scsi_get_u16_be(&hdd->modeCurrent[i][18]));
            }
            else
            {
                scsihdd_log(hdd, "MODE SELECT page %d taken", page);
            }
        }
        else
        {
            scsihdd_log(hdd, "MODE SELECT page 0x%02X not kept", page);
        }
        at += 2 + plen;
    }
}


/*
 * MODE SENSE(6) (0x1A) - first ported 2026-07-17 from RetroCore
 * SCSIHDD.CommandModeSense (SCSIHDD.cs); the page set changed 2026-10-31 to
 * the Micropolis 1370 one.
 *
 * Reply: 4-byte mode parameter header + 8-byte block descriptor + the
 * requested page(s). The drive has pages 1 to 4 and no others (Appendix B of
 * 101689 rev D: "The 1370 Series supports four pages"). Page 0x3F returns all
 * four in ASCENDING order, 1, 2, 3, 4, as SCSI-2 section 8.2.10 asks.
 *
 * DISK-MM depends on that order and on the page lengths: FORMAT-INITIALIZE
 * reads MODE SENSE page 0x3F and takes the sectors per track from byte 42
 * (page 3 byte 10) and the heads from byte 61 (page 4 byte 5) at fixed
 * offsets, then sets the size of partition entry 7 to (heads + 1) x sectors
 * per track. The C# reply (pages 0x30 Apple, 0x08, 0x04, 0x03, 0x02, 0x01,
 * 0x00, downwards) put zeros at both offsets, entry 7 got size 0, and the next
 * DISK-MM command stopped with "(CS) Error in consistency of partition block"
 * (BUGS.md C5; traced in DISK-MM at 125100-125146 and 133034-133072).
 *
 * An unimplemented single page (other than 0x3f) fails with CHECK CONDITION /
 * ILLEGAL REQUEST / invalid field in CDB (0x24).
 *
 * LUN note: the C# checks the LUN inside CommandModeSense (bad_lun); here the
 * generic lun != 0 rejection in SCSIHDD_Command's preamble already covers it.
 */
static void scsihdd_command_mode_sense(SCSIHDDDevice *hdd)
{
    SCSITarget *t = &hdd->target;
    int page = t->scsi_cmdbuf[2] & 0x3f;
    int pc = (t->scsi_cmdbuf[2] >> 6) & 0x03;
    int size = t->scsi_cmdbuf[4];
    int pos = 1;
    uint32_t dsize;
    int pmax;
    int pmin;
    int p;

    scsihdd_log(hdd, "command MODE SENSE(6) page=0x%02X pc=%d alloc=0x%02X link=0x%02X", page,
                pc, size, t->scsi_cmdbuf[5]);

    if (page != 0x3f && scsihdd_mode_page_index(page) < 0)
    {
        scsihdd_log(hdd, "mode sense page 0x%02X unhandled", page);
        scsi_device_status_complete(t, SS_CHECK_CONDITION);
        scsi_device_sense(t, false, SK_ILLEGAL_REQUEST, 0x24, 0x00);
        return;
    }

    t->scsi_cmdbuf[pos++] = 0x00; /* medium type */
    t->scsi_cmdbuf[pos++] = 0x00; /* WP, cache */

    /* Block descriptor. NOTE: the "number of blocks" field is filled from
     * DiskSCSI_LastLBA() because the C# uses hdinfo.DiskSizeInBlocks here and
     * that field actually holds the LAST LBA (count-1) - copied verbatim, do
     * not "correct" without changing both sides. */
    dsize = scsi_disk_last_lba(&hdd->hdinfo);
    t->scsi_cmdbuf[pos++] = 0x08; /* block descriptor length */
    t->scsi_cmdbuf[pos++] = 0x00;
    scsi_device_scsi_put_u24_be(&t->scsi_cmdbuf[pos], dsize);
    pos += 3;
    t->scsi_cmdbuf[pos++] = 0x00;
    scsi_device_scsi_put_u24_be(&t->scsi_cmdbuf[pos], hdd->hdinfo.sectorbytes);
    pos += 3;

    pmin = (page == 0x3f) ? 0x01 : page;
    pmax = (page == 0x3f) ? SCSI_HDD_MODE_PAGES : page;

    for (p = pmin; p <= pmax; p++)
    {
        /* Current, changeable, default or saved values, by the page control
         * field (CDB byte 2 bits 7-6). DISK-MM FORMAT-INITIALIZE reads the
         * default and the saved values of pages 3 and 4 after FORMAT UNIT and
         * compares them (BUGS.md C5). */
        pos += scsihdd_mode_page(hdd, p, pc, &t->scsi_cmdbuf[pos]);
    }

    t->scsi_cmdbuf[0] = (uint8_t)pos; /* mode data length, as the C# sets it */
    if (pos > size)
    {
        pos = size;
    }

    scsi_device_data_in(t, SBUF_MAIN, pos);
    scsi_device_status_complete(t, SS_GOOD);
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
        /* Every byte the target took in COMMAND phase (6, 10, 12 or 16 by the
         * command group), so the length fields of the long CDBs show too. */
        char bytes[3 * 16 + 1];
        int n = t->scsi_cmdsize < 16 ? t->scsi_cmdsize : 16;
        int w = 0;
        for (int i = 1; i < n; i++)
        {
            w += snprintf(bytes + w, sizeof(bytes) - (size_t)w, i == 1 ? "%02X" : ",%02X",
                          t->scsi_cmdbuf[i]);
        }
        bytes[w] = '\0';
        scsihdd_log(hdd, "CDB op=0x%02X cdb=%s lun=%d", cmd, bytes, lun);
    }

    /* LUNs other than 0 are rejected, except for the three commands that must
     * always answer (SCSIHDDMicropolis.scsi_command). */
    if (cmd != SC_REQUEST_SENSE && cmd != SC_INQUIRY && cmd != SC_TEST_UNIT_READY)
    {
        if (lun != 0)
        {
            scsi_device_report_bad_lun(t, cmd, (uint8_t)lun);
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
        scsi_device_data_in(t, SBUF_SENSE, 4);
        scsi_device_status_complete(t, SS_GOOD);
        break;

    case SC_INQUIRY:
        scsihdd_command_inquiry(hdd);
        break;

    case SC_READ_CAPACITY:
        scsihdd_command_read_capacity(hdd);
        break;

    case SC_READ_6:
        /* 21-bit LBA, and a transfer length where 0 means 256. */
        hdd->lba = (int)(scsi_device_scsi_get_u24_be(&t->scsi_cmdbuf[1]) & 0x1fffff);
        hdd->blocks = t->scsi_cmdbuf[4];
        if (hdd->blocks == 0)
        {
            hdd->blocks = 256;
        }

        scsihdd_log(hdd, "command READ(6) lba=%d blocks=%d", hdd->lba, hdd->blocks);

        if ((uint32_t)hdd->lba > scsi_disk_last_lba(&hdd->hdinfo))
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
            scsi_device_data_in(t, SBUF_DATA, hdd->blocks * hdd->hdinfo.sectorbytes);
            scsi_device_status_complete(t, SS_GOOD);
        }
        else
        {
            scsi_device_status_complete(t, SS_CHECK_CONDITION);
            scsi_device_sense(t, false, SK_ILLEGAL_REQUEST, 0x24, 0x00);
        }
        break;

    case SC_WRITE_6:
        hdd->lba = (int)(scsi_device_scsi_get_u24_be(&t->scsi_cmdbuf[1]) & 0x1fffff);
        hdd->blocks = t->scsi_cmdbuf[4];
        if (hdd->blocks == 0)
        {
            hdd->blocks = 256;
        }

        scsihdd_log(hdd, "command WRITE(6) lba=%d blocks=%d", hdd->lba, hdd->blocks);

        scsi_device_data_out(t, SBUF_DATA, hdd->blocks * hdd->hdinfo.sectorbytes);
        scsi_device_status_complete(t, SS_GOOD);
        break;

    case SC_READ_10:
        hdd->lba = (int)scsi_device_scsi_get_u32_be(&t->scsi_cmdbuf[2]);
        hdd->blocks = scsi_device_scsi_get_u16_be(&t->scsi_cmdbuf[7]);

        scsihdd_log(hdd, "command READ(10) lba=%d blocks=%d", hdd->lba, hdd->blocks);

        hdd->sectorDataValid = scsihdd_read_block(hdd, hdd->lba);
        hdd->cur_lba = hdd->lba;

        if (hdd->sectorDataValid)
        {
            scsi_device_data_in(t, SBUF_DATA, hdd->blocks * hdd->hdinfo.sectorbytes);
            scsi_device_status_complete(t, SS_GOOD);
        }
        else
        {
            scsi_device_status_complete(t, SS_CHECK_CONDITION);
            scsi_device_sense(t, false, SK_ILLEGAL_REQUEST, 0x24, 0x00);
        }
        break;

    case SC_WRITE_10:
        hdd->lba = (int)scsi_device_scsi_get_u32_be(&t->scsi_cmdbuf[2]);
        hdd->blocks = scsi_device_scsi_get_u16_be(&t->scsi_cmdbuf[7]);

        scsihdd_log(hdd, "command WRITE(10) lba=%d blocks=%d", hdd->lba, hdd->blocks);

        scsi_device_data_out(t, SBUF_DATA, hdd->blocks * hdd->hdinfo.sectorbytes);
        scsi_device_status_complete(t, SS_GOOD);
        break;

    case SC_SEEK_6:
        hdd->lba = (int)(scsi_device_scsi_get_u24_be(&t->scsi_cmdbuf[1]) & 0x1fffff);
        if (((t->scsi_cmdbuf[1] >> 5) != 0) ||
            ((uint32_t)hdd->lba > scsi_disk_last_lba(&hdd->hdinfo)))
        {
            /* NOTE: RetroCore reports SS_CONDITION_MET (not CHECK CONDITION) and
             * writes the sense KEY straight into sense byte 0, where the
             * response code (0x70) belongs. Both look wrong; both are copied
             * verbatim because this is the behaviour SINTRAN sees. */
            scsi_device_status_complete(t, SS_CONDITION_MET);
            t->scsi_sense_buffer[0] = SK_HARDWARE_ERROR;
        }
        else
        {
            scsi_device_status_complete(t, SS_GOOD);
        }
        break;

    case SC_RESERVE_6:
        scsi_device_status_complete(t, SS_GOOD);
        break;

    case SC_INIT_DRIVE_PARAMS: /* Micropolis vendor 0x0C */
        scsi_device_data_out(t, SBUF_DATA, 8);
        scsi_device_status_complete(t, SS_GOOD);
        break;

    case SC_MODE_SELECT_6:
        /* The parameter list arrives in the DATA OUT phase; scsihdd_put_data
         * applies it to the current values of pages 1 to 4 when its last byte
         * is in. The block descriptor is not taken: the sector size stays at
         * the image's 1024, whatever the descriptor asks for. */
        scsihdd_log(hdd, "command MODE SELECT(6) paramLen=%d", t->scsi_cmdbuf[4]);
        hdd->modeSelectLength = t->scsi_cmdbuf[4];
        if (t->scsi_cmdbuf[4] != 0)
        {
            scsi_device_data_out(t, SBUF_DATA, t->scsi_cmdbuf[4]);
        }
        scsi_device_status_complete(t, SS_GOOD);
        break;

    case SC_MODE_SENSE_6:
        /* Ported 2026-07-17 from RetroCore SCSIHDD.CommandModeSense - was
         * previously missing here (fell through to ReportBadCmd). */
        scsihdd_command_mode_sense(hdd);
        break;

    case SC_START_STOP_UNIT:
        scsihdd_log(hdd, "command %s UNIT", (t->scsi_cmdbuf[4] & 0x01) ? "START" : "STOP");
        scsi_device_status_complete(t, SS_GOOD);
        break;

    case SC_VERIFY_10:
        if ((t->scsi_cmdbuf[1] & 0x02) == 0)
        {
            scsi_device_status_complete(t, SS_GOOD);
        }
        else
        {
            scsi_device_report_bad_cmd(t, cmd);
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
         *
         * What it does do: the drive formats with the current values of the
         * format and geometry pages and keeps them as the saved values, which
         * MODE SENSE with page control 3 then reports. DISK-MM
         * FORMAT-INITIALIZE checks the saved values after the format (traced
         * 2026-10-31 under the DAP server; DISK-MM-B00 is loaded at file word +
         * 065400):
         *   - 124516-124565 returns NOT (page 3 good AND page 4 good); a 1
         *     prints "(DF) Must be formatted" (message code 050266);
         *   - 124175-124235 reads a page with page control 2 (default) and 3
         *     (saved) and compares them at 124236;
         *   - page 3 is good when the saved cylinder skew (page bytes 18-19)
         *     is ceil(sectors per track / 3), worked out at 124077, the saved
         *     track skew is 0 (124130), and the alternate sectors per zone,
         *     alternate tracks per zone and per volume, sectors per track and
         *     bytes per sector are the same in both;
         *   - page 4 is good when cylinders and heads are the same in both.
         * Before the format DISK-MM sends MODE SELECT of the default page 3
         * with track skew 0 and cylinder skew 6 (for 18 sectors per track),
         * and of the default page 4 unchanged. That the Micropolis keeps the
         * current values as the saved ones on FORMAT UNIT is inferred from
         * that check; its SCSI document (110011) has not been found
         * (BUGS.md C5).
         *
         * FmtData (CDB byte 1 bit 4) means a defect list follows in a DATA OUT
         * phase (SCSI-2 section 8.2.1). Its 4-byte header is taken; defect
         * descriptors after it are not (scsihdd_put_data logs an error).
         */
        scsihdd_log(hdd, "command FORMAT UNIT cdb1=0x%02X (image NOT modified)",
                    t->scsi_cmdbuf[1]);
        if (t->scsi_cmdbuf[1] & 0x10)
        {
            scsi_device_data_out(t, SBUF_DATA, SCSI_HDD_DEFECT_HEADER_BYTES);
        }
        memcpy(hdd->modeSaved, hdd->modeCurrent, sizeof(hdd->modeSaved));
        scsi_device_status_complete(t, SS_GOOD);
        break;

    case SC_RECEIVE_DIAGNOSTIC_RESULTS:
    case SC_SEND_DIAGNOSTIC:
    {
        int size = scsi_device_scsi_get_u16_be(&t->scsi_cmdbuf[3]);
        int pos = 0;

        if (cmd == SC_SEND_DIAGNOSTIC && (t->scsi_cmdbuf[1] & 4))
        {
            scsi_device_status_complete(t, SS_GOOD);
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
        scsi_device_data_in(t, SBUF_MAIN, size);
        scsi_device_status_complete(t, SS_GOOD);
        break;
    }

    /*
     * READ BUFFER (3Ch) and WRITE BUFFER (3Bh): the drive's data buffer as a
     * diagnostic of drive memory and SCSI bus integrity. They never touch the
     * medium. The Micropolis 1370 series lists both (product description
     * 101689 rev D, table 2-4) and refers to the ANSI SCSI standard and the
     * Common Command Set for their format; the CCS modes are defined in SCSI-2
     * X3T9.2/86-109 sections 8.2.12 and 8.2.17. Only mode 000b, combined
     * header and data, is done: SCSI-2 notes 81 and 93 say modes 000b and 001b
     * are the ones kept for CCS-era products, and 001b is vendor-specific with
     * no description to hand. Any other mode is refused with ILLEGAL REQUEST,
     * INVALID FIELD IN CDB, so a program that uses one fails visibly.
     *
     * Found missing by SCSI-TV-B00 device tests 4 "SCSI bus data transport"
     * and 5 "SCSI bus stress", which send READ BUFFER mode 000b to find the
     * buffer length and then write and read back test patterns (the former BUGS.md C4).
     */
    case SC_READ_BUFFER:
    case SC_WRITE_BUFFER:
    {
        int mode = t->scsi_cmdbuf[1] & 0x07;
        uint32_t length = scsi_device_scsi_get_u24_be(&t->scsi_cmdbuf[6]);
        const uint32_t whole = SCSI_HDD_BUFFER_HEADER_BYTES + SCSI_HDD_DATA_BUFFER_BYTES;

        scsihdd_log(hdd, "command %s mode=%d length=%u", cmd == SC_READ_BUFFER ? "READ BUFFER" : "WRITE BUFFER",
                    mode, length);

        if (mode != 0)
        {
            scsi_device_status_complete(t, SS_CHECK_CONDITION);
            scsi_device_sense(t, false, SK_ILLEGAL_REQUEST, 0x24, 0x00);
            break;
        }

        if (cmd == SC_READ_BUFFER)
        {
            /* 8.2.12.1: the transfer ends at the allocation length or at the end
             * of header plus buffer, whichever is less. */
            scsi_device_data_in(t, SBUF_DATA, (int)(length < whole ? length : whole));
            scsi_device_status_complete(t, SS_GOOD);
        }
        else if (length > whole)
        {
            /* 8.2.17.1: a parameter list longer than four plus the buffer
             * capacity is CHECK CONDITION, ILLEGAL REQUEST. The standard names
             * only the sense key for this mode; INVALID FIELD IN CDB is the
             * code it gives for the same fault in data mode (8.2.17.3). */
            scsi_device_status_complete(t, SS_CHECK_CONDITION);
            scsi_device_sense(t, false, SK_ILLEGAL_REQUEST, 0x24, 0x00);
        }
        else
        {
            scsi_device_data_out(t, SBUF_DATA, (int)length);
            scsi_device_status_complete(t, SS_GOOD);
        }
        break;
    }

    default:
        scsihdd_log(hdd, "command 0x%02X *** UNKNOWN ***", cmd);
        scsi_device_report_bad_cmd(t, cmd);
        break;
    }
}


static void scsihdd_device_reset(SCSIHDDDevice *hdd)
{
    if (!hdd)
    {
        return;
    }

    scsi_device_device_reset(&hdd->target);

    hdd->cur_lba = -1;
    hdd->sectorDataValid = false;

    /* After a reset the current mode values are the saved ones again
     * (SCSI-2 section 8.3.3: saved values are loaded at power-on and reset). */
    memcpy(hdd->modeCurrent, hdd->modeSaved, sizeof(hdd->modeCurrent));

    /* After a reset the drive posts UNIT ATTENTION / power-on-or-reset.
     * Verified trace: 70 00 06 00 00 00 00 0A 00 00 00 00 29 00 00 00 00 00 */
    scsi_device_sense(&hdd->target, false, SK_UNIT_ATTENTION, 0x29, 0x00);
}


void scsi_hdd_init(SCSIHDDDevice *hdd, SCSIBus *bus, uint8_t scsi_id, struct Device *owner,
                   int unit, SCSIDiskType disk_type)
{
    memset(hdd, 0, sizeof(SCSIHDDDevice));

    hdd->owner = owner;
    hdd->unit = unit;
    hdd->cur_lba = -1;

    scsi_disk_set_type(&hdd->hdinfo, disk_type);
    scsihdd_mode_pages_init(hdd);

    /* Hooks must be set before SCSITarget_Init - it calls DeviceReset, which
     * touches the sense buffer through them. */
    hdd->target.impl = hdd;
    hdd->target.scsi_command = scsihdd_command;
    hdd->target.scsi_get_data = scsihdd_get_data;
    hdd->target.scsi_put_data = scsihdd_put_data;

    scsi_device_init(&hdd->target, bus, scsi_id, "SCSI-HDD");

    scsihdd_device_reset(hdd);
}
