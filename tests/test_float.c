/*
 * nd100x - ND100 Virtual Machine
 *
 * Copyright (c) 2026 Ronny Hansen
 *
 * Unit tests for the floating point engine in src/cpu/float.c.
 *
 * Part 1 is a 48-bit REGRESSION LOCK: the expected words below were captured
 * from the unmodified integer float.c (the SIMH-derived 48-bit implementation)
 * on 2026-07-27, BEFORE the optional 32-bit FPP was added. Any refactor of
 * float.c must keep every one of these results bit-identical.
 *
 * The test links float.c directly against a fake register file and a fake
 * setbit(), so no machine, no devices and no disk image are involved
 * (same pattern as test_bcd.c).
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
/* Fakes: float.c only touches gReg (via gT/gA/gD) and setbit().     */
/* ---------------------------------------------------------------- */

static struct CpuRegs fptest_regs;
struct CpuRegs *gReg = &fptest_regs;

static int fptest_z_set;

void setbit(ushort regnum, ushort stsbit, char val)
{
    (void)regnum;
    if (stsbit == _Z && val)
        fptest_z_set = 1;
}

/* The routines under test (float.c has no public header of its own). */
int NDFloat_Add(ushort *p_a, ushort *p_b, ushort *p_r);
int NDFloat_Sub(ushort *p_a, ushort *p_b, ushort *p_r);
int NDFloat_Mul(ushort *p_a, ushort *p_b, ushort *p_r);
int NDFloat_Div(ushort *p_a, ushort *p_b, ushort *p_r);
void DoNLZ(char scaling);
void DoDNZ(char scaling);
int NDFloat_Add32(ushort *p_a, ushort *p_b, ushort *p_r);
int NDFloat_Sub32(ushort *p_a, ushort *p_b, ushort *p_r);
int NDFloat_Mul32(ushort *p_a, ushort *p_b, ushort *p_r);
int NDFloat_Div32(ushort *p_a, ushort *p_b, ushort *p_r);
void DoNLZ32(char scaling);
void DoDNZ32(char scaling);

/* ---------------------------------------------------------------- */
/* Test harness                                                     */
/* ---------------------------------------------------------------- */

static int fptest_total;
static int fptest_failed;

static void check3(const char *name, const char *op,
                   const ushort *exp, const ushort *got)
{
    fptest_total++;
    if (memcmp(exp, got, 3 * sizeof(ushort)) != 0) {
        printf("  FAIL  %-34s %s: expected %06o %06o %06o, got %06o %06o %06o\n",
               name, op, exp[0], exp[1], exp[2], got[0], got[1], got[2]);
        fptest_failed++;
    }
}

static void check_int(const char *name, const char *what, long exp, long got)
{
    fptest_total++;
    if (exp != got) {
        printf("  FAIL  %-34s %s: expected %ld (0%lo), got %ld (0%lo)\n",
               name, what, exp, exp, got, got);
        fptest_failed++;
    }
}

/* At PIL 0 (reg_STS == 0) the gT/gA/gD macros hit reg[0][...]. */
#define REG_T fptest_regs.reg[0][_T]
#define REG_A fptest_regs.reg[0][_A]
#define REG_D fptest_regs.reg[0][_D]

/* ---------------------------------------------------------------- */
/* Part 1: 48-bit regression lock                                   */
/* ---------------------------------------------------------------- */

typedef struct {
    ushort a[3];      /* accumulator operand {T,A,D} */
    ushort b[3];      /* memory operand */
    ushort add[3];    /* expected NDFloat_Add result */
    ushort sub[3];    /* expected NDFloat_Sub result */
    ushort mul[3];    /* expected NDFloat_Mul result */
    ushort dv[3];     /* expected NDFloat_Div result */
    int    div_rc;    /* expected NDFloat_Div return (1 = div by zero) */
} fp48_pair_case;

