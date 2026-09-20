/*
 * device_paper_tape_writer.c - Paper tape punch interface (ND-06.015.02).
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
 * Paper Tape Punch Interface (ND-06.015.02)
 *
 * Ported from RetroCore NDBusPapertapeWriter.cs
 */

#include "device_paper_tape_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../devices_types.h"
#include "../devices_protos.h"

// Forward declaration
static bool punch_end(void *context, int param);

static void paper_tape_writer_reset(Device *self)
{
    PaperTapeWriterData *data = (PaperTapeWriterData *)self->deviceData;
    if (!data)
    {
        return;
    }

    data->statusRegister.raw = 0;
    data->controlWord.raw = 0;
    data->characterBuffer = 0;
    // Don't reset tape buffer on reset - only on device clear
}

static uint16_t paper_tape_writer_tick(Device *self)
{
    if (!self)
    {
        return 0;
    }
    Device_TickIODelay(self);
    return self->interruptBits;
}

static uint16_t paper_tape_writer_read(Device *self, uint32_t address)
{
    if (!self)
    {
        return 0;
    }

    PaperTapeWriterData *data = (PaperTapeWriterData *)self->deviceData;
    uint16_t value = 0;
    uint32_t reg = Device_RegisterAddress(self, address);

    switch (reg)
    {
    case PTW_READ_DATA_REGISTER:
        // Only readable in test mode
        if (data->controlWord.bits.testMode)
        {
            value = data->characterBuffer;
        }
        break;

    case PTW_READ_STATUS_REGISTER:
        value = data->statusRegister.raw;
        break;
    default:
        break;
    }

    return value;
}

// Grow tape buffer if needed
static bool paper_tape_writer_grow_buffer(PaperTapeWriterData *data)
{
    if (!data->tapeBuffer)
    {
        data->tapeBuffer = malloc(PTW_INITIAL_TAPE_CAPACITY);
        if (!data->tapeBuffer)
        {
            LOG(LOG_CAT_TAPE, LOG_ERROR, "Paper tape punch: initial buffer allocation failed\n");
            return false;
        }
        data->tapeCapacity = PTW_INITIAL_TAPE_CAPACITY;
        return true;
    }

    if (data->tapePosition >= data->tapeCapacity)
    {
        size_t new_capacity = data->tapeCapacity * 2;
        uint8_t *new_buffer = realloc(data->tapeBuffer, new_capacity);
        if (!new_buffer)
        {
            LOG(LOG_CAT_TAPE, LOG_ERROR, "Paper tape punch: buffer grow failed at %zu bytes\n",
                new_capacity);
            return false;
        }
        data->tapeBuffer = new_buffer;
        data->tapeCapacity = new_capacity;
    }
    return true;
}

static void paper_tape_writer_write(Device *self, uint32_t address, uint16_t value)
{
    if (!self)
    {
        return;
    }

    PaperTapeWriterData *data = (PaperTapeWriterData *)self->deviceData;
    uint32_t reg = Device_RegisterAddress(self, address);

    switch (reg)
    {
    case PTW_WRITE_DATA_BUFFER:
        // Store byte to be punched
        data->characterBuffer = (uint8_t)(value & 0xFF);
        break;

    case PTW_WRITE_CONTROL_WORD:
    {
        data->controlWord.raw = value;

        // Set IE bit in status from control
        if (data->controlWord.bits.interruptEnable)
        {
            data->statusRegister.bits.interruptEnabled = 1;
        }
        else
        {
            data->statusRegister.bits.interruptEnabled = 0;
        }

        // Set Active bit in status from control
        if (data->controlWord.bits.activate)
        {
            data->statusRegister.bits.active = 1;
        }
        else
        {
            data->statusRegister.bits.active = 0;
        }

        // Device clear
        if (data->controlWord.bits.deviceClear)
        {
            data->statusRegister.bits.active = 0;
            data->characterBuffer = 0;
            data->tapePosition = 0;
        }

        // Ready for transfer
        data->statusRegister.bits.readyForTransfer = 1;

        Device_SetInterruptStatus(self,
                                  data->statusRegister.bits.interruptEnabled &&
                                      data->statusRegister.bits.readyForTransfer,
                                  self->interruptLevel);

        // Punch the byte if device is active
        if (data->statusRegister.bits.active)
        {
            // Store in tape buffer
            if (paper_tape_writer_grow_buffer(data))
            {
                data->tapeBuffer[data->tapePosition] = data->characterBuffer;
                data->tapePosition++;
                data->statusRegister.bits.readyForTransfer = 1;
            }
            else
            {
                data->statusRegister.bits.readyForTransfer = 0;
            }

            // Output the byte via callback
            Device_OutputCharacter(self, (char)data->characterBuffer);

            // Queue IO delay for punch timing
            if (data->statusRegister.bits.interruptEnabled)
            {
                Device_QueueIODelay(self, IODELAY_PAPERTAPE, punch_end, 0, self->interruptLevel);
            }
        }
        break;
    }
    break;
    default:
        break;
    }
}

