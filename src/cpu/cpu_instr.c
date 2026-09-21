/*
 * cpu_instr.c - ND-100 instruction implementations, privilege checks and illegal-instruction trap.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2006 Per-Olof Astrom
 * Copyright (c) 2006-2008 Roger Abrahamsson
 * Copyright (c) 2008 Zdravko
 * Copyright (c) 2025 Ronny Hansen
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


#include "cpu_types.h"
#include "cpu_protos.h"
#include "../ndlib/log.h"
#include "../devices/panel/panel.h" /* ProcessTerminalPanc/Lamp */

/* --ring-at-clpt=N: dump the CPU instruction ring at the N'th CLPT (0 = off). */
static long s_ring_at_clpt = 0;

/**
 * @brief Set the CLPT call at which to dump the instruction ring (--ring-at-clpt).
 * @param n CLPT call number counted from 1; 0 turns the dump off.
 */
void cpu_set_ring_at_clpt(int64_t n)
{
    s_ring_at_clpt = n > 0 ? n : 0;
}
#include <stdlib.h>
#include <string.h> /* strlen()/strcmp() - VERSN identity parsing, see opcode_versn_read_cpu_version() */
#include <stdio.h>
static void do_ident(uint16_t);
static void do_lrb(uint16_t);
static void do_srb(uint16_t);
static bool is_skip(uint16_t);
static uint32_t shift_double_reg(uint32_t, uint16_t);
static uint16_t shift_reg(uint16_t, uint16_t);


// Initialize the instruction function array
InstrFunc g_instr_funcs[65536];

/*********** TODO ***********/


/************************************ HELPER FUNCTIONS *************************************/

/// <summary>
/// Check if we are allowed to run a privileged instruction
///
/// Privileged intructions are only available to programs running in system mode (rings 2 and 3) or when memory protection is disabled;
/// </summary>
/// <returns>TRUE if allowed to execute</returns>
static bool check_priv(void)
{
    if (!STS_PAGING_ON_IS_SET)
    {
        return true; // memory protection disabled
    }

    // Check ring
    uint16_t pcr = g_reg->reg_PCR[CURR_LEVEL];
    uint16_t ring = pcr & 0x03;

    if ((ring == 2) || (ring == 3))
    {
        return true;
    }

    // Failed, not allowed to execute
    // Generate a privileged instruction interrupt
    cpu_interrupt(14, 1 << 6); // Privileged instruction
    return false;
}


int16_t cpu_sign_extend(uint16_t x)
{
    short res = (uint16_t)x;

    // If negative (bit 7==1), extend high 8 bits with 1's
    if ((x & 1 << 7) != 0)
    {
        res |= 0xFF00;
    }

    return res;
}


static uint16_t do_add(uint16_t a, uint16_t b, uint16_t k)
{
    int tmp;
    bool is_diff;
    tmp = ((int)a) + ((int)b) + ((int)k);
    /* C (carry) */
    if (tmp & 0xffff0000)
    {
        cpu_setbit(_STS, STS_CARRY, 1);
    }
    else
    {
        cpu_setbit(_STS, STS_CARRY, 0);
    }
    /* O(static overflow), Q (dynamic overflow) */
    is_diff = (((1 << 15) & a) ^ ((1 << 15) & b)); /* is bit 15 of the two operands different? */
    if (!(is_diff) && (((1 << 15) & a) ^ ((1 << 15) & tmp)))
    {                                             /* if equal and result is different... */
        cpu_setbit(_STS, STS_STATIC_OVERFLOW, 1); // Static overflow
        cpu_setbit(_STS, STS_DYNAMIC_OVERFLOW,
                   1); // Dynamic overflow (Instruction test shows Q must be set)
    }
    else
    {
        cpu_setbit(_STS, STS_DYNAMIC_OVERFLOW, 0);
        //setbit(_STS, STS_STATIC_OVERFLOW, 0); NO!
    }
    return (uint16_t)tmp;
}


// Calculate effective address for LDnTX
static unsigned int calc_el(uint8_t displacement)
{

    unsigned int el = (gX + displacement) & 0xFFFF;
    el = (gT & 0xFF) << 16 | el;
    el = el & 0xFFFFFF; // Cap at 24 bits

    return el;
}

// read el value from memory
static unsigned int read_el(unsigned el)
{
    return mms_read_physical_memory(el, true);
}

// write el to memory
static void write_el(uint32_t el, uint16_t value)
{
    mms_write_physical_memory(el, value, true);
}


/***************** HELPER INSTRUCTIONS *****************/

void cpu_illegal_instr(uint16_t operand)
{
    /*
     * --log=cpu:debug logs every illegal-instruction trap.  This is how a guest's
     * CPU-type probe is observed: TPE's INSTRUCTION program executes VERSN (140133)
     * and decides "ND-100" if - and only if - it traps here.
     */
    LOG(LOG_CAT_CPU, LOG_DEBUG, "ILLEGAL %06o at %06o", operand, gPC);

    cpu_interrupt(14, 1 << 4); /* Illegal Instruction <= WILL TRAP! */
}

static void unimplemented_instr(uint16_t operand)
{
    printf("\r\n");
    printf("--------------------------------\r\n");
    printf("CPU: Unimplemented instruction: %06o at PC: %06o\r\n", operand, gPC);
    printf("--------------------------------\r\n");
    printf("\r\n");

    //set_cpu_run_mode(CPU_STOPPED); /* OK unimplemented function, lets stop CPU and end program that way */
}


/************************************ INSTRUCTIONS *************************************/


/// <summary>
/// cjp - Conditional jump
/// Instruction bits 8-10 are used to specify one of 8 jump conditions.
///
/// If the specified condition becomes true, the displacement is added to the program counter and a jump relative to current location takes place.
/// The range is 128 locations backwards and 127 locations forwards. If the specified condition is false, no jump takes place.
///
/// Execution time depends on conditions, but is the same for all instructions.
///
/// A conditional jump instruction must be specified by means of the 8 mnemonics listed below.
/// It is illegal to specify cjp or any combinations of, B, | and , X.
/// </summary>
static void cjp(bool jmp_flag, uint16_t operand)
{
    if (jmp_flag)
    {
        uint16_t old_g_pc = gPC - 1;

        uint16_t temp = cpu_sign_extend(operand & 0xff);

        /* MICROCODE-VALIDATED 2026-07-20: the address arithmetic must NOT touch STS.
         * do_add() writes STS C (and O/Q) as a side effect, but the whole cjp family
         * (RASK CS 007300-007337, ND-110-RASK.LISTING.TXT:12249-12287) carries NO "STS,xx"
         * token - so no status bit is written by a conditional jump, taken or not. Each
         * entry is "A,<reg> ALUF,PASSA ALUD,NONE IDBS,LA COMM,CJMP,<cond> T,JMP T,HOLD":
         * ALUD,NONE means the ALU result is not even latched, and the target address is
         * formed by the COMM,CJMP command from the latched displacement (IDBS,LA), not by
         * a status-updating ALU pass. STS bits 0-7 are written only by the STS,EA / STS,ES
         * / STS,LO tokens, none of which appear here. Confirmed live against the RASK
         * oracle: C keeps whatever it was seeded with across a taken and a non-taken jump.
         * Plain wrapping 16-bit add - identical arithmetic to the old do_add() call, minus
         * the flag write-back.
         */
        gPC = (uint16_t)((uint16_t)(gPC - 1) + (uint16_t)temp);

        if (g_disasm)
        {
            disasm_userel(old_g_pc, gPC);
        }
    }
}


/* VERSN - ND110+
 * IN: uses A reg bit 11-8 as a bitfield to addess the byte of the version number read, the total is 16 bytes
 * so instruction has to be called 16 times, with incremented A each time.
 * OUT: Sets A, T, D
 */
/*
 * ---------------------------------------------------------------------------
 * VERSN identity - the BACK-WIRING PROM
 * ---------------------------------------------------------------------------
 *
 * WHAT THESE BYTES ACTUALLY ARE (verified):
 * They are NOT an opaque "installation number". They are the ND-110/ND-120
 * BACK-WIRING PROM, read by VERSN itself through the dedicated microcode IDB
 * source IDBS,INR (code 35 octal) which is wired to eight backplane pins; the
 * CPU board drives PIL[3:0] out to the B-plug and reads INR[7:0] back.
 *   - E:/Dev/Ronny/nd-120/Verilog/DECODE-GateArray/DGA/circuit/DECODE_DGA_IDBS.v:32
 *   - E:/Dev/Ronny/nd-120/Verilog/CPU-BOARD-3202/circuit/ND3202D.v:83,89
 * The byte address is the PIL, loaded from A bits 8-11 by COMM,LDPIL. A
 * microcode bug (LDPIL has not settled when the next microword samples
 * IDBS,INR) means the CURRENT PIL is used, which is why SINTRAN's GCPUNR runs
 * VERSN once on each of levels 0-7 to collect bytes 0-7:
 *   "DUE TO ERROR IN MICROPROGRAM THE VERSN INSTRUCTION HAS TO BE EXECUTED ON
 *    THE LEVEL CORRESPONDING TO THE BYT NO. TO BE READ"
 *   E:/Dev/Ronny/NDInsight/SINTRAN/NPL-SOURCE/NPL/PH-P2-OPPSTART.NPL:3534-3570
 * GCPUNR also sets A = level << 8 before each VERSN, so indexing from A bits
 * 8-11 (as below) returns exactly the byte the real hardware returns. No extra
 * device, memory region or IOX port is needed to model the PROM.
 *
 * DECODED LAYOUT (all verified against GCPUNR):
 *   byte 0 (MSB) + 1 (LSB)  INF0  SYSNO      -> banner "CPU NUMBER"; skipped if word == -1
 *   byte 2 (MSB) + 3 (LSB)  INF1  HWINFO(2)  -> banner "CPU TYPE";   skipped if word == -1
 *   byte 4                  INF2 high byte   NLEGU, legal users;      skipped if byte == 0377
 *   byte 5                  INF2 low byte    NEVER read by SINTRAN
 *   byte 6 (MSB) + 7 (LSB)  INF3  signature  MUST be 52652 octal = 21930 = 0x55AA,
 *                                            otherwise GCPUNR exits at once and SINTRAN
 *                                            keeps the values baked into the disk image
 *   bytes 8-15                               NEVER read by SINTRAN; meaning UNKNOWN, filler
 *
 * TWO DEFECTS FIXED HERE (2026-07-20):
 *   1. The array had only 15 entries while the A-register byte index is a full
 *      4-bit field (0..15), so index 15 read one byte PAST the array.
 *   2. The three values were file-scope statics with no way to configure them.
 *      They now live in one named module-state struct that cpu_versn_reset()
 *      re-initialises from the CPU type on every cpu_init(), and that
 *      cpu_versn_set_identity_from_env() can override - mirroring the RetroCore
 *      machine-config keys cpu_number / system_type / legal_users /
 *      installation_number / microcode_version / print_version.
 */

/* Byte indices inside the 16-byte back-wiring PROM image. */
#define VERSN_PROM_SIZE         16
#define VERSN_PROM_SYSNO_HI     0
#define VERSN_PROM_SYSNO_LO     1
#define VERSN_PROM_SYSTYPE_HI   2
#define VERSN_PROM_SYSTYPE_LO   3
#define VERSN_PROM_LEGAL_USERS  4
#define VERSN_PROM_UNUSED       5
#define VERSN_PROM_SIGNATURE_HI 6
#define VERSN_PROM_SIGNATURE_LO 7

/* INF3 signature GCPUNR demands: 52652 octal = 21930 decimal = 0x55AA. */
#define VERSN_PROM_SIGNATURE 0x55AA

/* NLEGU byte value meaning "GCPUNR must NOT set the number of legal users". */
#define VERSN_PROM_LEGAL_USERS_SKIP 0xFF

/* Lowest microprogram version SINTRAN's LOCOSTORE accepts: octal 013 = 11. */
#define VERSN_MIN_MICROCODE_VERSION 0x0B

/*
 * Identity reported by VERSN. ONE named module-state struct instead of three
 * loose statics, so a re-init genuinely starts from a known state.
 */
struct VersnIdentity
{
    unsigned char prom[VERSN_PROM_SIZE]; /* back-wiring PROM image, bytes 0-15 */
    int microcode_version;               /* T register; see VERSN_MIN_MICROCODE_VERSION */
    int print_version;                   /* A register, 12 bits (PCB artwork version) */
};

static struct VersnIdentity g_versn;

/*
 * Fill the PROM image with the default for the current CPU type.
 *
 * ND100 / ND100CE / ND100CX: the HISTORICAL filler bytes, byte for byte.
 * SINTRAN calls GCPUNR only on a 110/120 CPU ("CALLED ONLY IF 110/120 CPU",
 * OPPSTART.NPL:3538), so these machines are deliberately left untouched.
 * Index 15 is the placeholder that fixes the old out-of-range read; it is NOT
 * taken from any manual or EPROM dump.
 *
 * ND110 / ND110CE / ND110CX / ND110PCX: a VALID PROM image so GCPUNR succeeds
 * and the SINTRAN banner reflects the emulated machine.
 *
 * The concrete values below are CHOSEN DEFAULTS, not sourced PROM contents:
 *  - SYSNO = 102 is a real OBSERVED value (SINTRAN-STRUCTURES.md:2036-2039),
 *    not a dump of any physical PROM. SYSNO is FUNCTIONAL (COSMOS local vs
 *    remote routing, MP-P2-1.NPL:232), so give each node its own.
 *  - HWINFO(2) = 100 is one of the documented legal system-type codes
 *    (100,102,500,502,5561.. - OPPSTART.NPL:3440). CHOICE, not documentation.
 *  - NLEGU = 0377 octal = "do not touch SINTRAN's own licensed-user count".
 *  - byte 5 and bytes 8-15 are zero filler; SINTRAN never reads them and their
 *    real meaning is UNKNOWN.
 */
static void versn_load_default_prom(void)
{
    int i;

    for (i = 0; i < VERSN_PROM_SIZE; i++)
    {
        g_versn.prom[i] = 0x00;
    }

    if ((g_current_cpu_type == ND100) || (g_current_cpu_type == ND100CE) ||
        (g_current_cpu_type == ND100CX))
    {
        /* Historical filler - preserved byte for byte. 040171 = CPU? */
        static const unsigned char historical[VERSN_PROM_SIZE] = {
            0x01, 0x04, 0x00, 0x01, 0x07, 0x01, 0x01, 0x01,
            0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};

        for (i = 0; i < VERSN_PROM_SIZE; i++)
        {
            g_versn.prom[i] = historical[i];
        }
        return;
    }

    g_versn.prom[VERSN_PROM_SYSNO_HI] = (unsigned char)(102 >> 8); /* SYSNO = 102 */
    g_versn.prom[VERSN_PROM_SYSNO_LO] = (unsigned char)(102 & 0xFF);
    g_versn.prom[VERSN_PROM_SYSTYPE_HI] = (unsigned char)(100 >> 8); /* HWINFO(2) = 100 */
    g_versn.prom[VERSN_PROM_SYSTYPE_LO] = (unsigned char)(100 & 0xFF);
    g_versn.prom[VERSN_PROM_LEGAL_USERS] = VERSN_PROM_LEGAL_USERS_SKIP;
    g_versn.prom[VERSN_PROM_UNUSED] = 0x00;
    g_versn.prom[VERSN_PROM_SIGNATURE_HI] = (unsigned char)(VERSN_PROM_SIGNATURE >> 8);
    g_versn.prom[VERSN_PROM_SIGNATURE_LO] = (unsigned char)(VERSN_PROM_SIGNATURE & 0xFF);
}

/*
 * Re-initialise the whole VERSN identity from the current CPU type.
 * Called from cpu_init() BEFORE cpu_versn_set_identity_from_env().
 */
void cpu_versn_reset(void)
{
    versn_load_default_prom();

    /*
     * 0x0708 (octal 3410, printed by TPE as "3410B" - the trailing B is Norsk
     * Data octal notation, NOT a revision letter). Kept byte for byte; the
     * CORRECT ND-110 revision is UNKNOWN and is deliberately not invented.
     */
    g_versn.microcode_version = 0x0708;
    g_versn.print_version = 0x80C;
}

/*
 * Overlay a decoded PROM field and force the INF3 signature.
 *
 * A friendly field is only ever set because the caller wants GCPUNR to READ the
 * result, and GCPUNR exits immediately unless bytes 6-7 hold 52652 octal - so
 * writing a field always repairs the signature.
 */
static void versn_set_word(int hi_index, int lo_index, int value)
{
    g_versn.prom[hi_index] = (unsigned char)((value >> 8) & 0xFF);
    g_versn.prom[lo_index] = (unsigned char)(value & 0xFF);

    g_versn.prom[VERSN_PROM_SIGNATURE_HI] = (unsigned char)(VERSN_PROM_SIGNATURE >> 8);
    g_versn.prom[VERSN_PROM_SIGNATURE_LO] = (unsigned char)(VERSN_PROM_SIGNATURE & 0xFF);
}

/*
 * True when an identity value means "leave SINTRAN's own value alone".
 * GCPUNR skips SYSNO / HWINFO(2) when the PROM word is -1, and skips NLEGU when
 * its byte is 0377 octal.
 */
static bool versn_identity_is_skip(const char *text)
{
    if (text == NULL)
    {
        return false;
    }

    return (strcmp(text, "none") == 0) || (strcmp(text, "NONE") == 0) ||
           (strcmp(text, "keep") == 0) || (strcmp(text, "KEEP") == 0) ||
           (strcmp(text, "image") == 0) || (strcmp(text, "IMAGE") == 0) ||
           (strcmp(text, "-1") == 0);
}

/* Value of one hexadecimal digit, or -1 when the character is not hex. */
static int versn_hex_digit(char c)
{
    if ((c >= '0') && (c <= '9'))
    {
        return c - '0';
    }
    if ((c >= 'a') && (c <= 'f'))
    {
        return c - 'a' + 10;
    }
    if ((c >= 'A') && (c <= 'F'))
    {
        return c - 'A' + 10;
    }
    return -1;
}

/*
 * Parse one identity number, mirroring the RetroCore machine-config parser.
 *
 * Accepted radix forms:
 *   0x0C / 0X0C  hexadecimal
 *   0o14         octal, explicit prefix
 *   14B / 14b    octal, Norsk Data trailing-B convention
 *   014          octal, bare leading zero (how ND manuals write it)
 *   12           decimal
 *
 * Returns true and writes *out on success.
 */
static bool versn_parse_number(const char *text, int *out)
{
    int radix = 10;
    int acc = 0;
    size_t len;
    size_t i;
    size_t start = 0;
    size_t end;

    if ((text == NULL) || (out == NULL))
    {
        return false;
    }

    len = strlen(text);
    if (len == 0)
    {
        return false;
    }

    end = len;

    if ((len > 2) && (text[0] == '0') && ((text[1] == 'x') || (text[1] == 'X')))
    {
        radix = 16;
        start = 2;
    }
    else if ((len > 2) && (text[0] == '0') && ((text[1] == 'o') || (text[1] == 'O')))
    {
        radix = 8;
        start = 2;
    }
    else if ((len > 1) && ((text[len - 1] == 'b') || (text[len - 1] == 'B')))
    {
        radix = 8;
        end = len - 1;
    }
    else if ((len > 1) && (text[0] == '0'))
    {
        radix = 8;
        start = 1;
    }

    if (start >= end)
    {
        return false;
    }

    for (i = start; i < end; i++)
    {
        int d = versn_hex_digit(text[i]);

        if ((d < 0) || (d >= radix))
        {
            return false;
        }
        acc = acc * radix + d;
        if (acc > 0xFFFF) /* identity values are at most 16 bits */
        {
            return false;
        }
    }

    *out = acc;
    return true;
}

/*
 * Parse the microprogram version, which additionally accepts the REVISION
 * LETTER printed on the microcode EPROM label.
 *
 * The letter is the PLAIN alphabet position and the alphabet is NOT compressed
 * - "I" is NOT skipped. A=1 ... K=11 (octal 013) ... L=12 (octal 014). A letter
 * supplies only the LOW 8 bits; the high byte is kept, because no source
 * documents what the high byte should be.
 *
 * "L", "014", "0o14", "14B", "0x0C" and "12" therefore all mean revision L.
 * A lone "B" is the revision letter B (=2); the trailing-B octal form always has
 * at least one digit in front of it, so the two never collide.
 */
static bool versn_parse_microcode_version(const char *text, int *out)
{
    if ((text == NULL) || (out == NULL))
    {
        return false;
    }

    if (strlen(text) == 1)
    {
        char c = text[0];
        int letter = -1;

        if ((c >= 'A') && (c <= 'Z'))
        {
            letter = c - 'A' + 1;
        }
        else if ((c >= 'a') && (c <= 'z'))
        {
            letter = c - 'a' + 1;
        }

        if (letter > 0)
        {
            *out = (g_versn.microcode_version & 0x7F00) | letter;
            return true;
        }
    }

    return versn_parse_number(text, out);
}

/*
 * Apply one environment-variable identity override.
 * Prints a diagnostic and leaves the value alone when the text is unusable, so
 * a typo can never silently produce a machine with a different identity.
 */
static bool versn_env_number(const char *name, int *out)
{
    const char *text = getenv(name);

    if (text == NULL)
    {
        return false;
    }

    if (!versn_parse_number(text, out))
    {
        LOG(LOG_CAT_CPU, LOG_WARN, "Bad %s '%s' - keeping the default", name, text);
        return false;
    }
    return true;
}

/*
 * Configure the VERSN identity from environment variables.
 *
 * This is nd100x's machine-config mechanism (the same one CPU type selection
 * uses, cpu_set_type_from_env), and it mirrors the RetroCore .ini keys:
 *
 *   ND100X_CPU_NUMBER           <-> cpu_number / sysno    PROM bytes 0-1
 *   ND100X_SYSTEM_TYPE          <-> system_type           PROM bytes 2-3
 *   ND100X_LEGAL_USERS          <-> legal_users           PROM byte 4
 *   ND100X_INSTALLATION_NUMBER  <-> installation_number   PROM bytes 0-15, raw
 *   ND100X_MICROCODE_VERSION    <-> microcode_version     T register
 *   ND100X_PRINT_VERSION        <-> print_version         A register
 *
 * PRECEDENCE, identical to RetroCore: ND100X_INSTALLATION_NUMBER writes all
 * sixteen bytes verbatim (and MAY deliberately produce an invalid signature);
 * the friendly variables then overlay their own field bytes and force the
 * signature back on.
 *
 * "none" / "keep" / "image" / "-1" select SINTRAN's skip markers, i.e. leave
 * the value baked into the SINTRAN disk image alone.
 *
 * NOTE: environment variables are what nd100x has. There is no .ini / config
 * file layer here; adding one would mean a machine-config module plus a
 * front-end command-line surface in src/frontend/nd100x, which is out of scope.
 */
void cpu_versn_set_identity_from_env(void)
{
    const char *text;
    int value;

    /* 1. The RAW whole-image escape hatch, applied first. */
    text = getenv("ND100X_INSTALLATION_NUMBER");
    if (text != NULL)
    {
        unsigned char parsed[VERSN_PROM_SIZE];
        int digits = 0;
        int i;
        bool ok = true;

        for (i = 0; (text[i] != '\0') && ok; i++)
        {
            char c = text[i];
            int d;

            /* Separators are ignored so "01 04 .." and "0104.." both work. */
            if ((c == ' ') || (c == '\t') || (c == ',') || (c == ':') || (c == '-') || (c == '_'))
            {
                continue;
            }

            d = versn_hex_digit(c);
            if ((d < 0) || (digits >= VERSN_PROM_SIZE * 2))
            {
                ok = false;
                break;
            }

            if ((digits & 1) == 0)
            {
                parsed[digits / 2] = (unsigned char)(d << 4);
            }
            else
            {
                parsed[digits / 2] |= (unsigned char)d;
            }
            digits++;
        }

        if (ok && (digits == VERSN_PROM_SIZE * 2))
        {
            for (i = 0; i < VERSN_PROM_SIZE; i++)
            {
                g_versn.prom[i] = parsed[i];
            }
        }
        else
        {
            LOG(LOG_CAT_CPU, LOG_WARN,
                "Bad ND100X_INSTALLATION_NUMBER '%s' - need exactly 32 hex digits", text);
        }
    }

    /* 2. Friendly decoded fields overlay the image. */
    text = getenv("ND100X_CPU_NUMBER");
    if (text != NULL)
    {
        if (versn_identity_is_skip(text))
        {
            versn_set_word(VERSN_PROM_SYSNO_HI, VERSN_PROM_SYSNO_LO, 0xFFFF);
        }
        else if (versn_parse_number(text, &value))
        {
            versn_set_word(VERSN_PROM_SYSNO_HI, VERSN_PROM_SYSNO_LO, value);
        }
        else
        {
            LOG(LOG_CAT_CPU, LOG_WARN, "Bad ND100X_CPU_NUMBER '%s' - keeping the default", text);
        }
    }

    text = getenv("ND100X_SYSTEM_TYPE");
    if (text != NULL)
    {
        /*
         * The documented codes are 100/102/500/502/5561, but the SINTRAN source
         * writes that list with a trailing ".." (OPPSTART.NPL:3440), i.e. it is
         * OPEN-ENDED - any 16-bit value is accepted here on purpose.
         */
        if (versn_identity_is_skip(text))
        {
            versn_set_word(VERSN_PROM_SYSTYPE_HI, VERSN_PROM_SYSTYPE_LO, 0xFFFF);
        }
        else if (versn_parse_number(text, &value))
        {
            versn_set_word(VERSN_PROM_SYSTYPE_HI, VERSN_PROM_SYSTYPE_LO, value);
        }
        else
        {
            LOG(LOG_CAT_CPU, LOG_WARN, "Bad ND100X_SYSTEM_TYPE '%s' - keeping the default", text);
        }
    }

    text = getenv("ND100X_LEGAL_USERS");
    if (text != NULL)
    {
        if (versn_identity_is_skip(text))
        {
            value = VERSN_PROM_LEGAL_USERS_SKIP;
        }
        else if (!versn_parse_number(text, &value) || (value > 0xFE))
        {
            LOG(LOG_CAT_CPU, LOG_WARN, "Bad ND100X_LEGAL_USERS '%s' - want 0..254 or none", text);
            value = -1;
        }

        if (value >= 0)
        {
            g_versn.prom[VERSN_PROM_LEGAL_USERS] = (unsigned char)value;
            /* Same reason as versn_set_word(): the field is only useful if GCPUNR reads it. */
            g_versn.prom[VERSN_PROM_SIGNATURE_HI] = (unsigned char)(VERSN_PROM_SIGNATURE >> 8);
            g_versn.prom[VERSN_PROM_SIGNATURE_LO] = (unsigned char)(VERSN_PROM_SIGNATURE & 0xFF);
        }
    }

    /* 3. The two register-only values. */
    text = getenv("ND100X_MICROCODE_VERSION");
    if (text != NULL)
    {
        if (!versn_parse_microcode_version(text, &value))
        {
            LOG(LOG_CAT_CPU, LOG_WARN, "Bad ND100X_MICROCODE_VERSION '%s' - keeping the default",
                text);
        }
        else if (value < VERSN_MIN_MICROCODE_VERSION)
        {
            LOG(LOG_CAT_CPU, LOG_WARN,
                "ND100X_MICROCODE_VERSION '%s' is below the SINTRAN minimum of octal 013", text);
        }
        else if (value > 0xFFFF)
        {
            LOG(LOG_CAT_CPU, LOG_WARN, "ND100X_MICROCODE_VERSION '%s' does not fit in 16 bits",
                text);
        }
        else
        {
            g_versn.microcode_version = value;
        }
    }

    if (versn_env_number("ND100X_PRINT_VERSION", &value))
    {
        /* VERSN builds A as (print_version << 4) | (ALD & 0x0F) - only 12 bits survive. */
        if (value <= 0x0FFF)
        {
            g_versn.print_version = value;
        }
        else
        {
            LOG(LOG_CAT_CPU, LOG_WARN,
                "ND100X_PRINT_VERSION must fit in 12 bits - keeping the default");
        }
    }
}

/*
 * True when the emulated CPU is an ND-120 (any ND-120 variant). SINTRAN's SYSEVAL uses
 * VERSN's T-register bit 15 to tell an ND-120 from an ND-110; this predicate drives that
 * bit (see opcode_versn_read_cpu_version / the TRA CS path). Ported from the ND-120-support helper
 * (commit 97c9961); adapted to this tree's CpuType enum, which carries only ND120CX.
 */
static bool versn_is_nd120(void)
{
    return (g_current_cpu_type == ND120CX);
}


/************ IO INSTRUCTIONS *************/


/// <summary>
/// Check if the IO address points to special "in memory" registers
///
/// Addresses from 100000 . - 100777, are used to specify system control registers which have to be accessed via the ND-100 bus.
/// An example is the Error Correction Control Register (ECCR), physically located on the memory modules.
/// </summary>
/// <returns>true if the IO address was handled, false otherwise</returns>
static bool update_memory_io(void)
{
    if ((gT < 0x8000) || (gT > 0x81FF))
    {
        return false;
    }

    switch (gT)
    {
    case 0x804D: // 100115
        // By disabling this register ECCR test will say that there is no ECCR memory. Which is a benefit, then it can't fail :)
        // Test #5 in "MEMORY - Version: D00 - 1986-10-30" fails, because it expects and interrupt - but at the moment I dont know why..
        if (gECCR != gA)
        {
            gECCR = gA;
        }
        return true;
    default:
        break;
    }
    return false;
}

/* IOT
 * This is really an ND1 instruction
 * NOTE:: Privileged instructions
 * Format: IOT number
 * Code: 160 nnn. Opcode 5 bits, 11 bits for IO address
 * (0160000 - 0163777)
 *
 */
/* NORD-1 IOT dispatch, implemented in the device manager. Declared locally so
 * the CPU does not have to pull in the whole device-model header. */
bool devmgr_iot_op(uint8_t devno, uint8_t func, uint16_t *reg_a, bool *skip);


/********************SYSTEM FUNCTIONS  *******************/


/* DEPO (Privileged)
 */


/******************** CX FUNCTIONS  *******************/

/*
 * ================================================================================
 *  ND-110 S3SEG helpers
 * ================================================================================
 *
 * The ND-110 SINTRAN-III segment-handling instructions address PHYSICAL memory as
 * "segment:offset": a loaded segment (LDSEG in the microcode) selects a physical 64K
 * bank and the 16-bit offset indexes inside it.  Everything below shares that model.
 *
 * Ported from RetroCore Emulated.HW/ND/CPU/ND100/Instructions.ND110Specific.cs
 * (SegPhys / BankGroupPhysAddr), which was derived from the RASK microcode listing
 * <ND110Compile>\ND110Compile\uCode\ND-110-RASK.LISTING.TXT and
 * validated against the live ND-110 microcode oracle.
 */

/* WIP ("written in page") bit tested by CHREENTPAGES; R4 = BMG(14 octal) = 2^12. */
#define ND110_WIP_BIT (1 << 12)

/* PGU ("page used") bit collected by CLEPU; R7 = BMG(013 octal) = 04000 octal = 2^11. */
#define ND110_PGU_BIT (1 << 11)

