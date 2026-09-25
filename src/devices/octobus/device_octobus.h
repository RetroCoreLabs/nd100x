/*
 * device_octobus.h - ND-100 octobus interface: registers and API.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2026 Ronny Hansen
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

#ifndef DEVICE_OCTOBUS_H
#define DEVICE_OCTOBUS_H

#include <stdbool.h>
#include <stdint.h>
#include "../devices_types.h"

/*
 * Octobus Interface (ND-05.020.01, Appendix 2)
 *
 * The ND-100's end of the octobus - the serial, self-arbitrating message bus
 * that carries commands (never bulk data) between the ND-100 and up to seven
 * ND-5000 CPUs. The ND-100 is always octobus station 1B.
 *
 * TW0: 100400-100407 (idents 40B receive / 41B transmit, level 13)
 * TW1: 100410-100417 (idents 42B / 43B, level 13)
 * TW2: 100420-100427 (idents 44B / 45B, level 13)
 * TW3: 100430-100437 (idents 46B / 47B, level 13)
 *
 * SINTRAN logical device: 2400
 *
 * WHERE EVERY NUMBER COMES FROM
 *
 * Addresses are byte-verified: the resident commoncode E-frame sender at 063247
 * IOXTs the literal constants 100405 (write data) and 100406 (read status), and
 * s3vs-4.symb puts OOCT0 at 100400+4, so the input base 100400 follows from the
 * +4 controller spacing.
 *
 * Register semantics come from NPL source line by line: PH-P2-OPPSTART.NPL:4049
 * reads +2 to detect the card, :4054 writes 20 octal to +3 (DCONT) to clear the
 * input interface and :4055 reaches +7 for the output one, :3923 spins on output
 * status bit 3, :3931 writes the master clear to +5.
 *
 * Ident codes are live-verified: TPE OCTOBUS B00's LIST-OCTOBUS-DEVICES prints
 * the table above verbatim.
 *
 * TWO IDENT CLAIMS ARE REFUTED, and both are recorded because both look right:
 * the formula "ident = ((devaddr - 100200) / 4) + 20", which yields 60B; and
 * reading the L-VSX-500 L07 ITB13+37B / +40B slots as idents 37B/40B - with
 * those, TPE gets the level-13 interrupt but does not attribute the ident to the
 * octobus and prints "No Octobus interrupt detected".
 *
 * SINTRAN's OCSTART only ever handles interface 0; the others exist in the
 * hardware catalogue and in TPE's table.
 */

/** Interface 0's register block. The others are +010 octal per interface. */
#define OCTOBUS_BASE_ADDRESS 0100400
#define OCTOBUS_REGISTERS    8
#define OCTOBUS_MAX_CARDS    4

/** Receive FIFO depth in words. TPE's test 3 verifies it is exactly 16. */
#define OCTOBUS_RX_FIFO_WORDS 16

/**
 * The ND-100's own octobus station number. Fixed in the hardware (the T329
 * station table lists the ND-120 CPU at 1B), so it is not configurable - the
 * thumbwheel selects the INTERFACE, not the station.
 */
#define OCTOBUS_ND100_STATION 1

/**
 * A frame addressed to station 0, OR to the card's own station, is a LOCAL
 * HARDWARE LOOPBACK: the sender's own input side receives it - ready-for-transfer
 * in +2 and the frame in +0 with our station stamped as the source.
 *
 * Both destinations are used, for different tests:
 *   dest 0            TPE's LIST-HARDWARE-CONFIGURATION self-send cross-check
 *   dest own station  TPE test 1 (carve Q4, ram:c1f2 / ram:c1a3)
 *
 * This MUST be decided BEFORE any bus routing. The card's own adapter is a
 * station on the fabric at its own number, so routing a self-send out through
 * the bus would deliver it during the output-interrupt service instead of
 * consistently. Both tests poll with roughly a hundred-iteration window, so
 * immediate delivery is the simplest valid model.
 */
#define OCTOBUS_IS_SELF_LOOP(dest, own) ((dest) == 0 || (dest) == (own))

