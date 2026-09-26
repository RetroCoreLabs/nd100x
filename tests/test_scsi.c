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
 * path does one PIO word through them; nothing here needs real ND memory, and
 * linking the CPU in would mean a failure could come from anywhere. */
int  g_dma_access = 0;
int  mms_read_physical_memory(int addr, bool priv) { (void)addr; (void)priv; return 0; }
void mms_write_physical_memory(int addr, uint16_t v, bool priv)
{
    (void)addr;
    (void)v;
    (void)priv;
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

    printf("\n%d checks, %d failed\n", s_checks, s_failed);
    return s_failed == 0 ? 0 : 1;
}
