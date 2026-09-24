/*
 * devicemanager.c - Device manager: creates, registers, clears and ticks all I/O devices.
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
#include <stdarg.h>

#include "devices_types.h"
#include "devices_protos.h"

#include "../ndlib/ndlib_types.h"
#include "../ndlib/ndlib_protos.h"

// For DRIVE_TYPE and machine-level block IO callbacks
#include "../machine/machine_types.h"
#include "../machine/machine_protos.h"

#define INITIAL_DEVICE_CAPACITY 32


// Define the level strings array

static DeviceManager device_manager = {0}; // Initialize to zero

// Returns 0, or -1 if the device table could not be allocated.
int devmgr_init(void)
{

    device_manager.deviceCapacity = INITIAL_DEVICE_CAPACITY;
    device_manager.deviceCount = 0;
    device_manager.devices = malloc(sizeof(DeviceInfo) * INITIAL_DEVICE_CAPACITY);
    if (device_manager.devices)
    {
        // Zero initialize the device array
        memset(device_manager.devices, 0, sizeof(DeviceInfo) * INITIAL_DEVICE_CAPACITY);
    }
    else
    {
        LOG(LOG_CAT_DEVICE, LOG_ERROR, "Failed to allocate device array\n");
        return -1;
    }
    return 0;
}

void devmgr_destroy(void)
{
    // Clean up all devices
    for (int i = 0; i < device_manager.deviceCount; i++)
    {
        if (device_manager.devices[i].device)
        {
            dev_destroy(device_manager.devices[i].device);
            free(device_manager.devices[i].device); // Free the device itself
            device_manager.devices[i].device = NULL;
        }
    }

    if (device_manager.devices)
    {
        free(device_manager.devices);
        device_manager.devices = NULL;
    }

    device_manager.deviceCount = 0;
    device_manager.deviceCapacity = 0;
}

void devmgr_add_all_devices(void)
{

    // Initialize the panel controller
    panel_setup_pap();

    // Add the RTC at octal 1570-1577
    devmgr_add_device(DEVICE_TYPE_RTC, 0);

    // Add the Console at octal 300-307
    devmgr_add_device(DEVICE_TYPE_TERMINAL, 0);

    // Add the PaperTape Reader at octal 400-403
    devmgr_add_device(DEVICE_TYPE_PAPER_TAPE, 0);

    // Add the PaperTape Writer (Punch) at octal 410-413
    devmgr_add_device(DEVICE_TYPE_PAPER_TAPE_WRITER, 0);

    // Add the Line Printer at octal 430-433
    devmgr_add_device(DEVICE_TYPE_LINE_PRINTER, 0);

    // The PIO floppy controller is not added: its address range 1560-1567 is
    // the DMA floppy's.

    // Add the FloppyDMA at octal 1560-1567
    devmgr_add_device(DEVICE_TYPE_FLOPPY_DMA, 0);

    // Add the SMD at octal 1540-1547
    devmgr_add_device(DEVICE_TYPE_DISC_SMD, 0);

    // The NORD TSS swapping drum (octal 540-547) is NOT added here. Like the CDC
    // and the SCSI controller, it is now GATED: installed only when a --drum image
    // (or the .ini drum= key) was given, via the conditional block in nd100x.c after
    // machine_init. Adding it unconditionally put an ident-less card at 540 that
    // tripped the normal-boot device probe ("No identcode found on level 11D ...
    // Device number 000540B"). Default boot therefore installs no drum.

    // Note: HDLC device is added conditionally via DeviceManager_AddHDLCDevice()
    // based on command line configuration

    // Note: the SCSI controller is added conditionally via
    // DeviceManager_AddSCSIDevice_WithConfig() based on command line
    // configuration. It is deliberately NOT added here - putting an extra card
    // in every machine's IOX map would change the hardware configuration of
    // every existing boot.
}

/* Add the ND-3201/3204 SCSI controller and set the target class for each SCSI
 * ID. unitTypes must have SCSI_MAX_UNITS entries, indexed by SCSI ID (0-6);
 * SCSI_UNIT_NONE means "no target at this ID".
 *
 * SCSI IOX bases by thumbwheel TW2: 0=0144300, 1=0144400, 2=0144500, 3=0144600.
 */
