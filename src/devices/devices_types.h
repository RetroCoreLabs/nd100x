/*
 * devices_types.h - Shared device types, constants and the Device structure.
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


#ifndef DEVICES_TYPES_H
#define DEVICES_TYPES_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>


#include "../ndlib/ndlib_types.h" // for LogLevel def

// External function declaration

void interrupt(uint16_t lvl, uint16_t sub); // cpu.c

// Physical memory functions in cpu_mms.c
extern int ReadPhysicalMemory(int physical_address, bool privileged);
extern void WritePhysicalMemory(int physical_address, uint16_t value, bool privileged);

// ** Device **

#define MAX_DEVICES     16
#define MAX_DEVICE_NAME 64

// IO Delay definitions
#define IODELAY_TERMINAL     100
#define IODELAY_FLOPPY       300
#define IODELAY_HDD          10
#define IODELAY_HDD_SMD      10
#define IODELAY_LINEPRINTER  150
#define IODELAY_PAPERTAPE    200
#define IODELAY_SLOW         10
#define IODELAY_SCSI_SHORT   10
#define IODELAY_SCSI_TIMEOUT 0xFFFF

// Parity table size
#define PARITY_TABLE_SIZE 256
extern const uint8_t g_odd_parity_table[PARITY_TABLE_SIZE];


// Device initialization/classification types
// clang-format off
typedef enum {
    DEVICE_CLASS_STANDARD = 0,  // Standard device (no character or block I/O)
    DEVICE_CLASS_CHARACTER,     // Character device (terminal, serial, etc.)
    DEVICE_CLASS_BLOCK,         // Block device (disk, tape, etc.)
    DEVICE_CLASS_RTC            // Real-time clock device
} DeviceClass;
// clang-format on

// Forward declaration of Device structure
struct Device;

// Generic Character Device callback function types
typedef void (*CharacterDeviceOutputFunc)(struct Device *device, char c);
typedef void (*CharacterDeviceInputFunc)(struct Device *device, char c);

// Character Device callback structure
// clang-format off
typedef struct {
    CharacterDeviceOutputFunc outputFunc;  // Called when device outputs a character
    CharacterDeviceInputFunc inputFunc;    // Called when device receives input
} CharacterDeviceCallbacks;
// clang-format on

// Generic Block Device callback function types
#define MAX_BLOCK_SIZE 2048


typedef int (*BlockDeviceReadFunc)(struct Device *device, uint8_t *buffer, size_t size,
                                   uint32_t block_address, int unit);
typedef int (*BlockDeviceWriteFunc)(struct Device *device, const uint8_t *buffer, size_t size,
                                    uint32_t block_address, int unit);
typedef int (*BlockDeviceDiskInfoFunc)(struct Device *device, size_t *image_size,
                                       bool *is_write_protected, int unit);

// Block Device callback structure
// clang-format off
typedef struct {
    BlockDeviceReadFunc readFunc;        // Called when device reads a block
    BlockDeviceWriteFunc writeFunc;      // Called when device writes a block
    BlockDeviceDiskInfoFunc diskInfoFunc; // Called to read disk info (size, write-protect)
    void *userData;                      // User-defined data passed to callbacks (optional)
} BlockDeviceCallbacks;
// clang-format on

// IO Delay callback function type
typedef bool (*IODelayedCallback)(void *context, int param);

// IO Delay information structure
typedef struct
{
    int delayTicks;
    IODelayedCallback callback;
    void *context;
    int parameter;
    uint8_t level;
} DelayedIoInfo;

// Device types
// clang-format off
typedef enum {
    DEVICE_TYPE_NONE = 0,
    DEVICE_TYPE_RTC,
    DEVICE_TYPE_TERMINAL,
    DEVICE_TYPE_PAPER_TAPE,
    DEVICE_TYPE_FLOPPY_PIO,
    DEVICE_TYPE_FLOPPY_DMA,
    DEVICE_TYPE_DISC_SMD,
    DEVICE_TYPE_HDLC,
    DEVICE_TYPE_LINE_PRINTER,
    DEVICE_TYPE_PAPER_TAPE_WRITER,
    DEVICE_TYPE_DISC_SCSI,   /* ND-3201/3204 SCSI disk controller (NCR-5386) */
    DEVICE_TYPE_DRUM,          // NORD TSS swapping drum @ IOX 540
    DEVICE_TYPE_CDC,           // NORD TSS CDC/NCR cartridge system disc @ IOX 500
    /* 5 1/4 inch (ST506) / 8 inch Winchester disc controller, cards 3041/3038,
     * @ IOX 500-507 (system 1) or 510-517 (system 2). NOTE: system 1 shares its
     * address block with DEVICE_TYPE_CDC - a machine has one card or the other. */
    DEVICE_TYPE_DISC_WINCHESTER,
    DEVICE_TYPE_MAX
} DeviceType;
// clang-format on

