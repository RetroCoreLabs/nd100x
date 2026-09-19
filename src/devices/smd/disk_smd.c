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

void DiskSMD_SetDiskType(DiskInfo *disk, DiskType dt)
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
    default:
        break;
    }
}
