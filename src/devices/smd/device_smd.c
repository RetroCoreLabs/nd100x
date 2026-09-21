/*
 * device_smd.c - SMD disc controller (IOX 1540): registers, seek and DMA transfers.
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

#include "device_smd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>

#include <unistd.h>

#include "../devices_types.h"
#include "../devices_protos.h"
static void smd_destroy(Device *);


// Emulated controller-timeout window in ticks (the real card gives up after
// 500 ms). Used by M6 with no outstanding seek: the controller must stay
// ACTIVE while it searches, and only then raise timeout (status b6) -
// DISC-TEMA checks both that active holds during the search and that b6 comes
// on afterwards ("Status Bit 2b (active) becomes 0 immediately !!" was the
// failure when the timeout fired instantly).
#define SMD_TIMEOUT_TICKS 5000

// Local forward declarations for internal helpers
static void clear_flip_flops(ControllerRegs *regs);
static void set_selected_unit(ControllerRegs *regs, uint8_t unit);
static void handle_error(Device *self, DiskError error);
static void clear_errors(Device *self);
static void execute_go(Device *self);
static int64_t convert_cylinder_head_sector_to_logical_block(ControllerRegs *regs, int cylinder,
                                                             int head, int sector);
static uint32_t increment_core_address(ControllerRegs *regs);
static uint32_t decrement_word_counter(ControllerRegs *regs);
static bool smd_read_end(Device *self, int drive);
static bool smd_timeout_end(Device *self, int drive);
static void finish_operation(Device *self);
static bool unit_attached(Device *self, DiskInfo *disk);
static bool load_is_illegal(Device *self, SMDData *data);

static const char *smd_op_name(DeviceOperation op)
{
    switch (op)
    {
    case DEVICE_OP_READ_TRANSFER:
        return "DEVICE_OP_READ_TRANSFER";
    case DEVICE_OP_WRITE_TRANSFER:
        return "DEVICE_OP_WRITE_TRANSFER";
    case DEVICE_OP_READ_PARITY_TRANSFER:
        return "DEVICE_OP_READ_PARITY_TRANSFER";
    case DEVICE_OP_COMPARE_TRANSFER:
        return "DEVICE_OP_COMPARE_TRANSFER";
    case DEVICE_OP_INITIATE_SEEK:
        return "DEVICE_OP_INITIATE_SEEK";
    case DEVICE_OP_WRITE_FORMAT:
        return "DEVICE_OP_WRITE_FORMAT";
    case DEVICE_OP_SEEK_COMPLETE_SEARCH:
        return "DEVICE_OP_SEEK_COMPLETE_SEARCH";
    case DEVICE_OP_RETURN_TO_ZERO_SEEK:
        return "DEVICE_OP_RETURN_TO_ZERO_SEEK";
    case DEVICE_OP_RUN_ECC_OPERATION:
        return "DEVICE_OP_RUN_ECC_OPERATION";
    case DEVICE_OP_SELECT_RELEASE:
        return "DEVICE_OP_SELECT_RELEASE";
    default:
        return "Unknown";
    }
}

static const char *smd_reg_read_name(uint32_t reg, int cwr_bit)
{
    switch (reg)
    {
    case SMD_READ_MEMORY_ADDRESS:
        return cwr_bit ? "ReadWordCounter" : "ReadCoreAddr";
    case SMD_READ_SEEK_CONDITION:
        return cwr_bit ? "ReadECCCount" : "ReadSeekCondition";
    case SMD_READ_STATUS_REGISTER:
        return cwr_bit ? "ReadECCPattern" : "ReadStatus";
    case SMD_READ_BLOCK_ADDRESS:
        return cwr_bit ? "ReadBlockAddrII" : "ReadBlockAddrI";
    default:
        return "ReadUnknown";
    }
}

static const char *smd_reg_write_name(uint32_t reg, int cwr_bit)
{
    switch (reg)
    {
    case SMD_LOAD_MEMORY_ADDRESS:
        return cwr_bit ? "CountMemAddr" : "LoadCoreAddr";
    case SMD_LOAD_BLOCK_ADDRESS:
        return cwr_bit ? "LoadBlockAddrII" : "LoadBlockAddrI";
    case SMD_LOAD_CONTROL_WORD:
        return "LoadControlWord";
    case SMD_LOAD_WORD_COUNTER:
        return cwr_bit ? "LoadECCControl" : "LoadWordCounter";
    default:
        return "WriteUnknown";
    }
}

static const char *smd_error_name(DiskError error)
{
    switch (error)
    {
    case DISK_ERR_NO_DISK_ATTACHED:
        return "NoDiskAttached";
    case DISK_ERR_ADDRESS_MISMATCH:
        return "AddressMismatch";
    case DISK_ERR_SEEK_ERROR:
        return "SeekError";
    case DISK_ERR_READ_ERROR:
        return "ReadError";
    case DISK_ERR_WRITE_ERROR:
        return "WriteError";
    case DISK_ERR_COMPARER_ERROR:
        return "ComparerError";
    case DISK_ERR_DRIVE_NOT_SELECTED:
        return "DriveNotSelected";
    case DISK_ERR_ILLEGAL_WHILE_ACTIVE:
        return "IllegalWhileActive";
    case DISK_ERR_WRITE_PROTECT_ERROR:
        return "WriteProtectError";
    default:
        return "Unknown";
    }
}

static void smd_reset(Device *self)
{
    SMDData *data = (SMDData *)self->deviceData;
    if (!data)
    {
        return;
    }

    data->statusRegister.raw = 0;
    data->controlRegister.raw = 0;
    data->errorRegister.raw = 0;
    data->driveAddress.raw = 0;

    data->bufferPointer = 0;
    data->sector = 1;
    data->track = 0;
    data->selectedDrive = -1;
    data->sectorAutoIncrement = false;
    self->blockSizeBytes = 1024; // Default block size in bytes
}

static uint16_t smd_read(Device *self, uint32_t address)
{

    SMDData *data = (SMDData *)self->deviceData;
    if (!data)
    {
        return 0;
    }
    // NOTE: do NOT bail out when no disk is selected. The status register, ECC
    // pattern, seek condition, memory address and word counter are CONTROLLER
    // registers (cards 3043/3044) - they exist whether or not a drive is
    // selected, and only the drive-sourced bits (on-cylinder b14, unit-not-ready
    // b13, seek-complete) depend on a unit. Returning 0 here made the status
    // register unreadable after a GO on a not-specified unit, so the very error
    // that GO raises could not be seen: DISC-TEMA reports "Read (from NOT
    // specified unit), Status Bit 7b is 0 !". The no-disk case is handled per
    // register below (see the status register's else-branch: b14=0, b13=1).
    uint32_t reg = dev_register_address(self, address);
    uint16_t value = 0;

    if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
    {
        SMDData *dbg_data = (SMDData *)self->deviceData;
        int cwr_bit = dbg_data ? dbg_data->controlRegister.bits.registerMultiplexBit : 0;
        log_write(LOG_CAT_SMD, LOG_DEBUG, "IOX READ  addr=%o reg=%o (%s)\n", address, reg,
                  smd_reg_read_name(reg, cwr_bit));
    }

    switch (reg)
    {
    case SMD_READ_MEMORY_ADDRESS:
        if (data->controlRegister.bits.registerMultiplexBit)
        {
            // Read Word Counter
            //
            // The Word counter register is read the same way as the memory address register.
            // After a transfer, the upper/lower memory address(or word count) control bit(flip-flop) is reset.
            // A Read Status instruction(DEV.NO. + 4) or a Device Clear will also reset this bit

            if ((!data->regs.wcrFlipFlop) || (!data->regs.hasWordCountFlipFlop))
            {
                data->regs.wcrFlipFlop = true;
                value = data->regs.wordCounter;
            }
            else
            {
                data->regs.wcrFlipFlop = false;
                value = data->regs.wordCounterHI;
            }
        }
        else
        {
            // Read Core Address
            //
            // The Memory Address regster is read by two successive IOX instructions.
            // The first one gets the lower 16 bits(Address bits 0-15 into the A - reg. 0-15), and the second one gets the upper bits
            // (Address bits 16 - 23 into A-reg. 0 - 7). When reading the most significant bits, the upper byte of the A-reg. is undefined and has tobe masked.

            if ((!data->regs.marFlipFlop) || (!data->regs.hasFlipFlops))
            {
                data->regs.marFlipFlop = true;
                value = data->regs.coreAddress;
            }
            else
            {
                data->regs.marFlipFlop = false;
                value = data->regs.coreAddressHiBits;
            }
        }
        break;

    case SMD_READ_SEEK_CONDITION:
        if (data->controlRegister.bits.registerMultiplexBit)
        {
            value = data->regs.eccCount;
        }
        else
        {
            /*
            READ SEEK CONDITION

            Bits 0 - 7: Seek Complete
                Seek complete status for units 0-7. True if the unit has moved the heads to the correct cylinder or a seek error has occured and
                the heads are under the sector number prior to the one specified by the block address loaded before the initiate seek commands for that
                unit has first been issued.

                Thus, after an initiate seek command is given, the Seek Comptete bit for that unit will appear once per revolution after the unit is positioned on the
                correct cylinder, or a seek error has occurred. The condition will last until a transfer command is given.

            Bits 8 - 10: Unit Selected
                The unit number is loaded by the last control word.

            Bit 11: Seek Error
                Seek error for the selected unit.This signal indicates that the unit was unable to complete a move within 600 ms,
                or that the heads have moved to a position outside the recording field, or that an address greater than the maximum number of tracks has been selected.

                This signal will only be cleared by performing a Return to Zero command on the unit,

            Bit 12 Not defined.
                In the docs for the 15 MHZ  Controller, it says "Bit 12 = Always 1" (maybe this is to distingiush 10Mhz vs 15 Mhz drives?)

            Bit 13: ECC Correctable
                After the hardware ECC operation M8 has been performed after a data error, this bit signals that the error is correctable and that the ECC Count and
                ECC Pattern Registers contain valid information for correction of the data.The bit is reset by Reset ECC
                (ECC Control register bit 0) or Device Clear.

            Bit 14: ECC Parity Error(STS bit no. 7)
                This bit signals that a hardwere faut condition exists in the ECC polynomials.
                This condition will also set bit 7 of the status word register and hence tigger an error interrupt if this is enabled.
                The error is reset by the Reset ECC signal (ECC Controt register bit 0) or by Device Clear Signal (CWR bit 4).
                The error is forced set when ECC Control Register bit 1 is active (Force Parity Error).

            Bit 15: Address Field
                This bit indicates that the last field read from the disk was the address field within a sector (used for ECC processing after a data check only).
*/

            if ((data->controllerType == CONTR_SMD_15MHZ) ||
                (data->controllerType == CONTR_SMD_10MHZ))
            {
                // Bit 12 = Always 1 for 15Mhz SMD. This bit was always 0 on the NORD-10 controller.
                // If this is 1 then SINTRAN M will not r/w/boot from DISC-75-1 ??
                data->seekCondition.bits.isSMD15Mhz = 1;
            }
            else
            {
                data->seekCondition.bits.isSMD15Mhz = 0;
            }
            data->seekCondition.bits.unitSelected = data->regs.selectedUnit;
            value = data->seekCondition.raw;

            /*
            value |= (data->regs.seekCompleteBits & 0xF); // Bits 0-3

            value |= (data->regs.selectedUnit & 0x07) << 8; // Bits 8-10 (3-bit unit field)
            if (data->regs.seekError)
                value |= (1 << 11); // Bit 11

            */
            if ((data->controllerType == CONTR_SMD_15MHZ) ||
                (data->controllerType == CONTR_SMD_10MHZ))
            {
                value |=
                    (1
                     << 12); // 12 = Always 1 for 15Mhz SMD. This bit was always 0 on the NORD-10 controller.
                             // If this is 1 then SINTRAN M will not r/w/boot from DISC-75-1
            }

            // 13: ECC Correctable
            // 14: ECC parity Field
            // 15: Address Field
            return value;
        }
        break;

    case SMD_READ_STATUS_REGISTER:
        if (data->controlRegister.bits.registerMultiplexBit)
        {
            data->regs.eccPatternRegister = 0;
            // Read ECC Pattern Register

            /*
            +----+----+----+----+----+--------------+
            | 15 | 14 | 13 | 12 | 11 | 1O    -    O |
            +----+----+----+----+----+--------------+
            | 1  | 0  |  1 |  1 | 1  | Error pattern|
            +----+----+----+----+----+--------------+
            Bits
                            0 - 10 Error pattern.
                            11-13 Always 1.
                            14 Always 0.To distinguish from the old HD-100 SMD controller.
                            15 Always 1.Read~back of Control Word bit 15.
            */

            // Bits 0-10
            // eccPatternRegister |= eccErrorPattern & 0x3FF; NOT USED.. yet

            // Bits 11-13, Always one
            data->regs.eccPatternRegister |= (0b111 << 11);

            // Bits 14, Always 0

            // Bits 14 - Always 0 in the new - To distinguish from the old HD-100 SMD controller
            if ((data->controllerType == CONTR_BIG_DISC) ||
                (data->controllerType == CONTR_ECC_DISC))
            {
                data->regs.eccPatternRegister |= (1 << 14);
            }

            // Bits 15, Always 1
            data->regs.eccPatternRegister |= (1 << 15);

            value = data->regs.eccPatternRegister;
        }
        else
        {
            // hardwareError (b4) = inclusive OR of the error conditions. Must
            // include hardwareError2 (b7): DISC-TEMA's not-specified-unit check
            // raises b7, and it has to propagate into the OR. Matches nd_smd's
            // smd_inclusive_or (which includes hw_error2). See DISC-TEMA item 7.
            data->statusRegister.bits.hardwareError =
                data->statusRegister.bits.illegalLoad | data->statusRegister.bits.timeOut |
                data->statusRegister.bits.hardwareError2 | data->statusRegister.bits.comparerError |
                data->statusRegister.bits.addressMismatch | data->seekCondition.bits.seekError;

            if (data->regs.selectedDisk)
            {
                bool attached = unit_attached(self, data->regs.selectedDisk);
                // A unit with no pack mounted is not on cylinder and not ready,
                // whatever its per-disk flags say.
                data->statusRegister.bits.onCylinder =
                    attached ? data->regs.selectedDisk->onCylinder : 0;
                data->statusRegister.bits.diskUnitNotReady =
                    attached ? data->regs.selectedDisk->diskUnitNotReady : 1;
            }
            else
            {
                data->statusRegister.bits.onCylinder = 0;
                data->statusRegister.bits.diskUnitNotReady =
                    1; // Bit 13 = Always 1 if no disk is selected (disk unit not ready)
            }

            value = data->statusRegister.raw;


            clear_flip_flops(&data->regs);
        }
        break;

    case SMD_READ_BLOCK_ADDRESS:
        if (data->controlRegister.bits.registerMultiplexBit)
        {
            value = data->regs.blockAddressII;
        }
        else
        {
            value = data->regs.blockAddressI;
        }
        break;
    default:
        break;
    }

    if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
    {
        log_write(LOG_CAT_SMD, LOG_DEBUG, "IOX READ  addr=%o reg=%o -> value=%o (0x%04X)\n",
                  address, reg, value, value);
    }

    return value;
}

