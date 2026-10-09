/*
 * eth_m68k.h - The Ethernet II card's 68000: Musashi plus the card-side glue.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Runs Musashi (external/Musashi, RetroCoreLabs fork) the way RetroCore runs
 * its Cpu68K for this card (RetroCore commit 935163f):
 *  - one ND-100 tick = one 68000 "clock"; at a clock with no instruction in
 *    progress, interrupts are checked and then ONE whole instruction runs.
 *    Every instruction costs one tick except NOP, which costs four
 *    (Cpu68K.cs:515-526, 612-616, 757-795; Instructionset.cs:342 is the only
 *    instruction with a cycle count);
 *  - interrupts go through eth_irq (the interrupt logic RetroCore keeps in
 *    its CPU core);
 *  - bus accesses reach the card one byte at a time, word = byte at A then
 *    A+1, long = word at A then A+2 (MachineMemory.cs:405-421, 500-516);
 *  - reset: interrupt gate opened, registers D0-D7/A0-A7/USP cleared, SR
 *    0x2700, SSP and PC read from vectors 0 and 1; PC 0 leaves the CPU
 *    stopped until the next reset (Cpu68K.cs:831-855, Registers.cs Clear);
 *    pending interrupt levels and an unfinished NOP are kept;
 *  - runaway guard: more than ETH_M68K_MAX_CONSECUTIVE_TRAPS ticks in a row
 *    that each took an exception (any except interrupts) with no plain
 *    instruction completing halt the CPU until reset (Cpu68K.cs:537-575).
 *    Difference: RetroCore skips processing the tripping exception, Musashi
 *    has already pushed its frame when the hook runs.
 *
 * Musashi keeps one CPU in global state; only one EthM68k may exist at a time
 * (plan decision D6: one card).
 */

#ifndef ETH_M68K_H
#define ETH_M68K_H

#include <stdbool.h>
#include <stdint.h>

#include "eth_irq.h"

#define ETH_M68K_MAX_CONSECUTIVE_TRAPS 100 /* cs: Emulated.HW/Motorola/CPU/MC68K/Cpu68K.cs:513 */

/** How the card answers an interrupt acknowledge. */
typedef enum
{
    /* cs: none - C# InterruptAckType has implicit values; only the names matter */
    ETH_IACK_AUTOVECTOR = 0,
    ETH_IACK_SPURIOUS = 1, /* cs: none - see ETH_IACK_AUTOVECTOR */
    ETH_IACK_VECTORED = 2 /* cs: none - see ETH_IACK_AUTOVECTOR */
} EthIackType;

/** The card side of the 68000 bus. */
typedef struct
{
    void *ctx;
    /** Byte read. May call eth_m68k_bus_error() instead of returning. */
    uint8_t (*read8)(void *ctx, uint32_t address);
    /** Byte write. May call eth_m68k_bus_error() instead of returning. */
    void (*write8)(void *ctx, uint32_t address, uint8_t value);
    /** Interrupt acknowledge for a level; sets *vector for ETH_IACK_VECTORED. */
    EthIackType (*iack)(void *ctx, int level, uint8_t *vector);
    /** Optional: called at the start of every instruction (NULL = none). */
    void (*insn)(void *ctx, uint32_t pc);
} EthM68kBus;

typedef struct
{
    EthM68kBus bus;
    EthIrq irq;
    unsigned int fc;        /* function code of the current access */
    int stall;              /* ticks left of the current instruction (NOP) */
    bool no_code;           /* reset found PC 0: stopped until next reset */
    uint64_t instructions;  /* instructions started (instruction hook count) */
    uint64_t ticks;         /* eth_m68k_tick calls */
    int consecutive_traps;  /* RetroCore _consecutiveTrapCount */
    bool exception_in_tick; /* an exception (not an interrupt) started this tick */
    bool crashed;           /* runaway guard tripped: stopped until reset */
} EthM68k;

/**
 * @brief Set up the CPU and make this the current card. Does not reset it.
 * @param m   CPU state to initialise
 * @param bus card side; copied
 * @return 0, or -1 for a NULL argument or a missing read8/write8/iack
 */
int eth_m68k_init(EthM68k *m, const EthM68kBus *bus);

/**
 * @brief 68000 reset as RetroCore's Cpu68K does it (see file comment).
 * @param m CPU
 */
void eth_m68k_reset(EthM68k *m);

/**
 * @brief One ND-100 tick.
 * @param m         CPU
 * @param halt_line the card's halt OR reset (the CPU does not run, but the
 *                  interrupt check still happens, as in RetroCore)
 */
void eth_m68k_tick(EthM68k *m, bool halt_line);

/**
 * @brief Set or clear a 68000 interrupt request level (1-7).
 * @param m      CPU
 * @param level  1-7
 * @param active true = request
 */
void eth_m68k_set_irq(EthM68k *m, int level, bool active);

/**
 * @brief Open or close the interrupt gate.
 * @param m       CPU
 * @param enabled true = requests may be serviced
 */
void eth_m68k_enable_interrupts(EthM68k *m, bool enabled);

/**
 * @brief Raise a bus error for the access in progress. Call only from inside
 *        read8/write8: it does not return (Musashi longjmps back into
 *        m68k_execute, abandoning the instruction).
 * @param m       CPU
 * @param address faulting address
 * @param is_read true for a read
 */
void eth_m68k_bus_error(EthM68k *m, uint32_t address, bool is_read);

/**
 * @brief Read a 68000 register.
 * @param m   CPU
 * @param reg a Musashi m68k_register_t value (M68K_REG_PC, ...)
 * @return the register value
 */
uint32_t eth_m68k_get_reg(const EthM68k *m, int reg);

/**
 * @brief Write a 68000 register.
 * @param m     CPU
 * @param reg   a Musashi m68k_register_t value
 * @param value new value
 */
void eth_m68k_set_reg(EthM68k *m, int reg, uint32_t value);

/**
 * @brief Is the CPU halted for good (double fault) or stopped by a reset
 *        that found PC 0? A STOP instruction does not count.
 * @param m CPU
 * @return true when an interrupt cannot restart it
 */
bool eth_m68k_is_halted(const EthM68k *m);

#endif /* ETH_M68K_H */