/*
 * Computes a physical word address from a segment (physical 64K bank) and an offset.
 * address = (seg & 0xFF) << 16 | (offset & 0xFFFF).
 */
static uint32_t nd110_seg_phys(uint16_t seg, uint16_t offset)
{
    return ((uint32_t)(seg & 0xFF) << 16) | (uint32_t)(offset & 0xFFFF);
}

/*
 * Computes the physical word address for one of the ND-110 "bank group" instructions
 * (LASB/SASB/SZSB/LXSB against STBNK, LACB/SACB/SZCB/LXCB against CMBUK).
 *
 * The address is bank << 16 | ((index + delta) & 0xFFFF), where delta is the
 * instruction's 3-bit displacement (bits 3-5 of the opcode).  The index register
 * differs per group: the segment-table group uses B, the core-map group uses X (the
 * physical page number) - verified against the RASK microcode oracle (see the
 * BankGroupPhysAddr remarks in RetroCore Instructions.ND110Specific.cs).
 */
static uint32_t nd110_bankgroup_phys(uint16_t bank, uint16_t index, uint16_t operand)
{
    uint16_t delta = (uint16_t)((operand >> 3) & 0x07);
    uint32_t ea = (uint32_t)((index + delta) & 0xFFFF);

    return ((uint32_t)(bank & 0xFF) << 16) | ea;
}


/* SETPT - ND110+
 *
 * NOTE: Privileged instruction
 */


// **************************************************************************************
// ****  ND100 and ND110CX only - segment instructions
// **************************************************************************************

// PDF Page 101 (page number 99) in "MICROPROGRAMLISTNING FOR ND-110_32 BIT VERSION K-Gandalf-OCR.pdf"
/// SINTRAN III CONTROL INSTRUCTIONS
/// ALL ARE PRIVILEGED


/// <summary>
/// Clear page tables and collect PGU information.
/// Code: 140 304
/// Format: CLEPU
///
/// Segment function
///
/// Affected: (?)
/// </summary>
/* CLEPU PGU block (RASK 004100-004114): mark page idx in the 8-word
 * working-set table at L in the page-map bank; word = page >> 4,
 * bit = page & 0xF. */
static void clepu_mark_working_set(uint32_t idx)
{
    uint32_t page = idx & 0x7F;   /* 8 words * 16 bits = 128 pages */
    uint32_t word = page >> 4;    /* word number (0..7) */
    int bit = (int)(page & 0x0F); /* bit within the word */
    uint32_t table_addr = (uint32_t)((gL + word) & 0xFFFF);
    uint16_t tw = (uint16_t)mms_read_physical_memory((int)table_addr, true); /* 004107 EXRQ */

    tw |= (uint16_t)(1 << bit);                           /* 004112 set bit */
    mms_write_physical_memory((int)table_addr, tw, true); /* 004114 DERQ */
}


/*
 * ================================================================================
 *  ND-110 SPECIFIC INSTRUCTIONS - "INSTRUCTIONS TO SPEED UP SINTRAN III SEGMENT
 *  HANDLING", opcode groups 14050x / 14051x / 14070x.
 * ================================================================================
 *
 * ALL of these are PRIVILEGED and exist on ND-110/CX and ND-120/CX only.
 *
 * Every body below is a verbatim port of the corresponding RetroCore implementation in
 *   <RetroCore>\Emulated.HW\ND\CPU\ND100\Instructions.ND110Specific.cs
 * which was itself derived from the RASK microcode listing
 *   <ND110Compile>\ND110Compile\uCode\ND-110-RASK.LISTING.TXT
 * and validated instruction-by-instruction against the live ND-110 microcode oracle.
 * The RASK micro-addresses quoted in the comments are the ones in that listing; do NOT
 * delete them, they are the only traceability back to the silicon.
 *
 * Reference manuals: ND-06.029.1 EN (ND-110 Instruction Set) and ND-06.026.1 EN
 * (ND-110 Functional Description, p.196 for the WGLOB/RGLOB global pointers).
 */


/*
 * Shared body of ENPT (140506) and REPT (140507) - RASK REPK2, LISTING 10644-10704.
 *
 * The segment register is the core-map bank CMBUK.  For each node at X (a null forward
 * link at [X] terminates the walk): read descriptor word0 at [X+2], mask it with r4_mask
 * into A; read word1 at [X+3]; the page-table entry address is B = (word1 | 0176000) << 1;
 * write A to APT[B] and the physical page frame X >> 2 to APT[B+1]; then advance X := [X].
 * Page-table writes use the alternative page table; the descriptor/link reads are physical
 * in the CMBUK segment.
 *
 * r4_mask is 0173777 for ENPT (clears bit 11) and 073777 for REPT (clears bits 15 and 11).
 *
 * Port of RetroCore EnterPageTable().
 */
static void nd110_enter_page_table(uint16_t r4_mask)
{
    uint32_t cmbnk = (uint32_t)(gCMBUK & 0xFF)
                     << 16; /* segment = core-map bank (LDSEG from CMBNK) */

    /* 004561-004562: X == 0 terminates (nothing entered). */
    while (gX != 0)
    {
        uint16_t x_reg = gX;
        uint16_t word0;
        uint16_t word1;
        uint16_t b_reg;

        /* 004563-004564: descriptor word0 at [X+2] (physical, CMBUK segment); A := word0 & mask. */
        word0 = (uint16_t)mms_read_physical_memory((int)(cmbnk | (uint32_t)((x_reg + 2) & 0xFFFF)),
                                                   true);
        gA = (uint16_t)(word0 & r4_mask);

        /* 004566: descriptor word1 at [X+3].  004571: B register := (word1 | 0176000) << 1. */
        word1 = (uint16_t)mms_read_physical_memory((int)(cmbnk | (uint32_t)((x_reg + 3) & 0xFFFF)),
                                                   true);
        b_reg = (uint16_t)(((word1 | 0xFC00) << 1) & 0xFFFF);
        gB = b_reg;

        /* 004573: APT[B] := A (masked word0).  004575: APT[B+1] := X >> 2 (physical page frame). */
        mms_write_virtual_memory(b_reg, gA, true, WRITEMODE_WORD);
        mms_write_virtual_memory((uint16_t)((b_reg + 1) & 0xFFFF), (uint16_t)(x_reg >> 2), true,
                                 WRITEMODE_WORD);

        /* DIAG (--trace-nd110): per-node dump of the page-table entry actually written. */
        if (g_nd110_trace_fp != NULL)
        {
            fprintf(g_nd110_trace_fp,
                    "  ENPT node X=%06o w0=%06o w1=%06o -> B=%06o APT[B]=%06o APT[B+1]=%06o "
                    "shadow=%d PCR=%06o PONI=%d\n",
                    x_reg, word0, word1, b_reg, gA, (uint16_t)(x_reg >> 2),
                    mms_is_address_shadow_memory(b_reg, false) ? 1 : 0, g_reg->reg_PCR[CURR_LEVEL],
                    STS_PAGING_ON_IS_SET ? 1 : 0);
            fflush(g_nd110_trace_fp);
        }

        /* 004577-004600: advance X := [X] (forward link, physical CMBUK segment). */
        gX = (uint16_t)mms_read_physical_memory((int)(cmbnk | x_reg), true);
    }
}


/*
 * ---------------------------------------------------------------------------
 *  The 14070x "bank group": direct physical access to the segment table (STBNK,
 *  indexed by B) and to the core map (CMBUK, indexed by X - the physical page
 *  number, NOT B; that asymmetry is oracle-verified, see RetroCore
 *  BankGroupPhysAddr).  Opcode = 14070x + (delta << 3), delta = 3-bit displacement.
 * ---------------------------------------------------------------------------
 */


/********************* STACK INSTRUCTIONS *********************/


/************************ BYTE ************************/


/*********************** REGISTER OPERANDS ***********************/

// Math register operations
/* regop, RAD=0: SWAP RAND REXO RORA (see the comments in regop). */
static inline void regop_logical(uint16_t operand, uint16_t sr, uint16_t dr, uint16_t source,
                                 uint16_t destination)
{
    int cm1 = (int)((operand & 0x0080u) >> 7);
    int cld = (int)((operand & 0x0040u) >> 6);

    switch ((operand & 0x0300) >> 8)
    {
    case 0: /* SWAP: dr <- source (cm1->~source), sr <- old dr (cld->0) */
    {
        uint16_t old_dr = (dr == 0) ? 0 : (uint16_t)(g_reg->reg[CURR_LEVEL][dr] & 0xFFFF);
        uint16_t new_dr = (cm1) ? (uint16_t)~source : source;
        uint16_t new_sr = (cld) ? 0 : old_dr;
        if (dr != 0)
        {
            g_reg->reg[CURR_LEVEL][dr] = new_dr; /* discard write to register 0 (=STS) */
        }
        if (sr != 0)
        {
            g_reg->reg[CURR_LEVEL][sr] = new_sr; /* discard write to register 0 (=STS) */
        }
        break;
    }
    case 1: /* RAND: dr <- dest & (cm1?~src:src) */
        if (dr != 0)
        {
            g_reg->reg[CURR_LEVEL][dr] =
                (uint16_t)(destination & ((cm1) ? (uint16_t)~source : source));
        }
        break;
    case 2: /* REXO: plain = dest ^ src; but cm1 is OR-of-complement (dest | ~src), NOT XOR - the RASK
             * REXO;cm1;cld=0 routes through REX02 (ALUF,ORAB). cld (dest=0) yields ~src / src for free. */
        if (dr != 0)
        {
            g_reg->reg[CURR_LEVEL][dr] = (cm1) ? (uint16_t)(destination | (uint16_t)~source)
                                               : (uint16_t)(destination ^ source);
        }
        break;
    case 3: /* RORA: dr <- dest | (cm1?~src:src) */
        if (dr != 0)
        {
            g_reg->reg[CURR_LEVEL][dr] =
                (uint16_t)(destination | ((cm1) ? (uint16_t)~source : source));
        }
        break;
    default:
        break;
    }
}

/* regop, RAD=1: RADD / RSUB (see the comments in regop). */
static inline void regop_arith(uint16_t operand, uint16_t dr, uint16_t source, uint16_t destination)
{
    int tmp;

    tmp = (dr == 0)
              ? 0
              : g_reg->reg[CURR_LEVEL][dr]; /* NOOP-variant fallthrough value (unchanged dr) */
    switch ((operand & 0x0380) >> 7)
    {
    case 0:
        tmp = do_add(destination, source, 0);
        break; /* RADD */
    case 1:
        tmp = do_add(destination, ~source, 0);
        break; /* RADD CM1 */
    case 2:
        tmp = do_add(destination, source, 1);
        break; /* RADD AD1 */
    case 3:
        tmp = do_add(destination, ~source, 1);
        break; /* RADD AD1 CM1 */
    case 4:
        tmp = do_add(destination, source, cpu_getbit(_STS, STS_CARRY));
        break; /* RADD ADC */
    case 5:
        tmp = do_add(destination, ~source, cpu_getbit(_STS, STS_CARRY));
        break; /* RADD ADC CM1 */
    case 6:    /* NOOP */
        break;
    case 7: /* NOOP */
        break;
    default:
        break;
    }
    if (dr != 0)
    {
        g_reg->reg[CURR_LEVEL][dr] =
            (uint16_t)(tmp & 0xFFFF); /* discard write to register 0 (=STS) */
    }
}

static void regop(uint16_t operand)
{ /* SWAP RAND REXO RORA RADD RCLR EXIT RDCR RING RSUB */
    int rad, cld;
    uint16_t sr, dr, source, destination;
    uint16_t old_g_pc = gPC - 1;

    rad = ((operand & 0x0400) >> 10);
    cld = ((operand & 0x0040) >> 6);

    sr = ((operand & 0x0038) >> 3);
    dr = (operand & 0x0007);

    /* Register field 0 = "no register": reading yields 0, writing is DISCARDED. In nd100x reg[0] is
     * the STS register, so a write to register 0 must be suppressed or it corrupts STS. dr=0 must read
     * as 0 here too (NOT reg[0]=STS). Oracle-validated against the RASK microcode; see RetroCore commits
     * 0890b6fbb (SWAP reg-0), 7dbdbe729 (REXO;CM1), 581e7270a (RADD dr=0). */
    source = (sr == 0) ? 0 : g_reg->reg[CURR_LEVEL][sr] & 0xFFFF;
    destination = (cld) ? 0 : ((dr == 0) ? 0 : g_reg->reg[CURR_LEVEL][dr] & 0xFFFF);

    switch (rad)
    {
    case 0: /* Logical operation - SWAP RAND REXO RORA. NO dr!=0 guard: reg field 0 writes are discarded
             * (SWAP writes BOTH sr and dr, so dr=0 still writes the source-register half). */
        regop_logical(operand, sr, dr, source, destination);
        break;
    case 1: /* Arithmetic - RADD/RSUB. RASK has NO dr==0 special case: run do_add (which sets C/O/Q) on
             * EVERY path and only discard the register write for dr=0. The manual's "dr=0 resets carry,
             * else no-op" is WRONG for the ND-110 silicon (oracle-confirmed). */
        regop_arith(operand, dr, source, destination);
        break;
    default:
        break;
    }

    if ((g_disasm) && (dr == _P))
    {
        disasm_userel(old_g_pc, gPC);
    }
}


/********************* some */


/*
 * do_mcl - Masked Clear
 *  Affected: Internal register specified
 *  (Only STS, PID & PIE possible)
 *  <IR> = <IR> & (~A)
 *
 * NOTE:: STS need to be checked.
 * NOTE:: Privileged instructions
 */
static void do_mcl(uint16_t instr)
{
    if (!check_priv())
    {
        return;
    }

    switch (instr & 0x0F)
    {
    case 01: // STS
        g_reg->reg[CURR_LEVEL][_STS] &= ~(gA & 0x00FF);
        break;
    case 06: // PID
        /* This affects interrupt, so do locking and checking. */

        gPID &= ~gA;

        gCHKIT = true; // we need to check PK after this
        break;
    case 07: // PIE
        /* This affects interrupt, so do locking and checking. */
        gPIE &= ~gA;
        gCHKIT = true; // we need to check PK after this
        break;
    default:
        /* :TODO: Check if we need to do illegal instruction handling */
        break;
    }
}

/*
 * do_mst - Masked SET
 *  Affected: Internal register specified
 *  (Only STS, PID & PIE possible)
 *  <IR> = <IR> | (A)
 *
 * NOTE:: STS need to be checked.
 * NOTE:: Privileged instructions
 */
static void do_mst(uint16_t instr)
{
    if (!check_priv())
    {
        return;
    }

    switch (instr & 0x0F)
    {
    case 01: // STS
        g_reg->reg[CURR_LEVEL][0] |= (gA & 0x00ff);
        break;
    case 06: // PID
        /* This affects interrupt, so do locking and checking. */

        gPID |= gA;
        gCHKIT = true; // we need to check PK after this

        break;
    case 07: // PIE
        /* This affects interrupt, so do locking and checking. */
        gPIE |= gA;
        gCHKIT = true; // we need to check PK after this
        break;
    default:
        /* :TODO: Check if we need to do illegal instruction handling */
        break;
    }
}


/*
 * do_tra - Transfer to register
 *  Affected: Accumulator
 *  A = <IR>;
 *
 * NOTE: Privileged instructions
 */
static void do_tra(uint16_t instr)
{
    if (!check_priv())
    {
        return;
    }

    uint16_t temp, level;
    switch (instr & 0x0F)
    {
    case 00: /* TRA PANS */
        gA = gPANS;
        break;
    case 01:                                  /* TRA STS */
        gA = g_reg->reg[gPIL][_STS] & 0x00FF; /* Only lower 8 bits */
        gA |= g_reg->reg_STS & 0xFF00;        /* Upper 8 bits - SYSTEM bits*/

        break;
    case 02: /* TRA OPR */
        gA = gOPR;
        break;
    case 03: /* TRA PGS */
        /* TODO:: Check that this also is supposed to clear the PGS as it "unlocks" it */
        gA = gPGS;
        gPGS_Lock = false;
        gPGS = 0;
        break;
    case 04: /* TRA PVL */
        /* This one has a strange format. Described in ND-100 Functional Description section 2.9.2.5.4 */
        gA = 0;                           /* Clean it */
        gA = (gPVL & 0x0F) << 3 | 0xd782; /* = IRR (PVL) DP */
        break;
    case 05: /* TRA IIC */
        /* Manuals says(2.2.4.3) that this should be a number equal to the highest bit set in (IID & IIE) - Roger */
        /* Only bit 1-10 is used, so we only return a value between 1 and 10  or else  zero */

        gIIC = cpu_calc_iic();

        gA = gIIC;

        gIIC = 0;
        gIID = 0;

        gCHKIT = true; // recalc PK

        break;
    case 06: /* TRA PID */
        gA = gPID;
        break;
    case 07:
        gA = gPIE;
        break;
    case 010: // CSR
        gA =
            (1 << 2) |
            (1
             << 3); // Always report bit 2 and 3 as 1. Bit 2="MAN DIS" (Cache disabled manually as Emulator doesnt need caching. Bit 3=Cache Clear Finished
        break;
    case 011: /* TRA ACTL */
        gA = 1 << CURR_LEVEL;
        break;
    case 012: /* TRA ALD */
        gA = gALD;
        break;
    case 013: /* TRA PES */
        gA = gPES;
        break;
    case 014: /* PGC/PCR - Paging Control Register */
        temp = gA;
        level = (temp >> 3) & 0x0f;
        gA = g_reg->reg_PCR[level];
        if (g_mms_type == MMS1)
        {
            gA &= ~(1 << 2); // Clear bit 2 for MMS1 mode
        }

        // Always clear bit 15, as thats the way of the ND110 microcode
        gA = gA & ~(1 << 15);

        break;
    case 015: /* TRA PEA */
        gA = gPEA;

        // Unlock PEA and PES
        gPEA_Lock = false;
        gPES_Lock = false;
        break;
    case 017: /* TRA CS - read the writable control store (microprogram version). SINTRAN's LOCOSTORE
               * (PH-P2-RESTART.NPL: `X:=100; *150017; A=:MICVER`) reads the CPU's microcode version here
               * and compares bit 17 (bit 15) against the loaded microcode SEGMENT's CONVER: a 120 segment
               * on a non-120 CPU (or vice-versa) is a fatal "Mismatch CPU / micro-code-segm". Return octal
               * 023 (a revision >= SINTRAN's minimum 013 AND >= the on-disk segment rev, so LOCOSTORE takes
               * the NOTLOAD path instead of trying an IOX microcode download) with bit 15 SET on an ND-120
               * so it matches the ND-120 segment. nd100x has no real WCS; this mirrors RetroCore
               * ReadControlStore (commit 24ad44fd8). Without it an ND-120 aborts at RESTART.NPL 035551. */
        gA = (uint16_t)(0x13 | (versn_is_nd120() ? 0x8000 : 0));
        break;
    default: /* These registers dont exist, so just return 0 for now FIXME: Check correct behaviour.*/
        //  do nothing is the correct
        break;
    }
}

/*
 * do_exr - Run instruction in source register
 */
static void do_exr(uint16_t instr)
{
    uint16_t sr, exr_instr;
    sr = (instr >> 3) & 0x07;
    if (sr)
    {
        exr_instr = g_reg->reg[CURR_LEVEL][sr];
    }
    else
    {
        exr_instr = 0;
    }

    if (0140600 == disasm_extract_opcode(exr_instr))
    {                                             /* ILLEGAL:: EXR of EXR */
        cpu_setbit(_STS, STS_ERROR_INDICATOR, 1); //: TODO: activate CPU trap on level 14!!!
        return;
    }
    if (g_disasm)
    {
        disasm_exr(gPC, exr_instr);
    }

    // Execute opcode but do not touch Program Counter
    cpu_do_op(exr_instr, true);
}

/*
 * do_wait - Give up prio instruction
 * NOTE:: Only basic parts fixed yet, this is a fairly complex one
 *
 * NOTE:: Privileged instructions
 */
static void do_wait(uint16_t instr)
{
    (void)instr;
    if (!check_priv())
    {
        return;
    }

    uint16_t temp;
    if (!STS_INTERRUPT_ON_IS_SET)
    {
        // If the interrupt system is OFF
        // The ND-110 stops with the program counter (P register) pointing at the instruction after the WAIT and the front panel RUN indicator is turned off.
        // To restart the system, type ! on the console terminal
        printf("\r\nWAIT when IONI is off PIL[%d] PC[%6o] PID[0x%4X] PIE[0x%4X] IONI[%d] PONI[%d] "
               "STS_HI[%4X] STS_LO[%4X] A[%6o]\r\n",
               gPIL, gPC, gPID, gPIE, STS_INTERRUPT_ON_IS_SET, STS_PAGING_ON_IS_SET, g_reg->reg_STS,
               g_reg->reg[gPIL][_STS], gA);
        g_cpu_exit_code = (int)(short)gA;
        cpu_set_run_mode(CPU_STOPPED);
        return;
    }

    if (CURR_LEVEL == 0)
    {
        // Cant go lower
        return;
    }


    temp = ~(1 << CURR_LEVEL); /* Now we have a 0 in the position we want */
    gPID &= temp;              /* Give up this level */

    gCHKIT = true; // recalc PK (and do a level switch if needed)
}


//TODO: Make these into callbacks

/*
 * do_trr - Transfer to register
 *  Affected: Internal register specified
 *  <IR> = A;
 *
 * NOTE: STS and PCR NOT fixed yet!!!
 *
 * NOTE: Privileged instructions
 */
static void do_trr(uint16_t instr)
{
    if (!check_priv())
    {
        return;
    }

    uint16_t temp, level;
    switch (instr & 0x0F)
    {
    case 00: // TRR PANC
        gPANC = gA;
        panel_process_terminal_panc();

        break;
    case 01: // TRR STS
        /* ND-06.029.1 ND-110 Instruction Set, lists only lower 8 bits as changeable... */
        g_reg->reg[CURR_LEVEL][_STS] =
            (g_reg->reg[CURR_LEVEL][_STS] & 0xff00) | (gA & 0x00ff); /* Only change LSB  */
        break;
    case 02: // TRR LMP
        gLMP = gA;
        panel_process_terminal_lamp();

        break;
    case 03: /* PGC/PCR - Paging Control Register */
        temp = gA;
        level = (temp >> 3) & 0x0f;
        if (g_mms_type == MMS1)
        {
            temp &= ~(1 << 2); // Force Clear bit 2 for MMS1 mode
        }
        /* PCR0_WRITE tracing removed - was temporary overlay debugging */
        // The A-register bits 3-6 are the LEVEL SELECTOR (which PCR this TRR writes),
        // not PCR content, so they must be masked out before the store. Otherwise a
        // read-back (TRA PCR) returns value | (level<<3), which TPE PAGING-C02 test 2
        // (PAGING CONTROL REGISTERS on all levels) flags as "Failing data bits" under
        // MMS1. Real PCR content is ring (0-1), the MMS2 enable (2) and PT/APT (7-14).
        temp &= ~(0x0f << 3);
        g_reg->reg_PCR[level] = temp;

        break;
    case 05: // TRR IIE
        gIIE = gA;
        gCHKIT = true; // we need to check PK after this
        break;
    case 06: // TRR PID
        // TODO:? according to manual it can only set bit 15,13-12-11
        gPID = gA;
        gCHKIT = true; // we need to check PK after this
        break;
    case 07: // TRR PIE
        gPIE = gA;
        gCHKIT = true; // we need to check PK after this
        break;
    case 010: // TRR CCL (cache clear)
        gCCL = gA;
        break;
    case 011: // TRR LCIL
        gLCIL = gA;
        break;
    case 012: // TRR UCIL
        gUCIL = gA;
        break;
    case 013: /* TRR CILP (ND110 only??) */
        break;
    case 015: /* TRR ECCR (ND110 only??) */
        gECCR = gA;
        break;
    case 017: /* TRR CS (ND110 only) */
        break;
    default:
        break;
    }
}


/*
 * do_srb - Store register block.
 *  Affected:(EL),+ 1 +2 + 3 + 4 + 5 + 6 + 7
 *            P    X  T   A   D   L  STS  B
 *
 *  Uses the alternative pagetable!
 */
static void do_srb(uint16_t operand)
{

    if (!check_priv())
    {
        return;
    }

    uint16_t lvl, addr;
    uint16_t sts_temp;

    lvl = ((operand & 0x0078) >> 3);
    addr = gX;

    sts_temp = g_reg->reg[lvl][_STS] & 0x00ff;

    // If the current program level is specified, the stored P register points to the instruction following SRB.
    cpu_memory_write(g_reg->reg[lvl][_P], addr, true, 2);
    cpu_memory_write(g_reg->reg[lvl][_X], addr + 1, true, 2);
    cpu_memory_write(g_reg->reg[lvl][_T], addr + 2, true, 2);
    cpu_memory_write(g_reg->reg[lvl][_A], addr + 3, true, 2);
    cpu_memory_write(g_reg->reg[lvl][_D], addr + 4, true, 2);
    cpu_memory_write(g_reg->reg[lvl][_L], addr + 5, true, 2);
    cpu_memory_write(sts_temp, addr + 6, true, 2); /* Only write LSB of STS */
    cpu_memory_write(g_reg->reg[lvl][_B], addr + 7, true, 2);
}

/*
 * do_lrb - Load register block.
 *           (EL),+ 1 +2 + 3 + 4 + 5 + 6 + 7
 *  Affected:  P    X  T   A   D   L  STS  B
 *
 *  Uses the alternative pagetable!
 */

/// <summary>
/// LRB - Load register Block
/// Code: 152 6n2
/// Format: SRB <level* 10>
///
/// The instruction <LRB level * 10B> loads the contents  of the register block on program level specified in the
/// level field of the instruction.
///
/// The specified register block is  loaded by the contents of succeeding memory locations starting at the location
/// specified by the contents of the X register.
///
/// If the current program level is specified, the P register is not affected.
///
/// The LBR instruction is privileged
/// </summary>
static void do_lrb(uint16_t operand)
{

    if (!check_priv())
    {
        return;
    }

    uint16_t lvl, addr;

    lvl = ((operand & 0x0078) >> 3);
    addr = gX;


    if (lvl != CURR_LEVEL)
    { /* Dont change P on current level if this happens to be specified */
        g_reg->reg[lvl][_P] = cpu_memory_read(addr, true);
    }
    g_reg->reg[lvl][_X] = cpu_memory_read(addr + 1, true);
    g_reg->reg[lvl][_T] = cpu_memory_read(addr + 2, true);
    g_reg->reg[lvl][_A] = cpu_memory_read(addr + 3, true);
    g_reg->reg[lvl][_D] = cpu_memory_read(addr + 4, true);
    g_reg->reg[lvl][_L] = cpu_memory_read(addr + 5, true);
    g_reg->reg[lvl][_STS] = (g_reg->reg[lvl][_STS] & 0xff00) |
                            (cpu_memory_read(addr + 6, true) & 0x00ff); /* Only load LSB STS */
    g_reg->reg[lvl][_B] = cpu_memory_read(addr + 7, true);
}

