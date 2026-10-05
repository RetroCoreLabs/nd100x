/*
 * test_movb.c - MOVB (140131) and MOVBF (140132) on fields that start on different byte halves.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Two defects are pinned here, both ported from the RetroCore reference
 * ($RETROCORE/Emulated.HW/ND/CPU/ND100/Instructions.ByteInstruction.cs, MOVB
 * and MOVBF, "FIX A" and "FIX B", 2026-08-21):
 *
 *   A - the source byte half was selected with the DESTINATION's half bit, so a
 *       move between fields that start on different halves read the wrong half
 *       of the right word.
 *   B - MOVB chose its copy direction from the WORD addresses only. With source
 *       and destination in the same word on different halves it ran ascending,
 *       and an ascending copy of a field that moves forward overwrites each
 *       source byte before it is read.
 *
 * MEASURED: SINTRAN's ND-500 monitor shifts the 16-byte owner name one byte to
 * the right at 100236B-100244B to make room for '(' - A = X, D = 040020B,
 * T = 140020B - and the two defects together turned "SYSTEM'" into
 * "YSTSMS S S S S S", printed as "(YSTSMS S S S S S)TERMINAL-1".
 *
 * The instructions are run through the real dispatch table. Paging is off, so
 * the virtual addresses in A and X are physical word addresses.
 */

#include <stdio.h>
#include <string.h>

#include "cpu_types.h"

static int s_failed = 0;
static int s_passed = 0;

#define CHECK(cond, msg)                                                                 \
    do                                                                                   \
    {                                                                                    \
        if (cond)                                                                        \
        {                                                                                \
            s_passed++;                                                                  \
        }                                                                                \
        else                                                                             \
        {                                                                                \
            printf("  FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);                   \
            s_failed++;                                                                  \
        }                                                                                \
    } while (0)

#define TEST_LOCAL_WORDS 0x00040000u /* 256 KW installed */
#define OP_MOVB          0140131
#define OP_MOVBF         0140132
#define HALF_RIGHT       0x8000u /* D/T bit 15: the field starts on the right (low) byte */
#define MARKER           0xEEu

static struct CpuRegs s_test_regs;

static void movb_fixture(void)
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
}

/* A byte position is 2 * word address + half (0 = left/high byte, 1 = right/low byte). */
static uint8_t get_byte(uint32_t byte_pos)
{
    uint16_t word = (uint16_t)mms_read_physical_memory((int)(byte_pos >> 1u), true);
    return ((byte_pos & 1u) != 0u) ? (uint8_t)(word & 0xFFu) : (uint8_t)(word >> 8u);
}

static void put_byte(uint32_t byte_pos, uint8_t value)
{
    uint16_t word = (uint16_t)mms_read_physical_memory((int)(byte_pos >> 1u), true);
    if ((byte_pos & 1u) != 0u)
    {
        word = (uint16_t)((word & 0xFF00u) | value);
    }
    else
    {
        word = (uint16_t)((word & 0x00FFu) | ((uint16_t)value << 8u));
    }
    mms_write_physical_memory((int)(byte_pos >> 1u), word, true);
}

static void put_text(uint32_t byte_pos, const char *text, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        put_byte(byte_pos + i, (uint8_t)text[i]);
    }
}

static void fill(uint32_t byte_pos, uint8_t value, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        put_byte(byte_pos + i, value);
    }
}

static bool text_at(uint32_t byte_pos, const char *text, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        if (get_byte(byte_pos + i) != (uint8_t)text[i])
        {
            return false;
        }
    }
    return true;
}

static void show(const char *label, uint32_t byte_pos, uint32_t count)
{
    printf("  %s \"", label);
    for (uint32_t i = 0; i < count; i++)
    {
        uint8_t ch = get_byte(byte_pos + i);
        putchar((ch >= 0x20u && ch < 0x7Fu) ? (int)ch : '.');
    }
    printf("\"\n");
}

/* Load the registers from two byte positions and a length, then run one instruction. */
static void run_move(int opcode, uint32_t src_byte_pos, uint32_t dst_byte_pos, uint16_t length,
                     uint16_t extra_d_bits, uint16_t extra_t_bits)
{
    gA = (uint16_t)(src_byte_pos >> 1u);
    gX = (uint16_t)(dst_byte_pos >> 1u);
    gD = (uint16_t)((((src_byte_pos & 1u) != 0u) ? HALF_RIGHT : 0u) | extra_d_bits | length);
    gT = (uint16_t)((((dst_byte_pos & 1u) != 0u) ? HALF_RIGHT : 0u) | extra_t_bits | length);
    gPC = 0;
    g_instr_funcs[opcode]((uint16_t)opcode);
}

