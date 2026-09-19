/*
 * nd100x - ND100 Virtual Machine
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


#include <string.h>

#include "cpu_types.h"
#include "cpu_protos.h"

#define BUFSTRSIZE       24
#define BUFSTRSIZE_SMALL 16


static const char *regn[] = {"S", "D", "P", "B", "L", "A", "T", "X", "U0", "U1"};
static const char *regn_w[] = {"DS", "DD", "DP", "DB", "DL", "DA", "DT", "DX"};

static const char *intregn_r[] = {"PANS", "STS",  "OPR", "PGS", "PVL", "IIC", "PID", "PIE",
                                  "CSR",  "ACTL", "ALD", "PES", "PGC", "PEA", "16",  "17"};
static const char *intregn_w[] = {"PANC", "STS",  "LMP",   "PCR", "4",  "IIE", "PID", "PIE",
                                  "CCL",  "LCIL", "UCILR", "13",  "14", "15",  "16",  "17"};

static const char *relmode_str[] = {"", ",B ", "I ", "I ,B ", ",X ", ",X ,B ", "I ,X ", "I ,B ,X "};
static const char *shtype_str[] = {"", "ROT ", "ZIN ", "LIN "};

static const char *skiptype_str[] = {"EQL", "GEQ", "GRE", "MGRE", "UEQ", "LSS", "LST", "MLST"};
static const char *skipregn_dst[] = {"0", "DD", "DP", "DB", "DL", "DA", "DT", "DX"};
static const char *skipregn_src[] = {"0", "SD", "SP", "SB", "SL", "SA", "ST", "SX"};

static const char *bopstsbit_str[] = {"SSPTM", "SSTG", "SSK", "SSZ", "SSQ", "SSO", "SSC", "SSM",
                                      "",      "",     "",    "",    "",    "",    "",    ""};

static const char *bop_str[] = {
    "BSET ZRO", "BSET ONE", "BSET BCM", "BSET BAC", "BSKP ZRO", "BSKP ONE", "BSKP BCM", "BSKP BAC",
    "BSTC",     "BSTA",     "BLDC",     "BLDA",     "BANC",     "BAND",     "BORC",     "BORA"};

static const char *tx_str[] = {"LDATX", "LDXTX", "LDDTX", "LDBTX", "STATX", "STZTX", "STDTX"};

/* OpToStr
 * IN: pointer to string ,raw operand
 * OUT: Sets the string with the dissassembled operand and values
 */
