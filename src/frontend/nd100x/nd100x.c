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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h> // For signal / sigaction
#include <string.h> // For memset
#include <pthread.h>
#include <limits.h>
#include <time.h>   // for time()

#ifdef _WIN32
#  include <windows.h>   /* Sleep, GetProcessTimes, CreateDirectoryA */
#  include <direct.h>    /* _mkdir */
#  define ND_MKDIR(p) _mkdir(p)
#else
#  include <termios.h>
#  include <unistd.h>
#  include <poll.h>
#  include <sys/resource.h>  /* getrusage, struct rusage */
#  include <sys/time.h>      /* struct timeval */
#  define ND_MKDIR(p) mkdir((p), 0755)
#endif


#include "../ndlib/ndlib_types.h"
#include "../ndlib/ndlib_protos.h"
#include "../ndlib/floppydb.h"   /* catalog dbmount/dblist over the --pipe control channel */

#include "../machine/machine_types.h"
#include "../machine/machine_protos.h"
#include "../machine/machine_config.h"
#include "../machine/machine_config_apply.h"
#include "../../cpu/cpu_types.h"   // MMSType enum + extern mmsType (for --mms1/--mms2)
#include "../../cpu/cpu_model.h"   // CpuModel_FromName / _DisplayName (was two static tables here)
#include "devices_types.h"
#include "../../devices/devices_protos.h"

#ifdef WITH_DEBUGGER
void stop_debugger_thread(void);
#endif

#include "nd100x_types.h"
#include "nd100x_protos.h"
#include "keyboard.h"
#include "vscreen.h"

#include "../../devices/papertape/devicePapertape.h"
#include "../../devices/papertapewriter/devicePaperTapeWriter.h"
#include "../../ndlib/printjob.h"

#include "screenmenu.h"
#include "nd100x_shell.h"

#if !defined(PLATFORM_WASM) && !defined(__EMSCRIPTEN__)
#include "../../ndlib/telnetserver.h"
#endif

// dont include debugger.h here, it will cause other problems, but we define the function here
void debugger_kbd_input(char c) ;

#ifndef _WIN32
struct rusage *used;
#endif

double usertime;
double systemtime;
double totaltime;
Config_t config;

// Resolved machine configuration, populated when --config is given.
// When active, initialize() builds the machine from this model instead of the
// legacy per-flag path.
static MachineConfig g_machineConfig;
static bool g_useMachineConfig = false;

// Map a config controller type to the boot enum.
static BOOT_TYPE boot_type_for_ctrl(CtrlType t)
{
    switch (t) {
    case CTRL_SMD:        return BOOT_SMD;
    case CTRL_FLOPPY:     return BOOT_FLOPPY;
    case CTRL_WINCHESTER: return BOOT_WINCHESTER;
    case CTRL_SCSI:       return BOOT_SCSI;
    default:          return BOOT_NONE;
    }
}


#if !defined(PLATFORM_WASM) && !defined(__EMSCRIPTEN__)
static TelnetServer *telnetServer = NULL;

// Carrier callback for telnet connect/disconnect
static void set_terminal_carrier(Device *dev, bool missing)
{
    if (!dev) return;
    TerminalData *data = (TerminalData *)dev->deviceData;
    if (!data) return;

    data->noCarrier = missing;
    data->inputStatus.bits.carrierMissing = missing ? 1 : 0;

    if (missing) {
        // Queue a space character to wake SINTRAN
        Terminal_QueueKeyCode(dev, ' ');
    }
}
#endif

#if !defined(PLATFORM_WASM) && !defined(__EMSCRIPTEN__) && !defined(PLATFORM_RISCV)
// --pipe control channel. A line on stdin framed as 0xFF <command> \n is a CONTROL command, not a
// keystroke (0xFF never occurs in ND keyboard input). Lets an automation driver hot-swap floppies
// mid-run - the "operator inserts the next install disk" step. Results go to stderr with a
// [pipe-control] prefix so the driver can confirm them. Commands:
//    mount <unit> <path>   - eject unit (0-2) and mount <path> (spaces allowed)
//    eject <unit>          - eject unit
static bool s_ctrl_active = false;
static char s_ctrl_buf[512];
static int  s_ctrl_len = 0;

static void pipe_handle_control(const char *cmd)
{
    int unit = -1;
    char arg[400];
    if (sscanf(cmd, "mount %d %399[^\n]", &unit, arg) == 2) {
        // mount <unit> <local-path> : hot-swap a LOCAL floppy image.
        int rc = machine_floppy_swap(unit, arg);
        fprintf(stderr, "[pipe-control] mount %d '%s' -> %s\n", unit, arg,
                rc == 0 ? "ok" : (rc == -2 ? "FILE NOT FOUND" : "BAD UNIT"));
    } else if (sscanf(cmd, "eject %d", &unit) == 1) {
        int rc = machine_floppy_swap(unit, NULL);
        fprintf(stderr, "[pipe-control] eject %d -> %s\n", unit, rc == 0 ? "ok" : "BAD UNIT");
    } else if (sscanf(cmd, "dbmount %d %399[^\n]", &unit, arg) == 2) {
        // dbmount <unit> <md5:hash|dir:name|token> : mount straight from the online
        // catalog (needs libcurl to fetch the image; resolves either way).
        int rc = machine_floppy_mount_catalog(unit, arg);
        const char *msg = (rc == 0) ? "ok" :
                          (rc == -1) ? "BAD UNIT" :
                          (rc == -2) ? "NOT FOUND" :
                          (rc == -3) ? "NO CATALOG" : "MOUNT FAILED (no libcurl?)";
        fprintf(stderr, "[pipe-control] dbmount %d '%s' -> %s\n", unit, arg, msg);
    } else if (sscanf(cmd, "dblist %399[^\n]", arg) == 1) {
        // dblist <md5:hash|dir:name|token> : resolve and list catalog matches without
        // mounting (so a driver can disambiguate before choosing an md5).
        if (floppydb_count() == 0) floppydb_load(false);
        const FloppyDbEntry *hits[32];
        int n = 0;
        if (strncasecmp(arg, "md5:", 4) == 0) {
            const FloppyDbEntry *e = floppydb_find_md5(arg + 4);
            if (e) { hits[0] = e; n = 1; }
        } else {
            const char *name = (strncasecmp(arg, "dir:", 4) == 0) ? arg + 4 : arg;
            n = floppydb_find_directory(name, hits, 32);
        }
        fprintf(stderr, "[pipe-control] dblist '%s' -> %d match%s\n", arg, n, n == 1 ? "" : "es");
        int show = (n < 32) ? n : 32;
        for (int i = 0; i < show; i++)
            fprintf(stderr, "    - name='%s' dir='%s' size=%ld pages type=%s md5=%s\n",
                    hits[i]->name, hits[i]->directory_name, hits[i]->filesystem_pages,
                    hits[i]->is_smd ? "SMD" : "FLOPPY", hits[i]->md5);
    } else {
        fprintf(stderr, "[pipe-control] unknown command: '%s'\n", cmd);
    }
    fflush(stderr);
}

