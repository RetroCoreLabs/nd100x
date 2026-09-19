/*
 * nd100em - ND100 Virtual Machine
 *
 *  Copyright (c) 2025 Ronny Hansen
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

#ifndef CPU_TYPES_H
#define CPU_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>

// Sleep function for Windows and Linux.
//
// On Windows, Sleep()'s granularity defaults to one system tick (~15.6ms),
// which badly distorts the CPU idle-loop pacing and starves the RTC
// interrupt - the emulated 20ms RTC tick then fires every ~300ms and TPE
// reports "The clock is not updated". We raise the timer resolution to 1ms
// once per process (via timeBeginPeriod on winmm) on the first sleep_ms
// call so Sleep(1) actually sleeps ~1ms.
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <mmsystem.h> /* timeBeginPeriod - requires linking winmm */
static inline void sleep_ms(unsigned int ms)
{
    static LONG period_raised = 0;
    if (InterlockedCompareExchange(&period_raised, 1, 0) == 0)
    {
        timeBeginPeriod(1);
    }
    Sleep(ms);
}
#else
#include <time.h> // for nanosleep
static inline void sleep_ms(unsigned int ms)
{
    struct timespec req = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};
    nanosleep(&req, NULL);
}
#endif


// Integer types: <stdint.h> only (house rule 5.2). The old ushort/uint/ulong
// aliases and the unused u8..s64 set were removed on 18-SEP-2026.


// Memory Management System configuration
// clang-format off
#define ENABLE_BREAKPOINTS    // Enable breakpoint support for debugging
#define _DEGRADE_             // Enable ring degradation on instruction fetch (ring 3 fetching
// clang-format on
// from a lower-ring page lowers the PCR ring instead of raising MPV).
// BEHAVIORAL flag, not a debug flag - the DEGRADE: diagnostic print
// in cpu_mms.c is separately switched (mms log category, see below).

/* Hot-path trace output in the CPU and MMS (log categories mms, mmsmap, trap,
 * pkswitch). The code is always compiled and type-checked; with 0 the compiler
 * removes it, so Release, WASM and RISC-V builds pay nothing (measured
 * 18-SEP-2026: 0 extra host instructions over a 100M-instruction SINTRAN boot).
 * CMake sets it from the ND100X_HOT_TRACE option (ON in native Debug builds). */
#ifndef ND100X_HOT_TRACE
#define ND100X_HOT_TRACE 0
#endif


/********************* MMU *********************/

// Page table flags
// clang-format off
#define PGU_FLAG (1 << 27)  // Bit 27 - Page used
#define WIP_FLAG (1 << 28)  // Bit 28 - Written In Page
// clang-format on

// Shadow RAM addresses
// clang-format off
#define SHADOW_RAM_NORMAL_MODE_4PT  0xFF00  // 177400
#define SHADOW_RAM_EXTENDED_MODE_4PT 0xFE00  // 177000
#define SHADOW_RAM_EXTENDED_MODE_16PT 0xF800 // 177400
// clang-format on

// Memory Management System types
// clang-format off
typedef enum {
    MMS1,   // 4 page tables
    MMS2    // 16 page tables - Type 2 is necessary for VSX (Virtual Storage Extended)
} MMSType;
// clang-format on

// Page table modes
// clang-format off
typedef enum {
    Four,    // 4 page tables
    Sixteen  // 16 page tables
} PageTableMode;
// clang-format on

// Memory access modes
typedef enum
{
    READ = 1 << 0,
    WRITE = 1 << 1,
    FETCH = 1 << 2,
    READ_FETCH = READ | FETCH
} AccessMode;

// Write modes
// clang-format off
typedef enum {
    WRITEMODE_MSB,    // Most significant byte
    WRITEMODE_LSB,    // Least significant byte
    WRITEMODE_WORD    // Full word
} WriteMode;
// clang-format on

