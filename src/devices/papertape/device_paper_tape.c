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

/*
 * Paper Tape Reader Interface (ND-06.015.02)
 *
 * Ported from RetroCore NDBusPapertapeReader.cs
 * Buffer-based tape reading (no direct file I/O)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../devices_types.h"
#include "../devices_protos.h"

#include "device_paper_tape.h"

static void PaperTape_Reset(Device *self)
{
    PaperTapeData *data = (PaperTapeData *)self->deviceData;
    if (!data)
    {
        return;
    }

    data->statusRegister.raw = 0;
    data->statusRegister.bits.readyForTransfer = 1;
    data->controlWord.raw = 0;
    data->characterBuffer = 0;
    // Do NOT reset tapePosition here - reset only resets the device state,
    // not the tape position. Use device clear for that.
}

static uint16_t PaperTape_Tick(Device *self)
{
    if (!self)
    {
        return 0;
    }
    Device_TickIODelay(self);
    return self->interruptBits;
}

static uint16_t PaperTape_Read(Device *self, uint32_t address)
{
    if (!self)
    {
        return 0;
    }

    PaperTapeData *data = (PaperTapeData *)self->deviceData;
    uint16_t value = 0;
    uint32_t reg = Device_RegisterAddress(self, address);

    switch (reg)
    {
    case PAPERTAPE_READ_DATA_REGISTER:
        value = data->characterBuffer;
        data->statusRegister.bits.readyForTransfer = 0;
        break;

    case PAPERTAPE_READ_STATUS_REGISTER:
        value = data->statusRegister.raw;
        break;
    }

    return value;
}

static void PaperTape_Write(Device *self, uint32_t address, uint16_t value)
{
    if (!self)
    {
        return;
    }

    PaperTapeData *data = (PaperTapeData *)self->deviceData;
    uint32_t reg = Device_RegisterAddress(self, address);

    switch (reg)
    {
    case PAPERTAPE_WRITE_DATA_BUFFER:
        // Not used for paper tape reader
        break;

    case PAPERTAPE_WRITE_CONTROL_WORD:
    {
        data->controlWord.raw = value;

        // Process bits in same order as C# reference:
        // 1. IE, 2. ReadActive, 3. ReadyForTransfer, 4. DeviceClear, 5. Interrupt, 6. Read

        // Bit 0: InterruptEnable
        if (data->controlWord.bits.interruptEnabled)
        {
            data->statusRegister.bits.interruptEnabled = 1;
        }
        else
        {
            data->statusRegister.bits.interruptEnabled = 0;
        }

        // Bit 2: ReadActive from control -> status
        if (data->controlWord.bits.readActive)
        {
            data->statusRegister.bits.readActive = 1;
        }
        else
        {
            data->statusRegister.bits.readActive = 0;
        }

        // Bit 4: DeviceClear (processed inline, does NOT break)
        if (data->controlWord.bits.deviceClear)
        {
            data->statusRegister.bits.readActive = 0;
            data->characterBuffer = 0;
            data->tapePosition = 0;
        }

        // Always set readyForTransfer after control write
        // (device is always ready, matching line printer and punch behavior)
        data->statusRegister.bits.readyForTransfer = 1;

        // Update interrupt status
        Device_SetInterruptStatus(self,
                                  data->statusRegister.bits.interruptEnabled &&
                                      data->statusRegister.bits.readyForTransfer,
                                  self->interruptLevel);

        // Read next byte from tape when ReadActive is set
        if (data->statusRegister.bits.readActive)
        {
            data->statusRegister.bits.readyForTransfer = 0;

            if (data->tapeData && data->tapePosition < data->tapeLength)
            {
                data->characterBuffer = data->tapeData[data->tapePosition];
                data->tapePosition++;
            }
            else
            {
                // No tape loaded or EOF - return 0x00 (blank/no tape)
                data->characterBuffer = 0;
            }

            data->statusRegister.bits.readyForTransfer = 1;
            data->statusRegister.bits.readActive = 0;
        }

        // Final interrupt status update after read
        Device_SetInterruptStatus(self,
                                  data->statusRegister.bits.interruptEnabled &&
                                      data->statusRegister.bits.readyForTransfer,
                                  self->interruptLevel);
        break;
    }
    }
}

static uint16_t PaperTape_Ident(Device *self, uint16_t level)
{
    if (!self)
    {
        return 0;
    }

    if ((self->interruptBits & (1 << level)) != 0)
    {
        PaperTapeData *data = (PaperTapeData *)self->deviceData;
        data->statusRegister.bits.interruptEnabled = 0;
        Device_SetInterruptStatus(self, false, level);
        return self->identCode;
    }
    return 0;
}

static void PaperTape_Destroy(Device *self)
{
    if (!self)
    {
        return;
    }
    PaperTapeData *data = (PaperTapeData *)self->deviceData;
    if (data && data->tapeData)
    {
        free(data->tapeData);
        data->tapeData = NULL;
    }
}

// Load tape data into the reader's memory buffer
void PaperTape_LoadTape(Device *self, const uint8_t *data, size_t length)
{
    if (!self || !data || length == 0)
    {
        return;
    }

    PaperTapeData *ptData = (PaperTapeData *)self->deviceData;
    if (!ptData)
    {
        return;
    }

    // Free existing tape data
    if (ptData->tapeData)
    {
        free(ptData->tapeData);
    }

    // Allocate and copy
    ptData->tapeData = malloc(length);
    if (ptData->tapeData)
    {
        memcpy(ptData->tapeData, data, length);
        ptData->tapeLength = length;
        ptData->tapePosition = 0;
        LOG(LOG_CAT_TAPE, LOG_INFO, "Paper tape loaded: %zu bytes\n", length);
    }
    else
    {
        ptData->tapeLength = 0;
        ptData->tapePosition = 0;
        LOG(LOG_CAT_TAPE, LOG_ERROR, "Failed to allocate memory for paper tape (%zu bytes)\n",
            length);
    }
}

Device *CreatePaperTapeDevice(uint8_t thumbwheel)
{
    Device *dev = malloc(sizeof(Device));
    if (!dev)
    {
        return NULL;
    }

    PaperTapeData *data = malloc(sizeof(PaperTapeData));
    if (!data)
    {
        free(dev);
        return NULL;
    }

    // Initialize device base structure as character device
    Device_Init(dev, thumbwheel, DEVICE_CLASS_CHARACTER, 0);

    // Initialize device-specific data
    memset(data, 0, sizeof(PaperTapeData));

    // Set up address and interrupt settings based on thumbwheel
    switch (thumbwheel)
    {
    case 0:
        snprintf(dev->memoryName, sizeof(dev->memoryName), "PAPER TAPE READER 1");
        dev->interruptLevel = 12;
        dev->identCode = 02;     // octal 02
        dev->logicalDevice = 03; // SINTRAN logical device 3
        dev->startAddress = 0400;
        dev->endAddress = 0403;
        break;
    case 1:
        snprintf(dev->memoryName, sizeof(dev->memoryName), "PAPER TAPE READER 2");
        dev->interruptLevel = 12;
        dev->identCode = 022; // octal 22
        dev->logicalDevice = 013;
        dev->startAddress = 0404;
        dev->endAddress = 0407;
        break;
    default:
        free(data);
        free(dev);
        return NULL;
    }

    // Set up device function pointers
    dev->Reset = PaperTape_Reset;
    dev->Tick = PaperTape_Tick;
    dev->Read = PaperTape_Read;
    dev->Write = PaperTape_Write;
    dev->Ident = PaperTape_Ident;
    dev->Destroy = PaperTape_Destroy;
    dev->deviceData = data;

    LOG(LOG_CAT_TAPE, LOG_INFO, "Paper Tape Reader created: %s CODE[%o] ADDRESS[%o-%o]\n",
        dev->memoryName, dev->identCode, dev->startAddress, dev->endAddress);
    return dev;
}