/* size exemption: opcode table (house rule 7.1) - one case per instruction group */
void OpToStr(char *return_string, uint16_t max_len,
             uint16_t operand) // NOLINT(readability-function-size)
{
    uint16_t instr;
    char numstr[BUFSTRSIZE_SMALL];
    char deltastr[BUFSTRSIZE_SMALL];
    unsigned char nibble;
    char offset;
    char delta;
    unsigned char relmode;
    bool isneg;
    char opstr[BUFSTRSIZE];

    offset = operand & 0x00ff;
    nibble = operand & 0x000f;
    relmode = (operand & 0x0700) >> 8;
    delta = (operand & 070);

    /* put offset into a string variable in octal with +/- sign for easy reading */
    ((int)offset < 0) ? (void)snprintf(numstr, sizeof(numstr), "-%o", -(int)offset)
                      : (void)snprintf(numstr, sizeof(numstr), "%o", offset);

    /* ND110 delta offset for some instructions */
    (void)snprintf(deltastr, sizeof(deltastr), "%o", delta);

    instr = extract_opcode(operand);
    switch (instr)
    {
    case 0000000: /* STZ */
        //(void)snprintf(opstr, BUFSTRSIZE, "STZ %s%s", relmode_str[relmode], numstr);
        (void)snprintf(opstr, BUFSTRSIZE, "STZ %s%s", relmode_str[relmode], numstr);
        break;
    case 0004000: /* STA */
        (void)snprintf(opstr, BUFSTRSIZE, "STA %s%s", relmode_str[relmode], numstr);
        break;
    case 0010000: /* STT */
        (void)snprintf(opstr, BUFSTRSIZE, "STT %s%s", relmode_str[relmode], numstr);
        break;
    case 0014000: /* STX */
        (void)snprintf(opstr, BUFSTRSIZE, "STX %s%s", relmode_str[relmode], numstr);
        break;
    case 0020000: /* STD */
        (void)snprintf(opstr, BUFSTRSIZE, "STD %s%s", relmode_str[relmode], numstr);
        break;
    case 0024000: /* LDD */
        (void)snprintf(opstr, BUFSTRSIZE, "LDD %s%s", relmode_str[relmode], numstr);
        break;
    case 0030000: /* STF */
        (void)snprintf(opstr, BUFSTRSIZE, "STF %s%s", relmode_str[relmode], numstr);
        break;
    case 0034000: /* LDF */
        (void)snprintf(opstr, BUFSTRSIZE, "LDF %s%s", relmode_str[relmode], numstr);
        break;
    case 0040000: /* MIN */
        (void)snprintf(opstr, BUFSTRSIZE, "MIN %s%s", relmode_str[relmode], numstr);
        break;
    case 0044000: /* LDA */
        (void)snprintf(opstr, BUFSTRSIZE, "LDA %s%s", relmode_str[relmode], numstr);
        break;
    case 0050000: /* LDT */
        (void)snprintf(opstr, BUFSTRSIZE, "LDT %s%s", relmode_str[relmode], numstr);
        break;
    case 0054000: /* LDX */
        (void)snprintf(opstr, BUFSTRSIZE, "LDX %s%s", relmode_str[relmode], numstr);
        break;
    case 0060000: /* ADD */
        (void)snprintf(opstr, BUFSTRSIZE, "ADD %s%s", relmode_str[relmode], numstr);
        break;
    case 0064000: /* SUB */
        (void)snprintf(opstr, BUFSTRSIZE, "SUB %s%s", relmode_str[relmode], numstr);
        break;
    case 0070000: /* AND */
        (void)snprintf(opstr, BUFSTRSIZE, "AND %s%s", relmode_str[relmode], numstr);
        break;
    case 0074000: /* ORA */
        (void)snprintf(opstr, BUFSTRSIZE, "ORA %s%s", relmode_str[relmode], numstr);
        break;
    case 0100000: /* FAD */
        (void)snprintf(opstr, BUFSTRSIZE, "FAD %s%s", relmode_str[relmode], numstr);
        break;
    case 0104000: /* FSB */
        (void)snprintf(opstr, BUFSTRSIZE, "FSB %s%s", relmode_str[relmode], numstr);
        break;
    case 0110000: /* FMU */
        (void)snprintf(opstr, BUFSTRSIZE, "FMU %s%s", relmode_str[relmode], numstr);
        break;
    case 0114000: /* FDV */
        (void)snprintf(opstr, BUFSTRSIZE, "FDV %s%s", relmode_str[relmode], numstr);
        break;
    case 0120000: /* MPY */
        (void)snprintf(opstr, BUFSTRSIZE, "MPY %s%s", relmode_str[relmode], numstr);
        break;
    case 0124000: /* JMP */
        (void)snprintf(opstr, BUFSTRSIZE, "JMP %s%s", relmode_str[relmode], numstr);
        break;
    case 0130000: /* JAP */
        (void)snprintf(opstr, BUFSTRSIZE, "JAP %s", numstr);
        break;
    case 0130400: /* JAN */
        (void)snprintf(opstr, BUFSTRSIZE, "JAN %s", numstr);
        break;
    case 0131000: /* JAZ */
        (void)snprintf(opstr, BUFSTRSIZE, "JAZ %s", numstr);
        break;
    case 0131400: /* JAF */
        (void)snprintf(opstr, BUFSTRSIZE, "JAF %s", numstr);
        break;
    case 0132000: /* JPC */
        (void)snprintf(opstr, BUFSTRSIZE, "JPC %s", numstr);
        break;
    case 0132400: /* JNC */
        (void)snprintf(opstr, BUFSTRSIZE, "JNC %s", numstr);
        break;
    case 0133000: /* JXZ */
        (void)snprintf(opstr, BUFSTRSIZE, "JXZ %s", numstr);
        break;
    case 0133400: /* JXN */
        (void)snprintf(opstr, BUFSTRSIZE, "JXN %s", numstr);
        break;
    case 0134000: /* JPL */
        (void)snprintf(opstr, BUFSTRSIZE, "JPL %s%s", relmode_str[relmode], numstr);
        break;
    case 0140000: /* SKP */
        (void)snprintf(opstr, BUFSTRSIZE, "SKP IF %s %s %s", skipregn_dst[(operand & 0x0007)],
                       skiptype_str[((operand & 0x0700) >> 8)],
                       skipregn_src[((operand & 0x0038) >> 3)]);
        break;
    case 0140120: /* ADDD */
        (void)snprintf(opstr, BUFSTRSIZE, "ADDD");
        break;
    case 0140121: /* SUBD */
        (void)snprintf(opstr, BUFSTRSIZE, "SUBD");
        break;
    case 0140122: /* COMD */
        (void)snprintf(opstr, BUFSTRSIZE, "COMD");
        break;
    case 0140123: /* TSET */
        (void)snprintf(opstr, BUFSTRSIZE, "TSET");
        break;
    case 0140124: /* PACK */
        (void)snprintf(opstr, BUFSTRSIZE, "PACK");
        break;
    case 0140125: /* UPACK */
        (void)snprintf(opstr, BUFSTRSIZE, "UPACK");
        break;
    case 0140126: /* SHDE */
        (void)snprintf(opstr, BUFSTRSIZE, "SHDE");
        break;
    case 0140127: /* RDUS */
        (void)snprintf(opstr, BUFSTRSIZE, "RDUS");
        break;
    case 0140130: /* BFILL */
        (void)snprintf(opstr, BUFSTRSIZE, "BFILL");
        break;
    case 0140131: /* MOVB */
        (void)snprintf(opstr, BUFSTRSIZE, "MOVB");
        break;
    case 0140132: /* MOVBF */
        (void)snprintf(opstr, BUFSTRSIZE, "MOVBF");
        break;
    case 0140133: /* VERSN - ND110 specific */
        if ((g_current_cpu_type == ND100) || (g_current_cpu_type == ND100CE) ||
            (g_current_cpu_type == ND100CX)) /* We are ND100 */
        {
            break;
        }
        else /* We are a ND110, print instruction */
        {
            (void)snprintf(opstr, BUFSTRSIZE, "VERSN");
        }
        break;    /* WAS MISSING: fell through and overwrote "VERSN" with "INIT" */
    case 0140134: /* INIT */
        (void)snprintf(opstr, BUFSTRSIZE, "INIT");
        break;
    case 0140135: /* ENTR */
        (void)snprintf(opstr, BUFSTRSIZE, "ENTR");
        break;
    case 0140136: /* LEAVE */
        (void)snprintf(opstr, BUFSTRSIZE, "LEAVE");
        break;
    case 0140137: /* ELEAV */
        (void)snprintf(opstr, BUFSTRSIZE, "ELEAV");
        break;
    case 0140300: /* SETPT */
        (void)snprintf(opstr, BUFSTRSIZE, "SETPT");
        break;
    case 0140301: /* CLEPT */
        (void)snprintf(opstr, BUFSTRSIZE, "CLEPT");
        break;
    case 0140302: /* CLNREENT */
        (void)snprintf(opstr, BUFSTRSIZE, "CLNREENT");
        break;
    case 0140303: /* CHREENT-PAGES */
        (void)snprintf(opstr, BUFSTRSIZE, "CHREENT-PAGES");
        break;
    case 0140304: /* CLEPU */
        (void)snprintf(opstr, BUFSTRSIZE, "CLEPU");
        break;
    case 0140200: /* USER0 */
        (void)snprintf(opstr, BUFSTRSIZE, "USER0");
        break;
    case 0140500: /* USER1 or ND110 instruction WGLOB */
        if ((g_current_cpu_type == ND100) || (g_current_cpu_type == ND100CE) ||
            (g_current_cpu_type == ND100CX)) /* We are ND100 */
        {
            (void)snprintf(opstr, BUFSTRSIZE, "USER1");
        }
        else
        {
            (void)snprintf(opstr, BUFSTRSIZE, "WGLOB"); /* We are ND110 */
        }
        break;
    case 0140501: /* RGLOB - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "RGLOB");
        break;
    case 0140502: /* INSPL - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "INSPL");
        break;
    case 0140503: /* REMPL - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "REMPL");
        break;
    case 0140504: /* CNREK - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "CNREK");
        break;
    case 0140505: /* CLPT  - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "CLPT");
        break;
    case 0140506: /* ENPT  - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "ENPT");
        break;
    case 0140507: /* REPT  - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "REPT");
        break;
    case 0140510: /* LBIT  - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "LBIT");
        break;
    case 0140513: /* SBITP - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "SBITP");
        break;
    case 0140514: /* LBYTP - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "LBYTP");
        break;
    case 0140515: /* SBYTP - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "SBYTP");
        break;
    case 0140516: /* TSETP - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "TSETP");
        break;
    case 0140517: /* RDUSP - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "RDUSP");
        break;
    case 0140600: /* EXR */
        (void)snprintf(opstr, BUFSTRSIZE, "EXR %s", skipregn_src[((operand & 0x0038) >> 3)]);
        break;
    case 0140700: /* USER2 */
        if ((g_current_cpu_type == ND100) || (g_current_cpu_type == ND100CE) ||
            (g_current_cpu_type == ND100CX)) /* We are ND100 */
        {
            (void)snprintf(opstr, BUFSTRSIZE, "USER2");
        }
        else
        {
            (void)snprintf(opstr, BUFSTRSIZE, "LASB %s", deltastr); /* We are ND110 */
        }
        break;
    case 0140701: /* SASB - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "SASB %s", deltastr);
        break;
    case 0140702: /* LACB - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "LACB %s", deltastr);
        break;
    case 0140703: /* SASB - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "SASB %s", deltastr);
        break;
    case 0140704: /* LXSB - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "LXSB %s", deltastr);
        break;
    case 0140705: /* LXCB - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "LXCB %s", deltastr);
        break;
    case 0140706: /* SZSB - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "SZSB %s", deltastr);
        break;
    case 0140707: /* SZCB - ND110 Specific */
        (void)snprintf(opstr, BUFSTRSIZE, "SZCB %s", deltastr);
        break;
    case 0141100: /* USER3 */
        (void)snprintf(opstr, BUFSTRSIZE, "USER3");
        break;
    case 0141200: /* RMPY */
        (void)snprintf(opstr, BUFSTRSIZE, "RMPY %s %s", skipregn_src[((operand & 0x0038) >> 3)],
                       skipregn_dst[(operand & 0x0007)]);
        break;
    case 0141300: /* USER4 */
        (void)snprintf(opstr, BUFSTRSIZE, "USER4");
        break;
    case 0141500: /* USER5 */
        (void)snprintf(opstr, BUFSTRSIZE, "USER5");
        break;
    case 0141600: /* RDIV */
        (void)snprintf(opstr, BUFSTRSIZE, "RDIV %s", skipregn_src[((operand & 0x0038) >> 3)]);
        break;
    case 0141700: /* USER6 */
        (void)snprintf(opstr, BUFSTRSIZE, "USER6");
        break;
    case 0142100: /* USER7 */
        (void)snprintf(opstr, BUFSTRSIZE, "USER7");
        break;
    case 0142200: /* LBYT */
        /* NOTE : moved from old parsing, SKP part, might have introduced P++ probs here */
        (void)snprintf(opstr, BUFSTRSIZE, "LBYT");
        break;
    case 0142300: /* USER8 */
        (void)snprintf(opstr, BUFSTRSIZE, "USER8");
        break;
    case 0142500: /* USER9 */
        (void)snprintf(opstr, BUFSTRSIZE, "USER9");
        break;
    case 0142600: /* SBYT */
        /* NOTE : moved from old parsing, SKP part, might have introduced P++ probs here */
        (void)snprintf(opstr, BUFSTRSIZE, "SBYT");
        break;
    case 0142700: /* GECO - Undocumented instruction */
        (void)snprintf(opstr, BUFSTRSIZE, "GECO");
        break;
    case 0143100: /* MOVEW */
        (void)snprintf(opstr, BUFSTRSIZE, "MOVEW");
        break;
    case 0143200: /* MIX3 */
        (void)snprintf(opstr, BUFSTRSIZE, "MIX3");
        break;
    case 0143300: /* LDATX */
    case 0143301: /* LDXTX */
    case 0143302: /* LDDTX */
    case 0143303: /* LDBTX */
    case 0143304: /* STATX */
    case 0143305: /* STZTX */
    case 0143306: /* STDTX */
        /* bits 3-5 are a displacement added to X: EL = (T & 0xFF)<<16 | (X + disp) */
        if ((operand >> 3) & 07)
        {
            (void)snprintf(opstr, BUFSTRSIZE, "%s %o", tx_str[instr & 07], (operand >> 3) & 07);
        }
        else
        {
            (void)snprintf(opstr, BUFSTRSIZE, "%s", tx_str[instr & 07]);
        }
        break;
    case 0143500: /* LWCS */
        (void)snprintf(opstr, BUFSTRSIZE, "LWCS");
        break;
    case 0143604: /* IDENT PL10 */
        (void)snprintf(opstr, BUFSTRSIZE, "IDENT PL10");
        break;
    case 0143611: /* IDENT PL11 */
        (void)snprintf(opstr, BUFSTRSIZE, "IDENT PL11");
        break;
    case 0143622: /* IDENT PL12 */
        (void)snprintf(opstr, BUFSTRSIZE, "IDENT PL12");
        break;
    case 0143643: /* IDENT PL13 */
        (void)snprintf(opstr, BUFSTRSIZE, "IDENT PL13");
        break;
    case 0144000: /* SWAP */
        (void)snprintf(opstr, BUFSTRSIZE, "SWAP %s %s", skipregn_src[((operand & 0x0038) >> 3)],
                       skipregn_dst[(operand & 0x0007)]);
        break;
    case 0144100: /* SWAP CLD */
        (void)snprintf(opstr, BUFSTRSIZE, "SWAP CLD %s %s", skipregn_src[((operand & 0x0038) >> 3)],
                       skipregn_dst[(operand & 0x0007)]);
        break;
    case 0144200: /* SWAP CM1 */
        (void)snprintf(opstr, BUFSTRSIZE, "SWAP CM1 %s %s", skipregn_src[((operand & 0x0038) >> 3)],
                       skipregn_dst[(operand & 0x0007)]);
        break;
    case 0144300: /* SWAP CM1 CLD */
        (void)snprintf(opstr, BUFSTRSIZE, "SWAP CM1 CLD %s %s",
                       skipregn_src[((operand & 0x0038) >> 3)], skipregn_dst[(operand & 0x0007)]);
        break;
    case 0144400: /* RAND */
        (void)snprintf(opstr, BUFSTRSIZE, "RAND %s %s", skipregn_src[((operand & 0x0038) >> 3)],
                       skipregn_dst[(operand & 0x0007)]);
        break;
    case 0144500: /* RAND CLD */
        (void)snprintf(opstr, BUFSTRSIZE, "RAND CLD %s %s", skipregn_src[((operand & 0x0038) >> 3)],
                       skipregn_dst[(operand & 0x0007)]);
        break;
    case 0144600: /* RAND CM1 */
        (void)snprintf(opstr, BUFSTRSIZE, "RAND CM1 %s %s", skipregn_src[((operand & 0x0038) >> 3)],
                       skipregn_dst[(operand & 0x0007)]);
        break;
    case 0144700: /* RAND CM1 CLD */
        (void)snprintf(opstr, BUFSTRSIZE, "RAND CM1 CLD %s %s",
                       skipregn_src[((operand & 0x0038) >> 3)], skipregn_dst[(operand & 0x0007)]);
        break;
    case 0145000: /* REXO */
        (void)snprintf(opstr, BUFSTRSIZE, "REXO %s %s", skipregn_src[((operand & 0x0038) >> 3)],
                       skipregn_dst[(operand & 0x0007)]);
        break;
    case 0145100: /* REXO CLD */
        (void)snprintf(opstr, BUFSTRSIZE, "REXO CLD %s %s", skipregn_src[((operand & 0x0038) >> 3)],
                       skipregn_dst[(operand & 0x0007)]);
        break;
    case 0145200: /* REXO CM1 */
        (void)snprintf(opstr, BUFSTRSIZE, "REXO CM1 %s %s", skipregn_src[((operand & 0x0038) >> 3)],
                       skipregn_dst[(operand & 0x0007)]);
        break;
    case 0145300: /* REXO CM1 CLD */
        (void)snprintf(opstr, BUFSTRSIZE, "REXO CM1 CLD %s %s",
                       skipregn_src[((operand & 0x0038) >> 3)], skipregn_dst[(operand & 0x0007)]);
        break;
    case 0145400: /* RORA */
        (void)snprintf(opstr, BUFSTRSIZE, "RORA %s %s", skipregn_src[((operand & 0x0038) >> 3)],
                       skipregn_dst[(operand & 0x0007)]);
        break;
    case 0145500: /* RORA CLD */
        (void)snprintf(opstr, BUFSTRSIZE, "RORA CLD %s %s", skipregn_src[((operand & 0x0038) >> 3)],
                       skipregn_dst[(operand & 0x0007)]);
        break;
    case 0145600: /* RORA CM1 */
        (void)snprintf(opstr, BUFSTRSIZE, "RORA CM1 %s %s", skipregn_src[((operand & 0x0038) >> 3)],
                       skipregn_dst[(operand & 0x0007)]);
        break;
    case 0145700: /* RORA CM1 CLD */
        (void)snprintf(opstr, BUFSTRSIZE, "RORA CM1 CLD %s %s",
                       skipregn_src[((operand & 0x0038) >> 3)], skipregn_dst[(operand & 0x0007)]);
        break;
    case 0146000: /* RADD */
        (void)snprintf(opstr, BUFSTRSIZE, "RADD %s %s", skipregn_src[((operand & 0x0038) >> 3)],
                       skipregn_dst[(operand & 0x0007)]);
        break;
    case 0146100: /* RADD CLD */
        if (operand == 0146142)
        {
            snprintf(opstr, BUFSTRSIZE, "EXIT");
        }
        else
        {
            (void)snprintf(opstr, BUFSTRSIZE, "RADD CLD %s %s",
                           skipregn_src[((operand & 0x0038) >> 3)],
                           skipregn_dst[(operand & 0x0007)]);
        }
        break;
    case 0146200: /* RADD CM1 */
        (void)snprintf(opstr, BUFSTRSIZE, "RADD CM1 %s %s", skipregn_src[((operand & 0x0038) >> 3)],
                       skipregn_dst[(operand & 0x0007)]);
        break;
    case 0146300: /* RADD CM1 CLD */
        (void)snprintf(opstr, BUFSTRSIZE, "RADD CM1 CLD %s %s",
                       skipregn_src[((operand & 0x0038) >> 3)], skipregn_dst[(operand & 0x0007)]);
        break;
    case 0146400: /* RADD AD1 */
        (void)snprintf(opstr, BUFSTRSIZE, "RADD AD1 %s %s", skipregn_src[((operand & 0x0038) >> 3)],
                       skipregn_dst[(operand & 0x0007)]);
        break;
    case 0146500: /* RADD AD1 CLD */
        (void)snprintf(opstr, BUFSTRSIZE, "RADD AD1 CLD %s %s",
                       skipregn_src[((operand & 0x0038) >> 3)], skipregn_dst[(operand & 0x0007)]);
        break;
    case 0146600: /* RADD AD1 CM1  <== IS REALLY RSBUB*/
        //(void)snprintf(opstr, BUFSTRSIZE, "RADD AD1 CM1 %s %s", skipregn_src[((operand & 0x0038) >> 3)], skipregn_dst[(operand & 0x0007)]);
        (void)snprintf(opstr, BUFSTRSIZE, "RSUB %s %s", skipregn_src[((operand & 0x0038) >> 3)],
                       skipregn_dst[(operand & 0x0007)]);
        break;
    case 0146700: /* RADD AD1 CM1 CLD */
        (void)snprintf(opstr, BUFSTRSIZE, "RADD AD1 CM1 CLD %s %s",
                       skipregn_src[((operand & 0x0038) >> 3)], skipregn_dst[(operand & 0x0007)]);
        break;
    case 0147000: /* RADD ADC */
        (void)snprintf(opstr, BUFSTRSIZE, "RADD ADC %s %s", skipregn_src[((operand & 0x0038) >> 3)],
                       skipregn_dst[(operand & 0x0007)]);
        break;
    case 0147100: /* RADD ADC CLD */
        (void)snprintf(opstr, BUFSTRSIZE, "RADD ADC CLD %s %s",
                       skipregn_src[((operand & 0x0038) >> 3)], skipregn_dst[(operand & 0x0007)]);
        break;
    case 0147200: /* RADD ADC CM1 */
        (void)snprintf(opstr, BUFSTRSIZE, "RADD ADC CM1 %s %s",
                       skipregn_src[((operand & 0x0038) >> 3)], skipregn_dst[(operand & 0x0007)]);
        break;
    case 0147300: /* RADD ADC CM1 CLD */
        (void)snprintf(opstr, BUFSTRSIZE, "RADD ADC CM1 CLD %s %s",
                       skipregn_src[((operand & 0x0038) >> 3)], skipregn_dst[(operand & 0x0007)]);
        break;
    case 0147400: /* NOOP */
    case 0147500: /* NOOP */
    case 0147600: /* NOOP */
    case 0147700: /* NOOP */
        (void)snprintf(opstr, BUFSTRSIZE, "ROP NOOP");
        break;
    case 0150000: /* TRA */
        (void)snprintf(opstr, BUFSTRSIZE, "TRA %s", intregn_r[nibble]);
        break;
    case 0150100: /* TRR */
        (void)snprintf(opstr, BUFSTRSIZE, "TRR %s", intregn_w[nibble]);
        break;
    case 0150200: /* MCL */
        (void)snprintf(opstr, BUFSTRSIZE, "MCL %s", intregn_w[nibble]);
        break;
    case 0150300: /* MST */
        (void)snprintf(opstr, BUFSTRSIZE, "MST %s", intregn_w[nibble]);
        break;
    case 0150400: /* OPCOM */
        (void)snprintf(opstr, BUFSTRSIZE, "OPCOM");
        break;
    case 0150401: /* IOF */
        (void)snprintf(opstr, BUFSTRSIZE, "IOF");
        break;
    case 0150402: /* ION */
        (void)snprintf(opstr, BUFSTRSIZE, "ION");
        break;
    case 0150404: /* POF */
        (void)snprintf(opstr, BUFSTRSIZE, "POF");
        break;
    case 0150405: /* PIOF */
        (void)snprintf(opstr, BUFSTRSIZE, "PIOF");
        break;
    case 0150406: /* SEX */
        (void)snprintf(opstr, BUFSTRSIZE, "SEX");
        break;
    case 0150407: /* REX */
        (void)snprintf(opstr, BUFSTRSIZE, "REX");
        break;
    case 0150410: /* PON */
        (void)snprintf(opstr, BUFSTRSIZE, "PON");
        break;
    case 0150412: /* PION */
        (void)snprintf(opstr, BUFSTRSIZE, "PION");
        break;
    case 0150415: /* IOXT */
        (void)snprintf(opstr, BUFSTRSIZE, "IOXT");
        break;
    case 0150416: /* EXAM */
        (void)snprintf(opstr, BUFSTRSIZE, "EXAM");
        break;
    case 0150417: /* DEPO */
        (void)snprintf(opstr, BUFSTRSIZE, "DEPO");
        break;
    case 0151000:                                  /* WAIT */
        (void)snprintf(opstr, BUFSTRSIZE, "WAIT"); /* TODO:: number??*/
        break;
    case 0151400: /* NLZ*/
        (void)snprintf(opstr, BUFSTRSIZE, "NLZ %s", numstr);
        break;
    case 0152000: /* DNZ*/
        (void)snprintf(opstr, BUFSTRSIZE, "DNZ %s", numstr);
        break;
    case 0152400: /* SRB */ /* NOTE: These two seems to have bit req on 0-2 as well */
        (void)snprintf(opstr, BUFSTRSIZE, "SRB %o", (operand & 0x0078));
        break;
    case 0152600: /* LRB */ /* NOTE: These two seems to have bit req on 0-2 as well */
        (void)snprintf(opstr, BUFSTRSIZE, "LRB %o", (operand & 0x0078) >> 3);
        break;
    case 0153000: /* MON */
        (void)snprintf(opstr, BUFSTRSIZE, "MON %o", (operand & 0x00ff));
        break;
    case 0153400: /* IRW */
        (void)snprintf(opstr, BUFSTRSIZE, "IRW %o %s", (operand & 0x0078),
                       regn_w[(operand & 0x0007)]);
        break;
    case 0153600: /* IRR */
        (void)snprintf(opstr, BUFSTRSIZE, "IRR %o %s", (operand & 0x0078),
                       regn_w[(operand & 0x0007)]);
        break;
    case 0154000: /* SHT */
        /* negative value -> shift right  else shift left*/
        isneg = ((operand & 0x0020) >> 5) ? 1 : 0;
        offset = (isneg) ? (~((operand & 0x003F) | 0xFFC0) + 1) : (operand & 0x003F);
        (isneg) ? (void)snprintf(numstr, sizeof(numstr), "SHR %o", offset)
                : (void)snprintf(numstr, sizeof(numstr), "%o", offset);
        (void)snprintf(opstr, BUFSTRSIZE, "SHT %s%s", shtype_str[((operand & 0x0600) >> 9)],
                       numstr);
        break;
    case 0154200: /* SHD */
        /* negative value -> shift right  else shift left*/
        isneg = ((operand & 0x0020) >> 5) ? 1 : 0;
        offset = (isneg) ? (~((operand & 0x003F) | 0xFFC0) + 1) : (operand & 0x003F);
        (isneg) ? (void)snprintf(numstr, sizeof(numstr), "SHR %o", offset)
                : (void)snprintf(numstr, sizeof(numstr), "%o", offset);
        //      ((int)offset <0) ? (void)snprintf(numstr,BUFSTRSIZE,"SHR %o",-(int)offset) : (void)snprintf(numstr,BUFSTRSIZE,"%o",offset);
        (void)snprintf(opstr, BUFSTRSIZE, "SHD %s%s", shtype_str[((operand & 0x0600) >> 9)],
                       numstr);
        break;
    case 0154400: /* SHA */
        /* negative value -> shift right  else shift left*/
        isneg = ((operand & 0x0020) >> 5) ? 1 : 0;
        offset = (isneg) ? (~((operand & 0x003F) | 0xFFC0) + 1) : (operand & 0x003F);
        (isneg) ? (void)snprintf(numstr, sizeof(numstr), "SHR %o", offset)
                : (void)snprintf(numstr, sizeof(numstr), "%o", offset);
        //      ((int)offset <0) ? (void)snprintf(numstr,BUFSTRSIZE,"SHR %o",-(int)offset) : (void)snprintf(numstr,BUFSTRSIZE,"%o",offset);
        (void)snprintf(opstr, BUFSTRSIZE, "SHA %s%s", shtype_str[((operand & 0x0600) >> 9)],
                       numstr);
        break;
    case 0154600: /* SAD */
        /* negative value -> shift right  else shift left*/
        isneg = ((operand & 0x0020) >> 5) ? 1 : 0;
        offset = (isneg) ? (~((operand & 0x003F) | 0xFFC0) + 1) : (operand & 0x003F);
        (isneg) ? (void)snprintf(numstr, sizeof(numstr), "SHR %o", offset)
                : (void)snprintf(numstr, sizeof(numstr), "%o", offset);
        //      ((int)offset <0) ? (void)snprintf(numstr,BUFSTRSIZE,"SHR %o",-(int)offset) : (void)snprintf(numstr,BUFSTRSIZE,"%o",offset);
        (void)snprintf(opstr, BUFSTRSIZE, "SAD %s%s", shtype_str[((operand & 0x0600) >> 9)],
                       numstr);
        break;
    case 0160000: /* IOT - NORD-1 compatible I/O */
        /* IOT is NOT an alias of IOX. Bits 0-7 are a device number and bits
         * 8-10 are the function ACT / SKA / PIN, all zero meaning SNI
         * (NORD-1 Reference Manual sec 3.7; ND-06.008.01 lists IOT and IOX as
         * separate instructions). Printing the low 11 bits as one address, as
         * this used to, renders "IOT SKA 144" as "IOT 1144" and hides the
         * function entirely. */
        {
            unsigned devno = operand & 0x00ff;
            unsigned func = (operand >> 8) & 0x07;
            if (func == 0)
            {
                /* all three function bits clear is SNI, skip if not interrupt */
                (void)snprintf(opstr, BUFSTRSIZE, "IOT SNI %o", devno);
            }
            else
            {
                (void)snprintf(opstr, BUFSTRSIZE, "IOT %s%s%s%o", (func & 4) ? "PIN " : "",
                               (func & 2) ? "SKA " : "", (func & 1) ? "ACT " : "", devno);
            }
        }
        break;
    case 0164000: /* IOX */
        (void)snprintf(opstr, BUFSTRSIZE, "IOX %o", (operand & 0x07ff));
        break;
    case 0170000: /* SAB */
        (void)snprintf(opstr, BUFSTRSIZE, "SAB %s", numstr);
        break;
    case 0170400: /* SAA */
        (void)snprintf(opstr, BUFSTRSIZE, "SAA %s", numstr);
        break;
    case 0171000: /* SAT */
        (void)snprintf(opstr, BUFSTRSIZE, "SAT %s", numstr);
        break;
    case 0171400: /* SAX */
        (void)snprintf(opstr, BUFSTRSIZE, "SAX %s", numstr);
        break;
    case 0172000: /* AAB */
        (void)snprintf(opstr, BUFSTRSIZE, "AAB %s", numstr);
        break;
    case 0172400: /* AAA */
        (void)snprintf(opstr, BUFSTRSIZE, "AAA %s", numstr);
        break;
    case 0173000: /* AAT */
        (void)snprintf(opstr, BUFSTRSIZE, "AAT %s", numstr);
        break;
    case 0173400: /* AAX */
        (void)snprintf(opstr, BUFSTRSIZE, "AAX %s", numstr);
        break;
    case 0174000:                /* BSET ZRO */
    case 0174200:                /* BSET ONE */
    case 0174400:                /* BSET BCM */
    case 0174600:                /* BSET BAC */
    case 0175000:                /* BSKP ZRO */
    case 0175200:                /* BSKP ONE */
    case 0175400:                /* BSKP BCM */
    case 0175600:                /* BSKP BAC */
    case 0176000:                /* BSTC */
    case 0176200:                /* BSTA */
    case 0176400:                /* BLDC */
    case 0176600:                /* BLDA */
    case 0177000:                /* BANC */
    case 0177200:                /* BAND */
    case 0177400:                /* BORC */
    case 0177600:                /* BORA */
        if (!(operand & 0x0007)) /* STS reg bits handling FIXME:: what if it is bit >7 & STS??*/
        {
            (void)snprintf(opstr, BUFSTRSIZE, "%s %s", bop_str[((operand & 0x0780) >> 7)],
                           bopstsbit_str[((operand & 0x0078) >> 3)]);
        }
        else
        {
            (void)snprintf(opstr, BUFSTRSIZE, "%s %o D%s", bop_str[((operand & 0x0780) >> 7)],
                           (int)(operand & 0x0078), regn[(operand & 0x0007)]);
        }
        break;
    default: /* UNDEF */ /* Some ND instruction codes is undefined unfortunately. */
        (void)snprintf(opstr, BUFSTRSIZE, "UNDEF");
        break;
    }

    // Safe string copy with guaranteed null-termination
    snprintf(return_string, max_len, "%s", opstr);
}


