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

/* Read a card's status registers through the bitfield unions, the way the
 * device itself works with them. */
static OctobusInputStatus octobus_in_status(Device *card)
{
    OctobusInputStatus st;
    st.raw = card->Read(card, 0100402);
    return st;
}

static OctobusOutputStatus octobus_out_status(Device *card)
{
    OctobusOutputStatus st;
    st.raw = card->Read(card, 0100406);
    return st;
}

/* Control words with only the interrupt-enable bit set. */
static uint16_t octobus_in_ctrl_int_enable(void)
{
    OctobusInputControl c;
    c.raw = 0;
    c.bits.interruptEnabled = 1;
    return c.raw;
}

static uint16_t octobus_out_ctrl_int_enable(void)
{
    OctobusOutputControl c;
    c.raw = 0;
    c.bits.interruptEnabled = 1;
    return c.raw;
}

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

/* A mock bus behind the card: a station registry and nothing else. What is
 * under test is the CARD's half of the seam, so the bus is as simple as it can
 * be while still answering for some stations and not others. */
typedef struct
{
    bool          present[64];
    unsigned long sent;
} MockBus;

static bool mock_transmit(void *ctx, Device *card, uint16_t frame)
{
    MockBus *bus = (MockBus *)ctx;
    bus->sent++;

    uint8_t dest = (uint8_t)((frame >> 8u) & 0x3Fu);
    if (dest == 0 || dest > 62 || !bus->present[dest])
    {
        /* Nobody there: the real bus reports this as an Ack=00 timeout after the
         * 15 hardware retries, and FALSE is how the card is told - it turns that
         * into ERROR + NOT PRESENT in the output status. */
        return false;
    }

    /* The answer carries the SOURCE in bits 13-8 - the destination-to-source
     * rewrite the bus performs on delivery - and arrives in the card's FIFO. */
    uint16_t reply = (uint16_t)((frame & 0xC0FFu) | ((uint32_t)dest << 8u));
    (void)octobus_rx_push(card, reply);
    return true;
}

