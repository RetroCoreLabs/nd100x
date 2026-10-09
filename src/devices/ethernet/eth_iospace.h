/*
 * eth_iospace.h - Ethernet II card I/O space at 0xEF0000 (port of RetroCore ETH_IOMem).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Port of class ETH_IOMem in RetroCore NDBusEthernetII.cs (commit 935163f):
 * AM9513-like timer/STC block at 0xEF0100-0xEF01A1, SCIP doorbell and SCIP
 * channel registers, AM9519 reset/EOI detection, ETHSTAT, LANRESET, XCVPW,
 * EAREN, MERRSTAT, PROFF, MODCR. The C# events are callbacks here.
 */

#ifndef ETH_IOSPACE_H
#define ETH_IOSPACE_H

#include <stdbool.h>
#include <stdint.h>

#include "eth_mfp.h"

#define ETHIO_STC_PERIOD_TICKS 2000       /* cs: Emulated.HW/ND/CPU/NDBUS/NDBusEthernetII.cs:3926 */
#define ETHIO_TIMER_PRESCALER_DIVISOR 256 /* cs: Emulated.HW/ND/CPU/NDBUS/NDBusEthernetII.cs:3930 */
#define ETHIO_SCIP_SIDE_COUNT 8           /* cs: Emulated.HW/ND/CPU/NDBUS/NDBusEthernetII.cs:4108 */

typedef struct
{
    void *ctx;
    void (*on_nd_interrupt)(void *ctx);           /* OnNDInterrupt (SCIP -> ND-100 level 12) */
    void (*on_scip_doorbell)(void *ctx, uint8_t v); /* OnScipDoorbell (mailbox tracer hook) */
    bool (*get_lance_interrupt_state)(void *ctx); /* GetLANCEInterruptState */
    uint16_t (*get_earen_value)(void *ctx);       /* GetEARENValue */
    uint16_t (*get_merrstat_value)(void *ctx);    /* GetMERRSTATValue */
    void (*on_am9519_reset)(void *ctx);           /* OnAM9519Reset */
    void (*on_lan_reset)(void *ctx);              /* OnLANReset */
    bool (*was_mirrored_access)(void *ctx);       /* WasMirroredAccess (set, not read, by the C#) */
    void (*on_timer_interrupt)(void *ctx);        /* OnTimerInterrupt (level 5) */
    void (*on_scip_channel_tx)(void *ctx, int side); /* OnSCIPChannelTx */
} EthIoEvents;

typedef struct
{
    uint32_t start_address;
    uint32_t end_address;
    MC68901MFP *mfp_chip; /* MfpChip */
    bool transceiver_power_enabled;
    uint16_t timer_data[15];
    int timer_data_index;
    uint16_t timer_counter1;
    uint16_t timer_load_reg1;
    bool timer_armed1;
    bool timer_interrupt_pending;
    uint8_t timer_status;
    uint16_t pending_timer_data_word;
    bool timer_data_high_byte_written;
    uint16_t last_timer_command;
    uint16_t timer_mode_reg1;
    uint32_t timer_tick32;
    bool stc_armed;
    int stc_countdown;
    uint16_t timer_config_reg;
    int timer_prescaler;
    int reset_sequence_state;
    uint8_t scip_status[ETHIO_SCIP_SIDE_COUNT];
    uint8_t scip_rx_data[ETHIO_SCIP_SIDE_COUNT];
    bool scip_rx_ready[ETHIO_SCIP_SIDE_COUNT];
    int scip_cmd_count[ETHIO_SCIP_SIDE_COUNT];
    EthIoEvents ev;
} ETH_IOMem;

/** @brief Constructor ETH_IOMem(start, length, name). */
void ethio_create(ETH_IOMem *io, uint32_t start_address, uint32_t length, const EthIoEvents *events);
/** @brief IOMemoryBase.IsMappedAddress. */
bool ethio_is_mapped_address(const ETH_IOMem *io, uint32_t address);
/** @brief Clock: one ND-100 tick of the timer/STC block. */
void ethio_clock(ETH_IOMem *io);
uint8_t ethio_read(ETH_IOMem *io, uint32_t address);
void ethio_write(ETH_IOMem *io, uint32_t address, uint8_t value);
void ethio_reset(ETH_IOMem *io);
/** @brief handleIoRw: the whole decode (address is the unmirrored 68000 address). */
uint8_t ethio_handle_io_rw(ETH_IOMem *io, uint32_t address, uint8_t value, bool is_write);
bool ethio_is_timer_interrupt_pending(const ETH_IOMem *io);
void ethio_clear_timer_interrupt(ETH_IOMem *io);

#endif /* ETH_IOSPACE_H */
