/*
 * eth_mfp.c - Motorola MC68901 MFP (port of RetroCore MC68901MFP.cs).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Register access: the MFP ignores A0 (address |= 1), register index =
 * (address - start) >> 1. Out-of-range indexes read 0 and ignore writes,
 * as the C# switch's default case does.
 * The C# Logger.Log calls (Device level diagnostics) are not ported here.
 */

#include "eth_mfp.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static bool has16(uint16_t value, uint16_t flag)
{
    return (uint16_t)(value & flag) == flag;
}

static bool has8(uint8_t value, uint8_t flag)
{
    return (uint8_t)(value & flag) == flag;
}

static void mfp_check_interrupts(MC68901MFP *m);

/* UpdateIEO */
static void mfp_update_ieo(MC68901MFP *m)
{
    bool ieo_active = m->iei && (m->regs.isr == 0u);

    if (m->ev.on_ieo != NULL)
    {
        m->ev.on_ieo(m->ev.ctx, ieo_active);
    }
}

/* take_interrupt */
static void mfp_take_interrupt(MC68901MFP *m, uint16_t ivn)
{
    m->regs.ipr = (uint16_t)(m->regs.ipr | ivn);
    mfp_check_interrupts(m);
}

/* tryTakeInterrupt */
static void mfp_try_take_interrupt(MC68901MFP *m, uint16_t ivn)
{
    if (has16(m->regs.ier, ivn))
    {
        mfp_take_interrupt(m, ivn);
    }
}

/* Usart_OnSerialOutput */
static void mfp_usart_on_serial_output(void *ctx, bool state)
{
    MC68901MFP *m = (MC68901MFP *)ctx;

    if (m->ev.on_serial_output != NULL)
    {
        m->ev.on_serial_output(m->ev.ctx, state);
    }
}

/* Usart_OnTakeInterrupt */
static void mfp_usart_on_take_interrupt(void *ctx, uint16_t vector)
{
    mfp_try_take_interrupt((MC68901MFP *)ctx, vector);
}

/* Regs_OnTakeInterrupt */
static void mfp_regs_on_take_interrupt(void *ctx, uint16_t vector)
{
    MC68901MFP *m = (MC68901MFP *)ctx;
    int name = -1;

    mfp_try_take_interrupt(m, vector);
    switch (vector)
    {
    case INTERRUPT_VECTOR_NUMBER_IR_TIMER_A:
        name = MFP_TIMER_NAME_A;
        break;
    case INTERRUPT_VECTOR_NUMBER_IR_TIMER_B:
        name = MFP_TIMER_NAME_B;
        break;
    case INTERRUPT_VECTOR_NUMBER_IR_TIMER_C:
        name = MFP_TIMER_NAME_C;
        break;
    case INTERRUPT_VECTOR_NUMBER_IR_TIMER_D:
        name = MFP_TIMER_NAME_D;
        break;
    default:
        break;
    }
    if ((name >= 0) && (m->ev.on_timer_interrupt != NULL))
    {
        m->ev.on_timer_interrupt(m->ev.ctx, (MfpTimerName)name);
    }
}

/* Regs_OnTimerOutput */
static void mfp_regs_on_timer_output(void *ctx, MfpTimerName name, bool state)
{
    MC68901MFP *m = (MC68901MFP *)ctx;

    if (m->ev.on_timer_output != NULL)
    {
        m->ev.on_timer_output(m->ev.ctx, name, state);
    }
}

void mfp_create(MC68901MFP *m, uint32_t start_address, uint32_t length, const MfpEvents *events)
{
    if (m == NULL)
    {
        return;
    }
    memset(m, 0, sizeof *m);
    if (events != NULL)
    {
        m->ev = *events;
    }
    m->start_address = start_address;
    m->end_address = start_address + length - 1u;
    m->use_system_vector_mapping = false;
    m->cycle_exact_cpu = false;
    m->timer_b_event_cycle_pos = -1;
    m->current_line_cycle = 0;
    m->iei = true;
    m->last_irq_state = false;
    m->separate_timer_clock = false;

    mfpregs_create(&m->regs);
    m->regs.on_timer_output = mfp_regs_on_timer_output;
    m->regs.on_take_interrupt = mfp_regs_on_take_interrupt;
    m->regs.ctx = m;
    m->regs.usart.on_serial_output = mfp_usart_on_serial_output;
    m->regs.usart.on_take_interrupt = mfp_usart_on_take_interrupt;
    m->regs.usart.ctx = m;
}

