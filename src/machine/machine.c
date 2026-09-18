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
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "machine_types.h"
#include "machine_protos.h"

#include "../devices/devices_types.h"
#include "../devices/devices_protos.h"
#include "../devices/papertape/devicePapertape.h"  /* --boot=tape mounts the remainder */

#include "../ndlib/ndlib_types.h"
#include "../ndlib/ndlib_protos.h"
#include "../ndlib/floppydb.h"   /* online floppy/disk catalog (machine_floppy_mount_catalog) */

#ifndef _WIN32
#  include "../../external/libsymbols/include/symbols.h"
#  include "../../external/libsymbols/include/aout.h"
#endif


// Global arrays for mounted drive information
MountedDriveInfo_t* floppy_drives = NULL;
MountedDriveInfo_t* smd_drives = NULL;
// Winchester (ST506/8 inch, cards 3041/3038). TWO units per disk system:
// ND-11.015.01 sec 3.1 and the single unit bit (control word b9).
MountedDriveInfo_t* wd_drives = NULL;
MountedDriveInfo_t* scsi_drives = NULL;


/* Must stay index-aligned with BOOT_TYPE in machine_types.h.
 * "prog" was missing, which shifted every entry from BOOT_FLOPPY on:
 * boot_type_str[BOOT_FLOPPY] printed "smd". */
const char* boot_type_str[] = {
    "none",
    "bpun",
    "aout",
    "bp",
    "prog",
    "floppy",
    "smd",
    "scsi",
    "cdc",
    "tape",
    "wd"
};


// Initialize drive arrays
void init_drive_arrays(void) {
    if (!floppy_drives) {
        floppy_drives = calloc(3, sizeof(MountedDriveInfo_t));
    }
    if (!smd_drives) {
        smd_drives = calloc(4, sizeof(MountedDriveInfo_t));
    }
    if (!scsi_drives) {
        scsi_drives = calloc(SCSI_MAX_UNITS, sizeof(MountedDriveInfo_t));
    }
    if (!wd_drives) {
        wd_drives = calloc(2, sizeof(MountedDriveInfo_t));
    }
}

// Clean up drive arrays
static void cleanup_drive_arrays(void) {
    if (floppy_drives) {
        free(floppy_drives);
        floppy_drives = NULL;
    }
    if (smd_drives) {
        free(smd_drives);
        smd_drives = NULL;
    }
    if (scsi_drives) {
        free(scsi_drives);
        scsi_drives = NULL;
    }
    if (wd_drives) {
        free(wd_drives);
        wd_drives = NULL;
    }
}

// Map a drive type to its mounted-drive array and unit count.
// Returns NULL for an unknown drive type, or if the array is not allocated.
// This is the single place that knows the per-type array layout - callers must
// not re-derive it, or a new drive type silently aliases onto another one.
static MountedDriveInfo_t *drives_for_type(DRIVE_TYPE drive_type, int *max_units)
{
    MountedDriveInfo_t *drives = NULL;
    int units = 0;

    switch (drive_type) {
    case DRIVE_SMD:
        drives = smd_drives;
        units = 4;              // SMD has units 0-3
        break;
    case DRIVE_FLOPPY:
        drives = floppy_drives;
        units = 3;              // Floppy has units 0-2
        break;
    case DRIVE_SCSI:
        drives = scsi_drives;
        units = SCSI_MAX_UNITS; // SCSI targets are IDs 0-6 (7 is the controller)
        break;
    case DRIVE_WINCHESTER:
        drives = wd_drives;
        units = 2;              // Winchester has units 0-1 (one unit-select bit)
        break;
    default:
        break;
    }

    if (max_units)
        *max_units = units;
    return drives;
}

// Human-readable drive type, for log messages.
static const char *drive_type_name(DRIVE_TYPE drive_type)
{
    switch (drive_type) {
    case DRIVE_SMD:        return "SMD";
    case DRIVE_FLOPPY:     return "floppy";
    case DRIVE_SCSI:       return "SCSI";
    case DRIVE_WINCHESTER: return "Winchester";
    default:               return "unknown";
    }
}

// Map a device type to its drive type. Returns false if the device is not a
// block device this machine layer knows how to back with an image.
static bool drive_type_for_device(const Device *device, DRIVE_TYPE *drive_type)
{
    if (!device || !drive_type)
        return false;

    switch (device->type) {
    case DEVICE_TYPE_DISC_SMD:
        *drive_type = DRIVE_SMD;
        return true;
    case DEVICE_TYPE_DISC_SCSI:
        *drive_type = DRIVE_SCSI;
        return true;
    case DEVICE_TYPE_DISC_WINCHESTER:
        *drive_type = DRIVE_WINCHESTER;
        return true;
    case DEVICE_TYPE_FLOPPY_PIO:
    case DEVICE_TYPE_FLOPPY_DMA:
        *drive_type = DRIVE_FLOPPY;
        return true;
    default:
        return false;
    }
}

void
machine_init (bool debuggerEnabled, int debuggerPort)
{

    // Initialize drive arrays
    init_drive_arrays();

    // Initialize the CPU
    cpu_init(debuggerEnabled, debuggerPort);

    // Initialize the CPU debugger (if enabled, starts the debugger thread)
    init_cpu_debugger();

    // Initialize IO devices
    IO_Init();

    // Set the CPU to RUN mode
    set_cpu_run_mode(CPU_RUNNING);
}

void machine_add_hdlc(int deviceNum, bool isServer, const char *address, int port)
{
    bool success = DeviceManager_AddHDLCDevice_WithConfig(deviceNum, isServer, address, port);
    if (success) {
        if (isServer) {
            Log(LOG_INFO, "HDLC %d added (server mode on port %d)\n", deviceNum, port);
        } else {
            Log(LOG_INFO, "HDLC %d added (client mode to %s:%d)\n",
                deviceNum, address, port);
        }
    } else {
        Log(LOG_ERROR, "Failed to add HDLC device %d\n", deviceNum);
    }
}

