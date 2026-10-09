/*
 * test_eth_irq.c - Behaviour of the Ethernet II card's 68000 glue (plan Phase 3).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Each test drives eth_m68k (Musashi + eth_irq) through a small 68000 program
 * in a fake 64 KB memory; addresses at or above 0x10000 bus-error. The
 * behaviours are the ones RetroCore implements for this card (commit 935163f):
 * Cpu68K.Interrupts.cs (controller, IACK), Cpu68K.cs (clock, reset),
 * Instructionset.cs:342 (NOP = 4 cycles). Test names carry the plan's
 * behaviour id (I1 ...).
 *
 * Memory map of the test program:
 *   0x0000 vectors        SSP 0x1000, PC 0x400
 *   0x0400 main           move.w #0x2000,sr ; then the code under test
 *   0x0600 + 16*n         handler n: addq.b #1,(0x0700+n).l ; rte
 *   0x0700 + n            counter bytes written by handler n
 * Handlers: 1-7 = autovector levels 1-7, 8 = spurious (vector 24),
 * 9 = vector 0x40, 10 = uninitialised interrupt (vector 15), 11 = bus error.
 */

#include "eth_test.h"
#include "eth_m68k.h"
#include "m68k.h"

#include <string.h>

static uint8_t s_mem[0x10000];
static EthM68k s_cpu;
static EthIackType s_ack_type[8];
static uint8_t s_ack_vector[8];
static int s_ack_count[8];
static uint8_t s_mask_at_ack[8];
static int s_reassert_level;

static uint8_t rd(void *ctx, uint32_t a)
{
    (void)ctx;
    if (a >= sizeof s_mem)
    {
        eth_m68k_bus_error(&s_cpu, a, true);
    }
    return s_mem[a & 0xFFFFu];
}

static void wr(void *ctx, uint32_t a, uint8_t v)
{
    (void)ctx;
    if (a >= sizeof s_mem)
    {
        eth_m68k_bus_error(&s_cpu, a, false);
    }
    s_mem[a & 0xFFFFu] = v;
}

static EthIackType iack(void *ctx, int level, uint8_t *vector)
{
    (void)ctx;
    s_ack_count[level]++;
    s_mask_at_ack[level] = s_cpu.irq.pending_mask;
    if (level == s_reassert_level)
    {
        eth_m68k_set_irq(&s_cpu, level, true);
    }
    *vector = s_ack_vector[level];
    return s_ack_type[level];
}

static void w16(uint32_t a, uint32_t v)
{
    s_mem[a] = (uint8_t)(v >> 8u);
    s_mem[a + 1u] = (uint8_t)v;
}

static void w32(uint32_t a, uint32_t v)
{
    w16(a, v >> 16u);
    w16(a + 2u, v & 0xFFFFu);
}

static int counter(int n)
{
    return s_mem[0x700 + n];
}

static uint32_t handler(int n)
{
    return 0x600u + (16u * (uint32_t)n);
}

/* Build the program; main_code is placed after "move.w #0x2000,sr". */
static void setup(const uint16_t *main_code, int words)
{
    EthM68kBus bus = {NULL, rd, wr, iack, NULL};
    uint32_t a = 0x400u;

    memset(s_mem, 0, sizeof s_mem);
    memset(s_ack_type, 0, sizeof s_ack_type);
    memset(s_ack_vector, 0, sizeof s_ack_vector);
    memset(s_ack_count, 0, sizeof s_ack_count);
    memset(s_mask_at_ack, 0, sizeof s_mask_at_ack);
    s_reassert_level = 0;

    w32(0u, 0x1000u);
    w32(4u, 0x400u);
    w32(2u * 4u, handler(11));
    for (int lvl = 1; lvl <= 7; lvl++)
    {
        w32((24u + (uint32_t)lvl) * 4u, handler(lvl));
    }
    w32(24u * 4u, handler(8));
    w32(0x40u * 4u, handler(9));
    w32(15u * 4u, handler(10));
    for (int n = 1; n <= 11; n++)
    {
        w16(handler(n), 0x5239u);              /* addq.b #1,(abs).l */
        w32(handler(n) + 2u, 0x700u + (uint32_t)n);
        w16(handler(n) + 6u, 0x4E73u);         /* rte */
    }
    w16(a, 0x46FCu);                           /* move.w #0x2000,sr */
    w16(a + 2u, 0x2000u);
    a += 4u;
    for (int i = 0; i < words; i++)
    {
        w16(a, main_code[i]);
        a += 2u;
    }
    (void)eth_m68k_init(&s_cpu, &bus);
    eth_m68k_reset(&s_cpu);
}

