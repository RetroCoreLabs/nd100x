/*
 * device_paper_tape_writer.h - Paper tape punch interface: registers and API.
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

#ifndef DEVICE_PAPER_TAPE_WRITER_H
#define DEVICE_PAPER_TAPE_WRITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../devices_types.h"

/*
 * Paper Tape Punch Interface (ND-06.015.02)
 *
 * TW0: 0410-0413 (ident 02, level 10)
 * TW1: 0414-0417 (ident 022, level 10)
 *
 * SINTRAN logical device: 4
 */

// Paper tape writer registers
// clang-format off
typedef enum {
    PTW_READ_DATA_REGISTER = 0,     // +0: Read data (test mode only)
    PTW_WRITE_DATA_BUFFER = 1,      // +1: Write data (8-bit)
    PTW_READ_STATUS_REGISTER = 2,   // +2: Read status
    PTW_WRITE_CONTROL_WORD = 3      // +3: Write control
} PaperTapeWriterRegisters;
// clang-format on

// Status register bits (IOX +2, Read)
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t interruptEnabled : 1;    // Bit 0: Interrupt enabled
        uint16_t notUsed1 : 1;            // Bit 1: Not used
        uint16_t active : 1;              // Bit 2: Device active
        uint16_t readyForTransfer : 1;    // Bit 3: Device ready
        uint16_t notUsed4 : 1;            // Bit 4
        uint16_t notUsed5 : 1;            // Bit 5
        uint16_t notUsed6 : 1;            // Bit 6
        uint16_t notUsed7 : 1;            // Bit 7
        uint16_t notUsed8 : 1;            // Bit 8
        uint16_t notUsed9 : 1;            // Bit 9
        uint16_t notUsed10 : 1;           // Bit 10
        uint16_t notUsed11 : 1;           // Bit 11
        uint16_t notUsed12 : 1;           // Bit 12
        uint16_t notUsed13 : 1;           // Bit 13
        uint16_t notUsed14 : 1;           // Bit 14
        uint16_t notUsed15 : 1;           // Bit 15
    } bits;
} PaperTapeWriterStatus;
// clang-format on

// Control word bits (IOX +3, Write)
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t interruptEnable : 1;     // Bit 0: Enable interrupt
        uint16_t notUsed1 : 1;            // Bit 1: Not used
        uint16_t activate : 1;            // Bit 2: Activate (punch char in buffer)
        uint16_t testMode : 1;            // Bit 3: Test mode (read back buffer)
        uint16_t deviceClear : 1;         // Bit 4: Device clear
        uint16_t notUsed5 : 11;           // Bits 5-15: Not used
    } bits;
} PaperTapeWriterControl;
// clang-format on

#define PTW_INITIAL_TAPE_CAPACITY 65536

// Paper tape writer device data
typedef struct
{
    uint8_t characterBuffer;
    PaperTapeWriterStatus statusRegister;
    PaperTapeWriterControl controlWord;

    // Output tape buffer (accumulates punched bytes)
    uint8_t *tapeBuffer;
    size_t tapePosition;
    size_t tapeCapacity;
} PaperTapeWriterData;

// Function declarations
/**
 * @brief Create and initialize the paper tape punch (writer) device.
 * @param thumbwheel Card thumbwheel; selects the IOX address block.
 * @return The new Device, or NULL on allocation failure.
 */
Device *ptp_create_paper_tape_writer_device(uint8_t thumbwheel);

/**
 * @brief Get a pointer to the punch's accumulated output buffer.
 * @param self The paper tape writer device.
 * @param length Set to the number of bytes accumulated so far, or 0 if none.
 * @return Pointer to the internal tape buffer, or NULL if none is allocated.
 */
const uint8_t *ptp_get_tape_data(Device *self, size_t *length);

#endif /* DEVICE_PAPER_TAPE_WRITER_H */
