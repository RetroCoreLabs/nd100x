/*
 * disk_smd.c - SMD disk geometry per disk type (heads, sectors, cylinders).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2025 Ronny Hansen
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


#include "disk_smd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../devices_types.h"
#include "../devices_protos.h"

/* The SINTRAN layouts. isDefault marks the one layout used for a file of that size class when
 * the file length does not match any layout exactly: several formats of one drive size overlap
 * (288 MB: R, S, F, E, N; 450 MB: S, F, N; 140 MB: S, F) and the file length cannot tell them
 * apart. The default of a class is the first listed format of the layout table (288MB-R = DT288,
 * 450MB-S = DT450, 140MB-F = DT140). A formatN key in the config file overrides the choice. */
static const struct { DiskType type; int heads; int sectors; int cylinders; int isDefault; } smd_layouts[] = {
    { DISK_38_MB,    5, 18,  411, 1 },
    { DISK_75_MB,    5, 18,  823, 1 },
    { DISK_DT135,   10, 17,  823, 0 },
    { DISK_150_MB,  10, 18,  823, 1 },
    { DISK_288_MB,  19, 18,  823, 1 },
    { DISK_DT285,   24, 17,  711, 0 },
    { DISK_DT300,   24, 18,  711, 0 },
    { DISK_DT450,   24, 26,  711, 1 },
    { DISK_DT460,   24, 27,  711, 0 },
    { DISK_DT310,   10, 26, 1217, 0 },
    { DISK_DT470,   19, 31,  760, 0 },
    { DISK_DT290,   10, 35,  823, 0 }
};

DiskType smd_disk_type_for_size(long fileSize)
{
    size_t i;
    DiskType best = DISK_75_MB;
    long bestSize = 0;

    /* Old size windows first, so images that worked before keep their geometry. */
    if (fileSize > 0x1000000 && fileSize <= 0x2000000)
        return DISK_150_MB;
    if (fileSize >= 0x9600000 && fileSize <= 0x9601000)
        return DISK_150_MB;
    if (fileSize >= 0x12000000 && fileSize <= 0x12001000)
        return DISK_288_MB;
    if (fileSize >= 0x33900000)
        return DISK_825_MB;

    /* A file exactly as long as one layout (up to 4096 bytes more) is that layout. */
    for (i = 0; i < sizeof(smd_layouts) / sizeof(smd_layouts[0]); i++)
    {
        long size = (long)smd_layouts[i].cylinders * smd_layouts[i].heads *
                    smd_layouts[i].sectors * 1024L;
        if (fileSize >= size && fileSize <= size + 4096)
            return smd_layouts[i].type;
    }

    /* Otherwise the default layout of the largest size class that fits in the file. */
    for (i = 0; i < sizeof(smd_layouts) / sizeof(smd_layouts[0]); i++)
    {
        long size = (long)smd_layouts[i].cylinders * smd_layouts[i].heads *
                    smd_layouts[i].sectors * 1024L;
        if (smd_layouts[i].isDefault && size <= fileSize && size > bestSize)
        {
            best = smd_layouts[i].type;
            bestSize = size;
        }
    }
    return best;
}

static DiskType smd_preset[4];

void smd_disk_preset_format(int unit, DiskType dt)
{
    if (unit >= 0 && unit < 4)
        smd_preset[unit] = dt;
}

DiskType smd_disk_preset_get(int unit)
{
    return (unit >= 0 && unit < 4) ? smd_preset[unit] : DISK_TYPE_UNKNOWN;
}

/* Layout table name (DTxxx) or SINTRAN disk type name -> type. The pairs come from the
 * comments in the SINTRAN layout table (DT table). DT070 has the geometry of DT075,
 * and DT140 and DT160 the geometry of the 150 MB entry. */
