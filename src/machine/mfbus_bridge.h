/*
 * mfbus_bridge.h - the shared MFbus pool as seen by the ND-100
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * See mfbus_bridge.c. Declared unconditionally so a caller can ask whether an
 * MFbus is attached without itself being compiled twice; the implementation is
 * empty when the ND-500 is not built.
 */

#ifndef MFBUS_BRIDGE_H
#define MFBUS_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef ND100X_WITH_ND500

struct NdbusPool;
struct Device;

/*
 * Allocate the shared pool and register it as an ND_MEM_MPM5 bank at
 * `base_page` - the parameter of the ND-500 monitor's
 * DEFINE-MEMORY-CONFIGURATION, and an ND-100 page is 1024 WORDS.
 *
 * Returns false, having allocated nothing, when the pool cannot be allocated or
 * when the bank would overlap memory that is already registered. Overlap is the
 * likely one: it means the configured base page falls inside installed ND-100
 * memory.
 */
/**
 * @brief Allocate the shared MFbus pool and register it as an ND-100 MPM-5 bank.
 *
 * An ND-100 page is 1024 WORDS, so the window's first word address is
 * base_page * 1024. Page 004100B (2112) is word 0x210000, which is byte
 * 0x420000 - the value captured from a live machine.
 *
 * @param size_bytes Pool size in bytes. Every ND-5000 runs out of this one pool.
 * @param base_page  ND-100 page at which ND-500 physical address 0 appears -
 *                   the parameter of the ND-500 monitor's
 *                   DEFINE-MEMORY-CONFIGURATION.
 * @return true on success; false having allocated NOTHING when the pool cannot
 *         be allocated, when an MFbus is already attached, or when the bank
 *         would OVERLAP memory that is already registered. Overlap is the
 *         likely failure: it means the configured base page falls inside
 *         installed ND-100 memory.
 */
bool mfbus_attach(uint32_t size_bytes, uint32_t base_page);

/**
 * @brief Stop every CPU, drop every station, unregister the bank and free the pool.
 *
 * The order matters and is not an implementation detail: a running CPU thread
 * holds pointers to its machine, its CPU and the pool, so the threads are
 * stopped and JOINED before anything they touch is freed.
 */
void mfbus_detach(void);

/**
 * @brief The shared pool, so an ND-5000 can be given the same bytes.
 * @return The pool, or NULL when nothing is attached.
 */
struct NdbusPool *mfbus_pool(void);

/**
 * @brief Whether a shared MFbus pool is currently attached.
 * @return true when a pool exists and is registered as an MPM-5 bank.
 */
bool mfbus_is_attached(void);

/*
 * Put an ND-5000 station on the octobus at `station_number` (070B..076B) with
 * the shared pool behind it. Returns false for an illegal station number, a
 * duplicate, more than MC_ND5000_MAX_CPUS, or when no pool is attached - an
 * ND-5000 with no shared memory has nowhere to execute.
 */
/**
 * @brief Put an ND-5000 station on the octobus with the shared pool behind it.
 *
 * @param station_number Octobus station, 070B to 076B (56 to 62 decimal).
 *                       Seven slots, ND-05.020.01 T329.
 * @return true on success; false for a station number outside that range -
 *         where other kinds of device live - for a duplicate, for more than
 *         seven CPUs, or when no pool is attached. An ND-5000 with no shared
 *         memory has nowhere to execute: it has no private RAM at all.
 */
bool mfbus_add_nd5000(uint8_t station_number);

/**
 * @brief How many ND-5000 stations are on the bus.
 * @return The station count, 0 to 7.
 */
int mfbus_nd5000_count(void);

/* Drop every station. Called by mfbus_detach(); separate so a reconfiguration
 * can rebuild the bus without tearing down the pool. */
/**
 * @brief Stop, join and remove every ND-5000 station, leaving the pool attached.
 *
 * Separate from mfbus_detach() so a reconfiguration can rebuild the bus without
 * tearing down shared memory. Each CPU's host thread is stopped and joined
 * before its machine is freed.
 */