static void ticks(int n)
{
    for (int i = 0; i < n; i++)
    {
        eth_m68k_tick(&s_cpu, false);
    }
}

static const uint16_t LOOP[] = {0x60FEu}; /* bra.s self */

ETH_TEST(Port, I0_OneInstructionPerTick_NopTakesFourTicks)
{
    /* nop ; moveq #1,d0 ; moveq #2,d0 ; bra.s self */
    static const uint16_t code[] = {0x4E71u, 0x7001u, 0x7002u, 0x60FEu};
    setup(code, 4);
    ticks(1); /* move.w #0x2000,sr */
    CHECK_EQ(eth_m68k_get_reg(&s_cpu, M68K_REG_PC), 0x404);
    ticks(1); /* nop starts */
    CHECK_EQ(eth_m68k_get_reg(&s_cpu, M68K_REG_PC), 0x406);
    ticks(3); /* the nop's three remaining ticks */
    CHECK_EQ(eth_m68k_get_reg(&s_cpu, M68K_REG_PC), 0x406);
    ticks(1); /* moveq #1 */
    CHECK_EQ(eth_m68k_get_reg(&s_cpu, M68K_REG_D0), 1);
    ticks(1); /* moveq #2 */
    CHECK_EQ(eth_m68k_get_reg(&s_cpu, M68K_REG_D0), 2);
}

ETH_TEST(Port, I1_HighestPendingLevelAboveMaskIsServed)
{
    setup(LOOP, 1);
    ticks(2);
    eth_m68k_set_irq(&s_cpu, 2, true);
    eth_m68k_set_irq(&s_cpu, 5, true);
    ticks(1); /* level 5 taken, handler's first instruction runs */
    CHECK_EQ(s_ack_count[5], 1);
    CHECK_EQ(s_ack_count[2], 0);
    CHECK_EQ(counter(5), 1);
    CHECK_EQ((eth_m68k_get_reg(&s_cpu, M68K_REG_SR) >> 8) & 7, 5);
    ticks(1); /* rte; level 2 is still requested and above the restored mask 0 */
    ticks(1); /* level 2 taken */
    CHECK_EQ(s_ack_count[2], 1);
    CHECK_EQ(counter(2), 1);
}

ETH_TEST(Port, I1_LevelNotAboveMaskIsNotServed)
{
    /* move.w #0x2300,sr ; bra.s self */
    static const uint16_t code[] = {0x46FCu, 0x2300u, 0x60FEu};
    setup(code, 3);
    ticks(2); /* mask now 3 */
    eth_m68k_set_irq(&s_cpu, 3, true);
    ticks(5);
    CHECK_EQ(s_ack_count[3], 0);
    CHECK_EQ(s_cpu.irq.pending_mask & (1u << 3), 1u << 3);
    eth_m68k_set_irq(&s_cpu, 4, true);
    ticks(1);
    CHECK_EQ(s_ack_count[4], 1);
}

