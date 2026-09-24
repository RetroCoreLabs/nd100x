/*
 * test_machine_config.c - Unit tests for the [machine] fpp and rtc INI keys.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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

bool mc_to_json(const MachineConfig *cfg, char *out, size_t outlen);

/* machine_config.c calls these two SCSI helpers for the disk media names;
 * they are replicated here (1:1 with device_scsi.c) so the whole SCSI device
 * does not have to be linked in. */
SCSIUnitType scsi_parse_unit_type(const char *name)
{
    if (!name)
    {
        return SCSI_UNIT_NONE;
    }
    if (strcmp(name, "hdd") == 0)
    {
        return SCSI_UNIT_HDD;
    }
    if (strcmp(name, "tape") == 0)
    {
        return SCSI_UNIT_TAPE;
    }
    if (strcmp(name, "cdrom") == 0)
    {
        return SCSI_UNIT_CDROM;
    }
    if (strcmp(name, "floppy") == 0)
    {
        return SCSI_UNIT_FLOPPY;
    }
    return SCSI_UNIT_NONE;
}

const char *scsi_unit_type_name(SCSIUnitType type)
{
    switch (type)
    {
    case SCSI_UNIT_HDD:
        return "hdd";
    case SCSI_UNIT_TAPE:
        return "tape";
    case SCSI_UNIT_CDROM:
        return "cdrom";
    case SCSI_UNIT_FLOPPY:
        return "floppy";
    default:
        return "none";
    }
}

static int mc_total;
static int mc_failed;

static void mc_check(const char *name, long exp, long got)
{
    mc_total++;
    if (exp != got)
    {
        printf("  FAIL  %-40s expected %ld, got %ld\n", name, exp, got);
        mc_failed++;
    }
}