static void smd_write(Device *self, uint32_t address, uint16_t value)
{
    uint32_t reg = dev_register_address(self, address);

    if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
    {
        SMDData *dbg_data = (SMDData *)self->deviceData;
        int cwr_bit = dbg_data ? dbg_data->controlRegister.bits.registerMultiplexBit : 0;
        log_write(LOG_CAT_SMD, LOG_DEBUG, "IOX WRITE addr=%o reg=%o (%s) value=%o (0x%04X)\n",
                  address, reg, smd_reg_write_name(reg, cwr_bit), value, value);
    }

    SMDData *data = (SMDData *)self->deviceData;

    switch (reg)
    {
    case SMD_LOAD_MEMORY_ADDRESS:
        if (data->controlRegister.bits.registerMultiplexBit)
        {
            // Count Memory Address & Word Count: This instruction is implemented for maintenance purposes only.

            // By first loading the control word with 102010, a special test mode, each of these instructions will
            // * increment the memory address by one
            // * decrement the word count by one. (Refer to section 3.1, the DMA transfer.)

            if (data->controlRegister.bits.testMode &&
                data->controlRegister.bits.marginalRecoveryCycle)
            {
                data->regs.coreAddress++;
                data->regs.wordCounter--;
            }
        }
        else
        {
            // Load Memory Address
            // Illegal load (status b5) - see LoadIsIllegal for the two
            // conditions (controller active / unit not on cylinder).
            if (load_is_illegal(self, data))
            {
                handle_error(self, DISK_ERR_ILLEGAL_WHILE_ACTIVE); // ILLEGAL_WHILE_DRIVE_IS_ACTIVE
                return;
            }

            // The Load Memory Address Register is loaded by two successive instructions. The first loads the 8 upper bits(A-reg. 0 - 7 into Address bits 16 - 23),
            // and the second one loads the lower 16 bits(A-reg. 0 - 15 into Address bits 0 - 15).
            // After a transfer, the upper/ lower memory address control bit(flip-flop) is reset.A Read Status instruction(DEV.NO. +4) or a Device Clear will also reset this bit.

            if (!data->regs.hasFlipFlops)
            {
                data->regs.coreAddress = value;
                data->regs.mawFlipFlop = false;
            }
            else
            {
                // mawFlipFlop == false means "this is the FIRST of the two
                // accesses". Which half that first access loads is the
                // loadLowFirst question - see device_smd.h.
                bool first_access = !data->regs.mawFlipFlop;
                if (first_access == data->regs.loadLowFirst)
                {
                    data->regs.coreAddress = value; // low 16
                }
                else
                {
                    data->regs.coreAddressHiBits = value & 0xFF; // high 8
                }
                data->regs.mawFlipFlop = !data->regs.mawFlipFlop;
            }
        }
        break;

    case SMD_LOAD_BLOCK_ADDRESS:
        // Illegal load (status b5) - active or not-on-cylinder, see LoadIsIllegal.
        if (load_is_illegal(self, data))
        {
            handle_error(self, DISK_ERR_ILLEGAL_WHILE_ACTIVE);
            return;
        }

        if (data->controlRegister.bits.registerMultiplexBit)
        {
            data->regs.blockAddressII = value;
        }
        else
        {
            data->regs.blockAddressI = value;
        }
        break;

    case SMD_LOAD_CONTROL_WORD:
        if (data->statusRegister.bits.active)
        {
            // The Control Word is a register like any other, so loading it
            // while status bit 2 is true is an illegal load and must raise
            // bit 5 (ND-11.020.01 sec 2.5) - this used to return silently, and
            // DISC-TEMA caught it: "Error after Illegal Load (Control Word),
            // Bit 5b was 0 !".
            //
            // Device clear (control-word bit 4) is the one exception: it is the
            // programmed master clear (ND-11.013.01A: "Programmed master clear,
            // i.e., control word bit 4 (device clear)") and must always reach
            // the controller, otherwise an active controller could never be
            // recovered. Fall through to the normal handling below, which
            // clears the active flip-flop and the error bits.
            if (!((value >> 4) & 1))
            {
                handle_error(self, DISK_ERR_ILLEGAL_WHILE_ACTIVE);
                return;
            }
        }

        /*
            Bit:
                    0       Enable interrupt on device not active
                    1       Enable interrupt on errors
                    2       Active
                                When control word bit 2 is activated, the content of the block address register II (cylinder number) is transfered to the servo system in the selected unit.
                                Logic in the unit will calculate the difference between the current cylinder and the new one. The difference and direction will command the servo to seek the new cylinder.
                    3       Test mode
                    4       Device clear (clear the active flip-flop) and controller error bas,
                    5       Address bit 16 - Extension of core address register
                    6       Address bit 17 - Extension of core address register

                    7-9     Unit select (maximum 4 units)
                    10      Marginal recovery cycle
                    11-14   Device operation code
                    15      Register multiplex bit
        */

        if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
        {
            log_write(
                LOG_CAT_SMD, LOG_DEBUG,
                "CONTROL WORD=%o (0x%04X) Unit=%d Op=%s IntEn=%d ErrIntEn=%d Active=%d CWR15=%d\n",
                value, value, (value >> 7) & 0x07,
                smd_op_name((DeviceOperation)((value >> 11) & 0x0F)), value & 1, (value >> 1) & 1,
                (value >> 2) & 1, (value >> 15) & 1);
        }

        data->controlRegister.raw = value;

        data->statusRegister.bits.active = data->controlRegister.bits.active;
        data->statusRegister.bits.registerMultiplexBit =
            data->controlRegister.bits.registerMultiplexBit;
        data->statusRegister.bits.readyForTransfer = true;

        data->statusRegister.bits.interruptEnabled =
            data->controlRegister.bits.enableInterruptNotActive;
        data->statusRegister.bits.errorInterruptEnabled =
            data->controlRegister.bits.enableInterruptOnErrors;
        // Clear interrupt if not enabled
        if (!data->statusRegister.bits.interruptEnabled)
        {
            dev_set_interrupt_status(self, false, self->interruptLevel);
        }

        // Old device didn't load the HI bits through writing to the address-register twice, but had a few bits in the ControlWord
        // if (deviceType == DeviceType.ECC_DISC_CONTR)
        if (!data->regs.hasFlipFlops)
        {
            data->regs.coreAddressHiBits = ((uint16_t)((value >> 5) & 0b11));
        }


        set_selected_unit(&data->regs, data->controlRegister.bits.unitSelect);

        if (data->controlRegister.bits.deviceClear)
        {
            // Device Clear
            if (data->regs.selectedDisk)
            {
                data->regs.selectedDisk->diskUnitNotReady = 0;
            }

            // NOTE: device clear must NOT set the seek-complete bit. The
            // seek-condition register's seek-complete bits belong to actual
            // seek completions (M4/M6/M7); DISC-TEMA section 7 reads the seek
            // condition after a clear "With no previous Initiate-Seek" and
            // requires bits 0-7 to be ZERO.
            data->regs.seekIssuedMask = 0;

            data->statusRegister.bits.active = 0;
            data->regs.coreAddress = 0;
            data->regs.coreAddressHiBits = 0;
            data->regs.blockAddressI = 0;
            data->regs.blockAddressII = 0;
            data->regs.wordCounter = 0;
            data->regs.wordCounterHI = 0;

            data->statusRegister.bits.readyForTransfer = false;

            clear_flip_flops(&data->regs);
            clear_errors(self);
        }

        // (Do NOT force onCylinder=1 here: on-cylinder is drive state that only
        // seek completion may set. Forcing it on every control-word write made
        // status b14 permanently 1, which DISC-TEMA catches twice: "Bit 16b (on
        // cylinder) remained 1" after RTZ and "became 1 immediately" after M4.)

        if (data->statusRegister.bits.active)
        {
            if (!data->regs.selectedDisk)
            {
                // GO on a not-specified unit (4-7, no drive). DISC-TEMA expects
                // hardware-error (status b7) here - "Read (from NOT specified unit),
                // Status Bit 7b is 0 !". Matches nd_smd (hw_error2) / RetroCore.
                data->statusRegister.bits.diskUnitNotReady = 1;
                data->statusRegister.bits.hardwareError2 = 1;
                handle_error(self, DISK_ERR_DRIVE_NOT_SELECTED);
                // The operation was activated, so it must also END here - see
                // FinishOperation. Without this the controller stays active
                // forever after DISC-TEMA's not-specified-unit test.
                finish_operation(self);
                return;
            }
            // A GO against a unit with no pack mounted is the "read from a
            // NOT specified unit" case (DISC-TEMA item 9): hardware error b7
            // plus not-ready b13, and the operation must still terminate.
            if (!unit_attached(self, data->regs.selectedDisk))
            {
                data->statusRegister.bits.diskUnitNotReady = 1;
                data->statusRegister.bits.hardwareError2 = 1;
                handle_error(self, DISK_ERR_DRIVE_NOT_SELECTED);
                finish_operation(self);
                return;
            }

            data->regs.selectedDisk->diskUnitNotReady = 0;

            execute_go(self);
        }
        else
        {
            if (data->controlRegister.bits.testMode)
            {
                dev_set_interrupt_status(self, data->statusRegister.bits.interruptEnabled,
                                         self->interruptLevel);
            }
            else
            {
                dev_set_interrupt_status(self,
                                         data->statusRegister.bits.interruptEnabled &
                                             data->statusRegister.bits.readyForTransfer,
                                         self->interruptLevel);
            }
        }
        break;

    case SMD_LOAD_WORD_COUNTER:

        // Illegal load = status bit 2 true (ND-11.020.01 sec 2.5, bit 5). This
        // check was missing entirely; DISC-TEMA caught it as "Error after
        // Illegal Load (Word Count), Bit 5b was 0 !". Also raised while the
        // unit is off cylinder - see LoadIsIllegal.
        if (load_is_illegal(self, data))
        {
            handle_error(self, DISK_ERR_ILLEGAL_WHILE_ACTIVE);
            return;
        }

        // Load ECC Control
        /*
        ECC CONTROL

        Bit 0: Reset ECC
                        This bit wil cause the ECC polynomisis to reset to the zero initial state. Ths function is only used when a dats error has occurred,
                        otherwise the polynomials automatically go to the zero state upon completion of a Read or Write. Device Cleer function will also reset ECC.

        Bit 1: TST - Force Parity Error
                        Used for maintenance purposes only, This bit will force ECC parity error to be set.

        Bit 2: Long
                        Used for maintenance purposes only. When 8 sector is read or writwen, the date field of the sector is extended by 64 bits (
                        the length of the ECC appendage plus "end of record" byte). The date and the extra bits sre read into of written from the memory of the CPU.
                        This function is used to diagnose the operation of the ECC circuits and can be used with the following Device Operations: MO, M1, M2 and M3.
                        Thas bit is "echoed" in ECR bit 14.

        // NEW BITS FOR 15MHZ SMD DISK CONTROLLERS

        Bit 3: Format A
        Bit 4: Format B
        Bit 5: Format C
        Bit 6: Format D

        */

        if (data->controlRegister.bits.registerMultiplexBit)
        {
            // Load ECC Control
            if ((data->regs.wcEccwFlipFlop) || (!data->regs.hasFlipFlops))
            {
                data->regs.eccControl = value;

                // Bit 0 - Reset ECC
                if (data->regs.eccControl & 1)
                {
                    data->regs.eccCount = 0;
                }

                // Bit 1 - Force Parity Error
                if (data->regs.eccControl & (1 << 1))
                {
                    // Used for maintenance purposes only. (FILE-SYS-INV uses it!!)
                    // This bit will force ECC parity error to be set.

                    // TODO: WHat does this mean in practice - what now ?
                    data->statusRegister.bits.hardwareError2 =
                        1; // Set Bit 7 for the Status Register => Disk fault, missing read clocks, missing servoclocks, ECC parity error.
                }

                // Bit 2 - Long
                if ((data->regs.eccControl & 1 << 2) != 0)
                {
                    // Used for maintenance purposes only.

                    // When a sector is read or written, the data field of the sector is extended by 64 bits (The length of the ECC pattern plus "End of Record" byte).
                    // The data and the extra bits are read into or written from the memory of the CPU. This function is used to diagnose the
                    // operation of the ECC circuits, and can be used with the following Device operations: M0, M1, M2, M3. This bit is "echoed" in ECR bit 14.

                    // TODO: WHat does this mean
                }

                // Bit 3-5 - Format A-D. Used when formatting the drive.

                //
                data->regs.wcEccwFlipFlop = false;
            }
            else
            {
                // The first loads the upper 8 bits
                data->regs.eccControlHI = value & 0xFF;
                data->regs.wcEccwFlipFlop = true;
            }
        }
        else
        {
            // Load Word Counter
            // Load Word Counter; The Word Count register is increased from 16 to 24 bits, and is loaded by two successive instructions.
            // The first loads the 8 upper bits(A-reg. 0 -? into Word Count bits 16-32), and the second one loads the lower 16 bits(A - reg. 0-15 into Word Count bits 0-15).

            // After a transfer, the upper/lower Word Count control bit (flip-flop) is reset.A Read Status instruction(DEV.NO. +4) or a Device Clear will also reset this bit.
            // The controller is able to transfer a whole cylinder, or up to 16M words(24 bits), with a hardware increment of the head and sector addresses.

            // For the 75 Mb disk, the maximum word count is 132000(45k); starting with the head and cylinder address equal to 0.
            // The Word Count is set to an integer multiple of the number of words in a sector when device operation is M0-M3.


            if (!data->regs.hasWordCountFlipFlop)
            {
                data->regs.wordCounter = value;
                data->regs.wcwFlipFlop = false;
            }
            else
            {
                bool first_access = !data->regs.wcwFlipFlop;
                if (first_access == data->regs.loadLowFirst)
                {
                    data->regs.wordCounter = value; // low 16
                }
                else
                {
                    data->regs.wordCounterHI = value & 0xFF; // high 8
                }
                data->regs.wcwFlipFlop = !data->regs.wcwFlipFlop;
            }
        }
        break;
    default:
        break;
    }
}

