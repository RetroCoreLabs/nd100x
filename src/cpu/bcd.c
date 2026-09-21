/*
 * bcd.c - BCD Commercial Extended instructions ADDD, SUBD, COMD, SHDE, PACK, UPACK.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * BCD (Binary Coded Decimal) arithmetic for the ND-100 Commercial Extended
 * (CE) instruction set: ADDD, SUBD, COMD, SHDE, PACK, UPACK.
 *
 * Approach: EXACT arithmetic directly on the packed decimal nibbles.
 *
 * WHY (this replaced a host-`double` engine):
 *   The previous implementation converted each BCD field to a C `double`, did
 *   the arithmetic in binary floating point and converted the result back.  A
 *   double carries only ~15-16 significant decimal digits, but an ND decimal
 *   field may hold up to 31 significant digits (field length is 32 nibbles
 *   INCLUDING the sign), so every operand or result beyond ~15 digits silently
 *   lost precision (classic failure: 100000000000000001 + 1).  It also could
 *   not reproduce the hardware's truncate-and-error overflow behaviour, the
 *   sign-of-zero rule or the byte-aligned field layout.
 *
 *   The engine below mirrors the RASK microcode instead:
 *     SADD  (CS 010207) word-at-a-time decimal add,  per-nibble "add 6" carry;
 *     SSUB  (CS 010200) word-at-a-time decimal subtract with borrow;
 *     ADDE  (CS 010520-010722) the shared ADDD/SUBD driver: SUBD flips op2's
 *           sign and then re-uses the ADDE body; the SIGN block (CS 010677)
 *           does a magnitude ADD for equal signs and a magnitude SUBTRACT for
 *           unequal signs, the result taking the sign of the LARGER magnitude;
 *     TFIRE (CS 010563) "TEST FIRST OPERAND EMPTY" - an EMPTY first operand is
 *           NOT an error, it simply falls through into the ordinary driver;
 *     OVFLO (CS 011054) the ONLY error return of ADDD/SUBD is decimal overflow;
 *     SHDE  (CS 011121-011360) the decimal shifter (LEFT 011174 / RIGHT 011360);
 *     PACK  (CS 011533) reads the X.T operand FIRST, then the A.D operand.
 *   Schoolbook per-digit decimal arithmetic on extracted magnitude buffers is
 *   exactly equivalent to the microcode's nibble add-6/sub-6 and far clearer.
 *
 *   This is a port of the rewritten RetroCore C# engine
 *   (Emulated.HW/ND/CPU/ND100/Instructions.DecimalInstructions.cs), whose rules
 *   were locked against the LIVE ND-110/ND-120 microcode oracle and against the
 *   real ND TPE "INSTRUCTION" diagnostic.  Each non-obvious rule below carries
 *   the oracle/microcode citation it came from - do NOT "simplify" them away.
 *
 * FIELD LAYOUT (byte alignment - the single most important rule here):
 *   ND packed decimal is BYTE ALIGNED.  A field whose descriptor length is L
 *   nibbles (sign included) actually occupies M = L + (L & 1) nibbles, i.e.
 *   M/2 WHOLE bytes.  The SIGN nibble is the LAST nibble of that byte-aligned
 *   field (index M-1), and the magnitude digits are right adjusted below it,
 *   zero padded on the most-significant end (for odd L the extra leading nibble
 *   is a forced zero PAD, not extra precision).
 *   Manual ND-06.014.02: "the least significant digits (and sign) are placed
 *   right adjusted from the last byte of the field."
 *   Byte alignment changes the LAYOUT only - the significant-digit CAPACITY,
 *   and therefore the overflow threshold, stays L-1.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include "cpu_types.h"
#include "cpu_protos.h"


/* Max BCD field length in nibbles (descriptor bits 0-4 give 0..31, and the
 * manual caps the field at 32 nibbles/bytes). */
#define BCD_MAX_NIBBLES 32

/* Words a field can span.  32 nibbles = 8 words; the two extra slots absorb the
 * `lr` (start-in-right-byte) skew and the odd-L byte-align pad so nibble/byte
 * indexing can never run off the end of the struct.  Only the first
 * bcd_mem_words() words are ever read from / written back to memory. */
#define BCD_MAX_WORDS 10

/* BCD sign nibble values written OUT.  (Input decoding is wider - see
 * bcd_decode_sign: A/C/E = +, B/D = -, F = unsigned.) */
// clang-format off
#define BCD_SIGN_POS 0x0C  /* 14 oct */
#define BCD_SIGN_NEG 0x0D  /* 15 oct */
#define BCD_SIGN_UNS 0x0F  /* 17 oct */
// clang-format on

/* Sign representation for ASCII operands, D2 bits 13-11. */
// clang-format off
#define BCD_ASCII_EMBEDDED_TRAILING 0  /* rightmost byte = LSD *and* sign (default) */
#define BCD_ASCII_SEPARATE_TRAILING 1  /* rightmost byte = ASCII sign only */
#define BCD_ASCII_EMBEDDED_LEADING  2  /* leftmost byte  = MSD *and* sign */
#define BCD_ASCII_SEPARATE_LEADING  3  /* leftmost byte  = ASCII sign only */
#define BCD_ASCII_UNSIGNED          4  /* bit 13 set: unsigned (BCD sign code 17 oct) */
// clang-format on

/* Error codes reported in the D register by PACK/UPACK. */
#define BCD_ERR_ILLEGAL_CODE 2
#define BCD_ERR_OVERFLOW     3

/*
 * One decoded decimal operand (descriptor D1/D2 + the field words it covers).
 * The field words are read ONCE on load and written back ONCE on store, so all
 * nibble/byte updates are a read-modify-write and any bits of a boundary word
 * that lie OUTSIDE the field are preserved.
 */
typedef struct
{
    uint16_t addr;                 /* D1: word address of the field       */
    uint16_t d2;                   /* D2: the raw descriptor              */
    bool starts_in_right_byte;     /* D2 bit 15 (lr)                      */
    int ascii_format;              /* D2 bits 13-11 (BCD_ASCII_*)         */
    bool rounding_on;              /* D2 bit 10                           */
    int decimal_point;             /* D2 bits 9-5                         */
    int field_length;              /* D2 bits 4-0: L, nibbles or bytes    */
    bool is_ascii;                 /* field length counts BYTES, not nibbles */
    uint16_t words[BCD_MAX_WORDS]; /* the field's memory words            */
    bool error;                    /* take the error return (no skip)     */
    uint8_t error_code;            /* BCD_ERR_* (PACK/UPACK only)         */
} BcdOperand;