// Declare and define the disasm array and pointer to it
DisasmArray g_disasm_arr = {NULL};
DisasmArray *g_dis = &g_disasm_arr;

static int s_disasm_ctr = 0;

void disasm_allocate(uint16_t addr)
{
    if ((*g_dis)[addr])
    {
        return; /* already exists */
    }

    // Allocate memory for the disasm entry at the given address
    (*g_dis)[addr] = calloc(1, sizeof(struct disasm_entry));
}

void disasm_instr(uint16_t addr, uint16_t instr)
{

    // Add the instruction to the disassembly array if it doesn't exist
    if ((*g_dis)[addr] == NULL)
    {
        disasm_addword(addr, instr);
    }

    // If the instruction exists, set it to code and copy the disassembly string
    if ((*g_dis)[addr] != NULL)
    {
        char disasm_str[BUFSTRSIZE];
        OpToStr(disasm_str, BUFSTRSIZE, instr);


        (*g_dis)[addr]->iscode = true;
        snprintf((*g_dis)[addr]->asm_str, 32, "%s", disasm_str);
    }
}

void disasm_exr(uint16_t addr, uint16_t instr)
{
    char disasm_str[BUFSTRSIZE];
    OpToStr(disasm_str, BUFSTRSIZE, instr);

    if ((*g_dis)[addr] != NULL)
    {
        (*g_dis)[addr]->isexr = true;
        snprintf((*g_dis)[addr]->exr, 32, "%s", disasm_str);
    }
}

