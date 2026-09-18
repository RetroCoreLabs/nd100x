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


#ifndef ND_LIB_TYPES_H
#define ND_LIB_TYPES_H

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>


// ** KEYBOARD EVENTS **
// Classified keyboard input produced by read_key_event(). Placed here rather
// than in keyboard.h so the mkptypes-generated ndlib_protos.h - which declares
// read_key_event() - can see the full definition regardless of include order.
typedef enum {
    KEY_NONE = 0,      // No key available
    KEY_CHAR,          // Ordinary typed character (evt.ch)
    KEY_ESCAPE,        // ESC key pressed alone
    KEY_F12,           // F12 function key
    KEY_ALT_DIGIT,     // Alt+1..9 (evt.ch is '1'..'9')
    KEY_UNKNOWN,       // Multi-byte input we did not classify (raw in evt.seq)
} KeyType;

typedef struct {
    KeyType type;
    char    ch;        // Ordinary character (KEY_CHAR) or Alt digit (KEY_ALT_DIGIT)
    char    seq[8];    // Raw byte sequence - populated for passthrough/KEY_UNKNOWN
    int     seqLen;
} KeyEvent;


// Physical memory functions in cpu_mms.c
extern int ReadPhysicalMemory(int physicalAddress, bool privileged);
extern void WritePhysicalMemory(int physicalAddress, uint16_t value, bool privileged);


// ** LOGGING **
// Log levels
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
} LogLevel;

/// @brief Minimum log level for filtering messages
/// @details Messages below this level will not be logged.
extern LogLevel minLogLevel;


/// @brief Array of log level strings for formatting output
extern const char *level_str[];

/// @brief Callback type for redirecting log output (e.g. to a VScreen)
typedef void (*LogOutputFunc)(const char *message);

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
    bool     twoBank;        /* true when a Bank 2 image is present */
} PROG_Header;

typedef enum {
    LoadState_Preamble,
    LoadState_Address,
    LoadState_Count,
    LoadState_Data,
    LoadState_Checksum,
    LoadState_Action,
    LoadState_FloMonCount,
    LoadState_FloMonLoad
} LoadState;










#endif //