bool devmgr_add_scsi_device_with_config(int thumbwheel, const SCSIUnitType *unit_types)
{
    bool success = devmgr_add_device(DEVICE_TYPE_DISC_SCSI, (uint8_t)thumbwheel);

    if (success && unit_types)
    {
        static const uint16_t scsi_base_addr[] = {0144300, 0144400, 0144500, 0144600};
        Device *dev = devmgr_get_device_by_address(scsi_base_addr[thumbwheel & 0x03]);
        if (dev)
        {
            for (int unit = 0; unit < SCSI_MAX_UNITS; unit++)
            {
                if (unit_types[unit] != SCSI_UNIT_NONE)
                {
                    scsi_set_unit_type(dev, unit, unit_types[unit]);
                }
            }
        }
    }

    return success;
}

bool devmgr_add_hdlc_device_with_config(int thumbwheel, bool is_server, const char *address,
                                        int port)
{
    bool success = devmgr_add_device(DEVICE_TYPE_HDLC, (uint8_t)thumbwheel);

    if (success)
    {
        // Find the just-added device and start its modem with TCP config
        // HDLC base addresses: thumbwheel 1=01640, 2=01660, 3=01700, 4=01720
        static const uint16_t hdlc_base_addr[] = {0, 01640, 01660, 01700, 01720};
        if (thumbwheel >= 1 && thumbwheel <= 4)
        {
            Device *dev = devmgr_get_device_by_address(hdlc_base_addr[thumbwheel]);
            if (dev && dev->deviceData)
            {
                HDLCData *data = (HDLCData *)dev->deviceData;
                if (data->modem)
                {
                    modem_start(data->modem, is_server, address, port);
                }
            }
        }
    }

    return success;
}

static Device *create_device(DeviceType type, uint8_t thumbwheel)
{
    Device *dev = NULL;

    // Set up device-specific initialization based on type
    switch (type)
    {
    case DEVICE_TYPE_RTC:
        dev = rtc_create_device(thumbwheel);
        if (!dev)
        {
            LOG(LOG_CAT_DEVICE, LOG_ERROR, "Failed to create RTC device\n");
            return NULL;
        }
        break;
    case DEVICE_TYPE_OCTOBUS:
        dev = octobus_create_device(thumbwheel);
        if (!dev)
        {
            LOG(LOG_CAT_DEVICE, LOG_ERROR, "Failed to create octobus device\n");
            return NULL;
        }
        break;
    case DEVICE_TYPE_TERMINAL:
        dev = terminal_create_device(thumbwheel);
        if (!dev)
        {
            LOG(LOG_CAT_DEVICE, LOG_ERROR, "Failed to create terminal device\n");
            return NULL;
        }
        break;
    case DEVICE_TYPE_PAPER_TAPE:
        dev = ptr_create_paper_tape_device(thumbwheel);
        if (!dev)
        {
            LOG(LOG_CAT_DEVICE, LOG_ERROR, "Failed to create paper tape device\n");
            return NULL;
        }
        break;
    case DEVICE_TYPE_FLOPPY_PIO:
        dev = floppy_pio_create_device(thumbwheel);
        if (!dev)
        {
            LOG(LOG_CAT_DEVICE, LOG_ERROR, "Failed to create floppy PIO device\n");
            return NULL;
        }
        break;

    case DEVICE_TYPE_DISC_SMD:
        dev = smd_create_device(thumbwheel);
        if (!dev)
        {
            LOG(LOG_CAT_DEVICE, LOG_ERROR, "Failed to create SMD device\n");
            return NULL;
        }
        break;
    case DEVICE_TYPE_DISC_WINCHESTER:
        dev = wd_create_winchester_device(thumbwheel);
        if (!dev)
        {
            LOG(LOG_CAT_DEVICE, LOG_ERROR, "Failed to create Winchester device\n");
            return NULL;
        }
        break;
    case DEVICE_TYPE_DISC_SCSI:
        dev = scsi_create_device(thumbwheel);
        if (!dev)
        {
            LOG(LOG_CAT_DEVICE, LOG_ERROR, "Failed to create SCSI device\n");
            return NULL;
        }
        break;
    case DEVICE_TYPE_DRUM:
        dev = drum_create_device(thumbwheel);
        if (!dev)
        {
            LOG(LOG_CAT_DEVICE, LOG_ERROR, "Failed to create DRUM device\n");
            return NULL;
        }
        break;
    case DEVICE_TYPE_CDC:
        dev = cdc_create_device(thumbwheel);
        if (!dev)
        {
            LOG(LOG_CAT_DEVICE, LOG_ERROR, "Failed to create CDC disc device\n");
            return NULL;
        }
        break;
    case DEVICE_TYPE_FLOPPY_DMA:
        dev = floppy_dma_create_device(thumbwheel);
        if (!dev)
        {
            LOG(LOG_CAT_DEVICE, LOG_ERROR, "Failed to create floppy DMA device\n");
            return NULL;
        }
        break;
    case DEVICE_TYPE_LINE_PRINTER:
        dev = lp_create_line_printer_device(thumbwheel);
        if (!dev)
        {
            LOG(LOG_CAT_DEVICE, LOG_ERROR, "Failed to create line printer device\n");
            return NULL;
        }
        break;
    case DEVICE_TYPE_PAPER_TAPE_WRITER:
        dev = ptp_create_paper_tape_writer_device(thumbwheel);
        if (!dev)
        {
            LOG(LOG_CAT_DEVICE, LOG_ERROR, "Failed to create paper tape writer device\n");
            return NULL;
        }
        break;
    case DEVICE_TYPE_HDLC:
        dev = hdlc_create_device(thumbwheel);
        if (!dev)
        {
            LOG(LOG_CAT_DEVICE, LOG_ERROR, "Failed to create HDLC device\n");
            return NULL;
        }
        break;
    default:
        LOG(LOG_CAT_DEVICE, LOG_ERROR, "Unknown device type: %d\n", type);
        return NULL;
    }

    // Reset the device
    if (dev)
    {
        // Record the concrete type for downstream logic (read-only property)
        dev->type = type;
        dev_reset(dev);
    }

    return dev;
}

