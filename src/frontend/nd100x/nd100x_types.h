
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

#ifndef ND100X_TYPES_H
#define ND100X_TYPES_H

#include <stdbool.h>
#include <stdint.h>


#include "../../machine/machine_types.h"
#include "../ndlib/ndlib_types.h"
#include "charset.h"

// Printer emulation type (--printer= option)
typedef enum {
    PRINTER_TEXT,      // Simple line printer (plain ASCII)
    PRINTER_ESCP,      // Epson ESC/P interpreter
    PRINTER_LASER      // Color laser (future, not yet implemented)
} PrinterType_t;

// Printer output format (--printformat= option)
typedef enum {
    PRINT_FORMAT_TXT,  // Plain text output (.txt)
    PRINT_FORMAT_PDF   // PDF output (.pdf)
} PrintFormat_t;

// Configuration structure
typedef struct {
    BOOT_TYPE bootType;
    int bootUnit;        // Boot unit on the boot controller (--boot=smd1, --boot=scsi2; default 0)
    char *iniFile;       // --config/--ini: machine INI file (NULL = autoload <binaryname>.ini)
    bool showConfig;     // --show-config: resolve+print machine config and exit
    char *writeConfig;   // --write-config=FILE: serialize resolved config to INI and exit
    char *imageFile;
    uint32_t startAddress;
    bool disasmEnabled;
    bool verbose;
    bool showHelp;
    bool debuggerEnabled;
    int debuggerPort;
    bool smdDebug;
    bool bsdDebug;
    bool traceEnabled;
    uint64_t maxInstructions;
    bool breakpointEnabled;
    uint32_t breakpointAddr;
    int ringDumpSize;
    bool textStartSet;
    uint32_t textStart;
    bool overlayDeposit;
    char *printDir;      // Output directory for print jobs (default: ./prints/)
    char *tapeDir;       // Output directory for punched tape (default: ./tapes/)
    char *tapeFile;      // Input file for paper tape reader
    char *smdFile[4];    // SMD disk image files (--smd0 through --smd3)
    char *wdFile[2];     // Winchester disk image files (--wd0, --wd1). Two units:
                         // ND-11.015.01 sec 3.1, unit is one control-word bit.
    bool wdEnabled;      // add the Winchester card at all (opt-in: it shares
                         // IOX 500-507 with the CDC system disc)
    // SCSI targets (--scsi0 through --scsi6), indexed by SCSI ID.
    // ID 7 is the ND-3201/3204 controller itself and is never a target.
    bool scsiEnabled;            // true if any --scsiN was given
    bool scsiDebug;              // --scsi-debug
    char *scsiFile[SCSI_MAX_UNITS];
    SCSIUnitType scsiType[SCSI_MAX_UNITS];
    bool telnetEnabled;  // --telnet flag
    int telnetPort;      // Default: 9000
    // CLI memory watchpoints (--watch). Run at full native speed (no DAP needed).
    #define MAX_CLI_WATCHPOINTS 32
    int watchCount;
    struct {
        bool isPhysical;     // true = physical address (--watch=phys:ADDR)
        uint32_t address;
        int type;            // WatchpointType: 1=read, 2=write, 3=readwrite
    } watch[MAX_CLI_WATCHPOINTS];
    int watchSkip;           // --watch-skip N: ignore first N watchpoint hits before halting
    int watchMinValue;       // --watch-min-value V: WRITE watchpoint triggers only if value >= V
    bool pipeMode;       // --pipe: read keyboard from stdin (automation over pipes). Desktop only.
    int mmsType;         // --mms1/--mms2/--mms=N: 1=MMS1 (NORD-10/4 page tables), 2=MMS2 (16 PT, default)
    char *drumFile;      // --drum: NORD TSS swapping-drum image file (@ IOX 540)
    char *cdcFile;       // --cdc:  NORD TSS CDC cartridge system-disc image (@ IOX 500-507)
    // --cputype=TYPE: selected CPU model name (e.g. "ND120CX"). NULL = keep the
    // built-in default. Applied to CurrentCPUType in nd100x.c BEFORE machine_init
    // (Setup_Instructions reads CurrentCPUType to gate VERSN / ND-110 opcodes).
    char *cpuType;
    // --memory=MB / .ini memory=MB: installed main memory in megabytes (1..16,
    // default 4). Applied to ND_Memsize (= MB * 524288 words) BEFORE machine_init.
    // memorySet records whether the CLI flag was given, so the .ini value only
    // applies when the flag was not (CLI wins, mirroring drum/cdc/telnet).
    int  memoryMB;
    bool memorySet;
    // --fpp=32|48 / .ini [machine] fpp=: which floating point unit is installed
    // (the 32-bit single-precision FPP was a factory option, independent of the
    // CPU model). Applied to CurrentFPPType in nd100x.c; fppSet records whether
    // the CLI flag was given, so the .ini value only applies when it was not
    // (CLI wins, mirroring --memory).
    int  fppBits;
    bool fppSet;
    // --rtc=ticks|wall / .ini [machine] rtc=: RTC time base. false = one clock
    // pulse per 10550 executed instructions (default, deterministic); true = one
    // pulse per 20 ms of host wall-clock time. Applied via RTC_SetWallClockMode
    // in nd100x.c; rtcSet records whether the CLI flag was given, so the .ini
    // value only applies when it was not (CLI wins, mirroring --fpp).
    bool rtcWall;
    bool rtcSet;
    // Operator's-panel switch register preset (--opr). On real ND-100 this is the
    // 16 front-panel data switches read by "TRA OPR"; nd100x has no physical panel,
    // so this presets gReg->reg_OPR. NORD TSS reads it at cold start: 131313 (octal)
    // creates the SYSTEM user (SINIT), 111111 = verbose disc-error diagnostics, and
    // on NORD-10 the low 15 bits select a memory word shown in the LEV4 display.
    bool oprSet;         // --opr given: preset the panel switch register
    uint16_t opr;        // --opr=OCTAL value (see docs/TSS-CONTROL-PANEL-SWITCHES.md)
    PrinterType_t printerType;   // --printer= option (default: PRINTER_TEXT)
    PrintFormat_t printFormat;    // --printformat= option (default: PRINT_FORMAT_TXT)
    CharsetVariant charset;       // --charset= option: local-console national 7-bit charset (default: CHARSET_OFF)
    // HDLC configuration (up to 4 devices, thumbwheels 1-4)
    #define MAX_HDLC_DEVICES 4
    int hdlcCount;                           // Number of configured HDLC devices
    struct {
        int deviceNum;       // Thumbwheel number (1-4)
        bool isServer;       // true = server (listen), false = client (connect)
        char *address;       // IP/hostname for client mode (NULL for server)
        int port;            // TCP port
    } hdlc[MAX_HDLC_DEVICES];
    // Interactive shell mode (--monitor / --shell)
    bool shellEnabled;       // --monitor or --shell: enable interactive shell mode
    char *nd100Root;         // --nd100-root: directory containing BPUN/PROG files (default: current dir)
    char *scriptPath;        // --script: path to script file with shell commands
    char *logSpec;           // --log=SPEC: per-category log levels, applied after the .ini [runtime] log key
    bool traceNd110;         // --trace-nd110[=FILE]: ND-110-only opcode trace
    char *traceNd110File;    // its output file; NULL = stdout
    long ringAtPf;           // --ring-at-pf=N: dump the instruction ring at the N'th page fault; -1 = not given
    long ringAtClpt;         // --ring-at-clpt=N: same at the N'th CLPT; -1 = not given
} Config_t;

#endif // CONFIG_H