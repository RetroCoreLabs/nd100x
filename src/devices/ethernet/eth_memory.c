/*
 * eth_memory.c - The Ethernet II card's 68000 memory map (port of RetroCore NDEthernetMemory).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * See eth_memory.h for the decode. Dead code in the C# that is not ported:
 * Lance_OnReadDMA / Lance_OnWriteDMA / RaiseLanceMemoryError (the Am7990
 * never raises the IOMemoryBase DMA events; it only uses DmaIn/DmaOut,
 * Am2990Lance.cs:1020 on) - see the phase log.
 */

#include "eth_memory.h"

#include <stdio.h>
#include <string.h>

static const uint8_t s_reset_pattern[5] = {0x00u, 0xA0u, 0xA8u, 0xACu, 0xA4u};

static void trace(NDEthernetMemory *m, const char *event, const char *fields)
{
    if (m->ev.trace_m68k != NULL)
    {
        m->ev.trace_m68k(m->ev.ctx, event, fields);
    }
}

static int trace_fc(NDEthernetMemory *m)
{
    return (m->ev.trace_fc != NULL) ? m->ev.trace_fc(m->ev.ctx) : 0;
}

void ethmem_interrupt_controller_set_interrupt(NDEthernetMemory *m, int level, bool state)
{
    if (m->ev.on_trigger_interrupt != NULL)
    {
        m->ev.on_trigger_interrupt(m->ev.ctx, level, state);
    }
}

/* TriggerSCIPChannel */
static void ethmem_trigger_scip_channel(NDEthernetMemory *m, int channel)
{
    if ((channel < 0) || (channel > 7))
    {
        return;
    }
    m->current_scip_channel = channel;
    if (m->ev.on_nd_interrupt != NULL)
    {
        m->ev.on_nd_interrupt(m->ev.ctx);
    }
}

/* ---- chip callbacks ---------------------------------------------------------- */

/* Mfp_OnIRQ */
static void ethmem_mfp_on_irq(void *ctx, bool irq_state)
{
    ethmem_interrupt_controller_set_interrupt((NDEthernetMemory *)ctx, 3, irq_state);
}

/* mfp.OnRegisterWrite lambda: AM9519 reset pattern and software interrupt test */
static void ethmem_mfp_on_register_write(void *ctx, MFPRegister reg, uint8_t value)
{
    NDEthernetMemory *m = (NDEthernetMemory *)ctx;

    if (m->am9519_reset_pending && (reg == MFP_REGISTER_ISRB))
    {
        if ((m->isrb_write_count < 5) && (value == s_reset_pattern[m->isrb_write_count]))
        {
            m->isrb_write_count++;
            if (m->isrb_write_count >= 5)
            {
                for (int i = 0; i < 8; i++)
                {
                    mfp_gpio_input(&m->mfp, i, true);
                }
                m->am9519_reset_pending = false;
                m->isrb_write_count = 0;
                m->software_interrupt_test_active = true;
            }
        }
        else
        {
            m->isrb_write_count = 0;
            if (value == s_reset_pattern[0])
            {
                m->isrb_write_count = 1;
            }
        }
    }
    else if ((reg == MFP_REGISTER_ISRB) && (value >= 0x58u) && (value <= 0x5Fu))
    {
        int channel = 0x5F - (int)value;
        mfp_trigger_software_interrupt(&m->mfp, channel, 0x40u);
        ethmem_trigger_scip_channel(m, channel);
    }
}

/* Lance_OnIRQ */
static void ethmem_lance_on_irq(void *ctx)
{
    NDEthernetMemory *m = (NDEthernetMemory *)ctx;
    ethmem_interrupt_controller_set_interrupt(m, 2, lance_is_interrupt_active(&m->lance));
}

