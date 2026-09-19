/*
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * machine_config.h - machine configuration model, controller registry, and
 * INI parser/validator.
 *
 * This is the single in-memory model of "what machine to build": the CPU type,
 * the configured controllers (each a type + thumbwheel + disks/settings), the
 * boot device, and default runtime options. The native CLI, the WASM config
 * window, and the gateway all read from this one model instead of re-deriving
 * the hardware layout.
 *
 * The controller registry (ControllerDescriptor table) is the ONLY place that
 * knows a controller's IOX address map. Adding a controller type is one new row.
 *
 * Ident codes are deliberately NOT here - they are computed inside each device
 * from the thumbwheel and are never surfaced to config.
 *
 * See docs/MACHINE-CONFIG-DESIGN.md for the full design.
 */

#ifndef MACHINE_CONFIG_H
#define MACHINE_CONFIG_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "machine_types.h"               /* BOOT_TYPE */
#include "../devices/scsi/device_scsi.h" /* SCSIUnitType, SCSI_MAX_UNITS */

#define MC_MAX_CONTROLLERS 16
#define MC_MAX_DISK_SLOTS  SCSI_MAX_UNITS /* 7 - the largest slot count (SCSI) */
#define MC_MAX_TERMINALS   16
#define MC_PATH_LEN        256
#define MC_ERR_LEN         512

/* Configurable controller types. Core devices (CPU, RTC, console) are always
 * present and are not represented here. */
typedef enum
{
    CTRL_NONE = 0,
    CTRL_FLOPPY,
    CTRL_SMD,
    CTRL_WINCHESTER,
    CTRL_SCSI,
    CTRL_HDLC
} CtrlType;

/* One disk image slot on a disc controller. media uses the SCSI unit-type
 * vocabulary (hdd/cdrom/tape/floppy); for SMD and floppy controllers media is
 * SCSI_UNIT_HDD by convention (single fixed media) and is not shown. */
typedef struct
{
    bool present;
    SCSIUnitType media;
    char image[MC_PATH_LEN];
} MC_DiskSlot;

typedef struct
{
    CtrlType type;
    int wheel;
    bool enabled;
    MC_DiskSlot disks[MC_MAX_DISK_SLOTS];

    /* HDLC-only settings (ignored for other types). */
    bool hdlc_is_server; /* true = server (listen), false = client */
    char hdlc_host[MC_PATH_LEN];
    int hdlc_port;
} MC_Controller;

/* Boot device. For a disc boot, (type,wheel,unit) name the controller slot.
 * For a file boot (bpun/aout), file_boot_type + file are used instead. */
typedef struct
{
    bool is_disc;
    CtrlType type;
    int wheel;
    int unit;
    BOOT_TYPE file_boot_type; /* BOOT_BPUN / BOOT_AOUT when !is_disc */
    char file[MC_PATH_LEN];
} MC_BootSpec;

/* Non-hardware runtime options. A CLI flag overrides the INI value per run. */
typedef struct
{
    int telnet_port;     /* 0 = off */
    double throttle_mhz; /* 0 = off */
    char charset[16];    /* "off"|"norwegian"|"swedish"|"german" */
    char printdir[MC_PATH_LEN];
    char tapedir[MC_PATH_LEN];
    int debugger_port; /* 0 = off */
    bool trace;
    /* NORD TSS optional devices, OFF by default. A non-empty path installs the
     * device (same gate as the --drum / --cdc CLI options, which override these). */
    char drum[MC_PATH_LEN]; /* swapping-drum image  (@ IOX 540); "" = no drum */
    char cdc[MC_PATH_LEN];  /* CDC system-disc image (@ IOX 500); "" = no CDC */
    int memory_mb;          /* installed main memory in MB (1..16); 0 = unset (use default/CLI) */
    /* Interactive shell options (CLI flag overrides INI value) */
    bool shell_enabled;            /* enable interactive shell mode */
    char nd100_root[MC_PATH_LEN];  /* directory for BPUN/PROG files; "" = current dir */
    char script[MC_PATH_LEN];      /* script file to load in shell; "" = none */
    char log_spec[128];            /* log levels, e.g. "smd:debug,*:warn"; "" = defaults */
    char trace_nd110[MC_PATH_LEN]; /* "" = off, "on" = stdout, else the output file */
    long ring_at_pf;               /* instruction ring dump at the N'th page fault; 0 = off */
    long ring_at_clpt;             /* same at the N'th CLPT; 0 = off */
} MC_Runtime;

