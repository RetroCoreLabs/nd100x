/*
 * eth_mfp_regs.c - MC68901 register file (port of RetroCore Registers.cs).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * The 16 interrupt channels are one 16-bit value per register; register "A"
 * is the high byte and "B" the low byte, as in Registers.cs. The C# event
 * OnTimerElapsed is declared there but never raised; timer expiry is reported
 * through OnTakeInterrupt with the timer's channel bit.
 */

#include "eth_mfp.h"

#include <stddef.h>

/* Mapper (HelperEnum.cs) */
const uint16_t g_mfp_int_mask_gpio[8] = {
    INTERRUPT_VECTOR_NUMBER_IR_GPIP_0, INTERRUPT_VECTOR_NUMBER_IR_GPIP_1,
    INTERRUPT_VECTOR_NUMBER_IR_GPIP_2, INTERRUPT_VECTOR_NUMBER_IR_GPIP_3,
    INTERRUPT_VECTOR_NUMBER_IR_GPIP_4, INTERRUPT_VECTOR_NUMBER_IR_GPIP_5,
    INTERRUPT_VECTOR_NUMBER_IR_GPIP_6, INTERRUPT_VECTOR_NUMBER_IR_GPIP_7};

const uint16_t g_mfp_int_mask_timer[4] = {
    INTERRUPT_VECTOR_NUMBER_IR_TIMER_A, INTERRUPT_VECTOR_NUMBER_IR_TIMER_B,
    INTERRUPT_VECTOR_NUMBER_IR_TIMER_C, INTERRUPT_VECTOR_NUMBER_IR_TIMER_D};

const uint8_t g_mfp_gpio_timer[2] = {GPIP_GPIP_4, GPIP_GPIP_3};

/* Timer_OnTimerElapsed */
static void mfpregs_timer_on_timer_elapsed(void *ctx, MfpTimerName name)
{
    Registers *r = (Registers *)ctx;
    uint16_t ivn = g_mfp_int_mask_timer[(int)name];

    if (r->on_take_interrupt != NULL)
    {
        r->on_take_interrupt(r->ctx, ivn);
    }
}

/* Timers_OnTimerOutput */
static void mfpregs_timers_on_timer_output(void *ctx, MfpTimerName name, bool state)
{
    Registers *r = (Registers *)ctx;

    if (r->on_timer_output != NULL)
    {
        r->on_timer_output(r->ctx, name, state);
    }
}

void mfpregs_create(Registers *r)
{
    if (r == NULL)
    {
        return;
    }
    r->on_timer_output = NULL;
    r->on_timer_elapsed = NULL;
    r->on_take_interrupt = NULL;
    r->ctx = NULL;
    usart_create(&r->usart);

    mfptimer_create(&r->timer_a, MFP_TIMER_NAME_A);
    mfptimer_create(&r->timer_b, MFP_TIMER_NAME_B);
    mfptimer_create(&r->timer_c, MFP_TIMER_NAME_C);
    mfptimer_create(&r->timer_d, MFP_TIMER_NAME_D);
    r->timers[0] = &r->timer_a;
    r->timers[1] = &r->timer_b;
    r->timers[2] = &r->timer_c;
    r->timers[3] = &r->timer_d;
    for (int i = 0; i < 4; i++)
    {
        r->timers[i]->on_timer_output = mfpregs_timers_on_timer_output;
        r->timers[i]->on_timer_elapsed = mfpregs_timer_on_timer_elapsed;
        r->timers[i]->ctx = r;
    }
    mfpregs_clear(r);
}

void mfpregs_tick(Registers *r)
{
    for (int i = 0; i < 4; i++)
    {
        mfptimer_tick(r->timers[i]);
    }
}

void mfpregs_clear(Registers *r)
{
    usart_clear(&r->usart);
    r->imr = 0u;
    r->ipr = r->imr;
    for (int i = 0; i < 4; i++)
    {
        mfptimer_clear(r->timers[i]);
    }
    r->gpip = 0u;
    r->aer = 0u;
    r->ddr = 0u;
    r->ier = 0u;
    r->ipr = 0u;
    r->isr = 0u;
    r->imr = 0u;
    r->vr = 0u;
    r->gpio_input = 0u;
    r->gpio_output = 0xFFu;
}

uint8_t mfpregs_get_ier_a(const Registers *r)
{
    return (uint8_t)(r->ier >> 8u);
}

void mfpregs_set_ier_a(Registers *r, uint8_t v)
{
    r->ier = (uint16_t)(((unsigned)v << 8u) | (r->ier & 0xFFu));
}

uint8_t mfpregs_get_ier_b(const Registers *r)
{
    return (uint8_t)(r->ier & 0xFFu);
}

void mfpregs_set_ier_b(Registers *r, uint8_t v)
{
    r->ier = (uint16_t)((r->ier & 0xFF00u) | v);
}

uint8_t mfpregs_get_ipr_a(const Registers *r)
{
    return (uint8_t)(r->ipr >> 8u);
}

void mfpregs_set_ipr_a(Registers *r, uint8_t v)
{
    r->ipr = (uint16_t)(((unsigned)v << 8u) | (r->ipr & 0xFFu));
}

uint8_t mfpregs_get_ipr_b(const Registers *r)
{
    return (uint8_t)(r->ipr & 0xFFu);
}

void mfpregs_set_ipr_b(Registers *r, uint8_t v)
{
    r->ipr = (uint16_t)((r->ipr & 0xFF00u) | v);
}

uint8_t mfpregs_get_isr_a(const Registers *r)
{
    return (uint8_t)(r->isr >> 8u);
}

void mfpregs_set_isr_a(Registers *r, uint8_t v)
{
    r->isr = (uint16_t)(((unsigned)v << 8u) | (r->isr & 0xFFu));
}

uint8_t mfpregs_get_isr_b(const Registers *r)
{
    return (uint8_t)(r->isr & 0xFFu);
}

void mfpregs_set_isr_b(Registers *r, uint8_t v)
{
    r->isr = (uint16_t)((r->isr & 0xFF00u) | v);
}

uint8_t mfpregs_get_imr_a(const Registers *r)
{
    return (uint8_t)(r->imr >> 8u);
}

void mfpregs_set_imr_a(Registers *r, uint8_t v)
{
    r->imr = (uint16_t)(((unsigned)v << 8u) | (r->imr & 0xFFu));
}

uint8_t mfpregs_get_imr_b(const Registers *r)
{
    return (uint8_t)(r->imr & 0xFFu);
}

void mfpregs_set_imr_b(Registers *r, uint8_t v)
{
    r->imr = (uint16_t)((r->imr & 0xFF00u) | v);
}
