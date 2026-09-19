/*
 * device_cdc.h - CDC/NCR cartridge disc controller (IOX 500-507): register map and API.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * CDC / NCR cartridge system-disc controller for NORD TSS (IOX 500-507).
 *
 * This is the "cartridge disc" controller (CDC 9427 "Hawk") that Norsk Data
 * always places at device numbers 500-507, IDENT code 01, interrupt level 11.
 * It is a DIFFERENT and OLDER controller than the ECC/SMD "Big Disc" (nd100x's
 * deviceSMD at 1540). Do NOT copy the SMD CHS/seek/ECC geometry model.
 *
 * TSS reads its code OVERLAYS from this disc: the level-5 reader S5
 * (TSS1.SYMB:3069-3073) computes disc-sector = OVLAY*2 + OVDK (OVDK=160 octal)
 * and issues a 256-word READ into core at ROVER / ROV4.
 *
 * ---------------------------------------------------------------------------
 * AUTHORITATIVE SOURCES for the register / bit model in this file:
 *   [MANUAL-N10]  ND-11.008.01 "CARTRIDGE DISC SYSTEM FOR NORD-10",
 *                 Dec 1973 / Rev.A 1976. This is the controller TSS actually
 *                 drives, so it is the PRIMARY reference.
 *                 <NDInsight>\Reference-Manuals\10\
 *                 ND-11.008.01 CARTRIDGE DISC SYSTEM FOR NORD-10.md
 *   [MANUAL-N100] ND-06.016.01 "NORD-100 Input/Output System" pp.188-190,
 *                 the later ND-100 generation of the same 500-507 disc, used
 *                 here only for the fuller status-word bit names.
 *                 <NDInsight>\Reference-Manuals\
 *                 ND-06.016.01_NORD-100_Input_Output_System.md (~L6138-6270)
 *   [TSS]         TSS's own CDC driver - the behavioural ground truth for the
 *                 handful of bits it actually reads (busy/error/on-cylinder).
 *
 * Every claim below is tagged [VERIFIED] (manual line or TSS file:line) or
 * [INFERRED]. Where a manual bit and what TSS reads could differ, TSS wins and
 * the manual bit is noted. See <TSS>\docs\CDC-DISC-DEVICE.md.
 *
 * Structural template: src/devices/drum/device_drum.c + src/devices/smd/device_smd.c
 * (block-DMA, QueueIODelay completion, interrupt/Ident plumbing). The bit-field
 * unions below are modelled on device_smd.h's SMDControlRegister/SMDStatusRegister.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef DEVICE_CDC_H
#define DEVICE_CDC_H

#include <stdint.h>
#include <stdio.h>
#include "../devices_types.h"

/* --- Register offsets within the 500-507 block (address - startAddress) -----
 * [VERIFIED - MANUAL-N10 p.13 "DISC DEVICE REGISTER ADDRESSES"] and the later
 * [MANUAL-N100 p.188]. Both agree on the layout; the names here use the manual
 * function names. For disc system II add 010 to every code (the manual note).
 *
 * [VERIFIED - TSS1.SYMB:3356-3364] the "CDC N10" branch (DCHN=500) wires the
 * exact same offsets: LCA=+1 LBA=+3 LCW(=LMR)=+5 RST=+4 RCA=+0 RSECT=+2
 * SEEK=+6 LWC=+7.
 *
 * The nd100x IOX model routes EVEN device numbers through Read() and ODD through
 * Write() (io.c parity split), which matches this map exactly: every "read"
 * register (RCA/RSECT/RST/SEEK) is even, every "load" register (LCA/LBA/LCW/LWC)
 * is odd. */
// clang-format off
typedef enum {
    CDC_REG_RCA   = 0, /* IOX 500  Read Core Address register     (even -> Read)  */
    CDC_REG_LCA   = 1, /* IOX 501  Load Core Address register     (odd  -> Write) */
    CDC_REG_RSECT = 2, /* IOX 502  Read Sector Counter            (even -> Read)  */
    CDC_REG_LBA   = 3, /* IOX 503  Load Block (disc) Address       (odd  -> Write) */
    CDC_REG_RST   = 4, /* IOX 504  Read Status Register            (even -> Read)  */
    CDC_REG_LCW   = 5, /* IOX 505  Load Control Word (starts xfer) (odd  -> Write) */
    CDC_REG_SEEK  = 6, /* IOX 506  Seek / Read Block Address(test) (even -> Read)  */
    CDC_REG_LWC   = 7  /* IOX 507  Load Word Count Register        (odd  -> Write) */
} CdcRegister;
// clang-format on

