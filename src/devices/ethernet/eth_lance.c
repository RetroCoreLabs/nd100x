/*
 * eth_lance.c - AMD Am7990 LANCE (port of RetroCore Am2990Lance.cs).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Copied behaviour worth knowing (Am2990Lance.cs at RetroCore 935163f):
 *  - register offsets above 0x100 are mirrored down by 0x100; offset 0x100
 *    itself is not ("regAddress > 0x100");
 *  - byte writes are latched: the even byte is stored, the odd byte executes;
 *    SwapByteLanes decides which of the two is the high byte;
 *  - TX hands the backend the frame WITHOUT FCS; RX adds 4 to MCNT ("the real
 *    Am7990 counts the FCS"); RX does not check the CRC;
 *  - ProcessNetworkTest and Simulate* are diagnostic helpers kept as in C#.
 *
 * Intended differences (the C# throws an exception here; C would overflow):
 *  - TX: data beyond the 4096-byte packet buffer (chained buffers) is not
 *    copied (C#: IndexOutOfRangeException in DmaReadBuffer);
 *  - loopback: a frame longer than the 1522-byte loopback buffer is cut to
 *    1522 bytes (C#: ArgumentException from Array.Copy);
 *  - RX queue: a frame longer than 4096 bytes is cut to 4096 (C# stores any
 *    length).
 */

#include "eth_lance.h"

#include <string.h>

static void lance_check_interrupts(Am7990Lance *l);
static void lance_transmit(Am7990Lance *l);
static void lance_transmit_complete(Am7990Lance *l, int result);

/* ---- Crc32 ----------------------------------------------------------------- */

static uint32_t s_crc_table[256];
static bool s_crc_table_built;

/* Crc32.BuildTable */
static void lance_crc32_build_table(void)
{
    for (uint32_t i = 0; i < 256u; i++)
    {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++)
        {
            crc = (crc >> 1u) ^ (((crc & 1u) != 0u) ? 0xEDB88320u : 0u);
        }
        s_crc_table[i] = crc;
    }
    s_crc_table_built = true;
}

uint32_t lance_crc32_calculate(const uint8_t *data, int offset, int length)
{
    uint32_t crc = 0xFFFFFFFFu;

    if (!s_crc_table_built)
    {
        lance_crc32_build_table();
    }
    for (int i = 0; i < length; i++)
    {
        crc = s_crc_table[(crc ^ data[offset + i]) & 0xFFu] ^ (crc >> 8u);
    }
    return ~crc;
}

/* ---- helpers --------------------------------------------------------------- */

static bool has_dma_in(const Am7990Lance *l)
{
    return l->ev.dma_in != NULL;
}

static bool has_dma_out(const Am7990Lance *l)
{
    return l->ev.dma_out != NULL;
}

static uint16_t dma_in(Am7990Lance *l, uint32_t address)
{
    return l->ev.dma_in(l->ev.ctx, address);
}

static void dma_out(Am7990Lance *l, uint32_t address, uint16_t data)
{
    l->ev.dma_out(l->ev.ctx, address, data);
}

/* GetBufferLength */
static int lance_get_buffer_length(uint16_t data)
{
    if (data == 0xF000u)
    {
        return 4096;
    }
    return -((int)(int16_t)(uint16_t)(0xF000u | data));
}

static uint32_t ring_addr(uint32_t base, uint8_t pos)
{
    return (base + ((uint32_t)pos << 3u)) & 0xFFFFF8u;
}

/* ---- create / reset -------------------------------------------------------- */

void lance_create(Am7990Lance *l, uint32_t start_address, uint32_t length, const LanceEvents *events)
{
    if (l == NULL)
    {
        return;
    }
    memset(l, 0, sizeof *l);
    if (events != NULL)
    {
        l->ev = *events;
    }
    l->start_address = start_address;
    l->end_address = start_address + length - 1u;
    l->swap_byte_lanes = false;
    lance_reset(l);
}

bool lance_is_mapped_address(const Am7990Lance *l, uint32_t address)
{
    return (address >= l->start_address) && (address <= l->end_address);
}

void lance_reset(Am7990Lance *l)
{
    bool was_asserted;

    l->rap = 0u;
    memset(l->csr, 0, sizeof l->csr);
    l->csr[0] = CSR0_FLAGS_STOP;
    l->rdp_latch_lo = 0u;
    l->rap_latch_lo = 0u;
    l->rdp_latch_valid = false;
    l->rap_latch_valid = false;
    l->initialized = false;
    l->receiver_enabled = false;
    l->transmitter_enabled = false;
    l->tx_poll_pending = false;

    was_asserted = l->irq_asserted;
    l->irq_asserted = false;
    if (was_asserted && (l->ev.on_lance_irq != NULL))
    {
        l->ev.on_lance_irq(l->ev.ctx);
    }

    l->mode = 0u;
    l->logical_address_filter = 0u;
    memset(l->physical_address, 0, sizeof l->physical_address);
    l->rx_ring_base = 0u;
    l->rx_ring_mask = 0u;
    l->rx_ring_pos = 0u;
    memset(l->rx_md, 0, sizeof l->rx_md);
    l->tx_ring_base = 0u;
    l->tx_ring_mask = 0u;
    l->tx_ring_pos = 0u;
    memset(l->tx_md, 0, sizeof l->tx_md);
    l->loopback_length = 0;
}