ETH_TEST(Port, I2_ClosedGateBlocksService)
{
    setup(LOOP, 1);
    ticks(2);
    eth_m68k_enable_interrupts(&s_cpu, false);
    eth_m68k_set_irq(&s_cpu, 4, true);
    ticks(5);
    CHECK_EQ(s_ack_count[4], 0);
    eth_m68k_enable_interrupts(&s_cpu, true);
    ticks(1);
    CHECK_EQ(s_ack_count[4], 1);
}

ETH_TEST(Port, I2_ResetOpensTheGate)
{
    setup(LOOP, 1);
    eth_m68k_enable_interrupts(&s_cpu, false);
    eth_m68k_reset(&s_cpu);
    CHECK(s_cpu.irq.enabled);
}

ETH_TEST(Port, I4_AutovectorUsesVector24PlusLevel)
{
    setup(LOOP, 1);
    ticks(2);
    s_ack_type[6] = ETH_IACK_AUTOVECTOR;
    eth_m68k_set_irq(&s_cpu, 6, true);
    ticks(1);
    CHECK_EQ(counter(6), 1);
}

ETH_TEST(Port, I5_VectoredUsesTheCardsVector)
{
    setup(LOOP, 1);
    ticks(2);
    s_ack_type[3] = ETH_IACK_VECTORED;
    s_ack_vector[3] = 0x40u;
    eth_m68k_set_irq(&s_cpu, 3, true);
    ticks(1);
    CHECK_EQ(counter(9), 1);
    CHECK_EQ(counter(3), 0);
}

ETH_TEST(Port, I6_SpuriousUsesVector24)
{
    setup(LOOP, 1);
    ticks(2);
    s_ack_type[1] = ETH_IACK_SPURIOUS;
    eth_m68k_set_irq(&s_cpu, 1, true);
    ticks(1);
    CHECK_EQ(counter(8), 1);
    CHECK_EQ(counter(1), 0);
}

ETH_TEST(Port, I8_RequestIsClearedBeforeTheAcknowledge)
{
    setup(LOOP, 1);
    ticks(2);
    eth_m68k_set_irq(&s_cpu, 4, true);
    ticks(1);
    CHECK_EQ(s_ack_count[4], 1);
    CHECK_EQ(s_mask_at_ack[4] & (1u << 4), 0u); /* already cleared when the card answered */
    CHECK_EQ(s_cpu.irq.pending_mask & (1u << 4), 0u);
    ticks(5);
    CHECK_EQ(s_ack_count[4], 1); /* not requested again: served once */
}

ETH_TEST(Port, I8_DeviceThatReassertsInTheAcknowledgeIsServedAgain)
{
    setup(LOOP, 1);
    ticks(2);
    s_reassert_level = 4;
    eth_m68k_set_irq(&s_cpu, 4, true);
    ticks(1); /* served; the acknowledge sets level 4 again */
    CHECK_EQ(s_cpu.irq.pending_mask & (1u << 4), 1u << 4);
    s_reassert_level = 0;
    ticks(2); /* rte, then served again */
    CHECK_EQ(s_ack_count[4], 2);
}

ETH_TEST(Port, I9_StopIsEndedByAnInterrupt)
{
    /* stop #0x2000 ; moveq #9,d3 ; bra.s self */
    static const uint16_t code[] = {0x4E72u, 0x2000u, 0x7609u, 0x60FEu};
    setup(code, 4);
    ticks(2); /* move to sr, stop */
    CHECK(m68k_is_stopped() != 0);
    ticks(5);
    CHECK_EQ(eth_m68k_get_reg(&s_cpu, M68K_REG_D3), 0);
    eth_m68k_set_irq(&s_cpu, 2, true);
    ticks(1); /* interrupt ends the STOP; handler's first instruction runs */
    CHECK_EQ(counter(2), 1);
    ticks(2); /* rte, moveq #9,d3 */
    CHECK_EQ(eth_m68k_get_reg(&s_cpu, M68K_REG_D3), 9);
}