/* NORD-1 device numbers for the same controller, reached with IOT instead of
 * IOX. DCHN=100 on NORD-1 (TSS1.SYMB:43), so DISC=DCHN+44 and DCT=DCHN+45. */
// clang-format off
enum {
    CDC_N1_DISC = 0144,  /* start transfer / ready test  */
    CDC_N1_DCT  = 0145   /* control port, function bits select the register */
};
// clang-format on

/* --- Control Word (LCW = IOX 505) bit model --------------------------------
 * [VERIFIED - MANUAL-N10 p.14-15 "Load Control Word (CW)"]. Modelled as a
 * bit-field union exactly like device_smd.h's SMDControlRegister. ALL fields are
 * uint16_t so the `raw` overlay stays valid under MinGW -mms-bitfields (see the
 * PANS/PANC note in panel.h / SMDControlRegister: mixed-type bit-fields split
 * into separate storage units on Windows and break the overlay).
 *
 * NOTE on device operation - the operation is a plain 2-bit field at control-word
 * bits 11-12, per the ND manual (ND-11.008.01 / ND-06.016.01, "Load Control Word"):
 *     00 = read, 01 = write, 10 = read-parity, 11 = compare.
 * So a real READ control word is 000004 (op 0 + activate at bit 2), WRITE 002004,
 * read-parity 004004, compare 006004. Decode: op = (raw >> 11) & 3.
 *
 * The CDC overlay LOAD path is DKOP -> DKTR (TSS1.SYMB:3458, "CDC N10"), which now
 * builds the control word as
 *     LDA DKSVA; SHA 13; AAA 4; IOX LMR      (SHA 13 = 13 OCTAL = 11 decimal)
 * With the SHR/SHA shift now assembled correctly, DKSVA=0 for a read, so the read
 * control word is 000004 (op 0 at bits 11-12) - matching the manual.
 * [VERIFIED live] IOX 505 now writes 000004 (activate + op 0) for the overlay READ.
 *
 * HISTORY: an earlier version decoded the operation as an (op+1)<<12 field at bits
 * 12-14, with a "read control word = 010004". That was an ARTIFACT of a mac-c SHR
 * compiler bug (the shift was mis-encoded), which made DKTR emit 010004 for a read.
 * That compiler bug is now fixed, so DKTR emits the manual-correct 000004 and the
 * (op+1)<<12 model must NOT be used: it decodes 000004 as op 3 (compare), runs the
 * compare path, never DMAs, and hangs the boot in DWAIT. Back to bits 11-12. */
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t enableInterruptReady : 1; /* bit 0  : int on device ready-for-transfer  */
        uint16_t enableInterruptError : 1; /* bit 1  : int on errors                      */
        uint16_t activate             : 1; /* bit 2  : ACTIVATE device (starts the xfer)  */
        uint16_t testMode             : 1; /* bit 3  : test mode (pre-wired self-test)     */
        uint16_t deviceClear          : 1; /* bit 4  : device clear (clear active + error) */
        uint16_t addressBit16         : 1; /* bit 5  : core address bit 16 (extension)     */
        uint16_t addressBit17         : 1; /* bit 6  : core address bit 17 (extension)     */
        uint16_t unassigned7_8        : 2; /* bits 7-8 : not assigned                      */
        uint16_t unitSelect           : 2; /* bits 9-10 : unit select (0..3)               */
        uint16_t deviceOperation      : 2; /* bits 11-12 : 00=read 01=write 10=parity 11=cmp (manual) */
        uint16_t unassigned13_14      : 2; /* bits 13-14 : not assigned                    */
        uint16_t writeFormat          : 1; /* bit 15 : write format (write sector tags)    */
    } bits;
} CdcControlRegister;
// clang-format on

/* Convenience masks for the control word (same bit positions as the union).
 * Kept so the transfer engine and the unit tests can build/inspect the word
 * without depending on bit-field storage order. */
