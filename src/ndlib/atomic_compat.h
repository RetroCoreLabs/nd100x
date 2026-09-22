/*
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2026 Ronny Hansen
 *
 * atomic_compat.h - cross-platform shim for the few atomic flags the CPU
 * shares with the debugger thread.
 *
 * The CPU runs on one thread and the DAP server on another. Five values
 * cross between them: the run mode, the stop reason, the pause request and
 * the control-granted flag, plus whatever a future subsystem needs. They are
 * written by one thread and read by the other, so each access has to be a
 * single indivisible operation - not because the value is large, but because
 * a torn or cached read makes the debugger act on a state the CPU has already
 * left.
 *
 * Three targets provide that in three different ways:
 *
 *   - POSIX: C11 <stdatomic.h>, atomic_store() and atomic_load().
 *   - Windows: MSVC has no <stdatomic.h> in C mode, so the Interlocked
 *     family is used. A load is an InterlockedCompareExchange with both
 *     arguments zero, which returns the current value and writes nothing.
 *   - WebAssembly: single threaded. There is no second thread to race with,
 *     so a plain load or store is correct and an atomic one would only cost
 *     instructions.
 *
 * Callers use the nd_atomic_* types and functions and never test the platform
 * themselves. Everything here is a static inline function rather than a macro
 * so the argument is type checked and evaluated once.
 *
 * The stored type is int in all three builds. An enum or a bool is converted
 * on the way in and back on the way out, which keeps one set of functions
 * instead of one per value type.
 */

#ifndef ATOMIC_COMPAT_H
#define ATOMIC_COMPAT_H

#include <stdbool.h>

#if defined(__EMSCRIPTEN__)

/* Single threaded: nothing can observe a partial write. */
typedef int NdAtomicInt;

static inline void nd_atomic_store(NdAtomicInt *slot, int value)
{
    *slot = value;
}

static inline int nd_atomic_load(NdAtomicInt *slot)
{
    return *slot;
}

#elif defined(_WIN32) || defined(_WIN64)

#include <windows.h>

typedef volatile LONG NdAtomicInt;

static inline void nd_atomic_store(NdAtomicInt *slot, int value)
{
    (void)InterlockedExchange(slot, (LONG)value);
}

static inline int nd_atomic_load(NdAtomicInt *slot)
{
    /* Exchange value and comparand are both 0: the comparison fails against
     * any non-zero value and succeeds by writing 0 over 0, so the slot keeps
     * whatever it held and the current value comes back. */
    return (int)InterlockedCompareExchange(slot, 0, 0);
}

#else

#include <stdatomic.h>

typedef atomic_int NdAtomicInt;

static inline void nd_atomic_store(NdAtomicInt *slot, int value)
{
    atomic_store(slot, value);
}

static inline int nd_atomic_load(NdAtomicInt *slot)
{
    return atomic_load(slot);
}

#endif

/// @brief Store a boolean in an atomic slot.
static inline void nd_atomic_store_bool(NdAtomicInt *slot, bool value)
{
    nd_atomic_store(slot, value ? 1 : 0);
}

/// @brief Read a boolean from an atomic slot.
static inline bool nd_atomic_load_bool(NdAtomicInt *slot)
{
    return nd_atomic_load(slot) != 0;
}

#endif /* ATOMIC_COMPAT_H */