void
cleanup_machine (void)
{
    cleanup_cpu();
    IO_Destroy();

    // Unmount all drives to prevent memory leaks

	// Unmount all floppy drives
	if (floppy_drives) {
		for (int i = 0; i < 3; i++) {
			if (floppy_drives[i].name[0] != '\0') {
				unmount_drive(DRIVE_FLOPPY, i);
			}
		}
	}

	// Unmount all SMD drives
	if (smd_drives) {
		for (int i = 0; i < 4; i++) {
			if (smd_drives[i].name[0] != '\0') {
				unmount_drive(DRIVE_SMD, i);
			}
		}
	}

	// Unmount all Winchester drives
	if (wd_drives) {
		for (int i = 0; i < 2; i++) {
			if (wd_drives[i].name[0] != '\0') {
				unmount_drive(DRIVE_WINCHESTER, i);
			}
		}
	}

	// Unmount all SCSI drives
	if (scsi_drives) {
		for (int i = 0; i < SCSI_MAX_UNITS; i++) {
			if (scsi_drives[i].name[0] != '\0') {
				unmount_drive(DRIVE_SCSI, i);
			}
		}
	}

	// Clean up drive arrays
	cleanup_drive_arrays();

}




/// @brief Do NOT call from debugger thread
/// @param ticks Number of ticks to run the CPU. Use -1 for infinite.
void  machine_run (int ticks)
{
    // Run the CPU until it stops but also handle debugger requests
    while (get_cpu_run_mode() != CPU_SHUTDOWN)
    {
        ticks = cpu_run(ticks);

        // Check if DAP adapter has requested a pause
        if (gDebuggerEnabled)
        {
            if (get_debugger_request_pause())
            {
                if (!get_debugger_control_granted())
                {
                    set_debugger_control_granted(true);
                }
#ifndef __EMSCRIPTEN__
                // A DAP command is in flight and owns the CPU: do not execute
                // instructions while it works; park briefly and return so the
                // main loop keeps servicing keyboard/timeouts until released.
                sleep_ms(1);
#endif
                return;
            }

#ifdef __EMSCRIPTEN__
            /* WASM: if debugger has control, exit immediately to avoid
               busy-looping in single-threaded environment */
            if (get_debugger_control_granted())
            {
                return;
            }
#endif
            if ((get_cpu_run_mode() == CPU_PAUSED) || (get_cpu_run_mode() == CPU_BREAKPOINT))
            {
#ifndef __EMSCRIPTEN__
                sleep_ms(1); // idle politely while stopped in the debugger
#endif
                return; // exit back to main loop to handle keyboard input
            }

            // Free-running with the debugger enabled: deliberately NO sleep.
            // The old unconditional sleep_ms(100) after every 5000-instruction
            // batch capped the whole --debugger session at 5000/0.1s = ~50k
            // instructions/s (the "DAP tax"): a debugger-attached TSS read its
            // 50 Hz KLOK at 4.9 Hz because 50000/10550 = 4.74.
        }

        if (ticks == 0) return; // No more ticks to run
    }
}

void machine_stop(void)
{
    // Stop the CPU
    set_cpu_run_mode(CPU_STOPPED);
}


/*
 *
 *
 *  CONFIGURATION HANDLING
 *
 *
 */

 BOOT_TYPE	BootType; /* Variable holding the way we should boot up the emulator */

 void  setdefaultconfig (void)
 {
     // Set default configuration
     BootType = BOOT_SMD;
     STARTADDR = 0;
     DISASM = 0;
 }



/*
 *
 *
 *  BOOT HANDLING
 *
 *
 */


/// @brief Callback function to write to memory. Used by aout loader.
/// @param address Address to write to
/// @param value Value to write
void write_memory(uint32_t address, uint16_t value)
{
    // Write the value to physical memory at the given address
    WritePhysicalMemory((int)address, value, false);
    if (DISASM && address <= 0xFFFF) disasm_addword((uint16_t)address, value);
}


void mount_floppy(const char *imageFile, int unit)
{
    const char *floppy_img = imageFile ? imageFile : "FLOPPY.IMG";

    // if file exists  mount it
    FILE *ftmp = fopen(floppy_img, "rb");
    if (ftmp) {
        fclose(ftmp);
        mount_drive(DRIVE_FLOPPY, unit, "md5-unknown", "Boot Floppy", "Boot floppy image", floppy_img);
    }
}

/*
 * Runtime floppy HOT-SWAP for automation (the "operator inserts disk 2" step).
 * Ejects whatever is on floppy `unit` (0-2) and mounts `path`; a NULL/empty path
 * ejects only. Safe to call while the machine is running - the DMA/PIO floppy
 * re-reads size + blocks via the machine_block_* callbacks on the next command,
 * so the guest sees the new disk as soon as it issues its next floppy read
 * (which is exactly what an installer does after its "insert next disk" prompt).
 * Returns 0 = ok, -1 = bad unit, -2 = image file could not be opened.
 */
int machine_floppy_swap(int unit, const char *path)
{
    if (unit < 0 || unit >= 3) return -1;                 /* floppy units 0-2 */

    if (isMounted(DRIVE_FLOPPY, unit))
        unmount_drive(DRIVE_FLOPPY, unit);                /* eject: closes the old FILE* */

    if (path == NULL || path[0] == '\0')
        return 0;                                          /* eject only */

    FILE *probe = fopen(path, "rb");                       /* verify before we commit */
    if (!probe) return -2;
    fclose(probe);

    mount_drive(DRIVE_FLOPPY, unit, "md5-unknown", "Floppy", "Hot-swapped floppy", path);
    return 0;
}

