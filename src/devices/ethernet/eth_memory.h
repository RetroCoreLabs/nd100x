/*
 * eth_memory.h - The Ethernet II card's 68000 memory map (port of RetroCore NDEthernetMemory).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Port of class NDEthernetMemory in RetroCore NDBusEthernetII.cs (commit
 * 935163f). 68000 address decode, in the C# order (FindMemoryBank):
 *   0xF80000-0xFFFFFF  card DRAM (window used by the ND-100 side in the C#)
 *   0xEF00C0-0xEF00FF  MFP 68901
 *   0xEF00A0-0xEF00A7  LANCE Am7990 (byte lanes swapped)
 *   0xF00000-0xF7FFFE  protect table (reads 0, writes ignored)
 *   0x000000-0x07FFFF  card DRAM (512 KB)
 *   0xEF0000-0xEF00FF  I/O space (eth_iospace), gets the UNMIRRORED address
 *   anything else      bus error
 * Addresses 0xEF0100-0xEF01FF are first mirrored to 0xEF00xx (bit 8 cleared)
 * for the MFP/LANCE/IO decision.
 *
 * Not ported (logging only, approval pending): InstrumentedRAM mailbox
 * logging, EthMailboxTracer, LogBlockAccess, the PRKEY log.
 */

#ifndef ETH_MEMORY_H
#define ETH_MEMORY_H

#include <stdbool.h>
#include <stdint.h>

#include "eth_iospace.h"
#include "eth_lance.h"
#include "eth_mfp.h"

#define ETHMEM_DRAM_SIZE 0x80000u /* cs: Emulated.HW/ND/CPU/NDBUS/NDBusEthernetII.cs:573 */

typedef struct
{
    void *ctx;
    void (*on_nd_interrupt)(void *ctx);                       /* OnNDInterrupt */
    void (*on_trigger_interrupt)(void *ctx, int level, bool state); /* OnTriggerInterrupt */
    void (*on_bus_error)(void *ctx, uint32_t address, bool is_read);  /* OnBusError (does not return) */
    void (*on_packet_transmit)(void *ctx, const uint8_t *data, int length);
    void (*on_packet_receive)(void *ctx, const uint8_t *data, int length);
    void (*rx_lock)(void *ctx);
    void (*rx_unlock)(void *ctx);
    /* differential trace hooks (docs/ethernet-port/TRACE-FORMAT.md); NULL = off */
    void (*trace_m68k)(void *ctx, const char *event, const char *fields);
    int (*trace_fc)(void *ctx);
} EthMemEvents;

typedef struct
{
    uint8_t dram[ETHMEM_DRAM_SIZE];
    uint32_t ram_start; /* _ramStart: the DRAM's ND-100 byte address (window) */
    MC68901MFP mfp;
    Am7990Lance lance;
    ETH_IOMem eth_io_mem;
    uint32_t last_error_address;
    uint16_t last_error_status;
    bool memory_unavailable;
    bool last_write_was_mirrored;
    bool am9519_reset_pending;
    int isrb_write_count;
    bool software_interrupt_test_active;
    int current_scip_channel;
    EthMemEvents ev;
} NDEthernetMemory;

/** @brief Constructor NDEthernetMemory(shared_ram, ramStart): builds the chips. */
void ethmem_create(NDEthernetMemory *m, uint32_t ram_start, const EthMemEvents *events);
/** @brief ReadMemory: 68000 byte read (may raise a bus error, which does not return). */
uint8_t ethmem_read_memory(NDEthernetMemory *m, uint32_t address);
/** @brief WriteMemory: 68000 byte write (may raise a bus error, which does not return). */
void ethmem_write_memory(NDEthernetMemory *m, uint32_t address, uint8_t value);
/** @brief Clock: MFP, LANCE, I/O timer - one ND-100 tick. */
void ethmem_clock(NDEthernetMemory *m);
/** @brief LoadRom / LoadRAM: copy bytes into DRAM at offset. @return false if it does not fit */
bool ethmem_load_ram(NDEthernetMemory *m, uint32_t offset, const uint8_t *bytes, uint32_t length);
/** @brief Reset: RAM reset (no-op in the C#), ResetPeripherals, InitializeMFPFromFirmware. */
void ethmem_reset(NDEthernetMemory *m);
void ethmem_reset_peripherals(NDEthernetMemory *m);
/** @brief OnCpuResetInstruction: no caller in the C# (RESET instruction is not wired). */
void ethmem_on_cpu_reset_instruction(NDEthernetMemory *m);
/** @brief InitializeMFPFromFirmware: empty in the C# beyond a null check. */
void ethmem_initialize_mfp_from_firmware(NDEthernetMemory *m);
void ethmem_set_mfp_interrupt(NDEthernetMemory *m);
void ethmem_clear_mfp_interrupt(NDEthernetMemory *m);
bool ethmem_is_timer_interrupt_pending(const NDEthernetMemory *m);
void ethmem_clear_timer_interrupt(NDEthernetMemory *m);
void ethmem_clear_error_registers(NDEthernetMemory *m);
/** @brief InterruptControllerSetInterrupt: forward a 68000 level request. */
void ethmem_interrupt_controller_set_interrupt(NDEthernetMemory *m, int level, bool state);
/** @brief ReadWord: big-endian word at a DRAM offset, through ReadMemory. */
uint16_t ethmem_read_word(NDEthernetMemory *m, uint32_t mem_address);

#endif /* ETH_MEMORY_H */
