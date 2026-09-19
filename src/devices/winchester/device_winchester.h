/*
 * device_winchester.h - Winchester disc controller, cards 3041/3038: address map and API.
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

#ifndef DEVICE_WINCHESTER_H
#define DEVICE_WINCHESTER_H

#include <stddef.h>

/*
 * 5 1/4 inch (ST506) and 8 inch Winchester Disk Controller.
 *
 * Cards 3041 (5 1/4 inch) and 3038 (8 inch). Source of truth:
 *   ND-11.015.01 Winchester Disk Controller (programming specification,
 *   sections 3.1 - 3.5), cross-checked against the RetroCore C# model
 *   Emulated.HW/ND/CPU/NDBUS/NDBusDiscControllerWinchester.cs.
 *
 * WHY THIS CONTROLLER EXISTS ALONGSIDE THE SMD ONE
 *
 * The three ND disc controller families differ in how the 24-bit memory
 * address and the word count are loaded, and that difference decides which
 * one a given machine actually is:
 *
 *   NORD-10 large disc  memory address ONE access,  word count ONE access
 *   ECC (ND 558/559)    memory address ONE access,  word count ONE access
 *   15 MHz SMD (ND 632) memory address TWO accesses, word count TWO accesses
 *   Winchester (this)   memory address TWO accesses, word count ONE access
 *
 * The ND-120's own MASS STORAGE LOAD microcode (PROM listing, CSA 002221 -
 * 002227) writes the core address twice and the word count ONCE, which is the
 * Winchester pattern.
 *
 * ADDRESSES (ND-11.015.01 sec 3.1)
 *   Disk system 1: IOX 500-507, ident 1.  Disk system 2: add 10 octal, ident 5.
 *   Interrupt level 11. Each disk system may consist of TWO disk units.
 *   NOTE: this collides with the CDC cartridge disc, which also answers
 *   500-507 - a machine has one or the other card, never both.
 *
 * REGISTERS (offset from base)
 *   +0 R  Read memory address    (LO 16 first, then HI 8)
 *   +1 W  Load memory address    (HI 8 first, then LO 16)
 *   +2 R  Read sector counter
 *   +3 W  Load block address     (cylinder b15-5, sector b4-0)
 *   +4 R  Read status register
 *   +5 W  Load control word
 *   +6 R  Read block address     (test mode only)
 *   +7 W  Load word count / step count   (SINGLE access)
 *
 * The upper/lower selection flip-flop is set by any of: master clear,
 * programmed device clear (control word bit 4), read status register, or
 * activation (control word bit 2) - all four, per sec 3.2.
 */

#include <stdbool.h>
#include <stdint.h>
#include "../devices_types.h"

#include "disk_winchester.h"

/* Which card. Status bit 13 distinguishes them on a status read. */
// clang-format off
typedef enum {
    WD_CONTR_3041,   /* 5 1/4 inch ST506; status b13 always 1 */
    WD_CONTR_3038    /* 8 inch;         status b13 = read/write gate active */
} WDControllerType;
// clang-format on

/* ND-11.015.01 sec 3.1: "Each disk system may consist of 2 disk units", and
 * the control word carries the unit in a SINGLE bit (bit 9), so two is the
 * hardware maximum rather than a configuration choice. */
#define WD_MAX_UNITS 2

// clang-format off
#define WD_IDENT_SYSTEM1 001   /* octal, disk system 1 */
#define WD_IDENT_SYSTEM2 005   /* octal, disk system 2 */
#define WD_INT_LEVEL     11
// clang-format on

/* Device operation codes, control word bits 11-13 (sec 3.4). */
// clang-format off
typedef enum {
    WD_OP_READ_TRANSFER   = 0,  /* M0 */
    WD_OP_WRITE_TRANSFER  = 1,  /* M1 */
    WD_OP_READ_PARITY     = 2,  /* M2 */
    WD_OP_COMPARE         = 3,  /* M3 */
    WD_OP_SEEK            = 4,  /* M4 - step count in the word count register */
    WD_OP_WRITE_FORMAT    = 5,  /* M5 */
    WD_OP_LOAD_CTRL_BITS  = 6,  /* M6 - 3038 only, NOT activated */
    WD_OP_RETURN_TO_ZERO  = 7   /* M7 */
} WDDeviceOperation;
// clang-format on

