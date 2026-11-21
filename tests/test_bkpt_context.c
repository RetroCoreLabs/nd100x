/*
 * test_bkpt_context.c - breakpoints and single steps belong to one program.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Under SINTRAN III many programs run on the same virtual addresses: the monitor
 * and the drivers on their own interrupt levels, and the RT programs on one level
 * with their own page tables (the PCR of that level). The debugger names one
 * program by its level and its PCR. These checks run the real cpu_run() and the
 * breakpoint manager (cpu_bkpt.c) on a two-level program:
 *   - a breakpoint on the first instruction a level runs after a level switch
 *     stops there (before, it was never hit: the switch was made at the start of
 *     the next tick, after the breakpoint check);
 *   - a breakpoint tied to a level and PCR does not stop other code at the same
 *     address, and does stop its own program;
 *   - a user breakpoint with no level of its own follows the program of the
 *     session (bkpt_program_set), also the one taken from the first stop;
 *   - a single step of the program on level 0 does not end on level 1 when the
 *     instruction switches level: it ends on the next instruction of level 0,
 *     after level 1 has run and given the CPU back.
 *
 * The program, physical words, paging off, interrupt system on:
 *   level 0:  0: SAA 2      A = 2
 *             1: TRR PID    PID = A: asks for level 1
 *             2: SAA 3      A = 3
 *             3: JMP *+0    stays here
 *   level 1:  100: SAA 5    A = 5
 *             101: WAIT     gives level 1 up, back to level 0
 *             102: JMP *+0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "cpu_types.h"

// clang-format off
#define OP_SAA_2   0170402
#define OP_SAA_3   0170403
#define OP_SAA_5   0170405
#define OP_TRR_PID 0150106
#define OP_WAIT    0151000
#define OP_JMP_0   0124000   /* JMP, P-relative displacement 0 */
#define L1_START   0100
#define PCR_L0     0000000
#define PCR_L1     0004204   /* a PCR measured for an RT program on level 1 */
#define PCR_OTHER  0021606   /* the PCR of another program measured on level 1 */
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

/* Zeroed registers, local memory, MMS-1 tables and the dispatch table (as
 * test_cpu_ring.c), the program above, level 0 at word 0, level 1 enabled. */
static void fixture(int32_t pcr_level1)
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
    static bool s_dispatch_built = false;
    if (!s_dispatch_built)
    {
        cpu_instructions(); /* once: a second build warns for every opcode */
        s_dispatch_built = true;
    }

    mms_write_physical_memory(0, OP_SAA_2, true);
    mms_write_physical_memory(1, OP_TRR_PID, true);
    mms_write_physical_memory(2, OP_SAA_3, true);
    mms_write_physical_memory(3, OP_JMP_0, true);
    mms_write_physical_memory(L1_START, OP_SAA_5, true);
    mms_write_physical_memory(L1_START + 1, OP_WAIT, true);
    mms_write_physical_memory(L1_START + 2, OP_JMP_0, true);

    g_reg->reg_STS = (uint16_t)(1u << STS_INTERRUPT_ON); /* level 0, interrupts on */
    g_reg->reg[1][_P] = L1_START;
    g_reg->reg_PCR[0] = PCR_L0;
    g_reg->reg_PCR[1] = (uint16_t)pcr_level1;
    gPIE = 0x0002; /* level 1 enabled; TRR PID with A = 2 asks for it */
    gPID = 0;
    gPC = 0;
    gA = 0;
    gDebuggerEnabled = true;

    if (g_breakpoint_mgr)
    {
        bkpt_manager_clear();
    }
    bkpt_program_set(-1, -1, false);
    cpu_set_stop_reason(STOP_REASON_NONE);
    cpu_set_run_mode(CPU_RUNNING);
}

