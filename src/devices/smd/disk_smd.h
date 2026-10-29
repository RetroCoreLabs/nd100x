/*
 * disk_smd.h - SMD disk types and geometry structure.
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


#ifndef DISK_SMD_H
#define DISK_SMD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Disk types
typedef enum
{
    DISK_TYPE_UNKNOWN = 0,
    // ***************
    // 10MHZ drives
    // See also page 520 in "SINTRAN III J VSX programlistning Vol2-Gandalf-OCR.PDF"
    // ***************

    // Total number of pages  044066 (oct) 18486 (dec) = 38MB (Block size = 2048 Bytes)
    // Total number of pages  110121 (oct) 36945 (dec) = 75MB
    //  max 5 surfaces
    //  max 18 sectors
    //  max 411 cylinders (0-410)
    //  110176 number of sectors
    //  Table Name: DT037
    DISK_38_MB,

    //  max 5 surfaces
    //  max 18 sectors
    //  max 823 cylinders (0-822)
    //  220526 number of sectors
    //  Table Name: DT075
    DISK_75_MB,

    //  max 10 surfaces
    //  max 18 sectors
    //  max 823 cylinders
    //  441254 number of sectors
    //  Table Name:  DT140 or DT160 ??
    DISK_150_MB,

    //  max 19 surfaces
    //  max 18 sectors
    //  max 823 cylinders
    //  1045572 number of sectors
    //  Table Name: DT288
    DISK_288_MB,

    // ***************
    // 15 MHZ drives
    // ***************

    // Fujitsu 474Mb M2351A (EAGLE)
    //  max 20 surfaces
    //  max 24 sectors (+1 spare)
    //  max 842 cylinders
    DISK_474_MB,

    //  CDC 515MB FSD
    //  max 24 surfaces
    //  max 26 sectors (+1 spare)
    //  max 711 cylinders
    DISK_515_MB,

    //  CDC 825MB XMD
    //  max 16 surfaces
    //  max 44 sectors (+1 spare)
    //  max 1024 cylinders
    DISK_825_MB,

    // ***************
    // Formats from the SINTRAN disk layout table (DTxxx). Heads = SECSY/SECTR,
    // cylinders = MAXCY+1, sectors are 1024 bytes. Several formats of one drive
    // differ only in sectors per track (17/18, 26/27) or head count.
    // ***************
    DISK_DT135,   // DISC-140MB-S  10 heads 17 sectors 823 cyl
    DISK_DT285,   // DISC-288MB-S  24 heads 17 sectors 711 cyl
    DISK_DT300,   // DISC-288MB-F  24 heads 18 sectors 711 cyl
    DISK_DT450,   // DISC-450MB-S  24 heads 26 sectors 711 cyl
    DISK_DT460,   // DISC-450MB-F  24 heads 27 sectors 711 cyl
    DISK_DT310,   // DISC-288MB-E  10 heads 26 sectors 1217 cyl
    DISK_DT470,   // DISC-450MB-N  19 heads 31 sectors 760 cyl
    DISK_DT290    // DISC-288MB-N  10 heads 35 sectors 823 cyl
} DiskType;

// Disk structure
// clang-format off
typedef struct {
    // Whether a disk pack is mounted on this unit is NOT kept here: the
    // controller asks the machine's disk-info callback each time it needs it
    // (device_smd.c unit_attached), so a pack mounted or removed while the
    // machine runs is seen at once. DISC-TEMA requires that every unit NOT
    // specified for test is powered off (ND-11.020.01 sec 5, item 16), and
    // checks status bit 13 "by the selection of specified units and by reading
    // from non-specified units" (item 14): a unit with no image reads NOT READY.
    bool diskUnitNotReady;
    bool onCylinder;
    bool diskIsWriteProtected;
    int bytesPrSector;
    int headsPrCylinder;
    int sectorsPrTrack;
    int maxCylinders;
    int maxWordCount; // TODO: Find the correct MAX - is it different pr disk or controller?
    uint8_t unit; // disk unit number (0-3)
    DiskType diskType;

    long diskFileSize;      // Size of SMD disk file
    bool readOnly;          // Read-only flag

} DiskInfo;
// clang-format on


/**
 * @brief Fill in an SMD DiskInfo's geometry fields for a given disk type.
 * @param disk The DiskInfo to fill in.
 * @param dt Disk type selecting heads/cylinder, sectors/track and cylinder count.
 */
void smd_disk_set_type(DiskInfo *disk, DiskType dt);

/**
 * @brief Choose the disk type for an image file from its size in bytes.
 *
 * 1. The old size windows come first (so existing images keep their geometry).
 * 2. A file exactly as long as one SINTRAN layout (cylinders * heads * sectors * 1024 bytes,
 *    up to 4096 bytes more) is that layout.
 * 3. Otherwise the default layout of the largest size class that fits: 38 MB (DT037), 75 MB
 *    (DT075), 150 MB (DT140), 288 MB (DT288, the R format) or 450 MB (DT450, the S format);
 *    a file smaller than every class gets DISK_75_MB.
 *
 * Several formats share a size class and the file length cannot tell them apart (288 MB: R, S,
 * F, E, N). A formatN key in the config file forces the layout of a unit.
 * @param fileSize Image file size in bytes.
 * @return The disk type to pass to smd_disk_set_type().
 */
DiskType smd_disk_type_for_size(long fileSize);

/**
 * @brief Find a disk type by SINTRAN layout name.
 *
 * Accepts the layout table names DT037, DT070, DT075, DT135, DT140, DT160, DT285, DT288,
 * DT290, DT300, DT310, DT450, DT460, DT470, and the disk type names that SINTRAN gives them
 * (with or without the DISC- prefix): 38MB, 70MB, 75MB, 140MB-S, 140MB-F, 288MB-R, 288MB-S,
 * 288MB-F, 288MB-E, 288MB-N, 450MB-S, 450MB-F, 450MB-N. Upper or lower case.
 *
 * @return The disk type, or DISK_TYPE_UNKNOWN if the name is not one of these.
 */
DiskType smd_disk_type_from_name(const char *name);

/**
 * @brief Force the disk type of one unit (from the config file), before the image is used.
 *        DISK_TYPE_UNKNOWN goes back to choosing from the image size.
 */
void smd_disk_preset_format(int unit, DiskType dt);

/** @brief The forced disk type of a unit, or DISK_TYPE_UNKNOWN if none was set. */
DiskType smd_disk_preset_get(int unit);


#endif /* DISK_SMD_H */
