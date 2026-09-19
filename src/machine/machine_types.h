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


#ifndef MACHINE_TYPES_H
#define MACHINE_TYPES_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "../cpu/cpu_types.h"
#include "../cpu/cpu_protos.h"
#include "../devices/devices_types.h"


typedef enum
{
    BOOT_NONE = 0,
    BOOT_BPUN,
    BOOT_AOUT,
    BOOT_BP,
    BOOT_PROG,
    BOOT_FLOPPY,
    BOOT_SMD,
    BOOT_SCSI,
    BOOT_CDC,       /* NORD TSS CDC/NCR cartridge disc @ 500 */
    BOOT_TAPE,      /* octal-ASCII leader tape; remainder stays on reader @ 400 */
    BOOT_WINCHESTER /* ST506/8 inch Winchester @ 500, cards 3041/3038 */
} BOOT_TYPE;

/* program_load() failures that used to end the process. The native frontend
 * still turns them into the same exit codes (1 and 10); the shell, the DAP
 * launch path and the WASM frontend handle them and carry on. */
#define PROGRAM_LOAD_ERR_LOAD (-2)  /* the image could not be loaded (was exit(1))  */
#define PROGRAM_LOAD_ERR_BOOT (-10) /* the boot device failed (was exit(10))        */

extern const char *g_boot_type_str[];


// Drive types. The numeric values are part of the gateway wire protocol
// (driveType byte) and the JS DRIVE_TYPE table - keep them stable:
//   0=SMD, 1=FLOPPY, 2=SCSI, 3=WINCHESTER.
// WINCHESTER (ST506/8 inch, cards 3041/3038) has 2 units - ND-11.015.01 sec 3.1,
// and the control word carries the unit in a single bit (b9).
typedef enum
{
    DRIVE_SMD,
    DRIVE_FLOPPY,
    DRIVE_SCSI,
    DRIVE_WINCHESTER
} DRIVE_TYPE;

// NOTE: the SCSI unit count is SCSI_MAX_UNITS, defined in
// devices/scsi/device_scsi.h (reachable from here - this header includes
// devices_types.h, which includes device_scsi.h).

// Mounted drive information structure
typedef struct
{
    char md5[33];
    char name[256];
    char description[1024];
    char image_path[1024];  // Path or URL of the image
    bool is_mounted;        // Is the drive mounted
    bool is_remote;         // true if downloaded from HTTP, false if local file
    bool is_writeprotected; // true if the drive is write-protected
    bool is_opfs;           // true if using OPFS (block I/O via JS, no FILE*)
    bool is_gateway;        // true if using gateway WebSocket block I/O

    union
    {
        FILE *local_file;  // File handle for local files
        char *remote_data; // Downloaded data for remote files
    } data;
    size_t data_size; // Size of the data in bytes
    int block_size;   // Block size for this drive (256, 512, 1024 bytes)
} MountedDriveInfo_t;

#ifdef __EMSCRIPTEN__
/* Browser storage imports, defined with EM_JS in machine.c (OPFS and the
 * gateway WebSocket disk service). Return bytes transferred, or < 0. */
int opfs_block_read_js(int driveType, int unit, uint8_t *buffer, int bytes, int offset);
int opfs_is_available_js(int driveType, int unit);
int gateway_block_read_js(int driveType, int unit, uint8_t *buffer, int bytes, int offset);
int gateway_is_available_js(int driveType, int unit);
#endif

#endif
