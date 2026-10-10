/*
 * device_ethernet.c - ND Ethernet II controller, PCB 3094 (port of RetroCore NDBusEthernetII).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Ported from RetroCore NDBusEthernetII.cs (commit 935163f): constructor,
 * Clock, Reset, Read, Write, IDENT, the 68000 interrupt-acknowledge
 * handlers, MemoryMap_OnNDInterrupt / OnTriggerInterrupt / OnBusError,
 * TriggerMFPVector. Diagnostics that only log (Watch68KPc, [BIT2]/[PRKEY]
 * traces, DumpRamToFile, LogResetVector, the RX-inject probe which is off by
 * default) are not ported; see the trace matrix.
 */

#include "device_ethernet.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../devices_types.h"
#include "../devices_protos.h"
#include "../../cpu/cpu_types.h"
#include "m68k.h"
#include "net/eth_ipcsum.h"

/* ---- differential trace -------------------------------------------------------- */

static void trace_line(EthCard *c, const char *stream, long *seq, const char *event, const char *fields)
{
    if (!c->trace_on || (c->trace.out == NULL))
    {
        return;
    }
    if ((fields != NULL) && (fields[0] != '\0'))
    {
        (void)fprintf(c->trace.out, "%s %ld %ld %s %s\n", stream, (*seq)++, c->trace_tick, event, fields);
    }
    else
    {
        (void)fprintf(c->trace.out, "%s %ld %ld %s\n", stream, (*seq)++, c->trace_tick, event);
    }
}

static void trace_nd(EthCard *c, const char *event, const char *fields)
{
    trace_line(c, "ND", &c->trace.seq_nd, event, fields);
}

static void trace_m68k(EthCard *c, const char *event, const char *fields)
{
    if (!c->trace.bus_bytes && (event[0] == 'R' || event[0] == 'W') && (event[1] == '8'))
    {
        return;
    }
    trace_line(c, "M68K", &c->trace.seq_m68k, event, fields);
}

static void mem_trace_m68k(void *ctx, const char *event, const char *fields)
{
    trace_m68k((EthCard *)ctx, event, fields);
}

static int mem_trace_fc(void *ctx)
{
    return (int)((EthCard *)ctx)->cpu.fc;
}

void eth_set_trace(EthCard *card, FILE *out, bool bus_bytes)
{
    card->trace.out = out;
    card->trace.bus_bytes = bus_bytes;
    card->trace_on = (out != NULL);
    card->trace.seq_nd = 0;
    card->trace.seq_m68k = 0;
    card->trace.seq_net = 0;
    card->mem.ev.trace_m68k = ((out != NULL) && bus_bytes) ? mem_trace_m68k : NULL;
}

/* ---- 68000 interrupt lines --------------------------------------------------------- */

/* CpuSetIrq (trace hook) -> cpu.InterruptControllerSetInterrupt */
static void eth_cpu_set_irq(EthCard *c, int level, bool state)
{
    char f[32];
    (void)snprintf(f, sizeof f, "lvl=%x on=%d", level, state ? 1 : 0);
    trace_m68k(c, "IRQ", f);
    eth_m68k_set_irq(&c->cpu, level, state);
}

/* MemoryMap_OnTriggerInterrupt */
static void eth_memory_map_on_trigger_interrupt(void *ctx, int level, bool state)
{
    eth_cpu_set_irq((EthCard *)ctx, level, state);
}

/* SetInterruptBit (NDBusDeviceBase) */
static void eth_set_interrupt_bit(EthCard *c, bool active)
{
    dev_set_interrupt_status(c->dev, active, ETH_INTERRUPT_LEVEL);
}

static bool eth_interrupt_bit(const EthCard *c)
{
    return (c->dev->interruptBits & (1u << ETH_INTERRUPT_LEVEL)) != 0u;
}

/* MemoryMap_OnNDInterrupt: SCIP from the 68000 */
static void eth_memory_map_on_nd_interrupt(void *ctx)
{
    EthCard *c = (EthCard *)ctx;

    c->scip_pending = true;
    if (c->interrupt_enabled)
    {
        eth_set_interrupt_bit(c, true);
    }
}

