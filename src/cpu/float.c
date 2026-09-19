/*
 * nd100x - ND100 Virtual Machine
 *
 * Copyright (c) 2008 Zdravko
 *
 * This file is originated from the nd100em project.
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
 */

/*
 * 48-bit floating point arithmetic for the ND-100.
 *
 * Reimplemented using pure integer arithmetic (ported from the SIMH
 * ND100 simulator) instead of the previous long double approach.
 * This gives bit-exact results matching the real hardware.
 *
 * ND-100 48-bit float format (3 x 16-bit words):
 *   Word 0 (T register): bit 15 = sign, bits 14-0 = exponent (biased 16384)
 *   Word 1 (A register): upper 16 bits of mantissa
 *   Word 2 (D register): lower 16 bits of mantissa
 *   Mantissa is normalized: 0.5 <= |mantissa| < 1.0
 */

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

#include "cpu_types.h"
#include "cpu_protos.h"

int NDFloat_Div(unsigned short int *p_a, unsigned short int *p_b, unsigned short int *p_r);
int NDFloat_Mul(unsigned short int *p_a, unsigned short int *p_b, unsigned short int *p_r);
int NDFloat_Add(unsigned short int *p_a, unsigned short int *p_b, unsigned short int *p_r);
int NDFloat_Sub(unsigned short int *p_a, unsigned short int *p_b, unsigned short int *p_r);

void DoNLZ(char scaling);
void DoDNZ(char scaling);

/*
 * Internal floating point representation.
 * Used to unpack/repack the 48-bit ND format for arithmetic.
 */
// clang-format off
struct fp {
    int s;          /* sign: 0 = positive, 1 = negative */
    int e;          /* exponent (unbiased) */
    uint64_t m;     /* mantissa in upper 32 bits of a 64-bit value (for mul/div) */
};
// clang-format on

/*
 * Unpack three 16-bit words into the internal fp struct.
 * The mantissa is placed in the lower 32 bits (for add/sub)
 * or can be shifted as needed (mul/div do their own shifting).
 */
static void mkfp48(struct fp *fp, uint16_t w1, uint16_t w2, uint16_t w3)
{
    fp->s = (w1 >> 15) & 1;
    fp->e = (w1 & 0x7FFF) - 16384;
    fp->m = ((uint64_t)w2 << 16) + (uint64_t)w3;
}

/*
 * add_core - Add two same-sign normalized mantissas.
 *
 * The arithmetic body shared by the 48-bit and 32-bit FPP paths: align on
 * the larger exponent (folding shifted-out bits into a guard bit), add, and
 * renormalize one step if the sum carried out of bit 31. Only the packing
 * of (s, e, m3) back into words differs between the two formats.
 */
static void add_core(struct fp *f1, struct fp *f2, int *s, int *e, uint64_t *m3)
{
    struct fp *ft;
    int scale, gbit;

    /* Ensure f1 has the larger exponent */
    if (f2->e > f1->e)
    {
        ft = f1;
        f1 = f2;
        f2 = ft;
    }

    if ((scale = f1->e - f2->e) > 31)
    {
        *m3 = f1->m;
        *s = f1->s;
        *e = f1->e;
        return;
    }

    /* get shifted out guard bit */
    gbit = scale ? (((1LL << scale) - 1) & f2->m) != 0 : 0;
    f2->m >>= scale;
    *m3 = (f1->m + f2->m) | gbit;
    if (*m3 > 0xffffffffLL)
    {
        *m3 >>= 1;
        f1->e++;
    }
    *s = f1->s;
    *e = f1->e;
}

/*
 * add48 - Add two 48-bit floating point numbers with same sign.
 *
 * Result is written to the output array r[3].
 */
static void add48(struct fp *f1, struct fp *f2, uint16_t *r)
{
    uint64_t m3;
    int s, e;

    add_core(f1, f2, &s, &e, &m3);

    r[0] = (e + 16384) | (s << 15);
    r[1] = (uint16_t)(m3 >> 16);
    r[2] = (uint16_t)m3;
}

