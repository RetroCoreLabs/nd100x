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

#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "../devices_types.h"
#include "../devices_protos.h"


#include "../../cpu/cpu_types.h"
#include "../../cpu/cpu_protos.h"


#include "panel.h"

static struct display_panel *s_pap;


void setup_pap(void)
{

    gPANS =
        0x8000; /* Tell system we are here (Bit 15 active will activate MOPC logic in MS20 in microcode every 20 ms)*/
    gPANS = gPANS | 0x4000; /* Set FULL which is active low, so not full */

    /* static storage: allocated once, never freed, cannot fail */
    static struct display_panel s_pap_storage;
    memset(&s_pap_storage, 0, sizeof(s_pap_storage));
    s_pap = &s_pap_storage;

    UpdateMachineTime();
}

void ProcessMessageControl(PANC_Register panc)
{
    MessageControl mc = (panc.bits.wpan & 0b111);


    switch (mc)
    {
    case StopRotatingMessage:
        s_pap->function_mode = 0; // Stop rotating
        break;
    case ReturnDisplayToNormal:
        s_pap->function_mode = 1; // Return to normal
        break;
    case ClearTextBuffer:
        s_pap->fdisp_cntr = 0;                                       // Clear display counter
        memset(s_pap->func_display, 0, sizeof(s_pap->func_display)); // Clear display buffer
        break;
    case RotateMessage:
        s_pap->function_mode = 2; // Start rotating
        break;
    case ClearAndRotate:
        s_pap->fdisp_cntr = 0;                                       // Clear display counter
        memset(s_pap->func_display, 0, sizeof(s_pap->func_display)); // Clear display buffer
        s_pap->function_mode = 2;                                    // Start rotating
        break;
    default:
        break;
    }
}

/// <summary>
/// Called from TRR logic when "TRR PANC" has been executed
/// Process the command in gPANC
/// </summary>
void ProcessTerminalPanc(void)
{
    if (!s_pap)
    {
        return;
    }

    PANC_Register panc;
    panc.raw = gPANC;

    PANS_Register pans;
    pans.raw = 0;

    // Update time from the Host realtime clock when reading
    if (panc.bits.read_request)
    {
        UpdateMachineTime();
    }

    // Is this a system request?
    if (panc.bits.pfunc)
    {

        pans.bits.panel_present = 1; // yes, we are here
        pans.bits.input_pending = 1; // FIFO not full
        pans.bits.pan_interrupt = 1; // Yes, we have a PAN interrupt

        pans.bits.rpan = panc.bits.read_request; // Answering read or write request?
        pans.bits.pfunc = panc.bits.pfunc;       // whhat function are we responding to?

        switch (panc.bits.pfunc)
        {
        case STATUS_ILLEGAL:
            // TODO: Implement!
            break;
        case STATUS_FUTURE_EXTENSION:
            // TODO: Implement!
            break;
        case STATUS_MESSAGE_APPEND:
            if (panc.bits.read_request)
            {
                // Read request not implemented for message append
            }
            else
            {
                if (panc.bits.wpan != 0)
                {
                    if (panc.bits.wpan == 0x0E)
                    {
                        // "Shift Out" character - switch to alternate character set??
                        // Not implemented yet
                    }
                    else
                    {
                        char c = (char)panc.bits.wpan;

                        // Check if we have room in the buffer
                        if (s_pap->fdisp_cntr < sizeof(s_pap->func_display) - 1)
                        {
                            s_pap->func_display[s_pap->fdisp_cntr++] = c;
                        }
                    }
                }
            }
            break;
        case STATUS_MESSAGE_CONTROL:
            if (panc.bits.read_request)
            {
                // read what ?
            }
            else
            {
                ProcessMessageControl(panc);
            }
            break;
        case STATUS_UPDATE_LOW_SECONDS:
            if (panc.bits.read_request)
            {
                pans.bits.rpan = (uint16_t)(s_pap->seconds & 0xFF);
                pans.bits.read_panel_valid = 1; // Yes, we have a valid response
            }
            else
            {
                s_pap->seconds = (uint16_t)((s_pap->seconds & 0xff00) | panc.bits.wpan);
            }
            break;
        case STATUS_UPDATE_HIGH_SECONDS:
            if (panc.bits.read_request)
            {
                pans.bits.rpan = (uint16_t)(s_pap->seconds >> 8 & 0xFF);
                pans.bits.read_panel_valid = 1; // Yes, we have a valid response
            }
            else
            {
                s_pap->seconds = (s_pap->seconds & 0x00ff) | (panc.bits.wpan << 8);
            }
            break;
        case STATUS_UPDATE_LOW_DAYS:
            if (panc.bits.read_request)
            {
                pans.bits.rpan = (uint16_t)(s_pap->days & 0x00ff);
                pans.bits.read_panel_valid = 1; // Yes, we have a valid response
            }
            else
            {
                s_pap->days = (s_pap->days & 0xFF00) | panc.bits.wpan;
            }
            break;
        case STATUS_UPDATE_HIGH_DAYS:
            if (panc.bits.read_request)
            {
                pans.bits.rpan = (uint16_t)(s_pap->days >> 8 & 0x00ff);
                pans.bits.read_panel_valid = 1; // Yes, we have a valid response
            }
            else
            {
                s_pap->days = (s_pap->days & 0x00FF) | (uint16_t)(panc.bits.wpan << 8);
            }
            break;
        case STATUS_MEMORY_EXAMINE:
            // TODO: Implement!
            break;
        case STATUS_DATA_TO_EXAMINE:
            // TODO: Implement!
            break;
        case STATUS_ACTIVE_LEVELS:
            // TODO: Implement!
            break;
        case STATUS_OUT_EXAMINE_MODE:
            // TODO: Implement!
            break;
        case STATUS_SEND_LABEL:
            // TODO: Implement!
            break;
        case STATUS_F_TYPED:
            // TODO: Implement!
            break;
        }
    }
    gPANS = pans.raw;
}