/* MemoryMap_OnBusError */
static void eth_memory_map_on_bus_error(void *ctx, uint32_t address, bool is_read)
{
    EthCard *c = (EthCard *)ctx;
    char f[48];

    c->first_bus_error_occurred = true;
    (void)snprintf(f, sizeof f, "a=%x rw=%s fc=%x", address, is_read ? "r" : "w", c->cpu.fc);
    trace_m68k(c, "BERR", f);
    eth_m68k_bus_error(&c->cpu, address, is_read); /* does not return */
}

/* Record one frame in the recent-frame log (F12 packet view). */
static void eth_log_frame(EthCard *c, bool is_tx, const uint8_t *data, int length)
{
    EthFrameLogEntry *e;
    struct timespec ts;

    if ((c->frame_log == NULL) || (data == NULL) || (length < 0))
    {
        return;
    }
    e = &c->frame_log[c->frame_log_pos];
    c->frame_log_pos = (c->frame_log_pos + 1) % ETH_FRAME_LOG_SIZE;
    e->seq = ++c->frame_seq;
    e->time_ms = 0;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
    {
        e->time_ms = ((int64_t)ts.tv_sec * 1000) + (ts.tv_nsec / 1000000L);
    }
    e->is_tx = is_tx;
    e->length = length;
    e->captured = (length > ETH_FRAME_LOG_BYTES) ? ETH_FRAME_LOG_BYTES : length;
    memcpy(e->data, data, (size_t)e->captured);
}

int eth_get_recent_frames(const EthCard *card, EthFrameLogEntry *out, int max)
{
    int n = 0;

    if ((card == NULL) || (card->frame_log == NULL) || (out == NULL) || (max <= 0))
    {
        return 0;
    }
    /* frame_log_pos is the oldest slot once the ring has wrapped */
    for (int i = 0; (i < ETH_FRAME_LOG_SIZE) && (n < max); i++)
    {
        const EthFrameLogEntry *e = &card->frame_log[(card->frame_log_pos + i) % ETH_FRAME_LOG_SIZE];
        if (e->seq != 0u)
        {
            out[n++] = *e;
        }
    }
    return n;
}

void eth_clear_recent_frames(EthCard *card)
{
    if ((card == NULL) || (card->frame_log == NULL))
    {
        return;
    }
    memset(card->frame_log, 0, sizeof(EthFrameLogEntry) * ETH_FRAME_LOG_SIZE);
    card->frame_log_pos = 0;
    card->frame_seq = 0u;
}

static void eth_on_packet_transmit(void *ctx, const uint8_t *data, int length)
{
    EthCard *c = (EthCard *)ctx;
    c->tx_packets++;
    c->tx_bytes += length;
    eth_log_frame(c, true, data, length);
    /* guest -> wire (RetroCore AttachNetwork: lance.OnPacketTransmit -> backend.SendPacket) */
    if (c->net != NULL)
    {
        eth_net_send(c->net, data, length);
    }
}

static void eth_on_packet_receive(void *ctx, const uint8_t *data, int length)
{
    EthCard *c = (EthCard *)ctx;
    (void)data;
    c->rx_packets++;
    c->rx_bytes += length;
}

/* ---- 68000 bus -> card memory map ---------------------------------------------------- */

static uint8_t eth_bus_read8(void *ctx, uint32_t address)
{
    return ethmem_read_memory(&((EthCard *)ctx)->mem, address);
}

static void eth_bus_write8(void *ctx, uint32_t address, uint8_t value)
{
    ethmem_write_memory(&((EthCard *)ctx)->mem, address, value);
}

/* HandleLANCEInterruptAck */
static EthIackType eth_handle_lance_interrupt_ack(EthCard *c)
{
    if (lance_is_interrupt_active(&c->mem.lance))
    {
        eth_cpu_set_irq(c, 2, true);
    }
    return ETH_IACK_AUTOVECTOR;
}

/* HandleMFPInterruptAck */
static EthIackType eth_handle_mfp_interrupt_ack(EthCard *c, uint8_t *vector)
{
    uint8_t mfp_vector = mfp_get_interrupt_vector(&c->mem.mfp);

    if (mfp_vector != 0u)
    {
        *vector = mfp_vector;
        return ETH_IACK_VECTORED;
    }
    return ETH_IACK_SPURIOUS;
}

