/*
 * device_terminal.h - Terminal device: registers, status bits and input buffer.
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


#ifndef DEVICE_TERMINAL_H
#define DEVICE_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../devices_types.h"

#define TERMINAL_QUEUE_SIZE 256
#define MAX_TICKS           100 // Check for new incoming characters


// Circular buffer structure for input queue
typedef struct
{
    uint8_t buffer[TERMINAL_QUEUE_SIZE];
    size_t head;
    size_t tail;
    volatile size_t count; /* written by DAP thread, read by CPU thread */
} CircularBuffer;

// Terminal registers
// clang-format off
typedef enum {
    TERMINAL_READ_INPUT_DATA = 0,      // 300: Read input data
    TERMINAL_WRITE_NO_OPERATION = 1,   // 301: No operation
    TERMINAL_READ_INPUT_STATUS = 2,    // 302: Read input status
    TERMINAL_WRITE_INPUT_CONTROL = 3,  // 303: Set input control
    TERMINAL_READ_RETURN0 = 4,         // 304: Returns 0 in the A register and has no other effect.
    TERMINAL_WRITE_DATA = 5,           // 305: Write data (according to input control word setting).
    TERMINAL_READ_OUTPUT_STATUS = 6,   // 306: Read output status.
    TERMINAL_WRITE_SET_OUTPUT_CONTROL = 7 // 307: Set output control
} TerminalRegister;
// clang-format on

// Input status register bits
// IOX 302: Read input status.
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t interruptEnabled : 1;         // Bit 0: Data available will give interrupt
        uint16_t reserved1 : 1;                // Bit 1: Not used
        uint16_t deviceActivated : 1;          // Bit 2: Device is activated
        uint16_t deviceReadyForTransfer : 1;   // Bit 3: Data is available
        uint16_t errorOr : 1;                  // Bit 4: Inclusive or of error bits 5-7
        uint16_t framingError : 1;             // Bit 5: Framing error
        uint16_t parityError : 1;              // Bit 6: Parity error
        uint16_t overrunError : 1;             // Bit 7: Overrun
        uint16_t reserved2 : 3;                // Bits 8-10: Not used
        uint16_t carrierMissing : 1;           // Bit 11: Carrier missing
        uint16_t reserved3 : 4;                // Bits 12-15: Not used
        //Bits 1-2 and 8-15 are always zero.
    } bits;
} InputStatusRegister;
// clang-format on

// Input control register bits
// IOX 303: Set input control.
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t interruptEnabled : 1;     // Bit 0: Enable interrupt if data available
        uint16_t reserved1 : 1;            // Bit 1: Not used
        uint16_t deviceActivated : 1;      // Bit 2: Device is activated
        uint16_t testMode : 1;             // Bit 3: Test mode
        uint16_t deviceClear : 1;          // Bit 4: Device clear
        uint16_t reserved2 : 6;            // Bits 5-10: Not used
        uint16_t characterLength : 2;      // Bits 11-12: Character length (0=8, 1=7, 2=6, 3=5)
        uint16_t stopBits : 1;             // Bit 13: Stop bits (0=2 bits, 1=1 bit)
        uint16_t parityGeneration : 1;     // Bit 14: Parity generation (If this control bit is 0, no parity will bit will be added to the character on the output channel and the received character will not be checked for parity. A 1 in this control bit will add an even parity bit to the character on the output channel, and give an error indication if the received character has an odd parity.)
        uint16_t reserved3 : 1;            // Bit 15: Not used
    } bits;
} InputControlRegister;
// clang-format on

// Output status register bits
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t interruptEnabled : 1;     // Bit 0: Ready for transfer will give interrupt
        uint16_t reserved1 : 2;            // Bits 1-2: Not used
        uint16_t readyForTransfer : 1;     // Bit 3: Ready for transfer
        uint16_t reserved2 : 2;            // Bits 4-5: Not used
        uint16_t reserved3 : 10;           // Bits 6-15: Not used
    } bits;
} OutputStatusRegister;
// clang-format on

// Output control register bits
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t interruptEnabled : 1;     // Bit 0: Enable interrupt if ready for transfer
        uint16_t reserved : 15;            // Bits 1-15: Not used
    } bits;
} OutputControlRegister;
// clang-format on

// Device definition structure
typedef struct
{
    uint16_t addressBase;
    uint16_t identCode;
    uint16_t logicalDevice;
    const char *deviceName;
} DeviceDefinition;

// Terminal device data structure
typedef struct
{
    /*
    bool active;
    bool inputReadyForTransfer;

    bool inputInterruptEnabled;
    bool testMode;
    bool parityGeneration;
    */

    bool noCarrier;
    // UART input buffer
    uint16_t uartInputBuf;
    int checkInputQueueTick;

    // Add input queue
    CircularBuffer inputQueue;

    InputStatusRegister inputStatus;
    InputControlRegister inputControl;
    OutputStatusRegister outputStatus;
    OutputControlRegister outputControl;
} TerminalData;

// Function declarations
/**
 * @brief Create and initialize the console terminal character device.
 * @param thumbwheel Card thumbwheel; selects the IOX address block.
 * @return The new Device, or NULL on allocation failure.
 */
Device *CreateTerminalDevice(uint8_t thumbwheel);

/**
 * @brief Append a keycode to the terminal's input queue, or set the overrun
 *        error bit if the queue is full.
 * @param self The terminal device.
 * @param keycode The keycode to queue.
 */
void Terminal_QueueKeyCode(Device *self, uint8_t keycode);
#endif // DEVICE_TERMINAL_H
