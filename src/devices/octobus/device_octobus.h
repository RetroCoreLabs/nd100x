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

/** Interface 0's register block. The others are +010 octal per interface. */
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
/** The eight registers of one octobus interface, as offsets from its base. */
typedef enum
{
    OCTOBUS_REG_IN_READ_DATA    = 0, /**< +0 read data, input controller */
    OCTOBUS_REG_IN_WRITE_DATA   = 1, /**< +1 write data */
    OCTOBUS_REG_IN_READ_STATUS  = 2, /**< +2 read status; OCSTART's presence probe */
    OCTOBUS_REG_IN_WRITE_CTRL   = 3, /**< +3 write control (DCONT); 20 octal clears */
    OCTOBUS_REG_OUT_READ_DATA   = 4, /**< +4 read data, output controller */
    OCTOBUS_REG_OUT_WRITE_CMD   = 5, /**< +5 write command; CMMACLE master clear */
    OCTOBUS_REG_OUT_READ_STATUS = 6, /**< +6 read status; bit 3 is data ready */
    OCTOBUS_REG_OUT_WRITE_CTRL  = 7  /**< +7 write control, the output DCONT */
} OctobusRegister;

/**
 * Receive FIFO depth, in 16-bit words.
 *
 * TPE's octobus test 3, "Check receive fifo length", verifies this exactly: it
 * writes words while input status bit 2 stays set and counts how many it got in.
 * The count must be 16.
 */
#define OCTOBUS_RX_FIFO_WORDS 16

/**
 * Input status bit 2: FIFO NOT FULL.
 *
 * INVERTED from what the name might suggest, and this is the bit TPE counts on:
 *   SET   (1) = the FIFO has space and will accept another word
 *   CLEAR (0) = the FIFO is full
 *
 * Set after reset or master clear, because an empty FIFO has maximum space.
 * Reading it as "FIFO full" inverts the fill loop, which either writes nothing
 * or never stops.
 *
 * Source: hardware test program decode, via RetroCore's ReceiveStatusBits.
 */
#define OCTOBUS_IN_STATUS_FIFO_NOT_FULL (1u << 2u)

/**
 * Input status bit 3: data available in the receive FIFO.
 *
 * RetroCore's enum calls this bit Ready For Transfer, noting it overlaps the
 * speed field's low bit and that RFT is the primary meaning; its implementation
 * sets it "when FIFO has data available", and its threaded test reads it as
 * "RX data available". Those are the same bit with two names.
 */
#define OCTOBUS_IN_STATUS_DATA_AVAIL (1u << 3u)

/** Input status bits 8-13 carry the station number of the sender. */
#define OCTOBUS_IN_STATUS_STATION_SHIFT 8u
#define OCTOBUS_IN_STATUS_STATION_MASK  (0x3Fu << OCTOBUS_IN_STATUS_STATION_SHIFT)


/* OctobusTransmitFn is declared in devices_types.h: the generated
 * devices_protos.h names it in this function's prototype, and every consumer of
 * that header must be able to see the type. */

/**
 * @brief Install the handler that carries frames from this card onto the bus.
 *
 * With no handler installed the card accepts writes to the output command
 * register and transmits NOTHING - which is the correct standalone behaviour,
 * and is why TPE's tests 1 to 3 pass without a bus.
 *
 * @param self Device returned by octobus_create_device().
 * @param fn   The handler, or NULL to detach.
 * @param ctx  Passed back to the handler unchanged.
 */
void octobus_set_transmit(Device *self, OctobusTransmitFn fn, void *ctx);

/**
 * @brief Push one word into a card's receive FIFO, as an arriving frame would.
 *
 * The frame path from a fabric is not wired yet; this is how a test - and later
 * the octobus fabric - delivers a word to the ND-100 side.
 *
 * @param self Device returned by octobus_create_device().
 * @param word The 16-bit frame to deliver.
 * @return true when the word was queued; false when the FIFO is full, which is
 *         the card dropping it exactly as the hardware does.
 */
bool octobus_rx_push(Device *self, uint16_t word);

/**
 * @brief How many words are in a card's receive FIFO.
 * @param self Device returned by octobus_create_device().
 * @return The count, 0 to OCTOBUS_RX_FIFO_WORDS.
 */
int octobus_rx_count(Device *self);

/**
 * Output status bit 3: data ready.
 *
 * CH5CPUPRESENT spins on it before sending a command
 * (PH-P2-OPPSTART.NPL:3923), so a card that never sets it HANGS the probe
 * rather than reporting a missing CPU.
 */
#define OCTOBUS_STATUS_DATA_READY (1u << 3u)

/** Control value 20 octal clears an interface (PH-P2-OPPSTART.NPL:4054). */
#define OCTOBUS_CTRL_CLEAR 020

/**
 * @brief Create one ND-100 octobus interface card.
 *
 * Interfaces are 010 octal apart: thumbwheel 0 is 100400-100407 with receive
 * ident 40B and transmit ident 41B, thumbwheel 1 is 100410 with 42B/43B, and so
 * on. All four are on interrupt level 13. SINTRAN's OCSTART only ever handles
 * interface 0; the others exist in the hardware catalogue and in TPE's table.
 *
 * @param thumbwheel Which of the four interfaces, 0 to 3.
 * @return The device, or NULL for a thumbwheel outside 0..3 or an allocation
 *         failure. The caller owns the device and releases it through the
 *         device manager.
 */
Device *octobus_create_device(uint8_t thumbwheel);

#endif /* DEVICE_OCTOBUS_H */