/* The ND-500 at the other end of the bus interface.
 *
 * NOT a controller row. The ND-500 is not an IOX card on the ND-100 bus - it
 * sits behind the 3022 bus interface, and that interface is what becomes a
 * registry row when it exists. This describes the MACHINE at the other end:
 * how much memory it has, what kernel it runs, what discs it can see.
 *
 * Every path here is resolved by whoever builds the machine, not by the parser.
 * In the browser they are catalog names rather than filesystem paths, and the
 * parser has no business knowing the difference.
 */
#define MC_ND500_MAX_DISKS 16 /* NDIX's own MAXDISK (kernel machine/fevar.h) */

typedef struct
{
    bool enabled;             /* no [nd500] section at all = false */
    int memory_mb;            /* 0 = the emulator's default (16 MB) */
    char kernel[MC_PATH_LEN]; /* NDIX a.out; "" = taken from the root disc */
    /* Segment files beside the kernel. BOTH or NEITHER: with an incomplete
     * pair the sizes are derived from the a.out header instead, which is the
     * path a kernel extracted from a disc image has to take. */
    char pseg[MC_PATH_LEN];
    char dseg[MC_PATH_LEN];
    /* disk0 is the root. A slot is unused when its image is empty. */
    char disks[MC_ND500_MAX_DISKS][MC_PATH_LEN];
    bool disk_writable[MC_ND500_MAX_DISKS];
} MC_Nd500;

typedef struct
{
    /* The CPU family number, kept because that is what existing .ini files
     * say and what MachineConfig_WriteFile still writes for the three plain
     * cases: 100 | 110 | 120. */
    int cpu_type;
    /* The RESOLVED model, and what MachineConfig_Apply actually installs. A
     * config may now name any model the emulator implements (ND110CX,
     * ND100CE, ...) instead of only the family; cpu_model is where that lands.
     * A bare number still works and maps as it always did. See cpu_model.h. */
    int cpu_model; /* a CpuType */
    int fpp_bits;  /* 32 | 48 - floating point unit width (default 48) */
    bool rtc_wall; /* false = RTC counts instruction ticks (default);
                                       true = RTC pulses every 20 ms of host wall-clock time */

    MC_Controller controllers[MC_MAX_CONTROLLERS];
    int controllerCount;

    int terminals[MC_MAX_TERMINALS];
    int terminalCount;

    bool ptreader_enabled;
    bool ptpunch_enabled;
    bool lineprinter_enabled;

    MC_BootSpec boot;
    MC_Runtime runtime;
    MC_Nd500 nd500;

    bool loaded_from_file;
    char source_path[MC_PATH_LEN];
} MachineConfig;

/* ---- Controller registry ---- */
typedef struct
{
    CtrlType type;
    const char *name; /* INI section name: controller.<name>.<wheel> */
    int min_wheel;
    int max_wheel;
    const uint16_t *iox_base; /* iox_base[wheel], valid for min..max_wheel */
    int iox_span;             /* IOX addresses claimed (for overlap checks) */
    int disk_slots;           /* 0 = not a disc controller */
    bool is_disc;
    bool bootable;
} ControllerDescriptor;

/**
 * @brief Look up the registry row for a controller type.
 *
 * Scans the ControllerDescriptor table for the row whose type matches.
 *
 * @param type Controller type to look up.
 * @return Pointer to the descriptor, or NULL if the type has no row.
 */