// Paging Tables structure
// clang-format off
typedef struct {
    MMSType mmsType;           // What kind of MMS is this
    uint16_t* shadowRam;         // The Shadow RAM "chip"
    uint32_t shadowRamAddress;     // Start address of Shadow RAM
    uint16_t shadowRamSize;      // Size of shadow RAM array
    bool isInitialized;        // Whether the paging tables have been initialized
} PagingTables;
// clang-format on


extern MMSType g_mms_type;           // What MMS type is currently in use
extern PagingTables g_paging_tables; // Global paging tables structure

/********************* CPU *********************/

typedef void (*InstrFunc)(unsigned short);
extern InstrFunc g_instr_funcs[65536];


/* A complete listing of registers in a program level regbank including the 8 scratch regs. */
#define _STS 0
#define _D   1
#define _P   2
#define _B   3
#define _L   4
#define _A   5
#define _T   6
#define _X   7
#define _U0  8
#define _U1  9
#define _U2  10
#define _U3  11
#define _U4  12
#define _U5  13
#define _U6  14
#define _U7  15

/* A complete listing of privileged system registers */
/* Since some of them have the same "number" but are different */
/* Or even are part of some other register or take in bits from */
/* external sources, special care need to be taken when handling these */

#define PANS 0  /* Read */
#define PANC 0  /* Write */
#define STS  1  /* Read and write, but spread out over 16 levels too in the register file */
#define OPR  2  /* Read */
#define LMP  2  /* Write */
#define PGS  3  /* Read */
#define PCR  3  /* Write */
#define PVL  4  /* Read */
#define IIC  5  /* Read */
#define IIE  5  /* Write */
#define PID  6  /* Read and write */
#define PIE  7  /* Read and write */
#define CSR  8  /* Read */
#define CCL  8  /* Write */
#define ACTL 9  /* Read */
#define LCIL 9  /* Write */
#define ALD  10 /* Read */
#define UCIL 10 /* Write */
#define PES  11 /* Read */
#define PGC  12 /* Read */
#define PEA  13 /* Read */
#define ECCR 13 /* Write */

/*
 * PANS
 *
 * | 15 | 14 | 13 | 12 | 11 | 10 |  9 |  8 |  7 |  6 |  5 |  4 |  3 |  2 |  1 |
 * +----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
 * |DISP|INP |RPAN|PAN |  0 |    PFUNC     |             RPAN                 |
 * |PRES|PDY |VAL |INT |    |              |                                  |
 * +----+----+----+----+----+--------------+----+----+----+----+----+----+----+
 *
 */

/*
 * PANC
 *
 * | 15 | 14 | 13 | 12 | 11 | 10 |  9 |  8 |  7 |  6 |  5 |  4 |  3 |  2 |  1 |
 * +----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
 * |  0 |  0 |READ|N.A.|  0 |    PFUNC     |             WPAN                 |
 * |    |    | RQ |    |    |              |                                  |
 * +----+----+----+----+----+--------------+----+----+----+----+----+----+----+
 *
 */


/* Status register flags */

#define _PTM  0
#define _TG   1
#define _K    2
#define _Z    3
#define _Q    4
#define _O    5
#define _C    6
#define _M    7
#define _PL   8
#define _N100 12
#define _SEXI 13
#define _PONI 14
#define _IONI 15


#define PAGINGSYSTEM   0
#define OPERATORSPANEL 0

/*************************************************/
/* NEW ORGANIZATION OF MEMORY AND REGISTERS!!    */
/*************************************************/

/* Lets use the full 16MWord space now (32MB ram in host)*/
//#define MEMPTSIZE 16384

