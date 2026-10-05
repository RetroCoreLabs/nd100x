/*
 * device_octobus.c - ND-100 octobus interface card.
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
 *
 * Ported from RetroCore NDBusOctobus.cs
 * Register map, ident codes and probe sequences: see device_octobus.h
 *
 * Every "NDBusOctobus.cs Name:line" in this file refers to
 * $RETROCORE/Emulated.HW/ND/CPU/NDBUS/NDBusOctobus.cs.
 *
 * Every function here runs on the ND-100 thread. There are no locks.
 */

#include "device_octobus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../devices_types.h"
#include "../devices_protos.h"
#include "../../machine/mfbus_bridge.h"

// Recompute the interrupt line from the two request flip-flops and their
// enables. Called after every event and every IDENT.
//
// The OUTPUT controller also needs BUSY clear: a transfer held back by a full
// receive FIFO has not completed, so it must not interrupt yet.
// Ported from NDBusOctobus.cs UpdateInterruptState:2543-2557 (the Busy term is
// line 2552).
static void octobus_update_interrupt(Device *self, OctobusData *data)
{
    bool inputActive = data->inputIrqPending && data->statusRegister.bits.interruptEnabled;
    bool outputActive = data->outputIrqPending
                        && data->outputStatusRegister.bits.interruptEnabled
                        && !data->outputStatusRegister.bits.busy;

    dev_set_interrupt_status(self, inputActive || outputActive, self->interruptLevel);
}

// Recompute the FIFO status bits from the ring, so the two bits can never
// disagree with the count.
//
// This is also where output back-pressure is RELEASED. A loopback echo that
// found the FIFO full left the output controller BUSY with READY clear (see
// octobus_process_transmit_queue). The first update that finds space again, with
// the card in loopback mode, clears BUSY, sets READY and latches the output
// request: the blocked transfer has now completed, and that is an event.
// Ported from NDBusOctobus.cs UpdateReceiveFifoStatus:2606-2644 (release at
// 2622-2629).
//
// Reset and the two clears do NOT come through here: the C# assigns the status
// word directly there (2844, 2915, 3357), so a clear of the input side does not
// by itself release a BUSY output.
static void octobus_update_fifo_status(Device *self, OctobusData *data)
{
    if (data->rxCount >= OCTOBUS_RX_FIFO_WORDS)
    {
        data->statusRegister.bits.fifoNotFull = 0;
    }
    else
    {
        data->statusRegister.bits.fifoNotFull = 1;

        if (data->loopbackMode && data->outputStatusRegister.bits.busy)
        {
            data->outputStatusRegister.bits.busy = 0;
            data->outputStatusRegister.bits.readyForTransfer = 1;
            data->outputIrqPending = true;
        }
    }

    data->statusRegister.bits.dataAvailable = (data->rxCount > 0) ? 1 : 0;

    octobus_update_interrupt(self, data);
}

static void octobus_reset(Device *self)
{
    OctobusData *data = (OctobusData *)self->deviceData;
    if (!data)
    {
        return;
    }

    // Ported from NDBusOctobus.cs Reset:3354-3394, statement by statement.

    // Input status is FIFO-NOT-FULL and nothing else (3357): an empty FIFO has
    // maximum space. The station field is left 0 here, as in the C#; a read of
    // +2 puts the card's own station into it (octobus_read).
    data->statusRegister.raw = 0;
    data->statusRegister.bits.fifoNotFull = 1;
    data->controlWord.raw = 0;
    data->inputData = 0;

    // The output controller is ready from reset (3363): CH5CPUPRESENT spins on
    // this bit before sending a command, so a card that never sets it hangs the
    // probe rather than reporting a missing CPU.
    data->outputStatusRegister.raw = 0;
    data->outputStatusRegister.bits.readyForTransfer = 1;
    data->outputControlWord.raw = 0;
    data->outputData = 0;

    // Loopback is ON after reset exactly when no CPU station is attached (3370).
    // The attached flag itself survives the reset, as _nd500Cpu does in the C#.
    data->loopbackMode = !data->cpuAttached;

    data->rxHead = 0;
    data->rxCount = 0;
    // Frames parked behind a full FIFO belong to the state being reset, so they
    // go too (3376). Without this a read after the reset pulls an old frame in.
    data->busyRetryHead = 0;
    data->busyRetryCount = 0;

    data->inputIrqPending = false;
    data->outputIrqPending = false;

    // Our own station: the ND-100 is always 1B (T329) (3386).
    data->stationAddress = OCTOBUS_ND100_STATION;

    dev_set_interrupt_status(self, false, self->interruptLevel);
}

