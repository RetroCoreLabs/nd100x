/*
 * nd100x - ND100 Virtual Machine
 *
 * Swapping DRUM controller for NORD TSS (IOX 540-547).
 *
 * This device emulates the drum that the TSS XDRUM/TRSFR driver programs
 * (TSS1.SYMB:3695-3874). The wire protocol is documented in
 * <TSS>\docs\DRUM-DEVICE-SPEC.md - every constant below cites the
 * evidence (VERIFIED) or is marked PROVISIONAL where the source did not pin it
 * down (to be confirmed on the DAP once TSS reaches a live drum transfer).
 *
 * The drum is essentially a cut-down SMD controller: identical register
 * offsets (1=load core, 3=load block, 4=read status, 5=load control,
 * 7=load word count), same block-DMA transfer model, but drum (sector/track)
 * addressing instead of CHS and only two meaningful status bits.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef DEVICE_DRUM_H
#define DEVICE_DRUM_H

#include <stdint.h>
#include <stdio.h>
#include "../devices_types.h"

/* --- Register offsets within the 540-547 block (address - startAddress) ---
 * [VERIFIED] DRUM-DEVICE-SPEC.md section 1, TSS1.SYMB:3763-3769,3803,3838. */
typedef enum {
    DRUM_REG_READ_CORE    = 0, /* 540 RCX - defined, never issued by XDRUM */
    DRUM_REG_LOAD_CORE    = 1, /* 541 LCX - load core (memory) address     */
    DRUM_REG_LOAD_BLOCK   = 3, /* 543 LBX - load drum block address        */
    DRUM_REG_READ_STATUS  = 4, /* 544 RSX - read status register           */
    DRUM_REG_LOAD_CONTROL = 5, /* 545 LCR - load control (triggers go)     */
    DRUM_REG_LOAD_WORDCNT = 7  /* 547 LWX - load word count                */
} DrumRegister;

/* --- Status register bit MASKS (the values the driver BSKP-tests) ---
 * [VERIFIED] TSS1.SYMB:3773-3774,3784,3786. The driver branches on exactly
 * these two bits; all others are propagated to the OS but never tested. */
#define DRUM_STATUS_DVA 0020 /* DEVICE ACTIVE - 1 while a transfer is busy */
#define DRUM_STATUS_ERR 0040 /* inclusive OR of all error conditions      */

/* --- Control word decode ---
 * [VERIFIED] DRUM-DEVICE-SPEC.md section 2.2:
 *   bits 0-2  = 7  -> activate transfer AND enable completion interrupt
 *   bits 5-6  = core address bits 16-17 (extended memory address)
 *   bits 13-14 = function: 0 read, 1 write, 2 read-test, 3 compare
 * On a transfer error the driver instead writes control = 4 (bit 2 alone). */
#define DRUM_CTRL_GO_MASK    07     /* (control & 7)==7 -> start + interrupt   */
#define DRUM_CTRL_GO_VALUE   07
#define DRUM_CTRL_CLEAR      04     /* control==4 on error -> stop/clear       */
#define DRUM_CTRL_ADDR_HI(c) (((c) >> 5) & 03)   /* core addr bits 16-17      */
#define DRUM_CTRL_FUNC(c)    (((c) >> 13) & 03)   /* device operation, 2 bits */

typedef enum {
    DRUM_FUNC_READ      = 0, /* drum -> memory                    */
    DRUM_FUNC_WRITE     = 1, /* memory -> drum                    */
    DRUM_FUNC_READ_TEST = 2, /* read drum, no memory store        */
    DRUM_FUNC_COMPARE   = 3  /* read drum, compare with memory    */
} DrumFunction;

/* --- Drum block address decode ---
 * [VERIFIED] TSS1.SYMB:3808-3812 comments: sector in bits 15-11, track in
 * bits 10-0 of the LBX word. */
#define DRUM_BLOCK_SECTOR(b) (((b) >> 11) & 037)  /* 5 bits: 0..31           */
#define DRUM_BLOCK_TRACK(b)  ((b) & 03777)         /* 11 bits: 0..2047        */

/* --- Geometry ---
 * [VERIFIED] 32 sectors/track (TSS1.SYMB); DRMSZ default = 2000 octal = 1024
 * pages (build variant / docs/BUILD-TSS.md). One page = 2048 words, so a track
 * holds one page.
 * [PROVISIONAL] 64 words/sector is INFERRED (32*64 = 2048 = one page); the
 * source does not state the sector word count explicitly. Confirm on DAP. */
#define DRUM_SECTORS_PER_TRACK 32
#define DRUM_WORDS_PER_SECTOR  64   /* PROVISIONAL - see note above           */
#define DRUM_WORDS_PER_TRACK   (DRUM_SECTORS_PER_TRACK * DRUM_WORDS_PER_SECTOR)

/* DRMSZ logical size (TSS1.SYMB:46 "SIZE OF DRUM IN 256 WORD PAGES"):
 * 1024 x 256 = 262144 words = 512 KiB. This is TSS's *configured* drum size. */
#define DRUM_PAGES             1024 /* DRMSZ = 2000 octal                     */
#define DRUM_DRMSZ_WORDS       ((uint32_t)DRUM_PAGES * 256u)

/* Physical MAX size = the full 16-bit block address space the LBX register can
 * express: 65536 blocks (sector 5 bits + track 11 bits) x 64 words/sector =
 * 4194304 words = 8 MiB. The emulated surface and the drum.img backing file use
 * this MAX so any address the driver can form is in range (no spurious ERR). */
#define DRUM_MAX_BLOCKS        65536u
#define DRUM_MAX_WORDS         (DRUM_MAX_BLOCKS * (uint32_t)DRUM_WORDS_PER_SECTOR)

/* --- Interrupt ---
 * [VERIFIED-by-user] all ND HDD controllers share the same interrupt level,
 * so the drum uses SMD's level 11.
 * [PROVISIONAL] IDENT code: the TSS source enables the interrupt but binds no
 * IDENT that the source analysis could find. 024 is chosen unique versus the
 * SMD variants (017/020/023/06) and floppy (21); confirm on DAP. */
#define DRUM_INT_LEVEL  11
#define DRUM_IDENT_CODE 024 /* PROVISIONAL - confirm against TSS level-11 handler */

/* Per-device state hung off Device.deviceData. */
typedef struct {
    uint32_t coreAddress;   /* LCX + control bits 16-17, 18-bit DMA address  */
    uint16_t blockAddress;  /* LBX: sector(15-11) | track(10-0)              */
    uint16_t wordCount;     /* LWX                                           */
    uint16_t control;       /* last LCR value                                */
    uint16_t status;        /* RSX: DVA / ERR                                */
    bool interruptEnabled;  /* set when a go (control&7==7) is written        */

    uint16_t *surface;      /* the drum image, DRUM_TOTAL_WORDS words         */
    uint32_t surfaceWords;
    FILE *backingFile;      /* optional persistence; NULL = in-memory only    */
} DrumData;

/* Set the drum backing-image path used by the NEXT CreateDrumDevice() call.
 * Pass NULL for an in-memory-only drum. If the file does not exist it is created
 * and zero-filled to the full DRUM_MAX_WORDS size; if it exists it is loaded.
 * The drum.img format is raw big-endian 16-bit words (ND word order), which is
 * the same order mac-c writes, so the two agree. */
void DrumDevice_SetBackingFile(const char *path);

/* Factory. thumbwheel selects the device address block; 0 -> 540. */
Device *CreateDrumDevice(uint8_t thumbwheel);

#endif /* DEVICE_DRUM_H */
