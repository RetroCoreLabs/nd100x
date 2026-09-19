/*
 * device_line_printer.c - Line printer interface (ND-06.016.01), IOX 0430-0433.
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

/*
 * Line Printer Interface (ND-06.016.01)
 * CDC 9380 Line Printer for NORD-10/100
 *
 * Ported from RetroCore NDBusLinePrinter.cs
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../devices_types.h"
#include "../devices_protos.h"

#include "device_line_printer.h"

static void LinePrinter_Reset(Device *self)
{
    LinePrinterData *data = (LinePrinterData *)self->deviceData;
    if (!data)
    {
        return;
    }

    data->characterBuffer = 0;
    data->statusRegister.raw = 0;
    data->controlWord.raw = 0;

    // Printer is ready after reset
    data->statusRegister.bits.readyForTransfer = 1;
}

static uint16_t LinePrinter_Tick(Device *self)
{
    if (!self)
    {
        return 0;
    }
    return self->interruptBits;
}

static uint16_t LinePrinter_Read(Device *self, uint32_t address)
{
    if (!self)
    {
        return 0;
    }

    LinePrinterData *data = (LinePrinterData *)self->deviceData;
    uint16_t value = 0;
    uint32_t reg = Device_RegisterAddress(self, address);

    switch (reg)
    {
    case LP_READ_DATA_REGISTER:
        // Returns 0 (nop) - matches C# reference
        break;

    case LP_READ_STATUS_REGISTER:
        value = data->statusRegister.raw;
        break;

    default:
        break;
    }

    return value;
}

static void LinePrinter_Write(Device *self, uint32_t address, uint16_t value)
{
    if (!self)
    {
        return;
    }

    LinePrinterData *data = (LinePrinterData *)self->deviceData;
    uint32_t reg = Device_RegisterAddress(self, address);

    switch (reg)
    {
    case LP_WRITE_DATA_BUFFER:
    {
        // Store character in buffer (7-bit ASCII) and output it
        // Matches C# reference: no status changes, printer is always instantly ready
        char c = (char)(value & 0x7F);
        data->characterBuffer = c;
        Device_OutputCharacter(self, c);
        break;
    }

    case LP_WRITE_CONTROL_WORD:
    {
        data->controlWord.raw = value;

        // Process bits in same order as C# reference:
        // 1. IE bit, 2. Activate bit, 3. ReadyForTransfer, 4. Interrupt, 5. DeviceClear

        // Bit 0: InterruptEnable
        if (data->controlWord.bits.interruptEnable)
        {
            data->statusRegister.bits.interruptEnabled = 1;
        }
        else
        {
            data->statusRegister.bits.interruptEnabled = 0;
            Device_SetInterruptStatus(self, false, self->interruptLevel);
        }

        // Bit 2: Activate
        if (data->controlWord.bits.activate)
        {
            data->statusRegister.bits.active = 1;
        }
        else
        {
            data->statusRegister.bits.active = 0;
        }

        // Always set ReadyForTransfer after control write
        data->statusRegister.bits.readyForTransfer = 1;

        // If IE is set AND ready, raise interrupt
        Device_SetInterruptStatus(self,
                                  data->statusRegister.bits.interruptEnabled &&
                                      data->statusRegister.bits.readyForTransfer,
                                  self->interruptLevel);

        // Bit 4: DeviceClear (no-op in C# reference)
        if (data->controlWord.bits.deviceClear)
        {
            // Currently a no-op, matching C# reference
        }
        break;
    }

    default:
        break;
    }
}

static uint16_t LinePrinter_Ident(Device *self, uint16_t level)
{
    if (!self)
    {
        return 0;
    }

    if ((self->interruptBits & (1 << level)) != 0)
    {
        LinePrinterData *data = (LinePrinterData *)self->deviceData;
        data->statusRegister.bits.interruptEnabled = 0;
        Device_SetInterruptStatus(self, false, level);
        return self->identCode;
    }
    return 0;
}

Device *CreateLinePrinterDevice(uint8_t thumbwheel)
{
    Device *dev = malloc(sizeof(Device));
    if (!dev)
    {
        return NULL;
    }

    LinePrinterData *data = malloc(sizeof(LinePrinterData));
    if (!data)
    {
        free(dev);
        return NULL;
    }

    // Initialize device base structure as character device
    Device_Init(dev, thumbwheel, DEVICE_CLASS_CHARACTER, 0);

    // Initialize device-specific data
    data->characterBuffer = 0;
    data->statusRegister.raw = 0;
    data->controlWord.raw = 0;

    // Set up address and interrupt settings based on thumbwheel
    switch (thumbwheel)
    {
    case 0:
        snprintf(dev->memoryName, sizeof(dev->memoryName), "LINE PRINTER 1");
        dev->interruptLevel = 10;
        dev->identCode = 03;     // octal 03
        dev->logicalDevice = 05; // SINTRAN logical device 5
        dev->startAddress = 0430;
        dev->endAddress = 0433;
        break;
    case 1:
        snprintf(dev->memoryName, sizeof(dev->memoryName), "LINE PRINTER 2");
        dev->interruptLevel = 10;
        dev->identCode = 023;     // octal 23
        dev->logicalDevice = 015; // SINTRAN logical device 15
        dev->startAddress = 0434;
        dev->endAddress = 0437;
        break;
    default:
        free(data);
        free(dev);
        return NULL;
    }

    // Set up device function pointers
    dev->Reset = LinePrinter_Reset;
    dev->Tick = LinePrinter_Tick;
    dev->Read = LinePrinter_Read;
    dev->Write = LinePrinter_Write;
    dev->Ident = LinePrinter_Ident;
    dev->deviceData = data;

    LOG(LOG_CAT_PRINTER, LOG_INFO, "Line Printer device created: %s CODE[%o] ADDRESS[%o-%o]\n",
        dev->memoryName, dev->identCode, dev->startAddress, dev->endAddress);
    return dev;
}
