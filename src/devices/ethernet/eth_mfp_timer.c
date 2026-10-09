/*
 * eth_mfp_timer.c - MC68901 timer (port of RetroCore MfpTimer.cs).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Copied behaviour worth knowing (MfpTimer.cs at RetroCore 935163f):
 *  - the counter counts N, N-1, ..., 1, then toggles the output, raises
 *    "elapsed" and reloads from TDR (it never reaches 0 there); a counter
 *    that is 0 decrements to 255 (byte underflow);
 *  - a TDR write reloads the counter when the timer is stopped, and also in
 *    event count mode (RetroCore's own test Test_Timer_TDR_NotReloadedInEventCountMode
 *    expects no reload in event mode and fails against this code);
 *  - only timers A and B have the write-only reset bit 4.
 */

#include "eth_mfp.h"

#include <stddef.h>

const int g_mfptimer_prescaler[8] = {0, 4, 10, 16, 50, 64, 100, 200};

void mfptimer_create(MfpTimer *t, MfpTimerName name)
{
    if (t == NULL)
    {
        return;
    }
    t->tcr = 0u;
    t->tdr = 0u;
    t->tmc = 0u;
    t->in_latch = false;
    t->out_latch = false;
    t->is_enabled = false;
    t->prescaler_count = 0;
    t->current_divisor = 0;
    t->timer_name = name;
    t->on_timer_output = NULL;
    t->on_timer_elapsed = NULL;
    t->ctx = NULL;
}

void mfptimer_set_enable(MfpTimer *t, bool enable)
{
    t->is_enabled = enable;
}

/* timer_count */
static void mfptimer_timer_count(MfpTimer *t)
{
    if (t->tmc == 0x01u)
    {
        t->out_latch = !t->out_latch;
        if (t->on_timer_output != NULL)
        {
            t->on_timer_output(t->ctx, t->timer_name, t->out_latch);
        }
        if (t->on_timer_elapsed != NULL)
        {
            t->on_timer_elapsed(t->ctx, t->timer_name);
        }
        t->tmc = t->tdr;
    }
    else
    {
        t->tmc = (uint8_t)(t->tmc - 1u);
    }
}

void mfptimer_tick(MfpTimer *t)
{
    if (t->is_enabled && (t->current_divisor > 0))
    {
        t->prescaler_count++;
        if (t->prescaler_count >= t->current_divisor)
        {
            t->prescaler_count = 0;
            mfptimer_timer_count(t);
        }
    }
}

void mfptimer_tick_event_count(MfpTimer *t)
{
    mfptimer_timer_count(t);
}

void mfptimer_set_control_register(MfpTimer *t, uint8_t value)
{
    bool is_ab = (t->timer_name == MFP_TIMER_NAME_A) || (t->timer_name == MFP_TIMER_NAME_B);
    bool has_reset_bit = is_ab && ((value & 0x10u) != 0u);
    uint8_t readable_mask;
    uint8_t mode;

    if (has_reset_bit)
    {
        t->tmc = t->tdr;
        t->out_latch = false;
        if (t->on_timer_output != NULL)
        {
            t->on_timer_output(t->ctx, t->timer_name, false);
        }
    }

    readable_mask = is_ab ? 0x0Fu : 0x07u;
    t->tcr = (uint8_t)(value & readable_mask);

    mode = (uint8_t)(value & 0x0Fu);
    switch (mode)
    {
    case 0x00u:
        mfptimer_set_enable(t, false);
        t->current_divisor = 0;
        break;
    case 0x01u:
    case 0x02u:
    case 0x03u:
    case 0x04u:
    case 0x05u:
    case 0x06u:
    case 0x07u:
        t->current_divisor = g_mfptimer_prescaler[mode & 0x07u];
        t->prescaler_count = 0;
        mfptimer_set_enable(t, true);
        break;
    case 0x08u:
        mfptimer_set_enable(t, false);
        t->current_divisor = 0;
        t->tmc = t->tdr;
        break;
    case 0x09u:
    case 0x0Au:
    case 0x0Bu:
    case 0x0Cu:
    case 0x0Du:
    case 0x0Eu:
    case 0x0Fu:
        t->current_divisor = g_mfptimer_prescaler[mode & 0x07u];
        t->prescaler_count = 0;
        mfptimer_set_enable(t, false);
        break;
    default:
        break;
    }
}

void mfptimer_set_data_register(MfpTimer *t, uint8_t data)
{
    t->tdr = data;
    if (t->tcr == 0u)
    {
        t->tmc = t->tdr;
    }
    else if (t->tcr == TCR_STATE_TCR_TIMER_EVENT)
    {
        t->tmc = t->tdr;
    }
}

void mfptimer_clear(MfpTimer *t)
{
    t->tmc = 0u;
    t->tcr = 0u;
    t->tdr = 0u;
    t->is_enabled = false;
    t->in_latch = false;
    t->out_latch = false;
    t->prescaler_count = 0;
    t->current_divisor = 0;
}
