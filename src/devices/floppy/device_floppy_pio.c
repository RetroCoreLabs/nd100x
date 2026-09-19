/*
 * device_floppy_pio.c - Programmed-I/O floppy disc controller: read, seek, recalibrate.
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
#include "../devices_types.h"
#include "../devices_protos.h"
#include "device_floppy_pio.h"


// Floppy boot sector data
static const uint8_t floppy_boot[388] = {
    0xb1, 0x8d, 0x0a, 0x30, 0x30, 0x36, 0x30, 0x30, 0x30, 0x8d, 0x0a, 0xb1, 0x36, 0xb4, 0x33, 0xb1,
    0x36, 0x21, 0x0c, 0x00, 0x00, 0xb3, 0xf1, 0x00, 0xb2, 0x03, 0xd2, 0x40, 0xa8, 0x00, 0xf1, 0xff,
    0x08, 0x1b, 0x40, 0x1a, 0xa8, 0x02, 0xa8, 0x03, 0xf3, 0x31, 0xa8, 0x1a, 0x48, 0x16, 0xcc, 0x69,
    0xf1, 0x00, 0xf2, 0x03, 0xc3, 0xb0, 0x68, 0x12, 0xb2, 0x03, 0xf3, 0x32, 0xa8, 0x11, 0xcc, 0x4d,
    0x68, 0x0e, 0xb3, 0xfc, 0xf3, 0x00, 0x4c, 0x00, 0x0c, 0x00, 0xcd, 0x07, 0xcc, 0x7d, 0xb3, 0xfc,
    0xd0, 0x05, 0xd0, 0x0d, 0xa8, 0x23, 0x00, 0x00, 0x00, 0x11, 0x00, 0x05, 0x00, 0x02, 0x48, 0x1d,
    0xe8, 0xc3, 0xf2, 0x0d, 0xb8, 0x14, 0xf2, 0x0a, 0xb8, 0x12, 0xf2, 0x45, 0xb8, 0x10, 0xf2, 0x52,
    0xb8, 0x0e, 0xb8, 0x0d, 0xf2, 0x4f, 0xb8, 0x0b, 0xf2, 0x52, 0xb8, 0x09, 0xf2, 0x20, 0xb8, 0x07,
    0xcc, 0x7e, 0xb8, 0x05, 0xf2, 0x20, 0xb8, 0x03, 0xd2, 0x08, 0xa8, 0xc6, 0xe8, 0xc6, 0xfa, 0x9d,
    0xa8, 0xfe, 0xcc, 0x75, 0xe8, 0xc5, 0xcc, 0x62, 0x48, 0x04, 0xf1, 0xfb, 0x08, 0x49, 0xf1, 0x30,
    0xeb, 0x73, 0x48, 0x4e, 0xeb, 0x75, 0x00, 0x00, 0x00, 0x00, 0xeb, 0x72, 0xfa, 0x9d, 0xa8, 0xfe,
    0xfa, 0xa5, 0xa8, 0x0a, 0x08, 0x07, 0xeb, 0x74, 0xfa, 0x45, 0xa8, 0xf2, 0x08, 0x04, 0xf3, 0x33,
    0xa8, 0xcf, 0x00, 0x00, 0x00, 0x00, 0x48, 0x3d, 0xeb, 0x73, 0xeb, 0x72, 0xfa, 0x9d, 0xa8, 0xfe,
    0x48, 0x39, 0xeb, 0x77, 0x48, 0x38, 0xeb, 0x73, 0xeb, 0x72, 0xfa, 0x15, 0xa8, 0xfe, 0xfa, 0x25,
    0xa8, 0x20, 0xf1, 0x20, 0xeb, 0x73, 0xb8, 0x32, 0xf2, 0x21, 0x70, 0x2e, 0xc4, 0x2e, 0xa8, 0x04,
    0xcc, 0x4d, 0x08, 0x16, 0xa8, 0xf9, 0xb8, 0x1d, 0xcc, 0x6b, 0xb8, 0x1b, 0xcc, 0x6f, 0xcc, 0x41,
    0xb8, 0x18, 0xcc, 0x29, 0x09, 0x00, 0xcd, 0x03, 0xcc, 0x87, 0xc0, 0x07, 0xa8, 0xfa, 0xb8, 0x11,
    0xcd, 0x8d, 0xb3, 0x07, 0xeb, 0x70, 0x70, 0x19, 0xc0, 0x05, 0xd2, 0x00, 0xaa, 0x01, 0x00, 0x00,
    0x08, 0xd1, 0xeb, 0x74, 0x08, 0xd0, 0x40, 0x04, 0xa8, 0xbb, 0xf3, 0x34, 0xa8, 0x99, 0x00, 0x00,
    0xeb, 0x70, 0xdd, 0x08, 0xcc, 0x6e, 0xeb, 0x70, 0x70, 0x08, 0xcb, 0x35, 0xcc, 0x62, 0xc0, 0x01,
    0x40, 0x00, 0x01, 0x00, 0x10, 0x00, 0x00, 0x7f, 0x00, 0xff, 0x10, 0x14, 0xcc, 0x41, 0x50, 0x13,
    0xeb, 0x70, 0x70, 0x12, 0xc4, 0x35, 0xa8, 0xfd, 0x08, 0x0c, 0x68, 0x0f, 0xb1, 0x07, 0x68, 0x0e,
    0xb0, 0x05, 0x60, 0x0c, 0xdc, 0x83, 0xcb, 0x29, 0xa8, 0xf4, 0x48, 0x03, 0x50, 0x02, 0xcc, 0x62,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x7f, 0x00, 0x30, 0x00, 0x08, 0x54, 0xd4, 0x00, 0x00,
    0xf1, 0x00, 0x00, 0x00};

static const size_t floppy_boot_length = sizeof(floppy_boot);

static void FloppyPIO_Reset(Device *self)
{
    FloppyPIOData *data = (FloppyPIOData *)self->deviceData;
    if (!data)
    {
        return;
    }

    data->status1.bits.deviceReadyForTransfer = 1;
    data->bufferPointer = 0;
    data->testmodeByte = 0;
    self->interruptBits = 0;
    data->selectedDrive = -1;
}

static uint16_t FloppyPIO_Tick(Device *self)
{
    if (!self)
    {
        return 0;
    }
    Device_TickIODelay(self);
    return self->interruptBits;
}

static uint16_t FloppyPIO_Read(Device *self, uint32_t address)
{
    if (!self)
    {
        return 0;
    }

    FloppyPIOData *data = (FloppyPIOData *)self->deviceData;
    uint16_t value = 0;
    uint32_t reg = Device_RegisterAddress(self, address);

    switch (reg)
    {
    case FLOPPY_READ_DATA_BUFFER:
        value = data->dataBuffer[data->bufferPointer];
        if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_TRACE))
        {
            Log_Write(LOG_CAT_FLOPPY, LOG_TRACE, "%o Read offset [%04X]= %04X\r\n", address,
                      data->bufferPointer, value);
        }
        data->bufferPointer = (data->bufferPointer + 1) & 0x3FF;
        break;

    case FLOPPY_READ_STATUS_REGISTER1:
        data->status1.bits.inclusiveOrBitsInReg2 = data->status2.raw > 0;
        value = data->status1.raw;

        // For testprogram to succeed, this 'magic' needs to be here
        {
            int w = data->bufferPointer;
            if ((w & (1 << 1)) && (w & (1 << 6)))
            {
                value |= (1 << 9);
            }
            if ((w & (1 << 1)) && (w & (1 << 7)))
            {
                value |= (1 << 10);
            }
            if ((w & (1 << 1)) && (w & (1 << 8)))
            {
                value |= (1 << 11);
            }
        }
        break;

    case FLOPPY_READ_STATUS_REGISTER2:
        value = data->status2.raw;
        break;

    case FLOPPY_READ_TEST_DATA:
        if (data->control.bits.testMode)
        {
            int wbufptr = data->bufferPointer;
            uint16_t val = data->dataBuffer[wbufptr];

            if (data->testmodeByte > 0)
            {
                val = (val & 0xFF00) | data->testByte;
                data->dataBuffer[data->bufferPointer] = val;
                data->bufferPointer = (data->bufferPointer + 1) & 0x3FF;
                data->testmodeByte = 0;
            }
            else
            {
                val = (val & 0x00FF) | (data->testByte << 8);
                data->dataBuffer[data->bufferPointer] = val;
                data->testmodeByte++;
            }
        }
        break;
    default:
        break;
    }

    if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
    {
        if (reg != FLOPPY_READ_DATA_BUFFER)
        {
            Log_Write(LOG_CAT_FLOPPY, LOG_DEBUG, "Floppy PIO Reading from address: %o value: %o\n",
                      address, value);
        }
    }

    return value;
}

static void FloppyPIO_Write(Device *self, uint32_t address, uint16_t value)
{
    if (!self)
    {
        return;
    }

    FloppyPIOData *data = (FloppyPIOData *)self->deviceData;
    uint32_t reg = Device_RegisterAddress(self, address);

    if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
    {
        if (reg != FLOPPY_WRITE_DATA_BUFFER)
        {
            Log_Write(LOG_CAT_FLOPPY, LOG_DEBUG, "Floppy PIO Writing value: %o to address: %o\n",
                      value, address);
        }
    }

    switch (reg)
    {
    case FLOPPY_WRITE_DATA_BUFFER:
        if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_TRACE))
        {
            Log_Write(LOG_CAT_FLOPPY, LOG_TRACE, "FloppyPIO: Write Buffer offset %d value = %04x\n",
                      data->bufferPointer, value);
        }
        data->dataBuffer[data->bufferPointer] = value;
        data->bufferPointer = (data->bufferPointer + 1) & 0x3FF;
        break;

    case FLOPPY_WRITE_CONTROL_WORD:
        data->control.raw = value;
        data->status1.bits.interruptEnabled = data->control.bits.enableInterrupt;

        if (data->control.bits.autoload)
        {
            // HACK HACK HACK
            // Correct is to read track 0, sector 1
            data->track = 0;
            data->sector = 1;
            data->bufferPointer = 0;
            data->status1.bits.deviceReadyForTransfer = 1;

            for (size_t i = 0; i < floppy_boot_length; i++)
            {
                data->dataBuffer[i] = floppy_boot[i];
            }
        }

        if (data->control.bits.deviceClear)
        {
            data->selectedDrive = -1;
            data->bufferPointer = 0;
            data->status1.bits.deviceReadyForTransfer = 1;
            data->status2.raw = 0; // Clear all error flags
        }

        if (data->control.bits.clearInterfaceBufferAddress)
        {
            data->bufferPointer = 0;
            data->status1.bits.deviceReadyForTransfer = 1;
        }

        if ((value & 0xFF00) != 0)
        {
            data->status1.bits.deviceBusy = 1;
            int tmp = (value >> 8) & 0xFF;
            for (int i = 0; i < 8; i++)
            {
                if (tmp & (1 << i))
                {
                    data->command = (FloppyPIOCommand)i;
                }
            }
            FloppyPIO_ExecuteGo(self, data->command);
        }

        Device_SetInterruptStatus(
            self, data->status1.bits.interruptEnabled && data->status1.bits.deviceReadyForTransfer,
            self->interruptLevel);
        break;

    case FLOPPY_WRITE_DRIVE_ADDRESS:
        data->driveAddress.raw = value;
        if (data->driveAddress.bits.modeBit)
        { // Write Drive Address
            data->selectedDrive = data->driveAddress.bits.driveAddress;
            if (data->driveAddress.bits.deselectDrives)
            {
                data->selectedDrive = -1;
            }

            switch (data->driveAddress.bits.formatSelect)
            {
            case 0:
            case 1:
                data->bytes_pr_sector = 128;
                data->sectors_pr_track = 26;
                break;
            case 2:
                data->bytes_pr_sector = 256;
                data->sectors_pr_track = 15;
                break;
            case 3:
                data->bytes_pr_sector = 512;
                data->sectors_pr_track = 8;
                break;
            default:
                break;
            }
        }
        else
        { // Write Difference
            int difference = (value >> 8) & 0x7F;
            int move_in = (value >> 15) & 0x01;

            // Signed arithmetic: track is unsigned, and stepping out past
            // track 0 used to wrap round and then clamp to 76.
            int new_track = (int)data->track + (move_in ? difference : -difference);

            // Limits
            if (new_track < 0)
            {
                new_track = 0;
            }
            if (new_track > 76)
            {
                new_track = 76;
            }
            data->track = (uint16_t)new_track;
        }
        break;

    case FLOPPY_WRITE_SECTOR:
        if (data->control.bits.testMode)
        {
            data->testByte = ((value >> 8) & 0xFF);
        }
        else
        {
            data->sectorControl.raw = value;
            data->sector = data->sectorControl.bits.sectorNumber;
            data->sectorAutoIncrement = data->sectorControl.bits.autoIncrement;
        }
        break;
    default:
        break;
    }
}

static uint16_t FloppyPIO_Ident(Device *self, uint16_t level)
{
    if (!self)
    {
        return 0;
    }

    if ((self->interruptBits & (1 << level)) != 0)
    {
        FloppyPIOData *data = (FloppyPIOData *)self->deviceData;
        data->status1.bits.interruptEnabled = 0;
        Device_SetInterruptStatus(self, false, level);
        return self->identCode;
    }
    return 0;
}

static bool FloppyPIO_ReadEnd(Device *self, int drive)
{
    (void)drive;
    FloppyPIOData *data = (FloppyPIOData *)self->deviceData;
    if (!data)
    {
        return false;
    }

    data->status1.bits.deviceBusy = 0;
    data->status1.bits.deviceReadyForTransfer = 1;
    data->status1.bits.readWriteComplete = 1;

    if (data->sectorAutoIncrement)
    {
        if (data->sector <= data->sectors_pr_track)
        {
            data->sector++;
        }
    }

    return data->status1.bits.interruptEnabled;
}

static bool FloppyPIO_RecalibrateEnd(Device *self, int drive)
{
    (void)drive;
    FloppyPIOData *data = (FloppyPIOData *)self->deviceData;
    if (!data)
    {
        return false;
    }

    data->status1.bits.deviceBusy = 0;
    data->status1.bits.deviceReadyForTransfer = 1;
    data->status1.bits.seekComplete = 1;

    return data->status1.bits.interruptEnabled;
}

static bool FloppyPIO_SeekEnd(Device *self, int drive)
{
    (void)drive;
    FloppyPIOData *data = (FloppyPIOData *)self->deviceData;
    if (!data)
    {
        return false;
    }

    data->status1.bits.deviceBusy = 0;
    data->status1.bits.deviceReadyForTransfer = 1;
    data->status1.bits.seekComplete = 1;

    return data->status1.bits.interruptEnabled;
}

static void FloppyPIO_ClearAllErrorFlags(Device *self)
{
    FloppyPIOData *data = (FloppyPIOData *)self->deviceData;
    if (!data)
    {
        return;
    }

    // Clear all error flags
    data->status2.raw = 0;
}

static void SetSectorAsDeleted(FloppyPIOData *data, int sector, int track, bool deleted)
{
    if (!data)
    {
        return;
    }
    if (track >= 0 && track < 100 && sector > 0 && sector <= 100)
    {
        data->deletedSector[track][sector - 1] = deleted;
    }
}

static bool SectorIsDeleted(FloppyPIOData *data, int sector, int track)
{
    if (!data)
    {
        return false;
    }
    if (track >= 0 && track < 100 && sector > 0 && sector <= 100)
    {
        return data->deletedSector[track][sector - 1];
    }
    return false;
}

void FloppyPIO_ExecuteGo(Device *self, FloppyPIOCommand command)
{
    FloppyPIOData *data = (FloppyPIOData *)self->deviceData;
    if (!data)
    {
        return;
    }

    FloppyPIO_ClearAllErrorFlags(self);

    int transferWordCount = data->bytes_pr_sector >> 1;
    int wordsRead = 0;
    (void)wordsRead; /* reported only under DEBUG_DETAIL */

    data->status1.bits.deviceReadyForTransfer = 0;
    data->status1.bits.readWriteComplete = 0;
    data->status1.bits.seekComplete = 0;
    data->status1.bits.deletedRecord = 0;

    data->status2.bits.writeProtect = false;

    // Validate sector number
    if (data->sector <= 0)
    {
        data->sector = 1;
    }

    if (data->sector > data->sectors_pr_track)
    {
        data->status2.bits.sectorMissing = 1;
        data->status1.bits.deviceReadyForTransfer = 1;
        data->status1.bits.deviceBusy = 0;

        return;
    }

    uint8_t unit = (uint8_t)data->selectedDrive;

    if (!data->floppyFile || data->selectedDrive < 0)
    {
        data->status2.bits.driveNotReady = 1;
        data->status1.bits.deviceBusy = 0;

        return;
    }

    // Calculate file offset for read/write operations
    int position = ((data->sector - 1) * data->bytes_pr_sector) +
                   (data->track * data->bytes_pr_sector * data->sectors_pr_track);

    if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
    {
        Log_Write(
            LOG_CAT_FLOPPY, LOG_DEBUG,
            "Executing command %d on drive %d position %d [Sector %d Track %d DataBufAddr %d]\n",
            command, data->selectedDrive, position, data->sector, data->track, data->bufferPointer);
    }

    switch (command)
    {
    case FLOPPY_CMD_FORMAT_TRACK:
        if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
        {
            Log_Write(LOG_CAT_FLOPPY, LOG_DEBUG, "Starting FormatTrack \r\n");
        }
        if (data->status2.bits.writeProtect)
        {
            data->status1.bits.deviceBusy = 0;
            data->status1.bits.deviceReadyForTransfer = 1;
            return;
        }

        position = (1 * data->bytes_pr_sector) +
                   (data->track * data->bytes_pr_sector * data->sectors_pr_track);

        if (fseek(data->floppyFile, position, SEEK_SET) != 0)
        {
            if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
            {
                Log_Write(LOG_CAT_FLOPPY, LOG_DEBUG, "Floppy SEEK in FormatTrack to %d FAILED\r\n",
                          position);
            }
            data->status2.bits.sectorMissing = 1;
            data->status1.bits.deviceBusy = 0;
            data->status1.bits.deviceReadyForTransfer = 1;
            return;
        }

        uint16_t formatData = 0xAAFF;

        for (int s = 1; s <= data->sectors_pr_track; s++)
        {
            transferWordCount = data->bytes_pr_sector >> 1;
            while (transferWordCount > 0)
            {
                if (!Device_IO_WriteWord(self, data->floppyFile, formatData))
                {
                    if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
                    {
                        Log_Write(LOG_CAT_FLOPPY, LOG_DEBUG,
                                  "IO error during [FORMAT] Track=%d, Sector=%d\r\n", data->track,
                                  data->sector);
                    }
                    data->status2.bits.driveNotReady = 1;
                    data->status1.bits.deviceBusy = 0;
                    return;
                }
                transferWordCount--;
            }
            SetSectorAsDeleted(data, s, data->track, false);
        }

        Device_QueueIODelay(self, IODELAY_FLOPPY, (IODelayedCallback)FloppyPIO_ReadEnd, unit,
                            self->interruptLevel);
        break;

    case FLOPPY_CMD_WRITE_DATA:
        if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
        {
            Log_Write(LOG_CAT_FLOPPY, LOG_DEBUG, "Starting WriteData \r\n");
        }
        if (data->status2.bits.writeProtect)
        {
            data->status1.bits.deviceBusy = 0;
            data->status1.bits.deviceReadyForTransfer = 1;
            return;
        }

        if (fseek(data->floppyFile, position, SEEK_SET) != 0)
        {
            if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
            {
                Log_Write(LOG_CAT_FLOPPY, LOG_DEBUG, "Floppy SEEK in WRITE to %d FAILED\r\n",
                          position);
            }
            data->status2.bits.sectorMissing = 1;
            data->status1.bits.deviceBusy = 0;
            return;
        }

        while (transferWordCount > 0)
        {
            uint16_t writeData = data->dataBuffer[data->bufferPointer];
            data->bufferPointer = (data->bufferPointer + 1) & 0x3FF;

            if (!Device_IO_WriteWord(self, data->floppyFile, writeData))
            {
                if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
                {
                    Log_Write(LOG_CAT_FLOPPY, LOG_DEBUG, "IO ERROR in WRITE at %d\r\n", position);
                }
                data->status2.bits.driveNotReady = 1;
                data->status1.bits.deviceBusy = 0;
                return;
            }

            wordsRead++;
            transferWordCount--;
        }

        SetSectorAsDeleted(data, data->sector, data->track, false);

        if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_TRACE))
        {
            Log_Write(LOG_CAT_FLOPPY, LOG_TRACE, "FloppyPIO: Write %d WORDs\r\n", wordsRead);
        }

        Device_QueueIODelay(self, IODELAY_FLOPPY, (IODelayedCallback)FloppyPIO_ReadEnd, unit,
                            self->interruptLevel);
        break;

    case FLOPPY_CMD_WRITE_DELETED_DATA:
        if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
        {
            Log_Write(LOG_CAT_FLOPPY, LOG_DEBUG, "Starting WriteDeletedData \r\n");
        }
        if (data->status2.bits.writeProtect)
        {
            data->status1.bits.deviceBusy = 0;
            data->status1.bits.deviceReadyForTransfer = 1;
            return;
        }

        SetSectorAsDeleted(data, data->sector, data->track, true);

        if (fseek(data->floppyFile, position, SEEK_SET) != 0)
        {
            if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
            {
                Log_Write(LOG_CAT_FLOPPY, LOG_DEBUG,
                          "Floppy SEEK in WriteDeletedData to %d FAILED\r\n", position);
            }
            data->status2.bits.sectorMissing = 1;
            data->status1.bits.deviceBusy = 0;
            return;
        }

        while (transferWordCount > 0)
        {
            uint16_t writeData = data->dataBuffer[data->bufferPointer];
            data->bufferPointer = (data->bufferPointer + 1) & 0x3FF;

            if (!Device_IO_WriteWord(self, data->floppyFile, writeData))
            {
                if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
                {
                    Log_Write(LOG_CAT_FLOPPY, LOG_DEBUG, "IO ERROR in WriteDeletedData at %d\r\n",
                              position);
                }
                data->status2.bits.driveNotReady = 1;
                data->status1.bits.deviceBusy = 0;
                return;
            }

            wordsRead++;
            transferWordCount--;
        }

        Device_QueueIODelay(self, IODELAY_FLOPPY, (IODelayedCallback)FloppyPIO_ReadEnd, unit,
                            self->interruptLevel);
        break;

    case FLOPPY_CMD_READ_ID:
        if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
        {
            Log_Write(LOG_CAT_FLOPPY, LOG_DEBUG, "Starting ReadID \r\n");
        }

        if (SectorIsDeleted(data, data->sector, data->track))
        {
            data->dataBuffer[0] = 0xFF00;
            data->dataBuffer[1] = 0xFF02;
        }
        else
        {
            data->dataBuffer[0] = (uint16_t)((data->track << 8) | 0x00);
            data->dataBuffer[1] = (uint16_t)(data->sector << 8);
        }

        data->bufferPointer = 0;
        Device_QueueIODelay(self, IODELAY_FLOPPY, (IODelayedCallback)FloppyPIO_ReadEnd, unit,
                            self->interruptLevel);
        break;

    case FLOPPY_CMD_READ_DATA:
        if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
        {
            Log_Write(LOG_CAT_FLOPPY, LOG_DEBUG,
                      "Starting ReadData, transferWordCount=%d, position=%d\r\n", transferWordCount,
                      position);
        }

        if (fseek(data->floppyFile, position, SEEK_SET) != 0)
        {
            if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
            {
                Log_Write(LOG_CAT_FLOPPY, LOG_DEBUG, "Floppy SEEK in READ to %d FAILED\r\n",
                          position);
            }
            data->status2.bits.sectorMissing = 1;
            data->status1.bits.deviceBusy = 0;
            return;
        }

        if (data->sector <= 0)
        {
            data->status2.bits.sectorMissing = 1;
            data->status1.bits.deviceBusy = 0;
            return;
        }

        if (SectorIsDeleted(data, data->sector, data->track))
        {
            data->status1.bits.deletedRecord = 1;
        }

        while (transferWordCount > 0)
        {
            int readData = Device_IO_ReadWord(self, data->floppyFile);
            if (readData == -1)
            {
                if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
                {
                    Log_Write(LOG_CAT_FLOPPY, LOG_DEBUG, "IO ERROR in READ at %d FAILED\r\n",
                              position);
                }
                data->status2.bits.driveNotReady = 1;
                data->status1.bits.deviceBusy = 0;
                return;
            }

            wordsRead++;
            data->dataBuffer[data->bufferPointer] = (uint16_t)readData;
            data->bufferPointer = (data->bufferPointer + 1) & 0x3FF;
            transferWordCount--;
        }

        if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_TRACE))
        {
            Log_Write(LOG_CAT_FLOPPY, LOG_TRACE, "FloppyPIO: Read %d WORDs\r\n", wordsRead);
        }

        // Simulate transfer delay
        Device_QueueIODelay(self, IODELAY_FLOPPY, (IODelayedCallback)FloppyPIO_ReadEnd, unit,
                            self->interruptLevel);
        break;

    case FLOPPY_CMD_SEEK:
        if (data->sector <= 0)
        {
            data->status2.bits.sectorMissing = 1;
        }

        if (fseek(data->floppyFile, position, SEEK_SET) != 0)
        {
            LOG(LOG_CAT_FLOPPY, LOG_ERROR, "Floppy SEEK to %d FAILED\r\n", position);
            data->status2.bits.sectorMissing = 1;
            data->status1.bits.deviceBusy = 0;
            return;
        }

        Device_QueueIODelay(self, IODELAY_FLOPPY, (IODelayedCallback)FloppyPIO_SeekEnd, unit,
                            self->interruptLevel);
        break;

    case FLOPPY_CMD_RECALIBRATE:
        LOG(LOG_CAT_FLOPPY, LOG_DEBUG, "Starting Recalibrate\r\n");
        data->track = 0;
        data->sector = 1;
        Device_QueueIODelay(self, IODELAY_FLOPPY, (IODelayedCallback)FloppyPIO_RecalibrateEnd, unit,
                            self->interruptLevel);
        break;

    case FLOPPY_CMD_CONTROL_RESET:
        data->status1.bits.deviceBusy = 0;
        break;

    default:
        break;
    }
}