/* ================================================================ */
/* Field geometry                                                   */
/* ================================================================ */

/*
 * Byte-aligned nibble extent M of a PACKED-BCD field: M = roundUpToEven(L).
 * Meaningful for packed BCD only - ASCII field lengths already count whole
 * bytes, so the ASCII paths index bytes directly and never call this.
 */
static int bcd_byte_aligned_nibbles(const BcdOperand *op)
{
    return op->field_length + (op->field_length & 1);
}

/*
 * Number of memory words the field spans (and therefore how many words are
 * read on load and written on store).
 *
 * NOTE (faithful to the reference implementation): the `lr` start-in-right-byte
 * skew is deliberately NOT added to this count, so a field that `lr` pushes
 * across one more word boundary has that last word neither loaded nor stored.
 * The words[] array is oversized so this can never corrupt memory outside the
 * struct.  Kept identical to RetroCore on purpose - do not "fix" one side only.
 */
static int bcd_mem_words(const BcdOperand *op)
{
    int len;

    if (op->field_length == 0)
    {
        return 0;
    }

    if (!op->is_ascii)
    {
        int m = bcd_byte_aligned_nibbles(op);
        len = m >> 2; /* 4 nibbles per 16-bit word */
        if ((len << 2) != m)
        {
            len++; /* round up a partial word   */
        }
    }
    else
    {
        len = op->field_length >> 1; /* 2 bytes per 16-bit word   */
        if ((len << 1) != op->field_length)
        {
            len++; /* round up an odd byte count */
        }
    }

    if (len == 0)
    {
        len = 1;
    }
    if (len > BCD_MAX_WORDS)
    {
        len = BCD_MAX_WORDS;
    }
    return len;
}

/* ================================================================ */
/* Nibble / byte accessors (logical field index -> memory word bits) */
/* ================================================================ */

/*
 * Logical field NIBBLE i.  Nibble 0 is bits 15-12 of the field's first word
 * (the leftmost / most significant nibble).  ND is big endian, so nibble index
 * counts DOWN the word from the top; `lr` shifts the whole field right by one
 * byte (= two nibbles).
 */
static int bcd_get_nibble(const BcdOperand *op, int i)
{
    int p = i + (op->starts_in_right_byte ? 2 : 0);
    return (op->words[p >> 2] >> ((3 - (p & 3)) * 4)) & 0x0F;
}

static void bcd_set_nibble(BcdOperand *op, int i, int val)
{
    int p = i + (op->starts_in_right_byte ? 2 : 0);
    int idx = p >> 2;
    int sh = (3 - (p & 3)) * 4;

    op->words[idx] = (uint16_t)((op->words[idx] & ~(0x0F << sh)) | ((val & 0x0F) << sh));
}

/*
 * Logical field BYTE i (ASCII operands).  Byte 0 is the leftmost / most
 * significant byte = the HIGH 8 bits of the word (ND is big endian).
 */
static uint8_t bcd_get_byte(const BcdOperand *op, int i)
{
    int p = i + (op->starts_in_right_byte ? 1 : 0);
    uint16_t w = op->words[p >> 1];

    return (uint8_t)(((p & 1) == 0) ? (w >> 8) : (w & 0x00FF));
}

static void bcd_set_byte(BcdOperand *op, int i, uint8_t val)
{
    int p = i + (op->starts_in_right_byte ? 1 : 0);
    int idx = p >> 1;
    uint16_t w = op->words[idx];

    if ((p & 1) == 0)
    {
        w = (uint16_t)((w & 0x00FF) | ((uint16_t)val << 8));
    }
    else
    {
        w = (uint16_t)((w & 0xFF00) | val);
    }

    op->words[idx] = w;
}

/* ================================================================ */
/* Load / store                                                     */
/* ================================================================ */

/*
 * Decode a descriptor pair and read the operand's field words from memory.
 * Returns false when the descriptor itself is illegal (decimal point outside
 * the field), in which case the caller must take the error return.
 *
 * An EMPTY field (L = 0) is perfectly legal - nothing is read, and the operand
 * behaves as a positive zero with ZERO significant-digit capacity.
 */
static bool bcd_get_operand(BcdOperand *op, uint16_t d1, uint16_t d2, bool is_ascii)
{
    int mem_len;
    int i;

    memset(op, 0, sizeof(*op));

    op->addr = d1;
    op->d2 = d2;
    op->starts_in_right_byte = ((d2 >> 15) & 1) != 0;
    op->ascii_format = (d2 >> 11) & 0x07;
    op->rounding_on = ((d2 >> 10) & 1) != 0;
    op->decimal_point = (d2 >> 5) & 0x1F;
    op->field_length = d2 & 0x1F;
    op->is_ascii = is_ascii;

    /* "The number has to be less than the field length. (It is not legal to
     * specify a point outside the field.)" - manual 3.4.1.2.  A point of 0 is
     * always legal (it means "to the right of the least significant digit"). */
    if ((op->decimal_point >= op->field_length) && (op->decimal_point != 0))
    {
        op->error = true;
        return false;
    }

    if (op->field_length == 0)
    {
        return true; /* empty operand => nothing to read, = 0 */
    }

    /* LSD-FIRST (descending) read, matching the RASK microcode: decimal
     * operands are right adjusted, so the commercial-instruction microcode
     * walks them from the LAST word (highest address = the LSD/sign end) toward
     * the first.  words[i] still gets the word at D1+i by INDEX, so this is a
     * pure memory-ACCESS-ORDER choice with zero functional effect - it only
     * keeps our access trace an ordered subsequence of the oracle's. */
    mem_len = bcd_mem_words(op);
    for (i = mem_len - 1; i >= 0; i--)
    {
        op->words[i] = cpu_memory_read((uint16_t)((op->addr + i) & 0xFFFF), true);
    }

    return true;
}

/*
 * Write the operand's field words back to memory.  The words were loaded first
 * and every update went through bcd_set_nibble/bcd_set_byte, so this is a
 * read-modify-write: bits of a boundary word outside the field survive.
 */
static void bcd_store_operand(BcdOperand *op)
{
    int mem_len;
    int i;

    if (op->field_length == 0)
    {
        return; /* empty field: nothing to store */
    }

    mem_len = bcd_mem_words(op);
    for (i = 0; i < mem_len; i++)
    {
        cpu_memory_write(op->words[i], (uint16_t)((op->addr + i) & 0xFFFF), true, 2);
    }
}