// Device structure
// clang-format off
typedef struct Device {
    // Device memory range
    uint32_t startAddress;
    uint32_t endAddress;

    // Interrupt handling
    uint16_t interruptBits;
    uint16_t interruptLevel;  // Default interrupt level
    uint16_t identCode;      // Identcode for this device
    uint16_t logicalDevice;  // Logical device ID for this device
    DeviceType type;         // Read-only: concrete device type (set at creation)

    // Device name
    char memoryName[MAX_DEVICE_NAME];

    // IO Delay handling
    DelayedIoInfo *ioDelays;
    int ioDelayCount;
    int ioDelayCapacity;

    // Device functions
    void (*Reset)(struct Device *self);
    uint16_t (*Tick)(struct Device *self);
    int (*Boot)(struct Device *self, int unit);
    uint16_t (*Read)(struct Device *self, uint32_t address);
    void (*Write)(struct Device *self, uint32_t address, uint16_t value);
    uint16_t (*Ident)(struct Device *self, uint16_t level);

    /* NORD-1 compatible I/O (the IOT instruction, opcode 0160000).
     *
     * A NORD-10 keeps IOT as a separate instruction from IOX -- see
     * ND-06.008.01 sec 5, "IOT: NORD-1 compatible Input/Output", and the
     * NORD-10-S microprogram, where "IOX, IOT and IDENT are decoded
     * separately". They are NOT the same instruction:
     *
     *   IOX  bits 0-10 = a flat device-register address   (e.g. 500..507)
     *   IOT  bits 0-7  = a DEVICE NUMBER,
     *        bits 8-10 = function ACT / SKA / PIN, all zero = SNI
     *                    (NORD-1 Reference Manual sec 3.7)
     *
     * SKA means "skip if start acceptable": when the device is ready the CPU
     * skips the next instruction, which is what the classic wait loop
     * "IOT SKA DVN / JMP *-1" relies on.
     *
     * Devices reachable through a NORD-1 device number set nord1Device (and
     * nord1DeviceCount for a contiguous run) and implement IotOp. Returning
     * false means "not mine", and the caller falls back to the legacy
     * IOX-style handling. Set *skip to request the SKA/RST skip. */
    uint16_t nord1Device;       /* first NORD-1 device number, 0 = none */
    uint8_t  nord1DeviceCount;  /* how many consecutive numbers it answers */
    bool (*IotOp)(struct Device *self, uint8_t devno, uint8_t func,
                  uint16_t *reg_a, bool *skip);

    void (*Destroy)(struct Device *self);

    // Device classification
    DeviceClass deviceClass;  // Type of device (standard, character, block, RTC)

    // Block device properties (valid when deviceClass == DEVICE_CLASS_BLOCK)
    size_t blockSizeBytes;    // Sector/block size in bytes; set by the concrete device

    // Device callbacks
    CharacterDeviceCallbacks charCallbacks;  // Character device callbacks (if deviceClass == DEVICE_CLASS_CHARACTER)
    BlockDeviceCallbacks blockCallbacks;     // Block device callbacks (if deviceClass == DEVICE_CLASS_BLOCK)

    // Device-specific data
    void *deviceData;

} Device;
// clang-format on