static MockBus bus_lb;

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
    /* IDENT answers only when this card has actually REQUESTED the interrupt.
     * That is how devmgr_ident picks the right device off a shared level: a card
     * answering unconditionally would steal another card's ident. */
    CHECK(dev->Ident(dev, 13) == 0, "IDENT with nothing pending answers nothing");
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
    CHECK(octobus_out_status(dev).bits.readyForTransfer,
          "output status reports DATA READY from reset");

    /* Clearing an interface: value 20 octal to the control register
     * (PH-P2-OPPSTART.NPL:4054 for input, :4055 for output). */
    dev->Write(dev, 0100403, 020);
    dev->Write(dev, 0100407, 020);
    CHECK(octobus_out_status(dev).bits.readyForTransfer,
          "DATA READY survives the clear - OCSTART sends a command straight after");

    /* A command written to +5 is recorded. CMMACLE and the rest are NOT acted
     * on: mastering-clearing a SAMSON that is not attached would be inventing a
     * machine. */
    dev->Write(dev, 0100405, 0x1234);
    CHECK(dev->Read(dev, 0100404) == 0, "and no frame is invented in the data register");

    /* Reading a write register or writing a read register is a guest bug, not a
     * card feature. Neither may fault. */
    CHECK(dev->Read(dev, 0100401) == 0, "reading a write-only register is 0");
    dev->Reset(dev);
    dev->Write(dev, 0100400, 0xFFFF);
    CHECK(octobus_rx_count(dev) == 0, "writing the read-only data register queues nothing");
    CHECK(dev->Read(dev, 0100400) == 0, "and it reads back 0");

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
    CHECK(card->interruptLevel == 13, "TPE 1: on interrupt level 13");

    /* ---- TPE test 3: Check receive FIFO length --------------------------
     * The real loop: write words while input status bit 2 (FIFO NOT FULL)
     * stays set, and count them. The answer must be 16.
     *
     * Bit 2 is INVERTED from how it reads: SET means space available. Getting
     * that backwards either writes nothing or never terminates, so the loop is
     * bounded well above 16 to fail as a wrong COUNT rather than as a hang. */
    card->Reset(card);
    CHECK(octobus_in_status(card).bits.fifoNotFull,
          "TPE 3: an empty FIFO reports space available");
    CHECK(!octobus_in_status(card).bits.dataAvailable,
          "TPE 3: and reports no data available");

    int written = 0;
    while (octobus_in_status(card).bits.fifoNotFull && written < 64)
    {
        card->Write(card, 0100401, (uint16_t)(0x1000 + written));
        written++;
    }
    CHECK(written == OCTOBUS_RX_FIFO_WORDS, "TPE 3: the receive FIFO holds exactly 16 words");
    CHECK(octobus_rx_count(card) == OCTOBUS_RX_FIFO_WORDS, "TPE 3: and the card agrees");
    CHECK(octobus_in_status(card).bits.dataAvailable,
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
    CHECK(!octobus_in_status(card).bits.dataAvailable,
          "TPE 2: which the status bit reports");
    CHECK(octobus_in_status(card).bits.fifoNotFull,
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
    CHECK(octobus_in_status(card).bits.fifoNotFull,
          "and leaves FIFO-not-full SET, not zeroed");

    /* ---- TPE test 2, the real one: loop ALL patterns ---------------------
     * TPE's test 2 is "Loop all possible patterns - exhaustive pattern test
     * through loopback to verify data integrity across all bit combinations".
     * All 65536 of them, in FIFO-sized batches, because a card that corrupts
     * one bit in one pattern passes any sampled test. */
    card->Reset(card);
    unsigned long patterns = 0;
    bool          pattern_ok = true;
    for (uint32_t base = 0; base <= 0xFFFFu; base += OCTOBUS_RX_FIFO_WORDS)
    {
        int batch = 0;
        while (batch < OCTOBUS_RX_FIFO_WORDS && (base + (uint32_t)batch) <= 0xFFFFu)
        {
            card->Write(card, 0100401, (uint16_t)(base + (uint32_t)batch));
            batch++;
        }
        for (int i = 0; i < batch; i++)
        {
            uint16_t expect = (uint16_t)(base + (uint32_t)i);
            if (card->Read(card, 0100400) != expect)
            {
                pattern_ok = false;
            }
            patterns++;
        }
    }
    CHECK(pattern_ok, "TPE 2: all 65536 bit patterns survive the loopback");
    CHECK(patterns == 65536ul, "TPE 2: and every one of them was tried");

    /* =====================================================================
     * WHAT TPE'S *CONFIGURATION* PROGRAM CHECKS, AND REPORTED AS FAILING.
     *
     * A live run against a machine with [mfbus] + [controller.octobus.0] gave:
     *
     *   *** ERROR ***  Device number : 100400B
     *                  Device status : 000004B
     *                  Input channel did not receive test-data.
     *                  No identcode found on level 13D
     *                  Expected identcodes : 40B and 41B
     *
     * Two separate defects behind one report, so two groups of checks.
     * ===================================================================== */

    /* ---- (a) LOOPBACK: data written to the output must reach the input ----
     * Status 000004B is FIFO-not-full set with data-available CLEAR - an empty
     * receive FIFO. TPE wrote test data and nothing arrived.
     *
     * A frame addressed to station 0 is the card testing ITSELF: 0 is not a
     * legal station, which is exactly why it can mean loopback. It must echo
     * into this card's own receive FIFO, whether or not a bus is attached. */
    card->Reset(card);
    octobus_set_transmit(card, NULL, NULL);   /* standalone, no bus */
    CHECK(octobus_rx_count(card) == 0, "TPE-CONF: the receive FIFO starts empty");
    card->Write(card, 0100405, 0x00A5u);      /* destination 0 = loopback */
    CHECK(octobus_rx_count(card) == 1, "TPE-CONF: a dest-0 frame loops back");
    CHECK(octobus_in_status(card).bits.dataAvailable,
          "TPE-CONF: and the input channel reports test-data available");
    /* The frame comes back with OUR STATION STAMPED into bits 13:8 - the hardware
     * stamps the sender onto every received frame, and TPE's self-send
     * cross-check compares this against the +2 own-station field. C/B (15,14) and
     * the information byte (7:0) are preserved, which is what keeps TPE's pattern
     * echo working. */
    CHECK(card->Read(card, 0100400) ==
              (uint16_t)((0x00A5u & 0xC0FFu) | (OCTOBUS_ND100_STATION << 8)),
          "TPE-CONF: with the data intact and our station stamped as the source");
    CHECK(octobus_in_status(card).bits.station == OCTOBUS_ND100_STATION,
          "TPE-CONF: and the +2 own-station field matches it");

    /* Loopback must survive a bus being attached, or the standalone tests break
     * on any machine that has an ND-5000 - which is the configuration TPE was
     * run on. */
    card->Reset(card);
    octobus_set_transmit(card, mock_transmit, &bus_lb);
    memset(&bus_lb, 0, sizeof(bus_lb));
    card->Write(card, 0100405, 0x00A5u);
    CHECK(octobus_rx_count(card) == 1, "TPE-CONF: dest 0 still loops back with a bus attached");
    CHECK(bus_lb.sent == 0, "TPE-CONF: and does NOT go out on the bus");

    /* A frame with a real destination goes to the bus, not the FIFO. */
    card->Reset(card);
    memset(&bus_lb, 0, sizeof(bus_lb));
    bus_lb.present[56] = true;
    card->Write(card, 0100405, (uint16_t)(0x8000u | (56u << 8u)));
    CHECK(bus_lb.sent == 1, "TPE-CONF: a real destination goes onto the bus");

    /* ---- (b) IDENT ON LEVEL 13 -------------------------------------------
     * "No identcode found on level 13D. Expected identcodes : 40B and 41B."
     * The card must ASSERT level 13 when something happens and answer IDENT
     * with 40B for the input controller or 41B for the output one. */
    card->Reset(card);
    octobus_set_transmit(card, NULL, NULL);
    CHECK((card->interruptBits & (1u << card->interruptLevel)) == 0,
          "TPE-CONF: no interrupt is asserted from reset");

    /* Interrupts are enabled through bit 0 of each control register, and an
     * event only raises the line when its enable is set - otherwise a card
     * would interrupt before the driver was ready for it. */
    card->Write(card, 0100403, octobus_in_ctrl_int_enable());
    card->Write(card, 0100401, 0x1234u);      /* a word arrives: input event */
    CHECK((card->interruptBits & (1u << card->interruptLevel)) != 0,
          "TPE-CONF: an input event with the enable set asserts level 13");
    CHECK(card->Ident(card, card->interruptLevel) == 040,
          "TPE-CONF: and IDENT answers 40B for the INPUT controller");
    CHECK((card->interruptBits & (1u << card->interruptLevel)) == 0,
          "TPE-CONF: IDENT clears the request, so the level de-asserts");

    /* One-shot: IDENT clears the ENABLE too, so a second event does not
     * interrupt until the driver re-arms. Without that the level re-fires
     * forever after being serviced. */
    card->Write(card, 0100401, 0x5678u);
    CHECK((card->interruptBits & (1u << card->interruptLevel)) == 0,
          "TPE-CONF: a further event does not interrupt until re-armed");
    CHECK(card->Ident(card, card->interruptLevel) == 0, "TPE-CONF: and IDENT answers nothing");

    /* The OUTPUT controller answers 41B, and the input has priority when both
     * are pending. */
    card->Reset(card);
    card->Write(card, 0100407, octobus_out_ctrl_int_enable());
    card->Write(card, 0100405, (uint16_t)(0x8000u | (40u << 8u)));  /* a send completes */
    CHECK((card->interruptBits & (1u << card->interruptLevel)) != 0,
          "TPE-CONF: an output event asserts level 13");
    CHECK(card->Ident(card, card->interruptLevel) == 041,
          "TPE-CONF: and IDENT answers 41B for the OUTPUT controller");

    card->Reset(card);
    card->Write(card, 0100403, octobus_in_ctrl_int_enable());
    card->Write(card, 0100407, octobus_out_ctrl_int_enable());
    card->Write(card, 0100401, 0x1111u);                            /* input event */
    card->Write(card, 0100405, (uint16_t)(0x8000u | (40u << 8u)));   /* output event */
    CHECK(card->Ident(card, card->interruptLevel) == 040, "TPE-CONF: the INPUT has priority");
    CHECK(card->Ident(card, card->interruptLevel) == 041, "TPE-CONF: then the output answers");

    /* An event with the enable CLEAR must not assert the level. */
    card->Reset(card);
    card->Write(card, 0100401, 0x2222u);
    CHECK((card->interruptBits & (1u << card->interruptLevel)) == 0,
          "TPE-CONF: an event with interrupts disabled does not assert level 13");
    CHECK(card->Ident(card, card->interruptLevel) == 0, "TPE-CONF: and IDENT answers nothing");

    /* Another level must never be answered. */
    card->Reset(card);
    card->Write(card, 0100403, octobus_in_ctrl_int_enable());
    card->Write(card, 0100401, 0x3333u);
    CHECK(card->Ident(card, 11) == 0, "TPE-CONF: IDENT on level 11 answers nothing");

    /* ---- TPE test 4: Check octobus configuration ------------------------
     * Station discovery: send an "identify yourself" message to each station
     * and see who answers. A present station replies and its answer arrives in
     * the card's receive FIFO; an absent one answers nothing.
     *
     * The bus behind the card is a mock here on purpose. What is under test is
     * the CARD's half - that a write to the command register goes out, that a
     * reply comes back into the FIFO, and that silence stays silence. The real
     * fabric is wired in mfbus_bridge and tested there. */
    card->Reset(card);
    MockBus bus;
    memset(&bus, 0, sizeof(bus));
    bus.present[8] = true;   /* 010B, a SCSI controller */
    bus.present[56] = true;  /* 070B, an ND-5000 */
    octobus_set_transmit(card, mock_transmit, &bus);

    /* ---- ERROR and NOT PRESENT: how a scan tells absent from silent ----
     *
     * These are the bits that answer "is there a CPU at this station". The
     * register used to declare them "not used", which is how the ND-500 monitor's
     * discovery had nothing to read. They describe the LAST transfer only, so each
     * write clears them first.
     *
     * Source for the layout: the hardware decode via RetroCore NDBusOctobus.cs
     * TransmitStatusBits, where the same two bits are set together on a timeout
     * and cleared together at the top of every send. RetroCore labels the exact
     * positions inferred; so does device_octobus.h. */
    {
        /* 070B is present in this mock and answers. */
        card->Write(card, 0100405, (uint16_t)(0x8000u | (56u << 8u)));
        CHECK(octobus_out_status(card).bits.notPresent == 0,
              "a frame to a present station leaves NOT PRESENT clear");
        CHECK(octobus_out_status(card).bits.error == 0, "and ERROR clear");

        /* 077B is not a station at all - nothing can answer. */
        card->Write(card, 0100405, (uint16_t)(0x8000u | (63u << 8u)));
        CHECK(octobus_out_status(card).bits.notPresent == 1,
              "a frame nobody answers sets NOT PRESENT");
        CHECK(octobus_out_status(card).bits.error == 1, "and ERROR");
        CHECK(octobus_out_status(card).bits.readyForTransfer == 1,
              "the transfer attempt still completes - ready-for-transfer stays set");

        /* And the next successful send clears them again: they are the last
         * transfer's result, not a latched fault. */
        card->Write(card, 0100405, (uint16_t)(0x8000u | (56u << 8u)));
        CHECK(octobus_out_status(card).bits.notPresent == 0,
              "the next answered frame clears NOT PRESENT again");
        CHECK(octobus_out_status(card).bits.error == 0, "and ERROR again");

        /* The bits this card never asserts, because no evidence says when they
         * would. Named in the register, always zero here - so a later change that
         * starts setting one has to come with its own evidence. */
        OctobusOutputStatus ost = octobus_out_status(card);
        CHECK(ost.bits.requestOn == 0 && ost.bits.retryCounter0 == 0
                  && ost.bits.parityError == 0 && ost.bits.master == 0,
              "REQUEST ON, RETRY COUNTER 0, PARITY ERROR and MASTER are never set");

        /* Drain whatever the three sends above left in the FIFO, and forget them
         * in the mock's own counter, so the station scan that follows starts from
         * an empty FIFO and its send count still means what it says. */
        while (octobus_in_status(card).bits.dataAvailable)
        {
            (void)card->Read(card, 0100400);
        }
        bus.sent = 0;
    }

    int answered = 0;
    int probed = 0;
    for (int st = 1; st <= 62; st++)
    {
        if (st == OCTOBUS_ND100_STATION)
        {
            /* Our own station is a LOCAL loopback, not a bus probe - see
             * OCTOBUS_IS_SELF_LOOP. Probing it would answer from our own FIFO and
             * tell us nothing about the bus. */
            continue;
        }
        /* Ident: C=1, and E/K/M all clear. */
        uint16_t ident = (uint16_t)(0x8000u | ((uint32_t)st << 8u));
        card->Write(card, 0100405, ident);
        probed++;
        if (octobus_in_status(card).bits.dataAvailable)
        {
            uint16_t reply = card->Read(card, 0100400);
            /* The answer names the station that sent it, in bits 13-8 - the
             * destination-to-source rewrite the bus performs on delivery. */
            if (((reply >> 8u) & 0x3Fu) == (uint16_t)st)
            {
                answered++;
            }
        }
    }
    CHECK(probed == 61, "TPE 4: every legal station except our own was probed");
    CHECK(answered == 2, "TPE 4: exactly the two present stations answered");
    CHECK(bus.sent == 61, "TPE 4: and every probe actually went onto the bus");

    CHECK(octobus_rx_count(card) == 0, "TPE 4: with no reply left unread");

    /* Our own station answers from the LOCAL loopback, without the bus. */
    {
        unsigned long sent = bus.sent;
        card->Write(card, 0100405,
                    (uint16_t)(0x8000u | ((uint32_t)OCTOBUS_ND100_STATION << 8u)));
        CHECK(octobus_rx_count(card) == 1, "TPE 4: our own station self-loops");
        CHECK(bus.sent == sent, "TPE 4: and never reaches the bus");
    }

    /* Detaching the bus puts the card in LOOPBACK: with no CPU attached every
     * frame is echoed to our own input side, whatever its destination. That is
     * what makes TPE's stand-alone tests 1 to 3 runnable with no bus at all - and
     * it is a MODE, not a destination rule. */
    octobus_set_transmit(card, NULL, NULL);
    while (octobus_rx_count(card) > 0)
    {
        (void)card->Read(card, 0100400);
    }
    unsigned long sent_before = bus.sent;
    card->Write(card, 0100405, (uint16_t)(0x8000u | (56u << 8u)));
    CHECK(bus.sent == sent_before, "a detached card transmits nothing onto the bus");
    CHECK(octobus_rx_count(card) == 1, "and loops the frame back to its own input instead");

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