/* ================================================================ */
/* Sign / magnitude helpers                                         */
/* ================================================================ */

/*
 * Decode the operand's sign nibble.  *sign is +1 / -1, *is_unsigned is set for
 * the 0xF (17 oct) unsigned code.
 *
 * LENIENT ON PURPOSE: the RASK microcode does NOT reject a malformed sign or
 * digit nibble for ADDD/SUBD - the add-6 hardware simply processes the raw
 * nibble value and normalises via carry, then takes the normal SKIP return.
 * Probed on the live oracle: op1 = 0x0A2C (nibble 0xA) + 0x005C => 0x107C
 * (i.e. 102 + 5 = 107), P+2.  So anything that is not B/D/F decodes as '+'.
 *
 * BYTE ALIGNED: the sign is the LAST nibble of the byte-aligned field, index
 * M-1, NOT L-1.  For even L these coincide.
 */
static void bcd_decode_sign(const BcdOperand *op, int *sign, bool *is_unsigned)
{
    int sign_nib;

    *sign = +1;
    *is_unsigned = false;

    if (op->field_length == 0)
    {
        return; /* empty operand => +0 (FIROC, CS 010535) */
    }

    sign_nib = bcd_get_nibble(op, bcd_byte_aligned_nibbles(op) - 1);
    switch (sign_nib)
    {
    case 0x0B:
    case 0x0D:
        *sign = -1; /* B / D = minus            */
        break;
    case 0x0F:
        *is_unsigned = true; /* F     = unsigned (plus)  */
        break;
    default:
        break; /* A / C / E and anything else = plus */
    }
}

/*
 * Extract the operand's MAGNITUDE digits into dst, MSD-first and RIGHT
 * ADJUSTED: the least significant digit lands in dst[dst_len-1] and the leading
 * positions are zero.  Right adjusting means equal place values share the same
 * index across operands of DIFFERENT length (the shorter operand is simply
 * zero extended on the MSD side), so add/subtract can walk from the end with a
 * single carry/borrow.
 *
 * BYTE ALIGNED: the magnitude occupies nibbles 0..M-2 (M-1 nibbles).  For odd L
 * the leading nibble is a zero PAD, so reading M-1 nibbles yields exactly the
 * same numeric value as reading L-1.
 */
static void bcd_extract_magnitude(const BcdOperand *op, uint8_t *dst, int dst_len)
{
    int digits = (op->field_length == 0) ? 0 : bcd_byte_aligned_nibbles(op) - 1;
    int base;
    int i;

    memset(dst, 0, (size_t)dst_len);
    if (digits > dst_len)
    {
        digits = dst_len;
    }

    base = dst_len - digits; /* right adjust */
    for (i = 0; i < digits; i++)
    {
        dst[base + i] = (uint8_t)bcd_get_nibble(op, i);
    }
}

/*
 * a += b (both MSD-first, `len` digits each, values 0-9).  Returns the carry
 * OUT of the most significant position.  Schoolbook equivalent of SADD's
 * per-nibble add-6 decimal adjust with the carry chained LSW -> MSW.
 */
static int bcd_add_magnitude(uint8_t *a, const uint8_t *b, int len)
{
    int carry = 0;
    int i;

    for (i = len - 1; i >= 0; i--)
    {
        int s = a[i] + b[i] + carry;

        if (s >= 10)
        {
            s -= 10;
            carry = 1;
        }
        else
        {
            carry = 0;
        }
        a[i] = (uint8_t)s;
    }
    return carry;
}

/*
 * a -= b (both MSD-first, `len` digits).  REQUIRES |a| >= |b| (no final borrow)
 * - the caller guarantees that via bcd_cmp_magnitude.  Schoolbook equivalent of
 * SSUB's borrow + subtract-6 correction.
 */
static void bcd_sub_magnitude(uint8_t *a, const uint8_t *b, int len)
{
    int borrow = 0;
    int i;

    for (i = len - 1; i >= 0; i--)
    {
        int s = a[i] - b[i] - borrow;

        if (s < 0)
        {
            s += 10;
            borrow = 1;
        }
        else
        {
            borrow = 0;
        }
        a[i] = (uint8_t)s;
    }
}

/* Magnitude compare (MSD-first, equal length): -1 if a<b, 0 if a==b, +1 if a>b. */
static int bcd_cmp_magnitude(const uint8_t *a, const uint8_t *b, int len)
{
    int i;

    for (i = 0; i < len; i++)
    {
        if (a[i] != b[i])
        {
            return (a[i] < b[i]) ? -1 : 1;
        }
    }
    return 0;
}

/*
 * Pack a result magnitude (MSD-first, right adjusted, `len` digits) into this
 * operand's field and set the sign nibble.
 *
 * BYTE-ALIGNED LAYOUT, DESCRIPTOR CAPACITY.  The field spans M nibbles and the
 * sign goes to index M-1.  BUT the SIGNIFICANT digit capacity is the
 * descriptor's L-1, NOT M-1: for odd L the extra byte-align nibble is a FORCED
 * LEADING ZERO PAD, not extra precision, so a result with more than L-1 digits
 * still OVERFLOWS.  (Oracle locked: 999999999999999999 + 1 at L=19 overflows
 * the 18-digit field even though the byte-aligned field physically has 19
 * magnitude nibbles.)
 *
 * On overflow the LOW digits are STILL stored (truncated) and *overflow is set;
 * the caller then takes the error return.  STS.O is deliberately NOT touched -
 * that is oracle- and TPE-confirmed.
 *
 * EMPTY destination (L = 0): the capacity is ZERO digits.  A zero result fits
 * (no overflow, normal skip return); ANY nonzero digit overflows.  Nothing is
 * written to memory either way.  This is the TFIRE rule - an empty first
 * operand is NOT an error by itself.
 */
static void bcd_pack_magnitude_to_field(BcdOperand *op, const uint8_t *digits, int len,
                                        int out_sign_nibble, bool *overflow)
{
    int m;
    int cap;
    int di;
    int pos;

    *overflow = false;

    if (op->field_length == 0)
    {
        for (di = 0; di < len; di++)
        {
            if (digits[di] != 0)
            {
                *overflow = true;
                break;
            }
        }
        return;
    }

    m = bcd_byte_aligned_nibbles(op);
    cap = op->field_length - 1; /* significant capacity = descriptor L-1 */
    bcd_set_nibble(op, m - 1, out_sign_nibble);

    /* Fill the M-1 magnitude nibbles from the LSD (nibble M-2) upward: the low
     * `cap` nibbles take the result digits LSD..MSD, and any higher nibble (the
     * odd-L byte-align pad) is forced to zero. */
    di = len - 1; /* digits is MSD-first, so its LSD is last */
    for (pos = m - 2; pos >= 0; pos--)
    {
        int slot_from_lsd = (m - 2) - pos; /* 0 = the nibble just left of the sign */

        if (slot_from_lsd < cap)
        {
            bcd_set_nibble(op, pos, (di >= 0) ? digits[di--] : 0);
        }
        else
        {
            bcd_set_nibble(op, pos, 0); /* forced leading-zero pad */
        }
    }

    /* Any digit MORE significant than the capacity => decimal overflow. */
    while (di >= 0)
    {
        if (digits[di] != 0)
        {
            *overflow = true;
        }
        di--;
    }
}

