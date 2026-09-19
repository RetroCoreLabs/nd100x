/*
 * nd100x - ND100 Virtual Machine
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * This file is originated from the nd100x project and the RetroCore project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the nd100em
 * distribution in the file COPYING); if not, see <http://www.gnu.org/licenses/>.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <time.h>
#endif

#include "../devices_types.h"
#include "../devices_protos.h"

#include "device_rtc.h"

#define TICKS_20MS 10550 // Ticks for 20ms timer (real-time at 0.5275 MIPS, the --throttle default)

// RTC time base. Default (false) counts instruction ticks: one clock pulse per
// TICKS_20MS calls to RTC_Tick, so the clock runs in emulated instruction time
// and its wall rate follows the effective instruction rate. When enabled via
// [machine] rtc = wall, the pulse fires every 20 ms of host monotonic time
// instead, giving a real-time 50 Hz clock regardless of emulation speed.
#define RTC_WALL_PERIOD_NS 20000000ULL /* 20 ms */
static bool rtcWallClockMode = false;

void RTC_SetWallClockMode(bool enable)
{
    rtcWallClockMode = enable;
}

static uint64_t rtc_now_ns(void)
{
#if defined(_WIN32) || defined(_WIN64)
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0)
    {
        QueryPerformanceFrequency(&freq);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (uint64_t)(now.QuadPart * 1000000000ULL / freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

static void RTC_Reset(Device *self)
{
    RTCData *data = (RTCData *)self->deviceData;
    if (!data)
    {
        return;
    }

    // Clear all registers and status
    data->rtcCounter = 0;
    data->divisionNumberN = TICKS_20MS;
    data->register1 = 0;

    data->statusRegister.raw = 0;
    data->controlRegister.raw = 0;
    data->nextPulseNs = 0;
}

static void RTC_ClearClockTicks(Device *self)
{
    RTCData *data = (RTCData *)self->deviceData;
    if (!data)
    {
        return;
    }

    data->rtcCounter = data->divisionNumberN;
    /* Wall-clock mode: deliberately do NOT touch nextPulseNs here. This is
     * called on IDENT and on the IOX clear-counter/restart writes the guest's
     * clock handler issues every tick; re-arming "now + 20 ms" from here makes
     * the period 20 ms PLUS the guest's service latency, which wrecks the rate
     * whenever emulation runs slower than real time (measured: 9.5 Hz instead
     * of 50 Hz on a debugger-loaded TSS boot). The wall-mode pulse train free-
     * runs in RTC_Tick instead. This knowingly deviates from the documented
     * IOX 011 "next pulse exactly 20 ms later" phase reset - wall mode trades
     * that fidelity for a clock that keeps real time. */
}

static uint16_t RTC_Tick(Device *self)
{
    if (!self)
    {
        return 0;
    }

    RTCData *data = (RTCData *)self->deviceData;
    if (!data)
    {
        return 0;
    }

    // Process I/O delays
    Device_TickIODelay(self);

    // Count down the timer
    data->rtcCounter--;

    if (rtcWallClockMode)
    {
        // Wall-clock mode: the counter keeps running for data-register readers,
        // but the pulse fires on host time, not on the countdown.
        if (data->rtcCounter <= 0)
        {
            data->rtcCounter = data->divisionNumberN;
        }

        uint64_t now = rtc_now_ns();
        if (data->nextPulseNs == 0)
        {
            data->nextPulseNs = now + RTC_WALL_PERIOD_NS;
        }

        if (now >= data->nextPulseNs)
        {
            data->statusRegister.bits.readyForTransfer = true;
            if (data->statusRegister.bits.interruptEnabled)
            {
                Device_SetInterruptStatus(self, true, self->interruptLevel);
            }
            RTC_ClearClockTicks(self); // reload the countdown register only

            /* Free-running phase: advance by exactly one period so guest
             * service latency never stretches the train. If we have fallen
             * more than 10 periods (200 ms) behind - host stall, or a guest
             * too slow to service 50 Hz - drop the backlog and resync; a
             * short stall is caught up (back-to-back pulses as the guest
             * services them), a chronic one cannot queue unbounded. */
            data->nextPulseNs += RTC_WALL_PERIOD_NS;
            if ((int64_t)(now - data->nextPulseNs) > (int64_t)(10 * RTC_WALL_PERIOD_NS))
            {
                data->nextPulseNs = now + RTC_WALL_PERIOD_NS;
            }
        }
    }
    else if (data->rtcCounter <= 0)
    {
        data->statusRegister.bits.readyForTransfer = true;
        if (data->statusRegister.bits.interruptEnabled)
        {
            Device_SetInterruptStatus(self, true, self->interruptLevel);
        }
        RTC_ClearClockTicks(self);
    }

    return self->interruptBits;
}

static uint16_t RTC_Read(Device *self, uint32_t address)
{
    if (!self)
    {
        return 0;
    }

    RTCData *data = (RTCData *)self->deviceData;
    uint16_t value = 0;
    uint32_t reg = Device_RegisterAddress(self, address);

    switch (reg)
    {
    case RTC_READ_DATA_REGISTER:
        value = (uint16_t)data->rtcCounter;
        break;

    case RTC_READ_STATUS:
        value = data->statusRegister.raw;
        break;

    default:
        break;
    }

    if (Log_IsEnabled(LOG_CAT_RTC, LOG_DEBUG))
    {
        Log_Write(LOG_CAT_RTC, LOG_DEBUG, "RTC Reading from address: %o value: %o\n", address,
                  value);
    }


    return value;
}

static void RTC_Write(Device *self, uint32_t address, uint16_t value)
{
    if (!self)
    {
        return;
    }

    RTCData *data = (RTCData *)self->deviceData;
    uint32_t reg = Device_RegisterAddress(self, address);

    if (Log_IsEnabled(LOG_CAT_RTC, LOG_DEBUG))
    {
        Log_Write(LOG_CAT_RTC, LOG_DEBUG, "RTC Writing value: %o to address: %o\n", value, address);
    }

    switch (reg)
    {
    case RTC_CLEAR_COUNTER:
        RTC_ClearClockTicks(self);
        data->statusRegister.bits.readyForTransfer = false;
        Device_SetInterruptStatus(self, false, self->interruptLevel);
        break;

    case RTC_WRITE_CONTROL:
        data->controlRegister.raw = value;

        // Update status register
        data->statusRegister.bits.interruptEnabled = data->controlRegister.bits.interruptEnabled;

        // Handle interrupt enable/disable
        if (!data->statusRegister.bits.interruptEnabled)
        {
            Device_SetInterruptStatus(self, false, self->interruptLevel);
        }


        // Clear ready for transfer if requested and clear interrupt bit 13 (Needed for testprogram TPE Monitor version B)
        if (data->controlRegister.bits.clearReadyForTransfer)
        {
            data->statusRegister.bits.readyForTransfer = 0;
            self->interruptBits &= ~(1 << 13);
        }

        // Clear external hold signal if requested
        if (data->controlRegister.bits.clearExternalHold)
        {
            data->statusRegister.bits.externalHoldPulse = 0;
        }

        // Restart clock if requested
        if (data->controlRegister.bits.restartClock)
        {
            RTC_ClearClockTicks(self); // reset countdown (and re-arm wall-clock pulse)
            data->clockCountingStarted = true;
        }
        break;

    default:
        break;
    }
}

static uint16_t RTC_Ident(Device *self, uint16_t level)
{
    if (!self)
    {
        return 0;
    }

    RTCData *data = (RTCData *)self->deviceData;
    if (!data)
    {
        return 0;
    }

    if ((self->interruptBits & (1 << level)) != 0)
    {
        RTC_ClearClockTicks(self);
        data->statusRegister.bits.interruptEnabled = false;

        if (Log_IsEnabled(LOG_CAT_RTC, LOG_DEBUG))
        {
            Log_Write(LOG_CAT_RTC, LOG_DEBUG, "RTC_Ident: %d\n", self->identCode);
        }
        Device_SetInterruptStatus(self, false, level);
        return self->identCode;
    }
    /* The branch above returns, so reaching here means the level was not set. */
    LOG(LOG_CAT_RTC, LOG_DEBUG, "RTC_Ident: interrupt not set");
    return 0;
}

Device *CreateRTCDevice(uint8_t thumbwheel)
{
    Device *dev = malloc(sizeof(Device));
    if (!dev)
    {
        return NULL;
    }

    RTCData *data = malloc(sizeof(RTCData));
    if (!data)
    {
        free(dev);
        return NULL;
    }

    // Initialize device base structure
    Device_Init(dev, thumbwheel, DEVICE_CLASS_RTC, 0);

    // Set up device-specific data
    memset(data, 0, sizeof(RTCData));

    // Set up device properties based on thumbwheel
    switch (thumbwheel)
    {
    case 0:
        dev->identCode = 01;
        dev->startAddress = 010;
        dev->endAddress = 013;
        dev->interruptLevel = 13;
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "RTC 1");
        break;
    case 1:
        dev->identCode = 02;
        dev->startAddress = 014;
        dev->endAddress = 017;
        dev->interruptLevel = 13;
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "RTC 2");
        break;
    case 2:
        dev->identCode = 06;
        dev->startAddress = 020;
        dev->endAddress = 023;
        dev->interruptLevel = 13;
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "RTC 3");
        break;
    default:
        LOG(LOG_CAT_RTC, LOG_WARN, "Unexpected thumbwheel code %d\n", thumbwheel);
        free(data);
        free(dev);
        return NULL;
    }

    // Set up device function pointers
    dev->Reset = RTC_Reset;
    dev->Tick = RTC_Tick;
    dev->Read = RTC_Read;
    dev->Write = RTC_Write;
    dev->Ident = RTC_Ident;
    dev->deviceData = data;

    LOG(LOG_CAT_RTC, LOG_INFO, "RTC device created: %s\n", dev->memoryName);
    return dev;
}
