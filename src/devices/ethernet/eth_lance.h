/*
 * eth_lance.h - AMD Am7990 LANCE, ported from RetroCore.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Port of RetroCore Emulated.HW/AMD/LANCE/Am7990/Am2990Lance.cs (commit
 * 935163f). Behaviour is copied as it is; the only intended differences are
 * where the C# would throw (buffer bounds), listed in eth_lance.c.
 *
 * The receive queue is filled by a host network thread and emptied by the
 * emulation thread (C#: lock(_rxQueueLock)). Here the owner supplies
 * lock/unlock callbacks; with none set, both run on one thread.
 */

#ifndef ETH_LANCE_H
#define ETH_LANCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* LANCERegisters (register offset, A0 masked) */
#define LANCE_REGISTERS_REGISTER_DATA_PORT 0    /* cs: Emulated.HW/AMD/LANCE/Am7990/Am2990Lance.cs:137 */
#define LANCE_REGISTERS_REGISTER_ADDRESS_PORT 2 /* cs: Emulated.HW/AMD/LANCE/Am7990/Am2990Lance.cs:138 */

/* CSRAddress */
#define CSR_ADDRESS_CSR0 0 /* cs: Emulated.HW/AMD/LANCE/Am7990/Am2990Lance.cs:144 */
#define CSR_ADDRESS_CSR1 1 /* cs: Emulated.HW/AMD/LANCE/Am7990/Am2990Lance.cs:145 */
#define CSR_ADDRESS_CSR2 2 /* cs: Emulated.HW/AMD/LANCE/Am7990/Am2990Lance.cs:146 */
#define CSR_ADDRESS_CSR3 3 /* cs: Emulated.HW/AMD/LANCE/Am7990/Am2990Lance.cs:147 */

/* CSR0Flags */
#define CSR0_FLAGS_INIT 0x0001u /* cs: none - (1 << 0) */
#define CSR0_FLAGS_STRT 0x0002u /* cs: none - (1 << 1) */
#define CSR0_FLAGS_STOP 0x0004u /* cs: none - (1 << 2) */
#define CSR0_FLAGS_TDMD 0x0008u /* cs: none - (1 << 3) */
#define CSR0_FLAGS_TXON 0x0010u /* cs: none - (1 << 4) */
#define CSR0_FLAGS_RXON 0x0020u /* cs: none - (1 << 5) */
#define CSR0_FLAGS_INEA 0x0040u /* cs: none - (1 << 6) */
#define CSR0_FLAGS_INTR 0x0080u /* cs: none - (1 << 7) */
#define CSR0_FLAGS_IDON 0x0100u /* cs: none - (1 << 8) */
#define CSR0_FLAGS_TINT 0x0200u /* cs: none - (1 << 9) */
#define CSR0_FLAGS_RINT 0x0400u /* cs: none - (1 << 10) */
#define CSR0_FLAGS_MERR 0x0800u /* cs: none - (1 << 11) */
#define CSR0_FLAGS_MISS 0x1000u /* cs: none - (1 << 12) */
#define CSR0_FLAGS_CERR 0x2000u /* cs: none - (1 << 13) */
#define CSR0_FLAGS_BABL 0x4000u /* cs: none - (1 << 14) */
#define CSR0_FLAGS_ERR 0x8000u  /* cs: none - (1 << 15) */
#define CSR0_FLAGS_ANY_INTR 0x5F00u /* cs: none - BABL|MISS|MERR|RINT|TINT|IDON */
#define CSR0_FLAGS_ANY_ERR 0x7800u  /* cs: none - BABL|CERR|MISS|MERR */

/* CSR3Flags */
#define CSR3_FLAGS_BCON 0x0001u  /* cs: none - (1 << 0) */
#define CSR3_FLAGS_ACON 0x0002u  /* cs: none - (1 << 1) */
#define CSR3_FLAGS_BSWP 0x0004u  /* cs: none - (1 << 2) */
#define CSR3_FLAGS_IDONM 0x0100u /* cs: none - (1 << 8) */
#define CSR3_FLAGS_TINTM 0x0200u /* cs: none - (1 << 9) */
#define CSR3_FLAGS_RINTM 0x0400u /* cs: none - (1 << 10) */
#define CSR3_FLAGS_MERRM 0x0800u /* cs: none - (1 << 11) */
#define CSR3_FLAGS_MISSM 0x1000u /* cs: none - (1 << 12) */
#define CSR3_FLAGS_CERRM 0x2000u /* cs: none - (1 << 13) */
#define CSR3_FLAGS_BABLM 0x4000u /* cs: none - (1 << 14) */
#define CSR3_FLAGS_MASK 0x0007u  /* cs: Emulated.HW/AMD/LANCE/Am7990/Am2990Lance.cs:249 */

