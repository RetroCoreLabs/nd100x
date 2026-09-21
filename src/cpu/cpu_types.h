/*
 * cpu_types.h - CPU types, register macros, constants and shared CPU declarations.
 *
 * nd100em - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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


/*
 * Bit positions in the status register (STS). These are bit NUMBERS, for
 * shifting and for getbit/setbit; the STS_..._IS_SET macros further down read
 * the value of the same bit.
 *
 * The two halves of STS have different scope, which is easy to get wrong:
 *
 *   bits 0-7   per-level condition flags. Part of each program level's own
 *              register bank, saved and restored when the level changes.
 *   bits 8-15  machine state, global to the CPU. These do NOT change when
 *              the level changes. The kernel sets them once at boot (SEX,
 *              PON, ION) and they stay set. They cannot be changed while
 *              memory management is on except from ring 3.
 *
 * Bits 8-11 hold the current program level, so STS_PROGRAM_LEVEL is the
 * position of that 4-bit field, not a single flag.
 */
#define STS_PAGE_TABLE_MODE     0  /* 0 = main page table, 1 = alternative */
#define STS_FLOAT_OVERFLOW      1  /* TG, floating point overflow */
#define STS_BIT_ACCUMULATOR     2  /* K, the single bit accumulator */
#define STS_ERROR_INDICATOR     3  /* Z */
#define STS_DYNAMIC_OVERFLOW    4  /* Q */
#define STS_STATIC_OVERFLOW     5  /* O */
#define STS_CARRY               6  /* C */
#define STS_SHIFT_OUT           7  /* M, the bit shifted out */
#define STS_PROGRAM_LEVEL       8  /* bits 8-11, current level 0-15 */
#define STS_ND100_INDICATOR     12 /* always 1 on an ND-100 */
#define STS_EXTENDED_ADDRESSING 13 /* SEXI: 24-bit physical, 16 page tables */
#define STS_PAGING_ON           14 /* PONI: memory management enabled */
#define STS_INTERRUPT_ON        15 /* IONI: interrupt system enabled */


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
typedef union Ndram
{
    unsigned char c_Array[MEMPTSIZE * 1024 * 2];
    uint16_t n_Array[MEMPTSIZE * 1024];
    uint16_t n_Pages[MEMPTSIZE][1024];
} Ndram;


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
    uint16_t    reg[16][16];    /* main CPU registers for all runlevels */

    uint16_t    reg_STS;    /* STS register HIGH bits - not unique pr runlevel - used to be in reg[0][_STS]*/

    uint16_t    reg_PANS;   /* */
    uint16_t    reg_PANC;   /* */
    uint16_t    reg_OPR;    /* */
    uint16_t    reg_LMP;    /* */
    uint16_t    reg_PGS;    /* */
    uint16_t    reg_PCR[16];    /* Paging Control Registers */
    uint16_t    reg_PVL;    /* */
    uint16_t    reg_IIC;    /* IIC is actually just a priority encoded (IID | IIE) */
    uint16_t    reg_IID;    /* Actual interrupt reg */
    uint16_t    reg_IIE;    /* */
    uint16_t    reg_PID;    /* */
    uint16_t    reg_PIE;    /* */
    uint16_t    reg_CSR;    /* */
    uint16_t    reg_CCL;    /* */
    uint16_t    reg_LCIL;   /* */
    uint16_t    reg_ALD;    /* */
    uint16_t    reg_UCIL;   /* */
    uint16_t    reg_PES;    /* */
    uint16_t    reg_PGC;    /* */
    uint16_t    reg_PEA;    /* */
    uint16_t    reg_ECCR;   /* */
    uint16_t    reg_ECBits; /* Simulated ECC latch (store-on-write); see cpu_mms.c ECC block */

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
    uint16_t    reg_STBNK;  /* Bank number of the segment table  (written from T by WGLOB) */
    uint16_t    reg_STSRT;  /* Start address of the segment table within that bank (from A; must be /8) */
    uint16_t    reg_CMBUK;  /* Bank number of the core-map table (written from D by WGLOB) */

    /* Personally Added to do Prefetch and Instruction more alike ND */
    uint16_t    myreg_IR;   /* InstructionRegister */
    uint16_t    myreg_PFB;  /* PrefetchBuffer */

    // Calculated EA and pagetable info (updated before opcode is executed)
    uint16_t effectiveAddress;
    bool useAPT;

    /* "locks" for registers that according to manual works that way (PES, PGS, IIC) */
    /* 1 = "locked" */
    /* :TODO: Check if PEA and PES should have a common lock */
    bool    mylock_PEA;
    bool    mylock_PES;
    bool    mylock_PGS;


    /* taking a shortcut by creating a PK 4bit register */
    /* always modify this as well when touching PID or PIE */
    uint16_t    myreg_PK;

    // should cpu levels be checked ?
    bool    chkit;

    /* For MOPC/OPCOM tracing and breakpoint functionality */
    /* counter for semirun mode*/
    bool    has_instr_cntr;
    uint16_t    instructioncounter;
    /* flag for breakpoint and breakpoint address */
    bool    has_breakpoint;
    uint16_t    breakpoint;

    // Debugger enabled flag
    bool    debugger_enabled;
    // Debugger port
    int debugger_port;
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


#define CURR_LEVEL ((g_reg->reg_STS & 0x0f00) >> 8)
#define gPIL       ((g_reg->reg_STS & 0x0f00) >> 8)

/* Highest runlevel with PIE AND PID bits both set */
#define gPK g_reg->myreg_PK

/* Should CPU levels be checked ? */
#define gCHKIT g_reg->chkit

/* The complete Status register both MSB and LSB for current runlevel. Read only MACRO */
#define gSTSr ((g_reg->reg_STS & 0xFF00) | (g_reg->reg[gPIL][_STS] & 0x00FF))

#define INSTRUCTION_REGISTER g_reg->myreg_IR
#define PREFETCH_BUFFER      g_reg->myreg_PFB

// clang-format off
#define gEA                 g_reg->effectiveAddress
#define gUseAPT             g_reg->useAPT
// clang-format on

// clang-format off
#define STS_PAGE_TABLE_MODE_IS_SET  ((g_reg->reg[gPIL][_STS]>>0) & 0x01)   /* */
#define STS_FLOAT_OVERFLOW_IS_SET   ((g_reg->reg[gPIL][_STS]>>1) & 0x01)   /* */
#define STS_BIT_ACCUMULATOR_IS_SET    ((g_reg->reg[gPIL][_STS]>>2) & 0x01)   /* */
#define STS_ERROR_INDICATOR_IS_SET    ((g_reg->reg[gPIL][_STS]>>3) & 0x01)   /* */
#define STS_DYNAMIC_OVERFLOW_IS_SET    ((g_reg->reg[gPIL][_STS]>>4) & 0x01)   /* */
#define STS_STATIC_OVERFLOW_IS_SET    ((g_reg->reg[gPIL][_STS]>>5) & 0x01)   /* */
#define STS_CARRY_IS_SET    ((g_reg->reg[gPIL][_STS]>>6) & 0x01)   /* */
#define STS_SHIFT_OUT_IS_SET    ((g_reg->reg[gPIL][_STS]>>7) & 0x01)   /* */
// clang-format on

#define STS_PROGRAM_LEVEL_IS_SET   ((g_reg->reg_STS >> 8) & 0x0F)  /* Program runlevel */
#define STS_ND100_INDICATOR_IS_SET ((g_reg->reg_STS >> 12) & 0x01) /* Nord 100 indicator */
#define STS_EXTENDED_ADDRESSING_IS_SET                                                             \
    ((g_reg->reg_STS >> 13) &                                                                      \
     0x01) /* Extended MMS adressing on/off indicator (24 bit instead of 19 bit*/
#define STS_PAGING_ON_IS_SET                                                                       \
    ((g_reg->reg_STS >> 14) & 0x01) /* Memory management on/off indicator */
#define STS_INTERRUPT_ON_IS_SET                                                                    \
    ((g_reg->reg_STS >> 15) & 0x01) /* Interrupt system on/off indicator */

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

struct DisasmEntry
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

typedef struct DisasmEntry *DisasmArray[65536];
extern DisasmArray g_disasm_arr;
extern DisasmArray *g_dis;


// Global CPU variable definitions
extern struct CpuRegs *g_reg;
extern Ndram g_volatile_memory;
extern CpuType g_current_cpu_type;
extern FppType g_current_fpp_type;

extern uint64_t g_instr_counter;
extern uint16_t g_start_addr;
extern int g_disasm;

/* Called by the CPU but defined in other modules, whose prototypes are only
 * in their generated *_protos.h. Declared here so the defining files, which
 * include this header, are checked against the same signature. */