/*
 * Mount a disk FROM THE ONLINE CATALOG (https://ndlib.hackercorp.no/floppies.json)
 * onto `unit`, hot-swapping exactly like machine_floppy_swap() - the same eject +
 * mount_drive() path the F12 browser uses, so an installer's "insert next disk"
 * step can be driven straight from the catalog.
 *
 * `selector` is one of:
 *    "md5:<hash>"  - the UNIQUE image hash (always resolves to one image).
 *    "dir:<name>"  - the SINTRAN "Directory name". This MAY match several image
 *                    versions; we log every match as {directory name, filesystem
 *                    image size, md5} to stderr and mount the FIRST, so the caller
 *                    can then pin a specific version by its md5.
 *    "<token>"     - bare: tried as an md5 first, then as a directory name.
 *
 * The catalog is loaded on first use (fresh cache, else download, else stale
 * cache). The image itself is the remote images/<md5>.img, fetched by mount_drive
 * via download_file() - real only on libcurl builds; on a build without libcurl
 * the download is a stub and this returns -4 (resolve the md5 here, fetch the
 * image out of band, then machine_floppy_swap() the local file instead).
 *
 * The drive TYPE (floppy vs SMD) comes from the catalog entry, so `unit` is
 * range-checked against that type (floppy 0-2, SMD 0-3).
 *
 * Returns: 0 ok, -1 bad unit, -2 not found, -3 catalog unavailable, -4 mount failed.
 */
int machine_floppy_mount_catalog(int unit, const char *selector)
{
    if (!selector || !selector[0]) return -2;

    // Load the catalog once (idempotent - a prior F12 browse leaves it loaded).
    if (floppydb_count() == 0) {
        if (floppydb_load(false) <= 0) {
            fprintf(stderr, "[catalog] no catalog available (offline / no cache / no libcurl)\n");
            return -3;
        }
    }

    const FloppyDbEntry *entry = NULL;
    if (strncasecmp(selector, "md5:", 4) == 0) {
        entry = floppydb_find_md5(selector + 4);
    } else if (strncasecmp(selector, "dir:", 4) == 0) {
        const FloppyDbEntry *hits[32];
        int n = floppydb_find_directory(selector + 4, hits, 32);
        if (n > 1) {
            // Ambiguous: log the full disambiguation set, then take the first.
            fprintf(stderr, "[catalog] '%s' is ambiguous - %d images match (mounting the first):\n",
                    selector + 4, n);
            int show = (n < 32) ? n : 32;
            for (int i = 0; i < show; i++)
                fprintf(stderr, "    - dir='%s' size=%ld pages md5=%s\n",
                        hits[i]->directory_name, hits[i]->filesystem_pages, hits[i]->md5);
            fprintf(stderr, "    (pin a specific image with md5:<hash>)\n");
        }
        if (n > 0) entry = hits[0];
    } else {
        entry = floppydb_find_md5(selector);           // bare token: md5 first...
        if (!entry) {                                   // ...then directory name.
            const FloppyDbEntry *hits[1];
            if (floppydb_find_directory(selector, hits, 1) > 0) entry = hits[0];
        }
    }

    if (!entry) { fprintf(stderr, "[catalog] not found: %s\n", selector); return -2; }

    // Map the ndlib is_smd flag to the machine's DRIVE_TYPE, then range-check the unit.
    DRIVE_TYPE dt = entry->is_smd ? DRIVE_SMD : DRIVE_FLOPPY;
    int max_units = entry->is_smd ? 4 : 3;
    if (unit < 0 || unit >= max_units) return -1;

    char url[256];
    floppydb_image_url(entry, url, sizeof(url));

    if (isMounted(dt, unit))
        unmount_drive(dt, unit);                        // eject the old disk first

    mount_drive(dt, unit, entry->md5, entry->name, "Catalog mount", url);

    // mount_drive() is void and bails silently if the download failed; isMounted()
    // is the reliable success signal (is_mounted is set only after a real load).
    if (!isMounted(dt, unit)) {
        fprintf(stderr, "[catalog] mount FAILED (no libcurl? offline?): %s\n", url);
        return -4;
    }
    fprintf(stderr, "[catalog] mounted %s '%s' as %s unit %d (md5 %s)\n",
            entry->is_smd ? "SMD" : "FLOPPY", entry->name,
            entry->is_smd ? "SMD" : "floppy", unit, entry->md5);
    return 0;
}


void mount_smd(const char *imageFile, int unit)
{
    char path[256];
    sprintf(path,"SMD%d.IMG",unit);

    const char *smd_img = imageFile ? imageFile : path;

    // if file exists  mount it
    FILE *ftmp2 = fopen(smd_img, "rb");
    if (ftmp2) {
        fclose(ftmp2);
        if (unit ==0)
        {
            mount_drive(DRIVE_SMD, unit, "md5-unknown", "Boot SMD", "Boot SMD image", smd_img);
        }
        else
        {
            mount_drive(DRIVE_SMD, unit, "md5-unknown", "DATA SMD", "DATA SMD image", smd_img);
        }
    }
}


/* Mount a Winchester image on the given unit (0 or 1).
 *
 * ND-11.015.01 sec 3.1: a disk system has TWO units, and the control word
 * carries the unit in a single bit (b9), so the unit range is a hardware
 * property rather than a configuration choice.
 */
void mount_winchester(const char *imageFile, int unit)
{
    char path[256];
    sprintf(path, "WD%d.IMG", unit);

    const char *wd_img = imageFile ? imageFile : path;

    /* if the file exists, mount it */
    FILE *ftmp = fopen(wd_img, "rb");
    if (ftmp) {
        fclose(ftmp);
        if (unit == 0)
            mount_drive(DRIVE_WINCHESTER, unit, "md5-unknown", "Boot Winchester",
                        "Boot Winchester image", wd_img);
        else
            mount_drive(DRIVE_WINCHESTER, unit, "md5-unknown", "DATA Winchester",
                        "DATA Winchester image", wd_img);
    }
}


/* Mount a SCSI target image on the given unit (SCSI ID 0-6; ID 7 is the
 * ND-3201/3204 controller itself and is never a target).
 *
 * The unit's device type (hdd/tape/cdrom/floppy) is NOT needed here - it only
 * decides which SCSI target class the controller instantiates, and is passed
 * separately to DeviceManager_AddSCSIDevice_WithConfig(). The block offset math
 * in machine_block_read/write uses device->blockSizeBytes, not the mount's
 * block_size, so the mount stays type-agnostic.
 */
