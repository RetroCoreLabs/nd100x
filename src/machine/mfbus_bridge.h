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
bool mfbus_attach(uint32_t size_bytes, uint32_t base_page);

/* Unregister the bank and free the pool. Safe when nothing is attached. */
void mfbus_detach(void);

/* The pool, so an ND-5000 can be given the same bytes. NULL when detached. */
struct NdbusPool *mfbus_pool(void);

bool mfbus_is_attached(void);

/*
 * Put an ND-5000 station on the octobus at `station_number` (070B..076B) with
 * the shared pool behind it. Returns false for an illegal station number, a
 * duplicate, more than MC_ND5000_MAX_CPUS, or when no pool is attached - an
 * ND-5000 with no shared memory has nowhere to execute.
 */
bool mfbus_add_nd5000(uint8_t station_number);

/* How many ND-5000 stations are on the bus. */
int mfbus_nd5000_count(void);

/* Drop every station. Called by mfbus_detach(); separate so a reconfiguration
 * can rebuild the bus without tearing down the pool. */
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
bool mfbus_attach_cpu(uint8_t station_number);

/* Start / stop the host thread of the CPU at `station_number`. Stopping WAITS
 * for the thread, because everything it touches must outlive it. */
bool mfbus_start_nd5000(uint8_t station_number);
void mfbus_stop_nd5000(uint8_t station_number);

/* Instructions executed by the CPU at `station_number`, 0 if it has none. */
unsigned long long mfbus_nd5000_instructions(uint8_t station_number);

#endif /* ND100X_WITH_ND500 */

#endif /* MFBUS_BRIDGE_H */