// Feed one raw input byte to the control-line state machine. Returns true if the byte was consumed
// as part of a control line (the caller must then NOT forward it to the emulated terminal).
static bool pipe_control_feed(char ch)
{
    if (!s_ctrl_active) {
        if ((unsigned char)ch == 0xFF) { s_ctrl_active = true; s_ctrl_len = 0; return true; }
        return false;
    }
    if (ch == '\r' || ch == '\n') {
        s_ctrl_buf[s_ctrl_len] = '\0';
        pipe_handle_control(s_ctrl_buf);
        s_ctrl_active = false;
    } else if (s_ctrl_len < (int)sizeof(s_ctrl_buf) - 1) {
        s_ctrl_buf[s_ctrl_len++] = ch;
    }
    return true;
}
#endif

void handle_sigint(int sig) {
    printf("\nCaught signal %d (Ctrl-C). Cleaning up...\n", sig);

#if !defined(PLATFORM_WASM) && !defined(__EMSCRIPTEN__)
    if (telnetServer) {
        TelnetServer_Stop(telnetServer);
        TelnetServer_Destroy(telnetServer);
        telnetServer = NULL;
    }
#endif

#ifdef WITH_DEBUGGER
    // Stop the debugger server gracefully
    stop_debugger_thread();
#endif

    // Stop the machine
    machine_stop();

    // Restore terminal settings before exit
    unsetcbreak();

    // Exit the program
    exit(0);
}




void register_signals(void)
{
#ifdef _WIN32
    // Windows signal handling
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);
#else
    // POSIX signal handling using sigaction
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));  // Initialize the structure
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;  // Restart interrupted system calls

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    if (sigaction(SIGABRT, &sa, NULL) == -1) {
        perror("sigabrt");
        exit(1);
    }
#endif
}

void dump_stats(void)
{
#ifdef _WIN32
    FILETIME creation, exit, kernel, user;
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        ULARGE_INTEGER u, k;
        u.LowPart  = user.dwLowDateTime;  u.HighPart  = user.dwHighDateTime;
        k.LowPart  = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
        /* FILETIME is in 100ns units. */
        usertime   = (double)u.QuadPart / 10000000.0;
        systemtime = (double)k.QuadPart / 10000000.0;
    } else {
        usertime = systemtime = 0.0;
    }
#else
    getrusage(RUSAGE_SELF, used); /* Read how much resources we used */
    usertime   = used->ru_utime.tv_sec + ((float)used->ru_utime.tv_usec / 1000000);
    systemtime = used->ru_stime.tv_sec + ((float)used->ru_stime.tv_usec / 1000000);
#endif
    totaltime = (float)usertime + (float)systemtime;

    printf("Number of instructions run: %llu, time used: %f\n", (unsigned long long)instr_counter, totaltime);
    printf("usertime: %f  systemtime: %f\n", usertime, systemtime);
    if (instr_counter > 0) {
        printf("Current cpu cycle time is:%f microsecs\n",
               (totaltime / ((double)instr_counter / 1000000.0)));
    }
}

// Apply a --cputype=TYPE override to CurrentCPUType. MUST run BEFORE machine_init
// (-> cpu_init -> Setup_Instructions), which reads CurrentCPUType to decide which
// opcodes to install (VERSN, the ND-110-only privileged instructions, RTNSIM on
// ND110PCX). An unknown name is a hard, clearly-reported error (exit 1) rather than
// a silent fall-back to the default model.
static void apply_cputype_override(const char *name)
{
    if (!name) return;
    CpuType t;
    if (!CpuModel_FromName(name, &t)) {
        fprintf(stderr,
            "Invalid --cputype '%s'. Valid values: ND1, ND4, ND10, ND100, ND100CE, "
            "ND100CX, ND110, ND110CE, ND110CX, ND110PCX, ND120CX\n", name);
        exit(1);
    }
    CurrentCPUType = t;
}

/// @brief Initialize the emulator. Add devices and load program