/*
 * sub_core - Subtract two opposite-sign normalized mantissas.
 *
 * The arithmetic body shared by the 48-bit and 32-bit FPP paths: align on
 * the larger exponent (folding shifted-out bits into a sticky bit), subtract
 * the smaller magnitude, and left-renormalize, decrementing the exponent per
 * shift. Only the packing of (s, e, m3) back into words differs between the
 * two formats. (An earlier 32-bit-only "extra binade after renormalization"
 * rule was an oracle artifact - a loop-counter bug in the ND110 microcode
 * emulator's NLZ - and was disproven against the fixed oracle 2026-07-27:
 * FSB(4-3) -> 040100, FSB(3-2) -> 040100, FSB(4-3.875) -> 037600. The FAD/FSB
 * microcode has no format-specific exponent path.)
 */
static void sub_core(struct fp *f1, struct fp *f2, int *s, int *e, uint64_t *m3, bool *isZero)
{
    struct fp *ft;
    int scale, gbit;

    *isZero = false;

    /* Ensure f1 has the larger exponent */
    if (f2->e > f1->e)
    {
        ft = f1;
        f1 = f2;
        f2 = ft;
    }

    if ((scale = f1->e - f2->e) > 31)
    {
        *m3 = f1->m;
        *s = f1->s;
        *e = f1->e;
        return;
    }

    /* get shifted out sticky bit */
    gbit = scale ? (((1LL << scale) - 1) & f2->m) != 0 : 0;
    f2->m >>= scale;
    f2->e = f1->e;

    /* check for swap of mantissa */
    if (f2->m > f1->m)
    {
        ft = f1;
        f1 = f2;
        f2 = ft;
    }
    *m3 = (f1->m - f2->m) | gbit;

    if (*m3 == 0)
    {
        *s = 0;
        *e = 0;
        *isZero = true;
        return;
    }

    /* normalize */
    while ((*m3 & 0x80000000LL) == 0)
    {
        *m3 <<= 1;
        f1->e--;
    }

    *s = f1->s;
    *e = f1->e;
}

/*
 * sub48 - Subtract two 48-bit floating point numbers with different signs.
 *
 * Result is written to the output array r[3].
 */
static void sub48(struct fp *f1, struct fp *f2, uint16_t *r)
{
    uint64_t m3;
    int s, e;
    bool isZero;

    sub_core(f1, f2, &s, &e, &m3, &isZero);

    if (isZero)
    {
        r[0] = r[1] = r[2] = 0;
        return;
    }

    r[0] = (e + 16384) | (s << 15);
    r[1] = (uint16_t)(m3 >> 16);
    r[2] = (uint16_t)m3;
}

/*
 * NDFloat_Add - Add two 48-bit floating point numbers.
 *
 * The contents of the effective location and the two following locations
 * are added to the floating accumulator with the result in the floating
 * accumulator.
 */
int NDFloat_Add(uint16_t *p_a, uint16_t *p_b, uint16_t *p_r)
{
    struct fp f1, f2;

    mkfp48(&f1, p_a[0], p_a[1], p_a[2]);
    mkfp48(&f2, p_b[0], p_b[1], p_b[2]);

    if (f1.s ^ f2.s)
    {
        sub48(&f1, &f2, p_r);
    }
    else
    {
        add48(&f1, &f2, p_r);
    }
    return 0;
}

/*
 * NDFloat_Sub - Subtract two 48-bit floating point numbers.
 *
 * The contents of the effective location and the two following locations
 * are subtracted from the floating accumulator with the result
 * in the floating accumulator.
 */