ETH_TEST(Port, I10_UninitialisedVectorGoesToVector15)
{
    setup(LOOP, 1);
    w32((24u + 5u) * 4u, 0u); /* autovector 5 not set up */
    ticks(2);
    eth_m68k_set_irq(&s_cpu, 5, true);
    ticks(1);
    CHECK_EQ(counter(10), 1);
}

ETH_TEST(Port, I11_ResetWithPcZeroStaysStoppedAndKeepsRequests)
{
    setup(LOOP, 1);
    ticks(2);
    eth_m68k_set_irq(&s_cpu, 3, true);
    eth_m68k_enable_interrupts(&s_cpu, false);
    ticks(1);
    w32(4u, 0u);
    eth_m68k_reset(&s_cpu);
    CHECK(eth_m68k_is_halted(&s_cpu));
    ticks(10);
    CHECK_EQ(eth_m68k_get_reg(&s_cpu, M68K_REG_PC), 0);
    CHECK_EQ(s_ack_count[3], 0);
    CHECK_EQ(s_cpu.irq.pending_mask & (1u << 3), 1u << 3); /* reset kept the request */
}

ETH_TEST(Port, I11_ResetClearsDataAndAddressRegisters)
{
    /* moveq #5,d4 ; bra.s self */
    static const uint16_t code[] = {0x7805u, 0x60FEu};
    setup(code, 2);
    ticks(2);
    CHECK_EQ(eth_m68k_get_reg(&s_cpu, M68K_REG_D4), 5);
    eth_m68k_reset(&s_cpu);
    CHECK_EQ(eth_m68k_get_reg(&s_cpu, M68K_REG_D4), 0);
    CHECK_EQ(eth_m68k_get_reg(&s_cpu, M68K_REG_SR), 0x2700);
    CHECK_EQ(eth_m68k_get_reg(&s_cpu, M68K_REG_SP), 0x1000);
    CHECK_EQ(eth_m68k_get_reg(&s_cpu, M68K_REG_PC), 0x400);
}

ETH_TEST(Port, I12_ResetInstructionLeavesRequestsAndGateAlone)
{
    /* reset ; bra.s self */
    static const uint16_t code[] = {0x4E70u, 0x60FEu};
    setup(code, 2);
    eth_m68k_enable_interrupts(&s_cpu, false);
    s_cpu.irq.pending_mask = (uint8_t)(1u << 6);
    ticks(2);
    CHECK_EQ(eth_m68k_get_reg(&s_cpu, M68K_REG_PC), 0x406);
    CHECK(!s_cpu.irq.enabled);
    CHECK_EQ(s_cpu.irq.pending_mask, 1u << 6);
}

ETH_TEST(Port, I13_RequestDuringNopIsServedAfterIt)
{
    /* nop ; bra.s self */
    static const uint16_t code[] = {0x4E71u, 0x60FEu};
    setup(code, 2);
    ticks(2); /* move to sr, nop starts */
    eth_m68k_set_irq(&s_cpu, 4, true);
    ticks(3); /* the nop's remaining ticks: no check */
    CHECK_EQ(s_ack_count[4], 0);
    ticks(1);
    CHECK_EQ(s_ack_count[4], 1);
}

ETH_TEST(Port, Nmi_Level7WhileMaskIs7IsServedOnce)
{
    setup(LOOP, 1);
    /* mask is 7 after reset; the first tick runs move.w #0x2000,sr - stay at 7 */
    eth_m68k_set_irq(&s_cpu, 7, true);
    CHECK(s_cpu.irq.nmi_pending);
    CHECK_EQ(s_cpu.irq.pending_mask & (1u << 7), 0u);
    ticks(1);
    CHECK_EQ(s_ack_count[7], 1);
    CHECK_EQ(counter(7), 1);
    ticks(10);
    CHECK_EQ(s_ack_count[7], 1);
}

