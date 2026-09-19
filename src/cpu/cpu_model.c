/*
 * cpu_model.c - CPU model names, in one place.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * These two tables were static functions inside the native CLI frontend
 * (cpu_type_from_name / cpu_type_display_name in src/frontend/nd100x/nd100x.c),
 * which meant --cputype could name any of the eleven models the emulator
 * actually implements while a machine-configuration file could only say 100,
 * 110 or 120. The CPU knew about the CX and CE variants; the config could not
 * ask for one.
 *
 * Kept in lock-step with the CpuType enum in cpu_types.h. Adding a model is two
 * rows here and one enum value there.
 */
#include "cpu_types.h"
#include "cpu_model.h"

#include <string.h>
#ifdef _WIN32
#define nd_strcasecmp _stricmp
#else
#include <strings.h>
#define nd_strcasecmp strcasecmp
#endif

// clang-format off
static const struct { const char *name; CpuType type; } g_models[] = {
    {"ND1",      ND1},
    {"ND4",      ND4},
    {"ND10",     ND10},
    {"ND100",    ND100},
    {"ND100CE",  ND100CE},
    {"ND100CX",  ND100CX},
    {"ND110",    ND110},
    {"ND110CE",  ND110CE},
    {"ND110CX",  ND110CX},
    {"ND110PCX", ND110PCX},
    {"ND120CX",  ND120CX},
};
// clang-format on

#define MODEL_COUNT ((int)(sizeof(g_models) / sizeof(g_models[0])))

bool CpuModel_FromName(const char *name, CpuType *out)
{
    if (!name)
    {
        return false;
    }
    for (int i = 0; i < MODEL_COUNT; i++)
    {
        if (nd_strcasecmp(name, g_models[i].name) == 0)
        {
            if (out)
            {
                *out = g_models[i].type;
            }
            return true;
        }
    }
    return false;
}

const char *CpuModel_Name(CpuType t)
{
    for (int i = 0; i < MODEL_COUNT; i++)
    {
        if (g_models[i].type == t)
        {
            return g_models[i].name;
        }
    }
    return "ND?";
}

/* The "ND-nnn/xx" form TPE and CONFIGURATION print, so a boot-time line matches
 * what the guest itself reports. Deliberately separate from CpuModel_Name():
 * that one round-trips through the config file and must stay exactly what the
 * parser accepts, hyphens and all would break it. */
const char *CpuModel_DisplayName(CpuType t)
{
    switch (t)
    {
    case ND1:
        return "ND-1";
    case ND4:
        return "ND-4";
    case ND10:
        return "ND-10";
    case ND100:
        return "ND-100";
    case ND100CE:
        return "ND-100/CE";
    case ND100CX:
        return "ND-100/CX";
    case ND110:
        return "ND-110";
    case ND110CE:
        return "ND-110/CE";
    case ND110CX:
        return "ND-110/CX";
    case ND110PCX:
        return "ND-110/PCX";
    case ND120CX:
        return "ND-120/CX";
    default:
        return "ND?";
    }
}

int CpuModel_Count(void)
{
    return MODEL_COUNT;
}

const char *CpuModel_NameByIndex(int i)
{
    if (i < 0 || i >= MODEL_COUNT)
    {
        return NULL;
    }
    return g_models[i].name;
}
