/*
 * device_ethernet.h - ND Ethernet II controller, PCB 3094 (port of RetroCore NDBusEthernetII).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Port of class NDBusEthernetII in RetroCore NDBusEthernetII.cs (commit
 * 935163f); manual ND-12.055.1. The card has a 68000 (run by Musashi through
 * eth_m68k), 512 KB DRAM shared with the ND-100, an MC68901 MFP and an
 * Am7990 LANCE.
 *
 * ND-100 side (thumbwheel 0-3):
 *   IOX 140360/140364/140370/140374 (4 registers), IDENT 140034-140037,
 *   interrupt level 12.
 *   base+0, base+2 read  STATUS: bits 15-8 bank, 5 halt, 4 reset active,
 *                               2 INT12 set, 0 interrupt enabled
 *   base+1, base+3 write CONTROL: 0 enable SCIP interrupt, 2 ND interrupt
 *                               strobe (MFP I6), 3 start OPCOM (68000 level 6),
 *                               4 reset, 5 halt, 6 power-low enable,
 *                               8 disable check bit
 *   DRAM window: 512 KB at ND-100 byte address bank * 0x40 * 2048,
 *   bank = 16 + 4 * thumbwheel (0x200000 for thumbwheel 0).
 */

#ifndef DEVICE_ETHERNET_H
#define DEVICE_ETHERNET_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "eth_m68k.h"
#include "eth_memory.h"

struct Device;

#define ETH_INTERRUPT_LEVEL 12 /* cs: Emulated.HW/ND/CPU/NDBUS/NDBusEthernetII.cs:509 */
#define ETH_CPU68K_CYCLES_PER_ND100_TICK 1 /* cs: Emulated.HW/ND/CPU/NDBUS/NDBusEthernetII.cs:1044 */
#define ETH_PAGES_PER_CARD 0x100u      /* cs: Emulated.HW/ND/CPU/NDBUS/NDBusEthernetII.cs:562 */
#define ETH_BANK_STEP_PER_CARD 4u      /* cs: Emulated.HW/ND/CPU/NDBUS/NDBusEthernetII.cs:563 */
#define ETH_BASE_BANK 16u              /* cs: Emulated.HW/ND/CPU/NDBUS/NDBusEthernetII.cs:564 */

/* Register (IOX offset) */
#define ETH_REGISTER_READ_DATA_REGISTER 0   /* cs: none - C# enum Register, implicit values */
#define ETH_REGISTER_WRITE_DATA_BUFFER 1    /* cs: none - C# enum Register, implicit values */
#define ETH_REGISTER_READ_STATUS_REGISTER 2 /* cs: none - C# enum Register, implicit values */
#define ETH_REGISTER_WRITE_CONTROL_WORD 3   /* cs: none - C# enum Register, implicit values */

/** Differential trace writer (docs/ethernet-port/TRACE-FORMAT.md). */
typedef struct
{
    FILE *out;
    bool bus_bytes; /* false: leave out the per-byte R8/W8 lines (long runs) */
    long seq_nd;
    long seq_m68k;
    long seq_net;
} EthTrace;

/** The card's state (C#: the NDBusEthernetII fields). */
typedef struct
{
    struct Device *dev;
    uint8_t thumbwheel;
    uint16_t memory_bank;
    uint32_t physical_page_start; /* byte address of the DRAM window */
    NDEthernetMemory mem;
    EthM68k cpu;
    bool interrupt_enabled;
    bool scip_pending;
    bool nd_interrupt;
    bool previous_nd_interrupt;
    bool start_opcom;
    bool previous_start_opcom;
    bool reset;
    bool previous_reset;
    bool halt;
    bool power_low;
    bool previous_power_low;
    bool disable_check_bit;
    bool first_bus_error_occurred;
    bool bank_registered;
    uint64_t nd_window_reads;  /* ND-100 word reads through the DRAM window */
    uint64_t nd_window_writes; /* ND-100 word/byte writes through the DRAM window */
    int64_t tx_packets;
    int64_t tx_bytes;
    int64_t rx_packets;
    int64_t rx_bytes;
    int64_t runt_frames_padded;
    EthTrace trace;
    bool trace_on;
    long trace_tick;
    bool trace_int_last;
} EthCard;

/**
 * @brief Create the card (C# constructor NDBusEthernetII(thumbwheel, 1, 0)).
 * @param thumbwheel 0-3
 * @return the device, or NULL for a bad thumbwheel or no memory
 */
struct Device *eth_create_device(uint8_t thumbwheel);

/**
 * @brief Create the card with a memory bank strap (C#: memoryBankStrap; 7J/9J).
 * @param thumbwheel 0-3 (12J: IOX/IDENT)
 * @param memory_bank_strap bank number, a multiple of 4; 0 = 16 + 4 * thumbwheel
 * @return the device, or NULL for a bad argument, no memory, or an unmappable window
 */
struct Device *eth_create_device_strap(uint8_t thumbwheel, uint16_t memory_bank_strap);

/** @brief The card state behind a device created by eth_create_device. */
EthCard *eth_card(struct Device *dev);

/** @brief Map the card DRAM into ND-100 physical memory (backed bank). @return 0 or -1 */
int eth_register_dram_window(EthCard *card);

/** @brief Attach or detach (out = NULL) the differential trace.
 *  @param bus_bytes false = no per-byte R8/W8 lines (as RetroCore EthIITrace.BusBytes) */
void eth_set_trace(EthCard *card, FILE *out, bool bus_bytes);

/**
 * @brief Move the DRAM window to another bank (7J/9J strap) after creation.
 * @param card card
 * @param memory_bank multiple of 4; 0 = 16 + 4 * thumbwheel
 * @return 0, or -1 if the bank is invalid or cannot be mapped (window unchanged)
 */
int eth_set_memory_bank(EthCard *card, uint16_t memory_bank);

/** @brief TriggerMFPVector (public test helper in the C#). */
void eth_trigger_mfp_vector(EthCard *card, uint8_t vector);

/** @brief Is68KRunning: !halt && !reset. */
bool eth_is_68k_running(const EthCard *card);

#endif /* DEVICE_ETHERNET_H */