/* Captured from unmodified float.c on 2026-07-27 - DO NOT RECOMPUTE. */
static const fp48_pair_case fp48_pairs[] = {
    /* +1, +1 */
    {{0040001,0100000,0000000}, {0040001,0100000,0000000},
     {0040002,0100000,0000000}, {0000000,0000000,0000000},
     {0040001,0100000,0000000}, {0040001,0100000,0000000}, 0},
    /* +1, -1 */
    {{0040001,0100000,0000000}, {0140001,0100000,0000000},
     {0000000,0000000,0000000}, {0040002,0100000,0000000},
     {0140001,0100000,0000000}, {0140001,0100000,0000000}, 0},
    /* +3, +1 */
    {{0040002,0140000,0000000}, {0040001,0100000,0000000},
     {0040003,0100000,0000000}, {0040002,0100000,0000000},
     {0040002,0140000,0000000}, {0040002,0140000,0000000}, 0},
    /* +2, +1.5 */
    {{0040002,0100000,0000000}, {0040001,0140000,0000000},
     {0040002,0160000,0000000}, {0040000,0100000,0000000},
     {0040002,0140000,0000000}, {0040001,0125252,0125253}, 0},
    /* +2, +1.9375 */
    {{0040002,0100000,0000000}, {0040001,0174000,0000000},
     {0040002,0176000,0000000}, {0037775,0100000,0000000},
     {0040002,0174000,0000000}, {0040001,0102041,0004103}, 0},
    /* +1.5, +1.5 */
    {{0040001,0140000,0000000}, {0040001,0140000,0000000},
     {0040002,0140000,0000000}, {0000000,0000000,0000000},
     {0040002,0110000,0000000}, {0040001,0100000,0000000}, 0},
    /* +2, +3 */
    {{0040002,0100000,0000000}, {0040002,0140000,0000000},
     {0040003,0120000,0000000}, {0140001,0100000,0000000},
     {0040003,0140000,0000000}, {0040000,0125252,0125253}, 0},
    /* +6, +2 */
    {{0040003,0140000,0000000}, {0040002,0100000,0000000},
     {0040004,0100000,0000000}, {0040003,0100000,0000000},
     {0040004,0140000,0000000}, {0040002,0140000,0000000}, 0},
    /* -9, -3 */
    {{0140004,0110000,0000000}, {0140002,0140000,0000000},
     {0140004,0140000,0000000}, {0140003,0140000,0000000},
     {0040005,0154000,0000000}, {0040002,0140000,0000000}, 0},
    /* +0.5, +0.5 */
    {{0040000,0100000,0000000}, {0040000,0100000,0000000},
     {0040001,0100000,0000000}, {0000000,0000000,0000000},
     {0037777,0100000,0000000}, {0040001,0100000,0000000}, 0},
    /* pi-ish, e-ish (full 32-bit mantissas) */
    {{0040002,0144417,0157534}, {0040002,0125676,0130621},
     {0040003,0135147,0044166}, {0037777,0165211,0067130},
     {0040004,0103343,0057560}, {0040001,0112731,0105646}, 0},
    /* big exponent vs small exponent (scale > 31 path) */
    {{0042000,0123456,0165432}, {0036000,0134567,0012345},
     {0042000,0123456,0165432}, {0042000,0123456,0165432},
     {0037777,0171075,0054264}, {0044000,0163303,0164301}, 0},
    /* x, 0  (division by zero) */
    {{0040001,0100000,0000000}, {0000000,0000000,0000000},
     {0040001,0100000,0000000}, {0040001,0100000,0000000},
     {0000000,0000000,0000000}, {0077777,0177777,0177777}, 1},
    /* 0, y */
    {{0000000,0000000,0000000}, {0040002,0140000,0000000},
     {0040002,0140000,0000000}, {0140002,0140000,0000000},
     {0000000,0000000,0000000}, {0000000,0000000,0000000}, 0},
    /* 0, 0  (0/0 is also division by zero) */
    {{0000000,0000000,0000000}, {0000000,0000000,0000000},
     {0000000,0000000,0000000}, {0000000,0000000,0000000},
     {0000000,0000000,0000000}, {0077777,0177777,0177777}, 1},
    /* -1.5, +255 */
    {{0140001,0140000,0000000}, {0040010,0177400,0000000},
     {0040010,0176600,0000000}, {0140011,0100100,0000000},
     {0140011,0137500,0000000}, {0137771,0140300,0140301}, 0},
    /* tiny difference (deep cancellation in add path) */
    {{0040001,0100000,0000001}, {0140001,0100000,0000000},
     {0037742,0100000,0000000}, {0040002,0100000,0000000},
     {0140001,0100000,0000001}, {0140001,0100000,0000001}, 0},
};

typedef struct {
    int    val;       /* initial A register (signed) */
    int    scaling;   /* NLZ scaling; DNZ uses the negation */
    ushort nlz[3];    /* expected {T,A,D} after DoNLZ(scaling) */
    ushort dnz[3];    /* expected {T,A,D} after DoDNZ(-scaling) */
    int    z;         /* expected _Z set during the round trip */
} fp48_nlz_case;