/* ---- register access ------------------------------------------------------- */

/* ReadCSR */
static uint16_t lance_read_csr(const Am7990Lance *l, uint16_t csr_number)
{
    if (csr_number >= 4u)
    {
        return 0u;
    }
    return l->csr[csr_number];
}

uint8_t lance_read(Am7990Lance *l, uint32_t address)
{
    int reg_address = (int)(address - l->start_address);
    int reg;
    uint16_t value = 0u;
    bool even_is_low;

    if (reg_address > 0x100)
    {
        reg_address -= 0x100;
    }
    reg = reg_address & ~1;
    switch (reg)
    {
    case LANCE_REGISTERS_REGISTER_DATA_PORT:
        value = lance_read_csr(l, l->rap);
        break;
    case LANCE_REGISTERS_REGISTER_ADDRESS_PORT:
        value = l->rap;
        break;
    default:
        break;
    }
    even_is_low = ((address & 1u) == 0u) != l->swap_byte_lanes;
    if (even_is_low)
    {
        return (uint8_t)(value & 0xFFu);
    }
    return (uint8_t)((value >> 8u) & 0xFFu);
}

/* ExecuteInitialize */
static void lance_execute_initialize(Am7990Lance *l);
/* ExecuteStart */
static void lance_execute_start(Am7990Lance *l);

/* SetMemoryError */
static void lance_set_memory_error(Am7990Lance *l)
{
    l->csr[0] = (uint16_t)(l->csr[0] | CSR0_FLAGS_MERR);
    lance_check_interrupts(l);
}

/* WriteCSR0 */
static void lance_write_csr0(Am7990Lance *l, uint16_t value)
{
    uint16_t clear_mask;

    if ((value & CSR0_FLAGS_STOP) != 0u)
    {
        if ((l->csr[0] & CSR0_FLAGS_STOP) == 0u)
        {
            lance_reset(l);
        }
        return;
    }

    clear_mask = (uint16_t)(CSR0_FLAGS_BABL | CSR0_FLAGS_CERR | CSR0_FLAGS_MISS | CSR0_FLAGS_MERR |
                            CSR0_FLAGS_RINT | CSR0_FLAGS_TINT | CSR0_FLAGS_IDON);
    l->csr[0] = (uint16_t)(l->csr[0] & (uint16_t)~(value & clear_mask));

    if (((value & CSR0_FLAGS_INIT) != 0u) && ((l->csr[0] & CSR0_FLAGS_INIT) == 0u))
    {
        if ((l->csr[0] & CSR0_FLAGS_STOP) != 0u)
        {
            lance_execute_initialize(l);
        }
        else
        {
            l->csr[0] = (uint16_t)(l->csr[0] | (l->initialized ? CSR0_FLAGS_IDON : CSR0_FLAGS_INIT));
        }
    }

    if (((value & CSR0_FLAGS_STRT) != 0u) && ((l->csr[0] & CSR0_FLAGS_STRT) == 0u))
    {
        lance_execute_start(l);
    }

    if (((value & CSR0_FLAGS_TDMD) != 0u) && ((l->csr[0] & CSR0_FLAGS_TDMD) == 0u))
    {
        l->csr[0] = (uint16_t)(l->csr[0] | CSR0_FLAGS_TDMD);
        l->tx_poll_pending = true;
        lance_transmit_poll(l);
    }

    if ((value & CSR0_FLAGS_INEA) != 0u)
    {
        l->csr[0] = (uint16_t)(l->csr[0] | CSR0_FLAGS_INEA);
    }
    else
    {
        l->csr[0] = (uint16_t)(l->csr[0] & (uint16_t)~CSR0_FLAGS_INEA);
    }

    if ((l->csr[0] & (CSR0_FLAGS_BABL | CSR0_FLAGS_CERR | CSR0_FLAGS_MISS | CSR0_FLAGS_MERR)) != 0u)
    {
        l->csr[0] = (uint16_t)(l->csr[0] | CSR0_FLAGS_ERR);
    }
    else
    {
        l->csr[0] = (uint16_t)(l->csr[0] & (uint16_t)~CSR0_FLAGS_ERR);
    }

    if ((l->csr[0] & (CSR0_FLAGS_BABL | CSR0_FLAGS_MISS | CSR0_FLAGS_MERR | CSR0_FLAGS_RINT |
                      CSR0_FLAGS_TINT | CSR0_FLAGS_IDON)) != 0u)
    {
        l->csr[0] = (uint16_t)(l->csr[0] | CSR0_FLAGS_INTR);
    }
    else
    {
        l->csr[0] = (uint16_t)(l->csr[0] & (uint16_t)~CSR0_FLAGS_INTR);
    }

    lance_check_interrupts(l);
}