void initialize(void)
{
   srand ( time(NULL) ); /* Generate PRNG Seed */
#ifndef _WIN32
   static struct rusage s_used; /* Perf counter stuff */
   memset(&s_used, 0, sizeof(s_used));
   used = &s_used;
#endif


	//blocksignals();
	register_signals();

#if !defined(PLATFORM_WASM) && !defined(__EMSCRIPTEN__) && !defined(PLATFORM_RISCV)
	// --pipe: automation mode. Keyboard comes from a redirected stdin (a parent process / driver)
	// instead of the interactive console, and stdout is UNBUFFERED so an expect-style driver sees the
	// emulated terminal output as it is produced. Desktop only - WASM drives I/O from the browser and
	// the RISC-V target has no host console/pipe, so the whole block is compiled out there.
	if (config.pipeMode) {
		keyboard_set_pipe_mode(true);
		setvbuf(stdout, NULL, _IONBF, 0);
	}
#endif

	if (DISASM) disasm_init();

	// Select the MMU paging-system type BEFORE machine_init -> cpu_init -> CreatePagingTables(),
	// which reads this cpu_mms.c global to size/lay out the shadow RAM (MMS1 = 4 page tables;
	// MMS2 = 16 page tables). Default MMS2 keeps SINTRAN / every existing machine byte-identical.
	mmsType = (config.mmsType == 1) ? MMS1 : MMS2;

	// Install main memory size BEFORE machine_init -> cpu_init (which lazily sizes the
	// ECC latch calloc(ND_Memsize,...) and the MMS shadow), and before the boot banner
	// below, so all of them see the configured size. words = MB * 524288; range 1..16 MB
	// was already validated by --memory / the .ini memory= key. Bounded by the physical
	// backing array as a belt-and-braces guard (the option parsers already enforce <=16).
	ND_Memsize = (uint32_t)config.memoryMB * ND_WORDS_PER_MB;
	if (ND_Memsize > ND_MEMSIZE_MAX_WORDS)
		ND_Memsize = ND_MEMSIZE_MAX_WORDS;

	// Resolve the CPU model BEFORE machine_init, so Setup_Instructions() inside cpu_init
	// sees the selected model when it gates VERSN / ND-110 opcodes. With no --cputype the
	// built-in default is ND-100/CX - the identity the (hardcoded) VERSN path reports and
	// what TPE/CONFIGURATION prints. Without this, CurrentCPUType is a zero-initialised
	// global (== ND1), which would mislabel the boot line as "ND-1" even though the machine
	// presents itself as ND-100/CX. ND-100/CX and ND1 register the SAME instruction set
	// (only the ND110* models gate extra opcodes), so this default is behaviour-neutral for
	// execution - it only makes the reported label consistent.
	if (config.cpuType == NULL)
		CurrentCPUType = ND100CX;
	apply_cputype_override(config.cpuType);

	// Select the installed FPP width. The 32-bit single-precision FPP was a factory
	// option independent of the CPU model, so this is a separate knob from --cputype.
	// The CLI flag wins over the .ini [machine] fpp= key (applied in
	// apply_machine_config below only when --fpp was not given). Default: FPP48.
	CurrentFPPType = (config.fppBits == 32) ? FPP32 : FPP48;

	// RTC time base. The CLI flag wins over the .ini [machine] rtc= key (applied
	// in apply_machine_config below only when --rtc was not given). Default: ticks.
	RTC_SetWallClockMode(config.rtcWall);

	// Boot banner: LEAD the output with a clean, standalone CPU + memory line (NOT
	// [INFO]-prefixed), printed BEFORE the first device is created (device creation
	// happens inside machine_init below, and the noisy device-manager [INFO] lines are
	// silenced). Reflects the resolved CpuType (--cputype / default) and the installed
	// ND_Memsize just set above. ND_Memsize is in 16-bit WORDS; a word is 2 bytes, so the
	// Mbyte figure is words*2/1MiB - the same "Total memory size" CONFIGURATION reports.
	// The config's CPU model, FPP width and RTC base, BEFORE machine_init()
	// below - Setup_Instructions() reads CurrentCPUType to decide which opcode
	// groups exist, so a model chosen after it would never reach the guest.
	// It also means the banner on the next line reports the CPU the machine is
	// actually about to be, instead of the default it used to print.
	if (g_useMachineConfig) {
		MachineConfigApplyOpts mcOpts;
		mcOpts.fpp_already_set = config.fppSet ? 1 : 0;
		mcOpts.rtc_already_set = config.rtcSet ? 1 : 0;
		MachineConfig_ApplyCpu(&g_machineConfig, &mcOpts);
	}

	printf("CPU: %s   Memory: %.3f Mbytes (%u words)\n",
	       CpuModel_DisplayName(CurrentCPUType),
	       (double)ND_Memsize * 2.0 / (1024.0 * 1024.0),
	       (unsigned)ND_Memsize);

	if (machine_init(config.debuggerEnabled, config.debuggerPort) != 0) {
		fprintf(stderr, "nd100x: machine initialisation failed\n");
		exit(1);
	}

	// Add the NORD TSS CDC cartridge system disc @ IOX 500-507 ONLY when a --cdc image
	// (or the .ini cdc= key) was given, so the 500 slot stays empty otherwise (it never
	// pre-empts a future Winchester). The backing file MUST be set before CreateCdcDevice
	// runs, hence the setter call immediately before AddDevice.
	if (config.cdcFile) {
		CdcDevice_SetBackingFile(config.cdcFile);
		DeviceManager_AddDevice(DEVICE_TYPE_CDC, 0);
	}

	// Add the NORD TSS swapping drum @ IOX 540-547 ONLY when a --drum image (or the .ini
	// drum= key) was given - gated exactly like the CDC above. Default boot installs no
	// drum, so the empty 540 slot no longer trips the device probe's "No identcode found
	// on level 11D ... Device number 000540B" error. Backing file set before AddDevice.
	if (config.drumFile) {
		DrumDevice_SetBackingFile(config.drumFile);
		DeviceManager_AddDevice(DEVICE_TYPE_DRUM, 0);
	}

	if (g_useMachineConfig) {
		// INI-driven machine setup (from --config). Adds terminals, disc
		// controllers, HDLC and CPU type from the resolved MachineConfig.
		// Devices only. The CPU/FPP/RTC half ran before machine_init() - see
		// the call above the boot banner and the note in machine_config_apply.h.
		MachineConfig_ApplyDevices(&g_machineConfig);
	} else {
		//     {0340, 044, 044, "TERMINAL 5/ TET12"},
		DeviceManager_AddDevice(DEVICE_TYPE_TERMINAL, 5);

		// {0350, 045, 045, "TERMINAL 6/ TET11"},
		DeviceManager_AddDevice(DEVICE_TYPE_TERMINAL, 6);

		// {0360, 046, 046, "TERMINAL 7/ TET10"},
		DeviceManager_AddDevice(DEVICE_TYPE_TERMINAL, 7);

		// {0370, 047, 047, "TERMINAL 8/ TET9"},
		DeviceManager_AddDevice(DEVICE_TYPE_TERMINAL, 8);

		// {01300, 050, 060, "TERMINAL 9"},
		DeviceManager_AddDevice(DEVICE_TYPE_TERMINAL, 9);

		// {01310, 051, 061, "TERMINAL 10"},
		DeviceManager_AddDevice(DEVICE_TYPE_TERMINAL, 10);

		// {01320, 052, 062, "TERMINAL 11"},
		DeviceManager_AddDevice(DEVICE_TYPE_TERMINAL, 11);

		// Mount explicitly specified SMD images before program_load
		// (autoMountDrives will skip already-mounted units)
		for (int i = 0; i < 4; i++) {
			if (config.smdFile[i]) {
				mount_smd(config.smdFile[i], i);
			}
		}

		// Winchester (ST506 / 8 inch, cards 3041/3038). Opt-in via --wd0/--wd1:
		// the card answers IOX 500-507, the same address block as the CDC
		// system disc, so adding it unconditionally would change the IOX map
		// of every existing machine configuration.
		if (config.wdEnabled) {
			DeviceManager_AddDevice(DEVICE_TYPE_DISC_WINCHESTER, 0);
			for (int i = 0; i < 2; i++) {
				if (config.wdFile[i]) {
					mount_winchester(config.wdFile[i], i);
				}
			}
		}

		// Add the ND-3201/3204 SCSI controller only if a --scsiN target was given.
		// It is opt-in: adding the card unconditionally would change the IOX map and
		// the SINTRAN device scan of every existing machine configuration.
		if (config.scsiEnabled) {
			for (int i = 0; i < SCSI_MAX_UNITS; i++) {
				if (config.scsiFile[i]) {
					mount_scsi(config.scsiFile[i], i);
				}
			}
			// Thumbwheel 0 -> IOX 0144300, ident 0140440, logical device 2202.
			DeviceManager_AddSCSIDevice_WithConfig(0, config.scsiType);
		}
	}

	program_load(config.bootType, config.bootUnit, config.imageFile, config.verbose, (uint16_t)config.textStart, config.overlayDeposit);
	gPC = STARTADDR;

	// An explicit --start / config `start=` overrides the entry that
	// program_load() derived from the image. Needed for e.g. NORD TSS, whose
	// BPUN carries no usable autostart cell, so it must be entered at its real
	// cold-start rather than at address 0.
	if (config.startAddress != 0) {
		STARTADDR = (ushort)config.startAddress;
		gPC = STARTADDR;
	}

	// --opr: preset the operator's-panel switch register (what "TRA OPR" returns).
	// Real ND-100 reads the 16 front-panel data switches here; nd100x has none, so
	// we inject the value. MUST run AFTER program_load()/cpu_reset() (cpu_reset does
	// memset(gReg,0,...), which would otherwise wipe it). NORD TSS reads this at its
	// cold start: 131313 (octal) triggers SINIT -> create the SYSTEM user. Also live-
	// editable at run time via the F12 menu (Control Panel Switches). See
	// docs/TSS-CONTROL-PANEL-SWITCHES.md.
	if (config.oprSet && gReg) {
		gOPR = config.opr;
	}

	/* Direct input/output enabled */
	setcbreak ();
	setvbuf(stdout, NULL, _IONBF, 0);
}