static bool is_skip(uint16_t instr)
{
    uint16_t sr, dr, source, desti;
    signed short ss, sd, sgr, ovf;
    char z, o, c, s;
    sr = (instr >> 3) & 0x07;
    dr = (instr >> 0) & 0x07;
    source =
        (0 == sr) ? 0 : g_reg->reg[CURR_LEVEL][sr]; /* Never use STS reg but zero value instead */
    desti =
        (0 == dr) ? 0 : g_reg->reg[CURR_LEVEL][dr]; /* Never use STS reg but zero value instead */
    ss = (signed short)source;
    sd = (signed short)desti;

    /* Ok, lets set flags */
    z = (0 == (desti - source)) ? 1 : 0;
    sgr = sd - ss;
    ovf = (sd & ~ss & ~sgr) | (~sd & ss & sgr);
    o = (ovf < 0) ? 1 : 0;
    c = ((desti - source) < 0) ? 0 : 1;
    s = ((uint16_t)(sd - ss) >> 15) & 0x01;

    /* And use these to do the skipping, so we try and follow ND behaviour */
    switch ((instr >> 8) & 0x07)
    {
    case 0: /* EQL */
        if (z)
        {
            return true;
        }
        break;
    case 1: /* GEQ */
        if (!s)
        {
            return true;
        }
        break;
    case 2: /* GRE */
        if (!(s ^ o))
        {
            return true;
        }
        break;
    case 3: /* MGRE */
        if (c)
        {
            return true;
        }
        break;
    case 4: /* UEQ */
        if (!z)
        {
            return true;
        }
        break;
    case 5: /* LSS */
        if (s)
        {
            return true;
        }
        break;
    case 6: /* LST */
        if (s ^ o)
        {
            return true;
        }
        break;
    case 7: /* MLST */
        if (!c)
        {
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

static void do_bops(uint16_t operand)
{
    uint16_t bn, dr, desti;
    bn = ((operand & 0x0078) >> 3);
    dr = (operand & 0x0007);

    switch ((operand & 0x0780) >> 7)
    {
    case 0: /* BSET ZRO */
        cpu_setbit(dr, bn, 0);
        break;
    case 1: /* BSET ONE */
        cpu_setbit(dr, bn, 1);
        break;
    case 2: /* BSET BCM */
        desti = cpu_getbit(dr, bn);
        desti ^= 1; /* XOR with one to invert bit */
        cpu_setbit(dr, bn, desti);
        break;
    case 3: /* BSET BAC */
        cpu_setbit(dr, bn, cpu_getbit(_STS, STS_BIT_ACCUMULATOR));
        break;
    case 4: /* BSKP ZRO */
        if (!cpu_getbit(dr, bn))
        {
            gPC++; /* Skip next instruction if zero */
        }
        break;
    case 5: /* BSKP ONE */
        if (cpu_getbit(dr, bn))
        {
            gPC++; /* Skip next instruction if one */
        }
        break;
    case 6: /* BSKP BCM */
        if ((cpu_getbit(dr, bn) ^ 1) == cpu_getbit(_STS, STS_BIT_ACCUMULATOR))
        {
            gPC++; /* Skip next instruction if bit complement */
        }
        break;
    case 7: /* BSKP BAC */
        if (cpu_getbit(dr, bn) == cpu_getbit(_STS, STS_BIT_ACCUMULATOR))
        {
            gPC++; /* Skip next instruction if equal */
        }
        break;
    case 8: /* BSTC */
        cpu_setbit(dr, bn, (cpu_getbit(_STS, STS_BIT_ACCUMULATOR) ^ 1));
        cpu_setbit(_STS, STS_BIT_ACCUMULATOR, 1);
        break;
    case 9: /* BSTA */
        cpu_setbit(dr, bn, cpu_getbit(_STS, STS_BIT_ACCUMULATOR));
        cpu_setbit(_STS, STS_BIT_ACCUMULATOR, 0);
        break;
    case 10: /* BLDC */
        cpu_setbit(_STS, STS_BIT_ACCUMULATOR, cpu_getbit(dr, bn) ^ 1);
        break;
    case 11: /* BLDA */
        cpu_setbit(_STS, STS_BIT_ACCUMULATOR, cpu_getbit(dr, bn));
        break;
    case 12: /* BANC */
        cpu_setbit(_STS, STS_BIT_ACCUMULATOR,
                   ((cpu_getbit(dr, bn) ^ 1) & cpu_getbit(_STS, STS_BIT_ACCUMULATOR)));
        break;
    case 13: /* BAND */
        cpu_setbit(_STS, STS_BIT_ACCUMULATOR,
                   (cpu_getbit(dr, bn) & cpu_getbit(_STS, STS_BIT_ACCUMULATOR)));
        break;
    case 14: /* BORC */
        cpu_setbit(_STS, STS_BIT_ACCUMULATOR,
                   ((cpu_getbit(dr, bn) ^ 1) | cpu_getbit(_STS, STS_BIT_ACCUMULATOR)));
        break;
    case 15: /* BORA */
        cpu_setbit(_STS, STS_BIT_ACCUMULATOR,
                   (cpu_getbit(dr, bn) | cpu_getbit(_STS, STS_BIT_ACCUMULATOR)));
        break;
    default:
        break;
    }
}

static uint16_t shift_reg(uint16_t reg, uint16_t instr)
{
    bool isneg = ((instr & 0x0020) >> 5) ? 1 : 0;
    /* Right-shift count is the two's complement of the 6-bit field, but the hardware shift counter is
     * only 5 BITS, so it wraps mod 32: field 040 octal (= 32) loads as 0 -> NO shift (register unchanged,
     * M preserved). Oracle-validated (RetroCore CpuND100.Fetch, commit 135a2ff28). Fields 041..077
     * (counts 31..1) already fit and are unaffected. M-on-count-0 is already correct here (tmp inits to M). */
    uint16_t offset =
        (isneg) ? (uint16_t)((~((instr & 0x003F) | 0xFFC0) + 1) & 0x1F) : (instr & 0x003F);
    uint16_t shifttype = ((instr >> 9) & 0x03);
    int i, tmp, msb;
    int m = cpu_getbit(_STS, STS_SHIFT_OUT);
    tmp = m; /* just in case.. */
    for (i = 1; i <= offset; i++)
    {
        tmp = (isneg) ? (reg & 0x01) : ((reg >> 15) & 0x01); /* tmp = bit shifted out */
        msb = reg >> 15 & 1;                                 /* msb before shift */
        reg = (isneg) ? reg >> 1 : reg << 1;
        switch (shifttype)
        {
        case 0:                                                              /* Plain */
            reg = (isneg) ? ((reg & 0x7fff) | (msb << 15)) : (reg & 0xfffe); /* SHR : SHL */
            break;
        case 1: /* ROT */
            reg = (isneg) ? ((reg & 0x7fff) | (tmp << 15)) : ((reg & 0xfffe) | tmp);
            break;
        case 2: /* ZIN */
            reg = (isneg) ? (reg & 0x7fff) : (reg & 0xfffe);
            break;
        case 3: /* LIN */
            reg = (isneg) ? ((reg & 0x7fff) | (m << 15)) : ((reg & 0xfffe) | m);
            break;
        default:
            break;
        }
    }
    cpu_setbit(_STS, STS_SHIFT_OUT, tmp);
    return reg;
}

/* The A:D register pair is exactly 32 bits; do the arithmetic in uint32_t.
 * (It used ulong, 64-bit native and 32-bit on wasm, and shifted an int
 * into bit 31, which is undefined behaviour.) */
static uint32_t shift_double_reg(uint32_t reg, uint16_t instr)
{
    bool isneg = ((instr & 0x0020) >> 5) ? 1 : 0;
    /* 5-bit shift-counter wrap: field 040 octal (=32) -> 0 = NO shift (SAD register pair unchanged, M
     * preserved). Oracle-validated (RetroCore 135a2ff28). See shift_reg for the full note. */
    uint16_t offset =
        (isneg) ? (uint16_t)((~((instr & 0x003F) | 0xFFC0) + 1) & 0x1F) : (instr & 0x003F);
    uint16_t shifttype = ((instr >> 9) & 0x03);
    int i;
    uint32_t tmp, msb;
    uint32_t m = (uint32_t)cpu_getbit(_STS, STS_SHIFT_OUT);
    tmp = m; /* just in case.. */
    for (i = 1; i <= offset; i++)
    {
        tmp = (isneg) ? (reg & 0x01) : ((reg >> 31) & 0x01); /* tmp = bit shifted out */
        msb = reg >> 31 & 1;                                 /* msb before shift */
        reg = (isneg) ? reg >> 1 : reg << 1;
        switch (shifttype)
        {
        case 0:                                                                      /* Plain */
            reg = (isneg) ? ((reg & 0x7fffffff) | (msb << 31)) : (reg & 0xfffffffe); /* SHR : SHL */
            break;
        case 1: /* ROT */
            reg = (isneg) ? ((reg & 0x7fffffff) | (tmp << 31)) : ((reg & 0xfffffffe) | tmp);
            break;
        case 2: /* ZIN */
            reg = (isneg) ? (reg & 0x7fffffff) : (reg & 0xfffffffe);
            break;
        case 3: /* LIN */
            reg = (isneg) ? ((reg & 0x7fffffff) | (m << 31)) : ((reg & 0xfffffffe) | m);
            break;
        default:
            break;
        }
    }
    cpu_setbit(_STS, STS_SHIFT_OUT, (char)tmp);
    return reg;
}

/*
 * do_ident
 * Handles IDENT PLxx instructions
 */
static void do_ident(uint16_t priolevel)
{

    int id = io_ident(priolevel);

    // IDENT is the ND-100 interrupt ACKNOWLEDGE for this level. IO_Ident /
    // terminal_ident already clears the identified device's own request, and the
    // CPU's pending-interrupt latch (gPID) is recomputed from LIVE device requests
    // by the IO_Tick / device_interrupt() path - so IDENT must NOT force-clear gPID
    // or force a level switch here.
    //
    // HISTORY (do NOT re-add): a NORD-TSS-motivated change once did
    //     gPID &= ~(1 << priolevel);  gCHKIT = true;
    // on every IDENT to stop a TSS LEV12 ("IDENT PL12 ... WAIT; JMP LEV12") spin.
    // That STALLED interrupt servicing for every other guest: TPE INSTRUCTION hung
    // immediately after loading (never ran a test level) and SINTRAN III never
    // reached RUNNING - both boot correctly without it. The TSS LEV12 problem must
    // be solved without breaking IDENT for everyone else.

    if (id >= 0)
    {
        gA = id & 0xFFFF;
    }
    else
    {
        gA = 0;

        if (priolevel != 13) // ignore RTC
        {
            cpu_interrupt(14, 1 << 7); /* IOX Error if no IDENT code found */
        }
    }
    return;
}

/// <summary>
/// RDUS - Read don't use cache Code: 140127
/// Code: 140 127
/// Format: RDUS
///
///  This instruction reads the content of the memory location pointed to by the T-register into the A-register.
///  The address in the T-register is a logical memory address.Translation to a physical memory address is normally done by using the page tables.
///  However, the translation will use the alternative page table when PTM is on (Page Table Modus) (status register bit 0 is 1) and the paging system is on, PON.
/// </summary>

static void do_rdus(uint16_t instr)
{
    (void)instr;
    gA = cpu_memory_read(gT, true);
}

/// <summary>
/// TSET - Test and set
/// Code: 140 123
/// Format: TSET
///
/// This instruction writes -1 into the memory address pointed to by the T-register.
/// Simultaneously, the old content of the same address is read into the A-register.This read/write sequence is performed with the memory system 'locked',
/// so that the two memory accesses cannot be split by other accesses on other memory channels.
/// This may be used to implement processor synchronizing.
/// The address in the T-register is a logical memory address.
/// Translation to a physical memory address is normally done by using the page tables.
/// However, the translation will use the alternative page table when PTM is on (Page Table Modus) (status register bit 0 is 1) and the paging system is on, PON.
///
/// The old content of the memory address is always read from the memory, and never from the cache, Data is written both to memory and cache.
/// </summary>
static void do_tset(uint16_t instr)
{
    (void)instr;
    // cpu.WriteVirtualMemory(regs.currentRegisters.T, 0xFFFF, PageTable.AlternativePageTable); // Write -1

    gA = cpu_memory_read(gT, true);
    cpu_memory_write(0xFFFF, gT, true, 2);
}

/// <summary>
/// MOVEW - WORD BLOCK INSTRUCTION
///
/// Code 143 1nn
/// If the memory management system is off, bank 0 of physical memory is addressed. (Bit PTM of the STS register is zero) and the following transfer fields become equivalent:
///   nn = 00 = 01 = 03 = 04
///   nn = 02 = 05
///   nn = 06 = 07
///
/// MOVEW can be interrupted. L, A, D, X, T and P registers are then changed to restart execution.
///
/// A and D - Source address
/// X and T - Destination address
/// L       - The number of words to be moved (max 2048)
///
/// A and/or X are used for physical memory-block moves and are incremented when the D and/or T registers overflow.
///
/// If the L register contains a value grater then 2048 (L=o4000) no words are moved and A,D, T and X are unchanged.
///
/// After transfer the register contains: A,D, T,X - The addresses after the last moved word . L = zero
///
/// Format: MOVEW
/// </summary>
static void do_movew(uint16_t instr)
{
    unsigned int source_address = gD;
    unsigned int destination_address = gT;
    uint16_t cnt = gL;

    uint16_t displacement = (instr & 0x00F);

    // Check if source and destination are in physical memory
    bool is_source_physical = false;
    bool is_destination_physical = false;

    switch (displacement)
    {
    case 2:
    case 5:
        destination_address = (destination_address | (gX << 16)) & 0xFFFFFF;
        is_destination_physical = true;
        break;

    case 6:
    case 7:
        source_address = (source_address | (gA << 16)) & 0xFFFFFF;
        is_source_physical = true;
        break;
    case 8:
        destination_address = (destination_address | (gX << 16)) & 0xFFFFFF;
        is_destination_physical = true;

        source_address = (source_address | (gA << 16)) & 0xFFFFFF;
        is_source_physical = true;
        break;
    default:
        break;
    }

    // Check for priveleged instruction
    if (is_source_physical || is_destination_physical)
    {
        if (!check_priv())
        {
            return;
        }
    }

    // Warning: In the loop of read/write below, PageFault can occur, and the instruction can be restarted.
    uint16_t temp = 0;

    while (cnt > 0)
    {
        switch (displacement)
        {
        case 0: // move from PT to PT
            temp = cpu_memory_read(source_address, false);
            cpu_memory_write(temp, destination_address, false, 2);
            break;
        case 1: // move from PT to APT
            temp = cpu_memory_read(source_address, false);
            cpu_memory_write(temp, destination_address, true, 2);
            break;
        case 2: // move from PT to physical memory
            temp = cpu_memory_read(source_address, false);
            mms_write_physical_memory(destination_address, temp, true);
            break;
        case 3: // move from APT to PT
            temp = (uint16_t)cpu_memory_read(source_address, true);
            cpu_memory_write(temp, destination_address, false, 2);
            break;
        case 4: // move from APT to APT
            temp = (uint16_t)cpu_memory_read(source_address, true);
            cpu_memory_write(temp, destination_address, true, 2);
            break;
        case 5: // move from APT to physical memory
            temp = (uint16_t)cpu_memory_read(source_address, true);
            mms_write_physical_memory(destination_address, temp, true);
            break;
        case 6: // move from physical memory to PT
            temp = mms_read_physical_memory(source_address, true);
            cpu_memory_write(temp, destination_address, false, 2);
            break;
        case 7: // move from physical memory to APT
            temp = mms_read_physical_memory(source_address, true);
            cpu_memory_write(temp, destination_address, true, 2);
            break;

        case 8: // move from physical memory to physical memory
            temp = mms_read_physical_memory(source_address, true);
            mms_write_physical_memory(destination_address, temp, true);
            break;

        default:
            break;
        }
        source_address++;
        destination_address++;
        cnt--;
    }

    // After here, no PageFault can occur - update register values

    // update L
    gL = cnt;

    // Update Source with the new address
    gD = (source_address & 0xFFFF);
    if (is_source_physical)
    {
        gA = (source_address >> 16) & 0xFFFF;
    }

    // Update destination
    gT = (destination_address & 0xFFFF);
    if (is_destination_physical)
    {
        gX = (destination_address >> 16) & 0xFFFF;
    }
}

/*
 * MOVB and MOVBF exist twice, and the pair below is the one that runs.
 *
 * This pair is registered at 0140131 and 0140132. The other pair,
 * opcode_movb_move_byte_buggy and opcode_movbf_move_bytes_forward_buggy,
 * is further down; it calls do_move_bytes and its registration is commented
 * out. It is kept on purpose because it does not work, not because it is
 * dead code - do not delete it.
 *
 * The macro name below is misleading and is kept only because it is
 * referenced elsewhere: set to 1, it COMPILES this pair rather than removing
 * it. The unfinished-work notes on these two functions are the original
 * author's and still stand.
 */
#define _removed_MOVB_AND_MOVBF_ 1
#if _removed_MOVB_AND_MOVBF_ // replaced with do_move_bytes

#endif


void cpu_add_a_mem(uint16_t eff_addr, bool use_apt)
{
    int temp, data, oldreg;
    oldreg = gA;
    data = cpu_memory_read(eff_addr, use_apt);
    temp = gA + data;

    // FIXME - ADD FLAG HANDLING CORRECTLY FOR C,O,Q FLAGS (CHECK AGAIN THINK WE MIGHT HAVE SUBTLE BUGS)

    if ((temp > 0xFFFF) || (temp < 0))
    {
        cpu_setbit(_STS, STS_CARRY, 1);
        if ((oldreg & 0x8000) && (data & 0x8000) && !(temp & 0x8000))
        {
            cpu_setbit(_STS, STS_DYNAMIC_OVERFLOW, 1);
        }
        else
        {
            cpu_setbit(_STS, STS_DYNAMIC_OVERFLOW, 0);
        }
    }
    else
    {
        cpu_setbit(_STS, STS_CARRY, 0);
        if (!(oldreg & 0x8000) && !(data & 0x8000) && (temp & 0x8000))
        {
            cpu_setbit(_STS, STS_DYNAMIC_OVERFLOW, 1);
        }
        else
        {
            cpu_setbit(_STS, STS_DYNAMIC_OVERFLOW, 0);
        }
    }

    gA = (temp & 0xFFFF);
}

/*
 * Move bytes in memory
 * Note: This is part of the commercial instruction set
 * It seems SINTRAN doesnt use this function for booting and operating
 * checkOverlapping: MOVBF sets this to true, MOVB sets this to false
 */
static void do_move_bytes(bool check_overlapping)
{
    const int len_mask = 0xFFF;
    int read_value;

    int num_bytes_source = gD & len_mask; // Source length
    int num_bytes_dest = gT & len_mask;   // Destination length
    if (num_bytes_dest < num_bytes_source)
    {
        num_bytes_source = num_bytes_dest; // Cap number of bytes to max length of Destination
    }

    // If Bit 13 is set, then setup has been executed and we are returning from an interrupt
    if (!(gD & (1 << 13)))
    {
        gT = (gT & 0xC000) | num_bytes_source;
        gD = (gT & 0xC000);

        // Mark D bit 13 with setup done
        gD |= (1 << 13);
    }

    if (check_overlapping)
    {
        // Convert byte count to word count for addressing
        int num_words_d = num_bytes_source >> 1; // Same as numBytesD / 2

        // Calculate start and end positions for source and destination in terms of words
        int source_start = gA;
        int destination_start = gX;
        int source_end = source_start + num_words_d;
        int destination_end = destination_start + num_words_d;

        // Check for forbidden overlap
        // Overlap is forbidden if destination overlaps source before it is read
        if (destination_start < source_end && destination_end > source_start)
        {
            // OVERLAP EXISTS - ILLEGAL IF 'MOVBF'!!
            // Forbidden overlap exists, return with error (no skip)
            return;
        }
    }

    bool use_apt = true; // Use alternative page table
    WriteMode read_mode;
    WriteMode write_mode;

    if (gX < gA)
    {
        // High to low
        for (int i = (gT & len_mask); i > 0; i--)
        {
            // Bit 15: 0=>MSB, 1=> LSB
            read_mode = (gD & (1 << 15)) ? WRITEMODE_LSB : WRITEMODE_MSB;
            read_value = cpu_memory_read(gA, use_apt);

            if (read_mode == WRITEMODE_MSB)
            {
                read_value = (read_value >> 8) & 0xFF;
            }
            else
            {
                read_value = read_value & 0xFF;
            }

            write_mode = (gT & (1 << 15)) ? WRITEMODE_LSB : WRITEMODE_MSB;
            cpu_memory_write(read_value, gX, use_apt, write_mode);

            gD ^= (1 << 15); // Flip D bit 15
            if (!(gD & (1 << 15)))
            {
                gA--;
            }

            gT ^= (1 << 15); // Flip T bit 15
            if (!(gT & (1 << 15)))
            {
                gX--;
            }
        }
    }
    else
    {
        // Low to High
        for (int i = (gD & len_mask); i < (gT & len_mask); i++)
        {
            // Bit 15: 0=>MSB, 1=> LSB
            read_mode = (gD & (1 << 15)) ? WRITEMODE_LSB : WRITEMODE_MSB;
            read_value = cpu_memory_read(gA, use_apt);

            if (read_mode == WRITEMODE_MSB)
            {
                read_value = (read_value >> 8) & 0xFF;
            }
            else
            {
                read_value = read_value & 0xFF;
            }

            write_mode = (gT & (1 << 15)) ? WRITEMODE_LSB : WRITEMODE_MSB;
            cpu_memory_write(read_value, gX, use_apt, write_mode);

            gD ^= (1 << 15); // Flip D bit 15
            if (!(gD & (1 << 15)))
            {
                gA++;
            }

            gT ^= (1 << 15); // Flip T bit 15
            if (!(gT & (1 << 15)))
            {
                gX++;
            }
        }
    }

    // After execution, bit 15 of the D and T registers point to the end of the field that has been moved.
    // Note: DON'T CLEAR bit 15 of D and T, but clear bits 13 and 12.

    // After execution the field length of the D (source) equals Zero
    // Note: Clear setup and count bits
    gD &= 0xC000;

    // Documentation for MOVB and MOVBF says the same but implementation differs
    if (check_overlapping)
    {
        gT &= 0xC000; // MOVBF
    }
    else
    {
        gT &= 0xCFFF; // MOVB
    }

    gPC++; // SKIP return
}


/* ----------------------------------------------- Argument and register set */

/**
 * @brief MIX3 - Multiply index by 3.
 *
 * (X) <- [(A)-1] * 3 Take the contents of the A register as an operand and
 * subtract one. Multiply the result by three and place it in the X register.
 *
 * @par Instruction
 * Opcode 143200 octal, mask 1111_1111_1111_1111.
 * Category: Register Operations. Privilege: User.
 * Format: MIX3
 *
 * @par Registers affected
 * (X)
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section MIX3
 */
static void opcode_mix3_multiply_index_by_three(uint16_t operand)
{
    (void)operand;
    gX = (uint16_t)((gA - 1) * 3);
}

/**
 * @brief SAB - Set argument to B.
 *
 * @par Instruction
 * Opcode 170000 octal, mask 1111_1111_0000_0000.
 * Category: Argument Instruction. Privilege: User.
 * Format: SAB <number>
 *
 * @par Instruction word
 *   bits 15-8  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 7-0  number - 8-bit argument extended to 16 bits using sign
 *       extension
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section SAB
 */
static void opcode_sab_set_argument_to_b(uint16_t operand)
{
    cpu_setreg(_B, cpu_sign_extend(operand & 0xFF));
}

/**
 * @brief SAA - Set argument to A.
 *
 * @par Instruction
 * Opcode 170400 octal, mask 1111_1111_0000_0000.
 * Category: Argument Instruction. Privilege: User.
 * Format: SAA <number>
 *
 * @par Instruction word
 *   bits 15-8  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 7-0  number - 8-bit argument extended to 16 bits using sign
 *       extension
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section SAA
 */
static void opcode_saa_set_argument_to_a(uint16_t operand)
{
    cpu_setreg(_A, cpu_sign_extend(operand & 0xFF));
}

/**
 * @brief SAT - Set argument to T.
 *
 * @par Instruction
 * Opcode 171000 octal, mask 1111_1111_0000_0000.
 * Category: Argument Instruction. Privilege: User.
 * Format: SAT <number>
 *
 * @par Instruction word
 *   bits 15-8  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 7-0  number - 8-bit argument extended to 16 bits using sign
 *       extension
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section SAT
 */
static void opcode_sat_set_argument_to_t(uint16_t operand)
{
    cpu_setreg(_T, cpu_sign_extend(operand & 0xFF));
}

/**
 * @brief SAX - Set argument to X.
 *
 * @par Instruction
 * Opcode 171400 octal, mask 1111_1111_0000_0000.
 * Category: Argument Instruction. Privilege: User.
 * Format: SAX <number>
 *
 * @par Instruction word
 *   bits 15-8  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 7-0  number - 8-bit argument extended to 16 bits using sign
 *       extension
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section SAX
 */
static void opcode_sax_set_argument_to_x(uint16_t operand)
{
    cpu_setreg(_X, cpu_sign_extend(operand & 0xFF));
}

/**
 * @brief AAB - Add argument to B.
 *
 * @par Instruction
 * Opcode 172000 octal, mask 1111_1111_0000_0000.
 * Category: Argument Instruction. Privilege: User.
 * Format: AAB <number>
 *
 * @par Instruction word
 *   bits 15-8  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 7-0  number - 8-bit argument extended to 16 bits using sign
 *       extension
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section AAB
 */
static void opcode_aab_add_argument_to_b(uint16_t operand)
{
    uint16_t temp;

    temp = cpu_sign_extend(operand & 0xFF);
    gB = do_add(gB, temp, 0);
}

/**
 * @brief AAA - Add argument to A.
 *
 * @par Instruction
 * Opcode 172400 octal, mask 1111_1111_0000_0000.
 * Category: Argument Instruction. Privilege: User.
 * Format: AAA <number>
 *
 * @par Instruction word
 *   bits 15-8  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 7-0  number - 8-bit argument extended to 16 bits using sign
 *       extension
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section AAA
 */
static void opcode_aaa_add_argument_to_a(uint16_t operand)
{
    short temp;

    temp = cpu_sign_extend(operand & 0xFF);
    gA = do_add(gA, temp, 0);
}

/**
 * @brief AAT - Add argument to T.
 *
 * @par Instruction
 * Opcode 173000 octal, mask 1111_1111_0000_0000.
 * Category: Argument Instruction. Privilege: User.
 * Format: AAT <number>
 *
 * @par Instruction word
 *   bits 15-8  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 7-0  number - 8-bit argument extended to 16 bits using sign
 *       extension
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section AAT
 */
static void opcode_aat_add_argument_to_t(uint16_t operand)
{
    uint16_t temp;

    temp = cpu_sign_extend(operand & 0xFF);
    gT = do_add(gT, temp, 0);
}

/**
 * @brief AAX - Add argument to X.
 *
 * @par Instruction
 * Opcode 173400 octal, mask 1111_1111_0000_0000.
 * Category: Argument Instruction. Privilege: User.
 * Format: AAX <number>
 *
 * @par Instruction word
 *   bits 15-8  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 7-0  number - 8-bit argument extended to 16 bits using sign
 *       extension
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section AAX
 */
static void opcode_aax_add_argument_to_x(uint16_t operand)
{
    uint16_t temp;

    temp = cpu_sign_extend(operand & 0xFF);

    gX = do_add(gX, temp, 0);
}


/* -------------------------------------------------- Memory transfer - load */

/**
 * @brief STD - Store double word.
 *
 * (ea)     <- (A)   (ea) + 1 <- (D) Store the contents of the A register in
 * the memory location pointed to by the effective address Store the contents
 * of the D register in the memory location pointed to by the effective
 * address plus one.
 *
 * @par Instruction
 * Opcode 020000 octal, mask 1111_1000_0000_0000.
 * Category: Memory Transfer - Double word instructions. Privilege: User.
 * Format: STD <addressing_mode> <displacement>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section STD
 */
static void opcode_std_store_double_word(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);
    cpu_memory_write(gA, gEA + 0, gUseAPT, 2);
    cpu_memory_write(gD, gEA + 1, gUseAPT, 2);
}

/**
 * @brief LDD - Load double word.
 *
 * A <- (ea)   D <- (ea) + 1 Load the contents of the memory location pointed
 * to by the effective address into the A register  Load the contents of the
 * memory location pointed to by the effective address plus one into the D
 * register.
 *
 * @par Instruction
 * Opcode 024000 octal, mask 1111_1000_0000_0000.
 * Category: Memory Transfer - Double word instructions. Privilege: User.
 * Format: LDD <addressing_mode> <displacement>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LDD
 */
static void opcode_ldd_load_double_word(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);

    gA = cpu_memory_read(gEA + 0, gUseAPT);
    gD = cpu_memory_read(gEA + 1, gUseAPT);
}

/**
 * @brief LDA - Load A register.
 *
 * Load the contents of the memory location pointed to by the effective
 * address into the A register.
 *
 * @par Instruction
 * Opcode 044000 octal, mask 1111_1000_0000_0000.
 * Category: Memory Transfer - Load Instruction. Privilege: User.
 * Format: LDA <addressing_mode> <disp>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LDA
 */
static void opcode_lda_load_a_register(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);
    gA = cpu_memory_read(gEA, gUseAPT);
}

/**
 * @brief LDT - Load T register.
 *
 * (T) <- (ea) Load the contents of the memory location pointed to by the
 * effective address into the T register.
 *
 * @par Instruction
 * Opcode 050000 octal, mask 1111_1000_0000_0000.
 * Category: Memory Transfer - Load Instruction. Privilege: User.
 * Format: LDT <addressing_mode> <disp>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LDT
 */
static void opcode_ldt_load_t_register(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);
    gT = cpu_memory_read(gEA, gUseAPT);
}

/**
 * @brief LDX - Load X register.
 *
 * (X) <- (ea) Load the contents of the memory location pointed to by the
 * effective address into the X register.
 *
 * @par Instruction
 * Opcode 054000 octal, mask 1111_1000_0000_0000.
 * Category: Memory Transfer - Load Instruction. Privilege: User.
 * Format: LDX <addressing_mode> <disp>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LDX
 */
static void opcode_ldx_load_x_register(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);
    gX = cpu_memory_read(gEA, gUseAPT);
}


/* ------------------------------------------------- Memory transfer - store */

/**
 * @brief STZ - Store zero to memory location.
 *
 * (EA) <- 0 Store the value zero in the memory location pointed to by the
 * effective address.
 *
 * @par Instruction
 * Opcode 000000 octal, mask 1111_1000_0000_0000.
 * Category: Memory Transfer - Store Instruction. Privilege: User.
 * Format: STZ <addressing_mode> <displacement>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section STZ
 */
static void opcode_stz_store_zero(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);
    cpu_memory_write(0, gEA, gUseAPT, 2);
}

/**
 * @brief STA - Store A register to memory location .
 *
 * (EA) <- (A) Store the contents of the A register in the memory location
 * pointed to by the effective address.
 *
 * @par Instruction
 * Opcode 004000 octal, mask 1111_1000_0000_0000.
 * Category: Memory Transfer - Store Instruction. Privilege: User.
 * Format: STA <addressing_mode> <displacement>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section STA
 */
static void opcode_sta_store_a_register(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);

    cpu_memory_write(gA, gEA, gUseAPT, 2);
}

/**
 * @brief STT - Store T register to memory location.
 *
 * (EA) <- (T) Store the contents of the T register in the memory location
 * pointed to by the effective address.
 *
 * @par Instruction
 * Opcode 010000 octal, mask 1111_1000_0000_0000.
 * Category: Memory Transfer - Store Instruction. Privilege: User.
 * Format: STT <addressing_mode> <displacement>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section STT
 */
static void opcode_stt_store_t_register(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);
    cpu_memory_write(gT, gEA, gUseAPT, 2);
}

/**
 * @brief STX - Store X register to memory location .
 *
 * (EA) <- (X) Store the contents of the X register in the memory location
 * pointed to by the effective address.
 *
 * @par Instruction
 * Opcode 014000 octal, mask 1111_1000_0000_0000.
 * Category: Memory Transfer - Store Instruction. Privilege: User.
 * Format: STX <addressing_mode> <displacement>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section STX
 */
static void opcode_stx_store_x_register(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);
    cpu_memory_write(gX, gEA, gUseAPT, 2);
}

/**
 * @brief MIN - Memory increment and skip next instruction if zero (EA): = (EA) + 1.
 *
 * (ea) <- (ea) + 1 (P)  <- (P) + 2 IF new (ea) = 0 The contents of the
 * memory location pointed to by the effective address are incremented by
 * one.  If the new memory location when incremented becomes zero, the next
 * instruction is skipped.
 *
 * @par Instruction
 * Opcode 040000 octal, mask 1111_1000_0000_0000.
 * Category: Memory Transfer - Store Instruction. Privilege: User.
 * Format: MIN <addressing_mode> <displacement>
 *
 * @par Registers affected
 * (EL), (P)
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section MIN
 */
static void opcode_min_memory_increment_and_skip_if_zero(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);

    uint16_t temp = cpu_memory_read(gEA, gUseAPT);
    temp++;
    cpu_memory_write(temp, gEA, gUseAPT, 2);

    if (temp == 0)
    {
        gPC++; // Next instruction is skipped
    }
}


/* ----------------------------------------------- Memory transfer - general */

/**
 * @brief LDATX - Load A register.
 *
 * (A) <- (ea) Load the contents of the physical memory location pointed to
 * by the effective address into the A register.
 *
 * @par Instruction
 * Opcode 143300 octal, mask 1111_1111_1100_0111.
 * Category: Memory Transfer Instructions. Privilege: Privileged.
 * Format: LDATX <disp>
 *
 * @par Registers affected
 * (A)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LDATX
 */
static void opcode_ldatx_load_a_register_t_x_relative(uint16_t operand)
{
    if (!check_priv())
    {
        return;
    }

    uint8_t displacement = (operand >> 3) & 0x07;

    unsigned int el = calc_el(displacement);
    gA = read_el(el);
}

/**
 * @brief LDXTX - Load X register.
 *
 * (X) <- (ea) Load the contents of the physical memory location pointed to
 * by the effective address into the X register.
 *
 * @par Instruction
 * Opcode 143301 octal, mask 1111_1111_1100_0111.
 * Category: Memory Transfer Instructions. Privilege: Privileged.
 * Format: LDXTX <displacement>
 *
 * @par Registers affected
 * (X)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LDXTX
 */
static void opcode_ldxtx_load_x_register_t_x_relative(uint16_t operand)
{
    if (!check_priv())
    {
        return;
    }

    uint8_t displacement = (operand >> 3) & 0x07;
    unsigned int el = calc_el(displacement);

    gX = read_el(el);
}

/**
 * @brief LDDTX - Load double word.
 *
 * (A) <- (ea) (D) <- (ea + 1) Load the contents of the physical memory
 * location pointed to by the effective address into the A register and the
 * contents of the effective address plus one into the D register.
 *
 * @par Instruction
 * Opcode 143302 octal, mask 1111_1111_1100_0111.
 * Category: Memory Transfer Instructions. Privilege: Privileged.
 * Format: LDDTX <displacement>
 *
 * @par Registers affected
 * (A,D)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LDDTX
 */
