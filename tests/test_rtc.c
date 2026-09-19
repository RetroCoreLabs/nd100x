/*
 * test_rtc.c - Unit tests for the RTC ticks and wall-clock time bases.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2026 Ronny Hansen
 *
 * Unit tests for the RTC time base in src/devices/rtc/device_rtc.c:
 *
 *   - ticks mode (default): exactly one clock pulse per 10550 RTC_Tick calls
 *   - wall mode: pulses follow host monotonic time at 20 ms (50 Hz),
 *     independent of how fast RTC_Tick is called
 *
 * device_rtc.c is linked in DIRECTLY; the four Device_* helpers it uses are
 * stubbed below so the device manager does not have to be linked in.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "../src/devices/devices_types.h"
#include "../src/devices/rtc/device_rtc.h"

/* From device_rtc.c (normally declared in the generated devices_protos.h). */
Device *CreateRTCDevice(uint8_t thumbwheel);
void RTC_SetWallClockMode(bool enable);

/* ---- Device_* stubs (device_rtc.c uses exactly these four) ---- */
/* Prototypes match src/devices/device.c (devices_protos.h). */
void Device_Init(Device *dev, uint8_t thumbwheel, DeviceClass deviceClass, size_t blockSize);
void Device_TickIODelay(Device *dev);
void Device_SetInterruptStatus(Device *dev, bool active, uint16_t level);
uint32_t Device_RegisterAddress(Device *dev, uint32_t address);

void Device_Init(Device *dev, uint8_t thumbwheel, DeviceClass deviceClass, size_t blockSize)
{
    memset(dev, 0, sizeof(*dev));
    (void)thumbwheel;
    (void)deviceClass;
    (void)blockSize;
}

void Device_TickIODelay(Device *dev)
{
    (void)dev;
}

void Device_SetInterruptStatus(Device *dev, bool active, uint16_t level)
{
    if (active)
    {
        dev->interruptBits |= (uint16_t)(1u << level);
    }
    else
    {
        dev->interruptBits &= (uint16_t)~(1u << level);
    }
}

uint32_t Device_RegisterAddress(Device *dev, uint32_t address)
{
    return address - dev->startAddress;
}

/* ---- helpers ---- */
static int rtc_total;
static int rtc_failed;

static void rtc_check(const char *name, long exp, long got)
{
    rtc_total++;
    if (exp != got)
    {
        printf("  FAIL  %-44s expected %ld, got %ld\n", name, exp, got);
        rtc_failed++;
    }
}