/* Captured from unmodified float.c on 2026-07-27 - DO NOT RECOMPUTE. */
static const fp48_nlz_case fp48_nlz[] = {
    {     0,  16, {0000000,0000000,0000000}, {0000000,0000000,0000000}, 0},
    {     1,  16, {0040001,0100000,0000000}, {0000000,0000001,0000000}, 0},
    {    -1,  16, {0140001,0100000,0000000}, {0000000,0177777,0000000}, 0},
    {     3,  16, {0040002,0140000,0000000}, {0000000,0000003,0000000}, 0},
    {   255,  16, {0040010,0177400,0000000}, {0000000,0000377,0000000}, 0},
    {  -255,  16, {0140010,0177400,0000000}, {0000000,0177401,0000000}, 0},
    { 32767,  16, {0040017,0177776,0000000}, {0000000,0077777,0000000}, 0},
    {-32768,  16, {0140020,0100000,0000000}, {0000000,0000000,0000000}, 0},
    { 21845,  16, {0040017,0125252,0000000}, {0000000,0052525,0000000}, 0},
    {-30000,  16, {0140017,0165140,0000000}, {0000000,0105320,0000000}, 0},
    {     0,   0, {0000000,0000000,0000000}, {0000000,0000000,0000000}, 0},
    {     1,   0, {0037761,0100000,0000000}, {0000000,0000001,0000000}, 0},
    {    -1,   0, {0137761,0100000,0000000}, {0000000,0177777,0000000}, 0},
    {     3,   0, {0037762,0140000,0000000}, {0000000,0000003,0000000}, 0},
    {   255,   0, {0037770,0177400,0000000}, {0000000,0000377,0000000}, 0},
    {  -255,   0, {0137770,0177400,0000000}, {0000000,0177401,0000000}, 0},
    { 32767,   0, {0037777,0177776,0000000}, {0000000,0077777,0000000}, 0},
    {-32768,   0, {0140000,0100000,0000000}, {0000000,0000000,0000000}, 0},
    { 21845,   0, {0037777,0125252,0000000}, {0000000,0052525,0000000}, 0},
    {-30000,   0, {0137777,0165140,0000000}, {0000000,0105320,0000000}, 0},
    {     0,  -6, {0000000,0000000,0000000}, {0000000,0000000,0000000}, 0},
    {     1,  -6, {0037753,0100000,0000000}, {0000000,0000001,0000000}, 0},
    {    -1,  -6, {0137753,0100000,0000000}, {0000000,0177777,0000000}, 0},
    {     3,  -6, {0037754,0140000,0000000}, {0000000,0000003,0000000}, 0},
    {   255,  -6, {0037762,0177400,0000000}, {0000000,0000377,0000000}, 0},
    {  -255,  -6, {0137762,0177400,0000000}, {0000000,0177401,0000000}, 0},
    { 32767,  -6, {0037771,0177776,0000000}, {0000000,0077777,0000000}, 0},
    {-32768,  -6, {0137772,0100000,0000000}, {0000000,0000000,0000000}, 0},
    { 21845,  -6, {0037771,0125252,0000000}, {0000000,0052525,0000000}, 0},
    {-30000,  -6, {0137771,0165140,0000000}, {0000000,0105320,0000000}, 0},
    {     0,  60, {0000000,0000000,0000000}, {0000000,0000000,0000000}, 0},
    {     1,  60, {0040055,0100000,0000000}, {0000000,0000001,0000000}, 0},
    {    -1,  60, {0140055,0100000,0000000}, {0000000,0177777,0000000}, 0},
    {     3,  60, {0040056,0140000,0000000}, {0000000,0000003,0000000}, 0},
    {   255,  60, {0040064,0177400,0000000}, {0000000,0000377,0000000}, 0},
    {  -255,  60, {0140064,0177400,0000000}, {0000000,0177401,0000000}, 0},
    { 32767,  60, {0040073,0177776,0000000}, {0000000,0077777,0000000}, 0},
    {-32768,  60, {0140074,0100000,0000000}, {0000000,0000000,0000000}, 0},
    { 21845,  60, {0040073,0125252,0000000}, {0000000,0052525,0000000}, 0},
    {-30000,  60, {0140073,0165140,0000000}, {0000000,0105320,0000000}, 0},
};

