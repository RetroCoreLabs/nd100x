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
#include <stddef.h>   /* size_t - mfbus_context_field_count */
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
/* mfbus_apply_config() is declared in mfbus_config.h, NOT here.
 *
 * WHY THE SPLIT: both repositories have a machine_types.h and both guard it with
 * #ifndef MACHINE_TYPES_H. Including nd100x's machine_config.h pulls in nd100x's
 * machine_types.h and defines that guard, after which nd500x's machine_types.h is
 * silently skipped and Nd500Machine is never declared - the build then fails
 * inside nd500x's own cpu_protos.h, which reads like a missing library rather
 * than a guard collision. So this file sees nd500x's headers and mfbus_config.c
 * sees nd100x's, and the two never meet in one translation unit. */


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
 * @brief The octobus fabric, so a test can put a frame on the bus as any station.
 * @return The fabric, or NULL when nothing is attached.
 */
struct NdbusFabric *mfbus_fabric(void);

/**
 * @brief Frames other stations have sent to the ND-100 (station 1B) on their own
 *        initiative - not replies to a frame the ND-100 sent.
 *
 * A frame is QUEUED when the bus delivers it and DELIVERED when the octobus
 * card's tick moves it into the card's receive FIFO. DROPPED is a lost frame:
 * the queue or the card was full. With an ND-5000 running, `queued` grows by
 * one for every answer it writes; a `delivered` that stays behind means the
 * card is not being ticked.
 *
 * @param queued    Out: frames accepted from the bus. May be NULL.
 * @param delivered Out: frames moved into the card. May be NULL.
 * @param dropped   Out: frames lost. May be NULL.
 */
void mfbus_nd100_inbound_counts(unsigned long *queued, unsigned long *delivered,
                                unsigned long *dropped);

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
 * SETTING PC IS A CONVENIENCE FOR A BARE HARNESS, not how the machine starts.
 * The hardware start path is mfbus_place_context() followed by
 * mfbus_load_context(): the ND-100 writes a register image into shared memory
 * and NEWCNTXT loads the machine from it. Use those two when the question is
 * whether the real bring-up works.
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
 * @brief Place a SAMSON context block for this station's CPU.
 *
 * The real start path. The ND-100 does not poke a program counter: it writes a
 * register image into shared memory and the microcode's NEWCNTXT loads the
 * machine from it when the microprogram starts.
 *
 * @param station_number The station whose CPU to place a context for.
 * @param area_byte      Pool BYTE offset of the context block AREA - what the
 *                       control-store cell OFFSET (0o20) is patched with. The
 *                       CPU's own block is one stride past it.
 * @param entry_p        P, the entry point.
 * @param local_base     B, the local data base.
 * @return true on success; false when the station has no CPU, or the block does
 *         not fit the pool at that area base.
 */
bool mfbus_place_context(uint8_t station_number, uint32_t area_byte, uint32_t entry_p,
                         uint32_t local_base);

/**
 * @brief Load this station's CPU from its context block, as NEWCNTXT does.
 *
 * Loads ONLY the fields NEWCNTXT loads. The DOMAIN registers in the block - TOS,
 * LL, HL, THA, CES, CAS and the trap enables - are sourced from the Domain
 * Information Table on real hardware, so they are deliberately NOT copied here
 * however they were filled in. Copying them would make the emulator honour a
 * context the hardware ignores, and a bring-up that works here and not on the
 * machine is worse than one that fails in both.
 *
 * @param station_number The station whose CPU to load.
 * @return true on success; false when the station has no CPU, no context has
 *         been placed, or the CPU is running - loading registers underneath a
 *         thread that is executing produces a machine halfway between two
 *         contexts.
 */
bool mfbus_load_context(uint8_t station_number);

/**
 * @brief How many registers the context block carries across a switch.
 *
 * The bridge keeps ONE table of those registers and both CNTXTSAVE and NEWCNTXT
 * iterate it, so a register cannot be restored by the load and dropped by the
 * save. These two accessors exist so the unit test can assert that property over
 * every row rather than over a list it repeats for itself - a test that restates
 * the list cannot catch a register missing from both.
 *
 * @return The number of rows, always greater than zero.
 */
size_t mfbus_context_field_count(void);

/**
 * @brief Describe one row of the context block's register table.
 *
 * @param index      Row, 0 .. mfbus_context_field_count() - 1.
 * @param out_offset Byte offset of the slot inside the 256-byte block; may be NULL.
 * @param out_name   Static register name for a failure message; may be NULL.
 * @param out_mask   The width the microcode moves (PS is 13 bits, CED and CAD are
 *                   bytes), so a test can mask its marker the same way; may be NULL.
 * @param out_saved  False for a row the load reads and the save must NOT write
 *                   back, because the microcode sources that slot itself. A test
 *                   must not expect such a row to survive a round trip; may be NULL.
 * @return true if index named a row, false if it is out of range.
 */