static uint16_t octobus_tick(Device *self)
{
    if (!self)
    {
        return 0;
    }
    dev_tick_io_delay(self);

    // LEVEL-SENSITIVE RE-ASSERT. devmgr_ident() force-clears this device's level
    // bit after any successful IDENT, which is right for a device with one
    // request - but this card has TWO, and the second must still be served.
    //
    // The input controller has priority, so a write that raises both events is
    // IDENTed as 40B and leaves the output request pending. Without re-asserting
    // here the line stays down, the CPU never takes level 13 again, and TPE
    // reports "Found identcodes : 40B and 0B" - it collected the input ident and
    // never saw the output one.
    //
    // Recomputing from the request flip-flops every tick is what makes the line
    // level-sensitive rather than edge-triggered, which is what the hardware is.
    OctobusData *data = (OctobusData *)self->deviceData;
    if (data)
    {
        octobus_update_interrupt(self, data);
    }

    // THE MAILBOX POLL. Past ENKICK the octobus carries no more commands: the
    // monitor waits on the mailbox in MPM-5 shared memory instead, and SINTRAN's
    // ACT51 rings that doorbell without sending any kick. So the ND-5000 side has
    // to poll, and the ND-100 clock reaching this tick is where that poll gets its
    // turn. Without it the monitor waits out its watchdog and reports
    // "ND-500(0) timeout". See mfbus_service_nd5000_mailboxes().
    // Guarded exactly like the header it calls into: the mfbus bridge and every
    // ND-5000 station live behind ND100X_WITH_ND500, so a build without the ND-500
    // side has no mailbox to poll.
#ifdef ND100X_WITH_ND500
    (void)mfbus_service_nd5000_mailboxes();
#endif

    return self->interruptBits;
}

// Push one word into the receive FIFO. Returns false when full. What a caller
// does with that is the caller's decision: a frame that arrived over the bus is
// PARKED for retry (octobus_rx_push), while a loopback echo of the guest's own
// frame is refused and the output controller goes BUSY
// (octobus_process_transmit_queue).
static bool octobus_fifo_push(Device *self, OctobusData *data, uint16_t word)
{
    if (data->rxCount >= OCTOBUS_RX_FIFO_WORDS)
    {
        return false;
    }
    int tail = (data->rxHead + data->rxCount) % OCTOBUS_RX_FIFO_WORDS;
    data->rxFifo[tail] = word;
    data->rxCount++;
    octobus_update_fifo_status(self, data);
    return true;
}

// Pop one word. The caller checks rxCount first; an empty FIFO is not popped
// (see OCTOBUS_READ_INPUT_DATA in octobus_read).
static uint16_t octobus_fifo_pop(Device *self, OctobusData *data)
{
    if (data->rxCount == 0)
    {
        return 0;
    }
    uint16_t word = data->rxFifo[data->rxHead];
    data->rxHead = (data->rxHead + 1) % OCTOBUS_RX_FIFO_WORDS;
    data->rxCount--;
    octobus_update_fifo_status(self, data);
    return word;
}

// EVENT: a word arrived at the input controller. Latches the input request
// flip-flop; the line fires only if the input interrupt is enabled.
static void octobus_raise_input_event(Device *self, OctobusData *data)
{
    data->inputIrqPending = true;
    octobus_update_interrupt(self, data);
}

