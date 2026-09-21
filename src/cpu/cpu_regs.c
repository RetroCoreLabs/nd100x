/*
 * cpu_regs.c - CPU register access: PIL, PEA/PES/PGS, STS bits and per-level registers.
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


/// @brief Set the PIL (Program Interrupt Level)
/// @param newLevel The new level to set
/// @return Returns true if the level was set, false otherwise
bool cpu_set_pil(char new_level)
{
    if (new_level >= 16)
    {
        return false;
    }
    if (new_level == gPIL)
    {
        return true; // already set
    }

    gPVL = gPIL; /* Save current runlevel */

    // Update SYSTEM bits - PIL
    g_reg->reg_STS = (g_reg->reg_STS & 0xF000) | ((new_level & 0x0f) << 8);
    return true;
}

// Set and lock PEA
void cpu_set_pea(uint16_t pea)
{
    if (gPEA_Lock)
    {
        return;
    }
    gPEA = pea;
    gPEA_Lock = true;
}

// Set and lock PES
void cpu_set_pes(uint16_t pes)
{
    if (gPES_Lock)
    {
        return;
    }
    gPES = pes;
    gPES_Lock = true;
}

// Set and lock PGS
void cpu_set_pgs(uint16_t pgs)
{
    if (gPGS_Lock)
    {
        return;
    }
    gPGS = pgs;
    if (pgs != 0)
    {
        gPGS_Lock = true;
    }
}


void cpu_setreg(int r, int val)
{
    if (r == _STS)
    {
        g_reg->reg[CURR_LEVEL][r] = (uint16_t)(val & 0x00FF); // Only lower 8 bits
    }
    else
    {
        g_reg->reg[CURR_LEVEL][r] = (uint16_t)(val & 0xFFFF);
    }
}

uint16_t cpu_getbit(uint16_t regnum, uint16_t stsbit)
{
    uint16_t result;
    uint16_t tmp;
    if (regnum == _STS)
    {
        // Undoocumented, but all 16 STS bits are read
        tmp = gSTSr;
    }
    else
    {
        tmp = g_reg->reg[CURR_LEVEL][regnum];
    }
    result = (tmp >> stsbit) & 1;
    return result;
}

void cpu_clrbit(uint16_t regnum, uint16_t stsbit)
{
    uint16_t thebit;
    thebit = (1 << stsbit) ^ 0xFFFF;
    g_reg->reg[CURR_LEVEL][regnum] = (thebit & g_reg->reg[CURR_LEVEL][regnum]);
}

/*
 * setbit_STS_MSB:
 * This function handles all setting of MSB STS bits
 * NOTE:: PIL handling is done by setPIL function!!
 */
void cpu_setbit_sts_msb(uint16_t stsbit, char val)
{
    uint16_t thebit = 0;

    if (val)
    {
        thebit = (1 << stsbit);
        g_reg->reg_STS = g_reg->reg_STS | thebit;
    }
    else
    {
        thebit = (1 << stsbit) ^ 0xFFFF;
        g_reg->reg_STS = g_reg->reg_STS & thebit;
    }
}


void cpu_setbit(uint16_t regnum, uint16_t stsbit, char val)
{

    if ((regnum == _STS) && (stsbit > 7))
    {
        cpu_setbit_sts_msb(stsbit, val);
        return;
    }

    uint16_t thebit = 0;
    if (val)
    {
        thebit = (1 << stsbit);
        g_reg->reg[CURR_LEVEL][regnum] = (thebit | g_reg->reg[CURR_LEVEL][regnum]);

        if (stsbit == STS_ERROR_INDICATOR) // error bit is set
        {
            gCHKIT = true; // we need to check PK after this
        }
    }
    else
    {
        thebit = (1 << stsbit) ^ 0xFFFF;
        g_reg->reg[CURR_LEVEL][regnum] = (thebit & g_reg->reg[CURR_LEVEL][regnum]);
    }
}


void cpu_adjust_sts(uint16_t reg_a, uint16_t operand, int result)
{
    /* C (carry) */
    if (result > 0xFFFF)
    {
        cpu_setbit(_STS, STS_CARRY, 1);
    }
    else
    {
        cpu_setbit(_STS, STS_CARRY, 0);
    }

    /* O(static overflow), Q (dynamic overflow) */
    if (!(((1 << 15) & reg_a) ^ ((1 << 15) & operand)) &&
        (((1 << 15) & reg_a) ^ ((1 << 15) & result)))
    {
        cpu_setbit(_STS, STS_STATIC_OVERFLOW, 1);
        cpu_setbit(_STS, STS_DYNAMIC_OVERFLOW, 1);
    }
    else
    {
        cpu_setbit(_STS, STS_DYNAMIC_OVERFLOW, 0);
    }
}
