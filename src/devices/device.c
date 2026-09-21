/*
 * device.c - Common device base: init, reset, tick, boot, address registration, parity.
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
#include <errno.h>
#include <inttypes.h>

#include "devices_types.h"
#include "../cpu/cpu_types.h" /* gDMAAccess */
#include "devices_protos.h"


#define INITIAL_IO_DELAY_CAPACITY 16

// Odd parity lookup table
const uint8_t g_odd_parity_table[PARITY_TABLE_SIZE] = {
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0};

// Helper function to get odd parity for a value
uint8_t dev_get_odd_parity(uint8_t value)
{
    return g_odd_parity_table[value];
}


void dev_init(Device *dev, uint8_t thumbwheel, DeviceClass device_class, size_t block_size)
{
    (void)thumbwheel;
    if (!dev)
    {
        return;
    }

    // Clear all fields
    memset(dev, 0, sizeof(Device));

    // Set basic device properties
    dev->startAddress = 0;
    dev->endAddress = 0;
    dev->interruptBits = 0;
    dev->interruptLevel = 0;
    dev->identCode = 0;
    dev->deviceClass = device_class;

    // Initialize IO delay array
    dev->ioDelays = malloc(sizeof(DelayedIoInfo) * INITIAL_IO_DELAY_CAPACITY);
    if (dev->ioDelays)
    {
        dev->ioDelayCapacity = INITIAL_IO_DELAY_CAPACITY;
        dev->ioDelayCount = 0;
    }

    // Initialize based on device class
    switch (device_class)
    {
    case DEVICE_CLASS_CHARACTER:
        // Initialize character device callbacks
        memset(&dev->charCallbacks, 0, sizeof(CharacterDeviceCallbacks));
        break;

    case DEVICE_CLASS_BLOCK:
        // Initialize block device callbacks
        memset(&dev->blockCallbacks, 0, sizeof(BlockDeviceCallbacks));
        dev->blockSizeBytes = (block_size > 0 && block_size <= MAX_BLOCK_SIZE) ? block_size : 1024;
        break;

    case DEVICE_CLASS_RTC:
    case DEVICE_CLASS_STANDARD:
    default:
        // No special initialization needed
        break;
    }
}

