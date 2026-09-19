/*
 * hdlc_crc.h - HDLC CRC-16, CCITT CRC and parity function declarations.
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

#ifndef HDLC_CRC_H
#define HDLC_CRC_H

#include <stdint.h>
#include <stdbool.h>

// Parity modes
typedef enum
{
    HDLC_PARITY_ODD,
    HDLC_PARITY_EVEN
} HDLCParityMode;

// CRC calculation functions
uint16_t HDLC_CRC_CalculateCRC16Buffer(uint16_t crc, const uint8_t *buf, int length);
uint16_t HDLC_CRC_CalcCrc16(uint16_t crc, uint8_t byte);
uint16_t HDLC_CRC_CalcCCITT(uint16_t fcs, uint8_t byte);

// Parity functions
uint8_t HDLC_CRC_CalculateParityBit(uint8_t data, HDLCParityMode mode);
uint8_t HDLC_CRC_AddParityBit(uint8_t data, HDLCParityMode mode);
bool HDLC_CRC_CheckParity(uint8_t data, HDLCParityMode mode);

#endif // HDLC_CRC_H