// Backing store is sized to the MAXIMUM supported memory (8 MWords = 16 MBytes,
// the top of the byte-addressable space). The amount actually INSTALLED is the
// runtime variable ND_Memsize (see below), configurable 1..16 MB via --memory /
// memory=<MB>. MEMPTSIZE is in KWords: 8192 KW = 8 MW = 8388608 words = 16 MB.
// (Old note: MEMPTSIZE 16384 = 16 MW = 32 MB tripped a 'CONFIG' detection bug
// "Total memory size....: 65504.000 Mbytes"; capping the INSTALLED size via
// ND_Memsize - not the backing array - is what keeps CONFIG's probe correct.)
#define MEMPTSIZE 8192

/* Volatile Memory
 * Backing store fixed at MEMPTSIZE KWords (the 16 MB maximum); the installed
 * size that the CPU/probe honour is ND_Memsize.
 */
typedef union ndram
{
    unsigned char c_Array[MEMPTSIZE * 1024 * 2];
    uint16_t n_Array[MEMPTSIZE * 1024];
    uint16_t n_Pages[MEMPTSIZE][1024];
} _NDRAM_;


// Installed main-memory size, in 16-bit WORDS. Runtime-configurable 1..16 MB
// (words = MB * 524288); DEFAULT 4 MB = 2097152 words. Set ONCE at start-up
// (from --memory / the .ini memory= key) BEFORE any ND_Memsize-dependent
// allocation (ECC latch, MMS), and never changed afterwards. Every physical
// access guards on `addr >= ND_Memsize` so the extra backing above the installed
// size reads as unmapped (MOR) - which is how the memory-size probe stops here.
extern uint32_t g_nd_memsize;

// Absolute maximum installed size (words) = the whole backing array. Used to
// bound-check a configured value at start-up.
#define ND_MEMSIZE_MAX_WORDS ((uint32_t)(MEMPTSIZE * 1024))

// Words per megabyte: 1 MB = 1048576 bytes = 524288 words.
#define ND_WORDS_PER_MB 524288u


/*
 * ND-100 physical-memory TYPE identification (LOCAL vs shared / MPM).
 *
 * On a real ND-100 every physical memory bank is one of several KINDS, and
 * SINTRAN must know which is which.  The kind matters because:
 *   - Only LOCAL ND-100 memory (KMECCR) is ECC/parity checked (the ECCR error-
 *     correction network lives on the local memory modules), and
 *   - SINTRAN "selects the first MPM5 memory for its shared memory" (the memory
 *     shared with an attached ND-500/ND-5000 through the 3022/5015 interface).
 *
 * SINTRAN builds a per-bank MEMARRAY of these codes at start-up: it initially
 * marks every found bank as MPM5, then PROBES each page (OPPSTART: RETU / MPM3MAP
 * / MPM4MAP) - a page that responds to the ECCR error-correction network via
 * TRR ECCR + the internal ECC status register is reclassified as LOCAL (KMECCR),
 * MPM3 pages via IOX 751, MPM4 ports via IOX 100200.., the ECC controller
 * presence via IOX 100115.  Whatever is left stays MPM5.  The MON MEMORY-
 * CONFIGURATION info reads MEMARRAY back (RP-P2-CONFG: MEMCON), and FN5MEM/FMPM5
 * pick the first MPM5 page as the ND-500 shared window.
 *
 * These codes are the SINTRAN K-symbols (KMECCR/KMPM5/...) as used by RetroCore's
 * ND100Memory.MemoryType byte; we keep the same numeric values so the two
 * emulators agree.  nd100x currently models only LOCAL RAM and (as a documented
 * stub) the ND-500 MPM5 window; the other kinds are defined for completeness.
 */
// clang-format off
typedef enum {
	ND_MEM_NONE   = 0x00, // Unmapped / not memory
	ND_MEM_MPM5   = 0x04, // KMPM5  - MPM-5 multiport (ND-500/ND-5000 shared memory)
	ND_MEM_LOCAL  = 0x08, // KMECCR - Local ND-100 memory (ECC/parity checked)
	ND_MEM_PIOC   = 0x02, // KMPIOC - PIOC memory        (not modelled here)
	ND_MEM_MPM3   = 0x05, // KMPM3  - MPM-3 multiport     (not modelled here)
	ND_MEM_MPM4   = 0x06  // KMPM4  - MPM-4 multiport     (not modelled here)
} NDMemoryType;
// clang-format on

