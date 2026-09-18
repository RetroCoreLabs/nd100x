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



#include "cpu_types.h"
#include "cpu_protos.h"
#include "../ndlib/log.h"

/* --ring-at-clpt=N: dump the CPU instruction ring at the N'th CLPT (0 = off). */
static long s_ring_at_clpt = 0;

/**
 * @brief Set the CLPT call at which to dump the instruction ring (--ring-at-clpt).
 * @param n CLPT call number counted from 1; 0 turns the dump off.
 */
void cpu_set_ring_at_clpt(long n)
{
	s_ring_at_clpt = n > 0 ? n : 0;
}
#include <stdlib.h>
#include <string.h>	/* strlen()/strcmp() - VERSN identity parsing, see ndfunc_versn() */
#include <stdio.h>


// Initialize the instruction function array
InstrFunc instr_funcs[65536];

/*********** TODO ***********/

extern uint16_t io_op(uint16_t ioadd, uint16_t regA);
extern int IO_Ident(uint16_t level);

/************************************ HELPER FUNCTIONS *************************************/

/// <summary>
/// Check if we are allowed to run a privileged instruction
///
/// Privileged intructions are only available to programs running in system mode (rings 2 and 3) or when memory protection is disabled;
/// </summary>
/// <returns>TRUE if allowed to execute</returns>
bool CheckPriv(void)
{
	if (!STS_PONI)
		return true; // memory protection disabled

	// Check ring
	uint16_t pcr = gReg->reg_PCR[CurrLEVEL];
	uint16_t ring = pcr & 0x03;

	if ((ring == 2) || (ring == 3))
		return true;

	// Failed, not allowed to execute
	// Generate a privileged instruction interrupt
	interrupt(14, 1 << 6); // Privileged instruction
	return false;
}


short signExtend(uint16_t x)
{
	short res = (uint16_t)x;

	// If negative (bit 7==1), extend high 8 bits with 1's
	if ((x & 1 << 7) != 0)
		res |= 0xFF00;

	return res;
}


uint16_t do_add(uint16_t a, uint16_t b, uint16_t k)
{
	int tmp;
	bool is_diff;
	tmp = ((int)a) + ((int)b) + ((int)k);
	/* C (carry) */
	if (tmp & 0xffff0000)
		setbit(_STS, _C, 1);
	else
		setbit(_STS, _C, 0);
	/* O(static overflow), Q (dynamic overflow) */
	is_diff = (((1 << 15) & a) ^ ((1 << 15) & b)); /* is bit 15 of the two operands different? */
	if (!(is_diff) && (((1 << 15) & a) ^ ((1 << 15) & tmp)))
	{						 /* if equal and result is different... */
		setbit(_STS, _O, 1); // Static overflow
		setbit(_STS, _Q, 1); // Dynamic overflow (Instruction test shows Q must be set)
	}
	else
	{
		setbit(_STS, _Q, 0);
		//setbit(_STS, _O, 0); NO!
	}
	return (uint16_t)tmp;
}


// Calculate effective address for LDnTX
unsigned int calcEL(uint8_t displacement)
{

	unsigned int EL = (gX + displacement) & 0xFFFF;
	EL = (gT & 0xFF) << 16 | EL;
	EL = EL & 0xFFFFFF; // Cap at 24 bits

	return EL;
}

// read el value from memory
unsigned int ReadEL(unsigned el)
{
	return ReadPhysicalMemory(el, true);
}

// write el to memory
void WriteEL(uint32_t el, uint16_t value)
{
	WritePhysicalMemory(el, value, true);
}



/***************** HELPER INSTRUCTIONS *****************/

void illegal_instr(uint16_t operand)
{
	/*
	 * --log=cpu:debug logs every illegal-instruction trap.  This is how a guest's
	 * CPU-type probe is observed: TPE's INSTRUCTION program executes VERSN (140133)
	 * and decides "ND-100" if - and only if - it traps here.
	 */
	LOG(LOG_CAT_CPU, LOG_DEBUG, "ILLEGAL %06o at %06o", operand, gPC);

	interrupt(14, 1 << 4); /* Illegal Instruction <= WILL TRAP! */
}

void unimplemented_instr(uint16_t operand)
{
	printf("\r\n");
	printf("--------------------------------\r\n");
	printf("CPU: Unimplemented instruction: %06o at PC: %06o\r\n", operand, gPC);
	printf("--------------------------------\r\n");
	printf("\r\n");

	//set_cpu_run_mode(CPU_STOPPED); /* OK unimplemented function, lets stop CPU and end program that way */
}


/************************************ INSTRUCTIONS *************************************/



/* AAA
 */
void ndfunc_aaa(uint16_t operand)
{
	short temp;

	temp = signExtend(operand & 0xFF);
	gA = do_add(gA, temp, 0);
}

/* AAB
 */
void ndfunc_aab(uint16_t operand)
{
	uint16_t temp;

	temp = signExtend(operand & 0xFF);
	gB = do_add(gB, temp, 0);
}

/* AAT
 */
void ndfunc_aat(uint16_t operand)
{
	uint16_t temp;

	temp = signExtend(operand & 0xFF);
	gT = do_add(gT, temp, 0);
}

/* AAX
 */
void ndfunc_aax(uint16_t operand)
{
	uint16_t temp;

	temp = signExtend(operand & 0xFF);

	gX = do_add(gX, temp, 0);
}

/* MON
 */
void ndfunc_mon(uint16_t operand)
{
	uint16_t monitor_number = (operand & 0x1ff);

	// TODO:MAYBE, add emulation layer here
	if (false)
	{
		// identfy montitor call and check if it should be intercepted!
		//mon(monitor_number);
	}
	else
	{
		if (CurrLEVEL < 14)
		{
			if ((monitor_number & (1 << 8)) != 0)
			{
				monitor_number |= 0xFE00; // Sign extend
			}

			gReg->reg[14][_T] = monitor_number;
			interrupt(14, 1 << 1); /* Monitor Call */
			gCHKIT = true;
		}
	}
}

/* SAA
 */
void ndfunc_saa(uint16_t operand)
{
	setreg(_A, signExtend(operand & 0xFF));
}

/* SAB
 */
void ndfunc_sab(uint16_t operand)
{
	setreg(_B, signExtend(operand & 0xFF));
}

/* SAT
 */
void ndfunc_sat(uint16_t operand)
{
	setreg(_T, signExtend(operand & 0xFF));
}

/* SAX
 */
void ndfunc_sax(uint16_t operand)
{
	setreg(_X, signExtend(operand & 0xFF));
}

/* SHT, SHD, SHA, SAD
 */
void ndfunc_shifts(uint16_t operand)
{
	uint32_t double_reg;

	switch ((operand >> 7) & 0x03)
	{
	case 0: /* SHT */
		gT = ShiftReg(gT, operand);
		break;
	case 1: /* SHD */
		gD = ShiftReg(gD, operand);
		break;
	case 2: /* SHA */
		gA = ShiftReg(gA, operand);
		break;
	case 3: /* SAD */
		double_reg = ShiftDoubleReg(((uint32_t)gA << 16) | gD, operand);
		gA = double_reg >> 16;
		gD = double_reg & 0xFFFF;
		break;
	default: /* can never reach here but... */
		break;
	}
}

/* NLZ
 */
void ndfunc_nlz(uint16_t operand)
{
	if (CurrentFPPType == FPP48)
		DoNLZ(operand & 0xFF);
	else
		DoNLZ32(operand & 0xFF);   /* 32-bit FPP: gT is not touched */
}

/* DNZ
 */
void ndfunc_dnz(uint16_t operand)
{
	if (CurrentFPPType == FPP48)
		DoDNZ(operand & 0xFF);
	else
		DoDNZ32(operand & 0xFF);   /* 32-bit FPP: gT is not touched */
}

/* SRB (Privileged)
 */
void ndfunc_srb(uint16_t operand)
{
	if (!CheckPriv())
		return;

	/* SRB */ /* NOTE: These two seems to have bit req on 0-2 as well */
	DoSRB(operand);
}

/* LRB (Privileged)
 */
void ndfunc_lrb(uint16_t operand)
{
	if (!CheckPriv())
		return;

	/* SRB */ /* NOTE: These two seems to have bit req on 0-2 as well */
	DoLRB(operand);
}

/// <summary>
/// CJP - Conditional jump
/// Instruction bits 8-10 are used to specify one of 8 jump conditions.
///
/// If the specified condition becomes true, the displacement is added to the program counter and a jump relative to current location takes place.
/// The range is 128 locations backwards and 127 locations forwards. If the specified condition is false, no jump takes place.
///
/// Execution time depends on conditions, but is the same for all instructions.
///
/// A conditional jump instruction must be specified by means of the 8 mnemonics listed below.
/// It is illegal to specify CJP or any combinations of, B, | and , X.
/// </summary>
void CJP(bool jmp_flag, uint16_t operand)
{
	if (jmp_flag)
	{
		uint16_t old_gPC = gPC - 1;

		uint16_t temp = signExtend(operand & 0xff);

		/* MICROCODE-VALIDATED 2026-07-20: the address arithmetic must NOT touch STS.
		 * do_add() writes STS C (and O/Q) as a side effect, but the whole CJP family
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

		if (DISASM)
			disasm_userel(old_gPC, gPC);
	}
}

/// <summary>
/// JAP - Jump if A register is positive or zero, A bit 15 = 0.
/// Code: 130 000
/// Format: JAP <disp.>
///
/// Affected: (P)
/// </summary>
void ndfunc_jap(uint16_t operand)
{
	bool flag = ((1 << 15) & gA) == 0;
	CJP(flag, operand);
}

/// <summary>
/// JAN - Jump if A register is negative, A bit 15 = 1.
/// Code: 130 400
/// Format: JAN <disp.>
///
/// Affected: (P)
/// </summary>
void ndfunc_jan(uint16_t operand)
{
	bool flag = ((1 << 15) & gA) != 0;
	CJP(flag, operand);
}

/// <summary>
/// JAZ - Jump if A register is zero.
/// Code: 131 000
/// Format: JAZ<disp>
///
/// Affected: (P)
/// </summary>
void ndfunc_jaz(uint16_t operand)
{
	/* MICROCODE-VALIDATED 2026-07-20: JAZ does NOT touch STS.
	 * RASK CS 007310-007313 (ND-110-RASK.LISTING.TXT:12259-12262) is
	 *   "A,A ALUF,PASSA ALUD,NONE IDBS,LA COMM,CJMP,F=0 T,JMP T,HOLD CJP1"
	 * - there is NO "STS,xx" token in the micro-word, and STS bits 0-7 are written only by
	 * the STS,EA / STS,ES / STS,LO tokens. ALUD,NONE means the PASSA result is not even
	 * latched. The same holds for every other CJP entry (JAP 007300, JAN 007304,
	 * JAF 007314, JPC 007320, JNC 007324, JXZ 007330, JXN 007334).
	 * The live RASK oracle confirms it: with A=0 and C seeded 0 the taken jump leaves C=0,
	 * with C seeded 1 it leaves C=1 - C is simply PRESERVED.
	 * The removed line ("setbit(_STS, _C, gA == 0)") was a fabricated carry side effect.
	 */
	CJP(gA == 0, operand);
}

/// <summary>
/// JAF - Jump if A register is filled (not zero)
/// Code: 131 400
/// Format: JAF<disp. >
///
/// Affected: (P)
/// </summary>
void ndfunc_jaf(uint16_t operand)
{
	CJP(gA != 0, operand);
}

/// <summary>
/// JPC - Count and jump if X register is positive or zero.
/// Code: 132000
/// Format: JPC<disp. >
///
/// X is incremented by one, and if the X bit 15 equals zero after the incrementation, the jump takes place.
/// Affected: (P) and (X)
/// </summary>
void ndfunc_jpc(uint16_t operand)
{
	gX++;

	CJP(((1 << 15) & gX) == 0, operand);
}

/// <summary>
/// JNC - Count and jump if X register is negative.
/// Code: 132 400
/// Format: JNC<disp.>
/// X is incremented by one; if then the X bit 15 equals one, the jump takes place.
///
/// Affected: (P) and(X)
/// </summary>
void ndfunc_jnc(uint16_t operand)
{
	gX++;
	CJP((gX & (1 << 15)) != 0, operand);
}

/// <summary>
/// JXN - Jump if X register is negative. X bit 15 = 1.
/// Code: 133 400
/// Format: JXN <disp. >
///
/// Affected: (P)
/// </summary>
void ndfunc_jxn(uint16_t operand)
{
	CJP((gX & (1 << 15)) != 0, operand);
}

/// <summary>
/// JXZ - Jump if X register is zero.
/// Code: 133 000
/// Format: JXZ <disp. >_
///
/// Affected: (P)
/// </summary>
void ndfunc_jxz(uint16_t operand)
{
	CJP(gX == 0, operand);
}

/* JPL
 */
void ndfunc_jpl(uint16_t operand)
{
	uint16_t old_gPC = gPC - 1;

	gEA = New_GetEffectiveAddr(operand, &gUseAPT);

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
		gL = gPC;

	gPC = gEA;

	if (DISASM)
		disasm_userel(old_gPC, gPC);
}

/* SKP
 * Skip instructions, this one interleaves with other instructions so might need some extra checkings.
 */
void ndfunc_skp(uint16_t operand)
{
	if (IsSkip(operand))
		gPC++;
}

/// <summary>
/// BFILL - Byte Fill
/// Code: 140 130
/// Format: BFILL
///
/// This instruction has only one operand. The destination operand is specified in the X, and T registers.
/// The right-most byte in the A-reg. (bits 0-7) is filled into the destination field.
///
/// After execution, the X-register and T-register bit 15 point to the end of the field(after the last byte).
/// The T-register bits(0-11) equal zero.
///
/// The instruction will always have a skip return (no error condition)
/// </summary>
void ndfunc_bfill_new(uint16_t operand)
{
	(void)operand;
	bool useAPT = false;
	WriteMode wm;

	// Check if we should use alternative page table, bit 14 in T register
	if ((gT & (1 << 14)) != 0)
		useAPT = true;

	while ((gT & 0xfff) != 0)
	{
		// Bit 15:  0=>MSB, 1=> LSB
		wm = (gT & (1 << 15)) ? WRITEMODE_LSB : WRITEMODE_MSB;
		WriteVirtualMemory(gX, gA & 0xFF, useAPT, wm);

		gT--;

		gT ^= (1 << 15); // Flip T bit 15
		if ((gT & (1 << 15)) == 0)
			gX++;
	}

	gPC++; // Skip return
}

void ndfunc_bfill(uint16_t operand)
{
	(void)operand;
	uint16_t d1, len, addr, i;
	uint16_t right = (gT & ((uint16_t)1 << 15)) ? 1 : 0;	   /* Start with right byte? (LSB) */
	bool is_apt = (gT & ((uint16_t)1 << 14)) ? true : false; /* Use APT or not? */
	uint16_t thebyte = gA & 0xff;
	len = gT & 0x0fff; /* Number of bytes to do */
	addr = gX;		   /* just in case we do 0 bytes */
	d1 = gX;

	for (i = 0; i < len; i++)
	{
		addr = d1 + ((i + right) >> 1); /* Word adress of byte to write */
		MemoryWrite(thebyte, addr, is_apt, ((i + right) & 1));
	}
	gT &= 0x7000;				   /* Null number of bytes, as per manual, also null bit 15 */
	gT |= ((i + right) & 1) << 15; /* set bit 15 to point to next free byte */
	gX = d1 + ((i + right) >> 1);


	gPC++; /* This function has a SKIP return on no error, which is always? */
}

/* STZ
 */
void ndfunc_stz(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);
	MemoryWrite(0, gEA, gUseAPT, 2);
}

/* STA
 */
void ndfunc_sta(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);

	MemoryWrite(gA, gEA, gUseAPT, 2);
}

/* STT
 */
void ndfunc_stt(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);
	MemoryWrite(gT, gEA, gUseAPT, 2);
}

/* STX
 */
void ndfunc_stx(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);
	MemoryWrite(gX, gEA, gUseAPT, 2);
}

/* STD
 */
void ndfunc_std(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);
	MemoryWrite(gA, gEA + 0, gUseAPT, 2);
	MemoryWrite(gD, gEA + 1, gUseAPT, 2);
}

/* STF
 */
/* STF - store float, ALWAYS 3 words (T->ea, A->ea+1, D->ea+2), in BOTH FPP
 * modes. Deliberately NOT gated on CurrentFPPType: LDF/STF are CPU data
 * movers, not FPP arithmetic, and the real microcode has no mode branch -
 * verified in ND-110 RASK (STF: 11607 -> STF1: 405 -> STD1: 400) and ND-120
 * DELILAH-L/K (13593/475/462); every addressing-mode slot enters via the T
 * word. Consequence for --fpp=32: the 32-bit float lives in A,D, so store/
 * load it with STD/LDD (2 words, layout matching the FAD..FDV memory
 * operand at ea/ea+1). STF on a 32-bit float writes stale T at ea and lands
 * the value one word off - misaligned exactly as on the real CPU.
 */
void ndfunc_stf(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);
	MemoryWrite(gT, gEA + 0, gUseAPT, 2);
	MemoryWrite(gA, gEA + 1, gUseAPT, 2);
	MemoryWrite(gD, gEA + 2, gUseAPT, 2);
}

/* LDA
 */
void ndfunc_lda(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);
	gA = MemoryRead(gEA, gUseAPT);
}

/* LDT
 */
void ndfunc_ldt(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);
	gT = MemoryRead(gEA, gUseAPT);
}

/* LDX
 */
void ndfunc_ldx(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);
	gX = MemoryRead(gEA, gUseAPT);
}

/* LDD
 */
void ndfunc_ldd(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);

	gA = MemoryRead(gEA + 0, gUseAPT);
	gD = MemoryRead(gEA + 1, gUseAPT);
}

/* LDF
 */
/* LDF - load float, ALWAYS 3 words (ea->T, ea+1->A, ea+2->D), in BOTH FPP
 * modes - the exact mirror of STF above, and like STF deliberately NOT
 * gated on CurrentFPPType. Real-microcode evidence: ND-110 RASK LDF1: 407
 * -> LDD1: 402 -> fall-through into D; all 32 LDF addressing-mode slots
 * (11647-11690) funnel into LDF1, none starts at LDD1 (which a 2-word A,D
 * load would need). ND-120 DELILAH-L identical (481/468). Consequence for
 * --fpp=32: LDF overwrites T (which the 32-bit FPP never touches) and reads
 * the A,D value one word off - use LDD instead for 32-bit floats.
 */
void ndfunc_ldf(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);

	gT = MemoryRead(gEA + 0, gUseAPT);
	gA = MemoryRead(gEA + 1, gUseAPT);
	gD = MemoryRead(gEA + 2, gUseAPT);
}




/* STZTX
 */
void ndfunc_stztx(uint16_t operand)
{
	if (!CheckPriv())
		return;

	uint8_t displacement = (operand >> 3) & 0x07;
	uint32_t EL = calcEL(displacement);
	WriteEL(EL, 0);

}

/* STATX
 */
void ndfunc_statx(uint16_t operand)
{
	if (!CheckPriv())
		return;

	uint8_t displacement = (operand >> 3) & 0x07;
	uint32_t EL = calcEL(displacement);
	WriteEL(EL, gA);

}

/* STDTX
 */
void ndfunc_stdtx(uint16_t operand)
{
	if (!CheckPriv())
		return;

	uint8_t displacement = (operand >> 3) & 0x07;
	uint32_t EL = calcEL(displacement);
	WriteEL(EL, gA);
	WriteEL(EL + 1, gD);

}

