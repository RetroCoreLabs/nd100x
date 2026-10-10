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
#ifdef ND100X_WITH_ETHERNET
#include "ethernet/device_ethernet.h"
#include "m68k.h" /* M68K_REG_PC / M68K_REG_SR */
#endif

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

#ifdef ND100X_WITH_ETHERNET
_Static_assert(ETHERNET_FRAME_MAX_BYTES == ETH_FRAME_LOG_BYTES, "frame size differs from the card's log");
_Static_assert(ETHERNET_FRAME_LOG_SIZE == ETH_FRAME_LOG_SIZE, "frame count differs from the card's log");

/* The n'th Ethernet II card's state, or NULL. */
static EthCard *nth_ethernet_card(int n)
{
    int seen = 0;
    for (int i = 0; i < devmgr_get_device_count(); i++)
    {
        Device *dev = devmgr_get_device_by_index(i);
        if ((dev != NULL) && (dev->type == DEVICE_TYPE_ETHERNET) && (dev->deviceData != NULL))
        {
            if (seen++ == n)
            {
                return eth_card(dev);
            }
        }
    }
    return NULL;
}
#endif

int devmgr_get_ethernet_frames(int n, EthernetFrame *out, int max)
{
#ifdef ND100X_WITH_ETHERNET
    static EthFrameLogEntry tmp[ETH_FRAME_LOG_SIZE];
    const EthCard *c = nth_ethernet_card(n);
    int count;

    if ((c == NULL) || (out == NULL) || (max <= 0))
    {
        return 0;
    }
    count = eth_get_recent_frames(c, tmp, (max < ETH_FRAME_LOG_SIZE) ? max : ETH_FRAME_LOG_SIZE);
    for (int i = 0; i < count; i++)
    {
        out[i].seq = tmp[i].seq;
        out[i].time_ms = tmp[i].time_ms;
        out[i].is_tx = tmp[i].is_tx;
        out[i].length = tmp[i].length;
        out[i].captured = tmp[i].captured;
        memcpy(out[i].data, tmp[i].data, (size_t)tmp[i].captured);
    }
    return count;
#else
    (void)n;
    (void)out;
    (void)max;
    return 0;
#endif
}

void devmgr_clear_ethernet_frames(int n)
{
#ifdef ND100X_WITH_ETHERNET
    eth_clear_recent_frames(nth_ethernet_card(n));
#else
    (void)n;
#endif
}

bool devmgr_get_ethernet_status(int n, EthernetStatus *out)
{
#ifdef ND100X_WITH_ETHERNET
    int seen = 0;

    if (out == NULL)
    {
        return false;
    }
    for (int i = 0; i < devmgr_get_device_count(); i++)
    {
        Device *dev = devmgr_get_device_by_index(i);
        const EthCard *c;
        EthNetStats ns;
        if ((dev == NULL) || (dev->type != DEVICE_TYPE_ETHERNET) || (dev->deviceData == NULL))
        {
            continue;
        }
        if (seen++ != n)
        {
            continue;
        }
        c = eth_card(dev);
        memset(out, 0, sizeof *out);
        out->thumbwheel = c->thumbwheel;
        out->iox_start = dev->startAddress;
        out->iox_end = dev->endAddress;
        out->ident = dev->identCode;
        out->level = dev->interruptLevel;
        out->memory_bank = c->memory_bank;
        out->window_byte_address = c->physical_page_start;
        out->nd_window_reads = c->nd_window_reads;
        out->nd_window_writes = c->nd_window_writes;
        out->interrupt_enabled = c->interrupt_enabled;
        out->interrupt_pending = (dev->interruptBits & (1u << (unsigned)dev->interruptLevel)) != 0u;
        out->halt = c->halt;
        out->reset = c->reset;
        out->m68k_running = eth_is_68k_running(c);
        out->m68k_halted = eth_m68k_is_halted(&c->cpu);
        out->m68k_stopped = m68k_is_stopped() != 0; /* Musashi fork 4188dc5; one global CPU */
        out->m68k_pc = eth_m68k_get_reg(&c->cpu, M68K_REG_PC);
        out->m68k_sr = eth_m68k_get_reg(&c->cpu, M68K_REG_SR);
        out->lance_initialized = c->mem.lance.initialized;
        out->lance_csr0 = c->mem.lance.csr[0];
        lance_get_physical_address(&c->mem.lance, out->mac);
        out->lance_rx_queued = c->mem.lance.rx_queue_count;
        out->tx_packets = c->tx_packets;
        out->tx_bytes = c->tx_bytes;
        out->rx_packets = c->rx_packets;
        out->rx_bytes = c->rx_bytes;
        out->runt_frames_padded = c->runt_frames_padded;
        out->own_echoes_dropped = c->own_echoes_dropped;
        out->checksums_repaired = c->checksums_repaired;
        out->net_attached = (c->net != NULL);
        if (c->net != NULL)
        {
            out->net_active = eth_net_is_active(c->net);
            (void)snprintf(out->net_description, sizeof out->net_description, "%s",
                           eth_net_description(c->net));
            eth_net_get_stats(c->net, &ns);
            out->net_frames_sent = ns.frames_sent;
            out->net_frames_received = ns.frames_received;
            out->net_frames_dropped_ring = ns.frames_dropped_ring;
            out->net_send_failures = ns.send_failures;
            out->net_receive_errors = ns.receive_errors;
            out->net_links_up = ns.links_up;
        }
        return true;
    }
    return false;
#else
    (void)n;
    (void)out;
    return false;
#endif
}

