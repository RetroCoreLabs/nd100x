/*
 * test_scsi.c - the ND-3201 / ND-3204 SCSI controller's control word.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Two control-word bits were found missing against RetroCore
 * NDBusDiscControllerSCSI.cs and are what this file pins down:
 *
 *   bit 10 (Reset SCSI bus) must leave the controller ready for transfer.
 *          Without it the card comes out of SCSI_Reset with ready false and
 *          only bit 4 can ever set it, so a driver that resets the bus and
 *          polls RSTAU bit 3 waits forever.
 *   bit 2  (Activate) must restart the DMA byte counters, because their
 *          PARITY chooses the high or low half of the ND word. A stale odd
 *          count makes the next transfer's first byte land in the low half.
 *
 * device_scsi.c is INCLUDED rather than linked: dma_bytes_read and
 * dma_bytes_written live in SCSIData, which is file-local, and the parity bug
 * is not observable from outside the card without a full NCR transfer. Reading
 * the counters directly tests the actual fix rather than a proxy for it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "scsi/device_scsi.c"

/* Stubs for what device.c's DMA helpers reference. The control word's test-mode
 * path does one PIO word through them, and the autoload test reads back what
 * the card planted, so the stub keeps a small ND memory (words 0-7777 octal).
 * Linking the CPU in would mean a failure could come from anywhere. Writes
 * outside the window are counted, so a stray address cannot pass silently. */
#define TEST_MEM_WORDS 010000
static uint16_t s_mem[TEST_MEM_WORDS];
static int s_mem_out_of_range = 0;