uint16_t io_op(uint16_t ioadd, uint16_t reg_a); /* machine/io.c */
int IO_Ident(uint16_t level);                   /* machine/io.c */
void start_debugger(void);                      /* debugger/debugger.c */

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


/* ----------------------------------------------------------------------
 * Doxygen-documented declarations for the CPU module's exported functions.
 * The generated cpu_protos.h carries the same signatures; these carry the
 * documentation (house rule 11.2).
 * -------------------------------------------------------------------- */

/* src/cpu/cpu.c */

/**
 * @brief Turn the ND-110 opcode trace on, writing to a file or to stdout.
 * @param path File to write the trace to; NULL or empty string writes to stdout.
 * @return 0 on success, or -1 if the file could not be opened (the trace stays off).
 */
int cpu_trace_nd110_set(const char *);

/**
 * @brief Advance P (unless executed via EXR) and dispatch one opcode through g_instr_funcs,
 * calling illegal_instr() if no handler is registered, and logging ND-110-only opcodes
 * (VERSN, the 1403xx S3SEG group, the 14050x/14051x group, the 14070x bank group) to the
 * ND-110 trace file when enabled.
 * @param operand The instruction word to execute.
 * @param isEXR True if this instruction is being executed from a register (EXR), so P must
 * not be advanced.
 */
void do_op(uint16_t, bool);

/**
 * @brief Compute the effective address for an instruction using the 8 ND-100 addressing
 * modes (P/B/X relative, direct and indirect, per Manual ND.06.014 page 34), reading any
 * indirect word through ReadIndirectVirtualMemory().
 * @param instr The instruction word, whose bits 8-10 select the addressing mode and whose
 * low 8 bits are the signed displacement.
 * @param use_apt Set by this function to tell the caller whether the resulting address is
 * in the Alterable Page Table (APT) space (true) or Program Page Table (PPT)/P-relative
 * space (false).
 * @return The computed effective address.
 */
uint16_t New_GetEffectiveAddr(uint16_t, bool *);

/**
 * @brief Compute the Internal Interrupt Code (IIC) from the priority-encoded AND of gIID
 * and gIIE, scanning bits 10 down to 0 (MC, MPV, PF, II, Z, PI, IOX, PTY, MOR, POW per the
 * table in the preceding comment) and returning the highest-priority set bit's index.
 * @return The IIC code (0-12 octal per the source table), or 0 if no internal interrupt is
 * pending.
 */
uint16_t calcIIC(void);

/**
 * @brief Set an internal interrupt: for level 14, OR sub into gIID and set PID bit 14 if
 * the resulting gIID is enabled by gIIE; for any other level, OR bit lvl into gPID directly.
 * Then recalculates the internal interrupt bits, and if lvl is 14 with sub carrying MPV
 * (bit 2), PF (bit 3) or illegal instruction (bit 4), traces the fault and longjmp()s back
 * to the setjmp() in cpu_run() to abandon the faulting instruction.
 * @param lvl Interrupt level to set; 14 selects the internal-interrupt subfield path.
 * @param sub Subbitfield used only when lvl is 14, identifying which internal interrupt(s)
 * (MC/MPV/PF/II/Z/PI/IOX/PTY/MOR/POW) fired.
 */
void interrupt(uint16_t, uint16_t);

/**
 * @brief Merge external device interrupt bits into gPID, masked to bits 10-13 and 15 (the
 * device interrupt levels), and set gCHKIT if gPID actually changed so the run loop
 * re-evaluates the runlevel.
 * @param interruptBits Raw interrupt bits from the device layer; only bits 10-13 and 15
 * (mask 0xBC00) are used.
 */
void device_interrupt(uint16_t);

/**
 * @brief Write a word to physical memory (and its shadow copy) by calling
 * WritePhysicalMemory() in cpu_mms.c.
 * @param value The 16-bit word to write.
 * @param addr The physical address to write to.
 */
void PhysMemWrite(uint16_t, uint32_t);

/**
 * @brief Read a word from physical memory (and its shadow copy) by calling
 * ReadPhysicalMemory() in cpu_mms.c.
 * @param addr The physical address to read from.
 * @return The 16-bit word read.
 */
uint16_t PhysMemRead(uint32_t);

/**
 * @brief Common handling when a memory watchpoint matches: record STOP_REASON_DATA_BREAKPOINT
 * and enter CPU_BREAKPOINT. If no debugger is attached (gDebuggerEnabled is false), also
 * print the watchpoint message, dump the B-relative frame window (B-2..B+4, D-space,
 * UseAPT=true) and the ring buffer, and halt the machine by setting CPU_SHUTDOWN - matching
 * the -B breakpoint's behaviour.
 * @param addr The address that triggered the watchpoint.
 * @param isWrite True if the triggering access was a write, false if a read.
 */
void cpu_watchpoint_triggered(uint32_t, bool);

/**
 * @brief Write a word through the Memory Management System, first checking the watchpoint
 * bitmap/counter (WITH_DEBUGGER builds) and calling cpu_watchpoint_triggered() on a match,
 * then calling WriteVirtualMemory() in cpu_mms.c.
 * @param value The 16-bit word to write.
 * @param addr The virtual address to write to.
 * @param UseAPT True to use the Alterable Page Table, false for the Program Page Table.
 * @param byte_select Byte-write selector passed through to WriteVirtualMemory().
 */
void MemoryWrite(uint16_t, uint16_t, bool, uint8_t);

/**
 * @brief Read a word through the Memory Management System, first checking the watchpoint
 * bitmap/counter (WITH_DEBUGGER builds) and calling cpu_watchpoint_triggered() on a match,
 * then calling ReadVirtualMemory() in cpu_mms.c.
 * @param addr The virtual address to read from.
 * @param UseAPT True to use the Alterable Page Table, false for the Program Page Table.
 * @return The 16-bit word read.
 */
uint16_t MemoryRead(uint16_t, bool);

/**
 * @brief Check whether the next instruction at PC is a jump-family opcode (JMP, JAP, JAN,
 * JAZ, JAF, JPC, JNC, JXZ, JXN or SKP), used by the debugger to decide step-out/step-over
 * behaviour. JPL is named in a comment but has no matching check in this function body -
 * unverified whether that omission is intentional.
 * @return True if the fetched opcode matches one of the recognised jump/skip patterns,
 * false otherwise.
 */
bool cpu_instruction_is_jump(void);

/**
 * @brief Print the CPU state (PIL, PC, A, D, T, X, B, L, STS, PID, PIE, IID, IIE, PVL, and
 * per-level STS/PC) followed by the last g_cpu_ring_dump_size entries of the instruction
 * ring buffer (PC, opcode, disassembly, A, STS, PID, PIE, IID, IIE, device bits) to stderr.
 * Does nothing if g_cpu_ring_dump_size is not positive.
 */
void ring_dump(void);

/**
 * @brief Run the CPU instruction loop for a given number of ticks, handling runlevel
 * switches, the fault longjmp() target (MPV/PF/illegal instruction), device I/O ticks
 * (IO_Tick()), CPU throttling to cpu_throttle_mhz, the debugger's async pause poll and
 * PC-breakpoint check (WITH_DEBUGGER builds), and the CPU_STOPPED/other-state exit paths.
 * @param ticks_arg Number of ticks to run; -1 runs until a stop condition is hit.
 * @return The number of ticks left to run when the loop exited (0 if it ran to completion,
 * or a positive remainder if it returned early for the debugger).
 */
int cpu_run(int);

/**
 * @brief Select the emulated CPU model from the ND100X_CPUTYPE environment variable
 * (ND100, ND100CE, ND100CX, ND110, ND110CE, ND110CX, ND110PCX), setting g_current_cpu_type.
 * Must run before Setup_Instructions() builds the dispatch table, since the CPU type gates
 * whole instruction groups. Unset or unrecognised values leave the compiled-in default
 * (ND110CX) untouched and log a warning in the unrecognised case.
 */
void cpu_set_type_from_env(void);

/**
 * @brief Initialize the CPU: zero the register set and volatile memory, set the N100 STS
 * bit and disable the cache bit in gCSR, set run mode to CPU_RUNNING, create the paging
 * tables, pick the CPU model from the environment (cpu_set_type_from_env()), reset and
 * apply the VERSN identity, build the instruction dispatch table (Setup_Instructions()),
 * set the floppy binary-load ALD, and record the debugger enable flag and port.
 * @param debuggerEnabled True to enable the DAP debugger for this run.
 * @param debuggerPort TCP port the debugger will listen on.
 */
void cpu_init(bool, int);

