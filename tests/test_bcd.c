/*
 * test_bcd.c - Unit tests for the BCD instructions ADDD, SUBD, COMD, SHDE, PACK, UPACK.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * Unit tests for the packed-decimal (BCD) Commercial Extended instructions in
 * src/cpu/bcd.c: ADDD, SUBD, COMD, SHDE, PACK, UPACK.
 *
 * WHY this exists: nd100x had NO CPU-level regression tests at all, and the BCD
 * engine was just rewritten from a host-`double` implementation to exact
 * byte-aligned nibble arithmetic.  The vectors below are the ones that pinned
 * that rewrite down:
 *   - the TPE-hardware-validated invocations the real ND "INSTRUCTION"
 *     diagnostic performs (including the EMPTY-operand rule, which an earlier
 *     "empty operand => error" reading got wrong), and
 *   - the values probed off the LIVE ND-110/ND-120 microcode oracle (quoted in
 *     the comments of src/cpu/bcd.c).
 *
 * The test links bcd.c directly against a fake 64K word memory and a fake
 * register file, so no machine, no devices and no disk image are involved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "cpu_types.h"

/* ---------------------------------------------------------------- */
/* Fakes: bcd.c only ever touches MemoryRead/MemoryWrite and gReg.   */
/* ---------------------------------------------------------------- */

static uint16_t bcdtest_mem[65536];
static struct CpuRegs bcdtest_regs;

struct CpuRegs *g_reg = &bcdtest_regs;

/* Stubs for the real functions in src/cpu/cpu_mms.c (cpu_protos.h). */
uint16_t MemoryRead(uint16_t addr, bool UseAPT);
void MemoryWrite(uint16_t value, uint16_t addr, bool UseAPT, unsigned char byte_select);

uint16_t MemoryRead(uint16_t addr, bool UseAPT)
{
    (void)UseAPT;
    return bcdtest_mem[addr];
}

void MemoryWrite(uint16_t value, uint16_t addr, bool UseAPT, unsigned char byte_select)
{
    (void)UseAPT;
    (void)byte_select;
    bcdtest_mem[addr] = value;
}

/* The instructions under test. */
void opcode_addd_add_two_decimal_operands(uint16_t instr);
void opcode_subd_subtract_two_decimal_operands(uint16_t instr);
void opcode_comd_compare_two_decimal_operands(uint16_t instr);
void opcode_shde_decimal_shift(uint16_t instr);
void opcode_pack_convert_to_decimal(uint16_t instr);
void opcode_unpack_convert_from_decimal(uint16_t instr);

/* ---------------------------------------------------------------- */
/* Test harness                                                     */
/* ---------------------------------------------------------------- */

// clang-format off
#define OP1_ADDR 0x1000            /* A register: first  operand field */
#define OP2_ADDR 0x2000            /* X register: second operand field */
// clang-format on

#define MAX_W 6 /* words per operand field in these vectors */

/* Which instruction a vector drives. */
typedef enum
{
    OP_ADDD,
    OP_SUBD,
    OP_COMD,
    OP_SHDE,
    OP_PACK,
    OP_UPACK
} bcd_test_op;

/* Where the result is expected to land. */
typedef enum
{
    DEST_OP1, /* the A/D field (ADDD, SUBD)          */
    DEST_OP2, /* the X/T field (SHDE, PACK, UPACK)   */
    DEST_NONE /* nothing written (COMD, error paths) */
} bcd_test_dest;

typedef struct
{
    const char *name;
    bcd_test_op op;
    uint16_t d;         /* D register: descriptor D2 of operand 1 */
    uint16_t t;         /* T register: descriptor D2 of operand 2 */
    uint16_t w1[MAX_W]; /* initial words of the op1 field        */
    int n1;
    uint16_t w2[MAX_W]; /* initial words of the op2 field        */
    int n2;
    bcd_test_dest dest;
    uint16_t expect[MAX_W]; /* expected words at the destination     */
    int n_expect;
    bool expect_skip; /* true = SKIP return (P+2)              */
    int expect_a;     /* COMD: expected A register, else -1    */
} bcd_test_case;

