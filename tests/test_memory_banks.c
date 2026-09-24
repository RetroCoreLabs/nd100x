/*
 * test_memory_banks.c - Registered physical memory banks and the ECC/parity probe.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Two layers:
 *
 *   Layer 1 - the bank table itself: register / lookup / unregister, the refusal
 *             of an overlapping range, and the WORD-address rule.
 *   Layer 2 - the ECC/parity probe that SINTRAN's OPPSTART uses to tell LOCAL
 *             (KMECCR) memory from MPM-5: arm the ECCR simulate bits, write,
 *             clear DisableECC, read back, and see whether a level-14 parity
 *             interrupt is raised. LOCAL must raise it, MPM-5 must stay silent.
 */

#include <stdio.h>
#include <string.h>

#include "cpu_types.h"

static int s_failed = 0;

#define CHECK(cond, msg)                                                                 \
    do                                                                                   \
    {                                                                                    \
        if (!(cond))                                                                     \
        {                                                                                \
            printf("  FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);                   \
            s_failed++;                                                                  \
        }                                                                                \
    } while (0)

/* A small installed size keeps the test cheap and, crucially, makes the
 * byte-vs-word confusion VISIBLE: with 0x40000 words installed, word address
 * 0x30000 is LOCAL but the byte address of the same cell (0x60000) is not. */
#define TEST_LOCAL_WORDS 0x00040000u /* 256 KW = 512 KB */
#define TEST_MPM5_START  0x00100000u /* word address, well above local RAM */
#define TEST_MPM5_WORDS  0x00080000u

/* Minimal CPU fixture: the register set the gECCR / gIID / gIIE macros resolve
 * through, and the installed memory size. cpu_init() is deliberately NOT called -
 * it builds the paging tables and the dispatch table, none of which the physical
 * memory path under test touches. */
static struct CpuRegs s_test_regs;

static void banks_fixture(void)
{
    memset(&s_test_regs, 0, sizeof(s_test_regs));
    g_reg = &s_test_regs;
    g_nd_memsize = TEST_LOCAL_WORDS;
    mms_memory_banks_init();
}

static int run_bank_table_tests(void)
{
    int before = s_failed;
    printf("Layer 1: bank table\n");

    banks_fixture();

    /* Local RAM is registered by mms_memory_banks_init() and nothing else is. */
    CHECK(mms_get_physical_memory_type(0) == ND_MEM_LOCAL, "word 0 is LOCAL");
    CHECK(mms_get_physical_memory_type(TEST_LOCAL_WORDS - 1) == ND_MEM_LOCAL,
          "last installed word is LOCAL");
    CHECK(mms_get_physical_memory_type(TEST_LOCAL_WORDS) == ND_MEM_NONE,
          "first word above installed RAM is NONE");
    CHECK(mms_memory_bank_lookup(TEST_LOCAL_WORDS) == NULL, "no bank above installed RAM");

    /* An MPM-5 window registers, is found, and reads back as KMPM5. */
    CHECK(mms_memory_bank_register(TEST_MPM5_START, TEST_MPM5_WORDS, ND_MEM_MPM5),
          "MPM5 window registers");
    CHECK(mms_get_physical_memory_type(TEST_MPM5_START) == ND_MEM_MPM5, "MPM5 base is MPM5");
    CHECK(mms_get_physical_memory_type(TEST_MPM5_START + TEST_MPM5_WORDS - 1) == ND_MEM_MPM5,
          "MPM5 last word is MPM5");
    CHECK(mms_get_physical_memory_type(TEST_MPM5_START + TEST_MPM5_WORDS) == ND_MEM_NONE,
          "one word past the MPM5 window is NONE");
    CHECK(mms_get_physical_memory_type(TEST_MPM5_START - 1) == ND_MEM_NONE,
          "one word before the MPM5 window is NONE");

    /* Overlap is refused outright - not resolved by priority. */
    CHECK(!mms_memory_bank_register(TEST_MPM5_START + 1, 16, ND_MEM_MPM5),
          "a range inside an existing bank is refused");
    CHECK(!mms_memory_bank_register(TEST_MPM5_START - 8, 16, ND_MEM_LOCAL),
          "a range straddling the start of an existing bank is refused");
    CHECK(!mms_memory_bank_register(0, TEST_LOCAL_WORDS, ND_MEM_MPM5),
          "re-registering local RAM is refused");
    /* ...and the refusal registered nothing: the types are unchanged. */
    CHECK(mms_get_physical_memory_type(0) == ND_MEM_LOCAL, "local RAM survives a refused overlap");
    CHECK(mms_get_physical_memory_type(TEST_MPM5_START + 1) == ND_MEM_MPM5,
          "MPM5 survives a refused overlap");

    /* A range that exactly abuts an existing bank is NOT an overlap. */
    CHECK(mms_memory_bank_register(TEST_MPM5_START + TEST_MPM5_WORDS, 1024, ND_MEM_MPM5),
          "an abutting range registers");
    CHECK(mms_memory_bank_unregister(TEST_MPM5_START + TEST_MPM5_WORDS),
          "the abutting range unregisters");

    /* Degenerate ranges. */
    CHECK(!mms_memory_bank_register(0x200000u, 0, ND_MEM_MPM5), "a zero-length range is refused");
    CHECK(!mms_memory_bank_register(0xFFFFFF00u, 0x400, ND_MEM_MPM5),
          "a range wrapping the address space is refused");

    /* Unregister removes exactly that bank, by its start address. */
    CHECK(!mms_memory_bank_unregister(TEST_MPM5_START + 1), "unregister needs the exact start");
    CHECK(mms_memory_bank_unregister(TEST_MPM5_START), "MPM5 window unregisters");
    CHECK(mms_get_physical_memory_type(TEST_MPM5_START) == ND_MEM_NONE,
          "the unregistered window is gone");
    CHECK(mms_get_physical_memory_type(0) == ND_MEM_LOCAL, "local RAM is untouched by unregister");

    /* The table is bounded, and hitting the bound changes nothing else. */
    banks_fixture();
    uint32_t base = TEST_MPM5_START;
    int      registered = 0;
    for (int i = 0; i < ND_MEMORY_BANK_MAX + 4; i++)
    {
        if (mms_memory_bank_register(base + (uint32_t)i * 4096u, 4096, ND_MEM_MPM5))
        {
            registered++;
        }
    }
    CHECK(registered == ND_MEMORY_BANK_MAX - 1, "local RAM plus the bound is what fits");
    CHECK(mms_get_physical_memory_type(0) == ND_MEM_LOCAL,
          "local RAM still classified at the bound");

    /* REGRESSION: classify by WORD address, never by byte address.
     * The physical path builds `((ppn << 10) | dip)`, a WORD address. If a
     * caller ever passes the BYTE address of the same cell, everything in the
     * upper half of installed RAM falls out of the table. This test names the
     * confusion so it cannot come back silently. */
    banks_fixture();
    uint32_t word_addr = TEST_LOCAL_WORDS - 16;     /* inside installed RAM */
    uint32_t byte_addr = word_addr * 2u;            /* same cell, byte address */
    CHECK(mms_get_physical_memory_type(word_addr) == ND_MEM_LOCAL,
          "word address inside installed RAM is LOCAL");
    CHECK(mms_get_physical_memory_type(byte_addr) == ND_MEM_NONE,
          "the BYTE address of that same cell is NOT in the table - word addresses only");

    printf("  %d check(s) failed\n", s_failed - before);
    return s_failed - before;
}

/* -- Layer 2: the ECC/parity probe -------------------------------------------
 *
 * This is the sequence SINTRAN's OPPSTART uses to tell LOCAL from MPM-5, and it
 * is the reason the bank table has to exist: the probe code was already correct,
 * it simply had no memory behind the old hard-coded window to classify.
 *
 * ECCR bits used here (see nd_ecc_write_latch / nd_ecc_read_detect in cpu_mms.c):
 *   bit 0  SimBit0            - simulate a bad ECC on the next write
 *   bit 2  parity interrupt   - must be set for an error to raise level 14
 *   bit 3  DisableECC         - gates DETECTION on read only, never the latch
 */
static void probe_arm_write(void)
{
    gECCR = (uint16_t)((1 << 0) | (1 << 3) | (1 << 2)); /* simulate + disable + int enable */
}

static void probe_arm_read(void)
{
    gECCR = (uint16_t)(1 << 2); /* detection on, no simulate bit, DisableECC cleared */
}

/* Run one full probe at a physical WORD address; returns true if a level-14
 * parity interrupt (IIC PTY, bit 8) was raised by the read-back. */
static bool probe_bank(int physical_word_address)
{
    probe_arm_write();
    mms_write_physical_memory(physical_word_address, 0x1234, true);
    probe_arm_read();
    gIID = 0;
    (void)mms_read_physical_memory(physical_word_address, true);
    gECCR = 0;
    return (gIID & (1 << 8)) != 0;
}

static int run_ecc_probe_tests(void)
{
    int before = s_failed;
    printf("Layer 2: LOCAL / MPM5 ECC probe\n");

    banks_fixture();
    gIIE = 0xFFFF;

    /* LOCAL memory answers the probe: it carries ECC, so the simulated bad word
     * raises the level-14 parity interrupt on read-back. */
    CHECK(probe_bank(0x100), "LOCAL RAM raises the parity interrupt");
    CHECK(probe_bank((int)TEST_LOCAL_WORDS - 1), "the last LOCAL word raises it too");

    /* The latch is consumed by the read, so an immediate second read is silent. */
    gIID = 0;
    (void)mms_read_physical_memory(0x100, true);
    CHECK((gIID & (1 << 8)) == 0, "the latch is consumed by the first read");

    /* A clean write CLEARS a previously latched word (store-on-write). */
    probe_arm_write();
    mms_write_physical_memory(0x200, 0x1111, true);
    gECCR = (uint16_t)((1 << 3) | (1 << 2)); /* no simulate bit -> clean write */
    mms_write_physical_memory(0x200, 0x2222, true);
    probe_arm_read();
    gIID = 0;
    (void)mms_read_physical_memory(0x200, true);
    gECCR = 0;
    CHECK((gIID & (1 << 8)) == 0, "a clean write clears the word's latched bad ECC");

    /* An MPM-5 bank is silent: no ECC network lives on multiport memory, so the
     * probe finds nothing and SINTRAN leaves the bank classified as MPM5.
     * Registered INSIDE the installed backing store so the only thing that can
     * make it silent is its TYPE, not a missing backing array. */
    banks_fixture();
    uint32_t mpm5_start = TEST_LOCAL_WORDS / 2;
    uint32_t mpm5_len   = TEST_LOCAL_WORDS / 4;
    CHECK(mms_memory_bank_unregister(0), "local RAM unregisters for the split");
    CHECK(mms_memory_bank_register(0, mpm5_start, ND_MEM_LOCAL), "lower half is LOCAL");
    CHECK(mms_memory_bank_register(mpm5_start, mpm5_len, ND_MEM_MPM5), "middle is MPM5");
    CHECK(mms_memory_bank_register(mpm5_start + mpm5_len,
                                   TEST_LOCAL_WORDS - (mpm5_start + mpm5_len), ND_MEM_LOCAL),
          "upper part is LOCAL again");

    CHECK(!probe_bank((int)mpm5_start), "MPM5 stays silent under the probe");
    CHECK(!probe_bank((int)(mpm5_start + mpm5_len - 1)), "the last MPM5 word stays silent");
    CHECK(probe_bank((int)mpm5_start - 1), "the LOCAL word just below MPM5 still answers");
    CHECK(probe_bank((int)(mpm5_start + mpm5_len)), "the LOCAL word just above MPM5 answers");

    gIIE = 0;
    gECCR = 0;
    printf("  %d check(s) failed\n", s_failed - before);
    return s_failed - before;
}

int run_memory_bank_tests(void)
{
    int failed = 0;
    failed += run_bank_table_tests();
    failed += run_ecc_probe_tests();
    return failed;
}
