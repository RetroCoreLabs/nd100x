/*
 * disk_winchester.c - Winchester drive geometry table and CHS to LBA conversion.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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

#include "disk_winchester.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../devices_types.h"
#include "../devices_protos.h"

/*
 * Geometry table. Sources per entry are in disk_winchester.h; all Winchester
 * drives use 1024-byte (512-word) sectors per ND-11.015.01 sec 2.1.
 */
static const struct { WDDiskType type; int heads; int sectors; int cylinders; } wd_layouts[] = {
    { WD_DISK_DT014, 3, 12, 384 },
    { WD_DISK_DT021, 3, 12, 605 },
    { WD_DISK_DT023, 3, 9, 830 },
    { WD_DISK_DT028, 3, 9, 1022 },
    { WD_DISK_MICROPOLIS_1304, 6, 9, 830 },   /* DT045 */
    { WD_DISK_MICROPOLIS_1325, 8, 9, 1022 },  /* DT074 */
};

WDDiskType wd_disk_type_for_size(size_t fileSize)
{
    size_t i;
    WDDiskType best = WD_DISK_MICROPOLIS_1325;
    size_t bestSize = 0;

    for (i = 0; i < sizeof(wd_layouts) / sizeof(wd_layouts[0]); i++)
    {
        size_t size = (size_t)wd_layouts[i].cylinders * (size_t)wd_layouts[i].heads *
                      (size_t)wd_layouts[i].sectors * 1024u;
        /* at most 2% above the file */
        if (size <= fileSize + fileSize / 50 && size > bestSize)
        {
            best = wd_layouts[i].type;
            bestSize = size;
        }
    }
    return best;
}

static WDDiskType wd_preset[2];

void wd_disk_preset_format(int unit, WDDiskType dt)
{
    if (unit >= 0 && unit < 2)
        wd_preset[unit] = dt;
}

WDDiskType wd_disk_preset_get(int unit)
{
    return (unit >= 0 && unit < 2) ? wd_preset[unit] : WD_DISK_TYPE_UNKNOWN;
}

static const struct { const char *name; WDDiskType type; } wd_names[] = {
    { "DT014", WD_DISK_DT014 }, { "14MB", WD_DISK_DT014 },
    { "DT021", WD_DISK_DT021 }, { "21MB", WD_DISK_DT021 },
    { "DT023", WD_DISK_DT023 }, { "23MB", WD_DISK_DT023 },
    { "DT028", WD_DISK_DT028 }, { "28MB", WD_DISK_DT028 },
    { "DT045", WD_DISK_MICROPOLIS_1304 }, { "45MB", WD_DISK_MICROPOLIS_1304 },
    { "DT074", WD_DISK_MICROPOLIS_1325 }, { "74MB", WD_DISK_MICROPOLIS_1325 }
};

WDDiskType wd_disk_type_from_name(const char *name)
{
    size_t i, k;

    if (!name)
        return WD_DISK_TYPE_UNKNOWN;
    if (strncmp(name, "DISC-", 5) == 0 || strncmp(name, "disc-", 5) == 0)
        name += 5;
    for (i = 0; i < sizeof(wd_names) / sizeof(wd_names[0]); i++)
    {
        const char *want = wd_names[i].name;
        for (k = 0; want[k] && name[k]; k++)
        {
            char c = name[k];
            if (c >= 'a' && c <= 'z')
                c = (char)(c - 'a' + 'A');
            if (c != want[k])
                break;
        }
        if (want[k] == '\0' && name[k] == '\0')
            return wd_names[i].type;
    }
    return WD_DISK_TYPE_UNKNOWN;
}

void wd_disk_set_type(WDDiskInfo *disk, WDDiskType dt)
{
    if (!disk)
    {
        return;
    }

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

    case WD_DISK_DT014:
        disk->headsPrCylinder = 3;
        disk->sectorsPrTrack = 12;
        disk->maxCylinders = 384;
        disk->badTracks = 0; /* not in the layout table */
        break;

    case WD_DISK_DT021:
        disk->headsPrCylinder = 3;
        disk->sectorsPrTrack = 12;
        disk->maxCylinders = 605;
        disk->badTracks = 0; /* not in the layout table */
        break;

    case WD_DISK_DT023:
        disk->headsPrCylinder = 3;
        disk->sectorsPrTrack = 9;
        disk->maxCylinders = 830;
        disk->badTracks = 0; /* not in the layout table */
        break;

    case WD_DISK_DT028:
        disk->headsPrCylinder = 3;
        disk->sectorsPrTrack = 9;
        disk->maxCylinders = 1022;
        disk->badTracks = 0; /* not in the layout table */
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
long wd_disk_chs_to_lba(const WDDiskInfo *disk, int cylinder, int head, int sector)
{
    if (!disk || disk->headsPrCylinder <= 0 || disk->sectorsPrTrack <= 0)
    {
        return -1;
    }

    return ((long)cylinder * disk->headsPrCylinder + head) * disk->sectorsPrTrack + sector;
}
