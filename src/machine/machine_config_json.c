/*
 * machine_config_json.c - describe a resolved MachineConfig as JSON.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * WHY: the Machine Setup form has to show what a configuration currently says -
 * which CPU, which controllers, which images. The obvious way is to parse the
 * INI in JavaScript, and that is the one thing this codebase keeps refusing to
 * do: a second parser drifts from the first, and then the form shows one
 * machine while the emulator builds another. ValidateMachineINI already exists
 * for exactly that reason ("so the browser gets identical, friendly error
 * messages").
 *
 * So reading goes through the SAME C parser and comes back as JSON. The form
 * only ever GENERATES ini text, which is then handed straight back to the C
 * validator before it is saved. Neither direction has a second opinion about
 * what an .ini means.
 *
 * Hand-written, no cJSON: this emits a fixed, flat shape with no user-supplied
 * keys, and the only values that need care are paths - handled by esc() below.
 * Pulling a JSON library into the machine library for one function would cost
 * more than it saves.
 */
#include "machine_config_json.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "../cpu/cpu_model.h"

/* Append to a bounded buffer, tracking overflow rather than truncating
 * silently: a half-written JSON object would fail to parse in the browser with
 * no clue why. */
typedef struct
{
    char *p;
    size_t left;
    int overflow;
} Sink;