void mount_scsi(const char *imageFile, int unit)
{
    char path[256];
    sprintf(path, "SCSI%d.IMG", unit);

    const char *scsi_img = imageFile ? imageFile : path;

    // if file exists mount it
    FILE *ftmp = fopen(scsi_img, "rb");
    if (ftmp) {
        fclose(ftmp);
        if (unit == 0) {
            mount_drive(DRIVE_SCSI, unit, "md5-unknown", "Boot SCSI", "Boot SCSI image", scsi_img);
        } else {
            mount_drive(DRIVE_SCSI, unit, "md5-unknown", "DATA SCSI", "DATA SCSI image", scsi_img);
        }
    } else {
        // Unlike floppy/SMD there is no automount for SCSI - the unit was asked
        // for explicitly on the command line, so a missing image is an error
        // worth reporting rather than a silently absent drive.
        fprintf(stderr, "Error: SCSI unit %d image '%s' could not be opened\n", unit, scsi_img);
    }
}


// As a default, mount floppy and SMD drives (IF they exists)
void autoMountDrives(void)
{
    // Automount floppy if file "FLOPPY.IMG" exists
    if (!isMounted(DRIVE_FLOPPY,0))
    {
        mount_floppy(NULL,0);
    }

    // Automount SMD files
    for (int i=0; i<4;i++)
    {
        if (!isMounted(DRIVE_SMD,i))
        {
            mount_smd(NULL,i);
        }
    }

}

/* --boot=tape: boot an octal-ASCII leader tape from the paper-tape reader.
 *
 * This is the front-panel tape load: the leader part of the tape is octal
 * ASCII of the form "<addr>/" then one "<word> CR LF" per word (each word
 * DEPOSITED at the running location counter) and finally "<addr>!" - jump
 * to that address. Everything AFTER the '!' is left MOUNTED on the
 * paper-tape reader at address 0400, exactly like the physical tape that
 * stays in the reader: the just-started program reads the binary part of
 * the tape from the device itself.
 *
 * This is what a NORD TSS "CDBIN" distribution tape needs: its )8DUMP
 * header (TSS source TSS3.SYMB:191-235 / TDUMP.SYMB) is HLOAD in ASCII,
 * then TBOOT/HDKOP as raw binary that HLOAD reads from the reader, then
 * disc-tagged load blocks that TBOOT writes to the system disc. The
 * BOOT_BPUN path cannot load such a tape: it parses the ASCII part as
 * metadata only and expects a framed binary block after the '!'.
 *
 * Bytes are masked to 7 bits (punched parity); NUL leader/trailer bytes
 * and LF are ignored. Returns the '!' start address, or -1 on error.     */
