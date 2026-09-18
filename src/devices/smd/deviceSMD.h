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


#ifndef DEVICE_SMD_H
#define DEVICE_SMD_H

#include "diskSMD.h"

// Controller types
typedef enum {
    CONTR_BIG_DISC,      // BIG DISK CONTROLLER (For 33 and 66 MB disks)
    CONTR_ECC_DISC,      // ECC DISK CONTROLLER (30/60/90, 38,75,288,150 MBYTE DISK DRIVES)
    CONTR_SMD_10MHZ,     // 10 MHz SMD Interface (Legacy, should use 15MHz)
    CONTR_SMD_15MHZ      // 15 MHz SMD Interface (ND632)
} ControllerType;

// Disk error codes
typedef enum {
    DISK_ERR_NO_DISK_ATTACHED,
    DISK_ERR_ADDRESS_MISMATCH,
    DISK_ERR_SEEK_ERROR,
    DISK_ERR_READ_ERROR,
    DISK_ERR_WRITE_ERROR,
    DISK_ERR_COMPARER_ERROR,
    DISK_ERR_DRIVE_NOT_SELECTED,
    DISK_ERR_ILLEGAL_WHILE_ACTIVE,
    DISK_ERR_WRITE_PROTECT_ERROR,
    DISK_ERR_TIMEOUT
} DiskError;


// Device operation codes
typedef enum {
    DEVICE_OP_READ_TRANSFER = 0,      // M0 - Read Transfer
    DEVICE_OP_WRITE_TRANSFER = 1,     // M1 - Write Transfer
    DEVICE_OP_READ_PARITY = 2,        // M2 - Read Parity Transfer
    DEVICE_OP_COMPARE_TRANSFER = 3,   // M3 - Compare Transfer
    DEVICE_OP_INITIATE_SEEK = 4,      // M4 - Initiate Seek
    DEVICE_OP_WRITE_FORMAT = 5,       // M5 - Write Format
    DEVICE_OP_SEEK_COMPLETE = 6,      // M6 - Seek Complete Search
    DEVICE_OP_RETURN_TO_ZERO = 7,     // M7 - Return To Zero Seek
    DEVICE_OP_RUN_ECC = 8,            // M8 - Run ECC Operation
    DEVICE_OP_SELECT_RELEASE = 9      // M9 - Select Release
} DeviceOperation;


// SMD device registers
typedef enum
{
    SMD_READ_MEMORY_ADDRESS = 0,  // IOX 1540: Read Core Address
    SMD_LOAD_MEMORY_ADDRESS = 1,  // IOX 1541: Load Core Address
    SMD_READ_SEEK_CONDITION = 2,  // IOX 1542: Read Seek Condition (CWRBit=0) / Read ECCCount (CWRBit=1)
    SMD_LOAD_BLOCK_ADDRESS = 3,   // IOX 1543: Load Block Address I (CWRBit=0) / Load Block Address II (CWRBit=1)
    SMD_READ_STATUS_REGISTER = 4, // IOX 1544: Read Status Register (CWRBit=0) / Read ECC pattern (CWRBit=1)
    SMD_LOAD_CONTROL_WORD = 5,    // IOX 1545: Load Control Word
    SMD_READ_BLOCK_ADDRESS = 6,   // IOX 1546: Read Block Address I (CWRBit=0) / Read Block Address II (CWRBit=1)
    SMD_LOAD_WORD_COUNTER = 7     // IOX 1547: Load Word Count (CWRBit=0) / Load ECC Control (CWRBit=1)
} SMDRegisters;

// Status Register bits
// IOX 1544: Status Register
typedef union
{
    uint16_t raw;
    struct
    {
        uint16_t interruptEnabled : 1;      // Bit 0 (Interrupt enabled)
        uint16_t errorInterruptEnabled : 1; // Bit 1 (Error interrupt enabled)
        uint16_t active : 1;                // Bit 2 (Controller active)
        uint16_t readyForTransfer : 1;      // Bit 3 (Ready for transfer)
        uint16_t hardwareError : 1;         // Bit 4 (inclusive or of error conditions)
        uint16_t illegalLoad : 1;           // Bit 5 (Illegal load)
        uint16_t timeOut : 1;               // Bit 6 (Timeout)
        uint16_t hardwareError2 : 1;        // Bit 7 (Hardware error - disk fault, etc.)
        uint16_t addressMismatch : 1;       // Bit 8 (Address mismatch)
        uint16_t notUsed9 : 1;              // Bit 9 (Data error - reserved)
        uint16_t comparerError : 1;         // Bit 10 (Compare error)
        uint16_t notUsed11 : 1;             // Bit 11 (DMA Channel error - reserved)
        uint16_t abnormalCompletion : 1;    // Bit 12 (Abnormal completion)
        uint16_t diskUnitNotReady : 1;      // Bit 13 (Disk unit not ready)
        uint16_t onCylinder : 1;            // Bit 14 (OnCylinder)
        uint16_t registerMultiplexBit : 1;                // Bit 15 (Register Multiplex bit from CWR bit 15)
    } bits;
} SMDStatusRegister;


