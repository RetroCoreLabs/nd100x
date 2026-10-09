/*
 * eth_iospace.c - Ethernet II card I/O space (port of RetroCore ETH_IOMem).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * handleIoRw is ported statement by statement, including its fall-through
 * order: the timer block at 0xEF01xx is decoded on the UNMIRRORED address
 * first; timer-control writes that are not a complete, valid command fall
 * through to the mirrored 0xEF00xx decode (the AM9519 reset path relies on
 * that). The C# Logger.Log calls are not ported.
 */

#include "eth_iospace.h"

#include <stddef.h>
#include <string.h>

/* ResetTimerController */
static void ethio_reset_timer_controller(ETH_IOMem *io)
{
    for (int i = 0; i < 15; i++)
    {
        io->timer_data[i] = ((i % 3) == 0) ? (uint16_t)0x0B00u : (uint16_t)0x0000u;
    }
    io->timer_data_index = 0;
    io->timer_counter1 = 0u;
    io->timer_load_reg1 = 0u;
    io->timer_armed1 = false;
    io->timer_interrupt_pending = false;
    io->timer_status = 0u;
    io->pending_timer_data_word = 0u;
    io->timer_data_high_byte_written = false;
    io->last_timer_command = 0u;
    io->timer_mode_reg1 = 0u;
    io->timer_prescaler = 0;
    io->reset_sequence_state = 0;
    io->timer_tick32 = 0u;
    io->timer_config_reg = 0u;
    io->stc_armed = false;
    io->stc_countdown = 0;
}

/* ResetSCIPChannels */
static void ethio_reset_scip_channels(ETH_IOMem *io)
{
    for (int i = 0; i < ETHIO_SCIP_SIDE_COUNT; i++)
    {
        io->scip_status[i] = 0u;
        io->scip_rx_data[i] = 0u;
        io->scip_rx_ready[i] = false;
        io->scip_cmd_count[i] = 0;
    }
}

bool ethio_is_timer_interrupt_pending(const ETH_IOMem *io)
{
    return io->timer_interrupt_pending;
}

void ethio_clear_timer_interrupt(ETH_IOMem *io)
{
    io->timer_interrupt_pending = false;
}

/* GetSCIPSide */
static int ethio_get_scip_side(uint8_t offset, int *reg_type)
{
    int block = (offset >> 6u) & 1;
    int side = (offset >> 5u) & 1;
    int group = (offset >> 4u) & 1;
    int channel_group;

    *reg_type = ((offset & 0x08u) != 0u) ? 1 : 0;
    channel_group = (block * 2) + group;
    return (channel_group * 2) + side;
}

/* HandleSCIPChannelAccess */
static uint8_t ethio_handle_scip_channel_access(ETH_IOMem *io, uint32_t address, uint8_t value, bool is_write)
{
    uint8_t offset = (uint8_t)(address & 0xFFu);
    int reg_type;
    int side;

    if (offset >= 0x80u)
    {
        return 0u;
    }

    side = ethio_get_scip_side(offset, &reg_type);
    if (reg_type == 0)
    {
        if (is_write)
        {
            io->scip_rx_data[side] = value;
            io->scip_rx_ready[side] = true;
            io->scip_status[side] = (uint8_t)(io->scip_status[side] | 0x01u);
            if (io->ev.on_scip_channel_tx != NULL)
            {
                io->ev.on_scip_channel_tx(io->ev.ctx, side);
            }
            return 0u;
        }
        else
        {
            uint8_t data = io->scip_rx_data[side];
            io->scip_rx_ready[side] = false;
            io->scip_status[side] = (uint8_t)(io->scip_status[side] & 0xFEu);
            return data;
        }
    }

    if (is_write)
    {
        io->scip_cmd_count[side]++;
        if ((value == 0x10u) && (io->scip_cmd_count[side] > 4))
        {
            io->scip_status[side] = (uint8_t)(io->scip_status[side] | 0x28u);
            if ((io->mfp_chip != NULL) && (side >= 0) && (side <= 7))
            {
                mfp_trigger_software_interrupt(io->mfp_chip, side, 0x40u);
            }
        }
        else if ((value == 0x13u) || (value == 0x30u))
        {
            io->scip_status[side] = (uint8_t)(io->scip_status[side] | 0x04u);
        }
        return 0u;
    }
    else
    {
        uint8_t status = io->scip_status[side];
        if (io->scip_rx_ready[side])
        {
            status = (uint8_t)(status | 0x01u);
        }
        return status;
    }
}