// Octobus registers. Input controller +0..+3, output controller +4..+7;
// even addresses read, odd addresses write.
// clang-format off
typedef enum {
    OCTOBUS_READ_INPUT_DATA      = 0, // 100400: Read received data (pops the FIFO)
    OCTOBUS_WRITE_INPUT_DATA     = 1, // 100401: Write data to input controller
    OCTOBUS_READ_INPUT_STATUS    = 2, // 100402: Read input status (presence probe)
    OCTOBUS_WRITE_INPUT_CONTROL  = 3, // 100403: Write input control (DCONT)
    OCTOBUS_READ_OUTPUT_DATA     = 4, // 100404: Read output data
    OCTOBUS_WRITE_OUTPUT_COMMAND = 5, // 100405: Write command/frame to transmit
    OCTOBUS_READ_OUTPUT_STATUS   = 6, // 100406: Read output status (bit 3 = ready)
    OCTOBUS_WRITE_OUTPUT_CONTROL = 7  // 100407: Write output control (DCONT)
} OctobusRegisters;
// clang-format on

// Input (receive) status register bits (IOX +2, Read)
// Source: hardware test program decode, via RetroCore ReceiveStatusBits.
//
// BIT 2 IS INVERTED from how its name reads: SET means the FIFO has SPACE.
// TPE's "check receive fifo length" writes while it is set and counts what fits,
// so reading it as "FIFO full" either writes nothing or never terminates.
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t interruptEnabled : 1;    // Bit 0: Interrupt enabled
        uint16_t notUsed1 : 1;            // Bit 1: Not used
        uint16_t fifoNotFull : 1;         // Bit 2: SET = FIFO has space
        uint16_t dataAvailable : 1;       // Bit 3: FIFO holds data
        uint16_t speedBit1 : 1;           // Bit 4: Speed field
        uint16_t speedBit2 : 1;           // Bit 5: Speed field
        uint16_t notUsed6 : 1;            // Bit 6: Not used
        uint16_t notUsed7 : 1;            // Bit 7: Not used
        uint16_t station : 6;             // Bits 8-13: Sender station number
        uint16_t notUsed14 : 1;           // Bit 14: Not used
        uint16_t notUsed15 : 1;           // Bit 15: Not used
    } bits;
} OctobusInputStatus;
// clang-format on

// Input control word bits (IOX +3, Write). DCONT = 3.
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t interruptEnabled : 1;    // Bit 0: Enable interrupt
        uint16_t notUsed1 : 1;            // Bit 1: Not used
        uint16_t notUsed2 : 1;            // Bit 2: Not used
        uint16_t notUsed3 : 1;            // Bit 3: Not used
        uint16_t deviceClear : 1;         // Bit 4: 20 octal clears the interface
        uint16_t notUsed5 : 1;            // Bit 5: Not used
        uint16_t notUsed6 : 1;            // Bit 6: Not used
        uint16_t notUsed7 : 1;            // Bit 7: Not used
        uint16_t notUsed8 : 1;            // Bit 8: Not used
        uint16_t notUsed9 : 1;            // Bit 9: Not used
        uint16_t notUsed10 : 1;           // Bit 10: Not used
        uint16_t notUsed11 : 1;           // Bit 11: Not used
        uint16_t notUsed12 : 1;           // Bit 12: Not used
        uint16_t notUsed13 : 1;           // Bit 13: Not used
        uint16_t notUsed14 : 1;           // Bit 14: Not used
        uint16_t notUsed15 : 1;           // Bit 15: Not used
    } bits;
} OctobusInputControl;
// clang-format on

// Output (transmit) status register bits (IOX +6, Read)
//
// CH5CPUPRESENT spins on bit 3 before sending a command
// (PH-P2-OPPSTART.NPL:3923), so a card that never sets it HANGS the probe rather
// than reporting a missing CPU.
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t interruptEnabled : 1;    // Bit 0: Interrupt enabled
        uint16_t notUsed1 : 1;            // Bit 1: Not used
        uint16_t notUsed2 : 1;            // Bit 2: Not used
        uint16_t readyForTransfer : 1;    // Bit 3: Ready to accept a command
        uint16_t notUsed4 : 1;            // Bit 4: Not used
        uint16_t notUsed5 : 1;            // Bit 5: Not used
        uint16_t notUsed6 : 1;            // Bit 6: Not used
        uint16_t notUsed7 : 1;            // Bit 7: Not used
        uint16_t notUsed8 : 1;            // Bit 8: Not used
        uint16_t notUsed9 : 1;            // Bit 9: Not used
        uint16_t notUsed10 : 1;           // Bit 10: Not used
        uint16_t notUsed11 : 1;           // Bit 11: Not used
        uint16_t notUsed12 : 1;           // Bit 12: Not used
        uint16_t notUsed13 : 1;           // Bit 13: Not used
        uint16_t notUsed14 : 1;           // Bit 14: Not used
        uint16_t notUsed15 : 1;           // Bit 15: Not used
    } bits;
} OctobusOutputStatus;
// clang-format on

