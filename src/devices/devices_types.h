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


#ifndef DEVICES_TYPES_H
#define DEVICES_TYPES_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>


#include "../ndlib/ndlib_types.h" // for LogLevel def

// External function declaration

void interrupt(uint16_t lvl, uint16_t sub); // cpu.c

// Physical memory functions in cpu_mms.c
extern int ReadPhysicalMemory(int physicalAddress, bool privileged);
extern void WritePhysicalMemory(int physicalAddress, uint16_t value, bool privileged);

// ** Device **

#define MAX_DEVICES 16
#define MAX_DEVICE_NAME 64

// IO Delay definitions
#define IODELAY_TERMINAL 100
#define IODELAY_FLOPPY 300
#define IODELAY_HDD 10
#define IODELAY_HDD_SMD 10
#define IODELAY_LINEPRINTER 150
#define IODELAY_PAPERTAPE 200
#define IODELAY_SLOW 10
#define IODELAY_SCSI_SHORT 10
#define IODELAY_SCSI_TIMEOUT 0xFFFF

// Parity table size
#define PARITY_TABLE_SIZE 256
extern const uint8_t Device_OddParityTable[PARITY_TABLE_SIZE];


// Device initialization/classification types
typedef enum {
    DEVICE_CLASS_STANDARD = 0,  // Standard device (no character or block I/O)
    DEVICE_CLASS_CHARACTER,     // Character device (terminal, serial, etc.)
    DEVICE_CLASS_BLOCK,         // Block device (disk, tape, etc.)
    DEVICE_CLASS_RTC            // Real-time clock device
} DeviceClass;

// Forward declaration of Device structure
struct Device;

// Generic Character Device callback function types
typedef void (*CharacterDeviceOutputFunc)(struct Device *device, char c);
typedef void (*CharacterDeviceInputFunc)(struct Device *device, char c);

// Character Device callback structure
typedef struct {
    CharacterDeviceOutputFunc outputFunc;  // Called when device outputs a character
    CharacterDeviceInputFunc inputFunc;    // Called when device receives input
} CharacterDeviceCallbacks;

// Generic Block Device callback function types
#define MAX_BLOCK_SIZE 2048


typedef int (*BlockDeviceReadFunc)(struct Device *device, uint8_t *buffer, size_t size, uint32_t blockAddress, int unit);
typedef int (*BlockDeviceWriteFunc)(struct Device *device, const uint8_t *buffer, size_t size, uint32_t blockAddress, int unit);
typedef int (*BlockDeviceDiskInfoFunc)(struct Device *device, size_t *image_size, bool *is_write_protected, int unit);

// Block Device callback structure
typedef struct {
    BlockDeviceReadFunc readFunc;        // Called when device reads a block
    BlockDeviceWriteFunc writeFunc;      // Called when device writes a block
    BlockDeviceDiskInfoFunc diskInfoFunc; // Called to read disk info (size, write-protect)
    void *userData;                      // User-defined data passed to callbacks (optional)
} BlockDeviceCallbacks;

// IO Delay callback function type
typedef bool (*IODelayedCallback)(void *context, int param);

// IO Delay information structure
typedef struct {
    int delayTicks;
    IODelayedCallback callback;
    void *context;
    int parameter;
    uint8_t level;
} DelayedIoInfo;

// Device types
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

// Device structure
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
                  uint16_t *regA, bool *skip);

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


typedef struct {
    Device *devices[MAX_DEVICES];
    int count;
} DeviceList;


// ** Device Manager **

// Device info structure
typedef struct {
    Device *device;
} DeviceInfo;

// Device manager structure
typedef struct {
    DeviceInfo *devices;
    int deviceCount;
    int deviceCapacity;
} DeviceManager;



#include "./floppy/deviceFloppyPIO.h"
#include "./floppy/deviceFloppyDMA.h"
#include "./hdlc/dmaEnum.h"
#include "./hdlc/dmaDCB.h"
#include "./hdlc/dmaParamBuf.h"
#include "./hdlc/dmaControlBlocks.h"
#include "./hdlc/dmaTransmitter.h"
#include "./hdlc/dmaReceiver.h"
#include "./hdlc/dmaEngine.h"
#include "./hdlc/deviceHDLC.h"
#include "./hdlc/chipCOM5025.h"
#include "./hdlc/chipCOM5025Registers.h"
#include "./hdlc/hdlc_crc.h"
#include "./papertape/devicePapertape.h"
#include "./rtc/deviceRTC.h"
#include "./smd/deviceSMD.h"
/* Winchester: geometry first (defines WDDiskInfo), then the controller. */
#include "./winchester/diskWinchester.h"
#include "./winchester/deviceWinchester.h"
/* SCSI: bus first (defines SCSIDevice/SCSIBus), then the chip, then the card. */
#include "./scsi/scsiBus.h"
#include "./scsi/ncr5386.h"
#include "./scsi/scsiDevice.h"
#include "./scsi/diskSCSI.h"
#include "./scsi/scsiHDD.h"
#include "./scsi/deviceSCSI.h"
#include "./drum/deviceDrum.h"
#include "./cdc/deviceCDC.h"
#include "./terminal/deviceTerminal.h"
#include "./lineprinter/deviceLinePrinter.h"
#include "./papertapewriter/devicePaperTapeWriter.h"
#include "./panel/panel.h"
#endif // DEVICES_TYPES_H

