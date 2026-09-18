/*
 * nd100x - ND100 Virtual Machine
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


/* Most-recently parsed BPUN header, captured on every successful LoadBPUN().
 * LoadBPUN()'s return value is only the (obsolete) bootstrap-loader "boot"
 * address; callers that need the real program entry ("start") or the "action"
 * autostart flag read them here. See GetLastBPUNHeader(). */
static BPUN_Header s_last_bpun_header;
static bool        s_last_bpun_valid = false;

/* Copy the last successfully parsed BPUN header into *out.
 * Returns false (and leaves *out untouched) if no BPUN has loaded yet. */
bool GetLastBPUNHeader(BPUN_Header* out) {
    if (!s_last_bpun_valid || !out) return false;
    *out = s_last_bpun_header;
    return true;
}

int LoadBPUN(const char* filename, bool verbose) {
    BPUN_Header bpun = {0};

	FILE* bpunStream = fopen(filename, "rb");
	if (!bpunStream) {
		LOG(LOG_CAT_LOADER, LOG_ERROR, "Failed to open BPUN file '%s': %s\n", filename, strerror(errno));
		return false;
	}

    bool loadOK = LoadBPUNStream(bpunStream,&bpun );
	fclose(bpunStream);
    bpunStream = NULL;

	if (!loadOK) {
		LOG(LOG_CAT_LOADER, LOG_ERROR, "BPUN load failed: Error while parsing BPUN format (file may be corrupted or in wrong format)\n");
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

        const char* crc = "[OK]";
        if (bpun.checksum != bpun.calculatedChecksum) {
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
    s_last_bpun_valid  = true;

	return bpun.boot;
}


/// @brief Loads a BPUN format file from a stream into a BPUN_Header structure
/// @param bpunStream The file stream to read from
/// @param header The BPUN_Header structure to populate
/// @return true if successful, false if there was an error
bool LoadBPUNStream(FILE* bpunStream, BPUN_Header* header)
{
    // Initialize header
    header->calculatedChecksum = 0;
    header->address = 0;
    header->count = 0;
    header->checksum = 0;
    header->action = 0;
    header->isFloMon = false;

    LoadState loadState = LoadState_Preamble;
    char tmpString[51] = {0};  // Max 50 chars + null terminator
    int tmpStringPos = 0;
    uint16_t currentLocationCounter = 0;
    uint16_t loadAddress = 0;
    uint16_t lastValue = 0;
    uint16_t dataCounter = 0;
	uint16_t dataLoadAddress = 0;
    rewind(bpunStream);  // Seek to start of file
    int b;
    while ((b = fgetc(bpunStream)) != EOF) {
        switch (loadState) {
            case LoadState_Preamble: {
                char c = (char)(b & 0x7F);  // Convert to 7-bit ASCII

                if (c == '!') {
                    if (tmpStringPos > 0) {
                        tmpString[tmpStringPos] = '\0';
                        int tmp = (int)strtol(tmpString, NULL, 8);
                        if (tmp >= 0) {
                            loadAddress = (uint16_t)tmp;
                        }
                    }
                    if (loadAddress == header->start) {
                        header->boot = lastValue;
                    } else {
                        header->boot = loadAddress;
                    }
                    loadState = LoadState_Address;
                    tmpStringPos = 0;
                    continue;
                }
                else if (c == '/') {
                    if (tmpStringPos > 0) {
                        tmpString[tmpStringPos] = '\0';
                        int tmp = (int)strtol(tmpString, NULL, 8);
                        if (tmp >= 0) {
                            currentLocationCounter = (uint16_t)tmp;
                            lastValue = currentLocationCounter;
                            header->start = currentLocationCounter;
                            if (loadAddress == 0) {
                                loadAddress = currentLocationCounter;
                            }
                        }
                    }
                    tmpStringPos = 0;
                }
                else if (c >= '0' && c <= '9') {
                    if (tmpStringPos < 50) {
                        tmpString[tmpStringPos++] = c;
                    }
                }
                else if (c == 0x0D) {  // Carriage return
                    if (tmpStringPos > 0) {
                        tmpString[tmpStringPos] = '\0';
                        int tmp = (int)strtol(tmpString, NULL, 8);
                        if (tmp >= 0) {
                            lastValue = (uint16_t)tmp;
                        }
                        tmpStringPos = 0;
                    }
                }
                break;
            }

            case LoadState_Address:
                header->address = (uint16_t)(b << 8);
                b = fgetc(bpunStream);
                if (b == EOF) return false;
                header->address |= (uint8_t)b;
                loadState = LoadState_Count;

				dataLoadAddress = header->address;
                break;

            case LoadState_Count:
                header->count = (uint16_t)(b << 8);
                b = fgetc(bpunStream);
                if (b == EOF) return false;
                header->count |= (uint8_t)b;
                dataCounter = header->count * 2;  // Count is in words, we read bytes
                loadState = LoadState_Data;
                break;

            case LoadState_Data: {
                uint16_t data_word = 0;
                if (dataCounter > 0) {
                    dataCounter--;
                    data_word = (b << 8) & 0xFF00;
                }

                if (dataCounter > 0) {
                    b = fgetc(bpunStream);
                    if (b == EOF) {
                        return false;
                    }
                    dataCounter--;
                    data_word |= (b & 0xFF);
                }

                if (DISASM)
                    disasm_addword(dataLoadAddress, data_word);

				WritePhysicalMemory(dataLoadAddress++, data_word, false);

                if (dataCounter == 0) {
                    loadState = LoadState_Checksum;
                }

                header->calculatedChecksum = (uint16_t)(header->calculatedChecksum + data_word);
                break;
            }

            case LoadState_Checksum:
                header->checksum = (uint16_t)(b << 8);
                b = fgetc(bpunStream);
                if (b == EOF) return false;
                header->checksum |= (uint8_t)b;
                loadState = LoadState_Action;

                if (header->address == 0 && header->count == 0 && header->checksum == 0) {
                    loadState = LoadState_FloMonCount;
                }
                break;

            case LoadState_Action:
                header->action = (uint16_t)(b << 8);
                b = fgetc(bpunStream);
                if (b == EOF) return false;
                header->action |= (uint8_t)b;
                return true;

            case LoadState_FloMonCount:
                header->isFloMon = true;
                header->count = (uint16_t)b;
                loadState = LoadState_FloMonLoad;
                break;

            case LoadState_FloMonLoad: {
                uint16_t floWords = 0;
                while (floWords < header->count) {

					uint16_t data_word = 0;

					// Entering here the first 0x00 byte has already been read by the outside loop. Check it and contiue
                    if (b != 0) return false;

					// Read HI bits
                    b = fgetc(bpunStream);
                    if (b == EOF) return false;

					data_word = (b << 8);

                    b = fgetc(bpunStream);
                    if (b == EOF || b != 0) return false;

                    b = fgetc(bpunStream);
                    if (b == EOF) return false;

					data_word |= b & 0xFF;

                    b = fgetc(bpunStream);
                    if (b == EOF || b != 0) return false;

                    WritePhysicalMemory(header->address+floWords, data_word, false);

                    if (DISASM)
                        disasm_addword(header->address+floWords, data_word);



                    floWords++;
                }
                return true;
            }
        }
    }

    return false;  // Unexpected end of file
}


int
bp_load (const char *bpfile) {
	(void)bpfile;
	// do binary load of device
   return -1;
}