void mfbus_clear_nd5000(void);

/*
 * Give the ND-5000 at `station_number` a real ND-500 CPU running out of the
 * shared pool, on its own host thread.
 *
 * The CPU is NOT started: it is created, reset and left stopped, because the
 * ND-120 starts a microprogram with an ACCP STARTMIC over the octobus, not by
 * the emulator deciding to. Use mfbus_start_nd5000() when that arrives.
 *
 * False when there is no station at that number, when it already has a CPU, or
 * when no pool is attached.
 */
/**
 * @brief Connect an ND-100 octobus card to the MFbus fabric.
 *
 * After this, a frame the guest writes to the card's output command register
 * goes onto the bus, and any reply arrives in the card's receive FIFO - which is
 * where the hardware puts it. Station discovery, the ACCP bring-up sequence and
 * every mailbox kick travel this path.
 *
 * @param card The device returned by octobus_create_device().
 * @return true on success; false for a NULL card or when no pool is attached,
 *         since without one there is no bus for the card to reach.
 */
bool mfbus_attach_card(struct Device *card);

/**
 * @brief Give an ND-5000 station a real ND-500 CPU running out of the shared pool.
 *
 * The machine's memory IS the pool - not a copy and not a window - so ND-500
 * physical address 0 is pool offset 0, and an instruction the CPU fetches is a
 * byte the ND-100 can write through its MPM-5 bank.
 *
 * The CPU is created, reset and left STOPPED. The ND-120 starts a microprogram
 * with an ACCP STARTMIC over the octobus; the emulator does not decide to.
 *
 * @param station_number The station to give a CPU to.
 * @return true on success; false when there is no station at that number, when
 *         it already has a CPU, or when no pool is attached.
 */
bool mfbus_attach_cpu(uint8_t station_number);

/**
 * @brief Load a program image into the shared pool for the CPU at this station.
 *
 * AN EXPLICIT POOL OFFSET, not "the start of memory". The pool is SHARED: its
 * low bytes carry the mailbox global header, whose word 0 is the X5SEM
 * semaphore. A loader that writes from offset 0 - which is what the plain a.out
 * path does - would overwrite the semaphore with program text, and the symptom
 * is a mailbox that never unlocks rather than anything that points at the load.
 * So the caller says where, and the range is bounds-checked.
 *
 * The CPU's PC is set to @p pool_offset, because that is where the image now
 * begins. The CPU is NOT started; the ND-120 does that with an ACCP STARTMIC.
 *
 * @param station_number The station whose CPU to load for.
 * @param path           Host path of the image.
 * @param pool_offset    Pool BYTE offset to load at, which is also the ND-500
 *                       physical address, since the pool IS the CPU's memory.
 * @return true on success; false when the station has no CPU, the file cannot
 *         be read, or the image does not fit the pool at that offset.
 */
bool mfbus_load_nd5000(uint8_t station_number, const char *path, uint32_t pool_offset);

/**
 * @brief Start the host thread of the CPU at this station.
 * @param station_number The station whose CPU to run.
 * @return true when the thread started; false when the station has no CPU, the
 *         thread is already running, or the build has no host threads.
 */
bool mfbus_start_nd5000(uint8_t station_number);

/**
 * @brief Stop the host thread of the CPU at this station and WAIT for it.
 *
 * Asks and joins. Requesting a stop is not enough: the thread may be mid
 * instruction when the request arrives, and the pool it is reading must outlive
 * it. Safe on a station with no CPU and on one that is not running.
 *
 * @param station_number The station whose CPU to stop.
 */
void mfbus_stop_nd5000(uint8_t station_number);

/**
 * @brief How many instructions the CPU at this station has executed.
 * @param station_number The station to ask about.
 * @return The instruction count, or 0 when the station has no CPU.
 */
unsigned long long mfbus_nd5000_instructions(uint8_t station_number);

#endif /* ND100X_WITH_ND500 */

#endif /* MFBUS_BRIDGE_H */
