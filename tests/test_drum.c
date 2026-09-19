/*
 * test_drum.c - Unit tests for the NORD TSS swapping drum device.
 *
 * Unit tests for the NORD TSS swapping-drum device (src/devices/drum/device_drum.c).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Following the test_bcd.c pattern: device_drum.c is linked in DIRECTLY together
 * with FAKE Device_* infrastructure provided here, so the drum's real register
 * handlers and transfer engine are exercised through their true entry points
 * without pulling in the machine, the CPU memory subsystem or a disk image.
 *
 * The wire protocol under test is specified in
 * <TSS>\docs\DRUM-DEVICE-SPEC.md.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device_drum.h"
#include "devices_protos.h"

/* ---------------- fake device infrastructure ---------------------------- */

/* A small physical-memory stand-in for DMA. 64 Ki words is plenty. */
#define FAKE_MEM_WORDS 65536u
static uint16_t g_fakeMem[FAKE_MEM_WORDS];

/* One pending delayed callback (the drum only ever queues one at a time). */
static IODelayedCallback g_pendingCb;
static void *g_pendingCtx;
static int g_pendingParam;
static uint8_t g_pendingLevel;
static int g_pendingSet;

void Device_Init(Device *dev, uint8_t thumbwheel, DeviceClass deviceClass, size_t blockSize)
{
    (void)thumbwheel;
    (void)blockSize;
    memset(dev, 0, sizeof(Device)); /* real Device_Init zeroes the struct */
    dev->deviceClass = deviceClass;
}

void Device_DMAWrite(uint32_t coreAddress, uint16_t data)
{
    if (coreAddress < FAKE_MEM_WORDS)
    {
        g_fakeMem[coreAddress] = data;
    }
}

int32_t Device_DMARead(uint32_t coreAddress)
{
    if (coreAddress < FAKE_MEM_WORDS)
    {
        return g_fakeMem[coreAddress];
    }
    return 0;
}

void Device_QueueIODelay(Device *dev, uint16_t ticks, IODelayedCallback cb, int param,
                         uint8_t irqlevel)
{
    (void)dev;
    (void)ticks;
    g_pendingCb = cb;
    g_pendingCtx = dev;
    g_pendingParam = param;
    g_pendingLevel = irqlevel;
    g_pendingSet = 1;
}

/* Mimics the real Device_TickIODelay: fire the queued callback and, if it
 * returns true, raise the interrupt bit for its level (what the real IO-delay
 * machinery + Device_GenerateInterrupt do). */
void Device_TickIODelay(Device *dev)
{
    if (!g_pendingSet)
    {
        return;
    }
    g_pendingSet = 0;
    if (g_pendingCb && g_pendingCb(g_pendingCtx, g_pendingParam))
    {
        dev->interruptBits |= (uint16_t)(1u << g_pendingLevel);
    }
}

void Device_SetInterruptStatus(Device *dev, bool active, uint16_t level)
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

/* ---------------- tiny assert harness ----------------------------------- */

static int g_pass, g_fail;
// clang-format off
#define CHECK(cond, msg) do {                                            \
        if (cond) { g_pass++; }                                          \
        else { g_fail++; printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)
// clang-format on

/* Build a control word: function in bits 13-14, core-addr-hi in 5-6, go=7. */
static uint16_t ctrl_go(uint16_t func, uint16_t addrHi)
{
    return (uint16_t)((func << 13) | ((addrHi & 3) << 5) | DRUM_CTRL_GO_VALUE);
}
/* Build a drum block address: sector in 15-11, track in 10-0. */
static uint16_t block_of(uint16_t sector, uint16_t track)
{
    return (uint16_t)(((sector & 037) << 11) | (track & 03777));
}

/* Program and start one transfer. */
static void program(Device *dev, uint16_t core, uint16_t block, uint16_t wc, uint16_t control)
{
    uint32_t base = dev->startAddress;
    dev->Write(dev, base + DRUM_REG_LOAD_WORDCNT, wc);
    dev->Write(dev, base + DRUM_REG_LOAD_CORE, core);
    dev->Write(dev, base + DRUM_REG_LOAD_BLOCK, block);
    dev->Write(dev, base + DRUM_REG_LOAD_CONTROL, control); /* triggers go */
}
static uint16_t status(Device *dev)
{
    return dev->Read(dev, dev->startAddress + DRUM_REG_READ_STATUS);
}

