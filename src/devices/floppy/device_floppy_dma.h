/*
 * device_floppy_dma.h - DMA floppy disc controller: types and create function.
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


#ifndef DEVICE_FLOPPY_DMA_H
#define DEVICE_FLOPPY_DMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../devices_types.h"

// Floppy DMA device registers
typedef enum
{
    FLOPPY_DMA_READ_DATA = 0,       // Read data register    - 1560
    FLOPPY_DMA_NOT_USED1 = 1,       // Not used               - 1561
    FLOPPY_DMA_READ_STATUS1 = 2,    // Read status register 1 - 1562
    FLOPPY_DMA_LOAD_CONTROL = 3,    // Load control word      - 1563
    FLOPPY_DMA_READ_STATUS2 = 4,    // Read status register 2 - 1564
    FLOPPY_DMA_LOAD_POINTER_HI = 5, // Load pointer high (bits 16-23) - 1565
    FLOPPY_DMA_NOT_USED6 = 6,       // Not used               - 1566
    FLOPPY_DMA_LOAD_POINTER_LO = 7  // Load pointer low / load data - 1567
} FloppyDMARegisters;

// Floppy function codes
typedef enum
{
    /// <summary>
    /// Read Data
    /// Data is read from the floppy disk to the ND-100 memory. The start address is given as the logical sector address,
    /// and a choice can be made between the wordecount and the number of sectors to indicate the length of the transfer.
    ///
    /// NOTE: The transfer will always start at the beginning of a sector, but the number of words to be read may be preset to any number of words.
    /// </summary>
    FLOPPY_FUNC_READ_DATA = 0x00,

    /// <summary>
    /// Write Data
    /// Same procedure as READ DATA, except that transfer is now from ND-100 to the diskette.
    /// </summary>
    FLOPPY_FUNC_WRITE_DATA = 0x01,

    /// <summary>
    /// FindEOF - Read Without datatransfer
    /// Same procedure as READ DATA, except that data is only read to the 1local buffer. There is no transfer to ND-100, except for the status.
    /// Bit 5 in status word 1 indicates if it is an EOF (deleted record).
    /// </summary>
    FLOPPY_FUNC_FIND_EOF = 0x02,

    /// <summary>
    /// Write EOF (Write Deleted Record)
    /// The sector given in the command block is read to the local memory and written back as a deleted record.
    /// </summary>
    FLOPPY_FUNC_WRITE_EOF = 0x05,

    /// <summary>
    /// Format Floppy
    /// The floppy disk placed in the specified drive is formatted to the format given in the command word.
    /// </summary>
    FLOPPY_FUNC_FORMAT_FLOPPY = 0x21,

    /// <summary>
    /// Read format
    /// The format is read from the floppy disk and returned to status word two. The disk address and the format of the command field indicates where the format should be read.
    /// </summary>
    FLOPPY_FUNC_READ_FORMAT = 0x22,

    /// <summary>
    /// Read deleted record
    /// Reads data from a record marked as a deleted record, and transfers them to ND-100.
    /// </summary>
    FLOPPY_FUNC_READ_DELETED = 0x23,

    /// <summary>
    /// Write deleted record
    /// Writes a record from ND-100 and marks it as a deleted record.
    /// </summary>
    FLOPPY_FUNC_WRITE_DELETED = 0x24,

    /// <summary>
    /// Copy floppy
    /// Copies from one drive to another. The entire floppy is copied.
    /// </summary>
    FLOPPY_FUNC_COPY_FLOPPY = 0x2C,

    /// <summary>
    /// Format Track
    /// One track on one side is formatted. This command can be used to make IBM compatible diskettes.
    /// NOTE: The track address must be given as logical address to the first sector of the track. --
    /// </summary>
    FLOPPY_FUNC_FORMAT_TRACK = 0x2D,

    /// <summary>
    /// Check Floppy
    /// Data is read to the controllers 1local memory to test for CRC-errors.
    /// The test halts with the first discovered error. The address of the erroneous sector is held in LAST MEMADR. in the status field.
    /// </summary>
    FLOPPY_FUNC_CHECK_FLOPPY = 0x2E,

    /// <summary>
    /// Identify Floppy
    /// </summary>
    FLOPPY_FUNC_IDENTIFY = 0x38
} FloppyFunction;

// Error codes
typedef enum
{
    FLOPPY_ERR_OK = 0,
    FLOPPY_ERR_CRC = 5,
    FLOPPY_ERR_SECTOR_NOT_FOUND = 6,
    FLOPPY_ERR_TRACK_NOT_FOUND = 7,
    FORMAT_NOT_FOUND = 8, // oct 10
    /*

        DISKETTE DEFECT (IMPOSSIBLE TO FORMAT) = oct 11
        FORMAT MISMATCH = oct 12
        ILLEGAL FORMAT SPECIFIED oct 13
        SINGLE SIDED DISKETTE INSERTED oct 14
        DOUBLE SIDED DISKETTE INSERTED oct 15
    */
    WRITE_PROTECTED = 14, // WRITE PROTECTED DISKETTE/CARTRIGE oct 16
    /*
        DELETED RECORD = oct 17
        */
    DRIVE_NOT_READY = 16, // oct 20
    /*
        CONTROLLER Busv ON START = oct 21
        LOST DATA(OVER 0R UNDERRUN) = oct 22
        TRACK ZERO NOT DETECTED = oct 23
        vco FREQUENCV OUT OF RANGE = oct 24
        MICROPROGRAM OUT OF RANGE = oct 25
        TIMEOUT = oct 26
        UNDEFINED ERROR = oct 27
    */
    FLOPPY_ERR_TRACK_OUT_OF_RANGE = 0x18, // oct 30
    /*
        COMPARE ERROR(DURING COMPARE OF DATA) = oct 32
        INTERNAL DMA ERRORS = oct 33
        ND-100 BUS ERROR COMMAND FETCH = oct 41
        ND-100 BUS ERROR STATUS TRANSFER = oct 42
        ND-IDO BUS ERROR DATA TRANSFER = oct 43
        ILLEGAL COMMAND = oct 43
        WORDCOUNT NOT ZERO = oct 44
        ILLEGAL COMPETION(CONT.TRANSF) = oct 45
        ADR-REG ERROR = 46
    */
    FLOPPY_ERR_NO_BOOTSTRAP = 0x28,    // oct 50
    FLOPPY_ERR_WRONG_BOOTSTRAP = 0x29, // oct 51
                                       /*
        STREAMER HANDSHAKE ERROR = 60
        STREAMER STATUS TRANSFER ERROR = 61
        BAD CARTRIGE = 62
        NO CARTRIGE INSTALLED = 63
        END OF TAPE, CARTRIGE FULL = 64
        STREAMER DRIVE ERROR = 65
        UNIDENTIFIED EXCEPTION = 66
        ILLEGAL COMMAND TO STREAMER = 67
        PROM CHECKSUM ERROR = oct 70
    */
    RAM_ERROR = 57,                    // oct 71
    /*
        CTC ERROR = oct 72
        DMACTRL ERROR = oct 73 (selftest error)
        VCO ERROR = oct 74
        FLOPPv CONTROLLER ERROR = oct 75
        STREAMER DATA REGISTER ERROR = 76
        ND-100 REGISTER ERRROR = 77
    */
} FloppyError;