const ControllerDescriptor *MC_DescriptorForType(CtrlType type);


/**
 * @brief INI section name of a controller type ("floppy", "smd", "scsi", ...).
 *
 * @param type Controller type to name.
 * @return The descriptor's name, or "none" if the type has no registry row.
 */
const char *MC_CtrlTypeName(CtrlType type);

/**
 * @brief Baseline machine with NO disc/network controllers.
 *
 * cpu 100, terminals 5-11, peripherals on, boot smd.0.0, default runtime. Use
 * this before loading an INI (the INI fully specifies the controllers).
 *
 * @param cfg Configuration to overwrite with the baseline machine.
 */
void MachineConfig_InitBaseline(MachineConfig *cfg);

/**
 * @brief Build the built-in default machine.
 *
 * Baseline PLUS floppy + SMD + SCSI enabled on thumbwheel 0 - identical to
 * today's behavior when no INI is present.
 *
 * @param cfg Configuration to overwrite with the default machine.
 */
void MachineConfig_SetDefaults(MachineConfig *cfg);

/**
 * @brief Parse an INI file into cfg (cfg should be default-initialized first).
 *
 * @param cfg    Configuration filled in from the file.
 * @param path   INI file to read.
 * @param err    Buffer for a user-friendly, file:line qualified error message.
 * @param errlen Size of err in bytes.
 * @return true on success; false on a syntax/semantic error, with the message
 *         written into err.
 */
bool MachineConfig_LoadFile(MachineConfig *cfg, const char *path, char *err, size_t errlen);

/**
 * @brief Validate a populated config.
 *
 * Checks wheel ranges, duplicate controllers, IOX overlap and boot device
 * sanity.
 *
 * @param cfg    Configuration to check.
 * @param err    Buffer for the friendly message describing the first problem.
 * @param errlen Size of err in bytes.
 * @return true if the configuration is usable; false on the first problem.
 */
bool MachineConfig_Validate(const MachineConfig *cfg, char *err, size_t errlen);

/**
 * @brief Print the resolved machine (for --show-config).
 *
 * Controllers, wheels, IOX ranges, disks, boot, runtime. No ident codes.
 *
 * @param cfg Configuration to print.
 * @param out Stream to print to.
 */
void MachineConfig_Print(const MachineConfig *cfg, FILE *out);

/**
 * @brief Serialize the machine to INI text (the native twin of Download-.ini).
 *
 * Writes a commented, round-trippable file.
 *
 * @param cfg    Configuration to write.
 * @param path   File to create.
 * @param err    Buffer for a friendly message on a write error.
 * @param errlen Size of err in bytes.
 * @return true on success; false on a write error, message in err.
 */
bool MachineConfig_WriteFile(const MachineConfig *cfg, const char *path, char *err, size_t errlen);

/**
 * @brief Map the INI cpu number (100/110) to the CPU emulator's CpuType.
 *
 * Declared with int to avoid pulling the CPU header into every config consumer.
 *
 * @param cpuNumber The family number from the INI, e.g. 100 or 110.
 * @param outType   Receives the CpuType on success; untouched on failure.
 * @return true and *outType set on success; false if the number has no CpuType
 *         yet (e.g. 120).
 */
bool MachineConfig_CpuTypeForNumber(int cpuNumber, int *outType);

/**
 * @brief Derive the autoload INI filename from argv[0].
 *
 * Takes the basename, strips the directory and a trailing ".exe", and appends
 * ".ini". e.g. ".../nd110x" -> "nd110x.ini".
 *
 * @param argv0  Program path as invoked; NULL is treated as "nd100x".
 * @param outbuf Buffer receiving the filename; nothing is written if NULL.
 * @param outlen Size of outbuf in bytes; nothing is written if 0.
 */
void MachineConfig_DefaultIniName(const char *argv0, char *outbuf, size_t outlen);

#endif /* MACHINE_CONFIG_H */