int NDFloat_Sub(uint16_t *p_a, uint16_t *p_b, uint16_t *p_r)
{
    struct fp f1, f2;

    mkfp48(&f1, p_a[0], p_a[1], p_a[2]);
    mkfp48(&f2, p_b[0], p_b[1], p_b[2]);

    /* swap sign of the second operand (b) for subtraction */
    f2.s ^= 1;

    if (f1.s ^ f2.s)
    {
        sub48(&f1, &f2, p_r);
    }
    else
    {
        add48(&f1, &f2, p_r);
    }
    return 0;
}

/*
 * NDFloat_Mul - Multiply two 48-bit floating point numbers.
 *
 * The contents of the floating accumulator are multiplied with the
 * number at the effective floating word locations with the result in
 * the floating accumulator.
 */
int NDFloat_Mul(uint16_t *p_a, uint16_t *p_b, uint16_t *p_r)
{
    struct fp f1, f2;
    int s3, e3;
    uint64_t m3;

    mkfp48(&f1, p_a[0], p_a[1], p_a[2]);
    mkfp48(&f2, p_b[0], p_b[1], p_b[2]);

    /* calc */
    m3 = f1.m * f2.m;
    e3 = f1.e + f2.e;
    s3 = f1.s ^ f2.s;

    /* normalize (if needed) */
    if ((m3 & (1ULL << 63)) == 0)
    {
        m3 <<= 1;
        e3--;
    }

    /* store result */
    p_r[1] = (uint16_t)(m3 >> 48);
    p_r[2] = (uint16_t)(m3 >> 32);
    p_r[0] = (e3 + 16384) | (s3 << 15);
    if (m3 == 0 || e3 < -16383)
    {
        p_r[0] = p_r[1] = p_r[2] = 0;
    }
    return 0;
}

/*
 * NDFloat_Div - Divide two 48-bit floating point numbers.
 *
 * The contents of the floating accumulator (p_a) are divided by the number
 * at the effective floating word locations (p_b). Result in p_r.
 * If division by zero is attempted, the error indicator Z is set.
 */
int NDFloat_Div(uint16_t *p_a, uint16_t *p_b, uint16_t *p_r)
{
    struct fp f1, f2;
    int s3, e3;
    uint64_t m3;

    /* f1 = divisor (from memory, p_b) */
    mkfp48(&f1, p_b[0], p_b[1], p_b[2]);

    /* f2 = dividend (from registers, p_a) */
    mkfp48(&f2, p_a[0], p_a[1], p_a[2]);
    f2.m <<= 32;

    /* Division by zero check */
    if (f1.m == 0)
    {
        p_r[0] = p_a[0] | 0x7FFF;
        p_r[1] = 0xFFFF;
        p_r[2] = 0xFFFF;
        return 1; /* division by zero */
    }

    /* calc */
    s3 = f1.s ^ f2.s;
    e3 = f2.e - f1.e;
    m3 = f2.m / f1.m;
    if (f2.m % f1.m) /* "guard" bit */
    {
        m3++;
    }

    /* normalize (if needed) */
    if (m3 >= (1ULL << 32))
    {
        m3 >>= 1;
        e3++;
    }

    /* store result */
    p_r[1] = (uint16_t)(m3 >> 16);
    p_r[2] = (uint16_t)m3;
    p_r[0] = (e3 + 16384) | (s3 << 15);
    if (f2.m == 0 || e3 < -16383)
    {
        p_r[0] = p_r[1] = p_r[2] = 0;
    }
    return 0;
}

/*
 * DoNLZ - Normalize (integer to floating point).
 *
 * Converts the number in the A register to a standard form floating
 * number in the floating accumulator {T,A,D}, using the scaling factor.
 * For integers, the scaling factor should be +16.
 * Because of the single precision fixed point number, the D register
 * will be cleared.
 */
void DoNLZ(char scaling)
{
    int sh, s;
    int val;

    s = 0;
    gD = 0;
    if (gA == 0)
    { /* zero, special case */
        gT = 0;
        return;
    }

    val = (int)(int16_t)gA;
    sh = 16384 + (int)(signed char)scaling;
    if (val < 0)
    {
        val = -val;
        s = 0x8000;
    }
    if (val > 32767)
    {
        val >>= 1;
        sh++;
    }
    while ((val & 0x8000) == 0)
    {
        val <<= 1;
        sh--;
    }
    gT = sh + s;
    gA = (uint16_t)val;
}