static void opcode_lddtx_load_double_word_t_x_relative(uint16_t operand)
{

    if (!check_priv())
    {
        return;
    }

    uint8_t displacement = (operand >> 3) & 0x07;
    unsigned int el = calc_el(displacement);

    gA = read_el(el);
    el++;
    gD = read_el(el);
}

/**
 * @brief LDBTX - Load B register.
 *
 * (B) <- 1770008 OR (2(ea)) Load the contents of the physical memory
 * location pointed to by twice the effective address contents into the B
 * register, then OR the value with 1770008. See description for usage.
 *
 * @par Instruction
 * Opcode 143303 octal, mask 1111_1111_1100_0111.
 * Category: Memory Transfer Instructions. Privilege: Privileged.
 * Format: LDBTX <displacement>
 *
 * @par Registers affected
 * (B)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LDBTX
 */
static void opcode_ldbtx_load_b_register_t_x_relative(uint16_t operand)
{
    uint16_t temp;
    unsigned int result;

    if (!check_priv())
    {
        return;
    }

    uint8_t displacement = (operand >> 3) & 0x07;
    unsigned int el = calc_el(displacement);

    temp = read_el(el);
    result = (temp + temp) & 0xFFFF;
    gB = result | 0xFE00; // 0177000
}

/**
 * @brief STATX - Store A register.
 *
 * (ea) <- (A) Store the contents of the A register in the memory location
 * given by the effective address.
 *
 * @par Instruction
 * Opcode 143304 octal, mask 1111_1111_1100_0111.
 * Category: Memory Transfer Instructions. Privilege: Privileged.
 * Format: STATX <displacement>
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section STATX
 */
static void opcode_statx_store_a_register_t_x_relative(uint16_t operand)
{
    if (!check_priv())
    {
        return;
    }

    uint8_t displacement = (operand >> 3) & 0x07;
    uint32_t el = calc_el(displacement);
    write_el(el, gA);
}

/**
 * @brief STZTX - Store zero.
 *
 * (ea) <- 0000008 Store zero in the memory location given by the effective
 * address.
 *
 * @par Instruction
 * Opcode 143305 octal, mask 1111_1111_1100_0111.
 * Category: Memory Transfer Instructions. Privilege: Privileged.
 * Format: STZTX <displacement>
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section STZTX
 */
static void opcode_stztx_store_zero_t_x_relative(uint16_t operand)
{
    if (!check_priv())
    {
        return;
    }

    uint8_t displacement = (operand >> 3) & 0x07;
    uint32_t el = calc_el(displacement);
    write_el(el, 0);
}

/**
 * @brief STDTX - Store double word.
 *
 * (ea) <- (A) (ea) + 1 <- (D) Store the double word held in the A and D
 * registers in the memory locations given by the effective address and the
 * effective address plus one.
 *
 * @par Instruction
 * Opcode 143306 octal, mask 1111_1111_1100_0111.
 * Category: Memory Transfer Instructions. Privilege: Privileged.
 * Format: STDTX <displacement>
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section STDTX
 */
static void opcode_stdtx_store_double_word_t_x_relative(uint16_t operand)
{
    if (!check_priv())
    {
        return;
    }

    uint8_t displacement = (operand >> 3) & 0x07;
    uint32_t el = calc_el(displacement);
    write_el(el, gA);
    write_el(el + 1, gD);
}


/* -------------------------------------------------- Arithmetic and logical */

/**
 * @brief ADD - Add to A register.
 *
 * A <- A + (EL) Add the contents of the memory location pointed to by the
 * effective address to the A register, leaving the result in A.
 *
 * @par Instruction
 * Opcode 060000 octal, mask 1111_1000_0000_0000.
 * Category: Arithmetic and Logical. Privilege: User.
 * Format: ADD <addressing_mode> <displacement>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section ADD
 */
static void opcode_add_add_to_a_register(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);

    uint16_t eff_word = cpu_memory_read(gEA, gUseAPT);
    gA = do_add(gA, eff_word, 0);
}

/**
 * @brief SUB - Subtract from A register.
 *
 * A <- A - (EL) Subtract the contents of the memory location pointed to by
 * the effective address from the A register, leaving the result in A.
 *
 * @par Instruction
 * Opcode 064000 octal, mask 1111_1000_0000_0000.
 * Category: Arithmetic and Logical. Privilege: User.
 * Format: SUB <addressing_mode> <displacement>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section SUB
 */
static void opcode_sub_subtract_from_a_register(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);
    uint16_t eff_word = cpu_memory_read(gEA, gUseAPT);
    gA = do_add(gA, ~eff_word, 1);
}

/**
 * @brief AND - Logical AND to A register.
 *
 * A <- A & (EA) Perform a bitwise AND operation between the contents of the
 * A register and the contents of the memory location pointed to by the
 * effective address, leaving the result in A.
 *
 * @par Instruction
 * Opcode 070000 octal, mask 1111_1000_0000_0000.
 * Category: Arithmetic and Logical. Privilege: User.
 * Format: AND <addressing_mode> <displacement>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section AND
 */
static void opcode_and_logical_and_to_a_register(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);
    gA = gA & cpu_memory_read(gEA, gUseAPT);
}

/**
 * @brief ORA - Logical inclusive OR to A register.
 *
 * A <- A | (EA) Perform a bitwise OR operation between the contents of the A
 * register and the contents of the memory location pointed to by the
 * effective address, leaving the result in A.
 *
 * @par Instruction
 * Opcode 074000 octal, mask 1111_1000_0000_0000.
 * Category: Arithmetic and Logical. Privilege: User.
 * Format: ORA <addressing_mode> <displacement>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section ORA
 */
static void opcode_ora_logical_or_to_a_register(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);
    gA = gA | cpu_memory_read(gEA, gUseAPT);
}


/* ---------------------------------------------------------- Floating point */

/**
 * @brief STF - Store floating accumulator (TAD) to memory (ea).
 *
 * Memory format:  (ea)     <- (T)  (ea) + 1 <- (A)  (ea) + 2 <- (D) Store
 * the contents of the floating accumulator (T,A and D registers) into the
 * memory location pointed to by the effective address.
 *
 * @par Instruction
 * Opcode 030000 octal, mask 1111_1000_0000_0000.
 * Category: Standard Floating Instructions. Privilege: User.
 * Format: STF <addressing_mode> <disp>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section STF
 */
static void opcode_stf_store_floating_accumulator(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);
    cpu_memory_write(gT, gEA + 0, gUseAPT, 2);
    cpu_memory_write(gA, gEA + 1, gUseAPT, 2);
    cpu_memory_write(gD, gEA + 2, gUseAPT, 2);
}

/**
 * @brief LDF - Load floating accumulator (TAD) from memory (FW).
 *
 * Memory format:  (T) <- EL  (A) <- EL+1  (D) <- EL+2 Load the contents of
 * the memory location pointed to by the effective address into the T
 * register, the contents of the effective address plus one into the A
 * register and the contents of the effective address plus two into the D
 * register.
 *
 * @par Instruction
 * Opcode 034000 octal, mask 1111_1000_0000_0000.
 * Category: Standard Floating Instructions. Privilege: User.
 * Format: LDF <addressing_mode> <disp>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LDF
 */
static void opcode_ldf_load_floating_accumulator(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);

    gT = cpu_memory_read(gEA + 0, gUseAPT);
    gA = cpu_memory_read(gEA + 1, gUseAPT);
    gD = cpu_memory_read(gEA + 2, gUseAPT);
}

/**
 * @brief FAD - Add to floating point accumulator.
 *
 * (A) <- (ea)     + (T) (D) <- (ea + 1) + (A) The contents of two sequential
 * memory locations, pointed to by the effective address, are added to the
 * contents of the floating point accumulator (T and A registers). The result
 * is held in the accumulator.
 *
 * @par Instruction
 * Opcode 100000 octal, mask 1111_1000_0000_0000.
 * Category: Standard Floating Instructions. Privilege: User.
 * Format: FAD <addressing_mode> <disp>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section FAD
 */
static void opcode_fad_add_to_floating_accumulator(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);

    if (g_current_fpp_type == FPP48)
    {
        uint16_t a[3], b[3], r[3];

        a[0] = gT;
        a[1] = gA;
        a[2] = gD;
        b[0] = cpu_memory_read(gEA + 0, gUseAPT);
        b[1] = cpu_memory_read(gEA + 1, gUseAPT);
        b[2] = cpu_memory_read(gEA + 2, gUseAPT);
        float_add(a, b, r);
        gT = r[0];
        gA = r[1];
        gD = r[2];
    }
    else
    {
        uint16_t a[2], b[2], r[2];

        a[0] = gA;
        a[1] = gD;
        b[0] = cpu_memory_read(gEA + 0, gUseAPT); /* only TWO words */
        b[1] = cpu_memory_read(gEA + 1, gUseAPT);
        float_add_32(a, b, r);
        gA = r[0];
        gD = r[1]; /* gT untouched */
    }
}

/**
 * @brief FSB - Subtract from floating point accumulator.
 *
 * The contents of two sequential memory locations, pointed to by the
 * effective address, are subtracted from the contents of the floating point
 * accumulator (A and D registers).  The result is held in the accumulator.
 *
 * @par Instruction
 * Opcode 104000 octal, mask 1111_1000_0000_0000.
 * Category: Standard Floating Instructions. Privilege: User.
 * Format: FSB <addressing_mode> <disp>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section FSB
 */
static void opcode_fsb_subtract_from_floating_accumulator(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);

    if (g_current_fpp_type == FPP48)
    {
        uint16_t a[3], b[3], r[3];

        a[0] = gT;
        a[1] = gA;
        a[2] = gD;
        b[0] = cpu_memory_read(gEA + 0, gUseAPT);
        b[1] = cpu_memory_read(gEA + 1, gUseAPT);
        b[2] = cpu_memory_read(gEA + 2, gUseAPT);
        float_sub(a, b, r);
        gT = r[0];
        gA = r[1];
        gD = r[2];
    }
    else
    {
        uint16_t a[2], b[2], r[2];

        a[0] = gA;
        a[1] = gD;
        b[0] = cpu_memory_read(gEA + 0, gUseAPT); /* only TWO words */
        b[1] = cpu_memory_read(gEA + 1, gUseAPT);
        float_sub_32(a, b, r);
        gA = r[0];
        gD = r[1]; /* gT untouched */
    }
}

/**
 * @brief FMU - Multiply floating point accumulator.
 *
 * The contents of the floating point accumulator (A and D registers) are
 * multiplied by the contents of two sequential memory locations, pointed to
 * by the effective address.  The result is held in the accumulator.
 *
 * @par Instruction
 * Opcode 110000 octal, mask 1111_1000_0000_0000.
 * Category: Standard Floating Instructions. Privilege: User.
 * Format: FMU <addressing_mode> <disp>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section FMU
 */
static void opcode_fmu_multiply_floating_accumulator(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);

    if (g_current_fpp_type == FPP48)
    {
        uint16_t a[3], b[3], r[3];

        a[0] = gT;
        a[1] = gA;
        a[2] = gD;
        b[0] = cpu_memory_read(gEA + 0, gUseAPT);
        b[1] = cpu_memory_read(gEA + 1, gUseAPT);
        b[2] = cpu_memory_read(gEA + 2, gUseAPT);
        float_mul(a, b, r);
        gT = r[0];
        gA = r[1];
        gD = r[2];
    }
    else
    {
        uint16_t a[2], b[2], r[2];

        a[0] = gA;
        a[1] = gD;
        b[0] = cpu_memory_read(gEA + 0, gUseAPT); /* only TWO words */
        b[1] = cpu_memory_read(gEA + 1, gUseAPT);
        float_mul_32(a, b, r);
        gA = r[0];
        gD = r[1]; /* gT untouched */
    }
}

/**
 * @brief FDV - Divide floating point accumulator.
 *
 * The contents of the floating point accumulator (A and D registers) are
 * divided by the contents of two sequential memory locations, pointed to by
 * the effective address.
 *
 * @par Instruction
 * Opcode 114000 octal, mask 1111_1000_0000_0000.
 * Category: Standard Floating Instructions. Privilege: User.
 * Format: FDV <addressing_mode> <disp>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section FDV
 */
static void opcode_fdv_divide_floating_accumulator(uint16_t operand)
{
    gEA = cpu_get_effective_addr(operand, &gUseAPT);

    if (g_current_fpp_type == FPP48)
    {
        uint16_t a[3], b[3], r[3];

        a[0] = gT;
        a[1] = gA;
        a[2] = gD;
        b[0] = cpu_memory_read(gEA + 0, gUseAPT);
        b[1] = cpu_memory_read(gEA + 1, gUseAPT);
        b[2] = cpu_memory_read(gEA + 2, gUseAPT);
        if (float_div(a, b, r))
        {
            /* Division by zero - set error indicator Z */
            cpu_setbit(_STS, STS_ERROR_INDICATOR, 1);
        }
        gT = r[0];
        gA = r[1];
        gD = r[2];
    }
    else
    {
        uint16_t a[2], b[2], r[2];

        a[0] = gA;
        a[1] = gD;
        b[0] = cpu_memory_read(gEA + 0, gUseAPT); /* only TWO words */
        b[1] = cpu_memory_read(gEA + 1, gUseAPT);
        if (float_div_32(a, b, r))
        {
            /* Division by zero - set error indicator Z */
            cpu_setbit(_STS, STS_ERROR_INDICATOR, 1);
        }
        gA = r[0];
        gD = r[1]; /* gT untouched */
    }
}

/**
 * @brief NLZ - Normalize.
 *
 * Convert the number in A to a floating number in TAD
 *
 * @par Instruction
 * Opcode 151400 octal, mask 1111_1111_0000_0000.
 * Category: Floating Conversion (Standard Format). Privilege: User.
 * Format: NLZ <scaling_factor>
 *
 * @par Instruction word
 *   bits 15-8  opcode - The opcode for floating conversion
 *   bits 7-0  scaling_factor - Scaling factor in range -128 to 127 (gives
 *       converting range from 10^-39 to 10^39)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section NLZ
 */
static void opcode_nlz_normalize_floating_accumulator(uint16_t operand)
{
    if (g_current_fpp_type == FPP48)
    {
        float_do_nlz(operand & 0xFF);
    }
    else
    {
        float_do_nlz32(operand & 0xFF); /* 32-bit FPP: gT is not touched */
    }
}

/**
 * @brief DNZ - Denormalise     .
 *
 * Convert the floating number in TAD to a fixed point number in A
 *
 * @par Instruction
 * Opcode 152000 octal, mask 1111_1111_0000_0000.
 * Category: Floating Conversion (Standard Format). Privilege: User.
 * Format: DNZ <scaling_factor>
 *
 * @par Instruction word
 *   bits 15-8  opcode - The opcode for floating conversion
 *   bits 7-0  scaling_factor - Scaling factor in range -128 to 127 (gives
 *       converting range from 10^-39 to 10^39)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section DNZ
 */
static void opcode_dnz_denormalize_to_fixed_point(uint16_t operand)
{
    if (g_current_fpp_type == FPP48)
    {
        float_do_dnz(operand & 0xFF);
    }
    else
    {
        float_do_dnz32(operand & 0xFF); /* 32-bit FPP: gT is not touched */
    }
}


/* ----------------------------------------------------- Byte and word block */

/**
 * @brief BFILL - Byte fill.
 *
 * Only the destination is used as an operand in this instruction (it is
 * placed in the X and T registers). The lower byte of the A register is then
 * filled with the destination field. After execution, bit 15 of the T
 * register points to the end of the field (after the last byte position) and
 * the field length equals zero.
 *
 * @par Instruction
 * Opcode 140130 octal, mask 1111_1111_1111_1111.
 * Category: Byte Instructions. Privilege: User.
 * Format: BFILL
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section BFILL
 */
static void opcode_bfill_byte_fill(uint16_t operand)
{
    (void)operand;
    uint16_t d1, len, addr, i;
    uint16_t right = (gT & ((uint16_t)1 << 15)) ? 1 : 0;     /* Start with right byte? (LSB) */
    bool is_apt = (gT & ((uint16_t)1 << 14)) ? true : false; /* Use APT or not? */
    uint16_t thebyte = gA & 0xff;
    len = gT & 0x0fff; /* Number of bytes to do */
    addr = gX;         /* just in case we do 0 bytes */
    d1 = gX;

    for (i = 0; i < len; i++)
    {
        addr = d1 + ((i + right) >> 1); /* Word adress of byte to write */
        cpu_memory_write(thebyte, addr, is_apt, ((i + right) & 1));
    }
    gT &= 0x7000;                  /* Null number of bytes, as per manual, also null bit 15 */
    gT |= ((i + right) & 1) << 15; /* set bit 15 to point to next free byte */
    gX = d1 + ((i + right) >> 1);


    gPC++; /* This function has a SKIP return on no error, which is always? */
}

/**
 * @brief MOVB - Move byte.
 *
 * This instruction moves a block of bytes from the memory location addressed
 * by the source operand to that of the memory location addressed by the
 * destination operand. After execution, bit 15 of the D and T registers
 * point to the end of the field that has been moved. The field length of the
 * D register (source) equals zero and the T register (destination) field
 * length is equal to the number of bytes moved.
 *
 * @par Instruction
 * Opcode 140131 octal, mask 1111_1111_1111_1111.
 * Category: Byte Instructions. Privilege: User.
 * Format: MOVB
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section MOVB
 */
static void opcode_movb_move_byte(uint16_t instr)
{
    (void)instr;
    uint16_t source, dest, lens, lend, len, s_lr, d_lr, s_apt, d_apt;
    int dir; /* direction, 0=low to high, 1 = high to low */
    int i;
    uint16_t thebyte;
    uint16_t addr_d, addr_s;

    addr_d = 0;
    addr_s = 0;
    dir = 0;
    source = gA;
    dest = gX;
    lens = gD & 0x0fff;
    lend = gT & 0x0fff;
    s_lr = ((gD >> 15) & 1);
    d_lr = ((gT >> 15) & 1);
    s_apt = ((gD >> 14) & 1);
    d_apt = ((gT >> 14) & 1);
    len = (((int)lens - lend) < 0) ? lens : lend; /* get smallest length as number to copy */
    /* Check overlap if any and direction to copy */
    if (((int)source - dest) < 0)
    {
        dir = 1;
    }
    else if (((int)source - dest) == 0)
    { /* :TODO: check bytes to determine direction, or if no need to copy exist */
    }
    else
    {
        dir = 0;
    }

    /* COPY */
    if (dir)
    { /* high to low */
        for (i = len - 1; i >= 0; i--)
        {
            addr_s = source + ((i + s_lr) >> 1); /* Word adress of byte to read */
            thebyte = cpu_memory_read(addr_s, s_apt);
            thebyte =
                ((i + d_lr) & 1) ? thebyte : (thebyte >> 8) & 0xff; /* right, LSB : left, MSB */
            addr_d = dest + ((i + d_lr) >> 1); /* Word adress of byte to write */
            cpu_memory_write(thebyte, addr_d, d_apt, ((i + d_lr) & 1));
        }
        /* NOTE: resetting i to 0 here used to leak into the end-state "next free byte"
         * parity below. That was WRONG - see the end_half computation after the loop,
         * which no longer uses i. The reset is kept because i is the loop cursor only.
         */
        i = 0;
    }
    else
    { /* low to high */
        for (i = 0; i < len; i++)
        {
            addr_s = source + ((i + s_lr) >> 1); /* Word adress of byte to read */
            thebyte = cpu_memory_read(addr_s, s_apt);
            thebyte =
                ((i + d_lr) & 1) ? thebyte : (thebyte >> 8) & 0xff; /* right, LSB : left, MSB */
            addr_d = dest + ((i + d_lr) >> 1); /* Word adress of byte to write */
            cpu_memory_write(thebyte, addr_d, d_apt, ((i + d_lr) & 1));
        }
    }

    /* MICROCODE-VALIDATED 2026-07-20: the end-state byte-half parity is (len + d_lr) & 1,
     * NOT (i + d_lr) & 1.
     *
     * The manual is explicit - "After execution, bit 15 of the D and T registers point to
     * the end of the field that has been moved" (nd100-markdown cpu_documentation.md:5481).
     * "End of the field" = the byte AFTER the last one written, so its half is the start
     * half advanced by the number of bytes moved: (len + d_lr) & 1.
     *
     * The descending (dir != 0, source < dest) branch above resets i to 0, so the old
     * "(i + d_lr) & 1" evaluated the START half instead of the END half whenever the move
     * ran high-to-low. It only shows up for ODD byte counts (an even count leaves the
     * parity unchanged, which is why len=2 vectors always passed and len=3 always failed).
     *
     * Live RASK oracle, source 01500 -> dest 01540 (descending), destination word 0360:
     *   len=3 half=L -> D=8000 T=8003 X=0361   (parity 1 = (3+0)&1)
     *   len=3 half=R -> D=0000 T=0003 X=0362   (parity 0 = (3+1)&1)
     * Both are self-consistent with the bytes actually written (the next free byte really
     * is 0361-right / 0362-left), and reproduce bit-for-bit across runs. The ASCENDING
     * branch is unaffected: there i ends at len, so (i + d_lr) == (len + d_lr) already.
     */
    int end_half = (len + d_lr) & 1;

    gD &= 0x7000;         /* Null number of bytes, as per manual, also null bit 15 */
    gT &= 0x7000;         /* Null number of bytes, also null bit 15 */
    gD |= end_half << 15; /* set bit 15 to point to next free byte */
    gT |= end_half << 15; /* set bit 15 to point to next free byte */
    gT |= len & 0x0fff;   /* number of bytes done to lowest 12 bits*/

    gA = addr_s + ((len + s_lr) >> 1);
    gX = addr_d + ((len + d_lr) >> 1);

    gPC++; /* This function has a SKIP return on no error, which is always? */
}

/**
 * @brief MOVBF - Move bytes forward.
 *
 * This instruction moves a block of bytes from the memory location addressed
 * by the source operand to that of the memory location addressed by the
 * destination operand. After execution, bit 15 of the D and T registers
 * point to the end of the field that has been moved. The field length of the
 * D register (source) equals zero and the T register (destination) field
 * length is equal to the number of bytes moved.
 *
 * @par Instruction
 * Opcode 140132 octal, mask 1111_1111_1111_1111.
 * Category: Byte Instructions. Privilege: User.
 * Format: MOVBF
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section MOVBF
 */
static void opcode_movbf_move_bytes_forward(uint16_t instr)
{
    (void)instr;
    uint16_t source, dest, lens, lend, len, s_lr, d_lr, s_apt, d_apt;
    int i;
    uint16_t thebyte;
    uint16_t addr_d, addr_s;
    source = gA;
    dest = gX;
    bool overlap;

    addr_d = 0;
    addr_s = 0;
    lens = gD & 0x0fff;
    lend = gT & 0x0fff;
    s_lr = ((gD >> 15) & 1);
    d_lr = ((gT >> 15) & 1);
    s_apt = ((gD >> 14) & 1);
    d_apt = ((gT >> 14) & 1);

    len = (((int)lens - lend) < 0) ? lens : lend; /* get smallest length as number to copy */

    if (source > dest)
    {
        overlap = false;
    }
    else if ((uint16_t)((uint16_t)(ceil(len / 2)) + source - 1) > dest)
    {
        overlap = true;
    }
    else
    {
        overlap = false;
    }

    for (i = 0; i < len; i++)
    {
        addr_s = source + ((i + s_lr) >> 1); /* Word adress of byte to read */
        thebyte = cpu_memory_read(addr_s, s_apt);
        thebyte = ((i + d_lr) & 1) ? thebyte : (thebyte >> 8) & 0xff; /* right, LSB : left, MSB */
        addr_d = dest + ((i + d_lr) >> 1); /* Word adress of byte to write */
        cpu_memory_write(thebyte, addr_d, d_apt, ((i + d_lr) & 1));
        lens--;
        lend--;
    }

    gA = source + ((len + s_lr) >> 1);
    gX = dest + ((len + d_lr) >> 1);

    /* MICROCODE-VALIDATED 2026-07-20: bit 15 must be ASSIGNED the end-of-field parity, not
     * OR-ed on top of the start half.
     *
     * Manual: "After execution, bit 15 of the D and T registers point to the end of the
     * field that has been moved" (nd100-markdown cpu_documentation.md:5526). The masks
     * below (0xEFFF / 0xCFFF, and the later 0xF000) all PRESERVE bit 15, so the old
     * "|= parity << 15" could only ever SET it - a descriptor that started on the right
     * byte (bit 15 = 1) could never come back pointing at a left byte. It therefore only
     * diverged when the parity had to flip back to 0 (odd length starting on the right).
     *
     * Live RASK oracle, source 01500 -> dest 01540, destination word 0360:
     *   len=2 half=L -> D=0000 T=0000 X=0361   len=2 half=R -> D=8000 T=8000 X=0361
     *   len=3 half=L -> D=8000 T=8000 X=0361   len=3 half=R -> D=0000 T=0000 X=0362
     * i.e. exactly (len + d_lr) & 1 in all four cases (i == len here, the loop is always
     * ascending, so (i + d_lr) is already the right parity - only the CLEAR was missing).
     */
    int end_half = (i + d_lr) & 1;

    gD &= 0xefff;         /* Null bit 12 */
    gT &= 0xcfff;         /* Null bit 12 & 13 */
    gD &= 0x7fff;         /* Null bit 15 before assigning the end-of-field half */
    gT &= 0x7fff;         /* Null bit 15 before assigning the end-of-field half */
    gD |= end_half << 15; /* set bit 15 to point to next free byte */
    gT |= end_half << 15; /* set bit 15 to point to next free byte */

    gD &= 0xf000;        /* clean lowest bits before or */
    gT &= 0xf000;        /* clean lowest bits before or */
    gD |= lens & 0x0fff; /* decremented byte counter to lowest 12 bits*/
    gT |= lend & 0x0fff; /* decremented byte counter to lowest 12 bits*/

    if (!overlap)
    {
        gPC++; /* This function has a SKIP return on no error */
    }

    return;
}

/**
 * @brief LBYT - Load byte from memory to A register.
 *
 * Addressing: EL = (T) + (X)/2 - If least significant bit of X = 1: Load
 * right byte - If least significant bit of X = 0: Load left byte Load the
 * byte addressed by the contents of the T and X register into the lower byte
 * of the A register. The higher byte of the A register is cleared. The
 * contents of T point to the beginning of a character string and the
 * contents of X to a byte within the string.
 *
 * @par Instruction
 * Opcode 142200 octal, mask 1111_1111_1111_1111.
 * Category: Byte Instructions. Privilege: User.
 * Format: LBYT
 *
 * @par Registers affected
 * (A)
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LBYT
 */
static void opcode_lbyt_load_byte_to_a_register(uint16_t operand)
{
    (void)operand;

    uint16_t offset = gX >> 1;
    uint16_t memval = cpu_memory_read(gT + offset, true);

    if ((gX & 1) != 0)
    { /* ODD BYTE = LOW */
        gA = memval & 0xFF;
    }
    else
    {
        /* EVEN BYTE = HIGH*/
        gA = (memval >> 8) & 0xFF;
    }
}

/**
 * @brief SBYT - Store byte from A register to memory.
 *
 * Addressing: EL = (T) + (X)/2 - If least significant bit of X = 1: Store
 * right byte - If least significant bit of X = 0: Store left byte The
 * contents of T point to the beginning of a character string and the
 * contents of X to a byte within the string.
 *
 * @par Instruction
 * Opcode 142600 octal, mask 1111_1111_1111_1111.
 * Category: Byte Instructions. Privilege: User.
 * Format: SBYT
 *
 * @par Registers affected
 * (EL)
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section SBYT
 */
static void opcode_sbyt_store_byte_from_a_register(uint16_t operand)
{
    (void)operand;

    uint16_t offset = gX >> 1; /* same as divide by 2 */

    if ((gX & 1) != 0)

    {
        // Odd byte, write LSB value
        mms_write_virtual_memory((uint32_t)(gT + offset), gA, true, WRITEMODE_LSB);
    }
    else
    {
        // Even byte, write MSB value
        mms_write_virtual_memory((uint32_t)(gT + offset), gA, true, WRITEMODE_MSB);
    }
}

/**
 * @brief BFILL_NEW - see the notes below.
 *
 * @param operand The full instruction word as fetched.
 */
void opcode_bfill_new_byte_fill(uint16_t operand)
{
    (void)operand;
    bool use_apt = false;
    WriteMode wm;

    // Check if we should use alternative page table, bit 14 in T register
    if ((gT & (1 << 14)) != 0)
    {
        use_apt = true;
    }

    while ((gT & 0xfff) != 0)
    {
        // Bit 15:  0=>MSB, 1=> LSB
        wm = (gT & (1 << 15)) ? WRITEMODE_LSB : WRITEMODE_MSB;
        mms_write_virtual_memory(gX, gA & 0xFF, use_apt, wm);

        gT--;

        gT ^= (1 << 15); // Flip T bit 15
        if ((gT & (1 << 15)) == 0)
        {
            gX++;
        }
    }

    gPC++; // Skip return
}


/* ------------------------------------------------------------------- Shift */

/**
 * @brief SHIFTS - see the notes below.
 *
 * @param operand The full instruction word as fetched.
 */
static void opcode_shift_group(uint16_t operand)
{
    uint32_t double_reg;

    switch ((operand >> 7) & 0x03)
    {
    case 0: /* SHT */
        gT = shift_reg(gT, operand);
        break;
    case 1: /* SHD */
        gD = shift_reg(gD, operand);
        break;
    case 2: /* SHA */
        gA = shift_reg(gA, operand);
        break;
    case 3: /* SAD */
        double_reg = shift_double_reg(((uint32_t)gA << 16) | gD, operand);
        gA = double_reg >> 16;
        gD = double_reg & 0xFFFF;
        break;
    default: /* can never reach here but... */
        break;
    }
}


/* -------------------------------------------------------------- Sequencing */

/**
 * @brief JMP - Jump - Unconditional jump to specified address.
 *
 * The next instruction is taken from the effective address of the JMP
 * instruction(the effective address is ioaded into the program counter).
 *
 * @par Instruction
 * Opcode 124000 octal.
 * Category: Sequencing Instructions. Privilege: User.
 * Format: JMP <address mode> <displacement>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section JMP
 */
static void opcode_jmp_jump_unconditional(uint16_t operand)
{
    uint16_t old_g_pc = gPC - 1;

    gEA = cpu_get_effective_addr(operand, &gUseAPT);
    gPC = gEA;

    if (g_disasm)
    {
        disasm_userel(old_g_pc, gPC);
    }
}

/**
 * @brief JAP - Condition: Jump if (A) > 0 (jump if A positive).
 *
 * @par Instruction
 * Opcode 130000 octal, mask 1111_1111_0000_0000.
 * Category: Sequencing Instructions. Privilege: User.
 * Format: JAP <displacement>
 *
 * @par Registers affected
 * (P)
 *
 * @par Instruction word
 *   bits 15-8  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section JAP
 */