typedef struct
{
    Device *devices[MAX_DEVICES];
    int count;
} DeviceList;


/**
 * @brief Look up the odd-parity bit for one byte value in g_odd_parity_table.
 * @param value The byte to check.
 * @return 1 if value has odd parity, 0 if even.
 */
uint8_t Device_GetOddParity(uint8_t);

/**
 * @brief Zero a Device and set up its class-specific fields and IO delay array.
 * @param dev The device to initialize.
 * @param thumbwheel Card thumbwheel setting; currently unused (unverified).
 * @param deviceClass Device classification (standard, character, block, RTC).
 * @param blockSize Block size in bytes for a DEVICE_CLASS_BLOCK device; clamped to
 *        MAX_BLOCK_SIZE, defaults to 1024 if 0 or out of range.
 */
void Device_Init(Device *, uint8_t, DeviceClass, size_t);

/**
 * @brief Call the device's own Destroy callback, then free its IO delay array and
 *        deviceData block.
 * @param dev The device to tear down.
 */
void Device_Destroy(Device *);

/**
 * @brief Call the device's Reset callback, if it has one.
 * @param dev The device to reset.
 */
void Device_Reset(Device *);

/**
 * @brief Call the device's Tick callback, if it has one.
 * @param dev The device to tick.
 * @return The interrupt bits returned by the device's Tick callback, or 0 if none.
 */
uint16_t Device_Tick(Device *);

/**
 * @brief Load boot code from the given unit on this controller into memory.
 * @param dev The controller device to boot from.
 * @param unit Unit number on the controller to boot from.
 * @return The boot address, or -1 on error or if the device has no Boot callback.
 */
int32_t Device_Boot(Device *, int);

/**
 * @brief Check whether an IOX address falls inside a device's registered range.
 * @param dev The device to check.
 * @param address The IOX address to test.
 * @return true if startAddress <= address <= endAddress, false otherwise.
 */
bool Device_IsInAddress(Device *, uint32_t);

/**
 * @brief Convert an absolute IOX address into an offset relative to the device's
 *        startAddress.
 * @param dev The device that owns the address range.
 * @param address The absolute IOX address.
 * @return address - dev->startAddress, or 0 if dev is NULL.
 */
uint32_t Device_RegisterAddress(Device *, uint32_t);

/**
 * @brief Call the device's Read callback, if it has one.
 * @param dev The device being read.
 * @param address The IOX address being read.
 * @return The value returned by the device's Read callback, or 0 if none.
 */
uint16_t Device_Read(Device *, uint32_t);

/**
 * @brief Call the device's Write callback, if it has one.
 * @param dev The device being written to.
 * @param address The IOX address being written.
 * @param value The value to write.
 */
void Device_Write(Device *, uint32_t, uint16_t);

/**
 * @brief Call the device's Ident callback, if it has one.
 * @param dev The device being identified.
 * @param level Interrupt level being identified.
 * @return The IDENT code returned by the device's Ident callback, or 0 if none.
 */
uint16_t Device_Ident(Device *, uint16_t);

/**
 * @brief Queue a delayed IO callback on a device, growing the delay array if full.
 * @param dev The device to queue the delay on.
 * @param ticks Number of ticks to wait before the callback fires.
 * @param cb Callback to invoke when the delay expires.
 * @param param Parameter passed to the callback.
 * @param irqlevel Interrupt level to raise if the callback returns true (0 = none).
 */
void Device_QueueIODelay(Device *dev, uint16_t ticks, IODelayedCallback cb, int param,
                         uint8_t irqlevel);

/**
 * @brief Advance all of a device's queued IO delays by one tick, firing and
 *        removing any that reach zero, and raising an interrupt if requested.
 * @param dev The device whose delay queue is ticked.
 */
void Device_TickIODelay(Device *);

/**
 * @brief Set a device's interrupt request bit for a level in the range 10-13.
 * @param dev The device raising the interrupt.
 * @param level Interrupt level (10-13); other levels are ignored.
 */
void Device_GenerateInterrupt(Device *, uint16_t);