void disasm_addword(uint16_t addr, uint16_t myword)
{
    if ((*g_dis)[addr])
    {
        return; /* already exists */
    }

    (*g_dis)[addr] = calloc(1, sizeof(struct disasm_entry));
    if ((*g_dis)[addr] != NULL)
    {
        (*g_dis)[addr]->theword = myword;
    }
}

void disasm_init(void)
{
    int i;
    for (i = 0; i < 65536; i++)
    {
        (*g_dis)[i] = NULL;
    }
    s_disasm_ctr = 0;
}

void disasm_setlbl(uint16_t addr)
{
    s_disasm_ctr++;
    if ((*g_dis)[addr] != NULL)
    {
        (*g_dis)[addr]->labelno = s_disasm_ctr;
    }
}

void disasm_set_isdata(uint16_t addr)
{
    if ((*g_dis)[addr] != NULL)
    {
        (*g_dis)[addr]->isdata = true;
    }
}

void disasm_userel(uint16_t addr, uint16_t where)
{
    if ((*g_dis)[addr] != NULL)
    {
        if ((*g_dis)[addr]->use_rel)
        { /* we have already used relative from here */
        }
        else
        {
            (*g_dis)[addr]->use_rel = true;
            disasm_allocate(where); // make sure we dont have a null ptr.
            if ((*g_dis)[where]->labelno)
            { /* Where already has a label  */
                (*g_dis)[addr]->rel_acc_lbl = (*g_dis)[where]->labelno;
            }
            else
            {
                disasm_setlbl(where);
                (*g_dis)[addr]->rel_acc_lbl = (*g_dis)[where]->labelno;
            }
        }
    }
}