/* The measured SINTRAN call: shift 16 bytes one byte to the right, in place. */
static void test_in_place_right_shift(void)
{
    static const char BEFORE[] = "SYSTEM'         ";
    const uint32_t base = 2u * 0x1000u; /* left byte of word 010000B */

    printf("MOVB: in-place shift one byte to the right (the SINTRAN owner-name call)\n");
    put_text(base, BEFORE, 16);
    put_byte(base + 16u, MARKER);
    put_byte(base + 17u, MARKER);

    /* A = X, D = 040020B, T = 140020B, as measured. Bit 14 selects the alternative
     * page table and has no effect with paging off. */
    run_move(OP_MOVB, base, base + 1u, 16, 0x4000u, 0x4000u);
    show("after: ", base, 18);

    CHECK(get_byte(base) == 'S', "byte 0 is not written");
    CHECK(text_at(base + 1u, BEFORE, 16), "bytes 1..16 are the original bytes 0..15");
    CHECK(get_byte(base + 17u) == MARKER, "byte 17 is not written");
    CHECK(gPC == 1, "skip return");
    printf("  D=%06o T=%06o A=%06o X=%06o\n", gD, gT, gA, gX);
    CHECK((gD & 0x0FFFu) == 0u, "D bits 0-11 are zero");
    CHECK((gT & 0x0FFFu) == 16u, "T bits 0-11 are the 16 bytes moved");
    CHECK((gD & 0x8000u) != 0u && (gT & 0x8000u) != 0u,
          "D and T bit 15 name the half after the last byte written (right)");
    /* What the reference computes for this call; not checked against microcode. */
    CHECK(gA == 0x1008u && gX == 0x1008u, "A and X as the reference leaves them");
}

/* The mirror: shift 16 bytes one byte to the left, in place. Must run ascending. */
static void test_in_place_left_shift(void)
{
    static const char BEFORE[] = "(SYSTEM'         ";
    const uint32_t base = 2u * 0x1100u;

    printf("MOVB: in-place shift one byte to the left\n");
    put_text(base, BEFORE, 17);

    run_move(OP_MOVB, base + 1u, base, 16, 0u, 0u);
    show("after: ", base, 17);

    CHECK(text_at(base, BEFORE + 1, 16), "bytes 0..15 are the original bytes 1..16");
    CHECK(get_byte(base + 16u) == ' ', "byte 16 is not written");
    CHECK(gPC == 1, "skip return");
}

/* No overlap, different halves: only the source-half selection is in play. */
static void test_mixed_halves_no_overlap(void)
{
    static const char TEXT[] = "ABCDE";
    const uint32_t low = 2u * 0x1200u;
    const uint32_t high = 2u * 0x1300u;

    printf("MOVB: no overlap, source and destination on different halves\n");

    /* Source below destination (descending copy), right half to left half. */
    fill(low, MARKER, 8);
    fill(high, MARKER, 8);
    put_text(low + 1u, TEXT, 5);
    run_move(OP_MOVB, low + 1u, high, 5, 0u, 0u);
    show("right->left, descending:", high, 6);
    CHECK(text_at(high, TEXT, 5), "right-half source to left-half destination, descending");
    CHECK(get_byte(high + 5u) == MARKER, "nothing written past the field");

    /* Source above destination (ascending copy), left half to right half. */
    fill(low, MARKER, 8);
    fill(high, MARKER, 8);
    put_text(high, TEXT, 5);
    run_move(OP_MOVB, high, low + 1u, 5, 0u, 0u);
    show("left->right, ascending: ", low, 7);
    CHECK(get_byte(low) == MARKER, "the byte before the field is not written");
    CHECK(text_at(low + 1u, TEXT, 5), "left-half source to right-half destination, ascending");
    CHECK(get_byte(low + 6u) == MARKER, "nothing written past the field");
}

/* Control: both fields on the same half worked before the change and still must. */
static void test_same_half_control(void)
{
    static const char TEXT[] = "HELLO!";
    const uint32_t src = 2u * 0x1400u;
    const uint32_t dst = 2u * 0x1500u;

    printf("MOVB: control, both fields on the left half\n");
    fill(dst, MARKER, 8);
    put_text(src, TEXT, 6);
    run_move(OP_MOVB, src, dst, 6, 0u, 0u);
    CHECK(text_at(dst, TEXT, 6), "same-half copy is unchanged");
    CHECK(get_byte(dst + 6u) == MARKER, "nothing written past the field");
}

static void test_movbf_mixed_halves(void)
{
    static const char TEXT[] = "ABCDE";
    const uint32_t src = 2u * 0x1700u;
    const uint32_t dst = 2u * 0x1600u; /* destination below source: no forbidden overlap */

    printf("MOVBF: source on the right half, destination on the left half\n");
    fill(dst, MARKER, 8);
    fill(src, MARKER, 8);
    put_text(src + 1u, TEXT, 5);
    run_move(OP_MOVBF, src + 1u, dst, 5, 0u, 0u);
    show("after: ", dst, 6);
    CHECK(text_at(dst, TEXT, 5), "the five bytes arrive in order");
    CHECK(get_byte(dst + 5u) == MARKER, "nothing written past the field");
    CHECK(gPC == 1, "skip return (no forbidden overlap)");
}

int main(void)
{
    printf("=== MOVB / MOVBF tests ===\n");

    movb_fixture();
    cpu_instructions();

    test_in_place_right_shift();
    test_in_place_left_shift();
    test_mixed_halves_no_overlap();
    test_same_half_control();
    test_movbf_mixed_halves();

    printf("\n%d passed, %d failed\n", s_passed, s_failed);
    return (s_failed == 0) ? 0 : 1;
}
