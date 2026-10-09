/*
 * eth_mfp_usart.c - MC68901 USART (port of RetroCore Usart.cs).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Copied behaviour worth knowing (Usart.cs at RetroCore 935163f):
 *  - tx_frame_load computes tbits as (9 - (ucr & WORD_LENGTH_MASK)) >> 5
 *    because "-" binds tighter than ">>" in C# - for 8-bit words that is 0,
 *    not 9 (the C# line reads "(byte)(9 - (ushort)(ucr & MASK) >> 5)");
 *  - rx_async_frame_complete's error-interrupt test includes
 *    "(ushort)IR_RCV_ERROR != 0", which is always true;
 *  - rx_sync_frame_complete is empty; synchronous mode is not supported.
 */

#include "eth_mfp.h"

#include <stddef.h>

static bool has(uint8_t value, uint8_t flags)
{
    /* C# Enum.HasFlag: every bit of flags set in value */
    return (uint8_t)(value & flags) == flags;
}

static void usart_set_serial_output(Usart *u, bool state)
{
    if (state != u->so)
    {
        u->so = state;
        if (u->on_serial_output != NULL)
        {
            u->on_serial_output(u->ctx, state);
        }
    }
}

static void usart_try_take_interrupt(Usart *u, uint16_t ivn)
{
    if (u->on_take_interrupt != NULL)
    {
        u->on_take_interrupt(u->ctx, ivn);
    }
}

void usart_create(Usart *u)
{
    if (u == NULL)
    {
        return;
    }
    u->on_serial_output = NULL;
    u->on_take_interrupt = NULL;
    u->ctx = NULL;
    u->scr_parity = false;
}

void usart_clear(Usart *u)
{
    u->rsr = 0u;
    u->tsr = TSR_FLAGS_TSR_BUFFER_EMPTY;
    u->underrun = false;
    u->rclk = 0u;
    u->tclk = 0u;
    u->scr = 0u;
    u->ucr = 0u;
    u->rframe = 0x100u;
    u->next_rsr = 0u;
    u->si_scan = 0xFFu;
    u->si = true;
    u->last_si = true;
    u->rc = true;
    u->tc = true;
    u->so = true;
    u->rbits = 0u;
    u->tbits = 0u;
    u->osr = 0u;
    u->rparity = false;
    u->tparity = false;
    u->transmit_buffer = 0u;
    u->receive_buffer = 0u;
}

uint8_t usart_read_udr(Usart *u)
{
    u->rsr = (uint8_t)(u->rsr & ~RSR_FLAGS_RSR_BUFFER_FULL);
    if (u->next_rsr != 0u)
    {
        u->rsr = (uint8_t)(u->rsr | u->next_rsr);
        u->next_rsr = 0u;
        usart_rx_error(u);
    }
    if (has(u->rsr, RSR_FLAGS_RSR_BREAK) && ((u->rframe & (1u << 9)) != 0u))
    {
        u->rsr = (uint8_t)(u->rsr & ~RSR_FLAGS_RSR_BREAK);
        usart_rx_error(u);
    }
    return u->receive_buffer;
}

void usart_tx_buffer_empty(Usart *u)
{
    usart_try_take_interrupt(u, INTERRUPT_VECTOR_NUMBER_IR_XMIT_BUFFER_EMPTY);
}

void usart_tx_error(Usart *u)
{
    usart_try_take_interrupt(u, INTERRUPT_VECTOR_NUMBER_IR_XMIT_ERROR);
}

void usart_rx_buffer_full(Usart *u)
{
    usart_try_take_interrupt(u, INTERRUPT_VECTOR_NUMBER_IR_RCV_BUFFER_FULL);
}

void usart_rx_error(Usart *u)
{
    usart_try_take_interrupt(u, INTERRUPT_VECTOR_NUMBER_IR_RCV_ERROR);
}

static void usart_tx_clock(Usart *u);

