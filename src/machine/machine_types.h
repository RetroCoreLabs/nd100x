/*
 * machine_types.h - Machine types: boot types, drive types and load error codes.
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

/* ------------------------------------------------------------------------
 * Functions defined in io.c
 * ------------------------------------------------------------------------ */

/**
 * @brief Initialise the device manager and add all emulated devices.
 * @return 0 on success, -1 if the device manager could not be set up.
 */
int io_init(void);

/**
 * @brief Destroy the device manager and everything it owns.
 */
void io_destroy(void);

/**
 * @brief Ask the device manager which device identifies on an interrupt level.
 * @param level Interrupt level to run the IDENT search on.
 * @return The IDENT code returned by the device manager for that level.
 */
int io_ident(uint16_t);

/**
 * @brief Tick all devices once and raise the interrupts they report.
 */
void IO_Tick(void);

/**
 * @brief Perform one IOX transfer: odd addresses write, even addresses read.
 * @param ioadd I/O address; bit 0 selects write (odd) or read (even).
 * @param regA Value written to the device on a write operation.
 * @return regA on a write, the value read from the device on a read.
 */
uint16_t io_op(uint16_t, uint16_t);


/* ------------------------------------------------------------------------
 * Functions defined in machine.c
 * ------------------------------------------------------------------------ */

/**
 * @brief Mount a disk from the online catalog onto a unit, hot-swapping it like
 *        machine_floppy_swap(); the drive type comes from the catalog entry.
 * @param unit Drive unit to mount on (floppy 0-2, SMD 0-3).
 * @param selector "md5:<hash>", "dir:<name>", or a bare token tried as md5 then
 *        as a directory name.
 * @return 0 ok, -1 bad unit, -2 not found, -3 catalog unavailable, -4 mount failed.
 */
int machine_floppy_mount_catalog(int, const char *);

/**
 * @brief Allocate the floppy, SMD, SCSI and Winchester mounted-drive tables.
 * @return 0 on success, -1 if memory ran out (already allocated tables are kept).
 */
int machine_init_drive_arrays(void);

/**
 * @brief Set up the whole machine: drive tables, CPU, CPU debugger and I/O devices,
 *        then put the CPU into CPU_RUNNING mode.
 * @param debuggerEnabled true to enable the DAP debugger.
 * @param debuggerPort TCP port the debugger listens on.
 * @return 0 on success, -1 on out of memory or device manager failure.
 */
int machine_init(bool, int);

/**
 * @brief Add an HDLC device with the given connection configuration and log the result.
 * @param deviceNum HDLC device number to add.
 * @param isServer true for server mode (listen on port), false for client mode.
 * @param address Remote address to connect to in client mode.
 * @param port TCP port to listen on or connect to.
 */
void machine_add_hdlc(int, bool, const char *, int);

/**
 * @brief Shut down the CPU and I/O, unmount every mounted drive and free the drive tables.
 */
void machine_cleanup(void);

/**
 * @brief Unmount a drive, clearing its entry in the table for that drive type.
 * @param drive_type Which drive table to act on.
 * @param unit Unit index inside that table.
 */
void machine_unmount_drive(DRIVE_TYPE, int);

/**
 * @brief Run the CPU, servicing debugger pause/control requests, until the ticks are
 *        used up or the CPU shuts down. Do NOT call from the debugger thread.
 * @param ticks Number of ticks to run the CPU; -1 for infinite.
 */
void machine_run(int);

/**
 * @brief Stop the CPU by setting its run mode to CPU_STOPPED.
 */
void machine_stop(void);

/**
 * @brief Set the default configuration: boot type SMD, start address 0, disassembly off.
 */
void machine_setdefaultconfig(void);

/**
 * @brief Write one word to physical memory, and to the disassembler if it is enabled.
 *        Used as the write callback by the a.out loader.
 * @param address Physical address to write to.
 * @param value Value to write.
 */
void write_memory(uint32_t, uint16_t);

/**
 * @brief Mount a floppy image on a unit if the file can be opened, defaulting to
 *        "FLOPPY.IMG" when no image file is given.
 * @param imageFile Image file path, or NULL for the default.
 * @param unit Floppy unit to mount on.
 */
void machine_mount_floppy(const char *, int);

/**
 * @brief Mount a drive image, filling in the table entry for that drive type and unit.
 * @param drive_type Which drive table to act on.
 * @param unit Unit index inside that table.
 * @param md5 MD5 hash string stored with the mount.
 * @param name Short drive name.
 * @param description Longer drive description.
 * @param image_path Local file path or remote URL of the image.
 */
void machine_mount_drive(DRIVE_TYPE, int, const char *, const char *, const char *, const char *);

/**
 * @brief Hot-swap the floppy in a unit: eject the current image, then mount the new
 *        path; an empty or NULL path ejects only.
 * @param unit Floppy unit (0-2).
 * @param path Image file to mount, or NULL/empty to eject only.
 * @return 0 ok, -1 bad unit, -2 image file could not be opened.
 */
int machine_floppy_swap(int, const char *);

/**
 * @brief Report whether a unit of the given drive type currently has an image mounted.
 * @param drive_type Which drive table to check.
 * @param unit Unit index inside that table.
 * @return true if that unit is mounted, false otherwise.
 */
bool machine_is_mounted(DRIVE_TYPE, int);