/* ================================================================ */
/* ADDD / SUBD core                                                 */
/* ================================================================ */

/*
 * op1 (A/D, the destination) := op1 +/- op2 (X/T).
 * op2_sign_flip is +1 for ADDD and -1 for SUBD (SUBD negates op2's sign and
 * then shares the ADDE body - CS 010520).
 *
 * Returns true when the instruction takes the normal SKIP return, false for the
 * error return.  The ONLY error return is decimal OVERFLOW (OVFLO, CS 011054);
 * illegal digit/sign nibbles are NOT validated - see bcd_decode_sign.
 */
static bool bcd_add_sub(BcdOperand *op1, const BcdOperand *op2, int op2_sign_flip)
{
    uint8_t a[BCD_MAX_NIBBLES + 2]; /* result magnitude, MSD-first */
    uint8_t b[BCD_MAX_NIBBLES + 2];
    int s1;
    int s2;
    bool unsigned1;
    bool unsigned2;
    bool op1_unsigned;
    int eff_s2;
    int op1_dig;
    int op2_dig;
    int cap;
    int result_sign;
    bool is_zero;
    int out_sign;
    bool overflow;
    int i;

    /* AN EMPTY op1 IS NOT AN ERROR.  RASK "TEST FIRST OPERAND EMPTY" (TFIRE,
     * CS 010563) branches to FIROP only when the length is NON-zero; the
     * zero-length path reads nothing and CONTINUES into the ordinary sign/add
     * driver (TNOEQ), leaving through the SAME common exit as every other case.
     * The real ND TPE "INSTRUCTION" diagnostic issues ADDD/SUBD with an empty
     * op1 AND an empty op2, whose result is plainly zero and therefore FITS the
     * zero-digit destination - the hardware SKIPS there.  An empty op1 is thus
     * simply zero capacity, handled uniformly by bcd_pack_magnitude_to_field:
     * zero result => skip, nonzero result => overflow => error return. */

    bcd_decode_sign(op1, &s1, &unsigned1);
    bcd_decode_sign(op2, &s2, &unsigned2);
    (void)unsigned1;
    (void)unsigned2;

    /* op1's OUTPUT sign format: an unsigned op1 (D2 bit 13) forces the 0xF
     * (17 oct) sign nibble regardless of the arithmetic sign. */
    op1_unsigned = ((op1->d2 >> 13) & 1) != 0;

    eff_s2 = s2 * op2_sign_flip; /* SUBD negates op2, then this is a plain add */

    /* Buffer size: the larger of the two magnitudes plus ONE guard digit that
     * absorbs the add carry-out (so the overflow test can see it). */
    op1_dig = (op1->field_length == 0) ? 0 : bcd_byte_aligned_nibbles(op1) - 1;
    op2_dig = (op2->field_length == 0) ? 0 : bcd_byte_aligned_nibbles(op2) - 1;
    cap = ((op1_dig > op2_dig) ? op1_dig : op2_dig) + 1;

    bcd_extract_magnitude(op1, a, cap);
    bcd_extract_magnitude(op2, b, cap);

    if (s1 == eff_s2)
    {
        /* Like signs => add magnitudes, result keeps the common sign.  The
         * guard digit holds any carry-out, so nothing is lost here. */
        bcd_add_magnitude(a, b, cap);
        result_sign = s1;
    }
    else
    {
        /* Unlike signs => subtract the smaller magnitude from the larger; the
         * result takes the sign of the LARGER magnitude (SIGN, CS 010677). */
        if (bcd_cmp_magnitude(a, b, cap) >= 0)
        {
            bcd_sub_magnitude(a, b, cap);
            result_sign = s1;
        }
        else
        {
            bcd_sub_magnitude(b, a, cap);
            memcpy(a, b, (size_t)cap);
            result_sign = eff_s2;
        }
    }

    /* A ZERO result keeps op1's ORIGINAL sign (oracle locked: ADDD -5 + +5 =>
     * -0 = 0x000D; SUBD +5 - +5 => +0 = 0x000C).  s1 is op1's own sign and is
     * unaffected by the SUBD flip. */
    is_zero = true;
    for (i = 0; i < cap; i++)
    {
        if (a[i] != 0)
        {
            is_zero = false;
            break;
        }
    }
    if (is_zero)
    {
        result_sign = s1;
    }

    out_sign = op1_unsigned ? BCD_SIGN_UNS : ((result_sign < 0) ? BCD_SIGN_NEG : BCD_SIGN_POS);

    bcd_pack_magnitude_to_field(op1, a, cap, out_sign, &overflow);
    bcd_store_operand(op1);

    /* Overflow: the truncated LOW digits have been stored, STS.O stays CLEAR,
     * and we take the error return (P+1). */
    return !overflow;
}

/* ================================================================ */
/* ADDD - Add Decimal (140 120)                                     */
/* ================================================================ */
/*
 * "The second operand is added to the first operand and the sum is placed in
 * the first operand's location.  If necessary, high-order zeroes are supplied
 * for either operand.  When the first operand field is too short to contain all
 * significant digits of the sum, a decimal overflow occurs."
 */
void opcode_addd_add_two_decimal_operands(uint16_t instr)
{
    BcdOperand op1;
    BcdOperand op2;

    (void)instr;

    if (!bcd_get_operand(&op1, gA, gD, false))
    {
        return;
    }
    if (!bcd_get_operand(&op2, gX, gT, false))
    {
        return;
    }

    if (!bcd_add_sub(&op1, &op2, +1))
    {
        return; /* overflow => error return (P+1) */
    }

    gPC++; /* no error => SKIP return (P+2) */
}

/* ================================================================ */
/* SUBD - Subtract Decimal (140 121)                                */
/* ================================================================ */
/*
 * "The second operand is subtracted from the first operand and the difference
 * is placed in the first operand's location."  SUBD flips op2's sign and then
 * runs the exact same ADDE body as ADDD.
 */
