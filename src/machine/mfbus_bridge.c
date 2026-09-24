/*
 * mfbus_bridge.c - joins the shared MFbus pool to the ND-100's memory bank table
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2026 Ronny Hansen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the nd100em
 * distribution in the file COPYING); if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * THIS IS THE JOIN. One array of bytes, reached from two machines.
 *
 * The ND-5000 has no private RAM: it executes out of the MFbus pool. The ND-100
 * sees the SAME bytes through an MPM-5 window placed at a configured page, and
 * SINTRAN identifies that window as MPM5 rather than LOCAL because it carries no
 * ECC - see mms_get_physical_memory_type() and the bank table in cpu_mms.c.
 *
 * Everything that could be got wrong in the conversion between the two views -
 * word versus byte, which byte is the high half, whose offset an offset is - is
 * in ndbus_window.c on the nd500x side, deliberately in ONE place. This file is
 * only the adapter: it turns the ND-100's bank callbacks into window calls.
 *
 * The file compiles to NOTHING when the ND-500 is not built. It is picked up by
 * the machine library's file(GLOB), so the guard is here rather than in CMake;
 * a machine with no ND-500 must not need nd500x's headers to build.
 */

#ifdef ND100X_WITH_ND500

#include <stdio.h>
#include <string.h>

#include "mfbus_bridge.h"

#include "ndbus_nd5000.h"
#include "ndbus_runner.h"
#include "ndbus_octobus.h"
#include "ndbus_pool.h"
#include "ndbus_window.h"

#include "../cpu/cpu_types.h"
#include "../ndlib/ndlib_types.h"
#include "../ndlib/ndlib_protos.h"

/*
 * The ND-500 itself. This is the ONE place in nd100x that knows what an
 * Nd500Machine is; everything else reaches the ND-5000 through ndbus.
 *
 * SUBDIRECTORY-QUALIFIED ON PURPOSE. nd100x has its own cpu_protos.h and
 * machine_types.h with the SAME BASENAMES, so a bare #include "cpu_protos.h"
 * here picks up the ND-100's and fails with "unknown type name 'Nd500Machine'"
 * - which reads like a missing library rather than the wrong header. The
 * nd500::headers target puts nd500x's src/ on the path, and the cpu/ and
 * machine/ prefixes are what make these unambiguous.
 */
#include "cpu/cpu_protos.h"
#include "machine/machine_protos.h"

/*
 * One pool and one ND-100 window for the machine.
 *
 * Static because there is one MFbus in a machine, the same way there is one
 * backplane: the pool is shared by every ND-5000, which is the architecture
 * (ND-05.020.01 T40), not a simplification. A second MACHINE in one process
 * would need these per machine, and that is phase 5's problem, not this file's.
 */
static NdbusPool   s_pool;
static NdbusWindow s_window;
static bool        s_attached = false;
static uint32_t    s_base_word = 0;

/*
 * The octobus and the ND-5000 stations on it.
 *
 * The fabric is created whether or not the ND-100 has an octobus CARD: the card
 * is the ND-100's way onto the bus, and the bus exists independently of it. A
 * machine can be configured with ND-5000s and no card - it simply cannot talk to
 * them, which is a configuration to diagnose rather than a state to prevent
 * here.
 *
 * MFBUS_MAX_ND5000 is seven, the hardware's station count (070B..076B,
 * ND-05.020.01 T329).
 */
#define MFBUS_MAX_ND5000 7
static NdbusFabric s_fabric;
static NdbusNd5000 s_nd5000[MFBUS_MAX_ND5000];
static int         s_nd5000_count = 0;
static bool        s_fabric_ready = false;

/*
 * The ND-500 CPU behind each station, and the host thread that runs it.
 *
 * Parallel arrays indexed the same as s_nd5000[]: a station and its CPU are
 * created and destroyed together, and keeping them in one struct would put an
 * Nd500Machine inside a type that ndbus also sees - which is exactly the
 * coupling the vtable rule exists to prevent.
 */