Device *CreateFloppyPIODevice(uint8_t thumbwheel)
{
    Device *dev = malloc(sizeof(Device));
    if (!dev)
    {
        return NULL;
    }

    FloppyPIOData *data = malloc(sizeof(FloppyPIOData));
    if (!data)
    {
        free(dev);
        return NULL;
    }

    // Initialize device base structure
    Device_Init(dev, thumbwheel, DEVICE_CLASS_BLOCK, 1024);

    // Set up device-specific data
    data->floppyFile = NULL;
    data->floppyName = "FLOPPY.IMG"; //TODO: Make this configurable
    data->bufferPointer = 0;
    data->loadDriveAddress = 0;
    data->sector = 0;
    data->track = 0;
    data->selectedDrive = -1;
    data->bytes_pr_sector = 0;
    data->sectors_pr_track = 0;
    data->testByte = 0;
    data->sectorAutoIncrement = false;
    data->testmodeByte = 0;
    data->deletedRecord = false;
    memset(data->deletedSector, 0, sizeof(data->deletedSector));
    data->status1.raw = 0;
    data->control.raw = 0;
    data->status2.raw = 0;
    data->driveAddress.raw = 0;
    data->sectorControl.raw = 0;
    data->command = FLOPPY_CMD_NONE;

    // Set up device address and interrupt settings based on thumbwheel
    switch (thumbwheel)
    {
    case 0:
        dev->interruptLevel = 11;
        dev->identCode = 021; // octal 021
        dev->startAddress = 01560;
        dev->endAddress = 01567;
        break;
    case 1:
        dev->interruptLevel = 11;
        dev->identCode = 022; // Octal 022
        dev->startAddress = 01570;
        dev->endAddress = 01577;
        break;
    default:
        free(data);
        free(dev);
        return NULL;
    }

    // Open floppy file
    if ((data->floppyFile = fopen(data->floppyName, "r")) == NULL)
    {
        LOG(LOG_CAT_FLOPPY, LOG_ERROR, "Unable to open file %s\n", data->floppyName);
    }

    // Set up device function pointers
    dev->Reset = FloppyPIO_Reset;
    dev->Tick = FloppyPIO_Tick;
    dev->Read = FloppyPIO_Read;
    dev->Write = FloppyPIO_Write;
    dev->Ident = FloppyPIO_Ident;
    dev->deviceData = data;

    LOG(LOG_CAT_FLOPPY, LOG_INFO, "FloppyPIO object created.\n");
    return dev;
}