void disasm_dump(void)
{
    int i;
    int tmp;
    uint8_t u;
    uint8_t l;
    uint16_t w;
    char disasm_str[BUFSTRSIZE];

    //char* disasm_fname = "disasm.txt";
    const char *disasm_fname = "/dev/stdout";
    const char *disasm_ftype = "w";

    FILE *disasm_file = fopen(disasm_fname, disasm_ftype);

    for (i = 0; i < 65536; i++)
    {
        if ((*g_dis)[i] != NULL)
        {
            w = (*g_dis)[i]->theword;
            u = (w >> 8) & 0xff;
            l = w & 0xff;

            fprintf(disasm_file, "%06o    %06o   ", i, (*g_dis)[i]->theword);
            if ((*g_dis)[i]->labelno)
            {
                fprintf(disasm_file, " L%05d ", (*g_dis)[i]->labelno);
            }
            else
            {
                fprintf(disasm_file, "       ");
            }
            if ((*g_dis)[i]->iscode)
            {
                fprintf(disasm_file, "%s", (*g_dis)[i]->asm_str);
                tmp = strlen((const char *)(*g_dis)[i]->asm_str);
                fprintf(disasm_file, "%.*s", (32 - tmp),
                        "                                 "); /* align */
                if ((*g_dis)[i]->use_rel)
                {
                    fprintf(disasm_file, "%% L%05d ", (*g_dis)[i]->rel_acc_lbl);
                }
                if ((*g_dis)[i]->isexr)
                {
                    fprintf(disasm_file, "%% %s", (*g_dis)[i]->exr);
                }
            }
            else if ((*g_dis)[i]->isdata)
            {
                fprintf(disasm_file, "DATA: ");
                if (u >= 32 && u <= 127)
                {
                    fprintf(disasm_file, "\'%c\'", u);
                }
                if (l >= 32 && l <= 127)
                {
                    fprintf(disasm_file, "\'%c\'", l);
                }
            }
            else
            {
                fprintf(disasm_file, "UNKN: ");
                if (u >= 32 && u <= 127)
                {
                    fprintf(disasm_file, "\'%c\'", u);
                }
                if (l >= 32 && l <= 127)
                {
                    fprintf(disasm_file, "\'%c\'", l);
                }

                fprintf(disasm_file, "          ");
                OpToStr(disasm_str, BUFSTRSIZE, (*g_dis)[i]->theword);
                fprintf(disasm_file, "%% %s", disasm_str);
            }
            fprintf(disasm_file, "\n");
        }
    }

    fclose(disasm_file);
}