static void run_fp48_lock(void)
{
    unsigned i;
    char name[64];

    printf("48-bit regression lock: arithmetic pairs\n");
    for (i = 0; i < sizeof(fp48_pairs) / sizeof(fp48_pairs[0]); i++) {
        const fp48_pair_case *tc = &fp48_pairs[i];
        ushort a[3], b[3], r[3];
        int rc;

        snprintf(name, sizeof(name), "pair[%u] a=%06o:%06o:%06o",
                 i, tc->a[0], tc->a[1], tc->a[2]);

        memcpy(a, tc->a, sizeof(a)); memcpy(b, tc->b, sizeof(b));
        NDFloat_Add(a, b, r);
        check3(name, "FAD", tc->add, r);

        memcpy(a, tc->a, sizeof(a)); memcpy(b, tc->b, sizeof(b));
        NDFloat_Sub(a, b, r);
        check3(name, "FSB", tc->sub, r);

        memcpy(a, tc->a, sizeof(a)); memcpy(b, tc->b, sizeof(b));
        NDFloat_Mul(a, b, r);
        check3(name, "FMU", tc->mul, r);

        memcpy(a, tc->a, sizeof(a)); memcpy(b, tc->b, sizeof(b));
        rc = NDFloat_Div(a, b, r);
        check3(name, "FDV", tc->dv, r);
        check_int(name, "FDV rc", tc->div_rc, rc);
    }

    printf("48-bit regression lock: NLZ/DNZ round trips\n");
    for (i = 0; i < sizeof(fp48_nlz) / sizeof(fp48_nlz[0]); i++) {
        const fp48_nlz_case *tc = &fp48_nlz[i];

        snprintf(name, sizeof(name), "nlz[%u] val=%d sc=%d",
                 i, tc->val, tc->scaling);

        REG_T = 0125252;              /* sentinel: DoNLZ must overwrite it */
        REG_A = (ushort)tc->val;
        REG_D = 0177777;
        fptest_z_set = 0;

        DoNLZ((char)tc->scaling);
        check_int(name, "NLZ T", tc->nlz[0], REG_T);
        check_int(name, "NLZ A", tc->nlz[1], REG_A);
        check_int(name, "NLZ D", tc->nlz[2], REG_D);

        DoDNZ((char)-tc->scaling);
        check_int(name, "DNZ T", tc->dnz[0], REG_T);
        check_int(name, "DNZ A", tc->dnz[1], REG_A);
        check_int(name, "DNZ D", tc->dnz[2], REG_D);
        check_int(name, "Z flag", tc->z, fptest_z_set);
    }
}

/* ---------------------------------------------------------------- */
/* Part 2: 32-bit FPP (optional single-precision unit)              */
/*                                                                  */
/* Expected values verified against the FIXED ND110 microcode       */
/* oracle (2026-07-27). The original handoff readings were one      */
/* binade high because of a loop-counter bug in the oracle          */
/* emulator's NLZ (LCOUNT suppressed on the exit pass); with that   */
/* fixed, the packed constants agree with the ND-100 manual         */
/* (NLZ(1) -> 040100), the NLZ->DNZ round trip is the identity,     */
/* and the once-suspected 32-bit-only "extra binade after subtract  */
/* renormalization" rule is disproven (FAD/FSB have no format-      */
/* specific exponent path; the subtract core is shared with 48-bit).*/
/* ---------------------------------------------------------------- */

/* Run one 32-bit op and return the result words. */
static int op32(char op, ushort a0, ushort a1, ushort b0, ushort b1, ushort *r)
{
    ushort a[2] = {a0, a1}, b[2] = {b0, b1};
    switch (op) {
    case '+': return NDFloat_Add32(a, b, r);
    case '-': return NDFloat_Sub32(a, b, r);
    case '*': return NDFloat_Mul32(a, b, r);
    default:  return NDFloat_Div32(a, b, r);
    }
}

typedef struct {
    const char *name;
    char   op;
    ushort a[2], b[2];   /* packed operands */
    ushort exp[2];       /* expected result words */
    int    rc;           /* expected return code (div by zero) */
} fp32_op_case;

/* Arithmetic vectors. The three FSB cases and both FDV cases are direct
 * readings from the fixed microcode oracle; the FAD/FMU/remaining-FSB values
 * are computed with the same encodings and locked as implementation
 * behaviour (self-consistency proven by the FMU->DNZ round trip below). */