/* lance.DmaIn lambda */
static uint16_t ethmem_lance_dma_in(void *ctx, uint32_t addr)
{
    NDEthernetMemory *m = (NDEthernetMemory *)ctx;
    char f[48];
    uint16_t v;

    addr &= 0xFFFFFFu;
    if (addr + 1u >= ETHMEM_DRAM_SIZE)
    {
        (void)snprintf(f, sizeof f, "a=%x v=0 ok=0", addr);
        trace(m, "DMA_R", f);
        lance_simulate_memory_error(&m->lance);
        return 0u;
    }
    v = (uint16_t)(((unsigned)m->dram[addr] << 8u) | m->dram[addr + 1u]);
    (void)snprintf(f, sizeof f, "a=%x v=%x ok=1", addr, v);
    trace(m, "DMA_R", f);
    return v;
}

/* lance.DmaOut lambda */
static void ethmem_lance_dma_out(void *ctx, uint32_t addr, uint16_t data)
{
    NDEthernetMemory *m = (NDEthernetMemory *)ctx;
    char f[48];

    addr &= 0xFFFFFFu;
    (void)snprintf(f, sizeof f, "a=%x v=%x ok=%d", addr, data, (addr + 1u >= ETHMEM_DRAM_SIZE) ? 0 : 1);
    trace(m, "DMA_W", f);
    if (addr + 1u >= ETHMEM_DRAM_SIZE)
    {
        lance_simulate_memory_error(&m->lance);
        return;
    }
    m->dram[addr] = (uint8_t)(data >> 8u);
    m->dram[addr + 1u] = (uint8_t)(data & 0xFFu);
}

static void ethmem_lance_on_packet_transmit(void *ctx, const uint8_t *data, int length)
{
    NDEthernetMemory *m = (NDEthernetMemory *)ctx;
    if (m->ev.on_packet_transmit != NULL)
    {
        m->ev.on_packet_transmit(m->ev.ctx, data, length);
    }
}

static void ethmem_lance_on_packet_receive(void *ctx, const uint8_t *data, int length)
{
    NDEthernetMemory *m = (NDEthernetMemory *)ctx;
    if (m->ev.on_packet_receive != NULL)
    {
        m->ev.on_packet_receive(m->ev.ctx, data, length);
    }
}

static void ethmem_lance_rx_lock(void *ctx)
{
    NDEthernetMemory *m = (NDEthernetMemory *)ctx;
    if (m->ev.rx_lock != NULL)
    {
        m->ev.rx_lock(m->ev.ctx);
    }
}

static void ethmem_lance_rx_unlock(void *ctx)
{
    NDEthernetMemory *m = (NDEthernetMemory *)ctx;
    if (m->ev.rx_unlock != NULL)
    {
        m->ev.rx_unlock(m->ev.ctx);
    }
}

/* ethIoMem events */
static void ethmem_io_on_nd_interrupt(void *ctx)
{
    NDEthernetMemory *m = (NDEthernetMemory *)ctx;
    if (m->ev.on_nd_interrupt != NULL)
    {
        m->ev.on_nd_interrupt(m->ev.ctx);
    }
}

static void ethmem_io_on_scip_channel_tx(void *ctx, int channel)
{
    NDEthernetMemory *m = (NDEthernetMemory *)ctx;
    m->current_scip_channel = channel;
    if (m->ev.on_nd_interrupt != NULL)
    {
        m->ev.on_nd_interrupt(m->ev.ctx);
    }
}

static bool ethmem_io_get_lance_interrupt_state(void *ctx)
{
    return lance_is_interrupt_active(&((NDEthernetMemory *)ctx)->lance);
}

static void ethmem_io_on_lan_reset(void *ctx)
{
    lance_reset(&((NDEthernetMemory *)ctx)->lance);
}

/* GetEAREN */
static uint16_t ethmem_get_earen(void *ctx)
{
    return (uint16_t)((((NDEthernetMemory *)ctx)->last_error_address >> 1u) & 0xFFFFu);
}

static uint16_t ethmem_io_get_merrstat(void *ctx)
{
    return ((NDEthernetMemory *)ctx)->last_error_status;
}

static void ethmem_io_on_timer_interrupt(void *ctx)
{
    ethmem_interrupt_controller_set_interrupt((NDEthernetMemory *)ctx, 5, true);
}