/*
 * Mask out instruction opcode from parameters
 * NOTE: Manual does not specify details about how ND100
 * maps all bits and some opcodes will miss this decode.
 * This means we will not completely emulate ND100 behaviour for
 * illegal opcodes yet.
 */
uint16_t extract_opcode(uint16_t instr)
{
    switch (instr & (0xFFFF << 11))
    {
    case 0130000: /* JAP, JAN, JAZ, JAF, JPC, JNC, JXZ, JXN */
        return (instr & (0xFFFF << 8));
    case 0140000:
        if (0 == (instr & (0x03 << 6))) /* SKIP Instruction */
        {
            return 0140000;
        }
        else /* Decode further */
        {
            return (decode_140k(instr));
        }
    case 0150000: /* Decode further */
        return (decode_150k(instr));
    case 0144000: /* ROP Register operations */
        return instr & (0xFFFF << 6);
    case 0154000: /* Shift Instruction */
        return instr & ((0xFFFF << 11) | (0x03 << 7));
    case 0170000: /* Argument Instructions */
        return instr & (0xFFFF << 8);
    case 0174000:                     /* Bit Operation Instructions */
        return instr & (0xFFFF << 7); /* Bit operations, 16 of them, 4 BSET,4 BSKP and 8 others */
    default:                          /* Memory Reference Instructions & IOX */
        return instr & (0xFFFF << 11);
    }
    return instr; //:NOTE: This should not be reached. Added only to satisfy complaining compiler.
}