void usart_set_receiver_clock(Usart *u, bool state)
{
    if (state != u->rc)
    {
        u->rc = state;
        if (state && has(u->rsr, RSR_FLAGS_RSR_RCV_ENABLE) &&
            ((u->tsr & TSR_FLAGS_TSR_OUTPUT_MASK) != TSR_FLAGS_TSR_OUTPUT_LOOP))
        {
            usart_rx_clock(u, u->si);
        }
    }
}

void usart_set_serial_input(Usart *u, bool state)
{
    u->si = state;
}

void usart_set_transmitter_clock(Usart *u, bool state)
{
    if (state != u->tc)
    {
        u->tc = state;
        if (!state &&
            (has(u->tsr, TSR_FLAGS_TSR_XMIT_ENABLE) || !has(u->tsr, TSR_FLAGS_TSR_END_OF_XMIT)))
        {
            usart_tx_clock(u);
        }
        else if (state && has(u->rsr, RSR_FLAGS_RSR_RCV_ENABLE) &&
                 ((u->tsr & TSR_FLAGS_TSR_OUTPUT_LOOP) == TSR_FLAGS_TSR_OUTPUT_LOOP))
        {
            usart_rx_clock(u, u->so);
        }
    }
}

static void usart_rx_frame_start(Usart *u)
{
    u->rframe = 0u;
    u->rbits = (uint8_t)((uint16_t)(u->ucr & UCR_FLAGS_UCR_WORD_LENGTH_MASK) >> 5u);
    u->rparity = !has(u->ucr, UCR_FLAGS_UCR_PARITY_EVEN);
}

void usart_rx_sync_found(Usart *u)
{
    u->rsr = (uint8_t)(u->rsr | RSR_FLAGS_RSR_FOUND_SEARCH);
    usart_rx_error(u);
}

static void usart_rx_async_frame_complete(Usart *u)
{
    if (has(u->rsr, RSR_FLAGS_RSR_BUFFER_FULL))
    {
        if ((u->rframe == 0u) && !u->last_si)
        {
            u->next_rsr = (uint8_t)(u->next_rsr | RSR_FLAGS_RSR_BREAK);
        }
        else
        {
            u->next_rsr = (uint8_t)(u->next_rsr | RSR_FLAGS_RSR_OVERRUN_ERROR);
        }
    }
    else if (has(u->rsr, RSR_FLAGS_RSR_OVERRUN_ERROR))
    {
        if ((u->rframe == 0u) && !u->last_si)
        {
            u->rsr = (uint8_t)(u->rsr | RSR_FLAGS_RSR_BREAK);
        }
    }
    else
    {
        u->receive_buffer = (uint8_t)(u->rframe & 0xFFu);
        u->rsr = (uint8_t)(u->rsr | RSR_FLAGS_RSR_BUFFER_FULL);
        u->rsr = (uint8_t)(u->rsr & ~(RSR_FLAGS_RSR_PARITY_ERROR | RSR_FLAGS_RSR_FRAME_ERROR |
                                      RSR_FLAGS_RSR_BREAK));
        if (u->rparity)
        {
            u->rsr = (uint8_t)(u->rsr | RSR_FLAGS_RSR_PARITY_ERROR);
        }
        if (!u->last_si)
        {
            if (u->rframe == 0u)
            {
                u->rsr = (uint8_t)(u->rsr | RSR_FLAGS_RSR_BREAK);
            }
            else
            {
                u->rsr = (uint8_t)(u->rsr | RSR_FLAGS_RSR_FRAME_ERROR);
            }
        }
        /* C#: (rparity || !last_si) && ((ushort)IR_RCV_ERROR != 0) - always-true term kept */
        if ((u->rparity || !u->last_si) && (INTERRUPT_VECTOR_NUMBER_IR_RCV_ERROR != 0u))
        {
            usart_rx_error(u);
        }
        else
        {
            usart_rx_buffer_full(u);
        }
    }
}

