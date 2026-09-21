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

/**
 * @brief Look up a CPU model name case-insensitively in the model table and return its CpuType.
 * @param name Model spelling to look up, for example "ND110CX"; NULL is rejected.
 * @param out Receives the matching CpuType; may be NULL if only the match result is wanted.
 * @return true on a match (and *out written when out is non-NULL), false for NULL or unknown name.
 */
bool cpumodel_from_name(const char *name, CpuType *out);

/* CpuType -> the canonical config-file spelling ("ND110CX"). This is what the
 * parser accepts, so it round-trips; "ND?" for an unknown value. */
/**
 * @brief Return the canonical config-file spelling of a CpuType, for example "ND110CX".
 * @param t CPU model to name.
 * @return Pointer to a static string; "ND?" when the value is not in the model table.
 */
const char *cpumodel_name(CpuType t);

/* CpuType -> the human form TPE and CONFIGURATION print ("ND-110/CX"). For
 * display only - it does NOT round-trip through the config parser. */
/**
 * @brief Return the display form TPE and CONFIGURATION print for a CpuType, e.g. "ND-110/CX".
 * @param t CPU model to name.
 * @return Pointer to a static string; "ND?" for a CpuType the switch does not cover.
 */
const char *cpumodel_display_name(CpuType t);

/* The list, for building a picker. */
/**
 * @brief Return the number of entries in the CPU model table.
 * @return Count of models, used to bound CpuModel_NameByIndex().
 */
int cpumodel_count(void);

/**
 * @brief Return the config-file name of the model table entry at an index, for building a picker.
 * @param i Zero-based index into the model table.
 * @return Pointer to a static name string, or NULL when i is negative or past the last entry.
 */
const char *cpumodel_name_by_index(int i);

#endif /* CPU_MODEL_H */