void devmgr_master_clear(void)
{
    for (int i = 0; i < device_manager.deviceCount; i++)
    {
        if (device_manager.devices[i].device)
        {
            dev_reset(device_manager.devices[i].device);
        }
    }
}

bool devmgr_add_device(DeviceType type, uint8_t thumbwheel)
{
    // Check if we have capacity
    if (device_manager.deviceCount >= device_manager.deviceCapacity)
    {
        LOG(LOG_CAT_DEVICE, LOG_ERROR,
            "Failed to add device: device array is full (capacity: %d, count: %d)\n",
            device_manager.deviceCapacity, device_manager.deviceCount);
        return false;
    }

    // Create and add new device
    Device *dev = create_device(type, thumbwheel);
    if (dev)
    {
        /* Refuse an IOX address block that overlaps a device already present.
         * Without this the later device silently shadows the earlier one and
         * the machine answers one card while the operator believes both are
         * fitted. The Winchester controller makes this reachable: it answers
         * 500-507, the same block as the CDC system disc, exactly as the real
         * cards would - a backplane holds one or the other. */
        for (int i = 0; i < device_manager.deviceCount; i++)
        {
            Device *other = device_manager.devices[i].device;
            if (!other)
            {
                continue;
            }
            if (dev->startAddress <= other->endAddress && other->startAddress <= dev->endAddress)
            {
                LOG(LOG_CAT_DEVICE, LOG_ERROR,
                    "Refusing to add '%s' (IOX %o-%o): that address block is already "
                    "answered by '%s' (IOX %o-%o). These cards cannot both be fitted.\n",
                    dev->memoryName, dev->startAddress, dev->endAddress, other->memoryName,
                    other->startAddress, other->endAddress);
                dev_destroy(dev);
                free(dev);
                return false;
            }
        }

        device_manager.devices[device_manager.deviceCount].device = dev;
        // If this is a block device, hook up machine-level block IO callbacks
        if (dev->deviceClass == DEVICE_CLASS_BLOCK)
        {
            dev_set_block_read(dev, machine_block_read, NULL);
            dev_set_block_write(dev, machine_block_write, NULL);
            dev_set_block_disk_info(dev, machine_block_disk_info, NULL);
        }
        device_manager.deviceCount++;
        return true;
    }
    else
    {
        LOG(LOG_CAT_DEVICE, LOG_ERROR, "Failed to create device\n");
    }

    return false;
}

uint16_t devmgr_read(uint32_t address)
{
    for (int i = 0; i < device_manager.deviceCount; i++)
    {

        Device *dev = device_manager.devices[i].device;

        if (dev && dev_is_in_address(dev, address))
        {
            return dev_read(dev, address);
        }
    }

    cpu_interrupt(14, 1 << 7); /* IOX error lvl14 */
    LOG(LOG_CAT_DEVICE, LOG_DEBUG, "No device found for READ address: %o\n", address);
    return 0;
}