static const struct { const char *name; DiskType type; } smd_names[] = {
    { "DT037", DISK_38_MB },  { "38MB", DISK_38_MB },
    { "DT070", DISK_75_MB },  { "70MB", DISK_75_MB },
    { "DT075", DISK_75_MB },  { "75MB", DISK_75_MB },
    { "DT135", DISK_DT135 },  { "140MB-S", DISK_DT135 },
    { "DT140", DISK_150_MB }, { "140MB-F", DISK_150_MB },
    { "DT160", DISK_150_MB }, { "2-75MB", DISK_150_MB },
    { "DT288", DISK_288_MB }, { "288MB-R", DISK_288_MB },
    { "DT285", DISK_DT285 },  { "288MB-S", DISK_DT285 },
    { "DT300", DISK_DT300 },  { "288MB-F", DISK_DT300 },
    { "DT310", DISK_DT310 },  { "288MB-E", DISK_DT310 },
    { "DT290", DISK_DT290 },  { "288MB-N", DISK_DT290 },
    { "DT450", DISK_DT450 },  { "450MB-S", DISK_DT450 },
    { "DT460", DISK_DT460 },  { "450MB-F", DISK_DT460 },
    { "DT470", DISK_DT470 },  { "450MB-N", DISK_DT470 }
};

DiskType smd_disk_type_from_name(const char *name)
{
    size_t i, k;

    if (!name)
        return DISK_TYPE_UNKNOWN;
    if (strncmp(name, "DISC-", 5) == 0 || strncmp(name, "disc-", 5) == 0)
        name += 5;
    for (i = 0; i < sizeof(smd_names) / sizeof(smd_names[0]); i++)
    {
        const char *want = smd_names[i].name;
        for (k = 0; want[k] && name[k]; k++)
        {
            char c = name[k];
            if (c >= 'a' && c <= 'z')
                c = (char)(c - 'a' + 'A');
            if (c != want[k])
                break;
        }
        if (want[k] == '\0' && name[k] == '\0')
            return smd_names[i].type;
    }
    return DISK_TYPE_UNKNOWN;
}

void smd_disk_set_type(DiskInfo *disk, DiskType dt)
{
    disk->diskType = dt;
    disk->bytesPrSector = 1024; // 1024bytes / 512 Words
    disk->maxWordCount =
        4095; // TODO: Find the correct MAX - is it different pr disk or controller?

    switch (dt)
    {
    case DISK_38_MB:
        disk->headsPrCylinder = 5;
        disk->sectorsPrTrack = 18;
        disk->maxCylinders = 411;
        break;
    case DISK_75_MB:
        disk->headsPrCylinder = 5;
        disk->sectorsPrTrack = 18;
        disk->maxCylinders = 823;
        break;
    case DISK_150_MB:
        disk->headsPrCylinder = 10;
        disk->sectorsPrTrack = 18;
        disk->maxCylinders = 823;
        break;
    case DISK_288_MB:
        disk->headsPrCylinder = 19;
        disk->sectorsPrTrack = 18;
        disk->maxCylinders = 823;
        break;
    case DISK_474_MB:
        disk->headsPrCylinder = 20;
        disk->sectorsPrTrack = 24;
        disk->maxCylinders = 842;
        break;
    case DISK_515_MB:
        disk->headsPrCylinder = 24;
        disk->sectorsPrTrack = 26;
        disk->maxCylinders = 711;
        break;
    case DISK_825_MB:
        disk->headsPrCylinder = 16;
        disk->sectorsPrTrack = 44;
        disk->maxCylinders = 1024;
        break;
    case DISK_DT135:
        disk->headsPrCylinder = 10;
        disk->sectorsPrTrack = 17;
        disk->maxCylinders = 823;
        break;
    case DISK_DT285:
        disk->headsPrCylinder = 24;
        disk->sectorsPrTrack = 17;
        disk->maxCylinders = 711;
        break;
    case DISK_DT300:
        disk->headsPrCylinder = 24;
        disk->sectorsPrTrack = 18;
        disk->maxCylinders = 711;
        break;
    case DISK_DT450:
        disk->headsPrCylinder = 24;
        disk->sectorsPrTrack = 26;
        disk->maxCylinders = 711;
        break;
    case DISK_DT460:
        disk->headsPrCylinder = 24;
        disk->sectorsPrTrack = 27;
        disk->maxCylinders = 711;
        break;
    case DISK_DT310:
        disk->headsPrCylinder = 10;
        disk->sectorsPrTrack = 26;
        disk->maxCylinders = 1217;
        break;
    case DISK_DT470:
        disk->headsPrCylinder = 19;
        disk->sectorsPrTrack = 31;
        disk->maxCylinders = 760;
        break;
    case DISK_DT290:
        disk->headsPrCylinder = 10;
        disk->sectorsPrTrack = 35;
        disk->maxCylinders = 823;
        break;
    default:
        break;
    }
}