static int bcdtest_total;
static int bcdtest_failed;

static void bcdtest_fail(const bcd_test_case *tc, const char *what, unsigned expected, unsigned got)
{
    printf("  FAIL  %-46s %s: expected %04X, got %04X\n", tc->name, what, expected, got);
    bcdtest_failed++;
}

static void bcdtest_run(const bcd_test_case *tc)
{
    uint16_t dest_addr;
    int i;
    bool ok = true;

    bcdtest_total++;

    /* Fresh, poisoned memory so any stray write shows up as a mismatch. */
    memset(bcdtest_mem, 0xEE, sizeof(bcdtest_mem));
    memset(&bcdtest_regs, 0, sizeof(bcdtest_regs));

    for (i = 0; i < tc->n1; i++)
    {
        bcdtest_mem[OP1_ADDR + i] = tc->w1[i];
    }
    for (i = 0; i < tc->n2; i++)
    {
        bcdtest_mem[OP2_ADDR + i] = tc->w2[i];
    }

    gA = OP1_ADDR;
    gD = tc->d;
    gX = OP2_ADDR;
    gT = tc->t;
    gPC = 0; /* the CPU has already advanced past the
                                     * instruction, so PC == 0 is the ERROR
                                     * return and PC == 1 is the SKIP return */

    switch (tc->op)
    {
    case OP_ADDD:
        opcode_addd_add_two_decimal_operands(0140120);
        break;
    case OP_SUBD:
        opcode_subd_subtract_two_decimal_operands(0140121);
        break;
    case OP_COMD:
        opcode_comd_compare_two_decimal_operands(0140122);
        break;
    case OP_PACK:
        opcode_pack_convert_to_decimal(0140124);
        break;
    case OP_UPACK:
        opcode_unpack_convert_from_decimal(0140125);
        break;
    case OP_SHDE:
        opcode_shde_decimal_shift(0140126);
        break;
    default:
        break;
    }

    if ((gPC == 1) != tc->expect_skip)
    {
        printf("  FAIL  %-46s return: expected %s, got %s\n", tc->name,
               tc->expect_skip ? "SKIP (P+2)" : "ERROR (P+1)",
               (gPC == 1) ? "SKIP (P+2)" : "ERROR (P+1)");
        bcdtest_failed++;
        ok = false;
    }

    if (tc->dest != DEST_NONE)
    {
        dest_addr = (tc->dest == DEST_OP1) ? OP1_ADDR : OP2_ADDR;
        for (i = 0; i < tc->n_expect; i++)
        {
            if (bcdtest_mem[dest_addr + i] != tc->expect[i])
            {
                char what[32];

                snprintf(what, sizeof(what), "word %d", i);
                bcdtest_fail(tc, what, tc->expect[i], bcdtest_mem[dest_addr + i]);
                ok = false;
            }
        }
    }

    if (tc->expect_a >= 0)
    {
        if (gA != (uint16_t)tc->expect_a)
        {
            bcdtest_fail(tc, "A register", (unsigned)tc->expect_a, gA);
            ok = false;
        }
    }

    /* STS.O (overflow) must be left ALONE by every decimal instruction -
     * oracle- and TPE-confirmed, including on the overflow error return. */
    if (STS_STATIC_OVERFLOW_IS_SET != 0)
    {
        printf("  FAIL  %-46s STS.O was set (must stay clear)\n", tc->name);
        bcdtest_failed++;
        ok = false;
    }

    if (ok)
    {
        printf("  ok    %s\n", tc->name);
    }
}

/* ---------------------------------------------------------------- */
/* Vectors                                                          */
/* ---------------------------------------------------------------- */
/*
 * Descriptor D2 layout: bit15 lr, bits13-11 sign format, bit10 round,
 * bits9-5 decimal point, bits4-0 field length (nibbles for packed BCD,
 * INCLUDING the sign nibble; bytes for ASCII).
 */