/**
 * @brief Initialize the CPU debugger thread: if gDebuggerEnabled is set, initialize the
 * breakpoint manager and start the debugger thread (start_debugger()). No-op when
 * WITH_DEBUGGER is not compiled in, or when the debugger is not enabled.
 */
void init_cpu_debugger(void);

/**
 * @brief Reset the CPU: zero volatile memory and the register set (preserving
 * gDebuggerEnabled across the reset), reassert the N100 STS bit and the cache-disable bit
 * in gCSR, destroy and recreate the paging tables, set run mode to CPU_RUNNING, and zero
 * the instruction counter.
 */
void cpu_reset(void);

/**
 * @brief Clean up the CPU: destroy the paging tables and, if the debugger is enabled, stop
 * the debugger thread.
 */
void cleanup_cpu(void);

/**
 * @brief Set the flag by which the DAP debugger thread asks the CPU thread to pause,
 * stored as an atomic/interlocked value per platform (WITH_DEBUGGER builds only).
 * @param requested True to request a pause, false to clear the request.
 */
void set_debugger_request_pause(bool);

/**
 * @brief Read the debugger pause-request flag set by set_debugger_request_pause().
 * @return True if the debugger has requested a pause; always false when WITH_DEBUGGER is
 * not compiled in.
 */
bool get_debugger_request_pause(void);

/**
 * @brief Set the flag by which the CPU thread tells the debugger it is paused and the
 * debugger may access CPU state, stored as an atomic/interlocked value per platform
 * (WITH_DEBUGGER builds only).
 * @param requested True when control is granted to the debugger, false otherwise.
 */
void set_debugger_control_granted(bool);

/**
 * @brief Read the debugger-control-granted flag set by set_debugger_control_granted().
 * @return True if debugger control is currently granted; always false when WITH_DEBUGGER
 * is not compiled in.
 */
bool get_debugger_control_granted(void);

/**
 * @brief Set the reason the CPU last stopped (e.g. breakpoint, data breakpoint), stored as
 * an atomic/interlocked value per platform (WITH_DEBUGGER builds only).
 * @param reason The CpuStopReason to record.
 */
void set_cpu_stop_reason(CpuStopReason);

/**
 * @brief Read the CPU stop reason set by set_cpu_stop_reason().
 * @return The recorded CpuStopReason; STOP_REASON_NONE when WITH_DEBUGGER is not compiled
 * in.
 */
CpuStopReason get_cpu_stop_reason(void);

/**
 * @brief Set the current CPU run mode (CPU_RUNNING, CPU_BREAKPOINT, CPU_PAUSED,
 * CPU_STOPPED, CPU_SHUTDOWN, ...), stored as an atomic/interlocked value when
 * WITH_DEBUGGER is compiled in, or in the plain CurrentCPURunMode global otherwise.
 * @param new_mode The CPURunMode to set.
 */
void set_cpu_run_mode(CPURunMode);

/**
 * @brief Read the current CPU run mode set by set_cpu_run_mode().
 * @return The current CPURunMode value.
 */
CPURunMode get_cpu_run_mode(void);

/**
 * @brief Enable or disable CPU throttling to cpu_throttle_mhz, logging the new state.
 * @param enabled True to throttle the CPU to real-time speed, false to run at full speed.
 */
void cpu_throttle_set_enabled(bool);

/**
 * @brief Read whether CPU throttling is currently enabled.
 * @return True if throttling is enabled, false otherwise.
 */
bool cpu_throttle_get_enabled(void);

/**
 * @brief Set the target throttle speed in MHz, clamped to the range 0.1 to 100.0, and log
 * the new target.
 * @param mhz Requested target frequency in MHz; values outside 0.1-100.0 are clamped.
 */
void cpu_throttle_set_mhz(double);

/**
 * @brief Read the current CPU throttle target frequency.
 * @return The target frequency in MHz.
 */
double cpu_throttle_get_mhz(void);

/* src/cpu/cpu_instr.c and src/cpu/cpu_regs.c */

/**
 * @brief Set the ring-at-CLPT floor, clamping a negative value to 0.
 * @param n New ring-at-CLPT value.
 */
void cpu_set_ring_at_clpt(int64_t);

/**
 * @brief Sign-extend the low byte of x to a 16-bit signed value.
 * @param x Byte value in the low 8 bits (bit 7 is the sign bit).
 * @return Sign-extended value: bits 8-15 set to 1 when bit 7 of x is 1.
 */
int16_t signExtend(uint16_t);

/**
 * @brief Handle an illegal/unimplemented opcode by raising the illegal
 *        instruction interrupt. This is also how a guest program's CPU-type
 *        probe (e.g. TPE executing VERSN, opcode 140133) is detected as
 *        "ND-100": it decides so only if execution traps here.
 * @param operand The undecoded instruction word that could not be dispatched.
 */
void illegal_instr(uint16_t);

/**
 * @brief Execute BFILL: fill memory with the byte in A, X bytes/words
 *        addressed per the byte count and start-half encoded in T, using
 *        the alternative page table (APT) when T bit 14 is set.
 * @param operand Unused; BFILL takes its operands from A, T and X.
 */
void opcode_bfill_new_byte_fill(uint16_t);

/**
 * @brief Reset the VERSN PROM image to the default ND-110 back-wiring
 *        content and load the microcode/print version fields (microcode
 *        version 3410B, print version 80C octal), matching the byte-for-byte
 *        default kept for compatibility; the correct ND-110 revision is
 *        unverified.
 */
void cpu_versn_reset(void);

/**
 * @brief Overlay VERSN PROM identity fields (installation number, SYSNO,
 *        HWINFO, NLEGU, etc.) from ND100X_INSTALLATION_NUMBER and related
 *        environment variables, re-forcing the INF3 signature after each
 *        write so GCPUNR continues to read the PROM as valid.
 */
void cpu_versn_set_identity_from_env(void);

/**
 * @brief Execute VERSN: return the PROM byte selected by A bits 8-11 in D,
 *        and for the ND-120/CX identity rebuild A as the PRINT NUMBER/ECO
 *        LEVEL/CX-flag/PRINT RELEASE/ALD bit field and set T to the
 *        DELILAH-L microprogram version with bit 15 set, per the ND-120
 *        DELILAH-L microcode and the CPU board 3202 straps.
 * @param operand Unused; VERSN takes its selector from A and returns via D/T.
 */
void opcode_versn_read_cpu_version(uint16_t);

/**
 * @brief Add the memory word at eff_addr to A, updating C and Q in STS.
 *        Flag handling for C, O and Q is marked with a fix-me note in the source as
 *        possibly subtly wrong; treated here as unverified.
 * @param eff_addr Effective address of the memory operand to add to A.
 * @param UseAPT Whether to read through the alternative page table.
 */
void add_A_mem(uint16_t, bool);

/**
 * @brief Execute MOVB: move a byte field from the A/D-addressed source to
 *        the X/T-addressed destination without checking for overlap.
 * @param instr Unused; MOVB takes its operands from A, D, X and T.
 */
void opcode_movb_move_byte_buggy(uint16_t);

/**
 * @brief Execute MOVBF: move a byte field from the A/D-addressed source to
 *        the X/T-addressed destination, refusing the move (no skip return)
 *        when source and destination word ranges overlap.
 * @param instr Unused; MOVBF takes its operands from A, D, X and T.
 */
void opcode_movbf_move_bytes_forward_buggy(uint16_t);

/**
 * @brief Subtract the memory word at eff_addr from A, updating C and Q in
 *        STS. Flag handling for C, O and Q is marked with a fix-me note in the source as
 *        possibly subtly wrong; treated here as unverified.
 * @param eff_addr Effective address of the memory operand to subtract from A.
 * @param UseAPT Whether to read through the alternative page table.
 */
void sub_A_mem(uint16_t, bool);

/**
 * @brief Execute RDIV: divide the 32-bit signed double accumulator AD by the
 *        source register selected by instr bits 3-5, leaving the quotient
 *        in A and the remainder in D; sets STS bit Z on division by zero.
 *        Carry/overflow handling is marked as unverified in the source
 *        comments.
 * @param instr Instruction word; bits 3-5 select the divisor register.
 */
void rdiv_org(uint16_t);

/**
 * @brief Execute RMPY: multiply the source register (instr bits 3-5) by the
 *        destination register (instr bits 0-2), placing the 32-bit signed
 *        result in A (high 16 bits) and D (low 16 bits); sets STS bits O
 *        and Q on overflow. Carry handling is marked as unverified in the
 *        source comments.
 * @param instr Instruction word; selects the source and destination registers.
 */
void rmpy_org(uint16_t);