static void opcode_jap_jump_if_a_positive(uint16_t operand)
{
    bool flag = ((1 << 15) & gA) == 0;
    cjp(flag, operand);
}

/**
 * @brief JAN - Condition: Jump if (A) < 0 (jump if A is negative).
 *
 * @par Instruction
 * Opcode 130400 octal, mask 1111_1111_0000_0000.
 * Category: Sequencing Instructions. Privilege: User.
 * Format: JAN <displacement>
 *
 * @par Registers affected
 * (P)
 *
 * @par Instruction word
 *   bits 15-8  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section JAN
 */
static void opcode_jan_jump_if_a_negative(uint16_t operand)
{
    bool flag = ((1 << 15) & gA) != 0;
    cjp(flag, operand);
}

/**
 * @brief JAZ - Condition: Jump if (A) == 0 (jump if A is zero).
 *
 * @par Instruction
 * Opcode 131000 octal, mask 1111_1111_0000_0000.
 * Category: Sequencing Instructions. Privilege: User.
 * Format: JAZ <displacement>
 *
 * @par Registers affected
 * (P)
 *
 * @par Instruction word
 *   bits 15-8  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section JAZ
 */
static void opcode_jaz_jump_if_a_zero(uint16_t operand)
{
    /* MICROCODE-VALIDATED 2026-07-20: JAZ does NOT touch STS.
     * RASK CS 007310-007313 (ND-110-RASK.LISTING.TXT:12259-12262) is
     *   "A,A ALUF,PASSA ALUD,NONE IDBS,LA COMM,CJMP,F=0 T,JMP T,HOLD CJP1"
     * - there is NO "STS,xx" token in the micro-word, and STS bits 0-7 are written only by
     * the STS,EA / STS,ES / STS,LO tokens. ALUD,NONE means the PASSA result is not even
     * latched. The same holds for every other cjp entry (JAP 007300, JAN 007304,
     * JAF 007314, JPC 007320, JNC 007324, JXZ 007330, JXN 007334).
     * The live RASK oracle confirms it: with A=0 and C seeded 0 the taken jump leaves C=0,
     * with C seeded 1 it leaves C=1 - C is simply PRESERVED.
     * The removed line ("setbit(_STS, STS_CARRY, gA == 0)") was a fabricated carry side effect.
     */
    cjp(gA == 0, operand);
}

/**
 * @brief JAF - Condition: Jump if (A) != 0 (jump if A filled).
 *
 * @par Instruction
 * Opcode 131400 octal, mask 1111_1111_0000_0000.
 * Category: Sequencing Instructions. Privilege: User.
 * Format: JAF <displacement>
 *
 * @par Registers affected
 * (P)
 *
 * @par Instruction word
 *   bits 15-8  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section JAF
 */
static void opcode_jaf_jump_if_a_not_zero(uint16_t operand)
{
    cjp(gA != 0, operand);
}

/**
 * @brief JPC - Increment X and jump if X is positive.
 *
 * @par Instruction
 * Opcode 132000 octal, mask 1111_1111_0000_0000.
 * Category: Sequencing Instructions. Privilege: User.
 * Format: JPC <displacement>
 *
 * @par Registers affected
 * (P) and (X)
 *
 * @par Instruction word
 *   bits 15-8  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section JPC
 */
static void opcode_jpc_increment_x_and_jump_if_x_positive(uint16_t operand)
{
    gX++;

    cjp(((1 << 15) & gX) == 0, operand);
}

/**
 * @brief JNC - Increment X and jump if X is negative.
 *
 * @par Instruction
 * Opcode 132400 octal, mask 1111_1111_0000_0000.
 * Category: Sequencing Instructions. Privilege: User.
 * Format: JNC <displacement>
 *
 * @par Registers affected
 * (P) and(X)
 *
 * @par Instruction word
 *   bits 15-8  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section JNC
 */
static void opcode_jnc_increment_x_and_jump_if_x_negative(uint16_t operand)
{
    gX++;
    cjp((gX & (1 << 15)) != 0, operand);
}

/**
 * @brief JXZ - Condition: Jump if (X) == 0 (jump if X is zero).
 *
 * @par Instruction
 * Opcode 133000 octal, mask 1111_1111_0000_0000.
 * Category: Sequencing Instructions. Privilege: User.
 * Format: JXZ <displacement>
 *
 * @par Registers affected
 * (P)
 *
 * @par Instruction word
 *   bits 15-8  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section JXZ
 */
static void opcode_jxz_jump_if_x_zero(uint16_t operand)
{
    cjp(gX == 0, operand);
}

/**
 * @brief JXN - Condition: Jump if (X) < 0 (jump if X negative).
 *
 * @par Instruction
 * Opcode 133400 octal, mask 1111_1111_0000_0000.
 * Category: Sequencing Instructions. Privilege: User.
 * Format: JXN <displacement>
 *
 * @par Registers affected
 * (P)
 *
 * @par Instruction word
 *   bits 15-8  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section JXN
 */
static void opcode_jxn_jump_if_x_negative(uint16_t operand)
{
    cjp((gX & (1 << 15)) != 0, operand);
}

/**
 * @brief JPL - Jump if Plus - Jump to specified address if the result of the last operation was positive (sign bit is 0).
 *
 * The contents of the program counter are transferred to the L register and
 * the next instruction is taken from the effective address of the JPL
 * instruction. Note that the L register points to the instruction after the
 * jump(the program counter incremented before transfer to the L register).
 *
 * @par Instruction
 * Opcode 134000 octal.
 * Category: Sequencing Instructions. Privilege: User.
 * Format: JPL <address mode> <displacement>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  addressing_mode - These three bits give the addressing mode
 *       for the instruction
 *   bits 7-0  displacement - 8-bit signed field gives the memory address
 *       displacement (2's complement notation giving a displacement range of
 *       -128 to 127 memory locations)
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section JPL
 */
static void opcode_jpl_jump_if_last_result_positive(uint16_t operand)
{
    uint16_t old_g_pc = gPC - 1;

    gEA = cpu_get_effective_addr(operand, &gUseAPT);

    /* MICROCODE-VALIDATED 2026-07-20: addressing mode 5 (",X ,B" - X=1 I=0 B=1, i.e.
     * (B)+disp+(X)) does NOT update L on real ND-110/ND-120 silicon.
     *
     * Seven of the eight JPL entries begin with "A,P B,L ALUF,PASSA ALUD,B", i.e. L := P
     * (RASK CS 007340/007344/007350/007354/007360/007370/007374 =
     *  ND-110-RASK.LISTING.TXT:12289, 12294, 12299, 12304, 12309, 12319, 12324).
     * The mode-5 entry, RASK CS 007364-007367 (:12314-12317), is instead
     *   "A,X B,B ALUF,A+B ALUD,NONE IDBS,GPR T,JMP T,HOLD JMPXB"
     * - it forms X+B and hands over to JMPXB (CS 000213, :433) which is
     *   "ALUD,NONE IDBS,LA COMM,JMP,XB T,JMP T,HOLD".
     * Neither micro-word writes L, and the sequence is BIT-IDENTICAL to plain JMP's mode-5
     * entry (CS 007264-007267, :12233-12236) - the two instructions literally share the
     * JMPXB tail, so JPL,X,B cannot save a link. The ND-120 DELILAH microcode agrees
     * verbatim (ND-120-DELILAH-L.LISTING.TXT:15417-15429 vs :15365 for mode 0).
     * Confirmed live against the RASK oracle: mode 5 leaves L at its seeded value while the
     * mode-4 control returns L = P+1.
     */
    if (((operand >> 8) & 0x07) != 5)
    {
        gL = gPC;
    }

    gPC = gEA;

    if (g_disasm)
    {
        disasm_userel(old_g_pc, gPC);
    }
}

/**
 * @brief SKP - The next instruction is skipped if a specified condition is true.
 *
 * @par Instruction
 * Opcode 140000 octal, mask 1111_1000_1100_0000.
 * Category: Skip Instruction. Privilege: User.
 * Format: SKP <dr> <condition> <sr>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 10-8  condition - Condition to skip the next
 *       instruction<br><br>**Values:**<br>- `EQL` (`0`): Equal<br>- `GEQ`
 *       (`1`): Greater or equal to (signed)<br>- `GRE` (`2`): Greater or equal
 *       to (overflow, signed)<br>- `MGRE` (`3`): Magnitude greater or equal to
 *       (overflow, unsigned)<br>- `UEQ` (`4`): Unequal<br>- `LSS` (`5`): Less
 *       than (overflow, unsigned)<br>- `LST` (`6`): Less than (overflow,
 *       signed)<br>- `MLST` (`7`): Magnitude less than (overflow,
 *       unsigned)<br>
 *   bits 7-6  zeros - Must be 00
 *   bits 5-3  sr - The source register to be compared with the destination
 *       register
 *   bits 2-0  dr - The destination register to be compared with the source
 *       register
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section SKP
 */
static void opcode_skp_skip_next_if_condition(uint16_t operand)
{
    if (is_skip(operand))
    {
        gPC++;
    }
}


/* ------------------------------------------------------------------- Stack */

/**
 * @brief INIT - Initialize stack. .
 *
 * Loads the addresses pointed to by B with the stack frame addresses.  Sets
 * up    LINK  <- L + 1                          {stack start}     PREVB <-
 * (B)                            {save current pointer}     SMAX  <- stack
 * start address + maximum stack size     (B)   <- (B = 2008) + 2008
 * {establish new pointer}     STP   <- stack demand + (B)   Load the
 * addresses pointed to by **B** with the stack frame addresses. Stack
 * overflow and flag error causes an error return, that is the program
 * continues at the address following the stack demand value.  In all other
 * cases, the program skips this address to find the return address from the
 * stack.   Format:   INIT    <number of words allocated to stack>   <address
 * of stack start>   <maximum stack size>   <flag>   <address left empty>
 * <error return address>   <return address>
 *
 * @par Instruction
 * Opcode 140134 octal, mask 1111_1111_1111_1111.
 * Category: Stack Operations. Privilege: User.
 * Format: INIT
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section INIT
 */
static void opcode_init_initialize_stack(uint16_t operand)
{
    (void)operand;
    uint16_t demand, start, maxsize, flag;

    demand = cpu_memory_read(gPC + 0, 0);
    start = cpu_memory_read(gPC + 1, 0);
    maxsize = cpu_memory_read(gPC + 2, 0);
    flag = cpu_memory_read(gPC + 3, 0);
    if ((start + 128 + demand - 122) > (start + maxsize))
    { /* stack overflow */
        gPC += 5;
        return;
    }
    if ((flag & 0x01) != (g_reg->reg[gPIL][_STS] & 0x01))
    {
        gPC += 5;
        return;
    }
    cpu_memory_write(gL + 1, start, 1, 2);              /* L+1 ==> LINK */
    cpu_memory_write(gB, start + 1, 1, 2);              /* B   ==> PREVB */
    cpu_memory_write(start + maxsize, start + 3, 1, 2); /* SMAX */
    gB = start + 128;                                   /* + 200 oct. */
    /*:TODO:  Flag */
    cpu_memory_write(gB + demand - 122, start + 2, 1, 2); /* STP */
    gPC += 6;
    return;
}

/**
 * @brief ENTR - Enter stack.
 *
 * This instruction saves the current stack pointer (B), the return address
 * (LINK), and previous stack pointer (PREVB). It transfers the top of stack
 * address (SMAX) and establishes the new stack demand and pointer.   (B =
 * 1778) <- (B)                {save current pointer in PREVB}   (B = 1758)
 * <- (B = 1758)         {SMAX}   (B = 2008) <- (L) + 1            {save
 * return address in LINK}   (B)        <- (B = 1768) + 2008   {new pointer}
 * (B = 1768) <- stack demand + (B) Stack overflow causes an error return,
 * that is the program continues at the address following the stack demand
 * value. In all other cases, the program skips this address to find the
 * return address from the stack. Format:   ENTR    <stack demand-value in
 * words>   <error return address>   <return address>
 *
 * @par Instruction
 * Opcode 140135 octal, mask 1111_1111_1111_1111.
 * Category: Stack Operations. Privilege: User.
 * Format: ENTR
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section ENTR
 */
static void opcode_entr_enter_stack(uint16_t operand)
{
    (void)operand;
    uint16_t old_b, demand, smax, stp;
    demand = cpu_memory_read(gPC + 0, 0);
    smax = cpu_memory_read(gB - 125, 1); /* SMAX */
    if ((gB + demand - 122) > (smax))
    { /* stack overflow */
        gPC += 1;
        return;
    }
    stp = cpu_memory_read(gB - 126, 1); /* STP */
    old_b = gB;
    gB = stp + 128;                                      /* Advance stack frame */
    cpu_memory_write(gL + 1, gB - 128, 1, 2);            /* L+1 ==> LINK */
    cpu_memory_write(old_b, gB - 127, 1, 2);             /* B   ==> PREVB */
    cpu_memory_write(smax, gB - 125, 1, 2);              /* SMAX */
    cpu_memory_write(gB + demand - 122, gB - 126, 1, 2); /* STP */
    gPC += 2;
}

/**
 * @brief LEAVE - Leave stack.
 *
 * This instruction saves the previous stack pointer in LINK.  The B register
 * is restored to its previous value (PREVB) and the stack is left by loading
 * the P register (program counter) with the return address (LINK).   (P) <-
 * (B = 2008)   {LINK}   (B) <- (B = 1778)   {PREVB} Format:   LEAVE
 *
 * @par Instruction
 * Opcode 140136 octal, mask 1111_1111_1111_1111.
 * Category: Stack Operations. Privilege: User.
 * Format: LEAVE
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LEAVE
 */
static void opcode_leave_leave_stack(uint16_t operand)
{
    (void)operand;
    gPC = cpu_memory_read(gB - 128, 1);
    gB = cpu_memory_read(gB - 127, 1);
}

/**
 * @brief ELEAV - Error leave stack.
 *
 * If an error occurs, leave the stack. This instruction saves the previous
 * stack pointer in LINK and restores the B register to its previous value
 * (PREVB) before leaving the stack. The stack is left by loading the P
 * register (program counter) with the return address (LINK). The A register
 * is loaded with an error code which is saved in the ERRCODE stack entry
 * (pointed to by B = 1738).   (B = 2008) <- (B = 2008) - 1   (P)       <- (B
 * = 2008)   {LINK}   (B)       <- (B = 1778)   {PREVB}   (A)       <-
 * ERRCODE8   (B = 1738) <- (A)         {ERRCODE} Format:   ELEAV
 *
 * @par Instruction
 * Opcode 140137 octal, mask 1111_1111_1111_1111.
 * Category: Stack Operations. Privilege: User.
 * Format: ELEAV
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section ELEAV
 */
static void opcode_eleav_error_leave_stack(uint16_t operand)
{
    (void)operand;
    uint16_t tmp;
    tmp = cpu_memory_read(gB - 128, 1) - 1;
    cpu_memory_write(tmp, gB - 128, 1, 2); /* LINK */
    cpu_memory_write(gA, gB - 123, 1, 2);  /* A ==> ERRCODE */
    gPC = cpu_memory_read(gB - 128, 1);
    gB = cpu_memory_read(gB - 127, 1);
}


/* -------------------------------------------------------- Input and output */

/**
 * @brief IOXT - Exchange information between I/O system and A register.
 *
 * @par Instruction
 * Opcode 150415 octal, mask 1111_1111_1111_1111.
 * Category: Input and Output. Privilege: Privileged.
 * Format: IOXT
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section IOXT
 */
static void opcode_ioxt_exchange_with_io_system_t_addressed(uint16_t operand)
{
    (void)operand;
    if (!check_priv())
    {
        return;
    }


    if (!update_memory_io())
    {
        gA = io_op(gT, gA);
    }
}

/**
 * @brief IOT - NORD-1 INSTRUCTION (DO NOT USE).
 *
 * @par Instruction
 * Opcode 160000 octal, mask 1111_1000_0000_0000.
 * Category: Input and Output. Privilege: Privileged.
 * Format: IOT <device_register_address>
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section IOT
 */
static void opcode_iot_nord_1_legacy_do_not_use(uint16_t operand)
{
    // ND110 Microcode:
    // IOT - INSTRUCTION IS PRIVILEGED WHEN RING = 0 OR 1
    //                  AND ILLEGAL    WHEN RING = 2 OR 3
    if (!check_priv())
    {
        return;
    }

    // IOT is the NORD-10 I/O-transfer instruction. Its low 11 bits are the same
    // device/function field as IOX (opcode 0160000 vs 0164000; both mask 0x07ff),
    // so route it through the identical device dispatch. NORD TSS's teletype
    // scanner (LEV6, TSS1.SYMB:2673) issues "IOT ACT DIABD+2/+3" every 80 ms to
    // poke the Diablo terminal (device 156); with no such device attached, io_op
    // raises the IOX-error interrupt (level 14, IIC 7 = EIOX), which TSS's own
    // LEV14 handler counts and ignores (TSS1.SYMB:4294). Treating IOT as an
    // illegal instruction instead (the old stub) trapped IIC 4 -> ILLS -> TRAP
    // and spun TSS in an infinite trap loop, blocking LOGON.
    if (update_memory_io())
    {
        return;
    }

    /* NORD-1 decoding: bits 0-7 device number, bits 8-10 ACT/SKA/PIN
     * (all zero = SNI). See NORD-1 Reference Manual sec 3.7 and the Device
     * struct comment. If a device claims this NORD-1 device number we use
     * that; SKA / "skip if OK" then skips the next instruction, which is what
     * the classic "IOT SKA DVN / JMP *-1" wait loop needs. */
    {
        uint8_t devno = (uint8_t)(operand & 0x00ff);
        uint8_t func = (uint8_t)((operand >> 8) & 0x07);
        uint16_t a = gA;
        bool skip = false;

        if (devmgr_iot_op(devno, func, &a, &skip))
        {
            gA = a;
            if (skip)
            {
                gPC++;
            }
            return;
        }
    }

    /* Nothing claims it: keep the long-standing behaviour of treating IOT
     * like IOX. TSS's teletype scanner poking a device that is not present
     * relies on getting the IOX-error interrupt here rather than an illegal
     * instruction trap (which used to spin it in a trap loop). */
    gA = io_op(operand & 0x07ff, gA);
}

/**
 * @brief IOX - Exchange information between I/O system and A register.
 *
 * @par Instruction
 * Opcode 164000 octal, mask 1111_1000_0000_0000.
 * Category: Input and Output. Privilege: Privileged.
 * Format: IOX <device_register_address>
 *
 * @par Instruction word
 *   bits 15-10  opcode - The opcode determines what type of operation
 *       occurs (fixed code 111012)
 *   bits 10-0  device_register_address - 11-bit field limiting the number
 *       of external devices that can be addressed by the CPU.<br>Bit 0 gives
 *       the direction of transfer:<br>  - 0: input (from device to CPU)<br>  -
 *       1: output (from CPU to device)<br>
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section IOX
 */
static void opcode_iox_exchange_with_io_system(uint16_t operand)
{
    if (!check_priv())
    {
        return;
    }

    if (!update_memory_io())
    {
        gA = io_op(operand & 0x07ff, gA);
    }
}


/* ------------------------------------------------------- Interrupt control */

/**
 * @brief IDENT - Transfer IDENT code of interrupting device with highest priority on the specified level to A register.
 *
 * @par Instruction
 * Opcode 143600 octal, mask 1111_1111_1100_0000.
 * Category: Interrupt Control Instructions. Privilege: User.
 * Format: IDENT <level_code>
 *
 * @par Instruction word
 *   bits 15-6  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 5-0  level_code - The interrupt level
 *       code<br><br>**Values:**<br>- `PL10` (`000004`): Level 10<br>- `PL11`
 *       (`000011`): Level 11<br>- `PL12` (`000022`): Level 12<br>- `PL13`
 *       (`000043`): Level 13<br>
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section IDENT
 */
static void opcode_ident_identify_interrupting_device(uint16_t operand)
{
    if (!check_priv())
    {
        return;
    }

    switch ((operand & 0x003f))
    {
    case 004:
        do_ident(10);
        break;
    case 011:
        do_ident(11);
        break;
    case 022:
        do_ident(12);
        break;
    case 043:
        do_ident(13);
        break;
    default:
        cpu_illegal_instr(operand); /* Assume this is how we should hanle it.. TODO: Check!!! */
    }
}

/**
 * @brief OPCOM - Operator Communication.
 *
 * This instruction is PRIVILEGED and only available to:   - programs running
 * in system mode (rings 2-3)   - programs running without memory protection
 * This instruction allows the programmer to use a terminal in direct
 * communication with the CPU board. When the CPU is running, MOPC can be
 * used to read input from the console. This is the software equivalent to
 * pressing the OPCOM button on the control panel of the ND-110.
 *
 * @par Instruction
 * Opcode 150400 octal, mask 1111_1111_1111_1111.
 * Category: Interrupt Control Instructions. Privilege: Privileged.
 * Format: OPCOM
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section OPCOM
 */
static void opcode_opcom_operator_communication(uint16_t operand)
{
    (void)operand;
    if (!check_priv())
    {
        return;
    }
    printf("\r\nOPCOM at PIL[%d] PC[%6o] A[%6o]\r\n", gPIL, gPC, gA);
    cpu_set_run_mode(CPU_STOPPED);
}

/**
 * @brief IOF - Interrupt System OFF.
 *
 * Disables the interrupt system. On IOF the ND-110 continues operation at
 * the same program level.
 *
 * @par Instruction
 * Opcode 150401 octal, mask 1111_1111_1111_1111.
 * Category: Interrupt Control Instructions. Privilege: Privileged.
 * Format: IOF
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section IOF
 */
static void opcode_iof_interrupt_off(uint16_t operand)
{
    (void)operand;
    if (!check_priv())
    {
        return;
    }

    cpu_setbit_sts_msb(STS_INTERRUPT_ON, 0);
}

/**
 * @brief ION - Interrupt System ON.
 *
 * Enables the interrupt system. On ION the ND-i10 resumes operation in the
 * program level with highest priority.
 *
 * @par Instruction
 * Opcode 150402 octal, mask 1111_1111_1111_1111.
 * Category: Interrupt Control Instructions. Privilege: User.
 * Format: ION
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section ION
 */
static void opcode_ion_interrupt_on(uint16_t operand)
{
    (void)operand;
    cpu_setbit_sts_msb(STS_INTERRUPT_ON, 1);
    gCHKIT = true; // recalc PK
}

/**
 * @brief POF - Memory management OFF.
 *
 * Disable memory management system. The next instruction will be taken from
 * a physical address given by the address following the POF instruction.
 * Note: The CPU will be in an unrestricted mode without any hardware
 * protection features - all instructions are legal and all memory
 * accessible.
 *
 * @par Instruction
 * Opcode 150404 octal, mask 1111_1111_1111_1111.
 * Category: Interrupt Control Instructions. Privilege: Privileged.
 * Format: POF
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section POF
 */
static void opcode_pof_paging_off(uint16_t operand)
{
    (void)operand;

    if (!check_priv())
    {
        return;
    }
    cpu_setbit_sts_msb(STS_PAGING_ON, 0);
}

/**
 * @brief IRW - Inter Register Write.
 *
 * Write A to specified register on specified level
 *
 * @par Instruction
 * Opcode 153400 octal, mask 1111_1111_1000_0000.
 * Category: Inter-level Instructions. Privilege: Privileged.
 * Format: IRW <level> <register>
 *
 * @par Instruction word
 *   bits 15-7  opcode - The opcode for inter-register read
 *   bits 6-3  level - Privilege level to access (0-15)
 *   bits 2-0  register - Register to read from specified
 *       level<br><br>**Values:**<br>- `STS` (`000000`): Status register<br>-
 *       `DD` (`000001`): D register<br>- `DP` (`000002`): P register<br>- `DB`
 *       (`000003`): B register<br>- `DL` (`000004`): L register<br>- `DA`
 *       (`000005`): A register<br>- `DT` (`000006`): T register<br>- `DX`
 *       (`000007`): X register<br>
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section IRW
 */
static void opcode_irw_inter_register_write(uint16_t operand)
{
    if (!check_priv())
    {
        return;
    }

    uint16_t level = (operand >> 3) & 0x0F;
    uint16_t dr = (operand & 0x07);

    if ((level == CURR_LEVEL) && (dr == _A))
    {
        return; // A on same level, do nothing (Write from A to A on same level== NOP)
    }

    if ((level == CURR_LEVEL) && (dr == _P))
    {
        return; // P on same level, do nothing (Because this is what the microcode does)
    }

    if (dr == _STS)
    {
        // Update STS lower bits (which is unique for each runlevel)
        g_reg->reg[level][_STS] = (gA & 0x00FF);
    }
    else
    {
        g_reg->reg[level][dr] = gA;
    }
}

/**
 * @brief IRR - Inter Register Read.
 *
 * A: = specified register on specified level
 *
 * @par Instruction
 * Opcode 153600 octal, mask 1111_1111_1000_0000.
 * Category: Inter-level Instructions. Privilege: Privileged.
 * Format: IRR <level> <register>
 *
 * @par Instruction word
 *   bits 15-7  opcode - The opcode for inter-register read
 *   bits 6-3  level - Privilege level to access (0-15)
 *   bits 2-0  register - Register to read from specified
 *       level<br><br>**Values:**<br>- `STS` (`000000`): Status register<br>-
 *       `DD` (`000001`): D register<br>- `DP` (`000002`): P register<br>- `DB`
 *       (`000003`): B register<br>- `DL` (`000004`): L register<br>- `DA`
 *       (`000005`): A register<br>- `DT` (`000006`): T register<br>- `DX`
 *       (`000007`): X register<br>
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section IRR
 */
static void opcode_irr_inter_register_read(uint16_t operand)
{
    if (!check_priv())
    {
        return;
    }

    uint16_t level = (operand >> 3) & 0x0F;
    uint16_t sr = (operand & 0x07);

    if (sr == 0) // STS
    {
        gA = g_reg->reg[level][_STS] & 0xFF; // read only lower 8 bits
    }
    else
    {
        gA = g_reg->reg[level][sr];
    }
}


/* ---------------------------------------------------------- Register block */

/**
 * @brief SRB - Store register block.
 *
 * Load the register block of the program level given in the instruction into
 * the memory block pointed to by the X register. If the instruction
 * specifies the current program level, the P register points to the
 * instruction following SRB.
 *
 * @par Instruction
 * Opcode 152402 octal, mask 1111_1111_1100_0000.
 * Category: Register Block Instructions. Privilege: Privileged.
 * Format: SRB <level>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 6-3  level - The level to load the register block to
 *   bits 2-0  type - The type of register block
 *       function<br><br>**Values:**<br>- `SRB` (`0`): Store Register
 *       Block<br>- `LRB` (`2`): Load Register Block<br>
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section SRB
 */
static void opcode_srb_store_register_block(uint16_t operand)
{
    if (!check_priv())
    {
        return;
    }

    /* SRB */ /* NOTE: These two seems to have bit req on 0-2 as well */
    do_srb(operand);
}

/**
 * @brief LRB - Load register block.
 *
 * Load the contents of a memory block pointed to by the X register into the
 * register block of the program level given in the instruction. If the
 * instruction specifies the current program level, the P register (program
 * counter) is not loaded from memory and is unchanged.
 *
 * @par Instruction
 * Opcode 152600 octal, mask 1111_1111_1100_0000.
 * Category: Register Block Instructions. Privilege: Privileged.
 * Format: LRB <level>
 *
 * @par Instruction word
 *   bits 15-11  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 6-3  level - The level to load the register block to
 *   bits 2-0  type - The type of register block
 *       function<br><br>**Values:**<br>- `SRB` (`0`): Store Register
 *       Block<br>- `LRB` (`2`): Load Register Block<br>
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LRB
 */
static void opcode_lrb_load_register_block(uint16_t operand)
{
    if (!check_priv())
    {
        return;
    }

    /* SRB */ /* NOTE: These two seems to have bit req on 0-2 as well */
    do_lrb(operand);
}


/* ---------------------------------------------------------- Paging control */

/**
 * @brief PIOF - Memory management and interrupt system OFF.
 *
 * Disables both the memory management and interrupt systems. This combines
 * the functions of the IOF and POF instructions. Before_use:   Check
 * conditions of the IOF instruction.
 *
 * @par Instruction
 * Opcode 150405 octal, mask 1111_1111_1111_1111.
 * Category: Memory Management Instructions. Privilege: Privileged.
 * Format: PIOF
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section PIOF
 */
static void opcode_piof_paging_and_interrupt_off(uint16_t operand)
{
    (void)operand;

    if (!check_priv())
    {
        return;
    }

    cpu_setbit_sts_msb(STS_INTERRUPT_ON, 0);
    cpu_setbit_sts_msb(STS_PAGING_ON, 0);
}

/**
 * @brief SEX - Set extended address mode.
 *
 * @par Instruction
 * Opcode 150406 octal, mask 1111_1111_1111_1111.
 * Category: Memory Management Instructions. Privilege: Privileged.
 * Format: SEX
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section SEX
 */
static void opcode_sex_set_extended_address_mode(uint16_t operand)
{
    (void)operand;
    if (!check_priv())
    {
        return;
    }

    cpu_setbit_sts_msb(STS_EXTENDED_ADDRESSING, 1);
}

/**
 * @brief REX - Reset extended address mode.
 *
 * @par Instruction
 * Opcode 150407 octal, mask 1111_1111_1111_1111.
 * Category: Memory Management Instructions. Privilege: Privileged.
 * Format: REX
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section REX
 */
static void opcode_rex_reset_extended_address_mode(uint16_t operand)
{
    (void)operand;
    if (!check_priv())
    {
        return;
    }

    cpu_setbit_sts_msb(STS_EXTENDED_ADDRESSING, 0);
}

/**
 * @brief PON - Memory management ON.
 *
 * Enable memory management system. The next instruction after PON will then
 * use the pageindex table specified by PCR. BEFORE USE ENSURE:   - Interrupt
 * system is enabled,   - Internal hardware interrupts are enabled,   - Page
 * tables and PCR registers are initialized.
 *
 * @par Instruction
 * Opcode 150410 octal, mask 1111_1111_1111_1111.
 * Category: Memory Management Instructions. Privilege: User.
 * Format: PON
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section PON
 */
static void opcode_pon_paging_on(uint16_t operand)
{
    (void)operand;
    cpu_setbit_sts_msb(STS_PAGING_ON, 1);
}

/**
 * @brief PION - Memory management and interrupt system ON.
 *
 * Enable both the memory management and interrupt systems. This combines the
 * functions of the ION and PON instructions.
 *
 * @par Instruction
 * Opcode 150412 octal, mask 1111_1111_1111_1111.
 * Category: Memory Management Instructions. Privilege: User.
 * Format: PION
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section PION
 */
static void opcode_pion_paging_and_interrupt_on(uint16_t operand)
{
    (void)operand;
    cpu_setbit_sts_msb(STS_INTERRUPT_ON, 1);
    cpu_setbit_sts_msb(STS_PAGING_ON, 1);
    gCHKIT = true; // recalc PK
}


/* --------------------------------------------- Page tables (140300-140304) */

/**
 * @brief SETPT - Set page tables.
 *
 * @par Instruction
 * Opcode 140300 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: SETPT
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section SETPT
 */
