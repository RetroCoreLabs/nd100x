/*
 * test_octobus.c - the ND-100 octobus interface card.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * The addresses and ident codes here are the point of the test. Two plausible
 * ident claims are refuted in device_octobus.h - the "(addr-100200)/4+20"
 * formula that yields 60B, and the 37B/40B reading of the L07 ITB13 table - and
 * the value that is actually right came from the hardware's own printed table
 * via TPE OCTOBUS B00. A test that only checked "some number" would not have
 * caught either wrong answer, so it checks the numbers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "devices_types.h"
#include "devices_protos.h"

static int s_failed = 0;
static int s_checks = 0;

#define CHECK(cond, msg)                                                                 \
    do                                                                                   \
    {                                                                                    \
        s_checks++;                                                                      \
        if (!(cond))                                                                     \
        {                                                                                \
            printf("  FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);                   \
            s_failed++;                                                                  \
        }                                                                                \
    } while (0)

/* Stubs for what device.c references and this card never touches. Linking the
 * CPU in would mean a failure here could come from anywhere; the octobus card
 * does no DMA and reads no physical memory. */
int  g_dma_access = 0;
int  mms_read_physical_memory(int addr, bool priv) { (void)addr; (void)priv; return 0; }
void mms_write_physical_memory(int addr, uint16_t v, bool priv)
{
    (void)addr;
    (void)v;
    (void)priv;
}

int main(void)
{
    printf("ND-100 octobus card tests\n");
    printf("=========================\n\n");

    Device *dev = octobus_create_device(0);
    CHECK(dev != NULL, "interface 1 is created");
    if (!dev)
    {
        return 1;
    }

    /* THE ADDRESSES. Byte-verified: the resident commoncode E-frame sender at
     * 063247 IOXTs the literals 100405 and 100406, and s3vs-4.symb puts OOCT0
     * at 100400+4. */
    CHECK(dev->startAddress == 0100400, "interface 1 starts at 100400 octal");
    CHECK(dev->endAddress == 0100407, "and ends at 100407 - eight registers");
    CHECK(dev->interruptLevel == 13, "on interrupt level 13");

    /* THE IDENT CODE. 40B receive, 41B transmit, from TPE OCTOBUS B00's own
     * LIST-OCTOBUS-DEVICES table. NOT 60B (the refuted formula) and NOT 37B
     * (the refuted ITB13 reading). */
    CHECK(dev->identCode == 040, "the receive ident is 40B");
    CHECK(dev->identCode != 060, "NOT 60B - the (addr-100200)/4+20 formula is refuted");
    CHECK(dev->identCode != 037, "NOT 37B - the L07 ITB13 slot index is not the ident code");
    CHECK(dev->Ident(dev, 13) == 040, "and the ident hook answers on level 13");
    CHECK(dev->Ident(dev, 12) == 0, "and says nothing on any other level");

    /* Four interfaces, 010 octal apart, idents 40B 42B 44B 46B. SINTRAN's
     * OCSTART only handles interface 0, but the catalogue lists all four. */
    Device *dev2 = octobus_create_device(1);
    Device *dev4 = octobus_create_device(3);
    CHECK(dev2 != NULL && dev2->startAddress == 0100410, "interface 2 is at 100410");
    CHECK(dev2 != NULL && dev2->identCode == 042, "with receive ident 42B");
    CHECK(dev4 != NULL && dev4->startAddress == 0100430, "interface 4 is at 100430");
    CHECK(dev4 != NULL && dev4->identCode == 046, "with receive ident 46B");
    CHECK(octobus_create_device(4) == NULL, "there is no fifth interface");

    /* THE PRESENCE PROBE. OCSTART reads +2 purely to find out whether the card
     * exists: reaching the read at all means it does, and an absent card is an
     * IOX error rather than a value from here. */
    (void)dev->Read(dev, 0100402);
    CHECK(s_checks > 0, "reading the input status register does not fault");

    /* CH5CPUPRESENT spins on output status bit 3 before sending a command:
     * "T:=100406; *IOXT; WHILE A NBIT 3". A card that never sets it HANGS the
     * probe rather than reporting a missing CPU, so the bit is set from reset. */
    uint16_t status = dev->Read(dev, 0100406);
    CHECK((status & (1u << 3)) != 0, "output status reports DATA READY from reset");

    /* Clearing an interface: value 20 octal to the control register
     * (PH-P2-OPPSTART.NPL:4054 for input, :4055 for output). */
    dev->Write(dev, 0100403, 020);
    dev->Write(dev, 0100407, 020);
    status = dev->Read(dev, 0100406);
    CHECK((status & (1u << 3)) != 0,
          "DATA READY survives the clear - OCSTART sends a command straight after");

    /* A command written to +5 is recorded. CMMACLE and the rest are NOT acted
     * on: mastering-clearing a SAMSON that is not attached would be inventing a
     * machine. */
    dev->Write(dev, 0100405, 0x1234);
    CHECK(dev->Read(dev, 0100404) == 0, "and no frame is invented in the data register");

    /* Reading a write register or writing a read register is a guest bug, not a
     * card feature. Neither may fault. */
    CHECK(dev->Read(dev, 0100401) == 0, "reading a write-only register is 0");
    dev->Write(dev, 0100400, 0xFFFF);
    CHECK(dev->Read(dev, 0100400) == 0, "writing a read-only register changes nothing");

    /* Reset returns the card to the state the probes expect. */
    dev->Reset(dev);
    CHECK((dev->Read(dev, 0100406) & (1u << 3)) != 0, "reset leaves DATA READY set");

    if (dev->Destroy)
    {
        dev->Destroy(dev);
    }
    free(dev);
    if (dev2)
    {
        if (dev2->Destroy)
        {
            dev2->Destroy(dev2);
        }
        free(dev2);
    }
    if (dev4)
    {
        if (dev4->Destroy)
        {
            dev4->Destroy(dev4);
        }
        free(dev4);
    }

    printf("\n%d check(s), %d failed\n", s_checks, s_failed);
    if (s_failed != 0)
    {
        printf("FAIL\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