/*
 * ND-500 shared (MPM5) window as seen from the ND-100 side.
 *
 * RetroCore places Port-A of the 3022/5015 multiport memory at ND-100 physical
 * BYTE address 0x00420000 (physical page 0x420), 8 MB long.  nd100x is natively
 * WORD-addressed, so the same window in WORD address space is:
 *     base = 0x00420000 >> 1 = 0x00210000 words
 *     size = 0x00800000 >> 1 = 0x00400000 words (4 MW / 8 MB)
 *
 * NOTE: with the default installed memory (ND_Memsize = 4 MW = 0x200000 words)
 * this window sits entirely ABOVE local RAM, so nd100x does not yet back it with
 * a device - it is a documented STUB used only for TYPE classification.  When an
 * ND-500 interface is ported it should register real backing over this range;
 * the classifier already reports it as ND_MEM_MPM5 so the ECC path skips it.
 */
// clang-format off
#define ND_MPM5_WINDOW_START_WORD  0x00210000u   // ND-100 word address of MPM5 base
#define ND_MPM5_WINDOW_SIZE_WORD   0x00400000u   // 4 MW (8 MB) MPM5 window
// clang-format on


// clang-format off
struct CpuRegs {
	uint16_t	reg[16][16];	/* main CPU registers for all runlevels */

	uint16_t	reg_STS;	/* STS register HIGH bits - not unique pr runlevel - used to be in reg[0][_STS]*/

	uint16_t	reg_PANS;	/* */
	uint16_t	reg_PANC;	/* */
	uint16_t	reg_OPR;	/* */
	uint16_t	reg_LMP;	/* */
	uint16_t	reg_PGS;	/* */
	uint16_t	reg_PCR[16];	/* Paging Control Registers */
	uint16_t	reg_PVL;	/* */
	uint16_t	reg_IIC;	/* IIC is actually just a priority encoded (IID | IIE) */
	uint16_t	reg_IID;	/* Actual interrupt reg */
	uint16_t	reg_IIE;	/* */
	uint16_t	reg_PID;	/* */
	uint16_t	reg_PIE;	/* */
	uint16_t	reg_CSR;	/* */
	uint16_t	reg_CCL;	/* */
	uint16_t	reg_LCIL;	/* */
	uint16_t	reg_ALD;	/* */
	uint16_t	reg_UCIL;	/* */
	uint16_t	reg_PES;	/* */
	uint16_t	reg_PGC;	/* */
	uint16_t	reg_PEA;	/* */
	uint16_t	reg_ECCR;	/* */
	uint16_t	reg_ECBits;	/* Simulated ECC latch (store-on-write); see cpu_mms.c ECC block */

	/*
	 * ND-110 "global pointers" (the S3SEG / SINTRAN-III segment-handling group).
	 *
	 * These three internal registers are written by WGLOB (140500) and read back by
	 * RGLOB (140501); every one of the ND-110 core-map / segment-table instructions
	 * (INSPL, REMPL, CNREK, CLPT, ENPT, REPT and the LASB/LACB/... bank group) uses
	 * them as the *implicit* base of its physical accesses.  See ND-06.026.1 EN
	 * (ND-110 Functional Description) p.196 and RetroCore
	 * Emulated.HW/ND/CPU/ND100/Instructions.ND110Specific.cs (WGLOB/RGLOB).
	 *
	 * They are NOT per-runlevel: there is exactly one set for the whole CPU.
	 */
	uint16_t	reg_STBNK;	/* Bank number of the segment table  (written from T by WGLOB) */
	uint16_t	reg_STSRT;	/* Start address of the segment table within that bank (from A; must be /8) */
	uint16_t	reg_CMBUK;	/* Bank number of the core-map table (written from D by WGLOB) */

