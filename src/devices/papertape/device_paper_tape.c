/*
 * device_paper_tape.c - Paper tape reader interface (ND-06.015.02), IOX 0400-0403.
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
 * Paper Tape Reader Interface (ND-06.015.02)
 *
 * Ported from RetroCore NDBusPapertapeReader.cs
 * Buffer-based tape reading (no direct file I/O)
 */

#include "device_paper_tape.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../devices_types.h"
#include "../devices_protos.h"

static void paper_tape_reset(Device *self)
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

static uint16_t paper_tape_tick(Device *self)
{
    if (!self)
    {
        return 0;
    }
    dev_tick_io_delay(self);
    return self->interruptBits;
}

static uint16_t paper_tape_read(Device *self, uint32_t address)
{
    if (!self)
    {
        return 0;
    }

    PaperTapeData *data = (PaperTapeData *)self->deviceData;
    uint16_t value = 0;
    uint32_t reg = dev_register_address(self, address);

    switch (reg)
    {
    case PAPERTAPE_READ_DATA_REGISTER:
        value = data->characterBuffer;
        data->statusRegister.bits.readyForTransfer = 0;
        break;

    case PAPERTAPE_READ_STATUS_REGISTER:
        value = data->statusRegister.raw;
        break;
    default:
        break;
    }

    return value;
}

static void paper_tape_write(Device *self, uint32_t address, uint16_t value)
{
    if (!self)
    {
        return;
    }

    PaperTapeData *data = (PaperTapeData *)self->deviceData;
    uint32_t reg = dev_register_address(self, address);

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
        dev_set_interrupt_status(self,
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
        dev_set_interrupt_status(self,
                                 data->statusRegister.bits.interruptEnabled &&
                                     data->statusRegister.bits.readyForTransfer,
                                 self->interruptLevel);
        break;
    }
    break;
    default:
        break;
    }
}

static uint16_t paper_tape_ident(Device *self, uint16_t level)
{
    if (!self)
    {
        return 0;
    }

    if ((self->interruptBits & (1 << level)) != 0)
    {
        PaperTapeData *data = (PaperTapeData *)self->deviceData;
        data->statusRegister.bits.interruptEnabled = 0;
        dev_set_interrupt_status(self, false, level);
        return self->identCode;
    }
    return 0;
}

static void paper_tape_destroy(Device *self)
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
void ptr_load_tape(Device *self, const uint8_t *data, size_t length)
{
    if (!self || !data || length == 0)
    {
        return;
    }

    PaperTapeData *pt_data = (PaperTapeData *)self->deviceData;
    if (!pt_data)
    {
        return;
    }

    // Free existing tape data
    if (pt_data->tapeData)
    {
        free(pt_data->tapeData);
    }

    // Allocate and copy
    pt_data->tapeData = malloc(length);
    if (pt_data->tapeData)
    {
        memcpy(pt_data->tapeData, data, length);
        pt_data->tapeLength = length;
        pt_data->tapePosition = 0;
        LOG(LOG_CAT_TAPE, LOG_INFO, "Paper tape loaded: %zu bytes\n", length);
    }
    else
    {
        pt_data->tapeLength = 0;
        pt_data->tapePosition = 0;
        LOG(LOG_CAT_TAPE, LOG_ERROR, "Failed to allocate memory for paper tape (%zu bytes)\n",
            length);
    }
}

Device *ptr_create_paper_tape_device(uint8_t thumbwheel)
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
    dev_init(dev, thumbwheel, DEVICE_CLASS_CHARACTER, 0);

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
    dev->Reset = paper_tape_reset;
    dev->Tick = paper_tape_tick;
    dev->Read = paper_tape_read;
    dev->Write = paper_tape_write;
    dev->Ident = paper_tape_ident;
    dev->Destroy = paper_tape_destroy;
    dev->deviceData = data;

    LOG(LOG_CAT_TAPE, LOG_INFO, "Paper Tape Reader created: %s CODE[%o] ADDRESS[%o-%o]\n",
        dev->memoryName, dev->identCode, dev->startAddress, dev->endAddress);
    return dev;
}