/* WriteCSR */
static void lance_write_csr(Am7990Lance *l, uint16_t csr_number, uint16_t value)
{
    if (csr_number >= 4u)
    {
        return;
    }
    switch (csr_number)
    {
    case 0u:
        lance_write_csr0(l, value);
        break;
    case 1u:
        if ((l->csr[0] & CSR0_FLAGS_STOP) != 0u)
        {
            l->csr[1] = value;
            l->init_block_address = (l->init_block_address & 0xFFFF0000u) | value;
        }
        break;
    case 2u:
        if ((l->csr[0] & CSR0_FLAGS_STOP) != 0u)
        {
            l->csr[2] = value;
            l->init_block_address = (l->init_block_address & 0x0000FFFFu) | ((uint32_t)value << 16u);
        }
        break;
    case 3u:
        if ((l->csr[0] & CSR0_FLAGS_STOP) != 0u)
        {
            l->csr[3] = (uint16_t)(value & 0x0007u);
        }
        lance_check_interrupts(l);
        break;
    default:
        break;
    }
}

void lance_write(Am7990Lance *l, uint32_t address, uint8_t value)
{
    int reg_address = (int)(address - l->start_address);
    int reg;
    uint16_t write_value;

    if (reg_address > 0x100)
    {
        reg_address -= 0x100;
    }
    reg = reg_address & ~1;

    if ((address & 1u) == 0u)
    {
        switch (reg)
        {
        case LANCE_REGISTERS_REGISTER_DATA_PORT:
            l->rdp_latch_lo = value;
            l->rdp_latch_valid = true;
            break;
        case LANCE_REGISTERS_REGISTER_ADDRESS_PORT:
            l->rap_latch_lo = value;
            l->rap_latch_valid = true;
            break;
        default:
            break;
        }
        return;
    }

    switch (reg)
    {
    case LANCE_REGISTERS_REGISTER_DATA_PORT:
        write_value = l->swap_byte_lanes
                          ? (uint16_t)((l->rdp_latch_valid ? ((unsigned)l->rdp_latch_lo << 8u) : 0u) | value)
                          : (uint16_t)(l->rdp_latch_valid ? (l->rdp_latch_lo | ((unsigned)value << 8u))
                                                          : ((unsigned)value << 8u));
        l->rdp_latch_valid = false;
        lance_write_csr(l, l->rap, write_value);
        break;
    case LANCE_REGISTERS_REGISTER_ADDRESS_PORT:
        write_value = l->swap_byte_lanes
                          ? (uint16_t)((l->rap_latch_valid ? ((unsigned)l->rap_latch_lo << 8u) : 0u) | value)
                          : (uint16_t)(l->rap_latch_valid ? (l->rap_latch_lo | ((unsigned)value << 8u))
                                                          : ((unsigned)value << 8u));
        l->rap_latch_valid = false;
        l->rap = (uint16_t)(write_value & 0x3u);
        break;
    default:
        break;
    }
}

uint8_t lance_read_debug(const Am7990Lance *l, uint32_t address)
{
    uint32_t local_address = address - l->start_address;
    uint16_t value = 0u;

    if (local_address >= 4u)
    {
        return 0xFFu;
    }
    if (local_address > 0x100u)
    {
        local_address -= 0x100u;
    }
    switch (local_address)
    {
    case LANCE_REGISTERS_REGISTER_DATA_PORT:
        if (l->rap < 4u)
        {
            value = l->csr[l->rap];
        }
        break;
    case LANCE_REGISTERS_REGISTER_ADDRESS_PORT:
        value = l->rap;
        break;
    default:
        break;
    }
    if ((address & 1u) == 0u)
    {
        return (uint8_t)(value & 0xFFu);
    }
    return (uint8_t)((value >> 8u) & 0xFFu);
}

/* ---- init / start / stop --------------------------------------------------- */

static void lance_execute_initialize(Am7990Lance *l)
{
    uint32_t init_addr;
    uint16_t init_block[12];

    if (!has_dma_in(l))
    {
        lance_set_memory_error(l);
        return;
    }
    init_addr = (((uint32_t)l->csr[2] << 16u) | l->csr[1]) & 0xFFFFFEu;
    if (init_addr == 0u)
    {
        return;
    }
    for (int i = 0; i < 12; i++)
    {
        init_block[i] = dma_in(l, init_addr + (uint32_t)(i * 2));
    }
    l->mode = init_block[0];
    l->physical_address[0] = (uint8_t)(init_block[1] & 0xFFu);
    l->physical_address[1] = (uint8_t)(init_block[1] >> 8u);
    l->physical_address[2] = (uint8_t)(init_block[2] & 0xFFu);
    l->physical_address[3] = (uint8_t)(init_block[2] >> 8u);
    l->physical_address[4] = (uint8_t)(init_block[3] & 0xFFu);
    l->physical_address[5] = (uint8_t)(init_block[3] >> 8u);
    l->logical_address_filter = ((uint64_t)init_block[7] << 48u) | ((uint64_t)init_block[6] << 32u) |
                                ((uint64_t)init_block[5] << 16u) | init_block[4];
    l->rx_ring_base = (((uint32_t)(init_block[9] & 0xFFu) << 16u) | init_block[8]) & 0xFFFFF8u;
    l->rx_ring_mask = (uint8_t)((1u << ((init_block[9] >> 13u) & 7u)) - 1u);
    l->tx_ring_base = (((uint32_t)(init_block[11] & 0xFFu) << 16u) | init_block[10]) & 0xFFFFF8u;
    l->tx_ring_mask = (uint8_t)((1u << ((init_block[11] >> 13u) & 7u)) - 1u);
    l->rx_ring_pos = 0u;
    l->tx_ring_pos = 0u;
    l->initialized = true;
    l->csr[0] = (uint16_t)(l->csr[0] | CSR0_FLAGS_IDON | CSR0_FLAGS_INIT);
    l->csr[0] = (uint16_t)(l->csr[0] & (uint16_t)~CSR0_FLAGS_STOP);
    lance_check_interrupts(l);
}

