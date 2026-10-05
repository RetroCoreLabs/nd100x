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
 * Depth of the busy-retry park, in words.
 *
 * On the real bus a receiver whose 16-word FIFO is full answers Ack=10
 * (destination busy) and the SENDER's hardware retries the frame until it is
 * accepted - nothing is lost. The emulator hands a station's whole reply over
 * in one call, so the retry is modelled on the receiving side: frames that do
 * not fit wait here in arrival order and move into the FIFO as the guest drains
 * it. Without this a multibyte reply longer than 16 frames is TRUNCATED, and
 * the guest reads a different message than the one that was sent.
 *
 * The longest reply either side sends is the ACCP's and the Octobus Test
 * Protocol's: 255 payload bytes plus the SOMB, source-OMD, count and EOMB
 * frames, which is what nd500x sizes its own reply buffer to
 * (NDBUS_MAX_REPLY_FRAMES in src/ndbus/ndbus_octobus.h). One full reply can
 * therefore always be parked.
 */
#define OCTOBUS_BUSY_RETRY_WORDS (4 + 255)

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
 *
 * WHETHER THE SELF-SEND IS ECHOED depends on the card's mode: it is echoed while
 * the card is in loopback mode or no CPU station is attached, and it completes
 * WITHOUT an echo otherwise ($RETROCORE/Emulated.HW/ND/CPU/NDBUS/NDBusOctobus.cs
 * ProcessTransmitQueue:3239, 3288-3306). See octobus_set_cpu_attached().
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
        uint16_t station : 6;             // Bits 8-13: STORED = source of the last frame;
                                          //            a READ of +2 always shows the
                                          //            card's own station instead
        uint16_t notUsed14 : 1;           // Bit 14: Not used
        uint16_t notUsed15 : 1;           // Bit 15: Not used
    } bits;
} OctobusInputStatus;
// clang-format on

