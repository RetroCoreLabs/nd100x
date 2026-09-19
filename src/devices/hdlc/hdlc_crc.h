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

/**
 * @brief Fold a whole buffer into a running CRC-16 (polynomial 0xA001)
 *        accumulator by calling HDLC_CRC_CalcCrc16 for each byte.
 * @param crc Current CRC accumulator value.
 * @param buf Bytes to checksum.
 * @param length Number of bytes in buf.
 * @return Updated CRC value, or the unchanged crc if buf is NULL.
 */
uint16_t HDLC_CRC_CalculateCRC16Buffer(uint16_t crc, const uint8_t *buf, int length);

/**
 * @brief Fold one byte into a running CRC-16 (polynomial x^16+x^15+x^2+1,
 *        0xA001) accumulator using the 4-bit nibble lookup table.
 * @param crc Current CRC accumulator value.
 * @param byte Next byte to fold in.
 * @return Updated CRC value.
 */
uint16_t HDLC_CRC_CalcCrc16(uint16_t crc, uint8_t byte);

/**
 * @brief Fold one byte into a running CRC-16-CCITT (polynomial
 *        x^16+x^12+x^5+1, 0x1021) FCS accumulator using the byte lookup
 *        table.
 * @param fcs Current FCS accumulator value.
 * @param byte Next byte to fold in.
 * @return Updated FCS value.
 */
uint16_t HDLC_CRC_CalcCCITT(uint16_t fcs, uint8_t byte);

// Parity functions

/**
 * @brief Compute and set the parity bit (MSB, bit 7) of a data byte for the
 *        given parity mode, using the precomputed parity table.
 * @param data 7-bit data value to add a parity bit to.
 * @param mode Parity mode to apply (odd or even).
 * @return data with bit 7 set to the computed parity bit.
 */
uint8_t HDLC_CRC_AddParityBit(uint8_t data, HDLCParityMode mode);

/**
 * @brief Test whether a byte's precomputed parity bit is set for the given
 *        mode.
 * @param data Byte to check.
 * @param mode Parity mode to check against (odd or even).
 * @return true if the table parity bit for data under mode is 1, false
 *         otherwise.
 */
bool HDLC_CRC_CheckParity(uint8_t data, HDLCParityMode mode);

#endif // HDLC_CRC_H
