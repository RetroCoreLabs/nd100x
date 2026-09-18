/*
 * nd100x - ND100 Virtual Machine
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


#ifndef DEVICE_FLOPPY_PIO_H
#define DEVICE_FLOPPY_PIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "../devices_types.h"


// Floppy PIO registers
typedef enum {
    FLOPPY_READ_DATA_BUFFER = 0,      // IOX +0: Read one 16-bit word from the interface buffer
    FLOPPY_WRITE_DATA_BUFFER = 1,     // IOX +1: Write one 16-bit word to the interface buffer
    FLOPPY_READ_STATUS_REGISTER1 = 2,  // IOX +2: Read Status Register No. 1 (RSR1)
    FLOPPY_WRITE_CONTROL_WORD = 3,     // IOX +3: Write Control Word (WCWD)
    FLOPPY_READ_STATUS_REGISTER2 = 4,  // IOX +4: Read Status Register No. 2 (RSR2)
    FLOPPY_WRITE_DRIVE_ADDRESS = 5,    // IOX +5: Write Drive Address / Write Difference
    FLOPPY_READ_TEST_DATA = 6,         // IOX +6: Read test
    FLOPPY_WRITE_SECTOR = 7            // IOX +7: Write Sector / Write Test Byte
} FloppyPIORegisters;

// Drive commands
typedef enum {
    FLOPPY_CMD_FORMAT_TRACK = 0,
    FLOPPY_CMD_WRITE_DATA,
    FLOPPY_CMD_WRITE_DELETED_DATA,
    FLOPPY_CMD_READ_ID,
    FLOPPY_CMD_READ_DATA,
    FLOPPY_CMD_SEEK,
    FLOPPY_CMD_RECALIBRATE,
    FLOPPY_CMD_CONTROL_RESET,
    FLOPPY_CMD_NONE
} FloppyPIOCommand;

// Status Register 1 bits
typedef union {
    uint16_t raw;
    struct {
        uint16_t notUsed : 1;                // Bit 0
        uint16_t interruptEnabled : 1;       // Bit 1
        uint16_t deviceBusy : 1;             // Bit 2
        uint16_t deviceReadyForTransfer : 1; // Bit 3
        uint16_t inclusiveOrBitsInReg2 : 1;  // Bit 4 (Error indication; status reg 2 must be read)
        uint16_t deletedRecord : 1;          // Bit 5 (Sector had delete data address mark)
        uint16_t readWriteComplete : 1;      // Bit 6 (Read or write operation completed)
        uint16_t seekComplete : 1;           // Bit 7 (Seek or recalibration completed)
        uint16_t timeOut : 1;                // Bit 8 (Approximately 1.5 seconds timeout)
        uint16_t notUsed9 : 1;               // Bit 9
        uint16_t notUsed10 : 1;              // Bit 10
        uint16_t notUsed11 : 1;              // Bit 11
        uint16_t notUsed12 : 1;              // Bit 12
        uint16_t notUsed13 : 1;              // Bit 13
        uint16_t notUsed14 : 1;              // Bit 14
        uint16_t notUsed15 : 1;              // Bit 15
    } bits;
} FloppyPIOStatus1;

// Control Word bits
typedef union {
    uint16_t raw;
    struct {
        uint16_t notUsed0 : 1;                    // Bit 0
        uint16_t enableInterrupt : 1;             // Bit 1
        uint16_t autoload : 1;                    // Bit 2
        uint16_t testMode : 1;                    // Bit 3 (See IOX RTST and IOX WSCT)
        uint16_t deviceClear : 1;                 // Bit 4 (Selected drive is deselected)
        uint16_t clearInterfaceBufferAddress : 1; // Bit 5
        uint16_t enableTimeout : 1;               // Bit 6
        uint16_t notUsed7 : 1;                    // Bit 7
        uint16_t formatTrack : 1;                 // Bit 8
        uint16_t writeData : 1;                   // Bit 9
        uint16_t writeDeletedData : 1;            // Bit 10
        uint16_t readID : 1;                      // Bit 11
        uint16_t readData : 1;                    // Bit 12
        uint16_t seek : 1;                        // Bit 13
        uint16_t recalibrate : 1;                 // Bit 14
        uint16_t controlReset : 1;                // Bit 15
    } bits;
} FloppyPIOControl;

// Status Register 2 bits
typedef union {
    uint16_t raw;
    struct {
        uint16_t notUsed0 : 1;      // Bit 0
        uint16_t notUsed1 : 1;      // Bit 1
        uint16_t notUsed2 : 1;      // Bit 2
        uint16_t notUsed3 : 1;      // Bit 3
        uint16_t notUsed4 : 1;      // Bit 4
        uint16_t notUsed5 : 1;      // Bit 5
        uint16_t notUsed6 : 1;      // Bit 6
        uint16_t notUsed7 : 1;      // Bit 7
        uint16_t driveNotReady : 1; // Bit 8
        uint16_t writeProtect : 1;  // Bit 9
        uint16_t notUsed10 : 1;     // Bit 10
        uint16_t sectorMissing : 1; // Bit 11
        uint16_t crcError : 1;      // Bit 12
        uint16_t notUsed13 : 1;     // Bit 13
        uint16_t dataOverrun : 1;   // Bit 14
        uint16_t notUsed15 : 1;     // Bit 15
    } bits;
} FloppyPIOStatus2;

// Drive Address bits
typedef union {
    uint16_t raw;
    struct {
        uint16_t modeBit : 1;        // Bit 0 (1 = Write Drive Address, 0 = Write Difference)
        uint16_t notUsed1 : 1;       // Bit 1
        uint16_t notUsed2 : 1;       // Bit 2
        uint16_t notUsed3 : 1;       // Bit 3
        uint16_t notUsed4 : 1;       // Bit 4
        uint16_t notUsed5 : 1;       // Bit 5
        uint16_t notUsed6 : 1;       // Bit 6
        uint16_t notUsed7 : 1;       // Bit 7
        uint16_t driveAddress : 3;   // Bits 8-10
        uint16_t deselectDrives : 1; // Bit 11
        uint16_t notUsed12 : 1;      // Bit 12
        uint16_t notUsed13 : 1;      // Bit 13
        uint16_t formatSelect : 2;   // Bits 14-15
    } bits;
} FloppyPIODriveAddress;

// Sector Control bits
typedef union {
    uint16_t raw;
    struct {
        uint16_t notUsed0 : 1;      // Bit 0
        uint16_t notUsed1 : 1;      // Bit 1
        uint16_t notUsed2 : 1;      // Bit 2
        uint16_t notUsed3 : 1;      // Bit 3
        uint16_t notUsed4 : 1;      // Bit 4
        uint16_t notUsed5 : 1;      // Bit 5
        uint16_t notUsed6 : 1;      // Bit 6
        uint16_t notUsed7 : 1;      // Bit 7
        uint16_t sectorNumber : 7;  // Bits 8-14
        uint16_t autoIncrement : 1; // Bit 15
    } bits;
} FloppyPIOSector;

// Floppy PIO device data
typedef struct {
    FILE *floppyFile;
    const char *floppyName;
    uint16_t dataBuffer[1024];
    int16_t loadDriveAddress;
    uint16_t sector;
    uint16_t track;
    uint16_t bufferPointer;
    int selectedDrive;
    int bytes_pr_sector;
    int sectors_pr_track;
    uint8_t testByte;
    bool sectorAutoIncrement;
    int testmodeByte;
    bool deletedRecord;
    bool deletedSector[100][100];  // Array to track deleted sectors
    FloppyPIOStatus1 status1;
    FloppyPIOControl control;
    FloppyPIOStatus2 status2;
    FloppyPIODriveAddress driveAddress;
    FloppyPIOSector sectorControl;
    FloppyPIOCommand command;
} FloppyPIOData;

// Function declarations
Device* CreateFloppyPIODevice(uint8_t thumbwheel);
void FloppyPIO_ExecuteGo(Device *self, FloppyPIOCommand command);

#endif /* DEVICE_FLOPPY_PIO_H */