typedef struct
{
    Nd500Machine machine;
    Nd500Cpu     cpu;
    NdbusCpuOps  ops;
    NdbusRunner  runner;
    char         name[32];
    bool         present;
} MfbusCpuSlot;

static MfbusCpuSlot s_cpus[MFBUS_MAX_ND5000];

/* One instruction, as the runner sees it. Returns false when the CPU has
 * stopped on its own - halted, breakpoint, fault - and the runner then exits
 * without being asked. */
static bool mfbus_cpu_step(void *ctx)
{
    MfbusCpuSlot *slot = (MfbusCpuSlot *)ctx;
    return nd500_cpu_step(&slot->cpu);
}

/* The slot index of a station, or -1. */
static int mfbus_slot_of(uint8_t station_number)
{
    for (int i = 0; i < s_nd5000_count; i++)
    {
        if (s_nd5000[i].station.number == station_number)
        {
            return i;
        }
    }
    return -1;
}

/* The ND-100 reads a word out of the window. */
static uint16_t mfbus_bank_read(void *ctx, uint32_t word_offset)
{
    (void)ctx;
    return ndbus_window_read_word(&s_window, word_offset);
}

/*
 * The ND-100 writes a word, or half of one.
 *
 * WRITEMODE_MSB is the HIGH byte of the word. The ND-100 calls the EVEN byte
 * address MSB (see the WriteMode cases in cpu_mms.c), and the window's high byte
 * is pool byte 2N - the first of the pair - so the two agree and there is no
 * swap here. Getting this backwards puts every half-word write 256 off, which
 * shows up as a corrupted page-table entry long after the write.
 */
static void mfbus_bank_write(void *ctx, uint32_t word_offset, uint16_t value, WriteMode wm)
{
    (void)ctx;
    switch (wm)
    {
    case WRITEMODE_MSB:
        (void)ndbus_window_write_msb(&s_window, word_offset, (uint8_t)(value >> 8u));
        break;
    case WRITEMODE_LSB:
        (void)ndbus_window_write_lsb(&s_window, word_offset, (uint8_t)(value & 0xFF));
        break;
    case WRITEMODE_WORD:
    default:
        (void)ndbus_window_write_word(&s_window, word_offset, value);
        break;
    }
}

bool mfbus_attach(uint32_t size_bytes, uint32_t base_page)
{
    if (s_attached)
    {
        LOG(LOG_CAT_MMS, LOG_WARN, "MFbus: already attached\n");
        return false;
    }

    if (!ndbus_pool_create(&s_pool, size_bytes))
    {
        LOG(LOG_CAT_MMS, LOG_ERROR, "MFbus: could not allocate a %u byte pool\n",
            (unsigned)size_bytes);
        return false;
    }

    /* The whole pool is the ND-100's window for now. A pool larger than the
     * ND-100 can address is expressed by a part with nd100 = no in the .ini, and
     * that lands here as a SHORTER window over the same pool - not as a smaller
     * pool. */
    uint32_t length_word = size_bytes / 2u;
    if (!ndbus_window_attach(&s_window, &s_pool, 0, length_word))
    {
        LOG(LOG_CAT_MMS, LOG_ERROR, "MFbus: window of %u words does not fit the pool\n",
            (unsigned)length_word);
        ndbus_pool_destroy(&s_pool);
        return false;
    }

    /*
     * WHERE THE WINDOW SITS IN ND-100 MEMORY.
     *
     * A page is 1024 WORDS on the ND-100 (the physical address is
     * ((ppn << 10) | dip), cpu_mms.c), so the window's first word address is
     * base_page * 1024. base_page is the parameter of the ND-500 monitor's
     * DEFINE-MEMORY-CONFIGURATION, and 004100B - 2112 - is byte 0x420000, which
     * is word 0x210000. 2112 * 1024 = 0x210000, so the two agree; that
     * arithmetic is the whole placement and it is worth checking against a
     * known-good value rather than trusting the shift.
     */
    s_base_word = base_page * 1024u;

    if (!mms_memory_bank_register_backed(s_base_word, length_word, ND_MEM_MPM5, mfbus_bank_read,
                                         mfbus_bank_write, NULL))
    {
        /* Refused means it OVERLAPS an existing bank - almost always local RAM,
         * because the configured base page falls inside installed memory. That
         * is a configuration error and it must be loud: a silently unregistered
         * window leaves SINTRAN finding no MPM5 memory at all, and the ND-500
         * then has nowhere to run with nothing explaining why. */
        LOG(LOG_CAT_MMS, LOG_ERROR,
            "MFbus: bank at page %o (word %u, %u words) was REFUSED - it overlaps memory that is "
            "already registered. Check [mfbus] base_page against the installed ND-100 memory "
            "size.\n",
            (unsigned)base_page, (unsigned)s_base_word, (unsigned)length_word);
        ndbus_pool_destroy(&s_pool);
        memset(&s_window, 0, sizeof(s_window));
        return false;
    }

    if (!s_fabric_ready)
    {
        ndbus_fabric_init(&s_fabric, NULL);
        s_fabric_ready = true;
    }

    s_attached = true;
    LOG(LOG_CAT_MMS, LOG_INFO,
        "MFbus: %u MB shared pool as MPM5 at ND-100 page %o (word %u), %u words\n",
        (unsigned)(size_bytes / (1024u * 1024u)), (unsigned)base_page, (unsigned)s_base_word,
        (unsigned)length_word);
    return true;
}