// Output control word bits (IOX +7, Write). Same shape as the input control.
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t interruptEnabled : 1;    // Bit 0: Enable interrupt
        uint16_t notUsed1 : 1;            // Bit 1: Not used
        uint16_t notUsed2 : 1;            // Bit 2: Not used
        uint16_t notUsed3 : 1;            // Bit 3: Not used
        uint16_t deviceClear : 1;         // Bit 4: 20 octal clears the interface
        uint16_t notUsed5 : 1;            // Bit 5: Not used
        uint16_t notUsed6 : 1;            // Bit 6: Not used
        uint16_t notUsed7 : 1;            // Bit 7: Not used
        uint16_t notUsed8 : 1;            // Bit 8: Not used
        uint16_t notUsed9 : 1;            // Bit 9: Not used
        uint16_t notUsed10 : 1;           // Bit 10: Not used
        uint16_t notUsed11 : 1;           // Bit 11: Not used
        uint16_t notUsed12 : 1;           // Bit 12: Not used
        uint16_t notUsed13 : 1;           // Bit 13: Not used
        uint16_t notUsed14 : 1;           // Bit 14: Not used
        uint16_t notUsed15 : 1;           // Bit 15: Not used
    } bits;
} OctobusOutputControl;
// clang-format on

// Octobus device data
typedef struct
{
    OctobusInputStatus statusRegister;      // Input status  (IOX +2)
    OctobusInputControl controlWord;        // Input control (IOX +3)
    OctobusOutputStatus outputStatusRegister;  // Output status  (IOX +6)
    OctobusOutputControl outputControlWord;    // Output control (IOX +7)

    uint16_t inputData;
    uint16_t outputData;

    // This card's own octobus station number.
    uint16_t stationAddress;

    // Receive FIFO: a ring, so a drain is not quadratic - TPE drains the whole
    // FIFO in a loop.
    uint16_t rxFifo[OCTOBUS_RX_FIFO_WORDS];
    int rxHead;
    int rxCount;

    // Interrupt request flip-flops, latched by an EVENT and cleared by IDENT.
    // Separate from the enables in the status registers: the event is what
    // happened, the enable is whether anyone asked to hear about it.
    bool inputIrqPending;
    bool outputIrqPending;

    // The bus seam. NULL = standalone, and a write to the command register then
    // transmits nothing, which is what lets TPE's tests 1 to 3 run with no bus.
    OctobusTransmitFn transmit;
    void *transmitCtx;

    // Diagnostics: what the probes did, so a failure names the step.
    unsigned long clears;
    unsigned long commands;
    uint16_t lastCommand;
} OctobusData;

// Function declarations
/**
 * @brief Create and initialize an ND-100 octobus interface card.
 * @param thumbwheel Card thumbwheel 0..3; selects the IOX address block and the
 *                   ident pair.
 * @return The device, or NULL for a thumbwheel outside 0..3 or an allocation
 *         failure.
 */
Device *octobus_create_device(uint8_t thumbwheel);

/**
 * @brief Install the handler that carries frames from this card onto the bus.
 *
 * The card knows nothing about the octobus fabric - src/devices/ does not link
 * it - so whoever owns the bus installs a handler and routes the frame. Replies
 * come back through octobus_rx_push(), which is how the hardware presents them.
 *
 * @param self Device returned by octobus_create_device().
 * @param fn   The handler, or NULL to detach and return to standalone.
 * @param ctx  Passed back to the handler unchanged.
 */
void octobus_set_transmit(Device *self, OctobusTransmitFn fn, void *ctx);

/**
 * @brief Deliver one frame into a card's receive FIFO, as the bus would.
 *
 * Raises the input event, so an enabled input interrupt asserts level 13.
 *
 * @param self Device returned by octobus_create_device().
 * @param word The 16-bit frame to deliver.
 * @return true when queued; false when the FIFO is full, which is the card
 *         dropping the frame exactly as the hardware does.
 */
bool octobus_rx_push(Device *self, uint16_t word);

/**
 * @brief How many words are in a card's receive FIFO.
 * @param self Device returned by octobus_create_device().
 * @return The count, 0 to OCTOBUS_RX_FIFO_WORDS.
 */
int octobus_rx_count(Device *self);

#endif /* DEVICE_OCTOBUS_H */