bool devmgr_add_ethernet_device(int thumbwheel, int memory_bank, FILE *trace, const char *net_spec)
{
#ifdef ND100X_WITH_ETHERNET
    static const uint16_t eth_base_addr[] = {0140360, 0140364, 0140370, 0140374};
    Device *dev;

    /* Musashi holds one 68000 in global state (eth_m68k.c s_cur, plan decision D6), so a
     * second card would run on the first card's CPU. Refuse it until per-card CPU contexts
     * exist. */
    for (int i = 0; i < devmgr_get_device_count(); i++)
    {
        Device *other = devmgr_get_device_by_index(i);
        if ((other != NULL) && (other->type == DEVICE_TYPE_ETHERNET))
        {
            LOG(LOG_CAT_DEVICE, LOG_ERROR,
                "Ethernet II %d: only one Ethernet II card is supported in this version; "
                "a card at IOX %o is already configured\n", thumbwheel, (unsigned)other->startAddress);
            return false;
        }
    }
    if ((thumbwheel < 0) || (thumbwheel > 3) ||
        !devmgr_add_device(DEVICE_TYPE_ETHERNET, (uint8_t)thumbwheel))
    {
        return false;
    }
    dev = devmgr_get_device_by_address(eth_base_addr[thumbwheel]);
    if ((dev == NULL) || (dev->deviceData == NULL))
    {
        return false;
    }
    if (eth_set_memory_bank(eth_card(dev), (uint16_t)memory_bank) != 0)
    {
        LOG(LOG_CAT_DEVICE, LOG_ERROR, "Ethernet II %d: bank %d cannot be used for its DRAM window\n",
            thumbwheel, memory_bank);
        return false;
    }
    LOG(LOG_CAT_DEVICE, LOG_INFO,
        "Ethernet II %d Device object created. Address[%o-%o] Ident code: [%o] Level: [%d] "
        "DRAM bank %u = ND-100 byte address 0x%X, 512 KB\n",
        thumbwheel, (unsigned)dev->startAddress, (unsigned)dev->endAddress, (unsigned)dev->identCode,
        dev->interruptLevel, (unsigned)eth_card(dev)->memory_bank,
        (unsigned)eth_card(dev)->physical_page_start);
    if (trace != NULL)
    {
        eth_set_trace(eth_card(dev), trace, false);
    }
    if ((net_spec != NULL) && (net_spec[0] != '\0') && (eth_attach_network(eth_card(dev), net_spec) != 0))
    {
        return false;
    }
    return true;
#else
    (void)memory_bank;
    (void)trace;
    (void)net_spec;
    LOG(LOG_CAT_DEVICE, LOG_ERROR,
        "Ethernet II %d: this nd100x was built without the Ethernet controller (ND100X_ENABLE_ETHERNET=OFF)\n",
        thumbwheel);
    return false;
#endif
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
#ifdef ND100X_WITH_ETHERNET
    case DEVICE_TYPE_ETHERNET:
        dev = eth_create_device(thumbwheel);
        if (!dev)
        {
            LOG(LOG_CAT_DEVICE, LOG_ERROR, "Failed to create Ethernet II device %d\n", thumbwheel);
        }
        break;
#endif
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