static void lance_execute_start(Am7990Lance *l)
{
    if (!l->initialized)
    {
        return;
    }
    l->csr[0] = (uint16_t)(l->csr[0] | CSR0_FLAGS_STRT);
    l->csr[0] = (uint16_t)(l->csr[0] & (uint16_t)~CSR0_FLAGS_STOP);
    if ((l->mode & MODE_FLAGS_DRX) != 0u)
    {
        l->csr[0] = (uint16_t)(l->csr[0] & (uint16_t)~CSR0_FLAGS_RXON);
        l->receiver_enabled = false;
    }
    else
    {
        l->csr[0] = (uint16_t)(l->csr[0] | CSR0_FLAGS_RXON);
        l->receiver_enabled = true;
    }
    if ((l->mode & MODE_FLAGS_DTX) != 0u)
    {
        l->csr[0] = (uint16_t)(l->csr[0] & (uint16_t)~CSR0_FLAGS_TXON);
        l->transmitter_enabled = false;
    }
    else
    {
        l->csr[0] = (uint16_t)(l->csr[0] | CSR0_FLAGS_TXON);
        l->transmitter_enabled = true;
    }
    l->tx_poll_pending = true;
}

void lance_execute_stop(Am7990Lance *l)
{
    l->csr[0] = (uint16_t)(l->csr[0] | CSR0_FLAGS_STOP);
    l->csr[0] = (uint16_t)(l->csr[0] & (uint16_t)~(CSR0_FLAGS_RXON | CSR0_FLAGS_TXON));
    l->receiver_enabled = false;
    l->transmitter_enabled = false;
}

/* ExecuteTransmitDemand */
static void lance_execute_transmit_demand(Am7990Lance *l)
{
    l->csr[0] = (uint16_t)(l->csr[0] | CSR0_FLAGS_TDMD);
    lance_transmit_poll(l);
}

/* ---- DMA buffers ------------------------------------------------------------ */

/* DmaReadBuffer; stops at the end of the destination buffer (C# would throw) */
static void lance_dma_read_buffer(Am7990Lance *l, uint32_t address, uint8_t *buffer, int buffer_size,
                                  int offset, int length)
{
    bool byte_swap;
    int pos = offset;
    uint32_t addr = address;

    if (!has_dma_in(l))
    {
        return;
    }
    byte_swap = (l->csr[3] & CSR3_FLAGS_BSWP) != 0u;
    if ((addr & 1u) != 0u)
    {
        uint16_t word = dma_in(l, addr & ~1u);
        if (pos < buffer_size)
        {
            buffer[pos] = byte_swap ? (uint8_t)(word & 0xFFu) : (uint8_t)(word >> 8u);
        }
        pos++;
        addr++;
        length--;
    }
    while (length > 1)
    {
        uint16_t word = dma_in(l, addr);
        if (pos + 1 < buffer_size)
        {
            if (byte_swap)
            {
                buffer[pos] = (uint8_t)(word >> 8u);
                buffer[pos + 1] = (uint8_t)(word & 0xFFu);
            }
            else
            {
                buffer[pos] = (uint8_t)(word & 0xFFu);
                buffer[pos + 1] = (uint8_t)(word >> 8u);
            }
        }
        pos += 2;
        addr += 2u;
        length -= 2;
    }
    if (length > 0)
    {
        uint16_t word = dma_in(l, addr);
        if (pos < buffer_size)
        {
            buffer[pos] = byte_swap ? (uint8_t)(word >> 8u) : (uint8_t)(word & 0xFFu);
        }
    }
}

/* DmaWriteBuffer */
static void lance_dma_write_buffer(Am7990Lance *l, uint32_t address, const uint8_t *buffer, int offset,
                                   int length)
{
    bool byte_swap;
    int pos = offset;
    uint32_t addr = address;

    if (!has_dma_out(l))
    {
        return;
    }
    byte_swap = (l->csr[3] & CSR3_FLAGS_BSWP) != 0u;
    if ((addr & 1u) != 0u)
    {
        uint16_t word = byte_swap ? buffer[pos] : (uint16_t)((unsigned)buffer[pos] << 8u);
        dma_out(l, addr & ~1u, word);
        pos++;
        addr++;
        length--;
    }
    while (length > 1)
    {
        uint16_t word;
        if (byte_swap)
        {
            word = (uint16_t)(((unsigned)buffer[pos] << 8u) | buffer[pos + 1]);
        }
        else
        {
            word = (uint16_t)(buffer[pos] | ((unsigned)buffer[pos + 1] << 8u));
        }
        dma_out(l, addr, word);
        pos += 2;
        addr += 2u;
        length -= 2;
    }
    if (length > 0)
    {
        uint16_t word = byte_swap ? (uint16_t)((unsigned)buffer[pos] << 8u) : buffer[pos];
        dma_out(l, addr, word);
    }
}