uint16_t decode_140k(uint16_t instr)
{
    switch (instr & (0xFFFF))
    {
    case 0140120: /* ADDD */
    case 0140121: /* SUBD */
    case 0140122: /* COMD */
    case 0140123: /* TSET */
    case 0140124: /* PACK */
    case 0140125: /* UPACK */
    case 0140126: /* SHDE */
    case 0140127: /* RDUS */
    case 0140130: /* BFILL */
    case 0140131: /* MOVB */
    case 0140132: /* MOVBF */
        return (instr);
    case 0140133: /* VERSN - ND110 specific */
        if ((g_current_cpu_type == ND110) || (g_current_cpu_type == ND110CE) ||
            (g_current_cpu_type == ND110CX))
        {
            return (instr);
        }
        else
        {
            break;
        }
    case 0140134: /* INIT */
    case 0140135: /* ENTR */
    case 0140136: /* LEAVE */
    case 0140137: /* ELEAV */
    case 0140300: /* SETPT */
    case 0140301: /* CLEPT */
    case 0140302: /* CLNREENT */
    case 0140303: /* CHREENT-PAGES */
    case 0140304: /* CLEPU */
    case 0143500: /* LWCS */
    case 0143604: /* IDENT PL10 */
    case 0143611: /* IDENT PL11 */
    case 0143622: /* IDENT PL12 */
    case 0143643: /* IDENT PL13 */
        return (instr);
    default:
        break;
    }
    switch (instr & (0xFFFF << 6))
    {
    case 0140200: /* USER1 (microcode defined by user or illegal instruction otherwise) */
        return instr & (0xFFFF << 6);
    case 0140500: /* USER2 (microcode defined by user or illegal instruction otherwise) */
        if ((g_current_cpu_type == ND100) || (g_current_cpu_type == ND100CE) ||
            (g_current_cpu_type == ND100CX)) /* We are ND100 */
        {
            return instr & (0xFFFF << 6);
        }
        else
        {
            switch (instr & (0xFFFF))
            {             /* We are not */
            case 0140500: /* WGLOB - ND110 Specific */
            case 0140501: /* RGLOB - ND110 Specific */
            case 0140502: /* INSPL - ND110 Specific */
            case 0140503: /* REMPL - ND110 Specific */
            case 0140504: /* CNREK - ND110 Specific */
            case 0140505: /* CLPT  - ND110 Specific */
            case 0140506: /* ENPT  - ND110 Specific */
            case 0140507: /* REPT  - ND110 Specific */
            case 0140510: /* LBIT  - ND110 Specific */
            case 0140513: /* SBITP - ND110 Specific */
            case 0140514: /* LBYTP - ND110 Specific */
            case 0140515: /* SBYTP - ND110 Specific */
            case 0140516: /* TSETP - ND110 Specific */
            case 0140517: /* RDUSP - ND110 Specific */
                return (instr);
            default:
                break;
            }
        }
        __attribute__((fallthrough));
    case 0140600: /* EXR */
        return instr & (0xFFFF << 6);
    case 0140700: /* USER3 (microcode defined by user or illegal instruction otherwise) */
        if ((g_current_cpu_type == ND100) || (g_current_cpu_type == ND100CE) ||
            (g_current_cpu_type == ND100CX)) /* We are ND100 */
        {
            return instr & (0xFFFF << 6);
        }
        else
        {
            switch (instr & (0xFFC7))
            {             /* We are not */
            case 0140700: /* LASB - ND110 Specific */
            case 0140701: /* SASB - ND110 Specific */
            case 0140702: /* LACB - ND110 Specific */
            case 0140703: /* SASB - ND110 Specific */
            case 0140704: /* LXSB - ND110 Specific */
            case 0140705: /* LXCB - ND110 Specific */
            case 0140706: /* SZSB - ND110 Specific */
            case 0140707: /* SZCB - ND110 Specific */
                return (instr & (0xFFC7));
            default:
                break;
            }
        }
        __attribute__((fallthrough));
    case 0141100: /* USER4 (microcode defined by user or illegal instruction otherwise) */
    case 0141200: /* RMPY */
    case 0141300: /* USER5 (microcode defined by user or illegal instruction otherwise) */
    case 0141500: /* USER6 (microcode defined by user or illegal instruction otherwise) */
    case 0141600: /* RDIV */
    case 0141700: /* USER7 (microcode defined by user or illegal instruction otherwise) */
    case 0142100: /* USER8 (microcode defined by user or illegal instruction otherwise) */
    case 0142200: /* LBYT */
    case 0142300: /* USER9 (microcode defined by user or illegal instruction otherwise) */
    case 0142500: /* USER10 (microcode defined by user or illegal instruction otherwise) */
    case 0142600: /* SBYT */
    case 0142700: /* GECO - Undocumented instruction */
    case 0143100: /* MOVEW */
    case 0143200: /* MIX3 */
        return instr & (0xFFFF << 6);
    default:
        break;
    }
    switch (instr & (0xFFC7))
    {
    case 0143300: /* LDATX */
    case 0143301: /* LDXTX */
    case 0143302: /* LDDTX */
    case 0143303: /* LDBTX */
    case 0143304: /* STATX */
    case 0143305: /* STZTX */
    case 0143306: /* STDTX */
        return (instr & (0xFFC7));
    default:
        break;
    }
    return instr; /* NOTE: This should not be reached, but decoding might not be totally exhaustive.*/
    /* TODO: Check if we should return NOOP, or create our own internal illegal instruction code and trap that later. */
}