/* HandleParityErrorInterruptAck */
static EthIackType eth_handle_parity_error_interrupt_ack(EthCard *c)
{
    if (ethmem_is_timer_interrupt_pending(&c->mem))
    {
        ethmem_clear_timer_interrupt(&c->mem);
    }
    eth_cpu_set_irq(c, 5, false);
    return ETH_IACK_AUTOVECTOR;
}

/* HandleOPCOMInterruptAck */
static EthIackType eth_handle_opcom_interrupt_ack(EthCard *c)
{
    if (ethmem_is_timer_interrupt_pending(&c->mem))
    {
        ethmem_clear_timer_interrupt(&c->mem);
    }
    eth_cpu_set_irq(c, 6, false);
    return ETH_IACK_AUTOVECTOR;
}

/* HandlePowerFailureInterruptAck */
static EthIackType eth_handle_power_failure_interrupt_ack(EthCard *c)
{
    eth_cpu_set_irq(c, 7, false);
    return ETH_IACK_AUTOVECTOR;
}

/* Cpu_OnInterruptAck */
static EthIackType eth_cpu_on_interrupt_ack(void *ctx, int level, uint8_t *vector)
{
    EthCard *c = (EthCard *)ctx;
    EthIackType type;
    uint8_t vec = (uint8_t)(24 + level); /* InterruptAck.VectorNumber starts as the autovector */
    char f[48];

    switch (level)
    {
    case 2:
        type = eth_handle_lance_interrupt_ack(c);
        break;
    case 3:
        type = eth_handle_mfp_interrupt_ack(c, &vec);
        break;
    case 4:
        type = ETH_IACK_AUTOVECTOR; /* HandleConsoleInterruptAck */
        break;
    case 5:
        type = eth_handle_parity_error_interrupt_ack(c);
        break;
    case 6:
        type = eth_handle_opcom_interrupt_ack(c);
        break;
    case 7:
        type = eth_handle_power_failure_interrupt_ack(c);
        break;
    default:
        type = ETH_IACK_SPURIOUS;
        break;
    }
    (void)snprintf(f, sizeof f, "lvl=%x type=%s vec=%x", level,
                   (type == ETH_IACK_VECTORED) ? "VEC" : ((type == ETH_IACK_SPURIOUS) ? "SP" : "AV"), vec);
    trace_m68k(c, "IACK", f);
    *vector = vec;
    return type;
}

/* cpu.SetIrqFlag(IrqType.RESET), traced */
static void eth_cpu_reset(EthCard *c)
{
    trace_m68k(c, "RESET", "");
    eth_m68k_reset(&c->cpu);
}

/* ---- ND-100 side ---------------------------------------------------------------------- */

static EthCard *card_of(Device *self)
{
    return (EthCard *)self->deviceData;
}

EthCard *eth_card(Device *dev)
{
    return (dev != NULL) ? (EthCard *)dev->deviceData : NULL;
}

bool eth_is_68k_running(const EthCard *card)
{
    return !card->halt && !card->reset;
}

/* Reset (C# override Reset) */
static void eth_reset(Device *self)
{
    EthCard *c = card_of(self);

    if (c == NULL)
    {
        return;
    }
    c->interrupt_enabled = false;
    c->nd_interrupt = false;
    c->start_opcom = false;
    c->power_low = false;
    c->disable_check_bit = false;
    c->scip_pending = false;
    c->first_bus_error_occurred = false;
    c->reset = true;
    c->halt = true;
    ethmem_reset(&c->mem);
}

/* Read */
static uint16_t eth_read(Device *self, uint32_t address)
{
    EthCard *c = card_of(self);
    uint32_t reg = dev_register_address(self, address);
    uint16_t value = 0u;
    char f[40];

    switch (reg)
    {
    case ETH_REGISTER_READ_DATA_REGISTER:
        /* fall through */
    case ETH_REGISTER_READ_STATUS_REGISTER:
        value = (uint16_t)(value | ((unsigned)c->memory_bank << 8u));
        if (c->halt)
        {
            value = (uint16_t)(value | (1u << 5u));
        }
        if (c->reset)
        {
            value = (uint16_t)(value | (1u << 4u));
        }
        if (eth_interrupt_bit(c))
        {
            value = (uint16_t)(value | (1u << 2u));
        }
        if (c->interrupt_enabled)
        {
            value = (uint16_t)(value | (1u << 0u));
        }
        break;
    default:
        break;
    }
    (void)snprintf(f, sizeof f, "reg=%x val=%x", reg, value);
    trace_nd(c, "IOX_R", f);
    return value;
}