static void ethmem_io_on_am9519_reset(void *ctx)
{
    NDEthernetMemory *m = (NDEthernetMemory *)ctx;
    m->am9519_reset_pending = true;
    m->isrb_write_count = 0;
    m->software_interrupt_test_active = false;
    for (int i = 0; i < 8; i++)
    {
        mfp_gpio_input(&m->mfp, i, false);
    }
}

static bool ethmem_io_was_mirrored_access(void *ctx)
{
    return ((NDEthernetMemory *)ctx)->last_write_was_mirrored;
}

/* ---- construction -------------------------------------------------------------- */

void ethmem_create(NDEthernetMemory *m, uint32_t ram_start, const EthMemEvents *events)
{
    MfpEvents mev;
    LanceEvents lev;
    EthIoEvents iev;

    if (m == NULL)
    {
        return;
    }
    memset(m, 0, sizeof *m);
    if (events != NULL)
    {
        m->ev = *events;
    }
    m->ram_start = ram_start;
    m->current_scip_channel = -1;

    memset(&mev, 0, sizeof mev);
    mev.ctx = m;
    mev.on_irq = ethmem_mfp_on_irq;
    mev.on_register_write = ethmem_mfp_on_register_write;
    mfp_create(&m->mfp, 0xEF00C0u, 0x40u, &mev);
    mfp_set_timer_clock(&m->mfp, 3125000);
    mfp_gpio_input(&m->mfp, 5, true);
    mfp_gpio_input(&m->mfp, 6, true);
    mfp_gpio_input(&m->mfp, 7, true);

    memset(&lev, 0, sizeof lev);
    lev.ctx = m;
    lev.on_lance_irq = ethmem_lance_on_irq;
    lev.on_packet_transmit = ethmem_lance_on_packet_transmit;
    lev.on_packet_receive = ethmem_lance_on_packet_receive;
    lev.dma_in = ethmem_lance_dma_in;
    lev.dma_out = ethmem_lance_dma_out;
    lev.rx_lock = ethmem_lance_rx_lock;
    lev.rx_unlock = ethmem_lance_rx_unlock;
    lance_create(&m->lance, 0xEF00A0u, 8u, &lev);
    m->lance.swap_byte_lanes = true;

    memset(&iev, 0, sizeof iev);
    iev.ctx = m;
    iev.on_nd_interrupt = ethmem_io_on_nd_interrupt;
    iev.on_scip_channel_tx = ethmem_io_on_scip_channel_tx;
    iev.get_lance_interrupt_state = ethmem_io_get_lance_interrupt_state;
    iev.on_lan_reset = ethmem_io_on_lan_reset;
    iev.get_earen_value = ethmem_get_earen;
    iev.get_merrstat_value = ethmem_io_get_merrstat;
    iev.on_timer_interrupt = ethmem_io_on_timer_interrupt;
    iev.on_am9519_reset = ethmem_io_on_am9519_reset;
    iev.was_mirrored_access = ethmem_io_was_mirrored_access;
    ethio_create(&m->eth_io_mem, 0xEF0000u, 0x100u, &iev);
    m->eth_io_mem.mfp_chip = &m->mfp;
}

/* ---- decode -------------------------------------------------------------------- */

typedef enum
{
    BANK_NONE = 0,
    BANK_RAM,
    BANK_MFP,
    BANK_LANCE,
    BANK_PROTECT,
    BANK_IO
} Bank;

/* ResolveAddressMirror */
static uint32_t ethmem_resolve_address_mirror(NDEthernetMemory *m, uint32_t address)
{
    if ((address >= 0xEF0100u) && (address <= 0xEF01FFu))
    {
        m->last_write_was_mirrored = true;
        return address & 0xFFFEFFu;
    }
    m->last_write_was_mirrored = false;
    return address;
}

