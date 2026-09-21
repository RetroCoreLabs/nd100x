/*
 * device_paper_tape.h - Paper tape reader interface: registers and API.
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

#ifndef DEVICE_PAPER_TAPE_H
#define DEVICE_PAPER_TAPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../devices_types.h"

/*
 * Paper Tape Reader Interface (ND-06.015.02)
 *
 * TW0: 0400-0403 (ident 02, level 12)
 * TW1: 0404-0407 (ident 022, level 12)
 *
 * SINTRAN logical device: 3
 */

// Paper tape registers
// clang-format off
typedef enum {
    PAPERTAPE_READ_DATA_REGISTER = 0,    // 0400: Read data (8-bit)
    PAPERTAPE_WRITE_DATA_BUFFER = 1,     // 0401: Not used for reader
    PAPERTAPE_READ_STATUS_REGISTER = 2,  // 0402: Read status
    PAPERTAPE_WRITE_CONTROL_WORD = 3     // 0403: Write control
} PaperTapeRegisters;
// clang-format on

// Status register bits (IOX +2, Read)
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t interruptEnabled : 1;    // Bit 0: Interrupt enabled on ready
        uint16_t notUsed1 : 1;            // Bit 1: Not used
        uint16_t readActive : 1;          // Bit 2: Read is active
        uint16_t readyForTransfer : 1;    // Bit 3: Reader ready for transfer
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
} PaperTapeStatus;
// clang-format on

// Control word bits (IOX +3, Write)
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t interruptEnabled : 1;    // Bit 0: Enable interrupt on ready
        uint16_t notUsed1 : 1;            // Bit 1: Not used
        uint16_t readActive : 1;          // Bit 2: Activate read
        uint16_t testMode : 1;            // Bit 3: Test mode (NOT readyForTransfer)
        uint16_t deviceClear : 1;         // Bit 4: Device clear
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
} PaperTapeControl;
// clang-format on

// Paper tape device data
typedef struct
{
    uint8_t characterBuffer;
    PaperTapeStatus statusRegister;
    PaperTapeControl controlWord;

    // Tape image buffer (loaded from file or via WASM upload)
    uint8_t *tapeData;
    size_t tapeLength;
    size_t tapePosition;
} PaperTapeData;

// Function declarations
/**
 * @brief Create and initialize the paper tape reader device.
 * @param thumbwheel Card thumbwheel; selects the IOX address block.
 * @return The new Device, or NULL on allocation failure.
 */
Device *ptr_create_paper_tape_device(uint8_t thumbwheel);

/**
 * @brief Replace the reader's tape buffer with new tape data and rewind to
 *        position 0.
 * @param self The paper tape reader device.
 * @param data Tape bytes to load.
 * @param length Number of bytes in data.
 */
void ptr_load_tape(Device *self, const uint8_t *data, size_t length);

#endif /* DEVICE_PAPER_TAPE_H */
