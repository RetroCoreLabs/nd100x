/*
 * load_bpun.c - BPUN file loader and last-loaded BPUN header access.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * This file is originated from the nd100x project and the RetroCore project
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


#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "ndlib_types.h"
#include "ndlib_protos.h"

static bool load_bpun_stream(FILE *, BPUN_Header *);


/* Most-recently parsed BPUN header, captured on every successful LoadBPUN().
 * LoadBPUN()'s return value is only the (obsolete) bootstrap-loader "boot"
 * address; callers that need the real program entry ("start") or the "action"
 * autostart flag read them here. See GetLastBPUNHeader(). */
static BPUN_Header s_last_bpun_header;
static bool s_last_bpun_valid = false;

/* Copy the last successfully parsed BPUN header into *out.
 * Returns false (and leaves *out untouched) if no BPUN has loaded yet. */
bool GetLastBPUNHeader(BPUN_Header *out)
{
    if (!s_last_bpun_valid || !out)
    {
        return false;
    }
    *out = s_last_bpun_header;
    return true;
}

int LoadBPUN(const char *filename, bool verbose)
{
    BPUN_Header bpun = {0};

    FILE *bpun_stream = fopen(filename, "rb");
    if (!bpun_stream)
    {
        LOG(LOG_CAT_LOADER, LOG_ERROR, "Failed to open BPUN file '%s': %s\n", filename,
            strerror(errno));
        return false;
    }

    bool load_ok = load_bpun_stream(bpun_stream, &bpun);
    fclose(bpun_stream);
    bpun_stream = NULL;

    if (!load_ok)
    {
        LOG(LOG_CAT_LOADER, LOG_ERROR,
            "BPUN load failed: Error while parsing BPUN format (file may be corrupted or in wrong "
            "format)\n");
        return -1;
    }

    if (verbose)
    {
        LOG(LOG_CAT_LOADER, LOG_INFO, "BPUN load OK\n");

        LOG(LOG_CAT_LOADER, LOG_INFO, "--- Bootstrapper ---\n");
        LOG(LOG_CAT_LOADER, LOG_INFO, "Start: %06o\n", bpun.start);
        LOG(LOG_CAT_LOADER, LOG_INFO, "Boot: %06o\n", bpun.boot);

        LOG(LOG_CAT_LOADER, LOG_INFO, "--- Data ---\n");
        LOG(LOG_CAT_LOADER, LOG_INFO, "Address: %06o\n", bpun.address);
        LOG(LOG_CAT_LOADER, LOG_INFO, "Count: %06o\n", bpun.count);

        const char *crc = "[OK]";
        if (bpun.checksum != bpun.calculatedChecksum)
        {
            LOG(LOG_CAT_LOADER, LOG_ERROR, "CRC ERROR != %02X\n", bpun.calculatedChecksum);
            crc = "[CRC ERROR]";
        }

        LOG(LOG_CAT_LOADER, LOG_INFO, "Checksum: %06o %s\n", bpun.checksum, crc);
        LOG(LOG_CAT_LOADER, LOG_INFO, "Action: %06o\n", bpun.action);

        LOG(LOG_CAT_LOADER, LOG_INFO, "FloMon: %d\n", bpun.isFloMon);
    }

    /* Capture the full header so callers can read the real program entry
     * (bpun.start) and the autostart flag (bpun.action). */
    s_last_bpun_header = bpun;
    s_last_bpun_valid = true;

    return bpun.boot;
}