/*
 * Move frames parked by the busy-retry model into the receive FIFO while there
 * is space. Each frame that lands updates the input data register and latches
 * an input event, exactly as if the sender's hardware retry had just succeeded.
 *
 * Called from the one place that frees a slot: the guest popping the input data
 * register. Ported from RetroCore NDBusOctobus.cs PumpBusyRetryQueue, which is
 * called from the same place (its Read case Register.InputReadData).
 */
static void octobus_pump_busy_retry(Device *self, OctobusData *data)
{
    while (data->busyRetryCount > 0 && data->rxCount < OCTOBUS_RX_FIFO_WORDS)
    {
        uint16_t frame = data->busyRetry[data->busyRetryHead];
        data->busyRetryHead = (data->busyRetryHead + 1) % OCTOBUS_BUSY_RETRY_WORDS;
        data->busyRetryCount--;

        (void)octobus_fifo_push(self, data, frame);
        data->inputData = frame;
        // A receive-format frame carries its source station in bits 13-8, and
        // that goes into the stored input status (PumpBusyRetryQueue:3485-3487).
        data->statusRegister.bits.station = (uint16_t)((frame >> 8) & 0x3F);
        octobus_raise_input_event(self, data);
    }
}

// EVENT: the output controller completed a transfer.
static void octobus_raise_output_event(Device *self, OctobusData *data)
{
    data->outputIrqPending = true;
    octobus_update_interrupt(self, data);
}

// Complete one transfer that stays on the card: a frame to station 0 or to the
// card's own station, or any frame on a card with no bus attached.
// Ported from NDBusOctobus.cs ProcessTransmitQueue:3233-3307.
//
// `echo` says whether the frame is echoed into our own input side, as the
// hardware's local loopback does. The caller decides it (see the write to +5).
//
// The echoed frame carries the SOURCE station in bits 13:8 - the hardware
// stamps the sender's own station onto every received frame (manual ch. 3.2;
// carve Q2 step 10 decodes +0 bits 13:8 as the source and compares it against
// the +2 own-station field). For a self-loopback the source is THIS card, so
// C and B (bits 15,14) and the information byte (7:0) are kept and the station
// field is replaced. TPE's self-send cross-check requires the +0 source to equal
// the +2 own-station field, so both must carry stationAddress.
static void octobus_process_transmit_queue(Device *self, OctobusData *data, bool echo)
{
    if (echo)
    {
        // The source of a looped-back frame is this card (3244-3245).
        data->statusRegister.bits.station = (uint16_t)(data->stationAddress & 0x3F);

        // The frame echoed is the output data register, which the write to +5
        // has just loaded (3256-3257).
        uint16_t frame = (uint16_t)((data->outputData & 0xC0FF)
                                    | ((uint16_t)(data->stationAddress & 0x3F) << 8));

        if (!octobus_fifo_push(self, data, frame))
        {
            // FIFO full: back-pressure. The output goes BUSY with
            // ready-for-transfer CLEAR and the transfer does NOT complete, so the
            // test sees the FIFO is full. The frame itself is not kept anywhere:
            // the C# does not retry it either. BUSY is released by the next FIFO
            // status update that finds space (3263-3276).
            data->outputStatusRegister.bits.busy = 1;
            data->outputStatusRegister.bits.readyForTransfer = 0;
            octobus_update_fifo_status(self, data);
            return;
        }

        data->inputData = frame; // 3261
        octobus_raise_input_event(self, data);
    }

    // With a CPU station attached and loopback off there is no echo: the C#
    // branch for that case (3288-3296) only empties its transmit queue. Either
    // way the transfer is complete: BUSY clear, READY set, output event
    // (3301-3306).
    data->outputStatusRegister.bits.busy = 0;
    data->outputStatusRegister.bits.readyForTransfer = 1;
    octobus_raise_output_event(self, data);
}