// Input control word bits (IOX +3, Write). DCONT = 3.
// Bit names and positions: NDBusOctobus.cs ControlWordBits:1063-1110, which is
// ONE enum for both control words. What each bit does, and in which order when
// several are set in one word: ProcessControlChange:2837-2952.
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t interruptEnabled : 1;    // Bit 0: Enable interrupt
        uint16_t notUsed1 : 1;            // Bit 1: Not used
        uint16_t notUsed2 : 1;            // Bit 2: Not used
        uint16_t notUsed3 : 1;            // Bit 3: Not used
        uint16_t deviceClear : 1;         // Bit 4: 20 octal clears the interface
        uint16_t continueAccp : 1;        // Bit 5: Continue ACCP (calls the installed hook)
        uint16_t reset : 1;               // Bit 6: Reset this controller
        uint16_t testMode : 1;            // Bit 7: Test mode - turns loopback ON
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
//
// EVERY DEFINED BIT IS NAMED, and nine of them are defined. An earlier version of
// this file named bits 0 and 3 and called the other fourteen "not used", which is
// not what the hardware decode says and hid the one bit discovery needs: bit 6,
// NOT PRESENT. Source for the layout: the hardware decode with value 0377 octal,
// via RetroCore NDBusOctobus.cs TransmitStatusBits.
//
// ERROR (bit 4) and NOT PRESENT (bit 6) report the ACK RESULT OF THE LAST
// TRANSFER ONLY - a write to +5 clears both before it attempts delivery, and sets
// both when nothing answers at the destination (Ack=00, timeout after the 15
// hardware retries). That is how a discovery scan tells an absent station from a
// present but silent one.
//
// [INFERENCE] The exact positions of ERROR and NOT PRESENT come from the
// TransmitStatusBits decode, not from a manual, and RetroCore labels them
// inferred for the same reason. TPE OCTOBUS B00 test 4 and
// LIST-HARDWARE-CONFIGURATION are the judges: both probe every station 1-62 with
// unicast emergency frames and must be able to read the timeout result here.
//
// RETRY COUNTER 0 (bit 5), REQUEST ON (bit 2), PARITY ERROR (bit 8) and MASTER
// (bit 15) are named because the hardware defines them, and are NEVER SET by this
// card: no guest behaviour observed so far reads them, and inventing a rule for
// when they assert would be a guess dressed as an emulation.
//
// BUSY (bit 7) is set in exactly one case: a loopback echo that finds the receive
// FIFO full (NDBusOctobus.cs ProcessTransmitQueue:3263-3276). READY is cleared
// with it and the transfer does not complete. The first FIFO status update that
// finds space again, in loopback mode, clears BUSY, sets READY and latches the
// output request (UpdateReceiveFifoStatus:2622-2629). While BUSY is set the output
// controller cannot assert the interrupt line (UpdateInterruptState:2550-2552).
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t interruptEnabled : 1;    // Bit 0: Interrupt enabled
        uint16_t notUsed1 : 1;            // Bit 1: Not used
        uint16_t requestOn : 1;           // Bit 2: Request on (never set here)
        uint16_t readyForTransfer : 1;    // Bit 3: Ready to accept a command
        uint16_t error : 1;               // Bit 4: Last transfer failed
        uint16_t retryCounter0 : 1;       // Bit 5: Retry counter 0 (never set here)
        uint16_t notPresent : 1;          // Bit 6: No station answered the last frame
        uint16_t busy : 1;                // Bit 7: Transfer in progress
        uint16_t parityError : 1;         // Bit 8: Parity error (never set here)
        uint16_t notUsed9 : 1;            // Bit 9: Not used
        uint16_t notUsed10 : 1;           // Bit 10: Not used
        uint16_t notUsed11 : 1;           // Bit 11: Not used
        uint16_t notUsed12 : 1;           // Bit 12: Not used
        uint16_t notUsed13 : 1;           // Bit 13: Not used
        uint16_t notUsed14 : 1;           // Bit 14: Not used
        uint16_t master : 1;              // Bit 15: This card is bus master (never set here)
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
        uint16_t continueAccp : 1;        // Bit 5: Continue ACCP (calls the installed hook)
        uint16_t reset : 1;               // Bit 6: Reset this controller
        uint16_t testMode : 1;            // Bit 7: Test mode - turns loopback ON
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

    // The input data register. A read of +0 with the FIFO EMPTY returns this
    // (NDBusOctobus.cs Read:2703-2707). Loaded by: a pop of the FIFO (2679), a
    // write to +1 (2792), a loopback echo (3261), a frame from the bus (3612), a
    // parked frame landing (3483), and a write to +5 when it still holds 0
    // (3004-3007). Zeroed by reset and by the input clear.
    uint16_t inputData;
    // The output data register: the last word written to +5 (ProcessCommand:2976),
    // read back at +4 (Read:2735-2738). Zeroed by reset and by the output clear.
    uint16_t outputData;

    // This card's own octobus station number.
    uint16_t stationAddress;

    // Receive FIFO: a ring, so a drain is not quadratic - TPE drains the whole
    // FIFO in a loop.
    uint16_t rxFifo[OCTOBUS_RX_FIFO_WORDS];
    int rxHead;
    int rxCount;

    // Frames that arrived while the receive FIFO was full, in arrival order.
    // A ring, drained into rxFifo by octobus_pump_busy_retry() every time the
    // guest pops a word. See OCTOBUS_BUSY_RETRY_WORDS for why these are parked
    // instead of dropped.
    uint16_t busyRetry[OCTOBUS_BUSY_RETRY_WORDS];
    int busyRetryHead;
    int busyRetryCount;
    // How many frames were parked, and how many had to be dropped because even
    // the park was full. A non-zero drop count means a reply WAS mutilated.
    unsigned long busyRetryParked;
    unsigned long busyRetryDropped;

    // Interrupt request flip-flops, latched by an EVENT and cleared by IDENT.
    // Separate from the enables in the status registers: the event is what
    // happened, the enable is whether anyone asked to hear about it.
    bool inputIrqPending;
    bool outputIrqPending;

    // The bus seam. NULL = standalone, and a write to the command register then
    // transmits nothing, which is what lets TPE's tests 1 to 3 run with no bus.
    OctobusTransmitFn transmit;
    void *transmitCtx;

    // Loopback mode (NDBusOctobus.cs _loopbackMode:1572). TRUE from creation and
    // after every reset while no CPU station is attached (Reset:3370), turned ON
    // by control bit 7 TestMode (ProcessControlChange:2868-2874), turned OFF when
    // a CPU station is attached (AttachCpu:2224, AttachMicrocodeStation:2273).
    bool loopbackMode;
    // TRUE once the embedding has said a CPU station is attached - the C# test
    // "_nd500Cpu != null || _microcodeCpuAttached" (3239, 3370). The card cannot
    // find this out by itself; see octobus_set_cpu_attached().
    bool cpuAttached;
    // NDBusOctobus.cs _mudomDetected:1568, read there only through HasND5000Cpu
    // (1606). Set when a CPU station is attached (2220, 2269) and by control bit 5
    // ContinueACCP (2945). Nothing in this card reads it.
    bool mudomDetected;

    // Called when a control word with bit 5 (ContinueACCP) is written to +3 or +7.
    // The C# calls _nd5000Station.ContinueAccp() directly (2946-2950); this card
    // does not link the station, so the embedding installs the call. NULL = no
    // station, which is the C# "_nd5000Station == null" case: nothing is called.
    void (*continueAccp)(void *ctx);
    void *continueAccpCtx;

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
 * @return true when the frame is in the FIFO or parked for retry behind a full
 *         FIFO; false only when the park itself is full and the frame is lost.
 */
