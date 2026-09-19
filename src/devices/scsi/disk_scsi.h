/*
 * nd100x - ND-100 emulator
 *
 * disk_scsi.h - SCSI disk geometry and drive identity
 *
 * Ported from RetroCore:
 *   <RetroCore>\Emulated.HW\Common\SCSI\SCSIHDDMicropolis.cs
 *   (hdinfo lives in SCSIHDD.cs)
 *
 * Mirrors disk_smd.h: the concrete drive table lives here, the controller logic
 * lives in scsi_hdd.c.
 *
 * References:
 *   https://www.micropolis.com/support/hard-drives/1375
 *   http://www.bitsavers.org/pdf/micropolis/101689d_1370.pdf
 *   https://www.ndwiki.org/wiki/ND-110_Satellite_9883.21238
 */

#ifndef DISK_SCSI_H
#define DISK_SCSI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    SCSI_DISK_UNKNOWN = 0,
    /* Norsk Data ND-100/ND-110 variant: 1024-byte sectors, 18 sectors/track.
     * This is the drive the ND-3201 boots from. */
    SCSI_DISK_MICROPOLIS_1375_ND,
    /* Standard variant: 512-byte sectors, 36 sectors/track. Not used by ND. */
    SCSI_DISK_MICROPOLIS_1375,
    /* Sun-2 variant. Not used by ND. */
    SCSI_DISK_MICROPOLIS_1355
} SCSIDiskType;

/* hdinfo from SCSIHDD.cs. */
// clang-format off
typedef struct {
    SCSIDiskType diskType;
    uint16_t cylinders;
    uint8_t  heads;
    uint16_t sectors;
    uint16_t sectorbytes;   /* ND machines need 1024-byte sectors */

    /* SCSI INQUIRY identity, space-padded by DiskSCSI_SetDiskType. */
    char vendor[9];         /* 8 chars + NUL, INQUIRY bytes 8-15  */
    char product[17];       /* 16 chars + NUL, INQUIRY bytes 16-31 */
    char revision[5];       /* 4 chars + NUL, INQUIRY bytes 32-35  */

    /* Vendor command 0x0C INIT DRIVE PARAMS payload. */
    uint8_t drive_params[8];
} SCSIDiskInfo;
// clang-format on

/*
 * Last addressable LBA = cylinders*heads*sectors - 1.
 *
 * NOTE the -1: RetroCore calls this "DiskSizeInBlocks" but it is the LAST LBA,
 * not a block count. For the Micropolis 1375-ND that is
 *   898 * 8 * 18 - 1 = 129311 (0x0001F91F)
 * and the medium is 129312 * 1024 = 132,415,488 bytes - which matches the byte
 * size of SCSI-K.image exactly.
 *
 * READ CAPACITY must report this geometry-derived value. Reporting a
 * directory-derived one makes SINTRAN's completion handler (ECAPD) raise
 * DISC-TRANSFER-ERROR / STATUS 100020B.
 */
uint32_t DiskSCSI_LastLBA(const SCSIDiskInfo *disk);

/* Fill in geometry + identity for a drive type. */
void DiskSCSI_SetDiskType(SCSIDiskInfo *disk, SCSIDiskType dt);

#endif // DISK_SCSI_H