/* FindMemoryBank */
static Bank ethmem_find_memory_bank(const NDEthernetMemory *m, uint32_t address)
{
    if ((address >= 0xF80000u) && (address <= 0xFFFFFFu))
    {
        return BANK_RAM;
    }
    if (mfp_is_mapped_address(&m->mfp, address))
    {
        return BANK_MFP;
    }
    if (lance_is_mapped_address(&m->lance, address))
    {
        return BANK_LANCE;
    }
    /* protect_table = RAM(0xF00000, 0x7FFFF): 0xF00000 .. 0xF7FFFE */
    if ((address >= 0xF00000u) && (address <= 0xF7FFFEu))
    {
        return BANK_PROTECT;
    }
    /* ram.IsMappedAddress(address + _ramStart) */
    if (((uint64_t)address + m->ram_start >= m->ram_start) &&
        ((uint64_t)address + m->ram_start <= (uint64_t)m->ram_start + ETHMEM_DRAM_SIZE - 1u))
    {
        return BANK_RAM;
    }
    if (ethio_is_mapped_address(&m->eth_io_mem, address))
    {
        return BANK_IO;
    }
    return BANK_NONE;
}

/* CaptureErrorInfo */
static void ethmem_capture_error_info(NDEthernetMemory *m, uint32_t address, bool is_read)
{
    uint16_t status = 0u;

    (void)is_read;
    m->last_error_address = address;
    status = (uint16_t)(status | (1u << 7u) | (1u << 6u));
    if ((address & (1u << 17u)) != 0u)
    {
        status = (uint16_t)(status | (1u << 8u));
    }
    if ((address & (1u << 18u)) != 0u)
    {
        status = (uint16_t)(status | (1u << 9u));
    }
    m->last_error_status = status;
}

static void ethmem_bus_error(NDEthernetMemory *m, uint32_t address, bool is_read)
{
    m->memory_unavailable = true;
    ethmem_capture_error_info(m, address, is_read);
    if (m->ev.on_bus_error != NULL)
    {
        m->ev.on_bus_error(m->ev.ctx, address, is_read);
    }
}

/* ReadMemoryUntraced */
static uint8_t ethmem_read_memory_untraced(NDEthernetMemory *m, uint32_t address)
{
    uint32_t ram_address = address;
    bool from_nd100 = (address >= 0xF80000u) && (address <= 0xFFFFFFu);
    uint32_t unmirrored_address;

    m->memory_unavailable = false;
    if (from_nd100)
    {
        ram_address = address - 0xF80000u;
    }
    unmirrored_address = address;
    address = ethmem_resolve_address_mirror(m, address);

    switch (ethmem_find_memory_bank(m, address))
    {
    case BANK_RAM:
        if (ram_address < ETHMEM_DRAM_SIZE)
        {
            return m->dram[ram_address];
        }
        ethmem_bus_error(m, address, true);
        return 0xFFu;
    case BANK_PROTECT:
        return 0x00u;
    case BANK_IO:
        return ethio_read(&m->eth_io_mem, unmirrored_address);
    case BANK_MFP:
        return mfp_read(&m->mfp, address);
    case BANK_LANCE:
        return lance_read(&m->lance, address);
    case BANK_NONE:
    default:
        break;
    }
    ethmem_bus_error(m, address, true);
    return 0xFFu;
}

/* WriteMemoryUntraced */
static void ethmem_write_memory_untraced(NDEthernetMemory *m, uint32_t address, uint8_t value)
{
    uint32_t ram_address = address;
    bool from_nd100 = (address >= 0xF80000u) && (address <= 0xFFFFFFu);
    uint32_t unmirrored_address;

    m->memory_unavailable = false;
    if (from_nd100)
    {
        ram_address = address - 0xF80000u;
    }
    unmirrored_address = address;
    address = ethmem_resolve_address_mirror(m, address);

    switch (ethmem_find_memory_bank(m, address))
    {
    case BANK_RAM:
        if (ram_address < ETHMEM_DRAM_SIZE)
        {
            m->dram[ram_address] = value;
            return;
        }
        ethmem_bus_error(m, address, false);
        return;
    case BANK_PROTECT:
        return;
    case BANK_IO:
        ethio_write(&m->eth_io_mem, unmirrored_address, value);
        return;
    case BANK_MFP:
        mfp_write(&m->mfp, address, value);
        return;
    case BANK_LANCE:
        lance_write(&m->lance, address, value);
        return;
    case BANK_NONE:
    default:
        break;
    }
    ethmem_bus_error(m, address, false);
}