void ethio_create(ETH_IOMem *io, uint32_t start_address, uint32_t length, const EthIoEvents *events)
{
    if (io == NULL)
    {
        return;
    }
    memset(io, 0, sizeof *io);
    if (events != NULL)
    {
        io->ev = *events;
    }
    io->start_address = start_address;
    io->end_address = start_address + length - 1u;
    io->transceiver_power_enabled = false;
    ethio_reset_timer_controller(io);
    ethio_reset_scip_channels(io);
}

bool ethio_is_mapped_address(const ETH_IOMem *io, uint32_t address)
{
    return (address >= io->start_address) && (address <= io->end_address);
}

void ethio_clock(ETH_IOMem *io)
{
    io->timer_tick32++;

    if (io->stc_armed)
    {
        io->timer_counter1 = (uint16_t)io->stc_countdown;
        io->stc_countdown--;
        if (io->stc_countdown <= 0)
        {
            io->stc_countdown = ETHIO_STC_PERIOD_TICKS;
            io->timer_status = (uint8_t)(io->timer_status | 0x01u);
            io->timer_interrupt_pending = true;
            if (io->ev.on_timer_interrupt != NULL)
            {
                io->ev.on_timer_interrupt(io->ev.ctx);
            }
        }
    }

    if (io->timer_armed1 && (io->timer_counter1 > 0u))
    {
        io->timer_counter1 = (uint16_t)(io->timer_counter1 - 1u);
        if (io->timer_counter1 == 0u)
        {
            io->timer_status = (uint8_t)(io->timer_status | 0x01u);
            io->timer_interrupt_pending = true;
            if (io->ev.on_timer_interrupt != NULL)
            {
                io->ev.on_timer_interrupt(io->ev.ctx);
            }
            io->timer_armed1 = false;
        }
    }
}

uint8_t ethio_read(ETH_IOMem *io, uint32_t address)
{
    return ethio_handle_io_rw(io, address, 0u, false);
}

void ethio_reset(ETH_IOMem *io)
{
    io->transceiver_power_enabled = false;
    ethio_reset_timer_controller(io);
    ethio_reset_scip_channels(io);
}

void ethio_write(ETH_IOMem *io, uint32_t address, uint8_t value)
{
    (void)ethio_handle_io_rw(io, address, value, true);
}

/* Timer data register 0xEF0100-0xEF0101 */
static uint8_t ethio_timer_data(ETH_IOMem *io, uint32_t original_address, uint8_t value, bool is_write)
{
    bool is_high_byte = (original_address & 1u) == 0u;

    if (is_write)
    {
        if (is_high_byte)
        {
            io->pending_timer_data_word =
                (uint16_t)((io->pending_timer_data_word & 0x00FFu) | ((unsigned)value << 8u));
            io->timer_data_high_byte_written = true;
            io->timer_data[io->timer_data_index] =
                (uint16_t)((io->timer_data[io->timer_data_index] & 0x00FFu) | ((unsigned)value << 8u));
        }
        else
        {
            io->pending_timer_data_word = (uint16_t)((io->pending_timer_data_word & 0xFF00u) | value);
            io->timer_data[io->timer_data_index] =
                (uint16_t)((io->timer_data[io->timer_data_index] & 0xFF00u) | value);
            io->timer_data_index = (io->timer_data_index + 1) % 15;
            if ((io->last_timer_command & 0xFF00u) == 0xFF00u)
            {
                uint8_t cmd = (uint8_t)(io->last_timer_command & 0xFFu);
                if (cmd == 0x01u)
                {
                    io->timer_load_reg1 = io->pending_timer_data_word;
                    io->timer_counter1 = io->pending_timer_data_word;
                }
            }
        }
        return 0u;
    }
    else
    {
        uint8_t result;
        if (is_high_byte)
        {
            result = (uint8_t)(io->timer_data[io->timer_data_index] >> 8u);
        }
        else
        {
            result = (uint8_t)(io->timer_data[io->timer_data_index] & 0xFFu);
            io->timer_data_index = (io->timer_data_index + 1) % 15;
        }
        return result;
    }
}

