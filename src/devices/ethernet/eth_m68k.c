/*
 * eth_m68k.c - The Ethernet II card's 68000: Musashi plus the card-side glue.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * See eth_m68k.h for the behaviour and where each part comes from.
 *
 * Musashi calls the m68k_* memory functions and the three callbacks named in
 * m68k/eth_m68kconf.h with no context argument, so they work on s_cur, the
 * card set by eth_m68k_init().
 */

#include "eth_m68k.h"

#include <stddef.h>
#include <string.h>

#include "m68k.h"

/* cs: none - the 68000 NOP opcode (RetroCore builds it from bit fields, Instructionset.cs:342) */
#define OPCODE_NOP 0x4E71u
#define NOP_TICKS 4 /* cs: Emulated.HW/Motorola/CPU/MC68K/Instructionset.cs:342 */

static EthM68k *s_cur;

static int cpu_mask(void)
{
    return (int)((m68k_get_reg(NULL, M68K_REG_SR) >> 8u) & 7u);
}

static bool musashi_halted(void)
{
    return m68k_is_halted() != 0;
}

int eth_m68k_init(EthM68k *m, const EthM68kBus *bus)
{
    if ((m == NULL) || (bus == NULL) || (bus->read8 == NULL) || (bus->write8 == NULL) ||
        (bus->iack == NULL))
    {
        return -1;
    }
    memset(m, 0, sizeof *m);
    m->bus = *bus;
    eth_irq_reset(&m->irq);
    s_cur = m;
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    return 0;
}

void eth_m68k_reset(EthM68k *m)
{
    if (m == NULL)
    {
        return;
    }
    s_cur = m;
    eth_irq_enable(&m->irq, true);
    for (int r = M68K_REG_D0; r <= M68K_REG_A7; r++)
    {
        m68k_set_reg((m68k_register_t)r, 0);
    }
    m68k_set_reg(M68K_REG_USP, 0);
    m68k_pulse_reset(); /* SR 0x2700, SSP <- (0), PC <- (4) */
    (void)m68k_execute(0); /* spend Musashi's reset cycles now, not in a later tick */
    m->no_code = (m68k_get_reg(NULL, M68K_REG_PC) == 0u);
    m->crashed = false;
    m->consecutive_traps = 0;
}

bool eth_m68k_is_halted(const EthM68k *m)
{
    return (m != NULL) && (m->no_code || m->crashed || musashi_halted());
}

void eth_m68k_tick(EthM68k *m, bool halt_line)
{
    bool cpu_stopped;
    int level = 0;

    if (m == NULL)
    {
        return;
    }
    s_cur = m;
    m->ticks++;
    cpu_stopped = halt_line || eth_m68k_is_halted(m);

    if (m->stall == 0)
    {
        level = eth_irq_take(&m->irq, cpu_mask(), cpu_stopped);
        if (level != 0)
        {
            m68k_set_irq(0);
            m68k_set_irq((unsigned int)level); /* a 0 -> 7 change also makes level 7 an NMI */
        }
    }

    if (!cpu_stopped)
    {
        if (m->stall == 0)
        {
            uint64_t before = m->instructions;
            m->exception_in_tick = false;
            (void)m68k_execute(1); /* exactly one instruction (or none while STOPped) */
            if (m->exception_in_tick)
            {
                m->consecutive_traps++;
                if (m->consecutive_traps > ETH_M68K_MAX_CONSECUTIVE_TRAPS)
                {
                    m->crashed = true; /* RetroCore: stopMode |= CRASHED | STOP */
                }
            }
            else if (m->instructions != before)
            {
                m->consecutive_traps = 0;
            }
            if ((m->instructions != before) && (m68k_get_reg(NULL, M68K_REG_IR) == OPCODE_NOP))
            {
                m->stall = NOP_TICKS;
            }
        }
        if (m->stall > 0)
        {
            m->stall--;
        }
    }

    if (level != 0)
    {
        /* Delivered (or, if the CPU did not run, dropped like RetroCore's
         * cleared request): take the level off Musashi's pins again so it
         * never decides on an interrupt by itself. */
        m68k_set_irq(0);
    }
}

void eth_m68k_set_irq(EthM68k *m, int level, bool active)
{
    if (m == NULL)
    {
        return;
    }
    eth_irq_set(&m->irq, level, active, cpu_mask());
}

void eth_m68k_enable_interrupts(EthM68k *m, bool enabled)
{
    if (m == NULL)
    {
        return;
    }
    eth_irq_enable(&m->irq, enabled);
}

void eth_m68k_bus_error(EthM68k *m, uint32_t address, bool is_read)
{
    if (m == NULL)
    {
        return;
    }
    m68k_set_bus_error_info(address, is_read ? 0 : 1, m->fc);
    m68k_pulse_bus_error();
}

uint32_t eth_m68k_get_reg(const EthM68k *m, int reg)
{
    (void)m;
    return m68k_get_reg(NULL, (m68k_register_t)reg);
}

void eth_m68k_set_reg(EthM68k *m, int reg, uint32_t value)
{
    (void)m;
    m68k_set_reg((m68k_register_t)reg, value);
}

/* ---- Musashi callbacks (names fixed by m68k/eth_m68kconf.h and m68k.h) ---- */

int eth_m68k_int_ack(int int_level)
{
    uint8_t vector = 0u;
    EthIackType type;

    if (s_cur == NULL)
    {
        return (int)M68K_INT_ACK_SPURIOUS;
    }
    type = s_cur->bus.iack(s_cur->bus.ctx, int_level, &vector);
    switch (type)
    {
    case ETH_IACK_VECTORED:
        return (int)vector;
    case ETH_IACK_SPURIOUS:
        return (int)M68K_INT_ACK_SPURIOUS;
    case ETH_IACK_AUTOVECTOR:
        /* fall through */
    default:
        return (int)M68K_INT_ACK_AUTOVECTOR;
    }
}

void eth_m68k_exception_hook(unsigned int vector)
{
    (void)vector;
    if (s_cur != NULL)
    {
        s_cur->exception_in_tick = true;
    }
}

void eth_m68k_set_fc(unsigned int fc)
{
    if (s_cur != NULL)
    {
        s_cur->fc = fc;
    }
}

void eth_m68k_instr_hook(unsigned int pc)
{
    if (s_cur == NULL)
    {
        return;
    }
    s_cur->instructions++;
    if (s_cur->bus.insn != NULL)
    {
        s_cur->bus.insn(s_cur->bus.ctx, pc);
    }
}

static unsigned int rd8(unsigned int address)
{
    return s_cur->bus.read8(s_cur->bus.ctx, address);
}

static void wr8(unsigned int address, unsigned int value)
{
    s_cur->bus.write8(s_cur->bus.ctx, address, (uint8_t)value);
}

unsigned int m68k_read_memory_8(unsigned int address)
{
    return rd8(address);
}

unsigned int m68k_read_memory_16(unsigned int address)
{
    unsigned int hi = rd8(address);
    unsigned int lo = rd8(address + 1u);
    return (hi << 8u) | lo;
}

unsigned int m68k_read_memory_32(unsigned int address)
{
    unsigned int hi = m68k_read_memory_16(address);
    unsigned int lo = m68k_read_memory_16(address + 2u);
    return (hi << 16u) | lo;
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    wr8(address, value);
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    wr8(address, value >> 8u);
    wr8(address + 1u, value & 0xFFu);
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    m68k_write_memory_16(address, value >> 16u);
    m68k_write_memory_16(address + 2u, value & 0xFFFFu);
}
