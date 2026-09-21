/*
 * config.c - Command-line option parsing and help text for the native frontend.
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
#include <string.h>
#include <getopt.h>

#include "nd100x_types.h"
#include "nd100x_protos.h"
#include "../../machine/machine_types.h"
#include "../../devices/hdlc/hdlc_constants.h"
#include "../../cpu/cpu_protos.h"
#include "../../ndlib/log.h"
#include "nd100x_version.h" /* generated into the build dir by cmake/git_stamp.cmake */

// Long options
// clang-format off
static struct option long_options[] = {
    {"boot",       required_argument, 0, 'b'},
    {"image",      required_argument, 0, 'i'},
    {"start",      required_argument, 0, 's'},
    {"disasm",     no_argument,       0, 'a'},
    {"verbose",    no_argument,       0, 'v'},
    {"help",       no_argument,       0, 'h'},
    {"version",    no_argument,       0, 'V'},
    {"debugger",   no_argument,       0, 'd'},
    {"port",       required_argument, 0, 'p'},
    {"smd-debug",  no_argument,       0, 'S'},
    {"bsd-debug",  no_argument,       0, 'G'},
    {"trace",      no_argument,       0, 't'},
    {"max-instr",  required_argument, 0, 'n'},
    {"breakpoint", required_argument, 0, 'B'},
    {"watch",      required_argument, 0, 'W'},
    {"text-start", required_argument, 0, 'T'},
    {"printdir",   required_argument, 0, 'P'},
    {"tapedir",    required_argument, 0, 'D'},
    {"tape",       required_argument, 0, 'e'},
    {"telnet",     optional_argument, 0, 'N'},
    {"printer",    required_argument, 0, 'r'},
    {"printformat",required_argument, 0, 'f'},
    {"charset",    required_argument, 0, 'L'},
    {"hdlc",       required_argument, 0, 'H'},
    {"throttle",   optional_argument, 0, 'Z'},
    {"ring-dump",  optional_argument, 0, 'R'},
    {"overlay-deposit", no_argument, 0, 'O'},
    {"watch-skip", required_argument, 0, 0x130},
    {"watch-min-value", required_argument, 0, 0x131},
    {"wd0",        required_argument, 0, 0x170}, // --wd0=FILE : Winchester unit 0 image (IOX 500-507)
    {"wd1",        required_argument, 0, 0x171}, // --wd1=FILE : Winchester unit 1 image
    {"smd0",       required_argument, 0, 0x100},
    {"smd1",       required_argument, 0, 0x101},
    {"smd2",       required_argument, 0, 0x102},
    {"smd3",       required_argument, 0, 0x103},
    {"scsi0",      required_argument, 0, 0x110},
    {"scsi1",      required_argument, 0, 0x111},
    {"scsi2",      required_argument, 0, 0x112},
    {"scsi3",      required_argument, 0, 0x113},
    {"scsi4",      required_argument, 0, 0x114},
    {"scsi5",      required_argument, 0, 0x115},
    {"scsi6",      required_argument, 0, 0x116},
    {"scsi-debug", no_argument,       0, 0x117},
    {"config",     required_argument, 0, 0x120},
    {"ini",        required_argument, 0, 0x120},
    {"show-config",no_argument,       0, 0x121},
    {"write-config",required_argument,0, 0x122},
    {"pipe",       no_argument,       0, 0x140}, // --pipe: keyboard from stdin (automation)
    {"mms",        required_argument, 0, 0x106}, // --mms=1|2 : MMU paging-system type
    {"mms1",       no_argument,       0, 0x107}, // --mms1 : NORD-10 paging (MMS1)
    {"mms2",       no_argument,       0, 0x108}, // --mms2 : 16-page-table MMS (default)
    {"drum",       required_argument, 0, 0x104}, // --drum=FILE : NORD TSS swapping-drum image @ IOX 540
    {"cdc",        required_argument, 0, 0x105}, // --cdc=FILE  : NORD TSS CDC cartridge system-disc @ IOX 500
    {"opr",        required_argument, 0, 0x109}, // --opr=OCTAL : preset operator's-panel switch register (TRA OPR)
    {"cputype",    required_argument, 0, 0x150}, // --cputype=TYPE : select the emulated CPU model (see Config_PrintHelp)
    {"memory",     required_argument, 0, 0x151}, // --memory=MB : installed main memory in megabytes (1..16, default 4)
    {"fpp",        required_argument, 0, 0x152}, // --fpp=32|48 : installed floating point unit width (default 48)
    {"rtc",        required_argument, 0, 0x153}, // --rtc=ticks|wall : RTC time base (default ticks)
    {"monitor",    no_argument,       0, 0x160}, // --monitor : enable interactive shell mode
    {"shell",      no_argument,       0, 0x160}, // --shell : alias for --monitor
    {"nd100-root", required_argument, 0, 0x161}, // --nd100-root=PATH : directory for BPUN/PROG files (default: current dir)
    {"script",     required_argument, 0, 0x162}, // --script=FILE : load shell commands from script file
    {"log",        required_argument, 0, 0x154}, // --log=SPEC : per-category log levels, e.g. smd:debug,*:warn
    {"trace-nd110", optional_argument, 0, 0x155}, // --trace-nd110[=FILE] : trace ND-110-only opcodes
    {"ring-at-pf", required_argument, 0, 0x156}, // --ring-at-pf=N : instruction ring dump at the N'th page fault
    {"ring-at-clpt", required_argument, 0, 0x157}, // --ring-at-clpt=N : instruction ring dump at the N'th CLPT
    {0, 0, 0, 0}
};
// clang-format on