static const fp32_op_case fp32_ops[] = {
    {"FSB(4,3) oracle",      '-', {0040300,0}, {0040240,0}, {0040100,0}, 0},
    {"FSB(3,2) oracle",      '-', {0040240,0}, {0040200,0}, {0040100,0}, 0},
    {"FSB(4,3.875) oracle",  '-', {0040300,0}, {0040274,0}, {0037600,0}, 0},
    {"FDV(6,2) oracle",      '/', {0040340,0}, {0040200,0}, {0040240,0}, 0},
    {"FDV(-9,-3) oracle",    '/', {0140410,0}, {0140240,0}, {0040240,0}, 0},
    {"FSB(3,1) = 2",         '-', {0040240,0}, {0040100,0}, {0040200,0}, 0},
    {"FSB(2,1.5) = 0.5",     '-', {0040200,0}, {0040140,0}, {0040000,0}, 0},
    {"FSB(2,1.9375) = 1/16", '-', {0040200,0}, {0040174,0}, {0037500,0}, 0},
    {"FAD(3,1) = 4",         '+', {0040240,0}, {0040100,0}, {0040300,0}, 0},
    {"FMU(1.5,1.5) = 2.25",  '*', {0040140,0}, {0040140,0}, {0040210,0}, 0},
    {"FMU(2,3) = 6",         '*', {0040200,0}, {0040240,0}, {0040340,0}, 0},
};

/* Zero operands are exact special cases; divide by zero returns the
 * largest magnitude with the dividend's sign and rc=1 (caller sets Z). */
static const fp32_op_case fp32_zero[] = {
    {"x + 0",  '+', {0040340,0000001}, {0,0},           {0040340,0000001}, 0},
    {"0 + y",  '+', {0,0},             {0140240,0000002}, {0140240,0000002}, 0},
    {"x - 0",  '-', {0040340,0000001}, {0,0},           {0040340,0000001}, 0},
    {"0 - y",  '-', {0,0},             {0040240,0000003}, {0140240,0000003}, 0},
    {"0 - (-y)", '-', {0,0},           {0140240,0000003}, {0040240,0000003}, 0},
    {"x * 0",  '*', {0040340,0},       {0,0},           {0,0},             0},
    {"0 * y",  '*', {0,0},             {0040340,0},     {0,0},             0},
    {"0 / y",  '/', {0,0},             {0040340,0},     {0,0},             0},
    {"x / 0",  '/', {0040340,0},       {0,0},           {0077777,0177777}, 1},
    {"-x / 0", '/', {0140340,0},       {0,0},           {0177777,0177777}, 1},
    {"x - x",  '-', {0040340,0000005}, {0040340,0000005}, {0,0},           0},
    {"x + (-x)", '+', {0040340,0000005}, {0140340,0000005}, {0,0},         0},
};

typedef struct {
    int    val;       /* initial A register (signed) */
    ushort nlz[2];    /* expected A,D after DoNLZ32(+16) */
    int    z;         /* expected Z during the DNZ round trip */
} fp32_nlz_case;

/* NLZ(+16) packed constants (1/-1/3 read off the fixed oracle; they match
 * the ND-100 manual's worked examples) and the DNZ(-16) identity round
 * trip. -32768 survives the round trip but raises the overflow indicator
 * (the magnitude 32768 exceeds +32767 before the final negation). */
static const fp32_nlz_case fp32_nlz[] = {
    {     0, {0000000, 0000000}, 0},
    {     1, {0040100, 0000000}, 0},
    {    -1, {0140100, 0000000}, 0},
    {     3, {0040240, 0000000}, 0},
    {    -3, {0140240, 0000000}, 0},
    {   255, {0041077, 0100000}, 0},
    {  -255, {0141077, 0100000}, 0},
    { 32767, {0041777, 0177400}, 0},
    {-32768, {0142000, 0000000}, 1},
    { 21845, {0041725, 0052400}, 0},
};

