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
#include "octobus/device_octobus.h"

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

    /* =====================================================================
     * TPE's OWN OCTOBUS TESTS.
     *
     * RetroCore tests this card by mirroring the Test Program Environment's
     * octobus tests, which is the strongest idea available here: the card is
     * checked the way the real diagnostic checks it, so passing means the same
     * thing it means on hardware. TPE's list:
     *
     *   Test 1  Check Ident                - interface detection
     *   Test 2  Check data transmission
     *   Test 3  Check receive FIFO length  - exactly 16 words
     *   Test 4  Check octobus configuration - station discovery
     *
     * Tests 1 to 3 need only the card. Test 4 needs a populated bus.
     * ===================================================================== */

    Device *card = octobus_create_device(0);
    CHECK(card != NULL, "TPE: a card to test");
    if (card == NULL)
    {
        printf("\n%d check(s), %d failed\n", s_checks, s_failed);
        return s_failed ? 1 : 0;
    }

    /* ---- TPE test 1: Check Ident ---------------------------------------- */
    CHECK(card->identCode == 040, "TPE 1: interface 1 identifies as 40B");
    CHECK(card->Ident(card, OCTOBUS_INT_LEVEL) == 040, "TPE 1: and answers on level 13");

    /* ---- TPE test 3: Check receive FIFO length --------------------------
     * The real loop: write words while input status bit 2 (FIFO NOT FULL)
     * stays set, and count them. The answer must be 16.
     *
     * Bit 2 is INVERTED from how it reads: SET means space available. Getting
     * that backwards either writes nothing or never terminates, so the loop is
     * bounded well above 16 to fail as a wrong COUNT rather than as a hang. */
    card->Reset(card);
    CHECK((card->Read(card, 0100402) & OCTOBUS_IN_STATUS_FIFO_NOT_FULL) != 0,
          "TPE 3: an empty FIFO reports space available");
    CHECK((card->Read(card, 0100402) & OCTOBUS_IN_STATUS_DATA_AVAIL) == 0,
          "TPE 3: and reports no data available");

    int written = 0;
    while ((card->Read(card, 0100402) & OCTOBUS_IN_STATUS_FIFO_NOT_FULL) != 0 && written < 64)
    {
        card->Write(card, 0100401, (uint16_t)(0x1000 + written));
        written++;
    }
    CHECK(written == OCTOBUS_RX_FIFO_WORDS, "TPE 3: the receive FIFO holds exactly 16 words");
    CHECK(octobus_rx_count(card) == OCTOBUS_RX_FIFO_WORDS, "TPE 3: and the card agrees");
    CHECK((card->Read(card, 0100402) & OCTOBUS_IN_STATUS_DATA_AVAIL) != 0,
          "TPE 3: a full FIFO reports data available");

    /* A word written to a full FIFO is DROPPED, as the hardware drops it. */
    CHECK(!octobus_rx_push(card, 0xFFFF), "TPE 3: a word past 16 is refused");
    CHECK(octobus_rx_count(card) == OCTOBUS_RX_FIFO_WORDS, "TPE 3: and does not grow the FIFO");

    /* ---- TPE test 2: Check data transmission ---------------------------
     * Standalone, this is the loopback: what went in comes back out, in
     * order, and the FIFO empties exactly. */
    bool order_ok = true;
    for (int i = 0; i < OCTOBUS_RX_FIFO_WORDS; i++)
    {
        uint16_t got = card->Read(card, 0100400);
        if (got != (uint16_t)(0x1000 + i))
        {
            order_ok = false;
        }
    }
    CHECK(order_ok, "TPE 2: every word comes back in the order it went in");
    CHECK(octobus_rx_count(card) == 0, "TPE 2: and the FIFO is empty");
    CHECK((card->Read(card, 0100402) & OCTOBUS_IN_STATUS_DATA_AVAIL) == 0,
          "TPE 2: which the status bit reports");
    CHECK((card->Read(card, 0100402) & OCTOBUS_IN_STATUS_FIFO_NOT_FULL) != 0,
          "TPE 2: along with space being available again");
    CHECK(card->Read(card, 0100400) == 0, "TPE 2: a drained FIFO reads 0");

    /* The FIFO is a RING: drain some, refill, and it must not wrap wrongly.
     * A shift buffer would pass the tests above and fail this one. */
    card->Reset(card);
    for (int i = 0; i < 10; i++)
    {
        (void)octobus_rx_push(card, (uint16_t)(0x2000 + i));
    }
    for (int i = 0; i < 6; i++)
    {
        (void)card->Read(card, 0100400);
    }
    for (int i = 0; i < 10; i++)
    {
        (void)octobus_rx_push(card, (uint16_t)(0x3000 + i));
    }
    CHECK(octobus_rx_count(card) == 14, "TPE 2: the ring holds 4 old plus 10 new");
    CHECK(card->Read(card, 0100400) == 0x2006, "TPE 2: and reads the oldest remaining first");

    /* Clearing the interface empties the FIFO and leaves the status bits
     * describing an EMPTY one - not zeroed. Software polling bit 2 after a
     * clear would otherwise see a permanently full FIFO. */
    card->Write(card, 0100403, 020);
    CHECK(octobus_rx_count(card) == 0, "clearing the interface empties the FIFO");
    CHECK((card->Read(card, 0100402) & OCTOBUS_IN_STATUS_FIFO_NOT_FULL) != 0,
          "and leaves FIFO-not-full SET, not zeroed");

    /* ---- TPE test 4: Check octobus configuration ------------------------
     * Station discovery needs a populated bus behind the card, and the frame
     * path from the fabric to this card is not wired yet. Named here so the
     * gap is visible in the test output rather than only in a document. */
    printf("  TPE 4 (station discovery): NOT RUN - the card has no fabric attached yet\n");

    if (card->Destroy)
    {
        card->Destroy(card);
    }
    free(card);

    printf("\n%d check(s), %d failed\n", s_checks, s_failed);
    if (s_failed != 0)
    {
        printf("FAIL\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