/*

6.3.COMMAND ADDRESS BLOCK & STATUS BLOCK
+---------------+---------------+       +--------+--------------------------------------+---------------------------------------------------+
|15            8|7            0 |       | Offset | SINTRAN J - SOURCE Page 411          |  Comment                                          |
+---------------+---------------+       +--------+--------------------------------------+---------------------------------------------------+
|COMMAND        |  WORD         |       |  0     | CCBWO, COMMAND WORD                  |                                                   |
+---------------+---------------+       +--------+--------------------------------------+---------------------------------------------------+
|DISK           | ADDRESS       |       |  1     | BFDEV, DEVICE ADDRESS                |                                                   |
+---------------+---------------+       +--------+--------------------------------------+---------------------------------------------------+
|NOT USED       | MEMADDR HI  ) |       |  2     | FMEMH, MEMORY ADDRESS                | BIT 23-16 is placed in FMEMH                      |
+---------------+---------------+       +--------+--------------------------------------+---------------------------------------------------+
|  MEMORY ADDRESS               |       |  3     | FMEML, MEMORY ADDRESS                |                                                   |
+---------------+---------------+       +--------+--------------------------------------+---------------------------------------------------+
|WC/SC|Not used | Word Cnt HI   |       |  4     | OPWCH, OPTIONS AND WORD COUNT (HI)   | Bit 15 = 1 => WordCount. Bit 15=0 => SectorCount  |
+---------------+---------------+       +--------+--------------------------------------+---------------------------------------------------+
|   WORD COUNT / SECTOR COUNT   |       |  5     | WCOUN, WORD/RECORD COUNT (LO)        |                                                   |
+---------------+---------------+       +--------+--------------------------------------+---------------------------------------------------+
|       STATUS 1                |       |  6     | FSTA1, STATUS 1                      |                                                   |
+---------------+---------------+       +--------+--------------------------------------+---------------------------------------------------+
|       STATUS 2                |       |  7     | FSTA2, STATUS 2                      |                                                   |
+---------------+---------------+       +--------+--------------------------------------+---------------------------------------------------+
|               | LAST ADDR(HI) |       |  8     | LASMH, LAST MEMORY ADDRESS (HI)      |                                                   |
+---------------+---------------+       +--------+--------------------------------------+---------------------------------------------------+
|   LAST MEM ADDRESS            |       |  9     | LASML, LAST MEMORY ADDRESS (LO)      |                                                   |
+---------------+---------------+       +--------+--------------------------------------+---------------------------------------------------+
|               | REM WORD (HI) |       |  10    | MREMW, MOST REMAINIG WORDS           |                                                   |
+---------------+---------------+       +--------+--------------------------------------+---------------------------------------------------+
|   REMAINING WORDS             |       |  11    | LREMW, LEAST REMAINING WORDS         |                                                   |
+---------------+---------------+       +--------+--------------------------------------+---------------------------------------------------+



POINTS TO NOTE:

Offset 6-11 Is the Status-part of the Command Block and is written by means of DMA from the Controller at the end of a Command execution.

IP WC/SC = 1 = WORDCOUNT
IF WC/SC = 0 = SECTORCOUNT

DISK ADDRESS: INDICATES THE START ADDRESS ON THE FLOPPY DISK.
THIS IS GIVEN AS A LOGICAL SECTOR ADDRESS,
STARTING WITH TRACK 00, SIDE 0, SECTOR 1 WHICH
IS ADDRESS 0 AND INCREASING TO THE MAXIMUM
NUMBER OF SECTORS.

MEM. ADDRESS: INDICATES WHERE TO START IN THE ND-100 MEMORY.

WORD/SECT COUNT: WC/SC = 1 INDICATING WORD COUNT (224 BIT)
WC/SC = 0 INDICATING SECTOR COUNT (NUMBER OF SECTORS TO BE TRANSFERRED).

IF THE WORD COUNT IS LESS THAN THE NUMBER OF WORDS IN A SECTOR,
THE TRANSFER WILL START AT THE BEGINNING OF THE SECTOR.


ND-DUAL-DENSITY-FORMAT = 8 (SECT) X 77 (TRACKS) X 2 (SIDES)


STATUS WORD 1 - COMMAND ADDRESS BLOCK + 6
    // BIT 0-6 EQUAL to ReadStatusRegister1
    Bit 1 - INT. ENABLED
    Bit 2 - CONTROLLER BUSY
    Bit 3 - CONTROLLER READY
    Bit 4 - 0R OF ERRORS
    Bit 5 - DELETED RECORD / EOF
    Bit 6 - INTERNAL RETRIES (NO ERROR)
    //

    Bit 7 - SERIOUS ERROR, NO MEMORV CONTACT
    Bit 8 - NOT USE

    BIT 8-15 - ERROR CODE FROM CONTROLLER
    BIT 15 - ALWAYS 1


STATUS WORD 2 - COMMAND ADDRESS BLOCK + 7
    Bit 0-3 - "Read format" returns format
    Bit 3-4 - Not used
    Bit 6   - TRUE IBM FORMAT (128 B/S on track 0 side 0)
    Bit 8-9 - LAST ACCESSED UNIT (=Selected unit)
    Bit 10-15 - NOT USED
 */