uint16_t decode_150k(uint16_t instr)
{
    switch (instr & (0xFFFF))
    {
    case 0150400: /* OPCOM */
    case 0150401: /* IOF */
    case 0150402: /* ION */
    case 0150404: /* POF */
    case 0150405: /* PIOF */
    case 0150406: /* SEX */
    case 0150407: /* REX */
    case 0150410: /* PON */
    case 0150412: /* PION */
    case 0150415: /* IOXT */
    case 0150416: /* EXAM */
    case 0150417: /* DEPO */
        return (instr);
    default:
        break;
    }
    switch (instr & (0xFFFF << 8))
    {
    case 0151000: /* WAIT */
    case 0151400: /* NLZ*/
    case 0152000: /* DNZ*/
    case 0153000: /* MON */
        return (uint16_t)(instr & (0xFFFF << 8));
    default:
        break;
    }
    switch (instr & (0xFFFF << 7))
    {
    case 0152400: /* SRB */ /* NOTE: These two seems to have bit req on 0-2 as well */
    case 0152600:           /* LRB */
    case 0153400:           /* IRW */
    case 0153600:           /* IRR */
        return instr & (0xFFFF << 7);
    default:
        break;
    }

    switch (instr & (0xFFFF << 6))
    {
    case 0150000: /* TRA */
    case 0150100: /* TRR */
    case 0150200: /* MCL */
    case 0150300: /* MST */
        return instr & (0xFFFF << 6);
    default:
        break;
    }
    return instr; /* NOTE: This should not be reached, but decoding might not be totally exhaustive.*/
    /* TODO: Check if we should return NOOP, or create our own internal illegal instruction code and trap that later. */
}