int g_dma_access = 0;
int mms_read_physical_memory(int addr, bool priv)
{
    (void)priv;
    if (addr < 0 || addr >= TEST_MEM_WORDS)
    {
        s_mem_out_of_range++;
        return 0;
    }
    return s_mem[addr];
}
void mms_write_physical_memory(int addr, uint16_t v, bool priv)
{
    (void)priv;
    if (addr < 0 || addr >= TEST_MEM_WORDS)
    {
        s_mem_out_of_range++;
        return;
    }
    s_mem[addr] = v;
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

/* The control word and the status word, by their offsets from the card's base. */
#define WCONT_OFFSET 5
#define RSTAU_OFFSET 4

static uint16_t scsi_read_status(Device *card)
{
    return card->Read(card, (int)(card->startAddress + RSTAU_OFFSET));
}

static void scsi_write_ctrl(Device *card, uint16_t value)
{
    card->Write(card, (int)(card->startAddress + WCONT_OFFSET), value);
}

int main(void)
{
    printf("ND-100 SCSI controller control-word tests\n");
    printf("=========================================\n\n");

    Device *card = scsi_create_device(0);
    CHECK(card != NULL, "a card is created");
    if (card == NULL)
    {
        return 1;
    }

    /* The thumbwheel decode, so a wrong base would not be mistaken for a
     * control-word failure below. */
    CHECK(card->startAddress == 0144300, "thumbwheel 0 puts the card at 144300");
    CHECK(card->identCode == 0140440, "with ident code 140440B");
    CHECK(card->interruptLevel == 11, "on interrupt level 11");

    SCSIData *data = (SCSIData *)card->deviceData;

    /* ---- bit 10: Reset SCSI bus leaves the controller ready ---- */

    scsi_reset(card);
    CHECK((scsi_read_status(card) & SCSI_STAT_READY_FOR_TRANSFER) == 0,
          "after master clear the card is NOT ready for transfer");

    scsi_write_ctrl(card, SCSI_CTRL_RESET_SCSI_BUS);
    CHECK(data->resetOnSCSIBus, "control word bit 10 is recorded");
    CHECK((scsi_read_status(card) & SCSI_STAT_READY_FOR_TRANSFER) != 0,
          "resetting the SCSI bus leaves the card ready for transfer");

    /* Clear Device does the same, and did so before the fix - checked so that a
     * regression can be told apart from the bit-10 path. */
    scsi_reset(card);
    scsi_write_ctrl(card, SCSI_CTRL_CLEAR_DEVICE);
    CHECK((scsi_read_status(card) & SCSI_STAT_READY_FOR_TRANSFER) != 0,
          "Clear Device also leaves the card ready for transfer");

    /* ---- any control-word write leaves the card ready, and interrupts ----
     *
     * This is the sequence TPE CONFIGURATION D05 actually performs: control
     * word 1 (enable interrupt, activate CLEAR), then a poll of RSTAU bit 3.
     * Before the fix the card answered 1 (interrupt-enabled only) forever and
     * TPE printed "Device never ready for transfer / Expected identcode
     * 140440B". */

    scsi_reset(card);
    scsi_write_ctrl(card, SCSI_CTRL_ENABLE_INTERRUPT);
    CHECK((scsi_read_status(card) & SCSI_STAT_READY_FOR_TRANSFER) != 0,
          "TPE: control word 1 leaves the card ready for transfer");
    CHECK((card->interruptBits & (1u << 11)) != 0,
          "TPE: and raises the level 11 interrupt");
    CHECK(card->Ident(card, 11) == 0140440,
          "TPE: IDENT on level 11 answers 140440B");
    CHECK((card->interruptBits & (1u << 11)) == 0,
          "TPE: the ident clears the interrupt request");

    /* Activate wins: a transfer in progress is not ready for transfer. */
    scsi_reset(card);
    scsi_write_ctrl(card, (uint16_t)(SCSI_CTRL_ENABLE_INTERRUPT | SCSI_CTRL_ACTIVATE));
    CHECK((scsi_read_status(card) & SCSI_STAT_READY_FOR_TRANSFER) == 0,
          "a control word with activate set is NOT ready for transfer");

    /* ---- bit 2: Activate restarts the DMA byte counters ---- */

    scsi_reset(card);
    CHECK(data->dma_bytes_read == 0 && data->dma_bytes_written == 0,
          "master clear zeroes both DMA byte counters");

    /* An ODD count is the one that matters: parity picks the word half. */
    data->dma_bytes_read    = 7;
    data->dma_bytes_written = 3;

    scsi_write_ctrl(card, SCSI_CTRL_ACTIVATE);
    CHECK(data->active, "control word bit 2 activates the card");
    CHECK(!data->readyForTransfer, "activating clears ready-for-transfer");
    CHECK(data->dma_bytes_read == 0, "activating restarts the DMA read counter");
    CHECK(data->dma_bytes_written == 0, "activating restarts the DMA write counter");

    /* A control word WITHOUT activate must leave the counters alone: they
     * belong to the run that GO starts, not to every register write. */
    data->dma_bytes_read    = 5;
    data->dma_bytes_written = 5;
    scsi_write_ctrl(card, SCSI_CTRL_ENABLE_INTERRUPT);
    CHECK(data->dma_bytes_read == 5 && data->dma_bytes_written == 5,
          "a control word without activate does not touch the counters");

    /* ---- Boot: the card's autoload plants the ROM's ND-100 SCSI driver ----
     *
     * Z80 ROM 45900E handler 0x15B6: ROM 0x15DF-0x1A86 -> ND 6000-7123, and
     * ROM 0x15DB-0x15DE (125001 006035, JMP I *+1 to 6035) -> ND 0-1. It reads
     * nothing from the disc: no disc callback is attached here on purpose.
     * The expected values were read from the ROM dump at the offsets named,
     * not from the table under test. */
    for (int i = 0; i < TEST_MEM_WORDS; i++)
    {
        s_mem[i] = 0177777;
    }
    s_mem_out_of_range = 0;
    data->unitType[0] = SCSI_UNIT_HDD;

    CHECK(card->Boot(card, 0) == 0, "boot returns start address 0");
    CHECK(s_mem[0] == 0125001, "ND 0 = 125001 (JMP I *+1), ROM 0x15DB");
    CHECK(s_mem[1] == 0006035, "ND 1 = 006035 (driver entry), ROM 0x15DD");
    CHECK(s_mem[2] == 0177777, "ND 2 is not written");
    CHECK(s_mem[05777] == 0177777, "ND 5777, just below the driver, is not written");
    CHECK(s_mem[06000] == 0, "ND 6000 = 000000, ROM 0x15DF");
    CHECK(s_mem[06014] == 0144300, "ND 6014 = 144300 (SCSI bus 1 base), ROM 0x15F7");
    CHECK(s_mem[06035] == 0044052, "ND 6035 = 044052 (LDA 6107), ROM 0x1619");
    CHECK(s_mem[06036] == 0146153, "ND 6036 = 146153 (COPY SA DB), ROM 0x161B");
    CHECK(s_mem[06264] == 0150415, "ND 6264 = 150415 (IOXT), ROM 0x1747");
    CHECK(s_mem[07123] == 0005130, "ND 7123 = 005130, last word, ROM 0x1A85");
    CHECK(s_mem[07124] == 0177777, "ND 7124, just past the driver, is not written");
    CHECK(s_mem_out_of_range == 0, "boot writes nothing outside ND 0-7777");

    printf("\n%d checks, %d failed\n", s_checks, s_failed);
    return s_failed == 0 ? 0 : 1;
}