ETH_TEST(Port, Nmi_IsConsumedWhileTheGateIsClosed)
{
    setup(LOOP, 1);
    eth_m68k_enable_interrupts(&s_cpu, false);
    eth_m68k_set_irq(&s_cpu, 7, true); /* mask 7: queued as NMI */
    ticks(1);
    CHECK(!s_cpu.irq.nmi_pending);
    eth_m68k_enable_interrupts(&s_cpu, true);
    ticks(5);
    CHECK_EQ(s_ack_count[7], 0);
}

ETH_TEST(Port, Nmi_ClearingLevel7DropsTheQueuedNmi)
{
    setup(LOOP, 1);
    eth_m68k_set_irq(&s_cpu, 7, true);
    eth_m68k_set_irq(&s_cpu, 7, false);
    CHECK(!s_cpu.irq.nmi_pending);
    ticks(5);
    CHECK_EQ(s_ack_count[7], 0);
}

ETH_TEST(Port, HaltLine_StopsExecutionButNotTheCheck)
{
    setup(LOOP, 1);
    eth_m68k_set_irq(&s_cpu, 7, true); /* queued NMI */
    eth_m68k_tick(&s_cpu, true);
    CHECK_EQ(eth_m68k_get_reg(&s_cpu, M68K_REG_PC), 0x400); /* nothing executed */
    CHECK(!s_cpu.irq.nmi_pending);                         /* but the check consumed the NMI */
    CHECK_EQ(s_ack_count[7], 0);
}

ETH_TEST(Port, HaltLine_KeepsRequestsSet)
{
    setup(LOOP, 1);
    ticks(2);
    eth_m68k_set_irq(&s_cpu, 5, true);
    eth_m68k_tick(&s_cpu, true);
    CHECK_EQ(s_ack_count[5], 0);
    CHECK_EQ(s_cpu.irq.pending_mask & (1u << 5), 1u << 5);
    ticks(1);
    CHECK_EQ(s_ack_count[5], 1);
}

static unsigned int r16(uint32_t a)
{
    return ((unsigned int)s_mem[a] << 8u) | s_mem[a + 1u];
}

ETH_TEST(Port, BusError_HandlerRunsOnTheNextTick)
{
    /* move.b $00010000,d0 ; bra.s self */
    static const uint16_t code[] = {0x1039u, 0x0001u, 0x0000u, 0x60FEu};
    setup(code, 4);
    ticks(1);
    ticks(1); /* faults */
    CHECK_EQ(eth_m68k_get_reg(&s_cpu, M68K_REG_PC), handler(11));
    CHECK_EQ(counter(11), 0);
    CHECK_EQ(0x1000u - eth_m68k_get_reg(&s_cpu, M68K_REG_SP), 14);
    ticks(1);
    CHECK_EQ(counter(11), 1);
}

ETH_TEST(Port, BusError_FrameDescribesTheAccess)
{
    /* move.b $00010000,d0 (read, supervisor data) */
    static const uint16_t rd_code[] = {0x1039u, 0x0001u, 0x0000u, 0x60FEu};
    /* move.b d0,$00012345 (write, supervisor data) */
    static const uint16_t wr_code[] = {0x13C0u, 0x0001u, 0x2345u, 0x60FEu};
    uint32_t sp;

    setup(rd_code, 4);
    ticks(2);
    sp = eth_m68k_get_reg(&s_cpu, M68K_REG_SP);
    CHECK_EQ(r16(sp), 0x0015);       /* read (0x10), fc 5 */
    CHECK_EQ(r16(sp + 2u), 0x0001);  /* access address 0x00010000 */
    CHECK_EQ(r16(sp + 4u), 0x0000);
    CHECK_EQ(r16(sp + 6u), 0x1039);  /* IR */

    setup(wr_code, 4);
    ticks(2);
    sp = eth_m68k_get_reg(&s_cpu, M68K_REG_SP);
    CHECK_EQ(r16(sp), 0x0005);       /* write (0x00), fc 5 */
    CHECK_EQ(r16(sp + 2u), 0x0001);
    CHECK_EQ(r16(sp + 4u), 0x2345);
}

