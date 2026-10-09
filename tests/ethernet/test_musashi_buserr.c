/*
 * test_musashi_buserr.c - The patched Musashi pushes the 68000 bus error frame.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * external/Musashi is the RetroCoreLabs fork with commit 328a672 "68000 bus
 * error: push the 7-word group 0 frame". Before that commit Musashi pushed the
 * 68010 29-word frame for every CPU type (m68kcpu.h, m68ki_exception_bus_error),
 * while the card is a 68000 and RetroCore pushes the 7-word frame
 * (Instructionset.Helpers.cs CreateStackFrameBusError68000). The card firmware
 * copies frame words to the ND-100 mailbox, so the layout matters.
 *
 * Program: SSP 0x1000, PC 0x400: move.b $800000,d0 (unmapped -> bus error),
 * vector 2 -> 0x500: bra.s self.
 */

#include "eth_test.h"
#include "m68k.h"

#include <string.h>

static uint8_t s_mem[0x10000];
static int s_faults;

static unsigned int rd8(unsigned int a)
{
    if (a >= sizeof s_mem)
    {
        s_faults++;
        m68k_set_bus_error_info(a, 0, 5u);
        m68k_pulse_bus_error();
        return 0xFFu;
    }
    return s_mem[a];
}

unsigned int m68k_read_memory_8(unsigned int a)
{
    return rd8(a);
}

unsigned int m68k_read_memory_16(unsigned int a)
{
    return (rd8(a) << 8u) | rd8(a + 1u);
}

unsigned int m68k_read_memory_32(unsigned int a)
{
    return (m68k_read_memory_16(a) << 16u) | m68k_read_memory_16(a + 2u);
}

void m68k_write_memory_8(unsigned int a, unsigned int v)
{
    if (a < sizeof s_mem)
    {
        s_mem[a] = (uint8_t)v;
    }
}

void m68k_write_memory_16(unsigned int a, unsigned int v)
{
    m68k_write_memory_8(a, v >> 8u);
    m68k_write_memory_8(a + 1u, v & 0xFFu);
}

void m68k_write_memory_32(unsigned int a, unsigned int v)
{
    m68k_write_memory_16(a, v >> 16u);
    m68k_write_memory_16(a + 2u, v & 0xFFFFu);
}

unsigned int m68k_read_disassembler_16(unsigned int a)
{
    return m68k_read_memory_16(a);
}

unsigned int m68k_read_disassembler_32(unsigned int a)
{
    return m68k_read_memory_32(a);
}

static void w16(unsigned int a, unsigned int v)
{
    s_mem[a] = (uint8_t)(v >> 8u);
    s_mem[a + 1u] = (uint8_t)v;
}

static void w32(unsigned int a, unsigned int v)
{
    w16(a, v >> 16u);
    w16(a + 2u, v & 0xFFFFu);
}

static unsigned int r16(unsigned int a)
{
    return ((unsigned int)s_mem[a] << 8u) | s_mem[a + 1u];
}

static void load_program(unsigned int cpu_type)
{
    memset(s_mem, 0, sizeof s_mem);
    s_faults = 0;
    w32(0u, 0x1000u);
    w32(4u, 0x400u);
    w32(8u, 0x500u);
    w16(0x400u, 0x1039u);
    w32(0x402u, 0x00800000u);
    w16(0x406u, 0x60FEu);
    w16(0x500u, 0x7201u);                 /* handler: moveq #1,d1 */
    w16(0x502u, 0x60FEu);                 /*          bra.s self */
    m68k_init();
    m68k_set_cpu_type(cpu_type);
    m68k_pulse_reset();
    m68k_set_reg(M68K_REG_D1, 0);         /* reset leaves D/A registers as they were */
    (void)m68k_execute(0);                /* spend the reset cycles */
}

static void run_program(unsigned int cpu_type)
{
    load_program(cpu_type);
    (void)m68k_execute(200);
}

ETH_TEST(Port, Musashi_68000BusError_PushesSevenWordFrame)
{
    unsigned int sp;

    run_program(M68K_CPU_TYPE_68000);
    sp = m68k_get_reg(NULL, M68K_REG_SP);
    CHECK_EQ(s_faults, 1);
    CHECK_EQ(m68k_get_reg(NULL, M68K_REG_PC), 0x502);
    CHECK_EQ(0x1000u - sp, 14);
    CHECK_EQ(r16(sp), 0x0015);          /* read (0x10), in instruction, fc 5 */
    CHECK_EQ(r16(sp + 2u), 0x0080);     /* access address 0x00800000 */
    CHECK_EQ(r16(sp + 4u), 0x0000);
    CHECK_EQ(r16(sp + 6u), 0x1039);     /* IR = the move.b opcode */
    CHECK_EQ(r16(sp + 10u), 0x0000);    /* stacked PC 0x00000406 */
    CHECK_EQ(r16(sp + 12u), 0x0406);
}

ETH_TEST(Port, Musashi_68010BusError_StillPushesFormat8Frame)
{
    run_program(M68K_CPU_TYPE_68010);
    CHECK_EQ(s_faults, 1);
    CHECK_EQ(0x1000u - m68k_get_reg(NULL, M68K_REG_SP), 58);
}

/* A bus error with no cycles left must end the m68k_execute() call: the
 * handler's first instruction runs in the NEXT call. The card runs one
 * instruction per ND-100 tick and RetroCore starts the handler on the tick
 * after the fault (Cpu68K.cs ExecuteOneClockCycleImpl catch path). Musashi's
 * address error path already returned here; the bus error path re-entered
 * the instruction loop (fork commit fixes it). */
ETH_TEST(Port, Musashi_BusError_HandlerStartsInNextExecuteCall)
{
    load_program(M68K_CPU_TYPE_68000);
    (void)m68k_execute(1);
    CHECK_EQ(s_faults, 1);
    CHECK_EQ(m68k_get_reg(NULL, M68K_REG_PC), 0x500);
    CHECK_EQ(m68k_get_reg(NULL, M68K_REG_D1), 0);
    (void)m68k_execute(1);
    CHECK_EQ(m68k_get_reg(NULL, M68K_REG_D1), 1);
}
