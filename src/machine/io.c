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



#include "machine_types.h"
#include "machine_protos.h"

#include "../devices/devices_types.h"
#include "../devices/devices_protos.h"

// Returns 0, or -1 if the device manager could not be set up.
int IO_Init(void)
{
    if (DeviceManager_Init() != 0)
        return -1;
    DeviceManager_AddAllDevices();
    return 0;
}

void IO_Destroy(void)
{
    DeviceManager_Destroy();
}

uint16_t IO_Read(uint32_t address)
{
    return DeviceManager_Read(address);
}

void IO_Write(uint32_t address, uint16_t value)
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
    uint16_t interruptBits = DeviceManager_Tick();

    if (interruptBits)
        device_interrupt(interruptBits);
}


uint16_t io_op(uint16_t ioadd, uint16_t regA)
{
    // Even addresses are read operations, odd addresses are write operations
    if (ioadd & 1)
    {
        // Odd address - write operation

        // HACK!! needed for RISC-V version (if not the emulator just ignores output and input)
        IO_Write(ioadd, regA);
        return regA;
    }
    else
    {
        // Even address - read operation
        uint16_t val = IO_Read(ioadd);
        return val;
    }
}
