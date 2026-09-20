/*
 * ndlib_types.h - ndlib shared types: keyboard events and memory access declarations.
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


#ifndef NDLIB_TYPES_H
#define NDLIB_TYPES_H

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>


// ** KEYBOARD EVENTS **
// Classified keyboard input produced by read_key_event(). Placed here rather
// than in keyboard.h so the mkptypes-generated ndlib_protos.h - which declares
// read_key_event() - can see the full definition regardless of include order.
typedef enum
{
    KEY_NONE = 0,  // No key available
    KEY_CHAR,      // Ordinary typed character (evt.ch)
    KEY_ESCAPE,    // ESC key pressed alone
    KEY_F12,       // F12 function key
    KEY_ALT_DIGIT, // Alt+1..9 (evt.ch is '1'..'9')
    KEY_UNKNOWN,   // Multi-byte input we did not classify (raw in evt.seq)
} KeyType;

typedef struct
{
    KeyType type;
    char ch;     // Ordinary character (KEY_CHAR) or Alt digit (KEY_ALT_DIGIT)
    char seq[8]; // Raw byte sequence - populated for passthrough/KEY_UNKNOWN
    int seqLen;
} KeyEvent;


// Physical memory functions in cpu_mms.c
extern int ReadPhysicalMemory(int physical_address, bool privileged);
extern void WritePhysicalMemory(int physical_address, uint16_t value, bool privileged);
/* Also provided by the cpu module: the -a disassembler records each word a
 * loader deposits (cpu_disasm.c; declared for the cpu module in cpu_types.h). */
extern int g_disasm;
void disasm_addword(uint16_t addr, uint16_t myword);


// ** LOGGING ** (categories, levels and LOG() live in log.h)
#include "log.h"

// ** BPUN **
typedef struct
{
    uint16_t start;              /* octal start address for the program */
    uint16_t boot;               /* octal value giving the start address of the bootstrap loader */
    uint16_t address;            /* Address where the binary load of the data will start */
    uint16_t checksum;           /* Checksum value */
    uint16_t calculatedChecksum; /* Calculated checksum for verification */
    uint16_t action;             /* Action field - if zero, execution starts at start address */
    uint16_t count;              /* Number of 16-bit words in code stream */
    bool isFloMon;               /* Is this FloMon format (floppy-boot sector) */
} BPUN_Header;

// ** PROG (SINTRAN :PROG loadable image) **
// 512-byte header block: 6 big-endian 16-bit words, then padding.
//   Bank 1 data starts at file offset 512.
//   For 2-bank images a second header block starts at file offset 0x20000
//   (131072) and Bank 2 data at 0x20200 (131584); Bank 2 is meant to be reached
//   through the ALTERNATIVE page table (the program issues MON ALTON).
//   A 1-bank image has firstBank2 == 0xFFFF and lastBank2 == 0x0000 (no data)
//   and omits the second header/data section.
typedef struct
{
    uint16_t startAddress;   /* entry point (P register) */
    uint16_t restartAddress; /* restart entry */
    uint16_t firstBank1;     /* first word address of Bank 1 image */
    uint16_t lastBank1;      /* last  word address of Bank 1 image */
    uint16_t firstBank2;     /* first word address of Bank 2 image (0xFFFF if none) */
    uint16_t lastBank2;      /* last  word address of Bank 2 image (0x0000 if none) */
    bool twoBank;            /* true when a Bank 2 image is present */
} PROG_Header;

typedef enum
{
    LOAD_STATE_PREAMBLE,
    LOAD_STATE_ADDRESS,
    LOAD_STATE_COUNT,
    LOAD_STATE_DATA,
    LOAD_STATE_CHECKSUM,
    LOAD_STATE_ACTION,
    LOAD_STATE_FLO_MON_COUNT,
    LOAD_STATE_FLO_MON_LOAD
} LoadState;


/**
 * @brief Copy the header of the most recently loaded BPUN file into *out.
 * @details LoadBPUN() only returns the obsolete bootstrap-loader "boot"
 *          address, so callers that need the real program entry ("start") or
 *          the "action" autostart flag read them from here.
 * @param out Receives the cached header; untouched when the call fails.
 * @return true when a BPUN has been loaded successfully at least once and out
 *         is not NULL, false otherwise.
 */
bool GetLastBPUNHeader(BPUN_Header *out);

/**
 * @brief Load a BPUN (boot program unprotected) file into physical memory and
 *        cache its header for GetLastBPUNHeader().
 * @param filename Path of the BPUN file, opened with mode "rb".
 * @param verbose  true logs the parsed header fields and the checksum result.
 * @return The bootstrap-loader "boot" address from the file on success, 0 if
 *         the file could not be opened, and -1 if parsing failed.
 */
int LoadBPUN(const char *filename, bool verbose);

/**
 * @brief Binary load from a boot device. Not implemented - the argument is
 *        ignored and the call always fails.
 * @param bpfile Name of the device or file to load from.
 * @return -1 always.
 */
int bp_load(const char *bpfile);

/**
 * @brief Copy the header of the most recently loaded :PROG file into *out.
 * @param out Receives the cached header; untouched when the call fails.
 * @return true when a :PROG has been loaded successfully at least once and
 *         out is not NULL, false otherwise.
 */
bool GetLastPROGHeader(PROG_Header *out);

/**
 * @brief Load a SINTRAN :PROG image into physical memory and cache its header
 *        for GetLastPROGHeader().
 * @details Reads the six big-endian 16-bit header words, then the Bank 1 words
 *          from file offset 512 into addresses firstBank1..lastBank1. A 2-bank
 *          image is detected but Bank 2 is NOT loaded, because it belongs in
 *          the alternative page table, which nd100x does not map separately;
 *          only Bank 1 is loaded and a message is logged.
 * @param filename Path of the :PROG file, opened with mode "rb".
 * @param verbose  true logs the header fields and the loaded ranges.
 * @return The program start address (P register) on success, -1 if the file
 *         could not be opened, the header was short, a seek failed, or a bank
 *         could not be read.
 */
int LoadPROG(const char *filename, bool verbose);

/**
 * @brief Put the console into cbreak mode so keys arrive one at a time without
 *        being echoed.
 * @details POSIX: saves the current termios of file descriptor 0, then clears
 *          ECHO, ECHONL, ICANON and IEXTEN and sets VMIN and VTIME to 0 for a
 *          non-blocking read; SIGTTOU is ignored so the call also works from a
 *          background process. Windows: saves the console mode of the standard
 *          input handle, then clears ENABLE_LINE_INPUT, ENABLE_ECHO_INPUT and
 *          ENABLE_PROCESSED_INPUT and sets ENABLE_WINDOW_INPUT.
 */
void setcbreak(void);

/**
 * @brief Restore the console settings saved by setcbreak().
 * @details Does nothing on Windows when no console handle was saved.
 */
void unsetcbreak(void);


#endif //