static uint16_t smd_tick(Device *self)
{
    if (!self)
    {
        return 0;
    }

    dev_tick_io_delay(self);

    return self->interruptBits;
}

static uint16_t smd_ident(Device *self, uint16_t level)
{
    if (!self)
    {
        return 0;
    }

    if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
    {
        log_write(LOG_CAT_SMD, LOG_DEBUG, "IDENT level=%d identCode=%o\n", level, self->identCode);
    }

    if ((self->interruptBits & (1 << level)) != 0)
    {
        SMDData *data = (SMDData *)self->deviceData;
        data->statusRegister.bits.interruptEnabled = 0;
        dev_set_interrupt_status(self, false, level);
        return self->identCode;
    }
    return 0;
}

static int smd_boot(Device *self, int unit)
{
    SMDData *data = (SMDData *)self->deviceData;
    ControllerRegs *regs = &data->regs;

    if ((!self->blockCallbacks.readFunc) || (!self->blockCallbacks.writeFunc))
    {
        return -1; // Need callbacks hooked up
    }

    if (unit < 0 || unit >= regs->maxUnits)
    {
        LOG(LOG_CAT_SMD, LOG_ERROR, "Error: SMD boot unit %d out of range (0-%d)\n", unit,
            regs->maxUnits - 1);
        return -1;
    }

    regs->selectedUnit = unit;
    regs->selectedDisk = &regs->disks[regs->selectedUnit];

    // Initialize disk geometry if not already configured
    if (regs->selectedDisk->diskType == DISK_TYPE_UNKNOWN)
    {
        if (self->blockCallbacks.diskInfoFunc)
        {
            bool is_write_protected = false;
            size_t image_size = 0;
            self->blockCallbacks.diskInfoFunc(self, &image_size, &is_write_protected,
                                              regs->selectedUnit);
            regs->selectedDisk->diskFileSize = image_size;
            regs->selectedDisk->diskIsWriteProtected = is_write_protected;
        }
        DiskType dt = DISK_75_MB; // Default
        if (regs->selectedDisk->diskFileSize > 0x1000000 &&
            regs->selectedDisk->diskFileSize <= 0x2000000)
        {
            dt = DISK_150_MB;
        }
        smd_disk_set_type(regs->selectedDisk, dt);
    }
    self->blockSizeBytes = regs->selectedDisk->bytesPrSector;

    uint32_t block_counter = 4; // 4 blocks of 1024 bytes each (total 4096 bytes or 2048 KWords)
    uint8_t *buffer = (uint8_t *)malloc(block_counter * self->blockSizeBytes);

    if (!buffer)
    {
        return -1;
    }

    int word_counter =
        2048; // Load 2 KW of data from the disk (4096 bytes) to memory starting at address 0

    // Read all blocks from SMD disk file into buffer
    int blocks_read =
        self->blockCallbacks.readFunc(self, buffer, block_counter, 0, regs->selectedUnit);
    if ((blocks_read < 0) || (blocks_read != (int)block_counter))
    {
        LOG(LOG_CAT_SMD, LOG_ERROR, "[SMD Boot] Block read failed: got %d blocks, expected %d\n",
            blocks_read, block_counter);
        free(buffer);
        return -1;
    }

    // Check if boot sector is all zeros (blank/unformatted disk)
    {
        int all_zero = 1;
        for (uint32_t i = 0; i < block_counter * self->blockSizeBytes; i++)
        {
            if (buffer[i] != 0)
            {
                all_zero = 0;
                break;
            }
        }
        if (all_zero)
        {
            LOG(LOG_CAT_SMD, LOG_ERROR,
                "Error: SMD boot sector is all zeros (blank or unformatted disk)\n");
            free(buffer);
            return -1;
        }
    }

    for (int i = 0; i < word_counter; i++)
    {
        // Read word from disk buffer
        uint32_t read_data = dev_io_buffer_read_word(self, buffer, i);

        // Write to memory (DMA)
        dev_dma_write(i, (uint16_t)read_data);
    }

    free(buffer);

    // Return boot addrees. For BPUN this might be different!
    return 0;
}