void opcode_subd_subtract_two_decimal_operands(uint16_t instr)
{
    BcdOperand op1;
    BcdOperand op2;

    (void)instr;

    if (!bcd_get_operand(&op1, gA, gD, false))
    {
        return;
    }
    if (!bcd_get_operand(&op2, gX, gT, false))
    {
        return;
    }

    if (!bcd_add_sub(&op1, &op2, -1))
    {
        return; /* overflow => error return (P+1) */
    }

    gPC++; /* no error => SKIP return (P+2) */
}

/* ================================================================ */
/* COMD - Compare Decimal (140 122)                                 */
/* ================================================================ */

/*
 * Exact signed compare of two decimal operands, aligned on their decimal
 * points.  Returns -1 / 0 / +1 for op1 < / == / > op2.
 *
 * Alignment: decimal_point counts digits to the RIGHT of the point, so scaling
 * both magnitudes up to the LARGER decimal point puts equal place values in the
 * same column.  (The old double-based compare did this by dividing by 10^dp,
 * which silently lost precision past ~15 digits - the very bug this file was
 * rewritten to remove.)
 */
static int bcd_compare(const BcdOperand *op1, const BcdOperand *op2)
{
    uint8_t a[2 * (BCD_MAX_NIBBLES + 2)];
    uint8_t b[2 * (BCD_MAX_NIBBLES + 2)];
    int s1;
    int s2;
    bool u1;
    bool u2;
    int max_dp;
    int width;
    int cmp;
    bool a_zero = true;
    bool b_zero = true;
    int i;

    bcd_decode_sign(op1, &s1, &u1);
    bcd_decode_sign(op2, &s2, &u2);
    (void)u1;
    (void)u2;

    max_dp = (op1->decimal_point > op2->decimal_point) ? op1->decimal_point : op2->decimal_point;
    width = (int)sizeof(a);

    /* Extract right adjusted, then shift each magnitude LEFT by
     * (max_dp - own dp) digits so both share the same fractional scale. */
    bcd_extract_magnitude(op1, a, width - (max_dp - op1->decimal_point));
    memset(a + width - (max_dp - op1->decimal_point), 0, (size_t)(max_dp - op1->decimal_point));
    bcd_extract_magnitude(op2, b, width - (max_dp - op2->decimal_point));
    memset(b + width - (max_dp - op2->decimal_point), 0, (size_t)(max_dp - op2->decimal_point));

    for (i = 0; i < width; i++)
    {
        if (a[i] != 0)
        {
            a_zero = false;
        }
        if (b[i] != 0)
        {
            b_zero = false;
        }
    }

    /* Signed compare.  A zero magnitude is neither positive nor negative, so
     * -0 must compare EQUAL to +0. */
    if (a_zero)
    {
        s1 = 0;
    }
    if (b_zero)
    {
        s2 = 0;
    }

    if (s1 != s2)
    {
        return (s1 < s2) ? -1 : 1;
    }

    cmp = bcd_cmp_magnitude(a, b, width);
    if (s1 < 0)
    {
        cmp = -cmp; /* both negative: the larger magnitude is smaller */
    }
    return cmp;
}

/*
 * "The first operand is compared with the second operand.  The result is placed
 * in the A register.  If the operands are unequal in length, the shorter is
 * extended with zeroes.  None of the operands are changed."
 *
 * A = 1 (op1 > op2), 0 (equal), -1 / 0xFFFF (op1 < op2).
 */
void opcode_comd_compare_two_decimal_operands(uint16_t instr)
{
    BcdOperand op1;
    BcdOperand op2;
    int cmp;

    (void)instr;

    /* The microcode reads the SECOND operand (X,T) BEFORE the first (A,D) -
     * oracle-confirmed memory-access order.  Keep the load order matching so
     * the access trace stays an ordered subsequence of the oracle's (and so
     * "first error wins" picks the same operand). */
    if (!bcd_get_operand(&op2, gX, gT, false))
    {
        return;
    }
    if (!bcd_get_operand(&op1, gA, gD, false))
    {
        return;
    }

    cmp = bcd_compare(&op1, &op2);
    if (cmp > 0)
    {
        gA = 1;
    }
    else if (cmp < 0)
    {
        gA = 0xFFFF;
    }
    else
    {
        gA = 0;
    }

    gPC++; /* no error => SKIP return */
}

/* ================================================================ */
/* SHDE - Decimal Shift (140 126)                                   */
/* ================================================================ */
/*
 * "Operand one is moved to the operand two field with its digits offset
 * (shifted) to the left or right.  The shift count is computed as the
 * difference in decimal position of the two operands."
 *
 * Mirrors the RASK SHDE routine (CS 011121-011360, LEFT SHIFT at 011174, RIGHT
 * SHIFT at 011360, rounding at SADD/RWRIT, overflow test at FINSH 011267 /
 * NOVFL 011346).  All four ambiguities were resolved by probing the LIVE ND-110
 * oracle; the observed values are quoted per rule:
 *
 *  (1) DIRECTION + count: count = op1.decimal_point - op2.decimal_point.
 *      POSITIVE => shift op1 RIGHT by that many digits; NEGATIVE => LEFT by
 *      |count|.  (Oracle: op1.dp=1, op2.dp=0, +123 -> op2 +012 [right by 1];
 *      op1.dp=0, op2.dp=1, +123 -> op2 +230 [left by 1].)
 *  (2) ROUNDING (op2 D2 bit 10): on a RIGHT shift, ROUND ONCE half-up keyed on
 *      the MOST-SIGNIFICANT DISCARDED digit (>= 5 adds one).  NOT per digit.
 *      (Oracle: 125>>1 rnd -> 013; 124>>1 rnd -> 012; 149>>2 rnd -> 001 [the
 *      most significant dropped digit is 4 < 5 - per-digit would give 002];
 *      155>>2 rnd -> 002.)
 *  (3) LEFT-shift overflow / field truncation: if a NON-ZERO digit falls off
 *      the MSD end of op2's field, take the ERROR return (P+1) but STILL store
 *      the surviving low digits; STS.O stays CLEAR.  (Oracle: +123 left 1 ->
 *      op2 +230, P+1, STS.O=0;  +023 left 1 -> op2 +230, P+2 [only a zero was
 *      lost].)
 *  (4) decimal_point is ONLY the shift-count source - there is no separate
 *      field realignment; the shifted digits are stored right adjusted in op2's
 *      field.  SIGN: op1's sign propagates to op2 (0xC = +, 0xD = -); an
 *      unsigned op1 (0xF) becomes + UNLESS op2's D2 bit 13 is set, when op2 gets
 *      the unsigned 0xF nibble.  (Oracle: -123 -> 0x123D; op2 bit13 +123 ->
 *      0x123F; op1 0xF +123 -> op2 +123 = 0x123C.)
 *
 * Like ADDD/SUBD, input digit/sign nibbles are NOT validated (the oracle was
 * never probed for SHDE illegal-code reporting).
 */