/*
 * DoDNZ - Denormalize (floating point to integer).
 *
 * Converts the floating number in the floating accumulator to a
 * single precision fixed point number in the A register, using the
 * scaling factor.
 *
 * When converting to integers, the scaling factor should be -16 (0xF0).
 * If the conversion causes underflow, T, A, D are all set to zero.
 * If overflow occurs, the error indicator Z is set.
 * Negative numbers are converted to positive before conversion,
 * then the result is negated.
 */
void DoDNZ(char scaling)
{
    int32_t val = 0;
    int sh;

    sh = (gT & 0x7FFF) - 16384 + (int)(signed char)scaling;
    if (sh < 0)
    {
        /*
         * Right-shift the mantissa down to the integer.  A C shift by >= the
         * operand width (here -sh >= 32) is UNDEFINED BEHAVIOUR, and on real
         * ND hardware such a large downscale simply underflows the fixed-point
         * result to zero.  Guard the shift so deep underflow yields 0 instead
         * of garbage (matches the ND-100 microcode ZAD2 zero-path).
         */
        if (-sh >= 32)
        {
            val = 0;
        }
        else
        {
            val = gA;
            val >>= -sh;
        }
    }
    else if (sh > 0)
    {
        val = gA;
        val <<= sh;
        if (val > 32767)
        {
            setbit(_STS, _Z, 1);
        }
    }

    if (gT & 0x8000)
    {
        val = -val;
    }
    gT = 0;
    gD = 0;
    gA = (uint16_t)val;
}

/* ================================================================
 * Optional 32-bit single-precision FPP (CurrentFPPType == FPP32).
 *
 * ND-100 32-bit float format (2 x 16-bit words, the A,D pair; the T
 * register is NOT part of the accumulator and must never be written):
 *   A word: bit 15    = sign
 *           bits 14-6 = exponent, 9 bits, biased by 257
 *           bits 5-0  = mantissa bits 21..16 (top stored fraction bits)
 *   D word: mantissa bits 15..0
 * The mantissa is hidden-bit: 23 significant bits = 1 implicit MSB +
 * 22 stored bits, normalized so the value lies in [0.5, 1).
 * Floating zero = all 32 bits zero.
 *
 * The bias of 257 (not the manual's 256) and the exponent adjustments
 * marked "verified against the oracle" below were derived from and
 * validated against a live 32-bit-FPP ND-110 running RASK microcode
 * (RetroCore Nd100FloatMath.cs is the reference implementation).
 * ================================================================ */

#define FP32_BIAS 257

int NDFloat_Add32(unsigned short int *p_a, unsigned short int *p_b, unsigned short int *p_r);
int NDFloat_Sub32(unsigned short int *p_a, unsigned short int *p_b, unsigned short int *p_r);
int NDFloat_Mul32(unsigned short int *p_a, unsigned short int *p_b, unsigned short int *p_r);
int NDFloat_Div32(unsigned short int *p_a, unsigned short int *p_b, unsigned short int *p_r);
void DoNLZ32(char scaling);
void DoDNZ32(char scaling);

/*
 * mkfp32 - Unpack the A,D word pair into the internal fp struct.
 *
 * The 23-bit hidden-bit mantissa is left-justified so its MSB lands at
 * bit 31 - the same normalized shape the shared arithmetic core uses.
 */
static void mkfp32(struct fp *fp, uint16_t a, uint16_t d)
{
    uint32_t mant23;

    if (a == 0 && d == 0)
    { /* floating zero */
        fp->s = 0;
        fp->e = 0;
        fp->m = 0;
        return;
    }
    fp->s = (a >> 15) & 1;
    fp->e = ((a >> 6) & 0x1FF) - FP32_BIAS;
    mant23 = (1u << 22) | ((uint32_t)(a & 0x3F) << 16) | d;
    fp->m = (uint64_t)mant23 << 9;
}