bool octobus_rx_push(Device *self, uint16_t word);

/**
 * @brief How many words are in a card's receive FIFO.
 * @param self Device returned by octobus_create_device().
 * @return The count, 0 to OCTOBUS_RX_FIFO_WORDS.
 */
int octobus_rx_count(Device *self);

/**
 * @brief Tell the card whether a CPU station is attached to its bus.
 *
 * Decides what a frame the card sends to station 0 or to its own station does.
 * With no CPU station (the default) the frame is echoed into the card's own
 * receive FIFO, which is what TPE's stand-alone tests need. With a CPU station
 * attached and loopback mode off, the transfer completes and NOTHING is echoed
 * (NDBusOctobus.cs ProcessTransmitQueue:3239, 3288-3306).
 *
 * attached = true does what the C# AttachCpu:2219-2224 and
 * AttachMicrocodeStation:2268-2273 do to the card: loopback mode goes OFF at
 * once. attached = false only clears the flag - the C# has no detach, so there
 * is nothing to port for it; loopback mode comes back ON at the next reset
 * (Reset:3370). Call this before the card is reset, or reset the card after.
 *
 * @param self     Device returned by octobus_create_device().
 * @param attached true when a CPU station is attached, false when none is.
 */
void octobus_set_cpu_attached(Device *self, bool attached);

/**
 * @brief Install the function the card calls for control bit 5, ContinueACCP.
 *
 * Writing a control word with bit 5 set to +3 or +7 calls fn(ctx) once per
 * write. The embedding installs the ND-5000 station's "continue ACCP" here
 * (NDBusOctobus.cs ProcessControlChange:2943-2951 calls
 * _nd5000Station.ContinueAccp()).
 *
 * @param self Device returned by octobus_create_device().
 * @param fn   The function, or NULL when no ND-5000 station is attached.
 * @param ctx  Passed back to fn unchanged.
 */
void octobus_set_continue_accp(Device *self, void (*fn)(void *ctx), void *ctx);

#endif /* DEVICE_OCTOBUS_H */
