/*
 * nd100x - ND100 Virtual Machine
 *
 * Copyright (c) 2026 Ronny Hansen
 *
 * Unit tests for the [machine] fpp and rtc INI keys in
 * src/machine/machine_config.c: defaults, write/read round trip, and
 * rejection of invalid values.
 *
 * machine_config.c is linked in DIRECTLY; it has no machine/CPU dependencies
 * beyond headers, so no devices or disk images are involved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "machine_config.h"

/* machine_config.c calls these two SCSI helpers for the disk media names;
 * they are replicated here (1:1 with device_scsi.c) so the whole SCSI device
 * does not have to be linked in. */
SCSIUnitType SCSI_ParseUnitType(const char *name)
{
    if (!name)
        return SCSI_UNIT_NONE;
    if (strcmp(name, "hdd") == 0)
        return SCSI_UNIT_HDD;
    if (strcmp(name, "tape") == 0)
        return SCSI_UNIT_TAPE;
    if (strcmp(name, "cdrom") == 0)
        return SCSI_UNIT_CDROM;
    if (strcmp(name, "floppy") == 0)
        return SCSI_UNIT_FLOPPY;
    return SCSI_UNIT_NONE;
}

const char *SCSI_UnitTypeName(SCSIUnitType type)
{
    switch (type)
    {
    case SCSI_UNIT_HDD:    return "hdd";
    case SCSI_UNIT_TAPE:   return "tape";
    case SCSI_UNIT_CDROM:  return "cdrom";
    case SCSI_UNIT_FLOPPY: return "floppy";
    default:               return "none";
    }
}

static int mc_total;
static int mc_failed;

static void mc_check(const char *name, long exp, long got)
{
    mc_total++;
    if (exp != got) {
        printf("  FAIL  %-40s expected %ld, got %ld\n", name, exp, got);
        mc_failed++;
    }
}

static void mc_check_bool(const char *name, int cond)
{
    mc_total++;
    if (!cond) {
        printf("  FAIL  %s\n", name);
        mc_failed++;
    }
}