/*
 * pack32 - Pack (sign, unbiased exponent, 32-bit mantissa MSB@31) into
 * the A,D pair. TRUNCATES to 23 significant bits - the hardware keeps 23,
 * and the low guard bits have already folded their sticky bit into the
 * LSB during add/sub. Exponent underflow yields floating zero.
 */
static void pack32(int s, int e, uint64_t m, uint16_t *a, uint16_t *d)
{
    int eb, i;
    uint32_t mant23;

    if (m == 0)
    {
        *a = 0;
        *d = 0;
        return;
    }

    /* Safety-net renormalization: bring the MSB to bit 31. The bounds of 40
     * are pure paranoia - a sane input needs at most ~32 iterations. */
    for (i = 0; i < 40 && m > 0xFFFFFFFFULL; i++)
    {
        m >>= 1;
        e++;
    }
    for (i = 0; i < 40 && (m & 0x80000000ULL) == 0; i++)
    {
        m <<= 1;
        e--;
    }

    eb = e + FP32_BIAS;
    if (eb <= 0)
    {
        *a = 0;
        *d = 0;
        return;
    } /* underflow -> floating zero */
    if (eb > 0x1FF)
    {
        eb = 0x1FF;
    } /* overflow -> saturate; UNVERIFIED
                                                   * against hardware, chosen as a
                                                   * safe fallback (known gap)     */

    mant23 = (uint32_t)(m >> 9); /* top 23 bits, hidden MSB @ bit22 */
    *a = (uint16_t)((s << 15) | ((eb & 0x1FF) << 6) | ((mant23 >> 16) & 0x3F));
    *d = (uint16_t)(mant23 & 0xFFFF);
}

/*
 * NDFloat_Add32 - Add two 32-bit floating point numbers (reg + mem).
 *
 * Zero operands are exact special cases handled before the core (the
 * core assumes non-zero normalized mantissas).
 */
int NDFloat_Add32(uint16_t *p_a, uint16_t *p_b, uint16_t *p_r)
{
    struct fp f1, f2;
    int s, e;
    uint64_t m3;
    bool isZero;

    mkfp32(&f1, p_a[0], p_a[1]);
    mkfp32(&f2, p_b[0], p_b[1]);

    if (f1.m == 0)
    {
        p_r[0] = p_b[0];
        p_r[1] = p_b[1];
        return 0;
    } /* 0 + y = y */
    if (f2.m == 0)
    {
        p_r[0] = p_a[0];
        p_r[1] = p_a[1];
        return 0;
    } /* x + 0 = x */

    if (f1.s ^ f2.s)
    {
        sub_core(&f1, &f2, &s, &e, &m3, &isZero);
        if (isZero)
        {
            p_r[0] = 0;
            p_r[1] = 0;
            return 0;
        }
    }
    else
    {
        add_core(&f1, &f2, &s, &e, &m3);
    }
    pack32(s, e, m3, &p_r[0], &p_r[1]);
    return 0;
}

/*
 * NDFloat_Sub32 - Subtract two 32-bit floating point numbers (reg - mem).
 */
int NDFloat_Sub32(uint16_t *p_a, uint16_t *p_b, uint16_t *p_r)
{
    struct fp f1, f2;
    int s, e;
    uint64_t m3;
    bool isZero;

    mkfp32(&f1, p_a[0], p_a[1]);
    mkfp32(&f2, p_b[0], p_b[1]);

    if (f2.m == 0)
    {
        p_r[0] = p_a[0];
        p_r[1] = p_a[1];
        return 0;
    } /* x - 0 = x */
    if (f1.m == 0)
    { /* 0 - y = -y */
        p_r[0] = (uint16_t)(p_b[0] ^ 0x8000);
        p_r[1] = p_b[1];
        return 0;
    }

    f2.s ^= 1; /* subtract == add the negation */

    if (f1.s ^ f2.s)
    {
        sub_core(&f1, &f2, &s, &e, &m3, &isZero);
        if (isZero)
        {
            p_r[0] = 0;
            p_r[1] = 0;
            return 0;
        }
    }
    else
    {
        add_core(&f1, &f2, &s, &e, &m3);
    }
    pack32(s, e, m3, &p_r[0], &p_r[1]);
    return 0;
}

