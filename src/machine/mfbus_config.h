/*
 * mfbus_config.h - build the multifunction bus from a parsed .ini
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * See mfbus_config.c. Separate from mfbus_bridge.h on purpose: BOTH repositories
 * have a machine_types.h and both guard it with #ifndef MACHINE_TYPES_H, so a
 * translation unit that includes nd100x's machine_config.h can no longer see
 * nd500x's Nd500Machine - the second header is silently skipped and the build
 * fails inside nd500x's cpu_protos.h, reading like a missing library rather than
 * a guard collision.
 *
 * So the two header worlds are kept in separate translation units: mfbus_bridge.c
 * sees nd500x's, this file sees nd100x's, and neither sees both.
 */

#ifndef MFBUS_CONFIG_H
#define MFBUS_CONFIG_H

#include <stdbool.h>

#include "machine_config.h"

#ifdef ND100X_WITH_ND500

/**
 * @brief Build the whole multifunction bus from a parsed configuration.
 *
 * The pool FIRST, because both the card and the stations depend on it - an
 * ND-5000 with no shared memory has nowhere to execute - then the ND-100's
 * octobus card connected to the bus, then one station per enabled [nd5000.N]
 * with a CPU and its kernel if one is named.
 *
 * @param mc The parsed configuration. Nothing is built when it has no [mfbus].
 * @return true when a pool was attached; false when there is nothing to build or
 *         the pool could not be attached. Per-CPU failures are logged and
 *         skipped rather than abandoning the whole bus, so one bad station does
 *         not cost the others.
 */
bool mfbus_apply_config(const MachineConfig *mc);

#endif /* ND100X_WITH_ND500 */

#endif /* MFBUS_CONFIG_H */