/* ---- transmit --------------------------------------------------------------- */

void lance_transmit_poll(Am7990Lance *l)
{
    l->csr[0] = (uint16_t)(l->csr[0] & (uint16_t)~CSR0_FLAGS_TDMD);

    if ((l->csr[0] & CSR0_FLAGS_TXON) != 0u)
    {
        uint32_t ra;

        if (!has_dma_in(l) || !has_dma_out(l))
        {
            return;
        }
        ra = ring_addr(l->tx_ring_base, l->tx_ring_pos);
        l->tx_md[1] = dma_in(l, ra | 2u);
        if ((l->tx_md[1] & TMD1_FLAGS_OWN) != 0u)
        {
            if ((l->tx_md[1] & TMD1_FLAGS_STP) == 0u)
            {
                dma_out(l, ra | 2u, (uint16_t)(l->tx_md[1] & (uint16_t)~TMD1_FLAGS_OWN));
                l->tx_ring_pos = (uint8_t)((l->tx_ring_pos + 1u) & l->tx_ring_mask);
            }
            else
            {
                lance_transmit(l);
            }
        }
    }

    if ((l->loopback_length > 0) && ((l->mode & MODE_FLAGS_LOOP) != 0u))
    {
        int result;
        l->in_loopback_receive = true;
        result = lance_receive_packet(l, l->loopback_buffer, l->loopback_length);
        l->in_loopback_receive = false;
        l->loopback_length = 0;
        lance_receive_complete(l, result);
    }
}

static void lance_transmit(Am7990Lance *l)
{
    bool append_fcs;
    uint32_t ra;
    int length = 0;
    int wire_length;

    if (!has_dma_in(l) || !has_dma_out(l))
    {
        return;
    }
    append_fcs = (l->mode & MODE_FLAGS_DTCR) == 0u;
    ra = ring_addr(l->tx_ring_base, l->tx_ring_pos);

    for (;;)
    {
        uint32_t buf_addr;
        int buf_len;

        l->tx_md[0] = dma_in(l, ra | 0u);
        l->tx_md[2] = dma_in(l, ra | 4u);
        l->tx_md[3] = 0u;
        buf_addr = ((uint32_t)(l->tx_md[1] & TMD1_FLAGS_HADR) << 16u) | l->tx_md[0];
        buf_len = lance_get_buffer_length(l->tx_md[2]);

        l->tx_md[1] = (uint16_t)(l->tx_md[1] & (uint16_t)~TMD1_FLAGS_OWN);
        if (buf_len == 0)
        {
            dma_out(l, ra | 2u, l->tx_md[1]);
            l->tx_ring_pos = (uint8_t)((l->tx_ring_pos + 1u) & l->tx_ring_mask);
            return;
        }

        lance_dma_read_buffer(l, buf_addr, l->packet_buffer, LANCE_PACKET_BUFFER_SIZE, length, buf_len);
        length += buf_len;

        if ((l->tx_md[1] & TMD1_FLAGS_ENP) == 0u)
        {
            uint8_t next_ring_pos = (uint8_t)((l->tx_ring_pos + 1u) & l->tx_ring_mask);
            uint32_t next_ring_addr = ring_addr(l->tx_ring_base, next_ring_pos);
            uint16_t next_tmd1 = dma_in(l, next_ring_addr | 2u);

            if ((next_tmd1 & TMD1_FLAGS_OWN) != 0u)
            {
                dma_out(l, ra | 2u, l->tx_md[1]);
                l->tx_ring_pos = next_ring_pos;
                ra = next_ring_addr;
                l->tx_md[1] = next_tmd1;
            }
            else
            {
                l->tx_md[1] = (uint16_t)(l->tx_md[1] | TMD1_FLAGS_ERR);
                l->tx_md[3] = (uint16_t)(l->tx_md[3] | TMD3_FLAGS_BUFF | TMD3_FLAGS_UFLO);
                dma_out(l, ra | 6u, l->tx_md[3]);
                l->csr[0] = (uint16_t)(l->csr[0] & (uint16_t)~CSR0_FLAGS_TXON);
                l->transmitter_enabled = false;
                break;
            }
        }
        else
        {
            break;
        }
    }

    if (length > 1518)
    {
        l->csr[0] = (uint16_t)(l->csr[0] | CSR0_FLAGS_ERR | CSR0_FLAGS_BABL);
    }
    if (length > LANCE_PACKET_BUFFER_SIZE)
    {
        length = LANCE_PACKET_BUFFER_SIZE; /* C# would have thrown in DmaReadBuffer */
    }
    wire_length = length;

    if (append_fcs && (length > 0) && (length + 4 <= LANCE_PACKET_BUFFER_SIZE))
    {
        uint32_t crc = lance_crc32_calculate(l->packet_buffer, 0, length);
        l->packet_buffer[length++] = (uint8_t)crc;
        l->packet_buffer[length++] = (uint8_t)(crc >> 8u);
        l->packet_buffer[length++] = (uint8_t)(crc >> 16u);
        l->packet_buffer[length++] = (uint8_t)(crc >> 24u);
    }

    if ((l->mode & MODE_FLAGS_LOOP) != 0u)
    {
        int copy;

        if (((l->mode & MODE_FLAGS_COLL) != 0u) && ((l->mode & MODE_FLAGS_INTL) != 0u))
        {
            lance_transmit_complete(l, -1);
            return;
        }
        copy = (length > LANCE_LOOPBACK_BUFFER_SIZE) ? LANCE_LOOPBACK_BUFFER_SIZE : length;
        memcpy(l->loopback_buffer, l->packet_buffer, (size_t)copy);
        l->loopback_length = copy;
        if ((l->mode & MODE_FLAGS_INTL) != 0u)
        {
            lance_transmit_complete(l, length);
            return;
        }
    }
    else
    {
        if (l->ev.on_packet_transmit != NULL)
        {
            l->ev.on_packet_transmit(l->ev.ctx, l->packet_buffer, wire_length);
        }
    }
    lance_transmit_complete(l, length);
}