/*
 * NDFloat_Mul32 - Multiply two 32-bit floating point numbers.
 */
int NDFloat_Mul32(uint16_t *p_a, uint16_t *p_b, uint16_t *p_r)
{
    struct fp f1, f2;
    int s3, e3;
    uint64_t m3, m32;

    mkfp32(&f1, p_a[0], p_a[1]);
    mkfp32(&f2, p_b[0], p_b[1]);

    if (f1.m == 0 || f2.m == 0)
    {
        p_r[0] = 0;
        p_r[1] = 0;
        return 0;
    }

    m3 = f1.m * f2.m;     /* 32 x 32 -> 64 */
    e3 = f1.e + f2.e + 1; /* the +1 differs from the 48-bit path: it
                                  * compensates the hidden-bit packing (the
                                  * internal e is one below the packed binade) */
    s3 = f1.s ^ f2.s;

    /* Normalize: if the product's MSB landed at bit 62 rather than 63, shift up. */
    if ((m3 & (1ULL << 63)) == 0)
    {
        m3 <<= 1;
        e3--;
    }

    m32 = m3 >> 32; /* back to a 32-bit mantissa, MSB @ bit31 */
    pack32(s3, e3, m32, &p_r[0], &p_r[1]);
    return 0;
}

/*
 * NDFloat_Div32 - Divide two 32-bit floating point numbers.
 *
 * Returns non-zero on divide-by-zero (caller sets the error indicator Z).
 */
int NDFloat_Div32(uint16_t *p_a, uint16_t *p_b, uint16_t *p_r)
{
    struct fp f1, f2;
    int s3, e3;
    uint64_t m3;

    mkfp32(&f1, p_b[0], p_b[1]); /* divisor  (memory)    */
    mkfp32(&f2, p_a[0], p_a[1]); /* dividend (registers) */

    if (f1.m == 0)
    {
        /* ND returns the largest magnitude carrying the dividend's sign */
        p_r[0] = (uint16_t)((p_a[0] & 0x8000) | 0x7FFF);
        p_r[1] = 0xFFFF;
        return 1;
    }
    if (f2.m == 0)
    {
        p_r[0] = 0;
        p_r[1] = 0;
        return 0;
    } /* 0 / x = 0 */

    f2.m <<= 32;
    s3 = f1.s ^ f2.s;
    e3 = f2.e - f1.e - 1; /* the -1 differs from the 48-bit path: it
                                    * compensates the hidden-bit packing (the
                                    * internal e is one below the packed binade;
                                    * the offsets cancel in the subtraction and
                                    * the -1 restores the packed convention).
                                    * Oracle: FDV(6,2) -> 040240 = 3. */
    m3 = f2.m / f1.m;
    if (f2.m % f1.m)
    {
        m3++; /* guard bit */
    }

    /* Quotient of two normalized mantissas is in (0.5, 2). If it reached >= 1
     * (bit 32 set), shift back into [0.5,1) and bump the exponent. */
    if (m3 >= (1ULL << 32))
    {
        m3 >>= 1;
        e3++;
    }

    pack32(s3, e3, m3, &p_r[0], &p_r[1]);
    return 0;
}