///

/// And callbacks for read and write are set
///
static void execute_go(Device *self)
{

    if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
    {
        log_write(LOG_CAT_SMD, LOG_DEBUG, "ExecuteGO called\n");
    }

    if (!self)
    {
        return;
    }

    if ((!self->blockCallbacks.readFunc) || (!self->blockCallbacks.writeFunc))
    {
        return; // Need callbacks hooked up
    }


    SMDData *data = (SMDData *)self->deviceData;
    if (!data->regs.selectedDisk)
    {
        return;
    }
    ControllerRegs *regs = &data->regs;

    // illegal-load (b5) is PER-OPERATION: clear it at the start of each GO so a
    // prior operation's illegal load does not stay latched and contaminate this
    // one's status (DISC-TEMA item 5; nd100x used to latch it until device-clear).
    data->statusRegister.bits.illegalLoad = 0;
    // Abnormal completion (b12) is per-operation for the same reason.
    data->statusRegister.bits.abnormalCompletion = 0;
    // Timeout (b6) likewise: it reports on the operation that timed out, not on
    // everything after it.
    data->statusRegister.bits.timeOut = 0;

    // Get information on file size and readonly
    if (self->blockCallbacks.diskInfoFunc)
    {
        bool is_write_protected = false;
        size_t image_size = 0;

        self->blockCallbacks.diskInfoFunc(self, &image_size, &is_write_protected,
                                          data->regs.selectedUnit);
        data->regs.selectedDisk->diskFileSize = image_size;
        data->regs.selectedDisk->diskIsWriteProtected = is_write_protected;
    }
    else
    {
        data->regs.selectedDisk->diskFileSize = 0;
        data->regs.selectedDisk->diskIsWriteProtected = true;
    }

    if (data->regs.selectedDisk->diskType == DISK_TYPE_UNKNOWN)
    {
        // TODO: Set disk type based on size of SMD image file (TODO: Add more disk sizes)

        DiskType dt = DISK_75_MB; // Default to 75MB

        if (data->regs.selectedDisk->diskFileSize > 0x1000000 &&
            data->regs.selectedDisk->diskFileSize <= 0x2000000)
        {
            // Assume 150 MB disk
            dt = DISK_150_MB;
        }
        else if (data->regs.selectedDisk->diskFileSize >= 0x9600000 &&
                 data->regs.selectedDisk->diskFileSize <= 0x9601000)
        {
            // Assume 150 MB disk
            dt = DISK_150_MB;
        }
        else if (data->regs.selectedDisk->diskFileSize >= 0x12000000 &&
                 data->regs.selectedDisk->diskFileSize <= 0x12001000)
        {
            // Assume 288 MB disk
            dt = DISK_288_MB;
        }
        else if (data->regs.selectedDisk->diskFileSize >= 0x33900000)
        {
            // Assume 825 MB disk
            dt = DISK_825_MB;
        }
        smd_disk_set_type(data->regs.selectedDisk, dt);
    }


    // Ensure device block size reflects currently selected disk geometry
    self->blockSizeBytes = regs->selectedDisk->bytesPrSector;

    // Extract CHS values from block addresses
    int sector = data->regs.blockAddressI & 0xFF;
    int head = (data->regs.blockAddressI >> 8) & 0xFF;
    int cylinder = data->regs.blockAddressII;

    // Convert CHS to LBA. 64-bit on purpose: `long` is 32 bits on Windows, and
    // a deliberately huge cylinder number (DISC-TEMA's illegal-block-address
    // test) overflowed the byte position into a NEGATIVE value, sailing past
    // the `position > maxPosition` check - so the illegal address was neither
    // rejected nor flagged as a seek error.
    int64_t lba =
        convert_cylinder_head_sector_to_logical_block(&data->regs, cylinder, head, sector);
    int64_t position = lba * data->regs.selectedDisk->bytesPrSector;

    // Clear seek complete bit for this drive
    data->seekCondition.bits.seekComplete &= ~(1 << data->regs.selectedUnit);

    // Check for address mismatch
    int64_t max_position =
        (int64_t)convert_cylinder_head_sector_to_logical_block(
            &data->regs, data->regs.selectedDisk->maxCylinders,
            data->regs.selectedDisk->headsPrCylinder, data->regs.selectedDisk->sectorsPrTrack) *
        data->regs.selectedDisk->bytesPrSector;

    // Each CHS component is range-checked on its own (the old code compared
    // HEAD against maxCylinders - a typo that let out-of-range heads through
    // and never checked the cylinder at all).
    if ((position > max_position || cylinder >= data->regs.selectedDisk->maxCylinders ||
         head >= data->regs.selectedDisk->headsPrCylinder ||
         sector >= data->regs.selectedDisk->sectorsPrTrack) &&
        !data->controlRegister.bits.testMode)
    {
        // DISC-TEMA item 6: an illegal (out-of-range) block address is also a
        // seek-error condition (seek-condition b11); cleared only by M7 RTZ.
        // Matches nd_smd / RetroCore.
        data->seekCondition.bits.seekError = 1;
        handle_error(self, DISK_ERR_ADDRESS_MISMATCH); // ADDRESS_MISMATCH
        finish_operation(self);
        return;
    }

    // Check if disk is write protected for write operations
    if (data->regs.selectedDisk->diskIsWriteProtected &&
        (data->controlRegister.bits.deviceOperation == DEVICE_OP_WRITE_TRANSFER ||
         data->controlRegister.bits.deviceOperation == DEVICE_OP_WRITE_FORMAT))
    {
        data->regs.selectedDisk->diskUnitNotReady = true;
        handle_error(self, DISK_ERR_WRITE_PROTECT_ERROR); // WRITE_PROTECT_ERROR
        finish_operation(self);
        return;
    }

    uint32_t word_counter = (uint32_t)(data->regs.wordCounterHI << 16 | data->regs.wordCounter);
    uint32_t core_address = (uint32_t)(data->regs.coreAddressHiBits << 16 | data->regs.coreAddress);

    // A transfer operation CONSUMES any outstanding seek: the seek-complete
    // condition "will last until a transfer command is given" (ND-11.020.01,
    // seek-condition register), and a subsequent M6 with no NEW seek must time
    // out (DISC-TEMA section 6).
    if (data->controlRegister.bits.deviceOperation <= DEVICE_OP_COMPARE_TRANSFER)
    {
        data->regs.seekIssuedMask &= (uint8_t)~(1 << data->regs.selectedUnit);
    }

    // Number of blocks to transfer where each block is blockSizeBytes bytes  (typically 1024)
    uint32_t block_counter = (word_counter * 2) / self->blockSizeBytes;
    uint32_t buffer_ptr = 0;

    uint8_t *buffer;
    int blocks_read = -1;

    // Handle different device operations
    switch (data->controlRegister.bits.deviceOperation)
    {
    case DEVICE_OP_READ_TRANSFER:

        if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
        {
            log_write(LOG_CAT_SMD, LOG_DEBUG,
                      "GO Op=%s Unit=%d C/H/S=%d/%d/%d LBA=%" PRId64 " WC=%d CoreAddr=%o\n",
                      smd_op_name(DEVICE_OP_READ_TRANSFER), data->regs.selectedUnit, cylinder, head,
                      sector, lba, word_counter, core_address);
        }

        buffer = (uint8_t *)malloc(block_counter * self->blockSizeBytes);
        if (!buffer)
        {
            handle_error(self, DISK_ERR_READ_ERROR); // READ_ERROR
            finish_operation(self); // ends the operation, as RetroCore's HandleError clears Active
            return;
        }

        // Read all blocks from SMD disk file into buffer
        blocks_read = self->blockCallbacks.readFunc(self, buffer, block_counter, lba,
                                                    data->regs.selectedDisk->unit);
        if ((blocks_read < 0) || ((uint32_t)blocks_read != block_counter))
        {
            handle_error(self, DISK_ERR_READ_ERROR); // READ_ERROR
            free(buffer);
            finish_operation(self); // ends the operation, as RetroCore's HandleError clears Active
            return;
        }

        // DMA transfer to memory
        while (word_counter > 0)
        {
            // Read word from disk
            uint32_t read_data = dev_io_buffer_read_word(self, buffer, buffer_ptr++);

            // Write to memory (DMA)
            dev_dma_write(core_address, (uint16_t)read_data);

            core_address = increment_core_address(regs);
            word_counter = decrement_word_counter(regs);
        }

        free(buffer);
        dev_queue_io_delay(self, IODELAY_HDD_SMD, (IODelayedCallback)smd_read_end,
                           data->regs.selectedDisk->unit, self->interruptLevel);
        break;

    case DEVICE_OP_WRITE_TRANSFER:

        if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
        {
            log_write(LOG_CAT_SMD, LOG_DEBUG,
                      "GO Op=%s Unit=%d C/H/S=%d/%d/%d LBA=%" PRId64 " WC=%d CoreAddr=%o\n",
                      smd_op_name(DEVICE_OP_WRITE_TRANSFER), data->regs.selectedUnit, cylinder,
                      head, sector, lba, word_counter, core_address);
        }
        buffer = (uint8_t *)malloc(block_counter * self->blockSizeBytes);
        if (!buffer)
        {
            handle_error(self, DISK_ERR_READ_ERROR); // READ_ERROR
            finish_operation(self); // ends the operation, as RetroCore's HandleError clears Active
            return;
        }

        // DMA transfer from RAM to buffer
        while (word_counter > 0)
        {
            // Read from memory (DMA)
            int32_t read_data = dev_dma_read(core_address);

            if (read_data < 0)
            {
                handle_error(self, DISK_ERR_READ_ERROR); // DMA READ ERROR??
                free(buffer);
                finish_operation(
                    self); // ends the operation, as RetroCore's HandleError clears Active
                return;
            }
            // Write word to disk buffer
            if (dev_io_buffer_write_word(self, buffer, buffer_ptr++, (uint16_t)read_data) < 0)
            {
                handle_error(self, DISK_ERR_READ_ERROR); // WRITE_ERROR
                free(buffer);
                finish_operation(
                    self); // ends the operation, as RetroCore's HandleError clears Active
                return;
            }

            core_address = increment_core_address(regs);
            word_counter = decrement_word_counter(regs);
        }

        // Write all blocks to SMD disk file from buffer
        int blocks_write = self->blockCallbacks.writeFunc(self, buffer, block_counter, lba,
                                                          data->regs.selectedDisk->unit);
        if ((blocks_write < 0) || ((uint32_t)blocks_write != block_counter))
        {
            handle_error(self, DISK_ERR_WRITE_ERROR); // READ_ERROR
            free(buffer);
            finish_operation(self); // ends the operation, as RetroCore's HandleError clears Active
            return;
        }

        free(buffer);

        dev_queue_io_delay(self, IODELAY_HDD_SMD, (IODelayedCallback)smd_read_end,
                           data->regs.selectedDisk->unit, self->interruptLevel);
        break;

    case DEVICE_OP_READ_PARITY_TRANSFER:

        if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
        {
            log_write(LOG_CAT_SMD, LOG_DEBUG,
                      "GO Op=%s Unit=%d C/H/S=%d/%d/%d LBA=%" PRId64 " WC=%d CoreAddr=%o\n",
                      smd_op_name(DEVICE_OP_READ_PARITY_TRANSFER), data->regs.selectedUnit,
                      cylinder, head, sector, lba, word_counter, core_address);
        }
        buffer = (uint8_t *)malloc(block_counter * self->blockSizeBytes);
        if (!buffer)
        {
            handle_error(self, DISK_ERR_READ_ERROR); // READ_ERROR
            finish_operation(self); // ends the operation, as RetroCore's HandleError clears Active
            return;
        }

        // Read all blocks from SMD disk file into buffer
        blocks_read = self->blockCallbacks.readFunc(self, buffer, block_counter, lba,
                                                    data->regs.selectedDisk->unit);
        if ((blocks_read < 0) || ((uint32_t)blocks_read != block_counter))
        {
            handle_error(self, DISK_ERR_READ_ERROR); // READ_ERROR
            free(buffer);
            finish_operation(self); // ends the operation, as RetroCore's HandleError clears Active
            return;
        }

        // Read and check parity without transferring data (meaning what??)
        while (word_counter > 0)
        {
            // Read word from disk
            (void)dev_io_buffer_read_word(self, buffer, buffer_ptr++);

            //if (readData != WHAT??) then ERROR ?

            core_address = increment_core_address(regs);
            word_counter = decrement_word_counter(regs);
        }

        free(buffer);

        dev_queue_io_delay(self, IODELAY_HDD_SMD, (IODelayedCallback)smd_read_end,
                           data->regs.selectedDisk->unit, self->interruptLevel);
        break;

    case DEVICE_OP_COMPARE_TRANSFER:

        if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
        {
            log_write(LOG_CAT_SMD, LOG_DEBUG,
                      "GO Op=%s Unit=%d C/H/S=%d/%d/%d LBA=%" PRId64 " WC=%d CoreAddr=%o\n",
                      smd_op_name(DEVICE_OP_COMPARE_TRANSFER), data->regs.selectedUnit, cylinder,
                      head, sector, lba, word_counter, core_address);
        }

        buffer = (uint8_t *)malloc(block_counter * self->blockSizeBytes);
        if (!buffer)
        {
            handle_error(self, DISK_ERR_READ_ERROR); // READ_ERROR
            finish_operation(self); // ends the operation, as RetroCore's HandleError clears Active
            return;
        }

        // Read all blocks from SMD disk file into buffer
        blocks_read = self->blockCallbacks.readFunc(self, buffer, block_counter, lba,
                                                    data->regs.selectedDisk->unit);
        if ((blocks_read < 0) || ((uint32_t)blocks_read != block_counter))
        {
            handle_error(self, DISK_ERR_READ_ERROR); // READ_ERROR
            free(buffer);
            finish_operation(self); // ends the operation, as RetroCore's HandleError clears Active
            return;
        }

        while (word_counter > 0)
        {
            // Read from disk
            uint32_t disk_data = dev_io_buffer_read_word(self, buffer, buffer_ptr++);

            // Read from memory (DMA)
            int32_t mem_data;
            mem_data = dev_dma_read(core_address);

            // Compare data
            if (mem_data < 0 || disk_data != (uint32_t)mem_data)
            {
                handle_error(self, DISK_ERR_COMPARER_ERROR); // COMPARER_ERROR
                free(buffer);
                finish_operation(
                    self); // ends the operation, as RetroCore's HandleError clears Active
                return;
            }

            core_address = increment_core_address(regs);
            word_counter = decrement_word_counter(regs);
        }
        free(buffer);

        dev_queue_io_delay(self, IODELAY_HDD_SMD, (IODelayedCallback)smd_read_end,
                           data->regs.selectedDisk->unit, self->interruptLevel);
        break;

    case DEVICE_OP_INITIATE_SEEK:
        if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
        {
            log_write(LOG_CAT_SMD, LOG_DEBUG, "GO Op=%s Unit=%d C/H/S=%d/%d/%d pos=%" PRId64 "\n",
                      smd_op_name(DEVICE_OP_INITIATE_SEEK), data->regs.selectedUnit, cylinder, head,
                      sector, position);
        }
        // SEEK TIMING MODEL (M4/M6/M7): we deliberately do NOT model physical drive
        // timing (CDC 976x / Fujitsu Eagle: 3600 RPM = 16.67 ms/rev, ~30 ms avg
        // seek, 500 ms seek-timeout). A fast emulated seek (the IODELAY_HDD_SMD
        // delay below) is fine. What matters for DISC-TEMA is the on-cylinder (b14)
        // STATE TRANSITION, not duration: b14 must be 0 WHILE seeking (i.e. from GO
        // until the queued completion callback fires) and 1 when positioned.
        // DISC-TEMA checks both edges: "became 1 immediately" after M4 and
        // "remained 1" after M7.
        // The seek address is validated even in TEST MODE (the global address
        // check above is test-mode-exempt for the DMA loopback's sake): control
        // word bit 2 hands the cylinder to the drive's servo, and a cylinder
        // outside the recording field is a SEEK ERROR whatever mode the
        // controller is in ("an address greater than the maximum number of
        // tracks has been selected", seek-condition b11). DISC-TEMA section 7
        // provokes exactly this and reads the seek condition.
        if (cylinder >= data->regs.selectedDisk->maxCylinders)
        {
            data->seekCondition.bits.seekError = 1;
            handle_error(self, DISK_ERR_ADDRESS_MISMATCH);
            finish_operation(self);
            return;
        }

        data->seekCondition.bits.seekError = 0;
        data->regs.selectedDisk->onCylinder = 0; // heads moving
        data->regs.seekIssuedMask |= (uint8_t)(1 << data->regs.selectedUnit);

        // IODELAY_HDD_SMD, as RetroCore's NDBusDiscControllerSMD.cs does for
        // Initiate Seek. With a 2000-tick seek the controller was still active
        // when SINTRAN gave Seek Complete Search straight after Initiate Seek;
        // that control word was refused as an illegal load and SINTRAN
        // reported "Parallel seek disabled". Known cost: DISC-TEMA's check that
        // on-cylinder (status b14) is 0 right after Initiate Seek likely fails.
        dev_queue_io_delay(self, IODELAY_HDD_SMD, (IODelayedCallback)smd_read_end,
                           data->regs.selectedDisk->unit, self->interruptLevel);
        break;

    case DEVICE_OP_WRITE_FORMAT:
        // Format operation. Actual media formatting is not modelled (the image
        // file needs no physical layout), but the WORD COUNT is validated.
        // M5's word count is the size of the FORMAT SPECIFICATION the
        // controller DMA-reads from memory: 2 words of address-field data per
        // sector, i.e. 2 x sectorsPrTrack words per track (observed live: every
        // working DISC-TEMA format issues M5 with WC = 44B = 36 = 2x18 on the
        // 75MB disk - including the deliberate "Write-Incorrect-Format", which
        // must SUCCEED as a write). A count that is not a whole number of
        // track specifications leaves the controller waiting for format data
        // that never lines up with the sector pulses -> the 500 ms controller
        // timeout, status b6 (DISC-TEMA section 5 provokes this with WC 30000B
        // and demands "Bit 6b (timeout)").
        {
            uint32_t fmt_words_pr_track = 2u * (uint32_t)data->regs.selectedDisk->sectorsPrTrack;
            if (fmt_words_pr_track == 0 || word_counter == 0 ||
                (word_counter % fmt_words_pr_track) != 0)
            {
                if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
                {
                    log_write(LOG_CAT_SMD, LOG_DEBUG,
                              "GO Op=%s Unit=%d WC=%u not k x %u format words -> TIMEOUT\n",
                              smd_op_name(DEVICE_OP_WRITE_FORMAT), data->regs.selectedUnit,
                              word_counter, fmt_words_pr_track);
                }
                handle_error(self, DISK_ERR_TIMEOUT);
                finish_operation(self);
                return;
            }
        }

        if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
        {
            log_write(LOG_CAT_SMD, LOG_DEBUG, "GO Op=%s Unit=%d (media format not modelled)\n",
                      smd_op_name(DEVICE_OP_WRITE_FORMAT), data->regs.selectedUnit);
        }
        dev_queue_io_delay(self, IODELAY_HDD_SMD, (IODelayedCallback)smd_read_end,
                           data->regs.selectedDisk->unit, self->interruptLevel);
        break;

    case DEVICE_OP_SEEK_COMPLETE_SEARCH:
        if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
        {
            log_write(LOG_CAT_SMD, LOG_DEBUG, "GO Op=%s Unit=%d seekIssued=%d\n",
                      smd_op_name(DEVICE_OP_SEEK_COMPLETE_SEARCH), data->regs.selectedUnit,
                      (data->regs.seekIssuedMask >> data->regs.selectedUnit) & 1);
        }
        // M6 searches for the seek-complete pulse of a PREVIOUSLY initiated
        // seek. With no seek outstanding on this unit there is no pulse to
        // find, and the controller runs into its timeout (status b6) -
        // DISC-TEMA section 6: "Seek Complete Search (with no previous seek),
        // Status Bit 6b (timeout) is 0 !". The controller stays ACTIVE for the
        // whole search window and only then raises the timeout - dropping
        // active immediately fails "Status Bit 2b (active) becomes 0
        // immediately !!".
        if (!(data->regs.seekIssuedMask & (1 << data->regs.selectedUnit)))
        {
            dev_queue_io_delay(self, SMD_TIMEOUT_TICKS, (IODelayedCallback)smd_timeout_end,
                               data->regs.selectedDisk->unit, self->interruptLevel);
            break;
        }
        regs->selectedDisk->onCylinder = true;
        data->seekCondition.bits.seekError = 0;
        data->seekCondition.bits.seekComplete |= (uint16_t)(1 << regs->selectedUnit);

        dev_queue_io_delay(self, IODELAY_HDD_SMD, (IODelayedCallback)smd_read_end,
                           data->regs.selectedDisk->unit, self->interruptLevel);
        break;

    case DEVICE_OP_RETURN_TO_ZERO_SEEK:
        if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
        {
            log_write(LOG_CAT_SMD, LOG_DEBUG, "GO Op=%s Unit=%d\n",
                      smd_op_name(DEVICE_OP_RETURN_TO_ZERO_SEEK), data->regs.selectedUnit);
        }
        // RTZ is a seek to cylinder 0: heads move, so on-cylinder DROPS now and
        // the completion callback restores it (DISC-TEMA: "Error after
        // Return-To-Zero Seek, Bit 16b (on cylinder) remained 1 !"). The
        // seek-complete bit is likewise set by the completion, not here.
        data->seekCondition.bits.seekError = 0;
        regs->selectedDisk->onCylinder = 0;
        data->regs.seekIssuedMask |= (uint8_t)(1 << data->regs.selectedUnit);

        // IODELAY_HDD_SMD as in RetroCore; see the Initiate Seek case.
        dev_queue_io_delay(self, IODELAY_HDD_SMD, (IODelayedCallback)smd_read_end,
                           data->regs.selectedDisk->unit, self->interruptLevel);
        break;

    case DEVICE_OP_RUN_ECC_OPERATION:
        if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
        {
            log_write(LOG_CAT_SMD, LOG_DEBUG, "GO Op=%s (NOT IMPLEMENTED)\n",
                      smd_op_name(DEVICE_OP_RUN_ECC_OPERATION));
        }
        // Run ECC operation
        // TODO: Implement ECC operation
        // Unimplemented, but it WAS activated (control-word bit 2), so it still
        // has to finish - otherwise the controller stays active forever.
        finish_operation(self);
        break;

    case DEVICE_OP_SELECT_RELEASE:
        if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
        {
            log_write(LOG_CAT_SMD, LOG_DEBUG, "GO Op=%s Unit=%d\n",
                      smd_op_name(DEVICE_OP_SELECT_RELEASE), data->regs.selectedUnit);
        }
        // Release disk selection
        regs->selectedDisk = NULL;
        finish_operation(self);
        break;
    default:
        break;
    }
}

