/*
 * device_octobus.h - ND-100 octobus interface card
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2026 Ronny Hansen
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
 *
 *
 * The ND-100's end of the octobus - the serial, self-arbitrating message bus
 * that carries commands (never bulk data) between the ND-100 and up to seven
 * ND-5000 CPUs. The ND-100 is always station 1B.
 *
 * ADDRESSES AND IDENT CODES - WHERE EVERY NUMBER COMES FROM
 *
 * Four interfaces, eight registers each, spaced 010 octal apart:
 *
 *      interface   first    last    idents (level 13)
 *          1      100400  100407     40, 41
 *          2      100410  100417     42, 43
 *          3      100420  100427     44, 45
 *          4      100430  100437     46, 47
 *
 * The ADDRESSES are byte-verified: the resident commoncode E-frame sender at
 * 063247 IOXTs the literal constants 100405 (write data) and 100406 (read
 * status), and s3vs-4.symb puts OOCT0 at 100400+4, so the input base 100400
 * follows from the +4 controller spacing.
 *
 * The IDENT CODES are LIVE-VERIFIED: TPE OCTOBUS B00's LIST-OCTOBUS-DEVICES
 * prints exactly the table above, receive then transmit, all on level 13.
 *
 * TWO IDENT CLAIMS ARE REFUTED, and they are recorded because both look
 * plausible and both are wrong:
 *
 *   - The "ident = ((devaddr - 100200) / 4) + 20" formula yields 60B for
 *     interface 0. Wrong.
 *   - A byte carve of the L-VSX-500 L07 resident image reads MEM[ITB13+37B] =
 *     IOCT0 and MEM[ITB13+40B] = OOCT0, which looks like idents 37B and 40B.
 *     With those codes TPE gets the level-13 interrupt but does not attribute
 *     the returned ident to the octobus controller: "No Octobus interrupt
 *     detected". So the ITB13 slot index is NOT the ident code, and how that
 *     table is indexed is still open - but the IDENTS themselves are settled at
 *     40B/41B by the hardware's own printed table.
 *
 * SINTRAN's OCSTART only ever handles interface 0; the other three exist in the
 * hardware catalogue and in TPE's table.
 */

#ifndef DEVICE_OCTOBUS_H
#define DEVICE_OCTOBUS_H

#include "../devices_types.h"

/* Interface 0's register block. The others are +010 octal per interface. */
#define OCTOBUS_BASE_ADDRESS 0100400
#define OCTOBUS_REGISTERS    8
#define OCTOBUS_MAX_CARDS    4
#define OCTOBUS_INT_LEVEL    13

/*
 * The eight registers. Even addresses read, odd addresses write - the ND-100
 * convention, and it holds here.
 *
 * Input controller (+0..+3), datafield IOCT0, driver SOCTO:
 *   +0  read data
 *   +1  write data
 *   +2  read status. OCSTART uses this to detect the card at all:
 *       "T:=HDEV+2; *IOXT" (PH-P2-OPPSTART.NPL:4049). An IOX error - A=7 -
 *       means no interface is present.
 *   +3  write control (DCONT = 3). "T:=HDEV+DCONT; 20; *IOXT"
 *       (PH-P2-OPPSTART.NPL:4054): the value 20 octal CLEARS the interface.
 *
 * Output controller (+4..+7), datafield OOCT0, driver SOCTW. Its own base is
 * 100404, so these are +0..+3 relative to it:
 *   +4  read data
 *   +5  write command. "T:=100405; A\/CMMACLE; *IOXT"
 *       (PH-P2-OPPSTART.NPL:3931) sends the master clear to SAMSON.
 *   +6  read status. "T:=100406; *IOXT; WHILE A NBIT 3"
 *       (PH-P2-OPPSTART.NPL:3923): BIT 3 IS DATA READY, and A=0 after the IOX
 *       means the interface is present.
 *   +7  write control, the output controller's DCONT. OCSTART reaches it as
 *       "T+4" from +3 (PH-P2-OPPSTART.NPL:4055).
 */
typedef enum
{
    OCTOBUS_REG_IN_READ_DATA    = 0,
    OCTOBUS_REG_IN_WRITE_DATA   = 1,
    OCTOBUS_REG_IN_READ_STATUS  = 2,
    OCTOBUS_REG_IN_WRITE_CTRL   = 3,
    OCTOBUS_REG_OUT_READ_DATA   = 4,
    OCTOBUS_REG_OUT_WRITE_CMD   = 5,
    OCTOBUS_REG_OUT_READ_STATUS = 6,
    OCTOBUS_REG_OUT_WRITE_CTRL  = 7
} OctobusRegister;

/* Output status bit 3: data ready. CH5CPUPRESENT spins on it before sending a
 * command, so a card that never sets it hangs the probe rather than failing. */
#define OCTOBUS_STATUS_DATA_READY (1u << 3)

/* Control value 20 octal clears an interface (PH-P2-OPPSTART.NPL:4054). */
#define OCTOBUS_CTRL_CLEAR 020

/* Create the octobus card for `thumbwheel` 0..3. NULL for anything else. */
Device *octobus_create_device(uint8_t thumbwheel);

#endif /* DEVICE_OCTOBUS_H */
