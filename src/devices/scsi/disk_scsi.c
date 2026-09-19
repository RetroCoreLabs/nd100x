/*
 * nd100x - ND-100 emulator
 *
 * disk_scsi.c - SCSI disk geometry and drive identity
 *
 * Ported from RetroCore:
 *   <RetroCore>\Emulated.HW\Common\SCSI\SCSIHDDMicropolis.cs
 *
 * ---- Verified SCSI traces from real NDMICROP 1375 hardware --------------
 * (captured from an ND-110 system, recorded in SCSIHDDMicropolis.cs)
 *
 *   1. REQUEST SENSE after power-on -> 18 bytes:
 *        70 00 06 00 00 00 00 0A 00 00 00 00 29 00 00 00 00 00
 *      sense key 0x06 = UNIT ATTENTION, ASC 0x29 = power on / reset
 *
 *   2. READ CAPACITY  CDB: 25 00 00 00 00 00 00 00 00 00
 *      -> 8 bytes, STATUS 0x00 (GOOD)
 *      129,312 sectors * 1024 = 132,415,488 bytes, last LBA 129,311
 *
 *   3. INQUIRY        CDB: 12 00 00 00 24 00
 *      -> 36 bytes (alloc_len 0x24), STATUS 0x00 (GOOD)
 *
 * Cross-check on the geometry: 18 * 1024 == 36 * 512 == 18,432 bytes/track.
 * The ND variant uses 1024-byte sectors; the standard variant uses 512.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "../devices_types.h"
#include "../devices_protos.h"


uint32_t DiskSCSI_LastLBA(const SCSIDiskInfo *disk)
{
    if (!disk)
    {
        return 0;
    }

    /* cylinders * heads * sectors - 1 (see the note in disk_scsi.h: this is the
     * LAST LBA, despite RetroCore naming it DiskSizeInBlocks). */
    return (uint32_t)(disk->cylinders * disk->heads * disk->sectors - 1);
}


/* Copy src into a fixed-width field, space-padded (no NUL padding) - SCSI
 * INQUIRY identity fields are blank-filled ASCII. */
static void DiskSCSI_SetField(char *field, size_t width, const char *src)
{
    memset(field, ' ', width);
    field[width] = '\0';

    size_t len = strlen(src);
    if (len > width)
    {
        len = width;
    }
    memcpy(field, src, len);
}


void DiskSCSI_SetDiskType(SCSIDiskInfo *disk, SCSIDiskType dt)
{
    if (!disk)
    {
        return;
    }

    memset(disk, 0, sizeof(SCSIDiskInfo));
    disk->diskType = dt;

    switch (dt)
    {
    case SCSI_DISK_MICROPOLIS_1375_ND:
        /* Norsk Data ND-100/ND-110 variant.
         * 898 * 8 * 18 * 1024 = 132,415,488 bytes, last LBA 129,311. */
        disk->cylinders = 898;
        disk->heads = 8;
        disk->sectors = 18;
        disk->sectorbytes = 1024;
        DiskSCSI_SetField(disk->vendor, 8, "NDMICROP");
        DiskSCSI_SetField(disk->product, 16, "1375");
        DiskSCSI_SetField(disk->revision, 4, "B0C");
        {
            /* Drive params: {0, 153, 4, 0, 128, 0, 64, 11} */
            static const uint8_t nd_params[8] = {0, 153, 4, 0, 128, 0, 64, 11};
            memcpy(disk->drive_params, nd_params, sizeof(nd_params));
        }
        break;

    case SCSI_DISK_MICROPOLIS_1375:
        /* Standard variant - not used by the ND path. */
        disk->cylinders = 898;
        disk->heads = 8;
        disk->sectors = 36;
        disk->sectorbytes = 512;
        DiskSCSI_SetField(disk->vendor, 8, "MICROPOL");
        DiskSCSI_SetField(disk->product, 16, "1375");
        DiskSCSI_SetField(disk->revision, 4, "B0C");
        break;

    case SCSI_DISK_MICROPOLIS_1355:
        /* Sun-2 variant. Spec says 1024 cyl / 36 sectors, but the Sun-2 disk
         * uses 1018 / 34. Not used by the ND path. */
        disk->cylinders = 1018;
        disk->heads = 8;
        disk->sectors = 34;
        disk->sectorbytes = 512;
        DiskSCSI_SetField(disk->vendor, 8, "MICROPOL");
        DiskSCSI_SetField(disk->product, 16, "1355");
        DiskSCSI_SetField(disk->revision, 4, "");
        break;

    case SCSI_DISK_UNKNOWN:
    default:
        /* Defaults from hdinfo's constructor - "set some default values to
         * avoid crash". Same as the ND drive. */
        disk->cylinders = 898;
        disk->heads = 8;
        disk->sectors = 18;
        disk->sectorbytes = 1024;
        DiskSCSI_SetField(disk->vendor, 8, "NDMICROP");
        DiskSCSI_SetField(disk->product, 16, "1375");
        DiskSCSI_SetField(disk->revision, 4, "B0C");
        break;
    }
}