static bool bcd_shde(BcdOperand *dst, const BcdOperand *src)
{
    uint8_t digits[BCD_MAX_NIBBLES + 2];   /* op1's significant digits, MSD-first */
    uint8_t work[2 * BCD_MAX_NIBBLES + 4]; /* shifted magnitude, MSD-first */
    int wlen = 0;
    int src_pad;
    int src_digits;
    int dst_cap;
    int count;
    int mdst;
    int drop;
    int pos;
    int i;
    bool overflow = false;
    int src_sign;
    bool src_unsigned;
    int out_sign;

    /* An EMPTY op2 has nowhere to store the shifted result: error return
     * (P+1), op2 untouched. */
    if (dst->field_length == 0)
    {
        return false;
    }

    bcd_decode_sign(src, &src_sign, &src_unsigned);

    /* BYTE-ALIGNED LAYOUT, DESCRIPTOR CAPACITY (see bcd_pack_magnitude_to_field):
     * op1 has L-1 SIGNIFICANT magnitude nibbles sitting above an odd-L leading
     * zero pad, op2 has L-1 significant digit slots. */
    src_pad = bcd_byte_aligned_nibbles(src) - src->field_length; /* 0 or 1 */
    src_digits = (src->field_length == 0) ? 0 : src->field_length - 1;
    dst_cap = dst->field_length - 1;

    if (src_digits > (int)sizeof(digits))
    {
        src_digits = (int)sizeof(digits);
    }
    for (i = 0; i < src_digits; i++)
    {
        digits[i] = (uint8_t)bcd_get_nibble(src, src_pad + i);
    }

    count = src->decimal_point - dst->decimal_point; /* rule (1) */

    if (count == 0)
    {
        /* No shift - copy op1's digits across (re-signed and right adjusted
         * into op2's field below). */
        for (i = 0; i < src_digits; i++)
        {
            work[wlen++] = digits[i];
        }
    }
    else if (count > 0)
    {
        /* RIGHT shift: drop the `count` least significant digits. */
        int keep = src_digits - count;

        if (keep < 0)
        {
            keep = 0;
        }
        for (i = 0; i < keep; i++)
        {
            work[wlen++] = digits[i];
        }

        /* rule (2): round ONCE, half up, on the most significant DISCARDED digit */
        if (dst->rounding_on && (count <= src_digits) && (digits[keep] >= 5))
        {
            int carry = 1;

            for (i = wlen - 1; (i >= 0) && (carry != 0); i--)
            {
                int s = work[i] + carry;

                work[i] = (uint8_t)(s % 10);
                carry = s / 10;
            }
            if (carry != 0)
            {
                /* Rounding grew the magnitude by a digit (999 -> 1000). */
                for (i = wlen; i > 0; i--)
                {
                    work[i] = work[i - 1];
                }
                work[0] = (uint8_t)carry;
                wlen++;
            }
        }
    }
    else
    {
        /* LEFT shift by -count: multiply by 10^k = append k zero digits. */
        int k = -count;

        for (i = 0; i < src_digits; i++)
        {
            work[wlen++] = digits[i];
        }
        for (i = 0; (i < k) && (wlen < (int)sizeof(work)); i++)
        {
            work[wlen++] = 0;
        }
    }

    /* Right adjust `work` into op2's byte-aligned field.  rule (3): a NON-ZERO
     * digit pushed off the MSD end of the significant window is a significant
     * loss => error return, but the surviving low digits are still stored.
     * `drop` may be negative when the source is shorter than the field. */
    mdst = bcd_byte_aligned_nibbles(dst);
    drop = wlen - dst_cap;
    for (i = 0; i < drop; i++)
    {
        if (work[i] != 0)
        {
            overflow = true;
        }
    }

    for (pos = mdst - 2; pos >= 0; pos--)
    {
        int slot_from_lsd = (mdst - 2) - pos; /* 0 = the slot left of the sign */
        int d = 0;

        if (slot_from_lsd < dst_cap)
        {
            int wi = wlen - 1 - slot_from_lsd; /* align work's LSD to nibble mdst-2 */

            if ((wi >= 0) && (wi < wlen))
            {
                d = work[wi];
            }
        }
        bcd_set_nibble(dst, pos, d); /* slots >= dst_cap: forced 0 pad */
    }

    /* rule (4): output sign nibble. */
    out_sign = (((dst->d2 >> 13) & 1) != 0) ? BCD_SIGN_UNS
                                            : ((src_sign < 0) ? BCD_SIGN_NEG : BCD_SIGN_POS);
    bcd_set_nibble(dst, mdst - 1, out_sign);

    bcd_store_operand(dst);

    /* Significant digit loss => error return (P+1).  STS.O stays CLEAR. */
    return !overflow;
}

void opcode_shde_decimal_shift(uint16_t instr)
{
    BcdOperand op1;
    BcdOperand op2;

    (void)instr;

    /* The microcode reads the SECOND operand (X,T = op2/destination) BEFORE the
     * first (A,D = op1): STNOE at CS 011126 ("X.T-OPERAND NOT EMPTY, READ FIRST
     * AND LAST WORD"), then the source is read last (RSHFT CS 011362 / LSHFB
     * CS 011177 read op1 just before the write). */
    if (!bcd_get_operand(&op2, gX, gT, false))
    {
        return;
    }
    if (!bcd_get_operand(&op1, gA, gD, false))
    {
        return;
    }

    if (!bcd_shde(&op2, &op1))
    {
        return; /* significant digits lost => error return */
    }

    gPC++; /* no error => SKIP return */
}

/* ================================================================ */
/* PACK - Convert to packed decimal (140 124)                       */
/* ================================================================ */

/*
 * Report a PACK/UPACK error: the error code goes into D bits 0-4; for code 2
 * (illegal code) bit 15 of BOTH A and D is also set (they point at the offending
 * byte).  The exact low-bit byte pointer is UNCERTAIN - only bit 15 is pinned by
 * the manual, so only bit 15 is set here.
 */