static void mc_check_bool(const char *name, int cond)
{
    mc_total++;
    if (!cond)
    {
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

    if (!dir)
    {
        printf("mkdtemp failed\n");
        return 1;
    }

    /* Default is the standard 48-bit FPP. */
    mc_set_defaults(&cfg);
    mc_check("default fpp_bits", 48, cfg.fpp_bits);

    /* Write a config with fpp = 32 and read it back. */
    cfg.fpp_bits = 32;
    snprintf(path, sizeof(path), "%s/fpp32.ini", dir);
    mc_check_bool("WriteFile(fpp=32)", mc_write_file(&cfg, path, err, sizeof(err)));

    mc_init_baseline(&cfg);
    mc_check_bool("LoadFile(fpp=32)", mc_load_file(&cfg, path, err, sizeof(err)));
    mc_check("round-tripped fpp_bits", 32, cfg.fpp_bits);

    /* A config without an fpp key keeps the 48-bit default. */
    snprintf(path, sizeof(path), "%s/nofpp.ini", dir);
    {
        FILE *f = fopen(path, "w");
        mc_check_bool("write nofpp.ini", f != NULL);
        if (f)
        {
            fprintf(f, "[machine]\ncpu = 110\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    mc_check_bool("LoadFile(no fpp key)", mc_load_file(&cfg, path, err, sizeof(err)));
    mc_check("fpp_bits stays default", 48, cfg.fpp_bits);
    mc_check("cpu_type read", 110, cfg.cpu_type);

    /* fpp = 99 must be rejected with a clear error. */
    snprintf(path, sizeof(path), "%s/bad.ini", dir);
    {
        FILE *f = fopen(path, "w");
        mc_check_bool("write bad.ini", f != NULL);
        if (f)
        {
            fprintf(f, "[machine]\nfpp = 99\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    err[0] = '\0';
    mc_check_bool("LoadFile(fpp=99) rejected", !mc_load_file(&cfg, path, err, sizeof(err)));
    mc_check_bool("fpp=99 error mentions 'fpp'", strstr(err, "fpp") != NULL);

    /* Default RTC time base is instruction ticks. */
    mc_set_defaults(&cfg);
    mc_check("default rtc_wall", 0, cfg.rtc_wall);

    /* Write a config with rtc = wall and read it back. */
    cfg.rtc_wall = true;
    snprintf(path, sizeof(path), "%s/rtcwall.ini", dir);
    mc_check_bool("WriteFile(rtc=wall)", mc_write_file(&cfg, path, err, sizeof(err)));

    mc_init_baseline(&cfg);
    mc_check_bool("LoadFile(rtc=wall)", mc_load_file(&cfg, path, err, sizeof(err)));
    mc_check("round-tripped rtc_wall", 1, cfg.rtc_wall);

    /* rtc = ticks parses back to the default. */
    snprintf(path, sizeof(path), "%s/rtcticks.ini", dir);
    {
        FILE *f = fopen(path, "w");
        mc_check_bool("write rtcticks.ini", f != NULL);
        if (f)
        {
            fprintf(f, "[machine]\nrtc = ticks\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    cfg.rtc_wall = true; /* prove the key actively clears it */
    mc_check_bool("LoadFile(rtc=ticks)", mc_load_file(&cfg, path, err, sizeof(err)));
    mc_check("rtc=ticks clears rtc_wall", 0, cfg.rtc_wall);

    /* A config without an rtc key keeps the ticks default. */
    mc_init_baseline(&cfg);
    snprintf(path, sizeof(path), "%s/nofpp.ini", dir); /* reuse: has no rtc key */
    mc_check_bool("LoadFile(no rtc key)", mc_load_file(&cfg, path, err, sizeof(err)));
    mc_check("rtc_wall stays default", 0, cfg.rtc_wall);

    /* rtc = sometimes must be rejected with a clear error. */
    snprintf(path, sizeof(path), "%s/badrtc.ini", dir);
    {
        FILE *f = fopen(path, "w");
        mc_check_bool("write badrtc.ini", f != NULL);
        if (f)
        {
            fprintf(f, "[machine]\nrtc = sometimes\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    err[0] = '\0';
    mc_check_bool("LoadFile(rtc=sometimes) rejected", !mc_load_file(&cfg, path, err, sizeof(err)));
    mc_check_bool("rtc error mentions 'rtc'", strstr(err, "rtc") != NULL);

    /* ---- [nd500] ------------------------------------------------------ */

    /* No section at all: no ND-500. */
    mc_init_baseline(&cfg);
    snprintf(path, sizeof(path), "%s/nofpp.ini", dir); /* reuse: [machine] only */
    mc_check_bool("LoadFile(no nd500 section)", mc_load_file(&cfg, path, err, sizeof(err)));
    mc_check("no section means no ND-500", 0, cfg.nd500.enabled);

    /* Naming the section is what enables it. */
    snprintf(path, sizeof(path), "%s/nd500.ini", dir);
    {
        FILE *f = fopen(path, "w");
        mc_check_bool("write nd500.ini", f != NULL);
        if (f)
        {
            fprintf(f, "[machine]\ncpu = 110\n\n[nd500]\n"
                       "memory = 32\nkernel = vmunix\n"
                       "disk0 = rootfs_full.img\n"
                       "disk1 = rw:scratch.img\n"
                       "disk2 = ro:archive.img\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    mc_check_bool("LoadFile(nd500)", mc_load_file(&cfg, path, err, sizeof(err)));
    mc_check("[nd500] enables the ND-500", 1, cfg.nd500.enabled);
    mc_check("nd500 memory", 32, cfg.nd500.memory_mb);
    mc_check_bool("nd500 kernel", strcmp(cfg.nd500.kernel, "vmunix") == 0);
    mc_check_bool("nd500 disk0 image", strcmp(cfg.nd500.disks[0], "rootfs_full.img") == 0);
    /* THE important one. A bare image is READ-ONLY, unlike the ND-100 disc
     * slots - NDIX writes to its root as soon as it boots, and the images are
     * not reproducible. */
    mc_check("a bare nd500 disk is read-only", 0, cfg.nd500.disk_writable[0]);
    mc_check_bool("rw: strips its prefix", strcmp(cfg.nd500.disks[1], "scratch.img") == 0);
    mc_check("rw: means writable", 1, cfg.nd500.disk_writable[1]);
    mc_check_bool("ro: strips its prefix", strcmp(cfg.nd500.disks[2], "archive.img") == 0);
    mc_check("ro: means read-only", 0, cfg.nd500.disk_writable[2]);

    /* Round trip: the writer must not lose the section, or the first Save in
     * the config window silently deletes the whole ND-500. */
    snprintf(path, sizeof(path), "%s/nd500_out.ini", dir);
    mc_check_bool("WriteFile(nd500)", mc_write_file(&cfg, path, err, sizeof(err)));
    mc_init_baseline(&cfg);
    mc_check_bool("LoadFile(nd500 round trip)", mc_load_file(&cfg, path, err, sizeof(err)));
    mc_check("round-tripped enabled", 1, cfg.nd500.enabled);
    mc_check("round-tripped memory", 32, cfg.nd500.memory_mb);
    mc_check_bool("round-tripped kernel", strcmp(cfg.nd500.kernel, "vmunix") == 0);
    mc_check_bool("round-tripped disk0", strcmp(cfg.nd500.disks[0], "rootfs_full.img") == 0);
    mc_check("round-tripped disk0 stays read-only", 0, cfg.nd500.disk_writable[0]);
    mc_check("round-tripped disk1 stays writable", 1, cfg.nd500.disk_writable[1]);

    /* A machine with no ND-500 must not GROW an [nd500] section. */
    mc_set_defaults(&cfg);
    snprintf(path, sizeof(path), "%s/no_nd500_out.ini", dir);
    mc_check_bool("WriteFile(no nd500)", mc_write_file(&cfg, path, err, sizeof(err)));
    {
        FILE *f = fopen(path, "r");
        char buf[8192];
        size_t n = f ? fread(buf, 1, sizeof(buf) - 1, f) : 0;
        if (f)
        {
            fclose(f);
        }
        buf[n] = '\0';
        mc_check_bool("no ND-500 means no [nd500] in the file", strstr(buf, "[nd500]") == NULL);
    }

    /* Bad values are rejected, and the message says which key. */
    snprintf(path, sizeof(path), "%s/badnd500.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[nd500]\nmemory = 999\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    err[0] = '\0';
    mc_check_bool("LoadFile(nd500 memory=999) rejected",
                  !mc_load_file(&cfg, path, err, sizeof(err)));
    mc_check_bool("the error mentions memory", strstr(err, "memory") != NULL);

    snprintf(path, sizeof(path), "%s/badkey.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[nd500]\nwibble = 1\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    err[0] = '\0';
    mc_check_bool("LoadFile(nd500 unknown key) rejected",
                  !mc_load_file(&cfg, path, err, sizeof(err)));
    mc_check_bool("the error lists the known keys", strstr(err, "kernel") != NULL);

    snprintf(path, sizeof(path), "%s/baddisk.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[nd500]\ndisk99 = x.img\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    err[0] = '\0';
    mc_check_bool("LoadFile(nd500 disk99) rejected", !mc_load_file(&cfg, path, err, sizeof(err)));

    /* ------------------------------------------------------------------
     * The MFbus / octobus / ND-5000 schema.
     * ------------------------------------------------------------------ */

    /* A full configuration, including the octal page and station numbers every
     * ND manual uses. 004100B is 2112, which is ND-100 byte address 0x420000. */
    snprintf(path, sizeof(path), "%s/mfbus.ini", dir);
    {
        FILE *f = fopen(path, "w");
        mc_check_bool("write mfbus.ini", f != NULL);
        if (f)
        {
            fprintf(f, "[mfbus]\nsize = 16\nbase_page = 004100B\n\n");
            fprintf(f, "[mfbus.part.0]\npages = 4096\nnd100 = yes\n");
            fprintf(f, "nd500_p = yes\nnd500_d = yes\n\n");
            fprintf(f, "[mfbus.part.1]\npages = 2048\nnd100 = no\n\n");
            fprintf(f, "[controller.octobus.0]\nenabled = yes\n\n");
            fprintf(f, "[nd5000.1]\nstation = 070B\ncpu_type = 5000\n\n");
            fprintf(f, "[nd5000.2]\nenabled = no\nstation = 071B\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    mc_check_bool("LoadFile(mfbus)", mc_load_file(&cfg, path, err, sizeof(err)));
    mc_check_bool("mfbus enabled by naming the section", cfg.mfbus.enabled);
    mc_check("mfbus size", 16, cfg.mfbus.size_mb);
    /* OCTAL: 004100B is 2112 decimal, NOT 4100. */
    mc_check("base_page parsed as OCTAL", 2112, cfg.mfbus.base_page);
    mc_check("mfbus part count", 2, cfg.mfbus.partCount);
    mc_check("part 0 pages", 4096, cfg.mfbus.parts[0].pages);
    mc_check_bool("part 0 is ND-100 accessible", cfg.mfbus.parts[0].nd100);
    /* A part the ND-100 cannot reach is how a pool larger than its 32 MB view
     * is expressed. */
    mc_check_bool("part 1 is NOT ND-100 accessible", !cfg.mfbus.parts[1].nd100);
    mc_check_bool("part 1 defaults to ND-500 program access", cfg.mfbus.parts[1].nd500_p);
    mc_check_bool("octobus controller enabled", cfg.octobus.enabled);
    mc_check("two ND-5000 CPUs", 2, cfg.nd5000Count);
    /* 070B is 56 decimal. 70 would not fit the 6-bit station field. */
    mc_check("CPU 1 station is 070B = 56", 56, cfg.nd5000[0].station);
    mc_check("CPU 2 station is 071B = 57", 57, cfg.nd5000[1].station);
    mc_check_bool("CPU 1 enabled", cfg.nd5000[0].enabled);
    mc_check_bool("CPU 2 disabled", !cfg.nd5000[1].enabled);
    mc_check("CPU 1 cpu_type", 5000, cfg.nd5000[0].cpu_type);
    mc_check_bool("no per-CPU base_page set", !cfg.nd5000[0].base_page_set);

    /* Round trip: the written file must load back the same, and in particular
     * the octal page must not come back as a different page. */
    {
        char out[300];
        snprintf(out, sizeof(out), "%s/mfbus_out.ini", dir);
        mc_check_bool("WriteFile(mfbus)", mc_write_file(&cfg, out, err, sizeof(err)));
        MachineConfig back;
        mc_init_baseline(&back);
        mc_check_bool("LoadFile(mfbus round trip)", mc_load_file(&back, out, err, sizeof(err)));
        mc_check("round-tripped base_page", 2112, back.mfbus.base_page);
        mc_check("round-tripped station 1", 56, back.nd5000[0].station);
        mc_check("round-tripped station 2", 57, back.nd5000[1].station);
        mc_check("round-tripped part count", 2, back.mfbus.partCount);
        mc_check_bool("round-tripped part 1 nd100 = no", !back.mfbus.parts[1].nd100);
        mc_check_bool("round-tripped CPU 2 still disabled", !back.nd5000[1].enabled);
    }

    /* A section with no station gets 070B + (n-1). */
    snprintf(path, sizeof(path), "%s/nostation.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[nd5000.3]\nenabled = yes\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    mc_check_bool("LoadFile(no station)", mc_load_file(&cfg, path, err, sizeof(err)));
    mc_check("slot 3 defaults to 072B = 58", 58, cfg.nd5000[0].station);

    /* Decimal without the B is accepted too - 56 IS 070B. */
    snprintf(path, sizeof(path), "%s/decimal.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[nd5000.1]\nstation = 56\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    mc_check_bool("LoadFile(decimal station)", mc_load_file(&cfg, path, err, sizeof(err)));
    mc_check("decimal 56 is the same station", 56, cfg.nd5000[0].station);

    /* A station outside 070B..076B belongs to another device entirely. */
    snprintf(path, sizeof(path), "%s/badstation.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[nd5000.1]\nstation = 010B\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    err[0] = '\0';
    mc_check_bool("LoadFile(station 010B) rejected",
                  !mc_load_file(&cfg, path, err, sizeof(err)));
    mc_check_bool("the error names the legal range", strstr(err, "070B") != NULL);

    /* Two CPUs on one station would answer each other's messages. */
    snprintf(path, sizeof(path), "%s/dupstation.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[nd5000.1]\nstation = 070B\n\n[nd5000.2]\nstation = 070B\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    err[0] = '\0';
    mc_check_bool("LoadFile(duplicate station) rejected",
                  !mc_load_file(&cfg, path, err, sizeof(err)));

    /* A duplicate slot, the same way a duplicate controller+thumbwheel is. */
    snprintf(path, sizeof(path), "%s/dupslot.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[nd5000.1]\nstation = 070B\n\n[nd5000.1]\nstation = 071B\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    err[0] = '\0';
    mc_check_bool("LoadFile(duplicate slot) rejected",
                  !mc_load_file(&cfg, path, err, sizeof(err)));

    /* There are seven hardware slots, 070B..076B. */
    snprintf(path, sizeof(path), "%s/slot8.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[nd5000.8]\nenabled = yes\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    err[0] = '\0';
    mc_check_bool("LoadFile(slot 8) rejected", !mc_load_file(&cfg, path, err, sizeof(err)));

    /* A malformed page number is a configuration error, not a different page. */
    snprintf(path, sizeof(path), "%s/badpage.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[mfbus]\nbase_page = 12x4\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    err[0] = '\0';
    mc_check_bool("LoadFile(bad base_page) rejected",
                  !mc_load_file(&cfg, path, err, sizeof(err)));

    /* 008B is not octal. Accepting it as 8 would silently pick another page. */
    snprintf(path, sizeof(path), "%s/badoctal.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[mfbus]\nbase_page = 008B\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    err[0] = '\0';
    mc_check_bool("LoadFile(008B) rejected - 8 is not an octal digit",
                  !mc_load_file(&cfg, path, err, sizeof(err)));

    /* The existing [nd500] section must keep working untouched, so no existing
     * .ini file breaks. */
    snprintf(path, sizeof(path), "%s/legacy.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[nd500]\nmemory = 32\nkernel = ndix\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    mc_check_bool("LoadFile(legacy [nd500])", mc_load_file(&cfg, path, err, sizeof(err)));
    mc_check_bool("legacy [nd500] still enables the ND-500", cfg.nd500.enabled);
    mc_check("legacy memory still read", 32, cfg.nd500.memory_mb);
    mc_check("legacy config defines no ND-5000 CPUs", 0, cfg.nd5000Count);
    mc_check_bool("and no MFbus pool", !cfg.mfbus.enabled);

    /* ------------------------------------------------------------------
     * Phase 5 guard: every ND-5000 shares one ND-500 address zero, for now.
     * ------------------------------------------------------------------ */

    /* SINTRAN stores this per CPU, so the .ini allows a per-CPU base_page - but
     * whether the values may DIFFER is unsettled, and two different address
     * zeros over one shared pool would place every shared structure at two
     * different ND-500 addresses. Refused until the question is settled. */
    snprintf(path, sizeof(path), "%s/basepagediff.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[mfbus]\nsize = 16\n\n");
            fprintf(f, "[nd5000.1]\nstation = 070B\nbase_page = 004100B\n\n");
            fprintf(f, "[nd5000.2]\nstation = 071B\nbase_page = 004200B\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    mc_check_bool("LoadFile(differing base_page)", mc_load_file(&cfg, path, err, sizeof(err)));
    err[0] = '\0';
    mc_check_bool("validate rejects differing per-CPU base_page",
                  !mc_validate(&cfg, err, sizeof(err)));
    mc_check_bool("and says why", strstr(err, "address zero") != NULL);

    /* The same value on both is fine. */
    snprintf(path, sizeof(path), "%s/basepagesame.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[mfbus]\nsize = 16\nbase_page = 004100B\n\n");
            fprintf(f, "[nd5000.1]\nstation = 070B\nbase_page = 004100B\n\n");
            fprintf(f, "[nd5000.2]\nstation = 071B\nbase_page = 004100B\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    mc_check_bool("LoadFile(same base_page)", mc_load_file(&cfg, path, err, sizeof(err)));
    err[0] = '\0';
    /* These minimal files define no disc controller, so validation still fails
     * on the default boot device - which is not what is under test here. The
     * check is that it does NOT fail on the address-zero rule. */
    (void)mc_validate(&cfg, err, sizeof(err));
    mc_check_bool("matching per-CPU base_page raises no address-zero complaint",
                  strstr(err, "address zero") == NULL);

    /* A per-CPU base_page disagreeing with the pool's is the same mistake. */
    snprintf(path, sizeof(path), "%s/basepagepool.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[mfbus]\nsize = 16\nbase_page = 004100B\n\n");
            fprintf(f, "[nd5000.1]\nstation = 070B\nbase_page = 004200B\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    mc_check_bool("LoadFile(base_page vs pool)", mc_load_file(&cfg, path, err, sizeof(err)));
    err[0] = '\0';
    mc_check_bool("validate rejects a per-CPU base_page that disagrees with the pool",
                  !mc_validate(&cfg, err, sizeof(err)));

    /* An ND-5000 with no [mfbus] has nowhere to run: it has no private memory
     * at all, it executes out of the shared pool. */
    snprintf(path, sizeof(path), "%s/nopool.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[nd5000.1]\nenabled = yes\nstation = 070B\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    mc_check_bool("LoadFile(no pool)", mc_load_file(&cfg, path, err, sizeof(err)));
    err[0] = '\0';
    mc_check_bool("validate rejects an ND-5000 with no MFbus pool",
                  !mc_validate(&cfg, err, sizeof(err)));
    mc_check_bool("and says it has nowhere to run", strstr(err, "nowhere to run") != NULL);

    /* A DISABLED CPU with no pool is fine - keeping a configuration around is
     * what 'enabled = no' is for. */
    snprintf(path, sizeof(path), "%s/nopooloff.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[nd5000.1]\nenabled = no\nstation = 070B\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    mc_check_bool("LoadFile(no pool, CPU off)", mc_load_file(&cfg, path, err, sizeof(err)));
    err[0] = '\0';
    (void)mc_validate(&cfg, err, sizeof(err));
    mc_check_bool("a DISABLED ND-5000 with no pool raises no complaint",
                  strstr(err, "nowhere to run") == NULL);

    /* Per-CPU boot material: the schema accepts it, the ND-500 host interface
     * does not yet serve it per CPU, so TWO enabled CPUs with their own discs
     * are refused rather than both quietly given the same one. */
    snprintf(path, sizeof(path), "%s/twodisks.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[mfbus]\nsize = 16\n\n");
            fprintf(f, "[nd5000.1]\nstation = 070B\ndisk0 = a.img\n\n");
            fprintf(f, "[nd5000.2]\nstation = 071B\ndisk0 = b.img\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    mc_check_bool("LoadFile(two CPUs with discs)", mc_load_file(&cfg, path, err, sizeof(err)));
    err[0] = '\0';
    mc_check_bool("validate rejects boot material on two enabled CPUs",
                  !mc_validate(&cfg, err, sizeof(err)));
    mc_check_bool("and explains they would share one disc",
                  strstr(err, "same disc") != NULL);

    /* ONE CPU with discs is the NDIX case that works today. */
    snprintf(path, sizeof(path), "%s/onedisk.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[mfbus]\nsize = 16\n\n");
            fprintf(f, "[nd5000.1]\nstation = 070B\ndisk0 = a.img\n\n");
            fprintf(f, "[nd5000.2]\nstation = 071B\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    mc_check_bool("LoadFile(one CPU with a disc)", mc_load_file(&cfg, path, err, sizeof(err)));
    err[0] = '\0';
    (void)mc_validate(&cfg, err, sizeof(err));
    mc_check_bool("one CPU with boot material raises no complaint",
                  strstr(err, "same disc") == NULL);

    /* A DISABLED second CPU with discs is fine - it is not built. */
    snprintf(path, sizeof(path), "%s/onedisabled.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[mfbus]\nsize = 16\n\n");
            fprintf(f, "[nd5000.1]\nstation = 070B\ndisk0 = a.img\n\n");
            fprintf(f, "[nd5000.2]\nenabled = no\nstation = 071B\ndisk0 = b.img\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    mc_check_bool("LoadFile(second CPU disabled)", mc_load_file(&cfg, path, err, sizeof(err)));
    err[0] = '\0';
    (void)mc_validate(&cfg, err, sizeof(err));
    mc_check_bool("a DISABLED second CPU with discs raises no complaint",
                  strstr(err, "same disc") == NULL);

    /* ------------------------------------------------------------------
     * The JSON export - the browser's view of the same configuration.
     * ------------------------------------------------------------------ */
    snprintf(path, sizeof(path), "%s/json.ini", dir);
    {
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "[mfbus]\nsize = 16\nbase_page = 004100B\n\n");
            fprintf(f, "[mfbus.part.0]\npages = 4096\nnd100 = no\n\n");
            fprintf(f, "[controller.octobus.0]\nenabled = yes\n\n");
            fprintf(f, "[nd5000.1]\nstation = 070B\n\n");
            fprintf(f, "[nd5000.2]\nstation = 071B\n");
            fclose(f);
        }
    }
    mc_init_baseline(&cfg);
    mc_check_bool("LoadFile(json.ini)", mc_load_file(&cfg, path, err, sizeof(err)));
    {
        static char json[32768];
        mc_check_bool("mc_to_json succeeded", mc_to_json(&cfg, json, sizeof(json)));
        mc_check_bool("json has the mfbus object", strstr(json, "\"mfbus\":{") != NULL);
        mc_check_bool("json has the octobus object", strstr(json, "\"octobus\":{") != NULL);
        mc_check_bool("json has the nd5000 array", strstr(json, "\"nd5000\":[") != NULL);
        /* basePage travels as a NUMBER: JSON has no octal literal, so 004100B
         * is sent as 2112 and the form renders the octal. */
        mc_check_bool("basePage is the decimal 2112", strstr(json, "\"basePage\":2112") != NULL);
        mc_check_bool("station 070B is the decimal 56", strstr(json, "\"station\":56") != NULL);
        mc_check_bool("station 071B is the decimal 57", strstr(json, "\"station\":57") != NULL);
        mc_check_bool("a part the ND-100 cannot reach says so",
                      strstr(json, "\"nd100\":false") != NULL);
        /* TWO CPUs must be SEPARATED. An array element written without its
         * comma produces }{ , which is not JSON at all and would break the whole
         * machine description rather than just this section. */
        mc_check_bool("array elements are comma separated", strstr(json, "}{") == NULL);
        mc_check_bool("the two CPU objects are joined correctly",
                      strstr(json, "},{\"slot\":2") != NULL);
    }

    printf("machine_config tests: %d checks, %d failed\n", mc_total, mc_failed);
    return mc_failed ? 1 : 0;
}