bool mfbus_add_nd5000(uint8_t station_number)
{
    if (!s_attached)
    {
        /* The ND-5000 has no private memory at all - it executes out of the
         * shared pool - so a station with no pool behind it is not a degraded
         * machine, it is one that cannot run. */
        LOG(LOG_CAT_MMS, LOG_ERROR,
            "MFbus: cannot add an ND-5000 at %o - no shared pool is attached\n",
            (unsigned)station_number);
        return false;
    }

    if (s_nd5000_count >= MFBUS_MAX_ND5000)
    {
        LOG(LOG_CAT_MMS, LOG_ERROR, "MFbus: the octobus has only %d ND-5000 slots\n",
            MFBUS_MAX_ND5000);
        return false;
    }

    NdbusNd5000 *nd = &s_nd5000[s_nd5000_count];
    if (!ndbus_nd5000_init(nd, station_number, &s_pool, NULL, NULL))
    {
        /* Refused by the station itself: outside 070B..076B, where another kind
         * of device lives. */
        LOG(LOG_CAT_MMS, LOG_ERROR,
            "MFbus: %o is not an ND-5000 station number (070B to 076B)\n",
            (unsigned)station_number);
        return false;
    }

    if (!ndbus_fabric_register(&s_fabric, &nd->station))
    {
        LOG(LOG_CAT_MMS, LOG_ERROR, "MFbus: octobus station %o is already occupied\n",
            (unsigned)station_number);
        return false;
    }

    s_nd5000_count++;
    LOG(LOG_CAT_MMS, LOG_INFO, "MFbus: ND-5000 on the octobus at station %o\n",
        (unsigned)station_number);
    return true;
}

int mfbus_nd5000_count(void)
{
    return s_nd5000_count;
}

