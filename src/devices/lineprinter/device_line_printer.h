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

#ifndef DEVICE_LINEPRINTER_H
#define DEVICE_LINEPRINTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../devices_types.h"

/*
 * Line Printer Interface (ND-06.016.01)
 *
 * TW0: 0430-0433 (ident 03, level 10)
 * TW1: 0434-0437 (ident 023, level 10)
 *
 * SINTRAN logical device: 5
 */

// Line printer registers
// clang-format off
typedef enum {
    LP_READ_DATA_REGISTER = 0,     // +0: Read data (test mode only)
    LP_WRITE_DATA_BUFFER = 1,      // +1: Write data (7-bit ASCII)
    LP_READ_STATUS_REGISTER = 2,   // +2: Read status
    LP_WRITE_CONTROL_WORD = 3      // +3: Write control
} LinePrinterRegisters;
// clang-format on

// Status register bits (IOX +2, Read)
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t interruptEnabled : 1;      // Bit 0: Interrupt enabled on ready
        uint16_t errorInterruptEnabled : 1;  // Bit 1: Interrupt enabled on error
        uint16_t active : 1;                 // Bit 2: Device is activated
        uint16_t readyForTransfer : 1;       // Bit 3: Ready for transfer
        uint16_t errorAny : 1;              // Bit 4: Error (bit 5 or 6 set)
        uint16_t notReady : 1;              // Bit 5: Line printer not ready
        uint16_t outOfPaper : 1;            // Bit 6: Out of paper
        uint16_t compressedPitch : 1;       // Bit 7: Compressed pitch
        uint16_t lp9Format : 1;            // Bit 8: LP9 format info indicator
        uint16_t inhibitIllegal : 1;       // Bit 9: Inhibit, illegal char in buffer
        uint16_t notUsed10 : 1;            // Bit 10: Not used
        uint16_t bandDetect0 : 1;          // Bit 11: Band detect (low)
        uint16_t bandDetect1 : 1;          // Bit 12: Band detect (high)
        uint16_t notUsed13 : 1;            // Bit 13: Not used
        uint16_t notUsed14 : 1;            // Bit 14: Not used
        uint16_t notUsed15 : 1;            // Bit 15: Not used
    } bits;
} LinePrinterStatus;
// clang-format on

// Control word bits (IOX +3, Write)
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t interruptEnable : 1;       // Bit 0: Enable interrupt on ready
        uint16_t errorInterruptEnable : 1;  // Bit 1: Enable interrupt on error
        uint16_t activate : 1;              // Bit 2: Activate device (print char)
        uint16_t testMode : 1;             // Bit 3: Test mode
        uint16_t deviceClear : 1;          // Bit 4: Device and interface clear
        uint16_t notUsed5 : 11;            // Bits 5-15: Not used
    } bits;
} LinePrinterControl;
// clang-format on

// Line printer device data
typedef struct
{
    uint8_t characterBuffer;
    LinePrinterStatus statusRegister;
    LinePrinterControl controlWord;
} LinePrinterData;

// Function declarations
Device *CreateLinePrinterDevice(uint8_t thumbwheel);

#endif /* DEVICE_LINEPRINTER_H */