/**
 * @brief Mount an SMD image on a unit if the file can be opened, defaulting to
 *        "SMD<unit>.IMG" when no image file is given.
 * @param imageFile Image file path, or NULL for the default.
 * @param unit SMD unit to mount on.
 */
void machine_mount_smd(const char *, int);

/**
 * @brief Mount a Winchester image on a unit (0 or 1) if the file can be opened,
 *        defaulting to "WD<unit>.IMG" when no image file is given.
 * @param imageFile Image file path, or NULL for the default.
 * @param unit Winchester unit to mount on.
 */
void machine_mount_winchester(const char *, int);

/**
 * @brief Mount a SCSI image on a unit if the file can be opened, defaulting to
 *        "SCSI<unit>.IMG"; logs an error when the image cannot be opened.
 * @param imageFile Image file path, or NULL for the default.
 * @param unit SCSI unit to mount on.
 */
void machine_mount_scsi(const char *, int);

/**
 * @brief Mount the default floppy and SMD images for units that are not already
 *        mounted, if those image files exist.
 */
void machine_auto_mount_drives(void);

/**
 * @brief Load or boot according to the boot type: load a BP/BPUN/a.out/tape image or
 *        boot from a block device, set the start address, then automount drives.
 * @param bootType Which boot path to take.
 * @param bootUnit Unit to boot from for block devices.
 * @param imageFile Image file to load or mount.
 * @param verbose true to print loader detail.
 * @param text_start Text segment load address for a.out images.
 * @param overlay_deposit true to deposit over existing memory rather than clearing it.
 * @return 0 on success, PROGRAM_LOAD_ERR_LOAD (-2) if the image could not be loaded,
 *         PROGRAM_LOAD_ERR_BOOT (-10) if the boot device failed, -1 for BOOT_NONE.
 */
int machine_program_load(BOOT_TYPE, int, const char *, bool, uint16_t, bool);

/**
 * @brief Return the mounted-drive table for the given drive type.
 * @param drive_type Which drive table to return.
 * @return Pointer to the first entry of that table, or NULL if there is none.
 */
MountedDriveInfo_t *machine_list_mount(DRIVE_TYPE);

/**
 * @brief Mount a drive in OPFS mode, with no FILE*; the actual I/O goes through the
 *        JS opfsBlockRead/Write functions.
 * @param drive_type Which drive table to act on.
 * @param unit Unit index inside that table.
 * @param name Short drive name.
 * @param description Longer drive description.
 * @param imageSize Image size in bytes.
 */
void machine_mount_drive_opfs(DRIVE_TYPE, int, const char *, const char *, size_t);

/**
 * @brief Mount a drive in gateway mode, with no FILE*; the actual I/O goes through the
 *        JS gatewayBlockRead/Write functions over a WebSocket.
 * @param drive_type Which drive table to act on.
 * @param unit Unit index inside that table.
 * @param name Short drive name.
 * @param description Longer drive description.
 * @param imageSize Image size in bytes.
 */
void machine_mount_drive_gateway(DRIVE_TYPE, int, const char *, const char *, size_t);

/**
 * @brief Block-read callback for block devices: reads size blocks from the mounted
 *        image (local file, remote data, OPFS or gateway) into the buffer.
 * @param device Device asking for the read; its type selects the drive table.
 * @param buffer Destination buffer for the block data.
 * @param size Number of blocks to read.
 * @param blockAddress First block number to read.
 * @param unit Unit index inside the drive table.
 * @return Number of blocks read on success, or a value <= 0 on error (-1 for bad
 *         arguments, unknown device, bad unit or unmounted drive).
 */
int machine_block_read(Device *, uint8_t *, size_t, uint32_t, int);

/**
 * @brief Block-write callback for block devices: writes size blocks from the buffer to
 *        the mounted image.
 * @param device Device asking for the write; its type selects the drive table.
 * @param buffer Source buffer holding the block data.
 * @param size Number of blocks to write.
 * @param blockAddress First block number to write.
 * @param unit Unit index inside the drive table.
 * @return Number of blocks written on success, -1 for bad arguments, unknown device,
 *         bad unit or unmounted drive.
 */
int machine_block_write(Device *, const uint8_t *, size_t, uint32_t, int);

/**
 * @brief Disk-info callback for block devices: reports the mounted image size and its
 *        write-protect state. OPFS, gateway and remote images are never write protected.
 * @param device Device asking for the info; its type selects the drive table.
 * @param image_size Receives the image size in bytes.
 * @param is_write_protected Receives the write-protect flag.
 * @param unit Unit index inside the drive table.
 * @return 0 on success, -1 for bad arguments, unknown device, bad unit or unmounted drive.
 */
int machine_block_disk_info(Device *, size_t *, bool *, int);


#ifdef __EMSCRIPTEN__
/* Browser storage imports, defined with EM_JS in machine.c (OPFS and the
 * gateway WebSocket disk service). Return bytes transferred, or < 0. */
int opfs_block_read_js(int driveType, int unit, uint8_t *buffer, int bytes, int offset);
int opfs_is_available_js(int driveType, int unit);
int gateway_block_read_js(int driveType, int unit, uint8_t *buffer, int bytes, int offset);
int gateway_is_available_js(int driveType, int unit);
#endif

#endif