bool mfp_is_mapped_address(const MC68901MFP *m, uint32_t address)
{
    return (address >= m->start_address) && (address <= m->end_address);
}

void mfp_reset(MC68901MFP *m)
{
    mfpregs_clear(&m->regs);
    m->last_irq_state = false;
    if (m->ev.on_serial_output != NULL)
    {
        m->ev.on_serial_output(m->ev.ctx, true);
    }
}

void mfp_set_timer_clock(MC68901MFP *m, int xtal_speed)
{
    /* trivial: Emulated.HW/Motorola/MFP/MC68901/MC68901MFP.cs:219 */
    (void)m;
    (void)xtal_speed;
}

void mfp_set_iei(MC68901MFP *m, bool iei)
{
    m->iei = iei;
}

/* map2register */
static int mfp_map2register(const MC68901MFP *m, uint32_t address)
{
    int reg_address = (int)(address - m->start_address);
    reg_address >>= 1;
    return reg_address;
}

uint8_t mfp_read(MC68901MFP *m, uint32_t address)
{
    int reg;
    uint8_t retval = 0u;

    address |= 0x01u;
    reg = mfp_map2register(m, address);

    switch (reg)
    {
    case MFP_REGISTER_GPDR:
        retval = (uint8_t)((m->regs.gpip & m->regs.ddr) | (m->regs.gpio_input & (uint8_t)~m->regs.ddr));
        break;
    case MFP_REGISTER_AER:
        retval = m->regs.aer;
        break;
    case MFP_REGISTER_DDR:
        retval = m->regs.ddr;
        break;
    case MFP_REGISTER_IERA:
        retval = mfpregs_get_ier_a(&m->regs);
        break;
    case MFP_REGISTER_IERB:
        retval = mfpregs_get_ier_b(&m->regs);
        break;
    case MFP_REGISTER_IPRA:
        retval = mfpregs_get_ipr_a(&m->regs);
        break;
    case MFP_REGISTER_IPRB:
        retval = mfpregs_get_ipr_b(&m->regs);
        break;
    case MFP_REGISTER_ISRA:
        retval = mfpregs_get_isr_a(&m->regs);
        break;
    case MFP_REGISTER_ISRB:
        retval = mfpregs_get_isr_b(&m->regs);
        break;
    case MFP_REGISTER_IMRA:
        retval = mfpregs_get_imr_a(&m->regs);
        break;
    case MFP_REGISTER_IMRB:
        retval = mfpregs_get_imr_b(&m->regs);
        break;
    case MFP_REGISTER_VR:
        retval = m->regs.vr;
        break;
    case MFP_REGISTER_TACR:
        retval = m->regs.timer_a.tcr;
        break;
    case MFP_REGISTER_TBCR:
        retval = m->regs.timer_b.tcr;
        break;
    case MFP_REGISTER_TCDCR:
    {
        uint8_t tcr_d = (uint8_t)(m->regs.timer_d.tcr & 0x07u);
        uint8_t tcr_c = (uint8_t)(m->regs.timer_c.tcr & 0x07u);
        retval = (uint8_t)(tcr_d | (tcr_c << 4u));
        break;
    }
    case MFP_REGISTER_TADR:
        retval = m->regs.timer_a.tmc;
        break;
    case MFP_REGISTER_TBDR:
        retval = m->regs.timer_b.tmc;
        if (!m->cycle_exact_cpu && (m->regs.timer_b.tcr == TCR_STATE_TCR_TIMER_EVENT) &&
            (m->timer_b_event_cycle_pos >= 0))
        {
            int pos_read = m->current_line_cycle + 4;
            if ((m->timer_b_event_cycle_pos > m->current_line_cycle) &&
                (m->timer_b_event_cycle_pos <= pos_read))
            {
                retval = (uint8_t)(retval - 1u);
                if (retval == 0u)
                {
                    retval = m->regs.timer_b.tdr;
                }
            }
        }
        break;
    case MFP_REGISTER_TCDR:
        retval = m->regs.timer_c.tmc;
        break;
    case MFP_REGISTER_TDDR:
        retval = m->regs.timer_d.tmc;
        break;
    case MFP_REGISTER_SCR:
        retval = m->regs.usart.scr;
        break;
    case MFP_REGISTER_UCR:
        retval = m->regs.usart.ucr;
        break;
    case MFP_REGISTER_RSR:
        retval = m->regs.usart.rsr;
        m->regs.usart.rsr = (uint8_t)(m->regs.usart.rsr & ~RSR_FLAGS_RSR_OVERRUN_ERROR);
        break;
    case MFP_REGISTER_TSR:
        retval = m->regs.usart.tsr;
        if (!m->regs.usart.underrun)
        {
            m->regs.usart.tsr = (uint8_t)(m->regs.usart.tsr & ~TSR_FLAGS_TSR_UNDERRUN_ERROR);
        }
        break;
    case MFP_REGISTER_UDR:
        retval = usart_read_udr(&m->regs.usart);
        break;
    default:
        break;
    }
    return retval;
}