static void opcode_setpt_set_page_tables(uint16_t operand)
{
    (void)operand;
    if (!check_priv())
    {
        return;
    }

    /* ND110 Microcode:
    9217  004054  %        OPCODE 140300 : SETPT 4
    9218  004054  %
    9219  004054  % SETPT: JXZ * 10               % FINISHED
    9220  004054  %        LDDTX 20
    9221  004054  %        BSET ZRO 130 DA        % PGU-BIT
    9222  004054  %        LDBTX 10
    9223  004054  %        177777                 % OLD BUG IN LDBTX
    9224  004054  %        STD ,B                 % ALWAYS INSIDE PAGE TABLE
    9225  004054  %        LDXTX 00
    9226  004054  %        JMP *-7
    */

    int cnt = 0;

    // JXZ * 10 % FINISHED
    while (gX != 0)
    {
        uint32_t el = 0;
        uint32_t effective_address = 0;

        //  LDDTX 20 <=  A: = (EL), D: = (EL + 1)
        el = calc_el(2); // Calculates using X, T and mriDisplacement // oct 020 >>3
        gA = (uint16_t)read_el(el);
        gD = (uint16_t)read_el(el + 1);

        // BSET ZRO 130 DA % PGU - BIT *

        gA = gA & ~(1 << 0x0b); // 0x0b = 13 octalt. Clear bit 013 in register A

        // LDBTX 10
        el = calc_el(1); // oct 10 >> 3
        uint32_t elval = read_el(el);
        gB = (uint16_t)(((elval + elval) & 0xFFFF) | 0xFE00); // 177000

        // 177777                   % OLD BUG IN LDBTX

        // STD ,B
        effective_address = (uint32_t)(gB & 0xFFFF); // (+displacement, which is 0 here)
        mms_write_virtual_memory(effective_address, gA, true, WRITEMODE_WORD);
        mms_write_virtual_memory(effective_address + 1, gD, true, WRITEMODE_WORD);

        //  LDXTX 00 <=  X:= (EL)
        gX = (uint16_t)read_el(calc_el(0)); // Calculates using X, T and mriDisplacement

        // Increase counter
        cnt++;
    }

    gX = (uint16_t)
        cnt; // Report number of loops in X (undocumented, but testing using "INSTRUCTION - Version: C00 - 1986-10-30" sub-program "SEGMENTS" identified it.
}

/**
 * @brief CLEPT - Clear page tables.
 *
 * @par Instruction
 * Opcode 140301 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: CLEPT
 *
 * @par Registers affected
 * (?)
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section CLEPT
 */
static void opcode_clept_clear_page_tables(uint16_t operand)
{
    (void)operand;
    if (!check_priv())
    {
        return;
    }

    /* ND110 Microcode:
    9229  004054  %        OPCODE 140301 I CLEPT
    9230  004054  %9231  004054  % CLEPT: JXZ * 11               % FINISHED
    9232  004054  %        LDBTX 10
    9233  004054  %        177777                 % OLD BUG IN LDBTX
    9234  004054  %        LDA ,B
    9235  004054  %        JAZ * 3
    9236  004054  %        STATX 20
    9237  004054  %        STZ ,B                 % ALWAYS INSIDE PAGE TABLE
    9238  004054  %        LDXTX 00
    9239  004054  %        JMP *-10
    9240  004054  %*
    */

    /*

     *  Affected: Pagetables, A, T, X, B registers ????
     *  T,X used as an adress reg  with 24 bits in the xxxTX instructions
     *
     * This instruction apparently is a replacement for this sequence:
     * CLEPT:   JXZ * 10    (if X=0 goto END)
     *      LDBTX 10    (B:=177000|(2*(EL)), EL=T,X+1)
     *      LDA ,B      (A:=(B))
     *      JAZ * 3     (if A=0 goto LOOP)
     *      STATX 20    ((EL):=A, EL=T,X+2)
     *      STZ ,B      ( (B):=0 )
     *      LDXTX 00    (X:=(EL), EL=T,X)
     * LOOP:    JMP *-7     (goto CLEPT)
     * END:     ...
     */

    /*
     * Ported from RetroCore CLEPT (Emulated.HW/ND/CPU/ND100/Instructions.ND110Specific.cs),
     * which carries the oracle-verified access ORDER.  The equivalent-assembler comment above
     * is a paraphrase and is NOT the access order the hardware uses - the real RASK microcode
     * body is CLPT1 (ND-110-RASK.LISTING.TXT 9339-9420, shared with CLEPU when R7 = 0):
     *
     *   004071 CLPT1:  Q := D, ARG 600
     *   004072          R4 := ...,  T,PUSH PATA1      <-- the FIRST memory touch of every node
     *   004120 PATA1:   LDSEG T                          (segment for the physical T,X accesses)
     *   004121 PATA2:   R1 := X                          (R1 = the node pointer)
     *   004122          EXRQ @X,  COND,F=0               (read [X]; F latches "X was 0")
     *   004123          X := DBR,  T,JMP T,POP           (X := [X] - happens on EVERY pass,
     *                                                     including the terminating X == 0 one)
     *   004124-004130   R1 := R1+1; EXRQ @[X+1];         (LDBTX 10: page index at [X+1] ->
     *                   B := 177000 | (2 * [X+1])         page-table entry address in B)
     *   004074          RDRQ,APT  -> PATA4 (004131)      (LDA ,B via the ALTERNATIVE page table)
     *   004075          COND on A == 0                   (JAZ *3: skip an unused entry)
     *   004077          DERQ  -> [X+2] := A              (STATX 20: save the used entry)
     *   004116          WRRQ,APT  ZERO                   (STZ ,B: clear the entry)
     *
     * Two bugs are fixed here versus the previous implementation:
     *
     *  1) ACCESS ORDER.  [X] (the next-node pointer) is read at the START of each node by
     *     PATA2 (004121-004123) - BEFORE the [X+1] page-index read - not at the end.  The old
     *     code read [X+1] first and [X] last, so a node that modified its own [X] word (which
     *     is exactly what SINTRAN's page-table chains do) walked the wrong successor.
     *
     *  2) FINAL X.  004123 loads X := [X] unconditionally, so on the terminating pass (X == 0)
     *     the microcode still reads [X] and puts that word in X.  The old code instead returned
     *     a LOOP COUNTER in X - copied from SETPT, which really does report a count - and worse,
     *     that counter (`ushort cnt;`) was NEVER INITIALISED, so CLEPT returned a garbage X that
     *     varied run to run.  That undefined behaviour is why the TPE INSTRUCTION failure looked
     *     "timing sensitive" and why one traced run appeared clean.  CLPT1 has no counter at all.
     */

    while (1)
    {
        uint16_t next_x;
        uint32_t elval;

        /* 004121-004122 (PATA2): read the next-node pointer at [X] (physical, bank T). */
        next_x = (uint16_t)read_el(calc_el(0));

        /* 004123: X == 0 ends the walk - but X is still loaded from [X] on this final pass. */
        if (gX == 0)
        {
            gX = next_x;
            break;
        }

        /* 004124-004130 (LDBTX 10): page index at [X+1] -> entry address B = 0177000 | (2*index). */
        elval = read_el(calc_el(1));
        gB = (uint16_t)(((elval + elval) & 0xFFFF) | 0xFE00); /* 177000 */

        /* 004074 / PATA4 (LDA ,B): read the page-table entry via the ALTERNATIVE page table. */
        gA = (uint16_t)mms_read_virtual_memory(gB, true);

        /* 004075 (JAZ *3): a zero (unused) entry is skipped; a used entry is saved then cleared. */
        if (gA != 0)
        {
            /* 004077 (STATX 20): save the entry to [X+2] (physical, bank T). */
            write_el(calc_el(2), (uint16_t)gA);

            /* 004116 (STZ ,B): clear the page-table entry via the ALTERNATIVE page table. */
            mms_write_virtual_memory(gB, 0, true, WRITEMODE_WORD);
        }

        /* Advance to the next node (X := [X], already read at the top of this iteration). */
        gX = next_x;
    }
}

/**
 * @brief CLNREENT - Clear non reentrant pages.
 *
 * The contents of the memory address at A + 2 are read to find the page
 * table to be cleared along with the SINTRAN RT bitmap (addressed by the X
 * and T registers).  The page table entries corresponding to those bits set
 * in the RT bitmap are then cleared.
 *
 * @par Instruction
 * Opcode 140302 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: CLNREENT
 *
 * @par Registers affected
 * (?)
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section CLNREENT
 */
static void opcode_clnreent_clear_non_reentrant_pages(uint16_t operand)
{
    (void)operand;
    uint16_t a_reg;
    uint16_t x_reg;
    uint16_t t_reg;
    uint16_t r1; /* page-table clear cursor (APT-relative) */
    uint16_t r2; /* bitmap read cursor */
    uint16_t r3; /* bitmap end (exclusive) */

    if (!check_priv())
    {
        return;
    }

    /*
    OPCODE 140302 : CLNREENT

    READ ADDRESS A+2 TO FIND PAGE TABLE TO BE AFFECTED
    READ RT - DESCRIPTION BITMAP WORDS, FOUND FROM ADDRESS X + 25.
    CLEAR PAGE-TABLE ENTRIES CORRESPONDING TO 1 - BITS IN BITMAP.
    THE LAST BITMAP-ADDRESS IS IN ADDRESS X + T.
    */

    /*
     * Ported verbatim from RetroCore CLNREENT
     * (Emulated.HW/ND/CPU/ND100/Instructions.ND110Specific.cs), which is faithful to
     * RASK microcode CLNR1 (ND-110-RASK.LISTING.TXT lines 9460-9538) and was validated
     * against the ND-110 microcode oracle.  All memory accesses go through the
     * ALTERNATIVE page table (the operated-on process's page table).
     */
    a_reg = gA;
    x_reg = gX;
    t_reg = gT;

    /*
     * 004132: the S3SG1 prologue leaves Q = A + 1, so F = Q + 1 = A + 2.  If A + 2 == 0
     * the instruction does nothing and returns (RASK LISTING 9460 / 9467, cond0 -> CONTINUE).
     */
    if ((uint16_t)(a_reg + 2) == 0)
    {
        return;
    }

    /*
     * 004133: read the page-table pointer word via APT[A+2].  Its value is latched into Q
     * but the rest of CLNR1 uses the fixed APT base 0177000 instead, so this read is a side
     * effect only - it is kept so the memory-access trace matches the microcode oracle.
     */
    (void)mms_read_virtual_memory((uint16_t)(a_reg + 2), true);

    /*
     * 004135-004141: R1 = 0177000 (octal) APT-relative page-table base; R2 = X + 25 (octal)
     *                bitmap read cursor; R3 = X + T + 1 bitmap end (last bitmap word at X + T).
     */
    r1 = 0xFE00;                   /* 0177000 octal */
    r2 = (uint16_t)(x_reg + 0x15); /* + 025 octal (= 21 decimal) */
    r3 = (uint16_t)(x_reg + t_reg + 1);

    /* Outer loop over the bitmap words (CLNR1 004142..CLNR2 004152, LISTING 9490-9537). */
    while (r3 != r2) /* CLNR2: bitmap exhausted -> done */
    {
        uint16_t addr = r2; /* 004142: address = old R2, then R2++ */
        uint16_t word;
        int bit;

        r2 = (uint16_t)(r2 + 1);
        word = (uint16_t)mms_read_virtual_memory(addr, true); /* 004143 */

        if (word == 0)
        {
            /*
             * CLNR5 004156: a zero bitmap word clears nothing; skip its 16 entries
             * (R1 += 040 octal = 32 = 16 entries * 2-word stride).
             */
            r1 = (uint16_t)(r1 + 0x20);
            continue;
        }

        /*
         * Inner loop: 16 bit positions, LSB first.  Clear the page-table entry when its bit
         * is set (RASK 004150-004155; stride 2, one entry per bit).  The clear-when-set
         * predicate is the documented intent (LISTING 9246); the exact microcode latch is
         * oracle-validated.
         */
        for (bit = 0; bit < 16; bit++)
        {
            if ((word & (1 << bit)) != 0)
            {
                mms_write_virtual_memory(r1, 0, true, WRITEMODE_WORD); /* 004155 */
            }
            r1 = (uint16_t)(r1 + 2); /* 004153: 2-word stride per entry */
        }
    }
}

/**
 * @brief CLEPU - Clear page tables and collect PGU information.
 *
 * @par Instruction
 * Opcode 140304 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: CLEPU
 *
 * @par Registers affected
 * (?)
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section CLEPU
 */
static void opcode_clepu_clear_page_tables_and_collect_page_used(uint16_t operand)
{
    (void)operand;
    if (!check_priv())
    {
        return;
    }

    // TODO: Implement

    /*
        OPCODE 140304 : CLEPU

        AS 'CLEPT" BUT INCLUDING WORKING SET INFORMATION
        FOR ALL PAGE-TABLE ENTRIES HANDLED
        IF PGU OF ENTRY IS 1
            D /0 300
            B /0 776 SHR 1 - D
            B-REG BITS 0-3 IS NOW BIT NUMBER
            B-REG BITS 4-6 IS NOW WORD NUMBER
            SET BIT IN 8-WORD TABLE IN PAGE-MAP BANK
            POINTED TO BY L-REGISTER

        LAYOUT 0F 8-WORD TABLE

                            BIT 15                                  BIT O
                            ________________________________________________
        L-REG -> WORD   0   # PAGE 17                               PAGE 0 #
        WORD            1   # PAGE 37                                   20 #
        WORD            2   # PAGE 57                                   40 #
        WORD            3   # PAGE 177                                 160 #

    */

    /*
     * Ported verbatim from RetroCore CLEPU
     * (Emulated.HW/ND/CPU/ND100/Instructions.ND110Specific.cs), faithful to RASK
     * CLPU1/CLPT1 (ND-110-RASK.LISTING.TXT 9340-9418; the CLEPU dispatch at 005764 preloads
     * R7 = BMG(013 octal) = 04000 octal = bit 11) and validated against the microcode oracle.
     *
     * CLEPU is CLEPT plus: for every entry whose PGU (page-used) bit is set, BEFORE clearing
     * it, set that page's bit in an 8-word working-set table in the page-map bank pointed to
     * by L (PGU block LISTING 9370-9408).  Per the header table layout above, page = the
     * entry index at [X+1]; word number = page >> 4 (0..7), bit number = page & 0xF; the
     * table word lives at L + word (physical).  The save/PGU/clear order matches the
     * microcode: save [X+2] (004077), collect (004101-114), then clear (004116).
     *
     * NOTE: unlike the older opcode_clept_clear_page_tables above, the next-node pointer at [X] is read FIRST,
     * on every pass including the terminating one - that access order and the final X are
     * oracle-verified (see the RetroCore CLEPT/CLEPU comments).
     */
    for (;;)
    {
        uint16_t next_x;
        uint32_t idx;

        /* 004122 (PATA2): next-node pointer at [X], read first (physical, bank T). */
        next_x = (uint16_t)read_el(calc_el(0));

        /* 004123: X == 0 terminates; load X from [X] on the final pass. */
        if (gX == 0)
        {
            gX = next_x;
            break;
        }

        /* 004124-004130: page index at [X+1] -> entry address B = 0177000 | (2*index). */
        idx = read_el(calc_el(1));
        gB = (uint16_t)(((idx + idx) & 0xFFFF) | 0xFE00); /* 177000 */

        /* 004074 / PATA4: read the page-table entry via the alternative page table. */
        gA = (uint16_t)mms_read_virtual_memory(gB, true);

        /* 004075 (JAZ *3): skip unused (zero) entries. */
        if (gA != 0)
        {
            /* 004077 (STATX 20): save the entry to [X+2] (physical, bank T). */
            write_el(calc_el(2), gA);

            /*
             * 004100-004114 (PGU block): if the entry's PGU bit is set, mark the page in
             * the 8-word working-set table at L (page-map bank).
             * word = page >> 4, bit = page & 0xF.
             */
            if ((gA & ND110_PGU_BIT) != 0)
            {
                clepu_mark_working_set(idx);
            }

            /* 004116 (STZ ,B): clear the page-table entry via the alternative page table. */
            mms_write_virtual_memory(gB, 0, true, WRITEMODE_WORD);
        }

        /* Advance to the next node. */
        gX = next_x;
    }
}

/**
 * @brief CHREENT_PAGES - see the notes below.
 *
 * @par Registers affected
 * (?)
 *
 * @param operand The full instruction word as fetched.
 */
static void opcode_chreent_pages(uint16_t operand)
{
    (void)operand;
    uint16_t prog_d;
    uint16_t prog_x;
    uint16_t prog_t;
    uint16_t prev_seg;
    uint16_t prev_off;
    uint16_t seg;
    uint16_t off;

    if (!check_priv())
    {
        return;
    }

    /*
        OPCODE 140303 : CHREENTPAGES

        1. READ ADDRESS D.X -> R1 ; D,X -> PREVIOUS (SCRATCH REG)
        2. IF R1 = 0; SKIP RETURN (FINISHED)
        3. READ ADDRESS T,R1+2
        4. IF NOT WIP; T.R1 -> PREVIOUS; READ ADDR T.R1 -> R1; GOTO 2
        5. READ ADDRESS T,R1  -> R2
        6. WRITE R2 -> ADDRESS PREVIOUS
        7. R1 -> X ; PREVIOUS -> D.A ; RETURN
    */

    /*
     * Ported verbatim from RetroCore CHREENT_PAGES
     * (Emulated.HW/ND/CPU/ND100/Instructions.ND110Specific.cs), faithful to RASK microcode
     * CHRE1 (ND-110-RASK.LISTING.TXT lines 9540-9599) and validated against the microcode
     * oracle.  The chain lives in PHYSICAL memory addressed as segment:offset (a loaded
     * segment selects a 64K bank) - NOT through the page table.
     */
    prog_d = gD;
    prog_x = gX;
    prog_t = gT;

    /* PREVIOUS slot, initially the chain head at D:X (R6/R7 = progD/progX, LISTING 9540-9543). */
    prev_seg = prog_d;
    prev_off = prog_x;

    seg = prog_d; /* loaded segment register */
    off = prog_x; /* MAR offset */

    for (;;)
    {
        uint16_t link;
        uint16_t status;

        /* CHRE2 004161: read the link word at segment:offset. */
        link = (uint16_t)mms_read_physical_memory((int)nd110_seg_phys(seg, off), true);

        /*
         * 004162-004163 / CHRE4 004200: a zero link ends the chain -> SKIP return
         * (extra P+1), registers unchanged.
         */
        if (link == 0)
        {
            gPC++;
            return;
        }

        seg = prog_t; /* 004163: the status/link reads use the descriptor segment T */

        /* 004164-004166: read the status word at T:(link+2) and test WIP (bit 12). */
        status = (uint16_t)mms_read_physical_memory(
            (int)nd110_seg_phys(prog_t, (uint16_t)(link + 2)), true);

        if ((status & ND110_WIP_BIT) != 0)
        {
            /*
             * WIP set: unlink this page.  004170: read successor at T:link;
             * 004174: DEPOSIT it into the previous slot; 004172-004175: set D/A/X,
             * normal return.
             */
            uint16_t successor =
                (uint16_t)mms_read_physical_memory((int)nd110_seg_phys(prog_t, link), true);

            mms_write_physical_memory((int)nd110_seg_phys(prev_seg, prev_off), successor, true);
            gD = prev_seg;
            gA = prev_off;
            gX = link;
            return;
        }

        /* NOT WIP (CHRE3 004176-004177): advance PREVIOUS to T:link, then follow the chain link. */
        prev_seg = prog_t;
        prev_off = link;
        off = link; /* next CHRE2 reads T:link = the successor link */
    }
}


/* ---------------------------------- Page list and segments (140500-140507) */

/**
 * @brief WGLOB - Initialize global pointers.
 *
 * (T) = bank number of segment table (STBNK) (A) = start address within bank
 * (STSRT)* (D) = bank number of core map table (CMBNK) * must be divisible
 * by 8
 *
 * @par Instruction
 * Opcode 140500 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: WGLOB
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section WGLOB
 */
static void opcode_wglob_initialize_global_pointers(uint16_t operand)
{
    (void)operand;
    if (!check_priv())
    {
        return;
    }

    gSTBNK = gT;
    gSTSRT = gA;
    gCMBUK = gD;
}

/**
 * @brief RGLOB - Examine global pointers.
 *
 * (T) <--- bank number of segment table (STBNK) (A) <--- start address
 * within bank (STSRT) (D) <--- bank number of core map table (CMBNK)
 *
 * @par Instruction
 * Opcode 140501 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: RGLOB
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section RGLOB
 */
static void opcode_rglob_examine_global_pointers(uint16_t operand)
{
    (void)operand;
    if (!check_priv())
    {
        return;
    }

    gT = gSTBNK;
    gA = gSTSRT;
    gD = gCMBUK;
}

/**
 * @brief INSPL - Insert page in page list. (See Appendix B for a software description.).
 *
 * @par Instruction
 * Opcode 140502 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: INSPL
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section INSPL
 */
static void opcode_inspl_insert_page_in_page_list(uint16_t operand)
{
    (void)operand;
    uint32_t stbnk;
    uint32_t cmbnk;
    uint16_t b_reg;
    uint16_t x_reg;
    uint16_t t_reg;
    uint16_t old_head;
    uint16_t marker;

    if (!check_priv())
    {
        return;
    }

    stbnk = (uint32_t)(gSTBNK & 0xFF) << 16;
    cmbnk = (uint32_t)(gCMBUK & 0xFF) << 16;
    b_reg = gB;
    x_reg = gX;
    t_reg = gT;

    /* 004454-004457: R1 := old page-list head at STBNK[B+7]. */
    old_head =
        (uint16_t)mms_read_physical_memory((int)(stbnk | (uint32_t)((b_reg + 7) & 0xFFFF)), true);
    /* 004460-004461: new head := X. */
    mms_write_physical_memory((int)(stbnk | (uint32_t)((b_reg + 7) & 0xFFFF)), x_reg, true);
    /* 004462-004464: X's forward link (CMBUK[X]) := old head. */
    mms_write_physical_memory((int)(cmbnk | x_reg), old_head, true);

    if (old_head == 0)
    {
        /*
         * 004473-004474 (INSP2, empty list): back link := anchor marker segIndex | 3,
         * where segIndex = (B - STSRT) >> 1.
         */
        uint16_t seg_index = (uint16_t)(((b_reg - gSTSRT) & 0xFFFF) >> 1);

        marker = (uint16_t)(seg_index | 3);
    }
    else
    {
        /* 004466-004472 (non-empty): X inherits the old head's back link; old head.prev := X. */
        marker = (uint16_t)mms_read_physical_memory(
            (int)(cmbnk | (uint32_t)((old_head + 1) & 0xFFFF)), true);
        mms_write_physical_memory((int)(cmbnk | (uint32_t)((old_head + 1) & 0xFFFF)), x_reg, true);
    }

    /* 004475-004476 (INSP3): X's back link (CMBUK[X+1]) := marker. */
    mms_write_physical_memory((int)(cmbnk | (uint32_t)((x_reg + 1) & 0xFFFF)), marker, true);
    /* 004477-004501: X's tag word (CMBUK[X+3]) := T. */
    mms_write_physical_memory((int)(cmbnk | (uint32_t)((x_reg + 3) & 0xFFFF)), t_reg, true);
}

/**
 * @brief REMPL - Remove page from page list.
 *
 * @par Instruction
 * Opcode 140503 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: REMPL
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section REMPL
 */
static void opcode_rempl_remove_page_from_page_list(uint16_t operand)
{
    (void)operand;
    uint32_t stbnk;
    uint32_t cmbnk;
    uint16_t x_reg;
    uint16_t r1; /* successor */
    uint16_t r2; /* back link / anchor marker */
    bool tail;
    bool skip_inherit;

    if (!check_priv())
    {
        return;
    }

    stbnk = (uint32_t)(gSTBNK & 0xFF) << 16;
    cmbnk = (uint32_t)(gCMBUK & 0xFF) << 16;
    x_reg = gX;

    /* 004502-004507: R1 := successor (CMBUK[X]); R2 := back link / anchor marker (CMBUK[X+1]). */
    r1 = (uint16_t)mms_read_physical_memory((int)(cmbnk | x_reg), true);
    r2 = (uint16_t)mms_read_physical_memory((int)(cmbnk | (uint32_t)((x_reg + 1) & 0xFFFF)), true);

    tail = ((r2 & 3) != 0);
    if (tail)
    {
        /*
         * 004514-004520 (REMP2, tail page): the back link is the anchor marker; the
         * segment head slot is STBNK[(STSRT + 2*marker) | 7] (== B+7).  Set it to the
         * successor.
         */
        uint32_t head_off = (uint32_t)(((gSTSRT + 2 * r2) | 7) & 0xFFFF);

        mms_write_physical_memory((int)(stbnk | head_off), r1, true);
        skip_inherit = (r1 == 0);
    }
    else
    {
        /* 004512-004513 (middle page): predecessor.next := successor (executes even if R2==0). */
        mms_write_physical_memory((int)(cmbnk | r2), r1, true);
        skip_inherit = (r2 == 0);
    }

    /* 004521-004523 (REMP3): unless the successor is nil, successor.prev := R2 (predecessor/marker). */
    if (!skip_inherit)
    {
        mms_write_physical_memory((int)(cmbnk | (uint32_t)((r1 + 1) & 0xFFFF)), r2, true);
    }

    /* 004524-004527 (REMP4): zero the removed entry's forward and back links. */
    mms_write_physical_memory((int)(cmbnk | x_reg), 0, true);
    mms_write_physical_memory((int)(cmbnk | (uint32_t)((x_reg + 1) & 0xFFFF)), 0, true);
}

/**
 * @brief CNREK - Clear non reentrant pages (SINTRAN K only).
 *
 * @par Instruction
 * Opcode 140504 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: CNREK
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section CNREK
 */
static void opcode_cnrek_clear_non_reentrant_pages_sintran_k(uint16_t operand)
{
    (void)operand;
    uint16_t a_reg;
    uint16_t x_reg;
    uint16_t t_reg;
    uint32_t stbnk;
    uint32_t tseg;
    uint16_t r1;
    uint16_t r2;
    uint16_t r3;

    if (!check_priv())
    {
        return;
    }

    a_reg = gA;
    x_reg = gX;
    t_reg = gT;
    stbnk = (uint32_t)(gSTBNK & 0xFF) << 16;
    tseg = (uint32_t)(t_reg & 0xFF) << 16;

    /* 004530-004531: examine the descriptor at STBNK[A+2] (value unused in this path). */
    (void)mms_read_physical_memory((int)(stbnk | (uint32_t)((a_reg + 2) & 0xFFFF)), true);

    /* 004532: A+2 == 0 -> no-op.  004536/004540: X == 0 -> no-op. */
    if ((uint16_t)(a_reg + 2) == 0)
    {
        return;
    }
    if (x_reg == 0)
    {
        return;
    }

    r1 = 0xF800;                /* 0174000 octal - page-table clear base (APT) */
    r2 = x_reg;                 /* first bitmap word */
    r3 = (uint16_t)(x_reg + 8); /* bound = X + 010 octal (8 words) */

    while (r3 != r2)
    {
        uint16_t word;
        int bit;

        /* 004541: examine the bitmap word physically in segment T. */
        word = (uint16_t)mms_read_physical_memory((int)(tseg | r2), true);
        r2 = (uint16_t)(r2 + 1);

        if (word == 0)
        {
            r1 = (uint16_t)(r1 + 0x20); /* all-zero word clears nothing; skip its 16 entries */
            continue;
        }

        for (bit = 0; bit < 16; bit++)
        {
            if ((word & (1 << bit)) != 0)
            {
                mms_write_virtual_memory(r1, 0, true, WRITEMODE_WORD); /* 004155 clear via APT */
            }
            r1 = (uint16_t)(r1 + 2);
        }
    }
}

/**
 * @brief CLPT - Clear segment from the page tables.
 *
 * @par Instruction
 * Opcode 140505 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: CLPT
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section CLPT
 */
static void opcode_clpt_clear_segment_from_page_tables(uint16_t operand)
{
    (void)operand;
    uint32_t cmbnk;
    bool clear_mode;

    if (!check_priv())
    {
        return;
    }

    cmbnk = (uint32_t)(gCMBUK & 0xFF) << 16; /* segment = core-map bank (LDSEG from CMBNK) */
    clear_mode = ((gA & 0x8000) != 0);       /* 004545/004546: bit 15 of A (constant) */

    /* 004543-004544: X == 0 terminates (normal P+1, no writes). */
    while (gX != 0)
    {
        uint16_t x_reg = gX;
        uint16_t entry;
        uint16_t b_reg;

        /* 004545: examine the segment descriptor at (CMBUK : X+3). */
        entry = (uint16_t)mms_read_physical_memory((int)(cmbnk | (uint32_t)((x_reg + 3) & 0xFFFF)),
                                                   true);
        /* 004546: B := (entry | 0176000) << 1. */
        b_reg = (uint16_t)(((entry | 0xFC00) << 1) & 0xFFFF);
        gB = b_reg;

        if (clear_mode)
        {
            /* CLPK4 004554-004555 (bit 15 of A set): clear the page-table entry to 0. */
            mms_write_virtual_memory(b_reg, 0, true, WRITEMODE_WORD);
        }
        else
        {
            /* 004550-004553 (bit 15 clear): read APT[B]; if non-zero, deposit it physically to [X+2]. */
            uint16_t r3 = (uint16_t)mms_read_virtual_memory(b_reg, true);

            if (r3 != 0)
            {
                mms_write_physical_memory((int)(cmbnk | (uint32_t)((x_reg + 2) & 0xFFFF)), r3,
                                          true);

                /*
                 * 004553 falls through into CLPK4 (004554) whose CONDENABL routes the TRUE
                 * case to 004555 - the SAME `ALUF,ZERO / COMM,WRRQ,APT` clear the bit-15 path
                 * uses.  So a SAVED entry is also CLEARED; the instruction is, after all,
                 * CLear Page Tables and the bit-15 flag only selects whether the old entry is
                 * saved first.  The 004552 CONDENABL has already jumped to CLPK3 when the
                 * entry read back as zero, so a zero entry is neither saved nor cleared -
                 * hence this sits inside `r3 != 0`.
                 *
                 * HONESTY NOTE: the listing latches `COND,F=0` at 004553 on an ALU operand
                 * whose register select (`A,R3  B,A  ALUF,PASSB`) is not decidable from the
                 * listing text alone, so "always clear here" cannot be formally separated from
                 * "clear only when the A register is 0".  Every CLPT executed in the validated
                 * SINTRAN III ND-110 boot has A = 0 (91 of 91, measured on the RetroCore B26
                 * harness), so the two readings are indistinguishable on the available
                 * evidence; pin it against the microcode oracle if it ever matters.
                 *
                 * Without this clear the ND-110 SINTRAN boot never releases a page-table
                 * entry and live-locks re-entering the same pages forever (ledger B26): the
                 * ND100CX control run performs 91 clearing writes into page table 9 while the
                 * ND110CX run performed ZERO.  With it, RetroCore's ND110CX harness reaches
                 * "SINTRAN III RUNNING -" in 23 s.
                 */
                mms_write_virtual_memory(b_reg, 0, true, WRITEMODE_WORD);
            }

            /*
             * DIAG (--ring-at-clpt=<n>): once the swap-in/swap-out livelock is
             * in steady state, dump the CPU instruction ring so we can see what the guest
             * actually executed between the ENPT that mapped the segment and this CLPT that
             * unmapped it again.  One-shot.
             */
            {
                static long clpt_calls = 0;

                clpt_calls++;
                if (s_ring_at_clpt > 0 && clpt_calls == s_ring_at_clpt)
                {
                    cpu_ring_dump();
                }
            }

            /* DIAG (--trace-nd110): what CLPT read back out of the page table. */
            if (g_nd110_trace_fp != NULL)
            {
                fprintf(
                    g_nd110_trace_fp,
                    "  CLPT node X=%06o e=%06o -> B=%06o APT[B]=%06o shadow=%d PCR=%06o PONI=%d\n",
                    x_reg, entry, b_reg, r3, mms_is_address_shadow_memory(b_reg, false) ? 1 : 0,
                    g_reg->reg_PCR[CURR_LEVEL], STS_PAGING_ON_IS_SET ? 1 : 0);
                fflush(g_nd110_trace_fp);
            }
        }

        /* 004577-004600: advance X := [X] (forward link, physical CMBUK segment). */
        gX = (uint16_t)mms_read_physical_memory((int)(cmbnk | x_reg), true);
    }
}

