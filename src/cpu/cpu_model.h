/*
 * cpu_model.h - CPU model names.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * The emulator implements eleven CPU models (cpu_types.h, CpuType). Until now
 * only the CLI could name them: the lookup and display tables were static
 * functions inside src/frontend/nd100x/nd100x.c, so a machine-configuration
 * file was stuck with 100, 110 or 120 and could not ask for a CX or CE variant
 * the CPU already knew how to be.
 *
 * One table, three callers: --cputype, the machine config, and any UI that
 * wants to offer the list (CpuModel_Count / CpuModel_NameByIndex exist for
 * exactly that - so a picker cannot drift out of step with the enum).
 */
#ifndef CPU_MODEL_H
#define CPU_MODEL_H

#include <stdbool.h>
#include "cpu_types.h"

/* Case-insensitive name -> CpuType. True on a match. */
bool CpuModel_FromName(const char *name, CpuType *out);

/* CpuType -> the canonical config-file spelling ("ND110CX"). This is what the
 * parser accepts, so it round-trips; "ND?" for an unknown value. */
const char *CpuModel_Name(CpuType t);

/* CpuType -> the human form TPE and CONFIGURATION print ("ND-110/CX"). For
 * display only - it does NOT round-trip through the config parser. */
const char *CpuModel_DisplayName(CpuType t);

/* The list, for building a picker. */
int CpuModel_Count(void);
const char *CpuModel_NameByIndex(int i);

#endif /* CPU_MODEL_H */