/// <summary>
/// Load A register
///
/// Code: 143 3n0
/// Format: LDATX
///
/// Load the contents of the physical memory location pointed to
/// by the effective address into the A register.
/// A := (EL)
///
/// Affected: (A)
/// </summary>
void ndfunc_ldatx(uint16_t operand)
{
	if (!CheckPriv())
		return;

	uint8_t displacement = (operand >> 3) & 0x07;

	unsigned int EL = calcEL(displacement);
	gA = ReadEL(EL);
}

/// <summary>
/// Load X register
/// Code: 143 3n1
/// Format: LDXTX
///
/// Load the contents of the physical memory location pointed to
/// by the effective address into the X  register.
/// X := (EL)
///
/// Affected: (X)
/// </summary>
void ndfunc_ldxtx(uint16_t operand)
{
	if (!CheckPriv())
		return;

	uint8_t displacement = (operand >> 3) & 0x07;
	unsigned int EL = calcEL(displacement);

	gX = ReadEL(EL);
}

/// <summary>
/// Load Double Word
/// Code: 143 3n2
/// Format: LDDTX
///
/// Load the contents of the physical memory location pointed to by the effective address
/// into the A register and the contents of the effective address plus one  into the D register
/// A := (EL), D := (EL + 1) .
///
/// Affected: (A,D)
/// </summary>
void ndfunc_lddtx(uint16_t operand)
{

	if (!CheckPriv())
		return;

	uint8_t displacement = (operand >> 3) & 0x07;
	unsigned int EL = calcEL(displacement);

	gA = ReadEL(EL);
	EL++;
	gD = ReadEL(EL);
}

/// <summary>
/// Load B register
///
/// Code: 143 3n3
/// Format: LDBTX
///
/// Load the contents of the physical memory location pointed to by the twice the
/// effective address contents into the B register, then OR the value with 177 000
/// B := 177000 V ((EL) + (EL)) (V = inclusive OR)
///
/// Affected: (B)
/// </summary>
void ndfunc_ldbtx(uint16_t operand)
{
	uint16_t temp;
	unsigned int result;

	if (!CheckPriv())
		return;

	uint8_t displacement = (operand >> 3) & 0x07;
	unsigned int EL = calcEL(displacement);

	temp = ReadEL(EL);
	result = (temp + temp) & 0xFFFF;
	gB = result | 0xFE00; // 0177000
}

/// <summary>
/// MIN - Increment memory and skip if zero
/// Code: 040 000
///
/// Format: MIN <address mode> <disp.>
///
/// Effective word is read and incremented by one and then stored in the effective location.If the result becomes zero, the next instruction is skipped.
///
/// Affected: (EL), (P)
/// </summary>
void ndfunc_min(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);

	uint16_t temp = MemoryRead(gEA, gUseAPT);
	temp++;
	MemoryWrite(temp, gEA, gUseAPT, 2);

	if (temp == 0)
		gPC++; // Next instruction is skipped
}

/* ADD
 */
void ndfunc_add(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);

	uint16_t eff_word = MemoryRead(gEA, gUseAPT);
	gA = do_add(gA, eff_word, 0);
}

/* SUB
 */
void ndfunc_sub(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);
	uint16_t eff_word = MemoryRead(gEA, gUseAPT);
	gA = do_add(gA, ~eff_word, 1);
}

/* AND
 */
void ndfunc_and(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);
	gA = gA & MemoryRead(gEA, gUseAPT);
}

/* ORA
 */
void ndfunc_ora(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);
	gA = gA | MemoryRead(gEA, gUseAPT);
}

/* FAD
 *
 * FPP32 memory-operand layout (also FSB/FMU/FDV below): the 2-word operand
 * lives at ea/ea+1 and the accumulator is the A,D pair. IMPORTANT: LDF/STF
 * are NOT the store/load path for these floats - they are unconditional
 * 3-word T/A/D movers in the real microcode (see ndfunc_stf/ndfunc_ldf) and
 * would place the value one word off. Store/load 32-bit floats with STD/LDD.
 */
void ndfunc_fad(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);

	if (CurrentFPPType == FPP48) {
		uint16_t a[3], b[3], r[3];

		a[0] = gT;
		a[1] = gA;
		a[2] = gD;
		b[0] = MemoryRead(gEA + 0, gUseAPT);
		b[1] = MemoryRead(gEA + 1, gUseAPT);
		b[2] = MemoryRead(gEA + 2, gUseAPT);
		NDFloat_Add(a, b, r);
		gT = r[0];
		gA = r[1];
		gD = r[2];
	} else {
		uint16_t a[2], b[2], r[2];

		a[0] = gA;
		a[1] = gD;
		b[0] = MemoryRead(gEA + 0, gUseAPT);   /* only TWO words */
		b[1] = MemoryRead(gEA + 1, gUseAPT);
		NDFloat_Add32(a, b, r);
		gA = r[0];
		gD = r[1];                             /* gT untouched */
	}
}

/* FSB
 */
void ndfunc_fsb(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);

	if (CurrentFPPType == FPP48) {
		uint16_t a[3], b[3], r[3];

		a[0] = gT;
		a[1] = gA;
		a[2] = gD;
		b[0] = MemoryRead(gEA + 0, gUseAPT);
		b[1] = MemoryRead(gEA + 1, gUseAPT);
		b[2] = MemoryRead(gEA + 2, gUseAPT);
		NDFloat_Sub(a, b, r);
		gT = r[0];
		gA = r[1];
		gD = r[2];
	} else {
		uint16_t a[2], b[2], r[2];

		a[0] = gA;
		a[1] = gD;
		b[0] = MemoryRead(gEA + 0, gUseAPT);   /* only TWO words */
		b[1] = MemoryRead(gEA + 1, gUseAPT);
		NDFloat_Sub32(a, b, r);
		gA = r[0];
		gD = r[1];                             /* gT untouched */
	}
}

/* FMU
 */
void ndfunc_fmu(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);

	if (CurrentFPPType == FPP48) {
		uint16_t a[3], b[3], r[3];

		a[0] = gT;
		a[1] = gA;
		a[2] = gD;
		b[0] = MemoryRead(gEA + 0, gUseAPT);
		b[1] = MemoryRead(gEA + 1, gUseAPT);
		b[2] = MemoryRead(gEA + 2, gUseAPT);
		NDFloat_Mul(a, b, r);
		gT = r[0];
		gA = r[1];
		gD = r[2];
	} else {
		uint16_t a[2], b[2], r[2];

		a[0] = gA;
		a[1] = gD;
		b[0] = MemoryRead(gEA + 0, gUseAPT);   /* only TWO words */
		b[1] = MemoryRead(gEA + 1, gUseAPT);
		NDFloat_Mul32(a, b, r);
		gA = r[0];
		gD = r[1];                             /* gT untouched */
	}
}

/* FDV
 */
void ndfunc_fdv(uint16_t operand)
{
	gEA = New_GetEffectiveAddr(operand, &gUseAPT);

	if (CurrentFPPType == FPP48) {
		uint16_t a[3], b[3], r[3];

		a[0] = gT;
		a[1] = gA;
		a[2] = gD;
		b[0] = MemoryRead(gEA + 0, gUseAPT);
		b[1] = MemoryRead(gEA + 1, gUseAPT);
		b[2] = MemoryRead(gEA + 2, gUseAPT);
		if (NDFloat_Div(a, b, r)) {
			/* Division by zero - set error indicator Z */
			setbit(_STS, _Z, 1);
		}
		gT = r[0];
		gA = r[1];
		gD = r[2];
	} else {
		uint16_t a[2], b[2], r[2];

		a[0] = gA;
		a[1] = gD;
		b[0] = MemoryRead(gEA + 0, gUseAPT);   /* only TWO words */
		b[1] = MemoryRead(gEA + 1, gUseAPT);
		if (NDFloat_Div32(a, b, r)) {
			/* Division by zero - set error indicator Z */
			setbit(_STS, _Z, 1);
		}
		gA = r[0];
		gD = r[1];                             /* gT untouched */
	}
}

/* JMP
 */
void ndfunc_jmp(uint16_t operand)
{
	uint16_t old_gPC = gPC - 1;

	gEA = New_GetEffectiveAddr(operand, &gUseAPT);
	gPC = gEA;

	if (DISASM)
		disasm_userel(old_gPC, gPC);
}

/* GECO
 */
void ndfunc_geco(uint16_t operand)
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
#define VERSN_PROM_SIZE				16
#define VERSN_PROM_SYSNO_HI			0
#define VERSN_PROM_SYSNO_LO			1
#define VERSN_PROM_SYSTYPE_HI		2
#define VERSN_PROM_SYSTYPE_LO		3
#define VERSN_PROM_LEGAL_USERS		4
#define VERSN_PROM_UNUSED			5
#define VERSN_PROM_SIGNATURE_HI		6
#define VERSN_PROM_SIGNATURE_LO		7

/* INF3 signature GCPUNR demands: 52652 octal = 21930 decimal = 0x55AA. */
#define VERSN_PROM_SIGNATURE		0x55AA

/* NLEGU byte value meaning "GCPUNR must NOT set the number of legal users". */
#define VERSN_PROM_LEGAL_USERS_SKIP	0xFF

/* Lowest microprogram version SINTRAN's LOCOSTORE accepts: octal 013 = 11. */
#define VERSN_MIN_MICROCODE_VERSION	0x0B

/*
 * Identity reported by VERSN. ONE named module-state struct instead of three
 * loose statics, so a re-init genuinely starts from a known state.
 */
struct versn_identity
{
	unsigned char	prom[VERSN_PROM_SIZE];	/* back-wiring PROM image, bytes 0-15 */
	int				microcode_version;		/* T register; see VERSN_MIN_MICROCODE_VERSION */
	int				print_version;			/* A register, 12 bits (PCB artwork version) */
};

static struct versn_identity g_versn;

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
		g_versn.prom[i] = 0x00;

	if ((CurrentCPUType == ND100) || (CurrentCPUType == ND100CE) || (CurrentCPUType == ND100CX)) {
		/* Historical filler - preserved byte for byte. 040171 = CPU? */
		static const unsigned char historical[VERSN_PROM_SIZE] = {
			0x01, 0x04, 0x00, 0x01, 0x07, 0x01, 0x01, 0x01,
			0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01
		};

		for (i = 0; i < VERSN_PROM_SIZE; i++)
			g_versn.prom[i] = historical[i];
		return;
	}

	g_versn.prom[VERSN_PROM_SYSNO_HI]      = (unsigned char)(102 >> 8);	/* SYSNO = 102 */
	g_versn.prom[VERSN_PROM_SYSNO_LO]      = (unsigned char)(102 & 0xFF);
	g_versn.prom[VERSN_PROM_SYSTYPE_HI]    = (unsigned char)(100 >> 8);	/* HWINFO(2) = 100 */
	g_versn.prom[VERSN_PROM_SYSTYPE_LO]    = (unsigned char)(100 & 0xFF);
	g_versn.prom[VERSN_PROM_LEGAL_USERS]   = VERSN_PROM_LEGAL_USERS_SKIP;
	g_versn.prom[VERSN_PROM_UNUSED]        = 0x00;
	g_versn.prom[VERSN_PROM_SIGNATURE_HI]  = (unsigned char)(VERSN_PROM_SIGNATURE >> 8);
	g_versn.prom[VERSN_PROM_SIGNATURE_LO]  = (unsigned char)(VERSN_PROM_SIGNATURE & 0xFF);
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
		return false;

	return (strcmp(text, "none") == 0) || (strcmp(text, "NONE") == 0)
		|| (strcmp(text, "keep") == 0) || (strcmp(text, "KEEP") == 0)
		|| (strcmp(text, "image") == 0) || (strcmp(text, "IMAGE") == 0)
		|| (strcmp(text, "-1") == 0);
}