#define CDC_CTRL_INT_READY  0000001u /* bit 0                                    */
#define CDC_CTRL_INT_ERROR  0000002u /* bit 1                                    */
#define CDC_CTRL_ACTIVATE   0000004u /* bit 2  : ACTIVATE - starts the transfer  */
#define CDC_CTRL_TEST       0000010u /* bit 3  : test mode                       */
#define CDC_CTRL_DEVCLEAR   0000020u /* bit 4  : device clear                    */
#define CDC_CTRL_ADDR16     0000040u /* bit 5  : core address bit 16             */
#define CDC_CTRL_ADDR17     0000100u /* bit 6  : core address bit 17             */
#define CDC_CTRL_UNIT_SHIFT 9
#define CDC_CTRL_UNIT_MASK  0003000u /* bits 9-10                                */
/* Operation is a plain 2-bit field at bits 11-12 (control = op<<11 | activate),
 * per ND-11.008.01 / ND-06.016.01. To build/decode: field = op; op = (raw>>11)&3. */
// clang-format off
#define CDC_CTRL_OP_SHIFT   11
#define CDC_CTRL_OP_MASK    0030000u /* bits 11-12 : holds the 2-bit operation   */
#define CDC_CTRL_WRFORMAT   0100000u /* bit 15                                   */
// clang-format on

/* Device operation code. The enum values ARE the 2-bit field carried in control
 * bits 11-12 (op<<11 | activate) - see the header NOTE above and the ND manual. */
// clang-format off
typedef enum {
    CDC_OP_READ        = 0, /* control 000004 : disc -> core (overlay load)  */
    CDC_OP_WRITE       = 1, /* control 002004 : core -> disc                 */
    CDC_OP_READ_PARITY = 2, /* control 004004 : verify CRC, no transfer      */
    CDC_OP_COMPARE     = 3  /* control 006004 : compare disc vs core         */
} CdcOperation;
// clang-format on

/* --- Status Register (RST = IOX 504) bit model -----------------------------
 * [VERIFIED - MANUAL-N100 p.190 "Status Word"] (the fuller bit names) which
 * agrees with [MANUAL-N10 p.17 "Read Status Register (STR)"]. Modelled as a
 * bit-field union like device_smd.h's SMDStatusRegister; all fields uint16_t.
 *
 * TSS reads only three of these bits, and those are the behavioural ground
 * truth [TSS]:
 *   bit 2  active     : DWAIT loops while set  (TSS1.SYMB:3906 BSKP ONE 20 DA)
 *   bit 4  errorOr    : error exit             (TSS2.SYMB:555  BSKP ZRO 40 DA)
 *   bit 14 onCylinder : pre-transfer ready poll(TSS2.SYMB:549  BSKP ONE 160 DA)
 * (ND bit-skip operand = bit-number<<3: 020>>3=2, 040>>3=4, 0160>>3=14.) */
// clang-format off
typedef union {
    uint16_t raw;
    struct {
        uint16_t readyIntEnabled   : 1; /* bit 0  : ready-for-transfer, int enabled    */
        uint16_t errorIntEnabled   : 1; /* bit 1  : error interrupt enabled            */
        uint16_t active            : 1; /* bit 2  : DEVICE ACTIVE (BUSY) - TSS reads    */
        uint16_t readyForTransfer  : 1; /* bit 3  : device ready/finished               */
        uint16_t errorOr           : 1; /* bit 4  : inclusive OR of errors - TSS reads   */
        uint16_t writeProtect      : 1; /* bit 5  : write protect violate               */
        uint16_t timeOut           : 1; /* bit 6  : time out                            */
        uint16_t hardwareError     : 1; /* bit 7  : missing clock/disk fault/seek error  */
        uint16_t addressMismatch   : 1; /* bit 8  : address mismatch                    */
        uint16_t parityError       : 1; /* bit 9  : parity error                        */
        uint16_t compareError      : 1; /* bit 10 : compare error                       */
        uint16_t dmaError          : 1; /* bit 11 : DMA error / missing read clocks      */
        uint16_t transferComplete  : 1; /* bit 12 : transfer complete (WC = 0)           */
        uint16_t transferOn        : 1; /* bit 13 : transfer on                         */
        uint16_t onCylinder        : 1; /* bit 14 : ON CYLINDER (READY) - TSS reads      */
        uint16_t loadedByPrevCw    : 1; /* bit 15 : bit 15 loaded by previous control wd */
    } bits;
} CdcStatusRegister;
// clang-format on

