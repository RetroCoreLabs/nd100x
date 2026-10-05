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

#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mfbus_bridge.h"

#include "ndbus_context.h"
#include "ndbus_cpunum.h"
#include "ndbus_nd5000.h"
#include "ndbus_runner.h"
#include "ndbus_octobus.h"
#include "ndbus_lock.h"
#include "ndbus_pool.h"
#include "ndbus_window.h"
#include "ndbus_servicer.h"

#include "../cpu/cpu_types.h"
#include "../devices/devices_types.h"
#include "../devices/octobus/device_octobus.h"
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
#include "cpu/nd500_mmu.h"
#include "cpu/nd500_tlb.h"           /* nd500_mmu_tlb_flush - the dctsb on a context load */
#include "ndlib/nd_diag_budget.h"   /* ND_DIAG_BUDGET - a bounded diagnostic that says when it stops */
#include "cpu/instruction_helpers.h"   /* ND500_FLAG_K - the monitor-call error flag */
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
 * THE ND-100 AS A STATION ON THE BUS, station 1B.
 *
 * Ported from RetroCore NDBusOctobus.cs: the card registers an
 * ND100StationAdapter on the fabric, whose HandleFrame calls OnFrameFromOctobus,
 * which QUEUES the frame; the card's Clock() then moves one queued frame into
 * the receive FIFO and raises the input event.
 *
 * Without it nothing was registered at station 1, so every frame a station sent
 * to the ND-100 on its own initiative - above all the GIVEINT frame 0x8101 the
 * ND-5000 sends after each answer - ended in ndbus_fabric_send's "no station"
 * arm. The caller discards that result and the fabric has no logger, so nothing
 * said so. MEASURED 05-OCT-2026: SINTRAN then found every answer only when its
 * watchdog came round - 450 MICFU 1B watchdog messages against about 405 real
 * messages in one run - and RUN of CPU-STAT took 805 seconds.
 *
 * QUEUED, NOT PUSHED, for the same reason as in the reference and one more: the
 * sender can be the ND-5000's host thread (a trap or monitor-call stop is
 * reported from mfbus_cpu_step), and the card belongs to the ND-100's thread.
 * The handler only appends under the bus lock; mfbus_service_nd5000_mailboxes(),
 * which the card's tick calls on the ND-100 thread, does the push.
 */
/* One slot per X5CPU plus one, indexed X5CPU + 1 as in the reference. */
#define MFBUS_PROCESS_SLOTS 65
typedef struct
{
    bool     have_trap_enables;   /* false = never saved: the live values stay */
    uint32_t trap_enables[8];     /* OTE1 OTE2 CTE1 CTE2 MTE1 MTE2 TEMM1 TEMM2 */
    uint32_t call_return;         /* pending CALL: return address, 0 = none */
    uint32_t call_arg_count;
    uint32_t call_args[TRAP_SEQ_MAXARG];
} MfbusProcessState;

#define MFBUS_ND100_INBOUND_WORDS 256u
static NdbusStation  s_nd100_station;
static bool          s_nd100_registered = false;
static Device       *s_card = NULL;
static uint16_t      s_inbound[MFBUS_ND100_INBOUND_WORDS];
static unsigned      s_inbound_head = 0;
static unsigned      s_inbound_count = 0;
static unsigned long s_inbound_queued = 0;
static unsigned long s_inbound_delivered = 0;
static unsigned long s_inbound_dropped = 0;

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
    NdbusContext context;      /* this CPU's SAMSON context block */
    bool         context_set;  /* a block has been placed */
    char         name[32];
    bool         present;

    /* Which process this CPU is currently running, as X5CPU numbers them, or -1
     * when none has been started. A trap is reported on THAT process's message. */
    int           loaded_x5cpu;
    unsigned long parked_faults;   /* retryable faults reported to SINTRAN */
    unsigned long steps_logged;    /* bounded start-of-run instruction log */
    uint32_t      resume_trace_left; /* PCs still to log after a monitor-call resume */
    unsigned long mon_calls;       /* monitor calls handed to SINTRAN */
    unsigned long mon_resumes;     /* in-place resumes of the loaded process */
    unsigned long steps_since_resume; /* instructions run since the last resume/start */

    /* Set when the process has reported a stop and is waiting for SINTRAN. The
     * RUNNER tests this, because machine.run_flag is not what it looks at and a
     * monitor call returns INDIRECT_HANDLED - which tells the CPU to CONTINUE.
     * MEASURED 30-SEP-2026: without it the swapper re-asked the same MON 377B
     * fifteen million times in one run, with SINTRAN never having been given a
     * chance to answer. */
    bool          parked;
    /* THE HOST THREAD IS ON ITS WAY OUT. Set by the ND-5000 thread BEFORE it tells
     * SINTRAN about a stop, because from that moment the ND-100 thread may act on
     * the answer: it must first wait for this thread to finish (join) and only
     * then touch the CPU registers or start a new thread. See mfbus_quiesce_runner().
     * Written by one thread and read by the other, so atomic. */
    unsigned      runner_leaving;

    /* PER-PROCESS STATE THE CONTEXT BLOCK DOES NOT CARRY. Ported from RetroCore
     * Nd500CpuProcessBridge (_trapEnablesByProcess and the four _pendingCall*
     * arrays, slot = X5CPU + 1). One CPU runs several ND-500 processes in turn -
     * the swapper and each domain - and these registers leaked from one into the
     * next: a process that parked inside its trap handler left OTE = 0 for
     * whoever ran next, and a process that page-faulted on its callee's entry left
     * its CALL in flight for the other one. One user never sees it; two do. */
    MfbusProcessState proc[MFBUS_PROCESS_SLOTS];
    bool          stop_reported;  /* the non-retryable stop has been named once */
    uint32_t      continues_refused; /* continues with nothing to continue */

    /* WHAT THE TRAP LOOKED LIKE AT THE MOMENT IT WAS RAISED.
     *
     * Filled by mfbus_trap_sink below, which the ND-500 CPU calls from raise_trap
     * for any trap no local handler took. These three values CANNOT be recovered
     * after nd500_cpu_step() returns, which is why the sink exists:
     *
     * machine->stop_reason collapses 64 trap conditions onto a handful of enum
     * values (nd500x cpu.c trap_to_stop_reason) and gives DT and DE no value at
     * all, so the number cannot be recovered after the stop. The MMU status word
     * is NOT captured here - see mfbus_trap_sink for why that was tried and
     * measured to be wrong.
     *
     * trap_captured says the number is a real reading, not a leftover one. */
    uint16_t      trap_number_at_raise;
    bool          trap_captured;
    unsigned long steps_run;      /* instructions executed since attach */
    unsigned long spin_samples;   /* periodic P samples already logged */
} MfbusCpuSlot;

/** How many instructions of a newly started process get named in the log. */
#define MFBUS_STEP_LOG_LIMIT 20u
/* A SECOND, LARGER TRACE THAT STARTS AT EACH RESUME. The boot step log covers the
 * first instructions of the run and can never reach a monitor-call resume, but the
 * swapper's fault happens on the path FROM a resume - so the branch that leads there
 * is invisible to it. PCs only, which is what identifies a branch. */
#define MFBUS_RESUME_TRACE_LIMIT 30000u

/* THE LIMIT IS TUNABLE, because a budget that runs out mid-question answers it
 * wrongly. Measured 2026-10-04: the swapper's store to PST[13] landed 33 lines
 * before this budget expired, so the code that should have completed the entry
 * ran untraced and its ABSENCE from the log read exactly like it never running.
 * A silent instrument is not evidence. MFBUS_RESUME_TRACE sets the per-resume
 * count; the default is unchanged. */
static uint32_t mfbus_resume_trace_limit(void)
{
    static uint32_t limit;
    static int      resolved;
    if (!resolved)
    {
        resolved = 1;
        limit = MFBUS_RESUME_TRACE_LIMIT;
        const char *e = getenv("MFBUS_RESUME_TRACE");
        if (e != NULL)
        {
            long v = strtol(e, NULL, 0);
            if (v >= 0)
            {
                limit = (uint32_t)v;
            }
        }
    }
    return limit;
}

/** How many monitor calls get named before the log falls silent. */
#define MFBUS_MON_LOG_LIMIT 40u

/** How often a still-running process reports where it is, in instructions. */
#define MFBUS_SPIN_SAMPLE_STEPS 2000000u

/** How many such samples before the log falls silent. */
#define MFBUS_SPIN_SAMPLE_LIMIT 12u

static MfbusCpuSlot s_cpus[MFBUS_MAX_ND5000];

/* One instruction, as the runner sees it. Returns false when the CPU has
 * stopped on its own - halted, breakpoint, fault - and the runner then exits
 * without being asked. */
/*
 * One instruction, and on a RETRYABLE FAULT park the process and tell SINTRAN
 * instead of letting the CPU stop.
 *
 * A PAGE FAULT IS NOT A CRASH ON THIS LANE, IT IS THE DEMAND-PAGING HANDSHAKE.
 * The ND-5000 runs out of pages SINTRAN has not brought in yet; it reports the
 * fault through its own activation message, SINTRAN's swapper reads the fault
 * address and the physical segment out of the record, pages the page in, and sends
 * 3TRACO (25B) to resume. The servicer already routes 25B through the start class,
 * so the resume half was in place before this.
 *
 * MEASURED 30-SEP-2026: without this the swapper got eleven instructions into its
 * entry point, took a data fault at logical 0x08012818 - page 37 of a page table
 * whose entries 29-36 were correctly mapped and whose 37-45 SINTRAN had
 * deliberately zeroed - and the CPU simply stopped. The monitor then printed
 * nothing further, because nothing had told it a fault happened.
 *
 * P1, NOT PC, IS THE RESTART ADDRESS. A page fault and a protect violation both
 * mean "this instruction did not complete", so the process must resume ON it. PC
 * normally runs ahead of P1 by the time the trap is taken.
 */
/* Defined below, next to the context switch it mirrors; both park sites above
 * need it, so it is declared here rather than moved away from its pair. */
/*
 * THE CONTEXT BLOCK'S REGISTER LIST - ONE TABLE, BOTH DIRECTIONS.
 *
 * CNTXTSAVE and NEWCNTXT used to be two hand-written lists of the same
 * registers in two functions a thousand lines apart. That is how TOS and LL
 * came to be read by the load and written by neither: a register dropped from
 * one list and not the other is invisible, and no test could catch it because
 * there was nothing to compare the lists against.
 *
 * With one table the failure is not merely tested for, it is unrepresentable -
 * save and load iterate the SAME rows, so a row either crosses a context switch
 * in both directions or in neither. Adding a register is one line here.
 *
 * `mask` is the width the microcode moves, not a convenience: PS is a 13-bit
 * halfword register (015043 NEW_PS_1, "TYP,HW ... D,MM,PS", ND-05.020.01
 * section 6.6) so a dirty high halfword must not invent a segment, and CED/CAD
 * are byte transfers.
 *
 * `in_block_low_half` marks a slot the BLOCK also uses for something else:
 * SRF13 carries PS in its low halfword and belongs to the block above that, so
 * the save must read-modify-write it rather than overwrite the whole word.
 *
 * `saved` is false for a row the load reads and the save must NOT write back.
 * There is exactly one today - PS - and the reason is directional: CNTXTLOAD at
 * 0o14777 READS ctx+0x48, so a value we invent there is consumed by the
 * microcode as if SINTRAN had written it. The reference gates the same write off
 * for this generation. A row with saved=false is the one shape this table cannot
 * make safe by construction, so it is spelled out rather than implied.
 */
typedef struct
{
    uint32_t    offset;            /* NDBUS_CTX_* byte offset in the block */
    const char *name;              /* names the register in a failure message */
    size_t      cpu_field;         /* offsetof() into Nd500Cpu - every one is uint32_t */
    uint32_t    mask;              /* the width the microcode moves */
    bool        saved;             /* false: loaded only, never written back */
    bool        in_block_low_half; /* the slot's high halfword belongs to the block */
} MfbusCtxField;

#define MFBUS_CTX_ROW(off, nm, field, msk, sv, lowhalf) \
    { (off), (nm), offsetof(Nd500Cpu, field), (msk), (sv), (lowhalf) }

static const MfbusCtxField s_ctx_fields[] = {
    MFBUS_CTX_ROW(NDBUS_CTX_P,      "P",   PC,    0xFFFFFFFFu, true,  false),
    MFBUS_CTX_ROW(NDBUS_CTX_L,      "L",   L,     0xFFFFFFFFu, true,  false),
    MFBUS_CTX_ROW(NDBUS_CTX_B,      "B",   B,     0xFFFFFFFFu, true,  false),
    MFBUS_CTX_ROW(NDBUS_CTX_R,      "R",   R,     0xFFFFFFFFu, true,  false),

    MFBUS_CTX_ROW(NDBUS_CTX_I1,     "I1",  I[0],  0xFFFFFFFFu, true,  false),
    MFBUS_CTX_ROW(NDBUS_CTX_I2,     "I2",  I[1],  0xFFFFFFFFu, true,  false),
    MFBUS_CTX_ROW(NDBUS_CTX_I3,     "I3",  I[2],  0xFFFFFFFFu, true,  false),
    MFBUS_CTX_ROW(NDBUS_CTX_I4,     "I4",  I[3],  0xFFFFFFFFu, true,  false),

    MFBUS_CTX_ROW(NDBUS_CTX_A1,     "A1",  A[0],  0xFFFFFFFFu, true,  false),
    MFBUS_CTX_ROW(NDBUS_CTX_A2,     "A2",  A[1],  0xFFFFFFFFu, true,  false),
    MFBUS_CTX_ROW(NDBUS_CTX_A3,     "A3",  A[2],  0xFFFFFFFFu, true,  false),
    MFBUS_CTX_ROW(NDBUS_CTX_A4,     "A4",  A[3],  0xFFFFFFFFu, true,  false),

    MFBUS_CTX_ROW(NDBUS_CTX_E1,     "E1",  E[0],  0xFFFFFFFFu, true,  false),
    MFBUS_CTX_ROW(NDBUS_CTX_E2,     "E2",  E[1],  0xFFFFFFFFu, true,  false),
    MFBUS_CTX_ROW(NDBUS_CTX_E3,     "E3",  E[2],  0xFFFFFFFFu, true,  false),
    MFBUS_CTX_ROW(NDBUS_CTX_E4,     "E4",  E[3],  0xFFFFFFFFu, true,  false),

    /* The status composite. CNTXTSAVE writes 0x40 and 0x44; CNTXTLOAD reads them
     * back. ST1 carries PIA and the K flag. */
    MFBUS_CTX_ROW(NDBUS_CTX_STATUS, "ST1", ST1,   0xFFFFFFFFu, true,  false),
    MFBUS_CTX_ROW(NDBUS_CTX_SRF10,  "ST2", ST2,   0xFFFFFFFFu, true,  false),

    /* The stack limits, in the two slots the microcode ignores in BOTH
     * directions - see the load's comment for why these and not HL/THA. */
    MFBUS_CTX_ROW(NDBUS_CTX_DIT_TOS, "TOS", TOS,  0xFFFFFFFFu, true,  false),
    MFBUS_CTX_ROW(NDBUS_CTX_DIT_LL,  "LL",  LL,   0xFFFFFFFFu, true,  false),

    MFBUS_CTX_ROW(NDBUS_CTX_CED,    "CED", CED,   0x000000FFu, true,  false),
    MFBUS_CTX_ROW(NDBUS_CTX_CAD,    "CAD", CAD,   0x000000FFu, true,  false),

    /* LOADED ONLY. The microcode sources this slot itself; see the table note. */
    MFBUS_CTX_ROW(NDBUS_CTX_SRF13,  "PS",  PS,    0x00001FFFu, false, true),
};

#define MFBUS_CTX_FIELD_COUNT (sizeof(s_ctx_fields) / sizeof(s_ctx_fields[0]))

/* The register the row names, inside a given CPU. One cast, in one place, so the
 * offsetof arithmetic is not repeated at every use. */
static uint32_t *mfbus_ctx_reg(Nd500Cpu *cpu, const MfbusCtxField *f)
{
    return (uint32_t *)((char *)cpu + f->cpu_field);
}

/* Published for the unit test, which asserts the round-trip property over every
 * row rather than over a list it repeats for itself - a test that restates the
 * list cannot catch a register missing from both. */
size_t mfbus_context_field_count(void)
{
    return MFBUS_CTX_FIELD_COUNT;
}

bool mfbus_context_field_info(size_t index, uint32_t *out_offset, const char **out_name,
                              uint32_t *out_mask, bool *out_saved)
{
    if (index >= MFBUS_CTX_FIELD_COUNT)
    {
        return false;
    }
    const MfbusCtxField *f = &s_ctx_fields[index];
    if (out_offset != NULL) { *out_offset = f->offset; }
    if (out_name   != NULL) { *out_name   = f->name; }
    if (out_mask   != NULL) { *out_mask   = f->mask; }
    if (out_saved  != NULL) { *out_saved  = f->saved; }
    return true;
}

static bool mfbus_save_context(MfbusCpuSlot *c);

/* THE TRAP-STOP SEAM, the nd100x end of it.
 *
 * Ported from RetroCore Nd500CpuProcessBridge.OnUnhandledTrap
 * (Emulated.HW/ND/CPU/ND500/Servicer/, contract in ITrapSink.cs). The ND-500 CPU
 * calls this from raise_trap() for any trap no local THA/DIT handler consumed.
 *
 * IT CAPTURES THE TRAP NUMBER AND NOTHING ELSE, because the trap number is the
 * only thing that is genuinely gone by the time the stop is noticed:
 * trap_to_stop_reason() in nd500x cpu.c collapses 64 trap conditions onto a
 * handful of StopReason values in priority order, and DT and DE have no value at
 * all. Without this, a report can only name the traps someone hand-listed - which
 * is how a stack overflow came to be answered with silence.
 *
 * IT DELIBERATELY DOES NOT READ THE MMU LATCH. An earlier version of this
 * function composed the memory-management status here on the reasoning that the
 * latch must still be live at the raise. IT IS NOT: raise_trap() copies
 * mmu_pgf_where into trap_saved_info and zeroes it (nd500x cpu.c:1340) EARLY,
 * before the handler dispatch this callback sits after. MEASURED 04-OCT-2026 -
 * the sink reported "trap 46B ... mms=0x00000000 psn=0" for a page fault whose
 * status word had previously been composed correctly, so reading the latch here
 * is not better than reading it later, it is worse. The status word is composed
 * at the stop from trap_saved_info, which is the field that preserves it and
 * carries the PFZ2 default; the comment at that site has said so since
 * 30-SEP-2026 and it was right.
 *
 * It returns 0 - declines to park the CPU here. The reference parks from inside
 * this callback, but on this lane the stop fields, the context save and the answer
 * are already sequenced correctly after nd500_cpu_step() returns, and the
 * page-fault path through them is the one measured serving psn 11 and psn 12.
 * Rewriting a working sequence to gain nothing is not a port. If a trap is ever
 * found that must park before the instruction unwinds, this is where to do it and
 * the return value already says so. */
static int mfbus_trap_sink(void *ctx, uint16_t trap_number, uint32_t trapping_pc,
                           uint32_t trap_address)
{
    MfbusCpuSlot *slot = (MfbusCpuSlot *)ctx;
    if (slot == NULL)
    {
        return 0;
    }

    slot->trap_number_at_raise = trap_number;
    slot->trap_captured = true;

    /* THE TRAP, AS RAISED. The status word is not shown here on purpose - it is
     * composed at the stop, from the field that preserves it, and printing a
     * second reading of it here would invite comparing two numbers that are
     * measured at different moments and mean different things. THA is shown
     * because THA=0 is what makes "no local handler" a certainty rather than a
     * lookup that merely missed. */
    LOG(LOG_CAT_MMS, LOG_INFO,
        "MFbus: %s trap %oB raised at P=0x%08X addr=0x%08X (no local handler; "
        "THA=0x%08X)\n",
        slot->name, (unsigned)trap_number, (unsigned)trapping_pc,
        (unsigned)trap_address, (unsigned)slot->cpu.THA);

    return 0;  /* the stop path below reports it - see the comment above */
}

static bool mfbus_cpu_step_inner(MfbusCpuSlot *slot);

