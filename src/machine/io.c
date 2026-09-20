/*
 * io.c - I/O dispatch: IOX read/write/ident to devices and device ticks.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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


#include "machine_types.h"
#include "machine_protos.h"

#include "../devices/devices_types.h"
#include "../devices/devices_protos.h"


// Returns 0, or -1 if the device manager could not be set up.
int IO_Init(void)
{
    if (DeviceManager_Init() != 0)
    {
        return -1;
    }
    DeviceManager_AddAllDevices();
    return 0;
}

void IO_Destroy(void)
{
    DeviceManager_Destroy();
}

static uint16_t io_read(uint32_t address)
{
    return DeviceManager_Read(address);
}

static void io_write(uint32_t address, uint16_t value)
{
    DeviceManager_Write(address, value);
}

int IO_Ident(uint16_t level)
{
    return DeviceManager_Ident(level);
}

void IO_Tick(void)
{
    // Tick all devices, and check for interrupts
    uint16_t interrupt_bits = DeviceManager_Tick();

    if (interrupt_bits)
    {
        device_interrupt(interrupt_bits);
    }
}


uint16_t io_op(uint16_t ioadd, uint16_t reg_a)
{
    // Even addresses are read operations, odd addresses are write operations
    if (ioadd & 1)
    {
        // Odd address - write operation

        // HACK!! needed for RISC-V version (if not the emulator just ignores output and input)
        io_write(ioadd, reg_a);
        return reg_a;
    }
    else
    {
        // Even address - read operation
        uint16_t val = io_read(ioadd);
        return val;
    }
}