/* gpio_output */
static void mfp_gpio_output(MC68901MFP *m)
{
    uint8_t new_gpio_output = (uint8_t)(m->regs.gpip & m->regs.ddr);

    if (m->regs.gpio_output != new_gpio_output)
    {
        m->regs.gpio_output = new_gpio_output;
        if (m->ev.on_gpio != NULL)
        {
            m->ev.on_gpio(m->ev.ctx, m->regs.gpio_output);
        }
    }
}

/* UpdateGpipInterrupts */
static void mfp_update_gpip_interrupts(MC68901MFP *m, uint8_t gpip_old, uint8_t gpip_new, uint8_t aer_old,
                                       uint8_t aer_new)
{
    uint8_t state_old = (uint8_t)(gpip_old ^ aer_old);
    uint8_t state_new = (uint8_t)(gpip_new ^ aer_new);

    for (int bit = 0; bit < 8; bit++)
    {
        uint8_t mask = (uint8_t)(1u << (unsigned)bit);

        if ((m->regs.ddr & mask) != 0u)
        {
            continue;
        }
        if ((state_old & mask) == (state_new & mask))
        {
            continue;
        }
        if ((gpip_new & mask) == (aer_new & mask))
        {
            if ((m->regs.ier & g_mfp_int_mask_gpio[bit]) != 0u)
            {
                mfp_take_interrupt(m, g_mfp_int_mask_gpio[bit]);
            }
        }
    }
}