void dev_destroy(Device *dev)
{
    if (!dev)
    {
        return;
    }

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

void dev_reset(Device *dev)
{
    if (!dev || !dev->Reset)
    {
        return;
    }
    dev->Reset(dev);
}

uint16_t dev_tick(Device *dev)
{
    if (!dev || !dev->Tick)
    {
        return 0;
    }
    return dev->Tick(dev);
}

// Loads boot code from the given unit on this controller to memory.
// Returns the boot address, or -1 if error
int32_t dev_boot(Device *dev, int unit)
{
    if (!dev)
    {
        return -1;
    }
    if (dev->Boot == NULL)
    {
        return -1; // No boot function defined
    }

    return dev->Boot(dev, unit);
}

bool dev_is_in_address(Device *dev, uint32_t address)
{
    if (!dev)
    {
        return false;
    }
    return (address >= dev->startAddress && address <= dev->endAddress);
}

uint32_t dev_register_address(Device *dev, uint32_t address)
{
    if (!dev)
    {
        return 0;
    }
    return address - dev->startAddress;
}

uint16_t dev_read(Device *dev, uint32_t address)
{
    if (!dev || !dev->Read)
    {
        return 0;
    }
    return dev->Read(dev, address);
}

void dev_write(Device *dev, uint32_t address, uint16_t value)
{
    if (!dev || !dev->Write)
    {
        return;
    }
    dev->Write(dev, address, value);
}

uint16_t dev_ident(Device *dev, uint16_t level)
{
    if (!dev || !dev->Ident)
    {
        return 0;
    }
    return dev->Ident(dev, level);
}

void dev_queue_io_delay(Device *dev, uint16_t ticks, IODelayedCallback cb, int param,
                        uint8_t irqlevel)
{
    if (!dev || !dev->ioDelays)
    {
        return;
    }

    // Resize array if needed
    if (dev->ioDelayCount >= dev->ioDelayCapacity)
    {
        int new_capacity = dev->ioDelayCapacity * 2;
        DelayedIoInfo *new_delays = realloc(dev->ioDelays, sizeof(DelayedIoInfo) * new_capacity);
        if (!new_delays)
        {
            return;
        }

        dev->ioDelays = new_delays;
        dev->ioDelayCapacity = new_capacity;
    }

    // Add new delay
    DelayedIoInfo *delay = &dev->ioDelays[dev->ioDelayCount++];
    delay->delayTicks = ticks;
    delay->callback = cb;
    delay->context = dev;
    delay->parameter = param;
    delay->level = irqlevel;
}

void dev_tick_io_delay(Device *dev)
{
    if (!dev || !dev->ioDelays)
    {
        return;
    }

    for (int i = 0; i < dev->ioDelayCount; i++)
    {
        DelayedIoInfo *delay = &dev->ioDelays[i];
        delay->delayTicks--;

        if (delay->delayTicks <= 0)
        {
            bool triggered = delay->callback(delay->context, delay->parameter);
            if (triggered && delay->level > 0)
            {
                dev_generate_interrupt(dev, delay->level);
            }
            // Remove this delay by shifting remaining ones
            memmove(&dev->ioDelays[i], &dev->ioDelays[i + 1],
                    (dev->ioDelayCount - i - 1) * sizeof(DelayedIoInfo));
            dev->ioDelayCount--;
            i--; // Recheck this position
        }
    }
}

static void device_clear_interrupt(Device *dev, uint16_t level)
{
    if (!dev)
    {
        return;
    }

    if ((level >= 10) && (level <= 13))
    {
        if ((dev->interruptBits & (1 << level)) != 0)
        {
            dev->interruptBits &= ~(1 << level);
        }
    }
}

void dev_generate_interrupt(Device *dev, uint16_t level)
{
    if (!dev)
    {
        return;
    }

    if ((level >= 10) && (level <= 13))
    {
        if ((dev->interruptBits & (1 << level)) == 0)
        {
            dev->interruptBits |= (1 << level);
        }
    }
}

void dev_set_interrupt_status(Device *dev, bool active, uint16_t level)
{
    if (!dev)
    {
        return;
    }

    if (active)
    {
        dev_generate_interrupt(dev, level);
    }
    else
    {
        device_clear_interrupt(dev, level);
    }
}

int32_t dev_io_seek(Device *dev, FILE *f, int64_t offset)
{
    (void)dev;
    if (!f)
    {
        return -1;
    }
    return fseek(f, (long)offset, SEEK_SET); /* fseek takes long; images are far below 2 GB */
}


int32_t dev_io_read_word(Device *dev, FILE *f)
{
    (void)dev;
    if (!f)
    {
        return -1;
    }

    int16_t hi = getc(f);
    if (hi < 0)
    {
        return -1;
    }
    hi = hi & 0xFF;

    int16_t lo = getc(f);
    if (lo < 0)
    {
        return -1;
    }
    lo = lo & 0xFF;

    return (hi << 8) | lo;
}

int32_t dev_io_buffer_read_word(Device *dev, uint8_t *buf, int32_t word_offset)
{
    (void)dev;
    if (!buf)
    {
        return -1;
    }

    int32_t offset = word_offset * 2;

    int16_t hi = buf[offset];
    if (hi < 0)
    {
        return -1;
    }
    hi = hi & 0xFF;

    int16_t lo = buf[offset + 1];
    if (lo < 0)
    {
        return -1;
    }
    lo = lo & 0xFF;

    return (hi << 8) | lo;
}


int32_t dev_io_write_word(Device *dev, FILE *f, uint16_t data)
{
    (void)dev;
    if (!f)
    {
        return -1;
    }

    uint8_t hi = (data >> 8) & 0xFF;
    uint8_t lo = data & 0xFF;

    if (putc(hi, f) == EOF || putc(lo, f) == EOF)
    {
        if (ferror(f))
        {
            LOG(LOG_CAT_DEVICE, LOG_ERROR, "Write failed: %s", strerror(errno));
        }
        return -1;
    }

    return 0;
}

int32_t dev_io_buffer_write_word(Device *dev, uint8_t *buf, int32_t word_offset, uint16_t data)
{
    (void)dev;
    if (!buf)
    {
        return -1;
    }

    uint8_t hi = (data >> 8) & 0xFF;
    uint8_t lo = data & 0xFF;

    int32_t offset = word_offset * 2;

    buf[offset] = hi;
    buf[offset + 1] = lo;

    return 0;
}

// DMA bypasses shadow memory (page tables) - it's a physical bus transfer.
// Set g_dma_access so IsAddressShadowMemory skips the shadow check.

void dev_dma_write(uint32_t core_address, uint16_t data)
{
    g_dma_access = true;
    mms_write_physical_memory(core_address & 0xFFFFFF, data, false);
    g_dma_access = false;
}

int32_t dev_dma_read(uint32_t core_address)
{
    g_dma_access = true;
    int32_t result = mms_read_physical_memory(core_address & 0xFFFFFF, false);
    g_dma_access = false;
    return result;
}

// Character Device Functions

// Set character device output handler
void dev_set_character_output(Device *dev, CharacterDeviceOutputFunc output_func)
{
    if (!dev || dev->deviceClass != DEVICE_CLASS_CHARACTER)
    {
        return;
    }

    dev->charCallbacks.outputFunc = output_func;
}

// Set character device input handler
void dev_set_character_input(Device *dev, CharacterDeviceInputFunc input_func)
{
    if (!dev || dev->deviceClass != DEVICE_CLASS_CHARACTER)
    {
        return;
    }

    dev->charCallbacks.inputFunc = input_func;
}

// Output a character from the device
void dev_output_character(Device *dev, char c)
{
    if (!dev || dev->deviceClass != DEVICE_CLASS_CHARACTER || !dev->charCallbacks.outputFunc)
    {
        return;
    }

    dev->charCallbacks.outputFunc(dev, c);
}

// Input a character to the device
void dev_input_character(Device *dev, char c)
{
    if (!dev || dev->deviceClass != DEVICE_CLASS_CHARACTER || !dev->charCallbacks.inputFunc)
    {
        return;
    }

    dev->charCallbacks.inputFunc(dev, c);
}

// Block Device Functions

// Set block device read handler
void dev_set_block_read(Device *dev, BlockDeviceReadFunc read_func, void *user_data)
{
    if (!dev || dev->deviceClass != DEVICE_CLASS_BLOCK)
    {
        return;
    }

    dev->blockCallbacks.readFunc = read_func;
    dev->blockCallbacks.userData = user_data;
}

// Set block device write handler
void dev_set_block_write(Device *dev, BlockDeviceWriteFunc write_func, void *user_data)
{
    if (!dev || dev->deviceClass != DEVICE_CLASS_BLOCK)
    {
        return;
    }

    dev->blockCallbacks.writeFunc = write_func;
    if (user_data != NULL)
    {
        dev->blockCallbacks.userData = user_data;
    }
}

// Set block device disk info handler
void dev_set_block_disk_info(Device *dev, BlockDeviceDiskInfoFunc info_func, void *user_data)
{
    if (!dev || dev->deviceClass != DEVICE_CLASS_BLOCK)
    {
        return;
    }
    dev->blockCallbacks.diskInfoFunc = info_func;
    if (user_data != NULL)
    {
        dev->blockCallbacks.userData = user_data;
    }
}

// Read blocks from the device; returns number of blocks read, or -1 on error
int dev_read_block(Device *dev, uint8_t *buffer, size_t size, uint32_t block_address, int unit)
{
    if (!dev || dev->deviceClass != DEVICE_CLASS_BLOCK || !dev->blockCallbacks.readFunc || !buffer)
    {
        return -1;
    }

    // The controller must pass the correct size for the current transfer
    return dev->blockCallbacks.readFunc(dev, buffer, size, block_address, unit);
}

// Write blocks to the device; returns number of blocks written, or -1 on error
int dev_write_block(Device *dev, const uint8_t *buffer, size_t size, uint32_t block_address,
                    int unit)
{
    if (!dev || dev->deviceClass != DEVICE_CLASS_BLOCK || !dev->blockCallbacks.writeFunc || !buffer)
    {
        return -1;
    }

    // The controller must pass the correct size for the current transfer
    return dev->blockCallbacks.writeFunc(dev, buffer, size, block_address, unit);
}