int main(void)
{
    MachineConfig cfg;
    char err[MC_ERR_LEN];
    char dir_tmpl[] = "/tmp/nd100x_mc_testXXXXXX";
    char *dir = mkdtemp(dir_tmpl);
    char path[300];

    if (!dir) {
        printf("mkdtemp failed\n");
        return 1;
    }

    /* Default is the standard 48-bit FPP. */
    MachineConfig_SetDefaults(&cfg);
    mc_check("default fpp_bits", 48, cfg.fpp_bits);

    /* Write a config with fpp = 32 and read it back. */
    cfg.fpp_bits = 32;
    snprintf(path, sizeof(path), "%s/fpp32.ini", dir);
    mc_check_bool("WriteFile(fpp=32)",
                  MachineConfig_WriteFile(&cfg, path, err, sizeof(err)));

    MachineConfig_InitBaseline(&cfg);
    mc_check_bool("LoadFile(fpp=32)",
                  MachineConfig_LoadFile(&cfg, path, err, sizeof(err)));
    mc_check("round-tripped fpp_bits", 32, cfg.fpp_bits);

    /* A config without an fpp key keeps the 48-bit default. */
    snprintf(path, sizeof(path), "%s/nofpp.ini", dir);
    {
        FILE *f = fopen(path, "w");
        mc_check_bool("write nofpp.ini", f != NULL);
        if (f) { fprintf(f, "[machine]\ncpu = 110\n"); fclose(f); }
    }
    MachineConfig_InitBaseline(&cfg);
    mc_check_bool("LoadFile(no fpp key)",
                  MachineConfig_LoadFile(&cfg, path, err, sizeof(err)));
    mc_check("fpp_bits stays default", 48, cfg.fpp_bits);
    mc_check("cpu_type read", 110, cfg.cpu_type);

    /* fpp = 99 must be rejected with a clear error. */
    snprintf(path, sizeof(path), "%s/bad.ini", dir);
    {
        FILE *f = fopen(path, "w");
        mc_check_bool("write bad.ini", f != NULL);
        if (f) { fprintf(f, "[machine]\nfpp = 99\n"); fclose(f); }
    }
    MachineConfig_InitBaseline(&cfg);
    err[0] = '\0';
    mc_check_bool("LoadFile(fpp=99) rejected",
                  !MachineConfig_LoadFile(&cfg, path, err, sizeof(err)));
    mc_check_bool("fpp=99 error mentions 'fpp'", strstr(err, "fpp") != NULL);

    /* Default RTC time base is instruction ticks. */
    MachineConfig_SetDefaults(&cfg);
    mc_check("default rtc_wall", 0, cfg.rtc_wall);

    /* Write a config with rtc = wall and read it back. */
    cfg.rtc_wall = true;
    snprintf(path, sizeof(path), "%s/rtcwall.ini", dir);
    mc_check_bool("WriteFile(rtc=wall)",
                  MachineConfig_WriteFile(&cfg, path, err, sizeof(err)));

    MachineConfig_InitBaseline(&cfg);
    mc_check_bool("LoadFile(rtc=wall)",
                  MachineConfig_LoadFile(&cfg, path, err, sizeof(err)));
    mc_check("round-tripped rtc_wall", 1, cfg.rtc_wall);

    /* rtc = ticks parses back to the default. */
    snprintf(path, sizeof(path), "%s/rtcticks.ini", dir);
    {
        FILE *f = fopen(path, "w");
        mc_check_bool("write rtcticks.ini", f != NULL);
        if (f) { fprintf(f, "[machine]\nrtc = ticks\n"); fclose(f); }
    }
    MachineConfig_InitBaseline(&cfg);
    cfg.rtc_wall = true; /* prove the key actively clears it */
    mc_check_bool("LoadFile(rtc=ticks)",
                  MachineConfig_LoadFile(&cfg, path, err, sizeof(err)));
    mc_check("rtc=ticks clears rtc_wall", 0, cfg.rtc_wall);

    /* A config without an rtc key keeps the ticks default. */
    MachineConfig_InitBaseline(&cfg);
    snprintf(path, sizeof(path), "%s/nofpp.ini", dir); /* reuse: has no rtc key */
    mc_check_bool("LoadFile(no rtc key)",
                  MachineConfig_LoadFile(&cfg, path, err, sizeof(err)));
    mc_check("rtc_wall stays default", 0, cfg.rtc_wall);

    /* rtc = sometimes must be rejected with a clear error. */
    snprintf(path, sizeof(path), "%s/badrtc.ini", dir);
    {
        FILE *f = fopen(path, "w");
        mc_check_bool("write badrtc.ini", f != NULL);
        if (f) { fprintf(f, "[machine]\nrtc = sometimes\n"); fclose(f); }
    }
    MachineConfig_InitBaseline(&cfg);
    err[0] = '\0';
    mc_check_bool("LoadFile(rtc=sometimes) rejected",
                  !MachineConfig_LoadFile(&cfg, path, err, sizeof(err)));
    mc_check_bool("rtc error mentions 'rtc'", strstr(err, "rtc") != NULL);

    /* ---- [nd500] ------------------------------------------------------ */

    /* No section at all: no ND-500. */
    MachineConfig_InitBaseline(&cfg);
    snprintf(path, sizeof(path), "%s/nofpp.ini", dir);   /* reuse: [machine] only */
    mc_check_bool("LoadFile(no nd500 section)",
                  MachineConfig_LoadFile(&cfg, path, err, sizeof(err)));
    mc_check("no section means no ND-500", 0, cfg.nd500.enabled);

    /* Naming the section is what enables it. */
    snprintf(path, sizeof(path), "%s/nd500.ini", dir);
    {
        FILE *f = fopen(path, "w");
        mc_check_bool("write nd500.ini", f != NULL);
        if (f) {
            fprintf(f, "[machine]\ncpu = 110\n\n[nd500]\n"
                       "memory = 32\nkernel = vmunix\n"
                       "disk0 = rootfs_full.img\n"
                       "disk1 = rw:scratch.img\n"
                       "disk2 = ro:archive.img\n");
            fclose(f);
        }
    }
    MachineConfig_InitBaseline(&cfg);
    mc_check_bool("LoadFile(nd500)",
                  MachineConfig_LoadFile(&cfg, path, err, sizeof(err)));
    mc_check("[nd500] enables the ND-500", 1, cfg.nd500.enabled);
    mc_check("nd500 memory", 32, cfg.nd500.memory_mb);
    mc_check_bool("nd500 kernel", strcmp(cfg.nd500.kernel, "vmunix") == 0);
    mc_check_bool("nd500 disk0 image",
                  strcmp(cfg.nd500.disks[0], "rootfs_full.img") == 0);
    /* THE important one. A bare image is READ-ONLY, unlike the ND-100 disc
     * slots - NDIX writes to its root as soon as it boots, and the images are
     * not reproducible. */
    mc_check("a bare nd500 disk is read-only", 0, cfg.nd500.disk_writable[0]);
    mc_check_bool("rw: strips its prefix",
                  strcmp(cfg.nd500.disks[1], "scratch.img") == 0);
    mc_check("rw: means writable", 1, cfg.nd500.disk_writable[1]);
    mc_check_bool("ro: strips its prefix",
                  strcmp(cfg.nd500.disks[2], "archive.img") == 0);
    mc_check("ro: means read-only", 0, cfg.nd500.disk_writable[2]);

    /* Round trip: the writer must not lose the section, or the first Save in
     * the config window silently deletes the whole ND-500. */
    snprintf(path, sizeof(path), "%s/nd500_out.ini", dir);
    mc_check_bool("WriteFile(nd500)",
                  MachineConfig_WriteFile(&cfg, path, err, sizeof(err)));
    MachineConfig_InitBaseline(&cfg);
    mc_check_bool("LoadFile(nd500 round trip)",
                  MachineConfig_LoadFile(&cfg, path, err, sizeof(err)));
    mc_check("round-tripped enabled", 1, cfg.nd500.enabled);
    mc_check("round-tripped memory", 32, cfg.nd500.memory_mb);
    mc_check_bool("round-tripped kernel", strcmp(cfg.nd500.kernel, "vmunix") == 0);
    mc_check_bool("round-tripped disk0",
                  strcmp(cfg.nd500.disks[0], "rootfs_full.img") == 0);
    mc_check("round-tripped disk0 stays read-only", 0, cfg.nd500.disk_writable[0]);
    mc_check("round-tripped disk1 stays writable", 1, cfg.nd500.disk_writable[1]);

    /* A machine with no ND-500 must not GROW an [nd500] section. */
    MachineConfig_SetDefaults(&cfg);
    snprintf(path, sizeof(path), "%s/no_nd500_out.ini", dir);
    mc_check_bool("WriteFile(no nd500)",
                  MachineConfig_WriteFile(&cfg, path, err, sizeof(err)));
    {
        FILE *f = fopen(path, "r");
        char buf[8192];
        size_t n = f ? fread(buf, 1, sizeof(buf) - 1, f) : 0;
        if (f) fclose(f);
        buf[n] = '\0';
        mc_check_bool("no ND-500 means no [nd500] in the file",
                      strstr(buf, "[nd500]") == NULL);
    }

    /* Bad values are rejected, and the message says which key. */
    snprintf(path, sizeof(path), "%s/badnd500.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f) { fprintf(f, "[nd500]\nmemory = 999\n"); fclose(f); }
    }
    MachineConfig_InitBaseline(&cfg);
    err[0] = '\0';
    mc_check_bool("LoadFile(nd500 memory=999) rejected",
                  !MachineConfig_LoadFile(&cfg, path, err, sizeof(err)));
    mc_check_bool("the error mentions memory", strstr(err, "memory") != NULL);

    snprintf(path, sizeof(path), "%s/badkey.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f) { fprintf(f, "[nd500]\nwibble = 1\n"); fclose(f); }
    }
    MachineConfig_InitBaseline(&cfg);
    err[0] = '\0';
    mc_check_bool("LoadFile(nd500 unknown key) rejected",
                  !MachineConfig_LoadFile(&cfg, path, err, sizeof(err)));
    mc_check_bool("the error lists the known keys", strstr(err, "kernel") != NULL);

    snprintf(path, sizeof(path), "%s/baddisk.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f) { fprintf(f, "[nd500]\ndisk99 = x.img\n"); fclose(f); }
    }
    MachineConfig_InitBaseline(&cfg);
    err[0] = '\0';
    mc_check_bool("LoadFile(nd500 disk99) rejected",
                  !MachineConfig_LoadFile(&cfg, path, err, sizeof(err)));

    printf("machine_config tests: %d checks, %d failed\n", mc_total, mc_failed);
    return mc_failed ? 1 : 0;
}
