/*
 * nd100x - ND-100 emulator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * scsi_bus.c - SCSI bus (wire-OR of control and data lines)
 *
 * Ported from RetroCore:
 *   <RetroCore>\Emulated.HW\Common\SCSI\SCSIBus.cs
 * which is a port of MAME's nscsi_bus.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "../devices_types.h"
#include "../devices_protos.h"

static const char *scsi_phase_description[8] = {"DATA OUT", "DATA IN", "COMMAND",     "STATUS",
                                                "*",        "*",       "MESSAGE OUT", "MESSAGE IN"};


const char *SCSIBus_PhaseName(uint32_t phase)
{
    return scsi_phase_description[phase & S_PHASE_MASK];
}


void SCSIBus_Init(SCSIBus *bus)
{
    if (!bus)
    {
        return;
    }
    memset(bus, 0, sizeof(SCSIBus));
}


int SCSIBus_AddDevice(SCSIBus *bus, SCSIDevice *dev)
{
    if (!bus || !dev)
    {
        return -1;
    }
    if (bus->devCnt >= SCSI_BUS_MAX_DEVICES)
    {
        return -1;
    }

    int id = bus->devCnt;
    bus->devices[id].dev = dev;
    bus->devices[id].ctrl = 0;
    bus->devices[id].wait_ctrl = 0;
    bus->devices[id].data = 0;
    bus->devCnt++;

    dev->bus = bus;
    dev->refid = id;
    return id;
}


void SCSIBus_Clock(SCSIBus *bus)
{
    if (!bus)
    {
        return;
    }

    for (int i = 0; i < bus->devCnt; i++)
    {
        SCSIDevice *dev = bus->devices[i].dev;
        if (dev && dev->Clock)
        {
            dev->Clock(dev);
        }
    }
}


/* Recompute the OR of every device's data lines. */
static void scsi_bus_regen_data(SCSIBus *bus)
{
    uint8_t data = 0;
    for (int i = 0; i < bus->devCnt; i++)
    {
        data |= bus->devices[i].data;
    }
    bus->data = data;
}


/*
 * Recompute the OR of every device's control lines and notify the devices that
 * care. refid is the device that caused the change - it is NOT notified about
 * its own write (matching SCSIBus.cs regen_ctrl).
 */
static void scsi_bus_regen_ctrl(SCSIBus *bus, int refid)
{
    uint32_t octrl = bus->ctrl;
    uint32_t ctrl = 0;

    for (int i = 0; i < bus->devCnt; i++)
    {
        ctrl |= bus->devices[i].ctrl;
    }
    bus->ctrl = ctrl;

    uint32_t signal_bits_changed = octrl ^ ctrl;
    if (signal_bits_changed == 0)
    {
        return;
    }

    for (int i = 0; i < bus->devCnt; i++)
    {
        if (i == refid)
        {
            continue;
        }
        if ((bus->devices[i].wait_ctrl & signal_bits_changed) == 0)
        {
            continue;
        }

        SCSIDevice *dev = bus->devices[i].dev;
        if (dev && dev->ctrl_changed)
        {
            dev->ctrl_changed(dev);
        }
    }
}


uint32_t SCSIBus_ControlRead(SCSIBus *bus)
{
    if (!bus)
    {
        return 0;
    }
    return bus->ctrl;
}


/* Register which control lines this device wants scsi_ctrl_changed() for. */
void SCSIBus_ControlWait(SCSIBus *bus, int refid, uint32_t lines, uint32_t mask)
{
    if (!bus || refid < 0 || refid >= SCSI_BUS_MAX_DEVICES)
    {
        return;
    }
    if (!bus->devices[refid].dev)
    {
        return;
    }

    uint32_t w = bus->devices[refid].wait_ctrl;
    bus->devices[refid].wait_ctrl = (w & ~mask) | (lines & mask);
}


void SCSIBus_ControlWrite(SCSIBus *bus, int refid, uint32_t lines, uint32_t mask)
{
    if (!bus || refid < 0 || refid >= SCSI_BUS_MAX_DEVICES)
    {
        return;
    }

    if (bus->devices[refid].dev)
    {
        uint32_t ctrl = bus->devices[refid].ctrl;
        bus->devices[refid].ctrl = (ctrl & ~mask) | (lines & mask);
    }

    /* NOTE: RegenCtrl runs even when the slot is empty - SCSIBus.cs calls
     * regen_ctrl(scsi_refid) outside the null check. Kept identical. */
    scsi_bus_regen_ctrl(bus, refid);
}


uint8_t SCSIBus_DataRead(SCSIBus *bus)
{
    if (!bus)
    {
        return 0;
    }
    return bus->data;
}


void SCSIBus_DataWrite(SCSIBus *bus, int refid, uint8_t data)
{
    if (!bus || refid < 0 || refid >= SCSI_BUS_MAX_DEVICES)
    {
        return;
    }
    if (!bus->devices[refid].dev)
    {
        return;
    }

    bus->devices[refid].data = data;
    scsi_bus_regen_data(bus);
}