static void lance_transmit_complete(Am7990Lance *l, int result)
{
    uint32_t ra;

    if (!has_dma_out(l))
    {
        return;
    }
    ra = ring_addr(l->tx_ring_base, l->tx_ring_pos);
    switch (result)
    {
    case -2:
        l->tx_md[1] = (uint16_t)(l->tx_md[1] | TMD1_FLAGS_ERR);
        break;
    case -1:
        l->tx_md[1] = (uint16_t)(l->tx_md[1] | TMD1_FLAGS_ERR);
        l->tx_md[3] = (uint16_t)(l->tx_md[3] | TMD3_FLAGS_RTRY);
        dma_out(l, ra | 6u, l->tx_md[3]);
        break;
    case 0:
        l->tx_md[1] = (uint16_t)(l->tx_md[1] | TMD1_FLAGS_ERR);
        l->tx_md[3] = (uint16_t)(l->tx_md[3] | TMD3_FLAGS_LCAR);
        dma_out(l, ra | 6u, l->tx_md[3]);
        break;
    default:
        break;
    }
    dma_out(l, ra | 2u, l->tx_md[1]);
    l->tx_ring_pos = (uint8_t)((l->tx_ring_pos + 1u) & l->tx_ring_mask);
    l->csr[0] = (uint16_t)(l->csr[0] | CSR0_FLAGS_TINT | CSR0_FLAGS_INTR);
    lance_check_interrupts(l);
}

/* ---- receive ---------------------------------------------------------------- */

/* AddressFilter */
static bool lance_address_filter(const Am7990Lance *l, const uint8_t *buffer)
{
    if ((l->mode & MODE_FLAGS_PROM) != 0u)
    {
        return true;
    }
    if ((buffer[0] & 1u) != 0u)
    {
        uint32_t crc;
        int filter_bit;

        if ((buffer[0] == 0xFFu) && (buffer[1] == 0xFFu) && (buffer[2] == 0xFFu) && (buffer[3] == 0xFFu) &&
            (buffer[4] == 0xFFu) && (buffer[5] == 0xFFu))
        {
            return true;
        }
        crc = lance_crc32_calculate(buffer, 0, 6);
        filter_bit = (int)(crc >> 26u);
        if (((l->logical_address_filter >> (unsigned)filter_bit) & 1u) != 0u)
        {
            return true;
        }
    }
    else
    {
        if ((buffer[0] == l->physical_address[0]) && (buffer[1] == l->physical_address[1]) &&
            (buffer[2] == l->physical_address[2]) && (buffer[3] == l->physical_address[3]) &&
            (buffer[4] == l->physical_address[4]) && (buffer[5] == l->physical_address[5]))
        {
            return true;
        }
    }
    return false;
}