void cleanup(void)
{
	cleanup_machine();
	unsetcbreak ();
}



// =========================================================
// VScreen (virtual terminal switching) state
// =========================================================
static VScreen screens[VSCREEN_MAX];
static int screenCount = 0;
static int activeScreen = 0;
static MenuState menuState;

// =========================================================
// Log VScreen state
// =========================================================
static int logScreenIndex = -1;
static pthread_mutex_t logScreenMutex = PTHREAD_MUTEX_INITIALIZER;

// =========================================================
// Printer output state (managed by PrintJob in ndlib)
// =========================================================
static PrintJob *printJob = NULL;

// =========================================================
// Paper tape writer file output state
//
// Single-threaded: tapeWriterJobNumber increments per flush.
// Tape data is accumulated in the device's own buffer and
// flushed to a .bpun file on timeout or shutdown.
// =========================================================
static int tapeWriterJobNumber = 0;
#define TAPE_WRITER_JOB_TIMEOUT 5  // seconds of silence = end of tape job

// Helper: ensure directory exists (mkdir -p equivalent for one level)
static void ensure_directory(const char *path)
{
    if (ND_MKDIR(path) != 0 && errno != EEXIST) {
        fprintf(stderr, "Failed to create directory: %s\n", path);
    }
}

// Get the effective tape directory
static const char* get_tape_dir(void)
{
    return config.tapeDir ? config.tapeDir : "./tapes";
}


// VScreen output handler - routes output to the right screen buffer
// and only prints to physical terminal if screen is active
static void VScreenOutputHandler(Device *device, char c)
{
    if (!device) return;

    for (int i = 0; i < screenCount; i++) {
        if (screens[i].device == device) {
            VScreen_Write(&screens[i], c);
            if (i == activeScreen && !menu_is_active(&menuState)) {
                charset_emit_host(c);
            }
            return;
        }
    }

    // Fallback: just print (if no menu visible)
    if (!menu_is_active(&menuState)) {
        charset_emit_host(c);
    }
}

// Dedicated printer output handler - routes to PrintJob, also feeds VScreen
static void PrinterOutputHandler(Device *device, char c)
{
    if (!device) return;

    // Feed character to PrintJob manager
    if (printJob) {
        PrintJob_PutChar(printJob, c);
    }

    // Also write to VScreen if printer has one
    for (int i = 0; i < screenCount; i++) {
        if (screens[i].device == device) {
            VScreen_Write(&screens[i], c);
            if (i == activeScreen && !menu_is_active(&menuState)) {
                printf("%c", c);
            }
            return;
        }
    }
}

// Dedicated paper tape writer output handler - accumulates bytes
// Tape buffer is maintained in the device itself (PaperTapeWriterData.tapeBuffer).
// This handler stores to file on timeout or shutdown.
static time_t tapeWriterLastOutputTime = 0;
static bool tapeWriterActive = false;

static void PaperTapeWriterOutputHandler(Device *device, char c)
{
    (void)c;  // Data is stored in device's tapeBuffer already
    if (!device) return;
    tapeWriterLastOutputTime = time(NULL);
    tapeWriterActive = true;
}

// Flush paper tape writer output to a .bpun file
static void flush_tape_writer(Device *ptw)
{
    if (!ptw || !tapeWriterActive) return;

    size_t length = 0;
    const uint8_t *data = PaperTapeWriter_GetTapeData(ptw, &length);
    if (!data || length == 0) return;

    ensure_directory(get_tape_dir());
    tapeWriterJobNumber++;
    char filename[512];
    snprintf(filename, sizeof(filename), "%s/tape-%d.bpun",
             get_tape_dir(), tapeWriterJobNumber);

    FILE *f = fopen(filename, "wb");
    if (f) {
        fwrite(data, 1, length, f);
        fclose(f);
        LOG(LOG_CAT_TAPE, LOG_INFO, "Paper tape job %d saved: %s (%zu bytes)\n",
               tapeWriterJobNumber, filename, length);
    }
    tapeWriterActive = false;
}

