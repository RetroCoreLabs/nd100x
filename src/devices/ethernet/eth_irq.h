/*
 * eth_irq.h - Interrupt controller in front of the Ethernet II card's 68000.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Clean-room model of the interrupt logic RetroCore keeps inside its 68000
 * core (Cpu68K.Interrupts.cs at RetroCore commit 935163f), which Musashi does
 * not have. Behaviour, each item covered by tests/ethernet/test_eth_irq.c:
 *  - seven request levels held as a bit mask; set/clear one level at a time;
 *  - setting level 7 while the CPU's interrupt mask is already 7 queues a
 *    one-shot NMI instead of setting the level bit; clearing level 7 while
 *    that NMI is queued drops the NMI and leaves the bit alone;
 *  - selection: a queued NMI wins (and is consumed by the selection, also
 *    when the gate is closed); otherwise the highest set level above the
 *    CPU's mask; 0 = nothing;
 *  - a gate (open only while the card lets the 68000 run) blocks service;
 *  - a halted CPU (not STOP) is not serviced and its request stays set;
 *  - the selected level's request is cleared before the acknowledge is
 *    answered, so a device that still requests service sets it again.
 */

#ifndef ETH_IRQ_H
#define ETH_IRQ_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint8_t pending_mask; /* bit n = level n requested (bits 1-7) */
    bool nmi_pending;     /* level 7 raised while the mask was 7 */
    bool enabled;         /* the gate */
} EthIrq;

/**
 * @brief Clear all requests, the queued NMI, and close the gate.
 * @param irq controller
 */
void eth_irq_reset(EthIrq *irq);

/**
 * @brief Open or close the gate.
 * @param irq controller
 * @param enabled true = requests may be serviced
 */
void eth_irq_enable(EthIrq *irq, bool enabled);

/**
 * @brief Set or clear one request level.
 * @param irq controller
 * @param level 1-7 (other values are ignored)
 * @param active true = set, false = clear
 * @param cpu_mask the CPU's current interrupt mask (SR bits 8-10), 0-7
 */
void eth_irq_set(EthIrq *irq, int level, bool active, int cpu_mask);

/**
 * @brief Pick the level to service now. Consumes a queued NMI.
 * @param irq controller
 * @param cpu_mask the CPU's current interrupt mask, 0-7
 * @return 7 for a queued NMI, else the highest set level above cpu_mask, else 0.
 *         The gate is NOT applied here (see eth_irq_take).
 */
int eth_irq_select(EthIrq *irq, int cpu_mask);

/**
 * @brief Selection plus gate plus acknowledge-side clear, as done once per
 *        instruction boundary.
 * @param irq controller
 * @param cpu_mask the CPU's current interrupt mask, 0-7
 * @param cpu_halted true when the CPU is stopped for a reason an interrupt
 *        does not end (halt after a double fault); STOP is not such a reason.
 *        Then nothing is delivered and the request is NOT cleared (RetroCore
 *        returns before its clear in that case).
 * @return the level to deliver to the CPU now (its request is already
 *         cleared), or 0 when nothing is to be delivered.
 */
int eth_irq_take(EthIrq *irq, int cpu_mask, bool cpu_halted);

#endif /* ETH_IRQ_H */