/* Status masks for the three TSS-verified bits (kept for the engine and tests).
 * These names match the historical nd100x usage; the union above is the full
 * model. BUSY == active(bit2), ERR == errorOr(bit4), READY == onCylinder(bit14). */
#define CDC_STATUS_BUSY  0000004u /* bit 2  : device active (transfer in progress) */
#define CDC_STATUS_ERR   0000020u /* bit 4  : error (inclusive OR of error causes)  */
#define CDC_STATUS_READY 0040000u /* bit 14 : on cylinder / ready-for-transfer      */

/* --- Test mode (control word bit 3) ----------------------------------------
 * [VERIFIED - MANUAL-N10 p.14 "Bit 3: Test Mode"]: with test mode set, a Read
 * Transfer moves PRE-WIRED data to core - even words 125252 (octal), odd words
 * 052525 (octal). "In order to get a transfer successfully during test, the
 * block address register has to be specified with the content 125252." */
#define CDC_TESTMODE_BLOCK 0125252u /* required LBA for a successful test read     */
#define CDC_TESTMODE_EVEN  0125252u /* even-indexed words returned in test read     */
#define CDC_TESTMODE_ODD   0052525u /* odd-indexed  words returned in test read     */

/* --- Geometry ---
 * [VERIFIED] The overlay reader loads exactly 0400 octal = 256 words per
 * transfer (XDISK N10 TSS2.SYMB:552 "LDA (400; IOX LWC"), and ROVER is BSS 1000
 * = 512 words = two 256-word sectors (OVERLAY-DISC-SPEC.md sec 3). The backing
 * image is a raw big-endian word image in which linear sector S occupies bytes
 * [S*256*2, +512) - matching what mac-as -c writes.
 *
 * [INFERRED note] MANUAL-N10 p.6 gives the real CDC 9427 sector as 128 data
 * words + 1 CRC word. TSS's overlay contract uses 256-word transfers into a
 * linear sector store, so we model 256-word sectors (the TSS ground truth), not
 * the raw physical 128-word CDC sector. */
#define CDC_WORDS_PER_SECTOR 256u
#define CDC_SECTOR_BYTES     (CDC_WORDS_PER_SECTOR * 2u)

/* Default emulated surface when no (or a small) backing file is attached.
 * The running TSS driver runs each logical overlay sector through DKADR
 * (logical -> physical) before the read. The CORRECTED DKADR is
 *   DKADR(L) = 32*floor(L/12) + 2*(L mod 12)
 * (a base-12 track*32 + 2*sector repack; 12 sectors per track), VERIFIED by
 * single-stepping the running DKADR under nd100x --mms1 --trace: DKADR(0244)
 * = 32*13 + 2*8 = 432 = 0o660, and IOX 503 loads exactly A=000660. This
 * supersedes two earlier wrong formulas: 72*L (RGDIV divide undefined) and
 * 8*floor(L*65537/12)+64*L = 077300 (the mac-c assembler had miscompiled the
 * "SHR" shift modifier as an additive LEFT shift; fixed in mac-c/mac.c
 * eval_expr, ND-60.096.01 sec 2.3.8). The corrected map is DENSE and small:
 * the overlay logical range 0160..0257 packs into physical sectors 0450..0712
 * (max 458 dec). 512 sectors (256 KiB) covers it with headroom; the surface
 * still grows to a larger backing file if one is attached. */
#define CDC_DEFAULT_SECTORS 512u /* covers corrected DKADR overlay range (max phys 458) */

/* --- Bus identity ---
 * [VERIFIED - MANUAL-N10 p.20 and MANUAL-N100 p.190] "The disc interrupt level
 * is 11 and the ident number for the first disc system is 1." TSS polls via
 * DWAIT rather than relying on the interrupt, but a completion interrupt on
 * level 11 is modelled for correctness (and asserted by the unit test). */
#define CDC_IDENT_CODE 001
#define CDC_INT_LEVEL  11