void usart_rx_clock(Usart *u, bool si)
{
    bool rclk_sync;
    bool sync_mode;

    u->rclk = (uint8_t)(u->rclk + 1u);
    if (u->rclk >= 244u)
    {
        u->rclk = (uint8_t)(u->rclk & 15u);
    }

    u->si_scan = (uint8_t)((u->si_scan >> 1u) | (si ? 0x80u : 0u));

    rclk_sync = ((u->ucr & UCR_FLAGS_UCR_CLOCK_DIVIDE_16) == UCR_FLAGS_UCR_CLOCK_DIVIDE_1) ||
                ((u->rclk >= 4u) && ((u->si_scan & 0xE0u) == (u->last_si ? 0u : 0xE0u)));

    sync_mode = (u->ucr & UCR_FLAGS_UCR_START_STOP_1_2) == UCR_FLAGS_UCR_START_STOP_0_0;

    if (rclk_sync)
    {
        u->last_si = si;
        if (si && !sync_mode && !has(u->rsr, RSR_FLAGS_RSR_CHAR_IN_PROGRESS))
        {
            u->rframe = 0x100u;
            if (!has(u->rsr, RSR_FLAGS_RSR_BUFFER_FULL) && has(u->rsr, RSR_FLAGS_RSR_BREAK))
            {
                u->rsr = (uint8_t)(u->rsr & ~RSR_FLAGS_RSR_BREAK);
                usart_rx_error(u);
            }
        }
        u->rclk = 0u;
    }

    if (((u->ucr & UCR_FLAGS_UCR_CLOCK_DIVIDE_16) == UCR_FLAGS_UCR_CLOCK_DIVIDE_1) ||
        ((u->rclk & 15u) == 8u))
    {
        if (!sync_mode)
        {
            if (has(u->rsr, RSR_FLAGS_RSR_CHAR_IN_PROGRESS))
            {
                if (u->rbits > 8u)
                {
                    usart_rx_async_frame_complete(u);
                    u->rframe = (uint16_t)(u->last_si ? 0x100u : 0u);
                    u->rsr = (uint8_t)(u->rsr & ~RSR_FLAGS_RSR_CHAR_IN_PROGRESS);
                }
                else
                {
                    u->rframe = (uint16_t)((u->rframe >> 1u) | (u->last_si ? 0x100u : 0u));
                    if (u->last_si)
                    {
                        u->rparity = !u->rparity;
                    }
                    u->rbits = (uint8_t)(u->rbits + 1u);
                    if (u->rbits == 8u)
                    {
                        u->rframe = (uint16_t)(u->rframe >>
                                               ((unsigned)(u->ucr & UCR_FLAGS_UCR_WORD_LENGTH_MASK) >> 5u));
                        if (!has(u->ucr, UCR_FLAGS_UCR_PARITY_ENABLED))
                        {
                            u->rframe = (uint16_t)(u->rframe >> 1u);
                            u->rparity = false;
                            u->rbits = (uint8_t)(u->rbits + 1u);
                        }
                    }
                }
            }
            else if (!u->last_si && ((u->rframe & 0x100u) != 0u))
            {
                u->rsr = (uint8_t)(u->rsr | RSR_FLAGS_RSR_CHAR_IN_PROGRESS);
                usart_rx_frame_start(u);
            }
        }
    }
}

static void usart_tx_frame_load(Usart *u, uint8_t data)
{
    u->osr = data;
    /* C#: (byte)(9 - (ushort)(ucr & MASK) >> 5) == (9 - (ucr & MASK)) >> 5 */
    u->tbits = (uint8_t)((9 - (int)(uint16_t)(u->ucr & UCR_FLAGS_UCR_WORD_LENGTH_MASK)) >> 5);
    u->tparity = !has(u->ucr, UCR_FLAGS_UCR_PARITY_EVEN);
    if ((u->ucr & UCR_FLAGS_UCR_START_STOP_1_2) != UCR_FLAGS_UCR_START_STOP_0_0)
    {
        u->osr = (uint16_t)((u->osr << 1u) | (1u << u->tbits));
        u->tbits = (uint8_t)(u->tbits + 2u);
    }
}