static int tape_leader_load(const char *path, bool verbose)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        printf("Failed to open tape file '%s': %s\n", path, strerror(errno));
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    if (len <= 0)
    {
        fclose(f);
        printf("Tape file '%s' is empty\n", path);
        return -1;
    }
    uint8_t *data = (uint8_t *)malloc((size_t)len);
    if (!data || fread(data, 1, (size_t)len, f) != (size_t)len)
    {
        fclose(f);
        free(data);
        printf("Cannot read tape file '%s'\n", path);
        return -1;
    }
    fclose(f);

    uint16_t acc = 0, loc = 0, lo = 0xFFFF, hi = 0;
    int have = 0, start = -1, words = 0;
    long i;
    for (i = 0; i < len; i++)
    {
        int c = data[i] & 0x7F;
        if (c >= '0' && c <= '7')
        {
            acc = (uint16_t)((acc << 3) | (uint16_t)(c - '0'));
            have = 1;
        }
        else if (c == '/')
        {
            if (have) { loc = acc; }
            acc = 0; have = 0;
        }
        else if (c == 015)              /* CR: deposit the pending word */
        {
            if (have)
            {
                WritePhysicalMemory((int)loc, acc, false);
                if (loc < lo) { lo = loc; }
                if (loc > hi) { hi = loc; }
                loc++; words++;
            }
            acc = 0; have = 0;
        }
        else if (c == '!')
        {
            start = have ? (int)acc : (int)loc;
            i++;                        /* the tape rests just after '!' */
            break;
        }
        /* NUL leader, LF and anything else: the leader ignores it */
    }
    if (start < 0)
    {
        free(data);
        printf("Tape '%s' has no '!' start marker in its ASCII leader\n", path);
        return -1;
    }

    /* leave the rest of the tape in the reader for the started program */
    Device *reader = DeviceManager_GetDeviceByAddress(0400);
    if (reader != NULL && i < len)
    {
        PaperTape_LoadTape(reader, data + i, (size_t)(len - i));
    }
    else if (reader == NULL)
    {
        printf("Warning: no paper-tape reader at 0400 - tape remainder "
               "not mounted\n");
    }

    if (verbose)
    {
        printf("Tape leader: %d words deposited at %06o-%06o, start %06o, "
               "%ld bytes left in the reader\n",
               words, lo, hi, (unsigned)start, len - i);
    }
    free(data);
    return start;
}

 int program_load(BOOT_TYPE bootType, int bootUnit, const char *imageFile, bool verbose, uint16_t text_start, bool overlay_deposit)
 {
     int bootAddress;
     int result;
     STARTADDR = 0;

     switch (bootType)
     {
     case BOOT_BP:
         bootAddress = bp_load(imageFile);
         if (bootAddress < 0)
         {
             printf("Error loading BP file '%s'\n", imageFile);
#ifdef __EMSCRIPTEN__
             return -1;
#else
             exit(1);
#endif
         }
         break;

     case BOOT_BPUN:
         bootAddress = LoadBPUN(imageFile,verbose);
         if (bootAddress < 0)
         {
             printf("Error loading BPUN file '%s'\n", imageFile);
#ifdef __EMSCRIPTEN__
             return -1;
#else
             exit(1);
#endif
         }
         STARTADDR = bootAddress;
         break;
    case BOOT_AOUT:
#ifdef _WIN32
        printf("Error: AOUT boot not available on Windows (libsymbols not ported yet)\n");
        exit(1);
#else
        bootAddress = load_aout(imageFile, verbose, write_memory, text_start, overlay_deposit);
        if (bootAddress < 0)
        {
            printf("Error loading AOUT file '%s'\n", imageFile);
#ifdef __EMSCRIPTEN__
            return -1;
#else
            exit(1);
#endif
        }
        STARTADDR = bootAddress;
#endif
        break;
    case BOOT_PROG:
        /* SINTRAN :PROG loadable image. LoadPROG() writes the Bank 1 image to
         * physical memory and returns the program start address. Unlike BPUN,
         * the entry is the real start address (no separate boot/action fields). */
        bootAddress = LoadPROG(imageFile, verbose);
        if (bootAddress < 0)
        {
            printf("Error loading PROG file '%s'\n", imageFile);
#ifdef __EMSCRIPTEN__
            return -1;
#else
            exit(1);
#endif
        }
        STARTADDR = bootAddress;
        break;
     case BOOT_FLOPPY:
        // Record mount state for UI/menus; device still boots via BPUN for now
         mount_floppy(imageFile,0);

         bootAddress = LoadBPUN(imageFile, verbose);
         if (bootAddress < 0)
         {
             printf("Error loading BPUN file\n");
#ifdef __EMSCRIPTEN__
             return -1;
#else
             exit(1);
#endif
         }

         STARTADDR = bootAddress;

         //gPC = (CONFIG_OK) ? bootaddress : 0;

         /*
         result = sectorread(0, 0, 1, (ushort *)&VolatileMemory);
         if (result < 0) {
             printf("Error reading from floppy\n");
             exit(1);
         }
         gPC = 0;
         */
         break;
     case BOOT_SMD:

        // Only mount from MEMFS file if not already mounted (gateway/OPFS mounts take priority)
        if (!isMounted(DRIVE_SMD, bootUnit)) {
            mount_smd(imageFile, bootUnit);
        }

         bootAddress = DeviceManager_BootFrom(DEVICE_TYPE_DISC_SMD, bootUnit);
         if (bootAddress < 0)
         {
             printf("Error booting from SMD unit %d\n", bootUnit);
#ifdef __EMSCRIPTEN__
             return -1;
#else
             exit(10);
#endif
         }
         STARTADDR = bootAddress;
         break;
     case BOOT_WINCHESTER:

        // Only mount from MEMFS file if not already mounted
        if (!isMounted(DRIVE_WINCHESTER, bootUnit)) {
            mount_winchester(imageFile, bootUnit);
        }

         bootAddress = DeviceManager_BootFrom(DEVICE_TYPE_DISC_WINCHESTER, bootUnit);
         if (bootAddress < 0)
         {
             printf("Error booting from Winchester unit %d\n", bootUnit);
#ifdef __EMSCRIPTEN__
             return -1;
#else
             exit(10);
#endif
         }
         STARTADDR = bootAddress;
         break;
     case BOOT_SCSI:

        // Only mount from MEMFS file if not already mounted
        if (!isMounted(DRIVE_SCSI, bootUnit)) {
            mount_scsi(imageFile, bootUnit);
        }

         bootAddress = DeviceManager_BootFrom(DEVICE_TYPE_DISC_SCSI, bootUnit);
         if (bootAddress < 0)
         {
             printf("Error booting from SCSI unit %d\n", bootUnit);
#ifdef __EMSCRIPTEN__
             return -1;
#else
             exit(10);
#endif
         }
         STARTADDR = bootAddress;
         break;
     case BOOT_TAPE:
         bootAddress = tape_leader_load(imageFile, verbose);
         if (bootAddress < 0)
         {
             printf("Error booting tape '%s'\n", imageFile);
#ifdef __EMSCRIPTEN__
             return -1;
#else
             exit(10);
#endif
         }
         STARTADDR = bootAddress;
         break;

     case BOOT_CDC:

        /* The NORD TSS cartridge disc. On real hardware the LOAD button and
         * microcode read sector 0 into core and start at 0; TSS's own
         * LOAD-SYSTEM command (LOADV) does exactly the same thing at runtime.
         * The CDC controller keeps its surface in memory, so there is no
         * mount step here - the image is attached with --cdc=FILE. */
         bootAddress = DeviceManager_BootFrom(DEVICE_TYPE_CDC, bootUnit);
         if (bootAddress < 0)
         {
             printf("Error booting from CDC disc\n");
#ifdef __EMSCRIPTEN__
             return -1;
#else
             exit(10);
#endif
         }
         STARTADDR = bootAddress;
         break;
     case BOOT_NONE:
         return -1;
     }

     autoMountDrives();
     return 0;
 }

 /** DEVICE MOUNTING */

 // Return true if the drive is already mounted
 bool isMounted(DRIVE_TYPE drive_type, int unit)
 {
    MountedDriveInfo_t* drives = NULL;
    int max_units = 0;

    // Determine which array to use and max units
    drives = drives_for_type(drive_type, &max_units);
    if (!max_units) {
        //printf("Error: Invalid drive type\n");
        return false;
    }

    // Check if unit is valid
    if (unit < 0 || unit >= max_units) {
        return false;
    }

    // Safety check for NULL array
    if (!drives) {
        return false;
    }

    return drives[unit].is_mounted;
 }

 // Mount a drive to the specified unit