// Control Word bits
// IOX 1545: Control Word
//
// When control word bit 2 is activated, the content of the block address register II (cylinder number) is transfered to the servo system in the selected unit.
// Logic in the unit will calculate the difference between the current cylinder and the new one. The difference and direction will command the servo to seek the new cylinder.

typedef union
{
    uint16_t raw;
    struct
    {
        uint16_t enableInterruptNotActive : 1; // Bit 0: Enable interrupt on device not active
        uint16_t enableInterruptOnErrors : 1;  // Bit 1: Enable interrupt on errors
        uint16_t active : 1;                   // Bit 2: Active (triggers cylinder seek)
        uint16_t testMode : 1;                 // Bit 3: Test mode
        uint16_t deviceClear : 1;              // Bit 4: Device clear (clear active flip-flop and controller error)
        uint16_t addressBit16 : 1;             // Bit 5: Address bit 16 - Extension of core address register
        uint16_t addressBit17 : 1;             // Bit 6: Address bit 17 - Extension of core address register
        uint16_t unitSelect : 3;               // Bits 7-9: Unit select (maximum 4 units)
        uint16_t marginalRecoveryCycle : 1;    // Bit 10: Marginal recovery cycle
        // Must be uint16_t (not enum DeviceOperation) - see the PANS/PANC
        // note in panel.h: mixed-type bit-fields break on Windows/MinGW
        // where -mms-bitfields splits different-typed fields into separate
        // storage units, destroying the `raw` uint16_t overlay.
        uint16_t deviceOperation : 4;          // Bits 11-14: Device operation code (see DeviceOperation enum)
        uint16_t registerMultiplexBit : 1;     // Bit 15: Register multiplex bit
    } bits;
} SMDControlRegister;

// Error Register bits
typedef union
{
    uint16_t raw;
    struct
    {
        uint16_t notUsed0 : 1;      // Bit 0
        uint16_t notUsed1 : 1;      // Bit 1
        uint16_t notUsed2 : 1;      // Bit 2
        uint16_t notUsed3 : 1;      // Bit 3
        uint16_t notUsed4 : 1;      // Bit 4
        uint16_t notUsed5 : 1;      // Bit 5
        uint16_t notUsed6 : 1;      // Bit 6
        uint16_t notUsed7 : 1;      // Bit 7
        uint16_t driveNotReady : 1; // Bit 8
        uint16_t writeProtect : 1;  // Bit 9
        uint16_t notUsed10 : 1;     // Bit 10
        uint16_t sectorMissing : 1; // Bit 11
        uint16_t crcError : 1;      // Bit 12
        uint16_t notUsed13 : 1;     // Bit 13
        uint16_t dataOverrun : 1;   // Bit 14
        uint16_t notUsed15 : 1;     // Bit 15
    } bits;
} SMDErrorRegister;

// Drive Address bits
typedef union
{
    uint16_t raw;
    struct
    {
        uint16_t modeBit : 1;        // Bit 0 (1 = Write Drive Address, 0 = Write Difference)
        uint16_t notUsed1 : 1;       // Bit 1
        uint16_t notUsed2 : 1;       // Bit 2
        uint16_t notUsed3 : 1;       // Bit 3
        uint16_t notUsed4 : 1;       // Bit 4
        uint16_t notUsed5 : 1;       // Bit 5
        uint16_t notUsed6 : 1;       // Bit 6
        uint16_t notUsed7 : 1;       // Bit 7
        uint16_t driveAddress : 3;   // Bits 8-10
        uint16_t deselectDrives : 1; // Bit 11
        uint16_t notUsed12 : 1;      // Bit 12
        uint16_t notUsed13 : 1;      // Bit 13
        uint16_t formatSelect : 2;   // Bits 14-15
    } bits;
} SMDDriveAddress;