/**
 * @brief Load the two BCD working words s_bcd_d1/s_bcd_d2 from the memory
 *        word pair at address and address+1, using the alternative page
 *        table, for the BCD instructions (ADDD, SUBD, COMD, PACK, UPACK,
 *        SHDE) implemented in bcd.c.
 * @param address Address of the first of the two BCD words to read.
 */
void GetBCD(uint16_t);

/**
 * @brief Store the two BCD working words s_bcd_d1/s_bcd_d2 back to the
 *        memory word pair at address and address+1, using the alternative
 *        page table, for the BCD instructions implemented in bcd.c.
 * @param address Address of the first of the two BCD words to write.
 */
void StoreBCD(uint16_t);

/**
 * @brief Populate the g_instr_funcs dispatch table by registering every
 *        ND-100 instruction handler (STZ, STA, ADD, MOVB, VERSN, RDIV,
 *        RMPY, etc.) against its opcode, opcode range or opcode/mask
 *        signature.
 */
void Setup_Instructions(void);

/**
 * @brief Set the current PIL (Priority Interrupt Level), saving the old
 *        level into PVL and updating the STS SYSTEM PIL bits, unless
 *        newLevel is out of range or already the current level.
 * @param newLevel New PIL value (0-15); levels 16 and above are rejected.
 * @return true if the PIL was set or was already newLevel; false if
 *         newLevel is 16 or greater (invalid PIL).
 */
bool setPIL(char);

/**
 * @brief Set the PEA (Page Error Address) register once, then lock it so
 *        further calls are ignored until the lock is cleared elsewhere.
 * @param pea New PEA value.
 */
void setPEA(uint16_t);

/**
 * @brief Set the PES (Page Error Status) register once, then lock it so
 *        further calls are ignored until the lock is cleared elsewhere.
 * @param pes New PES value.
 */
void setPES(uint16_t);

/**
 * @brief Set the PGS (Page Table Group Status, unverified name) register
 *        once, then lock it, but only when pgs is nonzero; a pgs of 0 is
 *        stored without locking.
 * @param pgs New PGS value.
 */
void setPGS(uint16_t);

/**
 * @brief Write val into register r at the current run level, masking STS
 *        to its low 8 bits and all other registers to 16 bits.
 * @param r Register index (_STS or a general register index).
 * @param val Value to store into the register.
 */
void setreg(int, int);

/**
 * @brief Read one bit of a register at the current run level. For _STS,
 *        reads from the full 16-bit shadow gSTSr (all STS bits, including
 *        the undocumented ones) rather than the level-local STS word.
 * @param regnum Register index (_STS or a general register index).
 * @param stsbit Bit position to read (0-15).
 * @return The selected bit value, 0 or 1.
 */
uint16_t getbit(uint16_t, uint16_t);

/**
 * @brief Clear one bit of a register at the current run level.
 * @param regnum Register index (_STS or a general register index).
 * @param stsbit Bit position to clear (0-15).
 */
void clrbit(uint16_t, uint16_t);

/**
 * @brief Set or clear one bit of the shared MSB half of STS (reg_STS, common
 *        across all run levels). PIL bits are excluded and handled only by
 *        setPIL().
 * @param stsbit Bit position within STS to modify.
 * @param val Nonzero to set the bit, zero to clear it.
 */
void setbit_STS_MSB(uint16_t, char);

/**
 * @brief Set or clear one bit of a register at the current run level,
 *        routing STS bits above bit 7 to setbit_STS_MSB(); setting the Z
 *        (error) bit also raises gCHKIT so PK is checked afterward.
 * @param regnum Register index (_STS or a general register index).
 * @param stsbit Bit position to modify (0-15).
 * @param val Nonzero to set the bit, zero to clear it.
 */
void setbit(uint16_t, uint16_t, char);

/**
 * @brief Update STS carry (C), static overflow (O) and dynamic overflow (Q)
 *        bits after an arithmetic operation, comparing the sign bits of
 *        reg_a and operand against the sign bit of result.
 * @param reg_a First operand (destination register value before the op).
 * @param operand Second operand of the arithmetic operation.
 * @param result Result of the arithmetic operation (wider than 16 bits so
 *        carry-out is visible).
 */
void AdjustSTS(uint16_t, uint16_t, int);

/* src/cpu/cpu_mms.c */

/**
 * @brief Set the page-fault count at which the CPU instruction ring is dumped
 * (--ring-at-pf diagnostic).
 * @param n Page-fault number counted from 1 at which to dump; 0 disables the dump.
 */
void cpu_set_ring_at_pf(int64_t);

/**
 * @brief Allocate and initialize the global page-table shadow RAM (g_paging_tables)
 * for the configured MMS type (MMS1: 4 page tables/512 words, MMS2: 16 page
 * tables/2048 words). Must be called before any paging table access.
 * @return true on success; false if the shadow RAM calloc failed (g_paging_tables
 * is left with isInitialized still unset).
 */
bool CreatePagingTables(void);

/**
 * @brief Free the global page-table shadow RAM allocated by CreatePagingTables().
 */
void DestroyPagingTables(void);

/**
 * @brief Compute the word offset into g_paging_tables.shadowRam for a given page
 * table and virtual page number, taking into account extended (STS_EXTENDED_ADDRESSING_IS_SET) vs
 * normal mode and four- vs sixteen-page-table layout.
 * @param pageTable Page table number.
 * @param VPN Virtual page number (index within the page table).
 * @param ptm Page table mode (Four or Sixteen) selecting the extended-mode base
 * address used in the offset calculation.
 * @return Shadow-RAM address (offset added to g_paging_tables.shadowRamAddress).
 */
uint16_t GetPTShadowAddress(uint32_t, uint32_t, PageTableMode);

/**
 * @brief Write a 16-bit word into the page-table shadow RAM at a physical shadow
 * address, with optional LOG_TRACE decode of the resulting page table entry.
 * Out-of-range addresses (below g_paging_tables.shadowRamAddress or above
 * 0xFFFF) or a NULL shadowRam are silently ignored - trap-free.
 * @param address Physical shadow-RAM address (as seen on the 16-bit address bus).
 * @param value Word value to store.
 */
void PT_Write(uint32_t, uint16_t);

/**
 * @brief Read a 16-bit word from the page-table shadow RAM at a physical shadow
 * address. Trap-free.
 * @param address Physical shadow-RAM address (as seen on the 16-bit address bus).
 * @return The stored word, or 0 if shadowRam is NULL or address is out of range
 * (0 is not distinguishable from a real stored zero).
 */
uint16_t PT_Read(uint32_t);

/**
 * @brief Read a full page table entry (PTE) from shadow RAM for the current
 * STS_EXTENDED_ADDRESSING_IS_SET mode: 32-bit PTE from two consecutive words in extended mode, or a
 * 16-bit PTE expanded via convert_from_16_bit_pte() in normal mode (page tables 0-3
 * only). Trap-free.
 * @param pageTable Page table number (0-15).
 * @param VPN Virtual page number.
 * @param ptm Page table mode (Four or Sixteen), passed through to
 * GetPTShadowAddress().
 * @return The page table entry, or 0 if shadowRam is NULL, pageTable >= 16, or
 * (in normal mode) pageTable > 3 (0 is not distinguishable from a real all-zero
 * entry).
 */
uint32_t GetPageTableEntry(uint32_t, uint32_t, PageTableMode);

/**
 * @brief Debugger/inspector variant of GetPageTableEntry() that reads by
 * g_mms_type (hardware format) instead of the current level's STS_EXTENDED_ADDRESSING_IS_SET flag, so
 * page tables 4-15 can be inspected even when SEXI is currently off for the
 * paused level. Trap-free; does not modify CPU or PGU/WIP state.
 * @param pageTable Page table number (0-15).
 * @param VPN Virtual page number.
 * @param ptm Unused - kept for call-signature compatibility with
 * GetPageTableEntry().
 * @return The page table entry (32-bit on MMS2, or expanded from 16-bit on MMS1
 * for pageTable 0-3), or 0 if shadowRam is NULL, pageTable >= 16, or (on MMS1)
 * pageTable > 3.
 */
uint32_t GetPageTableEntryForDebugger(uint32_t, uint32_t, PageTableMode);

/**
 * @brief Write a page table entry back into shadow RAM, packing it as two words
 * in extended mode (STS_EXTENDED_ADDRESSING_IS_SET) or as a 16-bit PTE via convert_to_16_bit_pte() in
 * normal mode. Trap-free.
 * @param pageTable Page table number (0-15).
 * @param VPN Virtual page number.
 * @param ptm Page table mode (Four or Sixteen), passed through to
 * GetPTShadowAddress().
 * @param PTe Page table entry value to store.
 * @return true on success; false if shadowRam is NULL or pageTable >= 16 (no
 * write performed).
 */
