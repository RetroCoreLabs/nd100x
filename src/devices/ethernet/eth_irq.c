/*
 * eth_irq.c - Interrupt controller in front of the Ethernet II card's 68000.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * See eth_irq.h for the behaviour and where it comes from.
 */

#include "eth_irq.h"

#include <string.h>

void eth_irq_reset(EthIrq *irq)
{
    if (irq == NULL)
    {
        return;
    }
    memset(irq, 0, sizeof *irq);
}

void eth_irq_enable(EthIrq *irq, bool enabled)
{
    if (irq == NULL)
    {
        return;
    }
    irq->enabled = enabled;
}

void eth_irq_set(EthIrq *irq, int level, bool active, int cpu_mask)
{
    if ((irq == NULL) || (level < 1) || (level > 7))
    {
        return;
    }
    if (active)
    {
        if ((level == 7) && (cpu_mask == 7))
        {
            irq->nmi_pending = true;
            return;
        }
        irq->pending_mask = (uint8_t)(irq->pending_mask | (1u << (unsigned)level));
        return;
    }
    if ((level == 7) && irq->nmi_pending)
    {
        irq->nmi_pending = false;
        return;
    }
    irq->pending_mask = (uint8_t)(irq->pending_mask & ~(1u << (unsigned)level));
}

int eth_irq_select(EthIrq *irq, int cpu_mask)
{
    if (irq == NULL)
    {
        return 0;
    }
    if (irq->nmi_pending)
    {
        irq->nmi_pending = false;
        return 7;
    }
    for (int level = 7; level > 0; level--)
    {
        if (((irq->pending_mask & (1u << (unsigned)level)) != 0u) && (level > cpu_mask))
        {
            return level;
        }
    }
    return 0;
}

int eth_irq_take(EthIrq *irq, int cpu_mask, bool cpu_halted)
{
    int level = eth_irq_select(irq, cpu_mask);

    if ((irq == NULL) || !irq->enabled || (level == 0) || cpu_halted)
    {
        return 0;
    }
    /* The request is cleared before the acknowledge is answered. For a
     * selected NMI the queued flag is already consumed by the selection, so
     * this clears the level-7 bit (if set) like any other level. */
    eth_irq_set(irq, level, false, cpu_mask);
    return level;
}