/**
 * @brief ENPT - Enter segment in page tables.
 *
 * @par Instruction
 * Opcode 140506 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: ENPT
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section ENPT
 */
static void opcode_enpt_enter_segment_in_page_tables(uint16_t operand)
{
    (void)operand;
    if (!check_priv())
    {
        return;
    }

    nd110_enter_page_table(0xF7FF); /* R4 = 0173777 octal - clears bit 11 */
}

/**
 * @brief REPT - Enter reentrant segment in page tables. (See Appendix B for a software description.).
 *
 * @par Instruction
 * Opcode 140507 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: REPT
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section REPT
 */
static void opcode_rept_enter_reentrant_segment_in_page_tables(uint16_t operand)
{
    (void)operand;
    if (!check_priv())
    {
        return;
    }

    nd110_enter_page_table(0x77FF); /* R4 = 073777 octal - clears bits 15 and 11 */
}


/* --------------------------- Bit, byte and physical memory (140510-140517) */

/**
 * @brief LBIT - Load single bit accumulator (K) with logical memory bit.
 *
 * (X) points to the start of a bit array (A) points to the bit within the
 * array
 *
 * @par Instruction
 * Opcode 140510 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: LBIT
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LBIT
 */
static void opcode_lbit_load_bit_accumulator_from_logical_memory(uint16_t operand)
{
    (void)operand;
    uint32_t bit_index;
    uint32_t word_addr;
    int bit_in_word;
    uint16_t word;

    if (!check_priv())
    {
        return;
    }

    bit_index = gA;
    word_addr = (uint32_t)((gX + (bit_index >> 4)) & 0xFFFF);
    bit_in_word = (int)(bit_index & 0x0F);
    word = (uint16_t)mms_read_virtual_memory(word_addr, true);
    cpu_setbit(_STS, STS_BIT_ACCUMULATOR, (char)((word >> bit_in_word) & 1));
}

/**
 * @brief LBITP - Load single bit accumulator (K) with physical memory bit.
 *
 * (T) points to the bank number containing the bit array (X) points to the
 * start of a bit array (A) points to the bit within the array
 *
 * @par Instruction
 * Opcode 140511 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: LBITP
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LBITP
 */
static void opcode_lbitp_load_bit_accumulator_from_physical_memory(uint16_t operand)
{
    (void)operand;
    uint32_t bit_index;
    uint32_t bank;
    uint32_t word_offset;
    uint32_t phys_addr;
    int bit_in_word;
    uint16_t word;

    if (!check_priv())
    {
        return;
    }

    bit_index = gA;
    bank = (uint32_t)(gT & 0xFF);
    word_offset = (uint32_t)((gX + (bit_index >> 4)) & 0xFFFF);
    phys_addr = (bank << 16) | word_offset;
    bit_in_word = (int)(bit_index & 0x0F);
    word = (uint16_t)mms_read_physical_memory((int)phys_addr, true);
    cpu_setbit(_STS, STS_BIT_ACCUMULATOR, (char)((word >> bit_in_word) & 1));
}

/**
 * @brief SBIT - Store the single bit accumulator (K) in a logical memory bit.
 *
 * (X) points to the start of a bit array (A) points to the bit within the
 * array
 *
 * @par Instruction
 * Opcode 140512 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: SBIT
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section SBIT
 */
static void opcode_sbit_store_bit_accumulator_to_logical_memory(uint16_t operand)
{
    (void)operand;
    uint32_t bit_index;
    uint32_t word_addr;
    int bit_in_word;
    uint16_t word;

    if (!check_priv())
    {
        return;
    }

    bit_index = gA;
    word_addr = (uint32_t)((gX + (bit_index >> 4)) & 0xFFFF);
    bit_in_word = (int)(bit_index & 0x0F);
    word = (uint16_t)mms_read_virtual_memory(word_addr, true);
    if (STS_BIT_ACCUMULATOR_IS_SET)
    {
        word |= (uint16_t)(1 << bit_in_word);
    }
    else
    {
        word &= (uint16_t)(~(1 << bit_in_word));
    }
    mms_write_virtual_memory(word_addr, word, true, WRITEMODE_WORD);
}

/**
 * @brief SBITP - Store the single bit accumulator (K) in a physical memory bit.
 *
 * (T) points to the bank number containing the bit array (X) points to the
 * start of a bit array (A) points to the bit within the array
 *
 * @par Instruction
 * Opcode 140513 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: SBITP
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section SBITP
 */
static void opcode_sbitp_store_bit_accumulator_to_physical_memory(uint16_t operand)
{
    (void)operand;
    uint32_t bit_index;
    uint32_t bank;
    uint32_t word_offset;
    uint32_t phys_addr;
    int bit_in_word;
    uint16_t word;

    if (!check_priv())
    {
        return;
    }

    bit_index = gA;
    bank = (uint32_t)(gT & 0xFF);
    word_offset = (uint32_t)((gX + (bit_index >> 4)) & 0xFFFF);
    phys_addr = (bank << 16) | word_offset;
    bit_in_word = (int)(bit_index & 0x0F);
    word = (uint16_t)mms_read_physical_memory((int)phys_addr, true);
    if (STS_BIT_ACCUMULATOR_IS_SET)
    {
        word |= (uint16_t)(1 << bit_in_word);
    }
    else
    {
        word &= (uint16_t)(~(1 << bit_in_word));
    }
    mms_write_physical_memory((int)phys_addr, word, true);
}

/**
 * @brief LBYTP - Load the A register with a byte from physical memory.
 *
 * (D) points to the bank number containing the byte array (T) points to the
 * start of a byte array (X) points to the actual byte within the array
 *
 * @par Instruction
 * Opcode 140514 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: LBYTP
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LBYTP
 */
static void opcode_lbytp_load_byte_from_physical_memory(uint16_t operand)
{
    (void)operand;
    uint32_t bank;
    uint32_t word_offset;
    uint32_t phys_addr;
    uint16_t memval;

    if (!check_priv())
    {
        return;
    }

    bank = (uint32_t)(gD & 0xFF);
    word_offset = (uint32_t)((gT + (gX >> 1)) & 0xFFFF);
    phys_addr = (bank << 16) | word_offset;
    memval = (uint16_t)mms_read_physical_memory((int)phys_addr, true);
    if ((gX & 1) != 0)
    {
        gA = (uint16_t)(memval & 0xFF); /* odd byte  -> low  */
    }
    else
    {
        gA = (uint16_t)((memval >> 8) & 0xFF); /* even byte -> high */
    }
}

/**
 * @brief SBYTP - Store a byte in physical memory.
 *
 * (D) points to the bank number containing the byte array (T) points to the
 * start of a byte array (X) points to the actual byte within the array
 *
 * @par Instruction
 * Opcode 140515 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: SBYTP
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section SBYTP
 */
static void opcode_sbytp_store_byte_in_physical_memory(uint16_t operand)
{
    (void)operand;
    uint32_t bank;
    uint32_t word_offset;
    uint32_t phys_addr;
    uint16_t memval;
    unsigned char b;

    if (!check_priv())
    {
        return;
    }

    bank = (uint32_t)(gD & 0xFF);
    word_offset = (uint32_t)((gT + (gX >> 1)) & 0xFFFF);
    phys_addr = (bank << 16) | word_offset;
    memval = (uint16_t)mms_read_physical_memory((int)phys_addr, true);
    b = (unsigned char)(gA & 0xFF);
    if ((gX & 1) != 0)
    {
        memval = (uint16_t)((memval & 0xFF00) | b); /* odd byte  -> low  */
    }
    else
    {
        memval = (uint16_t)((memval & 0x00FF) | (b << 8)); /* even byte -> high */
    }
    mms_write_physical_memory((int)phys_addr, memval, true);
}

/**
 * @brief TSETP - Test and set physical memory word.
 *
 * @par Instruction
 * Opcode 140516 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: TSETP
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section TSETP
 */
static void opcode_tsetp_test_and_set_physical_word(uint16_t operand)
{
    (void)operand;
    uint32_t bank;
    uint32_t offset;
    uint32_t phys_addr;

    if (!check_priv())
    {
        return;
    }

    bank = (uint32_t)(gT & 0xFF);
    offset = (uint32_t)(gX & 0xFFFF);
    phys_addr = (bank << 16) | offset;
    gA = (uint16_t)mms_read_physical_memory((int)phys_addr, true);
    mms_write_physical_memory((int)phys_addr, 0xFFFF, true);
}

/**
 * @brief RDUSP - Read a physical memory word without using cache.
 *
 * (T) points to the physical memory bank to be accessed (X) points to the
 * address within the bank (A) is loaded with the memory word The old content
 * of the memory address is always read from the memory and never from cache.
 * Note: The execution time of this instruction includes two read-bus cycles
 * (The CPU uses semaphore cycles - see ND-110 Functional Description Manual
 * ND.06.027)
 *
 * @par Instruction
 * Opcode 140517 octal, mask 1111_1111_1111_1111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: RDUSP
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section RDUSP
 */
static void opcode_rdusp_read_physical_word_bypassing_cache(uint16_t operand)
{
    (void)operand;
    uint32_t bank;
    uint32_t offset;

    if (!check_priv())
    {
        return;
    }

    bank = (uint32_t)(gT & 0xFF);
    offset = (uint32_t)(gX & 0xFFFF);
    gA = (uint16_t)mms_read_physical_memory((int)((bank << 16) | offset), true);
}


/* ------------------------------------------ Bank registers (140700-140707) */

/**
 * @brief LASB - Load the A register with the contents of the segment-table bank (STBNK).
 *
 * (A) <--- (ea) ea = (B) + delta = STBNK entry delta: 3-bit displacement
 * added to B included in the instruction opcode (bits 5-3)
 *
 * @par Instruction
 * Opcode 140700 octal, mask 1111_1111_1100_0111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: LASB <displacement>
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LASB
 */
static void opcode_lasb_load_a_from_segment_table_bank(uint16_t operand)
{
    if (!check_priv())
    {
        return;
    }

    gA = (uint16_t)mms_read_physical_memory((int)nd110_bankgroup_phys(gSTBNK, gB, operand), true);
}

/**
 * @brief SASB - Store the A register contents in the segment table bank (STBNK).
 *
 * (ea) <--- (A) ea = (B) + delta = STBNK entry delta: 3-bit displacement
 * added to B included in the instruction opcode (bits 5-3)
 *
 * @par Instruction
 * Opcode 140701 octal, mask 1111_1111_1100_0111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: SASB <displacement>
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section SASB
 */
static void opcode_sasb_store_a_in_segment_table_bank(uint16_t operand)
{
    if (!check_priv())
    {
        return;
    }

    mms_write_physical_memory((int)nd110_bankgroup_phys(gSTBNK, gB, operand), gA, true);
}

/**
 * @brief LACB - Load the A register from the core map-table bank (CMBNK).
 *
 * (A) <--- (ea) ea = (B) + delta = CMBNK entry delta: 3-bit displacement
 * added to B included in the instruction opcode (bits 5-3)
 *
 * @par Instruction
 * Opcode 140702 octal, mask 1111_1111_1100_0111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: LACB <displacement>
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LACB
 */
static void opcode_lacb_load_a_from_core_map_bank(uint16_t operand)
{
    if (!check_priv())
    {
        return;
    }

    gA = (uint16_t)mms_read_physical_memory((int)nd110_bankgroup_phys(gCMBUK, gX, operand), true);
}

/**
 * @brief SACB - Store the A register in the core map table bank (CMBNK).
 *
 * (ea) <--- (A) ea = (B) + delta = CMBNK entry delta: 3-bit displacement
 * added to B included in the instruction opcode (bits 5-3)
 *
 * @par Instruction
 * Opcode 140703 octal, mask 1111_1111_1100_0111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: SACB <displacement>
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section SACB
 */
static void opcode_sacb_store_a_in_core_map_bank(uint16_t operand)
{
    if (!check_priv())
    {
        return;
    }

    mms_write_physical_memory((int)nd110_bankgroup_phys(gCMBUK, gX, operand), gA, true);
}

/**
 * @brief LXSB - Load the X register from the segment table bank (STBNK).
 *
 * (X) <--- (ea) ea = (B) + delta = STBNK entry delta: 3-bit displacement
 * added to B included in the instruction opcode (bits 5-3)
 *
 * @par Instruction
 * Opcode 140704 octal, mask 1111_1111_1100_0111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: LXSB <diplacement>
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LXSB
 */
static void opcode_lxsb_load_x_from_segment_table_bank(uint16_t operand)
{
    if (!check_priv())
    {
        return;
    }

    gX = (uint16_t)mms_read_physical_memory((int)nd110_bankgroup_phys(gSTBNK, gB, operand), true);
}

/**
 * @brief LXCB - Load the X register from the core table bank (CMBNK).
 *
 * (X) <--- (ea) ea = (B) + delta = CMBNK entry delta: 3-bit displacement
 * added to B included in the instruction opcode (bits 5-3)
 *
 * @par Instruction
 * Opcode 140705 octal, mask 1111_1111_1100_0111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: LXCB <displacement>
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LXCB
 */
static void opcode_lxcb_load_x_from_core_map_bank(uint16_t operand)
{
    if (!check_priv())
    {
        return;
    }

    gX = (uint16_t)mms_read_physical_memory((int)nd110_bankgroup_phys(gCMBUK, gX, operand), true);
}

/**
 * @brief SZSB - Store zero in the segment-table bank (STBNK).
 *
 * (ea) <--- 0 ea = (B) + delta = STBNK entry delta: 3-bit displacement added
 * to B included in the instruction opcode (bits 5-3)
 *
 * @par Instruction
 * Opcode 140706 octal, mask 1111_1111_1100_0111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: SZSB <displacement>
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section SZSB
 */
static void opcode_szsb_store_zero_in_segment_table_bank(uint16_t operand)
{
    if (!check_priv())
    {
        return;
    }

    mms_write_physical_memory((int)nd110_bankgroup_phys(gSTBNK, gB, operand), 0, true);
}

/**
 * @brief SZCB - Store zero in the core map-table bank (CMBNK).
 *
 * (ea) <--- 0 ea = (B) + delta = CMBNK entry delta: 3-bit displacement added
 * to B included in the instruction opcode (bits 5-3)
 *
 * @par Instruction
 * Opcode 140707 octal, mask 1111_1111_1100_0111.
 * Category: Control Instructions. Privilege: Privileged.
 * Format: SZCB <displacement>
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section SZCB
 */
static void opcode_szcb_store_zero_in_core_map_bank(uint16_t operand)
{
    if (!check_priv())
    {
        return;
    }

    mms_write_physical_memory((int)nd110_bankgroup_phys(gCMBUK, gX, operand), 0, true);
}


/* ------------------------------------ Monitor, examine and CPU information */

/**
 * @brief VERSN - ** ND-110/ND-120 ONLY**.
 *
 * Read ND-110 CPU version and installation number. This instruction is used
 * to read the version of ND-110 CPU installed. Three registers are loaded
 * simultaneously with information in the following format:
 *
 * @par Instruction
 * Opcode 140133 octal, mask 1111_1111_1111_1111.
 * Category: System/CPU Information. Privilege: User.
 * Format: VERSN
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section VERSN
 */
void opcode_versn_read_cpu_version(uint16_t operand)
{
    (void)operand;
    /*
     * A bits 8-11 select which of the SIXTEEN PROM bytes to return in D.
     * The array is now VERSN_PROM_SIZE (16) entries long; it used to be 15,
     * so index 15 read one byte past the end of the array.
     */
    int offset = (gA >> 8) & 0x0F;
    uint16_t a_in =
        gA; /* input A (PIL/offset selector) before VERSN overwrites it - for the trace below */

    // Set D register to the back-wiring PROM byte at the specified offset
    gD = g_versn.prom[offset];

    // ND-120/CX identity. Reapplied verbatim (constants + citations) from the validated
    // session-windows-work implementation; RetroCore CpuND100.Default*-aligned and TPE-validated.
    // The A register is a BIT-FIELD, NOT a flat print_version - the old `print_version << 4 | ALD`
    // produced A = 0x80C0 whose bits 15-13 = 100, which TPE cannot decode ("Print number: ????").
    // The ND-120 DELILAH-L microcode (ND-120-DELILAH-L.LISTING.txt:128-129) and the CPU board 3202
    // straps (IO_REG_41.v:121-130) assemble A (post-XOR, i.e. what the guest sees) as:
    //   bits 15-13 PRINT NUMBER  = 5   (101 binary => board print 3202)
    //   bits 12-8  ECO LEVEL     = 20  (straps 6,8,9 fitted)
    //   bit  7     CX/high-speed = 1   (CX fitted => "Cpu cycle: Fast")
    //   bits 6-4   PRINT RELEASE = 4   (100 binary => release "D")
    //   bits 3-0   ALD switch code
    // This is what makes TPE print a real "Print number: 3202" / "Print release: D" for the ND-120.
    if (g_current_cpu_type == ND120CX)
    {
        gA = (uint16_t)(((5 & 0x07) << 13)   /* PRINT NUMBER  = 5  => 3202 */
                        | ((20 & 0x1F) << 8) /* ECO LEVEL     = 20 (straps 6,8,9) */
                        | (1 << 7)           /* CX/high-speed = 1 */
                        | ((4 & 0x07) << 4)  /* PRINT RELEASE = 4  => "D" */
                        | (gALD & 0x0F));

        // T = microprogram version bits 0-14 (octal 14 = 0x000C = DELILAH-L "L") with bit 15 SET for
        // the ND-120. SINTRAN's SYSEVAL distinguishes ND-120 from ND-110 by this bit ("IF T BIT 17
        // THEN ..."); it is NEVER configurable - the CPU type always wins. 0x800C = octal 100014,
        // which TPE prints as "100014B".
        gT = 0x800C;
    }
    else
    {
        // Set A register with print version in upper 12 bits and preserve ALD in lower 4 bits
        gA = (g_versn.print_version << 4) | (gALD & 0x0F);

        // T register = microprogram version (bits 0-14) with bit 15 SET on an ND-120. SINTRAN's SYSEVAL
        // distinguishes ND-120 from ND-110 by this bit ("IF T BIT 17 THEN ..."); it is NEVER configurable.
        // On an ND-120/CX the if-branch above already forced T = 0x800C.
        gT = (uint16_t)((g_versn.microcode_version & 0x7FFF) | (versn_is_nd120() ? 0x8000 : 0));
    }

    /* Diagnostic (--log=cpu:debug): log every VERSN so an ND-110-vs-ND-120 boot can be diffed to see
     * why GCPUNR applies the PROM on one and not the other. Prints in octal: A_in (offset selector), the
     * PROM byte returned in D, and the assembled A / T. */
    LOG(LOG_CAT_CPU, LOG_DEBUG, "[VERSN] A_in=%06o off=%2d PC=%06o -> D=%06o A=%06o T=%06o", a_in,
        offset, gPC, gD, gA, gT);
}

/**
 * @brief GECO - GECO is a customer-specifed instruction which appears to be included as part of the standard instruction set from ND-100/CE and later. .
 *
 * The name comes from the customer, GECO (Geophysical Company of Norway)
 * SINTRAN III version L, and probably version K and possibly earlier, tests
 * for GECO as part of the startup.      The instruction is found in the
 * ND-110 microcode.
 *
 * @par Instruction
 * Opcode 142700 octal, mask 1111_1111_1111_1111.
 * Category: Undocumented Instructions. Privilege: User.
 * Format: GECO
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section GECO
 */
static void opcode_geco_customer_specified_instruction(uint16_t operand)
{
    (void)operand;
    /*
        * Microcode listing lists this instruction from micro address 004000. Page 99 in the PDF document "MICROPROGRAMLISTNING FOR ND-110_32 BIT VERSION K-Gandalf-OCR"
        * Page 134 listes the GECO offset address as 7427, assuming it is means opcode 1_427_nnn

        * https://www.ndwiki.org/wiki/GECO

        GECO is a customer-specifed instruction which appears to be included as part of the standard instruction set from ND-100/CE and later.
        The name comes from the customer, GECO (Geophysical Company of Norway).

        SINTRAN III version L, and probably version K and possibly earlier, tests for GECO as part of the startup.
        From this it looks like the registers B, D, A, and X are all used as input parameters. When all are set to 0 the instruction seems to do nothing.
    */
}

/**
 * @brief LWCS - Writable Control Store Instruction.
 *
 * This instruction is PRIVILEGED and only available to:   - programs running
 * in system mode (rings 2-3)   - programs running without memory protection
 * LWCS is a no-operation in the ND-110. The ND-110 is software compatible
 * but not microcode compatible and writing to the writable control store has
 * no meaning in the ND-110.  A no-operation is executed so that programs
 * written for the ND-100 and NORD-10 can continue.      Unused areas of the
 * microprogram can be read or written to using the TRR CS or TRA CS
 * instruction. Further information on the LWCS instruction for the ND-100
 * can be found in the ND-100 Reference Manual (ND-06.014).
 *
 * @par Instruction
 * Opcode 143500 octal, mask 1111_1111_1111_1111.
 * Category: privileged. Privilege: Privileged.
 * Format: LWCS
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section LWCS
 */
static void opcode_lwcs_load_writable_control_store(uint16_t instr)
{
    (void)instr;
    // LWCS is a no-operation on the ND-110
    // The ND-110 is software compatible but nor microcode compatible and writing to the writable control store has no meaning in the ND-110.
    // A no-operation is executed so that programs written for the ND-100 and NORD-10 can continue

    if (!check_priv())
    {
        return;
    }

    // noop
}

/**
 * @brief EXAM - Examine.
 *
 * Load the contents of the physical memory location, pointed to by the A and
 * D register contents, into the T register.
 *
 * @par Instruction
 * Opcode 150416 octal, mask 1111_1111_1111_1111.
 * Category: Physical Memory Control Instructions. Privilege: Privileged.
 * Format: EXAM
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section EXAM
 */
static void opcode_exam_examine_memory(uint16_t operand)
{
    (void)operand;
    if (!check_priv())
    {
        return;
    }

    // int fulladdress = (((unsigned int)gA) << 16) | (ushort)gD;
    unsigned int fulladdress = ((gA & 0xFF) << 16) | gD;
    gT = mms_read_physical_memory(fulladdress, true);
}

/**
 * @brief DEPO - Deposit.
 *
 * Store the contents of the T register in the physical memory location
 * pointed to by the A and D register contents.
 *
 * @par Instruction
 * Opcode 150417 octal, mask 1111_1111_1111_1111.
 * Category: Physical Memory Control Instructions. Privilege: Privileged.
 * Format: DEPO
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section DEPO
 */
static void opcode_depo_deposit_memory(uint16_t operand)
{
    (void)operand;
    if (!check_priv())
    {
        return;
    }

    unsigned int fulladdress = ((gA & 0xFF) << 16) | gD;
    mms_write_physical_memory(fulladdress, gT, true);
}

/**
 * @brief MON - The MON instruction is used in special different contexts when running under an operating system.
 *
 * It provides system call functionality through different monitor call
 * numbers.
 *
 * @par Instruction
 * Opcode 153000 octal, mask 1111_1111_0000_0000.
 * Category: Monitor Calls. Privilege: User.
 * Format: MON <monitor_call_number>
 *
 * @par Instruction word
 *   bits 15-8  opcode - The opcode determines what type of operation
 *       occurs
 *   bits 7-0  monitor_call_number - Monitor call number that determines
 *       the system function
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section MON
 */
static void opcode_mon_monitor_call(uint16_t operand)
{
    uint16_t monitor_number = (operand & 0x1ff);

    // TODO:MAYBE, add emulation layer here
    if (false)
    {
        // identfy montitor call and check if it should be intercepted!
    }
    else
    {
        if (CURR_LEVEL < 14)
        {
            if ((monitor_number & (1 << 8)) != 0)
            {
                monitor_number |= 0xFE00; // Sign extend
            }

            g_reg->reg[14][_T] = monitor_number;
            cpu_interrupt(14, 1 << 1); /* Monitor Call */
            gCHKIT = true;
        }
    }
}

/**
 * @brief HALT - see the notes below.
 *
 * @param operand The full instruction word as fetched.
 */
static void opcode_halt(uint16_t operand)
{
    (void)operand;
    printf("\r\nHALT opcode at PIL[%d] PC[%6o] A[%6o]\r\n", gPIL, gPC, gA);
    g_cpu_exit_code = (int)(short)gA;
    cpu_set_run_mode(CPU_STOPPED);
}


/* ------------------------------------------------- Kept but not dispatched */

/**
 * @brief MOVB - Move byte.
 *
 * This instruction moves a block of bytes from the memory location addressed
 * by the source operand to that of the memory location addressed by the
 * destination operand. After execution, bit 15 of the D and T registers
 * point to the end of the field that has been moved. The field length of the
 * D register (source) equals zero and the T register (destination) field
 * length is equal to the number of bytes moved.
 *
 * @par Instruction
 * Opcode 140131 octal, mask 1111_1111_1111_1111.
 * Category: Byte Instructions. Privilege: User.
 * Format: MOVB
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section MOVB
 */
void opcode_movb_move_byte_buggy(uint16_t instr)
{
    (void)instr;
    do_move_bytes(false);
}

/**
 * @brief MOVBF - Move bytes forward.
 *
 * This instruction moves a block of bytes from the memory location addressed
 * by the source operand to that of the memory location addressed by the
 * destination operand. After execution, bit 15 of the D and T registers
 * point to the end of the field that has been moved. The field length of the
 * D register (source) equals zero and the T register (destination) field
 * length is equal to the number of bytes moved.
 *
 * @par Instruction
 * Opcode 140132 octal, mask 1111_1111_1111_1111.
 * Category: Byte Instructions. Privilege: User.
 * Format: MOVBF
 *
 * @par Instruction word
 *   bits 15-0  opcode - The opcode determines what type of operation
 *       occurs
 *
 * @param operand The full instruction word as fetched.
 * @see docs/cpu_documentation.md section MOVBF
 */
void opcode_movbf_move_bytes_forward_buggy(uint16_t instr)
{
    (void)instr;
    do_move_bytes(true);
}


void cpu_sub_a_mem(uint16_t eff_addr, bool use_apt)
{
    int temp, data, oldreg;
    oldreg = gA;
    data = cpu_memory_read(eff_addr, use_apt);
    temp = gA - data;
    /*
     * FIXME - ADD FLAG HANDLING CORRECTLY FOR C,O,Q FLAGS (CHECK AGAIN THINK WE MIGHT HAVE SUBTLE BUGS)
     */
    if ((temp > 0xFFFF) || (temp < 0))
    {
        cpu_setbit(_STS, STS_CARRY, 0);
        if ((oldreg & 0x8000) && (data & 0x8000) && !(temp & 0x8000))
        {
            cpu_setbit(_STS, STS_DYNAMIC_OVERFLOW, 1);
        }
        else
        {
            cpu_setbit(_STS, STS_DYNAMIC_OVERFLOW, 0);
        }
    }
    else
    {
        cpu_setbit(_STS, STS_CARRY, 1);
        if (!(oldreg & 0x8000) && !(data & 0x8000) && (temp & 0x8000))
        {
            cpu_setbit(_STS, STS_DYNAMIC_OVERFLOW, 1);
        }
        else
        {
            cpu_setbit(_STS, STS_DYNAMIC_OVERFLOW, 0);
        }
    }

    gA = (temp & 0xFFFF);
}

/*
 * RDIV
 */
void cpu_rdiv_org(uint16_t instr)
{
    int16_t divider;
    int dividend;
    div_t result3; /* stdlib.h */
    /* :TODO: Apparently Carry can be set too. CHECK that... Might be RAD=1??? */
    /* Overflow and division with zero also need to be fixed!! */
    /* :NOTE: The way it is described in the manual, we assume this is a fraction (numerator/denominator and return a quotient and remainder as per manual */
    divider = ((instr & 0x0038) >> 3) ? (int16_t)g_reg->reg[gPIL][((instr & 0x0038) >> 3)] : 0;

    if (divider == 0)
    {
        // Division by zero
        cpu_setbit(_STS, STS_ERROR_INDICATOR, 1);
        return;
    }

    dividend = ((int)gA << 16) | gD;
    result3 = div(dividend, divider);
    gA = result3.quot;
    gD = result3.rem;
}

/// <summary>
/// RDIV - Integer inter-register divide
/// AD/<sr> -> A<- (Quotient) and D<- (Remainder)
///
/// Format: RDIV<sr>
///
/// Code: 141 600
///
/// The 32 bit signed integer contained in the double accumulator AD is divided by the contents of the register in the<sr> fieid, with the quotient in the A register
/// and the remainder in the D register, i.e., AD/sr = A< (quotient) and D<(remainder).
/// The sign of the remainder is always equal to the sign of the dividend (AD). The destination field of the instruction is not used.
///
/// If the division causes overflow, the error indicator Z is set to one.
/// The numbers are considered as fixed point integers with the fixed point after the rightmost position.
///
/// Divide double accumulator with source register.Quotient in A, remainder in D (AD= A*(sr)+ D)
/// A:= AD/(sr)