// Log output handler - routes Log() messages to the Log VScreen
static void LogScreenHandler(LogCategory cat, LogLevel lvl, const char *msg, void *ctx)
{
    (void)cat; (void)lvl; (void)ctx;
    if (logScreenIndex < 0) return;

    pthread_mutex_lock(&logScreenMutex);
    for (const char *p = msg; *p; p++) {
        VScreen_Write(&screens[logScreenIndex], *p);
    }
    if (logScreenIndex == activeScreen && !menu_is_active(&menuState)) {
        fputs(msg, stdout);
        fflush(stdout);
    }
    pthread_mutex_unlock(&logScreenMutex);
}

// Derive terminal display name using the same algorithm as the glass UI:
// Use logicalDevice if set, otherwise fall back to identCode.
static void make_terminal_name(char *buf, size_t bufsize, Device *dev)
{
    uint16_t num = dev->logicalDevice ? dev->logicalDevice : dev->identCode;
    snprintf(buf, bufsize, "Terminal %d", num);
}

// Find screen index for a device
static int findScreenForDevice(Device *device)
{
    for (int i = 0; i < screenCount; i++) {
        if (screens[i].device == device) return i;
    }
    return -1;
}

// Load paper tape from file
static void load_paper_tape_file(Device *ptr, const char *filename)
{
    if (!ptr || !filename) return;

    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open paper tape file: %s\n", filename);
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        fprintf(stderr, "Paper tape file is empty: %s\n", filename);
        return;
    }

    uint8_t *data = malloc((size_t)size);
    if (!data) {
        fclose(f);
        fprintf(stderr, "Failed to allocate memory for paper tape\n");
        return;
    }

    if (fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        fprintf(stderr, "Failed to read paper tape file\n");
        return;
    }
    fclose(f);

    PaperTape_LoadTape(ptr, data, (size_t)size);
    free(data);
}