/* Timer control register 0xEF0102-0xEF0103. Returns true when handled (the
 * value is in *ret); false = fall through to the mirrored decode. */
static bool ethio_timer_control(ETH_IOMem *io, uint32_t original_address, uint8_t value, bool is_write,
                                uint8_t *ret)
{
    bool is_high_byte = (original_address & 1u) == 0u;

    if (!is_write)
    {
        *ret = 0xFFu;
        return true;
    }
    if (is_high_byte)
    {
        io->last_timer_command = (uint16_t)((io->last_timer_command & 0x00FFu) | ((unsigned)value << 8u));
    }
    else
    {
        uint8_t cmd_low;
        bool valid_timer_command = false;

        io->last_timer_command = (uint16_t)((io->last_timer_command & 0xFF00u) | value);
        cmd_low = (uint8_t)(io->last_timer_command & 0xFFu);

        if (io->last_timer_command == 0xFFFFu)
        {
            io->reset_sequence_state = 1;
        }
        else if ((io->reset_sequence_state == 1) && (io->last_timer_command == 0xFFEFu))
        {
            io->reset_sequence_state = 2;
        }
        else if ((io->reset_sequence_state == 2) && (io->last_timer_command == 0xFFE0u))
        {
            io->reset_sequence_state = 3;
        }
        else if ((io->reset_sequence_state == 3) && (io->last_timer_command == 0xFF01u))
        {
            ethio_reset_timer_controller(io);
            *ret = 0u;
            return true;
        }
        else if (io->last_timer_command != 0xFFFFu)
        {
            io->reset_sequence_state = 0;
        }

        if (cmd_low == 0x01u)
        {
            valid_timer_command = true;
        }
        else if (cmd_low == 0x09u)
        {
            io->timer_armed1 = true;
            valid_timer_command = true;
        }
        else if (cmd_low == 0x17u)
        {
            valid_timer_command = true;
        }
        else if (cmd_low == 0xC1u)
        {
            io->timer_armed1 = false;
            valid_timer_command = true;
        }
        else if (cmd_low == 0xE1u)
        {
            io->timer_status = (uint8_t)(io->timer_status & 0xFEu);
            valid_timer_command = true;
        }
        if (valid_timer_command)
        {
            *ret = 0u;
            return true;
        }
    }
    if (is_high_byte)
    {
        *ret = 0u;
        return true;
    }
    return false;
}