void mfp_write(MC68901MFP *m, uint32_t address, uint8_t value)
{
    int reg;

    address |= 0x01u;
    reg = mfp_map2register(m, address);

    if (m->ev.on_register_write != NULL)
    {
        m->ev.on_register_write(m->ev.ctx, (MFPRegister)reg, value);
    }

    switch (reg)
    {
    case MFP_REGISTER_GPDR:
    {
        uint8_t gpip_old = m->regs.gpip;
        m->regs.gpip = value;
        mfp_gpio_output(m);
        mfp_update_gpip_interrupts(m, gpip_old, value, m->regs.aer, m->regs.aer);
        break;
    }
    case MFP_REGISTER_AER:
    {
        uint8_t aer_old = m->regs.aer;
        m->regs.aer = value;
        mfp_update_gpip_interrupts(m, m->regs.gpio_input, m->regs.gpio_input, aer_old, value);
        break;
    }
    case MFP_REGISTER_DDR:
        m->regs.ddr = value;
        mfp_gpio_output(m);
        break;
    case MFP_REGISTER_IERA:
        mfpregs_set_ier_a(&m->regs, value);
        m->regs.ipr = (uint16_t)(m->regs.ipr & m->regs.ier);
        mfp_check_interrupts(m);
        break;
    case MFP_REGISTER_IERB:
        mfpregs_set_ier_b(&m->regs, value);
        m->regs.ipr = (uint16_t)(m->regs.ipr & m->regs.ier);
        mfp_check_interrupts(m);
        break;
    case MFP_REGISTER_IPRA:
        m->regs.ipr = (uint16_t)(m->regs.ipr & (((unsigned)value << 8u) | (m->regs.ipr & 0xFFu)));
        mfp_check_interrupts(m);
        break;
    case MFP_REGISTER_IPRB:
        m->regs.ipr = (uint16_t)(m->regs.ipr & ((m->regs.ipr & 0xFF00u) | value));
        mfp_check_interrupts(m);
        break;
    case MFP_REGISTER_ISRA:
        m->regs.isr = (uint16_t)(m->regs.isr & (((unsigned)value << 8u) | (m->regs.isr & 0xFFu)));
        mfp_update_ieo(m);
        break;
    case MFP_REGISTER_ISRB:
        m->regs.isr = (uint16_t)(m->regs.isr & ((m->regs.isr & 0xFF00u) | value));
        mfp_update_ieo(m);
        break;
    case MFP_REGISTER_IMRA:
        mfpregs_set_imr_a(&m->regs, value);
        mfp_check_interrupts(m);
        break;
    case MFP_REGISTER_IMRB:
        mfpregs_set_imr_b(&m->regs, value);
        mfp_check_interrupts(m);
        break;
    case MFP_REGISTER_VR:
    {
        uint8_t old_vr = m->regs.vr;
        m->regs.vr = value;
        if ((((old_vr ^ value) & 0x08u) != 0u) && ((value & 0x08u) == 0u))
        {
            m->regs.isr = 0u;
            mfp_update_ieo(m);
        }
        break;
    }
    case MFP_REGISTER_TACR:
        mfptimer_set_control_register(&m->regs.timer_a, (uint8_t)(value & 0x1Fu));
        break;
    case MFP_REGISTER_TBCR:
        mfptimer_set_control_register(&m->regs.timer_b, (uint8_t)(value & 0x1Fu));
        break;
    case MFP_REGISTER_TCDCR:
        mfptimer_set_control_register(&m->regs.timer_d, (uint8_t)(value & 0x07u));
        mfptimer_set_control_register(&m->regs.timer_c, (uint8_t)((value >> 4u) & 0x07u));
        break;
    case MFP_REGISTER_TADR:
        mfptimer_set_data_register(&m->regs.timer_a, value);
        break;
    case MFP_REGISTER_TBDR:
        mfptimer_set_data_register(&m->regs.timer_b, value);
        break;
    case MFP_REGISTER_TCDR:
        mfptimer_set_data_register(&m->regs.timer_c, value);
        break;
    case MFP_REGISTER_TDDR:
        mfptimer_set_data_register(&m->regs.timer_d, value);
        break;
    case MFP_REGISTER_SCR:
        m->regs.usart.scr = value;
        m->regs.usart.scr_parity = mfp_is_bit_count_odd(value);
        break;
    case MFP_REGISTER_UCR:
        m->regs.usart.ucr = value;
        break;
    case MFP_REGISTER_RSR:
    {
        Usart *u = &m->regs.usart;
        uint8_t rsr = value;
        if (!has8(rsr, RSR_FLAGS_RSR_RCV_ENABLE))
        {
            u->rsr = 0u;
        }
        else
        {
            u->rsr = (uint8_t)(u->rsr | RSR_FLAGS_RSR_RCV_ENABLE);
        }
        if (has8(rsr, RSR_FLAGS_RSR_SYNC_STRIP_ENABLE))
        {
            u->rsr = (uint8_t)(u->rsr | RSR_FLAGS_RSR_SYNC_STRIP_ENABLE);
        }
        else
        {
            u->rsr = (uint8_t)(u->rsr & ~RSR_FLAGS_RSR_SYNC_STRIP_ENABLE);
        }
        if ((u->ucr & UCR_FLAGS_UCR_START_STOP_1_2) == UCR_FLAGS_UCR_START_STOP_0_0)
        {
            if (has8(rsr, RSR_FLAGS_RSR_FOUND_SEARCH))
            {
                u->rsr = (uint8_t)(u->rsr | RSR_FLAGS_RSR_FOUND_SEARCH);
            }
            else
            {
                u->rsr = (uint8_t)(u->rsr & ~RSR_FLAGS_RSR_FOUND_SEARCH);
            }
        }
        break;
    }
    case MFP_REGISTER_TSR:
    {
        Usart *u = &m->regs.usart;
        uint8_t keep = (uint8_t)(TSR_FLAGS_TSR_BUFFER_EMPTY | TSR_FLAGS_TSR_UNDERRUN_ERROR |
                                 TSR_FLAGS_TSR_END_OF_XMIT);
        u->tsr = (uint8_t)((u->tsr & keep) | (value & (uint8_t)~keep));
        if (!has8(value, TSR_FLAGS_TSR_XMIT_ENABLE))
        {
            u->tsr = (uint8_t)(u->tsr & ~TSR_FLAGS_TSR_UNDERRUN_ERROR);
            u->underrun = false;
            if (u->tbits == 0u)
            {
                bool status = (u->tsr & TSR_FLAGS_TSR_OUTPUT_MASK) != TSR_FLAGS_TSR_OUTPUT_LOW;
                if (m->ev.on_serial_output != NULL)
                {
                    m->ev.on_serial_output(m->ev.ctx, status);
                }
            }
        }
        else
        {
            u->tsr = (uint8_t)(u->tsr & ~TSR_FLAGS_TSR_END_OF_XMIT);
        }
        break;
    }
    case MFP_REGISTER_UDR:
        m->regs.usart.transmit_buffer = value;
        m->regs.usart.tsr = (uint8_t)(m->regs.usart.tsr & ~TSR_FLAGS_TSR_BUFFER_EMPTY);
        if (m->ev.on_serial_byte_transmit != NULL)
        {
            m->ev.on_serial_byte_transmit(m->ev.ctx, value);
        }
        break;
    default:
        break;
    }
}