static void rtc_check_range(const char *name, long lo, long hi, long got)
{
    rtc_total++;
    if (got < lo || got > hi)
    {
        printf("  FAIL  %-44s expected %ld..%ld, got %ld\n", name, lo, hi, got);
        rtc_failed++;
    }
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

#define TICKS_20MS 10550 /* must match device_rtc.c */

/* Count pulses (rising edges of readyForTransfer) over n RTC_Tick calls,
 * clearing the flag after each detected pulse. */
static long pulses_over_ticks(Device *rtc, RTCData *data, long n)
{
    long pulses = 0;
    for (long i = 0; i < n; i++)
    {
        rtc->Tick(rtc);
        if (data->statusRegister.bits.readyForTransfer)
        {
            pulses++;
            data->statusRegister.bits.readyForTransfer = false;
        }
    }
    return pulses;
}

/* Pulses in 1 s of host time, ticking as fast as possible. */
static int64_t wall_pulses_tight(Device *rtc, RTCData *data)
{
    long pulses = 0;
    uint64_t start = now_ns();
    while (now_ns() - start < 1000000000ULL)
    {
        rtc->Tick(rtc);
        if (data->statusRegister.bits.readyForTransfer)
        {
            pulses++;
            data->statusRegister.bits.readyForTransfer = false;
        }
    }
    return pulses;
}

/* Pulses in 1 s of host time at ~200 calls/s (5 ms sleeps). */
static int64_t wall_pulses_slow(Device *rtc, RTCData *data)
{
    long pulses = 0;
    uint64_t start = now_ns();
    struct timespec nap = {0, 5000000}; /* 5 ms */
    while (now_ns() - start < 1000000000ULL)
    {
        rtc->Tick(rtc);
        if (data->statusRegister.bits.readyForTransfer)
        {
            pulses++;
            data->statusRegister.bits.readyForTransfer = false;
        }
        nanosleep(&nap, NULL);
    }
    return pulses;
}

/* Pulses in 1 s of host time when a slow guest handler issues clear-counter ~10 ms after each pulse. */
static int64_t wall_pulses_slow_handler(Device *rtc, RTCData *data)
{
    long pulses = 0;
    uint64_t start = now_ns();
    struct timespec lat = {0, 10000000}; /* 10 ms service latency */
    while (now_ns() - start < 1000000000ULL)
    {
        rtc->Tick(rtc);
        if (data->statusRegister.bits.readyForTransfer)
        {
            pulses++;
            data->statusRegister.bits.readyForTransfer = false;
            nanosleep(&lat, NULL);                     /* slow guest handler */
            rtc->Write(rtc, rtc->startAddress + 1, 0); /* IOX clear counter */
        }
    }
    return pulses;
}

int main(void)
{
    Device *rtc = CreateRTCDevice(0);
    if (!rtc)
    {
        printf("CreateRTCDevice failed\n");
        return 1;
    }
    RTCData *data = (RTCData *)rtc->deviceData;
    rtc->Reset(rtc);

    /* ---- ticks mode (default): one pulse per 10550 calls, deterministic ----
     * After Reset the counter is 0, so the first call pulses immediately and
     * reloads the counter; from then on it is exactly one pulse per 10550. */
    rtc_check("ticks: immediate pulse on first call", 1, pulses_over_ticks(rtc, data, 1));
    rtc_check("ticks: no pulse for next 10549 calls", 0,
              pulses_over_ticks(rtc, data, TICKS_20MS - 1));
    rtc_check("ticks: pulse on the 10550th call after", 1, pulses_over_ticks(rtc, data, 1));
    rtc_check("ticks: exactly 10 pulses in 105500 calls", 10,
              pulses_over_ticks(rtc, data, 10L * TICKS_20MS));

    /* ---- wall mode: ~50 Hz host time, independent of call rate ---- */
    RTC_SetWallClockMode(true);
    rtc->Reset(rtc);

    /* Spin for 1.0 s of host time; expect ~50 pulses (20 ms period).
     * The band is generous to tolerate scheduler jitter on loaded hosts. */
    rtc_check_range("wall: ~50 pulses in 1 s (tight spin)", 40, 52, wall_pulses_tight(rtc, data));

    /* Same measurement at a artificially slow call rate (~200 calls/s via
     * 5 ms sleeps): pulse count must stay ~50/s, NOT drop with the call rate.
     * This is the 10x-slow-clock regression: in ticks mode 200 calls/s would
     * give 0 pulses here. */
    rtc_check_range("wall: ~50 pulses in 1 s (slow call rate)", 30, 52,
                    wall_pulses_slow(rtc, data));

    /* The guest's clock handler hits IOX clear-counter (and IDENT restarts
     * the countdown) after EVERY pulse. In wall mode those must NOT move the
     * free-running 20 ms phase - re-arming "now + 20 ms" from there makes the
     * period 20 ms plus the guest's service latency (measured 9.5 Hz instead
     * of 50 Hz on a debugger-loaded TSS boot). Simulate a handler that takes
     * ~10 ms to issue clear-counter after each pulse: the rate must stay ~50/s,
     * not drop to ~33/s (30 ms effective period). */
    rtc_check_range("wall: ~50/s despite 10 ms clear-counter latency", 40, 52,
                    wall_pulses_slow_handler(rtc, data));

    /* Back to ticks mode: behavior must return to the deterministic count. */
    RTC_SetWallClockMode(false);
    rtc->Reset(rtc);
    rtc_check("ticks again: 5 pulses in 52750 calls", 5,
              pulses_over_ticks(rtc, data, 5L * TICKS_20MS));

    if (rtc->Destroy)
    {
        rtc->Destroy(rtc);
    }
    free(rtc->deviceData);
    free(rtc);

    printf("rtc tests: %d checks, %d failed\n", rtc_total, rtc_failed);
    return rtc_failed ? 1 : 0;
}