// Clear ONE controller. The C# does this in two places with the same
// statements: for control bit 6 Reset (ProcessControlChange:2842-2858) and for
// control bit 4, the 20 octal clear (2913-2931).
//
// Both REPLACE THE WHOLE STATUS WORD. TPE OCTOBUS B00 test 4 ("Check Octobus
// configuration") reads +6 straight after the clear and demands exactly 000010B
// - READY and nothing else. Setting only READY leaves the ERROR (bit 4) and NOT
// PRESENT (bit 6) of the preceding probe standing, and the test reports "Wrong
// transmit status after Clear Device ... Found 000130B". Ready is SET rather
// than cleared because OCSTART sends a command straight after the clear.
static void octobus_clear_controller(OctobusData *data, bool isInput)
{
    if (isInput)
    {
        // FIFO-NOT-FULL and nothing else (2844, 2915): a cleared card has space,
        // and software polling bit 2 would otherwise see a full FIFO forever.
        data->statusRegister.raw = 0;
        data->statusRegister.bits.fifoNotFull = 1;
        data->inputData = 0;
        data->rxHead = 0;
        data->rxCount = 0;
        // Parked retry frames die with the clear (2848, 2919): they belong to a
        // transfer the guest has just abandoned.
        data->busyRetryHead = 0;
        data->busyRetryCount = 0;
        data->inputIrqPending = false;
    }
    else
    {
        data->outputStatusRegister.raw = 0;
        data->outputStatusRegister.bits.readyForTransfer = 1; // 2854, 2928
        data->outputData = 0;
        data->outputIrqPending = false;
    }
}

// Act on a control word written to +3 (input) or +7 (output).
// Ported from NDBusOctobus.cs ProcessControlChange:2837-2952. THE ORDER OF THE
// STEPS IS THE C# ORDER and it decides what a word with several bits set does:
//
//   1. bit 6 Reset        clears this controller                  (2840-2865)
//   2. bit 7 TestMode     turns loopback mode ON                  (2868-2874)
//   3. bit 0              copied to the status interrupt enable   (2879-2904)
//   4. bit 4 (20 octal)   clears this controller                  (2911-2937)
//   5. bit 5 ContinueACCP calls the installed function            (2943-2951)
//
// Step 4 comes AFTER step 3 and replaces the whole status word, so 21 octal
// (enable + clear) ends with the interrupt enable CLEAR, on both controllers.
// 101 octal (enable + Reset) ends with it SET, because step 1 comes before
// step 3.
//
// Both control words have the same layout - the C# decodes both with the one
// ControlWordBits enum (1063) - so OctobusInputControl is used to decode either.
static void octobus_process_control_change(Device *self, OctobusData *data, uint16_t control,
                                           bool isInput)
{
    OctobusInputControl cw;
    cw.raw = control;

    if (cw.bits.reset)
    {
        octobus_clear_controller(data, isInput);
        octobus_update_interrupt(self, data);
    }

    if (cw.bits.testMode)
    {
        data->loopbackMode = true;
    }

    if (isInput)
    {
        data->statusRegister.bits.interruptEnabled = cw.bits.interruptEnabled ? 1 : 0;
    }
    else
    {
        data->outputStatusRegister.bits.interruptEnabled = cw.bits.interruptEnabled ? 1 : 0;
    }
    octobus_update_interrupt(self, data);

    // 20 octal clears the interface: PH-P2-OPPSTART.NPL:4054 writes it to +3 and
    // :4055 reaches +7 as "T+4".
    if (cw.bits.deviceClear)
    {
        data->clears++;
        octobus_clear_controller(data, isInput);
        octobus_update_interrupt(self, data);
    }

    // The card cannot reach the ND-5000 station, so the embedding installs the
    // call (octobus_set_continue_accp). No function installed is the C#
    // "_nd5000Station == null" case: the flag is still set, nothing is called.
    if (cw.bits.continueAccp)
    {
        data->mudomDetected = true;
        if (data->continueAccp)
        {
            data->continueAccp(data->continueAccpCtx);
        }
    }
}