void mount_drive(DRIVE_TYPE drive_type, int unit, const char *md5, const char *name, const char *description, const char *image_path) {
    MountedDriveInfo_t* drives = NULL;
    int max_units = 0;

    // Determine which array to use and max units
    drives = drives_for_type(drive_type, &max_units);
    if (!max_units) {
        return; // Unknown drive type
    }

    // Check if unit is valid
    if (unit < 0 || unit >= max_units) {
        return;
    }

    // Lazy init if drive arrays not yet allocated
    if (!drives) {
        init_drive_arrays();
        drives = drives_for_type(drive_type, NULL);
        if (!drives) {
            return;
        }
    }

    // Handle image path (HTTP download or local file)
    if (image_path) {
        // Set block size based on drive type
        if (drive_type == DRIVE_SMD) {
            drives[unit].block_size = 1024;  // 1KB for SMD
        } else if (drive_type == DRIVE_SCSI) {
            drives[unit].block_size = 1024;  // 1KB for the ND SCSI disk (Micropolis 1375-ND)
        } else {
            // For floppy, we'll use 512 bytes as default, but could be determined from file
            drives[unit].block_size = 512;   // 512 bytes for floppy
        }

        // Store the image path
        strncpy(drives[unit].image_path, image_path, sizeof(drives[unit].image_path) - 1);
        drives[unit].image_path[sizeof(drives[unit].image_path) - 1] = '\0';

        // Check if it's an HTTP URL (case insensitive)
        if (strncasecmp(image_path, "http", 4) == 0) {
            //printf("Downloading image from: %s\n", image_path);
            char* image_data = download_file(image_path);
            if (image_data) {
                drives[unit].is_remote = true;
                drives[unit].data.remote_data = image_data;
                drives[unit].data_size = get_downloaded_size();  // Use actual size instead of strlen()
                //printf("Downloaded %zu bytes of image data\n", drives[unit].data_size);
            } else {
                //printf("Error: Failed to download image from %s\n", image_path);
                return;
            }
        } else {


            drives[unit].is_writeprotected = false;

            // Local file - open for read-write binary
            FILE* file = fopen(image_path, "rb+");
            if (!file) {
                int saved_errno = errno;
                // If we dont have write access, try to open the file for read-only
                if (saved_errno == EACCES || saved_errno == EROFS || saved_errno == EPERM) {
                    file = fopen(image_path, "rb");  // fallback
                    if (file) drives[unit].is_writeprotected = true;
                }
                if (!file) {
                    return;
                }
            }
            if (file) {
                // Get file size
                fseek(file, 0, SEEK_END);
                long file_size = ftell(file);
                fseek(file, 0, SEEK_SET);

                drives[unit].is_remote = false;
                drives[unit].data.local_file = file;
                drives[unit].data_size = (size_t)file_size;

                //printf("Opened local file: %s (size: %ld bytes)\n", image_path, file_size);
            } else {
                printf("mount_drive: Failed to open %s\n", image_path);
                drives[unit].is_mounted = false;
                return;
            }
        }
    }

    // Mount the drive
    drives[unit].is_mounted = true;

    strncpy(drives[unit].md5, md5, sizeof(drives[unit].md5) - 1);
    drives[unit].md5[sizeof(drives[unit].md5) - 1] = '\0';

    strncpy(drives[unit].name, name, sizeof(drives[unit].name) - 1);
    drives[unit].name[sizeof(drives[unit].name) - 1] = '\0';

    strncpy(drives[unit].description, description, sizeof(drives[unit].description) - 1);
    drives[unit].description[sizeof(drives[unit].description) - 1] = '\0';

#ifdef _debug_
    printf("Mounted %s to %s unit %d:\n",
           drive_type_name(drive_type),
           drive_type_name(drive_type),
           unit);
    printf("  Name: %s\n", name);
    printf("  Description: %s\n", description);
    printf("  MD5: %s\n", md5);
    printf("  Image Path: %s\n", image_path ? image_path : "None");
#endif

}

// Unmount a drive from the specified unit
void unmount_drive(DRIVE_TYPE drive_type, int unit) {
    MountedDriveInfo_t* drives = NULL;
    int max_units = 0;

    // Determine which array to use and max units
    drives = drives_for_type(drive_type, &max_units);
    if (!max_units) {
        printf("Error: Invalid drive type\n");
        return;
    }

    // Check if unit is valid
    if (unit < 0 || unit >= max_units) {
        printf("Error: Invalid unit %d for drive type %d\n", unit, drive_type);
        return;
    }

    // Check if array is initialized
    if (!drives) {
        printf("Error: Drive arrays not initialized\n");
        return;
    }

    // Check if drive is mounted
    if (drives[unit].name[0] == '\0') {
        printf("Error: No drive mounted on %s unit %d\n",
               drive_type_name(drive_type), unit);
        return;
    }

    // Unmount the drive
    printf("Unmounting %s from %s unit %d:\n",
           drives[unit].name,
           drive_type_name(drive_type),
           unit);

    // Clean up data based on type
    if (drives[unit].is_opfs || drives[unit].is_gateway) {
        // OPFS/gateway drives have no FILE* or malloc'd data - nothing to free
        drives[unit].data.local_file = NULL;
    } else if (drives[unit].is_remote) {
        // Free downloaded remote data
        if (drives[unit].data.remote_data) {
            free(drives[unit].data.remote_data);
            drives[unit].data.remote_data = NULL;
        }
    } else {
        // Close local file
        if (drives[unit].data.local_file) {
            fclose(drives[unit].data.local_file);
            drives[unit].data.local_file = NULL;
        }
    }

    // Clear the drive entry
    drives[unit].is_mounted = false;
    drives[unit].is_opfs = false;
    drives[unit].is_gateway = false;
    drives[unit].md5[0] = '\0';
    drives[unit].name[0] = '\0';
    drives[unit].description[0] = '\0';
    drives[unit].image_path[0] = '\0';
    drives[unit].is_remote = false;
    drives[unit].data_size = 0;
    drives[unit].block_size = 0;
}

// List mounted drives for the specified drive type
MountedDriveInfo_t* list_mount(DRIVE_TYPE drive_type) {
    return drives_for_type(drive_type, NULL);
}

#ifdef __EMSCRIPTEN__
/* OPFS block I/O via JavaScript.
 * In Worker mode, JS calls SyncAccessHandle.read/write directly.
 * In Direct mode, JS reads/writes an in-memory ArrayBuffer.
 * These EM_JS functions call into the global opfsBlockRead/Write
 * which are set up by emu-worker.js or emu-proxy.js.
 */