static uint16_t paper_tape_writer_ident(Device *self, uint16_t level)
{
    if (!self)
    {
        return 0;
    }

    if ((self->interruptBits & (1 << level)) != 0)
    {
        PaperTapeWriterData *data = (PaperTapeWriterData *)self->deviceData;
        data->statusRegister.bits.interruptEnabled = 0;
        Device_SetInterruptStatus(self, false, level);
        return self->identCode;
    }
    return 0;
}

static bool punch_end(void *context, int param)
{
    (void)param;
    Device *self = (Device *)context;
    if (!self)
    {
        return false;
    }

    PaperTapeWriterData *data = (PaperTapeWriterData *)self->deviceData;
    if (!data)
    {
        return false;
    }

    data->statusRegister.bits.readyForTransfer = 1;
    return data->statusRegister.bits.interruptEnabled;
}

static void paper_tape_writer_destroy(Device *self)
{
    if (!self)
    {
        return;
    }
    PaperTapeWriterData *data = (PaperTapeWriterData *)self->deviceData;
    if (data && data->tapeBuffer)
    {
        free(data->tapeBuffer);
        data->tapeBuffer = NULL;
    }
}

// Get the accumulated tape output data
const uint8_t *PaperTapeWriter_GetTapeData(Device *self, size_t *length)
{
    if (!self || !length)
    {
        return NULL;
    }

    PaperTapeWriterData *data = (PaperTapeWriterData *)self->deviceData;
    if (!data || !data->tapeBuffer)
    {
        *length = 0;
        return NULL;
    }

    *length = data->tapePosition;
    return data->tapeBuffer;
}

Device *CreatePaperTapeWriterDevice(uint8_t thumbwheel)
{
    Device *dev = malloc(sizeof(Device));
    if (!dev)
    {
        return NULL;
    }

    PaperTapeWriterData *data = malloc(sizeof(PaperTapeWriterData));
    if (!data)
    {
        free(dev);
        return NULL;
    }

    // Initialize device base structure as character device
    Device_Init(dev, thumbwheel, DEVICE_CLASS_CHARACTER, 0);

    // Initialize device-specific data
    memset(data, 0, sizeof(PaperTapeWriterData));

    // Set up address and interrupt settings based on thumbwheel
    switch (thumbwheel)
    {
    case 0:
        snprintf(dev->memoryName, sizeof(dev->memoryName), "PAPER TAPE PUNCH 1");
        dev->interruptLevel = 10;
        dev->identCode = 02;     // octal 02
        dev->logicalDevice = 04; // SINTRAN logical device 4
        dev->startAddress = 0410;
        dev->endAddress = 0413;
        break;
    case 1:
        snprintf(dev->memoryName, sizeof(dev->memoryName), "PAPER TAPE PUNCH 2");
        dev->interruptLevel = 10;
        dev->identCode = 022; // octal 22
        dev->logicalDevice = 014;
        dev->startAddress = 0414;
        dev->endAddress = 0417;
        break;
    default:
        free(data);
        free(dev);
        return NULL;
    }

    // Set up device function pointers
    dev->Reset = paper_tape_writer_reset;
    dev->Tick = paper_tape_writer_tick;
    dev->Read = paper_tape_writer_read;
    dev->Write = paper_tape_writer_write;
    dev->Ident = paper_tape_writer_ident;
    dev->Destroy = paper_tape_writer_destroy;
    dev->deviceData = data;

    LOG(LOG_CAT_TAPE, LOG_INFO, "Paper Tape Punch created: %s CODE[%o] ADDRESS[%o-%o]\n",
        dev->memoryName, dev->identCode, dev->startAddress, dev->endAddress);
    return dev;
}
