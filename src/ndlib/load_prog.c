/*
 * nd100x - ND100 Virtual Machine
 *
 * Copyright (c) 2025 Ronny Hansen
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

/*
 * SINTRAN :PROG loadable-image loader.
 *
 * File layout (from the :PROG format spec):
 *   Header 1 : one 256-word (512-byte) header block at offset 0
 *   N blocks : Bank 1 image, starting at file offset 512
 *   Header 2 : one 512-byte header block at file offset 0x20000 (131072)  [2-bank only]
 *   M blocks : Bank 2 image, starting at file offset 0x20200 (131584)     [2-bank only]
 *
 * Header block (all 16-bit big-endian):
 *   [0] Start address     [1] Restart address
 *   [2] First addr Bank 1 [3] Last addr Bank 1
 *   [4] First addr Bank 2 [5] Last addr Bank 2
 *
 * Words to load per bank = (Last - First) + 1. A 1-bank image has
 * First Bank 2 == 0xFFFF and Last Bank 2 == 0x0000 (no data) and no Header 2/M.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "ndlib_types.h"
#include "ndlib_protos.h"

#define PROG_HEADER_BYTES     512
#define PROG_BANK2_HDR_OFFSET  0x20000   /* 131072 */
#define PROG_BANK2_DATA_OFFSET 0x20200   /* 131584 */

/* Declared here (not via cpu_protos.h, which needs the whole CPU type set) to
 * keep the loader dependency-light, mirroring load_bpun.c. */
extern void WritePhysicalMemory(int physicalAddress, uint16_t value, bool privileged);
extern void disasm_addword(uint16_t addr, uint16_t myword);
extern int DISASM;

/* Most-recently parsed :PROG header, for callers that need start/restart. */
static PROG_Header s_last_prog_header;
static bool        s_last_prog_valid = false;

bool GetLastPROGHeader(PROG_Header* out) {
    if (!s_last_prog_valid || !out) return false;
    *out = s_last_prog_header;
    return true;
}

/* Read one big-endian 16-bit word from the stream. Returns -1 on EOF. */
static int read_be16(FILE* f) {
    int hi = fgetc(f);
    if (hi == EOF) return -1;
    int lo = fgetc(f);
    if (lo == EOF) return -1;
    return ((hi & 0xFF) << 8) | (lo & 0xFF);
}

/* Load one bank's image: (last-first)+1 words from the current file position
 * into physical memory starting at word address 'first'. Returns the number of
 * words loaded, or -1 on a short/truncated read. */
static int load_bank(FILE* f, uint16_t first, uint16_t last, bool verbose,
                     const char* label) {
    int count = (int)last - (int)first + 1;
    if (count <= 0) {
        if (verbose) LOG(LOG_CAT_LOADER, LOG_INFO, "  %s: no data (first=0o%o last=0o%o)",
                            label, first, last);
        return 0;
    }
    if (verbose) LOG(LOG_CAT_LOADER, LOG_INFO, "  %s: loading %d words at 0o%o..0o%o",
                        label, count, first, last);

    for (int i = 0; i < count; i++) {
        int w = read_be16(f);
        if (w < 0) {
            LOG(LOG_CAT_LOADER, LOG_INFO, "PROG load: truncated %s (got %d of %d words)\n",
                   label, i, count);
            return -1;
        }
        uint16_t addr = (uint16_t)(first + i);
        if (DISASM) disasm_addword(addr, (uint16_t)w);
        WritePhysicalMemory(addr, (uint16_t)w, false);
    }
    return count;
}

/*
 * Load a :PROG file. On success returns the program start address (>= 0);
 * returns -1 on any error. Fills the last-header cache (GetLastPROGHeader()).
 */
int LoadPROG(const char* filename, bool verbose) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        LOG(LOG_CAT_LOADER, LOG_ERROR, "Failed to open PROG file '%s': %s\n", filename, strerror(errno));
        return -1;
    }

    /* --- Header 1 --- */
    int start   = read_be16(f);
    int restart = read_be16(f);
    int firstB1 = read_be16(f);
    int lastB1  = read_be16(f);
    int firstB2 = read_be16(f);
    int lastB2  = read_be16(f);
    if (start < 0 || restart < 0 || firstB1 < 0 || lastB1 < 0 ||
        firstB2 < 0 || lastB2 < 0) {
        LOG(LOG_CAT_LOADER, LOG_INFO, "PROG load: header too short in '%s'\n", filename);
        fclose(f);
        return -1;
    }

    PROG_Header hdr;
    hdr.startAddress   = (uint16_t)start;
    hdr.restartAddress = (uint16_t)restart;
    hdr.firstBank1     = (uint16_t)firstB1;
    hdr.lastBank1      = (uint16_t)lastB1;
    hdr.firstBank2     = (uint16_t)firstB2;
    hdr.lastBank2      = (uint16_t)lastB2;
    /* 1-bank sentinel: first==0xFFFF, last==0x0000 -> no Bank 2. */
    hdr.twoBank = !(hdr.firstBank2 == 0xFFFF && hdr.lastBank2 == 0x0000);

    if (verbose) {
        LOG(LOG_CAT_LOADER, LOG_INFO, "PROG load OK\n");
        LOG(LOG_CAT_LOADER, LOG_INFO, "--- :PROG Header ---\n");
        LOG(LOG_CAT_LOADER, LOG_INFO, "Start:   0o%06o\n", hdr.startAddress);
        LOG(LOG_CAT_LOADER, LOG_INFO, "Restart: 0o%06o\n", hdr.restartAddress);
        LOG(LOG_CAT_LOADER, LOG_INFO, "Bank1:   0o%06o..0o%06o\n", hdr.firstBank1, hdr.lastBank1);
        if (hdr.twoBank)
            LOG(LOG_CAT_LOADER, LOG_INFO, "Bank2:   0o%06o..0o%06o (alt page table)\n",
                   hdr.firstBank2, hdr.lastBank2);
        else
            LOG(LOG_CAT_LOADER, LOG_INFO, "Bank2:   (none - 1-bank program)\n");
    }

    /* --- Bank 1 data at file offset 512 --- */
    if (fseek(f, PROG_HEADER_BYTES, SEEK_SET) != 0) {
        LOG(LOG_CAT_LOADER, LOG_ERROR, "PROG load: cannot seek to Bank 1 data\n");
        fclose(f);
        return -1;
    }
    if (load_bank(f, hdr.firstBank1, hdr.lastBank1, verbose, "Bank1") < 0) {
        fclose(f);
        return -1;
    }

    /* --- Bank 2 (2-bank images only) --- */
    if (hdr.twoBank) {
        /* Bank 2 lives behind the ALTERNATIVE page table, which the program
         * enables at runtime via MON ALTON. nd100x does not yet map a separate
         * Bank-2 physical area, so loading it into the same physical space as
         * Bank 1 would corrupt Bank 1. Refuse rather than silently mis-load. */
        LOG(LOG_CAT_LOADER, LOG_INFO, "PROG load: 2-bank :PROG not yet supported (Bank 2 needs the "
               "alternative page table). Loaded Bank 1 only.\n");
        /* Fall through: Bank 1 is loaded; caller decides. Header records twoBank. */
    }

    s_last_prog_header = hdr;
    s_last_prog_valid  = true;

    fclose(f);
    return (int)hdr.startAddress;
}
