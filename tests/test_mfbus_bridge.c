/*
 * test_mfbus_bridge.c - one array of bytes, reached from two machines.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * This is the join that makes the ND-100 and the ND-5000 one machine, so the
 * test is written from both sides: what the ND-5000 writes as bytes in the pool,
 * the ND-100 must read through its physical memory path as words at the
 * configured page - and the reverse.
 *
 * It also pins the three things that are easy to get wrong and silent when
 * wrong: the page-to-word arithmetic, which byte of a word is the high half,
 * and that the shared window is classified MPM5 rather than LOCAL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_types.h"
#include "mfbus_bridge.h"
#include "ndbus_pool.h"

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

/* A small machine so the pool sits well clear of installed RAM. */
#define TEST_LOCAL_WORDS 0x00040000u /* 256 KW = 512 KB */
#define TEST_BASE_PAGE   2112u       /* 004100B - the live-captured value */
#define TEST_POOL_BYTES  (1u * 1024u * 1024u)

static struct CpuRegs s_test_regs;

int main(void)
{
    printf("MFbus bridge tests - the ND-100 / ND-5000 join\n");
    printf("==============================================\n\n");

    memset(&s_test_regs, 0, sizeof(s_test_regs));
    g_reg = &s_test_regs;
    g_nd_memsize = TEST_LOCAL_WORDS;
    mms_memory_banks_init();

    CHECK(!mfbus_is_attached(), "nothing is attached to begin with");
    CHECK(mfbus_pool() == NULL, "and there is no pool");

    CHECK(mfbus_attach(TEST_POOL_BYTES, TEST_BASE_PAGE), "the MFbus attaches");
    CHECK(mfbus_is_attached(), "and reports itself attached");

    NdbusPool *pool = mfbus_pool();
    CHECK(pool != NULL, "the pool is reachable, so an ND-5000 can share it");
    if (pool == NULL)
    {
        return 1;
    }

    /* THE PLACEMENT. An ND-100 page is 1024 WORDS, so page 004100B (2112) puts
     * the window at word 0x210000 - which is byte 0x420000, the value captured
     * from a live machine. If this arithmetic were a shift too far either way
     * the window would land somewhere plausible and nothing would say so. */
    const uint32_t base_word = TEST_BASE_PAGE * 1024u;
    CHECK(base_word == 0x210000u, "page 004100B is word 0x210000");
    CHECK(base_word * 2u == 0x420000u, "which is byte 0x420000");

    /* THE CLASSIFICATION. The window must come back MPM5, not LOCAL: SINTRAN
     * picks the first MPM5 bank as the ND-500's shared memory, and a window that
     * identified as LOCAL would be claimed as ordinary ND-100 RAM instead. */
    CHECK(mms_get_physical_memory_type(base_word) == ND_MEM_MPM5, "the window is MPM5");
    CHECK(mms_get_physical_memory_type(0) == ND_MEM_LOCAL, "and local RAM is still LOCAL");
    CHECK(mms_get_physical_memory_type(base_word - 1) == ND_MEM_NONE,
          "the word below the window belongs to nobody");

    /* The window sits ABOVE installed local RAM. Reaching it at all proves the
     * backed-bank lookup happens before the g_nd_memsize bounds test - checking
     * bounds first would make every shared access a memory-out-of-range. */
    CHECK(base_word > g_nd_memsize, "the window is above installed local RAM");

    /* ---- ND-5000 writes, ND-100 reads -------------------------------------
     * The ND-5000 works in POOL BYTES; the ND-100 reads WORDS through its
     * physical path. Big-endian: the first byte of the pair is the high half. */
    CHECK(ndbus_pool_write8(pool, 0, 0x12), "ND-5000 writes the high byte at pool 0");
    CHECK(ndbus_pool_write8(pool, 1, 0x34), "and the low byte at pool 1");
    CHECK(mms_read_physical_memory((int)base_word, true) == 0x1234,
          "the ND-100 reads 0x1234 at the window base");

    CHECK(ndbus_pool_write8(pool, 200, 0xAB), "ND-5000 writes further in");
    CHECK(ndbus_pool_write8(pool, 201, 0xCD), "both bytes");
    CHECK(mms_read_physical_memory((int)(base_word + 100), true) == 0xABCD,
          "and word 100 of the window is pool bytes 200 and 201");

    /* ---- ND-100 writes, ND-5000 reads ------------------------------------- */
    mms_write_physical_memory((int)(base_word + 4), 0x5678, true);
    CHECK(ndbus_pool_read8(pool, 8) == 0x56, "the ND-100's word 4 is pool byte 8, high half");
    CHECK(ndbus_pool_read8(pool, 9) == 0x78, "and pool byte 9, low half");
    CHECK(ndbus_pool_read16(pool, 8) == 0x5678, "the ND-5000 reads the same word back");

    /* ---- half-word writes -------------------------------------------------
     * WRITEMODE_MSB is the HIGH byte, which is the FIRST byte of the pair.
     * Backwards, every half-word write lands 256 off and surfaces much later as
     * a corrupted page-table entry. */
    mms_write_physical_memory((int)(base_word + 6), 0x0000, true);
    mms_write_physical_memory_wm((int)(base_word + 6), 0xEE00, true, WRITEMODE_MSB);
    CHECK(ndbus_pool_read8(pool, 12) == 0xEE, "MSB lands in the FIRST byte of the pair");
    CHECK(ndbus_pool_read8(pool, 13) == 0x00, "and leaves the second alone");
    mms_write_physical_memory_wm((int)(base_word + 6), 0x0011, true, WRITEMODE_LSB);
    CHECK(ndbus_pool_read8(pool, 12) == 0xEE, "LSB leaves the high byte alone");
    CHECK(ndbus_pool_read8(pool, 13) == 0x11, "and lands in the second byte");
    CHECK(mms_read_physical_memory((int)(base_word + 6), true) == 0xEE11,
          "so the whole word reads back correctly");

    /* ---- the window does not bleed ---------------------------------------- */
    uint32_t last_word = base_word + (TEST_POOL_BYTES / 2u) - 1u;
    mms_write_physical_memory((int)last_word, 0x5A5A, true);
    CHECK(ndbus_pool_read16(pool, TEST_POOL_BYTES - 2u) == 0x5A5A,
          "the last word of the window is the last word of the pool");
    CHECK(mms_get_physical_memory_type(last_word) == ND_MEM_MPM5, "and is still MPM5");
    CHECK(mms_get_physical_memory_type(last_word + 1) == ND_MEM_NONE,
          "one word past the window belongs to nobody");

    /* ---- local RAM is untouched by any of it ------------------------------ */
    mms_write_physical_memory(0x100, 0xBEEF, true);
    CHECK(mms_read_physical_memory(0x100, true) == 0xBEEF, "local RAM still works");
    CHECK(ndbus_pool_read16(pool, 0) == 0x1234, "and writing it did not reach the pool");

    /* ---- attaching twice, and detaching ----------------------------------- */
    CHECK(!mfbus_attach(TEST_POOL_BYTES, TEST_BASE_PAGE), "attaching twice is refused");

    mfbus_detach();
    CHECK(!mfbus_is_attached(), "detach reports itself");
    CHECK(mfbus_pool() == NULL, "and the pool is gone");
    /* The bank must be OUT of the table: one left behind would hand the next
     * physical access a callback over a freed pool. */
    CHECK(mms_get_physical_memory_type(base_word) == ND_MEM_NONE,
          "and the bank is unregistered, not left pointing at freed memory");
    mfbus_detach(); /* must be safe twice */

    /* ---- a base page inside installed RAM is REFUSED ----------------------
     * Loudly, because a silently unregistered window leaves SINTRAN finding no
     * MPM5 memory at all and the ND-500 with nowhere to run. */
    CHECK(!mfbus_attach(TEST_POOL_BYTES, 0), "a window overlapping local RAM is refused");
    CHECK(!mfbus_is_attached(), "and nothing is left attached");
    CHECK(mms_get_physical_memory_type(0) == ND_MEM_LOCAL, "local RAM survives the refusal");

    /* And it can be attached properly afterwards - the refusal left no debris. */
    CHECK(mfbus_attach(TEST_POOL_BYTES, TEST_BASE_PAGE), "a correct attach still works after");
    mfbus_detach();

    /* ---- the octobus stations ---------------------------------------------
     * Seven slots, 070B..076B, each with the shared pool behind it. */
    CHECK(mfbus_attach(TEST_POOL_BYTES, TEST_BASE_PAGE), "reattach for the station tests");
    CHECK(mfbus_nd5000_count() == 0, "no stations to begin with");

    CHECK(mfbus_add_nd5000(56), "an ND-5000 joins at 070B");
    CHECK(mfbus_nd5000_count() == 1, "and is counted");
    CHECK(mfbus_add_nd5000(57), "a second at 071B");
    CHECK(mfbus_nd5000_count() == 2, "two on the bus");

    /* A station number outside 070B..076B belongs to another kind of device -
     * 010B is a SCSI controller. */
    CHECK(!mfbus_add_nd5000(8), "010B is refused");
    CHECK(!mfbus_add_nd5000(1), "and so is the ND-100's own 1B");
    CHECK(!mfbus_add_nd5000(63), "and 077B, which is not in the table");
    CHECK(mfbus_nd5000_count() == 2, "and none of those changed the count");

    /* Two CPUs on one station would answer each other's messages. */
    CHECK(!mfbus_add_nd5000(56), "070B a second time is refused");
    CHECK(mfbus_nd5000_count() == 2, "still two");

    /* All seven fit, and there is no eighth. */
    CHECK(mfbus_add_nd5000(58), "072B");
    CHECK(mfbus_add_nd5000(59), "073B");
    CHECK(mfbus_add_nd5000(60), "074B");
    CHECK(mfbus_add_nd5000(61), "075B");
    CHECK(mfbus_add_nd5000(62), "076B - the last slot");
    CHECK(mfbus_nd5000_count() == 7, "seven ND-5000s, the hardware's full complement");

    mfbus_clear_nd5000();
    CHECK(mfbus_nd5000_count() == 0, "the bus can be cleared without detaching the pool");
    CHECK(mfbus_is_attached(), "and the pool is still there");
    CHECK(ndbus_pool_read32(mfbus_pool(), 0) == 0, "with its contents intact");
    CHECK(mfbus_add_nd5000(56), "and a station can be added again afterwards");

    mfbus_detach();

    /* A station with no pool has nowhere to execute - the ND-5000 has no
     * private memory at all. */
    CHECK(!mfbus_add_nd5000(56), "an ND-5000 with no pool attached is refused");
    CHECK(mfbus_nd5000_count() == 0, "and none is created");

    /* ---- a REAL ND-500 CPU running out of the shared pool -------------------
     *
     * The last join: an Nd500Machine whose memory IS the pool, so an instruction
     * the CPU fetches is a byte the ND-100 can write through its MPM-5 window.
     */
    CHECK(mfbus_attach(TEST_POOL_BYTES, TEST_BASE_PAGE), "reattach for the CPU test");
    CHECK(mfbus_add_nd5000(56), "a station at 070B");

    CHECK(!mfbus_attach_cpu(57), "a CPU for a station that is not there is refused");
    CHECK(mfbus_attach_cpu(56), "the station gets a CPU");
    CHECK(!mfbus_attach_cpu(56), "and a second CPU for it is refused");

    /* The CPU is created STOPPED. The ND-120 starts a microprogram with an ACCP
     * STARTMIC over the octobus - the emulator does not decide to. */
    CHECK(mfbus_nd5000_instructions(56) == 0, "the CPU has executed nothing yet");

    /* Put something recognisable in the pool through the ND-100 side, and it is
     * the same memory the CPU was given. */
    const uint32_t probe_word = base_word + 0x40;
    mms_write_physical_memory((int)probe_word, 0xC0DE, true);
    CHECK(ndbus_pool_read16(mfbus_pool(), 0x80) == 0xC0DE,
          "an ND-100 write reaches the bytes the CPU runs out of");

    /* Start the thread, let it run, stop it. Whatever the CPU does with the
     * zeroed pool - executes, faults, halts - the point is that the thread
     * starts, is asked to stop, and is JOINED before anything is freed. */
    CHECK(mfbus_start_nd5000(56), "the CPU's host thread starts");
    mfbus_stop_nd5000(56);
    CHECK(true, "and it stops and joins cleanly");

    /* Tearing the bus down with a CPU attached must stop and join it first: a
     * running thread holds pointers to the machine, the CPU and the pool. */
    mfbus_clear_nd5000();
    CHECK(mfbus_nd5000_count() == 0, "the bus clears with a CPU attached");
    CHECK(mfbus_is_attached(), "and the pool survives");
    CHECK(ndbus_pool_read16(mfbus_pool(), 0x80) == 0xC0DE, "with its contents intact");

    /* The pool is NOT freed by the CPU that borrowed it - that is what
     * owns_memory is for. Detaching afterwards is what frees it. */
    mfbus_detach();
    CHECK(!mfbus_is_attached(), "and detaching afterwards is clean");

    printf("\n%d check(s), %d failed\n", s_checks, s_failed);
    if (s_failed != 0)
    {
        printf("FAIL\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