static const bcd_test_case bcdtest_cases[] = {
    /* ---------------------------------------------------------------- */
    /* TPE "INSTRUCTION" diagnostic invocations (hardware validated)     */
    /* ---------------------------------------------------------------- */
    {"TPE ADDD  L8 9999998+ + L8 1+ = 9999999+",
     OP_ADDD,
     0x0008,
     0x0008,
     {0x9999, 0x998C},
     2,
     {0x0000, 0x001C},
     2,
     DEST_OP1,
     {0x9999, 0x999C},
     2,
     true,
     -1},
    {"TPE ADDD  EMPTY + EMPTY = 0 (TFIRE: SKIP)",
     OP_ADDD,
     0x0000,
     0x0000,
     {0},
     0,
     {0},
     0,
     DEST_NONE,
     {0},
     0,
     true,
     -1},
    {"TPE ADDD  L4 097+ + L2 lr unsigned 3 = 100+",
     OP_ADDD,
     0x0004,
     0xA002,
     {0x097C},
     1,
     {0x003F},
     1,
     DEST_OP1,
     {0x100C},
     1,
     true,
     -1},
    {"TPE ADDD  L4 097+ + EMPTY = 097+",
     OP_ADDD,
     0x0004,
     0x0000,
     {0x097C},
     1,
     {0},
     0,
     DEST_OP1,
     {0x097C},
     1,
     true,
     -1},
    {"TPE ADDD  EMPTY + L4 097+ = overflow (ERROR)",
     OP_ADDD,
     0x0000,
     0x0004,
     {0},
     0,
     {0x097C},
     1,
     DEST_NONE,
     {0},
     0,
     false,
     -1},
    /* The SUBD counterparts of the five vectors above: the same shapes with
     * op2's sign flipped, so the arithmetic result is identical.  (For the
     * unsigned op2 there is no sign to flip, so 97 - 3 = 94.) */
    {"TPE SUBD  L8 9999998+ - L8 1- = 9999999+",
     OP_SUBD,
     0x0008,
     0x0008,
     {0x9999, 0x998C},
     2,
     {0x0000, 0x001D},
     2,
     DEST_OP1,
     {0x9999, 0x999C},
     2,
     true,
     -1},
    {"TPE SUBD  EMPTY - EMPTY = 0 (TFIRE: SKIP)",
     OP_SUBD,
     0x0000,
     0x0000,
     {0},
     0,
     {0},
     0,
     DEST_NONE,
     {0},
     0,
     true,
     -1},
    {"TPE SUBD  L4 097+ - L2 lr unsigned 3 = 094+",
     OP_SUBD,
     0x0004,
     0xA002,
     {0x097C},
     1,
     {0x003F},
     1,
     DEST_OP1,
     {0x094C},
     1,
     true,
     -1},
    {"TPE SUBD  L4 097+ - EMPTY = 097+",
     OP_SUBD,
     0x0004,
     0x0000,
     {0x097C},
     1,
     {0},
     0,
     DEST_OP1,
     {0x097C},
     1,
     true,
     -1},
    {"TPE SUBD  EMPTY - L4 097+ = overflow (ERROR)",
     OP_SUBD,
     0x0000,
     0x0004,
     {0},
     0,
     {0x097C},
     1,
     DEST_NONE,
     {0},
     0,
     false,
     -1},

    /* ---------------------------------------------------------------- */
    /* Basic signed arithmetic                                          */
    /* ---------------------------------------------------------------- */
    {"ADDD  +12 + +5 = +17",
     OP_ADDD,
     0x0004,
     0x0004,
     {0x012C},
     1,
     {0x005C},
     1,
     DEST_OP1,
     {0x017C},
     1,
     true,
     -1},
    {"ADDD  -5 + +12 = +7",
     OP_ADDD,
     0x0004,
     0x0004,
     {0x005D},
     1,
     {0x012C},
     1,
     DEST_OP1,
     {0x007C},
     1,
     true,
     -1},
    {"SUBD  +12 - +5 = +7",
     OP_SUBD,
     0x0004,
     0x0004,
     {0x012C},
     1,
     {0x005C},
     1,
     DEST_OP1,
     {0x007C},
     1,
     true,
     -1},
    {"SUBD  +5 - +12 = -7",
     OP_SUBD,
     0x0004,
     0x0004,
     {0x005C},
     1,
     {0x012C},
     1,
     DEST_OP1,
     {0x007D},
     1,
     true,
     -1},
    {/* A zero result keeps op1's ORIGINAL sign (oracle locked). */
     "ADDD  -5 + +5 = -0 (zero keeps op1 sign)",
     OP_ADDD,
     0x0004,
     0x0004,
     {0x005D},
     1,
     {0x005C},
     1,
     DEST_OP1,
     {0x000D},
     1,
     true,
     -1},
    {"SUBD  +5 - +5 = +0 (zero keeps op1 sign)",
     OP_SUBD,
     0x0004,
     0x0004,
     {0x005C},
     1,
     {0x005C},
     1,
     DEST_OP1,
     {0x000C},
     1,
     true,
     -1},
    {/* Overflow: the truncated LOW digits are stored, STS.O stays clear,
         * and the instruction takes the ERROR return. */
     "ADDD  +999 + +2 = 1001 -> truncated 001+ ERROR",
     OP_ADDD,
     0x0004,
     0x0004,
     {0x999C},
     1,
     {0x002C},
     1,
     DEST_OP1,
     {0x001C},
     1,
     false,
     -1},
    {/* >15 significant digits - impossible on the old double engine.
         * 100000000000000001 + 1 = 100000000000000002 (18 digits, L=19). */
     "ADDD  100000000000000001 + 1 (18 digits, exact)",
     OP_ADDD,
     0x0013,
     0x0013,
     {0x0100, 0x0000, 0x0000, 0x0000, 0x001C},
     5,
     {0x0000, 0x0000, 0x0000, 0x0000, 0x001C},
     5,
     DEST_OP1,
     {0x0100, 0x0000, 0x0000, 0x0000, 0x002C},
     5,
     true,
     -1},
    {/* BYTE ALIGNMENT, odd L: L=3 => M=4, sign at nibble 3, one leading
         * zero pad nibble.  Oracle: 12 + 5 -> 0x017C. */
     "ADDD  L3 (odd, byte aligned) +12 + +5 = 0x017C",
     OP_ADDD,
     0x0003,
     0x0003,
     {0x012C},
     1,
     {0x005C},
     1,
     DEST_OP1,
     {0x017C},
     1,
     true,
     -1},

    /* ---------------------------------------------------------------- */
    /* COMD                                                             */
    /* ---------------------------------------------------------------- */
    {"COMD  +12 > +5  -> A = 1",
     OP_COMD,
     0x0004,
     0x0004,
     {0x012C},
     1,
     {0x005C},
     1,
     DEST_NONE,
     {0},
     0,
     true,
     1},
    {"COMD  +5 < +12  -> A = 0xFFFF",
     OP_COMD,
     0x0004,
     0x0004,
     {0x005C},
     1,
     {0x012C},
     1,
     DEST_NONE,
     {0},
     0,
     true,
     0xFFFF},
    {"COMD  +12 = +12 -> A = 0",
     OP_COMD,
     0x0004,
     0x0004,
     {0x012C},
     1,
     {0x012C},
     1,
     DEST_NONE,
     {0},
     0,
     true,
     0},
    {"COMD  -12 < +5  -> A = 0xFFFF",
     OP_COMD,
     0x0004,
     0x0004,
     {0x012D},
     1,
     {0x005C},
     1,
     DEST_NONE,
     {0},
     0,
     true,
     0xFFFF},

    /* ---------------------------------------------------------------- */
    /* SHDE - every value here was probed off the LIVE ND-110 oracle     */
    /* ---------------------------------------------------------------- */
    {/* op1.dp = 1, op2.dp = 0 => shift RIGHT by 1: +123 -> +012 */
     "SHDE  +123 >> 1 = +012",
     OP_SHDE,
     0x0024,
     0x0004,
     {0x123C},
     1,
     {0x0000},
     1,
     DEST_OP2,
     {0x012C},
     1,
     true,
     -1},
    {/* op1.dp = 0, op2.dp = 1 => shift LEFT by 1: +123 -> +230, and the
         * non-zero '1' shifted off the MSD end forces the ERROR return. */
     "SHDE  +123 << 1 = +230, ERROR (1 lost)",
     OP_SHDE,
     0x0004,
     0x0024,
     {0x123C},
     1,
     {0x0000},
     1,
     DEST_OP2,
     {0x230C},
     1,
     false,
     -1},
    {/* Only a ZERO is lost off the MSD end => normal SKIP return. */
     "SHDE  +023 << 1 = +230, SKIP (only 0 lost)",
     OP_SHDE,
     0x0004,
     0x0024,
     {0x023C},
     1,
     {0x0000},
     1,
     DEST_OP2,
     {0x230C},
     1,
     true,
     -1},
    {/* Rounding (op2 D2 bit 10 = 0x0400): 125 >> 1 rounds to 013. */
     "SHDE  +125 >> 1 rounded = +013",
     OP_SHDE,
     0x0024,
     0x0404,
     {0x125C},
     1,
     {0x0000},
     1,
     DEST_OP2,
     {0x013C},
     1,
     true,
     -1},
    {"SHDE  +124 >> 1 rounded = +012",
     OP_SHDE,
     0x0024,
     0x0404,
     {0x124C},
     1,
     {0x0000},
     1,
     DEST_OP2,
     {0x012C},
     1,
     true,
     -1},
    {/* ROUND ONCE on the MOST SIGNIFICANT discarded digit ('4' < 5), NOT
         * per digit - per-digit rounding would wrongly give 002. */
     "SHDE  +149 >> 2 rounded = +001 (round once)",
     OP_SHDE,
     0x0044,
     0x0404,
     {0x149C},
     1,
     {0x0000},
     1,
     DEST_OP2,
     {0x001C},
     1,
     true,
     -1},
    {"SHDE  +155 >> 2 rounded = +002",
     OP_SHDE,
     0x0044,
     0x0404,
     {0x155C},
     1,
     {0x0000},
     1,
     DEST_OP2,
     {0x002C},
     1,
     true,
     -1},
    {/* op1's sign propagates to op2. */
     "SHDE  -123 (no shift) = 0x123D",
     OP_SHDE,
     0x0004,
     0x0004,
     {0x123D},
     1,
     {0x0000},
     1,
     DEST_OP2,
     {0x123D},
     1,
     true,
     -1},
    {/* op2 D2 bit 13 (0x2000) forces the unsigned 0xF sign nibble. */
     "SHDE  +123 -> unsigned dest = 0x123F",
     OP_SHDE,
     0x0004,
     0x2004,
     {0x123C},
     1,
     {0x0000},
     1,
     DEST_OP2,
     {0x123F},
     1,
     true,
     -1},
    {/* An unsigned (0xF) source becomes '+' in a signed destination. */
     "SHDE  unsigned +123 -> signed dest = 0x123C",
     OP_SHDE,
     0x0004,
     0x0004,
     {0x123F},
     1,
     {0x0000},
     1,
     DEST_OP2,
     {0x123C},
     1,
     true,
     -1},

    /* ---------------------------------------------------------------- */
    /* PACK - ASCII to packed BCD                                       */
    /* ---------------------------------------------------------------- */
    {/* ASCII L=3 bytes "125" embedded trailing: '5' is a plain digit, so
         * the value is positive.  Dest L=4 nibbles => 0x125C. */
     "PACK  ASCII '125' embedded trailing -> 0x125C",
     OP_PACK,
     0x0003,
     0x0004,
     {0x3132, 0x3500},
     2,
     {0x0000},
     1,
     DEST_OP2,
     {0x125C},
     1,
     true,
     -1},
    {/* Embedded trailing overpunch: 'L' = 0x4C = digit 3, sign '-'. */
     "PACK  ASCII '12L' embedded overpunch -> 0x123D",
     OP_PACK,
     0x0003,
     0x0004,
     {0x3132, 0x4C00},
     2,
     {0x0000},
     1,
     DEST_OP2,
     {0x123D},
     1,
     true,
     -1},
    {/* SEPARATE TRAILING (format 1 => D2 bits 13-11 = 001 = 0x0800): the
         * '-' sign byte sits OUTSIDE the digit range and must still be
         * decoded.  This is the regression the in-loop sign branch missed -
         * it silently packed '-' operands as POSITIVE. */
     "PACK  ASCII '123-' separate trailing -> 0x123D",
     OP_PACK,
     0x0804,
     0x0004,
     {0x3132, 0x332D},
     2,
     {0x0000},
     1,
     DEST_OP2,
     {0x123D},
     1,
     true,
     -1},
    {/* SEPARATE LEADING (format 3 => 0x1800): sign byte at index 0. */
     "PACK  ASCII '-123' separate leading -> 0x123D",
     OP_PACK,
     0x1804,
     0x0004,
     {0x2D31, 0x3233},
     2,
     {0x0000},
     1,
     DEST_OP2,
     {0x123D},
     1,
     true,
     -1},
    {/* Illegal ASCII code in a digit position => error return. */
     "PACK  ASCII '1X5' illegal code -> ERROR",
     OP_PACK,
     0x0003,
     0x0004,
     {0x3158, 0x3500},
     2,
     {0x0000},
     1,
     DEST_NONE,
     {0},
     0,
     false,
     -1},

    /* ---------------------------------------------------------------- */
    /* UPACK - packed BCD to ASCII                                      */
    /* ---------------------------------------------------------------- */
    {/* 0x123D (= -123) to a 3-byte embedded-trailing ASCII field: the LSD
         * byte carries the overpunched sign, 0x49 + 3 = 0x4C = 'L'. */
     "UPACK 0x123D -> ASCII '12L' (embedded overpunch)",
     OP_UPACK,
     0x0004,
     0x0003,
     {0x123D},
     1,
     {0x0000, 0x0000},
     2,
     DEST_OP2,
     {0x3132, 0x4C00},
     2,
     true,
     -1},
    {/* Positive overpunch: 0x40 + 3 = 0x43 = 'C'. */
     "UPACK 0x123C -> ASCII '12C' (embedded overpunch)",
     OP_UPACK,
     0x0004,
     0x0003,
     {0x123C},
     1,
     {0x0000, 0x0000},
     2,
     DEST_OP2,
     {0x3132, 0x4300},
     2,
     true,
     -1},
    {/* Separate trailing: digits then an ASCII '-' (0x2D) sign byte. */
     "UPACK 0x123D -> ASCII '123-' (separate trailing)",
     OP_UPACK,
     0x0004,
     0x0804,
     {0x123D},
     1,
     {0x0000, 0x0000},
     2,
     DEST_OP2,
     {0x3132, 0x332D},
     2,
     true,
     -1},
    {/* Right adjusted with ASCII '0' fill in the high bytes. */
     "UPACK 0x005C -> ASCII '00E' (right adjusted, '0' fill)",
     OP_UPACK,
     0x0004,
     0x0003,
     {0x005C},
     1,
     {0x0000, 0x0000},
     2,
     DEST_OP2,
     {0x3030, 0x4500},
     2,
     true,
     -1},
    {/* An illegal digit nibble (0xA) in the source is reported. */
     "UPACK 0x1A3C illegal digit -> ERROR",
     OP_UPACK,
     0x0004,
     0x0003,
     {0x1A3C},
     1,
     {0x0000, 0x0000},
     2,
     DEST_NONE,
     {0},
     0,
     false,
     -1}};

int main(void)
{
    size_t i;

    printf("ND-100 packed-decimal (BCD) instruction tests\n");
    printf("--------------------------------------------\n");

    for (i = 0; i < sizeof(bcdtest_cases) / sizeof(bcdtest_cases[0]); i++)
    {
        bcdtest_run(&bcdtest_cases[i]);
    }

    printf("--------------------------------------------\n");
    printf("%d test(s), %d failure(s)\n", bcdtest_total, bcdtest_failed);

    return (bcdtest_failed == 0) ? 0 : 1;
}