bool mfbus_context_field_info(size_t index, uint32_t *out_offset, const char **out_name,
                              uint32_t *out_mask, bool *out_saved);

/**
 * @brief Write one of the context table's registers in a station's CPU, by ROW INDEX.
 *
 * By index and not by name so a caller never restates the register list. A test
 * that keeps its own copy of that list cannot catch a register missing from BOTH
 * the save and the load, which is the defect that actually occurred: TOS and LL
 * were read by the load, written by nobody, and no test mentioned either.
 *
 * The value is masked to the width the microcode moves, so a caller poking a
 * full 32-bit marker into PS (13 bits) or CED/CAD (bytes) gets back what will
 * really survive instead of reading the difference as a fault.
 *
 * @param station_number Octobus station, 070B..076B.
 * @param index          Row, 0 .. mfbus_context_field_count() - 1.
 * @param value          Value to store; masked per row.
 * @return true on success, false for an unknown station or an out-of-range row.
 */
bool mfbus_context_register_set(uint8_t station_number, size_t index, uint32_t value);

/**
 * @brief Read one of the context table's registers from a station's CPU, by ROW INDEX.
 *
 * @param station_number Octobus station, 070B..076B.
 * @param index          Row, 0 .. mfbus_context_field_count() - 1.
 * @param out_value      Receives the register, masked per row.
 * @return true on success, false for an unknown station, an out-of-range row or
 *         a NULL out_value.
 */
bool mfbus_context_register_get(uint8_t station_number, size_t index, uint32_t *out_value);

/**
 * @brief CNTXTSAVE on demand - write the live registers into the context block.
 *
 * The public mirror of mfbus_load_context(), which has always been public. The
 * pair is what makes the save/load round trip testable at all; with only the
 * load exposed, a test can assert what the load does with a block somebody else
 * wrote, but never that the save puts back what the load will read.
 *
 * @param station_number Octobus station, 070B..076B.
 * @return true if the block was written, false for an unknown station or when no
 *         context block has been placed for it.
 */
bool mfbus_store_context(uint8_t station_number);

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

/**
 * @brief Poll every configured ND-5000's mailbox doorbell once.
 *
 * SOMETHING HAS TO DO THIS OR THE MACHINE TIMES OUT. After ENKICK the ND-500/5000
 * monitor stops talking on the octobus and waits on the mailbox in MPM-5 shared
 * memory. SINTRAN's ACT51 rings the doorbell by writing X5ACT := 0 and sends NO
 * kick, so the only way an ND-5000 learns of work is by polling - and with nothing
 * polling, SINTRAN waits out its watchdog and the monitor prints
 * "ND-500(0) timeout".
 *
 * Called from the octobus card's tick, which is where the ND-100 clock reaches
 * this code. A poll with nothing pending is two shared-memory reads and a
 * comparison.
 *
 * @return The number of stations that answered at least one message on this poll.
 *         0 is the ordinary idle result, not an error.
 */
int mfbus_service_nd5000_mailboxes(void);

/**
 * @brief Nonzero when mfbus_service_nd5000_mailboxes() has work to do.
 * @details Read it with __atomic_load_n and call the service function only when
 *          it is nonzero: the tick runs once per ND-100 instruction and the idle
 *          answer is "nothing". Zero means no frame waits for the ND-100, no
 *          station was changed by a frame since the last scan, no station has a
 *          started monitor and no CPU needs the main loop to run it.
 */
extern unsigned mfbus_tick_work;

/**
 * @brief Which ND-5000 stations have a started monitor (a mailbox to poll).
 * @details Bit n is the station added n-th. Updated after a frame has gone onto
 *          the bus, so it can lag by one ND-100 instruction. 0 until the first
 *          station's monitor is started with ENKICK - until then the tick does
 *          no mailbox work at all.
 * @return The bit mask, 0 when no station has a mailbox.
 */
unsigned mfbus_nd5000_armed_mask(void);

/**
 * @brief Which ND-5000 CPUs are running right now.
 * @details Bit n is the station added n-th. Read from each runner's own state, so
 *          it is correct for both a host thread (native) and a CPU run from the
 *          main loop (WebAssembly). Not for the hot path.
 * @return The bit mask, 0 when every CPU is stopped or none exists.
 */
unsigned mfbus_nd5000_running_mask(void);


#endif /* ND100X_WITH_ND500 */

#endif /* MFBUS_BRIDGE_H */