static uint16_t octobus_read(Device *self, uint32_t address)
{
    if (!self)
    {
        return 0;
    }

    OctobusData *data = (OctobusData *)self->deviceData;
    uint16_t value = 0;
    uint32_t reg = dev_register_address(self, address);


    switch (reg)
    {
    case OCTOBUS_READ_INPUT_DATA:
        // Ported from NDBusOctobus.cs Read:2673-2708.
        if (data->rxCount > 0)
        {
            // Pops the FIFO, and the word read stays in the input data register
            // (2678-2679).
            value = octobus_fifo_pop(self, data);
            data->inputData = value;

            // The freed slot lets a busy-retried frame land - the sender's
            // hardware retry after Ack=10 finally succeeding (2686).
            octobus_pump_busy_retry(self, data);

            // While the FIFO still holds unread data the input controller keeps
            // requesting the interrupt. IDENT clears the request flip-flop once,
            // so a reply whose frames all arrived before the first interrupt was
            // served would otherwise give ONE interrupt: a driver that reads one
            // frame per interrupt (TPE's receive routine) reads the first frame
            // and loses the rest - "No answer from Octobus station 10". Latching
            // the request again here gives one interrupt per frame (2700-2701).
            if (data->rxCount > 0)
            {
                octobus_raise_input_event(self, data);
            }
        }
        else
        {
            // FIFO empty: the input data register is read as it stands - the
            // last word popped, or whatever was loaded into it since
            // (2703-2707). Status bit 3 is how software tells this from data.
            value = data->inputData;
        }
        break;

    case OCTOBUS_READ_INPUT_STATUS:
    {
        // OCSTART reads this only to find out whether the card exists
        // (PH-P2-OPPSTART.NPL:4049). Reaching this code AT ALL means it does; an
        // absent card is an IOX error, which is the device manager's business.
        //
        // Bits 13:8 READ as the card's OWN station, whatever source station is
        // stored there: TPE reads its station number here and compares it with
        // the source of a self-loopback frame, which is in +0. The stored field
        // is not changed by the read (NDBusOctobus.cs Read:2714-2729).
        OctobusInputStatus st = data->statusRegister;
        st.bits.station = (uint16_t)(data->stationAddress & 0x3F);
        value = st.raw;
        break;
    }

    case OCTOBUS_READ_OUTPUT_DATA:
        // The last word written to +5 (NDBusOctobus.cs Read:2735-2738; loaded
        // at ProcessCommand:2976).
        value = data->outputData;
        break;

    case OCTOBUS_READ_OUTPUT_STATUS:
        value = data->outputStatusRegister.raw;
        break;

    default:
        // An odd address is a WRITE register; reading one is a guest bug.
        break;
    }

    LOG(LOG_CAT_DEVICE, LOG_TRACE, "Octobus: READ  +%u = %06o (rx=%d ist=%06o ost=%06o)\n",
        (unsigned)reg, (unsigned)value, data->rxCount, (unsigned)data->statusRegister.raw,
        (unsigned)data->outputStatusRegister.raw);
    return value;
}

