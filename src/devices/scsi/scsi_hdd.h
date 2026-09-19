/*
 * nd100x - ND-100 emulator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * scsi_hdd.h - SCSI hard disk target (CDB decode + block I/O)
 *
 * Ported from RetroCore:
 *   <RetroCore>\Emulated.HW\Common\SCSI\SCSIHDD.cs
 *   <RetroCore>\Emulated.HW\Common\SCSI\SCSIHDDMicropolis.cs
 *
 * The two are folded into one target here: the C# split exists only because
 * SCSIHDDMicropolis subclasses SCSIHDD to override INQUIRY, REQUEST SENSE,
 * SEEK(6), RESERVE(6) and the vendor commands. In C there is a single
 * scsi_command() with the Micropolis behaviour applied, since the ND path never
 * instantiates a bare SCSIHDD.
 *
 * Block I/O goes through the owning Device's machine block callbacks
 * (machine_block_read / machine_block_write), exactly like the SMD controller -
 * this target never opens a file itself. RetroCore's SCSIHDDImage.cs is
 * therefore NOT ported.
 */

#ifndef SCSI_HDD_H
#define SCSI_HDD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "scsi_device.h"
#include "disk_scsi.h"

/* SCSI command opcodes (SCSIEnums.cs SCSICommands) - only those the disk
 * target decodes. */
// clang-format off
#define SC_TEST_UNIT_READY             0x00
#define SC_REQUEST_SENSE               0x03
#define SC_FORMAT_UNIT                 0x04
#define SC_READ_6                      0x08
#define SC_WRITE_6                     0x0A
#define SC_SEEK_6                      0x0B
#define SC_INQUIRY                     0x12
#define SC_MODE_SELECT_6               0x15
#define SC_RESERVE_6                   0x16
#define SC_MODE_SENSE_6                0x1A
#define SC_START_STOP_UNIT             0x1B
#define SC_RECEIVE_DIAGNOSTIC_RESULTS  0x1C
#define SC_SEND_DIAGNOSTIC             0x1D
#define SC_READ_CAPACITY               0x25
#define SC_READ_10                     0x28
#define SC_WRITE_10                    0x2A
#define SC_VERIFY_10                   0x2F
#define SC_READ_BUFFER                 0x3C
// clang-format on

/* Micropolis vendor-specific commands. */
// clang-format off
#define SC_INIT_DRIVE_PARAMS           0x0C
#define SC_FORMAT_ALT_TRACK            0x0E
#define SC_WRITE_SECTOR_BUFFER         0x0F
#define SC_READ_SECTOR_BUFFER          0x10
// clang-format on

#define SCSI_HDD_MAX_SECTOR_BYTES 1024
#define SCSI_HDD_INQUIRY_SIZE     56

// clang-format off
typedef struct {
    SCSITarget target;      /* must be first - SCSITarget.impl points back here */

    /* The owning ND-3201 controller Device, used only to reach
     * blockCallbacks.readFunc / writeFunc / diskInfoFunc. */
    struct Device *owner;
    int unit;               /* SCSI id, also the drive unit for the callbacks */

    SCSIDiskInfo hdinfo;

    int  lba;               /* LBA of the current command */
    int  cur_lba;           /* LBA currently held in sectorData, -1 = none */
    int  blocks;            /* transfer length of the current command */

    uint8_t sectorData[SCSI_HDD_MAX_SECTOR_BYTES];
    bool    sectorDataValid;

    uint8_t inquiry_data[SCSI_HDD_INQUIRY_SIZE];
} SCSIHDDDevice;
// clang-format on

/* Attach a hard disk target to the bus at scsi_id, backed by owner's block
 * callbacks on the given unit. */
void SCSIHDD_Init(SCSIHDDDevice *hdd, SCSIBus *bus, uint8_t scsi_id, struct Device *owner, int unit,
                  SCSIDiskType diskType);


#endif // SCSI_HDD_H