void config_init(Config *config)
{
    if (!config)
    {
        return;
    }

    config->bootType = BOOT_NONE;
    config->bootUnit = 0;
    config->iniFile = NULL;
    config->showConfig = false;
    config->writeConfig = NULL;
    config->imageFile = NULL;
    config->startAddress = 0;
    config->disasmEnabled = false;
    config->verbose = false;
    config->showHelp = false;
    config->debuggerEnabled = false;
    config->debuggerPort = 4711;
    config->smdDebug = false;
    config->bsdDebug = false;
    config->traceEnabled = false;
    config->maxInstructions = 0;
    config->breakpointEnabled = false;
    config->breakpointAddr = 0;
    config->textStartSet = false;
    config->textStart = 0;
    config->overlayDeposit = false;
    config->printDir = NULL;
    config->tapeDir = NULL;
    config->tapeFile = NULL;
    for (int i = 0; i < 4; i++)
    {
        config->smdFile[i] = NULL;
    }
    for (int i = 0; i < 2; i++)
    {
        config->wdFile[i] = NULL;
    }
    config->wdEnabled = false;
    config->scsiEnabled = false;
    config->scsiDebug = false;
    for (int i = 0; i < SCSI_MAX_UNITS; i++)
    {
        config->scsiFile[i] = NULL;
        config->scsiType[i] = SCSI_UNIT_NONE;
    }
    config->telnetEnabled = false;
    config->pipeMode = false; // --pipe (pipe automation); desktop only
    config->mmsType =
        2; // --mms: default MMS2 (16 page tables); SINTRAN/existing machines unchanged
    config->drumFile =
        NULL; // --drum: NORD TSS swapping-drum image (@ IOX 540); no drum image by default
    config->cdcFile =
        NULL; // --cdc:  NORD TSS CDC cartridge system-disc image (@ IOX 500); none by default
    config->cpuType = NULL; // --cputype: NULL keeps the built-in default CPU model (no override)
    config->memoryMB = 4;   // --memory: installed main memory in MB (default 4 MB = 2097152 words)
    config->memorySet =
        false; // whether --memory was given on the CLI (CLI wins over the .ini memory= key)
    config->fppBits =
        48; // --fpp: installed FPP width; 48 = standard unit, 32 = optional single precision
    config->fppSet = false; // whether --fpp was given on the CLI (CLI wins over the .ini fpp= key)
    config->rtcWall =
        false; // --rtc: RTC time base; false = instruction ticks (default), true = wall-clock 20 ms
    config->rtcSet = false; // whether --rtc was given on the CLI (CLI wins over the .ini rtc= key)
    config->oprSet =
        false; // --opr: operator's-panel switch register preset (TRA OPR); unset -> power-on 0
    config->opr = 0;
    config->telnetPort = 9000;
    config->watchCount = 0;
    config->printerType = PRINTER_TEXT;
    config->printFormat = PRINT_FORMAT_TXT;
    config->charset = CHARSET_OFF;
    // HDLC configuration
    config->hdlcCount = 0;
    for (int i = 0; i < MAX_HDLC_DEVICES; i++)
    {
        config->hdlc[i].deviceNum = 0;
        config->hdlc[i].isServer = false;
        config->hdlc[i].address = NULL;
        config->hdlc[i].port = HDLC_DEFAULT_PORT;
    }
    // Interactive shell mode
    config->shellEnabled = false;
    config->nd100Root = NULL;
    config->scriptPath = NULL;
    config->logSpec = NULL;
    config->traceNd110 = false;
    config->traceNd110File = NULL;
    config->ringAtPf = -1;
    config->ringAtClpt = -1;
}

/* Parse a --boot argument into bootType + bootUnit.
 * Accepts the bare names (bp, bpun, tape, aout, prog, floppy, cdc, smd, wd,
 * scsi) plus an optional unit digit on the disk controllers: smd0-smd3,
 * wd0-wd1 and scsi0-scsi6. A bare "smd"/"wd"/"scsi" means unit 0. Prints its own error message
 * and returns false on an unknown name or an out-of-range unit.
 *
 * bpun vs tape - two REAL, different loaders, not aliases:
 *   bpun = the ND-100 ROM loader. ASCII preamble is metadata only; the
 *          payload is the framed binary block after '!'. Tape consumed.
 *   tape = the front-panel octal tape load (NORD-1 style). ASCII words are
 *          deposited, start at '!', and the remainder stays mounted on the
 *          paper-tape reader for the started program to read (raw, unframed).
 * The byte stream after '!' cannot be told apart mechanically, so guessing
 * is unsafe - the operator says which loader to model, as on the hardware. */
static bool parse_boot_spec(Config *config, const char *boot_str)
{
    if (!boot_str || !config)
    {
        return false;
    }

    config->bootUnit = 0;

    /* Boot types without a unit number. */
    // clang-format off
    static const struct { const char *name; BOOT_TYPE type; } simple[] = {
        { "bp",     BOOT_BP     },
        { "bpun",   BOOT_BPUN   },
        { "aout",   BOOT_AOUT   },
        { "floppy", BOOT_FLOPPY },
        { "cdc",    BOOT_CDC    },
        { "tape",   BOOT_TAPE   },
    };
    // clang-format on
    for (size_t i = 0; i < sizeof(simple) / sizeof(simple[0]); i++)
    {
        if (strcmp(simple[i].name, boot_str) == 0)
        {
            config->bootType = simple[i].type;
            return true;
        }
    }

    if (strncmp("smd", boot_str, 3) == 0)
    {
        const char *u = boot_str + 3;
        if (*u == '\0')
        {
            config->bootType = BOOT_SMD;
            return true;
        }
        if (u[0] >= '0' && u[0] <= '3' && u[1] == '\0')
        {
            config->bootType = BOOT_SMD;
            config->bootUnit = u[0] - '0';
            return true;
        }
        fprintf(stderr, "Invalid SMD boot unit in '%s' (use smd or smd0-smd3)\n", boot_str);
        return false;
    }

    /* Winchester (ST506 / 8 inch). Two units only - the control word carries
     * the unit in a single bit (ND-11.015.01 sec 3.1 / 3.4). Booting also
     * implies the card is fitted. */
    if (strncmp("wd", boot_str, 2) == 0)
    {
        const char *u = boot_str + 2;
        if (*u == '\0')
        {
            config->bootType = BOOT_WINCHESTER;
            config->wdEnabled = true;
            return true;
        }
        if ((u[0] == '0' || u[0] == '1') && u[1] == '\0')
        {
            config->bootType = BOOT_WINCHESTER;
            config->bootUnit = u[0] - '0';
            config->wdEnabled = true;
            return true;
        }
        fprintf(stderr, "Invalid Winchester boot unit in '%s' (use wd, wd0 or wd1)\n", boot_str);
        return false;
    }

    if (strncmp("scsi", boot_str, 4) == 0)
    {
        const char *u = boot_str + 4;
        if (*u == '\0')
        {
            config->bootType = BOOT_SCSI;
            return true;
        }
        if (u[0] >= '0' && u[0] <= '6' && u[1] == '\0')
        {
            config->bootType = BOOT_SCSI;
            config->bootUnit = u[0] - '0';
            return true;
        }
        fprintf(
            stderr,
            "Invalid SCSI boot unit in '%s' (use scsi or scsi0-scsi6; ID 7 is the controller)\n",
            boot_str);
        return false;
    }

    fprintf(stderr, "Invalid boot type: %s\n", boot_str);
    return false;
}