static void put(Sink *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void put(Sink *s, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (s->overflow)
    {
        return;
    }
    va_start(ap, fmt);
    n = vsnprintf(s->p, s->left, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= s->left)
    {
        s->overflow = 1;
        return;
    }
    s->p += n;
    s->left -= (size_t)n;
}

/* A JSON string body. Only the characters JSON actually forbids are escaped;
 * a Windows path full of backslashes is the realistic input here. */
static void esc(Sink *s, const char *v)
{
    if (!v)
    {
        return;
    }
    for (; *v; v++)
    {
        switch (*v)
        {
        case '"':
            put(s, "\\\"");
            break;
        case '\\':
            put(s, "\\\\");
            break;
        case '\n':
            put(s, "\\n");
            break;
        case '\r':
            put(s, "\\r");
            break;
        case '\t':
            put(s, "\\t");
            break;
        default:
            if ((unsigned char)*v < 0x20)
            {
                put(s, "\\u%04x", (unsigned char)*v);
            }
            else
            {
                put(s, "%c", *v);
            }
        }
    }
}

static void kv_str(Sink *s, const char *key, const char *val, int comma)
{
    put(s, "\"%s\":\"", key);
    esc(s, val);
    put(s, "\"%s", comma ? "," : "");
}

/* The SCSI media vocabulary, as the INI spells it. */
static const char *media_name(SCSIUnitType m)
{
    switch (m)
    {
    case SCSI_UNIT_HDD:
        return "hdd";
    case SCSI_UNIT_CDROM:
        return "cdrom";
    case SCSI_UNIT_TAPE:
        return "tape";
    case SCSI_UNIT_FLOPPY:
        return "floppy";
    default:
        return "hdd";
    }
}

bool MachineConfig_ToJson(const MachineConfig *cfg, char *out, size_t outlen)
{
    Sink s;
    int i;
    int j;

    if (!cfg || !out || outlen == 0)
    {
        return false;
    }
    s.p = out;
    s.left = outlen;
    s.overflow = 0;

    put(&s, "{");

    /* ---- machine ---- */
    put(&s, "\"machine\":{");
    kv_str(&s, "cpu", CpuModel_Name((CpuType)cfg->cpu_model), 1);
    kv_str(&s, "cpuDisplay", CpuModel_DisplayName((CpuType)cfg->cpu_model), 1);
    put(&s, "\"cpuNumber\":%d,", cfg->cpu_type);
    put(&s, "\"fpp\":%d,", cfg->fpp_bits);
    kv_str(&s, "rtc", cfg->rtc_wall ? "wall" : "ticks", 0);
    put(&s, "},");

    /* ---- the models a picker may offer, straight from the CPU's own table so
     * the list cannot go stale ---- */
    put(&s, "\"cpuModels\":[");
    for (i = 0; i < CpuModel_Count(); i++)
    {
        put(&s, "%s\"%s\"", i ? "," : "", CpuModel_NameByIndex(i));
    }
    put(&s, "],");

    /* ---- what a machine COULD have: the registry itself ----
     *
     * Without this the form can only ever show controllers the file already
     * names, so a config that never mentioned Winchester could not grow one.
     * Emitted from the same g_descriptors table the parser and validator use,
     * via MC_DescriptorForType, so a type added there appears here for free. */
    put(&s, "\"controllerTypes\":[");
    {
        static const CtrlType kinds[] = {CTRL_FLOPPY, CTRL_SMD, CTRL_WINCHESTER, CTRL_SCSI,
                                         CTRL_HDLC};
        int k;
        int first = 1;
        for (k = 0; k < (int)(sizeof(kinds) / sizeof(kinds[0])); k++)
        {
            const ControllerDescriptor *d = MC_DescriptorForType(kinds[k]);
            if (!d)
            {
                continue;
            }
            put(&s, "%s{", first ? "" : ",");
            first = 0;
            kv_str(&s, "type", d->name, 1);
            put(&s, "\"minWheel\":%d,", d->min_wheel);
            put(&s, "\"maxWheel\":%d,", d->max_wheel);
            put(&s, "\"diskSlots\":%d,", d->disk_slots);
            put(&s, "\"isDisc\":%s,", d->is_disc ? "true" : "false");
            put(&s, "\"bootable\":%s", d->bootable ? "true" : "false");
            put(&s, "}");
        }
    }
    put(&s, "],");

    /* ---- controllers ---- */
    put(&s, "\"controllers\":[");
    for (i = 0; i < cfg->controllerCount; i++)
    {
        const McController *c = &cfg->controllers[i];
        const ControllerDescriptor *d = MC_DescriptorForType(c->type);
        put(&s, "%s{", i ? "," : "");
        kv_str(&s, "type", MC_CtrlTypeName(c->type), 1);
        put(&s, "\"wheel\":%d,", c->wheel);
        put(&s, "\"enabled\":%s,", c->enabled ? "true" : "false");
        put(&s, "\"isDisc\":%s,", (d && d->is_disc) ? "true" : "false");
        put(&s, "\"bootable\":%s,", (d && d->bootable) ? "true" : "false");
        put(&s, "\"diskSlots\":%d,", d ? d->disk_slots : 0);
        put(&s, "\"disks\":[");
        for (j = 0; j < MC_MAX_DISK_SLOTS; j++)
        {
            const McDiskSlot *k = &c->disks[j];
            put(&s, "%s{\"slot\":%d,\"present\":%s,", j ? "," : "", j,
                k->present ? "true" : "false");
            kv_str(&s, "media", media_name(k->media), 1);
            kv_str(&s, "image", k->image, 0);
            put(&s, "}");
        }
        put(&s, "],");
        /* HDLC settings ride along on every controller; the form shows them
         * only for an HDLC row, and reading them elsewhere is harmless. */
        kv_str(&s, "hdlcMode", c->hdlc_is_server ? "server" : "client", 1);
        kv_str(&s, "hdlcHost", c->hdlc_host, 1);
        put(&s, "\"hdlcPort\":%d", c->hdlc_port);
        put(&s, "}");
    }
    put(&s, "],");

    /* ---- terminals ---- */
    put(&s, "\"terminals\":[");
    for (i = 0; i < cfg->terminalCount; i++)
    {
        put(&s, "%s%d", i ? "," : "", cfg->terminals[i]);
    }
    put(&s, "],");

    /* ---- peripherals ---- */
    put(&s, "\"peripherals\":{");
    put(&s, "\"papertapeReader\":%s,", cfg->ptreader_enabled ? "true" : "false");
    put(&s, "\"papertapePunch\":%s,", cfg->ptpunch_enabled ? "true" : "false");
    put(&s, "\"linePrinter\":%s", cfg->lineprinter_enabled ? "true" : "false");
    put(&s, "},");

    /* ---- boot ---- */
    put(&s, "\"boot\":{");
    put(&s, "\"isDisc\":%s,", cfg->boot.is_disc ? "true" : "false");
    kv_str(&s, "type", MC_CtrlTypeName(cfg->boot.type), 1);
    put(&s, "\"wheel\":%d,", cfg->boot.wheel);
    put(&s, "\"unit\":%d,", cfg->boot.unit);
    kv_str(&s, "file", cfg->boot.file, 0);
    put(&s, "}");

    /* The ND-500. Always present as an object, so the form can ask "is there
     * one?" without a special case; `enabled` is what says whether this machine
     * actually has one. Disc slots appear only when they hold something - an
     * array of 16 mostly-empty strings is noise. */
    put(&s, ",\"nd500\":{");
    put(&s, "\"enabled\":%s,", cfg->nd500.enabled ? "true" : "false");
    put(&s, "\"memoryMb\":%d,", cfg->nd500.memory_mb);
    kv_str(&s, "kernel", cfg->nd500.kernel, 1);
    kv_str(&s, "pseg", cfg->nd500.pseg, 1);
    kv_str(&s, "dseg", cfg->nd500.dseg, 1);
    put(&s, "\"disks\":[");
    {
        int first = 1;
        for (int slot = 0; slot < MC_ND500_MAX_DISKS; slot++)
        {
            if (!cfg->nd500.disks[slot][0])
            {
                continue;
            }
            put(&s, "%s{\"slot\":%d,", first ? "" : ",", slot);
            kv_str(&s, "image", cfg->nd500.disks[slot], 1);
            put(&s, "\"writable\":%s}", cfg->nd500.disk_writable[slot] ? "true" : "false");
            first = 0;
        }
    }
    put(&s, "]}");

    put(&s, "}");

    if (s.overflow)
    {
        /* Say so in-band. The caller is JavaScript and an empty object is
         * something it can react to; a truncated one is not. */
        snprintf(out, outlen, "{\"error\":\"machine description did not fit\"}");
        return false;
    }
    return true;
}