int lance_receive_packet(Am7990Lance *l, const uint8_t *buffer, int length)
{
    uint32_t ra;
    int offset = 0;

    if (!l->in_loopback_receive && ((l->mode & MODE_FLAGS_LOOP) != 0u) && ((l->mode & MODE_FLAGS_INTL) != 0u))
    {
        return 0;
    }
    if ((l->csr[0] & CSR0_FLAGS_RXON) == 0u)
    {
        l->rx_dropped_rx_off++;
        return -1;
    }
    if (!has_dma_in(l) || !has_dma_out(l))
    {
        return -1;
    }
    if ((l->mode & MODE_FLAGS_LOOP) == 0u)
    {
        if (length < 14)
        {
            return 0;
        }
        if (!lance_address_filter(l, buffer))
        {
            l->rx_filtered++;
            return -1;
        }
    }

    ra = ring_addr(l->rx_ring_base, l->rx_ring_pos);
    l->rx_md[1] = dma_in(l, ra | 2u);
    if ((l->rx_md[1] & RMD1_FLAGS_OWN) == 0u)
    {
        l->csr[0] = (uint16_t)(l->csr[0] | CSR0_FLAGS_ERR | CSR0_FLAGS_MISS);
        l->rx_missed++;
        return -2;
    }

    l->rx_md[1] = (uint16_t)(l->rx_md[1] | RMD1_FLAGS_STP);
    while (offset < length)
    {
        uint32_t buf_addr;
        int buf_len;
        int count;

        l->rx_md[0] = dma_in(l, ra | 0u);
        l->rx_md[2] = dma_in(l, ra | 4u);
        buf_addr = ((uint32_t)(l->rx_md[1] & RMD1_FLAGS_HADR) << 16u) | l->rx_md[0];
        buf_len = lance_get_buffer_length(l->rx_md[2]);
        count = length - offset;
        if (count > buf_len)
        {
            count = buf_len;
        }
        lance_dma_write_buffer(l, buf_addr, buffer, offset, count);
        offset += count;
        l->rx_md[1] = (uint16_t)(l->rx_md[1] & (uint16_t)~RMD1_FLAGS_OWN);

        if (offset < length)
        {
            uint8_t next_ring_pos = (uint8_t)((l->rx_ring_pos + 1u) & l->rx_ring_mask);
            uint32_t next_ring_addr = ring_addr(l->rx_ring_base, next_ring_pos);
            uint16_t next_rmd1 = dma_in(l, next_ring_addr | 2u);

            if ((next_rmd1 & RMD1_FLAGS_OWN) != 0u)
            {
                dma_out(l, ra | 2u, l->rx_md[1]);
                l->rx_ring_pos = next_ring_pos;
                ra = next_ring_addr;
                l->rx_md[1] = next_rmd1;
            }
            else
            {
                l->rx_md[1] = (uint16_t)(l->rx_md[1] | RMD1_FLAGS_ERR | RMD1_FLAGS_BUFF);
                break;
            }
        }
    }

    if (offset == length)
    {
        l->rx_md[1] = (uint16_t)(l->rx_md[1] | RMD1_FLAGS_ENP);
    }
    l->rx_accepted++;
    return offset;
}

void lance_receive_complete(Am7990Lance *l, int result)
{
    uint32_t ra;

    if (!has_dma_out(l))
    {
        return;
    }
    if (result == -2)
    {
        l->csr[0] = (uint16_t)(l->csr[0] | CSR0_FLAGS_RINT | CSR0_FLAGS_INTR);
        lance_check_interrupts(l);
        return;
    }
    if (result <= 0)
    {
        return;
    }
    ra = ring_addr(l->rx_ring_base, l->rx_ring_pos);
    dma_out(l, ra | 2u, l->rx_md[1]);
    if ((l->rx_md[1] & RMD1_FLAGS_ERR) == 0u)
    {
        dma_out(l, ra | 6u, (uint16_t)((result + 4) & RMD3_FLAGS_MCNT));
    }
    else
    {
        dma_out(l, ra | 6u, 0u);
    }
    l->rx_ring_pos = (uint8_t)((l->rx_ring_pos + 1u) & l->rx_ring_mask);
    l->csr[0] = (uint16_t)(l->csr[0] | CSR0_FLAGS_RINT | CSR0_FLAGS_INTR);
    lance_check_interrupts(l);
}

/* ---- interrupts --------------------------------------------------------------- */

/* SetReceiveInterrupt */
static void lance_set_receive_interrupt(Am7990Lance *l)
{
    l->csr[0] = (uint16_t)(l->csr[0] | CSR0_FLAGS_RINT);
    lance_check_interrupts(l);
}

/* SetTransmitInterrupt */
static void lance_set_transmit_interrupt(Am7990Lance *l)
{
    l->csr[0] = (uint16_t)(l->csr[0] | CSR0_FLAGS_TINT);
    lance_check_interrupts(l);
}

/* CheckInterrupts */
static void lance_check_interrupts(Am7990Lance *l)
{
    uint16_t current_flags = l->csr[0];
    uint16_t mask_flags = l->csr[3];
    bool should_interrupt = false;

    if ((current_flags & CSR0_FLAGS_INEA) != 0u)
    {
        if (((current_flags & CSR0_FLAGS_RINT) != 0u) && ((mask_flags & CSR3_FLAGS_RINTM) == 0u))
        {
            should_interrupt = true;
        }
        if (((current_flags & CSR0_FLAGS_TINT) != 0u) && ((mask_flags & CSR3_FLAGS_TINTM) == 0u))
        {
            should_interrupt = true;
        }
        if (((current_flags & CSR0_FLAGS_IDON) != 0u) && ((mask_flags & CSR3_FLAGS_IDONM) == 0u))
        {
            should_interrupt = true;
        }
        if (((current_flags & CSR0_FLAGS_MERR) != 0u) && ((mask_flags & CSR3_FLAGS_MERRM) == 0u))
        {
            should_interrupt = true;
        }
        if (((current_flags & CSR0_FLAGS_BABL) != 0u) && ((mask_flags & CSR3_FLAGS_BABLM) == 0u))
        {
            should_interrupt = true;
        }
        if (((current_flags & CSR0_FLAGS_CERR) != 0u) && ((mask_flags & CSR3_FLAGS_CERRM) == 0u))
        {
            should_interrupt = true;
        }
        if (((current_flags & CSR0_FLAGS_MISS) != 0u) && ((mask_flags & CSR3_FLAGS_MISSM) == 0u))
        {
            should_interrupt = true;
        }
    }

    if (should_interrupt)
    {
        l->csr[0] = (uint16_t)(l->csr[0] | CSR0_FLAGS_INTR);
    }
    else
    {
        l->csr[0] = (uint16_t)(l->csr[0] & (uint16_t)~CSR0_FLAGS_INTR);
    }

    if (should_interrupt != l->irq_asserted)
    {
        l->irq_asserted = should_interrupt;
        if (l->ev.on_lance_irq != NULL)
        {
            l->ev.on_lance_irq(l->ev.ctx);
        }
    }
}