/* ModeFlags */
#define MODE_FLAGS_DRX 0x0001u  /* cs: none - (1 << 0) */
#define MODE_FLAGS_DTX 0x0002u  /* cs: none - (1 << 1) */
#define MODE_FLAGS_LOOP 0x0004u /* cs: none - (1 << 2) */
#define MODE_FLAGS_DTCR 0x0008u /* cs: none - (1 << 3) */
#define MODE_FLAGS_COLL 0x0010u /* cs: none - (1 << 4) */
#define MODE_FLAGS_DRTY 0x0020u /* cs: none - (1 << 5) */
#define MODE_FLAGS_INTL 0x0040u /* cs: none - (1 << 6) */
#define MODE_FLAGS_EMBA 0x0080u /* cs: none - (1 << 7) */
#define MODE_FLAGS_PROM 0x8000u /* cs: none - (1 << 15) */

/* TMD1Flags */
#define TMD1_FLAGS_HADR 0x00FFu    /* cs: Emulated.HW/AMD/LANCE/Am7990/Am2990Lance.cs:371 */
#define TMD1_FLAGS_ENP 0x0100u     /* cs: none - (1 << 8) */
#define TMD1_FLAGS_STP 0x0200u     /* cs: none - (1 << 9) */
#define TMD1_FLAGS_DEF 0x0400u     /* cs: none - (1 << 10) */
#define TMD1_FLAGS_ONE 0x0800u     /* cs: none - (1 << 11) */
#define TMD1_FLAGS_MORE 0x1000u    /* cs: none - (1 << 12) */
#define TMD1_FLAGS_ADD_FCS 0x2000u /* cs: none - (1 << 13) */
#define TMD1_FLAGS_ERR 0x4000u     /* cs: none - (1 << 14) */
#define TMD1_FLAGS_OWN 0x8000u     /* cs: none - (1 << 15) */

/* TMD3Flags */
#define TMD3_FLAGS_TDR 0x03FFu  /* cs: Emulated.HW/AMD/LANCE/Am7990/Am2990Lance.cs:424 */
#define TMD3_FLAGS_RTRY 0x0400u /* cs: none - (1 << 10) */
#define TMD3_FLAGS_LCAR 0x0800u /* cs: none - (1 << 11) */
#define TMD3_FLAGS_LCOL 0x1000u /* cs: none - (1 << 12) */
#define TMD3_FLAGS_UFLO 0x4000u /* cs: none - (1 << 14) */
#define TMD3_FLAGS_BUFF 0x8000u /* cs: none - (1 << 15) */

/* RMD1Flags */
#define RMD1_FLAGS_HADR 0x00FFu /* cs: Emulated.HW/AMD/LANCE/Am7990/Am2990Lance.cs:462 */
#define RMD1_FLAGS_ENP 0x0100u  /* cs: none - (1 << 8) */
#define RMD1_FLAGS_STP 0x0200u  /* cs: none - (1 << 9) */
#define RMD1_FLAGS_BUFF 0x0400u /* cs: none - (1 << 10) */
#define RMD1_FLAGS_CRC 0x0800u  /* cs: none - (1 << 11) */
#define RMD1_FLAGS_OFLO 0x1000u /* cs: none - (1 << 12) */
#define RMD1_FLAGS_FRAM 0x2000u /* cs: none - (1 << 13) */
#define RMD1_FLAGS_ERR 0x4000u  /* cs: none - (1 << 14) */
#define RMD1_FLAGS_OWN 0x8000u  /* cs: none - (1 << 15) */

/* RMD3Flags */
#define RMD3_FLAGS_MCNT 0x0FFFu /* cs: Emulated.HW/AMD/LANCE/Am7990/Am2990Lance.cs:513 */

#define LANCE_LOOPBACK_BUFFER_SIZE 1522 /* cs: Emulated.HW/AMD/LANCE/Am7990/Am2990Lance.cs:611 */
#define LANCE_PACKET_BUFFER_SIZE 4096   /* cs: Emulated.HW/AMD/LANCE/Am7990/Am2990Lance.cs:616 */
#define LANCE_RX_QUEUE_SIZE 16          /* cs: Emulated.HW/AMD/LANCE/Am7990/Am2990Lance.cs:626 */
#define LANCE_FCS_RESIDUE 0xDEBB20E3u   /* cs: Emulated.HW/AMD/LANCE/Am7990/Am2990Lance.cs:1925 */