/* GetDaisyChainVector */
static uint8_t mfp_get_daisy_chain_vector(MC68901MFP *m)
{
    if (m->ev.daisy_chain_callback != NULL)
    {
        return m->ev.daisy_chain_callback(m->ev.ctx);
    }
    return MFP_SPURIOUS_VECTOR;
}

/* MapMFPVectorToSystem */
static uint8_t mfp_map_mfp_vector_to_system(const MC68901MFP *m, int channel, uint16_t vector_type)
{
    switch (vector_type)
    {
    case INTERRUPT_VECTOR_NUMBER_IR_GPIP_7:
        return 117u; /* cs: Emulated.HW/Motorola/MFP/MC68901/MC68901MFP.cs:665 */
    case INTERRUPT_VECTOR_NUMBER_IR_GPIP_6:
        return 116u;
    case INTERRUPT_VECTOR_NUMBER_IR_RCV_BUFFER_FULL:
        return 114u;
    case INTERRUPT_VECTOR_NUMBER_IR_RCV_ERROR:
        return 113u;
    case INTERRUPT_VECTOR_NUMBER_IR_XMIT_BUFFER_EMPTY:
        return 112u;
    case INTERRUPT_VECTOR_NUMBER_IR_XMIT_ERROR:
        return 111u;
    case INTERRUPT_VECTOR_NUMBER_IR_GPIP_5:
        return 107u;
    default:
        return (uint8_t)((m->regs.vr & 0xF0u) | (unsigned)channel);
    }
}

uint8_t mfp_get_vector(MC68901MFP *m)
{
    uint16_t isr_val;

    if (!m->iei)
    {
        return mfp_get_daisy_chain_vector(m);
    }

    isr_val = m->regs.isr;
    for (int ch = 15; ch >= 0; ch--)
    {
        uint16_t ivn = (uint16_t)(1u << (unsigned)ch);

        if (has16(m->regs.imr, ivn) && has16(m->regs.ipr, ivn))
        {
            uint16_t higher_priority_mask = (uint16_t)~(((1u << (unsigned)ch) << 1u) - 1u);
            uint8_t vector;

            if ((isr_val & higher_priority_mask) != 0u)
            {
                continue;
            }
            if ((m->regs.vr & 0x08u) != 0u)
            {
                m->regs.isr = (uint16_t)(m->regs.isr | ivn);
                mfp_update_ieo(m);
            }
            m->regs.ipr = (uint16_t)(m->regs.ipr & (uint16_t)~ivn);

            m->last_irq_state = false;
            if (m->ev.on_irq != NULL)
            {
                m->ev.on_irq(m->ev.ctx, false);
            }
            mfp_check_interrupts(m);

            if (m->use_system_vector_mapping)
            {
                vector = mfp_map_mfp_vector_to_system(m, ch, ivn);
            }
            else
            {
                vector = (uint8_t)((m->regs.vr & 0xF0u) | (unsigned)ch);
            }
            return vector;
        }
    }
    return mfp_get_daisy_chain_vector(m);
}

