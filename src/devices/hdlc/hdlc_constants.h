/*
 * hdlc_constants.h - HDLC protocol and asynchronous framing constants.
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

#ifndef HDLC_CONSTANTS_H
#define HDLC_CONSTANTS_H

// HDLC Protocol Constants
// clang-format off
#define HDLC_FRAME_DELIMITER 0x7E        // 01111110 - Frame delimiter
#define HDLC_FRAME_ABORT 0xFF            // 11111111 - Frame abort (7+ contiguous 1's)
#define HDLC_GO_AHEAD 0xFE               // 11111110 - Go ahead (LSB is 0, then 7x 1's)
#define HDLC_ADDRESS_ALL 0xFF            // 11111111 - Address all devices
// clang-format on

// HDLC Asynchronous framing constants
// clang-format off
#define HDLC_ASYNC_ESCAPE_OCTET 0x7D     // 01111101 - Escape octet for data that might be interpreted as control
#define HDLC_ASYNC_INVERT_OCTET 0x20     // 00100000 - Value to invert bit 5 of escaped data octet
// clang-format on

// CRC constants
#define HDLC_CCITT_VALID_CRC 0xF0B8 // Valid CCITT CRC value

// Queue and buffer limits
#define HDLC_MAX_RECEIVE_QUEUE_SIZE 1000
#define HDLC_MAX_NO_DATA            100

// Default network port
#define HDLC_DEFAULT_PORT 1362

#endif // HDLC_CONSTANTS_H