	/* Personally Added to do Prefetch and Instruction more alike ND */
	uint16_t	myreg_IR;	/* InstructionRegister */
	uint16_t	myreg_PFB;	/* PrefetchBuffer */

	// Calculated EA and pagetable info (updated before opcode is executed)
	uint16_t effectiveAddress;
	bool useAPT;

	/* "locks" for registers that according to manual works that way (PES, PGS, IIC) */
	/* 1 = "locked" */
	/* :TODO: Check if PEA and PES should have a common lock */
	bool	mylock_PEA;
	bool	mylock_PES;
	bool	mylock_PGS;


	/* taking a shortcut by creating a PK 4bit register */
	/* always modify this as well when touching PID or PIE */
	uint16_t	myreg_PK;

	// should cpu levels be checked ?
	bool    chkit;

	/* For MOPC/OPCOM tracing and breakpoint functionality */
	/* counter for semirun mode*/
	bool	has_instr_cntr;
	uint16_t	instructioncounter;
	/* flag for breakpoint and breakpoint address */
	bool	has_breakpoint;
	uint16_t	breakpoint;

	// Debugger enabled flag
	bool	debugger_enabled;
	// Debugger port
	int	debugger_port;
};
// clang-format on


// clang-format off
typedef enum {
	CPU_UNKNOWN_STATE, // Unknown state
	CPU_RUNNING, // CPU is running normally
	CPU_BREAKPOINT, // CPU hit a breakpoint
	CPU_PAUSED,  // CPU is paused and waiting for debugger to resume
	CPU_STOPPED,    // CPU is stopped and we are in OPCOM mode
	CPU_SHUTDOWN // Shut down and exit
}  CPURunMode;
// clang-format on

typedef enum
{
    ND1,
    ND4,
    ND10,
    ND100,
    ND100CE,
    ND100CX,
    ND110,
    ND110CE,
    ND110CX,
    ND110PCX,
    ND120CX
} CpuType;

/* Which floating point unit is installed. The 32-bit single-precision FPP was
 * a factory option; an ND-100 or ND-110 could ship with either, independent of
 * the CPU model. Software detects which one at runtime (SAT 0 / SAA 1 / NLZ 20:
 * if T changed it is the 48-bit FPP). */
// clang-format off
typedef enum {
	FPP32,   /* optional 32-bit single precision FPP (T register unused) */
	FPP48    /* standard 48-bit FPP (T,A,D floating accumulator)         */
} FppType;
// clang-format on

#define gPC g_reg->reg[gPIL][_P]
#define gA  g_reg->reg[gPIL][_A]
#define gT  g_reg->reg[gPIL][_T]
#define gB  g_reg->reg[gPIL][_B]
#define gD  g_reg->reg[gPIL][_D]
#define gX  g_reg->reg[gPIL][_X]
#define gL  g_reg->reg[gPIL][_L]

#define gPANC   g_reg->reg_PANC
#define gPANS   g_reg->reg_PANS
#define gOPR    g_reg->reg_OPR
#define gLMP    g_reg->reg_LMP
#define gPGS    g_reg->reg_PGS
#define gPVL    g_reg->reg_PVL
#define gIIC    g_reg->reg_IIC
#define gIID    g_reg->reg_IID
#define gIIE    g_reg->reg_IIE
#define gPID    g_reg->reg_PID
#define gPIE    g_reg->reg_PIE
#define gCSR    g_reg->reg_CSR
#define gCCL    g_reg->reg_CCL
#define gLCIL   g_reg->reg_LCIL
#define gALD    g_reg->reg_ALD
#define gUCIL   g_reg->reg_UCIL
#define gPES    g_reg->reg_PES
#define gPGC    g_reg->reg_PGC
#define gPEA    g_reg->reg_PEA
#define gECCR   g_reg->reg_ECCR
#define gECBits g_reg->reg_ECBits

