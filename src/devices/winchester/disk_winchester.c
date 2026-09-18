/*
 * nd100x - ND100 Virtual Machine
 *
 * Copyright (c) 2026 Ronny Hansen
 *
 * This file is originated from the nd100x project and the RetroCore project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the nd100em
 * distribution in the file COPYING); if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../devices_types.h"
#include "../devices_protos.h"

#include "disk_winchester.h"

/*
 * Geometry table. Sources per entry are in disk_winchester.h; all Winchester
 * drives use 1024-byte (512-word) sectors per ND-11.015.01 sec 2.1.
 */
void DiskWinchester_SetDiskType(WDDiskInfo *disk, WDDiskType dt)
{
    if (!disk)
        return;

    disk->diskType = dt;
    disk->bytesPrSector = 1024; /* 1024 bytes = 512 words, all drives */

    switch (dt)
    {
    case WD_DISK_CDC_9415_5_WREN:
        disk->headsPrCylinder = 5;
        disk->sectorsPrTrack = 9;
        disk->maxCylinders = 697;
        disk->badTracks = 90;
        break;

    case WD_DISK_MICROPOLIS_1304:
        disk->headsPrCylinder = 6;
        disk->sectorsPrTrack = 9;
        disk->maxCylinders = 830;
        disk->badTracks = 120;
        break;

    case WD_DISK_RODIME_RO208:
        disk->headsPrCylinder = 8;
        disk->sectorsPrTrack = 9;
        disk->maxCylinders = 640;
        disk->badTracks = 0; /* manual leaves the bad-track column blank */
        break;

    case WD_DISK_CMI_CM6640:
        disk->headsPrCylinder = 6;
        disk->sectorsPrTrack = 9;
        disk->maxCylinders = 640;
        disk->badTracks = 18;
        break;

    case WD_DISK_MICROPOLIS_1325:
        disk->headsPrCylinder = 8;
        disk->sectorsPrTrack = 9;
        disk->maxCylinders = 1024;
        disk->badTracks = 14;
        break;

    case WD_DISK_TYPE_UNKNOWN:
    default:
        /* Leave the geometry at zero so the caller can tell it was never set;
         * an operation against it will fail the address bound check rather
         * than compute a nonsense position. */
        disk->headsPrCylinder = 0;
        disk->sectorsPrTrack = 0;
        disk->maxCylinders = 0;
        disk->badTracks = 0;
        return;
    }
}

/*
 * CHS -> LBA, the same formula the SMD controller uses (device_smd.c
 * ConvertCHStoLBA) so an image prepared for one geometry reads the same way
 * here:
 *     LBA = (cylinder * headsPrCylinder + head) * sectorsPrTrack + sector
 * Sector numbering starts at 0.
 *
 * Returns -1 when the drive has no geometry set.
 */
long DiskWinchester_ChsToLba(const WDDiskInfo *disk, int cylinder, int head, int sector)
{
    if (!disk || disk->headsPrCylinder <= 0 || disk->sectorsPrTrack <= 0)
        return -1;

    return ((long)cylinder * disk->headsPrCylinder + head) * disk->sectorsPrTrack + sector;
}
