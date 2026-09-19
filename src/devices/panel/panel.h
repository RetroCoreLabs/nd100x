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

#ifndef PANEL_H
#define PANEL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// More doumentaion here:
// https://www.ndwiki.org/wiki/ND-100_front_panel
// http://sintran.com/sintran/hardware/nd-other/nd-322691.html

/// @brief Message control functions.
/// Read from WPAN bits 0-2
/// Used when PANS/PANC PANEL_STATUS_FUNCTIONS is STATUS_MESSAGE_CONTROL
typedef enum
{
    /// <summary>
    /// Stop rotating the message
    /// </summary>
    StopRotatingMessage = 0b000,

    /// <summary>
    /// Return display to normal funciont
    /// </summary>
    ReturnDisplayToNormal = 0b001,

    /// <summary>
    /// Clear text buffer and Function display. Prepare for text to be appended
    /// </summary>
    ClearTextBuffer = 0b010,

    /// <summary>
    /// Rotate the message in the text buffer, displaying four characters at a a time
    /// </summary>
    RotateMessage = 0b100,

    /// <summary>
    /// CLear the text and start rotating. (Command 010 plus command 100)
    /// </summary>
    ClearAndRotate = 0b110,
} MessageControl;

/// @brief User/external functions.
/// Used when PANS/PANC bit 11 is set to 0
typedef enum
{
    STATUS_ILLEGAL,
    STATUS_FUTURE_EXTENSION,
    STATUS_MESSAGE_APPEND,
    STATUS_MESSAGE_CONTROL,
    STATUS_UPDATE_LOW_SECONDS,
    STATUS_UPDATE_HIGH_SECONDS,
    STATUS_UPDATE_LOW_DAYS,
    STATUS_UPDATE_HIGH_DAYS,

    // ND INTERNAL FUNCTIONS - SEE ND-110 MICROCODE
    STATUS_MEMORY_EXAMINE,   // 8 - After 2 bytes from AB,OCTA2
    STATUS_DATA_TO_EXAMINE,  // 9 - After 2 bytes with register P (DYTP1:)
    STATUS_ACTIVE_LEVELS,    // 10 - After 2 bytes with register Q  (DYTP2:)
    STATUS_UNKNOWN_11,       // 11 -
    STATUS_UNKNOWN_12,       // 12 -
    STATUS_OUT_EXAMINE_MODE, // 13 - (EXM02) after 1 byte Q ?
    STATUS_SEND_LABEL,       // 14 - After, 2 words: AB,TXT1 then AB,TXT2 (LABEL:)
    STATUS_F_TYPED           // 15 -
} PANEL_STATUS_FUNCTIONS;

///  PANS (READ)
///  Response from panel to ND-100 cpu
///
/// +-----------+--------+----------+-------+------------+------------+
/// |   15      |  14    |  13      |   12  | 11-10-9-8  | 7    -   0 |
/// +-----------+--------+----------+-------+------------+------------+
/// |DISP.PRESS |INP PDY | RPAN VAL |PAN INT|   PFUNC    |   RPAN     |
/// +-----------+--------+----------+-------+------------+------------+
// NOTE on bit-field storage type: every bit-field must share the same
// underlying storage type (uint16_t). Using an enum (`PANEL_STATUS_FUNCTIONS`)
// for `pfunc` broke the layout on Windows/MinGW - GCC defaults to
// -mms-bitfields there, which places different-typed bit-fields in separate
// storage units, so the `raw` uint16_t overlay no longer covered `pfunc`.
// Comparisons against the enum values still work via implicit promotion.
typedef union
{
    uint16_t raw;
    struct
    {
        uint16_t rpan : 8;  // Bits 0-7: RPAN (return from Panel)
        uint16_t pfunc : 4; // Bits 8-11: PFUNC (Panel function code; see PANEL_STATUS_FUNCTIONS)
        uint16_t pan_interrupt
            : 1; // Bit 12: Panel Interrupt (set to 1 when responding) - Also known as "COM RDY", The command in PCOM has been processed
        uint16_t read_panel_valid
            : 1; // Bit 13: RPAN Valid (If 1, then last command PAP processed was a READ, data is in bit 0-7)
        uint16_t input_pending
            : 1; // Bit 14: Input Pending (When 0, the PAC fifo is full. If status stays at 0 for more than 2 ms then PAP (Panel Processor) is not working)
        uint16_t panel_present : 1; // Bit 15: Panel Present (1=Display is present)
    } bits;
} PANS_Register;

///  PANC - Panel Control Register (WRITE)
///  Command from ND-100 cpu to panel
///
/// +---+---+--------------+----+-----------+------------+
/// |15 | 14|  13          | 12 | 11-10-9-8 | 7    -   0 |
/// +---+---+--------------+----+-----------+------------+
/// | 0 | 0 | Read Request |N.A.|  PFUNC    |   WPAN     |
/// +---+---+--------------+----+-----------+------------+
// Same bit-field-storage-type constraint as PANS_Register - keep every
// field as uint16_t so the `raw` overlay matches the bits view on both
// Linux (packed) and Windows/MinGW (-mms-bitfields) ABIs.
typedef union
{
    uint16_t raw;
    struct
    {
        uint16_t wpan : 8;  // Bits 0-7: WPAN (Write to Panel)
        uint16_t pfunc : 4; // Bits 8-11: PFUNC (Panel function code; see PANEL_STATUS_FUNCTIONS)
        uint16_t reserved12 : 1;   // Bit 12: N.A.
        uint16_t read_request : 1; // Bit 13: Read Request
        uint16_t reserved14 : 1;   // Bit 14: Reserved
        uint16_t reserved15 : 1;   // Bit 15: Reserved
    } bits;
} PANC_Register;

struct display_panel
{
    bool sec_tick; /* Seconds tick from rtc, update counters */

    char func_display[40]; /* Max 40 chars in display buffer */
    uint16_t fdisp_cntr;   /* pointer to where we are */

    uint16_t seconds; /* 16 bit second counter for realtime clock, ticks day counter every 12h */
    uint16_t days;    /* 16 bit day counter for realtime clock */
    /* So seconds should wrap at 3600x12 = 43200, and day possibly lowest bit is am/pm */

    // bool power_lamp;
    // bool run_lamp; // read from RUN signal on ND-100 bus C. Here, maybe CPU in RUN mode?
    // bool opcom_lamp; // read from signal on ND-100 bus C. Her maybe CPU in STOP mode ?

    //  int lock_key; // Is the key in LOCK, ON or STANDBY position
    //  bool stop_button;
    //  bool load_button;
    //  bool opcom_button;
    //  bool mcl_button;

    int utilization; // Utilization percentage (calculated based on PIL level on 0 versus other PIL levels.0 =IDLE)
    int function_hit;  // Cache hit ? maybe on ND-BUS C signal
    int function_ring; // PIL register, or 1<<PIL to show which PIL bit is set
    int function_mode; // Panel Processor Functtion Mode Register (not used yet..)
};

void ProcessTerminalPanc(void);
void ProcessTerminalLamp(void);
void UpdateMachineTime(void);
void setup_pap(void);

#endif // PANEL_H