uint8_t mfp_handle_interrupt_acknowledge(MC68901MFP *m)
{
    return mfp_get_vector(m);
}

uint8_t mfp_get_interrupt_vector(MC68901MFP *m)
{
    return mfp_get_vector(m);
}

/* check_interrupts */
static void mfp_check_interrupts(MC68901MFP *m)
{
    bool any_serviceable = false;
    uint16_t ipr;
    uint16_t imr;
    uint16_t isr;

    if (!m->iei)
    {
        if (m->last_irq_state)
        {
            m->last_irq_state = false;
            if (m->ev.on_irq != NULL)
            {
                m->ev.on_irq(m->ev.ctx, false);
            }
        }
        return;
    }

    ipr = m->regs.ipr;
    imr = m->regs.imr;
    isr = m->regs.isr;
    for (int ch = 15; ch >= 0; ch--)
    {
        uint16_t bit = (uint16_t)(1u << (unsigned)ch);
        if ((ipr & imr & bit) != 0u)
        {
            uint16_t higher_priority_mask = (uint16_t)~(((unsigned)bit << 1u) - 1u);
            if ((isr & higher_priority_mask) == 0u)
            {
                any_serviceable = true;
                break;
            }
        }
    }

    if (any_serviceable != m->last_irq_state)
    {
        m->last_irq_state = any_serviceable;
        if (m->ev.on_irq != NULL)
        {
            m->ev.on_irq(m->ev.ctx, any_serviceable);
        }
    }
}

void mfp_trigger_timer_c_interrupt(MC68901MFP *m)
{
    mfp_try_take_interrupt(m, INTERRUPT_VECTOR_NUMBER_IR_TIMER_C);
}

void mfp_trigger_usart_receive_interrupt(MC68901MFP *m)
{
    mfp_try_take_interrupt(m, INTERRUPT_VECTOR_NUMBER_IR_RCV_BUFFER_FULL);
}

void mfp_trigger_usart_receive_error(MC68901MFP *m)
{
    mfp_try_take_interrupt(m, INTERRUPT_VECTOR_NUMBER_IR_RCV_ERROR);
}

void mfp_trigger_usart_transmit_interrupt(MC68901MFP *m)
{
    mfp_try_take_interrupt(m, INTERRUPT_VECTOR_NUMBER_IR_XMIT_BUFFER_EMPTY);
}

void mfp_trigger_usart_transmit_error(MC68901MFP *m)
{
    mfp_try_take_interrupt(m, INTERRUPT_VECTOR_NUMBER_IR_XMIT_ERROR);
}

void mfp_trigger_software_interrupt(MC68901MFP *m, int channel, uint8_t vector_base)
{
    uint16_t ivn;

    if ((channel < 0) || (channel > 15))
    {
        return;
    }
    ivn = (uint16_t)(1u << (unsigned)channel);
    m->regs.vr = (uint8_t)((vector_base & 0xF0u) | 0x08u);
    m->regs.ier = (uint16_t)(m->regs.ier | ivn);
    m->regs.imr = (uint16_t)(m->regs.imr | ivn);
    mfp_take_interrupt(m, ivn);
}

void mfp_trigger_interrupt(MC68901MFP *m, int gpio_pin)
{
    if ((gpio_pin >= 0) && (gpio_pin < 8))
    {
        mfp_try_take_interrupt(m, g_mfp_int_mask_gpio[gpio_pin]);
    }
}

const char *mfp_get_system_vector_name(uint8_t vector, char *buf, size_t len)
{
    switch (vector)
    {
    case 117u:
        return "Write violation by 68000";
    case 116u:
        return "ND-100 requesting interrupt";
    case 114u:
        return "Receive buffer full";
    case 113u:
        return "Receive error";
    case 112u:
        return "Transmit buffer empty";
    case 111u:
        return "Transmit error";
    case 107u:
        return "LANCE memory access error";
    case 105u:
        return "Real-time clock (Timer C)";
    default:
        if ((buf == NULL) || (len == 0u))
        {
            return "Unknown vector";
        }
        (void)snprintf(buf, len, "Unknown vector %u", (unsigned)vector);
        return buf;
    }
}

