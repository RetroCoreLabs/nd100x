/*
 * machine_config_apply.h - turn a MachineConfig into an actual machine.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * machine_config.h calls itself "the single in-memory model of what machine to
 * build". This is the other half: the code that BUILDS it. Until now that code
 * was a static function inside the native CLI frontend, which meant the model
 * was only single-source-of-truth for a caller that happened to be nd100x.c.
 * The browser parsed an INI, validated it, printed a friendly error - and then
 * threw the config away and built a hardcoded machine anyway.
 *
 * Nothing here needs a command line or a host filesystem beyond what the mount
 * helpers already need: mount_smd(), mount_floppy(), mount_winchester(),
 * mount_scsi() and machine_add_hdlc() all live in the machine library, and the
 * wasm build already calls the first of them.
 */
#ifndef MACHINE_CONFIG_APPLY_H
#define MACHINE_CONFIG_APPLY_H

#include "machine_config.h"

/* Things a command line may already have decided, which the .ini must not then
 * overwrite. The native frontend has --fpp and --rtc flags and the rule is
 * "the CLI wins over the .ini key", mirroring how --memory beats memory=.
 * A caller with no command line (the browser) passes zeroes and the config
 * decides everything. */
typedef struct MachineConfigApplyOpts
{
    int fpp_already_set; /* non-zero: leave CurrentFPPType alone */
    int rtc_already_set; /* non-zero: leave the RTC time base alone */
} MachineConfigApplyOpts;

/* TWO halves, and the order is not a style choice - they straddle machine_init().
 *
 *     MachineConfig_ApplyCpu(&mc, &opts);
 *     machine_init(...);
 *     MachineConfig_ApplyDevices(&mc);
 *
 * There is deliberately no single MachineConfig_Apply() that does both. It
 * would have to be called from one place, and no such place exists.
 *
 * <opts> may be NULL, which means "nothing was decided elsewhere". */

/**
 * @brief Install the CPU model, FPP width and RTC time base from the config.
 *
 * CPU model, FPP width and RTC time base.
 *
 * MUST run BEFORE machine_init(). cpu_init() calls Setup_Instructions(), which
 * READS CurrentCPUType to decide which opcode groups to register - VERSN, the
 * 14050x/14051x S3SEG group, the 14070x bank group. Setting the model after
 * that point changes a variable nothing looks at again: the dispatch table was
 * already built for whatever model was current, so the guest probes for VERSN,
 * traps, and concludes it is on an ND-100 no matter what the config said.
 *
 * That is exactly what used to happen to every `cpu = 110` in an .ini. The CLI
 * escaped it only because apply_cputype_override() is called before
 * machine_init, with a comment saying it must be.
 *
 * @param mc   The configuration to install.
 * @param opts What a command line already decided; may be NULL for "nothing".
 */
void MachineConfig_ApplyCpu(const MachineConfig *mc, const MachineConfigApplyOpts *opts);

/**
 * @brief Add the configured terminals, disc controllers with their mounted
 *        images, and HDLC.
 *
 * MUST run AFTER machine_init(): the core devices (RTC, console, floppy DMA,
 * SMD, tape, printer) are added by DeviceManager_AddAllDevices inside it, and
 * this adds the configured parts on top of them.
 *
 * @param mc The configuration whose devices are added.
 */
void MachineConfig_ApplyDevices(const MachineConfig *mc);

#endif /* MACHINE_CONFIG_APPLY_H */