// Parse HDLC config: "N:PORT" (server) or "N:HOST:PORT" (client)
// N = device number 1-4
static bool parse_hdlc_config(Config *config, const char *hdlc_str)
{
    if (!hdlc_str || !config)
    {
        return false;
    }
    if (config->hdlcCount >= MAX_HDLC_DEVICES)
    {
        fprintf(stderr, "Too many HDLC devices (max %d)\n", MAX_HDLC_DEVICES);
        return false;
    }

    char *str = strdup(hdlc_str);
    if (!str)
    {
        return false;
    }

    // First token: device number
    char *first_colon = strchr(str, ':');
    if (!first_colon)
    {
        fprintf(stderr, "HDLC config must start with device number: N:PORT or N:HOST:PORT\n");
        free(str);
        return false;
    }

    *first_colon = '\0';
    char *endptr;
    int dev_num = (int)strtol(str, &endptr, 10);
    if (*endptr != '\0' || dev_num < 1 || dev_num > 4)
    {
        fprintf(stderr, "HDLC device number must be 1-4, got: %s\n", str);
        free(str);
        return false;
    }

    // Check for duplicate device number
    for (int i = 0; i < config->hdlcCount; i++)
    {
        if (config->hdlc[i].deviceNum == dev_num)
        {
            fprintf(stderr, "HDLC device %d already configured\n", dev_num);
            free(str);
            return false;
        }
    }

    char *rest = first_colon + 1;
    char *second_colon = strchr(rest, ':');

    int idx = config->hdlcCount;
    config->hdlc[idx].deviceNum = dev_num;

    if (second_colon)
    {
        // Client mode: "HOST:PORT"
        *second_colon = '\0';
        char *port_str = second_colon + 1;

        config->hdlc[idx].address = strdup(rest);
        if (!config->hdlc[idx].address)
        {
            free(str);
            return false;
        }

        config->hdlc[idx].port = (int)strtol(port_str, &endptr, 10);
        if (*endptr != '\0' || config->hdlc[idx].port <= 0 || config->hdlc[idx].port > 65535)
        {
            free(config->hdlc[idx].address);
            config->hdlc[idx].address = NULL;
            free(str);
            return false;
        }
        config->hdlc[idx].isServer = false;
    }
    else
    {
        // Server mode: just PORT
        config->hdlc[idx].port = (int)strtol(rest, &endptr, 10);
        if (*endptr != '\0' || config->hdlc[idx].port <= 0 || config->hdlc[idx].port > 65535)
        {
            free(str);
            return false;
        }
        config->hdlc[idx].address = NULL;
        config->hdlc[idx].isServer = true;
    }

    config->hdlcCount++;
    free(str);
    return true;
}

// Parse watchpoint config: "[phys:]ADDR[:r|w|rw]"
// ADDR accepts octal (leading 0), hex (0x), or decimal, matching -B.
static bool parse_watch_config(Config *config, const char *watch_str)
{
    if (!watch_str || !config)
    {
        return false;
    }
    if (config->watchCount >= MAX_CLI_WATCHPOINTS)
    {
        fprintf(stderr, "Too many watchpoints (max %d)\n", MAX_CLI_WATCHPOINTS);
        return false;
    }

    char *str = strdup(watch_str);
    if (!str)
    {
        return false;
    }

    char *p = str;
    bool is_phys = false;
    if (strncmp(p, "phys:", 5) == 0)
    {
        is_phys = true;
        p += 5;
    }

    int type = 3; // default READWRITE
    char *colon = strrchr(p, ':');
    if (colon)
    {
        char *t = colon + 1;
        if (strcmp(t, "r") == 0)
        {
            type = 1;
        }
        else if (strcmp(t, "w") == 0)
        {
            type = 2;
        }
        else if (strcmp(t, "rw") == 0 || strcmp(t, "wr") == 0)
        {
            type = 3;
        }
        else
        {
            fprintf(stderr, "Invalid watch access type: %s (use r, w, or rw)\n", t);
            free(str);
            return false;
        }
        *colon = '\0';
    }

    char *endptr;
    uint32_t addr = (uint32_t)strtoul(p, &endptr, 0);
    if (p == endptr || *endptr != '\0')
    {
        fprintf(stderr, "Invalid watch address: %s\n", p);
        free(str);
        return false;
    }

    int idx = config->watchCount;
    config->watch[idx].isPhysical = is_phys;
    config->watch[idx].address = addr;
    config->watch[idx].type = type;
    config->watchCount++;
    free(str);
    return true;
}