static void octobus_write(Device *self, uint32_t address, uint16_t value)
{
    if (!self)
    {
        return;
    }

    OctobusData *data = (OctobusData *)self->deviceData;
    uint32_t reg = dev_register_address(self, address);

    switch (reg)
    {
    case OCTOBUS_WRITE_INPUT_DATA:
        // Stores the word in the input data register and does nothing else: no
        // FIFO entry, no event (NDBusOctobus.cs Write:2790-2793). The receive
        // FIFO is filled through +5 in loopback, which is how TPE's test 3 does
        // it.
        data->inputData = value;
        break;

    case OCTOBUS_WRITE_INPUT_CONTROL:
        // NDBusOctobus.cs Write:2795-2802.
        data->controlWord.raw = value;
        octobus_process_control_change(self, data, value, true);
        break;

    case OCTOBUS_WRITE_OUTPUT_COMMAND:
    {
        // CMMACLE (master clear SAMSON), CMACONT (continue ACCP) and every
        // outgoing frame arrive here. Recorded either way, so a test can see what
        // the guest sent even with no bus attached.
        data->lastCommand = value;
        data->commands++;

        // The word is stored in the output data register FIRST, and a read of
        // +4 returns it. The loopback echo below takes the frame from this
        // register (NDBusOctobus.cs ProcessCommand:2976).
        data->outputData = value;

        // ---- ProcessTransmitFrame:3064-3192 ----

        // ERROR and NOT PRESENT describe the LAST transfer only, so they are
        // cleared here, before delivery is attempted, and set again below only if
        // nothing answers. A discovery scan reads them once per frame (3101).
        data->outputStatusRegister.bits.error = 0;
        data->outputStatusRegister.bits.notPresent = 0;

        uint16_t dest = (uint16_t)((value >> 8) & 0x3F);
        if (OCTOBUS_IS_SELF_LOOP(dest, data->stationAddress))
        {
            // A frame to destination 0, or to our own station, stays on the card
            // and is decided BEFORE any bus routing - see device_octobus.h for
            // why the order matters (3113-3125).
            //
            // It is echoed into our own receive FIFO while the card is in
            // loopback mode or no CPU station is attached. With a CPU station
            // attached and loopback off, the transfer completes with NO echo
            // (ProcessTransmitQueue:3239, 3288-3306).
            octobus_process_transmit_queue(self, data,
                                           data->loopbackMode || !data->cpuAttached);
        }
        else if (!data->transmit)
        {
            // NO C# COUNTERPART: the C# card always has a bus object, and a frame
            // to a station nobody registered gets ERROR + NOT PRESENT and no echo
            // (3179-3191). A card here with no transmit function is stand-alone
            // and echoes every frame, as it did before this port.
            octobus_process_transmit_queue(self, data, true);
        }
        else
        {
            // Onto the bus. The handler pushes any reply back into this card's
            // receive FIFO, which is where the hardware puts it too, and reports
            // whether any station acknowledged the frame at all.
            if (!data->transmit(data->transmitCtx, self, value))
            {
                // Nothing at that station: Ack=00 after the hardware retries. The
                // transfer attempt itself still completes - ready-for-transfer is
                // set and the output event is raised below, exactly as for a
                // delivered frame - but the status now says why nothing arrived
                // (3189).
                data->outputStatusRegister.bits.error = 1;
                data->outputStatusRegister.bits.notPresent = 1;
            }
        }

        // ---- back in ProcessCommand:2993-3021 ----

        // Bring the FIFO bits in line with the ring (2993).
        octobus_update_fifo_status(self, data);

        // A stored source station of 0 means no frame has set one yet, and it is
        // then set to our own station (2997-3001). Not visible to the guest: a
        // read of +2 always shows the own station.
        if (data->statusRegister.bits.station == 0)
        {
            data->statusRegister.bits.station = (uint16_t)(data->stationAddress & 0x3F);
        }

        // The written word goes into the input data register ONLY while that
        // register still holds 0 - that is, when no echo or reply has loaded it
        // (3004-3007). A read of +0 with the FIFO empty returns it.
        if (data->inputData == 0)
        {
            data->inputData = value;
        }

        // Transmission complete: set ready and raise the output event - unless
        // the loopback echo found the FIFO full. Then the output is BUSY, the
        // transfer has NOT completed, and the event comes later, when the FIFO
        // has space again (3017-3021).
        if (!data->outputStatusRegister.bits.busy)
        {
            data->outputStatusRegister.bits.readyForTransfer = 1;
            octobus_raise_output_event(self, data);
        }
        break;
    }

    case OCTOBUS_WRITE_OUTPUT_CONTROL:
        // OCSTART reaches +7 as "T+4" from +3 (PH-P2-OPPSTART.NPL:4055).
        // NDBusOctobus.cs Write:2811-2822.
        data->outputControlWord.raw = value;
        octobus_process_control_change(self, data, value, false);
        break;

    default:
        // An even address is a READ register; writing one is a guest bug.
        break;
    }

    LOG(LOG_CAT_DEVICE, LOG_TRACE,
        "Octobus: WRITE +%u = %06o (rx=%d ist=%06o ost=%06o irq=%d/%d)\n", (unsigned)reg,
        (unsigned)value, data->rxCount, (unsigned)data->statusRegister.raw,
        (unsigned)data->outputStatusRegister.raw, data->inputIrqPending,
        data->outputIrqPending);
}