/**
 * @brief Set or clear a device's interrupt request bit for a given level.
 * @param dev The device whose interrupt status is set.
 * @param active true to raise the interrupt, false to clear it.
 * @param level Interrupt level (10-13 apply; see Device_GenerateInterrupt).
 */
void Device_SetInterruptStatus(Device *, bool, uint16_t);

/**
 * @brief Seek an open device backing file to an absolute byte offset.
 * @param dev Unused.
 * @param f The open file to seek.
 * @param offset Absolute byte offset to seek to (SEEK_SET).
 * @return 0 on success, -1 if f is NULL or fseek fails.
 */
int32_t Device_IO_Seek(Device *, FILE *, int64_t);

/**
 * @brief Read one big-endian 16-bit word from an open device backing file.
 * @param dev Unused.
 * @param f The open file to read from.
 * @return The word read, or -1 on EOF/error or if f is NULL.
 */
int32_t Device_IO_ReadWord(Device *, FILE *);

/**
 * @brief Read one big-endian 16-bit word from an in-memory buffer at a word offset.
 * @param dev Unused.
 * @param buf The buffer to read from.
 * @param word_offset Offset in 16-bit words (byte offset = word_offset * 2).
 * @return The word read, or -1 if buf is NULL.
 */
int32_t Device_IO_BufferReadWord(Device *, uint8_t *, int32_t);

/**
 * @brief Write one big-endian 16-bit word to an open device backing file.
 * @param dev Unused.
 * @param f The open file to write to.
 * @param data The word to write.
 * @return 0 on success, -1 if f is NULL or the write fails.
 */
int32_t Device_IO_WriteWord(Device *, FILE *, uint16_t);

/**
 * @brief Write one big-endian 16-bit word into an in-memory buffer at a word offset.
 * @param dev Unused.
 * @param buf The buffer to write into.
 * @param word_offset Offset in 16-bit words (byte offset = word_offset * 2).
 * @param data The word to write.
 * @return 0 always.
 */
int32_t Device_IO_BufferWriteWord(Device *, uint8_t *, int32_t, uint16_t);

/**
 * @brief Write a 16-bit word directly to physical memory as a DMA bus transfer,
 *        bypassing the shadow-memory (page table) check.
 * @param coreAddress Physical core address (masked to 24 bits).
 * @param data The word to write.
 */
void Device_DMAWrite(uint32_t, uint16_t);

/**
 * @brief Read a word directly from physical memory as a DMA bus transfer,
 *        bypassing the shadow-memory (page table) check.
 * @param coreAddress Physical core address (masked to 24 bits).
 * @return The word read from physical memory.
 */
int32_t Device_DMARead(uint32_t);

/**
 * @brief Install the output-character callback for a DEVICE_CLASS_CHARACTER device.
 * @param dev The device to configure.
 * @param outputFunc Callback invoked when the device outputs a character.
 */
void Device_SetCharacterOutput(Device *, CharacterDeviceOutputFunc);

/**
 * @brief Install the input-character callback for a DEVICE_CLASS_CHARACTER device.
 * @param dev The device to configure.
 * @param inputFunc Callback invoked when the device receives input.
 */
void Device_SetCharacterInput(Device *, CharacterDeviceInputFunc);

/**
 * @brief Invoke a character device's output callback with one character.
 * @param dev The device outputting the character.
 * @param c The character being output.
 */
void Device_OutputCharacter(Device *, char);

/**
 * @brief Invoke a character device's input callback with one character.
 * @param dev The device receiving the character.
 * @param c The character being input.
 */
void Device_InputCharacter(Device *, char);

/**
 * @brief Install the block-read callback and user data for a DEVICE_CLASS_BLOCK
 *        device.
 * @param dev The device to configure.
 * @param readFunc Callback invoked to read a block.
 * @param userData Opaque data passed back to readFunc.
 */
void Device_SetBlockRead(Device *, BlockDeviceReadFunc, void *);

