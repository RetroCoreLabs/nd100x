/*
 * test_ident.c - IDENT when no device answers.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * ND-06.015.02 (ND-100 Functional Description) section 6.7.4: "If the IDENT
 * instruction is executed, but there is no device to serve, the A register is
 * unchanged. An IOX error interrupt to level 14 will occur if enabled."
 * ND-06.029.1 (ND-110 Instruction Set) says the same about A; the IOX error is
 * internal interrupt 7, IID/IIE bit 7.
 *
 * nd100x used to set A to 0 and raise nothing, which SCSI-TV-B00 adapter test 4
 * reported as "Constant interrupt source on level 11. Ident code = 000000B"
 * (the former BUGS.md C2). With no device registered, every level must behave the same:
 * the old code also had an exception for level 13.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "cpu_types.h"

// clang-format off
#define OP_IDENT_PL11 0143611
#define OP_IDENT_PL13 0143643
#define IOX_ERROR_BIT (1 << 7)
#define LEVEL14_BIT   (1 << 14)
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

/* The ND-110 vector test's setup: zeroed registers, local memory, MMS-1 tables
 * and the dispatch table. No device is registered, so nothing answers IDENT. */
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
}

static void run_ident(uint16_t op, uint16_t iie)
{
    gA = 0072312;
    gIID = 0;
    gIIE = iie;
    gPID = 0;
    gPC = 1;
    g_instr_funcs[op](op);
}

int main(void)
{
    printf("IDENT with no device tests\n");
    printf("==========================\n\n");

    fixture();

    /* IOX error enabled: A unchanged, IID bit 7, level 14 requested. */
    run_ident(OP_IDENT_PL11, IOX_ERROR_BIT);
    CHECK(gA == 0072312, "IDENT PL11, nothing on level 11: A is unchanged");
    CHECK((gIID & IOX_ERROR_BIT) != 0, "IDENT PL11, nothing on level 11: IOX error in IID");
    CHECK((gPID & LEVEL14_BIT) != 0, "IDENT PL11 with IIE bit 7: level 14 requested");

    /* IOX error not enabled: IID still records it, but no level 14. */
    run_ident(OP_IDENT_PL11, 0);
    CHECK(gA == 0072312, "IDENT PL11, IOX error disabled: A is unchanged");
    CHECK((gIID & IOX_ERROR_BIT) != 0, "IDENT PL11, IOX error disabled: IID bit 7 still set");
    CHECK((gPID & LEVEL14_BIT) == 0, "IDENT PL11 without IIE bit 7: no level 14");

    /* Level 13 is no exception. */
    run_ident(OP_IDENT_PL13, IOX_ERROR_BIT);
    CHECK(gA == 0072312, "IDENT PL13, nothing on level 13: A is unchanged");
    CHECK((gIID & IOX_ERROR_BIT) != 0, "IDENT PL13, nothing on level 13: IOX error in IID");
    CHECK((gPID & LEVEL14_BIT) != 0, "IDENT PL13 with IIE bit 7: level 14 requested");

    printf("\n%d checks, %d failed\n", s_checks, s_failed);
    return s_failed == 0 ? 0 : 1;
}