ETH_TEST(Port, Bus_LongReadIsHighWordThenLowWord)
{
    /* move.l $00002000,d5 ; bra.s self */
    static const uint16_t code[] = {0x2A39u, 0x0000u, 0x2000u, 0x60FEu};
    setup(code, 4);
    w32(0x2000u, 0x12345678u);
    ticks(2);
    CHECK_EQ(eth_m68k_get_reg(&s_cpu, M68K_REG_D5), 0x12345678);
}

ETH_TEST(Port, Bus_LongWriteIsHighWordThenLowWord)
{
    /* move.l #$89ABCDEF,$00002000 ; bra.s self */
    static const uint16_t code[] = {0x23FCu, 0x89ABu, 0xCDEFu, 0x0000u, 0x2000u, 0x60FEu};
    setup(code, 6);
    ticks(2);
    CHECK_EQ(r16(0x2000u), 0x89AB);
    CHECK_EQ(r16(0x2002u), 0xCDEF);
}

ETH_TEST(Port, Init_RejectsMissingArguments)
{
    EthM68kBus bus = {NULL, rd, wr, iack, NULL};
    EthM68kBus no_read = {NULL, NULL, wr, iack, NULL};
    EthM68kBus no_write = {NULL, rd, NULL, iack, NULL};
    EthM68kBus no_iack = {NULL, rd, wr, NULL, NULL};

    CHECK_EQ(eth_m68k_init(NULL, &bus), -1);
    CHECK_EQ(eth_m68k_init(&s_cpu, NULL), -1);
    CHECK_EQ(eth_m68k_init(&s_cpu, &no_read), -1);
    CHECK_EQ(eth_m68k_init(&s_cpu, &no_write), -1);
    CHECK_EQ(eth_m68k_init(&s_cpu, &no_iack), -1);
    CHECK_EQ(eth_m68k_init(&s_cpu, &bus), 0);
}

ETH_TEST(Port, Irq_OutOfRangeLevelsAreIgnored)
{
    EthIrq irq;
    eth_irq_reset(&irq);
    eth_irq_set(&irq, 0, true, 0);
    eth_irq_set(&irq, 8, true, 0);
    eth_irq_set(&irq, -1, true, 0);
    CHECK_EQ(irq.pending_mask, 0);
    CHECK(!irq.nmi_pending);
}

ETH_TEST(Port, Nmi_OnlyLevel7BecomesAnNmiWhenTheMaskIs7)
{
    EthIrq irq;
    eth_irq_reset(&irq);
    eth_irq_set(&irq, 3, true, 7);
    CHECK_EQ(irq.pending_mask, 1u << 3);
    CHECK(!irq.nmi_pending);
    eth_irq_set(&irq, 7, true, 6);   /* mask below 7: an ordinary level-7 request */
    CHECK_EQ(irq.pending_mask, (1u << 3) | (1u << 7));
    CHECK(!irq.nmi_pending);
}

ETH_TEST(Port, Nmi_ClearingAnotherLevelKeepsTheQueuedNmi)
{
    EthIrq irq;
    eth_irq_reset(&irq);
    eth_irq_set(&irq, 3, true, 7);
    eth_irq_set(&irq, 7, true, 7);
    eth_irq_set(&irq, 3, false, 7);
    CHECK(irq.nmi_pending);
    CHECK_EQ(irq.pending_mask, 0);
}

ETH_TEST(Port, I11_ResetClearsTheUserStackPointer)
{
    setup(LOOP, 1);
    eth_m68k_set_reg(&s_cpu, M68K_REG_USP, 0x1234u);
    eth_m68k_reset(&s_cpu);
    CHECK_EQ(eth_m68k_get_reg(&s_cpu, M68K_REG_USP), 0);
}