static uint16_t octobus_ident(Device *self, uint16_t level)
{
    if (!self)
    {
        return 0;
    }

    LOG(LOG_CAT_DEVICE, LOG_TRACE, "Octobus: IDENT level %u (bits=%06o)\n", (unsigned)level,
        (unsigned)self->interruptBits);

    if ((self->interruptBits & (1 << level)) != 0)
    {
        OctobusData *data = (OctobusData *)self->deviceData;

        // The card has TWO ident codes: the receive controller answers identCode
        // and the transmit controller identCode + 1 (40B and 41B on interface 0).
        // The INPUT has priority.
        //
        // IDENT is a one-shot acknowledge: it clears the request flip-flop AND
        // the enable, so the driver must re-arm. Without clearing the enable the
        // level re-fires forever after being serviced.
        uint16_t activeIdent = 0;

        if (data->inputIrqPending && data->statusRegister.bits.interruptEnabled)
        {
            data->inputIrqPending = false;
            data->statusRegister.bits.interruptEnabled = 0;
            activeIdent = self->identCode;
        }
        else if (data->outputIrqPending && data->outputStatusRegister.bits.interruptEnabled)
        {
            data->outputIrqPending = false;
            data->outputStatusRegister.bits.interruptEnabled = 0;
            activeIdent = (uint16_t)(self->identCode + 1);
        }

        octobus_update_interrupt(self, data);
        LOG(LOG_CAT_DEVICE, LOG_TRACE, "Octobus: IDENT answered %02o\n", (unsigned)activeIdent);
        return activeIdent;
    }
    return 0;
}

static void octobus_destroy(Device *self)
{
    if (!self)
    {
        return;
    }
    free(self->deviceData);
    self->deviceData = NULL;
}

void octobus_set_transmit(Device *self, OctobusTransmitFn fn, void *ctx)
{
    if (!self || !self->deviceData)
    {
        return;
    }
    OctobusData *data = (OctobusData *)self->deviceData;
    data->transmit = fn;
    data->transmitCtx = ctx;
}

bool octobus_rx_push(Device *self, uint16_t word)
{
    if (!self || !self->deviceData)
    {
        return false;
    }
    OctobusData *data = (OctobusData *)self->deviceData;

    /*
     * FIFO FULL IS NOT A LOST FRAME. The real receiver answers Ack=10
     * (destination busy) and the sender's hardware retries until the frame is
     * taken, so a reply longer than the 16-word FIFO still arrives whole. Park
     * the frame in arrival order - and park it behind any frames already
     * waiting, or the reply would be reordered - and let the guest's next read
     * pull it in. No event and no register update yet: the frame has not landed.
     *
     * Dropping these instead truncated every reply over 16 frames. The ND-500
     * monitor's RECO (020B) asks for 16 words, a 36-frame reply, and read a
     * mutilated message: it answered with an emergency 244B TERMINATE ACCP and
     * printed "ECO not available".
     */
    if (data->busyRetryCount > 0 || data->rxCount >= OCTOBUS_RX_FIFO_WORDS)
    {
        if (data->busyRetryCount >= OCTOBUS_BUSY_RETRY_WORDS)
        {
            data->busyRetryDropped++;
            return false;
        }
        int tail = (data->busyRetryHead + data->busyRetryCount) % OCTOBUS_BUSY_RETRY_WORDS;
        data->busyRetry[tail] = word;
        data->busyRetryCount++;
        data->busyRetryParked++;
        return true;
    }

    // The frame has landed: it is in the FIFO, it is in the input data register,
    // and its source station is in the stored input status
    // (NDBusOctobus.cs DeliverInboundFrame:3608-3622).
    //
    // The C# is handed the source station as a second argument. Here it is taken
    // from bits 13-8 of the frame, which is where a receive-format frame carries
    // it (DeliverInboundFrame:3590; OctobusFabric.cs SendFrame:185 builds the
    // delivered frame from that same number) and where the C# itself takes it
    // from for a parked frame (PumpBusyRetryQueue:3485-3487).
    (void)octobus_fifo_push(self, data, word);
    data->inputData = word;
    data->statusRegister.bits.station = (uint16_t)((word >> 8) & 0x3F);
    octobus_raise_input_event(self, data);
    return true;
}