static bool smd_read_end(Device *self, int drive)
{
    if (!self)
    {
        return false;
    }
    SMDData *data = (SMDData *)self->deviceData;
    if (!data)
    {
        return false;
    }

    if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
    {
        log_write(LOG_CAT_SMD, LOG_DEBUG, "IO Complete drive=%d intEnabled=%d -> %s\n", drive,
                  data->statusRegister.bits.interruptEnabled,
                  data->statusRegister.bits.interruptEnabled ? "INTERRUPT" : "no interrupt");
    }

    data->statusRegister.bits.active = 0;
    data->statusRegister.bits.readyForTransfer = 1;

    clear_flip_flops(&data->regs);

    // Completion is operation-aware. Only a completed SEEK (M4/M6/M7) raises
    // the unit's seek-complete bit and puts the heads back on cylinder; a
    // completed TRANSFER must NOT set seek-complete (DISC-TEMA section 7 reads
    // the seek condition "With no previous Initiate-Seek" after transfers and
    // requires bits 0-7 zero). The control register still holds the operation
    // of the GO this callback belongs to. |= not =: other units keep their
    // pending seek-complete state.
    switch (data->controlRegister.bits.deviceOperation)
    {
    case DEVICE_OP_INITIATE_SEEK:
    case DEVICE_OP_SEEK_COMPLETE_SEARCH:
        data->seekCondition.bits.seekComplete |= (uint16_t)(1 << drive);
        if (drive >= 0 && drive < data->regs.maxUnits)
        {
            data->regs.disks[drive].onCylinder = 1;
        }
        break;
    case DEVICE_OP_RETURN_TO_ZERO_SEEK:
        // RTZ puts the heads back on cylinder 0 but does NOT raise the
        // seek-complete condition: the manual ties that condition to "the
        // initiate seek commands", and DISC-TEMA section 7 reads the seek
        // condition after an RTZ "With no previous Initiate-Seek" expecting
        // bits 0-7 zero. (Inferred from that check; the manuals don't state
        // the RTZ case explicitly.)
        if (drive >= 0 && drive < data->regs.maxUnits)
        {
            data->regs.disks[drive].onCylinder = 1;
        }
        break;
    default:
        break;
    }

    if (data->statusRegister.bits.interruptEnabled)
    {
        return true; // returning true triggers GenerateInterrupt()
    }
    return false;
}

