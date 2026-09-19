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


// CPU throttle: match emulated time to wall clock
// When enabled, sleeps to maintain target instruction rate.
#include <stdint.h>
#include <stdbool.h>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <time.h>
#endif
static bool cpu_throttle_enabled = false;
// Default matches the documented -Z/--throttle default (README/help): 0.5275 MHz,
// which makes the ticks-mode RTC (one pulse per 10550 instructions) exactly 50 Hz
// real-time. NOTE: an earlier comment claimed "SINTRAN dat matches wall clock at
// 1.125 MHz"; that contradicts the 10550-instruction RTC period (1.125 MHz gives
// 106.6 Hz) and was not re-verified - changed to 0.5275 by decision 27-JUL-2026.
static double cpu_throttle_mhz = 0.5275;

// High-resolution clock for throttle timing
static uint64_t throttle_get_ns(void) {
#if defined(_WIN32) || defined(_WIN64)
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (uint64_t)(now.QuadPart * 1000000000ULL / freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}


#include <string.h>
#include <stdlib.h>	/* getenv() - ND100X_CPUTYPE override, see cpu_set_type_from_env() */
#include <setjmp.h>
#ifndef __EMSCRIPTEN__
#include <unistd.h>
#endif


#include "cpu_types.h"
#include "cpu_protos.h"
#include "../ndlib/log.h"

/* Forward declarations for ring buffer diagnostics */
static uint16_t last_device_irq_bits = 0;
void ring_dump(void);



#ifdef WITH_DEBUGGER
	void stop_debugger_thread(void);

#ifdef __EMSCRIPTEN__
	/* WASM: single-threaded, no atomics needed */
	static int s_cpu_run_mode;
	static int s_cpu_stop_reason;
	static bool s_debugger_request_pause;
	static bool s_debugger_control_granted;
#elif defined(_WIN32)
    #include <windows.h>
	static volatile LONG s_cpu_run_mode;
	static volatile LONG s_cpu_stop_reason;
	static volatile LONG s_debugger_request_pause;
	static volatile LONG s_debugger_control_granted;
#else
    #include <stdatomic.h>
	#include <pthread.h>

	static atomic_int s_cpu_run_mode;           // CPURunMode
	static atomic_int s_cpu_stop_reason;     // CpuStopReason
	static atomic_bool s_debugger_request_pause; // set by DAP thread to request pause
	static atomic_bool s_debugger_control_granted; // set by CPU thread when paused and debugger can access
#endif

#else
int CurrentCPURunMode;
#endif

#include "../machine/machine_types.h"
#include "../machine/machine_protos.h"

// forward declaration for debugger.c function
void debugger_build_stack_trace(uint16_t pc, uint16_t operand);
void debugger_update_jpl_entrypoint(uint16_t ea);


// Global CPU variable definitions
_NDRAM_ g_volatile_memory;

/*
 * The CPU model this emulator presents to the guest.
 *
 * WHY THIS MATTERS: Setup_Instructions() gates whole instruction groups on this value,
 * and the guest software detects the CPU by *probing* those instructions.  The TPE
 * "INSTRUCTION" diagnostic executes VERSN (140133) at PC 003774 before it prints its
 * "CPU type.............:" line: if VERSN traps as an illegal instruction the program
 * concludes it is running on an ND-100, otherwise on an ND-110/ND-120 (SINTRAN's SYSEVAL
 * in PH-P2-OPPSTART.NPL:3467-3532 uses the same probe-and-trap technique for its
 * HWINFO(0) family/instruction-set bytes).
 *
 * This used to be a plain uninitialised global, i.e. 0 == ND1.  That silently disabled
 * EVERY ND-110-gated registration below (VERSN, the 14050x/14051x S3SEG group and the
 * 14070x bank group), so nd100x could ONLY ever identify as an ND-100/CX - which is
 * exactly what TPE's INSTRUCTION printed ("ND-100/CX upgraded for 16 PITs").
 *
 * It is now an explicit, run-time-selectable value (ND100X_CPUTYPE, see
 * cpu_set_type_from_env()).  Setting ND110CX makes TPE report "CPU type: ND-110/CX" and
 * run the ND-110 variants of its subtests - verified clean, zero "*** ERROR ***".
 *
 * The DEFAULT is ND110CX (Phase 6).  It used to be ND100CX, because under ND110CX SINTRAN
 * takes its ND-110 segment-handling path (WGLOB/RGLOB/INSPL/REMPL/ENPT/CLPT plus the
 * 14070x bank group) and used to live-lock in an ENPT/CLPT retry loop before the banner.
 * That live-lock (ledger B26) was root-caused and fixed - CLPT in save mode was not
 * clearing the page-table entry - so SINTRAN III now boots from SMD under ND110CX with the
 * ND-110 group live.  Defaulting to ND110CX is the point of Phase 6: nd100x and RetroCore
 * must run the IDENTICAL instruction set in their normal configuration.
 *
 * ND100X_CPUTYPE remains a full run-time override in BOTH directions (including selecting
 * ND100CX again) - see cpu_set_type_from_env().
 */
CpuType g_current_cpu_type = ND110CX;

/* Which FPP is installed. Default is the standard 48-bit unit so existing
 * boots, images and tests keep today's behaviour; the 32-bit option is
 * selected with [machine] fpp = 32 or the --fpp=32 command line flag. */
FppType g_current_fpp_type = FPP48;

// Installed main-memory size in 16-bit WORDS. Default 4 MB (4 MW = 2097152 words);
// overridden at start-up by --memory / the .ini memory= key (range 1..16 MB). The
// backing VolatileMemory array is always the 16 MB maximum; this caps how much is
// actually installed/visible (see the `addr >= ND_Memsize` guards in cpu_mms.c).
uint32_t g_nd_memsize = 4u * ND_WORDS_PER_MB;   // 2097152 words


struct CpuRegs *g_reg = NULL;

uint64_t  g_instr_counter = 0;
uint16_t g_start_addr = 0;
int g_disasm = 0;
int g_cpu_exit_code = 0;
int g_cpu_trace = 0;

/*
 * BSD kernel-stack high-water tracking (--bsd-debug).
 * The ND-100 2.11BSD port keeps the per-process kernel stack in a fixed
 * virtual window: u-area base 0xE400 (0162000) .. KERN_STOP 0172000, and
 * csav only allows frames >= 0164114.  When running kernel code (PIL >= 2),
 * the B register is the current kernel frame pointer; its minimum value is
 * the deepest frame reached = the stack high-water.  used = KERN_STOP - min.
 */
int g_bsd_debug = 0;
#define BSD_KSTK_BASE  0162000		/* u-area base (0xE400) */
#define BSD_KSTK_TOP   0170000		/* KERN_STOP (0xF000, USIZE=3/KERN_SSIZE=1) */
static unsigned short s_bsd_kstk_min = BSD_KSTK_TOP;
uint64_t g_cpu_max_instr = 0;
int g_cpu_breakpoint_enabled = 0;
uint16_t g_cpu_breakpoint_addr = 0;
int g_cpu_ring_dump_size = 0;



/* Instruction handling array */






// Used for TRAP handling to exit an instruction that fails
static jmp_buf s_cpu_jmp_buf;



/* ND-110 opcode trace: 0 = off, 1 = on. Set by cpu_trace_nd110_set(); see do_op(). */
static int nd110_trace_enabled = 0;

/*
 * Destination of the ND-110 trace.  Defaults to stdout, but when --trace-nd110=FILE
 * (or [runtime] trace_nd110 = FILE) names a path the trace is written THERE instead.  That matters for the console-driven
 * diagnostics (TPE, the SINTRAN SMD boot): those sessions are read back out of the Windows
 * console SCREEN BUFFER, so a trace on stdout scrolls the guest's own output away.  Writing
 * the trace to a side file keeps the screen pristine while still capturing every opcode.
 */
FILE *g_nd110_trace_fp = NULL;

/**
 * @brief Turn the ND-110 opcode trace on (--trace-nd110[=FILE]).
 * @param path File to write the trace to; NULL or "" writes to stdout.
 * @return 0, or -1 if the file could not be opened (the trace stays off).
 */
int cpu_trace_nd110_set(const char *path)
{
	FILE *fp = stdout;
	if (path != NULL && path[0] != '\0')
	{
		fp = fopen(path, "w");
		if (fp == NULL)
			return -1;
	}
	g_nd110_trace_fp = fp;
	nd110_trace_enabled = 1;
	return 0;
}

void do_op(uint16_t operand, bool isEXR)
{

	if (!isEXR)
		gPC++; // Move P before starting instruction. (but not if executed from register)

	if (g_instr_funcs[operand] == NULL)
	{
		illegal_instr(operand);
		return;
	}

	/*
	 * ND-110 trace (--trace-nd110[=FILE]): log every execution of an ND-110-only opcode
	 * (VERSN, the 1403xx S3SEG group, the 14050x/14051x group and the 14070x bank group).
	 * Used to see which of them the guest actually reaches when the CPU presents as
	 * ND-110/CX.
	 */
	if (nd110_trace_enabled)
	{
		if (operand == 0140133 ||
		    (operand >= 0140300 && operand <= 0140304) ||
		    (operand >= 0140500 && operand <= 0140517) ||
		    (operand >= 0140700 && operand <= 0140777))
		{
			fprintf(g_nd110_trace_fp,
				"ND110OP %06o at %06o A=%06o T=%06o X=%06o D=%06o B=%06o STBNK=%06o STSRT=%06o CMBUK=%06o\n",
				operand, (uint16_t)(gPC - 1), gA, gT, gX, gD, gB, gSTBNK, gSTSRT, gCMBUK);
			fflush(g_nd110_trace_fp);
		}
	}

	g_instr_funcs[operand](operand); /* call using a function pointer from the array
				   this way we are as flexible as possible as we
				   implement io calls. */

}





/* Calculates the effective address to use.
 * Uses MemoryRead to do this so we get the Page Table handling
 * done correctly. Also sets the bool use_apt points to, to tell caller what PT
 * to use for the actual use of the address supplied
 * See Manual ND.06.014, Page 34
 */
uint16_t New_GetEffectiveAddr(uint16_t instr, bool *use_apt)
{
	int disp = signExtend(instr & 0xFF);
	uint16_t eff_addr;

	uint16_t P = (gPC - 1) & 0xFFFF;

	switch ((instr >> 8) & 0x07)
	{
	case 0: /* (P) + disp */
		eff_addr = P + disp;
		*use_apt = false;
		break;
	case 1: /* (B) + disp */
		eff_addr = gB + disp;
		*use_apt = true;
		break;
	case 2: /* ((P) + disp) */
		eff_addr = P + disp;
		eff_addr = ReadIndirectVirtualMemory(eff_addr, false);
		*use_apt = true;
		break;
	case 3: /* ((B) + disp) */
		eff_addr = gB + disp;
		eff_addr = ReadIndirectVirtualMemory(eff_addr, true);
		*use_apt = true;
		break;
	case 4: /* (X) + disp */
		eff_addr = gX + disp;
		*use_apt = true;
		break;
	case 5: /* (B) + disp + (X) */
		eff_addr = gB + gX + disp;
		*use_apt = true;
		break;
	case 6: /* ((P) + disp) + (X) */
		eff_addr = P + disp;
		eff_addr = gX + ReadIndirectVirtualMemory(eff_addr, false);
		*use_apt = true;
		break;
	case 7: /* ((B) + disp) + (X) */
		eff_addr = (gB + disp) & 0xFFFF;
		eff_addr = gX + ReadIndirectVirtualMemory(eff_addr, true);
		*use_apt = true;
		break;
	}
	return eff_addr;
}


// Calculate internal Interrupt
/// <summary>
/// IIC - Internal Interrupt Code
///
/// This register will hold a code between 0 - 12 (oct), which will identify the internal source for the interrupt.
/// Priority encoded IID | IIE
/// </summary>
///
///                   (oct)
///      | IED bit  | IIC code |
///  ----+----------+----------+------------------------------------------------------------------------
///  n/a |   0      |    0     | Not assigned
///  MC  |   1      |    1     | Monitor Call
///  PV  |   2      |    2     | Protect Violation. Page number is found in the Paging Status Register.
///  PF  |   3      |    3     | Page fault. Page not in memory.
///  II  |   4      |    4     | lllegal instruction. Not implemented instruction.
///  Z   |   5      |    5     | Error indicator. The Z indicator is set.
///  PI  |   6      |    6     | Privileged instruction.
///  IOX |   7      |    7     | IOX error. No answer from external device.
///  PTY |   8      |    10    | Memory parity error
///  MOR |   9      |    11    | Memory out of range Addressing non-existent memory.
///  POW |   10     |    12    | Power fail interrupt
///  ----+----------+----------+------------------------------------------------------------------------
uint16_t calcIIC(void)
{
	uint16_t priorityCode = gIID & gIIE;
	if (priorityCode == 0)
		return 0;


	for (int i = 10; i >= 0; i--)
	{
		if ((priorityCode & (1 << i)) != 0)
		{
			return (uint16_t)i;
		}
	}
	return 0;
}





/*
 * Recalculate internal interrupt bits
 * Updates gIID, gPID and gPK
 */
void recalcInternalInterruptBits(void)
{
	// Check for Z (error) flag
	if (getbit(_STS, _Z))
	{
		gIID |= 1 << 5;
	}

	if ((gIID & gIIE) != 0)
	{
		// Set PID bit 14 to trigger LVL change to 14
		gPID |= (1 << 14);
		gIIC = calcIIC();
		gCHKIT = true; // removing this makes sintran crash during boot  // System malfunction. Sintran halt in ERRFATAL. L-reg: 042713
	}
}

// Calculate PK based on PID and PIE
void calcPK(void)
{
	// Recalculate PK based on PID and PIE
	int lvl;
	uint16_t i;
	gPK = 0;
	i = gPIE & gPID;

	if (i)
	{
		// Check for detected and enabled bits. Highest bits has highest priority
		for (lvl = 15; lvl >= 0; lvl--)
		{
			if (i & 1 << lvl)
			{
				gPK = lvl;
				return;
			}
		}
	}
}

/*
 * Internal interrupt setting routine.
 * IN: interrupt level and possible subbitfield
 * for those levels that has that. (LVL 14).
 */
void interrupt(uint16_t lvl, uint16_t sub)
{

	if (lvl == 14)
	{
		// Diagnostic: log internal interrupts that would cause TDTLEV ERRFATAL
		// SINTRAN expects internal interrupts only from levels 6-11 (RT program levels)
		// Levels 0-5 and 12-15 cause ERRFATAL
		//
		// static const char *iic_names[] = {
		// 	"n/a", "MC", "MPV", "PF", "II", "Z", "PI", "IOX", "PTY", "MOR", "POW"
		// };
		// int iic_bit = -1;
		// for (int b = 10; b >= 0; b--) {
		// 	if (sub & (1 << b)) { iic_bit = b; break; }
		// }
		// const char *iic_name = (iic_bit >= 0 && iic_bit <= 10) ? iic_names[iic_bit] : "?";
		//
		// // Log internal interrupts on device levels (12-15) that cause TDTLEV ERRFATAL
		// if (gPIL >= 12)
		// {
		// 		iic_name, sub, gPIL, gPC, gPVL);
		// }

		gIID |= sub;
		if (gIID & gIIE)
			gPID |= (1 << 14);
	}
	else
	{
		gPID |= (1 << lvl);
	}

	recalcInternalInterruptBits();

	// Check for MPV (bit 2), PF (bit 3), or illegal instruction (bit 4)
	if (lvl == 14 && (sub & ((1 << 2) | (1 << 3) | (1 << 4))))
	{
		if (g_cpu_trace)
			fprintf(stderr, "*** TRAP lvl=14 sub=%d(0x%x) P=%06o PGS=%04x PEA=%06o PIL=%d MMU=%d %s%s%s\n",
				sub, sub, gPC, gPGS, gPEA, gPIL, STS_PONI,
				(sub & (1<<2)) ? "MPV " : "",
				(sub & (1<<3)) ? "PF " : "",
				(sub & (1<<4)) ? "ILL " : "");
		if (ND100X_HOT_TRACE && Log_IsEnabled(LOG_CAT_TRAP, LOG_TRACE))
			Log_Write(LOG_CAT_TRAP, LOG_TRACE, "TRAP at P:[%6o], sub=%d", gPC, sub);
		longjmp(s_cpu_jmp_buf, 1); // Jump back to cpurun() in cpu_thread
	}
}

void device_interrupt(uint16_t interruptBits)
{
	last_device_irq_bits = interruptBits;

	// Only process bits 10-13 and 15 for device interrupts
	uint16_t validBits = interruptBits & 0xBC00; // Mask for bits 10-13,15 (0b1111010000000000)

	uint16_t tmp = gPID;

	// clear gIID bits 10-13,15
	gPID &= ~validBits;

	// set gIID bits from device(s)
	gPID |= validBits;

	if (tmp != gPID)
	{
		gCHKIT = true; // Check if we need to update PK based on new interrupts
	}
}


/*
 * Routine that handles phys mem writes and shadow memory.
 */
void PhysMemWrite(uint16_t value, uint32_t addr)
{
	WritePhysicalMemory(addr, value, false); // in cpu_mms.c
	return;


}

/*
 * Routine that handles phys mem reads and shadow memory.
 */
uint16_t PhysMemRead(uint32_t addr)
{
	return ReadPhysicalMemory(addr, false); // in cpu_mms.c

}

#ifdef WITH_DEBUGGER
/*
 * Common handling when a memory watchpoint matches.
 * With a debugger attached, enter CPU_BREAKPOINT so DAP can report and resume.
 * Without a debugger (CLI --watch), halt the machine like the -B breakpoint does.
 */
void cpu_watchpoint_triggered(uint32_t addr, bool isWrite)
{
	set_cpu_stop_reason(STOP_REASON_DATA_BREAKPOINT);
	set_cpu_run_mode(CPU_BREAKPOINT);
	if (!gDebuggerEnabled) {
		fprintf(stderr, "\n--- CPU stopped: watchpoint %s at %06o (PC=%06o) ---\n",
			isWrite ? "write" : "read", addr, gPC);
		/* Caller frame context: B, B[-1]=NNN (frame size), and a window
		 * so the offending .word NNN and arg-store offset can be read
		 * directly instead of reconstructed. D-space (UseAPT=true). */
		{
			int i;
			fprintf(stderr, "B=%06o  frame[B-2..B+4]:", gB);
			for (i = -2; i <= 4; i++)
				fprintf(stderr, " %06o",
					(unsigned short)ReadVirtualMemory((gB + i) & 0xFFFF, true));
			fprintf(stderr, "  (B[-1]=NNN)\n");
		}
		ring_dump();
		set_cpu_run_mode(CPU_SHUTDOWN);
	}
}
#endif

/*
 * Write a word to memory.
 * Here we implement all Memory Management System functions.
 */
void MemoryWrite(uint16_t value, uint16_t addr, bool UseAPT, unsigned char byte_select)
{
#ifdef WITH_DEBUGGER
	// Hot path: counter check -> bitmap check -> slow path
	// Cost when no watchpoints: 1 int compare (branch predictor: always not-taken)
	// Cost when watchpoints active but addr miss: + 1 byte load + 1 bit test
	if (g_watchpoint_count > 0
	    && (g_watchpoint_bitmap[addr >> 3] & (1 << (addr & 7)))
	    && (g_watchpoint_min_value == 0 || value >= (uint16_t)g_watchpoint_min_value)
	    && watchpoint_check_slow(addr, true, UseAPT)) {
		cpu_watchpoint_triggered(addr, true);
	}
#endif
	WriteVirtualMemory(addr, value, UseAPT, byte_select); // in cpu_mms.c
}

/*
 * Read a word from memory.
 * Here we implement all Memory Management System functions.
 */
uint16_t MemoryRead(uint16_t addr, bool UseAPT)
{
#ifdef WITH_DEBUGGER
	if (g_watchpoint_count > 0
	    && (g_watchpoint_bitmap[addr >> 3] & (1 << (addr & 7)))
	    && watchpoint_check_slow(addr, false, UseAPT)) {
		cpu_watchpoint_triggered(addr, false);
	}
#endif
	return ReadVirtualMemory(addr, UseAPT); // in cpu_mms.c
}

uint16_t MemoryFetch(uint16_t addr, bool UseAPT)
{
	return FetchVirtualMemory(addr, UseAPT); // in cpu_mms.c
}

/// @brief Check if we need to switch runlevel
/// @return Returns true if a switch was made, false otherwise
bool checkAndSwitch(void)
{
	if (gCHKIT)
	{
		gCHKIT = false; // reset flag

		// recalc internal interrupt bits
		recalcInternalInterruptBits();

		if (!STS_IONI)
			return false;

		calcPK();

		if (gPK != gPIL)
		{
			setPIL(gPK); /* Change to new runlevel */

			if (ND100X_HOT_TRACE && Log_IsEnabled(LOG_CAT_PKSWITCH, LOG_TRACE))
			{
				bool isRTC = ((gPVL == 13) || (gPIL == 13));
				if (!isRTC)
				{
					Log_Write(LOG_CAT_PKSWITCH, LOG_TRACE, "Switched from %d P[%6o] to %d P[%6o]",
					          gPVL, g_reg->reg[gPVL][_P], gPIL, g_reg->reg[gPIL][_P]);
					Log_Write(LOG_CAT_PKSWITCH, LOG_TRACE, "New pc after switch %6o", gPC);
				}
			}
			return true;
		}
	}
	return false;
}



// To reduce host load, we enable sleeping when the ND CPU is idle
// The ND CPU is idle when running in level 0 in SINTRAN.
// We detect that the CPU is idle by checking the gPIL register is == 0
// But we only activate sleep when the CPU is idle for a while, and after it has been in another PIL level to achieve a quick boot.
static uint16_t s_lvlcnt=0;
static bool s_activate_sleep = false;

// allocate once
uint16_t g_operand;

/// @brief CPU tick function - DO NOT CALL THIS DIRECT AS IT NEES setjmp() setup correctly
/// @details This function is called every CPU tick. It fetches the next instruction, executes it, and handles interrupts.
// The --trace line and the BSD kernel-stack high-water mark, both off
// unless asked for; kept out of private_cpu_tick() so the hot path stays short.
static void trace_before_exec(void)
{
	// CPU execution trace to stderr
	if (g_cpu_trace)
	{
		char disasm_str[128];
		OpToStr(disasm_str, sizeof(disasm_str), g_operand);
		fprintf(stderr, "%06o %06o %-24s PIL=%d prevPIL=%d A=%06o D=%06o T=%06o X=%06o B=%06o L=%06o P=%06o STS=%04x PIE=%04x PID=%04x IIE=%04x IID=%04x PGS=%04x MMU=%d INT=%d SEX=%d\n",
			gPC, g_operand, disasm_str,
			gPIL, (g_reg->reg_STS >> 8) & 0x0F,
			gA, gD, gT, gX, gB, gL, gPC,
			gSTSr, gPIE, gPID, gIIE, gIID, gPGS,
			STS_PONI, STS_IONI, STS_SEXI);
	}

	// BSD kernel-stack high-water: track deepest kernel frame pointer (B).
	if (g_bsd_debug && gPIL >= 2) {
		unsigned short b = gB;
		if (b >= BSD_KSTK_BASE && b < BSD_KSTK_TOP && b < s_bsd_kstk_min) {
			s_bsd_kstk_min = b;
			fprintf(stderr, "KSTKHW min=%06o used=%d\n",
				s_bsd_kstk_min, BSD_KSTK_TOP - s_bsd_kstk_min);
		}
	}
}

void private_cpu_tick(void)
{
    // Check for level shift (typically after an interrupt or WAIT instruction)
    checkAndSwitch();

    // Fetch next instruction
    g_reg->myreg_PFB = MemoryFetch(gPC, false); //TODO: Remove this  step?
    g_reg->myreg_IR = g_reg->myreg_PFB;

    g_operand = g_reg->myreg_IR;


    // Dissasemble ?
    if (g_disasm)
    {
        disasm_instr(gPC, g_operand);
    }

    trace_before_exec();

    // Check max instruction limit
	if (g_cpu_max_instr > 0 && g_instr_counter >= g_cpu_max_instr)
	{
		fprintf(stderr, "\n--- CPU stopped: max instruction count reached (%llu) ---\n",
			(unsigned long long)g_cpu_max_instr);
		ring_dump();
		set_cpu_run_mode(CPU_SHUTDOWN);
		return;
	}

	// Check breakpoint
	if (g_cpu_breakpoint_enabled && gPC == g_cpu_breakpoint_addr)
	{
		fprintf(stderr, "\n--- CPU stopped: breakpoint at %06o ---\n", g_cpu_breakpoint_addr);
		ring_dump();
		set_cpu_run_mode(CPU_SHUTDOWN);
		return;
	}

	if (gPIL>0)
		s_activate_sleep = true;


	if (s_activate_sleep)
	{
		// Check if we need to sleep
		s_lvlcnt = (gPIL == 0) ? (s_lvlcnt + 1) : 0;

		if (s_lvlcnt > 10000)
		{
#ifndef __EMSCRIPTEN__
			sleep_ms(1); // Sleep 1 ms - skip on WASM to avoid blocking browser
#endif
			s_lvlcnt = 0;
		}
	}

#ifdef WITH_DEBUGGER
	if (gDebuggerEnabled)
	{
		// Debugger need to build the stack-trace to be used for single stepping (step-out)
		debugger_build_stack_trace(gPC, g_operand);
	}
#endif

	// Execute instruction
	g_instr_counter++;
	do_op(g_operand, false);

#ifdef WITH_DEBUGGER
	// After JPL instruction, we need to update the entry point of the JPL instruction to be able to find the symbol for the stack frame
	if (gDebuggerEnabled)
	{
		// JPL instruction?
		if ((g_operand & 0xF800) == 0134000)
		{
			// We need to update the entry point of the JPL instruction to be able to find the symbol for the stack frame
			debugger_update_jpl_entrypoint(gPC);
		}
	}
#endif
}

/// @brief Helper function for debugger to check if the next instruction is a jump, conditional jump or skp
/// @return true if the next instruction is a jump, jaf, or similar, false otherwise
bool cpu_instruction_is_jump(void)
{
	uint16_t operand =  MemoryFetch(gPC, false);

	// JMP
	if ((operand & 0xF800) == 0124000) return true;

	// JPL

	// CJPs - Conditional jumps

	// JAP
	if ((operand & 0xFF00) == 0130000) return true;

	// JAN
	if ((operand & 0xFF00) == 0130400) return true;

	// JAZ
	if ((operand & 0xFF00) == 0131000) return true;

	// JAF
	if ((operand & 0xFF00) == 0131400) return true;

	// JPC
	if ((operand & 0xFF00) == 0132000) return true;

	// JNC
	if ((operand & 0xFF00) == 0132400) return true;

	// JXZ
	if ((operand & 0xFF00) == 0133000) return true;

	// JXN
	if ((operand & 0xFF00) == 0133400) return true;

	// SKP
	if ((operand & 0xF8C0) == 0140000) return true;


	return false;
}

/// @brief run the CPU for a number of ticks.
/// @details This function runs the CPU for a number of ticks. It handles interrupts and checks for level switches.
/* Ring buffer for last N instructions before exit */
#define RING_SIZE 65536
static struct {
    unsigned short pc;
    unsigned short opcode;
    unsigned char  pil;
    unsigned short a_reg;
    unsigned short sts;
    unsigned short pid;
    unsigned short pie;
    unsigned short iid;
    unsigned short iie;
    unsigned short devbits;  /* device interrupt bits from last IO_Tick */
} ring_buf[RING_SIZE];
static int ring_idx = 0;

static void ring_record(unsigned short pc, unsigned char pil, unsigned short opcode) {
    if (g_cpu_ring_dump_size <= 0) return;
    ring_buf[ring_idx].pc = pc;
    ring_buf[ring_idx].pil = pil;
    ring_buf[ring_idx].opcode = opcode;
    ring_buf[ring_idx].a_reg = gA;
    ring_buf[ring_idx].sts = gSTSr;
    ring_buf[ring_idx].pid = gPID;
    ring_buf[ring_idx].pie = gPIE;
    ring_buf[ring_idx].iid = gIID;
    ring_buf[ring_idx].iie = gIIE;
    ring_buf[ring_idx].devbits = last_device_irq_bits;
    ring_idx = (ring_idx + 1) % RING_SIZE;
}

void ring_dump(void) {
    int i;
    char disasm_str[128];

    if (g_cpu_ring_dump_size <= 0) return;

    int count = g_cpu_ring_dump_size;
    if (count > RING_SIZE) count = RING_SIZE;

    fprintf(stderr, "\r\n--- CPU state at exit ---\r\n");
    fprintf(stderr, "PIL=%d PC=%06o A=%06o D=%06o T=%06o X=%06o B=%06o L=%06o\r\n",
           gPIL, gPC, gA, gD, gT, gX, gB, gL);
    fprintf(stderr, "STS=%04x PID=%04x PIE=%04x IID=%04x IIE=%04x PVL=%d\r\n",
           gSTSr, gPID, gPIE, gIID, gIIE, gPVL);
    fprintf(stderr, "STS per-level: ");
    for (i = 0; i < 16; i++)
        fprintf(stderr, "[%d]=%03o ", i, g_reg->reg[i][0] & 0xFF);
    fprintf(stderr, "\r\n");
    fprintf(stderr, "PC per-level:  ");
    for (i = 0; i < 16; i++)
        fprintf(stderr, "[%d]=%06o ", i, g_reg->reg[i][_P]);
    fprintf(stderr, "\r\n");

    fprintf(stderr, "\r\n--- Last %d instructions before exit ---\r\n", count);
    fprintf(stderr, "  [  ] PIL    PC   OPCODE  DISASM                    A    STS   PID   PIE   IID   IIE  DEVBITS\r\n");
    /* Walk the ring from (ring_idx - count) to (ring_idx - 1), oldest first */
    for (i = 0; i < count; i++) {
        int idx = (ring_idx - count + i + RING_SIZE) % RING_SIZE;
        if (ring_buf[idx].pc || ring_buf[idx].pil) {
            OpToStr(disasm_str, sizeof(disasm_str), ring_buf[idx].opcode);
            fprintf(stderr, "  [%3d] %2d %06o %06o %-24s %06o %04x %04x %04x %04x %04x %04x\r\n",
                   i, ring_buf[idx].pil, ring_buf[idx].pc, ring_buf[idx].opcode,
                   disasm_str,
                   ring_buf[idx].a_reg, ring_buf[idx].sts,
                   ring_buf[idx].pid, ring_buf[idx].pie,
                   ring_buf[idx].iid, ring_buf[idx].iie,
                   ring_buf[idx].devbits);
        }
    }
}

/// @param ticks_arg Number of ticks to run the CPU. Use -1 for infinite.
/// @return Returns the number of ticks left to run.
int cpu_run(int ticks_arg)
{
	/* volatile: a fault longjmp()s back to the setjmp() below, and C11 7.13.2.1
	 * leaves a non-volatile local that changed since setjmp indeterminate. */
	volatile int ticks = ticks_arg;
#ifdef WITH_DEBUGGER
	static uint32_t dbg_poll_ctr = 0;   // emulated-instruction counter for async pause poll
	if (get_debugger_control_granted()) {
#ifndef __EMSCRIPTEN__
		sleep_ms(100); // Portable (POSIX nanosleep / Windows Sleep)
#endif
		return ticks; // Debugger has control, return immediately
	}
#endif

	// Set up longjmp target once at startup
	if (setjmp(s_cpu_jmp_buf) != 0)
	{
		// We had an interrupt (MPV, PF, or illegal instruction)
		// PGS bit 15 indicates if fault was during fetch (1) or data cycle (0)

		// PGS:
		//
		// if bit 15 is a one, the page fault or protection violation occurred during the fetch of an instruction.
		// In this case, the P register has not been incremented and the instruction causing the violation(and the restart point)
		//
		// If bit 15 is zero, the page fault or protection violation occurred during the data cycles of an instruction.
		// In this case, the P register points to the instruction after the instruction causing the internal hardware status interrupt.
		// When the cause of the internal hardware status interrupt has been removed, the restart point will be found by subtracting one from the P register.

		if (g_cpu_trace)
			fprintf(stderr, "*** FAULT RETURN PC=%06o PGS=%04x PEA=%06o PIL=%d MMU=%d\n",
				gPC, gPGS, gPEA, gPIL, STS_PONI);
		if (ND100X_HOT_TRACE && Log_IsEnabled(LOG_CAT_TRAP, LOG_TRACE))
			Log_Write(LOG_CAT_TRAP, LOG_TRACE, "CPU: Interrupt handler returned, PC=%06o, PGS=%04x", gPC, gPGS);
	}


	while (ticks !=0 )
	{
		CPURunMode current_run_mode = get_cpu_run_mode();

#ifdef WITH_DEBUGGER
		// Async pause is human-latency, so poll it on the emulated machine's own
		// timebase rather than per instruction or host wall-clock. 10550 instr is
		// one ~20ms RTC period (device_rtc.c TICKS_20MS); 10 periods ~= 200ms of
		// emulated time. Running faster than real-time only shortens the wall-clock
		// latency, never lengthens it, so this is robust to any throttle/"max" speed.
		//
		// This MUST live at the top of the loop, before private_cpu_tick(): a page
		// fault / protection violation longjmp()s back to the setjmp() target above
		// and re-enters the loop here, skipping the tail. A tight trap-14 page-fault
		// loop faults on every tick, so a tail-of-loop poll would never be reached
		// and pause would hang until it times out. Polling here is fault-loop-proof.
		#define DBG_POLL_INSTR (10550 * 10)
		if (gDebuggerEnabled && ++dbg_poll_ctr >= DBG_POLL_INSTR) {
			dbg_poll_ctr = 0;
			if (get_debugger_request_pause()) {
				// return ASAP, let the caller handle the debugger request
				return ticks;
			}
		}
#endif



		if (current_run_mode == CPU_RUNNING) // Including Normal and Paused (=debugger mode)
		{
			{
				uint16_t pre_pc = gPC;
				uint16_t pre_pil = gPIL;
				private_cpu_tick();
				ring_record(pre_pc, pre_pil, g_operand);
			}

			// Tick IO devices pr cpu tick
			IO_Tick();

			// CPU throttle: sleep if running ahead of target speed
			if (cpu_throttle_enabled) {
				static uint64_t throttle_start_ns = 0;
				static uint64_t throttle_instr_count = 0;
				#define THROTTLE_CHECK_INTERVAL 10550  // Check every RTC period

				if (throttle_start_ns == 0) {
					throttle_start_ns = throttle_get_ns();
				}
				throttle_instr_count++;

				if (throttle_instr_count >= THROTTLE_CHECK_INTERVAL) {
					uint64_t now_ns = throttle_get_ns();
					uint64_t elapsed_ns = now_ns - throttle_start_ns;
					// Target: THROTTLE_CHECK_INTERVAL instructions at cpu_throttle_mhz MHz
					uint64_t target_ns = (uint64_t)(THROTTLE_CHECK_INTERVAL * 1000.0 / cpu_throttle_mhz);

					if (elapsed_ns < target_ns) {
						uint64_t sleep_ns = target_ns - elapsed_ns;
						if (sleep_ns > 1000000) {  // > 1ms: use sleep
							sleep_ms((unsigned int)(sleep_ns / 1000000));
						}
						// Spin-wait remainder for precision
						while (throttle_get_ns() - throttle_start_ns < target_ns) {
							// spin
						}
					}
					throttle_start_ns = throttle_get_ns();
					throttle_instr_count = 0;
				}
			}

			if (ticks > 0)
			{
				ticks--;
			}

#ifdef WITH_DEBUGGER
			// PC-breakpoint check, gated like watchpoints: when nothing is armed
			// this is one compare; the hash walk in check_for_breakpoint() only
			// runs when gPC actually has a breakpoint (or a single-step is pending).
			if (gDebuggerEnabled
			    && (g_breakpoint_step_pending
			        || (g_breakpoint_entry_count > 0
			            && (g_breakpoint_bitmap[gPC >> 3] & (1 << (gPC & 7))))))
			{
				if (check_for_breakpoint() != STOP_REASON_NONE)
				{
					return ticks;
				}
			}
#endif
		}

        else if (current_run_mode == CPU_STOPPED)
        {
            printf("CPU: WAS STOPPED, SHUTTING DOWN\r\n");
			ring_dump();
			set_cpu_run_mode(CPU_SHUTDOWN);
			break;
        }
		else
		{
 			break;  // Exit loop if we are in any other state.
		}
	}

	return ticks;
}


/*
 * Selects the emulated CPU model from the ND100X_CPUTYPE environment variable.
 *
 * Accepted (case sensitive, matching the CpuType enum spelling):
 *   ND100, ND100CE, ND100CX, ND110, ND110CE, ND110CX, ND110PCX
 *
 * Unset or unrecognised leaves the compiled-in default (ND110CX) untouched.  This exists
 * so a machine's SINTRAN-/TPE-visible identity can be changed without a rebuild; it is
 * read once, from cpu_init(), BEFORE Setup_Instructions() builds the dispatch table.
 */
void cpu_set_type_from_env(void)
{
	const char *name = getenv("ND100X_CPUTYPE");

	if (name == NULL)
		return;

	if (strcmp(name, "ND100") == 0)
		g_current_cpu_type = ND100;
	else if (strcmp(name, "ND100CE") == 0)
		g_current_cpu_type = ND100CE;
	else if (strcmp(name, "ND100CX") == 0)
		g_current_cpu_type = ND100CX;
	else if (strcmp(name, "ND110") == 0)
		g_current_cpu_type = ND110;
	else if (strcmp(name, "ND110CE") == 0)
		g_current_cpu_type = ND110CE;
	else if (strcmp(name, "ND110CX") == 0)
		g_current_cpu_type = ND110CX;
	else if (strcmp(name, "ND110PCX") == 0)
		g_current_cpu_type = ND110PCX;
	else
		LOG(LOG_CAT_CPU, LOG_WARN, "Unknown ND100X_CPUTYPE '%s' - keeping the default", name);
}

void cpu_init(bool debuggerEnabled, int debuggerPort)
{
	/* initialize an empty register set (static storage: never freed, cannot fail) */
	static struct CpuRegs s_cpu_regs;
	memset(&s_cpu_regs, 0, sizeof(s_cpu_regs));
	g_reg = &s_cpu_regs;

	/* Initialize volatile memory to zero */
	memset(&g_volatile_memory, 0, sizeof(g_volatile_memory));

	setbit_STS_MSB(_N100, 1);
	gCSR = 1 << 2; /* this bit sets the cache as not available */

	/* Set cpu as running for now. Probably should depend on settings */
	set_cpu_run_mode(CPU_RUNNING);
	g_instr_counter = 0;

	// Allocate ShadowMemory for pagetables
	CreatePagingTables();

	/* Pick the CPU model BEFORE the dispatch table is built - it gates whole groups. */
	cpu_set_type_from_env();

	/*
	 * The VERSN identity (back-wiring PROM + microprogram/print version) depends on
	 * the CPU model, so it is reset AFTER cpu_set_type_from_env() and then given the
	 * chance to be overridden by the ND100X_* identity variables. See ndfunc_versn().
	 */
	cpu_versn_reset();
	cpu_versn_set_identity_from_env();

	/* OK lets set up the parsing for our current cpu before we start it. */
	Setup_Instructions();

	gALD = 01560; // oct 1560 (ALD position 4, Binary load from 1560) // Floppy

	gDebuggerEnabled = debuggerEnabled;
	gDebuggerPort = debuggerPort;

	if (g_disasm)
		disasm_setlbl(gPC);

}

/// @brief Initialize the CPU debugger
/// @details This function initializes the CPU debugger thread

void init_cpu_debugger(void)
{
#ifdef WITH_DEBUGGER
	if (!gDebuggerEnabled) return;
	breakpoint_manager_init();
	start_debugger();
#endif
}

/// @brief Reset the CPU
/// @details This function resets the CPU registers and memory. It also destroys the paging tables and recreates them.
void cpu_reset(void)
{

	/* Initialize volatile memory to zero */
	memset(&g_volatile_memory, 0, sizeof(g_volatile_memory));

	// Reset registers (preserve debugger state across reset)
#ifdef WITH_DEBUGGER
	bool saved_debugger_enabled = gDebuggerEnabled;
#endif
	memset(g_reg, 0, sizeof(struct CpuRegs));
#ifdef WITH_DEBUGGER
	gDebuggerEnabled = saved_debugger_enabled;
#endif

	setbit_STS_MSB(_N100, 1);
	gCSR = 1 << 2; /* this bit sets the cache as not available */

	// Destroy paging tables (they will be recreated when cpu is initialized)
	DestroyPagingTables();

	// Allocate ShadowMemory for pagetables
	CreatePagingTables();


	set_cpu_run_mode(CPU_RUNNING);
	g_instr_counter = 0;
}

/// @brief Cleanup the CPU
/// @details This function cleans up the CPU. It destroys the paging tables and stops the debugger thread.
void cleanup_cpu(void)
{
	// Destroy paging tables
	DestroyPagingTables();

#ifdef WITH_DEBUGGER
	if (gDebuggerEnabled)
    {
		stop_debugger_thread();
    }
#endif

}



/// @brief Request from the debugger to take control of the CPU
/// @param requested
void set_debugger_request_pause(bool requested)
{
#ifdef WITH_DEBUGGER
	#ifdef __EMSCRIPTEN__
		s_debugger_request_pause = requested;
	#elif defined(_WIN32)
		InterlockedExchange((volatile LONG *)&s_debugger_request_pause, (LONG)requested);
	#else
		atomic_store(&s_debugger_request_pause, requested);
	#endif
#endif
}

/// @brief Check if the debugger has requested to pause the CPU
/// @return true if the debugger has requested to pause the CPU, false otherwise
/// @details This function is used to check if the debugger has requested to pause the CPU.
bool get_debugger_request_pause(void) {
#ifdef WITH_DEBUGGER
	#ifdef __EMSCRIPTEN__
		return s_debugger_request_pause;
	#elif defined(_WIN32)
		return (CPURunMode)InterlockedCompareExchange(
			(volatile LONG *)&s_debugger_request_pause,
			0,  // Exchange value (ignored)
			0   // Comparand (ignored)
		);
	#else
		return atomic_load(&s_debugger_request_pause);
	#endif
#else
	return false;
#endif
}

/// @brief Set the debugger control granted flag
/// @param requested
/// @details This function is used to set the debugger control granted flag.
void set_debugger_control_granted(bool requested)
{
#ifdef WITH_DEBUGGER
	#ifdef __EMSCRIPTEN__
		s_debugger_control_granted = requested;
	#elif defined(_WIN32)
		InterlockedExchange((volatile LONG *)&s_debugger_control_granted, (LONG)requested);
	#else
		atomic_store(&s_debugger_control_granted, requested);
	#endif
#endif
}

/// @brief Check if the debugger control is granted
/// @details This function is used to check if the debugger control is granted.
/// @return  true if the debugger control is granted, false otherwise
bool get_debugger_control_granted(void) {
#ifdef WITH_DEBUGGER
	#ifdef __EMSCRIPTEN__
		return s_debugger_control_granted;
	#elif defined(_WIN32)
		return (CPURunMode)InterlockedCompareExchange(
			(volatile LONG *)&s_debugger_control_granted,
			0,  // Exchange value (ignored)
			0   // Comparand (ignored)
		);
	#else
		return atomic_load(&s_debugger_control_granted);
	#endif
#else
	return false;
#endif
}

/// @brief Set the debugger stop reason
/// @param reason The reason for stopping the cpu
void set_cpu_stop_reason(CpuStopReason reason) {
#ifdef WITH_DEBUGGER
	#ifdef __EMSCRIPTEN__
		s_cpu_stop_reason = reason;
	#elif defined(_WIN32)
		InterlockedExchange((volatile LONG *)&s_cpu_stop_reason, (LONG)reason);
	#else
		atomic_store(&s_cpu_stop_reason, reason);
	#endif
#endif
}


CpuStopReason get_cpu_stop_reason(void) {
#ifdef WITH_DEBUGGER
	#ifdef __EMSCRIPTEN__
		return (CpuStopReason)s_cpu_stop_reason;
	#elif defined(_WIN32)
		return (CpuStopReason)InterlockedCompareExchange(
			(volatile LONG *)&s_cpu_stop_reason,
			0,  // Exchange value (ignored)
			0   // Comparand (ignored)
		);
	#else
		return atomic_load(&s_cpu_stop_reason);
	#endif
#else
	return STOP_REASON_NONE;
#endif
}



void set_cpu_run_mode(CPURunMode new_mode) {
#ifdef WITH_DEBUGGER
	#ifdef __EMSCRIPTEN__
		s_cpu_run_mode = new_mode;
	#elif defined(_WIN32)
		InterlockedExchange((volatile LONG *)&s_cpu_run_mode, (LONG)new_mode);
	#else
		atomic_store(&s_cpu_run_mode, new_mode);
	#endif
#else
	CurrentCPURunMode = new_mode;
#endif
}


CPURunMode get_cpu_run_mode(void) {
#ifdef WITH_DEBUGGER
	#ifdef __EMSCRIPTEN__
		return (CPURunMode)s_cpu_run_mode;
	#elif defined(_WIN32)
		return (CPURunMode)InterlockedCompareExchange(
			(volatile LONG *)&s_cpu_run_mode,
			0,  // Exchange value (ignored)
			0   // Comparand (ignored)
		);
	#else
		return atomic_load(&s_cpu_run_mode);
	#endif
#else
	return CurrentCPURunMode;
#endif
}

// CPU throttle API
void cpu_throttle_set_enabled(bool enabled) {
	cpu_throttle_enabled = enabled;
	if (enabled) {
		LOG(LOG_CAT_CPU, LOG_INFO, "CPU throttle: ON (%.2f MHz)", cpu_throttle_mhz);
	} else {
		LOG(LOG_CAT_CPU, LOG_INFO, "CPU throttle: OFF (full speed)");
	}
}

bool cpu_throttle_get_enabled(void) {
	return cpu_throttle_enabled;
}

void cpu_throttle_set_mhz(double mhz) {
	if (mhz < 0.1) mhz = 0.1;
	if (mhz > 100.0) mhz = 100.0;
	cpu_throttle_mhz = mhz;
	LOG(LOG_CAT_CPU, LOG_INFO, "CPU throttle: target %.3f MHz", cpu_throttle_mhz);
}

double cpu_throttle_get_mhz(void) {
	return cpu_throttle_mhz;
}