bool config_parse_command_line(Config *config, int argc, char *argv[])
{
    int option_index = 0;
    int c;
    char *endptr;

    while ((c = getopt_long(argc, argv, "b:i:s:avVhdp:StGn:B:W:T:P:D:e:N::r:f:L:H:Z::R::O",
                            long_options, &option_index)) != -1)
    {
        switch (c)
        {
        case 'b':
            if (!parse_boot_spec(config, optarg))
            {
                return false;
            }
            break;

        case 'i':
            config->imageFile = strdup(optarg);
            if (!config->imageFile)
            {
                fprintf(stderr, "Failed to allocate memory for image file\n");
                return false;
            }
            break;

        case 's':
            config->startAddress = strtoul(optarg, &endptr, 0);
            if (*endptr != '\0')
            {
                fprintf(stderr, "Invalid start address: %s\n", optarg);
                return false;
            }
            break;

        case 'a':
            config->disasmEnabled = true;
            break;

        case 'd':
            config->debuggerEnabled = true;
            break;

        case 'p':
            config->debuggerPort = (int)strtol(optarg, &endptr, 0);
            if (*endptr != '\0' || config->debuggerPort <= 0 || config->debuggerPort > 65535)
            {
                fprintf(stderr, "Invalid port number: %s\n", optarg);
                return false;
            }
            break;

        case 'v':
            config->verbose = true;
            break;

        case 'P':
            config->printDir = strdup(optarg);
            if (!config->printDir)
            {
                fprintf(stderr, "Out of memory\n");
                return false;
            }
            break;

        case 'D':
            config->tapeDir = strdup(optarg);
            if (!config->tapeDir)
            {
                fprintf(stderr, "Out of memory\n");
                return false;
            }
            break;

        case 'e':
            config->tapeFile = strdup(optarg);
            if (!config->tapeFile)
            {
                fprintf(stderr, "Out of memory\n");
                return false;
            }
            break;

        case 'N':
            config->telnetEnabled = true;
            if (optarg)
            {
                char *port_end;
                long port = strtol(optarg, &port_end, 10);
                if (*port_end != '\0' || port <= 0 || port > 65535)
                {
                    fprintf(stderr, "Invalid telnet port: %s\n", optarg);
                    return false;
                }
                config->telnetPort = (int)port;
            }
            break;

        case 'r':
            if (strcmp(optarg, "text") == 0)
            {
                config->printerType = PRINTER_TEXT;
            }
            else if (strcmp(optarg, "escp") == 0)
            {
                config->printerType = PRINTER_ESCP;
            }
            else if (strcmp(optarg, "laser") == 0)
            {
                fprintf(stderr, "Laser printer emulation is not yet implemented\n");
                return false;
            }
            else
            {
                fprintf(stderr, "Invalid printer type: %s (use text, escp, or laser)\n", optarg);
                return false;
            }
            break;

        case 'f':
            if (strcmp(optarg, "txt") == 0)
            {
                config->printFormat = PRINT_FORMAT_TXT;
            }
            else if (strcmp(optarg, "pdf") == 0)
            {
                config->printFormat = PRINT_FORMAT_PDF;
            }
            else
            {
                fprintf(stderr, "Invalid print format: %s (use txt or pdf)\n", optarg);
                return false;
            }
            break;

        case 'L':
        {
            CharsetVariant cs;
            if (!charset_from_name(optarg, &cs))
            {
                fprintf(stderr, "Invalid charset: %s (use off, norwegian, swedish, german)\n",
                        optarg);
                return false;
            }
            config->charset = cs;
            break;
        }

        case 'h':
            config->showHelp = true;
            return true;

        case 'V':
            printf("nd100x %s (git %s, built %s)\n", ND100X_VERSION, ND100X_GIT_HASH,
                   ND100X_BUILD_TIME);
            exit(0);

        case 'H':
            if (!parse_hdlc_config(config, optarg))
            {
                fprintf(stderr, "Invalid HDLC configuration: %s\n", optarg);
                return false;
            }
            break;

        case 'Z':
            cpu_throttle_set_enabled(true);
            if (optarg)
            {
                double mhz = strtod(optarg, &endptr);
                if (endptr == optarg || *endptr != '\0' || mhz <= 0)
                {
                    fprintf(stderr, "Invalid throttle speed in MHz: %s\n", optarg);
                    return false;
                }
                cpu_throttle_set_mhz(mhz);
            }
            break;

        case 'R':
        {
            int n = 50; // default
            if (optarg)
            {
                n = (int)strtol(optarg, &endptr, 0);
                if (*endptr != '\0' || n < 1 || n > 65536)
                {
                    fprintf(stderr, "Invalid ring-dump size (1-65536): %s\n", optarg);
                    return false;
                }
            }
            config->ringDumpSize = n;
            break;
        }

        case 'S':
            config->smdDebug = true;
            break;

        case 'G':
            config->bsdDebug = true;
            break;

        case 't':
            config->traceEnabled = true;
            break;

        case 'n':
            config->maxInstructions = strtoull(optarg, &endptr, 0);
            if (*endptr != '\0')
            {
                fprintf(stderr, "Invalid max instruction count: %s\n", optarg);
                return false;
            }
            break;

        case 'B':
            config->breakpointEnabled = true;
            config->breakpointAddr = strtoul(optarg, &endptr, 0);
            if (*endptr != '\0')
            {
                fprintf(stderr, "Invalid breakpoint address: %s\n", optarg);
                return false;
            }
            break;

        case 'W':
            if (!parse_watch_config(config, optarg))
            {
                return false;
            }
            break;

        case 0x130: /* --watch-skip N */
            config->watchSkip = (int)strtol(optarg, &endptr, 0);
            if (*endptr != '\0' || config->watchSkip < 0)
            {
                fprintf(stderr, "Invalid --watch-skip value: %s\n", optarg);
                return false;
            }
            break;

        case 0x131: /* --watch-min-value V */
            config->watchMinValue = (int)strtoul(optarg, &endptr, 0);
            if (*endptr != '\0')
            {
                fprintf(stderr, "Invalid --watch-min-value: %s\n", optarg);
                return false;
            }
            break;

        case 'T':
            config->textStartSet = true;
            config->textStart = strtoul(optarg, &endptr, 0);
            if (*endptr != '\0')
            {
                fprintf(stderr, "Invalid text start address: %s\n", optarg);
                return false;
            }
            break;

        case 'O':
            config->overlayDeposit = true;
            break;

        /* Winchester (ST506/8 inch) images. Adding the card is opt-in: it
             * answers IOX 500-507, the same block as the CDC system disc, so a
             * machine has one card or the other. */
        case 0x170:
        case 0x171:
        {
            int unit = c - 0x170;
            config->wdFile[unit] = strdup(optarg);
            if (!config->wdFile[unit])
            {
                fprintf(stderr, "Out of memory parsing --wd%d\n", unit);
                return false;
            }
            config->wdEnabled = true;
            break;
        }
        case 0x100:
        case 0x101:
        case 0x102:
        case 0x103:
        {
            int unit = c - 0x100;
            config->smdFile[unit] = strdup(optarg);
            if (!config->smdFile[unit])
            {
                fprintf(stderr, "Failed to allocate memory for SMD%d file\n", unit);
                return false;
            }
            break;
        }

        case 0x110:
        case 0x111:
        case 0x112:
        case 0x113:
        case 0x114:
        case 0x115:
        case 0x116:
        {
            /* --scsiN=[TYPE:]FILE  e.g. --scsi0=hdd:/path/disk.img
                 * TYPE is optional and defaults to hdd. The type prefix is only
                 * honoured when the text before the first ':' is a known type
                 * name, so a bare path (including a Windows "C:\..." path) is
                 * still treated as a filename. */
            int unit = c - 0x110;
            SCSIUnitType type = SCSI_UNIT_HDD;
            const char *file = optarg;

            const char *colon = strchr(optarg, ':');
            const char *slash = strchr(optarg, '/');
            /* A colon only introduces a type when it comes before any '/',
                 * so "/tmp/a:b.img" stays a filename. If the text there is not
                 * a known type it is a typo, not a path - say so rather than
                 * silently trying to open a file named "hdX:...". */
            if (colon && colon != optarg && (!slash || colon < slash))
            {
                size_t len = (size_t)(colon - optarg);
                char prefix[16];
                if (len >= sizeof(prefix))
                {
                    fprintf(stderr, "Error: --scsi%d has an unknown type prefix in '%s'\n", unit,
                            optarg);
                    return false;
                }
                memcpy(prefix, optarg, len);
                prefix[len] = '\0';
                SCSIUnitType parsed = scsi_parse_unit_type(prefix);
                if (parsed == SCSI_UNIT_NONE)
                {
                    fprintf(stderr,
                            "Error: --scsi%d unknown type '%s' "
                            "(expected hdd, tape, cdrom or floppy)\n",
                            unit, prefix);
                    return false;
                }
                type = parsed;
                file = colon + 1;
            }

            if (*file == '\0')
            {
                fprintf(stderr, "Error: --scsi%d needs a file (got '%s')\n", unit, optarg);
                return false;
            }

            config->scsiFile[unit] = strdup(file);
            if (!config->scsiFile[unit])
            {
                fprintf(stderr, "Failed to allocate memory for SCSI%d file\n", unit);
                return false;
            }
            config->scsiType[unit] = type;
            config->scsiEnabled = true;
            break;
        }

        case 0x117:
            config->scsiDebug = true;
            break;

        case 0x140: /* --pipe : read keyboard from stdin (pipe automation). Desktop only. */
            config->pipeMode = true;
            break;

        case 0x106:
        { /* --mms=1|2 : select MMU paging-system type (1=MMS1, 2=MMS2 default) */
            char *ep;
            long m = strtol(optarg, &ep, 0);
            if (*ep != '\0' || (m != 1 && m != 2))
            {
                fprintf(stderr, "Invalid --mms value: %s (use 1 or 2)\n", optarg);
                exit(1);
            }
            config->mmsType = (int)m;
            break;
        }
        case 0x107:
            config->mmsType = 1;
            break; /* --mms1 : NORD-10 / Paging-System-I (NORD TSS) */
        case 0x108:
            config->mmsType = 2;
            break; /* --mms2 : 16-page-table MMS (default) */

        case 0x104:
        { /* --drum=FILE : NORD TSS swapping-drum image */
            config->drumFile = strdup(optarg);
            if (!config->drumFile)
            {
                fprintf(stderr, "Failed to allocate memory for drum file\n");
                return false;
            }
            break;
        }

        case 0x105:
        { /* --cdc=FILE : NORD TSS CDC system-disc image @ IOX 500 */
            config->cdcFile = strdup(optarg);
            if (!config->cdcFile)
            {
                fprintf(stderr, "Failed to allocate memory for cdc file\n");
                return false;
            }
            break;
        }

        case 0x150:
        { /* --cputype=TYPE : select the emulated CPU model (ND100, ND110CX, ND120CX, ...).
                             * Stored verbatim; the string->CpuType mapping and validation happen in the
                             * frontend (nd100x.c) BEFORE machine_init, because Setup_Instructions() reads
                             * CurrentCPUType to decide which opcodes (VERSN, ND-110 specials) to install. */
            config->cpuType = strdup(optarg);
            if (!config->cpuType)
            {
                fprintf(stderr, "Failed to allocate memory for cputype\n");
                return false;
            }
            break;
        }

        case 0x151:
        { /* --memory=MB : installed main memory in megabytes, integer 1..16 (default 4).
                             * No silent clamp / no silent default on bad input - reject and fail, same as
                             * --cputype / --opr. Applied to ND_Memsize (= MB * 524288 words) in nd100x.c
                             * BEFORE machine_init, ahead of the ECC latch / MMS allocations. */
            char *mem_end;
            long mb = strtol(optarg, &mem_end, 10);
            if (*mem_end != '\0' || mb < 1 || mb > 16)
            {
                fprintf(stderr,
                        "Invalid --memory value '%s' (expect an integer 1..16, in megabytes; "
                        "e.g. --memory=4)\n",
                        optarg);
                return false;
            }
            config->memoryMB = (int)mb;
            config->memorySet = true;
            break;
        }

        case 0x152:
        { /* --fpp=32|48 : installed floating point unit width. The 32-bit
                             * single-precision FPP was a factory option independent of the CPU
                             * model. Applied to CurrentFPPType in nd100x.c; no silent default
                             * on bad input - reject and fail, same as --memory. */
            char *fpp_end;
            long fb = strtol(optarg, &fpp_end, 10);
            if (*fpp_end != '\0' || (fb != 32 && fb != 48))
            {
                fprintf(stderr,
                        "Invalid --fpp value '%s' (expect 32 or 48, "
                        "e.g. --fpp=32)\n",
                        optarg);
                return false;
            }
            config->fppBits = (int)fb;
            config->fppSet = true;
            break;
        }

        case 0x153:
        { /* --rtc=ticks|wall : RTC time base. ticks = one clock pulse per
                             * 10550 executed instructions (default, deterministic); wall = one
                             * pulse per 20 ms of host wall-clock time (real-time 50 Hz clock
                             * regardless of emulation speed). No silent default on bad input -
                             * reject and fail, same as --fpp. */
            if (strcasecmp(optarg, "ticks") == 0)
            {
                config->rtcWall = false;
            }
            else if (strcasecmp(optarg, "wall") == 0)
            {
                config->rtcWall = true;
            }
            else
            {
                fprintf(stderr,
                        "Invalid --rtc value '%s' (expect ticks or wall, "
                        "e.g. --rtc=wall)\n",
                        optarg);
                return false;
            }
            config->rtcSet = true;
            break;
        }

        case 0x109:
        { /* --opr=OCTAL : preset the operator's-panel switch register (TRA OPR).
                             * ND panel switches are always read/quoted in OCTAL, so parse base 8
                             * (NOT base 0). NORD TSS cold-start uses 131313 (create SYSTEM user),
                             * 111111 (verbose disc-error diagnostics); range is a 16-bit word. */
            char *opr_end;
            long v = strtol(optarg, &opr_end, 8);
            if (*opr_end != '\0' || v < 0 || v > 0177777L)
            {
                fprintf(stderr,
                        "Invalid --opr value '%s' (expect octal 0..177777, "
                        "e.g. --opr=131313)\n",
                        optarg);
                return false;
            }
            config->opr = (uint16_t)v;
            config->oprSet = true;
            break;
        }

        case 0x120: /* --config / --ini */
            config->iniFile = strdup(optarg);
            if (!config->iniFile)
            {
                fprintf(stderr, "Failed to allocate memory for config file path\n");
                return false;
            }
            break;

        case 0x121: /* --show-config */
            config->showConfig = true;
            break;

        case 0x122: /* --write-config=FILE */
            config->writeConfig = strdup(optarg);
            if (!config->writeConfig)
            {
                fprintf(stderr, "Failed to allocate memory for write-config path\n");
                return false;
            }
            break;

        case 0x160: /* --monitor / --shell : enable interactive shell mode */
            config->shellEnabled = true;
            break;

        case 0x161: /* --nd100-root=PATH : directory for BPUN/PROG files */
            config->nd100Root = strdup(optarg);
            if (!config->nd100Root)
            {
                fprintf(stderr, "Failed to allocate memory for nd100-root path\n");
                return false;
            }
            break;

        case 0x154: /* --log=SPEC : per-category log levels. Checked here so a
                         * typo fails at once; applied again after the .ini is read
                         * so the CLI value wins (nd100x.c). */
            if (log_parse_spec(optarg) != 0)
            {
                fprintf(stderr,
                        "Invalid --log value '%s' (expect category:level pairs, "
                        "e.g. --log=smd:debug,*:warn)\n",
                        optarg);
                return false;
            }
            config->logSpec = strdup(optarg);
            if (!config->logSpec)
            {
                fprintf(stderr, "Out of memory\n");
                return false;
            }
            break;

        case 0x155: /* --trace-nd110[=FILE] : ND-110-only opcode trace, to stdout or FILE */
            config->traceNd110 = true;
            if (optarg)
            {
                config->traceNd110File = strdup(optarg);
                if (!config->traceNd110File)
                {
                    fprintf(stderr, "Out of memory\n");
                    return false;
                }
            }
            break;

        case 0x156: /* --ring-at-pf=N   */
        case 0x157:
        { /* --ring-at-clpt=N */
            char *ep;
            long n = strtol(optarg, &ep, 10);
            if (ep == optarg || *ep != '\0' || n < 1)
            {
                fprintf(stderr, "Invalid --%s value '%s' (expect a count >= 1)\n",
                        c == 0x156 ? "ring-at-pf" : "ring-at-clpt", optarg);
                return false;
            }
            if (c == 0x156)
            {
                config->ringAtPf = n;
            }
            else
            {
                config->ringAtClpt = n;
            }
            break;
        }

        case 0x162: /* --script=FILE : load shell commands from script file */
            config->scriptPath = strdup(optarg);
            if (!config->scriptPath)
            {
                fprintf(stderr, "Failed to allocate memory for script path\n");
                return false;
            }
            break;

        case '?':
            return false;

        default:
            fprintf(stderr, "Unknown option: %c\n", c);
            return false;
        }
    }

    // Check required arguments
    // Note: Shell mode doesn't require boot configuration since it loads programs explicitly
    if ((!config->showHelp && !config->showConfig && !config->writeConfig && !config->iniFile &&
         !config->debuggerEnabled && !config->shellEnabled))
    {
        if (config->bootType == BOOT_NONE)
        {
            config->bootType = BOOT_SMD;

            fprintf(stderr, "Boot type must be specified\n");
            return false;
        }
        // --image is only for aout, bpun, bp, and floppy boot types
        if (config->imageFile && config->bootType == BOOT_SMD)
        {
            fprintf(stderr,
                    "Error: --image is not used with --boot=smd. Use --smd0..--smd3 instead.\n");
            return false;
        }
        if (config->imageFile && config->bootType == BOOT_SCSI)
        {
            fprintf(stderr,
                    "Error: --image is not used with --boot=scsi. Use --scsi0..--scsi6 instead.\n");
            return false;
        }
        if (config->bootType == BOOT_SCSI)
        {
            int u = config->bootUnit;
            if (!config->scsiFile[u])
            {
                fprintf(stderr,
                        "Error: --boot=scsi%d needs a boot image on SCSI ID %d. Use "
                        "--scsi%d=hdd:FILE.\n",
                        u, u, u);
                return false;
            }
            if (config->scsiType[u] != SCSI_UNIT_HDD)
            {
                fprintf(stderr,
                        "Error: --boot=scsi%d needs a 'hdd' target on SCSI ID %d (it is '%s').\n",
                        u, u, scsi_unit_type_name(config->scsiType[u]));
                return false;
            }
        }
        if (!config->imageFile)
        {
            if (config->bootType == BOOT_FLOPPY)
            {
                config->imageFile = strdup("FLOPPY.IMG");
                if (!config->imageFile)
                {
                    fprintf(stderr, "Out of memory\n");
                    return false;
                }
            }
            else
                // SMD, Winchester and SCSI take their images from --smdN / --wdN /
                // --scsiN, not --image.
                if (config->bootType != BOOT_SMD && config->bootType != BOOT_SCSI &&
                    config->bootType != BOOT_WINCHESTER)
                {
                    fprintf(stderr, "Image file must be specified\n");
                    return false;
                }
        }
    }

    if (config->verbose)
    {
        printf("Configuration:\n");
        if (config->iniFile && config->bootType == BOOT_NONE)
        {
            // Boot device comes from the INI, resolved after this summary prints.
            printf("  Boot type: (from config file %s)\n", config->iniFile);
        }
        else if (config->bootType == BOOT_SMD || config->bootType == BOOT_SCSI)
        {
            printf("  Boot type: %s unit %d\n", g_boot_type_str[config->bootType],
                   config->bootUnit);
        }
        else
        {
            printf("  Boot type: %s\n", g_boot_type_str[config->bootType]);
        }
        printf("  Image file: %s\n", config->imageFile);
        for (int i = 0; i < 4; i++)
        {
            if (config->smdFile[i])
            {
                printf("  SMD%d image: %s\n", i, config->smdFile[i]);
            }
        }
        printf("  Start address: 0x%x\n", config->startAddress);
        printf("  Disassembly: %s\n", config->disasmEnabled ? "enabled" : "disabled");
        for (int i = 0; i < config->hdlcCount; i++)
        {
            if (config->hdlc[i].isServer)
            {
                printf("  HDLC %d: Server mode on port %d\n", config->hdlc[i].deviceNum,
                       config->hdlc[i].port);
            }
            else
            {
                printf("  HDLC %d: Client mode to %s:%d\n", config->hdlc[i].deviceNum,
                       config->hdlc[i].address, config->hdlc[i].port);
            }
        }
    }


    return true;
}