// Delayed controller-timeout completion (M6 with no outstanding seek): the
// search window has elapsed with no seek-complete pulse. Raise timeout (b6,
// with abnormal completion via HandleError) and terminate the operation the
// same way SMDReadEnd does.
static bool smd_timeout_end(Device *self, int drive)
{
    (void)drive;
    if (!self)
    {
        return false;
    }
    SMDData *data = (SMDData *)self->deviceData;
    if (!data)
    {
        return false;
    }

    if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
    {
        log_write(LOG_CAT_SMD, LOG_DEBUG, "TIMEOUT drive=%d (seek-complete search found no seek)\n",
                  drive);
    }

    handle_error(self, DISK_ERR_TIMEOUT);

    data->statusRegister.bits.active = 0;
    data->statusRegister.bits.readyForTransfer = 1;
    clear_flip_flops(&data->regs);

    if (data->statusRegister.bits.interruptEnabled)
    {
        return true;
    }
    return false;
}

// Is loading a data register (memory address, block address, word counter)
// illegal right now?  Two conditions, from two sources that we take as
// complementary rather than conflicting:
//  - ND-11.020.01 sec 2.5 bit 5: "Load of any register while STATUS BIT 2 is
//    true" - the controller is ACTIVE. Test the status bit, not the control
//    word's activate bit - the two part company as soon as an operation
//    completes (SMDReadEnd clears status bit 2 while the control word still
//    holds bit 2 from the last GO).
//  - ND-830005.3 (DISC-TEMA) status-bit table, bit 5: "Illegal load, i.e. load
//    while the unit is NOT ON CYLINDER". This is what DISC-TEMA section 5
//    exercises right after a seek: the heads are moving, so a register load
//    must be refused. Only applies when a real, mounted unit is selected -
//    with no drive there is no on-cylinder signal at all.
// The CONTROL WORD register is NOT gated by the on-cylinder condition (only by
// active, with device-clear excepted) - it has to be loadable off-cylinder,
// otherwise the seek/RTZ commands that MOVE the heads could never be issued.
static bool load_is_illegal(Device *self, SMDData *data)
{
    if (data->statusRegister.bits.active)
    {
        return true;
    }
    if (data->regs.selectedDisk && unit_attached(self, data->regs.selectedDisk) &&
        !data->regs.selectedDisk->onCylinder)
    {
        return true;
    }
    return false;
}

