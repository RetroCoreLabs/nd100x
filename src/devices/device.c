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
#include <errno.h>
#include <inttypes.h>

#include "devices_types.h"
#include "../cpu/cpu_types.h"   /* gDMAAccess */
#include "devices_protos.h"

#define INITIAL_IO_DELAY_CAPACITY 16

// Odd parity lookup table
const uint8_t Device_OddParityTable[PARITY_TABLE_SIZE] = {
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0};

// Helper function to get odd parity for a value
uint8_t Device_GetOddParity(uint8_t value)
{
    return Device_OddParityTable[value];
}


void Device_Init(Device *dev, uint8_t thumbwheel, DeviceClass deviceClass, size_t blockSize)
{
    (void)thumbwheel;
    if (!dev)
        return;

    // Clear all fields
    memset(dev, 0, sizeof(Device));

    // Set basic device properties
    dev->startAddress = 0;
    dev->endAddress = 0;
    dev->interruptBits = 0;
    dev->interruptLevel = 0;
    dev->identCode = 0;
    dev->deviceClass = deviceClass;

    // Initialize IO delay array
    dev->ioDelays = malloc(sizeof(DelayedIoInfo) * INITIAL_IO_DELAY_CAPACITY);
    if (dev->ioDelays)
    {
        dev->ioDelayCapacity = INITIAL_IO_DELAY_CAPACITY;
        dev->ioDelayCount = 0;
    }

    // Initialize based on device class
    switch (deviceClass) {
        case DEVICE_CLASS_CHARACTER:
            // Initialize character device callbacks
            memset(&dev->charCallbacks, 0, sizeof(CharacterDeviceCallbacks));
            break;

        case DEVICE_CLASS_BLOCK:
            // Initialize block device callbacks
            memset(&dev->blockCallbacks, 0, sizeof(BlockDeviceCallbacks));
            dev->blockSizeBytes = (blockSize > 0 && blockSize <= MAX_BLOCK_SIZE) ? blockSize : 1024;
            break;

        case DEVICE_CLASS_RTC:
        case DEVICE_CLASS_STANDARD:
        default:
            // No special initialization needed
            break;
    }
}

void Device_Destroy(Device *dev)
{
    if (!dev)
        return;

    // Call device-specific cleanup if it exists
    if (dev->Destroy)
    {
        dev->Destroy(dev);
    }

    if (dev->ioDelays)
    {
        free(dev->ioDelays);
        dev->ioDelays = NULL;
    }

    if (dev->deviceData)
    {
        free(dev->deviceData);
        dev->deviceData = NULL;
    }
}

void Device_Reset(Device *dev)
{
    if (!dev || !dev->Reset)
        return;
    dev->Reset(dev);
}

uint16_t Device_Tick(Device *dev)
{
    if (!dev || !dev->Tick)
        return 0;
    return dev->Tick(dev);
}

// Loads boot code from the given unit on this controller to memory.
// Returns the boot address, or -1 if error
int32_t Device_Boot(Device *dev, int unit)
{
    if (!dev)
        return -1;
    if (dev->Boot == NULL)
        return -1; // No boot function defined

    return dev->Boot(dev, unit);
}

bool Device_IsInAddress(Device *dev, uint32_t address)
{
    if (!dev)
        return false;
    return (address >= dev->startAddress && address <= dev->endAddress);
}

uint32_t Device_RegisterAddress(Device *dev, uint32_t address)
{
    if (!dev)
        return 0;
    return address - dev->startAddress;
}

uint16_t Device_Read(Device *dev, uint32_t address)
{
    if (!dev || !dev->Read)
        return 0;
    return dev->Read(dev, address);
}

void Device_Write(Device *dev, uint32_t address, uint16_t value)
{
    if (!dev || !dev->Write)
        return;
    dev->Write(dev, address, value);
}

uint16_t Device_Ident(Device *dev, uint16_t level)
{
    if (!dev || !dev->Ident)
        return 0;
    return dev->Ident(dev, level);
}