uint8_t ethmem_read_memory(NDEthernetMemory *m, uint32_t address)
{
    uint8_t v = ethmem_read_memory_untraced(m, address);

    if (m->ev.trace_m68k != NULL)
    {
        char f[48];
        (void)snprintf(f, sizeof f, "a=%x v=%x fc=%x", address, v, trace_fc(m));
        trace(m, "R8", f);
    }
    return v;
}

void ethmem_write_memory(NDEthernetMemory *m, uint32_t address, uint8_t value)
{
    if (m->ev.trace_m68k != NULL)
    {
        char f[48];
        (void)snprintf(f, sizeof f, "a=%x v=%x fc=%x", address, value, trace_fc(m));
        trace(m, "W8", f);
    }
    ethmem_write_memory_untraced(m, address, value);
}

/* ---- the rest of NDEthernetMemory ------------------------------------------------ */

void ethmem_clock(NDEthernetMemory *m)
{
    mfp_clock(&m->mfp);
    lance_clock(&m->lance);
    ethio_clock(&m->eth_io_mem);
}

bool ethmem_load_ram(NDEthernetMemory *m, uint32_t offset, const uint8_t *bytes, uint32_t length)
{
    if ((offset > ETHMEM_DRAM_SIZE) || (length > ETHMEM_DRAM_SIZE - offset))
    {
        return false;
    }
    memcpy(&m->dram[offset], bytes, length);
    return true;
}

void ethmem_clear_error_registers(NDEthernetMemory *m)
{
    m->last_error_address = 0u;
    m->last_error_status = 0u;
}

void ethmem_reset_peripherals(NDEthernetMemory *m)
{
    mfp_reset(&m->mfp);
    lance_reset(&m->lance);
    ethio_reset(&m->eth_io_mem);
    m->am9519_reset_pending = false;
    m->isrb_write_count = 0;
    m->software_interrupt_test_active = false;
    m->current_scip_channel = -1;
    ethmem_clear_error_registers(m);
}

void ethmem_initialize_mfp_from_firmware(NDEthernetMemory *m)
{
    /* trivial: Emulated.HW/ND/CPU/NDBUS/NDBusEthernetII.cs:3495 */
    (void)m;
}

void ethmem_reset(NDEthernetMemory *m)
{
    /* ram.Reset() and protect_table.Reset() are empty in RetroCore (RAM.cs:107) */
    ethmem_reset_peripherals(m);
    ethmem_initialize_mfp_from_firmware(m);
}

void ethmem_on_cpu_reset_instruction(NDEthernetMemory *m)
{
    ethio_reset(&m->eth_io_mem);
}

void ethmem_set_mfp_interrupt(NDEthernetMemory *m)
{
    mfp_gpio_input(&m->mfp, 6, false);
    mfp_trigger_interrupt(&m->mfp, 6);
}

void ethmem_clear_mfp_interrupt(NDEthernetMemory *m)
{
    mfp_gpio_input(&m->mfp, 6, true);
}

bool ethmem_is_timer_interrupt_pending(const NDEthernetMemory *m)
{
    return ethio_is_timer_interrupt_pending(&m->eth_io_mem);
}

void ethmem_clear_timer_interrupt(NDEthernetMemory *m)
{
    ethio_clear_timer_interrupt(&m->eth_io_mem);
}

uint16_t ethmem_read_word(NDEthernetMemory *m, uint32_t mem_address)
{
    uint16_t hi = ethmem_read_memory(m, mem_address + m->ram_start);
    uint16_t lo = ethmem_read_memory(m, mem_address + 1u + m->ram_start);
    return (uint16_t)((hi << 8u) | lo);
}