/* The runner's step. A false return ends the host thread, so it is marked as
 * leaving on EVERY such return - the two stop paths that answer SINTRAN set the
 * mark earlier, before the answer goes out, and this covers the rest. */
static bool mfbus_cpu_step(void *ctx)
{
    MfbusCpuSlot *slot = (MfbusCpuSlot *)ctx;

    if (mfbus_cpu_step_inner(slot))
    {
        return true;
    }
    __atomic_store_n(&slot->runner_leaving, 1u, __ATOMIC_RELEASE);
    return false;
}

static bool mfbus_cpu_step_inner(MfbusCpuSlot *slot)
{
    /* Parked processes do not step. The restart clears this. */
    if (slot->parked)
    {
        return false;
    }

    /* WHERE A SPINNING PROCESS IS SPINNING. A CPU that never stops reports nothing
     * at all - no stop reason, no park - and from the outside that is identical to
     * one doing useful work. MEASURED 30-SEP-2026: the swapper completed its monitor
     * call, took SINTRAN's answer and then ran without end, and neither the stop
     * instrument nor the park counter could say a word about it. A periodic sample
     * of P names the loop. Bounded, so a long healthy run does not fill the log. */
    slot->steps_run++;
    if ((slot->steps_run % MFBUS_SPIN_SAMPLE_STEPS) == 0u
        && slot->spin_samples < MFBUS_SPIN_SAMPLE_LIMIT)
    {
        slot->spin_samples++;
        LOG(LOG_CAT_MMS, LOG_INFO,
            "MFbus: %s still running after %lu steps: P=0x%08X B=0x%08X I1=0x%08X ST1=0x%08X\n",
            slot->name, slot->steps_run, (unsigned)slot->cpu.PC, (unsigned)slot->cpu.B,
            (unsigned)slot->cpu.I[0], (unsigned)slot->cpu.ST1);
    }

    /* THE FIRST INSTRUCTIONS, NAMED. The swapper faults on its twelfth, and a fault
     * address can be either the address the program meant or the address a
     * mis-executed instruction produced. Only the run of PCs before it tells those
     * apart. Bounded, and through the logger rather than stdout, because stdout on
     * this lane is the HOST guest's console. */
    /* THE TWO SIDES OF ONE COMPARISON, AT THE INSTRUCTION THAT MAKES IT.
     *
     * A trace says which way a branch went; it cannot say WHY, because the
     * operands are in memory the trace never prints. And the operands cannot be
     * read from a pool snapshot taken at some other moment either: the two data
     * pages involved here are 0x14700 bytes apart, so the logical-to-physical
     * delta measured at one of them does not hold at the other.
     *
     * MFBUS_PCDUMP names the program address to stop on; MFBUS_PCDUMP_ADDR an
     * ND-500 DATA logical address to show 16 bytes of, and the frame's own
     * B+0x14 is shown beside it. Both are translated through the NON-FAULTING
     * peek, because a diagnostic that could raise a page fault would corrupt the
     * run it is describing. Bounded, and it announces the bound.
     */
    {
        ND_DIAG_BUDGET_ENV(pcdump, 8, "MFBUS_PCDUMP_BUDGET");
        static uint32_t pcdump_pc;
        static uint32_t pcdump_addr;
        static int      pcdump_ready;
        if (!pcdump_ready)
        {
            pcdump_ready = 1;
            const char *e = getenv("MFBUS_PCDUMP");
            pcdump_pc = (e != NULL) ? (uint32_t)strtoul(e, NULL, 0) : 0u;
            e = getenv("MFBUS_PCDUMP_ADDR");
            pcdump_addr = (e != NULL) ? (uint32_t)strtoul(e, NULL, 0) : 0u;
        }

        if (pcdump_pc != 0u && slot->cpu.PC == pcdump_pc && ND_DIAG_TAKE(pcdump))
        {
            char tbl[80];
            int  n = 0;
            if (pcdump_addr != 0u)
            {
                for (uint32_t k = 0; k < 16u && n >= 0 && (size_t)n < sizeof tbl; k++)
                {
                    uint32_t pa = nd500_mmu_peek_space(&slot->cpu, pcdump_addr + k,
                                                       (uint8_t)slot->cpu.CED, 0);
                    if (pa == 0xFFFFFFFFu)
                    {
                        n += snprintf(tbl + n, sizeof tbl - (size_t)n, " --");
                        continue;
                    }
                    n += snprintf(tbl + n, sizeof tbl - (size_t)n, "%02X",
                                  (unsigned)ndbus_pool_read8(&s_pool, pa));
                }
            }

            uint32_t local = 0u;
            int      local_ok = 0;
            uint32_t lpa = nd500_mmu_peek_space(&slot->cpu, slot->cpu.B + 0x14u,
                                                (uint8_t)slot->cpu.CED, 0);
            if (lpa != 0xFFFFFFFFu)
            {
                local_ok = 1;
                local = ((uint32_t)ndbus_pool_read8(&s_pool, lpa) << 24)
                      | ((uint32_t)ndbus_pool_read8(&s_pool, lpa + 1u) << 16)
                      | ((uint32_t)ndbus_pool_read8(&s_pool, lpa + 2u) << 8)
                      |  (uint32_t)ndbus_pool_read8(&s_pool, lpa + 3u);
            }

            LOG(LOG_CAT_MMS, LOG_INFO,
                "MFbus: %s PCDUMP at P=0x%08X: B=0x%08X B+0x14=0x%08X -> %s "
                "R=0x%08X I1=0x%08X I2=0x%08X | [0x%08X]=%s\n",
                slot->name, (unsigned)slot->cpu.PC, (unsigned)slot->cpu.B,
                (unsigned)(slot->cpu.B + 0x14u),
                local_ok ? "" : "(untranslatable)",
                (unsigned)slot->cpu.R, (unsigned)slot->cpu.I[0], (unsigned)slot->cpu.I[1],
                (unsigned)pcdump_addr, (n > 0) ? tbl : "(none)");
            if (local_ok)
            {
                LOG(LOG_CAT_MMS, LOG_INFO,
                    "MFbus: %s PCDUMP   B+0x14 = 0x%08X (%u)\n",
                    slot->name, (unsigned)local, (unsigned)local);
            }
        }
    }

    if (slot->resume_trace_left > 0u)
    {
        slot->resume_trace_left--;
        /* THE PHYSICAL ADDRESS AND THE FIRST BYTES, NOT JUST THE PC.
         *
         * A run of PCs alone cannot say WHICH program produced them. Measured on
         * PLACE-DOMAIN CPU-STAT: the domain and the swapper produced the IDENTICAL
         * sequence 0x04 -> 0x11 -> 0x14 -> 0x16 from completely different code, and
         * CPU-STAT's own instruction boundaries (read from CPU-STAT.DOM: 0x04, 0x0A,
         * 0x10, 0x12, 0x18) are none of those. So either the domain fetched the
         * swapper's bytes, or the PC advance is wrong - and a PC-only trace cannot
         * tell those apart. The capability table in force decides which, so print
         * where the fetch actually landed.
         *
         * Through the NON-FAULTING peek, the same one the step log uses: a
         * diagnostic that could raise a page fault would corrupt the run it is
         * describing. */
        uint32_t rt_pa = nd500_mmu_peek_space(&slot->cpu, slot->cpu.PC,
                                              (uint8_t)slot->cpu.CED, 1);
        unsigned b0 = 0u, b1 = 0u;
        if (rt_pa != 0xFFFFFFFFu)
        {
            b0 = ndbus_pool_read8(&s_pool, rt_pa);
            b1 = ndbus_pool_read8(&s_pool, rt_pa + 1u);
        }
        /* NAME THE PROCESS. A PC and a PS do not say whose stream this is: two
         * processes share one slot->cpu, and a trace without the process number was
         * read as the domain's when it was the swapper's - the swapper's entry
         * 0x04 -> 0x11 -> 0x14 -> 0x16 is its own correct stream (a 13-byte init,
         * then move, stz, comp2) and was mistaken for a broken 6-byte call. */
        /* B AND L TOO. CPU-STAT's fourth page fault is a write to segment 0 offset
         * 4 with B = 0x00000004 - the frame base itself is 4, so the address is
         * wrong rather than missing, and no page-in can satisfy it. Which
         * instruction put 4 in B is only visible if B is printed per step. */
        LOG(LOG_CAT_MMS, LOG_INFO,
            "MFbus: %s rt x5=%d P=0x%08X pa=0x%08X %02X %02X B=0x%08X L=0x%08X R=0x%08X "
            "CED=%u PS=0x%X DIT=0x%X THA=0x%08X\n",
            slot->name, slot->loaded_x5cpu, (unsigned)slot->cpu.PC, (unsigned)rt_pa,
            b0, b1, (unsigned)slot->cpu.B, (unsigned)slot->cpu.L, (unsigned)slot->cpu.R,
            (unsigned)slot->cpu.CED, (unsigned)slot->cpu.PS,
            (unsigned)slot->cpu.DITBASE, (unsigned)slot->cpu.THA);
    }

    if (slot->steps_logged < MFBUS_STEP_LOG_LIMIT)
    {
        slot->steps_logged++;
        /* The bytes too, read through a NON-FAULTING peek - a diagnostic that could
         * itself raise a page fault would corrupt the very run it is describing. */
        uint32_t code_pa = nd500_mmu_peek_space(&slot->cpu, slot->cpu.PC,
                                                (uint8_t)slot->cpu.CED, 1);
        char bytes[40];
        bytes[0] = '\0';
        if (code_pa != 0xFFFFFFFFu)
        {
            for (uint32_t k = 0; k < 8u; k++)
            {
                char one[6];
                (void)snprintf(one, sizeof one, "%02X ",
                               (unsigned)ndbus_pool_read8(&s_pool, code_pa + k));
                (void)strncat(bytes, one, sizeof bytes - strlen(bytes) - 1u);
            }
        }
        LOG(LOG_CAT_MMS, LOG_INFO,
            "MFbus: %s step %2lu: P=0x%08X B=0x%08X L=0x%08X R=0x%08X pa=0x%08X %s\n",
            slot->name, slot->steps_logged, (unsigned)slot->cpu.PC, (unsigned)slot->cpu.B,
            (unsigned)slot->cpu.L, (unsigned)slot->cpu.R, (unsigned)code_pa, bytes);
    }

    /* THE STOP REASON MUST DESCRIBE THIS STEP AND NO EARLIER ONE.
     *
     * Nothing else clears it, so after any stop the field keeps its value for the
     * rest of the run and every later false return reads it again - together with
     * the stale stop_data, P1 and mmu_pgf_psn that went with it.
     *
     * MEASURED on PLACE-DOMAIN CPU-STAT. The domain faulted at P=0x08000016 on data
     * 0x08012818, that fault was correctly reported on the domain's message, and
     * the context switch then put the swapper back on the CPU at P=0x08008255. The
     * very next step returned false, the park path re-read the domain's fault and
     * posted it A SECOND TIME - this time with loaded_x5cpu = 0, so it landed on
     * the SWAPPER's message 0x8D30. SINTRAN's TRAPDECODER
     * (MP-P2-N500.NPL:135370) treats a page fault on the swapper's own message as
     * EPFINSWAP and calls XRSTARTALL: "*** FATAL SYSTEM ERROR *** / The Swapper
     * stopped", over a fault the swapper never took.
     *
     * Cleared BEFORE the step, so a false return carrying STOP_NONE says "this step
     * stopped and recorded no reason" instead of silently inheriting one. */
    slot->machine.stop_reason = STOP_NONE;

    /* THE PC THE STEP STARTED AT. A trap latches P1 from cur_instr_pc, which is set
     * at the top of every step, so "P1 names an instruction this step never began
     * at" is the signature of a trap that was already pending when the step was
     * entered - told apart from a real fault only by comparing the two. */
    uint32_t pc_at_entry = slot->cpu.PC;

    if (nd500_cpu_step(&slot->cpu))
    {
        slot->steps_since_resume++;
        return true;
    }

    StopReason why = slot->machine.stop_reason;
    if (slot->parked_faults < MFBUS_STEP_LOG_LIMIT)
    {
        LOG(LOG_CAT_MMS, LOG_INFO,
            "MFbus: %s step stopped: entry P=0x%08X now P=0x%08X P1=0x%08X cur=0x%08X "
            "reason=%s stop_addr=0x%08X stop_data=0x%08X psn=%u\n",
            slot->name, (unsigned)pc_at_entry, (unsigned)slot->cpu.PC,
            (unsigned)slot->cpu.P1, (unsigned)slot->cpu.cur_instr_pc,
            nd500_stop_reason_str(why), (unsigned)slot->machine.stop_addr,
            (unsigned)slot->machine.stop_data,
            /* THE PRESERVED psn, not the live one. raise_trap clears all three
             * latch fields now, so the live field reads 0 by the time any stop is
             * noticed - a diagnostic printing it would report "psn=0" for every
             * fault and look like a defect in the walk. psn=0 HERE means the trap
             * had no MMU access in it, which is a fact and not a gap. */
            (unsigned)slot->cpu.trap_saved_psn);
    }
    /* WHICH STOPS CAN BE REPORTED. Until 04-OCT-2026 this tested for exactly two
     * StopReason values - page fault and protect violation - and every other trap
     * fell through to the silent return below. That was not a port of anything: the
     * reference has a GENERIC seam (ITrapSink.OnUnhandledTrap, any trap by number)
     * and the two cases here were the only ones this bring-up had ever produced, so
     * nothing failed and nothing flagged the gap.
     *
     * MEASURED 04-OCT-2026: the swapper took a stack overflow (33B) and this
     * returned false without telling SINTRAN anything, so SINTRAN polled 3RMICV
     * until it timed out. Ronny saw a monitor that printed nothing and had to be
     * stopped with ESC.
     *
     * The gate is now "did the sink capture a trap number", which is true for every
     * trap no local handler took, whatever it was. */
    if (!slot->trap_captured)
    {
        /* NOT RETRYABLE - AND SAY SO. A silent stop here is indistinguishable from
         * a parked process from the outside: SINTRAN keeps polling 3RMICV, the
         * console prints nothing further, and the run looks like a hang with no
         * cause. MEASURED 30-SEP-2026: the swapper resumed from its first monitor
         * call, stopped for a reason nothing recorded, and SINTRAN polled for the
         * remaining seven minutes of the run. Once per CPU, because a stopped CPU
         * is re-entered on every poll. */
        if (!slot->stop_reported)
        {
            slot->stop_reported = true;
            LOG(LOG_CAT_MMS, LOG_WARN,
                "MFbus: %s STOPPED and cannot continue: %s at P=0x%X (stop_addr=0x%X "
                "stop_data=0x%X P1=0x%X) - nothing has been reported to SINTRAN, so it will "
                "poll until it times out\n",
                slot->name, nd500_stop_reason_str(why), (unsigned)slot->cpu.PC,
                (unsigned)slot->machine.stop_addr, (unsigned)slot->machine.stop_data,
                (unsigned)slot->cpu.P1);
        }
        return false;
    }

    /* s_cpus[] and s_nd5000[] are parallel arrays - same index, same machine. */
    int nd_index = (int)(slot - s_cpus);
    if (nd_index < 0 || nd_index >= s_nd5000_count || slot->loaded_x5cpu < 0)
    {
        return false;
    }
    NdbusNd5000 *nd = &s_nd5000[nd_index];

    /* The trap number SINTRAN expects, taken from the sink rather than reconstructed
     * from StopReason - which cannot name it (nd500x cpu.c trap_to_stop_reason
     * collapses 64 conditions and gives DT and DE no value at all). ND trap
     * numbering throughout, so octal: page fault 46B, protect violation 44B, stack
     * overflow 33B. */
    uint16_t trap_number = slot->trap_number_at_raise;

    /* The composed MMS status word. Bits 31-29 carry the access class - 100 read,
     * 101 write - and the low byte carries the fault-location nibble plus the
     * instruction-side bit the walk latched.
     *
     * READ trap_saved_info, NOT mmu_pgf_where. raise_trap() copies the walk's
     * fault-location code into trap_saved_info and then CLEARS mmu_pgf_where
     * (cpu.c), so by the time the stop is noticed here the live field is always
     * zero. MEASURED 30-SEP-2026: the record reached SINTRAN with a correct fault
     * address, a correct physical segment and MEMORY MANAGEMENT STATUS
     * 20000000000B - the access class alone, with no fault location - and SINTRAN
     * answered "The Swapper stopped / Fatal intern" rather than paging the page in.
     * trap_saved_info also carries the PFZ2 default for a page fault whose walk
     * recorded nothing, which is the right value rather than a zero. */
    /* The composed MMS status word. Bits 31-29 carry the access class - 100 read,
     * 101 write - and the low byte carries the fault-location nibble plus the
     * instruction-side bit the walk latched.
     *
     * READ trap_saved_info, NOT mmu_pgf_where. raise_trap() copies the walk's
     * fault-location code into trap_saved_info and then CLEARS mmu_pgf_where
     * (nd500x cpu.c:1340), so the live field is zero by the time anything outside
     * the walk looks at it - including the trap sink, which sits AFTER that clear
     * and not before it. Measured both ways: composed from trap_saved_info the
     * report carries a real fault location, composed from the live field it
     * carries 0x00000000 and SINTRAN answers "NOT KNOWN TRAP".
     * trap_saved_info also carries the PFZ2 default for a page fault whose walk
     * recorded nothing, which is the right value rather than a zero. */
    uint32_t mms = (slot->cpu.trap_saved_is_write ? 0xA0000000u : 0x80000000u)
                   | (slot->cpu.trap_saved_info & 0xFFu);

    uint32_t restart_p = (slot->cpu.P1 != 0u) ? slot->cpu.P1 : slot->machine.stop_addr;

    /* SET THE LIVE P. The microcode's context switch at 011473B compares the loaded
     * process against the wanted one and skips BOTH the save and the load when they
     * match, so a 3TRACO naming the running process resumes from the LIVE registers
     * and never reads the context block back. P must therefore already hold the
     * restart address at the moment of the park.
     *
     * THE BLOCK IS WRITTEN BACK TOO, now that it matters. This was a stated gap -
     * "it only matters once a DIFFERENT process is loaded in between, because that
     * is the only case where the block is read again" - and PLACE-DOMAIN is that
     * case: the domain parks here and SINTRAN then runs the SWAPPER, so the next
     * read of this block is a context switch back to the domain. The restart P is
     * already in PC above, so the saved block restarts the faulting instruction. */
    slot->cpu.PC = restart_p;
    if (!mfbus_save_context(slot))
    {
        LOG(LOG_CAT_MMS, LOG_WARN,
            "MFbus: %s trap park could not save the context block - a switch back to this "
            "process would resume it from a stale P\n",
            slot->name);
    }

    /* PARK BEFORE THE ANSWER GOES OUT. Ported from RetroCore CpuND500.Trap.cs
     * RaiseTrap ("PARK BEFORE THE SINK ANSWERS ... the answer + interrupt precede
     * the park otherwise, and a fast 3TRACO could race the WAIT bit"). The answer
     * raises an interrupt on the ND-100, which runs on another host thread and can
     * post the restart at once. With the park set afterwards that restart found
     * parked == false and was refused, or found the thread still running, took it
     * for "already going", and the thread then exited with nobody to start it
     * again. */
    /* THE ENGINE LOCK IS TAKEN BEFORE THE PARK IS MARKED, and held across the
     * answer. The ND-100 thread holds the same lock while it works through a
     * mailbox chain (nd500x ndbus_servicer_process_chain, the port of the
     * reference's _engineLock). Taking it first means this thread waits out a
     * chain walk in progress - one that may still be recording the message this
     * very process was started on - with the "leaving" mark still clear, so a
     * start inside that walk does not wait on a thread that is itself waiting
     * for the lock. */
    ndbus_engine_lock();
    slot->parked = true;
    __atomic_store_n(&slot->runner_leaving, 1u, __ATOMIC_RELEASE);

    bool trap_answered =
        ndbus_servicer_answer_trap_stop(&nd->servicer, (uint16_t)slot->loaded_x5cpu, trap_number,
                                        restart_p, slot->machine.stop_data, mms,
                                        (uint16_t)slot->cpu.trap_saved_psn);
    ndbus_engine_unlock();
    if (!trap_answered)
    {
        /* The servicer says why it refused. Nothing was told to SINTRAN, so this
         * is not a park it can restart; the thread still ends here and SINTRAN
         * will time out, which is the honest outcome. */
        slot->parked = false;
        return false;
    }

    /* THE TRAP HAS NOW BEEN DELIVERED, SO CONSUME IT.
     *
     * raise_trap() sets the trap state AND the machine's stop fields for a
     * non-ignorable trap with no handler installed, and only the top-of-step check
     * in nd500_cpu_step() clears the state. On an ND-5000 process there IS no
     * in-CPU handler - SINTRAN is the handler, reached through the message just
     * answered - so without this the same fault is delivered twice: once here from
     * the stop fields, and again on the next step when that check converts the
     * still-pending state into a second stop.
     *
     * MEASURED on PLACE-DOMAIN CPU-STAT. The domain's data fault at P=0x08000016
     * was reported on the domain's message, the context switch put the swapper back
     * at P=0x08008255, and its very first step stopped with entry P=0x08008255 and
     * cur_instr_pc=0x08008255 but P1, stop_addr and stop_data all still naming
     * 0x08000016 - the step had begun and raised nothing. That second report
     * carried loaded_x5cpu = 0, so it landed on the SWAPPER's message 0x8D30, and
     * SINTRAN's TRAPDECODER (MP-P2-N500.NPL:135370) read a page fault on the
     * swapper's own message as EPFINSWAP and called XRSTARTALL: "*** FATAL SYSTEM
     * ERROR *** / The Swapper stopped", over a fault the swapper never took.
     *
     * Only on the path where the report SUCCEEDED. A declined report has not been
     * delivered to anything, and discarding the trap there would lose the fault. */
    nd500_trap_clear();

    /* AND CONSUME THE CAPTURE. trap_captured is the gate above, so a stale one
     * would let the next stop for any reason at all re-report this same trap
     * number and status word. Cleared only here, on the path where the report
     * succeeded - the same rule nd500_trap_clear() above follows. */
    slot->trap_captured = false;

    slot->parked_faults++;
    LOG(LOG_CAT_MMS, LOG_INFO,
        "MFbus: %s parked on trap %oB at P=0x%X fault=0x%X psn=%u mms=0x%08X - reported to "
        "SINTRAN\n",
        slot->name, (unsigned)trap_number, (unsigned)restart_p,
        (unsigned)slot->machine.stop_data, (unsigned)slot->cpu.trap_saved_psn,
        (unsigned)mms);

    /* WHICH REGISTER PRODUCED THE FAULTING ADDRESS. The trap says where the access
     * went; it does not say what computed it. Without this a fault address can only
     * be guessed at, and the guess is usually wrong - so print the registers an
     * effective address is built from, once per parked fault and bounded by the same
     * limit as the step log so a fault loop cannot flood the console. */
    if (slot->parked_faults <= MFBUS_STEP_LOG_LIMIT)
    {
        LOG(LOG_CAT_MMS, LOG_INFO,
            "MFbus: %s   at fault: B=0x%08X R=0x%08X L=0x%08X "
            "I1=0x%08X I2=0x%08X I3=0x%08X I4=0x%08X\n",
            slot->name,
            (unsigned)slot->cpu.B, (unsigned)slot->cpu.R, (unsigned)slot->cpu.L,
            (unsigned)slot->cpu.I[0], (unsigned)slot->cpu.I[1],
            (unsigned)slot->cpu.I[2], (unsigned)slot->cpu.I[3]);

        /* AND THE INSTRUCTION THAT DID IT. Through the same NON-FAULTING peek the
         * step log uses: a diagnostic that could itself raise a page fault would
         * corrupt the run it is describing. */
        uint32_t fault_pa = nd500_mmu_peek_space(&slot->cpu, (uint32_t)restart_p,
                                                 (uint8_t)slot->cpu.CED, 1);
        char fbytes[40];
        fbytes[0] = '\0';
        if (fault_pa != 0xFFFFFFFFu)
        {
            for (uint32_t k = 0; k < 8u; k++)
            {
                char one[6];
                (void)snprintf(one, sizeof one, "%02X ",
                               (unsigned)ndbus_pool_read8(&s_pool, fault_pa + k));
                (void)strncat(fbytes, one, sizeof fbytes - strlen(fbytes) - 1u);
            }
        }
        LOG(LOG_CAT_MMS, LOG_INFO,
            "MFbus: %s   faulting instruction at P=0x%08X pa=0x%08X: %s\n",
            slot->name, (unsigned)restart_p, (unsigned)fault_pa, fbytes);

        /* WHICH DOMAIN, AND WHICH CAPABILITY. A fault one page past a mapped region
         * has two opposite causes: the wrong capability was resolved (the real
         * segment is larger), or the program overran a correctly-sized segment.
         * The domain and the capability word tell those apart; without them the
         * choice is a guess. The capability address is the same one the MMU walk
         * uses - DITBASE + domain*256 + 64 for data + segment*2. */
        {
            uint32_t seg = ((uint32_t)slot->machine.stop_data >> 27) & 0x1Fu;
            uint32_t cap_a = slot->cpu.DITBASE + (uint32_t)slot->cpu.CED * 256u
                           + 64u + seg * 2u;
            uint32_t cap_v = ((uint32_t)ndbus_pool_read8(&s_pool, cap_a) << 8)
                           |  (uint32_t)ndbus_pool_read8(&s_pool, cap_a + 1u);
            LOG(LOG_CAT_MMS, LOG_INFO,
                "MFbus: %s   CED=%u CAD=%u seg=%u data cap@0x%08X=0x%04X "
                "DITBASE=0x%08X dit_configured=%d\n",
                slot->name, (unsigned)slot->cpu.CED, (unsigned)slot->cpu.CAD,
                (unsigned)seg, (unsigned)cap_a, (unsigned)cap_v,
                (unsigned)slot->cpu.DITBASE, slot->cpu.dit_configured);
        }

        /* THE FIRST 24 PST ENTRIES, RAW, AT PSTP.
         *
         * "entry N is zero" only means something if PSTP points at the table
         * SINTRAN actually writes. The physical segment table lives in memory
         * owned by the ND-100 and is written with ORDINARY ND-100 STORES through
         * the shared MPM window - not by a mailbox copy - so a table that reads
         * all zeros says the stores are not landing where we look, while a table
         * with some entries set and others clear says the guest genuinely has not
         * defined those segments. Those are opposite causes and a single entry
         * cannot tell them apart.
         *
         * Ported from RetroCore CpuND500.MMU.cs, which keeps the same dump for the
         * same reason and records measuring a zero entry at psn=11 here. Entries
         * are 32-bit on the ND-5000 (MEASURED on SINTRAN III L over the octobus,
         * the swapper's index page reading 0x000000E9 0x000000EA 0x000000EB); the
         * halfword form is the older ND500 generation's and must not be used here.
         */
        if (slot->cpu.PSTP != 0u)
        {
            char line[120];
            for (uint32_t row = 0; row < 3u; row++)
            {
                int n = snprintf(line, sizeof line, "PST[%2u..%2u]@0x%08X:",
                                 (unsigned)(row * 8u), (unsigned)(row * 8u + 7u),
                                 (unsigned)(slot->cpu.PSTP + row * 32u));
                for (uint32_t k = 0; k < 8u && n > 0 && (size_t)n < sizeof line; k++)
                {
                    uint32_t pa = slot->cpu.PSTP + (row * 8u + k) * 4u;
                    uint32_t v = ((uint32_t)ndbus_pool_read8(&s_pool, pa) << 24)
                               | ((uint32_t)ndbus_pool_read8(&s_pool, pa + 1u) << 16)
                               | ((uint32_t)ndbus_pool_read8(&s_pool, pa + 2u) << 8)
                               |  (uint32_t)ndbus_pool_read8(&s_pool, pa + 3u);
                    n += snprintf(line + n, sizeof line - (size_t)n, " %08X", (unsigned)v);
                }
                LOG(LOG_CAT_MMS, LOG_INFO, "MFbus: %s   %s\n", slot->name, line);
            }
        }

        /* THE PCs THAT LED HERE. A faulting address whose value is wrong needs the
         * loop that computed it, not just the instruction that used it. The CPU keeps
         * a recent-PC ring for exactly this; gated on ND500X_STOPDBG. */
        nd500_dump_pc_ring("swapper fault");

        /* AN ARBITRARY CODE WINDOW (MFBUS_CODEDUMP_PC). The fault report dumps the
         * code around the FAULTING PC, but the value that causes the fault is often
         * written somewhere else entirely - here, by a loop at 0x08008E8C that a byte
         * watch named. Dump that window too, so it can be disassembled from a
         * trace-verified boundary. */
        {
            const char *e = getenv("MFBUS_CODEDUMP_PC");
            if (e != NULL && e[0] != '\0')
            {
                uint32_t want = (uint32_t)strtoul(e, NULL, 0);
                uint32_t pa = nd500_mmu_peek_space(&slot->cpu, want,
                                                   (uint8_t)slot->cpu.CED, 1);
                if (pa != 0xFFFFFFFFu && pa >= 0x20u)
                {
                    char line[100];
                    for (uint32_t row = 0; row < 8u; row++)
                    {
                        uint32_t a = (pa - 0x20u) + row * 16u;
                        int n = snprintf(line, sizeof line, "0x%08X:", (unsigned)a);
                        for (uint32_t k = 0; k < 16u && n > 0 && (size_t)n < sizeof line; k++)
                        {
                            n += snprintf(line + n, sizeof line - (size_t)n, " %02X",
                                          (unsigned)ndbus_pool_read8(&s_pool, a + k));
                        }
                        LOG(LOG_CAT_MMS, LOG_INFO, "MFbus: %s   want %s\n", slot->name, line);
                    }
                }
            }
        }

        /* WAS THE ANSWER INTERRUPT ACTUALLY DELIVERED. RetroCore's own note on this
         * path records "737 answers sent, 0 delivered" on its configuration, so the
         * counters are the only way to tell a delivered GIVEINT from a dropped one.
         * OctobusPhase3MonBringupTests.Samson3Start_ThenMon377_... requires the frame to
         * reach the ND-100 card's receive FIFO after an answered MON stop. */
        LOG(LOG_CAT_MMS, LOG_INFO,
            "MFbus: %s   giveint: frames=%lu no_fabric=%lu no_mailbox=%lu last=0x%04X "
            "lsyspar_w1=0x%04X model_reports=%lu no_store=%lu model=0x%02X ver=0x%04X "
            "running=%d\n",
            slot->name, nd->giveint_frames, nd->giveint_no_fabric, nd->giveint_no_mailbox,
            (unsigned)nd->last_giveint_frame, (unsigned)nd->lsyspar_word1,
            nd->model_reports, nd->report_no_store,
            (unsigned)nd->last_model_report_model, (unsigned)nd->last_model_report_version,
            nd->accp.microprogram_running ? 1 : 0);

        /* THE WHOLE LOOP, NOT JUST THE FAULTING INSTRUCTION. The PC ring shows the
         * fault sits inside a short loop; what the loop DOES decides whether the bad
         * value is a bound, an index or a base. Dump the bytes from a little before
         * the faulting PC so the loop can be disassembled. */
        if (fault_pa != 0xFFFFFFFFu && fault_pa >= 0x120u)
        {
            char line[100];
            for (uint32_t row = 0; row < 22u; row++)
            {
                uint32_t a = (fault_pa - 0x120u) + row * 16u;
                int n = snprintf(line, sizeof line, "0x%08X:", (unsigned)a);
                for (uint32_t k = 0; k < 16u && n > 0 && (size_t)n < sizeof line; k++)
                {
                    n += snprintf(line + n, sizeof line - (size_t)n, " %02X",
                                  (unsigned)ndbus_pool_read8(&s_pool, a + k));
                }
                LOG(LOG_CAT_MMS, LOG_INFO, "MFbus: %s   code %s\n", slot->name, line);
            }
        }

        /* THE FRAME THE LOOP READS ITS COUNTER AND LIMIT FROM. The faulting loop takes
         * its index from b.28 and its bound from b.240, so the frame says whether the
         * index ran away or the bound was wrong. Read through the DATA side, and
         * through the non-faulting peek so the diagnostic cannot fault. */
        {
            char line[100];
            for (uint32_t row = 0; row < 18u; row++)
            {
                uint32_t off = row * 16u;
                uint32_t va = slot->cpu.B + off;
                uint32_t pa = nd500_mmu_peek_space(&slot->cpu, va,
                                                   (uint8_t)slot->cpu.CAD, 0);
                if (pa == 0xFFFFFFFFu)
                {
                    continue;
                }
                int n = snprintf(line, sizeof line, "b.%-3u pa=0x%08X",
                                 (unsigned)off, (unsigned)pa);
                for (uint32_t k = 0; k < 16u && n > 0 && (size_t)n < sizeof line; k++)
                {
                    n += snprintf(line + n, sizeof line - (size_t)n, " %02X",
                                  (unsigned)ndbus_pool_read8(&s_pool, pa + k));
                }
                LOG(LOG_CAT_MMS, LOG_INFO, "MFbus: %s   frame %s\n", slot->name, line);
            }
        }
    }
    (void)nd;

    /* Parked, not dead: stop stepping and wait for the 3TRACO that follows the
     * page-in. Returning false stops the runner; the start class restarts it. */
    return false;
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
 * WHICH HALF OF THE WORD: WRITEMODE_MSB is the HIGH byte. The ND-100 calls the
 * EVEN byte address MSB, and the window's high byte is pool byte 2N - the first
 * of the pair - so ndbus_window_write_msb is the right target and there is no
 * swap here.
 *
 * WHERE THE BYTE COMES FROM IN `value`: the LOW 8 bits, for BOTH modes. The
 * callers put the byte there and let the write mode say which half it lands in -
 * SBYT passes gA (cpu_instr.c), BFILL passes `gA & 0xFF`, MOVB narrows its byte
 * with `& 0xFF` - and the local-RAM path agrees, building the high half with
 * `(value << 8)` (mms_write_physical_memory_wm in cpu_mms.c). Taking the MSB
 * byte from `value >> 8` instead stored 0 for every even-byte write into the
 * shared window, because the high half of `value` is empty on this path. Word
 * writes were unaffected, so a guest booted normally and then failed wherever it
 * had built a string or a byte field in a page that happened to land in the
 * window - measured 30-SEP-2026 as SINTRAN III VSX/500 L refusing every SYSTEM
 * login once the window was reachable through the MMU.
 */
/* WHO ON THE ND-100 SIDE WROTE A POOL CELL. Every ND-500-side watch is blind to this
 * path, so a cell that changes with no ND-500 write logged can only be explained here.
 * Set MFBUS_WWATCH_BYTE to a pool BYTE offset (decimal or 0x hex) to trace writes that
 * touch its word. Bounded so a busy cell cannot flood the log. */
static void mfbus_bank_write_watch(uint32_t word_offset, uint16_t value, WriteMode wm)
{
    static long watch_word = -2;
    static long watch_words = 1;
    /* MFBUS_WWATCH_BUDGET raises this. A watch over a range wide enough to
     * compare several records spends 400 writes on the first of them and then
     * says nothing about the rest, which reads as "nothing wrote the others". */
    ND_DIAG_BUDGET_ENV(bankw, 400, "MFBUS_WWATCH_BUDGET");
    if (watch_word == -2)
    {
        const char *e = getenv("MFBUS_WWATCH_BYTE");
        watch_word = (e != NULL && e[0] != '\0') ? (long)(strtoul(e, NULL, 0) / 2u) : -1;
        /* A RANGE, NOT ONE CELL. MFBUS_WWATCH_LEN is a length in BYTES from
         * MFBUS_WWATCH_BYTE, default 2 - one word, the old behaviour.
         *
         * Added 04-OCT-2026 because a single-cell watch is the wrong shape for
         * the question it keeps being asked. Hunting a corrupted process name,
         * the cell was taken from a PREVIOUS run's pool snapshot; the record had
         * moved by the time the watch ran, so it recorded a neighbouring record's
         * writes and said nothing about the subject. A watch aimed at a structure
         * must cover the structure, or its silence is a fact about the address. */
        const char *l = getenv("MFBUS_WWATCH_LEN");
        if (l != NULL && l[0] != '\0')
        {
            unsigned long bytes = strtoul(l, NULL, 0);
            watch_words = (long)((bytes + 1u) / 2u);
            if (watch_words < 1) { watch_words = 1; }
        }
    }
    if (watch_word < 0 || (long)word_offset < watch_word ||
        (long)word_offset >= watch_word + watch_words)
    {
        return;
    }
    /* THE BUDGET IS SPENT ONLY BY MATCHING WRITES - the address filter runs
     * first - and it announces itself when it runs out. A watch that goes quiet
     * is indistinguishable from a cell nothing writes, and that silence has been
     * read as a finding before. See docs/INVESTIGATION-TRAPS.md section 2 in the
     * nd500x checkout. */
    if (!ND_DIAG_TAKE(bankw))
    {
        return;
    }
    LOG(LOG_CAT_MMS, LOG_INFO,
        "MFbus: ND-100 writes pool word 0x%06X (byte 0x%06X) = 0x%04X mode=%d '%c%c' "
        "from P=%06oB level=%u\n",
        (unsigned)word_offset, (unsigned)(word_offset * 2u), (unsigned)value, (int)wm,
        /* THE CHARACTERS TOO. This watch is used on text fields as often as on
         * pointers, and "0x5329" does not read as the tail of "(SYSTEM)" until
         * someone decodes it by hand. Non-printing bytes show as '.'. */
        (((value >> 8) & 0xFFu) >= 0x20u && ((value >> 8) & 0xFFu) < 0x7Fu)
            ? (char)((value >> 8) & 0xFFu) : '.',
        ((value & 0xFFu) >= 0x20u && (value & 0xFFu) < 0x7Fu)
            ? (char)(value & 0xFFu) : '.',
        /* WHICH INSTRUCTION WROTE IT. Without the ND-100's P this watch can say
         * that a cell was written and what with, but not by what - and "what
         * with" was already visible in the pool. Naming the writer is the whole
         * point of a write watch, and the PC plus the interrupt level is what
         * turns a sequence of values into a routine that can be disassembled.
         *
         * The PC is the address AFTER the instruction was fetched, so a
         * disassembly starts one instruction earlier; printed in ND octal because
         * that is what the SINTRAN listings and the L07 symbol table use. */
        (unsigned)gPC, (unsigned)gPIL);
}

static void mfbus_bank_write(void *ctx, uint32_t word_offset, uint16_t value, WriteMode wm)
{
    (void)ctx;
    mfbus_bank_write_watch(word_offset, value, wm);
    switch (wm)
    {
    case WRITEMODE_MSB:
        (void)ndbus_window_write_msb(&s_window, word_offset, (uint8_t)(value & 0xFF));
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

    /* WE ARE THE EMBEDDER, SO SAY SO BEFORE ANYTHING CAN PRINT. nd500x is both a
     * library and a free-running binary; as the binary it owns stdout and its
     * diagnostics belong there, but in here stdout is the ND-100 GUEST'S console.
     * Measured 30-SEP-2026: an ND-500 MMU line printed inside the ND-500 monitor's
     * own "> Loading Swapper" output on the SINTRAN console. Set at attach, which is
     * the earliest point this side exists. */
    nd500_embedded = 1;

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

    /* ONE LOCK FOR A TEST-AND-SET ON THIS MEMORY. The ND-5000 side takes and
     * releases the mailbox semaphore under ndbus_lock(); the ND-100's TSET and
     * TSETP now do their read-and-write pair under the same mutex, so neither
     * side can land between the two halves of the other. Installed before the
     * window becomes reachable, removed in mfbus_detach(). */
    mms_set_tset_lock(ndbus_lock, ndbus_unlock);

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
        mms_set_tset_lock(NULL, NULL);
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

/**
 * Route the ndbus layer's diagnostics into nd100x's logger.
 *
 * WITHOUT THIS THE ND-5000 SIDE IS SILENT. Every station and the mailbox
 * servicer log through NdbusHostOps::log, and mfbus passed NULL for it, so a
 * declined micro-function, a skipped ring insert or a semaphore that never freed
 * produced no line anywhere - which reads exactly like a run where none of those
 * happened.
 */
/**
 * A start-class mailbox message arrived: start the process on this station's ND-500.
 *
 * This is the ND-5000 arm of RetroCore's INd500ProcessHost.OnStartProcessND5000.
 * On this generation there is NO 21B register image - 3WREG is MSG_ILLEG on the B30
 * - so the whole starting context comes from the per-process CONTEXT BLOCK the
 * microcode's NEWCNTXT loads, and the servicer has already computed which block
 * from the area base SINTRAN patched into control-store cell 0o20.
 *
 * TAKING THE START IS A PROMISE. Returning true tells the servicer to leave the
 * message WAITING and NOT answer it, because the process's own stop answers it
 * later. So this must only return true when a CPU really did begin running: a true
 * with nothing started leaves SINTRAN waiting on an answer that can never come.
 * Returning false is the honest outcome when there is no CPU or it cannot run, and
 * the servicer then answers the way a station with no CPU behind it answers.
 */
/**
 * Report the 256-byte block the servicer saw SINTRAN write trap configuration into.
 *
 * THIS IS A DIAGNOSTIC, NOT THE CAPABILITY TABLE BASE, and it used to be declared
 * as one. RetroCore's Nd500CpuProcessBridge says the same thing about its own
 * equivalent: the bridge does NOT hand a DIT base over, and the base it tracks is
 * learned from the trap-config writes for reporting only. The capability table is
 * found through PS - see mfbus_declare_capability_table() below.
 *
 * MEASURED 30-SEP-2026, which is why this changed: the learned base was 0x73000,
 * its segment-1 capability named physical segment 83, and PST entry 83 was zero, so
 * the first instruction fetch page-faulted and the ND-5000 stopped on an invalid
 * instruction at its own entry point. The PST page then showed 0x73000 to be the
 * segment of PST entry 1 - a DIFFERENT process. This process carried PS = 3, whose
 * PST entry named 0x74000, whose segment-1 capability named physical segment 2.
 */
static void mfbus_declare_dit_base(void *ctx, uint32_t base)
{
    NdbusNd5000 *nd = (NdbusNd5000 *)ctx;

    int slot = mfbus_slot_of(nd->station.number);
    if (slot < 0 || !s_cpus[slot].present)
    {
        return;
    }

    LOG(LOG_CAT_MMS, LOG_INFO,
        "MFbus: %s trap configuration was written into the 256-byte block at 0x%X "
        "(reported, NOT used as the capability table base)\n",
        s_cpus[slot].name, (unsigned)base);
}

/**
 * Point the MMU at this process's capability table, found the way the machine finds
 * it: through PS.
 *
 * PS is an INDEX into the physical segment table, not an address - ND-05.020.01
 * section 6.6: "This register points to an element of the Physical Segment Table.
 * The PST element addresses the process segment of the process." The capability of
 * a logical segment then lies at process_segment + CED*256 + (program ? 0 : 64) +
 * segment*2, which is the formula nd500x's walk already uses; only the base was
 * coming from the wrong place.
 *
 * PS comes from the context block, so this runs AFTER mfbus_load_context().
 */
static void mfbus_declare_capability_table(MfbusCpuSlot *c)
{
    uint32_t ps = ndbus_context_read(&c->context, NDBUS_CTX_SRF13) & 0x1FFFu;
    uint32_t base = 0u;

    if (nd500_mmu_declare_process_segment(&c->cpu, ps, &base) != 0)
    {
        LOG(LOG_CAT_MMS, LOG_WARN,
            "MFbus: %s PS=%u does not resolve to a process segment - the capability walk "
            "will fall back to the emulator's own table\n",
            c->name, (unsigned)ps);
        return;
    }

    /* AND THE DIT-SOURCED REGISTERS, HERE AND NOT IN THE CONTEXT LOAD. THA is one
     * of them: it has a slot in the context block but NEWCNTXT does not read it
     * from there, so it must come from the DIT - and the DIT is this process
     * segment, which is only known on the line above.
     *
     * MEASURED, because the first version of this read sat in mfbus_load_context
     * and was therefore one process out of date: a run printed PS=0xA with
     * DIT=0x74000 and PS=3 with DIT=0x8C000, while this function resolved PS=3 to
     * 0x74000 and PS=10 to 0x8C000 - exactly crossed. PST[3]=0x000000E8 and
     * PST[10]=0x00000118 are both mode 0, so there is no ambiguity in the
     * resolution itself; the read was simply happening before the base was set,
     * and so took the PREVIOUS process's PCB. SINTRAN writes segment 10's trap
     * handler address (0x08001628) into the PCB at 0x8C000, and the process that
     * needs it carries PS=10.
     *
     * The other DIT-sourced registers (CES, CAS, the trap enables) are NOT done
     * here: each needs its own evidence, and LL/HL come from TRAPSET. */
    c->cpu.THA = nd500_dit_read_tha(&c->cpu, c->cpu.CED);

    LOG(LOG_CAT_MMS, LOG_INFO,
        "MFbus: %s capability table := 0x%X from PS=%u via PSTP, THA=0x%08X\n",
        c->name, (unsigned)base, (unsigned)ps, (unsigned)c->cpu.THA);
}

/*
 * The ND-5000 made a monitor call, and SINTRAN on this ND-100 owns it.
 *
 * NOT SERVED HERE AND NOT SERVED BY nd500x EITHER. The record goes into the
 * calling process's own activation message, SINTRAN performs the call, and the
 * process is restarted with 3MONCO (24B) - which the servicer already routes
 * through the start class. The process PARKS meanwhile, exactly as it does on a
 * retryable trap, and for the same reason: there is nothing for it to run until
 * an answer comes back.
 *
 * A MONITOR CALL RESUMES AFTER THE CALL, NOT ON IT. That is the opposite of a page
 * fault or a protect violation, where the instruction did not complete.
 * pending_call_return_address is the address the CALL already computed.
 *
 * Returns nonzero when the call has been taken, which is what tells nd500x not to
 * run its own emulation.
 */
static int mfbus_mon_call(void *ctx, uint32_t mon_number, uint32_t arg_count,
                          const uint32_t *arg_addresses, uint32_t *out_resolved)
{
    MfbusCpuSlot *c = (MfbusCpuSlot *)ctx;

    int nd_index = (int)(c - s_cpus);
    if (nd_index < 0 || nd_index >= s_nd5000_count || c->loaded_x5cpu < 0)
    {
        return 0;   /* not ours - let the local seam say what it says */
    }
    NdbusNd5000 *nd = &s_nd5000[nd_index];

    /* The ARGUMENT VALUES as well as their addresses. SINTRAN reads both, and the
     * value slots are not decoration: on the swapper path one of them IS
     * SINTRAN's own SWPINFO cell. Read them through a NON-FAULTING peek - a
     * monitor call must not turn into a page fault while being reported. */
    uint32_t values[NDBUS_MON_MAX_ARGS];
    uint32_t count = (arg_count > NDBUS_MON_MAX_ARGS) ? NDBUS_MON_MAX_ARGS : arg_count;
    for (uint32_t k = 0; k < count; k++)
    {
        uint32_t va = (arg_addresses != NULL) ? arg_addresses[k] : 0u;
        values[k] = 0u;
        if (va != 0u)
        {
            uint32_t pa = nd500_mmu_peek_space(&c->cpu, va, (uint8_t)c->cpu.CED, 0);
            if (pa != 0xFFFFFFFFu)
            {
                values[k] = ((uint32_t)ndbus_pool_read8(&s_pool, pa) << 24)
                          | ((uint32_t)ndbus_pool_read8(&s_pool, pa + 1u) << 16)
                          | ((uint32_t)ndbus_pool_read8(&s_pool, pa + 2u) << 8)
                          |  (uint32_t)ndbus_pool_read8(&s_pool, pa + 3u);
            }
        }
    }

    uint32_t resume = c->cpu.pending_call_return_address;

    /* PARK AND SAVE BEFORE THE ANSWER GOES OUT - the order RetroCore uses
     * (CpuND500.IndirectSegments.cs "PARK BEFORE THE SINK ANSWERS", and
     * Nd500CpuProcessBridge.OnMonitorCall saves the block before it answers).
     * The answer interrupts the ND-100, which runs on another host thread and can
     * post the restart at once; everything that restart reads - the parked flag,
     * P, the context block - has to be in place first.
     *
     * Returning INDIRECT_HANDLED tells the CPU the call is done and to carry on
     * from the resume address, so the park cannot be expressed by that return
     * value alone; and the runner does not test machine.run_flag. The parked flag
     * is what stops it, at the top of the next step.
     *
     * SAVE THE BLOCK ON THE WAY OUT - CNTXTSAVE, which the microcode performs on
     * the stop and not only on a later switch. MEASURED on PLACE-DOMAIN CPU-STAT:
     * without it the swapper parked at P=0x08008255 while its block kept its ENTRY
     * POINT 0x08000004, and a later switch back restarted it from there. c->context
     * is attached to the loaded process's own block, so this writes the right one. */
    uint32_t pc_before = c->cpu.PC;
    c->cpu.PC = resume;
    if (!mfbus_save_context(c))
    {
        LOG(LOG_CAT_MMS, LOG_WARN,
            "MFbus: %s monitor-call park could not save the context block - a later switch "
            "back to this process would resume it from a stale P\n",
            c->name);
    }
    /* Engine lock first, then the park mark - see the trap path for why. The
     * refusal is undone inside the lock, so the ND-100 thread never sees a
     * "leaving" mark on a thread that is going to keep running. */
    ndbus_engine_lock();
    c->parked = true;
    __atomic_store_n(&c->runner_leaving, 1u, __ATOMIC_RELEASE);

    if (!ndbus_servicer_answer_monitor_call(&nd->servicer, (uint16_t)c->loaded_x5cpu, resume,
                                           (uint16_t)mon_number, count, arg_addresses, values))
    {
        /* The servicer said why. Do NOT claim a call that was not posted - letting
         * the local seam report it is more honest than a silent hang. Nothing was
         * told to SINTRAN, so the process is not parked and the thread goes on. */
        c->parked = false;
        __atomic_store_n(&c->runner_leaving, 0u, __ATOMIC_RELEASE);
        ndbus_engine_unlock();
        c->cpu.PC = pc_before;
        return 0;
    }
    ndbus_engine_unlock();

    c->mon_calls++;
    if (c->mon_calls <= MFBUS_MON_LOG_LIMIT)
    {
        LOG(LOG_CAT_MMS, LOG_INFO,
            "MFbus: %s monitor call MON %oB argc=%u resume=0x%X after %lu instruction(s) - "
            "reported to SINTRAN\n",
            c->name, (unsigned)mon_number, (unsigned)count, (unsigned)resume,
            c->steps_since_resume);
    }
    else
    {
        /* PAST THE DETAILED LIMIT, STILL ONE SHORT LINE PER CALL. The limit above
         * was spent on the swapper's first 36 calls, so a domain's own calls -
         * its file opens, its exit - left no trace at all, and "did it reach
         * MON 0B" could not be answered from the log. Cheap: one line per
         * monitor call, and a monitor call is a full round trip to SINTRAN. */
        LOG(LOG_CAT_MMS, LOG_INFO, "MFbus: %s MON %oB X5CPU=%d argc=%u resume=0x%X\n",
            c->name, (unsigned)mon_number, c->loaded_x5cpu, (unsigned)count, (unsigned)resume);
    }

    /* WHAT THE SWAPPER ACTUALLY ASKED FOR.
     *
     * SWPFU and its arguments are the swapper's request; argc alone says nothing
     * about which page of which segment it wants. SINTRAN answers a bad request
     * with one of its own MON error codes rather than a transfer - measured,
     * PLACE-DOMAIN CPU-STAT ends with error 1030 ADDRESS OUTSIDE DATA SEGMENT for
     * logical address 1 224030B on physical segment 15D - and the only way to tell
     * a wrong request from a correct request about a wrong address is to read the
     * arguments. Bounded by the same limit as the rest of the monitor-call log. */
    if (c->mon_calls <= MFBUS_MON_LOG_LIMIT)
    {
        char line[200];
        int n = snprintf(line, sizeof line, "MON %oB args:", (unsigned)mon_number);
        for (uint32_t k = 0; k < count && n > 0 && (size_t)n < sizeof line; k++)
        {
            n += snprintf(line + n, sizeof line - (size_t)n, " [%u]@0x%08X=0x%08X",
                          (unsigned)k,
                          (unsigned)((arg_addresses != NULL) ? arg_addresses[k] : 0u),
                          (unsigned)values[k]);
        }
        LOG(LOG_CAT_MMS, LOG_INFO, "MFbus: %s   %s\n", c->name, line);
    }

    /* THE SWAPPER'S PER-SEGMENT DESCRIPTORS, READ IN THE SWAPPER'S OWN CONTEXT.
     *
     * They live at logical 0x08038000 with a stride of 0o144 = 100 DECIMAL bytes;
     * the carve's anchor is PSN 14 at 0x08038578, and 0x08038000 + 14*100 = 0x08038578
     * confirms the stride. The packed STATE word is at descriptor offset 0o36,
     * decoded big-endian as (halfword >> 10) & 0xF - measured examples elsewhere:
     * 0xDE00 -> 7, 0xE580 -> 9, 0xEB2C -> 0xA.
     *
     * READ HERE, NOT AT A TRAP PARK. This address belongs to the SWAPPER's data, and
     * at a domain's page-fault park the domain is the loaded process, so the table
     * does not translate at all - measured, every psn reported "does not translate".
     * On a MON 377B the swapper IS the loaded process, so its own mapping is in
     * force.
     *
     * WHY EVERY SEGMENT, NOT JUST THE FAULTING ONE. The carved finding is that DISK
     * BACKING decides the outcome, not STATE: a segment with pages services the fault
     * via LNEWSWAP whatever its STATE, while a fresh UNBACKED writable segment must
     * take the grow path, which requires STATE in {13,14,15}. A STATE read for the
     * failing segment alone therefore proves nothing - it has to be read beside ones
     * that work. Here psn 11 and 12 back fine; psn 13, the scratch segment GSWSP
     * connected for the domain, never receives an LSWPAGE and SWPFU=4 LALLOPAGE
     * never fires at all. */
    /* SAMPLED EARLY AND LATE. The first gate was mon_calls <= 2 and every descriptor
     * read as 32 zero bytes - for psn 11 and 12 as well, which demonstrably back
     * fine. That is the table not yet populated, not a defect: the swapper's first
     * monitor calls happen before any segment is set up. The psn-13 fault arrives
     * around the fifteenth call, so sample both ends and let the pair show when the
     * table fills. */
    if (mon_number == 0377u
        && (c->mon_calls <= 2u || (c->mon_calls >= 13u && c->mon_calls <= 20u)))
    {
        for (uint32_t psn = 10u; psn <= 15u; psn++)
        {
            uint32_t dl = 0x08038000u + psn * 100u;
            uint32_t dp = nd500_mmu_peek_space(&c->cpu, dl, (uint8_t)c->cpu.CED, 0);
            if (dp == 0xFFFFFFFFu)
            {
                LOG(LOG_CAT_MMS, LOG_INFO,
                    "MFbus: %s   seg-desc psn=%2u logical 0x%08X does not translate\n",
                    c->name, (unsigned)psn, (unsigned)dl);
                continue;
            }
            char hex[80];
            int n = 0;
            for (uint32_t k = 0; k < 32u && n >= 0 && (size_t)n < sizeof hex; k++)
            {
                n += snprintf(hex + n, sizeof hex - (size_t)n, "%02X",
                              (unsigned)ndbus_pool_read8(&s_pool, dp + k));
            }
            uint32_t w36 = ((uint32_t)ndbus_pool_read8(&s_pool, dp + 036u) << 8)
                         |  (uint32_t)ndbus_pool_read8(&s_pool, dp + 036u + 1u);
            uint32_t st = (w36 >> 10) & 0x0Fu;
            LOG(LOG_CAT_MMS, LOG_INFO,
                "MFbus: %s   seg-desc psn=%2u pa=0x%08X w36=0x%04X STATE=0x%X grow=%d : %s\n",
                c->name, (unsigned)psn, (unsigned)dp, (unsigned)w36, (unsigned)st,
                (st >= 13u && st <= 15u) ? 1 : 0, hex);
        }
    }

    /* SNAPSHOT THE POOL AT A CHOSEN MONITOR CALL, so the state can be examined
     * again without another boot.
     *
     * MFBUS_SNAPSHOT_PATH names the file and MFBUS_SNAPSHOT_AT_MON the monitor
     * call to take it at, counted per CPU. Everything this lane argues about -
     * the mailbox, the message blocks, the segment descriptors, the page tables -
     * is in the pool, so one capture answers the questions that otherwise cost
     * eight minutes each. Taken once, and it says so, because a snapshot that
     * silently did not happen is the same trap as a silent diagnostic.
     *
     * Load it with ndbus_pool_snapshot_load(); a snapshot from a differently
     * sized pool is refused rather than misread. */
    {
        static int snap_at = -1;
        static const char *snap_path = NULL;
        static int snap_done = 0;
        if (snap_at == -1)
        {
            const char *e = getenv("MFBUS_SNAPSHOT_AT_MON");
            snap_at = (e != NULL && e[0] != '\0') ? (int)strtol(e, NULL, 0) : 0;
            snap_path = getenv("MFBUS_SNAPSHOT_PATH");
        }
        if (!snap_done && snap_at > 0 && snap_path != NULL && snap_path[0] != '\0'
            && (long)c->mon_calls >= (long)snap_at)
        {
            snap_done = 1;
            if (ndbus_pool_snapshot_save(&s_pool, snap_path))
            {
                LOG(LOG_CAT_MMS, LOG_INFO,
                    "MFbus: %s pool snapshot written to %s at monitor call %lu "
                    "(MON %oB)\n",
                    c->name, snap_path, c->mon_calls, (unsigned)mon_number);
            }
            else
            {
                LOG(LOG_CAT_MMS, LOG_ERROR,
                    "MFbus: %s pool snapshot to %s FAILED - nothing was written\n",
                    c->name, snap_path);
            }
        }
    }

    if (out_resolved != NULL)
    {
        *out_resolved = resume;
    }
    return 1;
}

/*
 * Put a parked process back on the CPU.
 *
 * A PARK STOPS THE RUNNER'S THREAD, AND A STOPPED RUNNER MUST BE JOINED BEFORE IT
 * RUNS AGAIN. mfbus_cpu_step() returns false to park, which takes the runner to
 * NDBUS_RUNNER_STOPPED - not IDLE - and ndbus_runner_start() refuses to start a
 * runner whose previous thread has not been reaped.
 *
 * MEASURED 30-SEP-2026: the resume path tested for IDLE only, so after the swapper's
 * first monitor call the restart silently did nothing. The process was never stepped
 * again - no stop reason, no park, no instructions - and the periodic P sample that
 * would have named a spin printed nothing either, because there was no spin. SINTRAN
 * polled 3RMICV for the rest of the run.
 */
/* SAVE ONE PROCESS'S REGISTERS-OUTSIDE-THE-BLOCK AND RESTORE ANOTHER'S.
 *
 * Ported from RetroCore Nd500CpuProcessBridge.SwapPendingCallState. The trap
 * enables OTE/CTE/MTE/TEMM are not in the context block and the load does not
 * refill them from the PCB on a switch (that reset a running program's own
 * `ote1:=` to SINTRAN's static zeros), so they travel here. A process seen for
 * the first time has no snapshot and keeps the live values. The pending CALL
 * (return address, argument count, argument addresses) travels the same way.
 * The reference also carries a "call anchor"; this CPU has no such field. */
static void mfbus_swap_process_state(MfbusCpuSlot *c, int from_x5cpu, int to_x5cpu)
{
    if (from_x5cpu == to_x5cpu)
    {
        return;
    }

    if (from_x5cpu >= 0 && (from_x5cpu + 1) < MFBUS_PROCESS_SLOTS)
    {
        MfbusProcessState *f = &c->proc[from_x5cpu + 1];
        f->trap_enables[0] = c->cpu.OTE1;
        f->trap_enables[1] = c->cpu.OTE2;
        f->trap_enables[2] = c->cpu.CTE1;
        f->trap_enables[3] = c->cpu.CTE2;
        f->trap_enables[4] = c->cpu.MTE1;
        f->trap_enables[5] = c->cpu.MTE2;
        f->trap_enables[6] = c->cpu.TEMM1;
        f->trap_enables[7] = c->cpu.TEMM2;
        f->have_trap_enables = true;

        uint32_t argc = c->cpu.pending_call_arg_count;
        if (argc > (uint32_t)TRAP_SEQ_MAXARG)
        {
            argc = (uint32_t)TRAP_SEQ_MAXARG;
        }
        f->call_arg_count = c->cpu.pending_call_arg_count;
        f->call_return = c->cpu.pending_call_return_address;
        for (uint32_t i = 0; i < argc; i++)
        {
            f->call_args[i] = c->cpu.pending_call_arg_addresses[i];
        }
    }

    if (to_x5cpu >= 0 && (to_x5cpu + 1) < MFBUS_PROCESS_SLOTS)
    {
        const MfbusProcessState *t = &c->proc[to_x5cpu + 1];
        if (t->have_trap_enables)
        {
            c->cpu.OTE1 = t->trap_enables[0];
            c->cpu.OTE2 = t->trap_enables[1];
            c->cpu.CTE1 = t->trap_enables[2];
            c->cpu.CTE2 = t->trap_enables[3];
            c->cpu.MTE1 = t->trap_enables[4];
            c->cpu.MTE2 = t->trap_enables[5];
            c->cpu.TEMM1 = t->trap_enables[6];
            c->cpu.TEMM2 = t->trap_enables[7];
        }

        uint32_t argc = t->call_arg_count;
        if (argc > (uint32_t)TRAP_SEQ_MAXARG)
        {
            argc = (uint32_t)TRAP_SEQ_MAXARG;
        }
        for (uint32_t i = 0; i < argc; i++)
        {
            c->cpu.pending_call_arg_addresses[i] = t->call_args[i];
        }
        c->cpu.pending_call_return_address = t->call_return;
        c->cpu.pending_call_arg_count = t->call_arg_count;
    }
}

/* A START in a slot is a new program. Ported from RetroCore
 * Nd500CpuProcessBridge.NoteLoadedProcess: the previous program's trap-enable
 * snapshot must not come back, the outgoing process's state is saved, and the
 * started process begins with no CALL in flight. */
static void mfbus_note_started_process(MfbusCpuSlot *c, int x5cpu)
{
    if (x5cpu >= 0 && (x5cpu + 1) < MFBUS_PROCESS_SLOTS)
    {
        c->proc[x5cpu + 1].have_trap_enables = false;
    }
    mfbus_swap_process_state(c, c->loaded_x5cpu, x5cpu);
    c->cpu.pending_call_return_address = 0u;
    c->cpu.pending_call_arg_count = 0u;
}

/* THE LIMITS, THE TRAP HANDLER AND THE TRAP ENABLES THIS PROCESS WAS GIVEN.
 *
 * Ported from RetroCore CpuND500.LoadDomainStateFromProcessSegment, which
 * Nd500CpuProcessBridge.OnStartProcessND5000 calls right after the context
 * block load on a 3START: the block sets PS and CED, and the domain
 * information table on that process segment holds TOS, LL, HL, THA and the
 * four trap-enable pairs. The offsets are the reference's LOADCT_* reads,
 * DPA = table + domain*256 + 0x80: TOS +0x3C, LL +0x40, HL +0x44, THA +0x36,
 * OTE +0x16/+0x1A, CTE +0x1E/+0x22, MTE +0x26/+0x2A, TEMM +0x2E/+0x32.
 *
 * START ONLY. On a switch back to a parked process the reference loads THA
 * alone, because reloading the enables there reset a running program's own
 * `ote1:=` to SINTRAN's static zeros.
 *
 * The block's own TOS and LL are logged beside the table's, because which of
 * the two is right for a domain's first start was an open question here. */
static void mfbus_load_domain_state_at_start(MfbusCpuSlot *c)
{
    uint32_t table = c->cpu.DITBASE;
    if (table == 0u)
    {
        LOG(LOG_CAT_MMS, LOG_WARN,
            "MFbus: %s DOMAIN STATE NOT LOADED - no process table base for PS=0x%X; TOS, LL, "
            "HL, THA and the trap enables keep their previous values\n",
            c->name, (unsigned)c->cpu.PS);
        return;
    }

    uint32_t dpa = table + ((uint32_t)c->cpu.CED * 256u) + 0x80u;
    uint32_t block_tos = c->cpu.TOS;
    uint32_t block_ll = c->cpu.LL;

    c->cpu.TOS = ndbus_pool_read32(&s_pool, dpa + 0x3Cu);
    c->cpu.LL  = ndbus_pool_read32(&s_pool, dpa + 0x40u);
    c->cpu.HL  = ndbus_pool_read32(&s_pool, dpa + 0x44u);
    c->cpu.THA = ndbus_pool_read32(&s_pool, dpa + 0x36u);

    c->cpu.OTE1  = ndbus_pool_read32(&s_pool, dpa + 0x16u);
    c->cpu.OTE2  = ndbus_pool_read32(&s_pool, dpa + 0x1Au);
    c->cpu.CTE1  = ndbus_pool_read32(&s_pool, dpa + 0x1Eu);
    c->cpu.CTE2  = ndbus_pool_read32(&s_pool, dpa + 0x22u);
    c->cpu.MTE1  = ndbus_pool_read32(&s_pool, dpa + 0x26u);
    c->cpu.MTE2  = ndbus_pool_read32(&s_pool, dpa + 0x2Au);
    c->cpu.TEMM1 = ndbus_pool_read32(&s_pool, dpa + 0x2Eu);
    c->cpu.TEMM2 = ndbus_pool_read32(&s_pool, dpa + 0x32u);

    LOG(LOG_CAT_MMS, LOG_INFO,
        "MFbus: %s start: domain state from table 0x%X CED=%u: TOS=0x%08X (block had 0x%08X) "
        "LL=0x%08X (block had 0x%08X) HL=0x%08X THA=0x%08X OTE=0x%08X%08X MTE=0x%08X%08X\n",
        c->name, (unsigned)table, (unsigned)c->cpu.CED, (unsigned)c->cpu.TOS,
        (unsigned)block_tos, (unsigned)c->cpu.LL, (unsigned)block_ll, (unsigned)c->cpu.HL,
        (unsigned)c->cpu.THA, (unsigned)c->cpu.OTE2, (unsigned)c->cpu.OTE1,
        (unsigned)c->cpu.MTE2, (unsigned)c->cpu.MTE1);
}

/* WAIT FOR A HOST THREAD THAT IS ON ITS WAY OUT. ND-100 thread only.
 *
 * The ND-5000 thread marks itself as leaving before it tells SINTRAN about a
 * stop. SINTRAN can answer before that thread has returned from its step - it
 * may still be writing its log lines, which read the CPU registers. Everything
 * on the ND-100 thread that changes those registers, or starts a new thread,
 * calls this first. The join is short: the thread has nothing left to do but
 * return.
 *
 * RetroCore has no such wait because its ND-5000 thread never ends - it parks on
 * a wake event and the restart sets that event. Here a park ends the thread, so
 * the restart has to see it out. */
static void mfbus_quiesce_runner(MfbusCpuSlot *c)
{
    /* Wait for the thread to reach STOPPED, not blindly join: the monitor-call
     * path sets the mark before it knows whether the servicer will accept the
     * answer, and takes it back if not - in that case the thread keeps running
     * and a plain join would wait for ever. */
    while (__atomic_load_n(&c->runner_leaving, __ATOMIC_ACQUIRE) != 0u)
    {
        NdbusRunnerState state = ndbus_runner_state(&c->runner);
        if (state == NDBUS_RUNNER_STOPPED || state == NDBUS_RUNNER_IDLE)
        {
            ndbus_runner_join(&c->runner);
            __atomic_store_n(&c->runner_leaving, 0u, __ATOMIC_RELEASE);
            return;
        }
        (void)sched_yield();
    }
}

static bool mfbus_resume_runner(MfbusCpuSlot *c, uint8_t station_number)
{
    /* A thread that has reported a stop is finishing, not running. */
    mfbus_quiesce_runner(c);

    NdbusRunnerState state = ndbus_runner_state(&c->runner);

    if (state == NDBUS_RUNNER_RUNNING || state == NDBUS_RUNNER_STOPPING)
    {
        return true;   /* already going */
    }

    if (state == NDBUS_RUNNER_STOPPED)
    {
        /* Reap the previous thread. Asks and waits, so the pool it was reading
         * outlives it. */
        ndbus_runner_stop_and_join(&c->runner);
    }

    return mfbus_start_nd5000(station_number);
}

/*
 * CNTXTSAVE - write the live registers back into the attached context block.
 *
 * The exact mirror of mfbus_load_context(): the same fields, in the same order.
 * Any field the load does NOT read must not be written either, because the
 * block's domain registers (TOS, LL, HL, THA, CES, CAS and the trap enables)
 * come from the Domain Information Table and the microcode never copies them -
 * ndbus_context_field_is_loaded() is the single statement of that list.
 *
 * Until this existed the save side was a stated gap, and the trap park said so:
 * "WRITING THE BLOCK BACK TOO IS NOT DONE HERE ... It only matters once a
 * DIFFERENT process is loaded in between, because that is the only case where
 * the block is read again." PLACE-DOMAIN is that case.
 */
static bool mfbus_save_context(MfbusCpuSlot *c)
{
    if (!c->context_set)
    {
        return false;
    }

    /* CNTXTSAVE, over s_ctx_fields. A row with saved=false is skipped - see the
     * table's note on direction; everything else goes back exactly as the load
     * will read it, because it is the same list. */
    bool ok = true;
    for (size_t k = 0; k < MFBUS_CTX_FIELD_COUNT; k++)
    {
        const MfbusCtxField *f = &s_ctx_fields[k];
        if (!f->saved)
        {
            continue;
        }
        uint32_t value = *mfbus_ctx_reg(&c->cpu, f) & f->mask;
        if (f->in_block_low_half)
        {
            /* Only the low halfword is ours; the rest of the word belongs to the
             * block and overwriting it would zero whatever else it carries. */
            uint32_t slot = ndbus_context_read(&c->context, f->offset);
            value = (slot & 0xFFFF0000u) | value;
        }
        ok = ndbus_context_write(&c->context, f->offset, value) && ok;
    }

    return ok;
}

/*
 * THE CONTEXT SWITCH - microcode 011473B, on the prologue that 3START, 3TRACO,
 * 3MONCO and 3WMONCO all enter (010207B/010210B):
 *
 *   011473  is AM#10 == AM#22 ?    loaded process vs wanted process
 *   011474  if same -> POPRET      save AND load both skipped
 *           else CALL 010330B      SAVE  to AM#10 << 8
 *   011477  CALL 010337B           LOAD from AM#22 << 8
 *   011500  AM#10 := AM#22
 *
 * So a monitor-call restart naming a DIFFERENT process switches context exactly
 * as a start does.
 *
 * MEASURED on PLACE-DOMAIN CPU-STAT, and the reference records the same run: the
 * domain page-faults at its entry and parks, SINTRAN answers with a 3MONCO for
 * the SWAPPER, and without the switch that restart ran whichever registers
 * happened to be loaded - the swapper's block was reloaded from its ENTRY POINT
 * 0x08000004 instead of resuming at its monitor-call return 0x08008255, so it
 * re-ran its initialisation, lost the page-fault work order and asked the
 * identical MON 377B again.
 *
 * Returns false when the wanted process has no usable context block, so the
 * caller can decline rather than resume something arbitrary.
 */
static bool mfbus_switch_to_process(NdbusNd5000 *nd, MfbusCpuSlot *c, int wanted_x5cpu)
{
    if (wanted_x5cpu < 0)
    {
        return false;
    }
    if (c->loaded_x5cpu == wanted_x5cpu)
    {
        return true;   /* 011474B true side: nothing to do */
    }

    int x5 = ndbus_cpu_context_x5cpu(nd->station.number);

    if (c->loaded_x5cpu >= 0)
    {
        uint32_t loaded_byte =
            ndbus_servicer_process_context_byte(&nd->servicer, (uint16_t)c->loaded_x5cpu);
        if (loaded_byte == 0u
            || !ndbus_context_attach(&c->context, &s_pool,
                                     loaded_byte - NDBUS_CTX_STRIDE_BYTES
                                         - ((uint32_t)x5 * NDBUS_CTX_STRIDE_BYTES),
                                     x5))
        {
            LOG(LOG_CAT_MMS, LOG_ERROR,
                "MFbus: %s context switch %d -> %d: the loaded process has no block to save "
                "into\n",
                c->name, c->loaded_x5cpu, wanted_x5cpu);
            return false;
        }
        c->context_set = true;
        if (!mfbus_save_context(c))
        {
            LOG(LOG_CAT_MMS, LOG_ERROR,
                "MFbus: %s context switch %d -> %d: the save failed, so the switch is refused "
                "rather than losing the process\n",
                c->name, c->loaded_x5cpu, wanted_x5cpu);
            return false;
        }
        LOG(LOG_CAT_MMS, LOG_INFO,
            "MFbus: %s CONTEXT SAVE X5CPU=%d P=0x%X B=0x%X R=0x%X PS=0x%X\n", c->name,
            c->loaded_x5cpu, (unsigned)c->cpu.PC, (unsigned)c->cpu.B, (unsigned)c->cpu.R,
            (unsigned)c->cpu.PS);
    }

    uint32_t wanted_byte =
        ndbus_servicer_process_context_byte(&nd->servicer, (uint16_t)wanted_x5cpu);
    if (wanted_byte == 0u
        || !ndbus_context_attach(&c->context, &s_pool,
                                 wanted_byte - NDBUS_CTX_STRIDE_BYTES
                                     - ((uint32_t)x5 * NDBUS_CTX_STRIDE_BYTES),
                                 x5))
    {
        LOG(LOG_CAT_MMS, LOG_ERROR,
            "MFbus: %s context switch %d -> %d: the wanted process has no block\n", c->name,
            c->loaded_x5cpu, wanted_x5cpu);
        return false;
    }
    c->context_set = true;

    /* A BLOCK WHOSE P IS ZERO IS NOT A PROCESS - the same refusal the start path
     * makes, for the same reason: resuming at address 0 would fault somewhere
     * unrelated and read as a different bug. */
    if (ndbus_context_read(&c->context, NDBUS_CTX_P) == 0u)
    {
        LOG(LOG_CAT_MMS, LOG_WARN,
            "MFbus: %s context switch %d -> %d refused - the wanted block has P = 0\n",
            c->name, c->loaded_x5cpu, wanted_x5cpu);
        return false;
    }

    /* The load needs the runner at IDLE - a thread already reaped, so the pool it
     * was reading outlives it. A switch only ever happens for a process that is
     * PARKED, so its thread has already returned and sits at STOPPED; the caller's
     * guard is what guarantees that. A process genuinely executing instructions
     * cannot have its registers swapped from this thread at all, which is why the
     * reference refuses that case outright rather than trying. */
    if (ndbus_runner_state(&c->runner) != NDBUS_RUNNER_IDLE)
    {
        ndbus_runner_stop_and_join(&c->runner);
    }

    if (!mfbus_load_context(nd->station.number))
    {
        return false;
    }
    mfbus_declare_capability_table(c);

    LOG(LOG_CAT_MMS, LOG_INFO,
        "MFbus: %s CONTEXT SWITCH X5CPU=%d -> %d (P=0x%X PS=0x%X CED=0x%X B=0x%X R=0x%X)\n",
        c->name, c->loaded_x5cpu, wanted_x5cpu, (unsigned)c->cpu.PC, (unsigned)c->cpu.PS,
        (unsigned)c->cpu.CED, (unsigned)c->cpu.B, (unsigned)c->cpu.R);

    mfbus_swap_process_state(c, c->loaded_x5cpu, wanted_x5cpu);
    c->loaded_x5cpu = wanted_x5cpu;
    return true;
}

/**
 * Read ND-500 DATA memory through the MMU, for the servicer's inline user buffer.
 *
 * Ported from RetroCore INd500ProcessHost.TryReadDataBytes
 * (Emulated.HW/ND/CPU/ND500/Servicer/INd500ProcessHost.cs:75).
 *
 * TRANSLATED ONE BYTE AT A TIME, DELIBERATELY. A buffer can straddle a page
 * boundary, and the two pages need not be adjacent in the pool, so translating the
 * first byte and walking forward would read a neighbour's page for the tail. The
 * ceiling is 0o4000 bytes, so the cost is bounded and small.
 *
 * ANY byte that does not translate fails the whole read. A partial buffer printed
 * as text is a wrong answer that looks like an answer.
 */
static bool mfbus_read_nd500_data_bytes(void *ctx, uint32_t logical_address,
                                        uint8_t *destination, uint32_t count)
{
    NdbusNd5000 *nd = (NdbusNd5000 *)ctx;
    if (nd == NULL || destination == NULL)
    {
        return false;
    }

    int slot = mfbus_slot_of(nd->station.number);
    if (slot < 0 || !s_cpus[slot].present)
    {
        return false;
    }
    MfbusCpuSlot *c = &s_cpus[slot];

    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t pa = nd500_mmu_peek_space(&c->cpu, logical_address + i,
                                           (uint8_t)c->cpu.CED, 0);
        if (pa == 0xFFFFFFFFu)
        {
            LOG(LOG_CAT_MMS, LOG_INFO,
                "MFbus: %s inline buffer read STOPPED at byte %u of %u - logical 0x%08X "
                "does not translate in domain %u\n",
                c->name, (unsigned)i, (unsigned)count,
                (unsigned)(logical_address + i), (unsigned)c->cpu.CED);
            return false;
        }
        destination[i] = ndbus_pool_read8(&s_pool, pa);
    }

    return true;
}

/**
 * Write ND-500 DATA memory through the MMU, for MICFU 11B DMEMWR.
 *
 * Ported from RetroCore Nd500CpuProcessBridge.TryWriteDataBytes
 * ($RETROCORE/Emulated.HW/ND/CPU/ND500/Servicer/Nd500CpuProcessBridge.cs), the
 * mirror of the read above: translated one byte at a time with a translate that
 * never traps, because the range is an arbitrary byte range that may straddle a
 * page. The reference reads the 32-bit word, replaces one byte and writes the
 * word back; the pool here is addressed by byte, so storing the byte is the same
 * operation.
 *
 * A byte that does not translate fails the write. As in the reference, bytes
 * already placed before it stay written; the servicer answers the failure to
 * SINTRAN, so it is seen and not a silent short write.
 */
static bool mfbus_write_nd500_data_bytes(void *ctx, uint32_t logical_address,
                                         const uint8_t *source, uint32_t count)
{
    NdbusNd5000 *nd = (NdbusNd5000 *)ctx;
    if (nd == NULL || source == NULL)
    {
        return false;
    }

    int slot = mfbus_slot_of(nd->station.number);
    if (slot < 0 || !s_cpus[slot].present)
    {
        return false;
    }
    MfbusCpuSlot *c = &s_cpus[slot];

    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t pa = nd500_mmu_peek_space(&c->cpu, logical_address + i,
                                           (uint8_t)c->cpu.CED, 0);
        if (pa == 0xFFFFFFFFu)
        {
            LOG(LOG_CAT_MMS, LOG_INFO,
                "MFbus: %s data write STOPPED at byte %u of %u - logical 0x%08X does not "
                "translate in domain %u\n",
                c->name, (unsigned)i, (unsigned)count,
                (unsigned)(logical_address + i), (unsigned)c->cpu.CED);
            return false;
        }
        (void)ndbus_pool_write8(&s_pool, pa, source[i]);
    }

    return true;
}

/**
 * Make the process a copy message names the loaded one, for MICFU 10B and 11B.
 *
 * Ported from RetroCore Nd500CpuProcessBridge.TryLoadNamedProcess: nothing to do
 * when no process is loaded yet or the named one already is; refuse, and say so,
 * when the CPU is executing another process, because its registers cannot be
 * swapped from this thread and the copy would go through the wrong mapping;
 * otherwise the ordinary context switch.
 */
static bool mfbus_load_named_process(void *ctx, uint16_t x5cpu)
{
    NdbusNd5000 *nd = (NdbusNd5000 *)ctx;
    if (nd == NULL)
    {
        return false;
    }

    int slot = mfbus_slot_of(nd->station.number);
    if (slot < 0 || !s_cpus[slot].present)
    {
        return false;
    }
    MfbusCpuSlot *c = &s_cpus[slot];

    /* A thread that has just reported a stop may still be on its way out. */
    mfbus_quiesce_runner(c);

    if (c->loaded_x5cpu < 0 || c->loaded_x5cpu == (int)x5cpu)
    {
        return true;
    }
    if (!c->parked)
    {
        LOG(LOG_CAT_MMS, LOG_WARN,
            "MFbus: %s NAMED-PROCESS COPY: message names X5CPU=%u while X5CPU=%d is RUNNING - "
            "cannot switch, the copy is refused\n",
            c->name, (unsigned)x5cpu, c->loaded_x5cpu);
        return false;
    }
    return mfbus_switch_to_process(nd, c, (int)x5cpu);
}

static bool mfbus_start_process(void *ctx, uint32_t msg_byte, uint16_t micfu, uint32_t ctx_byte)
{
    NdbusNd5000 *nd = (NdbusNd5000 *)ctx;

    int slot = mfbus_slot_of(nd->station.number);
    if (slot < 0 || !s_cpus[slot].present)
    {
        return false;
    }
    MfbusCpuSlot *c = &s_cpus[slot];

    /* The thread that reported the stop this message answers may still be on its
     * way out. Let it finish before any register is read or changed. */
    mfbus_quiesce_runner(c);

    /* A CONTINUE NAMING THE PROCESS ALREADY LOADED RESUMES FROM THE LIVE REGISTERS.
     * A 3START NEVER DOES, EVEN FOR THAT SAME PROCESS.
     *
     * The microcode's context switch at 011473B compares the loaded process against
     * the wanted one and, when they match, "if same -> POPRET - save AND load both
     * skipped". That is why a 3TRACO or 3MONCO for the running process must NOT
     * re-read the context block: it is a continue, and the live registers are the
     * state it continues from.
     *
     * 3START is a different message and reaches the same microcode by a different
     * route. A monitor-call stop leaves the ND-500 through CALL_MON -> SET_IDLE,
     * which marks "no current process" by setting the sign bit over the process
     * number in SRF11. The B30 IDLE loop at 0o24724-25 reads SRF11 and its MSGN test
     * then SKIPS CNTXTSAVE, so NEWCNTXT/CNTXTLOAD at 0o14661 takes its load-only
     * branch and reads the block SINTRAN has just filled - the swapper's entry point
     * 0x08000004, not the parked MON return address.
     *
     * MEASURED on the octobus, macro lane: after an explicit START-SWAPPER the
     * swapper makes a FRESH first call, FUNCV=0 and SWPINFO=0, because its
     * initialisation ran. Resuming in place instead leaves SWPINFO holding the
     * previous work order and the swapper carries on mid-loop with a stale one.
     *
     * Our own run showed the failure this produces. START-SWAPPER drives 14 MON 377B
     * rounds to completion, prints "Allocating memory - 7342B pages", then sends a
     * second 3START for X5CPU 0; this arm swallowed it and merely unparked the
     * process at P=0x08008255, so the swapper never re-entered at its entry point,
     * never answered, and the monitor printed "ADDRESS OUTSIDE PROGRAM SEGMENT /
     * NOT KNOWN TRAP / At program address: 0 1B" with no trap ever reported by the
     * ND-500 side at all.
     *
     * This arm therefore only unparks for a CONTINUE, and lets the CPU carry on. */
    const int msg_x5cpu = ndbus_servicer_read_message_x5cpu(&nd->servicer, msg_byte);

    /* A CONTINUE NAMING A DIFFERENT PROCESS SWITCHES CONTEXT AND THEN RESUMES.
     *
     * The three ways this arm is reachable are the reference's own, in its order:
     * nothing is loaded, the message names the process that IS loaded, or a switch
     * to the named process succeeds. Declining instead is as wrong as resuming the
     * wrong process - the start path below would reload from whatever block the
     * LAST start used, which is not this message's process.
     *
     * MEASURED on PLACE-DOMAIN CPU-STAT: the domain faults and parks, SINTRAN sends
     * a 3MONCO naming the SWAPPER, and with no switch the comparison failed and the
     * start path reloaded the swapper from its ENTRY POINT - so it re-ran its
     * initialisation, lost the page-fault work order and asked the identical
     * MON 377B again while the domain stayed parked forever. */
    if (ndbus_micfu_is_continue(micfu) && c->parked
        && (c->loaded_x5cpu < 0 || c->loaded_x5cpu == msg_x5cpu
            || mfbus_switch_to_process(nd, c, msg_x5cpu)))
    {
        c->parked = false;
        c->stop_reported = false;
        c->mon_resumes++;
        c->steps_since_resume = 0u;

        /* APPLY SINTRAN'S ANSWER BEFORE LETTING THE PROCESS RUN. Three slots of the
         * message carry the answer and two of them meant something else on the way
         * out - see ndbus_servicer_read_monitor_result(). The write-back is what the
         * program is actually waiting for: measured 30-SEP-2026, without it the
         * swapper's MON 377B was reported, answered and resumed, and the process then
         * spun forever on a cell whose new value had been written in the message and
         * never carried across. */
        /* 26B CARRIES THE SAME ANSWER AS 24B, so it reads the same result. The
         * difference is the answer-data block handled below. Testing only for
         * MONCO here left a 3WMONCO resumed with no FUNCV, no K and no
         * write-backs - resumed, but blind. */
        /* WHERE THE 26B ANSWER-DATA BLOCK IS, read before the restart record so
         * the oversize guard can override FUNCV and K before they are applied.
         * Nothing is buffered - the bytes are streamed below, after the
         * write-backs and immediately before the resume, which is the order the
         * microcode uses: NEWCNTXT, the shared 24B fetch, the copy, then EXECUTE. */
        NdbusWmoncoBlock wblock;
        bool have_wblock = (micfu == NDBUS_MICFU_WMONCO) &&
                           ndbus_servicer_read_wmonco_block(&nd->servicer, msg_byte, &wblock);

        /* THE ANSWER SLOTS ARE A UNION - ASK WHICH ARM THIS MESSAGE CARRIES.
         *
         * KFLIP re-uses the STOPR slot, the write-back MASK re-uses NUMPA, and
         * FUNCV spans MCNO/MSWMC. A TRAP stop writes its own record into exactly
         * those halfwords, so decoding the monitor-call arm after a trap stop
         * reads this emulator's own trap record back as an answer.
         *
         * MEASURED 2026-10-04 on PLACE-DOMAIN CPU-STAT: a 3MONCO restart of a
         * process parked on trap 46B gave K=1 from the TRAPCODE value and
         * FUNCV=0x467F0800 from the trapping P 0x0800467F with its halves
         * swapped. That went into I1 and the process faulted on it immediately -
         * trap 44B, "no data capability, segment 8" - which is the protection
         * violation the console reported as the run's failure.
         *
         * A restart of a trap-parked process still RESUMES it; what it must not
         * do is overwrite I1 and the K flag with a record it only wrote itself. */
        if (micfu == NDBUS_MICFU_MONCO || micfu == NDBUS_MICFU_WMONCO)
        {
            NdbusMonResult res;
            if (!ndbus_servicer_stop_was_monitor_call(&nd->servicer, (uint16_t)msg_x5cpu))
            {
                LOG(LOG_CAT_MMS, LOG_INFO,
                    "MFbus: %s MICFU %oB restart of a process parked on a TRAP - the "
                    "answer slots hold the trap record, not FUNCV/K/mask, so none is "
                    "applied\n",
                    c->name, (unsigned)micfu);
            }
            else if (ndbus_servicer_read_monitor_result(&nd->servicer, msg_byte, &res))
            {
                /* THE OVERSIZE GUARD, applied to the answer the process is about
                 * to see. A 26NRB of 0x2000 or more is not a refusal: the copy is
                 * skipped and the process resumes with FUNCV 0o174 and K set, so
                 * it takes its own error path. Refusing the message instead is
                 * what leaves a process parked for ever. */
                if (have_wblock && wblock.oversize)
                {
                    res.funcv = 0x7Cu;   /* 0o174 */
                    res.kflip = 1u;
                }
                /* FUNCV -> I1, the call's result register. */
                c->cpu.I[0] = res.funcv;

                /* KFLIP -> the K flag in ST1. The ND-500 error convention: the
                 * program branches on K, so a failed call whose flag never arrives
                 * reads as success. */
                if (res.kflip != 0u)
                {
                    c->cpu.ST1 |= ND500_FLAG_K;
                }
                else
                {
                    c->cpu.ST1 &= ~(uint32_t)ND500_FLAG_K;
                }

                /* The selected parameters, written into PROCESS memory - so they go
                 * through the process's own MMU, not straight at the pool. A
                 * write-back to an address the process cannot reach is reported
                 * rather than dropped into whatever the flat address happens to hit. */
                for (uint32_t k = 0; k < res.count; k++)
                {
                    /* AN ARGUMENT WITH NO ADDRESS HAS NOTHING TO WRITE BACK TO.
                     * Ported from RetroCore Nd500CpuProcessBridge.ApplyRestartWriteBack
                     * ("if (writeBackAddresses[k] == 0) continue;"). Without the skip
                     * the answer for such an argument was translated and stored at
                     * logical address 0 of the process. */
                    if (res.addresses[k] == 0u)
                    {
                        continue;
                    }
                    uint32_t pa = nd500_mmu_peek_space(&c->cpu, res.addresses[k],
                                                       (uint8_t)c->cpu.CED, 0);
                    if (pa == 0xFFFFFFFFu)
                    {
                        LOG(LOG_CAT_MMS, LOG_WARN,
                            "MFbus: %s monitor-call write-back to 0x%X does not translate - "
                            "parameter dropped\n",
                            c->name, (unsigned)res.addresses[k]);
                        continue;
                    }
                    /* WHAT WAS ALREADY THERE. SINTRAN performs some monitor calls by
                     * writing the answer into ND-500 memory itself with a PHYSWR and
                     * then sending the 3MONCO - measured on CPU-STAT's MON 422B
                     * GetScratchSegment, where a 31B PHYSWR addressed to the domain's
                     * block arrives between the report and the restart. If that is
                     * what happened, this write-back lands ON TOP of the real answer,
                     * and only the previous value says so. */
                    uint32_t was = ((uint32_t)ndbus_pool_read8(&s_pool, pa) << 24)
                                 | ((uint32_t)ndbus_pool_read8(&s_pool, pa + 1u) << 16)
                                 | ((uint32_t)ndbus_pool_read8(&s_pool, pa + 2u) << 8)
                                 |  (uint32_t)ndbus_pool_read8(&s_pool, pa + 3u);
                    if (was != res.values[k] && c->mon_resumes <= MFBUS_MON_LOG_LIMIT)
                    {
                        LOG(LOG_CAT_MMS, LOG_WARN,
                            "MFbus: %s   write-back OVERWRITES 0x%08X with 0x%08X at "
                            "logical 0x%08X - if the old value is the answer, this "
                            "write-back is destroying it\n",
                            c->name, (unsigned)was, (unsigned)res.values[k],
                            (unsigned)res.addresses[k]);
                    }
                    (void)ndbus_pool_write8(&s_pool, pa,
                                            (uint8_t)(res.values[k] >> 24));
                    (void)ndbus_pool_write8(&s_pool, pa + 1u,
                                            (uint8_t)(res.values[k] >> 16));
                    (void)ndbus_pool_write8(&s_pool, pa + 2u,
                                            (uint8_t)(res.values[k] >> 8));
                    (void)ndbus_pool_write8(&s_pool, pa + 3u,
                                            (uint8_t)res.values[k]);

                    /* NAME EACH WRITE-BACK. A count of parameters written says
                     * nothing about whether the right cell got the right number,
                     * and a value landing one halfword out is invisible in a
                     * count. */
                    if (c->mon_resumes <= MFBUS_MON_LOG_LIMIT)
                    {
                        LOG(LOG_CAT_MMS, LOG_INFO,
                            "MFbus: %s   write-back param: logical 0x%08X -> physical 0x%08X "
                            "= 0x%08X\n",
                            c->name, (unsigned)res.addresses[k], (unsigned)pa,
                            (unsigned)res.values[k]);
                    }
                }

                if (c->mon_resumes > MFBUS_MON_LOG_LIMIT)
                {
                    /* One short line per answer past the detailed limit, so a
                     * failed call (K set) and its error code are always visible. */
                    LOG(LOG_CAT_MMS, LOG_INFO, "MFbus: %s MON answer FUNCV=0x%X (%oB) K=%u\n",
                        c->name, (unsigned)res.funcv, (unsigned)res.funcv,
                        (unsigned)(res.kflip != 0u));
                }
                if (c->mon_resumes <= MFBUS_MON_LOG_LIMIT)
                {
                    LOG(LOG_CAT_MMS, LOG_INFO,
                        "MFbus: %s monitor-call answer: FUNCV=0x%X K=%u mask=0x%X, %u parameter(s) "
                        "written back\n",
                        c->name, (unsigned)res.funcv, (unsigned)(res.kflip != 0u),
                        (unsigned)res.mask, (unsigned)res.count);
                }
            }
        }
        /* THE 26B ANSWER-DATA BLOCK, copied in BEFORE the process runs.
         *
         * STREAMED, one byte at a time, straight from the ND-100's half of the
         * pool into the process's memory. No intermediate buffer: an earlier
         * version carried the bytes in NdbusMonResult and then through a static
         * array here, which put 8 KB on the stack of every monitor-call restart,
         * copied the data three times, and - because the runner is threaded and
         * there are up to seven stations - shared one static buffer between CPUs.
         *
         * The byte extraction lives in ndbus_servicer_read_nd100_byte and not
         * here, so there is only ever one copy of the rule that the even byte is
         * the high half.
         *
         * Written through the MMU's NON-FAULTING peek rather than a store,
         * because the destination can straddle a page and an unmapped page must
         * be skipped and SAID, not faulted on: a fault raised while delivering an
         * answer would be reported as the process's own and sent to SINTRAN as a
         * page fault it never took. */
        if (have_wblock && wblock.count > 0u)
        {
            uint32_t written = 0u, unmapped = 0u;
            for (uint32_t i = 0; i < wblock.count; i++)
            {
                uint32_t pa = nd500_mmu_peek(&c->cpu, wblock.dest + i);
                if (pa == 0xFFFFFFFFu)
                {
                    unmapped++;
                    continue;
                }
                (void)ndbus_pool_write8(&s_pool, pa,
                    ndbus_servicer_read_nd100_byte(&nd->servicer, wblock.src_byte + i));
                written++;
            }
            LOG(LOG_CAT_MMS, LOG_INFO,
                "MFbus: %s 3WMONCO answer data: %u byte(s) from pool 0x%06X -> logical "
                "0x%08X, %u written, %u skipped as unmapped\n",
                c->name, (unsigned)wblock.count, (unsigned)wblock.src_byte,
                (unsigned)wblock.dest, (unsigned)written, (unsigned)unmapped);
        }
        else if (have_wblock && wblock.oversize)
        {
            LOG(LOG_CAT_MMS, LOG_WARN,
                "MFbus: %s 3WMONCO answer data OVERSIZE (26NRB >= 0x2000) - copy "
                "skipped, resuming with FUNCV 174B and K set\n", c->name);
        }

        if (c->mon_resumes <= MFBUS_MON_LOG_LIMIT)
        {
            LOG(LOG_CAT_MMS, LOG_INFO,
                "MFbus: %s MICFU %oB resumes the LOADED process in place at P=0x%X - no context "
                "reload\n",
                c->name, (unsigned)micfu, (unsigned)c->cpu.PC);
        }
        /* Trace the path out of this resume: the swapper's fault is reached from here,
         * and only the PCs in between name the branch that skips the loop-limit
         * initialisation. Armed once per resume, bounded. */
        if (c->mon_resumes <= MFBUS_MON_LOG_LIMIT)
        {
            c->resume_trace_left = mfbus_resume_trace_limit();
        }
        if (!mfbus_resume_runner(c, nd->station.number))
        {
            LOG(LOG_CAT_MMS, LOG_ERROR,
                "MFbus: %s MICFU %oB could not resume the runner - the process stays parked\n",
                c->name, (unsigned)micfu);
            return false;
        }
        return true;
    }

    /* A CONTINUE THAT GOT THIS FAR IS REFUSED - IT MUST NOT BECOME A START.
     *
     * Reaching here with a continue means one of two things: the process is not
     * parked, or the switch to the process the message names failed. Either way
     * there is nothing to continue, and falling through to the start path below
     * would RELOAD THE CONTEXT BLOCK and restart the process from its block P -
     * that is, from its entry point - in the middle of whatever it was doing.
     *
     * MEASURED 04-OCT-2026: that is the swapper's instruction loop. In one
     * START-SWAPPER run 888,000 of the run's 1,011,254 ND-500 instructions sat in
     * just two monitor-call resumes - 484,785 and 405,768, where every other
     * resume is 41,000 to 62,000 - with repeating write-back OVERWRITES warnings
     * beside them and the MON 377B argument simply counting up 0x0B, 0x0C, ...
     * That is the shape of a process being sent back to its entry point and
     * re-running its initialisation, not of a process working. It is also why
     * START-SWAPPER takes 35 seconds; the cost is the wasted instructions, not
     * the logging - an A/B run with the per-instruction trace fully off
     * (0 lines against 128,517) took the same 35 seconds.
     *
     * The reference refuses for the same reason, in the same place, and says so:
     * Nd500CpuProcessBridge.OnMonitorCallRestart returns false when the CPU is
     * not WAIT-parked - "a crashed/halted CPU must not silently resume - decline
     * so the servicer answers the placeholder way and SINTRAN sees a normal (if
     * inert) answer" - and again when SwitchToProcessIfNeeded fails.
     *
     * Declining is a real answer, not silence: the servicer counts it in
     * starts_declined, names it once in the log, and answers the message the way
     * a station with no CPU answers, so SINTRAN gets a reply and decides what to
     * do rather than waiting on us. */
    if (ndbus_micfu_is_continue(micfu))
    {
        c->continues_refused++;
        if (c->continues_refused <= MFBUS_MON_LOG_LIMIT)
        {
            LOG(LOG_CAT_MMS, LOG_WARN,
                "MFbus: %s continue MICFU %oB REFUSED: parked=%d loaded_x5=%d msg_x5=%d - "
                "nothing to continue, and restarting from the block would send the "
                "process back to its entry point\n",
                c->name, (unsigned)micfu, (int)c->parked, c->loaded_x5cpu, msg_x5cpu);
        }
        return false;
    }

    /* A START FOR A PARKED PROCESS FIRST ENDS THAT PROCESS'S RESIDENCY ON THE CPU.
     *
     * The park is this seam's expression of the microcode's SET_IDLE. A monitor-call
     * stop leaves the ND-500 through CALL_MON -> SET_IDLE, which marks "no current
     * process"; here it sets c->parked, mfbus_cpu_step() then returns false, and the
     * runner thread exits and sits at NDBUS_RUNNER_STOPPED. mfbus_load_context()
     * requires NDBUS_RUNNER_IDLE - a thread already reaped, so the pool it was
     * reading outlives it - and STOPPED is not IDLE.
     *
     * MEASURED: without this the second 3START of START-SWAPPER got as far as
     * setting PSTP and then refused with "is running - stop it before loading a
     * context", the servicer answered without starting anything, and SINTRAN's
     * SWPDECODER reported "Fatal error from Swapper / ERROR CODE: 0B" directly after
     * "Allocating memory - 7342B pages".
     *
     * Only for a process that is parked. One genuinely executing instructions is
     * declined by the state test below instead, because stopping it here would throw
     * away the work it is in the middle of. */
    if (c->parked)
    {
        c->parked = false;
        c->stop_reported = false;
        if (ndbus_runner_state(&c->runner) != NDBUS_RUNNER_IDLE)
        {
            /* Asks and waits. Reuses the same reap the resume path performs. */
            ndbus_runner_stop_and_join(&c->runner);
        }
    }

    /* Already running: this is a restart-class message for a process that is live,
     * and stopping and reloading it here would discard its state. Declined rather
     * than guessed at.
     *
     * STOPPED IS NOT RUNNING. A runner whose thread has exited - because the process
     * parked or stopped - is a candidate for a fresh start, so only the two live
     * states decline here. Testing "!= IDLE" also refused every message that arrived
     * after a stop. */
    NdbusRunnerState runner_state = ndbus_runner_state(&c->runner);
    if (runner_state == NDBUS_RUNNER_RUNNING || runner_state == NDBUS_RUNNER_STOPPING)
    {
        LOG(LOG_CAT_MMS, LOG_INFO,
            "MFbus: start MICFU %oB for %s declined - the CPU is already running\n",
            (unsigned)micfu, c->name);
        return false;
    }

    /* The servicer computed the BLOCK address; ndbus_context_attach() computes the
     * same thing from the area base and X5CPU. Derive the area back out of the
     * block so the one formula in ndbus_context.c stays the only one, rather than
     * repeating it here where it could drift. */
    /* X5CPU IS THE PROCESS, NOT THE STATION.
     *
     * ndbus_cpu_context_x5cpu() is derived from the STATION NUMBER - 070B gives 0 -
     * so on a single ND-5000 it answers 0 for every process. X5CPU in a message is
     * the process number: 0 the swapper, 1 the first domain, both live at once from
     * PLACE-DOMAIN onward with a message block each.
     *
     * MEASURED on the octobus. With the station index used here, the domain's start
     * recorded loaded_x5cpu = 0, so its page fault was written onto the SWAPPER's
     * message 0x8D30 instead of the domain's 0x8E30. SINTRAN's TRAPDECODER
     * (MP-P2-N500.NPL:135332) compares the faulting message against the swapper's
     * own and, when they match, takes EPFINSWAP / XRSTARTALL - "page fault in
     * swapper", fatal. The console printed "*** FATAL SYSTEM ERROR *** / The
     * Swapper stopped" with a page-fault record that was otherwise completely
     * correct, which is precisely what its own code does with what we told it.
     *
     * The context block came out right either way only because the two errors
     * cancelled in the area arithmetic below: block_offset shrank by one stride and
     * area_byte grew by one, so the same block was attached. The recorded process
     * number did not cancel, and that is what the trap is reported on. */
    int x5cpu = (msg_x5cpu >= 0) ? msg_x5cpu : ndbus_cpu_context_x5cpu(nd->station.number);
    uint32_t stride = NDBUS_CTX_STRIDE_BYTES;
    uint32_t block_offset = stride + ((uint32_t)x5cpu * stride);
    if (ctx_byte < block_offset)
    {
        LOG(LOG_CAT_MMS, LOG_ERROR,
            "MFbus: start MICFU %oB for %s declined - context block 0x%X is below its own "
            "area\n",
            (unsigned)micfu, c->name, (unsigned)ctx_byte);
        return false;
    }
    uint32_t area_byte = ctx_byte - block_offset;

    if (!ndbus_context_attach(&c->context, &s_pool, area_byte, x5cpu))
    {
        LOG(LOG_CAT_MMS, LOG_ERROR,
            "MFbus: start MICFU %oB for %s declined - context block at 0x%X does not fit the "
            "pool\n",
            (unsigned)micfu, c->name, (unsigned)ctx_byte);
        return false;
    }
    c->context_set = true;

    /* A CONTEXT WHOSE P IS ZERO IS NOT A PROCESS. RetroCore records exactly this
     * case: when the block's P is 0 the start is declined, the servicer answers the
     * placeholder way, and SINTRAN's SWPDECODER then reports a swapper fault. Which
     * is the right outcome - starting a CPU at address 0 would fault somewhere else
     * entirely and look like a different bug. */
    uint32_t entry_p = ndbus_context_read(&c->context, NDBUS_CTX_P);
    if (entry_p == 0u)
    {
        LOG(LOG_CAT_MMS, LOG_WARN,
            "MFbus: start MICFU %oB for %s declined - context block at 0x%X has P = 0, so "
            "there is no entry point to start\n",
            (unsigned)micfu, c->name, (unsigned)ctx_byte);
        return false;
    }

    /* HAND THE MMU ITS TABLE BEFORE THE FIRST FETCH. The context block's P is a
     * LOGICAL ND-500 address - measured 0x08000004, i.e. segment 1 offset 4 - so
     * without a physical segment table pointer the CPU fetches at 0x08000004
     * PHYSICALLY, reads an uninitialised 0x00 and stops on an invalid instruction
     * with paddr equal to P. That is the exact failure this line prevents.
     *
     * The base is the one SINTRAN patched into control-store cell 0o21, which the
     * station read at microprogram start. RetroCore does the same thing at the same
     * point: LoadMmsPointersFromControlStore writes PSTP on the attached CPU. It
     * does NOT hand over a DIT base - the DIT base it tracks is a diagnostic learned
     * from the trap-config writes - so neither does this. */
    if (nd->pst_base != 0u)
    {
        c->cpu.PSTP = nd->pst_base;
        LOG(LOG_CAT_MMS, LOG_INFO, "MFbus: %s PSTP := 0x%X from patched CS cell 0o21\n",
            c->name, (unsigned)nd->pst_base);
    }
    else
    {
        /* Said out loud: a start with no segment table will fetch logical addresses
         * as physical ones, which stops on whatever happens to be there. */
        LOG(LOG_CAT_MMS, LOG_WARN,
            "MFbus: starting %s with NO physical segment table - logical addresses will be "
            "fetched as physical\n",
            c->name);
    }

    /* TURN TRANSLATION ON, because the process's P is a LOGICAL address.
     *
     * MEASURED 30-SEP-2026, both halves: the context block holds P=0x08000004 and
     * PS=0x3 - a real process segment, the same segment 3 the pre-start PHYSWR
     * resolved against - and PSTP is 0x3A000 from patched CS cell 0o21. With all
     * three set the fetch STILL came out at physical 0x08000004 and stopped on
     * "Invalid instruction 0x00", because nd500x carries an explicit MMU switch that
     * defaults off and nothing on this path set it.
     *
     * This is nd500x's own state, not a mechanism being invented: on the machine
     * translation is simply in effect once PS and PSTP are set, and the reference
     * has no equivalent switch to mirror. nd500_machine_enable_mmu() is the
     * repository's existing entry point for it - the same one machine.c uses - so
     * the state is set through that rather than by poking the flags. */
    nd500_machine_enable_mmu(&c->machine);

    if (!mfbus_load_context(nd->station.number))
    {
        return false;
    }

    /* PS is only known once the context block has been read, so this comes after
     * the load and before the CPU is allowed to fetch anything. */
    mfbus_declare_capability_table(c);

    /* A 3START, so the whole domain state comes from the process's own table -
     * not just THA, which the line above loads on every context load. */
    mfbus_load_domain_state_at_start(c);

    /* REMEMBER WHOSE PROCESS IS RUNNING. A trap is reported on that process's own
     * activation message, so the step loop needs to know which X5CPU to name.
     * The outgoing process's trap enables and pending CALL are put away first,
     * and the started one begins with no CALL in flight.
     *
     * ORDER AS IN THE REFERENCE, and worth knowing: OnStartProcessND5000 loads the
     * domain state (its line 970) BEFORE NoteLoadedProcess (its line 1022) saves
     * the outgoing process's trap enables, so what is saved for the outgoing
     * process is the STARTED process's freshly loaded values. That is copied
     * here unchanged; whether it is intended there is for Ronny to judge. */
    mfbus_note_started_process(c, (int)x5cpu);
    c->loaded_x5cpu = x5cpu;

    /* SINTRAN ON THIS ND-100 OWNS THE MONITOR CALLS. Installed per start, because
     * that is when this CPU becomes a process belonging to that SINTRAN. */
    c->cpu.mon_call_host = mfbus_mon_call;
    c->cpu.mon_call_host_ctx = c;
    c->steps_since_resume = 0u;

    /* Through the resume helper, because a CPU that stopped earlier has a thread to
     * reap before a new one can be created. */
    if (!mfbus_resume_runner(c, nd->station.number))
    {
        return false;
    }

    /* WHAT THE TABLES ACTUALLY SAY AT THE MOMENT OF THE START, read out of the pool
     * rather than reasoned about. The first fetch walks DIT[domain][segment] to a
     * capability, takes its low 13 bits as a physical segment number, and reads
     * PST[psn] at PSTP. Measured 30-SEP-2026: that walk reported "PST entry 83 is
     * ZERO", and a zero entry is a page fault, so the fetch never resolves. These
     * three lines say which of the three cells is the empty one. */
    if (nd->servicer.dit_base != 0u || nd->pst_base != 0u)
    {
        uint32_t seg = (entry_p >> 27u) & 0x1Fu;
        uint32_t dit_word = nd->servicer.dit_base + (ndbus_context_read(&c->context, NDBUS_CTX_CED) & 0xFFu) * 256u + seg * 2u;
        uint16_t cap = (uint16_t)((ndbus_pool_read8(&s_pool, dit_word) << 8)
                                 | ndbus_pool_read8(&s_pool, dit_word + 1u));
        uint32_t psn = (uint32_t)(cap & 0x1FFFu);
        uint32_t pst_cell = nd->pst_base + psn * 4u;
        uint32_t pste = 0u;
        for (uint32_t i = 0u; i < 4u; i++)
        {
            pste = (pste << 8u) | ndbus_pool_read8(&s_pool, pst_cell + i);
        }
        LOG(LOG_CAT_MMS, LOG_INFO,
            "MFbus: %s start tables: P=0x%X seg=%u DIT[0x%X]=0x%04X psn=%u "
            "PST[0x%X]=0x%08X\n",
            c->name, (unsigned)entry_p, (unsigned)seg, (unsigned)dit_word, (unsigned)cap,
            (unsigned)psn, (unsigned)pst_cell, (unsigned)pste);

        /* THE CAPABILITY TABLE IS FOUND THROUGH PS, NOT THROUGH A LEARNED BASE.
         *
         * RetroCore Nd500CpuProcessBridge.InstallGuestSegmentTable: "PS - the
         * per-process context block at +0x48, whose low halfword is the
         * process-segment index. MEASURED: the two live processes carry AL#17 =
         * 0x4C48000A and 0x48480003, giving PS 10 and 3, and PST entries 10 and 3
         * hold pages 0x16C and 0x0DE - exactly the capability-table bases
         * (0xB6000 / 0x6F000) those processes use." So the walk starts at
         * PST[PS], not at a DIT base, and the same file says outright that the
         * bridge does NOT hand a DIT base over - the one it tracks is a
         * diagnostic learned from the trap-config writes.
         *
         * Print what PST[PS] holds and what capability it implies, so the chain
         * can be compared against the one the learned base produced. */
        uint32_t ps = ndbus_context_read(&c->context, NDBUS_CTX_SRF13) & 0x1FFFu;
        uint32_t ps_cell = nd->pst_base + ps * 4u;
        uint32_t ps_pste = 0u;
        for (uint32_t i = 0u; i < 4u; i++)
        {
            ps_pste = (ps_pste << 8u) | ndbus_pool_read8(&s_pool, ps_cell + i);
        }
        uint32_t captab = (ps_pste & 0x3FFFFFFFu) << 11u;
        uint32_t cap_from_ps = (uint32_t)((ndbus_pool_read8(&s_pool, captab + seg * 2u) << 8)
                                         | ndbus_pool_read8(&s_pool, captab + seg * 2u + 1u));
        LOG(LOG_CAT_MMS, LOG_INFO,
            "MFbus: %s PS=%u PST[0x%X]=0x%08X -> capability table 0x%X, cap[seg %u]=0x%04X "
            "psn=%u\n",
            c->name, (unsigned)ps, (unsigned)ps_cell, (unsigned)ps_pste, (unsigned)captab,
            (unsigned)seg, (unsigned)cap_from_ps, (unsigned)(cap_from_ps & 0x1FFFu));

        /* WHERE IS THE TABLE, THEN. A zero cell says nothing about whether the
         * table is empty, in the wrong place, or on a different stride, so scan
         * the 4 KB page that holds PSTP for the first nonzero 32-bit word and say
         * how far it is from the base. */
        uint32_t page = nd->pst_base & ~0xFFFu;
        uint32_t first_nonzero = 0xFFFFFFFFu;
        for (uint32_t off = 0u; off < 0x1000u; off += 4u)
        {
            uint32_t w = 0u;
            for (uint32_t i = 0u; i < 4u; i++)
            {
                w = (w << 8u) | ndbus_pool_read8(&s_pool, page + off + i);
            }
            if (w != 0u)
            {
                first_nonzero = off;
                LOG(LOG_CAT_MMS, LOG_INFO,
                    "MFbus: %s PST page 0x%X: first nonzero word at +0x%X = 0x%08X "
                    "(entry %u on a 4-byte stride)\n",
                    c->name, (unsigned)page, (unsigned)off, (unsigned)w,
                    (unsigned)(off / 4u));
                break;
            }
        }
        if (first_nonzero == 0xFFFFFFFFu)
        {
            LOG(LOG_CAT_MMS, LOG_INFO,
                "MFbus: %s PST page 0x%X is entirely zero - the table is not there at all\n",
                c->name, (unsigned)page);
        }
    }

    LOG(LOG_CAT_MMS, LOG_INFO,
        "MFbus: start MICFU %oB taken for %s - context 0x%X, P=0x%X\n", (unsigned)micfu,
        c->name, (unsigned)ctx_byte, (unsigned)entry_p);
    return true;
}

static void mfbus_ndbus_log(void *ctx, int level, const char *message)
{
    (void)ctx;
    (void)level;
    LOG(LOG_CAT_MMS, LOG_INFO, "MFbus/ND-5000: %s\n", message);
}

/** The callbacks every station and servicer on this bus logs through. */
static const NdbusHostOps s_ndbus_host_ops = {
    .log   = mfbus_ndbus_log,
    .yield = NULL,
    .ctx   = NULL,
};

/* MASTER CLEAR, CPURES AND MICROPROGRAM START RESET THE CPU AND LEAVE IT PARKED.
 *
 * Ported from RetroCore OctobusND5000Station.ResetCpuToIdle and
 * CpuND500.ApplyND5000InitState ($RETROCORE/Emulated.HW/ND/CPU/ND500/
 * CpuND500.ProcessControl.cs): both clear the register file and park the CPU in
 * the microcode IDLE state; memory and the MMU tables are left alone. Here the
 * park is "no host thread, nothing loaded": the next thing that runs this CPU is
 * a 3START, exactly as after attach.
 *
 * Called by the station on the thread that delivered the frame, which is the
 * ND-100 thread. Without it a master clear or a second control-store load left
 * the old process's thread running and the old process recorded as loaded. */
static void mfbus_reset_cpu_to_idle(void *ctx)
{
    NdbusNd5000 *nd = (NdbusNd5000 *)ctx;
    if (nd == NULL)
    {
        return;
    }
    int slot = mfbus_slot_of(nd->station.number);
    if (slot < 0 || !s_cpus[slot].present)
    {
        return;
    }
    MfbusCpuSlot *c = &s_cpus[slot];

    ndbus_runner_stop_and_join(&c->runner);
    __atomic_store_n(&c->runner_leaving, 0u, __ATOMIC_RELEASE);

    nd500_cpu_reset(&c->cpu);
    nd500_trap_clear();
    c->parked = false;
    c->loaded_x5cpu = -1;
    c->trap_captured = false;
    memset(c->proc, 0, sizeof(c->proc));

    LOG(LOG_CAT_MMS, LOG_INFO,
        "MFbus: %s CPU reset - register file cleared, no process loaded, waiting for a start\n",
        c->name);
}

/* Input or output control bit 5 on the octobus card: CONTINUE ACCP. RetroCore
 * NDBusOctobus.ProcessControlChange calls the station's ContinueAccp(), which
 * ends the idle state an emergency 244B put it in. */
static void mfbus_continue_accp(void *ctx)
{
    (void)ctx;
    for (int i = 0; i < s_nd5000_count; i++)
    {
        s_nd5000[i].accp_idle = false;
    }
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
    if (!ndbus_nd5000_init(nd, station_number, &s_pool, &s_ndbus_host_ops, NULL))
    {
        /* Refused by the station itself: outside 070B..076B, where another kind
         * of device lives. */
        LOG(LOG_CAT_MMS, LOG_ERROR,
            "MFbus: %o is not an ND-5000 station number (070B to 076B)\n",
            (unsigned)station_number);
        return false;
    }

    /* WHERE THE WINDOW SITS IN ND-100 MEMORY. The servicer needs it for one
     * field only: ABUFA, which SINTRAN fills with an ND-100 PHYSICAL address
     * (MP-P2-N500.NPL:140675) while every other mailbox address is
     * window-relative. s_base_word is the ND-100 WORD address of pool byte 0,
     * set by mfbus_attach(), which the configuration always runs first.
     *
     * MEASURED 05-OCT-2026 without this: ABUFA = ND-100 byte 0x42D000 was used as
     * pool offset 0x42D000 instead of 0x00D000, so CPU-STAT's output text never
     * reached SINTRAN's buffer and its MON 143B answer read back as zeros. */
    ndbus_servicer_set_nd100_window_base(&nd->servicer, s_base_word * 2u);

    if (!ndbus_fabric_register(&s_fabric, &nd->station))
    {
        LOG(LOG_CAT_MMS, LOG_ERROR, "MFbus: octobus station %o is already occupied\n",
            (unsigned)station_number);
        return false;
    }

        /* WHO STARTS A PROCESS.
     *
     * THE MACRO LANE IS THE RIGHT ONE, confirmed from Ronny's own RetroCore script:
     * `Nd5000 attach` with no --microword attaches the FUNCTIONAL ND-500 CPU on the
     * octobus, and its own note reads "MACRO = the functional CpuND500 runs the
     * ND-500 macro code directly. This is the lane the DOM programs run on", against
     * "MICRO = ... NOT as far along as the macro CPU". So the bridge to mirror is
     * Nd500CpuProcessBridge.OnStartProcessND5000 - which loads the context block -
     * and NOT Nd5000CpuProcessBridge, the microword one that only unparks microcode.
     *
     * Comments in the reference claiming the octobus lane never gets this far are
     * STALE RECORDS: the machine prints the "ND-5000:" monitor prompt in both, which
     * is what identifies the lane, and Ronny's octobus run gets further than those
     * comments describe. Read the code and measure, not the commentary.
     *
     * Without this the servicer declines every start and SINTRAN reports "The
     * Swapper stopped".
     */
    (void)ndbus_nd5000_set_process_host(nd, mfbus_start_process);
    (void)ndbus_nd5000_set_dit_declarer(nd, mfbus_declare_dit_base);
    (void)ndbus_nd5000_set_data_reader(nd, mfbus_read_nd500_data_bytes);
    /* The write half and the named-process switch, for MICFU 10B and 11B. Set
     * directly: the reader's setter above has already pointed host.ctx at `nd`. */
    nd->servicer.host.write_nd500_data_bytes = mfbus_write_nd500_data_bytes;
    nd->servicer.host.load_named_process     = mfbus_load_named_process;

    /* Master clear, CPURES and microprogram start reach the CPU through these. */
    (void)ndbus_nd5000_set_cpu_hooks(nd, mfbus_reset_cpu_to_idle, mfbus_reset_cpu_to_idle, nd);

    /* The card stops echoing a frame the ND-100 sends to itself once a CPU
     * station is on the bus (RetroCore NDBusOctobus.AttachCpu). */
    if (s_card != NULL)
    {
        octobus_set_cpu_attached(s_card, true);
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

/*
 * The ND-100 card's frame going onto the bus.
 *
 * The ND-100 is always octobus station 1B (ND-05.020.01 T329), so that is the
 * source the fabric rewrites into the delivered frame - it is not configurable
 * and must not be guessed from the card's thumbwheel, which selects the
 * INTERFACE, not the station.
 *
 * A timeout - an illegal destination, or no station there - pushes NOTHING.
 * That is what the hardware does: the real bus reports Ack=00 and the software
 * times out on an empty receive FIFO. Pushing a synthetic "no answer" frame
 * would make an absent station indistinguishable from a quiet one.
 */
static bool mfbus_card_transmit(void *ctx, Device *card, uint16_t frame)
{
    (void)ctx;

    /* NO STATION AT THE NUMBER IN BITS 13-8 IS A TIMEOUT, BROADCAST OR NOT.
     * RetroCore NDBusOctobus.ProcessTransmitFrame looks the destination up before
     * it looks at the broadcast bit; ndbus_fabric_send only checks it on its
     * unicast branch, so a broadcast naming an empty station was delivered to
     * everyone with no error. */
    uint8_t destination = (uint8_t)((frame >> 8u) & 0x3Fu);
    if (!ndbus_fabric_has_station(&s_fabric, destination))
    {
        return false;
    }

    uint16_t replies[NDBUS_MAX_REPLY_FRAMES];
    int      n = ndbus_fabric_send(&s_fabric, (uint8_t)NDBUS_STATION_ND120_CPU, frame, replies);

    // THE TWO NEGATIVE-ISH RESULTS ARE DIFFERENT THINGS, and collapsing them into
    // one "n <= 0" was hiding the answer discovery asks for. ndbus_fabric_send
    // returns -1 when NO STATION is registered at the destination (Ack=00, the
    // timeout after the hardware retries) and 0 when a station took the frame and
    // simply had nothing to say back. The card turns the first into ERROR + NOT
    // PRESENT in its output status; the second is a normal, acknowledged transfer.
    if (n < 0)
    {
        return false;
    }
    if (n == 0)
    {
        return true;
    }

    for (int i = 0; i < n; i++)
    {
        if (!octobus_rx_push(card, replies[i]))
        {
            /* A full 16-word FIFO no longer loses anything - the card parks the
             * frame and the guest's next read pulls it in, which is the sender's
             * hardware retry after Ack=10. Getting here means even the park is
             * full, so the rest of the reply really is DROPPED and the guest
             * will read a different message than the one that was sent. Said out
             * loud, because that is a defect and not a quiet condition. */
            LOG(LOG_CAT_MMS, LOG_ERROR,
                "MFbus: octobus busy-retry park full - %d reply frame(s) dropped\n", n - i);
            break;
        }
    }

    // The station answered, so the frame was acknowledged - a full FIFO loses the
    // reply, not the acknowledge.
    return true;
}

/* A frame the bus delivers to station 1B. `frame` is already in receive format,
 * bits 13-8 naming the sender. Appends only - see the comment on s_nd100_station
 * for why the card is not touched here. Never answers: the ND-100 replies
 * through SINTRAN, later, not with a bus-level reply frame. */
static int mfbus_nd100_station_handle(NdbusStation *station, uint16_t frame, uint8_t source_station,
                                      uint16_t *replies)
{
    (void)station;
    (void)source_station;
    (void)replies;

    bool dropped = false;
    ndbus_lock();
    if (s_inbound_count >= MFBUS_ND100_INBOUND_WORDS)
    {
        s_inbound_dropped++;
        dropped = true;
    }
    else
    {
        unsigned tail = (s_inbound_head + s_inbound_count) % MFBUS_ND100_INBOUND_WORDS;
        s_inbound[tail] = frame;
        s_inbound_count++;
        s_inbound_queued++;
    }
    ndbus_unlock();

    if (dropped)
    {
        LOG(LOG_CAT_MMS, LOG_ERROR,
            "MFbus: frame 0x%04X for the ND-100 DROPPED - %u frames already wait for the "
            "octobus card\n",
            (unsigned)frame, (unsigned)MFBUS_ND100_INBOUND_WORDS);
    }
    return 0;
}

/* Move ONE waiting frame into the card's receive FIFO. One per call, as the
 * reference releases one per Clock(), so each frame raises its own input event.
 * ND-100 thread only. */
static void mfbus_nd100_deliver_inbound(void)
{
    if (s_card == NULL)
    {
        return;
    }

    uint16_t frame = 0;
    bool     have = false;
    ndbus_lock();
    if (s_inbound_count > 0u)
    {
        frame = s_inbound[s_inbound_head];
        s_inbound_head = (s_inbound_head + 1u) % MFBUS_ND100_INBOUND_WORDS;
        s_inbound_count--;
        have = true;
    }
    ndbus_unlock();

    if (!have)
    {
        return;
    }
    if (octobus_rx_push(s_card, frame))
    {
        s_inbound_delivered++;
    }
    else
    {
        s_inbound_dropped++;
        LOG(LOG_CAT_MMS, LOG_ERROR,
            "MFbus: frame 0x%04X for the ND-100 DROPPED - the octobus card's FIFO and its "
            "busy-retry park are both full\n",
            (unsigned)frame);
    }
}

void mfbus_nd100_inbound_counts(unsigned long *queued, unsigned long *delivered,
                                unsigned long *dropped)
{
    ndbus_lock();
    if (queued != NULL)
    {
        *queued = s_inbound_queued;
    }
    if (delivered != NULL)
    {
        *delivered = s_inbound_delivered;
    }
    if (dropped != NULL)
    {
        *dropped = s_inbound_dropped;
    }
    ndbus_unlock();
}

NdbusFabric *mfbus_fabric(void)
{
    return s_attached ? &s_fabric : NULL;
}

bool mfbus_attach_card(Device *card)
{
    if (card == NULL)
    {
        return false;
    }
    if (!s_attached)
    {
        LOG(LOG_CAT_MMS, LOG_ERROR,
            "MFbus: cannot connect the octobus card - no shared pool is attached\n");
        return false;
    }

    /* The ND-100 takes its place on the bus, so a station can reach it without
     * having been asked first. Once per attach; a second card on the same bus
     * would be a second INTERFACE to the same station, which nothing configures. */
    if (!s_nd100_registered)
    {
        memset(&s_nd100_station, 0, sizeof(s_nd100_station));
        s_nd100_station.number = (uint8_t)NDBUS_STATION_ND120_CPU;
        s_nd100_station.type   = "ND-100 CPU";
        s_nd100_station.handle = mfbus_nd100_station_handle;
        s_nd100_station.ctx    = NULL;
        if (!ndbus_fabric_register(&s_fabric, &s_nd100_station))
        {
            LOG(LOG_CAT_MMS, LOG_ERROR,
                "MFbus: octobus station 1B is already occupied - the ND-100 cannot be reached "
                "by the other stations\n");
            return false;
        }
        s_nd100_registered = true;
    }
    s_card = card;

    octobus_set_transmit(card, mfbus_card_transmit, NULL);
    octobus_set_continue_accp(card, mfbus_continue_accp, NULL);
    if (s_nd5000_count > 0)
    {
        octobus_set_cpu_attached(card, true);
    }
    LOG(LOG_CAT_MMS, LOG_INFO, "MFbus: octobus card connected to the bus as station 1B\n");
    return true;
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

    /* ALLOCATE THE MMU TABLES. nd500_mmu_state_create() leaves the PST and the PCB
     * table NULL on purpose (lazy), and nd500_mmu_translate_domain() refuses with
     * "[MMU] Tables not initialized! PST=(nil) PCB=(nil)" and returns the virtual
     * address unchanged before it ever reaches the guest-table branch. Measured
     * 30-SEP-2026: the swapper's first fetch at 0x08000004 came back untranslated
     * and stopped on byte 0x00. The free-running binary calls this in
     * nd500x.c:572; an embedded CPU needs it for the same reason. */
    nd500_mmu_init(&c->cpu);

    /* WIRE THE ND-100 OPERAND MAPPING FOR RIOM, DERIVED FROM THE SERVICER.
     *
     * Without this the CPU keeps its field defaults, which are the ND-500 3022
     * convention: base 0x40000, 2 host bytes per operand unit. On the octobus SINTRAN's
     * CNVWADR emits a BYTE offset inside the 5MPM window instead, so the 3022 reading
     * lands in memory that is backed but was never written.
     *
     * Measured 30-SEP-2026 before this call existed: the swapper's HSWPI arrived as
     * 0x00008E30, RIOM moved 70 halfwords of zeros to 0x080240BC, and SINTRAN printed
     *     ND-500(0) error: Fatal error from Swapper / ERROR CODE 201B
     * because the swapper scanned a record that had never been filled.
     *
     * base is 0 and window_base is 0 because the operand is ALREADY window-relative and
     * the CPU's own physical path treats ND-500 address 0 as the window start: adding
     * the base again would overshoot into unbacked memory. The SCALE is asked of the
     * servicer that defines the convention rather than restated here, so the copy engine
     * and RIOM cannot drift apart. */
    if (nd500_cpu_set_nd100_mapping(&c->cpu, 0u,
                                    ndbus_servicer_nd100_bytes_per_unit(), 0) != 0)
    {
        /* Only reachable if the servicer ever reports a scale that is not an ND address
         * convention. Say so rather than running on a silently wrong mapping. */
        printf("[MFBUS] ND-100 operand mapping REFUSED: servicer reported %u bytes per "
               "unit (expected 1 or 2); RIOM would read the wrong memory\n",
               (unsigned)ndbus_servicer_nd100_bytes_per_unit());
    }

    /* INSTALL THE TRAP-STOP SEAM. Without it the CPU keeps its free-running
     * behaviour, which is to halt on a trap no local handler took - correct for a
     * machine with nobody to report to, and wrong here, because on this lane
     * SINTRAN IS the trap handler and is reached through the message the sink's
     * data lets us fill in. See mfbus_trap_sink. */
    if (nd500_cpu_set_trap_sink(&c->cpu, mfbus_trap_sink, c) != 0)
    {
        printf("[MFBUS] trap sink REFUSED - traps would halt this CPU instead of "
               "being reported to SINTRAN\n");
    }

    /* -1, NOT 0: process 0 is a real X5CPU, so a zero here would name it as loaded
     * before anything had started and a stray trap would be reported against it. */
    c->loaded_x5cpu = -1;

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

bool mfbus_load_nd5000(uint8_t station_number, const char *path, uint32_t pool_offset)
{
    int slot = mfbus_slot_of(station_number);
    if (slot < 0 || !s_cpus[slot].present || path == NULL)
    {
        return false;
    }

    MfbusCpuSlot *c = &s_cpus[slot];

    /* Refuse a running CPU: loading underneath a thread that is fetching
     * instructions produces a machine executing half an old image and half a
     * new one, which is not a state any diagnosis would guess. */
    if (ndbus_runner_state(&c->runner) != NDBUS_RUNNER_IDLE)
    {
        LOG(LOG_CAT_MMS, LOG_ERROR, "MFbus: %s is running - stop it before loading\n", c->name);
        return false;
    }

    /* nd500_load_file_to_memory() bounds-checks against the machine's memory
     * size, which here is the whole pool. */
    int rc = nd500_load_file_to_memory(&c->machine, path, pool_offset);
    if (rc != 0)
    {
        LOG(LOG_CAT_MMS, LOG_ERROR, "MFbus: could not load %s at pool offset 0x%X for %s (%d)\n",
            path, (unsigned)pool_offset, c->name, rc);
        return false;
    }

    /* The image begins where it was put, so that is where execution starts. */
    c->cpu.PC = pool_offset;

    LOG(LOG_CAT_MMS, LOG_INFO, "MFbus: loaded %s at pool offset 0x%X for %s, PC set\n", path,
        (unsigned)pool_offset, c->name);
    return true;
}

bool mfbus_place_context(uint8_t station_number, uint32_t area_byte, uint32_t entry_p,
                         uint32_t local_base)
{
    int slot = mfbus_slot_of(station_number);
    if (slot < 0 || !s_cpus[slot].present)
    {
        return false;
    }
    MfbusCpuSlot *c = &s_cpus[slot];

    /* Through the converter, NOT by hand. The context block's X5CPU is 0-based
     * while the mailbox's CPUNO is 1-based, and both index a 256-byte stride -
     * so a bare subtraction here is right for one structure and off by one for
     * the other. ndbus_cpunum.h exists to keep that in one place. */
    int x5cpu = ndbus_cpu_context_x5cpu(station_number);

    if (!ndbus_context_attach(&c->context, &s_pool, area_byte, x5cpu))
    {
        LOG(LOG_CAT_MMS, LOG_ERROR,
            "MFbus: a context block for %s does not fit at area 0x%X\n", c->name,
            (unsigned)area_byte);
        return false;
    }
    if (!ndbus_context_place(&c->context, entry_p, local_base))
    {
        return false;
    }

    c->context_set = true;
    LOG(LOG_CAT_MMS, LOG_INFO, "MFbus: context for %s at 0x%X, P=0x%X B=0x%X\n", c->name,
        (unsigned)ndbus_context_base(&c->context), (unsigned)entry_p, (unsigned)local_base);
    return true;
}

bool mfbus_load_context(uint8_t station_number)
{
    int slot = mfbus_slot_of(station_number);
    if (slot < 0 || !s_cpus[slot].present)
    {
        return false;
    }
    MfbusCpuSlot *c = &s_cpus[slot];
    if (!c->context_set)
    {
        LOG(LOG_CAT_MMS, LOG_ERROR, "MFbus: no context block placed for %s\n", c->name);
        return false;
    }
    if (ndbus_runner_state(&c->runner) != NDBUS_RUNNER_IDLE)
    {
        LOG(LOG_CAT_MMS, LOG_ERROR, "MFbus: %s is running - stop it before loading a context\n",
            c->name);
        return false;
    }

    /*
     * NEWCNTXT, as far as the block goes.
     *
     * ONLY the fields the microcode actually loads. TOS, LL, HL, THA, CES, CAS
     * and the trap enables live in the block but come from the Domain
     * Information Table, so they are not copied - see
     * ndbus_context_field_is_loaded(). Copying them would make this emulator
     * honour a context the hardware ignores, and a bring-up that works here and
     * not on the machine is worse than one that fails in both.
     */
    /* CLEAR THE TRANSLATION CACHE - the microcode's own dctsb on a context load.
     *
     * nd500_tlb_tag() keys on (vpn, CED, is_instruction) and its comment claims a
     * tag match is therefore exact. That holds only while one capability table is
     * in force. Two ND-5000 processes both run with CED = 0 and are told apart by
     * PS and the capability table it selects - 3 / 0x74000 for the swapper, 0xA /
     * 0x8C000 for a domain - and neither is in the tag, so one process's cached
     * translation answers the other's fetch.
     *
     * MEASURED on PLACE-DOMAIN CPU-STAT, in a single step of the DOMAIN: the
     * non-faulting peek resolved logical 0x08000004 to pa=0x007FC804 holding the
     * domain's own bytes C3 08, while the decoder's mmu_read8() of the same address
     * returned 0xDC - the SWAPPER's byte. The domain therefore decoded the
     * swapper's 13-byte init instead of its own 6-byte call, ran the swapper's
     * instruction stream, and referenced a data address outside its own 6696-byte
     * data segment, which SINTRAN correctly reported as ADDRESS OUTSIDE DATA
     * SEGMENT.
     *
     * The real machine clears the TSB on a process switch - the X5CLR mask is
     * documented "Clear data tsb+cache+dump+forget process" - so this is a port of
     * that step, not a workaround for the tag. */
    nd500_mmu_tlb_flush();

    /* NEWCNTXT, over s_ctx_fields - the SAME rows the save writes, which is the
     * whole point of the table: a register cannot be restored here and dropped
     * there, because there is only one list.
     *
     * Masked per row. PS in particular is a 13-bit halfword register, so a dirty
     * high halfword must not invent a segment; CED and CAD are byte transfers.
     * PS is also the one row the save does not write back, because CNTXTLOAD
     * reads that slot itself - the table's note has the direction argument.
     *
     * WHICH FIELDS ARE DELIBERATELY ABSENT, and this is the part worth keeping in
     * view: HL, THA, CES, CAS and the trap enables live in the block but the
     * microcode sources them from the Domain Information Table, so copying them
     * would make this emulator honour a context the hardware ignores -
     * ndbus_context_field_is_loaded() is the single statement of that list. TOS
     * and LL are the exception and the table says why: the microcode touches
     * their two slots in NEITHER direction, so they are the only ones that can
     * serve as a private stash without the machine reading what we invented. */
    for (size_t k = 0; k < MFBUS_CTX_FIELD_COUNT; k++)
    {
        const MfbusCtxField *f = &s_ctx_fields[k];
        *mfbus_ctx_reg(&c->cpu, f) =
            ndbus_context_read(&c->context, f->offset) & f->mask;
    }

    /* SAY WHAT THE BLOCK ACTUALLY HELD. "PS is loaded" and "PS is loaded and it is
     * zero" produce the same untranslated fetch, and only the values tell them
     * apart - the first run after adding the load still stopped with
     * paddr == P, which could be either. */
    LOG(LOG_CAT_MMS, LOG_INFO,
        "MFbus: %s context block: P=0x%X PS=0x%X CED=0x%X CAD=0x%X SRF13=0x%X STATUS=0x%X\n",
        c->name, (unsigned)c->cpu.PC, (unsigned)c->cpu.PS, (unsigned)c->cpu.CED,
        (unsigned)c->cpu.CAD, (unsigned)ndbus_context_read(&c->context, NDBUS_CTX_SRF13),
        (unsigned)ndbus_context_read(&c->context, NDBUS_CTX_STATUS));

    LOG(LOG_CAT_MMS, LOG_INFO, "MFbus: %s loaded from its context block, P=0x%X\n", c->name,
        (unsigned)c->cpu.PC);
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

int mfbus_service_nd5000_mailboxes(void)
{
    int answered = 0;

    /* FIRST, hand the ND-100 one frame that a station sent it. This is the
     * card's tick on the ND-100 thread, which is the only place the card may be
     * touched from. */
    mfbus_nd100_deliver_inbound();

    for (int i = 0; i < s_nd5000_count; i++)
    {
        if (ndbus_nd5000_service_mailbox(&s_nd5000[i]))
        {
            answered++;
        }
    }

    return answered;
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
        /* AFTER the unregister, so no frame can reach a station whose
         * control-store buffer has just been freed. The station keeps a 256 KB
         * control store once SINTRAN has loaded microcode into it, and the memset
         * below would drop the pointer without freeing it. */
        ndbus_nd5000_destroy(&s_nd5000[i]);
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

    /* The ND-100's own place on the bus, and anything still waiting for a card
     * that is about to go away. */
    if (s_nd100_registered)
    {
        (void)ndbus_fabric_unregister(&s_fabric, s_nd100_station.number);
        s_nd100_registered = false;
    }
    ndbus_lock();
    s_inbound_head = 0;
    s_inbound_count = 0;
    s_inbound_queued = 0;
    s_inbound_delivered = 0;
    s_inbound_dropped = 0;
    ndbus_unlock();
    s_card = NULL;

    /* Unregister BEFORE freeing: a bank left in the table would hand the next
     * physical access a callback over a freed pool. */
    (void)mms_memory_bank_unregister(s_base_word);
    mms_set_tset_lock(NULL, NULL);
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

/* Read or write one of the table's registers in a station's CPU, BY ROW INDEX.
 *
 * By index and not by name, so a caller - the round-trip test above all - never
 * restates the register list. A test that keeps its own copy of the list cannot
 * catch a register that is missing from BOTH the save and the load, which is the
 * defect that actually happened: TOS and LL were read by the load, written by
 * nobody, and no test mentioned them.
 *
 * The mask is applied on the way in as well as the way out, because the block
 * only carries the width the microcode moves - PS is 13 bits, CED and CAD are
 * bytes - and a caller poking a full 32-bit marker into one of those must be
 * told what will actually survive rather than reading a mismatch as a bug.
 */
bool mfbus_context_register_set(uint8_t station_number, size_t index, uint32_t value)
{
    int slot = mfbus_slot_of(station_number);
    if (slot < 0 || !s_cpus[slot].present || index >= MFBUS_CTX_FIELD_COUNT)
    {
        return false;
    }
    const MfbusCtxField *f = &s_ctx_fields[index];
    *mfbus_ctx_reg(&s_cpus[slot].cpu, f) = value & f->mask;
    return true;
}

bool mfbus_context_register_get(uint8_t station_number, size_t index, uint32_t *out_value)
{
    int slot = mfbus_slot_of(station_number);
    if (slot < 0 || !s_cpus[slot].present || index >= MFBUS_CTX_FIELD_COUNT ||
        out_value == NULL)
    {
        return false;
    }
    const MfbusCtxField *f = &s_ctx_fields[index];
    *out_value = *mfbus_ctx_reg(&s_cpus[slot].cpu, f) & f->mask;
    return true;
}

/* CNTXTSAVE on demand - the public mirror of mfbus_load_context(), which has
 * always been public. The pair is what makes the round-trip property testable at
 * all; without a way to drive the save, a test can only assert what the load
 * does with a block somebody else wrote. */
bool mfbus_store_context(uint8_t station_number)
{
    int slot = mfbus_slot_of(station_number);
    if (slot < 0 || !s_cpus[slot].present)
    {
        return false;
    }
    return mfbus_save_context(&s_cpus[slot]);
}