/* driveType is threaded through so OPFS storage is namespaced per type - SCSI
 * unit 0 must not alias SMD unit 0 in the SyncAccessHandle pool. Matches the
 * gateway_block_*_js signature. */
EM_JS(int, opfs_block_read_js, (int driveType, int unit, uint8_t *buffer, int bytes, int offset), {
    if (typeof opfsBlockRead === 'function') {
        return opfsBlockRead(driveType, unit, buffer, bytes, offset);
    }
    return -1;
});

EM_JS(int, opfs_block_write_js, (int driveType, int unit, const uint8_t *buffer, int bytes, int offset), {
    if (typeof opfsBlockWrite === 'function') {
        return opfsBlockWrite(driveType, unit, buffer, bytes, offset);
    }
    return -1;
});

EM_JS(int, opfs_is_available_js, (int driveType, int unit), {
    if (typeof opfsIsAvailable === 'function') {
        return opfsIsAvailable(driveType, unit);
    }
    return 0;
});

/* Gateway block I/O via JavaScript sub-worker.
 * In Worker mode, a dedicated sub-worker holds a WebSocket to the gateway
 * server and uses SharedArrayBuffer + Atomics for synchronous block I/O.
 * These EM_JS functions call into the global gatewayBlockRead/Write
 * which are set up by emu-worker.js.
 */
EM_JS(int, gateway_block_read_js, (int driveType, int unit, uint8_t *buffer, int bytes, int offset), {
    if (typeof gatewayBlockRead === 'function') {
        return gatewayBlockRead(driveType, unit, buffer, bytes, offset);
    }
    return -1;
});

EM_JS(int, gateway_block_write_js, (int driveType, int unit, const uint8_t *buffer, int bytes, int offset), {
    if (typeof gatewayBlockWrite === 'function') {
        return gatewayBlockWrite(driveType, unit, buffer, bytes, offset);
    }
    return -1;
});

EM_JS(int, gateway_is_available_js, (int driveType, int unit), {
    if (typeof gatewayIsAvailable === 'function') {
        return gatewayIsAvailable(driveType, unit);
    }
    return 0;
});
#endif

/* Mount an SMD drive for OPFS mode (no FILE* needed).
 * The actual I/O goes through JS opfsBlockRead/Write. */
void mount_drive_opfs(DRIVE_TYPE drive_type, int unit, const char *name,
                      const char *description, size_t imageSize) {
    MountedDriveInfo_t* drives = NULL;
    int max_units = 0;

    drives = drives_for_type(drive_type, &max_units);
    if (!max_units) return; // Unknown drive type

    if (unit < 0 || unit >= max_units) return;

    if (!drives) {
        init_drive_arrays();
        drives = drives_for_type(drive_type, NULL);
        if (!drives) return;
    }

    drives[unit].is_mounted = true;
    drives[unit].is_remote = false;
    drives[unit].is_opfs = true;
    drives[unit].is_writeprotected = false;
    drives[unit].data.local_file = NULL;
    drives[unit].data_size = imageSize;
    drives[unit].block_size = (drive_type == DRIVE_FLOPPY) ? 512 : 1024;

    strncpy(drives[unit].md5, "opfs", sizeof(drives[unit].md5) - 1);
    strncpy(drives[unit].name, name, sizeof(drives[unit].name) - 1);
    drives[unit].name[sizeof(drives[unit].name) - 1] = '\0';
    strncpy(drives[unit].description, description, sizeof(drives[unit].description) - 1);
    drives[unit].description[sizeof(drives[unit].description) - 1] = '\0';
    drives[unit].image_path[0] = '\0';
}

/* Mount a drive for gateway mode (block I/O via WebSocket, no FILE*).
 * The actual I/O goes through JS gatewayBlockRead/Write. */
void mount_drive_gateway(DRIVE_TYPE drive_type, int unit, const char *name,
                         const char *description, size_t imageSize) {
    MountedDriveInfo_t* drives = NULL;
    int max_units = 0;

    drives = drives_for_type(drive_type, &max_units);
    if (!max_units) return; // Unknown drive type

    if (unit < 0 || unit >= max_units) return;

    if (!drives) {
        init_drive_arrays();
        drives = drives_for_type(drive_type, NULL);
        if (!drives) return;
    }

    drives[unit].is_mounted = true;
    drives[unit].is_remote = false;
    drives[unit].is_opfs = false;
    drives[unit].is_gateway = true;
    drives[unit].is_writeprotected = false;
    drives[unit].data.local_file = NULL;
    drives[unit].data_size = imageSize;
    drives[unit].block_size = (drive_type == DRIVE_FLOPPY) ? 512 : 1024;

    strncpy(drives[unit].md5, "gateway", sizeof(drives[unit].md5) - 1);
    strncpy(drives[unit].name, name, sizeof(drives[unit].name) - 1);
    drives[unit].name[sizeof(drives[unit].name) - 1] = '\0';
    strncpy(drives[unit].description, description, sizeof(drives[unit].description) - 1);
    drives[unit].description[sizeof(drives[unit].description) - 1] = '\0';
    drives[unit].image_path[0] = '\0';
}

