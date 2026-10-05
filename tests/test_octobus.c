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
 *
 * The card is a port of RetroCore's NDBusOctobus.cs, and that file is the
 * authority for what each register does. "NDBusOctobus.cs Name:line" below
 * refers to $RETROCORE/Emulated.HW/ND/CPU/NDBUS/NDBusOctobus.cs.
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

/* Stands in for the ND-5000 station's "continue ACCP", which the embedding
 * installs with octobus_set_continue_accp(). Counts the calls. */
static void count_continue_accp(void *ctx)
{
    unsigned long *calls = (unsigned long *)ctx;
    (*calls)++;
}

/* Is the card asserting its interrupt level right now? */
static bool octobus_line_up(Device *card)
{
    return (card->interruptBits & (1u << card->interruptLevel)) != 0;
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

    /* A word written to +5 is stored in the output data register and +4 reads it
     * back (NDBusOctobus.cs ProcessCommand:2976, Read:2735-2738).
     *
     * CHANGED CHECK: this used to demand 0 here ("no frame is invented in the
     * data register"). The C# stores the written word and returns it, so the
     * check now demands the word. */
    dev->Write(dev, 0100405, 0x1234);
    CHECK(dev->Read(dev, 0100404) == 0x1234, "+4 reads back the last word written to +5");
    dev->Write(dev, 0100405, 0x4321);
    CHECK(dev->Read(dev, 0100404) == 0x4321, "and follows the next write");
    /* The output clear zeroes it (ProcessControlChange:2929). */
    dev->Write(dev, 0100407, 020);
    CHECK(dev->Read(dev, 0100404) == 0, "the output clear zeroes the output data register");

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

    dev_destroy(dev);
    free(dev);
    if (dev2)
    {
        dev_destroy(dev2);
        free(dev2);
    }
    if (dev4)
    {
        dev_destroy(dev4);
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

    /* CHANGED: the words used to go in through +1, the input data register. In
     * the C# a write to +1 only stores the word (NDBusOctobus.cs Write:2790-2793)
     * and the FIFO is filled by frames sent to station 0 through +5, which the
     * card loops back (ProcessTransmitFrame:3113-3125). That is also what TPE's
     * test 3 does: it fills with the values 0..17B. Each word comes back with our
     * station stamped into bits 13:8. */
    int written = 0;
    while (octobus_in_status(card).bits.fifoNotFull && written < 64)
    {
        card->Write(card, 0100405, (uint16_t)written);
        written++;
    }
    CHECK(written == OCTOBUS_RX_FIFO_WORDS, "TPE 3: the receive FIFO holds exactly 16 words");
    CHECK(octobus_rx_count(card) == OCTOBUS_RX_FIFO_WORDS, "TPE 3: and the card agrees");
    CHECK(octobus_in_status(card).bits.dataAvailable,
          "TPE 3: a full FIFO reports data available");

    /* A frame the GUEST ITSELF loops back into its own full FIFO is not queued:
     * there is no sender to retry it. The output controller goes BUSY with READY
     * clear instead, so the guest can see the transfer did not complete
     * (NDBusOctobus.cs ProcessTransmitQueue:3263-3276).
     * A frame arriving over the BUS is a different case and is parked for retry
     * instead - checked at the end of this file. Keeping the two apart is the
     * point: before the retry model, a bus arrival was dropped here too, and
     * every multibyte reply over 16 frames came out truncated. */
    card->Write(card, 0100405, 0x00FF);
    CHECK(octobus_rx_count(card) == OCTOBUS_RX_FIFO_WORDS,
          "TPE 3: a guest write past 16 does not grow the FIFO");
    CHECK(!octobus_in_status(card).bits.fifoNotFull,
          "TPE 3: which still reports itself full");
    CHECK(octobus_out_status(card).bits.busy == 1,
          "TPE 3: the blocked transfer leaves the output controller BUSY");
    CHECK(octobus_out_status(card).bits.readyForTransfer == 0,
          "TPE 3: with ready-for-transfer CLEAR");

    /* ---- TPE test 2: Check data transmission ---------------------------
     * Standalone, this is the loopback: what went in comes back out, in
     * order, and the FIFO empties exactly. */
    bool order_ok = true;
    for (int i = 0; i < OCTOBUS_RX_FIFO_WORDS; i++)
    {
        uint16_t got = card->Read(card, 0100400);
        /* The looped-back word: information byte kept, our station as source. */
        if (got != (uint16_t)((unsigned)i | (OCTOBUS_ND100_STATION << 8)))
        {
            order_ok = false;
        }
        if (i == 0)
        {
            /* The first read frees a slot, and that releases the back-pressure
             * the 17th write above ran into (UpdateReceiveFifoStatus:2622-2629). */
            CHECK(octobus_out_status(card).bits.busy == 0,
                  "TPE 2: the first read of a full FIFO clears BUSY");
            CHECK(octobus_out_status(card).bits.readyForTransfer == 1,
                  "TPE 2: and sets ready-for-transfer again");
        }
    }
    CHECK(order_ok, "TPE 2: every word comes back in the order it went in");
    CHECK(octobus_rx_count(card) == 0, "TPE 2: and the FIFO is empty");
    CHECK(!octobus_in_status(card).bits.dataAvailable,
          "TPE 2: which the status bit reports");
    CHECK(octobus_in_status(card).bits.fifoNotFull,
          "TPE 2: along with space being available again");
    /* CHANGED CHECK: this used to demand 0 ("a drained FIFO reads 0"). In the C#
     * a read of +0 with the FIFO empty returns the input data register, which
     * holds the last word popped (NDBusOctobus.cs Read:2679, 2703-2707). The last
     * word popped here is the 16th: information byte 15, source station 1. */
    CHECK(card->Read(card, 0100400) == (uint16_t)(15u | (OCTOBUS_ND100_STATION << 8)),
          "TPE 2: a drained FIFO reads the last word popped, again");
    CHECK(card->Read(card, 0100400) == (uint16_t)(15u | (OCTOBUS_ND100_STATION << 8)),
          "TPE 2: and keeps reading it");

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
     * one bit in one pattern passes any sampled test.
     *
     * CHANGED: the patterns used to go in through +1 and were expected back
     * bit for bit. In the C# +1 does not reach the FIFO (Write:2790-2793); the
     * loopback is a frame written to +5, and what comes back is C and B (bits
     * 15,14) and the information byte (7:0) as written, with the station field
     * (13:8) replaced by our own station (ProcessTransmitQueue:3256-3257). So
     * every written word is checked against that. This card has no bus attached,
     * so every destination loops back. */
    card->Reset(card);
    unsigned long patterns = 0;
    bool          pattern_ok = true;
    for (uint32_t base = 0; base <= 0xFFFFu; base += OCTOBUS_RX_FIFO_WORDS)
    {
        int batch = 0;
        while (batch < OCTOBUS_RX_FIFO_WORDS && (base + (uint32_t)batch) <= 0xFFFFu)
        {
            card->Write(card, 0100405, (uint16_t)(base + (uint32_t)batch));
            batch++;
        }
        for (int i = 0; i < batch; i++)
        {
            uint16_t sent = (uint16_t)(base + (uint32_t)i);
            uint16_t expect = (uint16_t)((sent & 0xC0FFu) | (OCTOBUS_ND100_STATION << 8));
            if (card->Read(card, 0100400) != expect)
            {
                pattern_ok = false;
            }
            patterns++;
        }
    }
    CHECK(pattern_ok, "TPE 2: all 65536 written words survive the loopback, station restamped");
    CHECK(patterns == 65536ul, "TPE 2: and every one of them was tried");

    /* A frame that arrives over the BUS is stored whole, all 16 bits
     * (DeliverInboundFrame:3608-3610), so here every pattern does come back bit
     * for bit. */
    card->Reset(card);
    patterns = 0;
    pattern_ok = true;
    for (uint32_t base = 0; base <= 0xFFFFu; base += OCTOBUS_RX_FIFO_WORDS)
    {
        for (int i = 0; i < OCTOBUS_RX_FIFO_WORDS; i++)
        {
            (void)octobus_rx_push(card, (uint16_t)(base + (uint32_t)i));
        }
        for (int i = 0; i < OCTOBUS_RX_FIFO_WORDS; i++)
        {
            if (card->Read(card, 0100400) != (uint16_t)(base + (uint32_t)i))
            {
                pattern_ok = false;
            }
            patterns++;
        }
    }
    CHECK(pattern_ok, "bus arrival: all 65536 bit patterns come back unchanged");
    CHECK(patterns == 65536ul, "bus arrival: and every one of them was tried");

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
    /* The FIFO is empty now, and +0 reads the input data register: the word just
     * popped (NDBusOctobus.cs Read:2703-2707). */
    CHECK(octobus_rx_count(card) == 0, "TPE-CONF: the FIFO is empty again");
    CHECK(card->Read(card, 0100400) ==
              (uint16_t)((0x00A5u & 0xC0FFu) | (OCTOBUS_ND100_STATION << 8)),
          "TPE-CONF: and +0 then reads the last word popped, not 0");

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
    /* CHANGED: the input event used to be a write to +1. In the C# that write
     * only stores the word (NDBusOctobus.cs Write:2790-2793); the input event is
     * a word ARRIVING, here the echo of a frame sent to station 0. */
    card->Write(card, 0100403, octobus_in_ctrl_int_enable());
    card->Write(card, 0100405, 0x0034u);      /* looped back: a word arrives */
    CHECK((card->interruptBits & (1u << card->interruptLevel)) != 0,
          "TPE-CONF: an input event with the enable set asserts level 13");
    CHECK(card->Ident(card, card->interruptLevel) == 040,
          "TPE-CONF: and IDENT answers 40B for the INPUT controller");
    CHECK((card->interruptBits & (1u << card->interruptLevel)) == 0,
          "TPE-CONF: IDENT clears the request, so the level de-asserts");

    /* One-shot: IDENT clears the ENABLE too, so a second event does not
     * interrupt until the driver re-arms. Without that the level re-fires
     * forever after being serviced. */
    card->Write(card, 0100405, 0x0078u);
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
    (void)octobus_rx_push(card, (uint16_t)(0x8000u | (40u << 8u) | 0x11u)); /* input event */
    card->Write(card, 0100405, (uint16_t)(0x8000u | (40u << 8u)));   /* output event */
    CHECK(card->Ident(card, card->interruptLevel) == 040, "TPE-CONF: the INPUT has priority");
    CHECK(card->Ident(card, card->interruptLevel) == 041, "TPE-CONF: then the output answers");

    /* An event with the enable CLEAR must not assert the level. */
    card->Reset(card);
    card->Write(card, 0100405, 0x0022u);
    CHECK((card->interruptBits & (1u << card->interruptLevel)) == 0,
          "TPE-CONF: an event with interrupts disabled does not assert level 13");
    CHECK(card->Ident(card, card->interruptLevel) == 0, "TPE-CONF: and IDENT answers nothing");

    /* Another level must never be answered. */
    card->Reset(card);
    card->Write(card, 0100403, octobus_in_ctrl_int_enable());
    card->Write(card, 0100405, 0x0033u);
    CHECK(card->Ident(card, 11) == 0, "TPE-CONF: IDENT on level 11 answers nothing");

    /* ---- A WRITE TO +1 ONLY STORES THE WORD ---------------------------------
     * NDBusOctobus.cs Write:2790-2793: the word goes into the input data
     * register and that is all - nothing enters the FIFO and no input event is
     * raised. It is read back at +0 while the FIFO is empty (Read:2703-2707). */
    card->Reset(card);
    card->Write(card, 0100403, octobus_in_ctrl_int_enable());
    card->Write(card, 0100401, 0x1234u);
    CHECK(octobus_rx_count(card) == 0, "+1: a write queues nothing in the receive FIFO");
    CHECK(!octobus_in_status(card).bits.dataAvailable, "+1: and reports no data available");
    CHECK(!octobus_line_up(card), "+1: and raises no input interrupt");
    CHECK(card->Ident(card, card->interruptLevel) == 0, "+1: so IDENT answers nothing");
    CHECK(card->Read(card, 0100400) == 0x1234u,
          "+1: the word is in the input data register, read at +0 with the FIFO empty");

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

    /* ---------------------------------------------------------------------
     * A REPLY LONGER THAN THE 16-WORD FIFO ARRIVES WHOLE, AND IN ORDER.
     *
     * On the real bus a receiver whose FIFO is full answers Ack=10 (destination
     * busy) and the SENDER retries, so nothing is lost. Before this was modelled
     * the card dropped the excess: the ND-500 monitor's RECO (020B) asks for 16
     * words, a 36-frame reply, read a mutilated message, answered with an
     * emergency 244B TERMINATE ACCP and printed "ECO not available".
     *
     * Push 36 distinguishable frames the way the fabric does - all at once, with
     * no read in between - then drain and check every one came back, once, in
     * the order it was sent.
     * ------------------------------------------------------------------- */
    {
        Device *rc = octobus_create_device(0);
        CHECK(rc != NULL, "retry: a card to push a long reply at");

        const int FRAMES = 36;
        int accepted = 0;
        for (int i = 0; i < FRAMES; i++)
        {
            /* The low byte carries the sequence number, so a reordered or
             * duplicated frame is visible and not just a count. */
            if (octobus_rx_push(rc, (uint16_t)(0x8000u | (7u << 8u) | (unsigned)i)))
            {
                accepted++;
            }
        }
        CHECK(accepted == FRAMES, "retry: every frame of a 36-frame reply was accepted");

        /* Only 16 are in the FIFO - the card's depth is still 16, and the status
         * register must say the FIFO is full, not that it has room. */
        CHECK(octobus_rx_count(rc) == 16, "retry: the FIFO itself still holds exactly 16");
        CHECK(octobus_in_status(rc).bits.fifoNotFull == 0, "retry: and reports itself full");
        CHECK(octobus_in_status(rc).bits.dataAvailable == 1, "retry: with data to read");

        /* Drain. Each read frees a slot, which must pull in the next parked
         * frame, so the guest sees all 36 without the sender doing anything. */
        int read_count = 0;
        int order_ok = 1;
        while (octobus_in_status(rc).bits.dataAvailable)
        {
            uint16_t got = rc->Read(rc, 0100400);
            if ((got & 0xFFu) != (unsigned)read_count)
            {
                order_ok = 0;
            }
            read_count++;
            if (read_count > FRAMES + 8)
            {
                break; /* never spin if the pump is wrong */
            }
        }
        CHECK(read_count == FRAMES, "retry: all 36 frames were read back, none dropped");
        CHECK(order_ok == 1, "retry: and in the order they were sent");
        CHECK(octobus_rx_count(rc) == 0, "retry: with nothing left parked or queued");

        /* DEVICE CLEAR (20B to the input control register) abandons the
         * transfer, so the parked frames go with it rather than surfacing in the
         * middle of the next reply. */
        for (int i = 0; i < FRAMES; i++)
        {
            (void)octobus_rx_push(rc, (uint16_t)(0x8000u | (7u << 8u) | (unsigned)i));
        }
        CHECK(octobus_rx_count(rc) == 16, "retry: refilled, 16 in the FIFO");
        rc->Write(rc, 0100403, 020u);
        CHECK(octobus_rx_count(rc) == 0, "retry: device clear empties the FIFO");
        CHECK(octobus_in_status(rc).bits.fifoNotFull == 1,
              "retry: and a cleared card reports space again");
        /* The real check: a read after the clear must not resurrect a parked
         * frame from the abandoned reply. */
        CHECK(octobus_in_status(rc).bits.dataAvailable == 0,
              "retry: with no parked frame surviving the clear");
        (void)rc->Read(rc, 0100400);
        CHECK(octobus_rx_count(rc) == 0, "retry: and a read after it pulls nothing in");
        /* A frame arriving after the clear must land in the FIFO at once. With
         * parked frames left behind it would be parked behind them and never
         * arrive: nothing pops an empty FIFO. */
        CHECK(octobus_rx_push(rc, 0x8755u), "retry: a frame after the clear is accepted");
        CHECK(octobus_rx_count(rc) == 1, "retry: and lands in the FIFO, not behind old frames");
        CHECK(rc->Read(rc, 0100400) == 0x8755u, "retry: and is the next word read");

        /* RESET empties the park too (NDBusOctobus.cs Reset:3376). Same proof. */
        for (int i = 0; i < FRAMES; i++)
        {
            (void)octobus_rx_push(rc, (uint16_t)(0x8000u | (7u << 8u) | (unsigned)i));
        }
        rc->Reset(rc);
        CHECK(octobus_rx_count(rc) == 0, "retry: reset empties the FIFO");
        CHECK(octobus_rx_push(rc, 0x8766u), "retry: a frame after the reset is accepted");
        CHECK(octobus_rx_count(rc) == 1,
              "retry: and lands in the FIFO - reset left no parked frame in front of it");
        CHECK(rc->Read(rc, 0100400) == 0x8766u, "retry: and is the next word read");

        /* Control bit 6, Reset, on the input controller: the same again
         * (ProcessControlChange:2840-2851, park emptied at 2848). */
        for (int i = 0; i < FRAMES; i++)
        {
            (void)octobus_rx_push(rc, (uint16_t)(0x8000u | (7u << 8u) | (unsigned)i));
        }
        rc->Write(rc, 0100403, 0100u);
        CHECK(octobus_rx_count(rc) == 0, "retry: control bit 6 empties the FIFO");
        CHECK(octobus_rx_push(rc, 0x8777u), "retry: a frame after it is accepted");
        CHECK(octobus_rx_count(rc) == 1, "retry: and lands in the FIFO, the park was emptied");

        dev_destroy(rc);
        free(rc);
    }

    /* =====================================================================
     * BEHAVIOUR PORTED FROM NDBusOctobus.cs, ONE BLOCK PER SUBJECT.
     * Each block uses its own card, so no block depends on what another left.
     * ===================================================================== */

    /* ---- ONE INTERRUPT PER FRAME -------------------------------------------
     * Read:2700-2701. Three frames arrive before the first interrupt is served.
     * IDENT clears the request once; without the re-raise on read the driver
     * gets one interrupt, reads one frame and never hears about the other two. */
    {
        Device *mf = octobus_create_device(0);
        CHECK(mf != NULL, "per-frame: a card");
        mf->Write(mf, 0100403, octobus_in_ctrl_int_enable());
        for (unsigned i = 0; i < 3; i++)
        {
            (void)octobus_rx_push(mf, (uint16_t)(0x8000u | (56u << 8u) | i));
        }
        CHECK(octobus_line_up(mf), "per-frame: the arrival asserts level 13");
        CHECK(mf->Ident(mf, 13) == 040, "per-frame: IDENT answers 40B");

        /* The driver re-arms, then reads one frame. Two are left. */
        mf->Write(mf, 0100403, octobus_in_ctrl_int_enable());
        CHECK(!octobus_line_up(mf), "per-frame: re-arming alone does not interrupt");
        CHECK(mf->Read(mf, 0100400) == (uint16_t)(0x8000u | (56u << 8u) | 0u),
              "per-frame: frame 0 is read");
        CHECK(octobus_line_up(mf), "per-frame: the read asserts level 13 again - data is left");
        CHECK(mf->Ident(mf, 13) == 040, "per-frame: and IDENT answers 40B for frame 1");

        mf->Write(mf, 0100403, octobus_in_ctrl_int_enable());
        CHECK(mf->Read(mf, 0100400) == (uint16_t)(0x8000u | (56u << 8u) | 1u),
              "per-frame: frame 1 is read");
        CHECK(octobus_line_up(mf), "per-frame: and again for frame 2");
        CHECK(mf->Ident(mf, 13) == 040, "per-frame: IDENT answers 40B for frame 2");

        /* The last read empties the FIFO: nothing left to ask for. */
        mf->Write(mf, 0100403, octobus_in_ctrl_int_enable());
        CHECK(mf->Read(mf, 0100400) == (uint16_t)(0x8000u | (56u << 8u) | 2u),
              "per-frame: frame 2 is read");
        CHECK(!octobus_line_up(mf), "per-frame: the read that empties the FIFO does not interrupt");
        CHECK(mf->Ident(mf, 13) == 0, "per-frame: and IDENT answers nothing");

        dev_destroy(mf);
        free(mf);
    }

    /* ---- THE INPUT DATA REGISTER AND THE STORED SOURCE STATION --------------
     * DeliverInboundFrame:3608-3616: a frame from the bus goes into the FIFO,
     * into the input data register, and its source station into bits 13:8 of the
     * stored input status. A READ of +2 still shows the card's own station there
     * (Read:2729), so the stored values are looked at in the device data. */
    {
        Device *ir = octobus_create_device(0);
        CHECK(ir != NULL, "input register: a card");
        OctobusData *ird = (OctobusData *)ir->deviceData;

        uint16_t frame = (uint16_t)(0x8000u | (56u << 8u) | 0x42u);
        (void)octobus_rx_push(ir, frame);
        CHECK(ird->inputData == frame, "input register: a bus frame loads the register");
        CHECK(ird->statusRegister.bits.station == 56,
              "input register: and its source station goes into the stored status");
        CHECK(octobus_in_status(ir).bits.station == OCTOBUS_ND100_STATION,
              "input register: while +2 still reads the card's OWN station");
        CHECK(ird->statusRegister.bits.station == 56,
              "input register: and reading +2 does not change the stored one");

        /* A PARKED frame has not landed: nothing is updated until it does
         * (DeliverInboundFrame:3598; PumpBusyRetryQueue:3483-3487). */
        ir->Reset(ir);
        for (unsigned i = 0; i < OCTOBUS_RX_FIFO_WORDS; i++)
        {
            (void)octobus_rx_push(ir, (uint16_t)(0x8000u | (7u << 8u) | i));
        }
        uint16_t parked = (uint16_t)(0x8000u | (9u << 8u) | 0x99u);
        (void)octobus_rx_push(ir, parked);
        CHECK(ird->statusRegister.bits.station == 7,
              "input register: a parked frame does not touch the stored source station");
        CHECK(ird->inputData == (uint16_t)(0x8000u | (7u << 8u) | 15u),
              "input register: nor the input data register");
        (void)ir->Read(ir, 0100400);
        CHECK(ird->statusRegister.bits.station == 9,
              "input register: when it lands, its source station is stored");
        CHECK(ird->inputData == parked, "input register: and it is in the input data register");

        /* The write to +5 loads the register only while it holds 0
         * (ProcessCommand:3004-3007). A frame to an absent station gives no echo
         * and no reply, so nothing else loads it. */
        MockBus empty;
        memset(&empty, 0, sizeof(empty));
        ir->Reset(ir);
        octobus_set_transmit(ir, mock_transmit, &empty);
        uint16_t first = (uint16_t)(0x8000u | (63u << 8u) | 0x21u);
        uint16_t second = (uint16_t)(0x8000u | (62u << 8u) | 0x22u);
        ir->Write(ir, 0100405, first);
        CHECK(octobus_rx_count(ir) == 0, "input register: an absent station echoes nothing");
        CHECK(ir->Read(ir, 0100400) == first,
              "input register: +5 loads it while it holds 0, and +0 reads it back");
        ir->Write(ir, 0100405, second);
        CHECK(ir->Read(ir, 0100400) == first,
              "input register: a later write to +5 does not replace a non-zero value");

        /* The input clear zeroes it (ProcessControlChange:2916). */
        ir->Write(ir, 0100403, 020u);
        CHECK(ir->Read(ir, 0100400) == 0, "input register: the input clear zeroes it");

        dev_destroy(ir);
        free(ir);
    }

    /* ---- BUSY AND THE OUTPUT INTERRUPT LINE --------------------------------
     * A loopback echo into a full FIFO leaves the output BUSY
     * (ProcessTransmitQueue:3263-3276). While BUSY is set the output controller
     * cannot assert the line (UpdateInterruptState:2550-2552). The first read
     * that frees a slot clears BUSY, sets READY and latches the output request
     * (UpdateReceiveFifoStatus:2622-2629). */
    {
        Device *bp = octobus_create_device(0);
        CHECK(bp != NULL, "busy: a card");

        /* Fill the FIFO with the output interrupt DISABLED. Every completed
         * transfer latches the output request, so it is pending from here on. */
        for (unsigned i = 0; i < OCTOBUS_RX_FIFO_WORDS; i++)
        {
            bp->Write(bp, 0100405, (uint16_t)i);
        }
        CHECK(octobus_out_status(bp).bits.busy == 0, "busy: 16 words fit, the output is not busy");

        bp->Write(bp, 0100405, 0x00EEu); /* the 17th: no room */
        CHECK(octobus_out_status(bp).bits.busy == 1, "busy: the 17th word sets BUSY");
        CHECK(octobus_out_status(bp).bits.readyForTransfer == 0, "busy: and clears READY");
        CHECK(octobus_rx_count(bp) == OCTOBUS_RX_FIFO_WORDS, "busy: the FIFO still holds 16");
        CHECK(bp->Read(bp, 0100404) == 0x00EEu,
              "busy: the blocked word is in the output data register");

        /* The request is pending and now the enable is set too - but BUSY holds
         * the line down. */
        bp->Write(bp, 0100407, octobus_out_ctrl_int_enable());
        CHECK(!octobus_line_up(bp), "busy: a BUSY output does not assert level 13");

        /* One read frees a slot. */
        (void)bp->Read(bp, 0100400);
        CHECK(octobus_out_status(bp).bits.busy == 0, "busy: a read that frees a slot clears BUSY");
        CHECK(octobus_out_status(bp).bits.readyForTransfer == 1, "busy: and sets READY");
        CHECK(octobus_line_up(bp), "busy: and the output interrupt now asserts level 13");
        CHECK(bp->Ident(bp, 13) == 041, "busy: IDENT answers 41B, the output controller");
        /* The blocked word itself is gone: the C# releases the status and does
         * not send the frame again (3275 returns before anything is queued). */
        CHECK(octobus_rx_count(bp) == OCTOBUS_RX_FIFO_WORDS - 1,
              "busy: the blocked word was not delivered afterwards");

        /* The release is itself the event. Get to BUSY with NO request pending:
         * fill the last slot, let IDENT take that request, then block again. */
        bp->Write(bp, 0100407, octobus_out_ctrl_int_enable());
        bp->Write(bp, 0100405, 0x0011u);
        CHECK(bp->Ident(bp, 13) == 041, "busy: the 16th word completes and is acknowledged");
        bp->Write(bp, 0100405, 0x0012u); /* blocked again, nothing pending */
        CHECK(octobus_out_status(bp).bits.busy == 1, "busy: blocked again");
        bp->Write(bp, 0100407, octobus_out_ctrl_int_enable());
        CHECK(!octobus_line_up(bp), "busy: nothing pending and BUSY - the line is down");
        (void)bp->Read(bp, 0100400);
        CHECK(octobus_line_up(bp), "busy: the release latches the output request by itself");
        CHECK(bp->Ident(bp, 13) == 041, "busy: and IDENT answers 41B");

        dev_destroy(bp);
        free(bp);
    }

    /* ---- THE CONTROL WORDS +3 AND +7 ---------------------------------------
     * ProcessControlChange:2837-2952. The order of the steps there decides what
     * a word with several bits set does: bit 6 Reset, bit 7 TestMode, bit 0
     * enable, bit 4 clear, bit 5 ContinueACCP. */
    {
        Device *cw = octobus_create_device(0);
        CHECK(cw != NULL, "control: a card");
        OctobusData *cwd = (OctobusData *)cw->deviceData;

        /* 21 octal = enable + clear. The clear comes AFTER the enable and
         * replaces the whole status, so the enable ends CLEAR - on both
         * controllers (enable at 2879-2904, clear at 2911-2931). */
        cw->Write(cw, 0100407, 021u);
        CHECK(cw->Read(cw, 0100406) == 010u,
              "control: 21B to +7 leaves READY only - the enable does not survive the clear");
        cw->Write(cw, 0100403, 021u);
        CHECK(cw->Read(cw, 0100402) == (uint16_t)((OCTOBUS_ND100_STATION << 8) | 04u),
              "control: 21B to +3 leaves FIFO-not-full and the own station, enable clear");

        /* 101 octal = enable + Reset. Reset comes BEFORE the enable (2840), so
         * here the enable ends SET. */
        cw->Write(cw, 0100407, 0101u);
        CHECK(cw->Read(cw, 0100406) == 011u, "control: 101B to +7 leaves READY and the enable");
        cw->Write(cw, 0100403, 0101u);
        CHECK(cw->Read(cw, 0100402) == (uint16_t)((OCTOBUS_ND100_STATION << 8) | 05u),
              "control: 101B to +3 leaves FIFO-not-full and the enable");

        /* Bit 6 alone clears its controller like bit 4 does (2842-2858). */
        MockBus none;
        memset(&none, 0, sizeof(none));
        cw->Reset(cw);
        octobus_set_transmit(cw, mock_transmit, &none);
        cw->Write(cw, 0100405, (uint16_t)(0x8000u | (63u << 8u)));
        CHECK(octobus_out_status(cw).bits.error == 1 && octobus_out_status(cw).bits.notPresent == 1,
              "control: a frame nobody answers leaves ERROR and NOT PRESENT");
        cw->Write(cw, 0100407, 0100u);
        CHECK(cw->Read(cw, 0100406) == 010u, "control: bit 6 to +7 leaves READY and nothing else");
        CHECK(cw->Read(cw, 0100404) == 0, "control: and zeroes the output data register");

        (void)octobus_rx_push(cw, 0x8701u);
        (void)octobus_rx_push(cw, 0x8702u);
        cw->Write(cw, 0100403, 0100u);
        CHECK(octobus_rx_count(cw) == 0, "control: bit 6 to +3 empties the receive FIFO");
        CHECK(cw->Read(cw, 0100402) == (uint16_t)((OCTOBUS_ND100_STATION << 8) | 04u),
              "control: and leaves FIFO-not-full and the own station");
        CHECK(cw->Read(cw, 0100400) == 0, "control: and zeroes the input data register");

        /* Bit 5, ContinueACCP (2943-2951): the installed function is called once
         * per write that carries the bit, from either control register. */
        unsigned long calls = 0;
        cw->Write(cw, 0100403, 040u); /* nothing installed: must not fault */
        CHECK(cwd->mudomDetected, "control: bit 5 sets the MUDOM flag even with nothing installed");
        octobus_set_continue_accp(cw, count_continue_accp, &calls);
        cw->Write(cw, 0100403, 040u);
        CHECK(calls == 1, "control: bit 5 to +3 calls the continue-ACCP function");
        cw->Write(cw, 0100407, 040u);
        CHECK(calls == 2, "control: bit 5 to +7 calls it too");
        cw->Write(cw, 0100407, 0u);
        cw->Write(cw, 0100403, 020u);
        CHECK(calls == 2, "control: a word without bit 5 does not call it");
        octobus_set_continue_accp(cw, NULL, NULL);
        cw->Write(cw, 0100407, 040u);
        CHECK(calls == 2, "control: and nothing is called after the function is removed");

        dev_destroy(cw);
        free(cw);
    }

    /* ---- A CPU STATION ATTACHED: NO ECHO OF A SELF-SEND --------------------
     * ProcessTransmitQueue:3239, 3288-3306. A frame to station 0 or to the own
     * station is echoed while the card is in loopback mode or no CPU station is
     * attached. With one attached, the transfer completes and nothing comes
     * back. Control bit 7, TestMode, turns loopback on again (2868-2874), and a
     * reset turns it on only when no CPU station is attached (Reset:3370). */
    {
        Device *ca = octobus_create_device(0);
        CHECK(ca != NULL, "cpu attached: a card");
        OctobusData *cad = (OctobusData *)ca->deviceData;
        MockBus      cabus;
        memset(&cabus, 0, sizeof(cabus));
        cabus.present[56] = true;
        octobus_set_transmit(ca, mock_transmit, &cabus);

        CHECK(cad->loopbackMode, "cpu attached: a new card is in loopback mode");
        ca->Write(ca, 0100405, 0x00A5u);
        CHECK(octobus_rx_count(ca) == 1, "cpu attached: with no CPU station a dest-0 frame echoes");
        (void)ca->Read(ca, 0100400);

        octobus_set_cpu_attached(ca, true);
        CHECK(!cad->loopbackMode, "cpu attached: attaching turns loopback mode off");
        ca->Write(ca, 0100407, octobus_out_ctrl_int_enable());
        ca->Write(ca, 0100405, 0x00A5u);
        CHECK(octobus_rx_count(ca) == 0, "cpu attached: a dest-0 frame is NOT echoed");
        CHECK(cabus.sent == 0, "cpu attached: and does not go onto the bus either");
        CHECK(octobus_out_status(ca).bits.readyForTransfer == 1,
              "cpu attached: the transfer still completes");
        CHECK(octobus_out_status(ca).bits.busy == 0, "cpu attached: with BUSY clear");
        CHECK(ca->Ident(ca, 13) == 041, "cpu attached: and raises the output interrupt");
        ca->Write(ca, 0100405, (uint16_t)(0x8000u | ((uint32_t)OCTOBUS_ND100_STATION << 8u)));
        CHECK(octobus_rx_count(ca) == 0, "cpu attached: a frame to our own station is not echoed");
        CHECK(cabus.sent == 0, "cpu attached: and stays off the bus");

        /* A real destination still goes to the bus and is answered. */
        ca->Write(ca, 0100405, (uint16_t)(0x8000u | (56u << 8u)));
        CHECK(cabus.sent == 1, "cpu attached: a real destination goes onto the bus");
        CHECK(octobus_rx_count(ca) == 1, "cpu attached: and its reply arrives");
        (void)ca->Read(ca, 0100400);

        /* TestMode on either control register turns the echo back on. */
        ca->Write(ca, 0100407, 0200u);
        CHECK(cad->loopbackMode, "cpu attached: control bit 7 turns loopback mode on");
        ca->Write(ca, 0100405, 0x00A5u);
        CHECK(octobus_rx_count(ca) == 1, "cpu attached: and a dest-0 frame echoes again");

        /* Reset: loopback follows the attached flag. */
        ca->Reset(ca);
        CHECK(!cad->loopbackMode, "cpu attached: reset leaves loopback OFF while a CPU is attached");
        ca->Write(ca, 0100405, 0x00A5u);
        CHECK(octobus_rx_count(ca) == 0, "cpu attached: so a dest-0 frame is not echoed after it");
        ca->Write(ca, 0100403, 0200u);
        CHECK(cad->loopbackMode, "cpu attached: control bit 7 works from +3 as well");

        octobus_set_cpu_attached(ca, false);
        ca->Reset(ca);
        CHECK(cad->loopbackMode, "cpu attached: detached and reset, loopback is on again");
        ca->Write(ca, 0100405, 0x00A5u);
        CHECK(octobus_rx_count(ca) == 1, "cpu attached: and a dest-0 frame echoes");

        dev_destroy(ca);
        free(ca);
    }

    dev_destroy(card);
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
