/*
 * test_cpu_ring.c - the CPU trace ring as the debugger reads it.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * The DAP commands setCpuTracing and getCpuTraceRing (shared with RetroCore)
 * switch and read the same ring --ring-dump prints. These checks run a short
 * loop through the real cpu_run() and read the ring back with
 * cpu_ring_snapshot():
 *   - with recording off nothing is recorded;
 *   - with it on, every retired instruction is there, oldest first, with its
 *     PC, its instruction word and the A register after it;
 *   - a smaller max_entries gives the newest entries;
 *   - a PC filter keeps only the instructions fetched at that address.
 *
 * The program, at physical word 0 with paging off on level 0:
 *   0: SAA 1      A = 1
 *   1: AAA 1      A = A + 1
 *   2: AAA 1      A = A + 1
 *   3: JMP *-2    back to word 1
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "cpu_types.h"

// clang-format off
#define OP_SAA_1   0170401
#define OP_AAA_1   0172401
#define OP_JMP_M2  0124376   /* JMP, P-relative displacement -2 */
// clang-format on

#define TEST_LOCAL_WORDS 0x10000

static int s_failed = 0;
static int s_checks = 0;

#define CHECK(cond, msg)                                                                 \
    do                                                                                   \
    {                                                                                    \
        s_checks++;                                                                      \
        if (!(cond))                                                                     \
        {                                                                                \
            printf("  FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);                   \
            s_failed++;                                                                  \
        }                                                                                \
    } while (0)

static struct CpuRegs s_test_regs;
static CpuRingEntry s_snap[CPU_RING_CAPACITY];

/* The same setup the ND-110 vector test uses: zeroed registers, local memory,
 * MMS-1 paging tables and the dispatch table. */
static void fixture(void)
{
    memset(&s_test_regs, 0, sizeof(s_test_regs));
    g_reg = &s_test_regs;
    g_nd_memsize = TEST_LOCAL_WORDS;
    mms_memory_banks_init();
    g_mms_type = MMS1;
    if (!mms_create_paging_tables())
    {
        printf("  FAIL: paging tables did not allocate\n");
        s_failed++;
    }
    cpu_instructions();

    mms_write_physical_memory(0, OP_SAA_1, true);
    mms_write_physical_memory(1, OP_AAA_1, true);
    mms_write_physical_memory(2, OP_AAA_1, true);
    mms_write_physical_memory(3, OP_JMP_M2, true);
    gPC = 0;
    gA = 0;
    cpu_set_run_mode(CPU_RUNNING);
}

int main(void)
{
    printf("CPU trace ring tests\n");
    printf("====================\n\n");

    fixture();

    /* ---- recording off: nothing goes into the ring ---- */
    cpu_ring_set_tracing(false, false, 0);
    cpu_run(4);
    CHECK(cpu_ring_snapshot(s_snap, CPU_RING_CAPACITY) == 0, "nothing recorded while off");
    CHECK(!cpu_ring_tracing_enabled(), "reports off");

    /* ---- recording on: seven instructions, oldest first ---- */
    gPC = 0;
    gA = 0;
    cpu_ring_set_tracing(true, false, 0);
    CHECK(cpu_ring_tracing_enabled(), "reports on");
    uint64_t before = g_instr_counter;
    cpu_run(7);
    int ran = (int)(g_instr_counter - before);
    CHECK(ran == 7, "cpu_run(7) retires seven instructions");

    int n = cpu_ring_snapshot(s_snap, CPU_RING_CAPACITY);
    CHECK(n == 7, "seven entries recorded");
    static const unsigned short want_pc[7] = {0, 1, 2, 3, 1, 2, 3};
    static const unsigned short want_op[7] = {OP_SAA_1, OP_AAA_1, OP_AAA_1, OP_JMP_M2,
                                              OP_AAA_1, OP_AAA_1, OP_JMP_M2};
    static const unsigned short want_a[7] = {1, 2, 3, 3, 4, 5, 5};
    bool pcs = true, ops = true, as = true;
    for (int i = 0; i < 7 && i < n; i++)
    {
        if (s_snap[i].pc != want_pc[i])
        {
            printf("    entry %d: PC %06o, expected %06o\n", i, s_snap[i].pc, want_pc[i]);
            pcs = false;
        }
        if (s_snap[i].opcode != want_op[i])
        {
            ops = false;
        }
        if (s_snap[i].a_reg != want_a[i])
        {
            printf("    entry %d: A %06o, expected %06o\n", i, s_snap[i].a_reg, want_a[i]);
            as = false;
        }
    }
    CHECK(pcs, "PCs in execution order, oldest first");
    CHECK(ops, "each entry holds the instruction word");
    CHECK(as, "each entry holds A after the instruction");

    /* ---- a smaller max_entries gives the newest ones ---- */
    n = cpu_ring_snapshot(s_snap, 3);
    CHECK(n == 3, "three entries asked, three given");
    CHECK(n == 3 && s_snap[0].pc == 1 && s_snap[1].pc == 2 && s_snap[2].pc == 3,
          "the three newest, oldest first");

    /* ---- a PC filter keeps only that address ---- */
    cpu_ring_set_tracing(true, true, 2);
    uint16_t filter = 0;
    CHECK(cpu_ring_pc_filter(&filter) && filter == 2, "reports the filter");
    cpu_run(8); /* from word 1: 1 2 3 1 2 3 1 2 -> word 2 three times */
    n = cpu_ring_snapshot(s_snap, CPU_RING_CAPACITY);
    CHECK(n == 7 + 3, "three more entries, one per pass through word 2");
    bool only2 = n == 10;
    for (int i = 7; i < n; i++)
    {
        if (s_snap[i].pc != 2)
        {
            only2 = false;
        }
    }
    CHECK(only2, "the filtered entries are all at word 2");

    cpu_ring_set_tracing(true, false, 0);
    CHECK(!cpu_ring_pc_filter(NULL), "filter cleared");

    printf("\n%d checks, %d failed\n", s_checks, s_failed);
    return s_failed == 0 ? 0 : 1;
}
