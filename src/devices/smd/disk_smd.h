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
    // Is a disk pack actually mounted on this unit, i.e. was an image attached?
    // DISC-TEMA requires that every unit NOT specified for test is powered off
    // (ND-11.020.01 sec 5, item 16), and checks status bit 13 "by the selection
    // of specified units and by reading from non-specified units" (item 14). A
    // unit with no image must therefore read NOT READY. Resolved lazily on
    // first use (the image-size callback stats a file) and then cached, because
    // DISC-TEMA reads the status register tens of thousands of times per run.
    bool unitAttachChecked;
    bool unitAttached;

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
 * The old size windows come first (so existing images keep their geometry). After
 * that the largest SINTRAN layout whose full size (cylinders * heads * sectors * 1024)
 * fits in the file is used; a file smaller than every layout gets DISK_75_MB. The file
 * size cannot tell formats of equal size apart; that needs an image at least as long as
 * the layout, and a layout the file size selects.
 *
 * @param fileSize Image file size in bytes.
 * @return The disk type to pass to smd_disk_set_type().
 */
DiskType smd_disk_type_for_size(long fileSize);


#endif /* DISK_SMD_H */