int octobus_rx_count(Device *self)
{
    if (!self || !self->deviceData)
    {
        return 0;
    }
    return ((OctobusData *)self->deviceData)->rxCount;
}

void octobus_set_cpu_attached(Device *self, bool attached)
{
    if (!self || !self->deviceData)
    {
        return;
    }
    OctobusData *data = (OctobusData *)self->deviceData;

    data->cpuAttached = attached;
    if (attached)
    {
        // What attaching a CPU station does to the card itself
        // (NDBusOctobus.cs AttachCpu:2219-2224, AttachMicrocodeStation:2268-2273):
        // frames to station 0 and to the own station are no longer echoed.
        data->mudomDetected = true;
        data->loopbackMode = false;
    }
    // attached == false: the C# has no detach, so only the flag is cleared.
    // Loopback mode is recomputed from the flag at the next reset (Reset:3370).
}

void octobus_set_continue_accp(Device *self, void (*fn)(void *ctx), void *ctx)
{
    if (!self || !self->deviceData)
    {
        return;
    }
    OctobusData *data = (OctobusData *)self->deviceData;
    data->continueAccp = fn;
    data->continueAccpCtx = ctx;
}

Device *octobus_create_device(uint8_t thumbwheel)
{
    Device *dev = malloc(sizeof(Device));
    if (!dev)
    {
        return NULL;
    }

    OctobusData *data = malloc(sizeof(OctobusData));
    if (!data)
    {
        free(dev);
        return NULL;
    }

    // Initialize device base structure
    dev_init(dev, thumbwheel, DEVICE_CLASS_STANDARD, 0);

    // Set up device-specific data
    memset(data, 0, sizeof(OctobusData));

    // Set up device properties based on thumbwheel. Interfaces are 010 octal
    // apart and the receive ident advances by 2 per interface; the transmit ident
    // is the receive one plus 1.
    switch (thumbwheel)
    {
    case 0:
        dev->identCode = 040;
        dev->startAddress = 0100400;
        dev->endAddress = 0100407;
        dev->interruptLevel = 13;
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "Octobus 1");
        break;
    case 1:
        dev->identCode = 042;
        dev->startAddress = 0100410;
        dev->endAddress = 0100417;
        dev->interruptLevel = 13;
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "Octobus 2");
        break;
    case 2:
        dev->identCode = 044;
        dev->startAddress = 0100420;
        dev->endAddress = 0100427;
        dev->interruptLevel = 13;
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "Octobus 3");
        break;
    case 3:
        dev->identCode = 046;
        dev->startAddress = 0100430;
        dev->endAddress = 0100437;
        dev->interruptLevel = 13;
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "Octobus 4");
        break;
    default:
        LOG(LOG_CAT_DEVICE, LOG_WARN, "Unexpected thumbwheel code %d\n", thumbwheel);
        free(data);
        free(dev->ioDelays); /* allocated by dev_init() above */
        free(dev);
        return NULL;
    }

    // Set up device function pointers
    dev->Reset = octobus_reset;
    dev->Tick = octobus_tick;
    dev->Read = octobus_read;
    dev->Write = octobus_write;
    dev->Ident = octobus_ident;
    dev->Destroy = octobus_destroy;
    dev->deviceData = data;

    octobus_reset(dev);

    LOG(LOG_CAT_DEVICE, LOG_INFO,
        "Octobus device created: %s at %o, ident %02o/%02o level %d\n", dev->memoryName,
        (unsigned)dev->startAddress, (unsigned)dev->identCode, (unsigned)(dev->identCode + 1),
        dev->interruptLevel);
    return dev;
}