/* ND-110 global pointers - see the reg_STBNK/reg_STSRT/reg_CMBUK comment above. */
#define gSTBNK g_reg->reg_STBNK
#define gSTSRT g_reg->reg_STSRT
#define gCMBUK g_reg->reg_CMBUK


#define gPEA_Lock g_reg->mylock_PEA
#define gPES_Lock g_reg->mylock_PES
#define gPGS_Lock g_reg->mylock_PES
#define gIIC_Lock g_reg->mylock_IIC


#define CurrLEVEL ((g_reg->reg_STS & 0x0f00) >> 8)
#define gPIL      ((g_reg->reg_STS & 0x0f00) >> 8)

/* Highest runlevel with PIE AND PID bits both set */
#define gPK g_reg->myreg_PK

/* Should CPU levels be checked ? */
#define gCHKIT g_reg->chkit

/* The complete Status register both MSB and LSB for current runlevel. Read only MACRO */
#define gSTSr ((g_reg->reg_STS & 0xFF00) | (g_reg->reg[gPIL][_STS] & 0x00FF))

#define InstructionRegister g_reg->myreg_IR
#define PrefetchBuffer      g_reg->myreg_PFB

// clang-format off
#define gEA                 g_reg->effectiveAddress
#define gUseAPT             g_reg->useAPT
// clang-format on

// clang-format off
#define STS_PTM  ((g_reg->reg[gPIL][_STS]>>0) & 0x01)	/* */
#define STS_TG   ((g_reg->reg[gPIL][_STS]>>1) & 0x01)	/* */
#define STS_K    ((g_reg->reg[gPIL][_STS]>>2) & 0x01)	/* */
#define STS_Z    ((g_reg->reg[gPIL][_STS]>>3) & 0x01)	/* */
#define STS_Q    ((g_reg->reg[gPIL][_STS]>>4) & 0x01)	/* */
#define STS_O    ((g_reg->reg[gPIL][_STS]>>5) & 0x01)	/* */
#define STS_C    ((g_reg->reg[gPIL][_STS]>>6) & 0x01)	/* */
#define STS_M    ((g_reg->reg[gPIL][_STS]>>7) & 0x01)	/* */
// clang-format on

#define STS_PL   ((g_reg->reg_STS >> 8) & 0x0F)  /* Program runlevel */
#define STS_N100 ((g_reg->reg_STS >> 12) & 0x01) /* Nord 100 indicator */
#define STS_SEXI                                                                                   \
    ((g_reg->reg_STS >> 13) &                                                                      \
     0x01) /* Extended MMS adressing on/off indicator (24 bit instead of 19 bit*/
#define STS_PONI ((g_reg->reg_STS >> 14) & 0x01) /* Memory management on/off indicator */
#define STS_IONI ((g_reg->reg_STS >> 15) & 0x01) /* Interrupt system on/off indicator */

#define gDebuggerEnabled g_reg->debugger_enabled
#define gDebuggerPort    g_reg->debugger_port

/*

ALD SWITCH

+--------+------------------+-------------------+-----------------------------------------------------------------------
|SWITCH  | ALD VECTOR (hex) | ALD VALUE (octal) | DESCRIPTION
+--------+------------------+-------------------+-----------------------------------------------------------------------
|15      |     x0           | 0                 | (Note 2)
|14      |     x1           | 1560              | Switch setting 14 -  BPUN load from floppy (1560) and run (*3)
|13      |     x2           | 20500             | Bootstrap load from Winchester disk (500) and run (*3)
|12      |     x3           | 21540             | Bootstrap load from SMD disk (1540,) and run (*3)
|11      |     x4           | 400               | BPUN load from paper tape (400) and run (*3)
|10      |     x5           | 1600              | BPUN load from HDLC (1600) and run (*3)
|9       |     x6           | 21560             | Run (*3) (No load)
|8       |     x7           | 0                 | Run (*3) (No load)
|7       |     x8           | 100000            | (Note 2)
|6       |     x9           | 101560            | Binary load from 1560 (SCSI boot use this setting..?)
|5       |     xA           | 120500            | Mass storage from 500
|4       |     xB           | 121540            | Mass storage from 1540 (SMD disk)
|3       |     xC           | 100400            | Binary load from 400 (paper tape reader)
|2       |     xD           | 101600            | Switch setting 2 -  Binary load from 1600 (HDLC)
|1       |     xE           | 121560            |
|0       |     xF           | 100000            |
+--------+------------------+-------------------+-----------------------------------------------------------------------
*/