/* Per-device state hung off Device.deviceData. */
// clang-format off
typedef struct {
    uint16_t coreAddrLow;    /* LCA: least-significant 16 bits of the core address */
    uint8_t  coreAddrHigh;   /* high 8 bits (ND-100 24-bit variant; 0 for TSS)     */
    uint8_t  rcaReadPhase;   /* RCA two-read phase: 0 -> low16 next, 1 -> high8    */
    uint16_t blockAddress;   /* LBA: linear 256-word disc-sector number (see note) */
    uint16_t wordCount;      /* LWC: number of 16-bit words to transfer            */
    CdcControlRegister control; /* LCW: last control word written                  */
    CdcStatusRegister  status;  /* RST: active/onCylinder/errorOr/...              */
    uint16_t sectorCounter;  /* RSECT: last sector touched (informational)         */
    bool     interruptEnabled;

    uint16_t *surface;       /* the disc image, surfaceSectors*256 words           */
    uint32_t surfaceSectors; /* number of 256-word sectors in surface              */
    uint32_t surfaceWords;   /* surfaceSectors * 256                               */
    FILE    *backingFile;    /* optional persistence; NULL = in-memory only        */
} CdcData;
// clang-format on

/**
 * @brief Set the CDC backing-image path used by the NEXT CreateCdcDevice() call.
 *        If the file does not exist it is created and zero-filled; if it exists
 *        it is loaded and the surface grown to fit it. The image is raw
 *        big-endian 16-bit words (ND word order), sector S at byte offset S*512.
 * @param path Path to the backing image, or NULL/empty for an in-memory-only disc.
 */
void CdcDevice_SetBackingFile(const char *path);

/**
 * @brief Create and initialize a CDC/NCR cartridge disc device.
 * @param thumbwheel Card thumbwheel; selects the IOX address block (0 -> 500).
 * @return The new Device, or NULL on allocation failure.
 */
Device *CreateCdcDevice(uint8_t thumbwheel);

/*
 * ------------------------------------------------------------------------------
 * ADDRESSING NOTE - the device is a DUMB linear-by-PHYSICAL-sector store
 * ------------------------------------------------------------------------------
 * The running TSS driver converts the logical overlay sector (OVLAY*2+OVDK) to a
 * PHYSICAL disc address via DKADR (TSS1.SYMB:3563-3623) BEFORE loading LBA
 * (TSS2.SYMB:551 "JPL I (DKADR ... IOX LBA"). So the value that actually arrives
 * in the LBA register is already the PHYSICAL sector. Therefore this device
 * correctly treats the LBA value as a LINEAR PHYSICAL sector number: physical
 * sector S lives at byte offset S*512 (cdc_lba_to_sector is the identity). DKADR
 * is applied ONCE, at overlay-WRITE time, by mac-as -c (mac-c cdc_dkadr). Do NOT
 * add DKADR here: that would apply the conversion twice.
 *
 * ------------------------------------------------------------------------------
 * CORE-ADDRESS WIDTH - 18-bit (this NORD-10 controller) vs 24-bit (ND-100)
 * ------------------------------------------------------------------------------
 * [VERIFIED - MANUAL-N10 p.14] On the NORD-10 CDC 9427 the core address is 18
 * bits: a single 16-bit Load Core Address (IOX 501) plus control-word bits 5-6
 * as address bits 16-17. TSS drives exactly this: one IOX LCA write, and it
 * never sets control bits 5-6 (overlay core addresses are small), so the
 * effective address == the 16-bit LCA value. This device models that 18-bit
 * assembly (cdc_effective_core()).
 *
 * [INFERRED / not used by TSS - MANUAL-N100 p.188] The later ND-100 generation
 * widened this to 24 bits via TWO consecutive accesses: Load Core Address is
 * high-8 then low-16; Read Core Address is low-16 then high-8, the sequence
 * re-initialised by a read-status/device-clear/master-clear. We DO model the
 * two-read form of Read Core Address (a single read yields the low 16 bits, a
 * second consecutive read yields the high 8 - harmless to any single-read
 * caller). We deliberately do NOT model a blind two-WRITE Load Core Address:
 * TSS issues a single LCA write, so treating the first write as "high-8 only"
 * would corrupt the address. Load Core Address is therefore a single 16-bit
 * write here, which is [VERIFIED] correct for the NORD-10 controller TSS drives.
 * ------------------------------------------------------------------------------
 */

#endif /* DEVICE_CDC_H */