/**
 * @brief Install the block-write callback for a DEVICE_CLASS_BLOCK device.
 * @param dev The device to configure.
 * @param writeFunc Callback invoked to write a block.
 * @param userData Opaque data passed back to writeFunc; leaves the existing
 *        userData in place if NULL.
 */
void Device_SetBlockWrite(Device *, BlockDeviceWriteFunc, void *);

/**
 * @brief Install the block disk-info callback for a DEVICE_CLASS_BLOCK device.
 * @param dev The device to configure.
 * @param infoFunc Callback invoked to report disk size and write-protect state.
 * @param userData Opaque data passed back to infoFunc; leaves the existing
 *        userData in place if NULL.
 */
void Device_SetBlockDiskInfo(Device *, BlockDeviceDiskInfoFunc, void *);

/**
 * @brief Read one or more blocks from a DEVICE_CLASS_BLOCK device via its
 *        readFunc callback.
 * @param dev The device to read from.
 * @param buffer Destination buffer for the data read.
 * @param size Number of bytes to read.
 * @param blockAddress Starting block address.
 * @param unit Unit number on the device.
 * @return Whatever the device's readFunc returns, or -1 on error.
 */
int Device_ReadBlock(Device *, uint8_t *, size_t, uint32_t, int);

/**
 * @brief Write one or more blocks to a DEVICE_CLASS_BLOCK device via its
 *        writeFunc callback.
 * @param dev The device to write to.
 * @param buffer Source buffer holding the data to write.
 * @param size Number of bytes to write.
 * @param blockAddress Starting block address.
 * @param unit Unit number on the device.
 * @return Whatever the device's writeFunc returns, or -1 on error.
 */
int Device_WriteBlock(Device *, const uint8_t *, size_t, uint32_t, int);

// ** Device Manager **

// Device info structure
typedef struct
{
    Device *device;
} DeviceInfo;

// Device manager structure
typedef struct
{
    DeviceInfo *devices;
    int deviceCount;
    int deviceCapacity;
} DeviceManager;


/**
 * @brief Allocate the device manager's device table.
 * @return 0 on success, -1 if the table could not be allocated.
 */
int DeviceManager_Init(void);

/**
 * @brief Destroy every registered device and free the device table.
 */
void DeviceManager_Destroy(void);

/**
 * @brief Register the fixed set of always-present devices (panel, RTC, console,
 *        paper tape reader/writer, line printer, floppy DMA, SMD) at their
 *        standard IOX addresses. HDLC, SCSI, drum and CDC are added elsewhere,
 *        conditionally on configuration.
 */
void DeviceManager_AddAllDevices(void);

/**
 * @brief Add one device of the given type and thumbwheel setting to the device
 *        manager, refusing it if its IOX range overlaps an already-registered
 *        device.
 * @param type Device type to create.
 * @param thumbwheel Card thumbwheel setting passed to the device's constructor.
 * @return true if the device was created and added, false on failure or overlap.
 */
bool DeviceManager_AddDevice(DeviceType, uint8_t);

/**
 * @brief Find the registered device whose IOX range contains an address.
 * @param address The IOX address to look up.
 * @return The matching Device, or NULL if none is registered at that address.
 */
Device *DeviceManager_GetDeviceByAddress(uint32_t);

/**
 * @brief Add the HDLC controller and start its modem with the given TCP config.
 * @param thumbwheel HDLC card thumbwheel (1-4), selects the IOX base address.
 * @param isServer true to start the modem as a TCP server, false as a client.
 * @param address Remote address to connect to (client mode; unverified for
 *        server mode).
 * @param port TCP port for the modem connection.
 * @return true if the controller was added, false on failure.
 */
bool DeviceManager_AddHDLCDevice_WithConfig(int, bool, const char *, int);

/**
 * @brief Reset every registered device.
 */
void DeviceManager_MasterClear(void);

/**
 * @brief Dispatch an IOX read to the device whose range contains the address.
 * @param address The IOX address being read.
 * @return The value returned by the device, or 0 if no device answers (an IOX
 *         error interrupt is also raised on level 14 in that case).
 */
uint16_t DeviceManager_Read(uint32_t);

