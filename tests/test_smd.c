/*
 * test_smd.c - the SMD controller's memory address and word count registers.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * What the 10/15 MHz card (ND-632, cards 3043/3044) does with these two
 * registers, from its logic diagram, card 3044 sheet 3 (ND-11.020.01, scan
 * page 118), simulated gate by gate in the nd-120 repository
 * (Logisim/ND-BUS-DEVICES/SMD-3044):
 *
 *   - ONE flip-flop (4E) serves both registers, writes and reads. It toggles
 *     on every load memory address (+1), load word count (+7), read memory
 *     address and read word count (+0); not on the control word, the block
 *     address or the ECC control register. Device clear, status read and the
 *     end of a transfer reset it.
 *   - The FIRST access of a pair loads all 24 bits: bits 16-23 get A bits
 *     0-7 and bits 0-15 get A. The second loads bits 0-15 only.
 *   - Reads: first bits 0-15, then bits 16-23.
 *
 * The check that matters most is the ND-120 mass storage load (microcode
 * routine MASS, CSA 002217-002235 in the ND-120-DELILAH-L listing): it writes
 * +1 twice with 0, +3 with 0, +7 ONCE with 002000 and +5 with 4. On the card
 * that single +7 write leaves a word count of 1024 words, and '21540&' boots.
 * nd100x's --boot=smd reads the boot blocks directly and never makes these
 * writes, so this test is the only place the path is exercised.
 *
 * device_smd.c is INCLUDED rather than linked so the registers in the
 * file-local SMDData can be checked directly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "smd/device_smd.c"

/* The device tick clock: the CPU counts executed instructions here, and the
 * drive model reads it to know where the turning pack is. */
uint64_t g_instr_counter = 0;

/* Stubs for what device.c's DMA helpers reference. Nothing here transfers
 * data, and linking the CPU in would mean a failure could come from anywhere. */
int g_dma_access = 0;
int mms_read_physical_memory(int addr, bool priv)
{
    (void)addr;
    (void)priv;
    return 0;
}
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

/* Register offsets from the card's base (ND-11.020.01 sec 2). */
#define R_MAR      0 /* read memory address / word count (multiplex bit)  */
#define W_MAR      1 /* load memory address                                */
#define W_BLOCK    3 /* load block address I / II                          */
#define R_STATUS   4 /* read status (multiplex bit 0) - resets the flip-flop */
#define W_CONTROL  5 /* load control word                                  */
#define W_WC       7 /* load word count / ECC control (multiplex bit)      */

#define CW_MULTIPLEX 0100000

static Device *s_card;

static void wr(int reg, uint16_t v)
{
    s_card->Write(s_card, (int)(s_card->startAddress + reg), v);
}

static uint16_t rd(int reg)
{
    return (uint16_t)s_card->Read(s_card, (int)(s_card->startAddress + reg));
}

/* Status read with the multiplex bit clear: resets the flip-flop. */
static void reset_flip_flop(void)
{
    wr(W_CONTROL, 0);
    (void)rd(R_STATUS);
}

int main(void)
{
    printf("ND-100 SMD controller memory address / word count tests\n");
    printf("=======================================================\n\n");

    s_card = smd_create_device(0);
    CHECK(s_card != NULL, "a card is created");
    if (!s_card)
    {
        return 1;
    }
    SMDData *data = (SMDData *)s_card->deviceData;
    CHECK(data->regs.hasFlipFlops, "the default card is a flip-flop (10/15 MHz) card");

    /* ---- the ND-120 mass storage load: +1 0, +1 0, +3 0, +7 002000 ---- */
    reset_flip_flop();
    data->regs.wordCounter = 0177777;     /* make stale contents visible */
    data->regs.wordCounterHI = 0377;
    wr(W_MAR, 0);
    wr(W_MAR, 0);
    wr(W_BLOCK, 0);
    wr(W_WC, 002000);
    CHECK(data->regs.coreAddress == 0 && data->regs.coreAddressHiBits == 0,
          "MASS: memory address is 0");
    CHECK(data->regs.wordCounter == 002000, "MASS: word count bits 0-15 = 002000");
    CHECK(data->regs.wordCounterHI == 0, "MASS: word count bits 16-23 = 0 (1024 words)");

    /* Read it back the way the card gives it: status read first, then the
     * multiplex bit, then two reads (low, high). */
    reset_flip_flop();
    wr(W_CONTROL, CW_MULTIPLEX);
    CHECK(rd(R_MAR) == 002000, "read word count: first read = bits 0-15 = 002000");
    CHECK((rd(R_MAR) & 0377) == 0, "read word count: second read = bits 16-23 = 0");

    /* ---- the first write loads all 24 bits ---------------------------- */
    reset_flip_flop();
    wr(W_WC, 000401);
    CHECK(data->regs.wordCounterHI == 001, "first write: bits 16-23 = A bits 0-7");
    CHECK(data->regs.wordCounter == 000401, "first write: bits 0-15 = A");
    wr(W_WC, 000123);
    CHECK(data->regs.wordCounter == 000123, "second write: bits 0-15 = A");
    CHECK(data->regs.wordCounterHI == 001, "second write: bits 16-23 unchanged");

    /* ---- SINTRAN's pattern: high then low, as a pair ------------------- */
    reset_flip_flop();
    wr(W_MAR, 0);
    wr(W_MAR, 0172000);
    CHECK(data->regs.coreAddressHiBits == 0 && data->regs.coreAddress == 0172000,
          "pair: memory address = 00 172000");

    /* ---- one flip-flop for both registers ---------------------------- */
    reset_flip_flop();
    data->regs.wordCounterHI = 0055;
    wr(W_MAR, 000012);                    /* first access */
    wr(W_WC, 000034);                     /* so this is the SECOND: low 16 only */
    CHECK(data->regs.coreAddressHiBits == 012, "shared: +1 was the first access");
    CHECK(data->regs.wordCounter == 000034, "shared: +7 after +1 loads bits 0-15");
    CHECK(data->regs.wordCounterHI == 0055, "shared: +7 after +1 leaves bits 16-23");

    /* ---- the ECC control register does not move the flip-flop --------- */
    reset_flip_flop();
    wr(W_CONTROL, CW_MULTIPLEX);
    wr(W_WC, 000011);                     /* ECC control, not the word count */
    wr(W_CONTROL, 0);
    wr(W_WC, 001000);
    CHECK(data->regs.wordCounterHI == 0 && data->regs.wordCounter == 001000,
          "ECC control write leaves the next +7 a first access");

    /* ---- a status read resets it, the control word does not ---------- */
    reset_flip_flop();
    data->regs.wordCounterHI = 0066;
    wr(W_MAR, 0);                         /* now the next access is a second */
    wr(W_CONTROL, 0);                     /* control word: no reset */
    wr(W_WC, 000777);
    CHECK(data->regs.wordCounterHI == 0066 && data->regs.wordCounter == 000777,
          "control word write does not reset the flip-flop");
    (void)rd(R_STATUS);                   /* status read: reset */
    wr(W_WC, 000402);
    CHECK(data->regs.wordCounterHI == 002, "after a status read the next +7 is a first access");

    printf("\n%d checks, %d failed\n", s_checks, s_failed);
    printf("TB_RESULT: %s\n", s_failed == 0 ? "PASS" : "FAIL");
    return s_failed == 0 ? 0 : 1;
}