static void bcd_report_error(BcdOperand *op, uint8_t code)
{
    op->error = true;
    op->error_code = code;

    gD = (uint16_t)((gD & ~0x1F) | (code & 0x1F));
    if (code == BCD_ERR_ILLEGAL_CODE)
    {
        gA |= 0x8000;
        gD |= 0x8000;
    }
}

/* Which byte holds the sign, and the [first..last] digit-byte range, for an
 * ASCII operand of `len` bytes in the given sign format. */
static void bcd_ascii_layout(int format, int len, int *sign_byte, int *first, int *last)
{
    switch (format)
    {
    case BCD_ASCII_EMBEDDED_TRAILING:
        *sign_byte = len - 1;
        *first = 0;
        *last = len - 1;
        break;
    case BCD_ASCII_SEPARATE_TRAILING:
        *sign_byte = len - 1;
        *first = 0;
        *last = len - 2;
        break;
    case BCD_ASCII_EMBEDDED_LEADING:
        *sign_byte = 0;
        *first = 0;
        *last = len - 1;
        break;
    case BCD_ASCII_SEPARATE_LEADING:
        *sign_byte = 0;
        *first = 1;
        *last = len - 1;
        break;
    default: /* unsigned */
        *sign_byte = -1;
        *first = 0;
        *last = len - 1;
        break;
    }
}

/*
 * "The format of the first operand is changed from ASCII Coded Decimal Number
 * (unpacked) to Packed Decimal Number (packed), and the result put in the
 * second operand location."
 */
static bool bcd_convert_to_packed(BcdOperand *dst, const BcdOperand *src)
{
    uint8_t digits[BCD_MAX_NIBBLES]; /* MSD..LSD */
    int n_digits = 0;
    int sign = +1;
    int sign_byte;
    int first;
    int last;
    int out_sign;
    int m;
    int cap;
    int di;
    int pos;
    int i;
    bool overflow = false;

    bcd_ascii_layout(src->ascii_format, src->field_length, &sign_byte, &first, &last);

    /* SEPARATE sign formats: the ASCII sign byte lives at sign_byte, which is
     * OUTSIDE the [first..last] digit range (separate trailing => digits
     * [0..L-2], sign at L-1; separate leading => digits [1..L-1], sign at 0).
     * The digit loop below therefore NEVER visits it, so it must be decoded
     * HERE.  (An in-loop `i == sign_byte` branch is dead code for the separate
     * formats and silently packs a '-' operand as POSITIVE - oracle caught.)
     * '+' = 0x2B, '-' = 0x2D; anything else is an illegal code. */
    if ((src->ascii_format == BCD_ASCII_SEPARATE_TRAILING) ||
        (src->ascii_format == BCD_ASCII_SEPARATE_LEADING))
    {
        uint8_t sb = bcd_get_byte(src, sign_byte);

        if (sb == 0x2B)
        {
            sign = +1;
        }
        else if (sb == 0x2D)
        {
            sign = -1;
        }
        else
        {
            bcd_report_error(dst, BCD_ERR_ILLEGAL_CODE);
            return false;
        }
    }

    for (i = first; (i <= last) && (n_digits < (int)sizeof(digits)); i++)
    {
        uint8_t b = bcd_get_byte(src, i);
        bool embedded_sign =
            (i == sign_byte) && ((src->ascii_format == BCD_ASCII_EMBEDDED_TRAILING) ||
                                 (src->ascii_format == BCD_ASCII_EMBEDDED_LEADING));
        int digit;

        if (embedded_sign)
        {
            /* Manual Table 5 overpunch, or a plain ASCII digit (= positive). */
            if (b == 0x7B)
            {
                digit = 0;
                sign = +1;
            }
            else if (b == 0x7D)
            {
                digit = 0;
                sign = -1;
            }
            else if ((b >= 0x41) && (b <= 0x49))
            {
                digit = b - 0x40;
                sign = +1;
            }
            else if ((b >= 0x4A) && (b <= 0x52))
            {
                digit = b - 0x49;
                sign = -1;
            }
            else if ((b >= 0x30) && (b <= 0x39))
            {
                digit = b & 0x0F;
                sign = +1;
            }
            else
            {
                bcd_report_error(dst, BCD_ERR_ILLEGAL_CODE);
                return false;
            }
        }
        else
        {
            if ((b < 0x30) || (b > 0x39))
            {
                bcd_report_error(dst, BCD_ERR_ILLEGAL_CODE);
                return false;
            }
            digit = b & 0x0F;
        }
        digits[n_digits++] = (uint8_t)digit;
    }

    /* Output sign nibble: unsigned destination (D2 bit 13) => 0xF. */
    out_sign =
        (((dst->d2 >> 13) & 1) != 0) ? BCD_SIGN_UNS : ((sign < 0) ? BCD_SIGN_NEG : BCD_SIGN_POS);

    if (dst->field_length == 0)
    {
        /* Zero-capacity destination: only a zero value fits. */
        for (i = 0; i < n_digits; i++)
        {
            if (digits[i] != 0)
            {
                overflow = true;
            }
        }
        if (overflow)
        {
            bcd_report_error(dst, BCD_ERR_OVERFLOW);
            return false;
        }
        return true;
    }

    /* Same byte-aligned layout / descriptor-capacity rule as
     * bcd_pack_magnitude_to_field. */
    m = bcd_byte_aligned_nibbles(dst);
    cap = dst->field_length - 1;
    bcd_set_nibble(dst, m - 1, out_sign);

    di = n_digits - 1;
    for (pos = m - 2; pos >= 0; pos--)
    {
        int slot_from_lsd = (m - 2) - pos;

        if (slot_from_lsd < cap)
        {
            bcd_set_nibble(dst, pos, (di >= 0) ? digits[di--] : 0);
        }
        else
        {
            bcd_set_nibble(dst, pos, 0); /* forced leading-zero pad */
        }
    }
    while (di >= 0)
    {
        if (digits[di] != 0)
        {
            overflow = true;
        }
        di--;
    }

    bcd_store_operand(dst); /* field written even on overflow */

    if (overflow)
    {
        bcd_report_error(dst, BCD_ERR_OVERFLOW);
        return false;
    }
    return true;
}