/**
 * @brief Dispatch an IOX write to the device whose range contains the address.
 * @param address The IOX address being written.
 * @param value The value to write.
 */
void DeviceManager_Write(uint32_t, uint16_t);

/**
 * @brief Find the device with a pending request on an interrupt level and IDENT
 *        it, clearing its request bit.
 * @param level Interrupt level being identified.
 * @return The IDENT code from the highest-priority pending device on that
 *         level, or 0 if none is pending.
 */
int DeviceManager_Ident(uint16_t);

/**
 * @brief Tick every registered device and collect their interrupt bits.
 * @return The bitwise OR of all devices' returned interrupt bits.
 */
uint16_t DeviceManager_Tick(void);

/**
 * @brief Get the number of devices currently registered.
 * @return The device count.
 */
int DeviceManager_GetDeviceCount(void);

/**
 * @brief Get a registered device by its index in the device table.
 * @param index Index into the device table.
 * @return The Device at that index, or NULL if index is out of range.
 */
Device *DeviceManager_GetDeviceByIndex(int);

/**
 * @brief Dispatch a NORD-1 style IOT operation to the device that owns the
 *        given device number.
 * @param devno NORD-1 device number.
 * @param func IOT function code (ACT / SKA / PIN / SNI).
 * @param regA Pointer to the A register value exchanged with the device.
 * @param skip Set to request the SKA/RST instruction skip.
 * @return true if a device claimed devno and handled the operation, false if
 *         no device answers it (caller falls back to legacy IOX handling).
 */
bool DeviceManager_IotOp(uint8_t, uint8_t, uint16_t *, bool *);

/**
 * @brief Boot from the first registered controller of a given device type.
 * @param type Device type to boot from (matched by controller's recorded type).
 * @param unit Unit number on the controller to boot from.
 * @return The boot address, or -1 if no such controller is registered or on
 *         boot error.
 */
int DeviceManager_BootFrom(DeviceType, int);

#include "./floppy/device_floppy_pio.h"
#include "./floppy/device_floppy_dma.h"
#include "./hdlc/dma_enum.h"
#include "./hdlc/dma_dcb.h"
#include "./hdlc/dma_param_buf.h"
#include "./hdlc/dma_control_blocks.h"
#include "./hdlc/dma_transmitter.h"
#include "./hdlc/dma_receiver.h"
#include "./hdlc/dma_engine.h"
#include "./hdlc/device_hdlc.h"
#include "./hdlc/chip_com5025.h"
#include "./hdlc/chip_com5025_registers.h"
#include "./hdlc/hdlc_crc.h"
#include "./papertape/device_paper_tape.h"
#include "./rtc/device_rtc.h"
#include "./smd/device_smd.h"
/* Winchester: geometry first (defines WDDiskInfo), then the controller. */
#include "./winchester/disk_winchester.h"
#include "./winchester/device_winchester.h"
/* SCSI: bus first (defines SCSIDevice/SCSIBus), then the chip, then the card. */
#include "./scsi/scsi_bus.h"
#include "./scsi/ncr5386.h"
#include "./scsi/scsi_device.h"
#include "./scsi/disk_scsi.h"
#include "./scsi/scsi_hdd.h"
#include "./scsi/device_scsi.h"
#include "./drum/device_drum.h"
#include "./cdc/device_cdc.h"
#include "./terminal/device_terminal.h"
#include "./lineprinter/device_line_printer.h"
#include "./papertapewriter/device_paper_tape_writer.h"
#include "./panel/panel.h"

/**
 * @brief Add the ND-3201/3204 SCSI controller and set each SCSI ID's target type.
 * @param thumbwheel SCSI card thumbwheel (TW2), selects the IOX base address.
 * @param unitTypes Array of SCSI_MAX_UNITS entries indexed by SCSI ID (0-6);
 *        SCSI_UNIT_NONE means no target at that ID.
 * @return true if the controller was added, false on failure.
 */
bool DeviceManager_AddSCSIDevice_WithConfig(int, const SCSIUnitType *);

#endif // DEVICES_TYPES_H