/* Write */
static void eth_write(Device *self, uint32_t address, uint16_t value)
{
    EthCard *c = card_of(self);
    uint32_t reg = dev_register_address(self, address);
    char f[40];

    (void)snprintf(f, sizeof f, "reg=%x val=%x", reg, value);
    trace_nd(c, "IOX_W", f);

    switch (reg)
    {
    case ETH_REGISTER_WRITE_DATA_BUFFER:
        /* fall through: a write to the data buffer goes to the control word */
    case ETH_REGISTER_WRITE_CONTROL_WORD:
        c->interrupt_enabled = (value & (1u << 0u)) != 0u;

        c->previous_nd_interrupt = c->nd_interrupt;
        c->previous_start_opcom = c->start_opcom;
        c->previous_reset = c->reset;
        c->previous_power_low = c->power_low;

        c->nd_interrupt = (value & (1u << 2u)) != 0u;
        c->start_opcom = (value & (1u << 3u)) != 0u;
        c->reset = (value & (1u << 4u)) != 0u;
        c->halt = (value & (1u << 5u)) != 0u;
        c->power_low = (value & (1u << 6u)) != 0u;
        c->disable_check_bit = (value & (1u << 8u)) != 0u;

        /* SCIP latch (RFT): kept across control writes; INT12 = RFT AND RIE */
        if (c->scip_pending)
        {
            if (c->interrupt_enabled)
            {
                if (!eth_interrupt_bit(c))
                {
                    eth_set_interrupt_bit(c, true);
                }
            }
            else
            {
                if (eth_interrupt_bit(c))
                {
                    eth_set_interrupt_bit(c, false);
                }
            }
        }

        if (c->reset && !c->previous_reset)
        {
            ethmem_reset(&c->mem);
            c->scip_pending = false;
        }
        else if (!c->reset && c->previous_reset)
        {
            ethmem_initialize_mfp_from_firmware(&c->mem);
            eth_cpu_reset(c);
        }

        /* ND interrupt is a per-write strobe on MFP GPIP I6 (low then high) */
        if (c->nd_interrupt)
        {
            mfp_gpio_input(&c->mem.mfp, 6, false);
            mfp_gpio_input(&c->mem.mfp, 6, true);
        }
        else
        {
            mfp_gpio_input(&c->mem.mfp, 6, true);
        }

        /* bit 6 only stores the power-low enable; no NMI here */

        /* OPCOM flip-flop is clocked by every write with bit 3 set */
        if (c->start_opcom)
        {
            ethmem_interrupt_controller_set_interrupt(&c->mem, 6, true);
        }

        eth_m68k_enable_interrupts(&c->cpu, !c->halt && !c->reset);
        break;
    default:
        break;
    }
}

/* IDENT */
static uint16_t eth_ident(Device *self, uint16_t level)
{
    EthCard *c = card_of(self);
    uint16_t ident_code;
    char f[40];

    if ((self->interruptBits & (1u << level)) == 0u)
    {
        return 0u;
    }
    c->interrupt_enabled = false;
    c->scip_pending = false;
    /* NDBusDeviceBase.IDENT: clear the level's bit, return the ident code */
    dev_set_interrupt_status(self, false, level);
    ident_code = self->identCode;
    (void)snprintf(f, sizeof f, "lvl=%x code=%x", level, ident_code);
    trace_nd(c, "IDENT", f);
    return ident_code;
}

/* ---- host network ------------------------------------------------------------------- */

#define ETH_MIN_FRAME_BYTES 60 /* cs: Emulated.HW/ND/CPU/NDBUS/NDBusEthernetII.cs:1958 */

