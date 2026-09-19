/*
 * machine_config_json.h - a resolved MachineConfig as JSON.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * For the Machine Setup form: it has to SHOW what a configuration says, and the
 * one way it must not find out is by parsing the INI itself. A second parser
 * drifts from the first, and then the form shows one machine while the emulator
 * builds another.
 *
 * So the browser reads through this - the same MachineConfig_LoadFile the
 * native binary uses, handed back as JSON - and writes by generating INI text
 * that goes straight back through MachineConfig_Validate. Neither direction
 * gets a second opinion about what an .ini means.
 */
#ifndef MACHINE_CONFIG_JSON_H
#define MACHINE_CONFIG_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include "machine_config.h"

/* Write <cfg> into <out> as a JSON object. Returns true on success; on
 * overflow returns false AND leaves {"error":"..."} in the buffer, so a caller
 * that ignores the return value still gets something parseable rather than a
 * truncated object. 8 KB is comfortable for any real machine. */
bool MachineConfig_ToJson(const MachineConfig *cfg, char *out, size_t outlen);

#endif /* MACHINE_CONFIG_JSON_H */
