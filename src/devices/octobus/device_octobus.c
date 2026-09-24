/*
 * device_octobus.c - ND-100 octobus interface card
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
 * WHAT THIS DOES AND DOES NOT DO YET.
 *
 * The register map, the addresses, the ident codes and the two probe sequences
 * SINTRAN uses are all evidenced - see device_octobus.h for where each number
 * comes from. What is implemented here is enough for those PROBES to give the
 * right answers: the card is present, it clears, and its output status reports
 * data ready.
 *
 * What is NOT implemented is the frame path: nothing is handed to an octobus
 * fabric and no ND-5000 is reached. That wiring is a separate step, and until it
 * exists a read of the data registers returns 0 rather than a made-up frame.
 * A card that answers a probe correctly and then invents traffic is worse than
 * one that answers the probe and stays quiet - the first produces a machine that
 * looks like it is working.
 */

#include "device_octobus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../devices_protos.h"
#include "../devices_types.h"

typedef struct
{
    /*
     * The receive FIFO: 16 words, which TPE's test 3 checks by counting how many
     * it can write before input status bit 2 clears. A ring rather than a shift
     * buffer so a drain is not quadratic - TPE drains the whole FIFO in a loop.
     */
    uint16_t rx_fifo[OCTOBUS_RX_FIFO_WORDS];
    int      rx_head;  /* next word to read */
    int      rx_count; /* words currently held */

    /* The card's own registers. Reads of a read register and writes of a write
     * register both land here, so a test can see what SINTRAN did. */
    uint16_t input_data;
    uint16_t input_status;
    uint16_t input_control;
    uint16_t output_data;
    uint16_t output_command;
    uint16_t output_status;
    uint16_t output_control;

    /* Diagnostics: what the probes did, so a failure names the step. */
    unsigned long clears;      /* control writes carrying 20 octal */
    unsigned long commands;    /* writes to the output command register */
    uint16_t      last_command;
} OctobusData;

/* Recompute the input status bits that describe the FIFO. Called after every
 * push, pop and clear, so the two bits can never disagree with the count. */
static void octobus_update_input_status(OctobusData *d)
{
    if (d->rx_count < OCTOBUS_RX_FIFO_WORDS)
    {
        d->input_status |= (uint16_t)OCTOBUS_IN_STATUS_FIFO_NOT_FULL;
    }
    else
    {
        d->input_status &= (uint16_t)~OCTOBUS_IN_STATUS_FIFO_NOT_FULL;
    }

    if (d->rx_count > 0)
    {
        d->input_status |= (uint16_t)OCTOBUS_IN_STATUS_DATA_AVAIL;
    }
    else
    {
        d->input_status &= (uint16_t)~OCTOBUS_IN_STATUS_DATA_AVAIL;
    }
}

static bool octobus_rx_push_data(OctobusData *d, uint16_t word)
{
    if (d->rx_count >= OCTOBUS_RX_FIFO_WORDS)
    {
        /* Full: the word is DROPPED, which is what the hardware does. Reporting
         * it lets a caller count losses instead of silently believing the frame
         * arrived. */
        return false;
    }
    int tail = (d->rx_head + d->rx_count) % OCTOBUS_RX_FIFO_WORDS;
    d->rx_fifo[tail] = word;
    d->rx_count++;
    octobus_update_input_status(d);
    return true;
}

static uint16_t octobus_rx_pop_data(OctobusData *d)
{
    if (d->rx_count == 0)
    {
        return 0;
    }
    uint16_t word = d->rx_fifo[d->rx_head];
    d->rx_head = (d->rx_head + 1) % OCTOBUS_RX_FIFO_WORDS;
    d->rx_count--;
    octobus_update_input_status(d);
    return word;
}

bool octobus_rx_push(Device *self, uint16_t word)
{
    if (!self || !self->deviceData)
    {
        return false;
    }
    return octobus_rx_push_data((OctobusData *)self->deviceData, word);
}

int octobus_rx_count(Device *self)
{
    if (!self || !self->deviceData)
    {
        return 0;
    }
    return ((OctobusData *)self->deviceData)->rx_count;
}

static void octobus_reset(Device *self)
{
    if (!self || !self->deviceData)
    {
        return;
    }
    OctobusData *d = (OctobusData *)self->deviceData;
    memset(d, 0, sizeof(*d));

    /* An empty FIFO has maximum space, so FIFO-not-full is SET and
     * data-available is CLEAR. */
    octobus_update_input_status(d);

    /* Data ready from the start. CH5CPUPRESENT spins on output status bit 3
     * before it sends a command (PH-P2-OPPSTART.NPL:3923), so a card that never
     * sets it HANGS the probe rather than reporting a missing CPU. */
    d->output_status = OCTOBUS_STATUS_DATA_READY;
}

