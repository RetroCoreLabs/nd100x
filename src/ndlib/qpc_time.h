/*
 * qpc_time.h - QueryPerformanceCounter ticks to nanoseconds, overflow-free.
 *
 * The old conversion  ticks * 1000000000 / freq  wraps 64 bits every
 * 2^64 / 1e9 = 18446744073 ticks (1844.67 s at the usual 10 MHz QPC
 * frequency). The returned time then jumped backwards, the wall-clock RTC
 * next-pulse deadline stayed in the future, and the 50 Hz interrupt stopped,
 * which froze the SINTRAN clock and every timeout (reported 16-OCT-2026).
 *
 * Splitting into whole seconds and a remainder keeps every intermediate
 * value inside 64 bits as long as frequency < 18446744073 Hz, which holds
 * for every real QPC source. frequency must be nonzero.
 *
 * Header-only on purpose: used by both src/cpu/cpu.c and
 * src/devices/rtc/device_rtc.c, and by tests/test_qpc_time.c on every host.
 */
#ifndef QPC_TIME_H
#define QPC_TIME_H

#include <stdint.h>

static inline uint64_t qpc_ticks_to_ns(uint64_t ticks, uint64_t frequency)
{
    uint64_t seconds = ticks / frequency;
    uint64_t remainder = ticks % frequency;
    return seconds * UINT64_C(1000000000) +
           remainder * UINT64_C(1000000000) / frequency;
}

#endif /* QPC_TIME_H */