// Seek Condition Register bits
typedef union {
    uint16_t raw;
    struct {
        uint16_t seekComplete : 8;    // Bits 0-7: Seek complete status for units 0-7
        uint16_t unitSelected : 3;    // Bits 8-10: Unit number loaded by last control word
        uint16_t seekError : 1;       // Bit 11: Seek error for selected unit
        uint16_t isSMD15Mhz : 1;      // Bit 12: Not defined (Always 1 for 15MHz controller)
        uint16_t eccCorrectable : 1;  // Bit 13: ECC error is correctable
        uint16_t eccParityError : 1;  // Bit 14: Hardware fault in ECC polynomials
        uint16_t addressField : 1;    // Bit 15: Last field read was address field
    } bits;
} SMDSeekCondition;


// Controller registers
typedef struct {

/*
    bool hardwareError;
    bool hardwareError2;
    bool illegalLoad;
    bool timeOut;
    bool comparerError;
    bool addressMismatch;
    bool writeProtectError;

    bool diskUnitNotReady;
    bool onCylinder;
        bool seekError;
    uint16_t seekCompleteBits;


*/

    // FlipFlops
    bool hasFlipFlops; //SMD 10Mhz and 15Mhz has flip-flops
    // The WORD COUNTER's load/read protocol, kept separate from hasFlipFlops.
    // RetroCore models four independent flip-flop flags (core memory, memory
    // address, word counter, ECC control) rather than one card-wide strap, and
    // the ND-120 mass-load microroutine needs that distinction: it writes the
    // MEMORY ADDRESS with two +1 accesses (flip-flop protocol) but the WORD
    // COUNT with a single +7 write of 002000, which only loads 1024 words if
    // the word counter is single-access. Ground truth for that asymmetry:
    // nd-120 repo, Verilog/ND-BUS-DEVICES/SMD/sim/traces/mass-load-21540.trace
    // Defaults to hasFlipFlops; override with ND100X_SMD_WC_FF=0|1.
    bool hasWordCountFlipFlop;

    // Which half the FIRST of the two accesses loads. ND-11.020.01 sec 2.2
    // states HI first for the MEMORY ADDRESS ("The first loads the 8 upper
    // bits ... and the second one loads the lower 16 bits") but documents
    // READS as LO then HI, and never states the WRITE order for the word
    // counter at all. If the real flip-flop simply toggles from a known reset
    // state, writes and reads would share one order (LO first) - and then the
    // mass-load microroutine's single +7 write of 002000 loads 1024 directly,
    // with no per-register strap needed. Switchable so the two orders can be
    // compared against DISC-TEMA: ND100X_SMD_LOAD_ORDER=hi (default) | lo
    bool loadLowFirst;

    bool wcwFlipFlop;
    bool wcrFlipFlop;
    bool wcEccwFlipFlop;
    bool mawFlipFlop;
    bool marFlipFlop;

    // Controller registers
    uint8_t selectedUnit;
    uint16_t blockAddressI;
    uint16_t blockAddressII;
    uint16_t coreAddress;
    uint16_t coreAddressHiBits;
    uint16_t wordCounter;
    uint16_t wordCounterHI;
    uint16_t eccControl;
    uint16_t eccControlHI;
    uint16_t eccPatternRegister;
    uint16_t eccCount;

    // Disk info
    int maxUnits;
    DiskInfo* disks;
    DiskInfo* selectedDisk;

    // Per-unit "a seek (M4/M7) has been initiated and not yet consumed" mask.
    // M6 (Seek Complete Search) waits for the seek-complete pulse of a
    // PREVIOUSLY initiated seek; with no seek outstanding there is no pulse and
    // the controller runs into its timeout (status b6). DISC-TEMA section 6
    // checks exactly this: "Seek Complete Search (with no previous seek),
    // Status Bit 6b (timeout) is 0 !". Consumed by transfer operations (M0-M3)
    // and cleared by device clear.
    uint8_t seekIssuedMask;

} ControllerRegs;

// SMD device data
typedef struct
{
    const char *smdName;
    uint16_t dataBuffer[1024];
    int16_t loadDriveAddress;
    uint16_t sector;
    uint16_t track;
    uint16_t bufferPointer;

    int selectedDrive;
    int bytes_pr_sector;
    int sectors_pr_track;
    uint8_t testByte;

    bool sectorAutoIncrement;
    int testmodeByte;

    SMDStatusRegister statusRegister;
    SMDControlRegister controlRegister;
    SMDErrorRegister errorRegister;
    SMDDriveAddress driveAddress;
    SMDSeekCondition seekCondition;
    ControllerType controllerType;


    ControllerRegs regs;
} SMDData;



extern int smd_debug_enabled;

// Function declarations
// Only expose the factory; internal helpers are kept private in the .c file
Device *CreateSMDDevice(uint8_t thumbwheel);

#endif /* DEVICE_SMD_H */