/// Affected: (A), (D), Z,C, O, Q
/// </summary>
static void rdiv(uint16_t instr)
{
    /* FAITHFUL to RASK RDIV6 (CS 000430-000463); oracle-validated (RetroCore 4c29170d1). The success
     * "loop path" results are UNCHANGED (what SINTRAN depends on); only the ERROR paths and the
     * negative-dividend C/O/Q flags are corrected. Divide-by-zero / true overflow leave the dividend's
     * two's-complement MAGNITUDE in A/D (minus |divisor| in the high word) and OR-set Z; the ND manual's
     * "divide-by-zero -> A/D unchanged" is an abstraction (magnitude == original for a POSITIVE dividend,
     * so they coincide there - which is why the old code passed only for positive dividends). */
    int dividend = ((int)gA << 16) | (int)gD;
    short divisor = ((instr & 0x0038) >> 3) ? (short)g_reg->reg[gPIL][((instr & 0x0038) >> 3)] : 0;

    int dividend_negative = (dividend < 0);
    uint16_t orig_low = gD; /* low word the microcode negates at CS 000434 (`-B`) */

    /* CS 000434 (NEGATIVE DIVIDEND): negate the 32-bit dividend to its magnitude; STS,EA latches the
     * flags of the LOW-word (D) two's-complement negation. This precedes the STS save that brackets the
     * loop, so these flags PERSIST on both the loop and error paths. Positive dividend: C/O/Q untouched. */
    if (dividend_negative)
    {
        int neg_ovf =
            (orig_low == 0x8000); /* only 0x8000 overflows a 16-bit two's-complement negate */
        cpu_setbit(_STS, STS_CARRY, (orig_low == 0)); /* carry-out of -Dlow set iff Dlow == 0 */
        cpu_setbit(_STS, STS_DYNAMIC_OVERFLOW, neg_ovf);
        if (neg_ovf)
        {
            cpu_setbit(_STS, STS_STATIC_OVERFLOW, 1); /* static overflow is sticky */
        }
    }

    /* Operand magnitudes via UNSIGNED arithmetic (correct even for 0x80000000 / -32768). */
    unsigned int dividend_mag =
        dividend_negative ? (0u - (unsigned int)dividend) : (unsigned int)dividend;
    uint16_t divisor_mag =
        (uint16_t)((divisor < 0) ? (0u - (unsigned int)(int)divisor) : (unsigned int)(int)divisor);
    uint16_t dividend_mag_high = (uint16_t)(dividend_mag >> 16);

    /* CS 000436 RDIV2 overflow PRE-CHECK: A := |dividend|_high - |divisor| (written back, ALUD,B). If
     * |dividend|_high >= |divisor| (unsigned, no borrow) OR divisor == 0, the quotient cannot fit 16
     * bits, so branch to RDIVZ BEFORE the loop: OR-set Z, leave A = that subtract and D = |dividend| low.
     * The quotient/remainder are NEVER computed on this path. */
    if (divisor_mag == 0 || dividend_mag_high >= divisor_mag)
    {
        gA = (uint16_t)(dividend_mag_high - divisor_mag);
        gD = (uint16_t)(dividend_mag & 0xFFFF);
        cpu_setbit(_STS, STS_ERROR_INDICATOR, 1);
        return;
    }

    /* LOOP PATH (|dividend|_high < |divisor|): the quotient magnitude fits 16 bits. */
    unsigned int quotient_mag = dividend_mag / divisor_mag;
    unsigned int remainder_mag = dividend_mag % divisor_mag;

    /* Quotient sign = sign(AD) XOR sign(SRCE); remainder sign = dividend sign (CS 000456). */
    int quotient_negative = dividend_negative ^ (divisor < 0);
    gA = quotient_negative ? (uint16_t)(0u - quotient_mag) : (uint16_t)quotient_mag;
    gD = dividend_negative ? (uint16_t)(0u - remainder_mag) : (uint16_t)remainder_mag;

    /* CS 000457 RDIV5 sign check: Z on SIGNED overflow (positive q > 32767, negative q > 32768 - so a
     * -32768 quotient is VALID and does NOT set Z, unlike a naive |q| >= 32768 test). */
    if (quotient_negative ? (quotient_mag > 0x8000u) : (quotient_mag > 0x7FFFu))
    {
        cpu_setbit(_STS, STS_ERROR_INDICATOR, 1);
    }
}

/*
 * RMPY
 */
void cpu_rmpy_org(uint16_t instr)
{
    /* :TODO: Apparently Carry can be set too. CHECK that... Might be RAD=1??? */
    int a, b, result;
    a = ((instr & 0x0038) >> 3) ? (int)g_reg->reg[gPIL][((instr & 0x0038) >> 3)] : 0;
    b = (instr & 0x0007) ? (int)g_reg->reg[gPIL][(instr & 0x0007)] : 0;
    result = a * b;
    if (abs(result) > INT_MAX)
    { /* Set O and Q */
        cpu_setbit(_STS, STS_DYNAMIC_OVERFLOW, 1);
        cpu_setbit(_STS, STS_STATIC_OVERFLOW, 1);
    }
    else
    {
        ; //: TODO: Carry???;
        cpu_setbit(_STS, STS_DYNAMIC_OVERFLOW, 0);
        cpu_setbit(_STS, STS_STATIC_OVERFLOW, 0);
    }
    gA = (int16_t)((result & 0xffff0000) >> 16);
    gD = (int16_t)(result & 0x0000ffff);
}

/// <summary>
/// RMPY - Integer inter-register multiply
/// AD <- dr * sr
///
/// Format: RMPY<sr><dr>
///
/// Code: 141 200
///
/// The <sr> and <dr> fields are used to specify the two operands to be mutiplied (represented as two's complement integers), the codes are the same as for ROP.
/// The result is a 32 bit signed integer which will be placed in the A and D registers with the 16 most significant bits in the A register and the 16 least significant bits in the D register.
///
/// Multiply source with destination.Result in double accumulator
/// AD: = (sr)*(dr)
///
/// Affected: (A),(D), C,O,Q
/// </summary>
static void rmpy(uint16_t instr)
{
    int minus_cnt = 0;
    short source_value =
        (short)((instr & 0x0038) >> 3) ? (short)g_reg->reg[gPIL][((instr & 0x0038) >> 3)] : 0;
    short dest_value = (short)(instr & 0x0007) ? (short)g_reg->reg[gPIL][(instr & 0x0007)] : 0;

    // Use int for absolute values to avoid overflow when negating -32768
    int abs_src = (int)source_value;
    int abs_dst = (int)dest_value;

    if (abs_src < 0)
    {
        abs_src = -abs_src;
        minus_cnt++;
    }

    if (abs_dst < 0)
    {
        abs_dst = -abs_dst;
        minus_cnt++;
    }

    int result = abs_src * abs_dst; /* magnitude of the product (always non-negative here) */

    /* STATUS FLAGS from the RASK microcode, NOT "product > 16 bits" (that was a guess and is wrong).
     * RMPY runs its own routine RMPY4 (CS 004350-004363): a SAME-SIGN result writes NO status (C/O/Q/M
     * left unchanged); an OPPOSITE-SIGN result negates the product and STS,EA (CS 004362) latches the
     * flags of the LOW-word two's-complement negation: C = carry-out (low word == 0), Q = overflow
     * (low word == 0x8000), O = O OR that overflow. Oracle-validated (RetroCore 135a2ff28). */
    if (minus_cnt == 1)
    {
        int low_word = result & 0xFFFF; /* low word of the positive magnitude (what -Q negates) */
        int ovf = (low_word == 0x8000); /* only 0x8000 overflows a 16-bit two's-complement negate */
        cpu_setbit(_STS, STS_CARRY, (low_word == 0)); /* carry-out of -Q is set iff Q == 0 */
        cpu_setbit(_STS, STS_DYNAMIC_OVERFLOW, ovf);
        if (ovf)
        {
            cpu_setbit(_STS, STS_STATIC_OVERFLOW, 1); /* static overflow is sticky (OVF | O) */
        }
        result = -result; /* sign-correct the product */
    }
    /* else (minusCnt 0 or 2): same-sign result -> microcode writes NO status; leave C/O/Q/M unchanged. */

    // set A and D registers
    gA = (uint16_t)((result >> 16) & 0xFFFF);
    gD = (uint16_t)(result & 0xFFFF);
}

/*
 * MPY
 */
static void mpy(uint16_t operand)
{
    int a, b, result;
    a = (int16_t)gA;

    gEA = cpu_get_effective_addr(operand, &gUseAPT);
    uint16_t mem = cpu_memory_read(gEA, gUseAPT);
    b = (int16_t)mem;

    cpu_setbit(_STS, STS_DYNAMIC_OVERFLOW, 0);

    result = a * b;

    if (abs(result) > 32767)
    { /* Set O and Q */
        cpu_setbit(_STS, STS_DYNAMIC_OVERFLOW, 1);
        cpu_setbit(_STS, STS_STATIC_OVERFLOW, 1);
    }
    gA = (int16_t)result;
}


/************************ BCD instructions *************************/

/* BCD registers and helper functions */
static uint16_t s_bcd_d1 = 0;
static uint16_t s_bcd_d2 = 0;

void cpu_get_bcd(uint16_t address)
{
    s_bcd_d1 = cpu_memory_read(address, true);
    s_bcd_d2 = cpu_memory_read((address + 1) & 0xFFFF, true);
}

void cpu_store_bcd(uint16_t address)
{
    cpu_memory_write(address, s_bcd_d1, true, WRITEMODE_WORD);
    cpu_memory_write((address + 1) & 0xFFFF, s_bcd_d2, true, WRITEMODE_WORD);
}

/* ADDD, SUBD, COMD, PACK, UPACK, SHDE are in bcd.c */


/*************************** INITIALIZATION ***************************/

static void instruction_add(int opcode, void *funcpointer)
{
    if (g_instr_funcs[opcode] != NULL)
    {
        LOG(LOG_CAT_CPU, LOG_WARN, "Overwriting instruction %06o", opcode);
    }

    g_instr_funcs[opcode] = funcpointer;
}

static void instruction_add_range(int start, int stop, void *funcpointer)
{
    int i;
    for (i = start; i <= stop; i++)
    {
        if (g_instr_funcs[i] != NULL)
        {
            LOG(LOG_CAT_CPU, LOG_WARN, "Overwriting instruction %06o", i);
        }

        g_instr_funcs[i] = funcpointer;
    }
    return;
}

static void instruction_add_mask(int opcode, int mask, void *funcpointer)
{
    int i;
    int signature = opcode & mask;


    for (i = opcode; i <= 0xFFFF; i++)
    {
        if ((i & mask) == signature)
        {
            if (g_instr_funcs[i] != NULL)
            {
                LOG(LOG_CAT_CPU, LOG_WARN, "Overwriting instruction %06o with %06o", i, opcode);
            }

            g_instr_funcs[i] = funcpointer;
        }
    }
    return;
}

/*
 * Add IO handler addresses in this function
 * This also thus actually acts as the new instruction parser also.
 */
/* size exemption: opcode table (house rule 7.1) - one instruction_add per opcode group */
void cpu_instructions(void) // NOLINT(readability-function-size)
{
    //instruction_add_range(0000000, 0177777, &illegal_instr); /* First make all instructions by default point to illegal_instr  */

    // instruction_add_range(0000000, 0003777, &opcode_stz_store_zero); /* STZ  */
    instruction_add_mask(0000000, 0xF800, &opcode_stz_store_zero);

    // instruction_add_range(0004000, 0007777, &opcode_sta_store_a_register); /* STA  */
    instruction_add_mask(0004000, 0xF800, &opcode_sta_store_a_register);

    // instruction_add_range(0010000, 0013777, &opcode_stt_store_t_register); /* STT  */
    instruction_add_mask(0010000, 0xF800, &opcode_stt_store_t_register);

    // instruction_add_range(0014000, 0017777, &opcode_stx_store_x_register); /* STX  */
    instruction_add_mask(0014000, 0xF800, &opcode_stx_store_x_register);

    // instruction_add_range(0020000, 0023777, &opcode_std_store_double_word); /* STD  */
    instruction_add_mask(0020000, 0xF800, &opcode_std_store_double_word);

    // instruction_add_range(0024000, 0027777, &opcode_ldd_load_double_word); /* LDD  */
    instruction_add_mask(0024000, 0xF800, &opcode_ldd_load_double_word);

    // instruction_add_range(0030000, 0033777, &opcode_stf_store_floating_accumulator); /* STF  */
    instruction_add_mask(0030000, 0xF800, &opcode_stf_store_floating_accumulator);

    // instruction_add_range(0034000, 0037777, &opcode_ldf_load_floating_accumulator); /* LDF  */
    instruction_add_mask(0034000, 0xF800, &opcode_ldf_load_floating_accumulator);

    // instruction_add_range(0040000, 0043777, &opcode_min_memory_increment_and_skip_if_zero); /* MIN  */
    instruction_add_mask(0040000, 0xF800, &opcode_min_memory_increment_and_skip_if_zero);

    // instruction_add_range(0044000, 0047777, &opcode_lda_load_a_register); /* LDA  */
    instruction_add_mask(0044000, 0xF800, &opcode_lda_load_a_register);

    // instruction_add_range(0050000, 0053777, &opcode_ldt_load_t_register); /* LDT  */
    instruction_add_mask(0050000, 0xF800, &opcode_ldt_load_t_register);

    // instruction_add_range(0054000, 0057777, &opcode_ldx_load_x_register); /* LDX  */
    instruction_add_mask(0054000, 0xF800, &opcode_ldx_load_x_register);

    // instruction_add_range(0060000, 0063777, &opcode_add_add_to_a_register); /* ADD  */
    instruction_add_mask(0060000, 0xF800, &opcode_add_add_to_a_register);

    // instruction_add_range(0064000, 0067777, &opcode_sub_subtract_from_a_register); /* SUB  */
    instruction_add_mask(0064000, 0xF800, &opcode_sub_subtract_from_a_register);

    // instruction_add(0070000, 0073777, &opcode_and_logical_and_to_a_register); /* AND  */
    instruction_add_mask(0070000, 0xF800, &opcode_and_logical_and_to_a_register);

    // instruction_add_range(0074000, 0077777, &opcode_ora_logical_or_to_a_register); /* ORA  */
    instruction_add_mask(0074000, 0xF800, &opcode_ora_logical_or_to_a_register);

    // instruction_add_range(0100000, 0103777, &opcode_fad_add_to_floating_accumulator); /* FAD  */
    instruction_add_mask(0100000, 0xF800, &opcode_fad_add_to_floating_accumulator);

    // instruction_add_range(0104000, 0107777, &opcode_fsb_subtract_from_floating_accumulator); /* FSB  */
    instruction_add_mask(0104000, 0xF800, &opcode_fsb_subtract_from_floating_accumulator);

    // instruction_add_range(0110000, 0113777, &opcode_fmu_multiply_floating_accumulator); /* FMU  */
    instruction_add_mask(0110000, 0xF800, &opcode_fmu_multiply_floating_accumulator);

    // instruction_add_range(0114000, 0117777, &opcode_fdv_divide_floating_accumulator); /* FDV  */
    instruction_add_mask(0114000, 0xF800, &opcode_fdv_divide_floating_accumulator);

    // instruction_add_range(0120000, 0123777, &mpy);       /* MPY  */
    instruction_add_mask(0120000, 0xF800, &mpy);

    // instruction_add_range(0124000, 0127777, &opcode_jmp_jump_unconditional); /* JMP  */
    instruction_add_mask(0124000, 0xF800, &opcode_jmp_jump_unconditional);

    // instruction_add_range(0134000, 0137777, &opcode_jpl_jump_if_last_result_positive); /* JPL  */
    instruction_add_mask(0134000, 0xF800, &opcode_jpl_jump_if_last_result_positive);

    // CJPs - Conditional jumps
    // instruction_add_range(0130000, 0130377, &opcode_jap_jump_if_a_positive); /* JAP */
    instruction_add_mask(0130000, 0xFF00, &opcode_jap_jump_if_a_positive);

    // instruction_add_range(0130400, 0130777, &opcode_jan_jump_if_a_negative); /* JAN */
    instruction_add_mask(0130400, 0xFF00, &opcode_jan_jump_if_a_negative);

    // instruction_add_range(0131000, 0131377, &opcode_jaz_jump_if_a_zero); /* JAZ */
    instruction_add_mask(0131000, 0xFF00, &opcode_jaz_jump_if_a_zero);

    // instruction_add_range(0131400, 0131777, &opcode_jaf_jump_if_a_not_zero); /* JAF */
    instruction_add_mask(0131400, 0xFF00, &opcode_jaf_jump_if_a_not_zero);

    // instruction_add_range(0132000, 0132377, &opcode_jpc_increment_x_and_jump_if_x_positive); /* JPC */
    instruction_add_mask(0132000, 0xFF00, &opcode_jpc_increment_x_and_jump_if_x_positive);

    // instruction_add_range(0132400, 0132777, &opcode_jnc_increment_x_and_jump_if_x_negative); /* JNC */
    instruction_add_mask(0132400, 0xFF00, &opcode_jnc_increment_x_and_jump_if_x_negative);

    // instruction_add_range(0133000, 0133377, &opcode_jxz_jump_if_x_zero); /* JXZ */
    instruction_add_mask(0133000, 0xFF00, &opcode_jxz_jump_if_x_zero);

    // instruction_add_range(0133400, 0133777, &opcode_jxn_jump_if_x_negative); /* JXN */
    instruction_add_mask(0133400, 0xFF00, &opcode_jxn_jump_if_x_negative);

    instruction_add_mask(0140000, 0xF8C0, &opcode_skp_skip_next_if_condition);

    // BCD (CX)
    instruction_add(0140120, &opcode_addd_add_two_decimal_operands);      /* ADDD  */
    instruction_add(0140121, &opcode_subd_subtract_two_decimal_operands); /* SUBD  */
    instruction_add(0140122, &opcode_comd_compare_two_decimal_operands);  /* COMD  */
    instruction_add(0140124, &opcode_pack_convert_to_decimal);            /* PACK  */
    instruction_add(0140125, &opcode_unpack_convert_from_decimal);        /* UPACK */
    instruction_add(0140126, &opcode_shde_decimal_shift);                 /* SHDE  */

    instruction_add(0140123, &do_tset); /* TSET  */
    instruction_add(0140127, &do_rdus); /* RDUS  */

    { // CE; CX

        instruction_add(0140130, &opcode_bfill_byte_fill);          /* BFILL */
        instruction_add(0140131, &opcode_movb_move_byte);           /* MOVB  */
        instruction_add(0140132, &opcode_movbf_move_bytes_forward); /* MOVBF */

        // instruction_add(0140131, &opcode_movb_move_byte_buggy);  /* MOVB  */
        // instruction_add(0140132, &opcode_movbf_move_bytes_forward_buggy); /* MOVBF */
    }

    switch (g_current_cpu_type)
    {
    case ND110:
    case ND110CE:
    case ND110CX:
    case ND110PCX:
    case ND120CX: /* ND-120 is instruction-set-identical to the ND-110/CX (VERSN + the 140133 / */
                  /* 140500-140517 / 14070x ND-110 groups). Without VERSN here it traps illegal, */
        /* and TPE cannot read the ND-120/CX identity. Reapplied from session-windows-work. */
        instruction_add(0140133, &opcode_versn_read_cpu_version); /* VERSN - ND110+ */
        break;
    default:
        break;
    }

    { // CE; CX

        instruction_add(0140134, &opcode_init_initialize_stack);   /* INIT  */
        instruction_add(0140135, &opcode_entr_enter_stack);        /* ENTR  */
        instruction_add(0140136, &opcode_leave_leave_stack);       /* LEAVE */
        instruction_add(0140137, &opcode_eleav_error_leave_stack); /* ELEAV */
    }
    // instruction_add(0140200, 0140277, &illegal_instr); /* USER1 (microcode defined by user or illegal instruction otherwise) */
    instruction_add(0140200, &opcode_halt); /* HALT - emulator exit, A=exit code */

    switch (g_current_cpu_type)
    {
    case ND110:
    case ND110CE:
    case ND110CX:
    case ND110PCX:
    case ND120CX: /* ND-120 is instruction-set-identical to the ND-110/CX - same ND-110 opcode group. */
        // ALL are priveleged!
        instruction_add(0140500,
                        &opcode_wglob_initialize_global_pointers); /* WGLOB - ND110 Specific */
        instruction_add(0140501,
                        &opcode_rglob_examine_global_pointers); /* RGLOB - ND110 Specific */
        instruction_add(0140502,
                        &opcode_inspl_insert_page_in_page_list); /* INSPL - ND110 Specific */
        instruction_add(0140503,
                        &opcode_rempl_remove_page_from_page_list); /* REMPL - ND110 Specific */
        instruction_add(
            0140504,
            &opcode_cnrek_clear_non_reentrant_pages_sintran_k); /* CNREK - ND110 Specific */
        instruction_add(0140505,
                        &opcode_clpt_clear_segment_from_page_tables); /* CLPT  - ND110 Specific */
        instruction_add(0140506,
                        &opcode_enpt_enter_segment_in_page_tables); /* ENPT  - ND110 Specific */
        instruction_add(
            0140507,
            &opcode_rept_enter_reentrant_segment_in_page_tables); /* REPT  - ND110 Specific */
        instruction_add(
            0140510,
            &opcode_lbit_load_bit_accumulator_from_logical_memory); /* LBIT  - ND110 Specific */
        /*
         * 140511 LBITP and 140512 SBIT were MISSING from this table entirely (not even
         * registered as unimplemented) - see ND-06.029.1 EN and RetroCore
         * Instructions.cs (hasND110Group), which registers the full 140510-140517 run.
         */
        instruction_add(
            0140511,
            &opcode_lbitp_load_bit_accumulator_from_physical_memory); /* LBITP - ND110 Specific */
        instruction_add(
            0140512,
            &opcode_sbit_store_bit_accumulator_to_logical_memory); /* SBIT  - ND110 Specific */
        instruction_add(
            0140513,
            &opcode_sbitp_store_bit_accumulator_to_physical_memory); /* SBITP - ND110 Specific */
        instruction_add(0140514,
                        &opcode_lbytp_load_byte_from_physical_memory); /* LBYTP - ND110 Specific */
        instruction_add(0140515,
                        &opcode_sbytp_store_byte_in_physical_memory); /* SBYTP - ND110 Specific */
        instruction_add(0140516,
                        &opcode_tsetp_test_and_set_physical_word); /* TSETP - ND110 Specific */
        instruction_add(
            0140517, &opcode_rdusp_read_physical_word_bypassing_cache); /* RDUSP - ND110 Specific */

        break;
    default:
        // instruction_add_range(0140500, 0140577, &illegal_instr); /* USER2 (microcode defined by user or illegal instruction otherwise) */
        break;
    }

    instruction_add_mask(0140600, 0xFFC0, &do_exr); /* EXR */
    switch (g_current_cpu_type)
    {
    case ND110:
    case ND110CE:
    case ND110CX:
    case ND110PCX:
    case ND120CX: /* ND-120 is instruction-set-identical to the ND-110/CX - same ND-110 opcode group. */
        /*
         * ALL are priveleged!
         *
         * These carry a 3-bit displacement in bits 3-5 of the opcode (14070x + delta<<3),
         * so they MUST be registered with mask 0xFFC7 (bits 3-5 left free) - registering
         * only the bare 14070x word left the 56 displaced encodings undecoded.
         * Note also that 0140703 was mislabelled "SASB" here; it is SACB.
         */
        instruction_add_mask(
            0140700, 0xFFC7,
            &opcode_lasb_load_a_from_segment_table_bank); /* LASB - ND110 Specific */
        instruction_add_mask(
            0140701, 0xFFC7,
            &opcode_sasb_store_a_in_segment_table_bank); /* SASB - ND110 Specific */
        instruction_add_mask(0140702, 0xFFC7,
                             &opcode_lacb_load_a_from_core_map_bank); /* LACB - ND110 Specific */
        instruction_add_mask(0140703, 0xFFC7,
                             &opcode_sacb_store_a_in_core_map_bank); /* SACB - ND110 Specific */
        instruction_add_mask(
            0140704, 0xFFC7,
            &opcode_lxsb_load_x_from_segment_table_bank); /* LXSB - ND110 Specific */
        instruction_add_mask(0140705, 0xFFC7,
                             &opcode_lxcb_load_x_from_core_map_bank); /* LXCB - ND110 Specific */
        instruction_add_mask(
            0140706, 0xFFC7,
            &opcode_szsb_store_zero_in_segment_table_bank); /* SZSB - ND110 Specific */
        instruction_add_mask(0140707, 0xFFC7,
                             &opcode_szcb_store_zero_in_core_map_bank); /* SZCB - ND110 Specific */
        break;
    default:
        break;
    }

    if (true)
    {
        // ND100-CX and ND110-CX only

        instruction_add(0140300, &opcode_setpt_set_page_tables);              /* SETPT */
        instruction_add(0140301, &opcode_clept_clear_page_tables);            /* CLEPT */
        instruction_add(0140302, &opcode_clnreent_clear_non_reentrant_pages); /* CLNREENT */
        instruction_add(0140303, &opcode_chreent_pages);                      /* CHREENT-PAGES */
        instruction_add(0140304, &opcode_clepu_clear_page_tables_and_collect_page_used); /* CLEPU */
    }
    instruction_add_mask(0141200, 0xFFC0, &rmpy);                                   /* RMPY */
    instruction_add_mask(0141600, 0xFFC0, &rdiv);                                   /* RDIV */
    instruction_add_mask(0142200, 0xFFC0, &opcode_lbyt_load_byte_to_a_register);    /* LBYT */
    instruction_add_mask(0142600, 0xFFC0, &opcode_sbyt_store_byte_from_a_register); /* SBYT */

    // CX instructions
    instruction_add(
        0142700, &opcode_geco_customer_specified_instruction); /* GECO - Undocumented instruction */
    instruction_add_mask(0143100, 0xFFC0, &do_movew);          /* MOVEW */
    instruction_add_mask(0143200, 0xFFC0, &opcode_mix3_multiply_index_by_three); /* MIX3 */

    instruction_add_mask(0143300, 0xFFC7, &opcode_ldatx_load_a_register_t_x_relative);   /* LDATX */
    instruction_add_mask(0143301, 0xFFC7, &opcode_ldxtx_load_x_register_t_x_relative);   /* LDXTX */
    instruction_add_mask(0143302, 0xFFC7, &opcode_lddtx_load_double_word_t_x_relative);  /* LDDTX */
    instruction_add_mask(0143303, 0xFFC7, &opcode_ldbtx_load_b_register_t_x_relative);   /* LDBTX */
    instruction_add_mask(0143304, 0xFFC7, &opcode_statx_store_a_register_t_x_relative);  /* STATX */
    instruction_add_mask(0143305, 0xFFC7, &opcode_stztx_store_zero_t_x_relative);        /* STZTX */
    instruction_add_mask(0143306, 0xFFC7, &opcode_stdtx_store_double_word_t_x_relative); /* STDTX */

    instruction_add(0143500, &opcode_lwcs_load_writable_control_store); /* LWCS */

    instruction_add(0143604, &opcode_ident_identify_interrupting_device); /* IDENT PL10 */
    instruction_add(0143611, &opcode_ident_identify_interrupting_device); /* IDENT PL11 */
    instruction_add(0143622, &opcode_ident_identify_interrupting_device); /* IDENT PL12 */
    instruction_add(0143643, &opcode_ident_identify_interrupting_device); /* IDENT PL13 */

    instruction_add_range(0144000, 0147777, &regop);                /* --ROPS-- */
    instruction_add_mask(0150000, 0xFFF0, &do_tra);                 /* TRA */
    instruction_add_mask(0150100, 0xFFF0, &do_trr);                 /* TRR */
    instruction_add_mask(0150200, 0xFFF0, &do_mcl);                 /* MCL */
    instruction_add_mask(0150300, 0xFFF0, &do_mst);                 /* MST */
    instruction_add(0150400, &opcode_opcom_operator_communication); /* OPCOM */
    instruction_add(0150401, &opcode_iof_interrupt_off);            /* IOF */
    instruction_add(0150402, &opcode_ion_interrupt_on);             /* ION */
    switch (g_current_cpu_type)
    {
    case ND110PCX:
        /* ND110 Butterfly only instruction */
        instruction_add(0150403, &unimplemented_instr); /* RTNSIM (SECRE) */
        break;
    default:
        break;
    }
    instruction_add(0150404, &opcode_pof_paging_off);                  /* POF */
    instruction_add(0150405, &opcode_piof_paging_and_interrupt_off);   /* PIOF */
    instruction_add(0150406, &opcode_sex_set_extended_address_mode);   /* SEX */
    instruction_add(0150407, &opcode_rex_reset_extended_address_mode); /* REX */
    instruction_add(0150410, &opcode_pon_paging_on);                   /* PON */
    instruction_add(0150412, &opcode_pion_paging_and_interrupt_on);    /* PION */

    instruction_add(0150415, &opcode_ioxt_exchange_with_io_system_t_addressed); /* IOXT */
    instruction_add(0150416, &opcode_exam_examine_memory);                      /* EXAM */
    instruction_add(0150417, &opcode_depo_deposit_memory);                      /* DEPO */

    instruction_add_mask(0151000, 0xFF00, &do_wait); /* WAIT - Range 151000 - 151377 */
    instruction_add_mask(0151400, 0xFF00, &opcode_nlz_normalize_floating_accumulator); /* NLZ */
    instruction_add_mask(0152000, 0xFF00, &opcode_dnz_denormalize_to_fixed_point);     /* DNZ */
    instruction_add_mask(0152402, 0xFF07, &opcode_srb_store_register_block);           /* SRB */
    instruction_add_mask(0152600, 0xFF07, &opcode_lrb_load_register_block);            /* LRB */
    instruction_add_mask(0153000, 0xFF00,
                         &opcode_mon_monitor_call); /* MON  - Range 153000-153377  */
    instruction_add_mask(0153400, 0xFF80, &opcode_irw_inter_register_write); /* IRW */
    instruction_add_mask(0153600, 0xFF80, &opcode_irr_inter_register_read);  /* IRR */

    // instruction_add_range(0154000, 0157777, &opcode_shift_group); /* SHT, SHD, SHA, SAD */  /* NOTE: this is actually a ND1 instruction, so need to check which NDs implement it later */
    instruction_add_mask(0154000, 0x7980, &opcode_shift_group); // SHT
    instruction_add_mask(0154200, 0x7980, &opcode_shift_group); // SHD
    instruction_add_mask(0154400, 0x7980, &opcode_shift_group); // SHA
    instruction_add_mask(0154600, 0x7980, &opcode_shift_group); // SAD

    // IOT Range 0160000 - 0163777
    instruction_add_mask(
        0160000, 0xF800,
        &opcode_iot_nord_1_legacy_do_not_use); /* IOT  - ND1 specific, but exists on all CPU's*/

    instruction_add_mask(0164000, 0xF800, &opcode_iox_exchange_with_io_system); /* IOX */

    instruction_add_mask(0170000, 0xFF00, &opcode_sab_set_argument_to_b); /* SAB */
    instruction_add_mask(0170400, 0xFF00, &opcode_saa_set_argument_to_a); /* SAA */
    instruction_add_mask(0171000, 0xFF00, &opcode_sat_set_argument_to_t); /* SAT */
    instruction_add_mask(0171400, 0xFF00, &opcode_sax_set_argument_to_x); /* SAX */
    instruction_add_mask(0172000, 0xFF00, &opcode_aab_add_argument_to_b); /* AAB */
    instruction_add_mask(0172400, 0xFF00, &opcode_aaa_add_argument_to_a); /* AAA */
    instruction_add_mask(0173000, 0xFF00, &opcode_aat_add_argument_to_t); /* AAT */
    instruction_add_mask(0173400, 0xFF00, &opcode_aax_add_argument_to_x); /* AAX */

    instruction_add_range(0174000, 0177777, &do_bops); /* Bit Operation Instructions */
    /* Bit operations, 16 of them, 4 BSET,4 BSKP and 8 others */
}