/// @brief Loads a BPUN format file from a stream into a BPUN_Header structure
/// @param bpunStream The file stream to read from
/// @param header The BPUN_Header structure to populate
/// @return true if successful, false if there was an error
static bool load_bpun_stream(FILE *bpun_stream, BPUN_Header *header)
{
    // Initialize header
    header->calculatedChecksum = 0;
    header->address = 0;
    header->count = 0;
    header->checksum = 0;
    header->action = 0;
    header->isFloMon = false;

    LoadState load_state = LOAD_STATE_PREAMBLE;
    char tmp_string[51] = {0}; // Max 50 chars + null terminator
    int tmp_string_pos = 0;
    uint16_t current_location_counter = 0;
    uint16_t load_address = 0;
    uint16_t last_value = 0;
    uint16_t data_counter = 0;
    uint16_t data_load_address = 0;
    rewind(bpun_stream); // Seek to start of file
    int b;
    while ((b = fgetc(bpun_stream)) != EOF)
    {
        switch (load_state)
        {
        case LOAD_STATE_PREAMBLE:
        {
            char c = (char)(b & 0x7F); // Convert to 7-bit ASCII

            if (c == '!')
            {
                if (tmp_string_pos > 0)
                {
                    tmp_string[tmp_string_pos] = '\0';
                    int tmp = (int)strtol(tmp_string, NULL, 8);
                    if (tmp >= 0)
                    {
                        load_address = (uint16_t)tmp;
                    }
                }
                if (load_address == header->start)
                {
                    header->boot = last_value;
                }
                else
                {
                    header->boot = load_address;
                }
                load_state = LOAD_STATE_ADDRESS;
                tmp_string_pos = 0;
                continue;
            }
            else if (c == '/')
            {
                if (tmp_string_pos > 0)
                {
                    tmp_string[tmp_string_pos] = '\0';
                    int tmp = (int)strtol(tmp_string, NULL, 8);
                    if (tmp >= 0)
                    {
                        current_location_counter = (uint16_t)tmp;
                        last_value = current_location_counter;
                        header->start = current_location_counter;
                        if (load_address == 0)
                        {
                            load_address = current_location_counter;
                        }
                    }
                }
                tmp_string_pos = 0;
            }
            else if (c >= '0' && c <= '9')
            {
                if (tmp_string_pos < 50)
                {
                    tmp_string[tmp_string_pos++] = c;
                }
            }
            else if (c == 0x0D)
            { // Carriage return
                if (tmp_string_pos > 0)
                {
                    tmp_string[tmp_string_pos] = '\0';
                    int tmp = (int)strtol(tmp_string, NULL, 8);
                    if (tmp >= 0)
                    {
                        last_value = (uint16_t)tmp;
                    }
                    tmp_string_pos = 0;
                }
            }
            break;
        }

        case LOAD_STATE_ADDRESS:
            header->address = (uint16_t)(b << 8);
            b = fgetc(bpun_stream);
            if (b == EOF)
            {
                return false;
            }
            header->address |= (uint8_t)b;
            load_state = LOAD_STATE_COUNT;

            data_load_address = header->address;
            break;

        case LOAD_STATE_COUNT:
            header->count = (uint16_t)(b << 8);
            b = fgetc(bpun_stream);
            if (b == EOF)
            {
                return false;
            }
            header->count |= (uint8_t)b;
            data_counter = header->count * 2; // Count is in words, we read bytes
            load_state = LOAD_STATE_DATA;
            break;

        case LOAD_STATE_DATA:
        {
            uint16_t data_word = 0;
            if (data_counter > 0)
            {
                data_counter--;
                data_word = (b << 8) & 0xFF00;
            }

            if (data_counter > 0)
            {
                b = fgetc(bpun_stream);
                if (b == EOF)
                {
                    return false;
                }
                data_counter--;
                data_word |= (b & 0xFF);
            }

            if (g_disasm)
            {
                disasm_addword(data_load_address, data_word);
            }

            WritePhysicalMemory(data_load_address++, data_word, false);

            if (data_counter == 0)
            {
                load_state = LOAD_STATE_CHECKSUM;
            }

            header->calculatedChecksum = (uint16_t)(header->calculatedChecksum + data_word);
            break;
        }

        case LOAD_STATE_CHECKSUM:
            header->checksum = (uint16_t)(b << 8);
            b = fgetc(bpun_stream);
            if (b == EOF)
            {
                return false;
            }
            header->checksum |= (uint8_t)b;
            load_state = LOAD_STATE_ACTION;

            if (header->address == 0 && header->count == 0 && header->checksum == 0)
            {
                load_state = LOAD_STATE_FLO_MON_COUNT;
            }
            break;

        case LOAD_STATE_ACTION:
            header->action = (uint16_t)(b << 8);
            b = fgetc(bpun_stream);
            if (b == EOF)
            {
                return false;
            }
            header->action |= (uint8_t)b;
            return true;

        case LOAD_STATE_FLO_MON_COUNT:
            header->isFloMon = true;
            header->count = (uint16_t)b;
            load_state = LOAD_STATE_FLO_MON_LOAD;
            break;

        case LOAD_STATE_FLO_MON_LOAD:
        {
            uint16_t flo_words = 0;
            while (flo_words < header->count)
            {

                uint16_t data_word = 0;

                // Entering here the first 0x00 byte has already been read by the outside loop. Check it and contiue
                if (b != 0)
                {
                    return false;
                }

                // Read HI bits
                b = fgetc(bpun_stream);
                if (b == EOF)
                {
                    return false;
                }

                data_word = (b << 8);

                b = fgetc(bpun_stream);
                if (b == EOF || b != 0)
                {
                    return false;
                }

                b = fgetc(bpun_stream);
                if (b == EOF)
                {
                    return false;
                }

                data_word |= b & 0xFF;

                b = fgetc(bpun_stream);
                if (b == EOF || b != 0)
                {
                    return false;
                }

                WritePhysicalMemory(header->address + flo_words, data_word, false);

                if (g_disasm)
                {
                    disasm_addword(header->address + flo_words, data_word);
                }


                flo_words++;
            }
            return true;
        }
        break;
        default:
            break;
        }
    }

    return false; // Unexpected end of file
}


int bp_load(const char *bpfile)
{
    (void)bpfile;
    // do binary load of device
    return -1;
}