static void usart_tx_clock(Usart *u)
{
    bool sync_mode;
    bool send_break;
    bool tbusy = false;

    if (u->tclk != 0u)
    {
        u->tclk = (uint8_t)(u->tclk - 1u);
        return;
    }

    u->underrun = false;
    sync_mode = (u->ucr & UCR_FLAGS_UCR_START_STOP_1_2) == UCR_FLAGS_UCR_START_STOP_0_0;

    if (u->tbits == (sync_mode ? 2u : 3u))
    {
        if (has(u->ucr, UCR_FLAGS_UCR_PARITY_ENABLED))
        {
            u->osr = (uint16_t)((u->osr << 1u) | (u->tparity ? 1u : 0u));
        }
        else
        {
            u->tbits = (uint8_t)(u->tbits - 1u);
        }
    }

    send_break = !sync_mode && has(u->tsr, TSR_FLAGS_TSR_BREAK);

    if (u->tbits != 0u)
    {
        u->tbits = (uint8_t)(u->tbits - 1u);
        if (u->tbits != 0u)
        {
            tbusy = true;
        }
        else if (!has(u->tsr, TSR_FLAGS_TSR_XMIT_ENABLE))
        {
            u->tsr = (uint8_t)(u->tsr | TSR_FLAGS_TSR_END_OF_XMIT);
            usart_tx_error(u);
            if (has(u->tsr, TSR_FLAGS_TSR_AUTO_TURNAROUND))
            {
                u->rsr = (uint8_t)(u->rsr | RSR_FLAGS_RSR_RCV_ENABLE);
                u->tsr = (uint8_t)(u->tsr & ~TSR_FLAGS_TSR_AUTO_TURNAROUND);
            }
        }
        else if (has(u->tsr, TSR_FLAGS_TSR_BUFFER_EMPTY) && !has(u->tsr, TSR_FLAGS_TSR_UNDERRUN_ERROR) &&
                 !send_break)
        {
            u->tsr = (uint8_t)(u->tsr | TSR_FLAGS_TSR_UNDERRUN_ERROR);
            u->underrun = true;
            usart_tx_error(u);
        }
    }

    if (!tbusy && has(u->tsr, TSR_FLAGS_TSR_XMIT_ENABLE))
    {
        if (!has(u->tsr, TSR_FLAGS_TSR_BUFFER_EMPTY) && !send_break)
        {
            u->tsr = (uint8_t)(u->tsr | TSR_FLAGS_TSR_BUFFER_EMPTY);
            usart_tx_buffer_empty(u);
            usart_tx_frame_load(u, u->transmit_buffer);
            tbusy = true;
        }
    }

    if (tbusy)
    {
        usart_set_serial_output(u, (u->osr & 1u) != 0u);
        if ((u->osr & 1u) != 0u)
        {
            u->tparity = !u->tparity;
        }
        u->osr = (uint16_t)(u->osr >> 1u);

        if ((u->tbits == 1u) &&
            ((u->ucr & UCR_FLAGS_UCR_START_STOP_1_2) >= UCR_FLAGS_UCR_START_STOP_1_15))
        {
            if (has(u->ucr, UCR_FLAGS_UCR_CLOCK_DIVIDE_16))
            {
                u->tclk = (uint8_t)(((u->ucr & UCR_FLAGS_UCR_START_STOP_1_2) == UCR_FLAGS_UCR_START_STOP_1_2)
                                        ? 31u
                                        : 23u);
            }
            else
            {
                u->tclk = (uint8_t)(((u->ucr & UCR_FLAGS_UCR_START_STOP_1_2) == UCR_FLAGS_UCR_START_STOP_1_2)
                                        ? 1u
                                        : 0u);
            }
        }
        else if (has(u->ucr, UCR_FLAGS_UCR_CLOCK_DIVIDE_16))
        {
            u->tclk = 15u;
        }
    }
    else if (!has(u->tsr, TSR_FLAGS_TSR_XMIT_ENABLE))
    {
        usart_set_serial_output(u, (u->tsr & TSR_FLAGS_TSR_OUTPUT_MASK) != TSR_FLAGS_TSR_OUTPUT_LOW);
    }
    else if (send_break)
    {
        usart_set_serial_output(u, false);
        u->tbits = 1u;
    }
    else
    {
        usart_set_serial_output(u, true);
    }
}