void Device_QueueIODelay(Device *dev, uint16_t ticks, IODelayedCallback cb, int param, uint8_t irqlevel)
{
    if (!dev || !dev->ioDelays)
        return;

    // Resize array if needed
    if (dev->ioDelayCount >= dev->ioDelayCapacity)
    {
        int newCapacity = dev->ioDelayCapacity * 2;
        DelayedIoInfo *newDelays = realloc(dev->ioDelays, sizeof(DelayedIoInfo) * newCapacity);
        if (!newDelays)
            return;

        dev->ioDelays = newDelays;
        dev->ioDelayCapacity = newCapacity;
    }

    // Add new delay
    DelayedIoInfo *delay = &dev->ioDelays[dev->ioDelayCount++];
    delay->delayTicks = ticks;
    delay->callback = cb;
    delay->context = dev;
    delay->parameter = param;
    delay->level = irqlevel;
}

void Device_TickIODelay(Device *dev)
{
    if (!dev || !dev->ioDelays)
        return;

    for (int i = 0; i < dev->ioDelayCount; i++)
    {
        DelayedIoInfo *delay = &dev->ioDelays[i];
        delay->delayTicks--;

        if (delay->delayTicks <= 0)
        {
            bool triggered = delay->callback(delay->context, delay->parameter);
            if (triggered && delay->level > 0)
            {
                Device_GenerateInterrupt(dev, delay->level);
            }
            // Remove this delay by shifting remaining ones
            memmove(&dev->ioDelays[i], &dev->ioDelays[i + 1], (dev->ioDelayCount - i - 1) * sizeof(DelayedIoInfo));
            dev->ioDelayCount--;
            i--; // Recheck this position
        }
    }
}

void Device_ClearInterrupt(Device *dev, uint16_t level)
{
    if (!dev)
        return;

    if ((level >= 10) && (level <= 13))
    {
        if ((dev->interruptBits & (1 << level)) != 0)
        {
            dev->interruptBits &= ~(1 << level);
        }
    }
}

void Device_GenerateInterrupt(Device *dev, uint16_t level)
{
    if (!dev)
        return;

    if ((level >= 10) && (level <= 13))
    {
        if ((dev->interruptBits & (1 << level)) == 0)
        {
            dev->interruptBits |= (1 << level);
        }
    }
}

void Device_SetInterruptStatus(Device *dev, bool active, uint16_t level)
{
    if (!dev)
        return;

    if (active)
    {
        Device_GenerateInterrupt(dev, level);
    }
    else
    {
        Device_ClearInterrupt(dev, level);
    }
}

int32_t Device_IO_Seek(Device *dev, FILE *f, long  offset)
{
    (void)dev;
    if (!f) return -1;
    return fseek(f, offset, SEEK_SET);
}


int32_t Device_IO_ReadWord(Device *dev, FILE *f)
{
    (void)dev;
    if (!f)
        return -1;

    int16_t hi = getc(f);
    if (hi < 0)
        return -1;
    hi = hi & 0xFF;

    int16_t lo = getc(f);
    if (lo < 0)
        return -1;
    lo = lo & 0xFF;

    return (hi << 8) | lo;
}

int32_t Device_IO_BufferReadWord(Device *dev, uint8_t *buf, int32_t word_offset)
{
    (void)dev;
    if (!buf)
        return -1;

    int32_t offset = word_offset * 2;

    int16_t hi = buf[offset];
    if (hi < 0)
        return -1;
    hi = hi & 0xFF;

    int16_t lo = buf[offset + 1];
    if (lo < 0)
        return -1;
    lo = lo & 0xFF;

    return (hi << 8) | lo;
}



int32_t Device_IO_WriteWord(Device *dev, FILE *f, uint16_t data)
{
    (void)dev;
    if (!f)
        return -1;

    uint8_t hi = (data >> 8) & 0xFF;
    uint8_t lo = data & 0xFF;

    if (putc(hi, f) == EOF || putc(lo, f) == EOF) {
        if (ferror(f)) {
            LOG(LOG_CAT_DEVICE, LOG_ERROR, "Write failed: %s", strerror(errno));
        }
        return -1;
    }

    return 0;
}

int32_t Device_IO_BufferWriteWord(Device *dev,uint8_t *buf, int32_t word_offset, uint16_t data)
{
    (void)dev;
    if (!buf)
        return -1;

    uint8_t hi = (data >> 8) & 0xFF;
    uint8_t lo = data & 0xFF;

    int32_t offset = word_offset * 2;

    buf[offset] = hi;
    buf[offset + 1] = lo;

    return 0;
}