//********** Disassembly **********

struct disasm_entry
{
    bool isdata;
    bool iscode;
    int labelno;
    bool use_rel;
    int rel_acc_lbl;
    char asm_str[32];
    bool isexr;
    char exr[32];
    uint16_t theword;
};

typedef struct disasm_entry *DisasmArray[65536];
extern DisasmArray g_disasm_arr;
extern DisasmArray *g_dis;


// Global CPU variable definitions
extern struct CpuRegs *g_reg;
extern _NDRAM_ g_volatile_memory;
extern CpuType g_current_cpu_type;
extern FppType g_current_fpp_type;

extern uint64_t g_instr_counter;
extern uint16_t g_start_addr;
extern int g_disasm;

/* Called by the CPU but defined in other modules, whose prototypes are only
 * in their generated *_protos.h. Declared here so the defining files, which
 * include this header, are checked against the same signature. */
uint16_t io_op(uint16_t ioadd, uint16_t regA); /* machine/io.c */
int IO_Ident(uint16_t level);                  /* machine/io.c */
void start_debugger(void);                     /* debugger/debugger.c */

/* Set by device DMA (devices/device.c) around a transfer so the shadow-RAM
 * check in cpu_mms.c is skipped: DMA is a physical bus access. */
extern bool g_dma_access;
extern int g_cpu_exit_code;
extern int g_cpu_trace;
extern int g_bsd_debug;
extern uint64_t g_cpu_max_instr;
extern int g_cpu_breakpoint_enabled;
extern uint16_t g_cpu_breakpoint_addr;
extern int g_cpu_ring_dump_size;

/*
 * ND-110 diagnostic trace sink (see cpu.c do_op()).  NULL when tracing is off.
 * Enabled by --trace-nd110; redirected to a side file by --trace-nd110=FILE
 * so that console-driven sessions (TPE, the SINTRAN SMD boot) keep a clean screen buffer.
 * Declared here and NOT in cpu_protos.h - that header is auto-generated from the .c files.
 */
extern FILE *g_nd110_trace_fp;


//********** Breakpoints **********

#define HASH_SIZE 256 // adjust depending on address space

typedef enum
{
    BT_NONE = 0,
    BP_TYPE_USER,
    BP_TYPE_TEMPORARY,
    BP_TYPE_FUNCTION,
    BP_TYPE_DATA,
    BP_TYPE_INSTRUCTION
} BreakpointType;

// clang-format off
typedef struct BreakpointEntry {
    uint16_t address;
    BreakpointType type;
    char* condition;     // expression string (NULL if none)
    char* hitCondition;  // numeric string or expression (NULL if none)
    char* logMessage;    // log message (NULL if none)
    int hitCount;        // internal counter
    struct BreakpointEntry* next;
} BreakpointEntry;
// clang-format on

typedef struct
{
    BreakpointEntry *buckets[HASH_SIZE];

    // Number of instructions to step (for single stepping when we cant set a breakpoint)
    int step_count;

    // Address of last breakpoint hit (for DAP hitBreakpointIds)
    uint16_t last_hit_address;
    bool last_hit_valid;
} BreakpointManager;