typedef struct
{
    void *ctx;
    /** OnLANCEIRQ: the interrupt line changed (read IsInterruptActive). */
    void (*on_lance_irq)(void *ctx);
    /** OnPacketTransmit: a frame left the chip (FCS-less, length = wire length). */
    void (*on_packet_transmit)(void *ctx, const uint8_t *data, int length);
    /** OnPacketReceive: a frame was queued from the network (before filtering). */
    void (*on_packet_receive)(void *ctx, const uint8_t *data, int length);
    /** DmaIn: read a 16-bit word of local memory. NULL = no DMA. */
    uint16_t (*dma_in)(void *ctx, uint32_t address);
    /** DmaOut: write a 16-bit word of local memory. NULL = no DMA. */
    void (*dma_out)(void *ctx, uint32_t address, uint16_t data);
    /** ReadMemory / WriteMemory: declared in the C# ("legacy, PLANC"), never called. */
    uint8_t (*read_memory)(void *ctx, uint32_t address);
    void (*write_memory)(void *ctx, uint32_t address, uint8_t value);
    /** Receive-queue lock (C# lock(_rxQueueLock)); NULL = single-threaded. */
    void (*rx_lock)(void *ctx);
    void (*rx_unlock)(void *ctx);
} LanceEvents;

typedef struct
{
    uint32_t start_address;
    uint32_t end_address;
    bool swap_byte_lanes;
    uint16_t rap;
    uint16_t csr[4];
    uint8_t rdp_latch_lo;
    uint8_t rap_latch_lo;
    bool rdp_latch_valid;
    bool rap_latch_valid;
    uint32_t init_block_address;
    bool initialized;
    uint16_t mode;
    uint64_t logical_address_filter;
    uint8_t physical_address[6];
    uint32_t rx_ring_base;
    uint8_t rx_ring_mask;
    uint8_t rx_ring_pos;
    uint16_t rx_md[4];
    uint32_t tx_ring_base;
    uint8_t tx_ring_mask;
    uint8_t tx_ring_pos;
    uint16_t tx_md[4];
    uint8_t loopback_buffer[LANCE_LOOPBACK_BUFFER_SIZE];
    int loopback_length;
    bool in_loopback_receive;
    uint8_t packet_buffer[LANCE_PACKET_BUFFER_SIZE];
    bool receiver_enabled;
    bool transmitter_enabled;
    bool tx_poll_pending;
    uint8_t rx_queue[LANCE_RX_QUEUE_SIZE][LANCE_PACKET_BUFFER_SIZE];
    int rx_queue_lengths[LANCE_RX_QUEUE_SIZE];
    bool rx_queue_used[LANCE_RX_QUEUE_SIZE];
    int rx_queue_head;
    int rx_queue_tail;
    int rx_queue_count;
    uint8_t rx_scratch[LANCE_PACKET_BUFFER_SIZE]; /* frame taken off the queue (C#: local reference) */
    bool irq_asserted;
    int64_t rx_accepted;
    int64_t rx_filtered;
    int64_t rx_missed;
    int64_t rx_dropped_rx_off;
    LanceEvents ev;
} Am7990Lance;

/** @brief Constructor Am7990Lance(start, length, name); calls Reset. */
void lance_create(Am7990Lance *l, uint32_t start_address, uint32_t length, const LanceEvents *events);
/** @brief IOMemoryBase.IsMappedAddress. */
bool lance_is_mapped_address(const Am7990Lance *l, uint32_t address);
void lance_reset(Am7990Lance *l);
uint8_t lance_read(Am7990Lance *l, uint32_t address);
void lance_write(Am7990Lance *l, uint32_t address, uint8_t value);
/** @brief ReadDebug: register byte without side effects. */
uint8_t lance_read_debug(const Am7990Lance *l, uint32_t address);
/** @brief IsInterruptActive: INTR and INEA both set in CSR0. */
bool lance_is_interrupt_active(const Am7990Lance *l);
/** @brief GetPhysicalAddress: copy of the MAC from the init block. */
void lance_get_physical_address(const Am7990Lance *l, uint8_t out[6]);
void lance_transmit_poll(Am7990Lance *l);
/** @brief ExecuteStop: no caller in the C# (a STOP write goes through Reset). */
void lance_execute_stop(Am7990Lance *l);
/** @brief ReceivePacket. @return bytes stored, 0 not for us, -1 dropped, -2 missed (no buffer) */
int lance_receive_packet(Am7990Lance *l, const uint8_t *buffer, int length);
void lance_receive_complete(Am7990Lance *l, int result);
void lance_simulate_packet_received(Am7990Lance *l);
void lance_simulate_memory_error(Am7990Lance *l);
void lance_process_network_test(Am7990Lance *l, int test_mode);
/** @brief EnqueueReceivedPacket: called from the network thread. */
void lance_enqueue_received_packet(Am7990Lance *l, const uint8_t *data, int length);
/** @brief Clock: drain the receive queue, then poll TX when requested. */
void lance_clock(Am7990Lance *l);
void lance_clear_interrupt(Am7990Lance *l);
/** @brief Crc32.Calculate (IEEE 802.3 CRC-32, reflected, poly 0xEDB88320). */
uint32_t lance_crc32_calculate(const uint8_t *data, int offset, int length);

#endif /* ETH_LANCE_H */