// DMA bypasses shadow memory (page tables) - it's a physical bus transfer.
// Set gDMAAccess flag so IsAddressShadowMemory skips the shadow check.

const char *gDMADeviceName = "?";

void Device_DMAWrite(uint32_t coreAddress, uint16_t data) {
    gDMAAccess = true;
    WritePhysicalMemory(coreAddress & 0xFFFFFF, data, false);
    gDMAAccess = false;
}

int32_t Device_DMARead(uint32_t coreAddress)
{
    gDMAAccess = true;
    int32_t result = ReadPhysicalMemory(coreAddress & 0xFFFFFF, false);
    gDMAAccess = false;
    return result;
}

// Character Device Functions

// Set character device output handler
void Device_SetCharacterOutput(Device *dev, CharacterDeviceOutputFunc outputFunc)
{
    if (!dev || dev->deviceClass != DEVICE_CLASS_CHARACTER)
        return;

    dev->charCallbacks.outputFunc = outputFunc;
}

// Set character device input handler
void Device_SetCharacterInput(Device *dev, CharacterDeviceInputFunc inputFunc)
{
    if (!dev || dev->deviceClass != DEVICE_CLASS_CHARACTER)
        return;

    dev->charCallbacks.inputFunc = inputFunc;
}

// Output a character from the device
void Device_OutputCharacter(Device *dev, char c)
{
    if (!dev || dev->deviceClass != DEVICE_CLASS_CHARACTER || !dev->charCallbacks.outputFunc)
        return;

    dev->charCallbacks.outputFunc(dev, c);
}

// Input a character to the device
void Device_InputCharacter(Device *dev, char c)
{
    if (!dev || dev->deviceClass != DEVICE_CLASS_CHARACTER || !dev->charCallbacks.inputFunc)
        return;

    dev->charCallbacks.inputFunc(dev, c);
}

// Block Device Functions

// Set block device read handler
void Device_SetBlockRead(Device *dev, BlockDeviceReadFunc readFunc, void *userData)
{
    if (!dev || dev->deviceClass != DEVICE_CLASS_BLOCK)
        return;

    dev->blockCallbacks.readFunc = readFunc;
    dev->blockCallbacks.userData = userData;
}

// Set block device write handler
void Device_SetBlockWrite(Device *dev, BlockDeviceWriteFunc writeFunc, void *userData)
{
    if (!dev || dev->deviceClass != DEVICE_CLASS_BLOCK)
        return;

    dev->blockCallbacks.writeFunc = writeFunc;
    if (userData != NULL) {
        dev->blockCallbacks.userData = userData;
    }
}

// Set block device disk info handler
void Device_SetBlockDiskInfo(Device *dev, BlockDeviceDiskInfoFunc infoFunc, void *userData)
{
    if (!dev || dev->deviceClass != DEVICE_CLASS_BLOCK)
        return;
    dev->blockCallbacks.diskInfoFunc = infoFunc;
    if (userData != NULL) {
        dev->blockCallbacks.userData = userData;
    }
}

// Read blocks from the device; returns number of blocks read, or -1 on error
int Device_ReadBlock(Device *dev, uint8_t *buffer, size_t size, uint32_t blockAddress, int unit)
{
    if (!dev || dev->deviceClass != DEVICE_CLASS_BLOCK || !dev->blockCallbacks.readFunc || !buffer)
        return -1;

    // The controller must pass the correct size for the current transfer
    return dev->blockCallbacks.readFunc(dev, buffer, size, blockAddress, unit);
}

// Write blocks to the device; returns number of blocks written, or -1 on error
int Device_WriteBlock(Device *dev, const uint8_t *buffer, size_t size, uint32_t blockAddress, int unit)
{
    if (!dev || dev->deviceClass != DEVICE_CLASS_BLOCK || !dev->blockCallbacks.writeFunc || !buffer)
        return -1;

    // The controller must pass the correct size for the current transfer
    return dev->blockCallbacks.writeFunc(dev, buffer, size, blockAddress, unit);
}