int main(int argc, char *argv[])
{

	// Initialize the configuration
    Config_Init(&config);

	 // Parse command line arguments
    if (!Config_ParseCommandLine(&config, argc, argv)) {
        Config_PrintHelp(argv[0]);
        return EXIT_FAILURE;
    }

    // Show help if requested
    if (config.showHelp) {
        Config_PrintHelp(argv[0]);
        return EXIT_SUCCESS;
    }

    // Resolve and (optionally) print the machine configuration.
    // --config/--ini names the INI; otherwise autoload <binaryname>.ini if it
    // exists. With --show-config we validate + print the resolved machine and
    // exit without booting.
    if (config.showConfig || config.writeConfig) {
        MachineConfig mc;
        char mcErr[MC_ERR_LEN];

        char iniName[MC_PATH_LEN];
        const char *iniPath = config.iniFile;
        if (!iniPath) {
            MachineConfig_DefaultIniName(argv[0], iniName, sizeof(iniName));
            FILE *probe = fopen(iniName, "r");
            if (probe) { fclose(probe); iniPath = iniName; }
        }

        if (iniPath) {
            /* An INI fully specifies the controllers, so start from the
             * controller-less baseline before loading. */
            MachineConfig_InitBaseline(&mc);
            if (!MachineConfig_LoadFile(&mc, iniPath, mcErr, sizeof(mcErr))) {
                fprintf(stderr, "Config error: %s\n", mcErr);
                return EXIT_FAILURE;
            }
        } else {
            if (config.showConfig)
                printf("(no INI file found; showing built-in defaults)\n");
            MachineConfig_SetDefaults(&mc);
        }

        if (!MachineConfig_Validate(&mc, mcErr, sizeof(mcErr))) {
            fprintf(stderr, "Config error: %s\n", mcErr);
            return EXIT_FAILURE;
        }

        if (config.writeConfig) {
            if (!MachineConfig_WriteFile(&mc, config.writeConfig, mcErr, sizeof(mcErr))) {
                fprintf(stderr, "Config error: %s\n", mcErr);
                return EXIT_FAILURE;
            }
            printf("Wrote machine config to %s\n", config.writeConfig);
        }
        if (config.showConfig)
            MachineConfig_Print(&mc, stdout);
        return EXIT_SUCCESS;
    }

    // Resolve an explicit --config INI into the machine model. This drives the
    // machine build (see apply_machine_config) and the boot device. Autoloaded
    // INI does not yet drive a normal boot - only an explicit --config does, so
    // existing invocations without --config are unaffected.
    if (config.iniFile) {
        char mcErr[MC_ERR_LEN];
        MachineConfig_InitBaseline(&g_machineConfig);
        if (!MachineConfig_LoadFile(&g_machineConfig, config.iniFile, mcErr, sizeof(mcErr))) {
            fprintf(stderr, "Config error: %s\n", mcErr);
            return EXIT_FAILURE;
        }
        if (!MachineConfig_Validate(&g_machineConfig, mcErr, sizeof(mcErr))) {
            fprintf(stderr, "Config error: %s\n", mcErr);
            return EXIT_FAILURE;
        }
        g_useMachineConfig = true;

        // Translate the INI boot device into the loader's boot type/unit.
        if (g_machineConfig.boot.is_disc) {
            config.bootType = boot_type_for_ctrl(g_machineConfig.boot.type);
            config.bootUnit = g_machineConfig.boot.unit;
        } else {
            config.bootType = g_machineConfig.boot.file_boot_type;
            if (!config.imageFile && g_machineConfig.boot.file[0])
            {
                config.imageFile = strdup(g_machineConfig.boot.file);
                if (!config.imageFile) { fprintf(stderr, "nd100x: out of memory\n"); exit(1); }
            }
        }

        // Apply [runtime] settings as defaults. A CLI flag always wins, so only
        // apply an INI value when the corresponding CLI option was not given
        // (detected via its default-initialized value in config).
        const MC_Runtime *rt = &g_machineConfig.runtime;
        if (!config.telnetEnabled && rt->telnet_port > 0) {
            config.telnetEnabled = true;
            config.telnetPort = rt->telnet_port;
        }
        if (!config.debuggerEnabled && rt->debugger_port > 0) {
            config.debuggerEnabled = true;
            config.debuggerPort = rt->debugger_port;
        }
        if (!config.traceEnabled && rt->trace)
            config.traceEnabled = true;
        if (rt->log_spec[0] && Log_ParseSpec(rt->log_spec) != 0) {
            fprintf(stderr, "nd100x: invalid [runtime] log = %s in the .ini\n", rt->log_spec);
            exit(1);
        }
        if (config.charset == CHARSET_OFF && rt->charset[0] &&
            strcmp(rt->charset, "off") != 0) {
            CharsetVariant cs;
            if (charset_from_name(rt->charset, &cs)) config.charset = cs;
        }
        if (!config.printDir && rt->printdir[0]) {
            config.printDir = strdup(rt->printdir);
            if (!config.printDir) { fprintf(stderr, "nd100x: out of memory\n"); exit(1); }
        }
        if (!config.tapeDir && rt->tapedir[0]) {
            config.tapeDir = strdup(rt->tapedir);
            if (!config.tapeDir) { fprintf(stderr, "nd100x: out of memory\n"); exit(1); }
        }
        if (rt->throttle_mhz > 0) {
            cpu_throttle_set_enabled(true);
            cpu_throttle_set_mhz(rt->throttle_mhz);
        }
        // NORD TSS optional devices: install from the .ini drum=/cdc= keys unless the
        // matching --drum/--cdc flag already supplied a path (CLI wins). Both feed the
        // SAME config.drumFile/cdcFile the CLI sets, so the "install only when non-NULL"
        // gate in initialize() turns them on. Default (neither) leaves both OFF.
        if (!config.drumFile && rt->drum[0]) {
            config.drumFile = strdup(rt->drum);
            if (!config.drumFile) { fprintf(stderr, "nd100x: out of memory\n"); exit(1); }
        }
        if (!config.cdcFile && rt->cdc[0]) {
            config.cdcFile = strdup(rt->cdc);
            if (!config.cdcFile) { fprintf(stderr, "nd100x: out of memory\n"); exit(1); }
        }
        // Installed memory: the .ini memory= key applies only when --memory was NOT given
        // on the CLI (memorySet), so the CLI value wins.
        if (!config.memorySet && rt->memory_mb) config.memoryMB = rt->memory_mb;
        // Interactive shell: CLI --monitor/--shell wins; INI shell= applies if CLI didn't set it
        if (!config.shellEnabled && rt->shell_enabled) {
            config.shellEnabled = true;
        }
        if (!config.nd100Root && rt->nd100_root[0]) {
            config.nd100Root = strdup(rt->nd100_root);
            if (!config.nd100Root) { fprintf(stderr, "nd100x: out of memory\n"); exit(1); }
        }
        if (!config.scriptPath && rt->script[0]) {
            config.scriptPath = strdup(rt->script);
            if (!config.scriptPath) { fprintf(stderr, "nd100x: out of memory\n"); exit(1); }
        }
    }

    if (config.debuggerEnabled) {
        printf("DAP Debugger enabled on port %d\n", config.debuggerPort);
    }

    // Set global variables from config
    DISASM = config.disasmEnabled;
    STARTADDR = config.startAddress;
    // --log after the .ini [runtime] log key, so the CLI wins per category.
    // The spec was already checked when the command line was parsed.
    // --smd-debug / --scsi-debug are aliases for --log=smd:debug / scsi:debug;
    // applied first, so an explicit --log still decides.
    if (config.smdDebug) Log_SetLevel(LOG_CAT_SMD, LOG_DEBUG);
    if (config.scsiDebug) Log_SetLevel(LOG_CAT_SCSI, LOG_DEBUG);
    if (config.logSpec) (void)Log_ParseSpec(config.logSpec);
    // The vendored NCR 5386 port reads its own switch.
    scsi_debug_enabled = Log_IsEnabled(LOG_CAT_SCSI, LOG_DEBUG) ? 1 : 0;

    CPU_TRACE = config.traceEnabled;
    BSD_DEBUG = config.bsdDebug;
    CPU_MAX_INSTR = config.maxInstructions;
    CPU_BREAKPOINT_ENABLED = config.breakpointEnabled;
    CPU_BREAKPOINT_ADDR = (ushort)config.breakpointAddr;
    CPU_RING_DUMP_SIZE = config.ringDumpSize;
    charset_set(config.charset);  // local-console national 7-bit charset (telnet/TCP unaffected)

    initialize();

    // Arm command-line memory watchpoints (--watch). These use the same fast
    // in-CPU watchpoint engine as DAP, but run at full native speed: on a hit
    // with no debugger attached, the CPU halts (message + ring dump).
#ifdef WITH_DEBUGGER
    for (int i = 0; i < config.watchCount; i++) {
        WatchpointType wt = (WatchpointType)config.watch[i].type;
        const char *typeStr = (wt == WATCH_READ) ? "r" : (wt == WATCH_WRITE) ? "w" : "rw";
        int rc;
        if (config.watch[i].isPhysical) {
            rc = phys_watchpoint_add(config.watch[i].address, wt, -1);
        } else {
            rc = watchpoint_add((uint16_t)(config.watch[i].address & 0xFFFF), wt, WATCH_SPACE_ANY, -1);
        }
        if (rc != 0) {
            fprintf(stderr, "Failed to arm watchpoint at %06o\n", config.watch[i].address);
        } else {
            fprintf(stderr, "Watchpoint armed: %s %06o (%s)\n",
                    config.watch[i].isPhysical ? "phys" : "virt",
                    config.watch[i].address, typeStr);
        }
    }
    /* --watch-skip N: ignore the first N watchpoint hits before halting.
     * (declared here; cpu_protos.h is auto-generated so cannot host the extern) */
    extern int watchpoint_skip_hits;
    extern int watchpoint_min_value;
    watchpoint_skip_hits = config.watchSkip;
    watchpoint_min_value = config.watchMinValue;
    if (config.watchSkip > 0)
        fprintf(stderr, "Watchpoint skip: ignoring first %d hit(s)\n", config.watchSkip);
    if (config.watchMinValue > 0)
        fprintf(stderr, "Watchpoint min-value: %06o\n", config.watchMinValue);
#else
    if (config.watchCount > 0) {
        fprintf(stderr, "Warning: --watch requires a debugger-enabled build; ignoring\n");
    }
#endif

    // Add HDLC devices if configured via command line
    for (int i = 0; i < config.hdlcCount; i++) {
        machine_add_hdlc(config.hdlc[i].deviceNum,
                         config.hdlc[i].isServer,
                         config.hdlc[i].address,
                         config.hdlc[i].port);
    }

    // =========================================================
    // Set up terminal devices with VScreen output handlers
    // =========================================================
    Device *terminal = DeviceManager_GetDeviceByAddress(0300);
    if (!terminal) {
        printf("Terminal device not found\n");
        return EXIT_FAILURE;
    }

    // Initialize VScreens for all character devices
    screenCount = 0;

    // Screen 0: Console terminal
    VScreen_Init(&screens[screenCount], "Console", terminal, 80, true);
    Device_SetCharacterOutput(terminal, VScreenOutputHandler);
    screenCount++;

    // Additional terminals - names derived from logicalDevice (same algorithm as glass UI)
    static const uint16_t termAddresses[] = { 0340, 0350, 0360, 0370, 01300, 01310, 01320 };
    Device *extraTerminals[7] = {0};
    char tname[32];

    for (int i = 0; i < 7; i++) {
        extraTerminals[i] = DeviceManager_GetDeviceByAddress(termAddresses[i]);
        if (extraTerminals[i]) {
            make_terminal_name(tname, sizeof(tname), extraTerminals[i]);
            VScreen_Init(&screens[screenCount], tname, extraTerminals[i], 80, true);
            Device_SetCharacterOutput(extraTerminals[i], VScreenOutputHandler);
            screenCount++;
        }
    }

    // Create print job manager based on CLI options
    {
        PjPrinterType ptype = (config.printerType == PRINTER_ESCP) ? PJ_PRINTER_ESCP : PJ_PRINTER_TEXT;
        PjOutputFormat pfmt = (config.printFormat == PRINT_FORMAT_PDF) ? PJ_FORMAT_PDF : PJ_FORMAT_TXT;
        const char *printDir = config.printDir ? config.printDir : "./prints";
        printJob = PrintJob_Create(ptype, pfmt, printDir);
    }

    // Screen: Line Printer (output only, file-based)
    Device *printer = DeviceManager_GetDeviceByAddress(0430);
    if (printer) {
        VScreen_Init(&screens[screenCount], "Line Printer", printer, 132, false);
        Device_SetCharacterOutput(printer, PrinterOutputHandler);
        screenCount++;
    }

    // Screen: Paper Tape Punch (output only, file-based)
    Device *ptw = DeviceManager_GetDeviceByAddress(0410);
    if (ptw) {
        VScreen_Init(&screens[screenCount], "Paper Tape Punch", ptw, 80, false);
        Device_SetCharacterOutput(ptw, PaperTapeWriterOutputHandler);
        screenCount++;
    }

    // Screen: Log messages (output only, no device)
    VScreen_Init(&screens[screenCount], "Log", NULL, 120, false);
    logScreenIndex = screenCount;
    screenCount++;
    Log_SetSink(LogScreenHandler, NULL);

    // Load paper tape file if specified on command line
    Device *ptr = DeviceManager_GetDeviceByAddress(0400);
    if (ptr && config.tapeFile) {
        load_paper_tape_file(ptr, config.tapeFile);
    }

    // Start telnet server if enabled (after all VScreen setup so origOutput is set)
#if !defined(PLATFORM_WASM) && !defined(__EMSCRIPTEN__)
    if (config.telnetEnabled) {
        TelnetServerConfig tc = {
            .port = config.telnetPort,
            .maxConnections = 8,
            .transport = TRANSPORT_TELNET
        };
        telnetServer = TelnetServer_Create(&tc);
        if (telnetServer) {
            // Register terminals 5-11 (not console)
            for (int i = 0; i < 7; i++) {
                Device *dev = extraTerminals[i];
                if (!dev) continue;

                // Use VScreen name (derived from logicalDevice) for telnet display
                int si = findScreenForDevice(dev);
                TelnetTerminalInfo info = {
                    .device = dev,
                    .identCode = dev->identCode,
                    .ioAddress = dev->startAddress,
                    .name = (si >= 0) ? screens[si].name : dev->memoryName,
                    .inputFunc = Terminal_QueueKeyCode,
                    .origOutput = dev->charCallbacks.outputFunc,
                    .carrierFunc = set_terminal_carrier,
                };
                TelnetServer_RegisterTerminal(telnetServer, &info);

                // Replace output handler with telnet-aware version
                Device_SetCharacterOutput(dev, telnet_output_handler);
            }

            // Set initial localActive state: terminals 8-11 (indices 3-6) start released for telnet
            for (int r = 3; r < 7; r++) {
                if (!extraTerminals[r]) continue;
                int si = findScreenForDevice(extraTerminals[r]);
                if (si >= 0) {
                    screens[si].localActive = false;
                }
            }

            // Sync locallyActive flags to telnet server (match by device pointer)
            for (int si = 0; si < screenCount; si++) {
                if (screens[si].device) {
                    TelnetServer_SetDeviceLocallyActive(telnetServer, screens[si].device, screens[si].localActive);
                }
            }

            TelnetServer_Start(telnetServer);
        }
    }
#endif

    if (config.debuggerEnabled) {
        set_cpu_run_mode(CPU_PAUSED);
    }

    // Initialize the menu state machine
    menu_init(&menuState, screens, screenCount, &activeScreen);

    // Run the interactive shell if enabled
    if (config.shellEnabled) {
        // The machine setup put the tty in cbreak/raw mode (VMIN=0,VTIME=0) for
        // the emulated terminal - in that mode read() returns instantly with no
        // bytes, so the shell's fgets/readline would see immediate EOF and quit.
        // The shell is a line-oriented REPL, so run it in cooked/canonical mode
        // (echo + line editing), the same way the nd500x monitor does. Restore
        // cbreak afterwards is unnecessary because we exit right after.
        unsetcbreak();
        setvbuf(stdout, NULL, _IOLBF, 0);
        printf("\n=== ND-100 Interactive Shell Mode ===\n");
        int shell_result = nd100x_shell_run(config.nd100Root, config.scriptPath);
        if (shell_result == SHELL_RESULT_RUN) {
            // RUN-PROGRAM loaded an image and armed the CPU (gPC=STARTADDR,
            // CPU_RUNNING). Restore cbreak/raw + unbuffered stdout for the
            // emulated terminal, then fall through to the normal machine run
            // loop below so the program executes with the full terminal I/O,
            // menu and telnet plumbing. When it halts the emulator exits, the
            // same as a normal --image boot.
            setcbreak();
            setvbuf(stdout, NULL, _IONBF, 0);
            // (fall through - do NOT return)
        } else {
            if (shell_result < 0) {
                fprintf(stderr, "Shell exited with error\n");
            }
            // EXIT / EOF: leave the terminal in cooked mode on the way out.
            return EXIT_SUCCESS;
        }
    }

    // Run the machine until it stops
    CPURunMode runMode = get_cpu_run_mode();

    while (runMode != CPU_SHUTDOWN)
    {
        runMode = get_cpu_run_mode();
        machine_run(5000);

        runMode = get_cpu_run_mode();

        // Check for print job timeout
        if (printJob) {
            PrintJob_CheckTimeout(printJob);
        }

        // Check for paper tape writer timeout (flush to file)
        if (tapeWriterActive && ptw) {
            time_t now = time(NULL);
            if (tapeWriterLastOutputTime > 0 &&
                (now - tapeWriterLastOutputTime) >= TAPE_WRITER_JOB_TIMEOUT) {
                flush_tape_writer(ptw);
            }
        }

        // Handle keyboard input
        if (runMode != CPU_SHUTDOWN)
        {
            KeyEvent key = read_key_event();

            // If menu is active, route keys to menu and check timeouts
            if (menu_is_active(&menuState)) {
#if !defined(PLATFORM_WASM) && !defined(__EMSCRIPTEN__)
                menu_tick(&menuState, telnetServer);
                if (key.type != KEY_NONE) {
                    menu_process_key(&menuState, &key, telnetServer);
                }
#else
                menu_tick(&menuState, NULL);
                if (key.type != KEY_NONE) {
                    menu_process_key(&menuState, &key, NULL);
                }
#endif
            } else if (key.type == KEY_ALT_DIGIT) {
                int altScreen = key.ch - '0';
                if (altScreen > 0 && altScreen <= screenCount) {
                    int target = altScreen - 1;
#if !defined(PLATFORM_WASM) && !defined(__EMSCRIPTEN__)
                    // Block switching to telnet-connected terminal
                    if (telnetServer &&
                        TelnetServer_IsDeviceConnected(telnetServer, screens[target].device)) {
                        // Terminal in use by telnet - ignore
                    } else
#endif
                    {
                        // If not locally active, re-activate it
#if !defined(PLATFORM_WASM) && !defined(__EMSCRIPTEN__)
                        if (telnetServer && screens[target].isInputCapable &&
                            !screens[target].localActive) {
                            screens[target].localActive = true;
                            TelnetServer_SetDeviceLocallyActive(telnetServer, screens[target].device, true);
                            set_terminal_carrier(screens[target].device, false);
                        }
#endif
                        activeScreen = target;
                        VScreen_Redraw(&screens[activeScreen]);
                    }
                }
            } else if (key.type == KEY_F12) {
                // F12 - enter non-blocking menu
#if !defined(PLATFORM_WASM) && !defined(__EMSCRIPTEN__)
                menu_enter(&menuState, telnetServer);
#else
                menu_enter(&menuState, NULL);
#endif
            } else if (key.type != KEY_NONE) {
                // Process regular characters - forward every raw byte in the
                // event (covers KEY_CHAR, KEY_ESCAPE, KEY_UNKNOWN multi-byte
                // escape sequences the emulated terminal may want to consume).
                // When a national charset is active, collapse UTF-8/Latin-1
                // accented keystrokes (e.g. 'ae') to their single 7-bit code
                // ('{') before queueing. CHARSET_OFF copies verbatim.
                char mappedSeq[32];
                int mappedLen = charset_translate_input(key.seq, key.seqLen,
                                                        mappedSeq, sizeof(mappedSeq));
                for (int i = 0; i < mappedLen; i++) {
                    char ch = mappedSeq[i];

#if !defined(PLATFORM_WASM) && !defined(__EMSCRIPTEN__) && !defined(PLATFORM_RISCV)
                    // --pipe control framing (0xFF <cmd> \n) is intercepted here as a command
                    // (e.g. floppy hot-swap), NOT forwarded to the emulated terminal as keystrokes.
                    if (config.pipeMode && pipe_control_feed(ch)) continue;
#endif

                    // ND doesnt like \n
                    if (ch == '\n') {
                        ch = '\r';
                    }

                    if ((runMode == CPU_PAUSED)||(runMode == CPU_BREAKPOINT)) {
                        debugger_kbd_input(ch);
                    } else {
                        // Route keyboard input to the active screen's device (if input capable and locally active)
                        if (screens[activeScreen].isInputCapable &&
                            screens[activeScreen].localActive &&
                            screens[activeScreen].device) {
                            Terminal_QueueKeyCode(screens[activeScreen].device, ch);
                        }
                    }
                }
            }
        }
    }

    // Flush any pending output before shutdown
    if (printJob) {
        PrintJob_Destroy(printJob);
        printJob = NULL;
    }
    if (ptw) flush_tape_writer(ptw);

    // Stop telnet server before VScreen cleanup
#if !defined(PLATFORM_WASM) && !defined(__EMSCRIPTEN__)
    if (telnetServer) {
        TelnetServer_Stop(telnetServer);
        TelnetServer_Destroy(telnetServer);
        telnetServer = NULL;
    }
#endif

    // Cleanup VScreens
    for (int i = 0; i < screenCount; i++) {
        VScreen_Destroy(&screens[i]);
    }

#ifdef WITH_DEBUGGER
    // Stop the debugger server gracefully (if its still running)
    stop_debugger_thread();
#endif

    if (DISASM)
        disasm_dump();

    dump_stats();
    cleanup();

    // exit with A register value from WAIT instruction
    return(gCpuExitCode);
}
