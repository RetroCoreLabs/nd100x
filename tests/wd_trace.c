/*
 * nd100x - ND100 Virtual Machine
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
#include "deviceWinchester.h"
#include "devices_protos.h"

#include "wd_conformance_seq.h"

/* ---------------- fake machine hooks ------------------------------------ *
 * Same stubs as tests/test_winchester.c: this driver links the real
 * deviceWinchester.c + diskWinchester.c against a fake bus and a fake image,
 * with no machine and no CPU. */

#define FAKE_MEM_WORDS 65536u
static uint16_t g_fakeMem[FAKE_MEM_WORDS];

static IODelayedCallback g_pendingCb;
static void            *g_pendingCtx;
static int              g_pendingParam;
static uint8_t          g_pendingLevel;
static int              g_pendingSet;

#define FAKE_DISK_BLOCKS 64u
#define FAKE_BLOCK_BYTES 1024u
static uint8_t g_fakeDisk[FAKE_DISK_BLOCKS * FAKE_BLOCK_BYTES];

void Device_Init(Device *dev, uint8_t thumbwheel, DeviceClass deviceClass, size_t blockSize)
{
    (void)thumbwheel;
    memset(dev, 0, sizeof(Device));
    dev->deviceClass = deviceClass;
    dev->blockSizeBytes = blockSize;
}

void Device_DMAWrite(uint32_t coreAddress, uint16_t data)
{
    if (coreAddress < FAKE_MEM_WORDS)
        g_fakeMem[coreAddress] = data;
}

int32_t Device_DMARead(uint32_t coreAddress)
{
    if (coreAddress < FAKE_MEM_WORDS)
        return g_fakeMem[coreAddress];
    return 0;
}

void Device_QueueIODelay(Device *dev, uint16_t ticks, IODelayedCallback cb, int param, uint8_t irqlevel)
{
    (void)ticks;
    g_pendingCb = cb;
    g_pendingCtx = dev;
    g_pendingParam = param;
    g_pendingLevel = irqlevel;
    g_pendingSet = 1;
}

void Device_TickIODelay(Device *dev)
{
    if (!g_pendingSet)
        return;
    g_pendingSet = 0;
    if (g_pendingCb && g_pendingCb(g_pendingCtx, g_pendingParam))
        dev->interruptBits |= (uint16_t)(1u << g_pendingLevel);
}

void Device_SetInterruptStatus(Device *dev, bool active, uint16_t level)
{
    if (active)
        dev->interruptBits |= (uint16_t)(1u << level);
    else
        dev->interruptBits &= (uint16_t)~(1u << level);
}

uint32_t Device_RegisterAddress(Device *dev, uint32_t address)
{
    return address - dev->startAddress;
}

int32_t Device_IO_BufferReadWord(Device *dev, uint8_t *buf, int32_t word_offset)
{
    (void)dev;
    return (int32_t)(((uint16_t)buf[word_offset * 2] << 8) | buf[word_offset * 2 + 1]);
}

int32_t Device_IO_BufferWriteWord(Device *dev, uint8_t *buf, int32_t word_offset, uint16_t data)
{
    (void)dev;
    buf[word_offset * 2] = (uint8_t)(data >> 8);
    buf[word_offset * 2 + 1] = (uint8_t)(data & 0xFF);
    return 0;
}

static int fake_read(Device *self, uint8_t *buffer, size_t blockCount, uint32_t lba, int unit)
{
    (void)self; (void)unit;
    if (lba + blockCount > FAKE_DISK_BLOCKS)
        return -1;
    memcpy(buffer, &g_fakeDisk[lba * FAKE_BLOCK_BYTES], blockCount * FAKE_BLOCK_BYTES);
    return (int)blockCount;
}

static int fake_write(Device *self, const uint8_t *buffer, size_t blockCount, uint32_t lba, int unit)
{
    (void)self; (void)unit;
    if (lba + blockCount > FAKE_DISK_BLOCKS)
        return -1;
    memcpy(&g_fakeDisk[lba * FAKE_BLOCK_BYTES], buffer, blockCount * FAKE_BLOCK_BYTES);
    return (int)blockCount;
}

static int fake_info(Device *self, size_t *size, bool *readOnly, int unit)
{
    (void)self; (void)unit;
    if (size)
        *size = sizeof(g_fakeDisk);
    if (readOnly)
        *readOnly = false;
    return 0;
}

/* ------------------------------------------------------------------------ */

int main(void)
{
    Device *dev;
    size_t i;

    memset(g_fakeMem, 0, sizeof(g_fakeMem));
    memset(g_fakeDisk, 0, sizeof(g_fakeDisk));

    dev = CreateWinchesterDevice(0);
    if (dev == NULL)
    {
        printf("INIT FAILED\n");
        return 1;
    }

    dev->blockCallbacks.readFunc     = fake_read;
    dev->blockCallbacks.writeFunc    = fake_write;
    dev->blockCallbacks.diskInfoFunc = fake_info;

    for (i = 0; i < WD_CONFORMANCE_SEQ_LEN; i++)
    {
        const wds_step *s = &WD_CONFORMANCE_SEQ[i];

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
            Device_TickIODelay(dev);
            break;
        }
    }

    printf("END %u\n", (unsigned)WD_CONFORMANCE_SEQ_LEN);
    return 0;
}