// respond to TRR LMP
void ProcessTerminalLamp(void)
{
    // read gLMP
    gPANS = 0x0000;
}
/// <summary>
/// Calculate HW clock info
///
/// HW Clock contains an offset since 00:00:00 1.January 1979 (TBASE)
/// The clock counts seconds and half-days (12 hours) from this time.
/// </summary>
void UpdateMachineTime(void)
{
    time_t tbase = 0;
    time_t now = time(NULL);

    // Set base time to 1979-01-01 00:00:00 CET
    struct tm *tm_base = localtime(&tbase);
    tm_base->tm_year = 79; // Years since 1900, so 79 = 1979
    tm_base->tm_mon = 0;   // Months are 0-based, so 0 = January
    tm_base->tm_mday = 1;  // Day of month
    tm_base->tm_hour = 0;
    tm_base->tm_min = 0;
    tm_base->tm_sec = 0;
    tbase = mktime(tm_base);

    struct tm *tm_now = localtime(&now);


    // Sintran doesn't support Y2K (without patches) so stay in year before 2000...
    // Subtract 30 years from current time (2025-30 = 1995)

    tm_now->tm_year -= 30;
    now = mktime(tm_now);

    time_t midnight = now;
    struct tm *tm_midnight = localtime(&midnight);

    // Calculate days difference from TBASE
    int days_diff = (int)(difftime(now, tbase) / (24.0 * 3600.0));
    s_pap->days = (uint16_t)(days_diff * 2); // Convert to half-days

    // Check if we've passed noon
    if (tm_now->tm_hour >= 11)
    {
        s_pap->days++; // Add another half day

        // Get midnight of current day
        tm_midnight->tm_hour = 0;
        tm_midnight->tm_min = 0;
        tm_midnight->tm_sec = 0;
        midnight = mktime(tm_midnight);

        // Now counting since noon
        struct tm *tm_noon = localtime(&now);
        tm_noon->tm_hour -= 12;
        now = mktime(tm_noon);
    }

    // Calculate seconds since midnight
    s_pap->seconds = (uint16_t)difftime(now, midnight);
}