bool mfbus_attach_cpu(uint8_t station_number)
{
    if (!s_attached)
    {
        return false;
    }
    int slot = mfbus_slot_of(station_number);
    if (slot < 0)
    {
        LOG(LOG_CAT_MMS, LOG_ERROR, "MFbus: no ND-5000 station at %o to give a CPU\n",
            (unsigned)station_number);
        return false;
    }
    if (s_cpus[slot].present)
    {
        LOG(LOG_CAT_MMS, LOG_ERROR, "MFbus: the ND-5000 at %o already has a CPU\n",
            (unsigned)station_number);
        return false;
    }

    MfbusCpuSlot *c = &s_cpus[slot];
    memset(c, 0, sizeof(*c));

    /* THE POOL IS THE MEMORY. Not a copy of it, not a window onto it - the same
     * bytes the ND-100 reaches through its MPM-5 bank and every other ND-5000
     * runs out of. ND-500 physical address 0 is pool offset 0. */
    nd500_machine_init_shared(&c->machine, s_pool.bytes, s_pool.size);
    nd500_cpu_init(&c->cpu, &c->machine);
    nd500_cpu_reset(&c->cpu);

    (void)snprintf(c->name, sizeof(c->name), "ND-5000 at %o", (unsigned)station_number);
    c->ops.name = c->name;
    c->ops.ctx = c;

    if (!ndbus_runner_init(&c->runner, &c->ops, mfbus_cpu_step, NULL))
    {
        nd500_cpu_free(&c->cpu);
        nd500_machine_free(&c->machine);
        memset(c, 0, sizeof(*c));
        return false;
    }

    c->present = true;
    LOG(LOG_CAT_MMS, LOG_INFO, "MFbus: %s has a CPU, running out of the shared pool (%u bytes)\n",
        c->name, (unsigned)s_pool.size);
    return true;
}

bool mfbus_start_nd5000(uint8_t station_number)
{
    int slot = mfbus_slot_of(station_number);
    if (slot < 0 || !s_cpus[slot].present)
    {
        return false;
    }
    return ndbus_runner_start(&s_cpus[slot].runner);
}

void mfbus_stop_nd5000(uint8_t station_number)
{
    int slot = mfbus_slot_of(station_number);
    if (slot < 0 || !s_cpus[slot].present)
    {
        return;
    }
    /* Asks AND waits: the thread may be mid-instruction when the request
     * arrives, and the pool it is reading must outlive it. */
    ndbus_runner_stop_and_join(&s_cpus[slot].runner);
}

unsigned long long mfbus_nd5000_instructions(uint8_t station_number)
{
    int slot = mfbus_slot_of(station_number);
    if (slot < 0 || !s_cpus[slot].present)
    {
        return 0;
    }
    return ndbus_runner_instructions(&s_cpus[slot].runner);
}

void mfbus_clear_nd5000(void)
{
    for (int i = 0; i < s_nd5000_count; i++)
    {
        /* STOP AND JOIN BEFORE ANYTHING ELSE. A running thread holds a pointer
         * to the machine, the CPU and the pool; unregistering the station or
         * freeing the CPU underneath it is the one mistake here that produces a
         * crash with no useful stack. */
        if (s_cpus[i].present)
        {
            ndbus_runner_stop_and_join(&s_cpus[i].runner);
            nd500_cpu_free(&s_cpus[i].cpu);
            nd500_machine_free(&s_cpus[i].machine);
            memset(&s_cpus[i], 0, sizeof(s_cpus[i]));
        }
        (void)ndbus_fabric_unregister(&s_fabric, s_nd5000[i].station.number);
    }
    s_nd5000_count = 0;
    memset(s_nd5000, 0, sizeof(s_nd5000));
}

void mfbus_detach(void)
{
    if (!s_attached)
    {
        return;
    }

    /* Stations first: each one holds a pointer to the pool, and the fabric
     * holds a pointer to each station. */
    mfbus_clear_nd5000();
    /* Unregister BEFORE freeing: a bank left in the table would hand the next
     * physical access a callback over a freed pool. */
    (void)mms_memory_bank_unregister(s_base_word);
    ndbus_pool_destroy(&s_pool);
    memset(&s_window, 0, sizeof(s_window));
    s_attached = false;
    s_base_word = 0;
}

NdbusPool *mfbus_pool(void)
{
    return s_attached ? &s_pool : NULL;
}

bool mfbus_is_attached(void)
{
    return s_attached;
}

#endif /* ND100X_WITH_ND500 */