ETH_TEST(Port, Delivery_MusashiDoesNotRetakeALevelByItself)
{
    /* For every level: serve it once, return with rte, and check the CPU
     * does not take it again although nothing re-requested it. */
    for (int lvl = 1; lvl <= 6; lvl++)
    {
        setup(LOOP, 1);
        ticks(2);
        eth_m68k_set_irq(&s_cpu, lvl, true);
        ticks(1);
        ticks(10);
        CHECK_EQ(s_ack_count[lvl], 1);
        CHECK_EQ(counter(lvl), 1);
    }
}

ETH_TEST(Port, Irq_NullControllerSelectsNothing)
{
    CHECK_EQ(eth_irq_select(NULL, 0), 0);
    CHECK_EQ(eth_irq_take(NULL, 0, false), 0);
}

static int s_insn_count;
static uint32_t s_insn_last_pc;

static void insn_hook(void *ctx, uint32_t pc)
{
    (void)ctx;
    s_insn_count++;
    s_insn_last_pc = pc;
}

ETH_TEST(Port, Insn_HookSeesEveryInstructionStart)
{
    /* moveq #1,d0 ; moveq #2,d0 ; bra.s self */
    static const uint16_t code[] = {0x7001u, 0x7002u, 0x60FEu};
    EthM68kBus bus = {NULL, rd, wr, iack, insn_hook};
    setup(code, 3);
    (void)eth_m68k_init(&s_cpu, &bus);
    eth_m68k_reset(&s_cpu);
    s_insn_count = 0;
    ticks(3);
    CHECK_EQ(s_insn_count, 3);
    CHECK_EQ(s_insn_last_pc, 0x406);
    CHECK_EQ(s_cpu.instructions, 3);
}

ETH_TEST(Port, NullArguments_AreIgnored)
{
    EthIrq irq;

    eth_irq_reset(NULL);
    eth_irq_enable(NULL, true);
    eth_irq_set(NULL, 3, true, 0);
    eth_m68k_reset(NULL);
    eth_m68k_tick(NULL, false);
    eth_m68k_set_irq(NULL, 3, true);
    eth_m68k_enable_interrupts(NULL, true);
    eth_m68k_bus_error(NULL, 0u, true);
    CHECK(!eth_m68k_is_halted(NULL));
    eth_irq_reset(&irq);
    CHECK_EQ(irq.pending_mask, 0);
}

ETH_TEST(Port, Runaway_101ConsecutiveTrapsHaltTheCpu)
{
    /* every vector -> 0x10000 (unmapped): each instruction fetch bus-errors,
     * the bus error handler fetch bus-errors again ... a runaway */
    setup(LOOP, 1);
    for (unsigned v = 2u; v < 64u; v++)
    {
        w32(v * 4u, 0x00010000u);
    }
    w32(4u, 0x00010000u);
    eth_m68k_reset(&s_cpu);
    CHECK(!eth_m68k_is_halted(&s_cpu));
    ticks(100);
    CHECK(!eth_m68k_is_halted(&s_cpu));
    CHECK_EQ(s_cpu.consecutive_traps, 100);
    ticks(1);
    CHECK(eth_m68k_is_halted(&s_cpu));
    eth_m68k_reset(&s_cpu);
    CHECK(!s_cpu.crashed);
}

ETH_TEST(Port, Runaway_TrapInstructionsAloneDoNotTrip)
{
    /* trap #0 ; bra.s self - TRAP vector 32 -> handler 0x6F0... use a handler
     * that returns with rte: one trap tick then normal ticks reset the count */
    static const uint16_t code[] = {0x4E40u, 0x60FEu};
    setup(code, 2);
    w32(32u * 4u, handler(8));
    ticks(2);
    CHECK_EQ(s_cpu.consecutive_traps, 1);
    ticks(1);
    CHECK_EQ(s_cpu.consecutive_traps, 0);
}
