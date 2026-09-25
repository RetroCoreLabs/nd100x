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
 */

#include "device_octobus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../devices_types.h"
#include "../devices_protos.h"

// Recompute the FIFO status bits from the ring, so the two bits can never
// disagree with the count.
static void octobus_update_fifo_status(OctobusData *data)
{
    data->statusRegister.bits.fifoNotFull = (data->rxCount < OCTOBUS_RX_FIFO_WORDS) ? 1 : 0;
    data->statusRegister.bits.dataAvailable = (data->rxCount > 0) ? 1 : 0;
}

// Recompute the interrupt line from the two request flip-flops and their
// enables. Called after every event and every IDENT.
static void octobus_update_interrupt(Device *self, OctobusData *data)
{
    bool inputActive = data->inputIrqPending && data->statusRegister.bits.interruptEnabled;
    bool outputActive =
        data->outputIrqPending && data->outputStatusRegister.bits.interruptEnabled;

    dev_set_interrupt_status(self, inputActive || outputActive, self->interruptLevel);
}

static void octobus_reset(Device *self)
{
    OctobusData *data = (OctobusData *)self->deviceData;
    if (!data)
    {
        return;
    }

    data->statusRegister.raw = 0;
    data->controlWord.raw = 0;
    data->outputStatusRegister.raw = 0;
    data->outputControlWord.raw = 0;
    data->inputData = 0;
    data->outputData = 0;
    data->rxHead = 0;
    data->rxCount = 0;
    data->inputIrqPending = false;
    data->outputIrqPending = false;

    // An empty FIFO has maximum space, so fifoNotFull is SET.
    octobus_update_fifo_status(data);

    // The output controller is ready from reset: CH5CPUPRESENT spins on this bit
    // before sending a command, so a card that never sets it hangs the probe
    // rather than reporting a missing CPU.
    data->outputStatusRegister.bits.readyForTransfer = 1;

    dev_set_interrupt_status(self, false, self->interruptLevel);
}

static uint16_t octobus_tick(Device *self)
{
    if (!self)
    {
        return 0;
    }
    dev_tick_io_delay(self);
    return self->interruptBits;
}

// Push one word into the receive FIFO. Returns false when full, which is the
// card dropping the frame exactly as the hardware does.
static bool octobus_fifo_push(OctobusData *data, uint16_t word)
{
    if (data->rxCount >= OCTOBUS_RX_FIFO_WORDS)
    {
        return false;
    }
    int tail = (data->rxHead + data->rxCount) % OCTOBUS_RX_FIFO_WORDS;
    data->rxFifo[tail] = word;
    data->rxCount++;
    octobus_update_fifo_status(data);
    return true;
}

static uint16_t octobus_fifo_pop(OctobusData *data)
{
    if (data->rxCount == 0)
    {
        return 0;
    }
    uint16_t word = data->rxFifo[data->rxHead];
    data->rxHead = (data->rxHead + 1) % OCTOBUS_RX_FIFO_WORDS;
    data->rxCount--;
    octobus_update_fifo_status(data);
    return word;
}

// EVENT: a word arrived at the input controller. Latches the input request
// flip-flop; the line fires only if the input interrupt is enabled.
static void octobus_raise_input_event(Device *self, OctobusData *data)
{
    data->inputIrqPending = true;
    octobus_update_interrupt(self, data);
}

// EVENT: the output controller completed a transfer.
static void octobus_raise_output_event(Device *self, OctobusData *data)
{
    data->outputIrqPending = true;
    octobus_update_interrupt(self, data);
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
        // Pops the FIFO. An empty FIFO reads 0; status bit 3 is how software
        // tells that from a real 0.
        value = octobus_fifo_pop(data);
        break;

    case OCTOBUS_READ_INPUT_STATUS:
        // OCSTART reads this only to find out whether the card exists
        // (PH-P2-OPPSTART.NPL:4049). Reaching this code AT ALL means it does; an
        // absent card is an IOX error, which is the device manager's business.
        value = data->statusRegister.raw;
        break;

    case OCTOBUS_READ_OUTPUT_DATA:
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
        // Writing the input data register pushes into the receive FIFO. This is
        // the stand-alone loopback TPE's test 3 fills the FIFO through.
        data->inputData = value;
        if (octobus_fifo_push(data, value))
        {
            octobus_raise_input_event(self, data);
        }
        break;

    case OCTOBUS_WRITE_INPUT_CONTROL:
        data->controlWord.raw = value;

        // Bit 0: InterruptEnable from control -> status
        data->statusRegister.bits.interruptEnabled =
            data->controlWord.bits.interruptEnabled ? 1 : 0;

        // Bit 4: DeviceClear - 20 octal clears the interface
        // (PH-P2-OPPSTART.NPL:4054)
        if (data->controlWord.bits.deviceClear)
        {
            data->clears++;
            data->inputData = 0;
            data->rxHead = 0;
            data->rxCount = 0;
            data->inputIrqPending = false;
            // The FIFO bits are recomputed rather than zeroed: a cleared card has
            // space, and software polling bit 2 would otherwise see a full FIFO
            // forever.
            octobus_update_fifo_status(data);
        }

        octobus_update_interrupt(self, data);
        break;

    case OCTOBUS_WRITE_OUTPUT_COMMAND:
    {
        // CMMACLE (master clear SAMSON), CMACONT (continue ACCP) and every
        // outgoing frame arrive here. Recorded either way, so a test can see what
        // the guest sent even with no bus attached.
        // NOT copied into outputData: nothing evidences that the output data
        // register reads a sent command back, and inventing a readback would let
        // a guest "confirm" a transmission that never happened.
        data->lastCommand = value;
        data->commands++;

        // A frame addressed to station 0 is the card testing ITSELF: echo it into
        // our own receive FIFO instead of putting it on the bus. This must work
        // whether or not a bus is attached - see device_octobus.h.
        uint16_t dest = (uint16_t)((value >> 8) & 0x3F);
        if (dest == OCTOBUS_LOOPBACK_DEST)
        {
            if (octobus_fifo_push(data, value))
            {
                octobus_raise_input_event(self, data);
            }
        }
        else if (data->transmit)
        {
            // Onto the bus. The handler pushes any reply back into this card's
            // receive FIFO, which is where the hardware puts it too.
            data->transmit(data->transmitCtx, self, value);
        }

        // The send completed, however it was routed.
        octobus_raise_output_event(self, data);
        break;
    }

    case OCTOBUS_WRITE_OUTPUT_CONTROL:
        data->outputControlWord.raw = value;

        data->outputStatusRegister.bits.interruptEnabled =
            data->outputControlWord.bits.interruptEnabled ? 1 : 0;

        // OCSTART reaches +7 as "T+4" from +3 (PH-P2-OPPSTART.NPL:4055).
        if (data->outputControlWord.bits.deviceClear)
        {
            data->clears++;
            data->outputData = 0;
            data->outputIrqPending = false;
            // Ready survives the clear: OCSTART sends a command straight after.
            data->outputStatusRegister.bits.readyForTransfer = 1;
        }

        octobus_update_interrupt(self, data);
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
    if (!octobus_fifo_push(data, word))
    {
        return false;
    }
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
