/*
 * wd_trace.c - Winchester conformance trace driver (nd100x side).
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

/*
 * WINCHESTER CONFORMANCE TRACE - nd100x side.
 *
 * Walks WD_CONFORMANCE_SEQ (tests/wd_conformance_seq.h) against this
 * repository's synchronous Winchester model and prints a canonical trace on
 * stdout.
 *
 * The counterpart driver is the portable Pi Pico core's test/wd_trace.c,
 * which walks the byte-identical copy of the same sequence header against an
 * asynchronous phase machine. Diffing the two traces is what proves the port
 * is bit-identical rather than merely similar: two completely different
 * internal structures must produce the same observable register behaviour.
 *
 * Trace format, deliberately dumb and stable, everything octal:
 *   NNN OP ARG VALUE
 * The step descriptions are NOT printed - editing a comment must not look
 * like a behavioural difference.
 *
 * Not a self-checking test: it has no verdict of its own, the DIFF is the
 * verdict. It is therefore built but not registered with add_test().
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "devices_types.h"
#include "device_winchester.h"
#include "devices_protos.h"

#include "wd_conformance_seq.h"

/* ---------------- fake machine hooks ------------------------------------ *
 * Same stubs as tests/test_winchester.c: this driver links the real
 * device_winchester.c + disk_winchester.c against a fake bus and a fake image,
 * with no machine and no CPU. */

#define FAKE_MEM_WORDS 65536u
static uint16_t fake_mem[FAKE_MEM_WORDS];

static IODelayedCallback pending_cb;
static void *pending_ctx;
static int pending_param;
static uint8_t pending_level;
static int pending_set;

#define FAKE_DISK_BLOCKS 64u
#define FAKE_BLOCK_BYTES 1024u
static uint8_t fake_disk[FAKE_DISK_BLOCKS * FAKE_BLOCK_BYTES];

void dev_init(Device *dev, uint8_t thumbwheel, DeviceClass device_class, size_t block_size)
{
    (void)thumbwheel;
    memset(dev, 0, sizeof(Device));
    dev->deviceClass = device_class;
    dev->blockSizeBytes = block_size;
}

void dev_dma_write(uint32_t core_address, uint16_t data)
{
    if (core_address < FAKE_MEM_WORDS)
    {
        fake_mem[core_address] = data;
    }
}

int32_t dev_dma_read(uint32_t core_address)
{
    if (core_address < FAKE_MEM_WORDS)
    {
        return fake_mem[core_address];
    }
    return 0;
}

void dev_queue_io_delay(Device *dev, uint16_t ticks, IODelayedCallback cb, int param,
                        uint8_t irqlevel)
{
    (void)ticks;
    pending_cb = cb;
    pending_ctx = dev;
    pending_param = param;
    pending_level = irqlevel;
    pending_set = 1;
}

void dev_tick_io_delay(Device *dev)
{
    if (!pending_set)
    {
        return;
    }
    pending_set = 0;
    if (pending_cb && pending_cb(pending_ctx, pending_param))
    {
        dev->interruptBits |= (uint16_t)(1u << pending_level);
    }
}

void dev_set_interrupt_status(Device *dev, bool active, uint16_t level)
{
    if (active)
    {
        dev->interruptBits |= (uint16_t)(1u << level);
    }
    else
    {
        dev->interruptBits &= (uint16_t)~(1u << level);
    }
}

uint32_t dev_register_address(Device *dev, uint32_t address)
{
    return address - dev->startAddress;
}

int32_t dev_io_buffer_read_word(Device *dev, uint8_t *buf, int32_t word_offset)
{
    (void)dev;
    return (int32_t)(((uint16_t)buf[word_offset * 2] << 8) | buf[word_offset * 2 + 1]);
}

int32_t dev_io_buffer_write_word(Device *dev, uint8_t *buf, int32_t word_offset, uint16_t data)
{
    (void)dev;
    buf[word_offset * 2] = (uint8_t)(data >> 8);
    buf[word_offset * 2 + 1] = (uint8_t)(data & 0xFF);
    return 0;
}

static int fake_read(Device *self, uint8_t *buffer, size_t block_count, uint32_t lba, int unit)
{
    (void)self;
    (void)unit;
    if (lba + block_count > FAKE_DISK_BLOCKS)
    {
        return -1;
    }
    memcpy(buffer, &fake_disk[lba * FAKE_BLOCK_BYTES], block_count * FAKE_BLOCK_BYTES);
    return (int)block_count;
}

static int fake_write(Device *self, const uint8_t *buffer, size_t block_count, uint32_t lba,
                      int unit)
{
    (void)self;
    (void)unit;
    if (lba + block_count > FAKE_DISK_BLOCKS)
    {
        return -1;
    }
    memcpy(&fake_disk[lba * FAKE_BLOCK_BYTES], buffer, block_count * FAKE_BLOCK_BYTES);
    return (int)block_count;
}

static int fake_info(Device *self, size_t *size, bool *read_only, int unit)
{
    (void)self;
    (void)unit;
    if (size)
    {
        *size = sizeof(fake_disk);
    }
    if (read_only)
    {
        *read_only = false;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */

int main(void)
{
    Device *dev;
    size_t i;

    memset(fake_mem, 0, sizeof(fake_mem));
    memset(fake_disk, 0, sizeof(fake_disk));

    dev = wd_create_winchester_device(0);
    if (dev == NULL)
    {
        printf("INIT FAILED\n");
        return 1;
    }

    dev->blockCallbacks.readFunc = fake_read;
    dev->blockCallbacks.writeFunc = fake_write;
    dev->blockCallbacks.diskInfoFunc = fake_info;

    for (i = 0; i < WD_CONFORMANCE_SEQ_LEN; i++)
    {
        const WdsStep *s = &WD_CONFORMANCE_SEQ[i];

        switch (s->op)
        {
        case WDS_RESET:
            dev->Reset(dev);
            printf("%03u RESET\n", (unsigned)i);
            break;

        case WDS_WRITE:
            dev->Write(dev, dev->startAddress + s->arg, (uint16_t)s->val);
            printf("%03u WRITE %u %06o\n", (unsigned)i, s->arg, s->val);
            break;

        case WDS_READ:
        {
            uint16_t v = dev->Read(dev, dev->startAddress + s->arg);
            printf("%03u READ %u %06o\n", (unsigned)i, s->arg, v);
            break;
        }

        case WDS_IDENT:
        {
            uint16_t v = dev->Ident(dev, (uint16_t)s->arg);
            printf("%03u IDENT %u %06o\n", (unsigned)i, s->arg, v);
            break;
        }

        case WDS_INTBITS:
            printf("%03u INTBITS %u\n", (unsigned)i,
                   (dev->interruptBits & (uint16_t)(1u << 11)) ? 1u : 0u);
            break;

        case WDS_SETTLE:
            /* This model completes an operation on the queued IO delay. The
             * Pico counterpart pumps its DMA/storage engines here instead;
             * both print nothing, so the traces stay aligned. */
            dev_tick_io_delay(dev);
            break;
        default:
            break;
        }
    }

    printf("END %u\n", (unsigned)WD_CONFORMANCE_SEQ_LEN);
    return 0;
}