/* One received frame, wire -> guest (RetroCore AttachNetwork receive hook, :2070-2119). */
static void eth_deliver_received(EthCard *c, uint8_t *data, int length)
{
    if (length >= 12)
    {
        uint8_t mac[6];
        lance_get_physical_address(&c->mem.lance, mac);
        if (memcmp(data + 6, mac, sizeof mac) == 0)
        {
            /* Own transmission echoed back (multicast loopback, capture): a LANCE in
             * normal mode does not receive its own frame. */
            c->own_echoes_dropped++;
            return;
        }
    }
    /* Frames from a host with TX checksum offload arrive unfinished; repair only
     * what fails to verify. */
    if (c->repair_checksums && (eth_ipcsum_repair(data, length) != ETH_IPCSUM_NONE))
    {
        c->checksums_repaired++;
    }
    /* A real segment never delivers less than 60 bytes plus FCS; software adapters do.
     * RetroCore measured SINTRAN ignoring 42-byte ARP replies until they were padded. */
    if ((length >= 14) && (length < ETH_MIN_FRAME_BYTES))
    {
        memset(data + length, 0, (size_t)(ETH_MIN_FRAME_BYTES - length));
        length = ETH_MIN_FRAME_BYTES;
        c->runt_frames_padded++;
    }
    eth_log_frame(c, false, data, length);
    lance_enqueue_received_packet(&c->mem.lance, data, length);
}

/* Move waiting frames from the backend's ring into the LANCE receive queue. Runs on the
 * emulation thread. Deviation: RetroCore enqueues on the backend thread and the LANCE drops
 * when its 16-frame queue is full; here a frame is only taken from the ring when the LANCE
 * queue has room, so it waits in the 512-frame ring instead of being lost. */
static void eth_poll_network(EthCard *c)
{
    static uint8_t frame[ETH_NET_MAX_FRAME];

    while (eth_net_has_frame(c->net) && (c->mem.lance.rx_queue_count < LANCE_RX_QUEUE_SIZE))
    {
        int length = eth_net_receive(c->net, frame, ETH_NET_MAX_FRAME);
        if (length <= 0)
        {
            break;
        }
        eth_deliver_received(c, frame, length);
    }
}

int eth_attach_network(EthCard *card, const char *spec)
{
    EthNetSpec parsed;
    EthNet *net;

    if ((card == NULL) || (eth_net_parse_spec(spec, &parsed) != 0))
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Ethernet II %d: bad network spec '%s'\n",
            (card != NULL) ? card->thumbwheel : -1, (spec != NULL) ? spec : "");
        return -1;
    }
    net = eth_net_create(&parsed);
    if (net == NULL)
    {
        return -1;
    }
    eth_detach_network(card);
    if (eth_net_start(net) != 0)
    {
        eth_net_destroy(net);
        return -1;
    }
    card->net = net;
    LOG(LOG_CAT_NET, LOG_INFO, "Ethernet II %d: network attached: %s (active=%d)\n", card->thumbwheel,
        eth_net_description(net), eth_net_is_active(net) ? 1 : 0);
    return 0;
}

void eth_detach_network(EthCard *card)
{
    if ((card == NULL) || (card->net == NULL))
    {
        return;
    }
    eth_net_destroy(card->net);
    card->net = NULL;
}

/* Clock */
static uint16_t eth_tick(Device *self)
{
    EthCard *c = card_of(self);

    c->trace_tick++;
    ethmem_clock(&c->mem);
    for (int i = 0; i < ETH_CPU68K_CYCLES_PER_ND100_TICK; i++)
    {
        eth_m68k_tick(&c->cpu, c->halt || c->reset);
    }
    if (c->net != NULL)
    {
        eth_poll_network(c);
    }

    if (c->trace_on)
    {
        bool int_now = eth_interrupt_bit(c);
        if (int_now != c->trace_int_last)
        {
            char f[32];
            c->trace_int_last = int_now;
            (void)snprintf(f, sizeof f, "lvl=%x on=%d", ETH_INTERRUPT_LEVEL, int_now ? 1 : 0);
            trace_nd(c, "INT", f);
        }
    }
    return self->interruptBits;
}

static void eth_destroy(Device *self)
{
    EthCard *c = card_of(self);

    if (c == NULL)
    {
        return;
    }
    eth_detach_network(c);
    if (c->bank_registered)
    {
        (void)mms_memory_bank_unregister_over_local(c->physical_page_start / 2u);
    }
    free(c->frame_log);
    free(c);
    self->deviceData = NULL;
}

/* ---- DRAM window seen by the ND-100 (big-endian words, ND100Memory) ------------------- */