bool UpdatePageTableEntry(uint32_t, uint32_t, PageTableMode, uint32_t);

/**
 * @brief Set the PGU (page used) flag in a page table entry if not already set,
 * writing the updated entry back via UpdatePageTableEntry().
 * @param pageTable Page table number.
 * @param VPN Virtual page number.
 * @param ptm Page table mode, forwarded to UpdatePageTableEntry().
 * @param PTe Current page table entry value.
 * @return The page table entry with PGU_FLAG set (same value if it was already set).
 */
uint32_t SetPageUsed(uint32_t, uint32_t, PageTableMode, uint32_t);

/**
 * @brief Set the WIP (written) flag in a page table entry if not already set,
 * writing the updated entry back via UpdatePageTableEntry().
 * @param pageTable Page table number.
 * @param VPN Virtual page number.
 * @param ptm Page table mode, forwarded to UpdatePageTableEntry().
 * @param PTe Current page table entry value.
 * @return The page table entry with WIP_FLAG set, or the unmodified PTe if
 * shadowRam is NULL or pageTable >= 16.
 */
uint32_t SetPageWritten(uint32_t, uint32_t, PageTableMode, uint32_t);

/**
 * @brief Format a page table entry into a short human-readable string (WPM/RPM/
 * FPM/WIP/PGU flags, ring, and physical page number) for LOG_TRACE messages.
 * @param PTe Page table entry to decode.
 * @return Pointer to a static internal buffer holding the formatted string;
 * overwritten on the next call, not thread-safe.
 */
const char *GetPageTableEntryDebugInfo(uint32_t);

/**
 * @brief Translate a virtual address to a physical address through the current
 * level's page tables: selects the page table via PCR (APT or PT field,
 * four/sixteen mode), reads the page table entry, checks page presence/access
 * permission via checkPageProtection() and ring protection against the PCR ring,
 * then computes the physical page from the PTE and checks it against
 * g_nd_memsize. Marks PGU (and WIP on write) via SetPageUsed()/SetPageWritten().
 * Ring-3 accesses to shadow memory and STS_PAGING_ON_IS_SET-disabled (unmapped) accesses are
 * returned directly as identity-mapped. Traps: raises MPV (HandleMPV) on a ring
 * violation, PF/MPV via checkPageProtection() on a page fault or permit
 * violation, and a memory-out-of-range interrupt (HandleMemoryOutOfRange) if the
 * mapped physical address exceeds installed memory.
 * @param virtualAddress Virtual address to translate (masked to 16 bits).
 * @param am Access mode being performed (READ, WRITE, FETCH, or READ_FETCH).
 * @param UseAPT Whether to use the Alternative Page Table field of the PCR
 * (data/APT access) instead of the normal PT field (instruction access).
 * @return The physical address on success; -1 if the paging tables are not
 * initialized, or on a ring violation, page fault/permit violation, or
 * out-of-range physical address (all of which also raise a level-14 interrupt).
 */
int mapVirtualToPhysical(uint32_t, AccessMode, bool);

/**
 * @brief Build and store the PGS (Page Status) register value for a fault: packs
 * the page table and VPN, sets the permit-violation bit (bit 14) if requested,
 * and sets the fetch bit (bit 15) for a pure FETCH access (not READ_FETCH).
 * @param pageTable Page table number involved in the fault.
 * @param VPN Virtual page number involved in the fault.
 * @param am Access mode being performed.
 * @param permitViolation True if this is a permit violation (sets PGS bit 14);
 * per the comment in checkPageProtection(), this is also set true for a page
 * not present, matching hardware behaviour validated against the ND paging
 * diagnostic (TPE) - unverified beyond what that comment documents.
 */
void UpdatePGS(uint32_t, uint32_t, AccessMode, bool);

/**
 * @brief Check whether a page table entry permits the requested access: raises a
 * page fault (IIC=3) if WPM/RPM/FPM are all clear (page not present), or a
 * memory protection violation (IIC=2, MPV) if the entry is present but lacks the
 * bit for the requested access mode. Also runs the --ring-at-pf and
 * --trace-nd110 diagnostics on a page fault. Traps via HandlePF()/HandleMPV() on
 * failure.
 * @param VPN Virtual page number being accessed.
 * @param pageTable Page table number being accessed.
 * @param pageTableEntry The page table entry read for this VPN/pageTable.
 * @param am Access mode being performed (READ/WRITE/FETCH bits).
 * @param virtualAddress Virtual address being accessed, used for PGS/diagnostics
 * and passed to HandlePF()/HandleMPV().
 * @return true if the access is permitted; false if a page fault or permit
 * violation was raised (an internal interrupt has already been generated).
 */
bool checkPageProtection(uint32_t, uint32_t, uint32_t, AccessMode, uint32_t);

/**
 * @brief Determine whether a 16-bit address falls in the page-table shadow
 * memory window for the current PCR ring/mode (ring 3, non-paged, or privileged
 * access; normal vs extended mode; MMS1 4-page-table vs MMS2 16-page-table
 * address ranges). Always false during a DMA bus transfer (g_dma_access) or for
 * addresses above 0xFFFF. Trap-free.
 * @param addr Address to classify (16-bit address space).
 * @param privileged True to treat the access as privileged (e.g. debugger/EXAM-
 * DEPO), which allows the shadow-memory window regardless of the current ring.
 * @return true if addr lies in the active shadow-RAM window; false otherwise.
 */
bool IsAddressShadowMemory(uint32_t, bool);

/**
 * @brief Read a data word through the virtual memory path (READ access mode),
 * translating via mapVirtualToPhysical() and then ReadPhysicalMemory(). Traps
 * (via mapVirtualToPhysical()/checkPageProtection()) on a page fault, MPV, or
 * memory-out-of-range condition.
 * @param virtualAddress Virtual address to read.
 * @param UseAPT Whether to translate via the APT (data) page table field.
 * @return The word read; 0 if translation failed (a fault was already raised).
 */
int ReadVirtualMemory(uint32_t, bool);

/**
 * @brief Read a word through the virtual memory path using READ_FETCH access
 * mode, for indirect reads during effective address calculation (does not set
 * the PGS fetch bit the way a plain FETCH does). Traps on page fault/MPV/out-of-
 * range exactly like ReadVirtualMemory().
 * @param virtualAddress Virtual address to read.
 * @param UseAPT Whether to translate via the APT (data) page table field.
 * @return The word read; 0 if translation failed (a fault was already raised).
 */
int ReadIndirectVirtualMemory(uint32_t, bool);

/**
 * @brief Fetch an instruction word through the virtual memory path (FETCH
 * access mode). Traps on page fault/MPV/out-of-range exactly like
 * ReadVirtualMemory().
 * @param virtualAddress Virtual address to fetch from.
 * @param UseAPT Whether to translate via the APT (data) page table field rather
 * than the PT (instruction) field.
 * @return The word fetched; 0 if translation failed (a fault was already raised).
 */
int FetchVirtualMemory(uint32_t, bool);

/**
 * @brief Write a data word through the virtual memory path (WRITE access mode),
 * translating via mapVirtualToPhysical() and then WritePhysicalMemoryWM(). Traps
 * on page fault, MPV, or memory-out-of-range condition, in which case the write
 * is discarded.
 * @param virtualAddress Virtual address to write.
 * @param value Word value to write.
 * @param UseAPT Whether to translate via the APT (data) page table field.
 * @param wm Write mode (full word, or MSB/LSB byte write).
 */
void WriteVirtualMemory(uint32_t, uint16_t, bool, WriteMode);

/**
 * @brief Classify a physical word address into its ND-100 memory region: the
 * ND-500 MPM5 shared-memory window (not ECC checked), installed local RAM
 * (ECC/parity checked), or unclaimed. Trap-free.
 * @param physicalWordAddress Physical word address to classify.
 * @return ND_MEM_MPM5, ND_MEM_LOCAL, or ND_MEM_NONE.
 */
NDMemoryType GetPhysicalMemoryType(uint32_t);

/**
 * @brief Read a word from physical memory: checks physical watchpoints (when
 * WITH_DEBUGGER), redirects to PT_Read() if the address is shadow memory, checks
 * memory bounds, runs the ECC read-detect simulation (nd_ecc_read_detect(), which
 * may raise a level-14 parity interrupt), then returns the stored word.
 * @param physicalAddress Physical address to read.
 * @param privileged Whether this access is privileged, forwarded to
 * IsAddressShadowMemory() to decide the shadow-memory window.
 * @return The word read; 0 if physicalAddress is negative or out of range
 * (HandleMemoryOutOfRange() has already been called for the out-of-range case).
 */
int ReadPhysicalMemory(int, bool);