/*
43.0 BFDIS - Floppy disk driver for single/double density & side

Modified 24/2-82 by TP

Exit information:

    If NORMAL operation
        T = Hardware Status (IOX Read ReadStatusRegister1)
        X = STATUS 1 (From FSTAT + 0)
        D = STATUS 2 (From FSTAT + 1)
    If TEST operations
        T = Hardware Status  (IOX Read ReadStatusRegister1)
        X = Hardware Status
        D = 177777


    STATUS 2 (From FSTAT +1)
    Bit 0-3 - If READ FORMAT command or FORMAT ERROR : Format read from diskette, otherwhise =0
*/

// clang-format off
typedef union {
    struct {
        uint16_t commandWord;        // Offset 0: Command word
        uint16_t diskAddress;        // Offset 1: Device/disk address
        uint16_t memoryAddressHi;    // Offset 2: Memory address high bits (23-16)
        uint16_t memoryAddressLo;    // Offset 3: Memory address low bits
        uint16_t optionsWordCountHi; // Offset 4: Options and word count high (bit 15=WC/SC)
        uint16_t wordSectorCount;    // Offset 5: Word/sector count low
        uint16_t status1;            // Offset 6: Status word 1
        uint16_t status2;            // Offset 7: Status word 2
        uint16_t lastMemAddrHi;      // Offset 8: Last memory address high
        uint16_t lastMemAddrLo;      // Offset 9: Last memory address low
        uint16_t remainWordsHi;      // Offset 10: Remaining words high
        uint16_t remainWordsLo;      // Offset 11: Remaining words low
    } fields;
    uint16_t raw[12];               // Raw access to command block words
} CommandBlock;
// clang-format on