static uint16_t eth_bank_read(void *ctx, uint32_t word_offset)
{
    EthCard *c = (EthCard *)ctx;
    uint32_t b = word_offset * 2u;

    c->nd_window_reads++;
    if (b + 1u >= ETHMEM_DRAM_SIZE)
    {
        return 0u;
    }
    return (uint16_t)(((unsigned)c->mem.dram[b] << 8u) | c->mem.dram[b + 1u]);
}

static void eth_bank_write(void *ctx, uint32_t word_offset, uint16_t value, WriteMode wm)
{
    EthCard *c = (EthCard *)ctx;
    uint32_t b = word_offset * 2u;

    c->nd_window_writes++;
    if (b + 1u >= ETHMEM_DRAM_SIZE)
    {
        return;
    }
    switch (wm)
    {
    case WRITEMODE_MSB:
        c->mem.dram[b] = (uint8_t)(value & 0xFFu);
        break;
    case WRITEMODE_LSB:
        c->mem.dram[b + 1u] = (uint8_t)(value & 0xFFu);
        break;
    case WRITEMODE_WORD:
    default:
        c->mem.dram[b] = (uint8_t)(value >> 8u);
        c->mem.dram[b + 1u] = (uint8_t)(value & 0xFFu);
        break;
    }
}

int eth_register_dram_window(EthCard *card)
{
    if (card->bank_registered)
    {
        return 0;
    }
    /* Over local RAM the card takes the window (RetroCore ND100Memory.FindMemoryBank
     * checks the card before local RAM); mms splits the local bank around it. */
    if (!mms_memory_bank_register_over_local(card->physical_page_start / 2u, ETHMEM_DRAM_SIZE / 2u, ND_MEM_PIOC,
                                             eth_bank_read, eth_bank_write, card))
    {
        return -1;
    }
    card->bank_registered = true;
    return 0;
}

int eth_set_memory_bank(EthCard *card, uint16_t memory_bank)
{
    uint16_t bank;
    uint32_t old_start = card->physical_page_start;
    uint16_t old_bank = card->memory_bank;

    if ((memory_bank % 4u) != 0u)
    {
        return -1;
    }
    bank = (memory_bank != 0u) ? memory_bank : (uint16_t)(ETH_BASE_BANK + (ETH_BANK_STEP_PER_CARD * card->thumbwheel));
    if (bank == card->memory_bank)
    {
        return 0;
    }
    if (card->bank_registered)
    {
        (void)mms_memory_bank_unregister_over_local(old_start / 2u);
        card->bank_registered = false;
    }
    card->memory_bank = bank;
    card->physical_page_start = (uint32_t)bank * 0x40u * 2048u;
    card->mem.ram_start = card->physical_page_start;
    if (eth_register_dram_window(card) != 0)
    {
        card->memory_bank = old_bank;
        card->physical_page_start = old_start;
        card->mem.ram_start = old_start;
        (void)eth_register_dram_window(card);
        return -1;
    }
    return 0;
}

/* ---- TriggerMFPVector ---------------------------------------------------------------- */

void eth_trigger_mfp_vector(EthCard *card, uint8_t vector)
{
    MC68901MFP *mfp = &card->mem.mfp;

    switch (vector)
    {
    case 117u:
        mfp_gpio_input(mfp, 7, false);
        mfp_trigger_interrupt(mfp, 7);
        break;
    case 116u:
        mfp_gpio_input(mfp, 6, false);
        mfp_trigger_interrupt(mfp, 6);
        break;
    case 114u:
        mfp_trigger_usart_receive_interrupt(mfp);
        break;
    case 113u:
        mfp_trigger_usart_receive_error(mfp);
        break;
    case 112u:
        mfp_trigger_usart_transmit_interrupt(mfp);
        break;
    case 111u:
        mfp_trigger_usart_transmit_error(mfp);
        break;
    case 107u:
        mfp_gpio_input(mfp, 5, false);
        mfp_trigger_interrupt(mfp, 5);
        break;
    case 105u:
        mfp_trigger_timer_c_interrupt(mfp);
        break;
    default:
        break;
    }
}

/* ---- construction ------------------------------------------------------------------------- */

Device *eth_create_device(uint8_t thumbwheel)
{
    return eth_create_device_strap(thumbwheel, 0u);
}