/**
 * @brief Write a full 16-bit word to physical memory. Thin wrapper around
 * WritePhysicalMemoryWM() with WRITEMODE_WORD.
 * @param physicalAddress Physical address to write.
 * @param value Word value to write.
 * @param privileged Whether this access is privileged, forwarded to
 * WritePhysicalMemoryWM().
 */
void WritePhysicalMemory(int, uint16_t, bool);

/**
 * @brief Write to physical memory with byte/word write mode: checks physical
 * watchpoints (when WITH_DEBUGGER), redirects to PT_Write() (with MSB/LSB
 * merge-read via PT_Read() as needed) if the address is shadow memory, checks
 * memory bounds, runs the ECC write-latch simulation (nd_ecc_write_latch()),
 * then stores the value (full word, or merged MSB/LSB byte) into physical RAM.
 * @param physicalAddress Physical address to write.
 * @param value Value to write (word, or byte value for MSB/LSB modes).
 * @param privileged Whether this access is privileged, forwarded to
 * IsAddressShadowMemory().
 * @param wm Write mode: WRITEMODE_WORD, WRITEMODE_MSB, or WRITEMODE_LSB.
 */
void WritePhysicalMemoryWM(int, uint16_t, bool, WriteMode);

/**
 * @brief Record a memory-out-of-range fault into PEA/PES and raise the level-14
 * "memory out of range" interrupt (bit 9). Always traps.
 * @param physicalAddress Physical address that was out of range, stored (masked)
 * into PEA/PES.
 */
void HandleMemoryOutOfRange(uint32_t);

/**
 * @brief Handle a memory protection violation: optionally logs the faulting PTE
 * (LOG_TRACE) then raises the level-14 MPV interrupt (bit 2). Always traps the
 * current instruction.
 * @param virtualAddress Virtual address that caused the violation, used only for
 * the diagnostic log line.
 */
void HandleMPV(uint32_t);

/**
 * @brief Handle a page fault by raising the level-14 PF interrupt (bit 3).
 * Always traps the current instruction.
 * @param virtualAddress Virtual address that faulted; unused (parameter kept for
 * the HandleMPV()-matching call signature).
 */
void HandlePF(uint32_t);

/**
 * @brief Debugger-only physical memory read that bypasses the MMU, watchpoint
 * hooks, and protection traps, reading directly from g_volatile_memory. Trap-free.
 * @param physicalAddress Physical address to read.
 * @return The word read (0-65535, as int); -1 if physicalAddress is out of range
 * (>= g_nd_memsize).
 */
int Dbg_ReadPhysicalMemory(uint32_t);

/**
 * @brief Debugger-only physical memory write that bypasses the MMU, watchpoint
 * hooks, and protection traps, writing directly into g_volatile_memory. Trap-free.
 * @param physicalAddress Physical address to write.
 * @param value Word value to write.
 * @return 0 on success; -1 if physicalAddress is out of range (>= g_nd_memsize),
 * in which case no write is performed.
 */
int Dbg_WritePhysicalMemory(uint32_t, uint16_t);

/**
 * @brief Debugger-only I-space (instruction/PT field) virtual memory read at a
 * given PIL, via the internal trap-free Dbg_MapVirtualToPhysical() translation
 * (no faults raised, PGU/WIP not modified) followed by Dbg_ReadPhysicalMemory().
 * Trap-free.
 * @param virtualAddress Virtual address to read.
 * @param pil Privilege/interrupt level whose PCR to use for translation; -1
 * selects CurrLEVEL.
 * @return The word read; -1 if the address could not be translated (page not
 * present or out of range) or is out of physical range.
 */
int Dbg_ReadVirtualMemoryISpace_PIL(uint32_t, int8_t);

/**
 * @brief Debugger-only D-space (data/APT field) virtual memory read at a given
 * PIL, via the internal trap-free Dbg_MapVirtualToPhysical() translation (no
 * faults raised, PGU/WIP not modified) followed by Dbg_ReadPhysicalMemory().
 * Trap-free.
 * @param virtualAddress Virtual address to read.
 * @param pil Privilege/interrupt level whose PCR to use for translation; -1
 * selects CurrLEVEL.
 * @return The word read; -1 if the address could not be translated (page not
 * present or out of range) or is out of physical range.
 */
int Dbg_ReadVirtualMemoryDSpace_PIL(uint32_t, int8_t);

/**
 * @brief Debugger-only I-space (instruction/PT field) virtual memory write at a
 * given PIL, via the internal trap-free Dbg_MapVirtualToPhysical() translation
 * (no faults raised) followed by Dbg_WritePhysicalMemory(). Trap-free.
 * @param virtualAddress Virtual address to write.
 * @param value Word value to write.
 * @param pil Privilege/interrupt level whose PCR to use for translation; -1
 * selects CurrLEVEL.
 * @return 0 on success; -1 if the address could not be translated (page not
 * present or out of range) or is out of physical range.
 */
int Dbg_WriteVirtualMemoryISpace_PIL(uint32_t, uint16_t, int8_t);

/**
 * @brief Debugger-only D-space (data/APT field) virtual memory write at a given
 * PIL, via the internal trap-free Dbg_MapVirtualToPhysical() translation (no
 * faults raised) followed by Dbg_WritePhysicalMemory(). Trap-free.
 * @param virtualAddress Virtual address to write.
 * @param value Word value to write.
 * @param pil Privilege/interrupt level whose PCR to use for translation; -1
 * selects CurrLEVEL.
 * @return 0 on success; -1 if the address could not be translated (page not
 * present or out of range) or is out of physical range.
 */
int Dbg_WriteVirtualMemoryDSpace_PIL(uint32_t, uint16_t, int8_t);

/**
 * @brief Backward-compatible wrapper for Dbg_ReadVirtualMemoryISpace_PIL() using
 * the current PIL (CurrLEVEL). Trap-free.
 * @param virtualAddress Virtual address to read.
 * @return The word read; -1 on translation failure, as in
 * Dbg_ReadVirtualMemoryISpace_PIL().
 */
int Dbg_ReadVirtualMemoryISpace(uint32_t);

/**
 * @brief Backward-compatible wrapper for Dbg_ReadVirtualMemoryDSpace_PIL() using
 * the current PIL (CurrLEVEL). Trap-free.
 * @param virtualAddress Virtual address to read.
 * @return The word read; -1 on translation failure, as in
 * Dbg_ReadVirtualMemoryDSpace_PIL().
 */
int Dbg_ReadVirtualMemoryDSpace(uint32_t);

/**
 * @brief Backward-compatible wrapper for Dbg_WriteVirtualMemoryISpace_PIL()
 * using the current PIL (CurrLEVEL). Trap-free.
 * @param virtualAddress Virtual address to write.
 * @param value Word value to write.
 * @return 0 on success; -1 on translation failure, as in
 * Dbg_WriteVirtualMemoryISpace_PIL().
 */
int Dbg_WriteVirtualMemoryISpace(uint32_t, uint16_t);

/**
 * @brief Backward-compatible wrapper for Dbg_WriteVirtualMemoryDSpace_PIL()
 * using the current PIL (CurrLEVEL). Trap-free.
 * @param virtualAddress Virtual address to write.
 * @param value Word value to write.
 * @return 0 on success; -1 on translation failure, as in
 * Dbg_WriteVirtualMemoryDSpace_PIL().
 */
int Dbg_WriteVirtualMemoryDSpace(uint32_t, uint16_t);

/* src/cpu/cpu_bkpt.c */

/**
 * @brief Allocate the single static BreakpointManager instance and reset it.
 */
void breakpoint_manager_init(void);

/**
 * @brief Clear all breakpoints and drop the global manager pointer to NULL.
 */
void breakpoint_manager_cleanup(void);

/**
 * @brief Arm a one-instruction single step by setting step_count to 1.
 */
void breakpoint_manager_step_one(void);

/**
 * @brief Add a breakpoint entry at address, hashed into g_breakpoint_mgr's bucket table.
 * @param address Address (PC value) the breakpoint fires on.
 * @param type Breakpoint kind: user, function, data, instruction, or temporary.
 * @param condition Optional condition expression string, copied with strdup, or NULL.
 * @param hitCondition Optional hit-count string compared against hitCount, or NULL.
 * @param logMessage Optional logpoint message, copied with strdup, or NULL.
 */
void breakpoint_manager_add(uint16_t, BreakpointType, const char *, const char *, const char *);

/**
 * @brief Remove breakpoint entries at address, matching type or all types if type is -1.
 * @param address Address to remove entries from.
 * @param type Breakpoint type to match, or -1 to remove every entry at address.
 */
void breakpoint_manager_remove(uint16_t, int);

/**
 * @brief Free every breakpoint entry in every hash bucket and reset the PC bitmap.
 */