int main(void)
{
    printf("Breakpoint and step context tests\n");
    printf("=================================\n\n");

    /* ---- the first instruction of a level entered by a switch is checked ---- */
    fixture(PCR_L1);
    bkpt_manager_add(L1_START, BP_TYPE_INSTRUCTION, NULL, NULL, NULL);
    cpu_run(20);
    CHECK(cpu_get_stop_reason() == STOP_REASON_INSTRUCTION_BREAKPOINT,
          "breakpoint on the first instruction of level 1 stops");
    CHECK(gPIL == 1 && gPC == L1_START, "stopped on level 1 at 000100, before it ran");
    CHECK(g_reg->reg[1][_A] == 0, "SAA 5 on level 1 has not run");

    /* ---- a breakpoint of another program does not stop this one ---- */
    fixture(PCR_L1);
    bkpt_manager_add_ctx(L1_START, BP_TYPE_INSTRUCTION, NULL, NULL, NULL, 1, PCR_OTHER);
    cpu_run(20);
    CHECK(cpu_get_stop_reason() == STOP_REASON_NONE,
          "breakpoint with another PCR does not stop the program at the same address");
    CHECK(g_reg->reg[1][_A] == 5, "level 1 ran through the address");

    fixture(PCR_L1);
    bkpt_manager_add_ctx(L1_START, BP_TYPE_INSTRUCTION, NULL, NULL, NULL, 0, PCR_L1);
    cpu_run(20);
    CHECK(cpu_get_stop_reason() == STOP_REASON_NONE,
          "breakpoint for level 0 does not stop level 1 at the same address");

    fixture(PCR_L1 | 3); /* ring bits differ: the same program */
    bkpt_manager_add_ctx(L1_START, BP_TYPE_INSTRUCTION, NULL, NULL, NULL, 1, PCR_L1);
    cpu_run(20);
    CHECK(cpu_get_stop_reason() == STOP_REASON_INSTRUCTION_BREAKPOINT && gPIL == 1,
          "breakpoint with the program's level and PCR stops it, whatever its ring");

    /* ---- a user breakpoint follows the program of the session ---- */
    fixture(PCR_L1);
    bkpt_program_set(1, PCR_OTHER, false);
    bkpt_manager_add(L1_START, BP_TYPE_USER, NULL, NULL, NULL);
    cpu_run(20);
    CHECK(cpu_get_stop_reason() == STOP_REASON_NONE,
          "session program is another one: its breakpoint does not stop this program");

    fixture(PCR_L1);
    bkpt_program_set(-1, -1, true);
    bkpt_manager_add(L1_START, BP_TYPE_USER, NULL, NULL, NULL);
    cpu_run(20);
    int8_t pil = -1;
    int32_t pcr = -1;
    CHECK(cpu_get_stop_reason() == STOP_REASON_BREAKPOINT, "first stop with from-first-stop");
    CHECK(bkpt_program_get(&pil, &pcr) && pil == 1 && pcr == PCR_L1,
          "the session program is taken from the first stop");

    /* ---- a condition can name the program by PCR ---- */
    fixture(PCR_L1);
    bkpt_manager_add(L1_START, BP_TYPE_INSTRUCTION, "PCR == 2180", NULL, NULL); /* 004204 */
    cpu_run(20);
    CHECK(cpu_get_stop_reason() == STOP_REASON_INSTRUCTION_BREAKPOINT,
          "condition on PCR is true for the program");

    /* ---- a step of level 0 over a level switch ends on level 0 ---- */
    fixture(PCR_L1);
    cpu_run(1); /* SAA 2 */
    CHECK(gPIL == 0 && gPC == 1, "on level 0 before TRR PID");
    bkpt_manager_step_one_ctx(0, PCR_L0);
    cpu_run(20);
    CHECK(cpu_get_stop_reason() == STOP_REASON_STEP, "the step ends");
    CHECK(gPIL == 0 && gPC == 2, "step over TRR PID ends on level 0 at word 2");
    CHECK(g_reg->reg[1][_A] == 5, "level 1 ran during the step");
    CHECK(gA == 2, "SAA 3 on level 0 has not run yet");

    /* ---- a step with no program follows the CPU, as before ---- */
    fixture(PCR_L1);
    cpu_run(1);
    bkpt_manager_step_one();
    cpu_run(20);
    CHECK(cpu_get_stop_reason() == STOP_REASON_STEP && gPIL == 1 && gPC == L1_START,
          "a step with no program ends on the next instruction the CPU runs");

    printf("\n%d checks, %d failed\n", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
