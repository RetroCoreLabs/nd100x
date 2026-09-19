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

#ifndef DISK_WINCHESTER_H
#define DISK_WINCHESTER_H

#include <stddef.h>

#include <stdbool.h>
#include <stdint.h>

/*
 * Winchester drive geometries.
 *
 * The capacities and geometries are the example drives tabulated in
 * ND-11.015.01 sec 2.1 ("Some examples for the 8 inch" / "Examples of
 * 5 1/4 inch", printed pages 3-4). Every one of them uses 1024-byte sectors,
 * i.e. 512 words - sec 2.1: "Each sector contains 512 words (1 Kbytes) of
 * data."
 *
 * The manual gives sectors/track, tracks/cylinder, total cylinders and the
 * bad-track count for each drive; "tracks per cylinder" is the head count.
 */
typedef enum
{
    WD_DISK_TYPE_UNKNOWN = 0,

    /* 5 1/4 inch (ST506), ND-11.015.01 printed p.4 */

    /* CDC-9415-5 Wren, 31.19 MB usable */
    WD_DISK_CDC_9415_5_WREN,

    /* Micropolis 1304, 44.78 MB usable (formatted as 23 MB or 45 MB) */
    WD_DISK_MICROPOLIS_1304,

    /* Rodime RO208, 41.2 MB usable. The manual leaves its bad-track count
     * blank, so it is modelled as 0. */
    WD_DISK_RODIME_RO208,

    /* CMI CM6640, 35.22 MB usable */
    WD_DISK_CMI_CM6640,

    /* Micropolis 1325 - formatted as 28 MB, 45 MB or 74 MB. This is the
     * "DISC-74-1" drive: 8 heads x 9 sectors x 1024 cylinders x 1024 bytes =
     * 75,497,472 bytes. It is not in the manual's example table; the geometry
     * comes from the RetroCore model, which SINTRAN boots from today. */
    WD_DISK_MICROPOLIS_1325
} WDDiskType;

/* One drive. Mirrors DiskInfo in disk_smd.h so the two block controllers keep
 * the same shape. */
typedef struct
{
    /* Is a disk pack mounted on this unit? A unit with no attached image is a
     * powered-off drive: it must report not ready. Resolved lazily on first
     * use (the image-size callback stats a file) and then cached. */
    bool unitAttachChecked;
    bool unitAttached;

    bool diskUnitNotReady;
    bool onCylinder;
    bool diskIsWriteProtected;

    int bytesPrSector;
    int headsPrCylinder;
    int sectorsPrTrack;
    int maxCylinders;
    int badTracks;

    /* Current arm position. The Winchester's M4 is a RELATIVE step seek (step
     * count in the word-count register, direction in control-word bit 14), so
     * the controller has to remember where each arm is. */
    int32_t cylinder;

    uint8_t unit;
    WDDiskType diskType;
    size_t diskFileSize;
} WDDiskInfo;

void DiskWinchester_SetDiskType(WDDiskInfo *disk, WDDiskType dt);
long DiskWinchester_ChsToLba(const WDDiskInfo *disk, int cylinder, int head, int sector);

#endif /* DISK_WINCHESTER_H */