/*
 * DoNLZ32 - Normalize (integer to 32-bit floating point).
 *
 * Same shift-based normalization as DoNLZ, but the result is packed into
 * the A,D pair and the T register is NEVER written. That is exactly what
 * the manual's FPP detection sequence (SAT 0 / SAA 1 / NLZ 20) keys on:
 * if T changed the machine has the 48-bit FPP, if T is untouched it has
 * the 32-bit FPP.
 */
void DoNLZ32(char scaling)
{
    int sh, s, val, e;
    uint64_t m;

    gD = 0;
    if (gA == 0)
    {
        return; /* integer zero -> floating zero (A,D already 0) */
    }

    s = 0;
    val = (int)(int16_t)gA;
    sh = 16384 + (int)(signed char)scaling;
    if (val < 0)
    {
        val = -val;
        s = 1;
    }
    if (val > 32767)
    {
        val >>= 1;
        sh++;
    }
    while ((val & 0x8000) == 0)
    {
        val <<= 1;
        sh--;
    }

    /* The intermediate keeps the 16384 bias so the normalization loop stays
     * identical to the 48-bit one; pack32 re-biases with 257. The extra -1
     * places the result in the manual's binade: NLZ(+16) of 1 -> 040100,
     * of 3 -> 040240, of -1 -> 140100, and NLZ(+17) of 1 -> 040200 - all
     * confirmed against the FIXED microcode oracle 2026-07-27 (the earlier
     * one-binade-high oracle readings came from a loop-counter bug in the
     * ND110 microcode emulator's NLZ, since root-caused and fixed). This
     * also makes the NLZ(+16) -> DNZ(-16) round trip the identity, which
     * DoDNZ32's shift formula is calibrated to. */
    e = sh - 16384 - 1;
    m = ((uint64_t)(uint32_t)val) << 16; /* 16-bit mantissa MSB@15 -> MSB@31 */
    pack32(s, e, m, &gA, &gD);
    /* gT deliberately NOT touched - this is what the 32/48 detection test keys on. */
}

/*
 * DoDNZ32 - Denormalize (32-bit floating point to integer).
 *
 * Converts the floating number in the A,D pair to a single precision
 * fixed point number in the A register. Sets the error indicator Z on
 * overflow, like DoDNZ. The T register is NEVER written.
 */
void DoDNZ32(char scaling)
{
    int s, e, shift;
    uint32_t mant23;
    int64_t val;

    if (gA == 0 && gD == 0)
    {
        gD = 0;
        return;
    } /* floating zero -> integer 0 */

    s = (gA >> 15) & 1;
    e = ((gA >> 6) & 0x1FF) - FP32_BIAS;
    mant23 = (1u << 22) | ((uint32_t)(gA & 0x3F) << 16) | gD;

    /* Extract the fixed-point integer by right-shifting the 23-bit mantissa.
     * The shift amount was derived empirically from the live 32-bit RASK
     * oracle via the NLZ(+16) -> DNZ(-16) round trip: for scaling = -16 the
     * shift is (22 - e), and each +1 of scaling halves the shift (doubles the
     * result). Hence shift = 6 - e - scaling.
     * UNVERIFIED (known gap): behaviour for scaling factors other than -16 -
     * the manual says other factors "will not cause a different result but
     * will affect the test for overflow", which this formula does not model
     * exactly. Do not extend without new oracle data. */
    shift = 6 - e - (int)(signed char)scaling;

    /* A C shift by >= the operand width is undefined behaviour; real ND
     * hardware just underflows to zero (cf. the guard in DoDNZ). */
    if (shift >= 0)
    {
        val = (shift >= 32) ? 0 : (int64_t)(mant23 >> shift); /* deep underflow -> 0 */
    }
    else
    {
        int ls = -shift;
        val = (ls >= 41) ? 0x7FFFFFFFLL : ((int64_t)mant23 << ls); /* overflows anyway */
    }

    if (val > 32767)
    {
        setbit(_STS, _Z, 1);
    }
    if (s)
    {
        val = -val;
    }

    gA = (uint16_t)val;
    gD = 0;
    /* gT deliberately NOT touched. */
}