void devmgr_write(uint32_t address, uint16_t value)
{
    for (int i = 0; i < device_manager.deviceCount; i++)
    {
        Device *dev = device_manager.devices[i].device;
        if (!dev)
        {
            LOG(LOG_CAT_DEVICE, LOG_ERROR, "Device at index %d is NULL\n", i);
            continue;
        }

        if (dev_is_in_address(dev, address))
        {
            dev_write(dev, address, value);
            return;
        }
    }

    cpu_interrupt(14, 1 << 7); /* IOX error lvl14 */
    LOG(LOG_CAT_DEVICE, LOG_DEBUG, "No device found for WRITE address: %o\n", address);
}

int devmgr_ident(uint16_t level)
{
    for (int i = 0; i < device_manager.deviceCount; i++)
    {
        Device *dev = device_manager.devices[i].device;
        if (dev && (dev->interruptBits & (1 << level)))
        {
            uint16_t id = dev_ident(dev, level);
            if (id > 0)
            {
                // IDENT is the ND-100 interrupt ACKNOWLEDGE: identifying the
                // highest-priority device on this level clears ITS interrupt
                // request, so the level de-asserts. (If another device on the
                // same level is still pending, a subsequent IDENT services it.)
                // Without this, a device that raised a level kept the request
                // asserted after being serviced, so its level handler re-fired
                // forever - e.g. NORD TSS's LEV12 console-input handler did
                // "IDENT PL12 ... WAIT; JMP LEV12", and the terminal kept
                // re-asserting level 12 after each char, starving LOGON.
                dev->interruptBits &= ~(1 << level);
                return id;
            }
        }
    }

    // interrupt(14,1<<7); /* IOX error lvl14 */
    LOG(LOG_CAT_DEVICE, LOG_DEBUG, "No device found for IDENT level: %d\n", level);

    return 0;
}

uint16_t devmgr_tick(void)
{
    uint16_t interrupt_bits = 0;
    for (int i = 0; i < device_manager.deviceCount; i++)
    {
        Device *dev = device_manager.devices[i].device;
        if (dev)
        {
            interrupt_bits |= dev_tick(dev);
        }
    }

    return interrupt_bits;
}
Device *devmgr_get_device_by_address(uint32_t address)
{
    for (int i = 0; i < device_manager.deviceCount; i++)
    {
        Device *dev = device_manager.devices[i].device;
        if (dev && dev_is_in_address(dev, address))
        {
            return dev;
        }
    }

    return NULL;
}

int devmgr_get_device_count(void)
{
    return device_manager.deviceCount;
}

Device *devmgr_get_device_by_index(int index)
{
    if (index < 0 || index >= device_manager.deviceCount)
    {
        return NULL;
    }
    return device_manager.devices[index].device;
}

// Loads boot code from disk to memory. Returns the boot address, or -1 if error.
//
// The controller is found by its device TYPE (DEVICE_TYPE_DISC_SMD,
// DEVICE_TYPE_DISC_SCSI, ...), not by IOX address. The old address lookup had
// to mask boot-mode flag bits (bit 15 = BPUN load, bit 13 = bootstrap, as the
// real ND boot code encodes them in the load device number) out of the id
// first, which broke for the SCSI card: its IOX base 0144300 has bit 15 set as
// part of the ADDRESS. Booting by type + unit sidesteps that entirely.
//
// Note: each controller's Boot function performs a MEMORY boot (first blocks
// of the unit loaded to address 0). BPUN and bootstrap boot modes are handled
// elsewhere (program_load) or not implemented.
bool devmgr_iot_op(uint8_t devno, uint8_t func, uint16_t *reg_a, bool *skip)
{
    for (int i = 0; i < device_manager.deviceCount; i++)
    {
        Device *dev = device_manager.devices[i].device;
        if (!dev || !dev->IotOp || dev->nord1Device == 0)
        {
            continue;
        }
        uint16_t first = dev->nord1Device;
        uint16_t count = dev->nord1DeviceCount ? dev->nord1DeviceCount : 1;
        if (devno >= first && devno < first + count)
        {
            return dev->IotOp(dev, devno, func, reg_a, skip);
        }
    }
    return false;
}

int devmgr_boot_from(DeviceType type, int unit)
{
    for (int i = 0; i < device_manager.deviceCount; i++)
    {
        Device *dev = device_manager.devices[i].device;
        if (dev && dev->type == type)
        {
            return dev_boot(dev, unit);
        }
    }

    LOG(LOG_CAT_DEVICE, LOG_WARN, "No controller of device type %d present to boot from\n", type);
    return -1;
}