uint8_t ethio_handle_io_rw(ETH_IOMem *io, uint32_t address, uint8_t value, bool is_write)
{
    uint32_t original_address = address;

    /* ---- Timer controller (unmirrored 0xEF01xx addresses) ---- */
    if ((original_address >= 0xEF0120u) && (original_address <= 0xEF0121u))
    {
        if (!is_write)
        {
            uint8_t status = io->timer_status;
            io->timer_status = (uint8_t)(io->timer_status & 0xFEu);
            return status;
        }
        return 0u;
    }
    if ((original_address >= 0xEF0140u) && (original_address <= 0xEF0141u))
    {
        if (!is_write)
        {
            return ((original_address & 1u) == 0u) ? (uint8_t)(io->timer_counter1 >> 8u)
                                                    : (uint8_t)(io->timer_counter1 & 0xFFu);
        }
        return 0u;
    }
    if ((original_address >= 0xEF0160u) && (original_address <= 0xEF0163u))
    {
        if (!is_write)
        {
            unsigned shift = (3u - (original_address & 3u)) * 8u;
            return (uint8_t)(io->timer_tick32 >> shift);
        }
        return 0u;
    }
    if ((original_address >= 0xEF01A0u) && (original_address <= 0xEF01A1u))
    {
        bool hi = (original_address & 1u) == 0u;
        if (is_write)
        {
            if (hi)
            {
                io->timer_config_reg = (uint16_t)((io->timer_config_reg & 0x00FFu) | ((unsigned)value << 8u));
            }
            else
            {
                io->timer_config_reg = (uint16_t)((io->timer_config_reg & 0xFF00u) | value);
                io->stc_armed = true;
                io->stc_countdown = ETHIO_STC_PERIOD_TICKS;
            }
            return 0u;
        }
        return hi ? (uint8_t)(io->timer_config_reg >> 8u) : (uint8_t)(io->timer_config_reg & 0xFFu);
    }
    if ((original_address >= 0xEF0100u) && (original_address <= 0xEF0101u))
    {
        return ethio_timer_data(io, original_address, value, is_write);
    }
    if ((original_address >= 0xEF0102u) && (original_address <= 0xEF0103u))
    {
        uint8_t ret = 0u;
        if (ethio_timer_control(io, original_address, value, is_write, &ret))
        {
            return ret;
        }
    }

    /* ---- mirrored decode: EF01xx = EF00xx ---- */
    if ((address >= 0xEF0100u) && (address <= 0xEF01FFu))
    {
        address &= 0xFFFEFFu;
    }

    if ((address >= 0xEF00B8u) && (address <= 0xEF00BFu))
    {
        uint8_t status = 0xFFu;
        if (io->transceiver_power_enabled)
        {
            status = (uint8_t)(status & (uint8_t)~(1u << 2u));
        }
        if ((io->ev.get_lance_interrupt_state != NULL) && io->ev.get_lance_interrupt_state(io->ev.ctx))
        {
            status = (uint8_t)(status & (uint8_t)~(1u << 0u));
        }
        return status;
    }
    if ((address >= 0xEF00B0u) && (address <= 0xEF00B7u) && is_write)
    {
        if (io->ev.on_lan_reset != NULL)
        {
            io->ev.on_lan_reset(io->ev.ctx);
        }
        return 0x00u;
    }
    if ((address >= 0xEF00A8u) && (address <= 0xEF00AFu) && is_write)
    {
        io->transceiver_power_enabled = (value & 0x01u) != 0u;
        return 0x00u;
    }
    if ((address >= 0xEF0080u) && (address <= 0xEF009Fu))
    {
        if (address <= 0xEF0081u)
        {
            if (is_write)
            {
                if (io->ev.on_nd_interrupt != NULL)
                {
                    io->ev.on_nd_interrupt(io->ev.ctx);
                }
                if (io->mfp_chip != NULL)
                {
                    mfp_write(io->mfp_chip, address + 0x40u, value);
                }
                if (io->ev.on_scip_doorbell != NULL)
                {
                    io->ev.on_scip_doorbell(io->ev.ctx, value);
                }
                return 0x00u;
            }
            return (io->mfp_chip != NULL) ? mfp_read(io->mfp_chip, address + 0x40u) : 0xFFu;
        }
        return ethio_handle_scip_channel_access(io, address, value, is_write);
    }
    if ((address >= 0xEF0060u) && (address <= 0xEF007Fu) && !is_write)
    {
        uint16_t earen = (io->ev.get_earen_value != NULL) ? io->ev.get_earen_value(io->ev.ctx) : 0u;
        return ((address & 0x01u) == 0u) ? (uint8_t)(earen >> 8u) : (uint8_t)(earen & 0xFFu);
    }
    if ((address >= 0xEF0040u) && (address <= 0xEF005Fu) && !is_write)
    {
        uint16_t merrstat = (io->ev.get_merrstat_value != NULL) ? io->ev.get_merrstat_value(io->ev.ctx) : 0u;
        return ((address & 0x01u) == 0u) ? (uint8_t)(merrstat >> 8u) : (uint8_t)(merrstat & 0xFFu);
    }
    if ((address >= 0xEF0010u) && (address <= 0xEF001Fu))
    {
        return 0x00u;
    }
    if ((address >= 0xEF0020u) && (address <= 0xEF003Fu))
    {
        return 0x00u;
    }
    if ((address >= 0xEF0000u) && (address <= 0xEF007Fu))
    {
        if (is_write && (address <= 0xEF000Fu) && (value == 0xFFu))
        {
            if (io->ev.on_am9519_reset != NULL)
            {
                io->ev.on_am9519_reset(io->ev.ctx);
            }
        }
        else if (is_write && (address <= 0xEF000Fu) && (value >= 0x60u) && (value <= 0x67u))
        {
            if (io->ev.on_nd_interrupt != NULL)
            {
                io->ev.on_nd_interrupt(io->ev.ctx);
            }
        }
        return ethio_handle_scip_channel_access(io, address, value, is_write);
    }
    return 0x00u;
}