/* The breakpoint manager (cpu_bkpt.c); NULL until first use. */
extern BreakpointManager *g_breakpoint_mgr;

//********** Watchpoints (memory access breakpoints) **********

#define MAX_WATCHPOINTS 32

// clang-format off
typedef enum {
    WATCH_NONE      = 0,
    WATCH_READ      = 1,
    WATCH_WRITE     = 2,
    WATCH_READWRITE = 3
} WatchpointType;
// clang-format on

// clang-format off
typedef enum {
    WATCH_SPACE_ANY    = 0,  // Fire on any access (backward compatible default)
    WATCH_SPACE_ISPACE = 1,  // Fire only on I-space access (UseAPT=false)
    WATCH_SPACE_DSPACE = 2   // Fire only on D-space access (UseAPT=true)
} WatchpointSpace;
// clang-format on

// clang-format off
typedef struct {
    uint16_t address;
    WatchpointType type;
    WatchpointSpace space;   // I-space, D-space, or any
    int8_t pil;              // -1 = any PIL, 0-15 = specific PIL only
    bool active;
} WatchpointEntry;
// clang-format on

// Extern globals for hot-path access from cpu.c (defined in cpu_bkpt.c)
extern int g_watchpoint_count;
extern int g_watchpoint_skip_hits;        // --watch-skip: ignore first N hits before halting
extern int g_watchpoint_min_value;        // --watch-min-value: WRITE triggers only if value >= this
extern uint8_t g_watchpoint_bitmap[8192]; // 64K addresses, 1 bit each

// PC-breakpoint hot-path gates (defined in cpu_bkpt.c). Mirror the watchpoint
// design so check_for_breakpoint() costs ~1 compare when nothing is armed and
// only does the hash walk when gPC actually has a breakpoint.
extern int g_breakpoint_entry_count;      // live breakpoint entries (any type)
extern int g_breakpoint_step_pending;     // nonzero while a single-step is in flight
extern uint8_t g_breakpoint_bitmap[8192]; // 1 bit per 16-bit PC address

//********** Physical Watchpoints (physical memory address breakpoints) **********

// clang-format off
typedef struct {
    uint32_t address;          // Physical address (21+ bits for extended memory)
    WatchpointType type;
    int8_t pil;                // -1 = any PIL, 0-15 = specific PIL only
    bool active;
} PhysicalWatchpointEntry;
// clang-format on

// Physical-watchpoint hot-path gate (defined in cpu_bkpt.c). Page-granular
// pre-filter: 1 bit per 1K-word page, masked into a small L1-resident map.
// Aliasing only yields false positives, which fall through to the exact scan.
#define PHYS_WP_BITMAP_BYTES 4096 // covers 2^15 pages = 32M-word phys space without aliasing
extern int g_phys_watchpoint_count;
extern uint8_t g_phys_watchpoint_pagemap[PHYS_WP_BITMAP_BYTES];

static inline int phys_watchpoint_page_armed(uint32_t addr)
{
    uint32_t idx = (addr >> 10) & (PHYS_WP_BITMAP_BYTES * 8u - 1u);
    return g_phys_watchpoint_pagemap[idx >> 3] & (1u << (idx & 7u));
}

/// @brief Enumeration of CPU stop reasons for the debugger
/// @details This enum is used to indicate the reason for stopping the CPU in the debugger. - aligned with DAP spec
typedef enum
{
    STOP_REASON_NONE,
    STOP_REASON_STEP,
    STOP_REASON_BREAKPOINT,
    STOP_REASON_EXCEPTION,
    STOP_REASON_PAUSE,
    STOP_REASON_ENTRY,
    STOP_REASON_GOTO,
    STOP_REASON_FUNCTION_BREAKPOINT,
    STOP_REASON_DATA_BREAKPOINT,
    STOP_REASON_INSTRUCTION_BREAKPOINT
} CpuStopReason;

#endif // CPU_TYPES_H