// Callback-based block READ for block devices
int machine_block_read(Device *device, uint8_t *buffer, size_t size, uint32_t blockAddress, int unit) {
    if (!device || !buffer || size == 0) return -1;

    DRIVE_TYPE drive_type;
    int max_units = 0;
    if (!drive_type_for_device(device, &drive_type)) return -1;
    MountedDriveInfo_t *drives = drives_for_type(drive_type, &max_units);
    // drives[unit] is indexed below - bound it. SCSI has 7 units where SMD has 4
    // and floppy 3, so an unchecked unit would index past the shorter arrays.
    if (unit < 0 || unit >= max_units) return -1;

#ifdef FLOPPY_DIAG
    // Diagnostic: log first floppy block read attempt
    if (drive_type == DRIVE_FLOPPY) {
        static int _floppy_read_log = 0;
        if (_floppy_read_log < 5) {
            _floppy_read_log++;
            printf("[FLOPPY-DIAG] machine_block_read: unit=%d drives=%s mounted=%d gateway=%d opfs=%d remote=%d size=%d blkAddr=%u blkSize=%u\n",
                unit,
                drives ? "ok" : "NULL",
                drives ? drives[unit].is_mounted : -1,
                drives ? drives[unit].is_gateway : -1,
                drives ? drives[unit].is_opfs : -1,
                drives ? drives[unit].is_remote : -1,
                drives ? (int)drives[unit].data_size : -1,
                blockAddress, (unsigned)device->blockSizeBytes);
        }
    }
#endif

    if (!drives) return -1;

    // block size is determined by the device; size is number of blocks
    size_t bytes = size * device->blockSizeBytes;
    size_t offset = (size_t)blockAddress * device->blockSizeBytes;

    MountedDriveInfo_t *entry = &drives[unit];
    if (!entry->is_mounted) return -1; // not mounted

#ifdef __EMSCRIPTEN__
    if (entry->is_opfs && opfs_is_available_js((int)drive_type, unit)) {
        int rc = opfs_block_read_js((int)drive_type, unit, buffer, (int)bytes, (int)offset);
        if (rc < 0) return -1;
        if ((size_t)rc < bytes) {
            memset(buffer + rc, 0, bytes - rc);
        }
        return (int)size;
    }

    if (entry->is_gateway && gateway_is_available_js((int)drive_type, unit)) {
        int rc = gateway_block_read_js((int)drive_type, unit, buffer, (int)bytes, (int)offset);
        if (rc < 0) return -1;
        if ((size_t)rc < bytes) {
            memset(buffer + rc, 0, bytes - rc);
        }
        return (int)size;
    }
#endif

    if (entry->is_remote) {
        if (!entry->data.remote_data) return -1;
        if (offset >= entry->data_size) return 0;
        size_t to_copy = bytes;
        if (offset + to_copy > entry->data_size) {
            to_copy = entry->data_size - offset;
        }
        memcpy(buffer, entry->data.remote_data + offset, to_copy);
        if (to_copy < bytes) {
            memset(buffer + to_copy, 0, bytes - to_copy);
        }
    } else {
        if (!entry->data.local_file) return -1;
        if (fseek(entry->data.local_file, (long)offset, SEEK_SET) != 0) return -1;
        size_t read_bytes = fread(buffer, 1, bytes, entry->data.local_file);
        if (read_bytes < bytes) {
            memset(buffer + read_bytes, 0, bytes - read_bytes);
        }
    }
    return (int)size; // number of blocks
}

// Callback-based block WRITE for block devices
int machine_block_write(Device *device, const uint8_t *buffer, size_t size, uint32_t blockAddress, int unit) {
    if (!device || !buffer || size == 0) return -1;

    DRIVE_TYPE drive_type;
    int max_units = 0;
    if (!drive_type_for_device(device, &drive_type)) return -1;
    MountedDriveInfo_t *drives = drives_for_type(drive_type, &max_units);
    // drives[unit] is indexed below - bound it. SCSI has 7 units where SMD has 4
    // and floppy 3, so an unchecked unit would index past the shorter arrays.
    if (unit < 0 || unit >= max_units) return -1;
    if (!drives) return -1;

    // block size is determined by the device; size is number of blocks
    size_t bytes = size * device->blockSizeBytes;
    size_t offset = (size_t)blockAddress * device->blockSizeBytes;

    MountedDriveInfo_t *entry = &drives[unit];
    if (!entry->is_mounted) return -1; // not mounted

#ifdef __EMSCRIPTEN__
    if (entry->is_opfs && opfs_is_available_js((int)drive_type, unit)) {
        int rc = opfs_block_write_js((int)drive_type, unit, buffer, (int)bytes, (int)offset);
        return (rc >= 0) ? (int)size : -1;
    }

    if (entry->is_gateway && gateway_is_available_js((int)drive_type, unit)) {
        int rc = gateway_block_write_js((int)drive_type, unit, buffer, (int)bytes, (int)offset);
        return (rc >= 0) ? (int)size : -1;
    }
#endif

    if (entry->is_remote) {
        // For remote images in-memory, allow write if buffer exists and fits
        if (!entry->data.remote_data) return -1;
        if (offset + bytes > entry->data_size) return -1; // out of bounds
        memcpy(entry->data.remote_data + offset, buffer, bytes);
    } else {
        if (!entry->data.local_file) return -1;
        if (fseek(entry->data.local_file, (long)offset, SEEK_SET) != 0) return -1;
        fwrite(buffer, 1, bytes, entry->data.local_file);
        fflush(entry->data.local_file);
    }
    return (int)size;
}

// Callback-based DISK INFO for block devices
// Needed to retrive info about image size and if its write protected
int machine_block_disk_info(Device *device, size_t *image_size, bool *is_write_protected, int unit) {
    if (!device) return -1;

    // Set
    *image_size = 0;
    *is_write_protected = true;

    DRIVE_TYPE drive_type;
    int max_units = 0;
    if (!drive_type_for_device(device, &drive_type)) return -1;
    MountedDriveInfo_t *drives = drives_for_type(drive_type, &max_units);
    // drives[unit] is indexed below - bound it. SCSI has 7 units where SMD has 4
    // and floppy 3, so an unchecked unit would index past the shorter arrays.
    if (unit < 0 || unit >= max_units) return -1;
    if (!drives) return -1;

    MountedDriveInfo_t *entry = &drives[unit];
    if (!entry->is_mounted) return -1; // not mounted

    *image_size = entry->data_size;

    // OPFS, gateway, and remote files are always NOT write protected
    // Local files are write protected if we dont have access to write to the file
    if (entry->is_opfs || entry->is_gateway || entry->is_remote) {
        *is_write_protected = false;
    }
    else {
        *is_write_protected = entry->is_writeprotected;
    }

    return 0;
}