// Status Register 1 bits
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t notUsed0 : 1;        // Bit 0: Not used
        uint16_t interruptEnabled : 1; // Bit 1: Interrupt enabled
        uint16_t deviceActive : 1;     // Bit 2: Device active
        uint16_t readyForTransfer : 1; // Bit 3: Ready for transfer
        uint16_t inclusiveOrBits : 1;  // Bit 4: Inclusive or of bits in reg 2
        uint16_t deletedRecord : 1;    // Bit 5: Deleted record
        uint16_t retryOnController : 1;// Bit 6: Retry on controller
        uint16_t hardError : 1;        // Bit 7: Hard error
        uint16_t notUsed8 : 1;         // Bit 8: Not used (ND-11.021.1 section 3.4)
        uint16_t errorCode : 6;        // Bits 9-14: Error code (section 3.9)
        uint16_t dualDensity : 1;      // Bit 15: Dual density controller (hardware status word only)

    } bits;
} StatusRegister1;
// clang-format on

// Control Word bits
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t notUsed0 : 1;         // Bit 0: Not used
        uint16_t enableInterrupt : 1;  // Bit 1: Enable interrupt
        uint16_t activateAutoload : 1; // Bit 2: Activate autoload
        uint16_t testMode : 1;         // Bit 3: Test mode
        uint16_t deviceClear : 1;      // Bit 4: Device clear
        uint16_t enableStreamer : 1;   // Bit 5: Enable streamer (New in ND-11.021.1)
        uint16_t notUsed6 : 2;         // Bits 6-7: Not used
        uint16_t executeCommand : 1;   // Bit 8: Execute command
        uint16_t testData : 5;         // Bits 9-13: Used when test mode is active
        uint16_t notUsed14 : 1;        // Bit 14: Not used
        uint16_t notUsed15 : 1;        // Bit 15: Not used
    } bits;
} ControlWord;
// clang-format on

// Status Register 2 bits
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t bytesPrSector : 2;    // Bit 0-1: Bytes per sector - 2 bits
        uint16_t doubleSided : 1;      // Bit 2: Double sided
        uint16_t doubleDensity : 1;    // Bit 3: Double density
        uint16_t notUsed4 : 12;        // Bits 4-15: Not used
    } bits;
} StatusRegister2;
// clang-format on

// Floppy DMA device data
// TODO: Refactor to support multiple units
typedef struct
{
    StatusRegister1 status1; // Status register 1
    StatusRegister2 status2; // Status register 2
    ControlWord controlWord; // Control word

    uint32_t commandBlockAddress;
    CommandBlock commandBlock; // Command black for DMA  read and write to ND memory

    uint8_t data;   // Data register
    uint8_t sector; // Current sector
    uint8_t track;  // Current track
    uint8_t drive;  // Selected drive

    FloppyError errorCode;  // Current error code
    FloppyFunction command; // Current command
    uint32_t pointerHI;     // High pointer bits
    uint32_t pointerLO;     // Low pointer bits
    uint16_t readFormat;    // Format read from disk
    long diskFileSize;      // Size of floppy disk file
    bool readOnly;          // Read-only flag
} FloppyDMAData;


// Function declarations
/**
 * @brief Create and initialize the DMA floppy controller device.
 * @param thumbwheel Card thumbwheel; selects the IOX address block.
 * @return The new Device, or NULL on allocation failure.
 */
Device *floppy_dma_create_device(uint8_t thumbwheel);

#endif /* DEVICE_FLOPPY_DMA_H */