void opcode_pack_convert_to_decimal(uint16_t instr)
{
    BcdOperand op1; /* A/D: ASCII source      */
    BcdOperand op2; /* X/T: packed BCD dest   */

    (void)instr;

    /* MEMORY-ACCESS ORDER (oracle locked, RASK microcode CS 011533 PACK): the
     * microcode reads the X.T operand (op2 = BCD DESTINATION) FIRST - see the
     * block header at CS 011542 "READ X.T-OPERAND FIRST AND LAST WORD IF NOT
     * EMPTY" - and only THEN the A.D operand (op1 = ASCII SOURCE) at CS 011570
     * "TEST A,D-OPERAND EMPTY, AND READ IT".  The packed result is written back
     * to op2 last. */
    if (!bcd_get_operand(&op2, gX, gT, false))
    {
        return;
    }
    if (!bcd_get_operand(&op1, gA, gD, true))
    {
        return;
    }

    if (!bcd_convert_to_packed(&op2, &op1))
    {
        return; /* illegal code / overflow => error return */
    }

    gPC++; /* no error => SKIP return */
}

/* ================================================================ */
/* UPACK - Convert to unpacked decimal (140 125)                    */
/* ================================================================ */

/*
 * "The format of the first operand is changed from Packed Decimal Number
 * (packed) to ASCII Coded Decimal (unpacked), and the result is placed in the
 * second operand's location."
 *
 * Textbook right-adjusted ASCII with ASCII '0' fill.  Unlike ADDD/SUBD/SHDE,
 * UPACK DOES validate its source: an illegal sign or digit nibble reports error
 * code 2.
 */
/* The unpacked (ASCII) byte for one digit at byte position pos: the plain
 * digit 0x30|digit, or, at the sign byte of an embedded-sign format, the
 * Manual Table 5 overpunch of sign and digit. */
static uint8_t unpacked_digit_byte(const BcdOperand *dst, int pos, int sign_byte, bool unsigned_req,
                                   int sign, int digit)
{
    uint8_t b;

    if ((pos == sign_byte) && !unsigned_req &&
        ((dst->ascii_format == BCD_ASCII_EMBEDDED_TRAILING) ||
         (dst->ascii_format == BCD_ASCII_EMBEDDED_LEADING)))
    {
        /* Manual Table 5 overpunch: sign and digit share one byte. */
        if (sign < 0)
        {
            b = (digit == 0) ? (uint8_t)0x7D : (uint8_t)(0x49 + digit);
        }
        else
        {
            b = (digit == 0) ? (uint8_t)0x7B : (uint8_t)(0x40 + digit);
        }
    }
    else
    {
        b = (uint8_t)(0x30 | digit);
    }
    return b;
}

static bool bcd_convert_to_unpacked(BcdOperand *dst, const BcdOperand *src)
{
    uint8_t digits[BCD_MAX_NIBBLES]; /* MSD..LSD */
    int n_digits = 0;
    int m_src;
    int pad;
    int sign_nib;
    int sign;
    bool unsigned_src = false;
    bool unsigned_req;
    int sign_byte;
    int first;
    int last;
    int di;
    int pos;
    int i;
    bool overflow;

    /* BYTE-ALIGNED source read: the sign nibble is at index M-1 and the L-1
     * SIGNIFICANT magnitude digits are the LOW L-1 nibbles (indices pad..M-2),
     * where pad = M - L is the odd-L leading zero pad that is skipped. */
    m_src = bcd_byte_aligned_nibbles(src);
    pad = m_src - src->field_length;
    if (m_src == 0)
    {
        /* Empty source: nothing to convert.  Treated as +0 with no digits. */
        sign = +1;
    }
    else
    {
        sign_nib = bcd_get_nibble(src, m_src - 1);
        switch (sign_nib)
        {
        case 0x0A:
        case 0x0C:
        case 0x0E:
            sign = +1;
            break;
        case 0x0B:
        case 0x0D:
            sign = -1;
            break;
        case 0x0F:
            sign = +1;
            unsigned_src = true;
            break;
        default:
            bcd_report_error(dst, BCD_ERR_ILLEGAL_CODE);
            return false;
        }

        for (i = pad; (i < m_src - 1) && (n_digits < (int)sizeof(digits)); i++)
        {
            int nib = bcd_get_nibble(src, i);

            if (nib > 9)
            {
                bcd_report_error(dst, BCD_ERR_ILLEGAL_CODE);
                return false;
            }
            digits[n_digits++] = (uint8_t)nib;
        }
    }

    unsigned_req = unsigned_src || (((dst->d2 >> 13) & 1) != 0);

    bcd_ascii_layout(dst->ascii_format, dst->field_length, &sign_byte, &first, &last);
    overflow = (n_digits > (last - first + 1));

    /* Fill the digit bytes right adjusted (last = LSD), ASCII '0' filling the
     * high bytes. */
    di = n_digits - 1;
    for (pos = last; pos >= first; pos--)
    {
        int digit = (di >= 0) ? digits[di--] : 0;

        bcd_set_byte(dst, pos, unpacked_digit_byte(dst, pos, sign_byte, unsigned_req, sign, digit));
    }

    /* Separate sign byte: '+' = 0x2B, '-' = 0x2D; unsigned => plain '0'. */
    if ((sign_byte >= 0) && ((dst->ascii_format == BCD_ASCII_SEPARATE_TRAILING) ||
                             (dst->ascii_format == BCD_ASCII_SEPARATE_LEADING)))
    {
        bcd_set_byte(dst, sign_byte,
                     unsigned_req ? (uint8_t)0x30 : (uint8_t)((sign < 0) ? 0x2D : 0x2B));
    }

    bcd_store_operand(dst); /* field written even on overflow */

    if (overflow)
    {
        bcd_report_error(dst, BCD_ERR_OVERFLOW);
        return false;
    }
    return true;
}

void opcode_unpack_convert_from_decimal(uint16_t instr)
{
    BcdOperand op1; /* A/D: packed BCD source */
    BcdOperand op2; /* X/T: ASCII destination */

    (void)instr;

    /* UPACK reads its source (A,D) first.  A bad descriptor is reported as an
     * illegal code in D bits 0-4. */
    if (!bcd_get_operand(&op1, gA, gD, false))
    {
        bcd_report_error(&op1, BCD_ERR_ILLEGAL_CODE);
        return;
    }
    if (!bcd_get_operand(&op2, gX, gT, true))
    {
        bcd_report_error(&op2, BCD_ERR_ILLEGAL_CODE);
        return;
    }

    if (op2.field_length == 0)
    {
        return; /* no destination => error return */
    }

    if (!bcd_convert_to_unpacked(&op2, &op1))
    {
        return; /* illegal code / overflow => error return */
    }

    gPC++; /* no error => SKIP return */
}