/* TimerInput */
static void mfp_timer_input(MC68901MFP *m, int index, bool new_state)
{
    int bit = (int)g_mfp_gpio_timer[index];
    bool active_edge = (m->regs.aer & (1u << (unsigned)bit)) != 0u;
    MfpTimer *t = (index == 0) ? &m->regs.timer_a : &m->regs.timer_b;
    uint8_t pulse = (uint8_t)(t->tcr & 0x0Fu);

    switch (pulse)
    {
    case TCR_STATE_TCR_TIMER_EVENT:
        if ((t->in_latch != active_edge) && (new_state == active_edge))
        {
            mfptimer_tick_event_count(t);
        }
        break;
    case TCR_STATE_TCR_TIMER_PULSE_4:
    case TCR_STATE_TCR_TIMER_PULSE_10:
    case TCR_STATE_TCR_TIMER_PULSE_16:
    case TCR_STATE_TCR_TIMER_PULSE_50:
    case TCR_STATE_TCR_TIMER_PULSE_64:
    case TCR_STATE_TCR_TIMER_PULSE_100:
    case TCR_STATE_TCR_TIMER_PULSE_200:
        mfptimer_set_enable(t, new_state == active_edge);
        if ((t->in_latch == active_edge) && (new_state != active_edge))
        {
            mfp_try_take_interrupt(m, g_mfp_int_mask_gpio[bit]);
        }
        break;
    case TCR_STATE_TCR_TIMER_RESET:
        break;
    default:
        break;
    }
    t->in_latch = new_state;
}

void mfp_timer_input_a(MC68901MFP *m, bool new_state)
{
    mfp_timer_input(m, 0, new_state);
}

void mfp_timer_input_b(MC68901MFP *m, bool new_state)
{
    mfp_timer_input(m, 1, new_state);
}

void mfp_clock(MC68901MFP *m)
{
    if (!m->separate_timer_clock)
    {
        mfpregs_tick(&m->regs);
    }
}

void mfp_clock_timers(MC68901MFP *m)
{
    mfpregs_tick(&m->regs);
}

void mfp_set_serial_input(MC68901MFP *m, bool state)
{
    usart_set_serial_input(&m->regs.usart, state);
}

void mfp_set_receiver_clock(MC68901MFP *m, bool state)
{
    usart_set_receiver_clock(&m->regs.usart, state);
}

void mfp_set_transmitter_clock(MC68901MFP *m, bool state)
{
    usart_set_transmitter_clock(&m->regs.usart, state);
}

void mfp_receive_serial_byte(MC68901MFP *m, uint8_t data)
{
    m->regs.usart.receive_buffer = data;
    m->regs.usart.rsr = (uint8_t)(m->regs.usart.rsr | RSR_FLAGS_RSR_BUFFER_FULL);
    usart_rx_buffer_full(&m->regs.usart);
}

void mfp_gpio_input(MC68901MFP *m, int bit, bool state)
{
    bool current_state = (m->regs.gpio_input & (1u << (unsigned)bit)) != 0u;
    bool aer_stat = (m->regs.aer & (1u << (unsigned)bit)) != 0u;

    if (state != current_state)
    {
        if (state == aer_stat)
        {
            if ((m->regs.ier & g_mfp_int_mask_gpio[bit]) != 0u)
            {
                mfp_take_interrupt(m, g_mfp_int_mask_gpio[bit]);
            }
        }
        if (state)
        {
            m->regs.gpio_input = (uint8_t)(m->regs.gpio_input | (1u << (unsigned)bit));
        }
        else
        {
            m->regs.gpio_input = (uint8_t)(m->regs.gpio_input & ~(1u << (unsigned)bit));
        }
    }
}

bool mfp_get_gpio_pin_status(const MC68901MFP *m, int bit)
{
    return ((m->regs.gpio_output & m->regs.ddr) & (1u << (unsigned)bit)) != 0u;
}

bool mfp_is_bit_count_odd(uint8_t b)
{
    int count = 0;
    while (b != 0u)
    {
        b = (uint8_t)(b & (uint8_t)(b - 1u));
        count++;
    }
    return (count % 2) != 0;
}
