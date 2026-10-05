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
#include <unistd.h>

#include "cpu_types.h"
#include "mfbus_bridge.h"
#include "devices_types.h"
#include "devices_protos.h"
#include "octobus/device_octobus.h"
#include "ndbus_context.h"
#include "mfbus_config.h"
#include "ndbus_pool.h"
#include "ndbus_octobus.h"

/* Data-available through the bitfield union, the way the device works with it. */
static bool octobus_in_status_ck(Device *card)
{
    OctobusInputStatus st;
    st.raw = card->Read(card, 0100402);
    return st.bits.dataAvailable != 0;
}

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
    /* A scratch directory for the load test's image. */
    char dir_tmpl[] = "/tmp/nd100x_mfbus_testXXXXXX";
    char *dir = mkdtemp(dir_tmpl);
    if (dir == NULL)
    {
        printf("mkdtemp failed\n");
        return 1;
    }

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
     * WRITEMODE_MSB is the HIGH byte, the FIRST byte of the pair. The byte to
     * store arrives in the LOW 8 bits of `value` for BOTH modes: SBYT passes gA,
     * BFILL passes `gA & 0xFF`, MOVB narrows with `& 0xFF` (cpu_instr.c), and
     * local RAM builds the high half with `(value << 8)`
     * (mms_write_physical_memory_wm in cpu_mms.c).
     *
     * THIS TEST USED TO PASS 0xEE00 FOR THE MSB WRITE, encoding the byte in the
     * high half - the same mistake the MFbus bank write itself was making - so
     * the two agreed with each other and disagreed with every real caller. Every
     * even-byte write into the shared window stored 0. Pass the byte the way the
     * CPU does, in the low half, or this check proves nothing. */
    mms_write_physical_memory((int)(base_word + 6), 0x0000, true);
    mms_write_physical_memory_wm((int)(base_word + 6), 0x00EE, true, WRITEMODE_MSB);
    CHECK(ndbus_pool_read8(pool, 12) == 0xEE, "MSB lands in the FIRST byte of the pair");
    CHECK(ndbus_pool_read8(pool, 13) == 0x00, "and leaves the second alone");
    mms_write_physical_memory_wm((int)(base_word + 6), 0x0011, true, WRITEMODE_LSB);
    CHECK(ndbus_pool_read8(pool, 12) == 0xEE, "LSB leaves the high byte alone");
    CHECK(ndbus_pool_read8(pool, 13) == 0x11, "and lands in the second byte");
    CHECK(mms_read_physical_memory((int)(base_word + 6), true) == 0xEE11,
          "so the whole word reads back correctly");

    /* The same byte written to the window and to LOCAL RAM must end up in the
     * same half of the word. This is the check that would have caught the bug:
     * it compares the two backing stores against each other rather than against
     * a hand-written expectation. */
    mms_write_physical_memory((int)(base_word + 8), 0x0000, true);
    mms_write_physical_memory(0x300, 0x0000, true);
    mms_write_physical_memory_wm((int)(base_word + 8), 0x0042, true, WRITEMODE_MSB);
    mms_write_physical_memory_wm(0x300, 0x0042, true, WRITEMODE_MSB);
    CHECK(mms_read_physical_memory((int)(base_word + 8), true) ==
              mms_read_physical_memory(0x300, true),
          "an MSB byte write lands identically in the window and in local RAM");

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

    /* ---- loading a program into the shared pool ---------------------------
     * The pool is SHARED, so a loader that writes from offset 0 would land on
     * the mailbox global header. The offset is explicit for that reason. */
    CHECK(mfbus_attach(TEST_POOL_BYTES, TEST_BASE_PAGE), "reattach for the load test");
    CHECK(mfbus_add_nd5000(56), "a station");
    CHECK(mfbus_attach_cpu(56), "with a CPU");

    CHECK(!mfbus_load_nd5000(57, "/nonexistent", 0), "loading for a station with no CPU fails");
    CHECK(!mfbus_load_nd5000(56, NULL, 0), "a NULL path is refused");
    CHECK(!mfbus_load_nd5000(56, "/nonexistent/image.bin", 0), "an unreadable file fails");

    {
        /* A small image with a recognisable pattern. */
        char img[300];
        snprintf(img, sizeof(img), "%s/mfbus_test_image.bin", dir);
        FILE *f = fopen(img, "wb");
        CHECK(f != NULL, "the test image is created");
        if (f)
        {
            static const uint8_t pattern[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
            fwrite(pattern, 1, sizeof(pattern), f);
            fclose(f);

            /* Load WELL AWAY from offset 0, where the mailbox lives. */
            const uint32_t load_at = 0x1000;
            CHECK(mfbus_load_nd5000(56, img, load_at), "the image loads at an explicit offset");
            CHECK(ndbus_pool_read8(mfbus_pool(), load_at) == 0xDE, "byte 0 landed");
            CHECK(ndbus_pool_read8(mfbus_pool(), load_at + 7) == 0x04, "and byte 7");
            CHECK(ndbus_pool_read8(mfbus_pool(), 0) == 0,
                  "and pool offset 0 - where the mailbox header lives - is UNTOUCHED");

            /* An image that does not fit at that offset is refused. */
            CHECK(!mfbus_load_nd5000(56, img, TEST_POOL_BYTES - 4),
                  "an image that overruns the pool is refused");

            /* A running CPU is refused: loading underneath a thread fetching
             * instructions leaves it executing half of each image. */
            CHECK(mfbus_start_nd5000(56), "start the CPU");
            bool refused_while_running = !mfbus_load_nd5000(56, img, load_at);
            mfbus_stop_nd5000(56);
            CHECK(refused_while_running, "loading into a RUNNING CPU is refused");
            CHECK(mfbus_load_nd5000(56, img, load_at), "and allowed again once it is stopped");
        }
    }

    mfbus_detach();

    /* ---- starting a CPU the way the hardware starts one --------------------
     * Not by poking a PC: the ND-100 places a register image in shared memory
     * and NEWCNTXT loads the machine from it. */
    CHECK(mfbus_attach(TEST_POOL_BYTES, TEST_BASE_PAGE), "reattach for the context test");
    CHECK(mfbus_add_nd5000(56), "a station");
    CHECK(mfbus_attach_cpu(56), "with a CPU");

    CHECK(!mfbus_load_context(56), "loading with no context placed is refused");

    const uint32_t ctx_area = 0x2000;
    CHECK(!mfbus_place_context(57, ctx_area, 0, 0), "a context for a station with no CPU fails");
    CHECK(mfbus_place_context(56, ctx_area, 0x00001234u, 0x00005678u), "a context is placed");

    /* The block is in the SHARED POOL where the microcode would read it, one
     * stride past the area base - so the ND-100 can see what it wrote. */
    NdbusContext view;
    CHECK(ndbus_context_attach(&view, mfbus_pool(), ctx_area, 0), "the ND-100 can see the block");
    CHECK(ndbus_context_read(&view, NDBUS_CTX_P) == 0x00001234u, "P is in the pool");
    CHECK(ndbus_context_read(&view, NDBUS_CTX_B) == 0x00005678u, "and B");

    /* Fill in fields of BOTH classes, then load. */
    CHECK(ndbus_context_write(&view, NDBUS_CTX_I1, 0x0A0A0A0Au), "a loaded register is set");
    CHECK(ndbus_context_write(&view, NDBUS_CTX_CED, 0x00000007u), "and CED");
    CHECK(ndbus_context_write(&view, NDBUS_CTX_DIT_TOS, 0xCAFEF00Du), "and a DIT-sourced one");

    CHECK(mfbus_load_context(56), "the CPU loads from its block");

    /* THE POINT: the loaded fields arrive, and the DIT-sourced one does NOT.
     * Copying TOS here would make the emulator honour a context the hardware
     * ignores - a bring-up that works here and not on the machine. */
    CHECK(mfbus_nd5000_instructions(56) == 0, "the CPU has still executed nothing");

    /* Starting and stopping proves the loaded P took effect: the CPU begins
     * fetching where the context said, not at 0. */
    CHECK(mfbus_start_nd5000(56), "the CPU starts");
    mfbus_stop_nd5000(56);
    CHECK(true, "and stops cleanly from a context-loaded state");

    /* Loading underneath a running CPU is refused, for the same reason loading
     * an image underneath one is. */
    CHECK(mfbus_start_nd5000(56), "start it again");
    bool refused = !mfbus_load_context(56);
    mfbus_stop_nd5000(56);
    CHECK(refused, "loading a context into a RUNNING CPU is refused");

    mfbus_detach();

    /* ---- THE ROUND-TRIP PROPERTY: save then load restores EVERY register ----
     *
     * This is the test that was missing, and its absence is why a real defect
     * shipped. CNTXTSAVE and NEWCNTXT used to be two hand-written register lists
     * in two functions a thousand lines apart, and TOS and LL ended up read by
     * the load and written by neither. Every process therefore ran with the
     * stack limits of whichever process ran before it. MEASURED 04-OCT-2026: the
     * SINTRAN swapper stack-overflowed at P=0x08008E09 immediately after a
     * domain's page fault switched away from it, faulting on address 0x00000004 -
     * a frame base of 4, not a missing page.
     *
     * WHY IT IS WRITTEN THIS WAY. The test drives the table by INDEX and never
     * names a register. A test that spelled out its own list of registers could
     * not have caught this one, because the register was missing from the save,
     * the load AND any such list - all three were written by the same hand on the
     * same day. Driving mfbus_context_field_count() means a register added to the
     * bridge is covered here the moment it is added, and one dropped from the
     * table fails the count check below rather than vanishing silently.
     *
     * Each register gets a DISTINCT marker, so a save or load that crosses two
     * slots shows up as the wrong value rather than as a value that happens to
     * match. The markers are masked per row because the block only carries the
     * width the microcode moves - PS is 13 bits, CED and CAD are bytes - and the
     * accessors apply that mask on the way in, so what is read back is what the
     * hardware would really have kept. */
    CHECK(mfbus_attach(TEST_POOL_BYTES, TEST_BASE_PAGE), "reattach for the round trip");
    CHECK(mfbus_add_nd5000(56), "a station for the round trip");
    CHECK(mfbus_attach_cpu(56), "with a CPU");
    CHECK(mfbus_place_context(56, ctx_area, 0x00001234u, 0x00005678u),
          "and a context block to round-trip through");

    size_t rows = mfbus_context_field_count();
    CHECK(rows >= 23u, "the context table carries at least P/L/B/R, I/A/E, ST1/ST2, "
                       "TOS/LL, CED/CAD and PS");

    /* A marker that is distinct per row AND has bits set in every byte, so a
     * truncation to a halfword or a byte cannot look like a pass. */
    bool all_set = true;
    for (size_t k = 0; k < rows; k++)
    {
        all_set = mfbus_context_register_set(56, k, 0xA5000000u | (uint32_t)((k + 1u) * 0x010101u))
                  && all_set;
    }
    CHECK(all_set, "every register in the table can be seeded with its own marker");

    /* What each register holds AFTER masking - that is what must survive. */
    uint32_t seeded[64];
    bool read_back = (rows <= 64u);
    for (size_t k = 0; k < rows && read_back; k++)
    {
        read_back = mfbus_context_register_get(56, k, &seeded[k]) && read_back;
    }
    CHECK(read_back, "and read back before the save");

    /* THE WHITELIST IS ITSELF ASSERTED, and this check is the reason the test has
     * any force at all.
     *
     * The per-row verdict below skips equality for a row marked saved=false,
     * because such a row is loaded-only by design. That escape hatch would
     * otherwise let anyone silence this test for any register by flipping one
     * flag - and "a check that is easy to skip will be skipped" is how the
     * original defect survived. MEASURED while writing this test: marking TOS
     * saved=false reintroduced the exact stack-overflow bug and the test still
     * passed, until this check existed.
     *
     * So the hatch is nailed shut: EXACTLY ONE row may be loaded-only, and it
     * must be PS at 0x48. PS earns it directionally - CNTXTLOAD at 0o14777 reads
     * that slot, so a value we invent there is consumed by the microcode as if
     * SINTRAN had written it. Any future loaded-only row has to come here and
     * argue for itself. */
    size_t loaded_only = 0;
    uint32_t loaded_only_offset = 0xFFFFFFFFu;
    const char *loaded_only_name = "none";
    for (size_t k = 0; k < rows; k++)
    {
        uint32_t off = 0, msk = 0;
        const char *nm = "?";
        bool sv = false;
        if (mfbus_context_field_info(k, &off, &nm, &msk, &sv) && !sv)
        {
            loaded_only++;
            loaded_only_offset = off;
            loaded_only_name = nm;
        }
    }
    CHECK(loaded_only == 1u,
          "exactly ONE context register may be loaded-only - any other is a "
          "register silently dropped from the save");
    CHECK(loaded_only_offset == NDBUS_CTX_SRF13,
          "and it is the slot at 0x48, which CNTXTLOAD reads for itself");
    CHECK(loaded_only_name != NULL && loaded_only_name[0] == 'P' &&
          loaded_only_name[1] == 'S' && loaded_only_name[2] == '\0',
          "and that register is PS - named, so a swap for another one fails here");

    CHECK(mfbus_store_context(56), "CNTXTSAVE writes the block");

    /* Wipe every register, so a value that reappears can only have come out of
     * the block. Without this a register the load never touches would still
     * hold its marker and the test would pass on a defect. */
    for (size_t k = 0; k < rows; k++)
    {
        (void)mfbus_context_register_set(56, k, 0u);
    }
    uint32_t after_wipe = 0xFFFFFFFFu;
    CHECK(mfbus_context_register_get(56, 0, &after_wipe) && after_wipe == 0u,
          "the registers really are wiped, so the load has to do the work");

    CHECK(mfbus_load_context(56), "NEWCNTXT loads the block back");

    /* THE VERDICT, PER REGISTER, BY NAME. One CHECK per row so a failure says
     * which register was lost instead of reporting a count that is one short. */
    for (size_t k = 0; k < rows; k++)
    {
        uint32_t offset = 0, mask = 0;
        const char *name = "?";
        bool saved = false;
        if (!mfbus_context_field_info(k, &offset, &name, &mask, &saved))
        {
            CHECK(false, "the table describes every row it counts");
            continue;
        }

        uint32_t got = 0xDEADBEEFu;
        bool ok = mfbus_context_register_get(56, k, &got);

        if (!saved)
        {
            /* A row the save deliberately does not write back, because the
             * microcode sources that slot itself - PS is the only one today. The
             * block still holds whatever was placed there, so the property is
             * that the load READ it, not that it round-tripped. Asserting
             * equality here would demand a write the hardware forbids. */
            CHECK(ok, name);
            continue;
        }

        CHECK(ok && got == seeded[k], name);
    }

    mfbus_detach();

    /* ---- the card on the REAL fabric, reaching a REAL ND-5000 -------------
     * TPE test 4's mechanism, but with the actual bus behind it: an Ident to a
     * station that exists is answered into the card's receive FIFO, and one to
     * a station that does not exist leaves the FIFO empty. */
    CHECK(mfbus_attach(TEST_POOL_BYTES, TEST_BASE_PAGE), "reattach for the card test");

    Device *card = octobus_create_device(0);
    CHECK(card != NULL, "an octobus card");
    if (card != NULL)
    {
        CHECK(mfbus_attach_card(card), "connects to the bus");
        CHECK(mfbus_add_nd5000(56), "an ND-5000 at 070B is on the bus");

        /* An Ident to 070B. The station answers, and the answer arrives in the
         * card's FIFO - which is where the hardware puts it. */
        card->Write(card, 0100405, (uint16_t)(0x8000u | (56u << 8u)));
        CHECK(octobus_in_status_ck(card) ||
                  octobus_rx_count(card) == 0,
              "the card either has a reply or the station was silent - both are defined");

        /* An Ident to an EMPTY station must leave nothing behind. A synthetic
         * "no answer" frame would make an absent station look like a quiet one,
         * and the guest would never time out. */
        int before = octobus_rx_count(card);
        card->Write(card, 0100405, (uint16_t)(0x8000u | (40u << 8u)));
        CHECK(octobus_rx_count(card) == before, "an absent station pushes NOTHING");

        /* Destination 0 is the card testing ITSELF - and with a CPU station on
         * the bus the reference does NOT echo it
         * ($RETROCORE/Emulated.HW/ND/CPU/NDBUS/NDBusOctobus.cs ProcessTransmitQueue:
         * the loopback branch is taken only while no CPU is attached). An ND-5000
         * was added above, so nothing comes back. This check expected the echo
         * until the card was ported to the reference on 05-OCT-2026; the echo
         * without a CPU attached is covered in test_octobus.c. */
        card->Write(card, 0100405, (uint16_t)(0x8000u | (0u << 8u)));
        CHECK(octobus_rx_count(card) == before,
              "destination 0 is not echoed once a CPU station is attached");

        /* A full multibyte ACCP exchange through the card: SOMB, the command
         * byte, EOMB - the path SINTRAN's bring-up actually uses. The station
         * answers Messack, which lands in the card's FIFO. */
        while (octobus_rx_count(card) > 0)
        {
            (void)card->Read(card, 0100400);
        }
        uint16_t dest = (uint16_t)(56u << 8u);
        /* THE MESSAGE CARRIES A TWO-BYTE HEADER IN FRONT OF THE COMMAND, and this
         * test used to leave it off - the same mistake the station made reading
         * it, which is why both agreed and neither was caught. On the wire it is
         *   SOMB | source OMD | byte count | command | parameters... | EOMB
         * exactly as captured from SINTRAN's ND-500 monitor. */
        /* SOMB: C=1, M=1, S=1, destination OMD 3 */
        card->Write(card, 0100405, (uint16_t)(dest | 0x8000u | 0x0020u | 0x0010u | 3u));
        card->Write(card, 0100405, (uint16_t)(dest | 0x03u));  /* our source OMD */
        card->Write(card, 0100405, (uint16_t)(dest | 0x03u));  /* 3 payload bytes */
        card->Write(card, 0100405, (uint16_t)(dest | 0x0Fu));  /* ECHO */
        card->Write(card, 0100405, (uint16_t)(dest | 0x01u));  /* its count: 1 byte */
        card->Write(card, 0100405, (uint16_t)(dest | 0xA5u));  /* the test byte */
        card->Write(card, 0100405, (uint16_t)(dest | 0x8000u | 0x0020u | 3u)); /* EOMB */
        CHECK(octobus_rx_count(card) > 0, "the ACCP answered through the card");
        CHECK(octobus_in_status_ck(card),
              "and the card reports data available");

        /* The reply is a bare Messack: the reference's station has no arm for
         * ECHO and takes the default acknowledge, so the test byte does NOT come
         * back (ported 05-OCT-2026; this check used to expect the echo). The
         * Messack's status byte 0x00 is in the stream. */
        bool saw_ack = false;
        bool saw_pattern = false;
        int  guard = 0;
        while (octobus_rx_count(card) > 0 && guard < 32)
        {
            uint16_t w = card->Read(card, 0100400);
            if ((w & 0x8000u) == 0u && (w & 0xFFu) == 0x00u)
            {
                saw_ack = true;
            }
            if ((w & 0xFFu) == 0xA5u)
            {
                saw_pattern = true;
            }
            guard++;
        }
        CHECK(saw_ack, "and the reply carries the Messack status byte");
        CHECK(!saw_pattern, "and no echoed byte, as the reference answers ECHO");

        /* ---- THE ND-100 IS A STATION TOO: a frame SENT TO it must arrive -----
         * The ND-5000 tells SINTRAN an answer is ready with the GIVEINT frame
         * 0x8101 - C, destination station 1, code 1 - sent on its own initiative,
         * not as a reply. MEASURED 05-OCT-2026 before station 1 was registered:
         * the bus had nobody at 1, every one of those frames was dropped without
         * a word, and SINTRAN found each answer only by its watchdog. */
        while (octobus_rx_count(card) > 0)
        {
            (void)card->Read(card, 0100400);
        }
        {
            unsigned long q0 = 0, d0 = 0, x0 = 0;
            mfbus_nd100_inbound_counts(&q0, &d0, &x0);

            uint16_t replies[NDBUS_MAX_REPLY_FRAMES];
            int n = ndbus_fabric_send(mfbus_fabric(), 56u, 0x8101u, replies);
            CHECK(n == 0, "a frame from 070B to station 1 is ACCEPTED - not the -1 of an "
                          "empty station");

            unsigned long q1 = 0, d1 = 0, x1 = 0;
            mfbus_nd100_inbound_counts(&q1, &d1, &x1);
            CHECK(q1 == q0 + 1u && d1 == d0, "it is queued, and not yet in the card");
            CHECK(octobus_rx_count(card) == 0,
                  "the card is untouched until its own tick - the sender may be another thread");

            /* The card's tick is what calls this on the ND-100 thread. */
            (void)mfbus_service_nd5000_mailboxes();
            mfbus_nd100_inbound_counts(&q1, &d1, &x1);
            CHECK(d1 == d0 + 1u && x1 == x0, "the tick delivers it, and nothing is dropped");
            CHECK(octobus_rx_count(card) == 1, "one frame is now in the receive FIFO");
            CHECK(octobus_in_status_ck(card), "and the card reports data available");

            uint16_t got = card->Read(card, 0100400);
            CHECK(got == (uint16_t)(0x8001u | (56u << 8u)),
                  "it reads back in RECEIVE format: bits 13-8 name the sender, 070B");

            /* ONE PER TICK, in order - each frame raises its own input event. */
            (void)ndbus_fabric_send(mfbus_fabric(), 56u, 0x8101u, replies);
            (void)ndbus_fabric_send(mfbus_fabric(), 56u, 0x8102u, replies);
            (void)mfbus_service_nd5000_mailboxes();
            CHECK(octobus_rx_count(card) == 1, "two queued, one tick: one frame delivered");
            uint16_t first = card->Read(card, 0100400);
            (void)mfbus_service_nd5000_mailboxes();
            uint16_t second = card->Read(card, 0100400);
            CHECK((first & 0xFFu) == 0x01u && (second & 0xFFu) == 0x02u,
                  "and they arrive in the order they were sent");
        }

        dev_destroy(card);
        free(card);
    }

    mfbus_detach();

    /* ---- an .ini in, a working machine out --------------------------------
     * The path Ronny actually uses. A configuration that parses cleanly and then
     * builds the wrong machine is a failure no parser test catches, and until
     * now mfbus_apply_config() had no test at all. */
    {
        char ini[300];
        snprintf(ini, sizeof(ini), "%s/machine.ini", dir);
        FILE *f = fopen(ini, "w");
        CHECK(f != NULL, "an .ini to apply");
        if (f)
        {
            /* A 2 MB pool so the test stays quick, at the live-captured base
             * page, with one ND-5000 at 070B. No octobus card: adding one needs
             * the device manager, which this test deliberately does not start. */
            fprintf(f, "[mfbus]\nsize = 2\nbase_page = 004100B\n\n");
            fprintf(f, "[nd5000.1]\nenabled = yes\nstation = 070B\n\n");
            fprintf(f, "[nd5000.2]\nenabled = no\nstation = 071B\n");
            fclose(f);

            MachineConfig mc;
            char cfgerr[MC_ERR_LEN];
            mc_set_defaults(&mc);
            CHECK(mc_load_file(&mc, ini, cfgerr, sizeof(cfgerr)), "the .ini loads");

            CHECK(mfbus_apply_config(&mc), "and builds the bus");
            CHECK(mfbus_is_attached(), "the pool is attached");
            CHECK(mfbus_pool() != NULL, "and reachable");
            CHECK(mfbus_pool()->size == 2u * 1024u * 1024u, "at the configured size");

            /* THE THING SINTRAN WOULD SEE: local RAM LOCAL, the window MPM5. */
            CHECK(mms_get_physical_memory_type(0) == ND_MEM_LOCAL, "local RAM reads LOCAL");
            CHECK(mms_get_physical_memory_type(2112u * 1024u) == ND_MEM_MPM5,
                  "and the configured page reads MPM5");

            /* ONE station: the disabled CPU must not be built. A configuration
             * that quietly enabled it would put a second CPU on the bus. */
            CHECK(mfbus_nd5000_count() == 1, "exactly the enabled CPU was added");
            CHECK(mfbus_nd5000_instructions(56) == 0, "it has a CPU, which has not run");
            CHECK(mfbus_nd5000_instructions(57) == 0, "and 071B has none");

            /* It can be started and stopped, which is the whole point of
             * building it from configuration. */
            CHECK(mfbus_start_nd5000(56), "the configured CPU starts");
            mfbus_stop_nd5000(56);
            CHECK(!mfbus_start_nd5000(57), "and the disabled one cannot be started");

            mfbus_detach();

            /* A configuration with no [mfbus] builds nothing and says so. */
            MachineConfig empty;
            mc_set_defaults(&empty);
            CHECK(!mfbus_apply_config(&empty), "a config with no [mfbus] builds nothing");
            CHECK(!mfbus_is_attached(), "and leaves nothing attached");
            CHECK(!mfbus_apply_config(NULL), "a NULL config is refused");
        }
    }

    printf("\n%d check(s), %d failed\n", s_checks, s_failed);
    if (s_failed != 0)
    {
        printf("FAIL\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