void breakpoint_manager_clear(void);

/**
 * @brief Free only breakpoint entries whose type matches, keeping the rest.
 * @param type Breakpoint type to remove.
 */
void breakpoint_manager_clear_type(BreakpointType);

/**
 * @brief Advance one pending single step, then check the current PC against
 * all breakpoints, evaluating each entry's condition and hit-count expressions.
 * @return BT_NONE if nothing fired, otherwise the BreakpointType of the last
 * matching entry evaluated at this PC (a logpoint entry logs and does not
 * change the returned type).
 */
int check_for_breakpoint(void);

/**
 * @brief Fetch and consume the address of the last breakpoint hit recorded on
 * g_breakpoint_mgr, clearing last_hit_valid so a second call returns false.
 * @param address Pointer that receives the hit address; left unwritten if
 * no hit is pending or if address is NULL.
 * @return true if a hit was pending and address was written, false otherwise.
 */
bool breakpoint_manager_get_last_hit(uint16_t *);

/**
 * @brief Add a watchpoint on a 16-bit virtual address, or update the type of
 * an existing entry with the same address, space, and PIL.
 * @param address Virtual address to watch.
 * @param type WATCH_READ, WATCH_WRITE, or WATCH_READWRITE.
 * @param space WATCH_SPACE_ANY, WATCH_SPACE_ISPACE, or WATCH_SPACE_DSPACE.
 * @param pil -1 to match any PIL, or 0-15 to match one specific PIL.
 * @return 0 on success, -1 if the table is full (MAX_WATCHPOINTS reached).
 */
int watchpoint_add(uint16_t, WatchpointType, WatchpointSpace, int8_t);

/**
 * @brief Remove the first active watchpoint whose address matches, filling
 * the hole with the last active slot and rebuilding the address bitmap.
 * @param address Virtual address of the watchpoint to remove.
 */
void watchpoint_remove(uint16_t);

/**
 * @brief Slow-path scan of every active watchpoint for a match on address,
 * PIL, and I-space/D-space, applying the skip-hits ignore-count on a match.
 * @param address Virtual address being accessed.
 * @param isWrite true for a write access, false for a read access.
 * @param useAPT true if the access is D-space (APT), false if I-space.
 * @return 1 if a watchpoint matched and was not swallowed by the skip-count,
 * 0 if nothing matched or the match was swallowed.
 */
int watchpoint_check_slow(uint16_t, bool, bool);

/**
 * @brief Legacy wrapper for watchpoint_check_slow that always passes
 * useAPT as false (I-space).
 * @param address Virtual address being accessed.
 * @param isWrite true for a write access, false for a read access.
 * @return 1 if a watchpoint matched, 0 otherwise.
 */
int watchpoint_check(uint16_t, bool);

/**
 * @brief Deactivate every watchpoint slot and clear the address bitmap.
 */
void watchpoint_clear(void);

/**
 * @brief Return the number of active virtual watchpoints (g_watchpoint_count).
 * @return Current watchpoint count.
 */
int watchpoint_get_count(void);

/**
 * @brief Read back the address and type of the watchpoint at a table index.
 * @param index Index into the internal watchpoint table.
 * @param out_addr Pointer that receives the watchpoint's address.
 * @param out_type Pointer that receives the watchpoint's type, cast to int.
 * @return 0 on success, -1 if index is out of range or that slot is inactive.
 */
int watchpoint_get(int, uint16_t *, int *);

/**
 * @brief Add a watchpoint on a 32-bit physical address, or update the type of
 * an existing entry with the same address and PIL.
 * @param address Physical address to watch.
 * @param type WATCH_READ, WATCH_WRITE, or WATCH_READWRITE.
 * @param pil -1 to match any PIL, or 0-15 to match one specific PIL.
 * @return 0 on success, -1 if the table is full (MAX_WATCHPOINTS reached).
 */
int phys_watchpoint_add(uint32_t, WatchpointType, int8_t);

/**
 * @brief Remove the first active physical watchpoint whose address matches,
 * filling the hole with the last active slot and rebuilding the page bitmap.
 * @param address Physical address of the watchpoint to remove.
 */
void phys_watchpoint_remove(uint32_t);

/**
 * @brief Scan every active physical watchpoint for a match on address, PIL,
 * and access direction (read/write/readwrite).
 * @param address Physical address being accessed.
 * @param isWrite true for a write access, false for a read access.
 * @return 1 if a watchpoint matched, 0 otherwise.
 */
int phys_watchpoint_check(uint32_t, bool);

/**
 * @brief Deactivate every physical watchpoint slot and clear the page bitmap.
 */
void phys_watchpoint_clear(void);

/**
 * @brief Return the number of active physical watchpoints
 * (g_phys_watchpoint_count).
 * @return Current physical watchpoint count.
 */
int phys_watchpoint_get_count(void);

/**
 * @brief Read back the address and type of the physical watchpoint at a
 * table index.
 * @param index Index into the internal physical watchpoint table.
 * @param out_addr Pointer that receives the watchpoint's address.
 * @param out_type Pointer that receives the watchpoint's type, cast to int.
 * @return 0 on success, -1 if index is out of range or that slot is inactive.
 */
int phys_watchpoint_get(int, uint32_t *, int *);

/* src/cpu/cpu_disasm.c and src/cpu/float.c */

/**
 * @brief Format one raw instruction word as its mnemonic and operand text.
 * @param return_string Buffer that receives the null-terminated disassembly text.
 * @param max_len Size in bytes of return_string, passed to the final snprintf.
 * @param operand The raw 16-bit instruction word to decode.
 */
void OpToStr(char *, uint16_t, uint16_t);

/**
 * @brief Record a decoded instruction word into the disassembly map at addr.
 * @param addr Address the instruction word occupies.
 * @param instr The raw 16-bit instruction word.
 */
void disasm_instr(uint16_t, uint16_t);

/**
 * @brief Record the disassembly text of an EXR-executed instruction at addr.
 * @param addr Address of the EXR instruction whose target is being recorded.
 * @param instr The raw 16-bit instruction word executed via EXR.
 */
void disasm_exr(uint16_t, uint16_t);

/**
 * @brief Allocate a disassembly entry at addr and store its raw word, unverified.
 * @param addr Address to store the word at.
 * @param myword The raw 16-bit word (instruction or data) to record.
 */
void disasm_addword(uint16_t, uint16_t);

/**
 * @brief Clear the whole 65536-entry disassembly map and reset the label counter.
 */
void disasm_init(void);

/**
 * @brief Assign the next sequential label number to the entry at addr.
 * @param addr Address to receive a label number.
 */
void disasm_setlbl(uint16_t);

/**
 * @brief Mark the disassembly entry at addr as data rather than code.
 * @param addr Address to mark as data.
 */
void disasm_set_isdata(uint16_t);

/**
 * @brief Mark addr as using a relative reference to where, labelling where if needed.
 * @param addr Address of the instruction that refers to where.
 * @param where Target address being referenced; allocated and labelled if not
 *              already labelled.
 */
void disasm_userel(uint16_t, uint16_t);

/**
 * @brief Write the full 65536-word disassembly map to /dev/stdout as text.
 */
void disasm_dump(void);

/**
 * @brief Mask instr down to its decoded opcode, resolving multi-level opcode groups.
 * @param instr The raw 16-bit instruction word.
 * @return The masked opcode value used to index the OpToStr switch; for
 *         opcodes the decode tables do not cover, returns instr unchanged
 *         (marked in the source as a case that should not be reached).
 */
uint16_t extract_opcode(uint16_t);

/**
 * @brief Add two 48-bit ND-100 floating point numbers (FAD), serving the 48-bit
 *        FPP (T/A/D three-word format).
 * @param p_a First operand: 3-word {T,A,D} float, e.g. the floating accumulator.
 * @param p_b Second operand: 3-word {T,A,D} float, e.g. the memory operand.
 * @param p_r Output: 3-word {T,A,D} result.
 * @return 0 always.
 */
int NDFloat_Add(uint16_t *, uint16_t *, uint16_t *);

/**
 * @brief Subtract two 48-bit ND-100 floating point numbers (FSB, p_a - p_b),
 *        serving the 48-bit FPP (T/A/D three-word format).
 * @param p_a Minuend: 3-word {T,A,D} float, the floating accumulator.
 * @param p_b Subtrahend: 3-word {T,A,D} float, the memory operand.
 * @param p_r Output: 3-word {T,A,D} result.
 * @return 0 always.
 */
int NDFloat_Sub(uint16_t *, uint16_t *, uint16_t *);