int main(void)
{
    printf("=== drum device tests ===\n");
    Device *dev = CreateDrumDevice(0);
    CHECK(dev != NULL, "device created");
    if (!dev)
    {
        return 1;
    }
    CHECK(dev->startAddress == 0540 && dev->endAddress == 0547, "address block 540-547");
    CHECK(dev->interruptLevel == DRUM_INT_LEVEL, "interrupt level 11");
    DrumData *d = (DrumData *)dev->deviceData;

    /* --- 1. WRITE (memory -> drum): fill memory, transfer, check surface. --- */
    memset(g_fakeMem, 0, sizeof(g_fakeMem));
    for (uint16_t i = 0; i < 8; i++)
    {
        g_fakeMem[0x100 + i] = (uint16_t)(0xA000 + i);
    }
    uint16_t blk = block_of(/*sector*/ 3, /*track*/ 5);
    program(dev, 0x100, blk, 8, ctrl_go(DRUM_FUNC_WRITE, 0));
    uint32_t off = ((uint32_t)5 * DRUM_SECTORS_PER_TRACK + 3) * DRUM_WORDS_PER_SECTOR;
    int okW = 1;
    for (uint16_t i = 0; i < 8; i++)
    {
        if (d->surface[off + i] != (uint16_t)(0xA000 + i))
        {
            okW = 0;
        }
    }
    CHECK(okW, "write transfer copied memory to the correct drum offset");
    CHECK((status(dev) & DRUM_STATUS_DVA) != 0, "DVA set (busy) after go");

    /* completion: tick fires the delayed callback -> DVA clears, interrupt up */
    dev->Tick(dev);
    CHECK((status(dev) & DRUM_STATUS_DVA) == 0, "DVA cleared after completion");
    CHECK((dev->interruptBits & (1u << DRUM_INT_LEVEL)) != 0, "interrupt raised on level 11");
    CHECK(dev->Ident(dev, DRUM_INT_LEVEL) == DRUM_IDENT_CODE, "Ident returns the ident code");
    CHECK((dev->interruptBits & (1u << DRUM_INT_LEVEL)) == 0, "Ident clears the interrupt bit");

    /* --- 2. READ (drum -> memory): read the page we just wrote back out. --- */
    memset(g_fakeMem, 0, sizeof(g_fakeMem));
    program(dev, 0x200, blk, 8, ctrl_go(DRUM_FUNC_READ, 0));
    dev->Tick(dev);
    int okR = 1;
    for (uint16_t i = 0; i < 8; i++)
    {
        if (g_fakeMem[0x200 + i] != (uint16_t)(0xA000 + i))
        {
            okR = 0;
        }
    }
    CHECK(okR, "read transfer copied the drum back into memory");
    CHECK((status(dev) & DRUM_STATUS_ERR) == 0, "no error on a good read");

    /* --- 3. COMPARE: equal -> no ERR; mismatched -> ERR. --- */
    program(dev, 0x200, blk, 8, ctrl_go(DRUM_FUNC_COMPARE, 0));
    dev->Tick(dev);
    CHECK((status(dev) & DRUM_STATUS_ERR) == 0, "compare of identical data: no error");
    g_fakeMem[0x203] ^= 0xFFFF; /* corrupt one word */
    program(dev, 0x200, blk, 8, ctrl_go(DRUM_FUNC_COMPARE, 0));
    dev->Tick(dev);
    CHECK((status(dev) & DRUM_STATUS_ERR) != 0, "compare mismatch sets ERR");

    /* --- 4. Extended core address (control bits 5-6 = addr bits 16-17). --- */
    /* addrHi=1 -> core base 0x10000, which is beyond FAKE_MEM: the DMA is
       simply dropped by the fake, but coreAddress assembly is what we assert. */
    dev->Write(dev, dev->startAddress + DRUM_REG_LOAD_CORE, 0x0005);
    dev->Write(dev, dev->startAddress + DRUM_REG_LOAD_CONTROL, ctrl_go(DRUM_FUNC_READ_TEST, 1));
    CHECK(d->coreAddress == 0x10005u, "control bits 5-6 extend the core address to bit 16-17");
    dev->Tick(dev);

    /* --- 5. Full-size surface: every block address is in range; only a word
       count that overruns the end of the surface trips ERR. --- */
    uint16_t maxBlk = block_of(31, 03777); /* last addressable block */
    program(dev, 0x100, maxBlk, 8, ctrl_go(DRUM_FUNC_READ_TEST, 0));
    CHECK((status(dev) & DRUM_STATUS_ERR) == 0,
          "max block address is in range on a full-size drum");
    dev->Tick(dev);
    /* At the very last sector, a count past its final word overruns -> ERR. */
    program(dev, 0x100, maxBlk, DRUM_WORDS_PER_SECTOR + 1, ctrl_go(DRUM_FUNC_READ_TEST, 0));
    CHECK((status(dev) & DRUM_STATUS_ERR) != 0, "word count past end of surface sets ERR");
    dev->Tick(dev);
    CHECK((status(dev) & DRUM_STATUS_DVA) == 0, "device not left busy after a bounds error");

    if (dev->Destroy)
    {
        dev->Destroy(dev);
    }
    free(dev->deviceData); /* Device_Destroy() does this in the emulator */
    free(dev);

    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