static uint16_t octobus_read(Device *self, uint32_t address)
{
    if (!self || !self->deviceData)
    {
        return 0;
    }
    OctobusData *d = (OctobusData *)self->deviceData;
    uint32_t reg = address - self->startAddress;

    switch (reg)
    {
    case OCTOBUS_REG_IN_READ_DATA:
        /* Pops the receive FIFO. An empty FIFO reads 0, and input status bit 3
         * is how software knows the difference between that and a real 0. */
        return octobus_rx_pop_data(d);

    case OCTOBUS_REG_IN_READ_STATUS:
        /* OCSTART reads this purely to find out whether the card exists:
         * "T:=HDEV+2; *IOXT". Reaching this code AT ALL means it does - an
         * absent card is an IOX error, which is the device manager's business,
         * not a value returned from here. */
        return d->input_status;

    case OCTOBUS_REG_OUT_READ_DATA:
        return d->output_data;

    case OCTOBUS_REG_OUT_READ_STATUS:
        /* Bit 3 is data ready. */
        return d->output_status;

    default:
        /* An odd address is a WRITE register. Reading one is a guest bug, not a
         * card feature, so it reads 0 and says so once. */
        LOG(LOG_CAT_DEVICE, LOG_DEBUG, "Octobus: read of write-only register +%u\n",
            (unsigned)reg);
        return 0;
    }
}

static void octobus_write(Device *self, uint32_t address, uint16_t value)
{
    if (!self || !self->deviceData)
    {
        return;
    }
    OctobusData *d = (OctobusData *)self->deviceData;
    uint32_t reg = address - self->startAddress;

    switch (reg)
    {
    case OCTOBUS_REG_IN_WRITE_DATA:
        /* Writing the input data register pushes into the receive FIFO. This is
         * the standalone loopback TPE uses: its test 3 fills the FIFO this way
         * and counts the writes that fit. */
        d->input_data = value;
        (void)octobus_rx_push_data(d, value);
        break;

    case OCTOBUS_REG_IN_WRITE_CTRL:
        d->input_control = value;
        if (value == OCTOBUS_CTRL_CLEAR)
        {
            /* "T:=HDEV+DCONT; 20; *IOXT" - clear the input interface. */
            d->clears++;
            d->input_data = 0;
            d->input_status = 0;
            /* Clearing empties the FIFO, so the status bits are recomputed
             * rather than left at 0 - a cleared card has space, and software
             * that polls bit 2 would otherwise see a full FIFO forever. */
            d->rx_head = 0;
            d->rx_count = 0;
            octobus_update_input_status(d);
        }
        break;

    case OCTOBUS_REG_OUT_WRITE_CMD:
        /* CMMACLE (master clear SAMSON), CMACONT (continue ACCP) and the rest
         * arrive here. Recorded, not acted on: acting on a master clear without
         * an ND-5000 attached would be inventing a machine. */
        d->output_command = value;
        d->last_command = value;
        d->commands++;
        break;

    case OCTOBUS_REG_OUT_WRITE_CTRL:
        d->output_control = value;
        if (value == OCTOBUS_CTRL_CLEAR)
        {
            d->clears++;
            d->output_data = 0;
            /* Data ready survives a clear: the card is still able to take a
             * command afterwards, which is what OCSTART goes on to do. */
            d->output_status = OCTOBUS_STATUS_DATA_READY;
        }
        break;

    default:
        LOG(LOG_CAT_DEVICE, LOG_DEBUG, "Octobus: write to read-only register +%u\n",
            (unsigned)reg);
        break;
    }
}

static uint16_t octobus_ident(Device *self, uint16_t level)
{
    if (!self || level != OCTOBUS_INT_LEVEL)
    {
        return 0;
    }
    /* The card has TWO ident codes - receive and transmit - and this hook
     * returns one. The receive code is returned because nothing here raises a
     * transmit interrupt yet; when the frame path exists, whichever controller
     * interrupted decides. */
    return self->identCode;
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

Device *octobus_create_device(uint8_t thumbwheel)
{
    if (thumbwheel >= OCTOBUS_MAX_CARDS)
    {
        LOG(LOG_CAT_DEVICE, LOG_WARN,
            "Octobus: thumbwheel %u out of range - the hardware catalogue lists %d interfaces\n",
            (unsigned)thumbwheel, OCTOBUS_MAX_CARDS);
        return NULL;
    }

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

    dev_init(dev, thumbwheel, DEVICE_CLASS_STANDARD, 0);
    memset(data, 0, sizeof(*data));

    /* Interfaces are 010 octal apart: 100400, 100410, 100420, 100430. */
    dev->startAddress = OCTOBUS_BASE_ADDRESS + ((uint32_t)thumbwheel * OCTOBUS_REGISTERS);
    dev->endAddress = dev->startAddress + OCTOBUS_REGISTERS - 1;
    dev->interruptLevel = OCTOBUS_INT_LEVEL;

    /* Receive ident 40B for interface 0, then 42B, 44B, 46B; transmit is the
     * receive code plus one. Live-verified from TPE OCTOBUS B00's own table -
     * see the header, including the two plausible-looking claims it refutes. */
    dev->identCode = (uint16_t)(040 + (thumbwheel * 2));

    snprintf(dev->memoryName, sizeof(dev->memoryName), "Octobus %u", (unsigned)(thumbwheel + 1));

    dev->Reset = octobus_reset;
    dev->Read = octobus_read;
    dev->Write = octobus_write;
    dev->Ident = octobus_ident;
    dev->Destroy = octobus_destroy;
    dev->deviceData = data;

    octobus_reset(dev);

    LOG(LOG_CAT_DEVICE, LOG_INFO, "Octobus device created: %s at %06o, ident %02o/%02o level %d\n",
        dev->memoryName, (unsigned)dev->startAddress, (unsigned)dev->identCode,
        (unsigned)(dev->identCode + 1), OCTOBUS_INT_LEVEL);
    return dev;
}