void config_print_help(const char *prog_name)
{
    printf("nd100x %s (git %s, built %s)\n", ND100X_VERSION, ND100X_GIT_HASH, ND100X_BUILD_TIME);
    printf("Usage: %s [options]\n\n", prog_name);
    printf("Options:\n");
    printf("  -b,      --boot=TYPE    Boot type (bp, bpun, tape, aout, prog, floppy,\n");
    printf("                          smd[0-3], wd[0-1], scsi[0-6], cdc)\n");
    printf("                          smd/wd/scsi take an optional boot unit digit,\n");
    printf("                          e.g. --boot=smd1 or --boot=scsi2 (default: unit 0)\n");
    printf("                          bpun = ND-100 ROM loader: the octal-ASCII preamble is\n");
    printf("                          metadata; a FRAMED binary block after '!' is loaded\n");
    printf("                          ([addr][count][words][checksum][action]).\n");
    printf("                          tape = front-panel octal tape load: the ASCII words\n");
    printf("                          ARE deposited into memory, execution starts at the\n");
    printf("                          '!' address, and the tape REMAINS in the paper-tape\n");
    printf("                          reader (0400) so the started program reads the raw\n");
    printf("                          binary remainder itself - what a NORD TSS CDBIN\n");
    printf("                          distribution tape needs. The two formats are NOT\n");
    printf("                          interchangeable after the '!'.\n");
    printf("                          wd = Winchester MASS STORAGE LOAD: 1K words from\n");
    printf("                          mass storage address 0 into core 0 (needs --wd0/--wd1).\n");
    printf("                          cdc = LOAD button on the TSS cartridge disc: sector 0\n");
    printf("                          into core 0, start at 0 (needs --cdc=FILE).\n");
    printf("  -i,      --image=FILE   Image file to load (bpun, tape, aout, prog, floppy;\n");
    printf("                          --boot=cdc also demands one but never reads it)\n");
    printf("           --wd0=FILE     Winchester unit 0 disk image (default: WD0.IMG)\n");
    printf("           --wd1=FILE     Winchester unit 1 disk image (default: WD1.IMG)\n");
    printf("                          Winchester answers IOX 500-507 - same block as the\n");
    printf("                          CDC system disc, so only one of the two can be used.\n");
    printf("                          Also settable via the .ini [controller.wd.0] section\n");
    printf("                          (disk0/disk1 keys; boot with [boot] device = wd.0.0)\n");
    printf("           --smd0=FILE    SMD unit 0 disk image (default: SMD0.IMG)\n");
    printf("           --smd1=FILE    SMD unit 1 disk image (default: SMD1.IMG)\n");
    printf("           --smd2=FILE    SMD unit 2 disk image (default: SMD2.IMG)\n");
    printf("           --smd3=FILE    SMD unit 3 disk image (default: SMD3.IMG)\n");
    printf(
        "           --scsi0=[TYPE:]FILE  SCSI ID 0 target image (adds the ND-3201 controller)\n");
    printf("           --scsi1=[TYPE:]FILE  SCSI ID 1 target image\n");
    printf("           --scsi2=[TYPE:]FILE  SCSI ID 2 target image\n");
    printf("           --scsi3=[TYPE:]FILE  SCSI ID 3 target image\n");
    printf("           --scsi4=[TYPE:]FILE  SCSI ID 4 target image\n");
    printf("           --scsi5=[TYPE:]FILE  SCSI ID 5 target image\n");
    printf("           --scsi6=[TYPE:]FILE  SCSI ID 6 target image\n");
    printf("                          TYPE is one of:\n");
    printf("                            hdd     Micropolis 1375-ND hard disk (default)\n");
    printf("                            tape    streamer tape           (not implemented yet)\n");
    printf("                            cdrom   CD-ROM                  (not implemented yet)\n");
    printf("                            floppy  SCSI floppy             (not implemented yet)\n");
    printf(
        "                          SCSI ID 7 is the controller itself and cannot be a target.\n");
    printf("                          Example: --scsi0=hdd:SCSI-K.image\n");
    printf("  -s,      --start=ADDR   Start address (default: 0)\n");
    printf("  -a,      --disasm       Enable disassembly output\n");
    printf("  -d,      --debugger     Enable DAP debugger\n");
    printf("  -p PORT, --port=PORT    Set debugger port (default: 4711)\n");
    printf("  -S,      --smd-debug    Enable SMD disk controller debug log (stderr)\n");
    printf("           --scsi-debug   Enable SCSI disk controller debug log (stderr)\n");
    printf("           --drum=FILE    NORD TSS swapping-drum image @ IOX 540\n");
    printf("           --cdc=FILE     NORD TSS CDC cartridge system-disc image @ IOX 500\n");
    printf("           --opr=OCTAL    Preset operator's-panel switches (TRA OPR); e.g. "
           "--opr=131313\n");
    printf("                          (NORD TSS: 131313=create SYSTEM user, 111111=disc-error "
           "diag)\n");
    printf("           --cputype=TYPE Select the emulated CPU model (case-insensitive). Valid "
           "values:\n");
    printf("                            ND1, ND4, ND10,\n");
    printf("                            ND100, ND100CE, ND100CX,\n");
    printf("                            ND110, ND110CE, ND110CX, ND110PCX,\n");
    printf("                            ND120CX\n");
    printf("                          (default: built-in; with no flag the machine reports as\n");
    printf("                          ND-100/CX). VERSN and the ND-110-only instructions are\n");
    printf("                          enabled only for the ND110* models.\n");
    printf("                          e.g. --cputype=ND120CX\n");
    printf("           --memory=MB    Installed main memory in megabytes: integer 1..16\n");
    printf("                          (default: 4). 1 MB = 524288 words (4 MB = 2097152).\n");
    printf("                          Also settable via the .ini 'memory = MB' key.\n");
    printf("           --fpp=BITS     Installed floating point unit width: 32 or 48\n");
    printf("                          (default: 48, the standard FPP). 32 selects the optional\n");
    printf("                          single-precision FPP (FAD/FSB/FMU/FDV on the A,D pair;\n");
    printf("                          NLZ/DNZ leave T untouched).\n");
    printf("                          Also settable via the .ini '[machine] fpp = BITS' key.\n");
    printf("           --rtc=MODE     RTC time base: ticks or wall (default: ticks).\n");
    printf("                          ticks = one clock pulse per 10550 executed instructions\n");
    printf(
        "                          (deterministic, follows emulation speed). wall = one pulse\n");
    printf("                          per 20 ms of host time (real-time 50 Hz clock).\n");
    printf("                          Also settable via the .ini '[machine] rtc = MODE' key.\n");
    printf(
        "           --log=SPEC     Log levels per category, e.g. smd:debug,hdlc:trace,*:warn.\n");
    printf("                          Levels: error warn info debug trace (default: info).\n");
    printf(
        "                          Categories: general cpu mms mmsmap trap pkswitch device smd\n");
    printf(
        "                          floppy wd scsi cdc drum hdlc rtc term panel tape printer net\n");
    printf("                          dap machine loader config; * or all = every category.\n");
    printf("                          mms, mmsmap, trap and pkswitch print only in builds made\n");
    printf(
        "                          with -DND100X_HOT_TRACE=ON (the default for Debug builds).\n");
    printf("                          Log lines go to the Log\n");
    printf("                          screen (Alt+N) or stderr, never to a guest terminal.\n");
    printf("                          Also settable via the .ini '[runtime] log = SPEC' key.\n");
    printf(
        "           --trace-nd110[=FILE]  Trace every ND-110-only opcode, page fault and CLPT\n");
    printf("                          to stdout or FILE ([runtime] trace_nd110 = on|FILE).\n");
    printf("           --ring-at-pf=N   Dump the instruction ring at the N'th page fault\n");
    printf("                          ([runtime] ring_at_pf = N).\n");
    printf("           --ring-at-clpt=N Dump the instruction ring at the N'th CLPT\n");
    printf("                          ([runtime] ring_at_clpt = N).\n");
    printf("           --bsd-debug    Track BSD kernel-stack high-water (KSTKHW, stderr)\n");
    printf("  -t,      --trace        Enable CPU execution trace to stderr\n");
    printf("  -n N,    --max-instr=N  Stop after N instructions\n");
    printf("  -B ADDR, --breakpoint=ADDR  Stop at address (octal/hex/decimal)\n");
    printf("  -W SPEC, --watch=SPEC   Stop on memory access at full speed (repeatable, max %d)\n",
           MAX_CLI_WATCHPOINTS);
    printf("                          SPEC = [phys:]ADDR[:r|w|rw]  (default rw, virtual)\n");
    printf("  -T ADDR, --text-start=ADDR  Text segment load address for a.out (default: 0)\n");
    printf("  -v,      --verbose      Enable verbose output\n");
    printf("  -P DIR,  --printdir=DIR  Printer output directory (default: ./prints/)\n");
    printf("  -D DIR,  --tapedir=DIR   Paper tape output directory (default: ./tapes/)\n");
    printf("  -e FILE, --tape=FILE     Paper tape reader input file (.bpun)\n");
    printf("  -N[PORT],--telnet[=PORT] Enable telnet server (default port: 9000)\n");
    printf("  -r TYPE, --printer=TYPE  Printer emulation: text (default), escp, laser\n");
    printf("  -f FMT,  --printformat=FMT  Output format: txt (default), pdf\n");
    printf("  -L CS,   --charset=CS    Local-console national 7-bit charset (telnet/TCP "
           "unaffected):\n");
    printf("                          off (default), norwegian, swedish, german\n");
    printf("  -H CFG,  --hdlc=CFG     Enable HDLC controller (up to 4x)\n");
    printf("                          Server: --hdlc=N:PORT  (N=1-4)\n");
    printf("                          Client: --hdlc=N:HOST:PORT\n");
    printf("  -O,      --overlay-deposit Deposit data_click at phys word 1 for kernel boot-info\n");
    printf("  -R[N],   --ring-dump[=N]  Dump last N instructions on halt/crash (default: 50, max: "
           "65536)\n");
    printf("  -Z[MHZ], --throttle[=MHZ] Throttle CPU to real-time speed (default: 0.5275 MHz)\n");
    printf("           --config=FILE  Machine configuration INI file\n");
    printf("           --ini=FILE     Alias for --config\n");
    printf("                          (default: autoload <binaryname>.ini in the current dir)\n");
    printf("           --show-config  Resolve+validate the machine config, print it, and exit\n");
    printf("           --write-config=FILE  Write the resolved machine config to an INI file and "
           "exit\n");
    printf("           --monitor, --shell   Enable interactive shell mode for loading BPUN/PROG "
           "files\n");
    printf("           --nd100-root=PATH    Directory containing BPUN/PROG files (default: current "
           "dir)\n");
    printf("           --script=FILE        Load shell commands from a script file\n");
    printf("  -h,      --help         Show this help message\n");
    printf("  -V,      --version      Show version, git hash and build time, then exit\n\n");
    printf("Examples:\n");
    printf("  %s --boot=bpun --image=test.bpun\n", prog_name);
    printf("  %s --boot=floppy --image=disk.img --start=0x1000 --disasm\n", prog_name);
    printf("  %s --debugger\n", prog_name);
    printf("  %s --hdlc=1:%d                  # HDLC 1 server on port %d\n", prog_name,
           HDLC_DEFAULT_PORT, HDLC_DEFAULT_PORT);
    printf("  %s --hdlc=1:192.168.1.10:%d     # HDLC 1 client\n", prog_name, HDLC_DEFAULT_PORT);
    printf("  %s --boot=smd --smd0=myboot.img --smd1=data.img\n", prog_name);
    printf("  %s --boot=smd1                  # Boot from SMD unit 1\n", prog_name);
    printf("  %s --boot=scsi0 --scsi0=hdd:SCSI-K.image  # Boot from SCSI ID 0\n", prog_name);
    printf("  %s --hdlc=1:5000 --hdlc=2:5001  # Two HDLC devices\n", prog_name);

    /*
     * Machine identity is configured through environment variables (nd100x has no
     * .ini/config-file layer). Listed here because --help is where users look.
     * Implemented in cpu_versn_set_identity_from_env(), src/cpu/cpu_instr.c.
     */
    printf("\nEnvironment variables (CPU identity):\n");
    printf(
        "  ND100X_CPUTYPE=NAME             ND100|ND100CE|ND100CX|ND110|ND110CE|ND110CX|ND110PCX\n");
    printf(
        "  ND100X_CPU_NUMBER=N             SYSNO   -> SINTRAN banner \"CPU NUMBER\" (or none)\n");
    printf("  ND100X_SYSTEM_TYPE=N            HWINFO(2) -> banner \"CPU TYPE\": "
           "100/102/500/502/5561,\n");
    printf("                                  any other number is accepted too (or none)\n");
    printf("  ND100X_LEGAL_USERS=N            NLEGU, 0-254, or none to keep SINTRAN's own value\n");
    printf("  ND100X_INSTALLATION_NUMBER=HEX  Raw 16-byte back-wiring PROM, 32 hex digits\n");
    printf("  ND100X_MICROCODE_VERSION=V      Revision letter (L), ND octal (014/0o14/14B),\n");
    printf("                                  hex (0x0C) or decimal (12)\n");
    printf("  ND100X_PRINT_VERSION=N          PCB artwork version, 12 bits\n");
    printf("  Numbers: 0x..=hex, 0o../..B/leading 0=octal, otherwise decimal.\n");
    printf("  The identity PROM is only read by SINTRAN on an ND-110/ND-120 CPU.\n");
}