Device *eth_create_device_strap(uint8_t thumbwheel, uint16_t memory_bank_strap)
{
    static const uint16_t base[4] = {0140360, 0140364, 0140370, 0140374};
    static const uint16_t ident[4] = {0140034, 0140035, 0140036, 0140037};
    Device *dev;
    EthCard *c;
    EthMemEvents mev;
    EthM68kBus bus;

    if (thumbwheel > 3u)
    {
        return NULL;
    }
    /* ND-12.055.1 status register: the card starts on a half-megabyte boundary,
     * so the bank number is a multiple of 4; 0 = not strapped (C#) */
    if ((memory_bank_strap % 4u) != 0u)
    {
        LOG(LOG_CAT_DEVICE, LOG_ERROR, "Ethernet II %d: bank %u is not a multiple of 4\n", thumbwheel,
            memory_bank_strap);
        return NULL;
    }
    dev = (Device *)malloc(sizeof(Device));
    if (dev == NULL)
    {
        return NULL;
    }
    c = (EthCard *)calloc(1u, sizeof(EthCard));
    if (c == NULL)
    {
        free(dev);
        return NULL;
    }

    dev_init(dev, thumbwheel, DEVICE_CLASS_STANDARD, 0);
    dev->deviceData = c;
    dev->type = DEVICE_TYPE_ETHERNET;
    dev->startAddress = base[thumbwheel];
    dev->endAddress = (uint32_t)base[thumbwheel] + 3u;
    dev->interruptLevel = ETH_INTERRUPT_LEVEL;
    dev->identCode = ident[thumbwheel];
    (void)snprintf(dev->memoryName, sizeof(dev->memoryName), "Ethernet II  3094 %d", thumbwheel + 1);

    dev->Read = eth_read;
    dev->Write = eth_write;
    dev->Tick = eth_tick;
    dev->Reset = eth_reset;
    dev->Ident = eth_ident;
    dev->Destroy = eth_destroy;

    c->dev = dev;
    c->repair_checksums = true;
    c->frame_log = calloc(ETH_FRAME_LOG_SIZE, sizeof(EthFrameLogEntry));
    if (c->frame_log == NULL)
    {
        free(c);
        free(dev);
        return NULL;
    }
    c->thumbwheel = thumbwheel;
    c->memory_bank = (memory_bank_strap != 0u) ? memory_bank_strap
                                               : (uint16_t)(ETH_BASE_BANK + (ETH_BANK_STEP_PER_CARD * thumbwheel));
    c->physical_page_start = (uint32_t)c->memory_bank * 0x40u * 2048u;
    c->reset = true;
    c->previous_reset = true;
    c->halt = true;

    memset(&mev, 0, sizeof mev);
    mev.ctx = c;
    mev.on_nd_interrupt = eth_memory_map_on_nd_interrupt;
    mev.on_trigger_interrupt = eth_memory_map_on_trigger_interrupt;
    mev.on_bus_error = eth_memory_map_on_bus_error;
    mev.on_packet_transmit = eth_on_packet_transmit;
    mev.on_packet_receive = eth_on_packet_receive;
    mev.trace_fc = mem_trace_fc;
    ethmem_create(&c->mem, c->physical_page_start, &mev);

    memset(&bus, 0, sizeof bus);
    bus.ctx = c;
    bus.read8 = eth_bus_read8;
    bus.write8 = eth_bus_write8;
    bus.iack = eth_cpu_on_interrupt_ack;
    if (eth_m68k_init(&c->cpu, &bus) != 0)
    {
        free(c->frame_log);
        free(c);
        free(dev);
        return NULL;
    }
    eth_cpu_reset(c); /* InitializeCPU: cpu.SetIrqFlag(RESET) */

    /* The ND-100 must reach the card's DRAM, never local RAM, at the window
     * addresses. If the window cannot be mapped (it overlaps another device's
     * memory, e.g. an MPM-5 window), the card is not created at all rather than
     * run with the ND-100 using some other memory there. */
    if (eth_register_dram_window(c) != 0)
    {
        LOG(LOG_CAT_DEVICE, LOG_ERROR,
            "Ethernet II %d: DRAM window ND-100 words %o-%o (bank %d) overlaps another device's "
            "memory - choose another bank\n",
            thumbwheel, c->physical_page_start / 2u, (c->physical_page_start + ETHMEM_DRAM_SIZE) / 2u - 1u,
            c->memory_bank);
        free(c->frame_log);
        free(c);
        free(dev);
        return NULL;
    }
    return dev;
}