// Is a disk pack mounted on this unit? A unit with no attached image is a
// powered-off drive: it must report DISK UNIT NOT READY (status b13), and a GO
// against it is a hardware error (b7). DISC-TEMA requires every unit not under
// test to be powered off (ND-11.020.01 sec 5 item 16) and checks b13 "by the
// selection of specified units and by reading from non-specified units"
// (item 14). Without this, units 1-3 answered READY on a unit-0 run and
// DISC-TEMA reported "Status bit 15b became 0 (Unit ready) after selection"
// (15b octal = b13) for each of them.
//
// The size callback stats the image file, so the answer is cached per unit -
// the status register is read tens of thousands of times in one test run.
static bool unit_attached(Device *self, DiskInfo *disk)
{
    if (!self || !disk)
    {
        return false;
    }
    if (!disk->unitAttachChecked)
    {
        size_t image_size = 0;
        bool is_write_protected = false;
        if (self->blockCallbacks.diskInfoFunc)
        {
            self->blockCallbacks.diskInfoFunc(self, &image_size, &is_write_protected, disk->unit);
        }
        disk->unitAttached = (image_size > 0);
        disk->unitAttachChecked = true;
    }
    return disk->unitAttached;
}

// Terminate the current device operation on a path that does NOT reach the
// queued I/O-delay completion (SMDReadEnd).
//
// Status bit 2 is set from control-word bit 2 when the operation is activated,
// and SMDReadEnd is the only thing that clears it. An error exit that simply
// returns therefore leaves the controller "active" for the rest of the session,
// after which every register load is correctly flagged illegal-load (bit 5) and
// nothing works again. DISC-TEMA reports this as "Status Bit 2b (active)
// remained 1 !!" followed by a cascade of unrelated-looking failures.
//
// ND-11.020.01 sec 2.5: bit 2 = "Controller active", bit 3 = "Controller
// finished with a device operation". An operation that ended in error has still
// finished, so bit 3 is set as bit 2 is cleared; the error bits set by
// HandleError stay, and bit 4 (inclusive OR) is recomputed on the status read.
static void finish_operation(Device *self)
{
    if (!self)
    {
        return;
    }
    SMDData *data = (SMDData *)self->deviceData;
    if (!data)
    {
        return;
    }

    data->statusRegister.bits.active = 0;
    data->statusRegister.bits.readyForTransfer = 1;

    clear_flip_flops(&data->regs);

    // Control-word bit 0 is "enable interrupt on device not active", and the
    // controller has just gone not-active.
    if (data->statusRegister.bits.interruptEnabled)
    {
        dev_set_interrupt_status(self, true, self->interruptLevel);
    }
}

static void clear_flip_flops(ControllerRegs *regs)
{
    regs->wcwFlipFlop = false;
    regs->wcEccwFlipFlop = false;
    regs->wcrFlipFlop = false;
    regs->mawFlipFlop = false;
    regs->marFlipFlop = false;
}

static int64_t convert_cylinder_head_sector_to_logical_block(ControllerRegs *regs, int cylinder,
                                                             int head, int sector)
{
    if (!regs || !regs->selectedDisk)
    {
        return -1;
    }

    // LBA = (C x HPC + H) x SPT + (S - 1)
    if ((cylinder == 0) && (head == 0) && (sector == 0))
    {
        return 0; // invalid, but used by SeekToZero
    }

    // 64-bit math - see the caller: a huge (illegal) cylinder must produce a
    // huge positive LBA, not a 32-bit wrap-around.
    return ((int64_t)cylinder * regs->selectedDisk->headsPrCylinder + head) *
               regs->selectedDisk->sectorsPrTrack +
           (sector); // was (sector-1), but for this BigDisk driver sector 0 is the start sector (not 1)
}

static void clear_errors(Device *self)
{
    if (!self)
    {
        return;
    }
    SMDData *data = (SMDData *)self->deviceData;

    data->statusRegister.bits.hardwareError = 0;
    data->statusRegister.bits.hardwareError2 = 0;
    data->statusRegister.bits.illegalLoad = 0;
    data->statusRegister.bits.timeOut = 0;
    data->statusRegister.bits.comparerError = 0;
    data->statusRegister.bits.addressMismatch = 0;
    data->statusRegister.bits.abnormalCompletion = 0;
    data->seekCondition.bits.seekError = 0;
}
static void set_selected_unit(ControllerRegs *regs, uint8_t unit)
{
    if (!regs)
    {
        return;
    }
    // Control-word bits 7-9 = a 3-bit unit field (0-7), but every ND SMD/ECC
    // controller (ND 558/559/632) handles only up to 4 drives - unit-select line
    // "8" is "not used" (ECC manual), "up to four drives per controller" (ND-11.020
    // sec.1), and FILSYS-INV reports max unit 3. So keep the full 3-bit value but
    // select NO disk for units 4-7 (DISC-TEMA REJECTS a not-specified unit, it does
    // NOT wrap unit&3 -> the hardware-error/not-ready checks must be observable).
    regs->selectedUnit = unit & 0x07;
    if (regs->selectedUnit < regs->maxUnits)
    {
        regs->selectedDisk = &regs->disks[regs->selectedUnit];
    }
    else
    {
        regs->selectedDisk = NULL;
    }
}

static void handle_error(Device *self, DiskError error)
{
    if (!self)
    {
        return;
    }
    SMDData *data = (SMDData *)self->deviceData;

    if (Log_IsEnabled(LOG_CAT_SMD, LOG_DEBUG))
    {
        log_write(LOG_CAT_SMD, LOG_DEBUG, "ERROR %s (%d)\n", smd_error_name(error), error);
    }

    // Every error termination is an ABNORMAL COMPLETION (status b12,
    // ND-11.020.01 sec 2.5). DISC-TEMA checks it in combination rather than on
    // its own - "Status bit 14b is 0 when Bit 7b is 1 !" and "... when bit 15b
    // is 1 !" (14b octal = b12, 7b = hardware error, 15b = disk unit not
    // ready). Cleared per operation at the next GO, like illegal-load.
    data->statusRegister.bits.abnormalCompletion = 1;
    switch (error)
    {
    case DISK_ERR_NO_DISK_ATTACHED: // NO_DISK_ATTACHED
        data->statusRegister.bits.diskUnitNotReady = 1;
        break;

    case DISK_ERR_ADDRESS_MISMATCH: // ADDRESS_MISMATCH
        data->statusRegister.bits.addressMismatch = 1;
        break;

    case DISK_ERR_SEEK_ERROR:
        data->statusRegister.bits.diskUnitNotReady = 1;
        break;

    case DISK_ERR_READ_ERROR:
        data->statusRegister.bits.diskUnitNotReady = 1;
        break;

    case DISK_ERR_WRITE_ERROR:
        data->statusRegister.bits.diskUnitNotReady = 1;
        break;

    case DISK_ERR_WRITE_PROTECT_ERROR:
        data->statusRegister.bits.diskUnitNotReady = 1;
        break;

    case DISK_ERR_COMPARER_ERROR:
        data->statusRegister.bits.comparerError = 1;
        break;

    case DISK_ERR_DRIVE_NOT_SELECTED: // DRIVE_NOT_SELECTED
        data->statusRegister.bits.diskUnitNotReady = 1;
        break;

    case DISK_ERR_ILLEGAL_WHILE_ACTIVE: // ILLEGAL_WHILE_DRIVE_IS_ACTIVE
        data->statusRegister.bits.illegalLoad = 1;
        break;

    case DISK_ERR_TIMEOUT: // 500 ms controller timeout (status b6)
        data->statusRegister.bits.timeOut = 1;
        break;
    default:
        break;
    }
}

static uint32_t increment_core_address(ControllerRegs *regs)
{
    if (!regs)
    {
        return 0;
    }
    uint32_t address = (regs->coreAddressHiBits << 16) | regs->coreAddress;
    address++;
    regs->coreAddress = address & 0xFFFF;
    regs->coreAddressHiBits = (address >> 16) & 0xFF;
    return address;
}

static uint32_t decrement_word_counter(ControllerRegs *regs)
{
    if (!regs)
    {
        return 0;
    }
    uint32_t counter = (regs->wordCounterHI << 16) | regs->wordCounter;
    counter--;
    regs->wordCounter = counter & 0xFFFF;
    regs->wordCounterHI = (counter >> 16) & 0xFF;
    return counter;
}