/**
 * @brief Multiply two 48-bit ND-100 floating point numbers (FMU), serving the
 *        48-bit FPP (T/A/D three-word format).
 * @param p_a First operand: 3-word {T,A,D} float, the floating accumulator.
 * @param p_b Second operand: 3-word {T,A,D} float, the memory operand.
 * @param p_r Output: 3-word {T,A,D} result; set to all zero on mantissa
 *            underflow to zero or exponent underflow below -16383.
 * @return 0 always.
 */
int NDFloat_Mul(uint16_t *, uint16_t *, uint16_t *);

/**
 * @brief Divide two 48-bit ND-100 floating point numbers (FDV, p_a / p_b),
 *        serving the 48-bit FPP (T/A/D three-word format).
 * @param p_a Dividend: 3-word {T,A,D} float, the floating accumulator.
 * @param p_b Divisor: 3-word {T,A,D} float, the memory operand.
 * @param p_r Output: 3-word {T,A,D} result. On division by zero, set to the
 *            dividend's sign with maximum exponent and mantissa (0x7FFF/
 *            0xFFFF/0xFFFF pattern); set to all zero on exponent underflow
 *            below -16383.
 * @return 0 on success, 1 if the divisor p_b is zero (division by zero;
 *         caller is expected to set the Z error indicator).
 */
int NDFloat_Div(uint16_t *, uint16_t *, uint16_t *);

/**
 * @brief Normalize the integer in register A into the 48-bit floating
 *        accumulator {T,A,D} (NLZ), serving the 48-bit FPP.
 * @param scaling Signed scaling factor added to the exponent bias; +16 for a
 *                plain integer-to-float conversion.
 */
void DoNLZ(char);

/**
 * @brief Denormalize the 48-bit floating accumulator {T,A,D} into the
 *        integer register A (DNZ), serving the 48-bit FPP. Sets the Z error
 *        indicator (_STS/STS_ERROR_INDICATOR) on overflow; deep downscale underflows to zero
 *        instead of invoking undefined-behaviour shifts.
 * @param scaling Signed scaling factor added to the exponent; -16 for a
 *                plain float-to-integer conversion.
 */
void DoDNZ(char);

/**
 * @brief Add two 32-bit ND-100 floating point numbers (FAD), serving the
 *        optional 32-bit FPP (A,D pair; T register is never written).
 * @param p_a First operand: 2-word {A,D} float, the floating accumulator.
 * @param p_b Second operand: 2-word {A,D} float, the memory operand.
 * @param p_r Output: 2-word {A,D} result.
 * @return 0 always.
 */
int NDFloat_Add32(uint16_t *, uint16_t *, uint16_t *);

/**
 * @brief Subtract two 32-bit ND-100 floating point numbers (FSB, p_a - p_b),
 *        serving the optional 32-bit FPP (A,D pair; T register is never
 *        written).
 * @param p_a Minuend: 2-word {A,D} float, the floating accumulator.
 * @param p_b Subtrahend: 2-word {A,D} float, the memory operand.
 * @param p_r Output: 2-word {A,D} result.
 * @return 0 always.
 */
int NDFloat_Sub32(uint16_t *, uint16_t *, uint16_t *);

/**
 * @brief Multiply two 32-bit ND-100 floating point numbers (FMU), serving
 *        the optional 32-bit FPP (A,D pair; T register is never written).
 * @param p_a First operand: 2-word {A,D} float, the floating accumulator.
 * @param p_b Second operand: 2-word {A,D} float, the memory operand.
 * @param p_r Output: 2-word {A,D} result; set to all zero if either operand
 *            is zero.
 * @return 0 always.
 */
int NDFloat_Mul32(uint16_t *, uint16_t *, uint16_t *);

/**
 * @brief Divide two 32-bit ND-100 floating point numbers (FDV, p_a / p_b),
 *        serving the optional 32-bit FPP (A,D pair; T register is never
 *        written).
 * @param p_a Dividend: 2-word {A,D} float, the floating accumulator.
 * @param p_b Divisor: 2-word {A,D} float, the memory operand.
 * @param p_r Output: 2-word {A,D} result. On division by zero, set to the
 *            dividend's sign with the largest magnitude (0x7FFF or 0xFFFF
 *            with sign bit / 0xFFFF); set to all zero if the dividend is zero.
 * @return 0 on success, 1 if the divisor p_b is zero (division by zero;
 *         caller is expected to set the Z error indicator).
 */
int NDFloat_Div32(uint16_t *, uint16_t *, uint16_t *);

/**
 * @brief Normalize the integer in register A into the 32-bit floating
 *        accumulator A,D pair (NLZ), serving the optional 32-bit FPP; the T
 *        register is deliberately never touched, which is what the 32-vs-48
 *        bit FPP detection sequence keys on.
 * @param scaling Signed scaling factor added to the exponent bias; +16 for a
 *                plain integer-to-float conversion.
 */
void DoNLZ32(char);

/**
 * @brief Denormalize the 32-bit floating accumulator A,D pair into the
 *        integer register A (DNZ), serving the optional 32-bit FPP; the T
 *        register is never touched. Sets the Z error indicator (_STS/STS_ERROR_INDICATOR) on
 *        overflow. Behaviour for scaling factors other than -16 is an
 *        unverified known gap per the source comment.
 * @param scaling Signed scaling factor added to the exponent; -16 for a
 *                plain float-to-integer conversion.
 */
void DoDNZ32(char);

/* src/cpu/bcd.c */

/**
 * @brief ADDD - Add Decimal (140 120): add the X.T packed decimal operand
 * into the A.D packed decimal operand, storing the sum in A.D. Skips
 * (P+2) on success; on decimal overflow takes the error return (P+1)
 * with the truncated low digits already stored (RASK ADDE/OVFLO, CS
 * 010520-011054).
 * @param instr Unused; the instruction word (opcode only, no operand fields).
 */
void opcode_addd_add_two_decimal_operands(uint16_t);

/**
 * @brief SUBD - Subtract Decimal (140 121): subtract the X.T packed
 * decimal operand from the A.D packed decimal operand, storing the
 * difference in A.D. Shares the ADDE driver with ADDD after flipping
 * the X.T operand's sign (RASK CS 010520). Skips (P+2) on success; on
 * decimal overflow takes the error return (P+1) with the truncated low
 * digits already stored.
 * @param instr Unused; the instruction word (opcode only, no operand fields).
 */
void opcode_subd_subtract_two_decimal_operands(uint16_t);

/**
 * @brief COMD - Compare Decimal (140 122): compare the A.D packed
 * decimal operand against the X.T packed decimal operand, aligning
 * both on their decimal points, and set A to 1, 0, or 0xFFFF for
 * A.D > X.T, A.D == X.T, or A.D < X.T. Neither operand is modified.
 * Always takes the skip return (P+2).
 * @param instr Unused; the instruction word (opcode only, no operand fields).
 */
void opcode_comd_compare_two_decimal_operands(uint16_t);

/**
 * @brief SHDE - Decimal Shift (140 126): move the A.D packed decimal
 * operand into the X.T packed decimal operand's field, shifting its
 * digits left or right by the difference between the two operands'
 * decimal points (RASK CS 011121-011360). Rounds the X.T result on a
 * right shift when its D2 rounding bit is set. Skips (P+2) on success;
 * if a significant digit is lost off the most significant end of X.T,
 * takes the error return (P+1) with the surviving low digits still
 * stored.
 * @param instr Unused; the instruction word (opcode only, no operand fields).
 */
void opcode_shde_decimal_shift(uint16_t);

/**
 * @brief PACK - Convert to packed decimal (140 124): convert the A.D
 * ASCII coded decimal (unpacked) operand to packed decimal format and
 * store the result in the X.T operand's field (RASK CS 011533). Skips
 * (P+2) on success; on an illegal ASCII digit/sign code or decimal
 * overflow, reports the error code in D bits 0-4 (and, for an illegal
 * code, sets bit 15 of both A and D) and takes the error return (P+1).
 * @param instr Unused; the instruction word (opcode only, no operand fields).
 */
void opcode_pack_convert_to_decimal(uint16_t);

/**
 * @brief UPACK - Convert to unpacked decimal (140 125): convert the
 * A.D packed decimal operand to ASCII coded decimal (unpacked) format
 * and store the result in the X.T operand's field. Unlike ADDD/SUBD/
 * SHDE, validates the source's sign and digit nibbles. Skips (P+2) on
 * success; on an illegal packed code or decimal overflow, reports the
 * error code in D bits 0-4 (and, for an illegal code, sets bit 15 of
 * both A and D) and takes the error return (P+1).
 * @param instr Unused; the instruction word (opcode only, no operand fields).
 */
void opcode_unpack_convert_from_decimal(uint16_t);

#endif // CPU_TYPES_H