/* Value of one hexadecimal digit, or -1 when the character is not hex. */
static int versn_hex_digit(char c)
{
	if ((c >= '0') && (c <= '9'))
		return c - '0';
	if ((c >= 'a') && (c <= 'f'))
		return c - 'a' + 10;
	if ((c >= 'A') && (c <= 'F'))
		return c - 'A' + 10;
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
		return false;

	len = strlen(text);
	if (len == 0)
		return false;

	end = len;

	if ((len > 2) && (text[0] == '0') && ((text[1] == 'x') || (text[1] == 'X'))) {
		radix = 16;
		start = 2;
	} else if ((len > 2) && (text[0] == '0') && ((text[1] == 'o') || (text[1] == 'O'))) {
		radix = 8;
		start = 2;
	} else if ((len > 1) && ((text[len - 1] == 'b') || (text[len - 1] == 'B'))) {
		radix = 8;
		end = len - 1;
	} else if ((len > 1) && (text[0] == '0')) {
		radix = 8;
		start = 1;
	}

	if (start >= end)
		return false;

	for (i = start; i < end; i++) {
		int d = versn_hex_digit(text[i]);

		if ((d < 0) || (d >= radix))
			return false;
		acc = acc * radix + d;
		if (acc > 0xFFFF)			/* identity values are at most 16 bits */
			return false;
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
		return false;

	if (strlen(text) == 1) {
		char c = text[0];
		int letter = -1;

		if ((c >= 'A') && (c <= 'Z'))
			letter = c - 'A' + 1;
		else if ((c >= 'a') && (c <= 'z'))
			letter = c - 'a' + 1;

		if (letter > 0) {
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
		return false;

	if (!versn_parse_number(text, out)) {
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
	if (text != NULL) {
		unsigned char parsed[VERSN_PROM_SIZE];
		int digits = 0;
		int i;
		bool ok = true;

		for (i = 0; (text[i] != '\0') && ok; i++) {
			char c = text[i];
			int d;

			/* Separators are ignored so "01 04 .." and "0104.." both work. */
			if ((c == ' ') || (c == '\t') || (c == ',') || (c == ':') || (c == '-') || (c == '_'))
				continue;

			d = versn_hex_digit(c);
			if ((d < 0) || (digits >= VERSN_PROM_SIZE * 2)) {
				ok = false;
				break;
			}

			if ((digits & 1) == 0)
				parsed[digits / 2] = (unsigned char)(d << 4);
			else
				parsed[digits / 2] |= (unsigned char)d;
			digits++;
		}

		if (ok && (digits == VERSN_PROM_SIZE * 2)) {
			for (i = 0; i < VERSN_PROM_SIZE; i++)
				g_versn.prom[i] = parsed[i];
		} else {
			LOG(LOG_CAT_CPU, LOG_WARN, "Bad ND100X_INSTALLATION_NUMBER '%s' - need exactly 32 hex digits", text);
		}
	}

	/* 2. Friendly decoded fields overlay the image. */
	text = getenv("ND100X_CPU_NUMBER");
	if (text != NULL) {
		if (versn_identity_is_skip(text))
			versn_set_word(VERSN_PROM_SYSNO_HI, VERSN_PROM_SYSNO_LO, 0xFFFF);
		else if (versn_parse_number(text, &value))
			versn_set_word(VERSN_PROM_SYSNO_HI, VERSN_PROM_SYSNO_LO, value);
		else
			LOG(LOG_CAT_CPU, LOG_WARN, "Bad ND100X_CPU_NUMBER '%s' - keeping the default", text);
	}

	text = getenv("ND100X_SYSTEM_TYPE");
	if (text != NULL) {
		/*
		 * The documented codes are 100/102/500/502/5561, but the SINTRAN source
		 * writes that list with a trailing ".." (OPPSTART.NPL:3440), i.e. it is
		 * OPEN-ENDED - any 16-bit value is accepted here on purpose.
		 */
		if (versn_identity_is_skip(text))
			versn_set_word(VERSN_PROM_SYSTYPE_HI, VERSN_PROM_SYSTYPE_LO, 0xFFFF);
		else if (versn_parse_number(text, &value))
			versn_set_word(VERSN_PROM_SYSTYPE_HI, VERSN_PROM_SYSTYPE_LO, value);
		else
			LOG(LOG_CAT_CPU, LOG_WARN, "Bad ND100X_SYSTEM_TYPE '%s' - keeping the default", text);
	}

	text = getenv("ND100X_LEGAL_USERS");
	if (text != NULL) {
		if (versn_identity_is_skip(text)) {
			value = VERSN_PROM_LEGAL_USERS_SKIP;
		} else if (!versn_parse_number(text, &value) || (value > 0xFE)) {
			LOG(LOG_CAT_CPU, LOG_WARN, "Bad ND100X_LEGAL_USERS '%s' - want 0..254 or none", text);
			value = -1;
		}

		if (value >= 0) {
			g_versn.prom[VERSN_PROM_LEGAL_USERS] = (unsigned char)value;
			/* Same reason as versn_set_word(): the field is only useful if GCPUNR reads it. */
			g_versn.prom[VERSN_PROM_SIGNATURE_HI] = (unsigned char)(VERSN_PROM_SIGNATURE >> 8);
			g_versn.prom[VERSN_PROM_SIGNATURE_LO] = (unsigned char)(VERSN_PROM_SIGNATURE & 0xFF);
		}
	}

	/* 3. The two register-only values. */
	text = getenv("ND100X_MICROCODE_VERSION");
	if (text != NULL) {
		if (!versn_parse_microcode_version(text, &value))
			LOG(LOG_CAT_CPU, LOG_WARN, "Bad ND100X_MICROCODE_VERSION '%s' - keeping the default", text);
		else if (value < VERSN_MIN_MICROCODE_VERSION)
			LOG(LOG_CAT_CPU, LOG_WARN, "ND100X_MICROCODE_VERSION '%s' is below the SINTRAN minimum of octal 013", text);
		else if (value > 0xFFFF)
			LOG(LOG_CAT_CPU, LOG_WARN, "ND100X_MICROCODE_VERSION '%s' does not fit in 16 bits", text);
		else
			g_versn.microcode_version = value;
	}

	if (versn_env_number("ND100X_PRINT_VERSION", &value)) {
		/* VERSN builds A as (print_version << 4) | (ALD & 0x0F) - only 12 bits survive. */
		if (value <= 0x0FFF)
			g_versn.print_version = value;
		else
			LOG(LOG_CAT_CPU, LOG_WARN, "ND100X_PRINT_VERSION must fit in 12 bits - keeping the default");
	}
}

/*
 * True when the emulated CPU is an ND-120 (any ND-120 variant). SINTRAN's SYSEVAL uses
 * VERSN's T-register bit 15 to tell an ND-120 from an ND-110; this predicate drives that
 * bit (see ndfunc_versn / the TRA CS path). Ported from the ND-120-support helper
 * (commit 97c9961); adapted to this tree's CpuType enum, which carries only ND120CX.
 */
static bool versn_is_nd120(void)
{
	return (CurrentCPUType == ND120CX);
}

void ndfunc_versn(uint16_t operand)
{
	(void)operand;
	/*
	 * A bits 8-11 select which of the SIXTEEN PROM bytes to return in D.
	 * The array is now VERSN_PROM_SIZE (16) entries long; it used to be 15,
	 * so index 15 read one byte past the end of the array.
	 */
	int offset = (gA >> 8) & 0x0F;
	uint16_t a_in = gA; /* input A (PIL/offset selector) before VERSN overwrites it - for the trace below */

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
	if (CurrentCPUType == ND120CX)
	{
		gA = (uint16_t)(((5  & 0x07) << 13)   /* PRINT NUMBER  = 5  => 3202 */
		            | ((20 & 0x1F) << 8)    /* ECO LEVEL     = 20 (straps 6,8,9) */
		            | (1           << 7)    /* CX/high-speed = 1 */
		            | ((4  & 0x07) << 4)    /* PRINT RELEASE = 4  => "D" */
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
	LOG(LOG_CAT_CPU, LOG_DEBUG, "[VERSN] A_in=%06o off=%2d PC=%06o -> D=%06o A=%06o T=%06o",
	    a_in, offset, gPC, gD, gA, gT);
}


/************ IO INSTRUCTIONS *************/



/// <summary>
/// Check if the IO address points to special "in memory" registers
///
/// Addresses from 100000 . - 100777, are used to specify system control registers which have to be accessed via the ND-100 bus.
/// An example is the Error Correction Control Register (ECCR), physically located on the memory modules.
/// </summary>
/// <returns>true if the IO address was handled, false otherwise</returns>
bool UpdateMemoryIO(void)
{
	if ((gT < 0x8000) || (gT > 0x81FF))
		return false;

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
bool DeviceManager_IotOp(uint8_t devno, uint8_t func, uint16_t *regA, bool *skip);

void ndfunc_iot(uint16_t operand)
{
	// ND110 Microcode:
	// IOT - INSTRUCTION IS PRIVILEGED WHEN RING = 0 OR 1
	//                  AND ILLEGAL    WHEN RING = 2 OR 3
	if (!CheckPriv())
		return;

	// IOT is the NORD-10 I/O-transfer instruction. Its low 11 bits are the same
	// device/function field as IOX (opcode 0160000 vs 0164000; both mask 0x07ff),
	// so route it through the identical device dispatch. NORD TSS's teletype
	// scanner (LEV6, TSS1.SYMB:2673) issues "IOT ACT DIABD+2/+3" every 80 ms to
	// poke the Diablo terminal (device 156); with no such device attached, io_op
	// raises the IOX-error interrupt (level 14, IIC 7 = EIOX), which TSS's own
	// LEV14 handler counts and ignores (TSS1.SYMB:4294). Treating IOT as an
	// illegal instruction instead (the old stub) trapped IIC 4 -> ILLS -> TRAP
	// and spun TSS in an infinite trap loop, blocking LOGON.
	if (UpdateMemoryIO())
		return;

	/* NORD-1 decoding: bits 0-7 device number, bits 8-10 ACT/SKA/PIN
	 * (all zero = SNI). See NORD-1 Reference Manual sec 3.7 and the Device
	 * struct comment. If a device claims this NORD-1 device number we use
	 * that; SKA / "skip if OK" then skips the next instruction, which is what
	 * the classic "IOT SKA DVN / JMP *-1" wait loop needs. */
	{
		uint8_t  devno = (uint8_t)(operand & 0x00ff);
		uint8_t  func  = (uint8_t)((operand >> 8) & 0x07);
		uint16_t a     = gA;
		bool     skip  = false;

		if (DeviceManager_IotOp(devno, func, &a, &skip))
		{
			gA = a;
			if (skip)
				gPC++;
			return;
		}
	}

	/* Nothing claims it: keep the long-standing behaviour of treating IOT
	 * like IOX. TSS's teletype scanner poking a device that is not present
	 * relies on getting the IOX-error interrupt here rather than an illegal
	 * instruction trap (which used to spin it in a trap loop). */
	gA = io_op(operand & 0x07ff, gA);
}

/* IOX (Privileged)
 */
void ndfunc_iox(uint16_t operand)
{
	if (!CheckPriv())
		return;

	if (!UpdateMemoryIO())
		gA = io_op(operand & 0x07ff, gA);

}

/* IOXT (Privileged)
 */
void ndfunc_ioxt(uint16_t operand)
{
	(void)operand;
	if (!CheckPriv())
		return;


	if (!UpdateMemoryIO())
		gA = io_op(gT, gA);

}



/* IDENT
 *
 * NOTE: Privileged instruction
 */
void ndfunc_ident(uint16_t operand)
{
	if (!CheckPriv())
		return;

	switch ((operand & 0x003f))
	{
	case 004:
		DoIDENT(10);
		break;
	case 011:
		DoIDENT(11);
		break;
	case 022:
		DoIDENT(12);
		break;
	case 043:
		DoIDENT(13);
		break;
	default:
		illegal_instr(operand); /* Assume this is how we should hanle it.. TODO: Check!!! */
	}
}

/********************SYSTEM FUNCTIONS  *******************/


/* OPCOM (Privileged)
 */
void ndfunc_opcom(uint16_t operand)
{
	(void)operand;
	if (!CheckPriv())
		return;
	printf("\r\nOPCOM at PIL[%d] PC[%6o] A[%6o]\r\n", gPIL, gPC, gA);
	set_cpu_run_mode(CPU_STOPPED);
}

/// <summary>
/// IRW - Inter-Register Write
///
/// Note: This instruction results in a no-operation if the A register of the current program level is used
/// </summary>
void ndfunc_irw(uint16_t operand)
{
	if (!CheckPriv())
		return;

	uint16_t level = (operand >> 3) & 0x0F;
	uint16_t dr = (operand & 0x07);

	if ((level == CurrLEVEL) && (dr == _A))
		return; // A on same level, do nothing (Write from A to A on same level== NOP)

	if ((level == CurrLEVEL) && (dr == _P))
		return; // P on same level, do nothing (Because this is what the microcode does)

	if (dr == _STS)
	{
		// Update STS lower bits (which is unique for each runlevel)
		gReg->reg[level][_STS] = (gA & 0x00FF);
	}
	else
	{
		gReg->reg[level][dr] = gA;
	}
}

/// <summary>
/// IRR - Inter-Register Read
/// Code 0153600
///
/// This instruction is used to read into the A register on current program level one of the general registers inside/outside the current program level.
/// If bits 0-2 are zero, the status registers on the specified program level will be read into the A register bits 0-7, with bits 8-15 cleared.
/// The IRR instruction is privileged.
/// </summary>
void ndfunc_irr(uint16_t operand)
{
	if (!CheckPriv())
		return;

	uint16_t level = (operand >> 3) & 0x0F;
	uint16_t sr = (operand & 0x07);

	if (sr == 0) // STS
	{
		gA = gReg->reg[level][_STS] & 0xFF; // read only lower 8 bits
	}
	else
	{
		gA = gReg->reg[level][sr];
	}
}

/* EXAM (Privileged)
 */
void ndfunc_exam(uint16_t operand)
{
	(void)operand;
	if (!CheckPriv())
		return;

	// int fulladdress = (((unsigned int)gA) << 16) | (ushort)gD;
	unsigned int fulladdress = ((gA & 0xFF) << 16) | gD;
	gT = ReadPhysicalMemory(fulladdress, true);
}

/* DEPO (Privileged)
 */

void ndfunc_depo(uint16_t operand)
{
	(void)operand;
	if (!CheckPriv())
		return;

	unsigned int fulladdress = ((gA & 0xFF) << 16) | gD;
	WritePhysicalMemory(fulladdress, gT, true);
}

/* POF (Privileged)
 */
void ndfunc_pof(uint16_t operand)
{
	(void)operand;

	if (!CheckPriv())
		return;
	setbit_STS_MSB(_PONI, 0);
}

/* PIOF (Privileged)
 */
void ndfunc_piof(uint16_t operand)
{
	(void)operand;

	if (!CheckPriv())
		return;

	setbit_STS_MSB(_IONI, 0);
	setbit_STS_MSB(_PONI, 0);
}

/* PON
 */
void ndfunc_pon(uint16_t operand)
{
	(void)operand;
	setbit_STS_MSB(_PONI, 1);
}

/* PION
 */
void ndfunc_pion(uint16_t operand)
{
	(void)operand;
	setbit_STS_MSB(_IONI, 1);
	setbit_STS_MSB(_PONI, 1);
	gCHKIT = true; // recalc PK
}

/// <summary>
/// IOF - Turn off interrupt system
/// </summary>
void ndfunc_iof(uint16_t operand)
{
	(void)operand;
	if (!CheckPriv())
		return;

	setbit_STS_MSB(_IONI, 0);
}

/// <summary>
/// ION
///
/// Turn on interrupt system
/// </summary>
void ndfunc_ion(uint16_t operand)
{
	(void)operand;
	setbit_STS_MSB(_IONI, 1);
	gCHKIT = true; // recalc PK
}


/* REX (Privileged)
 */
void ndfunc_rex(uint16_t operand)
{
	(void)operand;
	if (!CheckPriv())
		return;

	setbit_STS_MSB(_SEXI, 0);
}

/* SEX (Privileged)
 */
void ndfunc_sex(uint16_t operand)
{
	(void)operand;
	if (!CheckPriv())
		return;

	setbit_STS_MSB(_SEXI, 1);
}

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
#define ND110_WIP_BIT	(1 << 12)

/* PGU ("page used") bit collected by CLEPU; R7 = BMG(013 octal) = 04000 octal = 2^11. */
#define ND110_PGU_BIT	(1 << 11)

/*
 * Computes a physical word address from a segment (physical 64K bank) and an offset.
 * address = (seg & 0xFF) << 16 | (offset & 0xFFFF).
 */
uint32_t nd110_seg_phys(uint16_t seg, uint16_t offset)
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
uint32_t nd110_bankgroup_phys(uint16_t bank, uint16_t index, uint16_t operand)
{
	uint16_t delta = (uint16_t)((operand >> 3) & 0x07);
	uint32_t ea = (uint32_t)((index + delta) & 0xFFFF);

	return ((uint32_t)(bank & 0xFF) << 16) | ea;
}



/* SETPT - ND110+
 *
 * NOTE: Privileged instruction
 */

void ndfunc_setpt(uint16_t operand)
{
	(void)operand;
	if (!CheckPriv())
		return;

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
		uint32_t EL = 0;
		uint32_t EffectiveAddress = 0;

		//  LDDTX 20 <=  A: = (EL), D: = (EL + 1)
		EL = calcEL(2); // Calculates using X, T and mriDisplacement // oct 020 >>3
		gA = (uint16_t)ReadEL(EL);
		gD = (uint16_t)ReadEL(EL + 1);

		// BSET ZRO 130 DA % PGU - BIT *

		gA = gA & ~(1 << 0x0b); // 0x0b = 13 octalt. Clear bit 013 in register A

		// LDBTX 10
		EL = calcEL(1); // oct 10 >> 3
		uint32_t elval = ReadEL(EL);
		gB = (uint16_t)(((elval + elval) & 0xFFFF) | 0xFE00); // 177000

		// 177777					% OLD BUG IN LDBTX

		// STD ,B
		EffectiveAddress = (uint32_t)(gB & 0xFFFF); // (+displacement, which is 0 here)
		WriteVirtualMemory(EffectiveAddress, gA, true, WRITEMODE_WORD);
		WriteVirtualMemory(EffectiveAddress + 1, gD, true, WRITEMODE_WORD);

		//  LDXTX 00 <=  X:= (EL)
		gX = (uint16_t)ReadEL(calcEL(0)); // Calculates using X, T and mriDisplacement

		// Increase counter
		cnt++;
	}

	gX = (uint16_t)cnt; // Report number of loops in X (undocumented, but testing using "INSTRUCTION - Version: C00 - 1986-10-30" sub-program "SEGMENTS" identified it.
}

// **************************************************************************************
// ****  ND100 and ND110CX only - segment instructions
// **************************************************************************************

// PDF Page 101 (page number 99) in "MICROPROGRAMLISTNING FOR ND-110_32 BIT VERSION K-Gandalf-OCR.pdf"
/// SINTRAN III CONTROL INSTRUCTIONS
/// ALL ARE PRIVILEGED

/// <summary>
/// Clear Page Tables
/// Code: 140 301
/// Format: CLEPT
///
/// Affected: (?)
/// </summary>
void ndfunc_clept(uint16_t operand)
{
	(void)operand;
	if (!CheckPriv())
		return;

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
	 * CLEPT:	JXZ * 10	(if X=0 goto END)
	 *		LDBTX 10	(B:=177000|(2*(EL)), EL=T,X+1)
	 *		LDA ,B		(A:=(B))
	 *		JAZ * 3		(if A=0 goto LOOP)
	 *		STATX 20	((EL):=A, EL=T,X+2)
	 *		STZ ,B		( (B):=0 )
	 *		LDXTX 00	(X:=(EL), EL=T,X)
	 * LOOP:	JMP *-7		(goto CLEPT)
	 * END:		...
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
		uint16_t nextX;
		uint32_t elval;

		/* 004121-004122 (PATA2): read the next-node pointer at [X] (physical, bank T). */
		nextX = (uint16_t)ReadEL(calcEL(0));

		/* 004123: X == 0 ends the walk - but X is still loaded from [X] on this final pass. */
		if (gX == 0)
		{
			gX = nextX;
			break;
		}

		/* 004124-004130 (LDBTX 10): page index at [X+1] -> entry address B = 0177000 | (2*index). */
		elval = ReadEL(calcEL(1));
		gB = (uint16_t)(((elval + elval) & 0xFFFF) | 0xFE00); /* 177000 */

		/* 004074 / PATA4 (LDA ,B): read the page-table entry via the ALTERNATIVE page table. */
		gA = (uint16_t)ReadVirtualMemory(gB, true);

		/* 004075 (JAZ *3): a zero (unused) entry is skipped; a used entry is saved then cleared. */
		if (gA != 0)
		{
			/* 004077 (STATX 20): save the entry to [X+2] (physical, bank T). */
			WriteEL(calcEL(2), (uint16_t)gA);

			/* 004116 (STZ ,B): clear the page-table entry via the ALTERNATIVE page table. */
			WriteVirtualMemory(gB, 0, true, WRITEMODE_WORD);
		}

		/* Advance to the next node (X := [X], already read at the top of this iteration). */
		gX = nextX;
	}
}

/// <summary>
/// Clear non re-entrant pages
/// Code: 140 302
/// Format: CLNREENT
///
/// Segment function
///
///
/// The contents of the memory address at A+2 are read to find the page table to be cleared along with the SINTRAN RT bitmap (addressed by the X and T registers).
/// The page table entries corresponding to those bits set in the RT bitmap are then cleared.
///
/// Affected: (?)
/// </summary>
void ndfunc_clnreent(uint16_t operand)
{
	(void)operand;
	uint16_t a_reg;
	uint16_t x_reg;
	uint16_t t_reg;
	uint16_t r1;	/* page-table clear cursor (APT-relative) */
	uint16_t r2;	/* bitmap read cursor */
	uint16_t r3;	/* bitmap end (exclusive) */

	if (!CheckPriv())
		return;

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
		return;

	/*
	 * 004133: read the page-table pointer word via APT[A+2].  Its value is latched into Q
	 * but the rest of CLNR1 uses the fixed APT base 0177000 instead, so this read is a side
	 * effect only - it is kept so the memory-access trace matches the microcode oracle.
	 */
	(void)ReadVirtualMemory((uint16_t)(a_reg + 2), true);

	/*
	 * 004135-004141: R1 = 0177000 (octal) APT-relative page-table base; R2 = X + 25 (octal)
	 *                bitmap read cursor; R3 = X + T + 1 bitmap end (last bitmap word at X + T).
	 */
	r1 = 0xFE00;				/* 0177000 octal */
	r2 = (uint16_t)(x_reg + 0x15);		/* + 025 octal (= 21 decimal) */
	r3 = (uint16_t)(x_reg + t_reg + 1);

	/* Outer loop over the bitmap words (CLNR1 004142..CLNR2 004152, LISTING 9490-9537). */
	while (r3 != r2)			/* CLNR2: bitmap exhausted -> done */
	{
		uint16_t addr = r2;		/* 004142: address = old R2, then R2++ */
		uint16_t word;
		int bit;

		r2 = (uint16_t)(r2 + 1);
		word = (uint16_t)ReadVirtualMemory(addr, true);	/* 004143 */

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
				WriteVirtualMemory(r1, 0, true, WRITEMODE_WORD);	/* 004155 */
			r1 = (uint16_t)(r1 + 2);		/* 004153: 2-word stride per entry */
		}
	}
}

/// <summary>
/// Change Page Tables
/// Code 140 303
/// Format: CHREENTPAGES
///
/// Segment function
///
/// The X  register is used to address the current (R1) and previous(Rp)  scratch registers.
/// If the R1 is zero, the re-entrant page has nothing to change so the loop is left, otherwise the contents of the memory location pointed to by the R1+2 are loaded into T.
///
/// T then contains the protect table entry, if the page has not been written to (WIP bit 12 is zero )
/// T and R1 are loaded with Rp.
/// R1 (now containing Rp) is tested again for zero.
/// If the page has been written to, the T register is loaded with the contents of the second scratch register(R2) pointed to by R1,
/// and R2 becomes the address of Rp. X is loaded with R1 as the new pointer to the reentrant pages and Rp is loaded into the D register pointed to by A.
///
/// Affected: (?)
/// </summary>
void ndfunc_chreent_pages(uint16_t operand)
{
	(void)operand;
	uint16_t prog_d;
	uint16_t prog_x;
	uint16_t prog_t;
	uint16_t prev_seg;
	uint16_t prev_off;
	uint16_t seg;
	uint16_t off;

	if (!CheckPriv())
		return;

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

	seg = prog_d;			/* loaded segment register */
	off = prog_x;			/* MAR offset */

	for (;;)
	{
		uint16_t link;
		uint16_t status;

		/* CHRE2 004161: read the link word at segment:offset. */
		link = (uint16_t)ReadPhysicalMemory((int)nd110_seg_phys(seg, off), true);

		/*
		 * 004162-004163 / CHRE4 004200: a zero link ends the chain -> SKIP return
		 * (extra P+1), registers unchanged.
		 */
		if (link == 0)
		{
			gPC++;
			return;
		}

		seg = prog_t;		/* 004163: the status/link reads use the descriptor segment T */

		/* 004164-004166: read the status word at T:(link+2) and test WIP (bit 12). */
		status = (uint16_t)ReadPhysicalMemory((int)nd110_seg_phys(prog_t, (uint16_t)(link + 2)), true);

		if ((status & ND110_WIP_BIT) != 0)
		{
			/*
			 * WIP set: unlink this page.  004170: read successor at T:link;
			 * 004174: DEPOSIT it into the previous slot; 004172-004175: set D/A/X,
			 * normal return.
			 */
			uint16_t successor = (uint16_t)ReadPhysicalMemory((int)nd110_seg_phys(prog_t, link), true);

			WritePhysicalMemory((int)nd110_seg_phys(prev_seg, prev_off), successor, true);
			gD = prev_seg;
			gA = prev_off;
			gX = link;
			return;
		}

		/* NOT WIP (CHRE3 004176-004177): advance PREVIOUS to T:link, then follow the chain link. */
		prev_seg = prog_t;
		prev_off = link;
		off = link;		/* next CHRE2 reads T:link = the successor link */
	}
}

/// <summary>
/// Clear page tables and collect PGU information.
/// Code: 140 304
/// Format: CLEPU
///
/// Segment function
///
/// Affected: (?)
/// </summary>
void ndfunc_clepu(uint16_t operand)
{
	(void)operand;
	if (!CheckPriv())
		return;

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

							BIT 15									BIT O
							________________________________________________
		L-REG -> WORD	0	# PAGE 17								PAGE 0 #
		WORD			1	# PAGE 37									20 #
		WORD			2	# PAGE 57									40 #
		WORD			3	# PAGE 177								   160 #

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
	 * NOTE: unlike the older ndfunc_clept above, the next-node pointer at [X] is read FIRST,
	 * on every pass including the terminating one - that access order and the final X are
	 * oracle-verified (see the RetroCore CLEPT/CLEPU comments).
	 */
	for (;;)
	{
		uint16_t next_x;
		uint32_t idx;

		/* 004122 (PATA2): next-node pointer at [X], read first (physical, bank T). */
		next_x = (uint16_t)ReadEL(calcEL(0));

		/* 004123: X == 0 terminates; load X from [X] on the final pass. */
		if (gX == 0)
		{
			gX = next_x;
			break;
		}

		/* 004124-004130: page index at [X+1] -> entry address B = 0177000 | (2*index). */
		idx = ReadEL(calcEL(1));
		gB = (uint16_t)(((idx + idx) & 0xFFFF) | 0xFE00);	/* 177000 */

		/* 004074 / PATA4: read the page-table entry via the alternative page table. */
		gA = (uint16_t)ReadVirtualMemory(gB, true);

		/* 004075 (JAZ *3): skip unused (zero) entries. */
		if (gA != 0)
		{
			/* 004077 (STATX 20): save the entry to [X+2] (physical, bank T). */
			WriteEL(calcEL(2), gA);

			/*
			 * 004100-004114 (PGU block): if the entry's PGU bit is set, mark the page in
			 * the 8-word working-set table at L (page-map bank).
			 * word = page >> 4, bit = page & 0xF.
			 */
			if ((gA & ND110_PGU_BIT) != 0)
			{
				uint32_t page = idx & 0x7F;		/* 8 words * 16 bits = 128 pages */
				uint32_t word = page >> 4;		/* word number (0..7) */
				int bit = (int)(page & 0x0F);	/* bit within the word */
				uint32_t table_addr = (uint32_t)((gL + word) & 0xFFFF);
				uint16_t tw = (uint16_t)ReadPhysicalMemory((int)table_addr, true);	/* 004107 EXRQ */

				tw |= (uint16_t)(1 << bit);					/* 004112 set bit */
				WritePhysicalMemory((int)table_addr, tw, true);			/* 004114 DERQ */
			}

			/* 004116 (STZ ,B): clear the page-table entry via the alternative page table. */
			WriteVirtualMemory(gB, 0, true, WRITEMODE_WORD);
		}

		/* Advance to the next node. */
		gX = next_x;
	}
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

/* WGLOB - 140500 (privileged)
 *
 * Initialize the global pointers:
 *   (T) => bank number of segment table  (STBNK)
 *   (A) => start address within bank     (STSRT - must be divisible by 8)
 *   (D) => bank number of core map table (CMBUK)
 *
 * Ref ND-06.026.1 EN, page 196. Port of RetroCore WGLOB().
 */
void ndfunc_wglob(uint16_t operand)
{
	(void)operand;
	if (!CheckPriv())
		return;

	gSTBNK = gT;
	gSTSRT = gA;
	gCMBUK = gD;
}

/* RGLOB - 140501 (privileged)
 *
 * Examine the global pointers - the exact inverse of WGLOB:
 *   (T) <= STBNK, (A) <= STSRT, (D) <= CMBUK
 *
 * Ref ND-06.026.1 EN, page 196. Port of RetroCore RGLOB().
 */
void ndfunc_rglob(uint16_t operand)
{
	(void)operand;
	if (!CheckPriv())
		return;

	gT = gSTBNK;
	gA = gSTSRT;
	gD = gCMBUK;
}

/* INSPL - 140502 (privileged)
 *
 * Insert a page's core-map entry at the HEAD of a segment's page list.  Atomic - no
 * loop, normal P+1 return.  Operand registers:
 *   B = base word of the segment descriptor (in STBNK; page-list head lives at B+7)
 *   X = base word of the page's core-map entry (in CMBUK)
 *   T = the tag word stored at X+3
 *
 * Faithful to RASK microcode INSP1 (ND-110-RASK.LISTING.TXT 10386-10501).
 * Port of RetroCore INSPL().
 */
void ndfunc_inspl(uint16_t operand)
{
	(void)operand;
	uint32_t stbnk;
	uint32_t cmbnk;
	uint16_t b_reg;
	uint16_t x_reg;
	uint16_t t_reg;
	uint16_t old_head;
	uint16_t marker;

	if (!CheckPriv())
		return;

	stbnk = (uint32_t)(gSTBNK & 0xFF) << 16;
	cmbnk = (uint32_t)(gCMBUK & 0xFF) << 16;
	b_reg = gB;
	x_reg = gX;
	t_reg = gT;

	/* 004454-004457: R1 := old page-list head at STBNK[B+7]. */
	old_head = (uint16_t)ReadPhysicalMemory((int)(stbnk | (uint32_t)((b_reg + 7) & 0xFFFF)), true);
	/* 004460-004461: new head := X. */
	WritePhysicalMemory((int)(stbnk | (uint32_t)((b_reg + 7) & 0xFFFF)), x_reg, true);
	/* 004462-004464: X's forward link (CMBUK[X]) := old head. */
	WritePhysicalMemory((int)(cmbnk | x_reg), old_head, true);

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
		marker = (uint16_t)ReadPhysicalMemory((int)(cmbnk | (uint32_t)((old_head + 1) & 0xFFFF)), true);
		WritePhysicalMemory((int)(cmbnk | (uint32_t)((old_head + 1) & 0xFFFF)), x_reg, true);
	}

	/* 004475-004476 (INSP3): X's back link (CMBUK[X+1]) := marker. */
	WritePhysicalMemory((int)(cmbnk | (uint32_t)((x_reg + 1) & 0xFFFF)), marker, true);
	/* 004477-004501: X's tag word (CMBUK[X+3]) := T. */
	WritePhysicalMemory((int)(cmbnk | (uint32_t)((x_reg + 3) & 0xFFFF)), t_reg, true);
}

/* REMPL - 140503 (privileged)
 *
 * Remove a page's core-map entry from its segment's page list.  Atomic - no loop,
 * normal P+1 return.  The ONLY operand register is X = base word of the page's core-map
 * entry (in CMBUK); the segment and links are recovered from the entry's own words plus
 * the STBNK/STSRT globals.  Forward link at X, back link (or anchor marker) at X+1.
 * A marker (low 2 bits set) means this is the segment's tail.
 *
 * Faithful to RASK microcode REMP1 (ND-110-RASK.LISTING.TXT 10463-10527).
 * Port of RetroCore REMPL().
 */
void ndfunc_rempl(uint16_t operand)
{
	(void)operand;
	uint32_t stbnk;
	uint32_t cmbnk;
	uint16_t x_reg;
	uint16_t r1;		/* successor */
	uint16_t r2;		/* back link / anchor marker */
	bool tail;
	bool skip_inherit;

	if (!CheckPriv())
		return;

	stbnk = (uint32_t)(gSTBNK & 0xFF) << 16;
	cmbnk = (uint32_t)(gCMBUK & 0xFF) << 16;
	x_reg = gX;

	/* 004502-004507: R1 := successor (CMBUK[X]); R2 := back link / anchor marker (CMBUK[X+1]). */
	r1 = (uint16_t)ReadPhysicalMemory((int)(cmbnk | x_reg), true);
	r2 = (uint16_t)ReadPhysicalMemory((int)(cmbnk | (uint32_t)((x_reg + 1) & 0xFFFF)), true);

	tail = ((r2 & 3) != 0);
	if (tail)
	{
		/*
		 * 004514-004520 (REMP2, tail page): the back link is the anchor marker; the
		 * segment head slot is STBNK[(STSRT + 2*marker) | 7] (== B+7).  Set it to the
		 * successor.
		 */
		uint32_t head_off = (uint32_t)(((gSTSRT + 2 * r2) | 7) & 0xFFFF);

		WritePhysicalMemory((int)(stbnk | head_off), r1, true);
		skip_inherit = (r1 == 0);
	}
	else
	{
		/* 004512-004513 (middle page): predecessor.next := successor (executes even if R2==0). */
		WritePhysicalMemory((int)(cmbnk | r2), r1, true);
		skip_inherit = (r2 == 0);
	}

	/* 004521-004523 (REMP3): unless the successor is nil, successor.prev := R2 (predecessor/marker). */
	if (!skip_inherit)
		WritePhysicalMemory((int)(cmbnk | (uint32_t)((r1 + 1) & 0xFFFF)), r2, true);

	/* 004524-004527 (REMP4): zero the removed entry's forward and back links. */
	WritePhysicalMemory((int)(cmbnk | x_reg), 0, true);
	WritePhysicalMemory((int)(cmbnk | (uint32_t)((x_reg + 1) & 0xFFFF)), 0, true);
}

/* CNREK - 140504 (privileged)
 *
 * Faithful to RASK CNRE1 (ND-110-RASK.LISTING.TXT 10545-10581) plus the shared CLNR4
 * clear loop (9498-9537 - the SAME loop validated in CLNREENT).  CNREK reads the segment
 * descriptor at STBNK[A+2] (its value is dead in the clear path), then walks 8 RT-
 * description bitmap words at T[X..X+8) (examined PHYSICALLY in segment T, not through
 * the APT) and, for every set bit, clears the corresponding page-table entry via the
 * alternative page table: base 0174000, stride 2, 16 entries per word LSB-first (R1
 * advances continuously).  Early-out (clean no-op) if A+2 == 0 or X == 0.
 *
 * Only differs from CLNREENT in: clear base (0174000 vs 0177000), first word ([X] vs
 * [X+025]), bound (X+8 vs X+T+1), and the bitmap read path (physical segment T vs APT).
 *
 * Port of RetroCore CNREK().
 */
void ndfunc_cnrek(uint16_t operand)
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

	if (!CheckPriv())
		return;

	a_reg = gA;
	x_reg = gX;
	t_reg = gT;
	stbnk = (uint32_t)(gSTBNK & 0xFF) << 16;
	tseg = (uint32_t)(t_reg & 0xFF) << 16;

	/* 004530-004531: examine the descriptor at STBNK[A+2] (value unused in this path). */
	(void)ReadPhysicalMemory((int)(stbnk | (uint32_t)((a_reg + 2) & 0xFFFF)), true);

	/* 004532: A+2 == 0 -> no-op.  004536/004540: X == 0 -> no-op. */
	if ((uint16_t)(a_reg + 2) == 0)
		return;
	if (x_reg == 0)
		return;

	r1 = 0xF800;			/* 0174000 octal - page-table clear base (APT) */
	r2 = x_reg;			/* first bitmap word */
	r3 = (uint16_t)(x_reg + 8);	/* bound = X + 010 octal (8 words) */

	while (r3 != r2)
	{
		uint16_t word;
		int bit;

		/* 004541: examine the bitmap word physically in segment T. */
		word = (uint16_t)ReadPhysicalMemory((int)(tseg | r2), true);
		r2 = (uint16_t)(r2 + 1);

		if (word == 0)
		{
			r1 = (uint16_t)(r1 + 0x20);	/* all-zero word clears nothing; skip its 16 entries */
			continue;
		}

		for (bit = 0; bit < 16; bit++)
		{
			if ((word & (1 << bit)) != 0)
				WriteVirtualMemory(r1, 0, true, WRITEMODE_WORD);	/* 004155 clear via APT */
			r1 = (uint16_t)(r1 + 2);
		}
	}
}

/* CLPT - 140505 (privileged)
 *
 * Clear (or re-link) a segment's entries from the page tables.  Walks a forward-linked
 * chain of core-map nodes in the core-map bank (CMBUK) from X (a null next-pointer at [X]
 * terminates).  For each node it reads the descriptor at [X+3] and forms the alternative-
 * page-table entry address B = (descriptor | 0176000) << 1.
 *
 * Bit 15 of A selects the mode for the WHOLE instruction:
 *   set   -> clear the entry (APT[B] := 0) WITHOUT saving it
 *   clear -> read APT[B] and, if non-zero, deposit it physically to [X+2] and THEN clear it
 *
 * Faithful to RASK microcode CLPK1/CLPK4/CLPK3 (ND-110-RASK.LISTING.TXT 10585-10704).
 * Port of RetroCore CLPT().
 */
void ndfunc_clpt(uint16_t operand)
{
	(void)operand;
	uint32_t cmbnk;
	bool clear_mode;

	if (!CheckPriv())
		return;

	cmbnk = (uint32_t)(gCMBUK & 0xFF) << 16;			/* segment = core-map bank (LDSEG from CMBNK) */
	clear_mode = ((gA & 0x8000) != 0);			/* 004545/004546: bit 15 of A (constant) */

	/* 004543-004544: X == 0 terminates (normal P+1, no writes). */
	while (gX != 0)
	{
		uint16_t x_reg = gX;
		uint16_t entry;
		uint16_t b_reg;

		/* 004545: examine the segment descriptor at (CMBUK : X+3). */
		entry = (uint16_t)ReadPhysicalMemory((int)(cmbnk | (uint32_t)((x_reg + 3) & 0xFFFF)), true);
		/* 004546: B := (entry | 0176000) << 1. */
		b_reg = (uint16_t)(((entry | 0xFC00) << 1) & 0xFFFF);
		gB = b_reg;

		if (clear_mode)
		{
			/* CLPK4 004554-004555 (bit 15 of A set): clear the page-table entry to 0. */
			WriteVirtualMemory(b_reg, 0, true, WRITEMODE_WORD);
		}
		else
		{
			/* 004550-004553 (bit 15 clear): read APT[B]; if non-zero, deposit it physically to [X+2]. */
			uint16_t r3 = (uint16_t)ReadVirtualMemory(b_reg, true);

			if (r3 != 0)
			{
				WritePhysicalMemory((int)(cmbnk | (uint32_t)((x_reg + 2) & 0xFFFF)), r3, true);

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
				WriteVirtualMemory(b_reg, 0, true, WRITEMODE_WORD);
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
					ring_dump();
			}

			/* DIAG (--trace-nd110): what CLPT read back out of the page table. */
			if (nd110_trace_fp != NULL)
			{
				fprintf(nd110_trace_fp,
					"  CLPT node X=%06o e=%06o -> B=%06o APT[B]=%06o shadow=%d PCR=%06o PONI=%d\n",
					x_reg, entry, b_reg, r3,
					IsAddressShadowMemory(b_reg, false) ? 1 : 0,
					gReg->reg_PCR[CurrLEVEL], STS_PONI ? 1 : 0);
				fflush(nd110_trace_fp);
			}
		}

		/* 004577-004600: advance X := [X] (forward link, physical CMBUK segment). */
		gX = (uint16_t)ReadPhysicalMemory((int)(cmbnk | x_reg), true);
	}
}

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
void nd110_enter_page_table(uint16_t r4_mask)
{
	uint32_t cmbnk = (uint32_t)(gCMBUK & 0xFF) << 16;	/* segment = core-map bank (LDSEG from CMBNK) */

	/* 004561-004562: X == 0 terminates (nothing entered). */
	while (gX != 0)
	{
		uint16_t x_reg = gX;
		uint16_t word0;
		uint16_t word1;
		uint16_t b_reg;

		/* 004563-004564: descriptor word0 at [X+2] (physical, CMBUK segment); A := word0 & mask. */
		word0 = (uint16_t)ReadPhysicalMemory((int)(cmbnk | (uint32_t)((x_reg + 2) & 0xFFFF)), true);
		gA = (uint16_t)(word0 & r4_mask);

		/* 004566: descriptor word1 at [X+3].  004571: B register := (word1 | 0176000) << 1. */
		word1 = (uint16_t)ReadPhysicalMemory((int)(cmbnk | (uint32_t)((x_reg + 3) & 0xFFFF)), true);
		b_reg = (uint16_t)(((word1 | 0xFC00) << 1) & 0xFFFF);
		gB = b_reg;

		/* 004573: APT[B] := A (masked word0).  004575: APT[B+1] := X >> 2 (physical page frame). */
		WriteVirtualMemory(b_reg, gA, true, WRITEMODE_WORD);
		WriteVirtualMemory((uint16_t)((b_reg + 1) & 0xFFFF), (uint16_t)(x_reg >> 2), true, WRITEMODE_WORD);

		/* DIAG (--trace-nd110): per-node dump of the page-table entry actually written. */
		if (nd110_trace_fp != NULL)
		{
			fprintf(nd110_trace_fp,
				"  ENPT node X=%06o w0=%06o w1=%06o -> B=%06o APT[B]=%06o APT[B+1]=%06o shadow=%d PCR=%06o PONI=%d\n",
				x_reg, word0, word1, b_reg, gA, (uint16_t)(x_reg >> 2),
				IsAddressShadowMemory(b_reg, false) ? 1 : 0,
				gReg->reg_PCR[CurrLEVEL], STS_PONI ? 1 : 0);
			fflush(nd110_trace_fp);
		}

		/* 004577-004600: advance X := [X] (forward link, physical CMBUK segment). */
		gX = (uint16_t)ReadPhysicalMemory((int)(cmbnk | x_reg), true);
	}
}

/* ENPT - 140506 (privileged)
 *
 * Enter a segment's pages into the page tables.  Faithful to RASK ENPK1/REPK2
 * (ND-110-RASK.LISTING.TXT 10634-10704).  Port of RetroCore ENPT().
 */
void ndfunc_enpt(uint16_t operand)
{
	(void)operand;
	if (!CheckPriv())
		return;

	nd110_enter_page_table(0xF7FF);		/* R4 = 0173777 octal - clears bit 11 */
}

/* REPT - 140507 (privileged)
 *
 * Enter a REENTRANT segment's pages into the page tables.  Identical to ENPT except it
 * masks BOTH bit 15 and bit 11 out of the descriptor word - marking the entered pages
 * reentrant.  Faithful to RASK REPK1/REPK2 (ND-110-RASK.LISTING.TXT 10640-10704).
 * Port of RetroCore REPT().
 */
void ndfunc_rept(uint16_t operand)
{
	(void)operand;
	if (!CheckPriv())
		return;

	nd110_enter_page_table(0x77FF);		/* R4 = 073777 octal - clears bits 15 and 11 */
}

/* LBIT - 140510 (privileged)
 *
 * Load the single-bit accumulator K with a bit from LOGICAL memory.
 *   X = start of the bit array (word address), A = bit index within the array.
 * The word read is X + (A >> 4) and the selected bit is A & 0xF (bit 0 = LSB).
 * Logical access uses the alternative page table, like the rest of the S3SEG group.
 *
 * Port of RetroCore LBIT().
 */
void ndfunc_lbit(uint16_t operand)
{
	(void)operand;
	uint32_t bit_index;
	uint32_t word_addr;
	int bit_in_word;
	uint16_t word;

	if (!CheckPriv())
		return;

	bit_index = gA;
	word_addr = (uint32_t)((gX + (bit_index >> 4)) & 0xFFFF);
	bit_in_word = (int)(bit_index & 0x0F);
	word = (uint16_t)ReadVirtualMemory(word_addr, true);
	setbit(_STS, _K, (char)((word >> bit_in_word) & 1));
}

/* LBITP - 140511 (privileged)
 *
 * The PHYSICAL variant of LBIT: T = bank number, X = bit-array start word (offset within
 * the bank), A = bit index.  The physical word is (T & 0xFF) << 16 | ((X + (A >> 4)) &
 * 0xFFFF) and the selected bit is A & 0xF.
 *
 * Port of RetroCore LBITP().
 */
void ndfunc_lbitp(uint16_t operand)
{
	(void)operand;
	uint32_t bit_index;
	uint32_t bank;
	uint32_t word_offset;
	uint32_t phys_addr;
	int bit_in_word;
	uint16_t word;

	if (!CheckPriv())
		return;

	bit_index = gA;
	bank = (uint32_t)(gT & 0xFF);
	word_offset = (uint32_t)((gX + (bit_index >> 4)) & 0xFFFF);
	phys_addr = (bank << 16) | word_offset;
	bit_in_word = (int)(bit_index & 0x0F);
	word = (uint16_t)ReadPhysicalMemory((int)phys_addr, true);
	setbit(_STS, _K, (char)((word >> bit_in_word) & 1));
}

/* SBIT - 140512 (privileged)
 *
 * Store the single-bit accumulator K into a bit in LOGICAL memory.  X = bit-array start
 * word, A = bit index; target word X + (A >> 4), bit A & 0xF.  Read-modify-write via the
 * alternative page table.
 *
 * Port of RetroCore SBIT().
 */
void ndfunc_sbit(uint16_t operand)
{
	(void)operand;
	uint32_t bit_index;
	uint32_t word_addr;
	int bit_in_word;
	uint16_t word;

	if (!CheckPriv())
		return;

	bit_index = gA;
	word_addr = (uint32_t)((gX + (bit_index >> 4)) & 0xFFFF);
	bit_in_word = (int)(bit_index & 0x0F);
	word = (uint16_t)ReadVirtualMemory(word_addr, true);
	if (STS_K)
		word |= (uint16_t)(1 << bit_in_word);
	else
		word &= (uint16_t)(~(1 << bit_in_word));
	WriteVirtualMemory(word_addr, word, true, WRITEMODE_WORD);
}

/* SBITP - 140513 (privileged)
 *
 * The PHYSICAL variant of SBIT: T = bank, X = bit-array start word, A = bit index;
 * read-modify-write of the word at (T & 0xFF) << 16 | ((X + (A >> 4)) & 0xFFFF).
 *
 * Port of RetroCore SBITP().
 */
void ndfunc_sbitp(uint16_t operand)
{
	(void)operand;
	uint32_t bit_index;
	uint32_t bank;
	uint32_t word_offset;
	uint32_t phys_addr;
	int bit_in_word;
	uint16_t word;

	if (!CheckPriv())
		return;

	bit_index = gA;
	bank = (uint32_t)(gT & 0xFF);
	word_offset = (uint32_t)((gX + (bit_index >> 4)) & 0xFFFF);
	phys_addr = (bank << 16) | word_offset;
	bit_in_word = (int)(bit_index & 0x0F);
	word = (uint16_t)ReadPhysicalMemory((int)phys_addr, true);
	if (STS_K)
		word |= (uint16_t)(1 << bit_in_word);
	else
		word &= (uint16_t)(~(1 << bit_in_word));
	WritePhysicalMemory((int)phys_addr, word, true);
}

/* LBYTP - 140514 (privileged)
 *
 * The PHYSICAL variant of LBYT: D = bank number, T = byte-array start word, X = byte index.
 * The physical word is (D & 0xFF) << 16 | ((T + (X >> 1)) & 0xFFFF); an EVEN X selects the
 * high (MSB) byte, an ODD X the low (LSB) byte - ND big-endian order, as in LBYT.
 *
 * Port of RetroCore LBYTP().
 */
void ndfunc_lbytp(uint16_t operand)
{
	(void)operand;
	uint32_t bank;
	uint32_t word_offset;
	uint32_t phys_addr;
	uint16_t memval;

	if (!CheckPriv())
		return;

	bank = (uint32_t)(gD & 0xFF);
	word_offset = (uint32_t)((gT + (gX >> 1)) & 0xFFFF);
	phys_addr = (bank << 16) | word_offset;
	memval = (uint16_t)ReadPhysicalMemory((int)phys_addr, true);
	if ((gX & 1) != 0)
		gA = (uint16_t)(memval & 0xFF);		/* odd byte  -> low  */
	else
		gA = (uint16_t)((memval >> 8) & 0xFF);	/* even byte -> high */
}

/* SBYTP - 140515 (privileged)
 *
 * The PHYSICAL variant of SBYT: D = bank, T = byte-array start word, X = byte index.
 * Read-modify-write of the word at (D & 0xFF) << 16 | ((T + (X >> 1)) & 0xFFFF);
 * even X = high byte, odd X = low byte.
 *
 * Port of RetroCore SBYTP().
 */
void ndfunc_sbytp(uint16_t operand)
{
	(void)operand;
	uint32_t bank;
	uint32_t word_offset;
	uint32_t phys_addr;
	uint16_t memval;
	unsigned char b;

	if (!CheckPriv())
		return;

	bank = (uint32_t)(gD & 0xFF);
	word_offset = (uint32_t)((gT + (gX >> 1)) & 0xFFFF);
	phys_addr = (bank << 16) | word_offset;
	memval = (uint16_t)ReadPhysicalMemory((int)phys_addr, true);
	b = (unsigned char)(gA & 0xFF);
	if ((gX & 1) != 0)
		memval = (uint16_t)((memval & 0xFF00) | b);		/* odd byte  -> low  */
	else
		memval = (uint16_t)((memval & 0x00FF) | (b << 8));	/* even byte -> high */
	WritePhysicalMemory((int)phys_addr, memval, true);
}

/* TSETP - 140516 (privileged)
 *
 * Atomically read a PHYSICAL memory word into A and write all-ones (0xFFFF) back - the
 * physical test-and-set used for multi-processor synchronisation.  T = bank, X = address
 * within the bank.  The read is always from memory and the write never reaches cache.
 * Unlike the logical TSET there is NO page-table side effect (physical access bypasses
 * paging).
 *
 * Port of RetroCore TSETP().
 */
void ndfunc_tsetp(uint16_t operand)
{
	(void)operand;
	uint32_t bank;
	uint32_t offset;
	uint32_t phys_addr;

	if (!CheckPriv())
		return;

	bank = (uint32_t)(gT & 0xFF);
	offset = (uint32_t)(gX & 0xFFFF);
	phys_addr = (bank << 16) | offset;
	gA = (uint16_t)ReadPhysicalMemory((int)phys_addr, true);
	WritePhysicalMemory((int)phys_addr, 0xFFFF, true);
}

/* RDUSP - 140517 (privileged)
 *
 * The PHYSICAL variant of RDUS: load A with the word at (T & 0xFF) << 16 | (X & 0xFFFF),
 * always from memory, never cache (cache is not modelled).
 *
 * Port of RetroCore RDUSP().
 */
void ndfunc_rdusp(uint16_t operand)
{
	(void)operand;
	uint32_t bank;
	uint32_t offset;

	if (!CheckPriv())
		return;

	bank = (uint32_t)(gT & 0xFF);
	offset = (uint32_t)(gX & 0xFFFF);
	gA = (uint16_t)ReadPhysicalMemory((int)((bank << 16) | offset), true);
}

/*
 * ---------------------------------------------------------------------------
 *  The 14070x "bank group": direct physical access to the segment table (STBNK,
 *  indexed by B) and to the core map (CMBUK, indexed by X - the physical page
 *  number, NOT B; that asymmetry is oracle-verified, see RetroCore
 *  BankGroupPhysAddr).  Opcode = 14070x + (delta << 3), delta = 3-bit displacement.
 * ---------------------------------------------------------------------------
 */

/* LASB - 140700 + (delta << 3) (privileged): A := STBNK[B + delta]. */
void ndfunc_lasb(uint16_t operand)
{
	if (!CheckPriv())
		return;

	gA = (uint16_t)ReadPhysicalMemory((int)nd110_bankgroup_phys(gSTBNK, gB, operand), true);
}

/* SASB - 140701 + (delta << 3) (privileged): STBNK[B + delta] := A. */
void ndfunc_sasb(uint16_t operand)
{
	if (!CheckPriv())
		return;

	WritePhysicalMemory((int)nd110_bankgroup_phys(gSTBNK, gB, operand), gA, true);
}

/* LACB - 140702 + (delta << 3) (privileged): A := CMBUK[X + delta]. */
void ndfunc_lacb(uint16_t operand)
{
	if (!CheckPriv())
		return;

	gA = (uint16_t)ReadPhysicalMemory((int)nd110_bankgroup_phys(gCMBUK, gX, operand), true);
}

/* SACB - 140703 + (delta << 3) (privileged): CMBUK[X + delta] := A. */
void ndfunc_sacb(uint16_t operand)
{
	if (!CheckPriv())
		return;

	WritePhysicalMemory((int)nd110_bankgroup_phys(gCMBUK, gX, operand), gA, true);
}

/* LXSB - 140704 + (delta << 3) (privileged): X := STBNK[B + delta]. */
void ndfunc_lxsb(uint16_t operand)
{
	if (!CheckPriv())
		return;

	gX = (uint16_t)ReadPhysicalMemory((int)nd110_bankgroup_phys(gSTBNK, gB, operand), true);
}

/* LXCB - 140705 + (delta << 3) (privileged): X := CMBUK[X + delta]. */
void ndfunc_lxcb(uint16_t operand)
{
	if (!CheckPriv())
		return;

	gX = (uint16_t)ReadPhysicalMemory((int)nd110_bankgroup_phys(gCMBUK, gX, operand), true);
}

/* SZSB - 140706 + (delta << 3) (privileged): STBNK[B + delta] := 0. */
void ndfunc_szsb(uint16_t operand)
{
	if (!CheckPriv())
		return;

	WritePhysicalMemory((int)nd110_bankgroup_phys(gSTBNK, gB, operand), 0, true);
}

/* SZCB - 140707 + (delta << 3) (privileged): CMBUK[X + delta] := 0. */
void ndfunc_szcb(uint16_t operand)
{
	if (!CheckPriv())
		return;

	WritePhysicalMemory((int)nd110_bankgroup_phys(gCMBUK, gX, operand), 0, true);
}


/********************* STACK INSTRUCTIONS *********************/



/* INIT
 * INIT instruction:
 * IN: nothing. uses PC.
 * ADDR  : INIT
 * ADDR+1: Stack demand
 * ADDR+2: Address of stack start
 * ADDR+3: Maximum stack size
 * ADDR+4: Flag
 * ADDR+5: Not used
 * ADDR+6: Error return
 * ADDR+7: Normal return
 *
 */
/*
 * Stack instructions (INIT / ENTR / LEAVE / ELEAV) and the page table they use.
 *
 * Verified against the ND-110 RASK microcode source (ND-110-RASK.uc):
 *
 *   5INIT (001144)  reads its inline parameters at P+1.. with COMM,RDRQ,PT
 *   ENTR  (001162)  frame reads                        COMM,RDRQ,APT
 *   ENTRC (001174)  frame writes                       COMM,WRRQ,APT
 *                   BUT the argument read at "A,P"     COMM,RDRQ,PT
 *   ENTRB (001206)  frame writes                       COMM,WRRQ,APT
 *   ELEAV (001213)  frame read + writes                COMM,RDRQ/WRRQ,APT
 *   LEAV  (001224)  frame reads                        COMM,RDRQ,APT
 *
 * The rule the microcode follows, without exception:
 *   - the stack FRAME (LINK/PREVB/STP/SMAX/ERRCODE) is DATA  -> APT
 *   - the inline PARAMETERS live in the instruction stream    -> PT
 * (5INIT jumps into the shared ENTRC/ENTRB code, which is why INIT reads its
 *  parameters via PT yet builds the frame via APT.)
 *
 * APT is gated on PTM by the MMU itself - mapVirtualToPhysical() does
 * "if ((STS_PTM) && (UseAPT))" (cpu_mms.c) - so passing UseAPT=1 here means
 * "alternative table when PTM is set, standard table otherwise", exactly like
 * the hardware. No PTM test belongs in these functions.
 *
 * Reading the frame with UseAPT=0 while PTM=1 (split I/D space) fetches a word
 * from the CODE page where SMAX should be, producing a bogus stack overflow.
 */
void ndfunc_init(uint16_t operand)
{
	(void)operand;
	uint16_t demand, start, maxsize, flag;

	demand = MemoryRead(gPC + 0, 0);
	start = MemoryRead(gPC + 1, 0);
	maxsize = MemoryRead(gPC + 2, 0);
	flag = MemoryRead(gPC + 3, 0);
	if ((start + 128 + demand - 122) > (start + maxsize))
	{ /* stack overflow */
		gPC += 5;
		return;
	}
	if ((flag & 0x01) != (gReg->reg[gPIL][_STS] & 0x01))
	{
		gPC += 5;
		return;
	}
	MemoryWrite(gL + 1, start, 1, 2); /* L+1 ==> LINK */
	MemoryWrite(gB, start + 1, 1, 2); /* B   ==> PREVB */
	MemoryWrite(start + maxsize, start + 3, 1, 2); /* SMAX */
	gB = start + 128; /* + 200 oct. */
	/*:TODO:  Flag */
	MemoryWrite(gB + demand - 122, start + 2, 1, 2); /* STP */
	gPC += 6;
	return;
}

/* ENTR
 * IN: nothing. uses PC.
 * ADDR  : ENTR
 * ADDR+1: Stack demand
 * ADDR+2: Error return
 * ADDR+3: Normal return
 *
 */
void ndfunc_entr(uint16_t operand)
{
	(void)operand;
	uint16_t oldB, demand, smax, stp;
	demand = MemoryRead(gPC + 0, 0);
	smax = MemoryRead(gB - 125, 1); /* SMAX */
	if ((gB + demand - 122) > (smax))
	{ /* stack overflow */
		gPC += 1;
		return;
	}
	stp = MemoryRead(gB - 126, 1); /* STP */
	oldB = gB;
	gB = stp + 128;									/* Advance stack frame */
	MemoryWrite(gL + 1, gB - 128, 1, 2);			/* L+1 ==> LINK */
	MemoryWrite(oldB, gB - 127, 1, 2);				/* B   ==> PREVB */
	MemoryWrite(smax, gB - 125, 1, 2);				/* SMAX */
	MemoryWrite(gB + demand - 122, gB - 126, 1, 2); /* STP */
	gPC += 2;
}

/* LEAVE
 */
void ndfunc_leave(uint16_t operand)
{
	(void)operand;
	gPC = MemoryRead(gB - 128, 1);
	gB = MemoryRead(gB - 127, 1);
}

/* ELEAV
 */
void ndfunc_eleav(uint16_t operand)
{
	(void)operand;
	uint16_t tmp;
	tmp = MemoryRead(gB - 128, 1) - 1;
	MemoryWrite(tmp, gB - 128, 1, 2); /* LINK */
	MemoryWrite(gA, gB - 123, 1, 2);  /* A ==> ERRCODE */
	gPC = MemoryRead(gB - 128, 1);
	gB = MemoryRead(gB - 127, 1);
}


/************************ BYTE ************************/



/// <summary>
/// LBYT Load byte
/// Code: 142200
/// Format: LBYT
///
/// The 8 bit byte specified by the contents of the T and X registers is loaded into the A register bits 0-7, with the A register bits 8-15 cleared.
///
/// Affected: (A)
/// </summary>
void ndfunc_lbyt(uint16_t operand)
{
	(void)operand;

	uint16_t offset = gX >> 1;
	uint16_t memval = MemoryRead(gT + offset, true);

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

/// <summary>
/// SBYT - Store byte
/// Code: 142 600
/// Format: SBYT
///
/// The byte contained in the A register bits 0-7 is stored in one half of the effective location pointed by the T and X registers,
/// the second half of this effective location being unchanged. The contents of the A register are unchanged.
///
/// Affected: (EL)
/// </summary>
void ndfunc_sbyt(uint16_t operand)
{
	(void)operand;

	uint16_t offset = gX >> 1; /* same as divide by 2 */

	if ((gX & 1) != 0)

	{
		// Odd byte, write LSB value
		WriteVirtualMemory((uint32_t)(gT + offset), gA, true, WRITEMODE_LSB);
	}
	else
	{
		// Even byte, write MSB value
		WriteVirtualMemory((uint32_t)(gT + offset), gA, true, WRITEMODE_MSB);
	}
}

/// <summary>
/// MIX3 - Multiply index by 3
///
/// X <- ((A) - 1) *3
///
/// Format: MIX3
///
/// Code: 143 200
///
/// Multiply index by 3
/// The X register is set equal to the contents of the A register minus one multiplied by three, i.e., (X) <- [(A) - 1] *3
///
/// Affected: (X)
/// </summary>
void ndfunc_mix3(uint16_t operand)
{
	(void)operand;
	gX = (uint16_t)((gA - 1) * 3);
}

/*********************** REGISTER OPERANDS ***********************/

// Math register operations
void regop(uint16_t operand)
{ /* SWAP RAND REXO RORA RADD RCLR EXIT RDCR RING RSUB */
	int RAD, CLD, CM1, tmp;
	uint16_t sr, dr, source, destination;
	uint16_t old_gPC = gPC-1;

	RAD = ((operand & 0x0400) >> 10);
	CM1 = ((operand & 0x0080) >> 7);
	CLD = ((operand & 0x0040) >> 6);

	sr = ((operand & 0x0038) >> 3);
	dr = (operand & 0x0007);

	/* Register field 0 = "no register": reading yields 0, writing is DISCARDED. In nd100x reg[0] is
	 * the STS register, so a write to register 0 must be suppressed or it corrupts STS. dr=0 must read
	 * as 0 here too (NOT reg[0]=STS). Oracle-validated against the RASK microcode; see RetroCore commits
	 * 0890b6fbb (SWAP reg-0), 7dbdbe729 (REXO;CM1), 581e7270a (RADD dr=0). */
	source = (sr == 0) ? 0 : gReg->reg[CurrLEVEL][sr] & 0xFFFF;
	destination = (CLD) ? 0 : ((dr == 0) ? 0 : gReg->reg[CurrLEVEL][dr] & 0xFFFF);

	switch (RAD)
	{
	case 0: /* Logical operation - SWAP RAND REXO RORA. NO dr!=0 guard: reg field 0 writes are discarded
	         * (SWAP writes BOTH sr and dr, so dr=0 still writes the source-register half). */
		switch ((operand & 0x0300) >> 8)
		{
		case 0:								/* SWAP: dr <- source (CM1->~source), sr <- old dr (CLD->0) */
		{
			uint16_t old_dr = (dr == 0) ? 0 : (uint16_t)(gReg->reg[CurrLEVEL][dr] & 0xFFFF);
			uint16_t new_dr = (CM1) ? (uint16_t)~source : source;
			uint16_t new_sr = (CLD) ? 0 : old_dr;
			if (dr != 0) gReg->reg[CurrLEVEL][dr] = new_dr;      /* discard write to register 0 (=STS) */
			if (sr != 0) gReg->reg[CurrLEVEL][sr] = new_sr;      /* discard write to register 0 (=STS) */
			break;
		}
		case 1: /* RAND: dr <- dest & (CM1?~src:src) */
			if (dr != 0) gReg->reg[CurrLEVEL][dr] = (uint16_t)(destination & ((CM1) ? (uint16_t)~source : source));
			break;
		case 2: /* REXO: plain = dest ^ src; but CM1 is OR-of-complement (dest | ~src), NOT XOR - the RASK
		         * REXO;CM1;CLD=0 routes through REX02 (ALUF,ORAB). CLD (dest=0) yields ~src / src for free. */
			if (dr != 0)
				gReg->reg[CurrLEVEL][dr] = (CM1) ? (uint16_t)(destination | (uint16_t)~source)
				                                : (uint16_t)(destination ^ source);
			break;
		case 3: /* RORA: dr <- dest | (CM1?~src:src) */
			if (dr != 0) gReg->reg[CurrLEVEL][dr] = (uint16_t)(destination | ((CM1) ? (uint16_t)~source : source));
			break;
		}
		break;
	case 1: /* Arithmetic - RADD/RSUB. RASK has NO dr==0 special case: run do_add (which sets C/O/Q) on
	         * EVERY path and only discard the register write for dr=0. The manual's "dr=0 resets carry,
	         * else no-op" is WRONG for the ND-110 silicon (oracle-confirmed). */
		tmp = (dr == 0) ? 0 : gReg->reg[CurrLEVEL][dr]; /* NOOP-variant fallthrough value (unchanged dr) */
		switch ((operand & 0x0380) >> 7)
		{
		case 0: tmp = do_add(destination, source, 0); break;                 /* RADD */
		case 1: tmp = do_add(destination, ~source, 0); break;                /* RADD CM1 */
		case 2: tmp = do_add(destination, source, 1); break;                 /* RADD AD1 */
		case 3: tmp = do_add(destination, ~source, 1); break;                /* RADD AD1 CM1 */
		case 4: tmp = do_add(destination, source, getbit(_STS, _C)); break;  /* RADD ADC */
		case 5: tmp = do_add(destination, ~source, getbit(_STS, _C)); break; /* RADD ADC CM1 */
		case 6: /* NOOP */
			break;
		case 7: /* NOOP */
			break;
		}
		if (dr != 0) gReg->reg[CurrLEVEL][dr] = (uint16_t)(tmp & 0xFFFF); /* discard write to register 0 (=STS) */
		break;
	}

	if ((DISASM) && (dr == _P))
	{
		disasm_userel(old_gPC, gPC);
	}
}


/********************* some */


/*
 * DoMCL - Masked Clear
 *  Affected: Internal register specified
 *  (Only STS, PID & PIE possible)
 *  <IR> = <IR> & (~A)
 *
 * NOTE:: STS need to be checked.
 * NOTE:: Privileged instructions
 */
void DoMCL(uint16_t instr)
{
	if (!CheckPriv())
		return;

	switch (instr & 0x0F)
	{
	case 01: // STS
		gReg->reg[CurrLEVEL][_STS] &= ~(gA & 0x00FF);
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
 * DoMST - Masked SET
 *  Affected: Internal register specified
 *  (Only STS, PID & PIE possible)
 *  <IR> = <IR> | (A)
 *
 * NOTE:: STS need to be checked.
 * NOTE:: Privileged instructions
 */
void DoMST(uint16_t instr)
{
	if (!CheckPriv())
		return;

	switch (instr & 0x0F)
	{
	case 01: // STS
		gReg->reg[CurrLEVEL][0] |= (gA & 0x00ff);
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
 * DoTRA - Transfer to register
 *  Affected: Accumulator
 *  A = <IR>;
 *
 * NOTE: Privileged instructions
 */
void DoTRA(uint16_t instr)
{
	if (!CheckPriv())
		return;

	uint16_t temp, level;
	switch (instr & 0x0F)
	{
	case 00: /* TRA PANS */
		gA = gPANS;
		break;
	case 01:								 /* TRA STS */
		gA = gReg->reg[gPIL][_STS] & 0x00FF; /* Only lower 8 bits */
		gA |= gReg->reg_STS & 0xFF00;		 /* Upper 8 bits - SYSTEM bits*/

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
		gA = 0;							  /* Clean it */
		gA = (gPVL & 0x0F) << 3 | 0xd782; /* = IRR (PVL) DP */
		break;
	case 05: /* TRA IIC */
		/* Manuals says(2.2.4.3) that this should be a number equal to the highest bit set in (IID & IIE) - Roger */
		/* Only bit 1-10 is used, so we only return a value between 1 and 10  or else  zero */

		gIIC = calcIIC();

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
	case 010:					  // CSR
		gA = (1 << 2) | (1 << 3); // Always report bit 2 and 3 as 1. Bit 2="MAN DIS" (Cache disabled manually as Emulator doesnt need caching. Bit 3=Cache Clear Finished
		// gA = gCSR;
		break;
	case 011: /* TRA ACTL */
		gA = 1 << CurrLEVEL;
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
		gA = gReg->reg_PCR[level];
		if (mmsType == MMS1)
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
			 // gA = 0;
		//  do nothing is the correct
		break;
	}
}

/*
 * DoEXR - Run instruction in source register
 */
void DoEXR(uint16_t instr)
{
	uint16_t sr, exr_instr;
	sr = (instr >> 3) & 0x07;
	if (sr)
		exr_instr = gReg->reg[CurrLEVEL][sr];
	else
		exr_instr = 0;

	if (0140600 == extract_opcode(exr_instr))
	{						 /* ILLEGAL:: EXR of EXR */
		setbit(_STS, _Z, 1); //: TODO: activate CPU trap on level 14!!!
		return;
	}
	if (DISASM)
		disasm_exr(gPC, exr_instr);

	// Execute opcode but do not touch Program Counter
	do_op(exr_instr, true);
}

/*
 * DoWAIT - Give up prio instruction
 * NOTE:: Only basic parts fixed yet, this is a fairly complex one
 *
 * NOTE:: Privileged instructions
 */
void DoWAIT(uint16_t instr)
{
	(void)instr;
	if (!CheckPriv())
		return;

	uint16_t temp;
	if (!STS_IONI)
	{
		// If the interrupt system is OFF
		// The ND-110 stops with the program counter (P register) pointing at the instruction after the WAIT and the front panel RUN indicator is turned off.
		// To restart the system, type ! on the console terminal
		printf("\r\nWAIT when IONI is off PIL[%d] PC[%6o] PID[0x%4X] PIE[0x%4X] IONI[%d] PONI[%d] STS_HI[%4X] STS_LO[%4X] A[%6o]\r\n", gPIL, gPC, gPID, gPIE, STS_IONI, STS_PONI, gReg->reg_STS, gReg->reg[gPIL][_STS], gA);
		gCpuExitCode = (int)(short)gA;
		set_cpu_run_mode(CPU_STOPPED);
		return;
	}

	if (CurrLEVEL == 0)
	{
		// Cant go lower
		return;
	}


	temp = ~(1 << CurrLEVEL); /* Now we have a 0 in the position we want */
	gPID &= temp;			  /* Give up this level */

	gCHKIT = true; // recalc PK (and do a level switch if needed)
}

/* HALT (emulator extension)
 * Opcode 0140200 (USER1 slot 0)
 * Unconditionally stops the emulator.
 * A register = process exit code.
 */
void ndfunc_halt(uint16_t operand)
{
	(void)operand;
	printf("\r\nHALT opcode at PIL[%d] PC[%6o] A[%6o]\r\n", gPIL, gPC, gA);
	gCpuExitCode = (int)(short)gA;
	set_cpu_run_mode(CPU_STOPPED);
}

/* LWCS (Privileged)
 */
void ndfunc_lwcs(uint16_t instr)
{
	(void)instr;
	// LWCS is a no-operation on the ND-110
	// The ND-110 is software compatible but nor microcode compatible and writing to the writable control store has no meaning in the ND-110.
	// A no-operation is executed so that programs written for the ND-100 and NORD-10 can continue

	if (!CheckPriv())
		return;

	// noop
}


//TODO: Make these into callbacks
extern void ProcessTerminalPanc(void);
extern void ProcessTerminalLamp(void);

/*
 * DoTRR - Transfer to register
 *  Affected: Internal register specified
 *  <IR> = A;
 *
 * NOTE: STS and PCR NOT fixed yet!!!
 *
 * NOTE: Privileged instructions
 */
void DoTRR(uint16_t instr)
{
	if (!CheckPriv())
		return;

	uint16_t temp, level;
	switch (instr & 0x0F)
	{
	case 00: // TRR PANC
		gPANC = gA;
		ProcessTerminalPanc();

		break;
	case 01: // TRR STS
		/* ND-06.029.1 ND-110 Instruction Set, lists only lower 8 bits as changeable... */
		gReg->reg[CurrLEVEL][_STS] = (gReg->reg[CurrLEVEL][_STS] & 0xff00) | (gA & 0x00ff); /* Only change LSB  */
		break;
	case 02: // TRR LMP
		gLMP = gA;
		ProcessTerminalLamp();

		break;
	case 03: /* PGC/PCR - Paging Control Register */
		temp = gA;
		level = (temp >> 3) & 0x0f;
		if (mmsType == MMS1)
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
		gReg->reg_PCR[level] = temp;

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
	}
}


/*
 * DoSRB - Store register block.
 *  Affected:(EL),+ 1 +2 + 3 + 4 + 5 + 6 + 7
 *            P    X  T   A   D   L  STS  B
 *
 *  Uses the alternative pagetable!
 */
void DoSRB(uint16_t operand)
{

	if (!CheckPriv())
		return;

	uint16_t lvl, addr;
	uint16_t sts_temp;

	lvl = ((operand & 0x0078) >> 3);
	addr = gX;

    sts_temp = gReg->reg[lvl][_STS] & 0x00ff;

	// If the current program level is specified, the stored P register points to the instruction following SRB.
	MemoryWrite(gReg->reg[lvl][_P], addr, true, 2);
	MemoryWrite(gReg->reg[lvl][_X], addr + 1, true, 2);
	MemoryWrite(gReg->reg[lvl][_T], addr + 2, true, 2);
	MemoryWrite(gReg->reg[lvl][_A], addr + 3, true, 2);
	MemoryWrite(gReg->reg[lvl][_D], addr + 4, true, 2);
	MemoryWrite(gReg->reg[lvl][_L], addr + 5, true, 2);
	MemoryWrite(sts_temp, addr + 6, true, 2); /* Only write LSB of STS */
	MemoryWrite(gReg->reg[lvl][_B], addr + 7, true, 2);
}

/*
 * DoLRB - Load register block.
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
void DoLRB(uint16_t operand)
{

	if (!CheckPriv())
		return;

	uint16_t lvl, addr;

	lvl = ((operand & 0x0078) >> 3);
	addr = gX;


	if (lvl != CurrLEVEL)
	{ /* Dont change P on current level if this happens to be specified */
		gReg->reg[lvl][_P] = MemoryRead(addr, true);
	}
	gReg->reg[lvl][_X] = MemoryRead(addr + 1, true);
	gReg->reg[lvl][_T] = MemoryRead(addr + 2, true);
	gReg->reg[lvl][_A] = MemoryRead(addr + 3, true);
	gReg->reg[lvl][_D] = MemoryRead(addr + 4, true);
	gReg->reg[lvl][_L] = MemoryRead(addr + 5, true);
	gReg->reg[lvl][_STS] = (gReg->reg[lvl][_STS] & 0xff00) | (MemoryRead(addr + 6, true) & 0x00ff); /* Only load LSB STS */
	gReg->reg[lvl][_B] = MemoryRead(addr + 7, true);

}

bool IsSkip(uint16_t instr)
{
	uint16_t sr, dr, source, desti;
	signed short ss, sd, sgr, ovf;
	char z, o, c, s;
	sr = (instr >> 3) & 0x07;
	dr = (instr >> 0) & 0x07;
	source = (0 == sr) ? 0 : gReg->reg[CurrLEVEL][sr]; /* Never use STS reg but zero value instead */
	desti = (0 == dr) ? 0 : gReg->reg[CurrLEVEL][dr];  /* Never use STS reg but zero value instead */
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
			return true;
		break;
	case 1: /* GEQ */
		if (!s)
			return true;
		break;
	case 2: /* GRE */
		if (!(s ^ o))
			return true;
		break;
	case 3: /* MGRE */
		if (c)
			return true;
		break;
	case 4: /* UEQ */
		if (!z)
			return true;
		break;
	case 5: /* LSS */
		if (s)
			return true;
		break;
	case 6: /* LST */
		if (s ^ o)
			return true;
		break;
	case 7: /* MLST */
		if (!c)
			return true;
		break;
	}
	return false;
}

void do_bops(uint16_t operand)
{
	uint16_t bn, dr, desti;
	bn = ((operand & 0x0078) >> 3);
	dr = (operand & 0x0007);

    switch ((operand & 0x0780) >> 7)
	{
	case 0: /* BSET ZRO */
		setbit(dr, bn, 0);
		break;
	case 1: /* BSET ONE */
		setbit(dr, bn, 1);
		break;
	case 2: /* BSET BCM */
		desti = getbit(dr, bn);
		desti ^= 1; /* XOR with one to invert bit */
		setbit(dr, bn, desti);
		break;
	case 3: /* BSET BAC */
		setbit(dr, bn, getbit(_STS, _K));
		break;
	case 4: /* BSKP ZRO */
		if (!getbit(dr, bn))
			gPC++; /* Skip next instruction if zero */
		break;
	case 5: /* BSKP ONE */
		if (getbit(dr, bn))
			gPC++; /* Skip next instruction if one */
		break;
	case 6: /* BSKP BCM */
		if ((getbit(dr, bn) ^ 1) == getbit(_STS, _K))
			gPC++; /* Skip next instruction if bit complement */
		break;
	case 7: /* BSKP BAC */
		if (getbit(dr, bn) == getbit(_STS, _K))
			gPC++; /* Skip next instruction if equal */
		break;
	case 8: /* BSTC */
		setbit(dr, bn, (getbit(_STS, _K) ^ 1));
		setbit(_STS, _K, 1);
		break;
	case 9: /* BSTA */
		setbit(dr, bn, getbit(_STS, _K));
		setbit(_STS, _K, 0);
		break;
	case 10: /* BLDC */
		setbit(_STS, _K, getbit(dr, bn) ^ 1);
		break;
	case 11: /* BLDA */
		setbit(_STS, _K, getbit(dr, bn));
		break;
	case 12: /* BANC */
		setbit(_STS, _K, ((getbit(dr, bn) ^ 1) & getbit(_STS, _K)));
		break;
	case 13: /* BAND */
		setbit(_STS, _K, (getbit(dr, bn) & getbit(_STS, _K)));
		break;
	case 14: /* BORC */
		setbit(_STS, _K, ((getbit(dr, bn) ^ 1) | getbit(_STS, _K)));
		break;
	case 15: /* BORA */
		setbit(_STS, _K, (getbit(dr, bn) | getbit(_STS, _K)));
		break;
	}
}

uint16_t ShiftReg(uint16_t reg, uint16_t instr)
{
	bool isneg = ((instr & 0x0020) >> 5) ? 1 : 0;
	/* Right-shift count is the two's complement of the 6-bit field, but the hardware shift counter is
	 * only 5 BITS, so it wraps mod 32: field 040 octal (= 32) loads as 0 -> NO shift (register unchanged,
	 * M preserved). Oracle-validated (RetroCore CpuND100.Fetch, commit 135a2ff28). Fields 041..077
	 * (counts 31..1) already fit and are unaffected. M-on-count-0 is already correct here (tmp inits to M). */
	uint16_t offset = (isneg) ? (uint16_t)((~((instr & 0x003F) | 0xFFC0) + 1) & 0x1F) : (instr & 0x003F);
	uint16_t shifttype = ((instr >> 9) & 0x03);
	int i, tmp, msb;
	int m = getbit(_STS, _M);
	tmp = m; /* just in case.. */
	for (i = 1; i <= offset; i++)
	{
		tmp = (isneg) ? (reg & 0x01) : ((reg >> 15) & 0x01); /* tmp = bit shifted out */
		msb = reg >> 15 & 1;								 /* msb before shift */
		reg = (isneg) ? reg >> 1 : reg << 1;
		switch (shifttype)
		{
		case 0:																 /* Plain */
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
		}
	}
	setbit(_STS, _M, tmp);
	return reg;
}

/* The A:D register pair is exactly 32 bits; do the arithmetic in uint32_t.
 * (It used ulong, 64-bit native and 32-bit on wasm, and shifted an int
 * into bit 31, which is undefined behaviour.) */
uint32_t ShiftDoubleReg(uint32_t reg, uint16_t instr)
{
	bool isneg = ((instr & 0x0020) >> 5) ? 1 : 0;
	/* 5-bit shift-counter wrap: field 040 octal (=32) -> 0 = NO shift (SAD register pair unchanged, M
	 * preserved). Oracle-validated (RetroCore 135a2ff28). See ShiftReg for the full note. */
	uint16_t offset = (isneg) ? (uint16_t)((~((instr & 0x003F) | 0xFFC0) + 1) & 0x1F) : (instr & 0x003F);
	uint16_t shifttype = ((instr >> 9) & 0x03);
	int i;
	uint32_t tmp, msb;
	uint32_t m = (uint32_t)getbit(_STS, _M);
	tmp = m; /* just in case.. */
	for (i = 1; i <= offset; i++)
	{
		tmp = (isneg) ? (reg & 0x01) : ((reg >> 31) & 0x01); /* tmp = bit shifted out */
		msb = reg >> 31 & 1;								 /* msb before shift */
		reg = (isneg) ? reg >> 1 : reg << 1;
		switch (shifttype)
		{
		case 0:																		 /* Plain */
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
		}
	}
	setbit(_STS, _M, (char)tmp);
	return reg;
}

/*
 * DoIDENT
 * Handles IDENT PLxx instructions
 */
void DoIDENT(uint16_t priolevel)
{

	int id = IO_Ident(priolevel);

	// IDENT is the ND-100 interrupt ACKNOWLEDGE for this level. IO_Ident /
	// Terminal_Ident already clears the identified device's own request, and the
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

		if (priolevel != 13)	   // ignore RTC
			interrupt(14, 1 << 7); /* IOX Error if no IDENT code found */

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

void DoRDUS(uint16_t instr)
{
	(void)instr;
	gA = MemoryRead(gT, true);
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
void DoTSET(uint16_t instr)
{
	(void)instr;
	// regs.currentRegisters.A = (ushort)cpu.ReadVirtualMemory(regs.currentRegisters.T, PageTable.AlternativePageTable);
	// cpu.WriteVirtualMemory(regs.currentRegisters.T, 0xFFFF, PageTable.AlternativePageTable); // Write -1

	gA = MemoryRead(gT, true);
	MemoryWrite(0xFFFF, gT, true, 2);
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
/// L		- The number of words to be moved (max 2048)
///
/// A and/or X are used for physical memory-block moves and are incremented when the D and/or T registers overflow.
///
/// If the L register contains a value grater then 2048 (L=o4000) no words are moved and A,D, T and X are unchanged.
///
/// After transfer the register contains: A,D, T,X - The addresses after the last moved word . L = zero
///
/// Format: MOVEW
/// </summary>
void DoMOVEW(uint16_t instr)
{
	unsigned int sourceAddress = gD;
	unsigned int destinationAddress = gT;
	uint16_t cnt = gL;

	uint16_t displacement = (instr & 0x00F);

	// Check if source and destination are in physical memory
	bool isSourcePhysical = false;
	bool isDestinationPhysical = false;

	switch (displacement)
	{
	case 2:
	case 5:
		destinationAddress = (destinationAddress | (gX << 16)) & 0xFFFFFF;
		isDestinationPhysical = true;
		break;

	case 6:
	case 7:
		sourceAddress = (sourceAddress | (gA << 16)) & 0xFFFFFF;
		isSourcePhysical = true;
		break;
	case 8:
		destinationAddress = (destinationAddress | (gX << 16)) & 0xFFFFFF;
		isDestinationPhysical = true;

		sourceAddress = (sourceAddress | (gA << 16)) & 0xFFFFFF;
		isSourcePhysical = true;
		break;
	}

	// Check for priveleged instruction
	if (isSourcePhysical || isDestinationPhysical)
	{
		if (!CheckPriv())
			return;
	}

	// Warning: In the loop of read/write below, PageFault can occur, and the instruction can be restarted.
	uint16_t temp = 0;

	while (cnt > 0)
	{
		switch (displacement)
		{
		case 0: // move from PT to PT
			temp = MemoryRead(sourceAddress, false);
			MemoryWrite(temp, destinationAddress, false, 2);
			break;
		case 1: // move from PT to APT
			temp = MemoryRead(sourceAddress, false);
			MemoryWrite(temp, destinationAddress, true, 2);
			break;
		case 2: // move from PT to physical memory
			temp = MemoryRead(sourceAddress, false);
			WritePhysicalMemory(destinationAddress, temp, true);
			break;
		case 3: // move from APT to PT
			temp = (uint16_t)MemoryRead(sourceAddress, true);
			MemoryWrite(temp, destinationAddress, false, 2);
			break;
		case 4: // move from APT to APT
			temp = (uint16_t)MemoryRead(sourceAddress, true);
			MemoryWrite(temp, destinationAddress, true, 2);
			break;
		case 5: // move from APT to physical memory
			temp = (uint16_t)MemoryRead(sourceAddress, true);
			WritePhysicalMemory(destinationAddress, temp, true);
			break;
		case 6: // move from physical memory to PT
			temp = ReadPhysicalMemory(sourceAddress, true);
			MemoryWrite(temp, destinationAddress, false, 2);
			break;
		case 7: // move from physical memory to APT
			temp = ReadPhysicalMemory(sourceAddress, true);
			MemoryWrite(temp, destinationAddress, true, 2);
			break;

		case 8: // move from physical memory to physical memory
			temp = ReadPhysicalMemory(sourceAddress, true);
			WritePhysicalMemory(destinationAddress, temp, true);
			break;

		default:
			break;
		}
		sourceAddress++;
		destinationAddress++;
		cnt--;
	}

	// After here, no PageFault can occur - update register values

	// update L
	gL = cnt;

	// Update Source with the new address
	gD = (sourceAddress & 0xFFFF);
	if (isSourcePhysical)
	{
		gA = (sourceAddress >> 16) & 0xFFFF;
	}

	// Update destination
	gT = (destinationAddress & 0xFFFF);
	if (isDestinationPhysical)
	{
		gX = (destinationAddress >> 16) & 0xFFFF;
	}
}

#define _removed_MOVB_AND_MOVBF_ 1
#if _removed_MOVB_AND_MOVBF_ // replaced with doMoveBytes
/*
 * MOVB instruction. TODO:: Fix edge case and document params here...
 */
void DoMOVB(uint16_t instr)
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
			thebyte = MemoryRead(addr_s, s_apt);
			thebyte = ((i + d_lr) & 1) ? thebyte : (thebyte >> 8) & 0xff; /* right, LSB : left, MSB */
			addr_d = dest + ((i + d_lr) >> 1);							  /* Word adress of byte to write */
			MemoryWrite(thebyte, addr_d, d_apt, ((i + d_lr) & 1));
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
			thebyte = MemoryRead(addr_s, s_apt);
			thebyte = ((i + d_lr) & 1) ? thebyte : (thebyte >> 8) & 0xff; /* right, LSB : left, MSB */
			addr_d = dest + ((i + d_lr) >> 1);							  /* Word adress of byte to write */
			MemoryWrite(thebyte, addr_d, d_apt, ((i + d_lr) & 1));
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

	gD &= 0x7000;				  /* Null number of bytes, as per manual, also null bit 15 */
	gT &= 0x7000;				  /* Null number of bytes, also null bit 15 */
	gD |= end_half << 15;		  /* set bit 15 to point to next free byte */
	gT |= end_half << 15;		  /* set bit 15 to point to next free byte */
	gT |= len & 0x0fff;			  /* number of bytes done to lowest 12 bits*/

	gA = addr_s + ((len + s_lr) >> 1);
	gX = addr_d + ((len + d_lr) >> 1);

	gPC++; /* This function has a SKIP return on no error, which is always? */
}

/*
 * MOVBF instruction. TODO:: ALL
 */
void DoMOVBF(uint16_t instr)
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
		overlap = false;
	else if ((uint16_t)((uint16_t)(ceil(len / 2)) + source - 1) > dest)
		overlap = true;
	else
		overlap = false;

	for (i = 0; i < len; i++)
	{
		addr_s = source + ((i + s_lr) >> 1); /* Word adress of byte to read */
		thebyte = MemoryRead(addr_s, s_apt);
		thebyte = ((i + d_lr) & 1) ? thebyte : (thebyte >> 8) & 0xff; /* right, LSB : left, MSB */
		addr_d = dest + ((i + d_lr) >> 1);							  /* Word adress of byte to write */
		MemoryWrite(thebyte, addr_d, d_apt, ((i + d_lr) & 1));
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

	gD &= 0xefff;				  /* Null bit 12 */
	gT &= 0xcfff;				  /* Null bit 12 & 13 */
	gD &= 0x7fff;				  /* Null bit 15 before assigning the end-of-field half */
	gT &= 0x7fff;				  /* Null bit 15 before assigning the end-of-field half */
	gD |= end_half << 15;		  /* set bit 15 to point to next free byte */
	gT |= end_half << 15;		  /* set bit 15 to point to next free byte */

	gD &= 0xf000;		 /* clean lowest bits before or */
	gT &= 0xf000;		 /* clean lowest bits before or */
	gD |= lens & 0x0fff; /* decremented byte counter to lowest 12 bits*/
	gT |= lend & 0x0fff; /* decremented byte counter to lowest 12 bits*/

	if (!overlap)
		gPC++; /* This function has a SKIP return on no error */

	return;
}
#endif


void add_A_mem(uint16_t eff_addr, bool UseAPT)
{
	int temp, data, oldreg;
	oldreg = gA;
	data = MemoryRead(eff_addr, UseAPT);
	temp = gA + data;

	// FIXME - ADD FLAG HANDLING CORRECTLY FOR C,O,Q FLAGS (CHECK AGAIN THINK WE MIGHT HAVE SUBTLE BUGS)

	if ((temp > 0xFFFF) || (temp < 0))
	{
		setbit(_STS, _C, 1);
		if ((oldreg & 0x8000) && (data & 0x8000) && !(temp & 0x8000))
		{
			setbit(_STS, _Q, 1);
		}
		else
		{
			setbit(_STS, _Q, 0);
		}
	}
	else
	{
		setbit(_STS, _C, 0);
		if (!(oldreg & 0x8000) && !(data & 0x8000) && (temp & 0x8000))
		{
			setbit(_STS, _Q, 1);
		}
		else
		{
			setbit(_STS, _Q, 0);
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
void doMoveBytes(bool checkOverlapping)
{
	const int LEN_MASK = 0xFFF;
	int readValue;

	int numBytesSource = gD & LEN_MASK; // Source length
	int numBytesDest = gT & LEN_MASK;	// Destination length
	if (numBytesDest < numBytesSource)
		numBytesSource = numBytesDest; // Cap number of bytes to max length of Destination

	// If Bit 13 is set, then setup has been executed and we are returning from an interrupt
	if (!(gD & (1 << 13)))
	{
		gT = (gT & 0xC000) | numBytesSource;
		gD = (gT & 0xC000);

		// Mark D bit 13 with setup done
		gD |= (1 << 13);
	}

	if (checkOverlapping)
	{
		// Convert byte count to word count for addressing
		int numWordsD = numBytesSource >> 1; // Same as numBytesD / 2

		// Calculate start and end positions for source and destination in terms of words
		int sourceStart = gA;
		int destinationStart = gX;
		int sourceEnd = sourceStart + numWordsD;
		int destinationEnd = destinationStart + numWordsD;

		// Check for forbidden overlap
		// Overlap is forbidden if destination overlaps source before it is read
		if (destinationStart < sourceEnd && destinationEnd > sourceStart)
		{
			// OVERLAP EXISTS - ILLEGAL IF 'MOVBF'!!
			// Forbidden overlap exists, return with error (no skip)
			return;
		}
	}

	bool useAPT = true; // Use alternative page table
	WriteMode readMode;
	WriteMode writeMode;

	if (gX < gA)
	{
		// High to low
		for (int i = (gT & LEN_MASK); i > 0; i--)
		{
			// Bit 15: 0=>MSB, 1=> LSB
			readMode = (gD & (1 << 15)) ? WRITEMODE_LSB : WRITEMODE_MSB;
			readValue = MemoryRead(gA, useAPT);

			if (readMode == WRITEMODE_MSB)
			{
				readValue = (readValue >> 8) & 0xFF;
			}
			else
			{
				readValue = readValue & 0xFF;
			}

			writeMode = (gT & (1 << 15)) ? WRITEMODE_LSB : WRITEMODE_MSB;
			MemoryWrite(readValue, gX, useAPT, writeMode);

			gD ^= (1 << 15); // Flip D bit 15
			if (!(gD & (1 << 15)))
				gA--;

			gT ^= (1 << 15); // Flip T bit 15
			if (!(gT & (1 << 15)))
				gX--;
		}
	}
	else
	{
		// Low to High
		for (int i = (gD & LEN_MASK); i < (gT & LEN_MASK); i++)
		{
			// Bit 15: 0=>MSB, 1=> LSB
			readMode = (gD & (1 << 15)) ? WRITEMODE_LSB : WRITEMODE_MSB;
			readValue = MemoryRead(gA, useAPT);

			if (readMode == WRITEMODE_MSB)
			{
				readValue = (readValue >> 8) & 0xFF;
			}
			else
			{
				readValue = readValue & 0xFF;
			}

			writeMode = (gT & (1 << 15)) ? WRITEMODE_LSB : WRITEMODE_MSB;
			MemoryWrite(readValue, gX, useAPT, writeMode);

			gD ^= (1 << 15); // Flip D bit 15
			if (!(gD & (1 << 15)))
				gA++;

			gT ^= (1 << 15); // Flip T bit 15
			if (!(gT & (1 << 15)))
				gX++;
		}
	}

	// After execution, bit 15 of the D and T registers point to the end of the field that has been moved.
	// Note: DON'T CLEAR bit 15 of D and T, but clear bits 13 and 12.

	// After execution the field length of the D (source) equals Zero
	// Note: Clear setup and count bits
	gD &= 0xC000;

	// Documentation for MOVB and MOVBF says the same but implementation differs
	if (checkOverlapping)
		gT &= 0xC000; // MOVBF
	else
		gT &= 0xCFFF; // MOVB

	gPC++; // SKIP return
}

/*
 * MOVB
 */
void ndfunc_movb(uint16_t instr)
{
	(void)instr;
	doMoveBytes(false);
}

/*
 * MOVBF instruction.
 */
void ndfunc_movbf(uint16_t instr)
{
	(void)instr;
	doMoveBytes(true);
}

void sub_A_mem(uint16_t eff_addr, bool UseAPT)
{
	int temp, data, oldreg;
	oldreg = gA;
	data = MemoryRead(eff_addr, UseAPT);
	temp = gA - data;
	/*
	 * FIXME - ADD FLAG HANDLING CORRECTLY FOR C,O,Q FLAGS (CHECK AGAIN THINK WE MIGHT HAVE SUBTLE BUGS)
	 */
	if ((temp > 0xFFFF) || (temp < 0))
	{
		setbit(_STS, _C, 0);
		if ((oldreg & 0x8000) && (data & 0x8000) && !(temp & 0x8000))
		{
			setbit(_STS, _Q, 1);
		}
		else
		{
			setbit(_STS, _Q, 0);
		}
	}
	else
	{
		setbit(_STS, _C, 1);
		if (!(oldreg & 0x8000) && !(data & 0x8000) && (temp & 0x8000))
		{
			setbit(_STS, _Q, 1);
		}
		else
		{
			setbit(_STS, _Q, 0);
		}
	}

	gA = (temp & 0xFFFF);
}

/*
 * RDIV
 */
void rdiv_org(uint16_t instr)
{
	int16_t divider;
	int dividend;
	div_t result3; /* stdlib.h */
	/* :TODO: Apparently Carry can be set too. CHECK that... Might be RAD=1??? */
	/* Overflow and division with zero also need to be fixed!! */
	/* :NOTE: The way it is described in the manual, we assume this is a fraction (numerator/denominator and return a quotient and remainder as per manual */
	divider = ((instr & 0x0038) >> 3) ? (int16_t)gReg->reg[gPIL][((instr & 0x0038) >> 3)] : 0;

	if (divider == 0)
	{
		// Division by zero
		setbit(_STS, _Z, 1);
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
void rdiv(uint16_t instr)
{
	/* FAITHFUL to RASK RDIV6 (CS 000430-000463); oracle-validated (RetroCore 4c29170d1). The success
	 * "loop path" results are UNCHANGED (what SINTRAN depends on); only the ERROR paths and the
	 * negative-dividend C/O/Q flags are corrected. Divide-by-zero / true overflow leave the dividend's
	 * two's-complement MAGNITUDE in A/D (minus |divisor| in the high word) and OR-set Z; the ND manual's
	 * "divide-by-zero -> A/D unchanged" is an abstraction (magnitude == original for a POSITIVE dividend,
	 * so they coincide there - which is why the old code passed only for positive dividends). */
	int dividend = ((int)gA << 16) | (int)gD;
	short divisor = ((instr & 0x0038) >> 3) ? (short)gReg->reg[gPIL][((instr & 0x0038) >> 3)] : 0;

	int dividendNegative = (dividend < 0);
	uint16_t origLow = gD; /* low word the microcode negates at CS 000434 (`-B`) */

	/* CS 000434 (NEGATIVE DIVIDEND): negate the 32-bit dividend to its magnitude; STS,EA latches the
	 * flags of the LOW-word (D) two's-complement negation. This precedes the STS save that brackets the
	 * loop, so these flags PERSIST on both the loop and error paths. Positive dividend: C/O/Q untouched. */
	if (dividendNegative)
	{
		int negOvf = (origLow == 0x8000); /* only 0x8000 overflows a 16-bit two's-complement negate */
		setbit(_STS, _C, (origLow == 0)); /* carry-out of -Dlow set iff Dlow == 0 */
		setbit(_STS, _Q, negOvf);
		if (negOvf)
			setbit(_STS, _O, 1); /* static overflow is sticky */
	}

	/* Operand magnitudes via UNSIGNED arithmetic (correct even for 0x80000000 / -32768). */
	unsigned int dividendMag = dividendNegative ? (0u - (unsigned int)dividend) : (unsigned int)dividend;
	uint16_t divisorMag = (uint16_t)((divisor < 0) ? (0u - (unsigned int)(int)divisor) : (unsigned int)(int)divisor);
	uint16_t dividendMagHigh = (uint16_t)(dividendMag >> 16);

	/* CS 000436 RDIV2 overflow PRE-CHECK: A := |dividend|_high - |divisor| (written back, ALUD,B). If
	 * |dividend|_high >= |divisor| (unsigned, no borrow) OR divisor == 0, the quotient cannot fit 16
	 * bits, so branch to RDIVZ BEFORE the loop: OR-set Z, leave A = that subtract and D = |dividend| low.
	 * The quotient/remainder are NEVER computed on this path. */
	if (divisorMag == 0 || dividendMagHigh >= divisorMag)
	{
		gA = (uint16_t)(dividendMagHigh - divisorMag);
		gD = (uint16_t)(dividendMag & 0xFFFF);
		setbit(_STS, _Z, 1);
		return;
	}

	/* LOOP PATH (|dividend|_high < |divisor|): the quotient magnitude fits 16 bits. */
	unsigned int quotientMag = dividendMag / divisorMag;
	unsigned int remainderMag = dividendMag % divisorMag;

	/* Quotient sign = sign(AD) XOR sign(SRCE); remainder sign = dividend sign (CS 000456). */
	int quotientNegative = dividendNegative ^ (divisor < 0);
	gA = quotientNegative ? (uint16_t)(0u - quotientMag) : (uint16_t)quotientMag;
	gD = dividendNegative ? (uint16_t)(0u - remainderMag) : (uint16_t)remainderMag;

	/* CS 000457 RDIV5 sign check: Z on SIGNED overflow (positive q > 32767, negative q > 32768 - so a
	 * -32768 quotient is VALID and does NOT set Z, unlike a naive |q| >= 32768 test). */
	if (quotientNegative ? (quotientMag > 0x8000u) : (quotientMag > 0x7FFFu))
		setbit(_STS, _Z, 1);
}

/*
 * RMPY
 */
void rmpy_org(uint16_t instr)
{
	/* :TODO: Apparently Carry can be set too. CHECK that... Might be RAD=1??? */
	int a, b, result;
	a = ((instr & 0x0038) >> 3) ? (int)gReg->reg[gPIL][((instr & 0x0038) >> 3)] : 0;
	b = (instr & 0x0007) ? (int)gReg->reg[gPIL][(instr & 0x0007)] : 0;
	result = a * b;
	if (abs(result) > INT_MAX)
	{ /* Set O and Q */
		setbit(_STS, _Q, 1);
		setbit(_STS, _O, 1);
	}
	else
	{
		; //: TODO: Carry???;
		setbit(_STS, _Q, 0);
		setbit(_STS, _O, 0);
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
void rmpy(uint16_t instr)
{
	int minusCnt = 0;
	short source_value = (short)((instr & 0x0038) >> 3) ? (short)gReg->reg[gPIL][((instr & 0x0038) >> 3)] : 0;
	short dest_value = (short)(instr & 0x0007) ? (short)gReg->reg[gPIL][(instr & 0x0007)] : 0;

	// Use int for absolute values to avoid overflow when negating -32768
	int abs_src = (int)source_value;
	int abs_dst = (int)dest_value;

	if (abs_src < 0)
	{
		abs_src = -abs_src;
		minusCnt++;
	}

	if (abs_dst < 0)
	{
		abs_dst = -abs_dst;
		minusCnt++;
	}

	int result = abs_src * abs_dst; /* magnitude of the product (always non-negative here) */

	/* STATUS FLAGS from the RASK microcode, NOT "product > 16 bits" (that was a guess and is wrong).
	 * RMPY runs its own routine RMPY4 (CS 004350-004363): a SAME-SIGN result writes NO status (C/O/Q/M
	 * left unchanged); an OPPOSITE-SIGN result negates the product and STS,EA (CS 004362) latches the
	 * flags of the LOW-word two's-complement negation: C = carry-out (low word == 0), Q = overflow
	 * (low word == 0x8000), O = O OR that overflow. Oracle-validated (RetroCore 135a2ff28). */
	if (minusCnt == 1)
	{
		int lowWord = result & 0xFFFF;         /* low word of the positive magnitude (what -Q negates) */
		int ovf = (lowWord == 0x8000);         /* only 0x8000 overflows a 16-bit two's-complement negate */
		setbit(_STS, _C, (lowWord == 0));      /* carry-out of -Q is set iff Q == 0 */
		setbit(_STS, _Q, ovf);
		if (ovf) setbit(_STS, _O, 1);          /* static overflow is sticky (OVF | O) */
		result = -result;                      /* sign-correct the product */
	}
	/* else (minusCnt 0 or 2): same-sign result -> microcode writes NO status; leave C/O/Q/M unchanged. */

	// set A and D registers
	gA = (uint16_t)((result >> 16) & 0xFFFF);
	gD = (uint16_t)(result & 0xFFFF);
}

/*
 * MPY
 */
void mpy(uint16_t operand)
{
	int a, b, result;
	a = (int16_t)gA;

	gEA = New_GetEffectiveAddr(operand, &gUseAPT);
	uint16_t mem = MemoryRead(gEA, gUseAPT);
	b = (int16_t)mem;

	setbit(_STS, _Q, 0);

	result = a * b;

	if (abs(result) > 32767)
	{ /* Set O and Q */
		setbit(_STS, _Q, 1);
		setbit(_STS, _O, 1);
	}
	gA = (int16_t)result;
}



/************************ BCD instructions *************************/

/* BCD registers and helper functions */
uint16_t D1 = 0;
uint16_t D2 = 0;

void GetBCD(uint16_t address)
{
	D1 = MemoryRead(address, true);
	D2 = MemoryRead((address + 1) & 0xFFFF, true);
}

void StoreBCD(uint16_t address)
{
	MemoryWrite(address, D1, true, WRITEMODE_WORD);
	MemoryWrite((address + 1) & 0xFFFF, D2, true, WRITEMODE_WORD);
}

/* ADDD, SUBD, COMD, PACK, UPACK, SHDE are in bcd.c */


/*************************** INITIALIZATION ***************************/

void Instruction_Add(int opcode, void *funcpointer)
{
	if (instr_funcs[opcode] != NULL)
	{
		LOG(LOG_CAT_CPU, LOG_WARN, "Overwriting instruction %06o", opcode);
	}

	instr_funcs[opcode] = funcpointer;
}

void Instruction_Add_Range(int start, int stop, void *funcpointer)
{
	int i;
	for (i = start; i <= stop; i++)
	{
		if (instr_funcs[i] != NULL)
		{
			LOG(LOG_CAT_CPU, LOG_WARN, "Overwriting instruction %06o",i);
		}

		instr_funcs[i] = funcpointer;
	}
	return;
}

void Instruction_Add_Mask(int opcode, int mask, void *funcpointer)
{
	int i;
	int signature = opcode & mask;


	for (i = opcode; i <= 0xFFFF; i++)
	{
		if ((i & mask) == signature)
		{
			if (instr_funcs[i] != NULL)
			{
				LOG(LOG_CAT_CPU, LOG_WARN, "Overwriting instruction %06o with %06o", i, opcode);
			}

			instr_funcs[i] = funcpointer;
		}
	}
	return;
}

/*
 * Add IO handler addresses in this function
 * This also thus actually acts as the new instruction parser also.
 */
void Setup_Instructions(void)
{
	//Instruction_Add_Range(0000000, 0177777, &illegal_instr); /* First make all instructions by default point to illegal_instr  */

	// Instruction_Add_Range(0000000, 0003777, &ndfunc_stz); /* STZ  */
	Instruction_Add_Mask(0000000, 0xF800, &ndfunc_stz);

	// Instruction_Add_Range(0004000, 0007777, &ndfunc_sta); /* STA  */
	Instruction_Add_Mask(0004000, 0xF800, &ndfunc_sta);

	// Instruction_Add_Range(0010000, 0013777, &ndfunc_stt); /* STT  */
	Instruction_Add_Mask(0010000, 0xF800, &ndfunc_stt);

	// Instruction_Add_Range(0014000, 0017777, &ndfunc_stx); /* STX  */
	Instruction_Add_Mask(0014000, 0xF800, &ndfunc_stx);

	// Instruction_Add_Range(0020000, 0023777, &ndfunc_std); /* STD  */
	Instruction_Add_Mask(0020000, 0xF800, &ndfunc_std);

	// Instruction_Add_Range(0024000, 0027777, &ndfunc_ldd); /* LDD  */
	Instruction_Add_Mask(0024000, 0xF800, &ndfunc_ldd);

	// Instruction_Add_Range(0030000, 0033777, &ndfunc_stf); /* STF  */
	Instruction_Add_Mask(0030000, 0xF800, &ndfunc_stf);

	// Instruction_Add_Range(0034000, 0037777, &ndfunc_ldf); /* LDF  */
	Instruction_Add_Mask(0034000, 0xF800, &ndfunc_ldf);

	// Instruction_Add_Range(0040000, 0043777, &ndfunc_min); /* MIN  */
	Instruction_Add_Mask(0040000, 0xF800, &ndfunc_min);

	// Instruction_Add_Range(0044000, 0047777, &ndfunc_lda); /* LDA  */
	Instruction_Add_Mask(0044000, 0xF800, &ndfunc_lda);

	// Instruction_Add_Range(0050000, 0053777, &ndfunc_ldt); /* LDT  */
	Instruction_Add_Mask(0050000, 0xF800, &ndfunc_ldt);

	// Instruction_Add_Range(0054000, 0057777, &ndfunc_ldx); /* LDX  */
	Instruction_Add_Mask(0054000, 0xF800, &ndfunc_ldx);

	// Instruction_Add_Range(0060000, 0063777, &ndfunc_add); /* ADD  */
	Instruction_Add_Mask(0060000, 0xF800, &ndfunc_add);

	// Instruction_Add_Range(0064000, 0067777, &ndfunc_sub); /* SUB  */
	Instruction_Add_Mask(0064000, 0xF800, &ndfunc_sub);

	// Instruction_Add(0070000, 0073777, &ndfunc_and); /* AND  */
	Instruction_Add_Mask(0070000, 0xF800, &ndfunc_and);

	// Instruction_Add_Range(0074000, 0077777, &ndfunc_ora); /* ORA  */
	Instruction_Add_Mask(0074000, 0xF800, &ndfunc_ora);

	// Instruction_Add_Range(0100000, 0103777, &ndfunc_fad); /* FAD  */
	Instruction_Add_Mask(0100000, 0xF800, &ndfunc_fad);

	// Instruction_Add_Range(0104000, 0107777, &ndfunc_fsb); /* FSB  */
	Instruction_Add_Mask(0104000, 0xF800, &ndfunc_fsb);

	// Instruction_Add_Range(0110000, 0113777, &ndfunc_fmu); /* FMU  */
	Instruction_Add_Mask(0110000, 0xF800, &ndfunc_fmu);

	// Instruction_Add_Range(0114000, 0117777, &ndfunc_fdv); /* FDV  */
	Instruction_Add_Mask(0114000, 0xF800, &ndfunc_fdv);

	// Instruction_Add_Range(0120000, 0123777, &mpy);		/* MPY  */
	Instruction_Add_Mask(0120000, 0xF800, &mpy);

	// Instruction_Add_Range(0124000, 0127777, &ndfunc_jmp); /* JMP  */
	Instruction_Add_Mask(0124000, 0xF800, &ndfunc_jmp);

	// Instruction_Add_Range(0134000, 0137777, &ndfunc_jpl); /* JPL  */
	Instruction_Add_Mask(0134000, 0xF800, &ndfunc_jpl);

	// CJPs - Conditional jumps
	// Instruction_Add_Range(0130000, 0130377, &ndfunc_jap); /* JAP */
	Instruction_Add_Mask(0130000, 0xFF00, &ndfunc_jap);

	// Instruction_Add_Range(0130400, 0130777, &ndfunc_jan); /* JAN */
	Instruction_Add_Mask(0130400, 0xFF00, &ndfunc_jan);

	// Instruction_Add_Range(0131000, 0131377, &ndfunc_jaz); /* JAZ */
	Instruction_Add_Mask(0131000, 0xFF00, &ndfunc_jaz);

	// Instruction_Add_Range(0131400, 0131777, &ndfunc_jaf); /* JAF */
	Instruction_Add_Mask(0131400, 0xFF00, &ndfunc_jaf);

	// Instruction_Add_Range(0132000, 0132377, &ndfunc_jpc); /* JPC */
	Instruction_Add_Mask(0132000, 0xFF00, &ndfunc_jpc);

	// Instruction_Add_Range(0132400, 0132777, &ndfunc_jnc); /* JNC */
	Instruction_Add_Mask(0132400, 0xFF00, &ndfunc_jnc);

	// Instruction_Add_Range(0133000, 0133377, &ndfunc_jxz); /* JXZ */
	Instruction_Add_Mask(0133000, 0xFF00, &ndfunc_jxz);

	// Instruction_Add_Range(0133400, 0133777, &ndfunc_jxn); /* JXN */
	Instruction_Add_Mask(0133400, 0xFF00, &ndfunc_jxn);

	// Instruction_Add(0140000, 0143777, &ndfunc_skp);
	Instruction_Add_Mask(0140000, 0xF8C0, &ndfunc_skp);

	// BCD (CX)
	Instruction_Add(0140120, &ndfunc_addd);	  /* ADDD  */
	Instruction_Add(0140121, &ndfunc_subd);	  /* SUBD  */
	Instruction_Add(0140122, &ndfunc_comd);	  /* COMD  */
	Instruction_Add(0140124, &ndfunc_pack);	  /* PACK  */
	Instruction_Add(0140125, &ndfunc_unpack); /* UPACK */
	Instruction_Add(0140126, &ndfunc_shde);	  /* SHDE  */

	Instruction_Add(0140123, &DoTSET); /* TSET  */
	Instruction_Add(0140127, &DoRDUS); /* RDUS  */

	{ // CE; CX

		Instruction_Add(0140130, &ndfunc_bfill); /* BFILL */
		Instruction_Add(0140131, &DoMOVB);		 /* MOVB  */
		Instruction_Add(0140132, &DoMOVBF);		 /* MOVBF */

		// Instruction_Add(0140131, &ndfunc_movb);  /* MOVB  */
		// Instruction_Add(0140132, &ndfunc_movbf); /* MOVBF */
	}

	switch (CurrentCPUType)
	{
	case ND110:
	case ND110CE:
	case ND110CX:
	case ND110PCX:
	case ND120CX:    /* ND-120 is instruction-set-identical to the ND-110/CX (VERSN + the 140133 / */
	                 /* 140500-140517 / 14070x ND-110 groups). Without VERSN here it traps illegal, */
	                 /* and TPE cannot read the ND-120/CX identity. Reapplied from session-windows-work. */
		Instruction_Add(0140133, &ndfunc_versn); /* VERSN - ND110+ */
		break;
	default:
		break;
	}

	{ // CE; CX

		Instruction_Add(0140134, &ndfunc_init);	 /* INIT  */
		Instruction_Add(0140135, &ndfunc_entr);	 /* ENTR  */
		Instruction_Add(0140136, &ndfunc_leave); /* LEAVE */
		Instruction_Add(0140137, &ndfunc_eleav); /* ELEAV */
	}
	// Instruction_Add(0140200, 0140277, &illegal_instr); /* USER1 (microcode defined by user or illegal instruction otherwise) */
	Instruction_Add(0140200, &ndfunc_halt); /* HALT - emulator exit, A=exit code */

	switch (CurrentCPUType)
	{
	case ND110:
	case ND110CE:
	case ND110CX:
	case ND110PCX:
	case ND120CX:    /* ND-120 is instruction-set-identical to the ND-110/CX - same ND-110 opcode group. */
		// ALL are priveleged!
		Instruction_Add(0140500, &ndfunc_wglob); /* WGLOB - ND110 Specific */
		Instruction_Add(0140501, &ndfunc_rglob); /* RGLOB - ND110 Specific */
		Instruction_Add(0140502, &ndfunc_inspl); /* INSPL - ND110 Specific */
		Instruction_Add(0140503, &ndfunc_rempl); /* REMPL - ND110 Specific */
		Instruction_Add(0140504, &ndfunc_cnrek); /* CNREK - ND110 Specific */
		Instruction_Add(0140505, &ndfunc_clpt);	 /* CLPT  - ND110 Specific */
		Instruction_Add(0140506, &ndfunc_enpt);	 /* ENPT  - ND110 Specific */
		Instruction_Add(0140507, &ndfunc_rept);	 /* REPT  - ND110 Specific */
		Instruction_Add(0140510, &ndfunc_lbit);	 /* LBIT  - ND110 Specific */
		/*
		 * 140511 LBITP and 140512 SBIT were MISSING from this table entirely (not even
		 * registered as unimplemented) - see ND-06.029.1 EN and RetroCore
		 * Instructions.cs (hasND110Group), which registers the full 140510-140517 run.
		 */
		Instruction_Add(0140511, &ndfunc_lbitp); /* LBITP - ND110 Specific */
		Instruction_Add(0140512, &ndfunc_sbit);	 /* SBIT  - ND110 Specific */
		Instruction_Add(0140513, &ndfunc_sbitp); /* SBITP - ND110 Specific */
		Instruction_Add(0140514, &ndfunc_lbytp); /* LBYTP - ND110 Specific */
		Instruction_Add(0140515, &ndfunc_sbytp); /* SBYTP - ND110 Specific */
		Instruction_Add(0140516, &ndfunc_tsetp); /* TSETP - ND110 Specific */
		Instruction_Add(0140517, &ndfunc_rdusp); /* RDUSP - ND110 Specific */

		break;
	default:
		// Instruction_Add_Range(0140500, 0140577, &illegal_instr); /* USER2 (microcode defined by user or illegal instruction otherwise) */
		break;
	}

	Instruction_Add_Mask(0140600, 0xFFC0, &DoEXR); /* EXR */
	switch (CurrentCPUType)
	{
	case ND110:
	case ND110CE:
	case ND110CX:
	case ND110PCX:
	case ND120CX:    /* ND-120 is instruction-set-identical to the ND-110/CX - same ND-110 opcode group. */
		/*
		 * ALL are priveleged!
		 *
		 * These carry a 3-bit displacement in bits 3-5 of the opcode (14070x + delta<<3),
		 * so they MUST be registered with mask 0xFFC7 (bits 3-5 left free) - registering
		 * only the bare 14070x word left the 56 displaced encodings undecoded.
		 * Note also that 0140703 was mislabelled "SASB" here; it is SACB.
		 */
		Instruction_Add_Mask(0140700, 0xFFC7, &ndfunc_lasb); /* LASB - ND110 Specific */
		Instruction_Add_Mask(0140701, 0xFFC7, &ndfunc_sasb); /* SASB - ND110 Specific */
		Instruction_Add_Mask(0140702, 0xFFC7, &ndfunc_lacb); /* LACB - ND110 Specific */
		Instruction_Add_Mask(0140703, 0xFFC7, &ndfunc_sacb); /* SACB - ND110 Specific */
		Instruction_Add_Mask(0140704, 0xFFC7, &ndfunc_lxsb); /* LXSB - ND110 Specific */
		Instruction_Add_Mask(0140705, 0xFFC7, &ndfunc_lxcb); /* LXCB - ND110 Specific */
		Instruction_Add_Mask(0140706, 0xFFC7, &ndfunc_szsb); /* SZSB - ND110 Specific */
		Instruction_Add_Mask(0140707, 0xFFC7, &ndfunc_szcb); /* SZCB - ND110 Specific */
		break;
	default:
		break;
	}

	if (true)
	{
		// ND100-CX and ND110-CX only

		Instruction_Add(0140300, &ndfunc_setpt);		 /* SETPT */
		Instruction_Add(0140301, &ndfunc_clept);		 /* CLEPT */
		Instruction_Add(0140302, &ndfunc_clnreent);		 /* CLNREENT */
		Instruction_Add(0140303, &ndfunc_chreent_pages); /* CHREENT-PAGES */
		Instruction_Add(0140304, &ndfunc_clepu);		 /* CLEPU */
	}
	Instruction_Add_Mask(0141200, 0xFFC0, &rmpy);		 /* RMPY */
	Instruction_Add_Mask(0141600, 0xFFC0, &rdiv);		 /* RDIV */
	Instruction_Add_Mask(0142200, 0xFFC0, &ndfunc_lbyt); /* LBYT */
	Instruction_Add_Mask(0142600, 0xFFC0, &ndfunc_sbyt); /* SBYT */

	// CX instructions
	Instruction_Add(0142700, &ndfunc_geco);				 /* GECO - Undocumented instruction */
	Instruction_Add_Mask(0143100, 0xFFC0, &DoMOVEW);	 /* MOVEW */
	Instruction_Add_Mask(0143200, 0xFFC0, &ndfunc_mix3); /* MIX3 */

	Instruction_Add_Mask(0143300, 0xFFC7, &ndfunc_ldatx); /* LDATX */
	Instruction_Add_Mask(0143301, 0xFFC7, &ndfunc_ldxtx); /* LDXTX */
	Instruction_Add_Mask(0143302, 0xFFC7, &ndfunc_lddtx); /* LDDTX */
	Instruction_Add_Mask(0143303, 0xFFC7, &ndfunc_ldbtx); /* LDBTX */
	Instruction_Add_Mask(0143304, 0xFFC7, &ndfunc_statx); /* STATX */
	Instruction_Add_Mask(0143305, 0xFFC7, &ndfunc_stztx); /* STZTX */
	Instruction_Add_Mask(0143306, 0xFFC7, &ndfunc_stdtx); /* STDTX */

	Instruction_Add(0143500, &ndfunc_lwcs); /* LWCS */

	Instruction_Add(0143604, &ndfunc_ident); /* IDENT PL10 */
	Instruction_Add(0143611, &ndfunc_ident); /* IDENT PL11 */
	Instruction_Add(0143622, &ndfunc_ident); /* IDENT PL12 */
	Instruction_Add(0143643, &ndfunc_ident); /* IDENT PL13 */

	Instruction_Add_Range(0144000, 0147777, &regop); /* --ROPS-- */
	Instruction_Add_Mask(0150000, 0xFFF0, &DoTRA);	 /* TRA */
	Instruction_Add_Mask(0150100, 0xFFF0, &DoTRR);	 /* TRR */
	Instruction_Add_Mask(0150200, 0xFFF0, &DoMCL);	 /* MCL */
	Instruction_Add_Mask(0150300, 0xFFF0, &DoMST);	 /* MST */
	Instruction_Add(0150400, &ndfunc_opcom);		 /* OPCOM */
	Instruction_Add(0150401, &ndfunc_iof);			 /* IOF */
	Instruction_Add(0150402, &ndfunc_ion);			 /* ION */
	switch (CurrentCPUType)
	{
	case ND110PCX:
		/* ND110 Butterfly only instruction */
		Instruction_Add(0150403, &unimplemented_instr); /* RTNSIM (SECRE) */
		break;
	default:
		break;
	}
	Instruction_Add(0150404, &ndfunc_pof);	/* POF */
	Instruction_Add(0150405, &ndfunc_piof); /* PIOF */
	Instruction_Add(0150406, &ndfunc_sex);	/* SEX */
	Instruction_Add(0150407, &ndfunc_rex);	/* REX */
	Instruction_Add(0150410, &ndfunc_pon);	/* PON */
	Instruction_Add(0150412, &ndfunc_pion); /* PION */

	Instruction_Add(0150415, &ndfunc_ioxt); /* IOXT */
	Instruction_Add(0150416, &ndfunc_exam); /* EXAM */
	Instruction_Add(0150417, &ndfunc_depo); /* DEPO */

	Instruction_Add_Mask(0151000, 0xFF00, &DoWAIT);		/* WAIT - Range 151000 - 151377 */
	Instruction_Add_Mask(0151400, 0xFF00, &ndfunc_nlz); /* NLZ */
	Instruction_Add_Mask(0152000, 0xFF00, &ndfunc_dnz); /* DNZ */
	Instruction_Add_Mask(0152402, 0xFF07, &ndfunc_srb); /* SRB */
	Instruction_Add_Mask(0152600, 0xFF07, &ndfunc_lrb); /* LRB */
	Instruction_Add_Mask(0153000, 0xFF00, &ndfunc_mon); /* MON  - Range 153000-153377  */
	Instruction_Add_Mask(0153400, 0xFF80, &ndfunc_irw); /* IRW */
	Instruction_Add_Mask(0153600, 0xFF80, &ndfunc_irr); /* IRR */

	// Instruction_Add_Range(0154000, 0157777, &ndfunc_shifts); /* SHT, SHD, SHA, SAD */  /* NOTE: this is actually a ND1 instruction, so need to check which NDs implement it later */
	Instruction_Add_Mask(0154000, 0x7980, &ndfunc_shifts); // SHT
	Instruction_Add_Mask(0154200, 0x7980, &ndfunc_shifts); // SHD
	Instruction_Add_Mask(0154400, 0x7980, &ndfunc_shifts); // SHA
	Instruction_Add_Mask(0154600, 0x7980, &ndfunc_shifts); // SAD

	// IOT Range 0160000 - 0163777
	Instruction_Add_Mask(0160000, 0xF800, &ndfunc_iot); /* IOT  - ND1 specific, but exists on all CPU's*/

	Instruction_Add_Mask(0164000, 0xF800, &ndfunc_iox); /* IOX */

	Instruction_Add_Mask(0170000, 0xFF00, &ndfunc_sab); /* SAB */
	Instruction_Add_Mask(0170400, 0xFF00, &ndfunc_saa); /* SAA */
	Instruction_Add_Mask(0171000, 0xFF00, &ndfunc_sat); /* SAT */
	Instruction_Add_Mask(0171400, 0xFF00, &ndfunc_sax); /* SAX */
	Instruction_Add_Mask(0172000, 0xFF00, &ndfunc_aab); /* AAB */
	Instruction_Add_Mask(0172400, 0xFF00, &ndfunc_aaa); /* AAA */
	Instruction_Add_Mask(0173000, 0xFF00, &ndfunc_aat); /* AAT */
	Instruction_Add_Mask(0173400, 0xFF00, &ndfunc_aax); /* AAX */

	Instruction_Add_Range(0174000, 0177777, &do_bops); /* Bit Operation Instructions */
													   /* Bit operations, 16 of them, 4 BSET,4 BSKP and 8 others */
}