/* Seek direction, control word bit 14 (sec 3.4.5): bit 14 zero means the
 * heads move TOWARDS cylinder 0. */
typedef enum
{
    WD_SEEK_IN = 0, /* towards cylinder 0 */
    WD_SEEK_OUT = 1
} WDSeekDirection;

/* Status register, sec 3.5. */
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t interruptEnabled : 1;      /* b0  controller not active, int enabled */
        uint16_t errorInterruptEnabled : 1; /* b1  error interrupt enabled */
        uint16_t active : 1;                /* b2  controller active */
        uint16_t readyForTransfer : 1;      /* b3  finished with a device operation */
        uint16_t inclusiveOrErrors : 1;     /* b4  inclusive OR of the error bits */
        uint16_t rtzViolation : 1;          /* b5  3041: r/w attempted during RTZ; 3038: unused */
        uint16_t timeOut : 1;               /* b6  timeout */
        uint16_t diskFault : 1;             /* b7  disk fault or missing clocks */
        uint16_t addressMismatch : 1;       /* b8  address mismatch */
        uint16_t crcError : 1;              /* b9  CRC error */
        uint16_t compareError : 1;          /* b10 compare error */
        uint16_t dmaChannelError : 1;       /* b11 FIFO over/under-run or DMA channel error */
        uint16_t notUsed12 : 1;             /* b12 always 0 */
        uint16_t controllerId : 1;          /* b13 3041: always 1; 3038: r/w gate active */
        uint16_t onCylinder : 1;            /* b14 on cylinder */
        uint16_t notUsed15 : 1;             /* b15 always 0, distinguishes from the 10 Mb controller */
    } bits;
} WDStatusRegister;
// clang-format on

/* Control word, sec 3.4. */
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t enableInterruptNotActive : 1; /* b0 */
        uint16_t enableInterruptOnErrors : 1;  /* b1 */
        uint16_t active : 1;                   /* b2  activate device operation */
        uint16_t testMode : 1;                 /* b3 */
        uint16_t deviceClear : 1;              /* b4  clears drive and controller */
        uint16_t head : 4;                     /* b5-8  head number */
        uint16_t unit : 1;                     /* b9    unit number (max 2 units) */
        uint16_t notUsed10 : 1;                /* b10 */
        /* Must be uint16_t, not the enum type: mixed-type bit-fields break on
         * Windows/MinGW where -mms-bitfields splits differently-typed fields
         * into separate storage units and destroys the `raw` overlay. Same
         * note as panel.h / device_smd.h. */
        uint16_t deviceOperation : 3;          /* b11-13 */
        uint16_t direction : 1;                /* b14   seek direction */
        uint16_t badTrack : 1;                 /* b15   bad track */
    } bits;
} WDControlRegister;
// clang-format on

/* Controller register file. */
// clang-format off
typedef struct {
    uint16_t memoryAddress;      /* low 16 bits */
    uint8_t  memoryAddressHiBits;/* high 8 bits -> 24-bit address */
    uint16_t wordCounter;        /* also the STEP COUNT for M4 */
    uint16_t blockAddress;       /* cylinder b15-5, sector b4-0 */
    uint16_t sectorCounter;

    /* The single upper/lower selection flip-flop, one for the write side and
     * one for the read side (sec 3.2). */
    bool memoryAddressWriteFF;
    bool memoryAddressReadFF;

    uint8_t  selectedUnit;       /* control word b9 */
    uint8_t  head;               /* control word b5-8 */
    int32_t  cylinder;           /* current arm position, per unit below */
    uint16_t sector;

    bool testMode;
    bool badTrack;
    WDSeekDirection seekDirection;
    WDDeviceOperation deviceOperation;

    int maxUnits;
    WDDiskInfo *disks;
    WDDiskInfo *selectedDisk;
} WDControllerRegs;
// clang-format on

typedef struct
{
    WDControllerRegs regs;
    WDStatusRegister statusRegister;
    WDControlRegister controlRegister;
    WDControllerType controllerType;
} WinchesterData;

Device *CreateWinchesterDevice(uint8_t thumbwheel);

#endif /* DEVICE_WINCHESTER_H */