Device *smd_create_device(uint8_t thumbwheel)
{
    Device *dev = (Device *)malloc(sizeof(Device));
    if (!dev)
    {
        return NULL;
    }

    SMDData *data = (SMDData *)malloc(sizeof(SMDData));
    if (!data)
    {
        free(dev);
        return NULL;
    }

    memset(dev, 0, sizeof(Device));
    memset(data, 0, sizeof(SMDData));

    // Initialize device base structure
    dev_init(dev, thumbwheel, DEVICE_CLASS_BLOCK, 2048);

    dev->deviceData = data;

    // Controller type. The 10/15 MHz SMD cards load and read back the 24-bit
    // memory-address and word-count registers with TWO accesses (HI then LO);
    // the BIG-DISC and ECC cards do it in ONE. This is a real hardware
    // difference and the ND-120 recreation can be strapped either way, so make
    // it selectable here instead of hardwiring it - otherwise the emulator and
    // the FPGA cannot be compared in the same configuration.
    //   ND100X_SMD_TYPE=smd15 (default) | smd10 | ecc | bigdisc
    data->controllerType = CONTR_SMD_15MHZ; // 10 and 15Mhz has flip-flops
    {
        const char *t = getenv("ND100X_SMD_TYPE");
        if (t != NULL)
        {
            if (strcmp(t, "ecc") == 0)
            {
                data->controllerType = CONTR_ECC_DISC;
            }
            else if (strcmp(t, "bigdisc") == 0)
            {
                data->controllerType = CONTR_BIG_DISC;
            }
            else if (strcmp(t, "smd10") == 0)
            {
                data->controllerType = CONTR_SMD_10MHZ;
            }
            else if (strcmp(t, "smd15") != 0)
            {
                LOG(LOG_CAT_SMD, LOG_WARN, "unknown ND100X_SMD_TYPE '%s', using smd15\n", t);
            }
        }
    }
    if (data->controllerType == CONTR_SMD_10MHZ || data->controllerType == CONTR_SMD_15MHZ)
    {
        data->regs.hasFlipFlops = true;
    }
    else
    {
        data->regs.hasFlipFlops = false;
    }

    // Word-counter protocol: follows the card by default, separately override-
    // able so the single-+7-write mass-load path can be tested against a card
    // that is otherwise the two-access 15 MHz type. See device_smd.h.
    data->regs.hasWordCountFlipFlop = data->regs.hasFlipFlops;
    {
        const char *w = getenv("ND100X_SMD_WC_FF");
        if (w != NULL)
        {
            data->regs.hasWordCountFlipFlop = (w[0] != '0');
        }
    }

    // Two-access load order: HI first (as the memory-address text states) or
    // LO first (which would match the documented READ order and make a single
    // +7 write load a full count). See device_smd.h.
    data->regs.loadLowFirst = false;
    {
        const char *o = getenv("ND100X_SMD_LOAD_ORDER");
        if (o != NULL && strcmp(o, "lo") == 0)
        {
            data->regs.loadLowFirst = true;
        }
    }

    // Initialize device properties
    data->bytes_pr_sector = 1024; // Standard SMD sector size
    data->sectors_pr_track = 32;  // Standard SMD sectors per track

    // Set up function pointers
    dev->Read = smd_read;
    dev->Write = smd_write;
    dev->Tick = smd_tick;
    dev->Reset = smd_reset;
    dev->Ident = smd_ident;
    dev->Boot = smd_boot;
    dev->Destroy = smd_destroy;
    // Initialize device state
    smd_reset(dev);

    data->regs.maxUnits = 4; // Max disks
    data->regs.disks = malloc(sizeof(DiskInfo) * data->regs.maxUnits);
    if (!data->regs.disks)
    {

        free(data);
        free(dev);
        return NULL;
    }
    memset(data->regs.disks, 0, sizeof(DiskInfo) * data->regs.maxUnits);

    // Initialize each DiskInfo's unit index. Without this, disks[i].unit stayed 0
    // (memset), and the M0/M1/M2/M3 data transfers pass selectedDisk->unit to the
    // block callback (device_smd.c ExecuteGO), so EVERY read/write hit unit 0's file
    // regardless of the selected unit - i.e. "only one disc works". Attach/size and
    // SMD_Boot already used the correct regs.selectedUnit; only the data transfers
    // used this field. Setting it here makes selectedDisk->unit valid everywhere.
    for (int i = 0; i < data->regs.maxUnits; i++)
    {
        data->regs.disks[i].unit = (uint8_t)i;
        // A powered-up drive that has loaded its heads sits ON CYLINDER (over
        // cylinder 0) until a seek moves them. Starting at 0 would make every
        // register load before the first seek an illegal load (b5 = load while
        // not on cylinder, ND-830005.3 status-bit table).
        data->regs.disks[i].onCylinder = 1;
    }

    // blockSizeBytes will be set when a disk is selected (SMD_Boot or ExecuteGO)

    // Set up device address and interrupt settings based on thumbwheel
    switch (thumbwheel)
    {
    case 0:
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "SMD 1540");
        dev->identCode = 017; // octal 017
        dev->startAddress = 01540;
        break;
    case 1:
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "SMD 1550");
        dev->identCode = 020; // Octal 020
        dev->startAddress = 01550;
        break;
    case 2:
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "SMD 540");
        dev->identCode = 023; // Octal 02
        dev->startAddress = 0540;
        break;
    case 3:
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "SMD 550");
        dev->identCode = 06; // Octal 06
        dev->startAddress = 0550;
        break;
    default:
        LOG(LOG_CAT_SMD, LOG_WARN, "SMD: Unknown thumbwheel value: %d\n", thumbwheel);
        free(data);
        free(dev);
        return NULL;
    }

    dev->interruptLevel = 11; // disk
    dev->endAddress = dev->startAddress + 7;

    // Match the Floppy DMA line shape: name+instance, then Address[lo-hi] (octal),
    // Ident code (octal), Level (decimal). Values come from THIS instance's real
    // configured fields (set per thumbwheel above), not literals - so the 1540/1550/
    // 540/550 slots each print their own address range / ident / level.
    LOG(LOG_CAT_SMD, LOG_INFO,
        "SMD [%s] Device object created. Address[%o-%o] Ident code: [%o] Level: [%d]\n",
        dev->memoryName, dev->startAddress, dev->endAddress, dev->identCode, dev->interruptLevel);

    return dev;
}

/// @brief Device specific destroy function
/// @param dev
static void smd_destroy(Device *dev)
{
    if (!dev)
    {
        return;
    }

    SMDData *data = (SMDData *)dev->deviceData;
    if (data)
    {
        // Free disk array if it exists
        if (data->regs.disks)
        {
            free(data->regs.disks);
            data->regs.disks = NULL;
        }
    }
}

/*
TPE>fun
Disc name: disc-75-1
Unit (0 to 3 oct): 0
On this Disc type, Function will destroy data in the last
cylinder in the spare track buffer pool !
(Including the Alternative Track Table.)

Do you still want to continue (YES or NO): Y

  1. Data way to controller test  === End of test ===
  2. Memory Address Register test === End of test ===
  3. Block Address Register test  === End of test ===
  4. Test-Mode test
Error after Compare-In-Test-Mode
Status and Failing Bits:  040010 001020
=== End of test ===
  5. Status Register bits test
Error after Return-To-Zero Seek, Bit 16b (on cylinder) remained 1 !
   Status 040070b, Unit 0
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true


Error. Status bit 14b is 0 when Bit 5b is 1 !
   Status 040070b, Unit 0
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true


Error after Write Format Word Count 10000b, Bit 6b (timeout) became 0.
   Status 040070b, Unit 0
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true


Error after Read (from NOT specified unit), Status Bit 7b is 0 !

   Status 040474b, Unit 4
   - Controller active
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true
   - Address mismatch


Error after Write-Incorrect-Format
   Status 040474b, Unit 0
   - Controller active
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true
   - Address mismatch


Error after Read (parity error expected), Status Bit 11b is 0 !
   Status 040474b, Unit 0
   - Controller active
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true
   - Address mismatch


Error after Read
   Status 040474b, Unit 0
   - Controller active
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true
   - Address mismatch


Error after Read (overrun expected), Status Bit 13b remains 0 !        7)
   Status 040474b, Unit 0
   - Controller active
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true
   - Address mismatch


Error after Write (overrun expected), Status Bit 13b remains 0 !        7)
   Status 040474b, Unit 0
   - Controller active
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true
   - Address mismatch


Error. Status bit 15b became 0 (Unit ready) after selection.
   Status 040474b, Unit 1
   - Controller active
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true
   - Address mismatch


Error. Status bit 15b became 0 (Unit ready) after selection.
   Status 040474b, Unit 2
   - Controller active
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true
   - Address mismatch


Error. Status bit 15b became 0 (Unit ready) after selection.
   Status 040474b, Unit 3
   - Controller active
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true
   - Address mismatch


Error after Initiate Seek,
status bit 16b (on cylinder) became 1 immediately.
   Status 040474b, Unit 0
   - Controller active
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true
   - Address mismatch


Error. Status bit 17b became 0 !
=== End of test ===
  6. Operation test
***ERROR***
   Status 040474b
   - Controller active
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true
   - Address mismatch


Error. Not on-cylinder, or active.
   Status 040474b, Unit 0
   - Controller active
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true
   - Address mismatch


Error after Read
   Status 040474b, Unit 0
   - Controller active
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true
   - Address mismatch


Error after Write
   Status 040474b, Unit 0
   - Controller active
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true
   - Address mismatch


Error after Read
   Status 040474b, Unit 0
   - Controller active
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true
   - Address mismatch


WRITE DOES NOT WORK (?).
After read data, complement it, write it back, read it again,
the second block of data is equal to the first !!

READ OR WRITE DOES NOT WORK (?).
After read data, complement it, write it back, read it again,
the complement of the second block of data is unequal to the first !!
Seek Complete Search (with no previous seek),
Status Bit 2b (active) remained 1 !!
   Status 040474b, Unit 0
   - Controller active
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true
   - Address mismatch

=== End of test ===
  7. Read-Seek-Condition test
***ERROR***
   Status 040474b
   - Controller active
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true
   - Address mismatch


Error. Not on-cylinder, or active.
   Status 040474b, Unit 0
   - Controller active
   - Controller finished
   - Illegal load, i.e. load while status bit 2 is true
   - Address mismatch


Error after READ-SEEK-Condition, for a specified unit,
Bits 12b-10b (unit no.) are incorrect.
Seek-Condition 000001 Unit No.: 0


Error after READ-SEEK-Condition, for a specified unit,
With no previous Initiate-Seek,
Bits 7b-0b (Seek-Complete) are nonzero !
Seek-Condition 000001 Unit No.: 0


Error after READ-SEEK-Condition, for a specified unit,
With previous illegal block address,
Bit 13b (Seek Error) is 0 !
Seek-Condition 010401 Unit No.: 0

=== End of test ===
*/