static void run_fp32(void)
{
    unsigned i;
    ushort r[2];
    int rc;
    char name[64];

    printf("32-bit FPP: arithmetic (fixed-oracle vectors)\n");
    for (i = 0; i < sizeof(fp32_ops) / sizeof(fp32_ops[0]); i++) {
        const fp32_op_case *tc = &fp32_ops[i];
        rc = op32(tc->op, tc->a[0], tc->a[1], tc->b[0], tc->b[1], r);
        check_int(tc->name, "r[0]", tc->exp[0], r[0]);
        check_int(tc->name, "r[1]", tc->exp[1], r[1]);
        check_int(tc->name, "rc",   tc->rc,     rc);
    }

    printf("32-bit FPP: zero operands and divide by zero\n");
    for (i = 0; i < sizeof(fp32_zero) / sizeof(fp32_zero[0]); i++) {
        const fp32_op_case *tc = &fp32_zero[i];
        rc = op32(tc->op, tc->a[0], tc->a[1], tc->b[0], tc->b[1], r);
        check_int(tc->name, "r[0]", tc->exp[0], r[0]);
        check_int(tc->name, "r[1]", tc->exp[1], r[1]);
        check_int(tc->name, "rc",   tc->rc,     rc);
    }

    printf("32-bit FPP: NLZ constants and DNZ identity round trip\n");
    for (i = 0; i < sizeof(fp32_nlz) / sizeof(fp32_nlz[0]); i++) {
        const fp32_nlz_case *tc = &fp32_nlz[i];

        snprintf(name, sizeof(name), "nlz32 %d", tc->val);
        REG_T = 0125252;               /* sentinel: must NEVER change */
        REG_A = (ushort)tc->val;
        REG_D = 0177777;
        fptest_z_set = 0;

        DoNLZ32(16);
        check_int(name, "NLZ A", tc->nlz[0], REG_A);
        check_int(name, "NLZ D", tc->nlz[1], REG_D);
        check_int(name, "NLZ T", 0125252,    REG_T);

        DoDNZ32(-16);
        check_int(name, "DNZ A (identity)", (ushort)tc->val, REG_A);
        check_int(name, "DNZ D", 0,       REG_D);
        check_int(name, "DNZ T", 0125252, REG_T);
        check_int(name, "Z flag", tc->z,  fptest_z_set);
    }

    /* NLZ with scaling +17 sits one binade above the integer-preserving +16:
     * NLZ(+17) of 1 -> 040200 (= 2.0). Read off the fixed oracle. */
    {
        REG_T = 0125252; REG_A = 1; REG_D = 0177777;
        DoNLZ32(17);
        check_int("nlz32(+17) of 1", "A", 0040200, REG_A);
        check_int("nlz32(+17) of 1", "D", 0,       REG_D);
        check_int("nlz32(+17) of 1", "T", 0125252, REG_T);
        DoDNZ32(-17);
        check_int("nlz32(+17) of 1", "round trip", 1, REG_A);
    }

    printf("32-bit FPP: FPP detection rule (48-bit NLZ must change T)\n");
    {
        REG_T = 0125252;
        REG_A = 1;
        REG_D = 0;
        DoNLZ(16);
        fptest_total++;
        if (REG_T == 0125252) {
            printf("  FAIL  detect48: T unchanged by 48-bit NLZ (must change)\n");
            fptest_failed++;
        }
    }

    printf("32-bit FPP: DNZ32 overflow sets Z\n");
    {
        /* 042000:000000 (= 32768.0) denormalizes to magnitude 32768: overflow. */
        REG_T = 0125252;
        REG_A = 0042000;
        REG_D = 0;
        fptest_z_set = 0;
        DoDNZ32(-16);
        check_int("dnz32 ovf", "A", 0100000, REG_A);
        check_int("dnz32 ovf", "Z", 1, fptest_z_set);
        check_int("dnz32 ovf", "T", 0125252, REG_T);

        /* Floating zero converts to integer 0 with no flags. */
        REG_A = 0; REG_D = 0; fptest_z_set = 0;
        DoDNZ32(-16);
        check_int("dnz32 zero", "A", 0, REG_A);
        check_int("dnz32 zero", "Z", 0, fptest_z_set);
    }

    printf("32-bit FPP: FMU -> DNZ self-consistency\n");
    {
        /* float(2) * float(3) denormalizes back to the integer 6. */
        ushort a[2] = {0040200, 0}, b[2] = {0040240, 0};
        NDFloat_Mul32(a, b, r);
        REG_T = 0125252; REG_A = r[0]; REG_D = r[1]; fptest_z_set = 0;
        DoDNZ32(-16);
        check_int("fmu(2,3)->dnz", "A", 6, REG_A);
        check_int("fmu(2,3)->dnz", "Z", 0, fptest_z_set);
        check_int("fmu(2,3)->dnz", "T", 0125252, REG_T);
    }
}

/* ---------------------------------------------------------------- */

int main(void)
{
    memset(&fptest_regs, 0, sizeof(fptest_regs));

    run_fp48_lock();
    run_fp32();

    printf("float tests: %d checks, %d failed\n", fptest_total, fptest_failed);
    return fptest_failed ? 1 : 0;
}