bool lance_is_interrupt_active(const Am7990Lance *l)
{
    return ((l->csr[0] & CSR0_FLAGS_INTR) != 0u) && ((l->csr[0] & CSR0_FLAGS_INEA) != 0u);
}

void lance_get_physical_address(const Am7990Lance *l, uint8_t out[6])
{
    memcpy(out, l->physical_address, 6);
}

void lance_clear_interrupt(Am7990Lance *l)
{
    l->csr[0] = (uint16_t)(l->csr[0] & (uint16_t)~CSR0_FLAGS_INTR);
}

/* ---- test / simulation helpers (public in the C#) ---------------------------- */

void lance_simulate_packet_received(Am7990Lance *l)
{
    if (l->receiver_enabled)
    {
        lance_set_receive_interrupt(l);
    }
}

void lance_simulate_memory_error(Am7990Lance *l)
{
    lance_set_memory_error(l);
}

/* ExecuteLoopbackTest */
static void lance_execute_loopback_test(Am7990Lance *l)
{
    lance_set_transmit_interrupt(l);
    lance_set_receive_interrupt(l);
}

/* ExecuteReceiveTest */
static void lance_execute_receive_test(Am7990Lance *l)
{
    lance_simulate_packet_received(l);
}

/* ExecuteTransmitTest */
static void lance_execute_transmit_test(Am7990Lance *l)
{
    lance_execute_transmit_demand(l);
}

/* ExecuteFullDuplexTest */
static void lance_execute_full_duplex_test(Am7990Lance *l)
{
    lance_execute_transmit_demand(l);
    lance_simulate_packet_received(l);
}

void lance_process_network_test(Am7990Lance *l, int test_mode)
{
    switch (test_mode)
    {
    case 1:
        lance_execute_loopback_test(l);
        break;
    case 5:
        lance_execute_receive_test(l);
        break;
    case 6:
        lance_execute_transmit_test(l);
        break;
    case 7:
        lance_execute_full_duplex_test(l);
        break;
    default:
        break;
    }
}

/* ---- receive queue (network thread -> emulation thread) ----------------------- */

static void rx_lock(Am7990Lance *l)
{
    if (l->ev.rx_lock != NULL)
    {
        l->ev.rx_lock(l->ev.ctx);
    }
}

static void rx_unlock(Am7990Lance *l)
{
    if (l->ev.rx_unlock != NULL)
    {
        l->ev.rx_unlock(l->ev.ctx);
    }
}

void lance_enqueue_received_packet(Am7990Lance *l, const uint8_t *data, int length)
{
    int copy;

    rx_lock(l);
    if (l->rx_queue_count >= LANCE_RX_QUEUE_SIZE)
    {
        rx_unlock(l);
        return;
    }
    copy = (length > LANCE_PACKET_BUFFER_SIZE) ? LANCE_PACKET_BUFFER_SIZE : length;
    if (copy < 0)
    {
        copy = 0;
    }
    memcpy(l->rx_queue[l->rx_queue_head], data, (size_t)copy);
    l->rx_queue_lengths[l->rx_queue_head] = copy;
    l->rx_queue_used[l->rx_queue_head] = true;
    l->rx_queue_head = (l->rx_queue_head + 1) & (LANCE_RX_QUEUE_SIZE - 1);
    l->rx_queue_count++;
    rx_unlock(l);

    if (l->ev.on_packet_receive != NULL)
    {
        l->ev.on_packet_receive(l->ev.ctx, data, length);
    }
}

void lance_clock(Am7990Lance *l)
{
    for (;;)
    {
        uint8_t *pkt = l->rx_scratch;
        int pkt_len;
        bool have;

        rx_lock(l);
        if (l->rx_queue_count == 0)
        {
            rx_unlock(l);
            break;
        }
        have = l->rx_queue_used[l->rx_queue_tail];
        pkt_len = l->rx_queue_lengths[l->rx_queue_tail];
        memcpy(pkt, l->rx_queue[l->rx_queue_tail], (size_t)pkt_len);
        l->rx_queue_used[l->rx_queue_tail] = false;
        l->rx_queue_tail = (l->rx_queue_tail + 1) & (LANCE_RX_QUEUE_SIZE - 1);
        l->rx_queue_count--;
        rx_unlock(l);

        if (have)
        {
            int result = lance_receive_packet(l, pkt, pkt_len);
            if (result != 0)
            {
                lance_receive_complete(l, result);
            }
        }
    }

    if (l->tx_poll_pending && l->transmitter_enabled && ((l->csr[0] & CSR0_FLAGS_TXON) != 0u))
    {
        l->tx_poll_pending = false;
        lance_transmit_poll(l);
    }
}
