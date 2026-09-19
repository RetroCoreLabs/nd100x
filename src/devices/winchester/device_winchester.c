/*
 * nd100x - ND100 Virtual Machine
 *
 * Copyright (c) 2026 Ronny Hansen
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
 * 5 1/4 inch (ST506) and 8 inch Winchester Disk Controller, cards 3041/3038.
 * Register model per ND-11.015.01 sections 3.1 - 3.5; see device_winchester.h
 * for the address map and for why this controller differs from the SMD one
 * (two-access memory address, SINGLE-access word count).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../devices_types.h"
#include "../devices_protos.h"

#include "device_winchester.h"

/* Registers, offset from the device base address (ND-11.015.01 sec 3.1). */
// clang-format off
typedef enum {
    WD_READ_MEMORY_ADDRESS = 0,
    WD_LOAD_MEMORY_ADDRESS = 1,
    WD_READ_SECTOR_COUNTER = 2,
    WD_LOAD_BLOCK_ADDRESS  = 3,
    WD_READ_STATUS         = 4,
    WD_LOAD_CONTROL_WORD   = 5,
    WD_READ_BLOCK_ADDRESS  = 6,
    WD_LOAD_WORD_COUNT     = 7
} WDRegister;
// clang-format on


static void Wd_Reset(Device *self);
static bool WdTransferEnd(Device *self, int drive);
static void Wd_ExecuteGO(Device *self);
static bool WdUnitAttached(Device *self, WDDiskInfo *disk);

static const char *Wd_OpName(WDDeviceOperation op)
{
    switch (op)
    {
    case WD_OP_READ_TRANSFER:
        return "M0-Read";
    case WD_OP_WRITE_TRANSFER:
        return "M1-Write";
    case WD_OP_READ_PARITY:
        return "M2-ReadParity";
    case WD_OP_COMPARE:
        return "M3-Compare";
    case WD_OP_SEEK:
        return "M4-Seek";
    case WD_OP_WRITE_FORMAT:
        return "M5-WriteFormat";
    case WD_OP_LOAD_CTRL_BITS:
        return "M6-LoadCtrlBits";
    case WD_OP_RETURN_TO_ZERO:
        return "M7-ReturnToZero";
    default:
        return "Unknown";
    }
}

/*
 * The upper/lower selection flip-flop. ND-11.015.01 sec 3.2 lists FOUR things
 * that set it: master clear, programmed device clear (control word bit 4),
 * read status register, and ACTIVATION (control word bit 2). The last one is
 * easy to miss and matters: a driver that activates without re-reading status
 * still gets a known state.
 */
static void Wd_ClearFlipFlops(WDControllerRegs *regs)
{
    regs->memoryAddressWriteFF = false;
    regs->memoryAddressReadFF = false;
}

/*
 * Is a disk pack mounted on this unit? A unit with no image is a powered-off
 * drive and must report not ready. The size callback stats a file, so the
 * answer is cached per unit - status is read very frequently.
 */
static bool WdUnitAttached(Device *self, WDDiskInfo *disk)
{
    if (!self || !disk)
    {
        return false;
    }
    if (!disk->unitAttachChecked)
    {
        size_t imageSize = 0;
        bool isWriteProtected = false;
        if (self->blockCallbacks.diskInfoFunc)
        {
            self->blockCallbacks.diskInfoFunc(self, &imageSize, &isWriteProtected, disk->unit);
        }
        disk->unitAttached = (imageSize > 0);
        disk->diskIsWriteProtected = isWriteProtected;
        disk->diskFileSize = imageSize;
        disk->unitAttachChecked = true;
    }
    return disk->unitAttached;
}

/*
 * Terminate the current operation. Status bit 2 is set from control-word bit 2
 * and only a completion clears it, so every exit path - including the error
 * ones - has to come through here or the controller stays active forever.
 * Sec 3.5: bit 2 "Controller active", bit 3 "Controller finished with a device
 * operation"; an operation that ended in error has still finished.
 */
static void Wd_FinishOperation(Device *self)
{
    WinchesterData *data = (WinchesterData *)self->deviceData;
    if (!data)
    {
        return;
    }

    data->statusRegister.bits.active = 0;
    data->statusRegister.bits.readyForTransfer = 1;
    Wd_ClearFlipFlops(&data->regs);

    if (data->statusRegister.bits.interruptEnabled)
    {
        Device_SetInterruptStatus(self, true, self->interruptLevel);
    }
}

static void Wd_ClearErrors(WinchesterData *data)
{
    data->statusRegister.bits.inclusiveOrErrors = 0;
    data->statusRegister.bits.rtzViolation = 0;
    data->statusRegister.bits.timeOut = 0;
    data->statusRegister.bits.diskFault = 0;
    data->statusRegister.bits.addressMismatch = 0;
    data->statusRegister.bits.crcError = 0;
    data->statusRegister.bits.compareError = 0;
    data->statusRegister.bits.dmaChannelError = 0;
}

static void Wd_DeviceClear(Device *self)
{
    WinchesterData *data = (WinchesterData *)self->deviceData;

    data->statusRegister.bits.active = 0;
    Wd_ClearErrors(data);
    Wd_ClearFlipFlops(&data->regs);

    data->regs.memoryAddress = 0;
    data->regs.memoryAddressHiBits = 0;
    data->regs.wordCounter = 0;
    data->regs.blockAddress = 0;
    data->regs.sectorCounter = 0;

    Device_SetInterruptStatus(self, false, self->interruptLevel);
}

static uint16_t Wd_ReadStatus(Device *self)
{
    WinchesterData *data = (WinchesterData *)self->deviceData;
    WDStatusRegister st = data->statusRegister;

    /* Inclusive OR of the error bits (sec 3.5 bit 4). Bit 13 is excluded even
     * though the manual's wording says "bits 5-13": on a 3041 bit 13 is always
     * 1, so including it would make the error OR permanently set. */
    st.bits.inclusiveOrErrors = st.bits.rtzViolation | st.bits.timeOut | st.bits.diskFault |
                                st.bits.addressMismatch | st.bits.crcError | st.bits.compareError |
                                st.bits.dmaChannelError;

    /* Drive-sourced bits. With no unit attached the drive is powered off:
     * not on cylinder. */
    if (data->regs.selectedDisk && WdUnitAttached(self, data->regs.selectedDisk))
    {
        st.bits.onCylinder = data->regs.selectedDisk->onCylinder ? 1 : 0;
    }
    else
    {
        st.bits.onCylinder = 0;
    }

    /* Bit 13 is the card identity: the 3041 always reads 1, which is how
     * software tells a 3041 from a 3038 (sec 3.5). */
    st.bits.controllerId = (data->controllerType == WD_CONTR_3041) ? 1 : 0;

    /* Bit 15 always 0 - distinguishes this card from the 10 Mb controller. */
    st.bits.notUsed15 = 0;
    st.bits.notUsed12 = 0;

    /* A status read is one of the four flip-flop reset conditions. */
    Wd_ClearFlipFlops(&data->regs);

    return st.raw;
}

static uint16_t Wd_Read(Device *self, uint32_t address)
{
    WinchesterData *data = (WinchesterData *)self->deviceData;
    if (!data)
    {
        return 0;
    }

    uint32_t reg = Device_RegisterAddress(self, address);
    uint16_t value = 0;

    switch (reg)
    {
    case WD_READ_MEMORY_ADDRESS:
        /* Sec 3.2: read is the OPPOSITE order to write - the first IOX 500
         * returns the low 16 bits, the second the upper 8. */
        if (!data->regs.memoryAddressReadFF)
        {
            value = data->regs.memoryAddress;
        }
        else
        {
            value = data->regs.memoryAddressHiBits;
        }
        data->regs.memoryAddressReadFF = !data->regs.memoryAddressReadFF;
        break;

    case WD_READ_SECTOR_COUNTER:
        /* Sec 3.1 IOX table: "IOX 502  Not used". There is no programmer-
         * visible sector counter on this card - the "sector counters" named
         * in the hardware description are internal. Reads return 0, the same
         * as the write-only registers. */
        value = 0;
        break;

    case WD_READ_STATUS:
        value = Wd_ReadStatus(self);
        break;

    case WD_READ_BLOCK_ADDRESS:
        /* Sec 3.1 IOX table: "IOX 506  Read Block (disk) address register",
         * with NO test-mode qualifier. This used to be gated on test mode,
         * which was an invention - the cross-check against the portable core
         * (tests/wd_trace.c) caught the divergence and the manual settled it. */
        value = data->regs.blockAddress;
        break;

    default:
        value = 0;
        break;
    }

    if (Log_IsEnabled(LOG_CAT_WD, LOG_DEBUG))
    {
        Log_Write(LOG_CAT_WD, LOG_DEBUG, "IOX READ  addr=%o reg=%o -> %o\n", address, reg, value);
    }

    return value;
}

static void Wd_Write(Device *self, uint32_t address, uint16_t value)
{
    WinchesterData *data = (WinchesterData *)self->deviceData;
    if (!data)
    {
        return;
    }

    uint32_t reg = Device_RegisterAddress(self, address);

    if (Log_IsEnabled(LOG_CAT_WD, LOG_DEBUG))
    {
        Log_Write(LOG_CAT_WD, LOG_DEBUG, "IOX WRITE addr=%o reg=%o value=%o\n", address, reg,
                  value);
    }

    switch (reg)
    {
    case WD_LOAD_MEMORY_ADDRESS:
        /* Sec 3.2: "The first one loads the 8 upper bits of the 24 bits
         * address (A-reg 0-7 => Address 16-23). The second one loads the
         * lower 16 bits." */
        if (!data->regs.memoryAddressWriteFF)
        {
            data->regs.memoryAddressHiBits = (uint8_t)(value & 0xFF);
        }
        else
        {
            data->regs.memoryAddress = value;
        }
        data->regs.memoryAddressWriteFF = !data->regs.memoryAddressWriteFF;
        break;

    case WD_LOAD_BLOCK_ADDRESS:
        /* Sec 3.3: cylinder in bits 15-5, sector in bits 4-0. */
        data->regs.blockAddress = value;
        data->regs.cylinder = (value >> 5) & 0x7FF;
        data->regs.sector = value & 0x1F;
        break;

    case WD_LOAD_WORD_COUNT:
        /* SINGLE access - this is the register that differs from the 15 MHz
         * SMD card, and the reason the ND-120 mass-load microcode works here:
         * it writes the word count once, with 002000 (1024 words). For M4 the
         * same register carries the STEP COUNT (sec 3.4.5). */
        data->regs.wordCounter = value;
        break;

    case WD_LOAD_CONTROL_WORD:
        data->controlRegister.raw = value;

        data->statusRegister.bits.interruptEnabled =
            data->controlRegister.bits.enableInterruptNotActive;
        data->statusRegister.bits.errorInterruptEnabled =
            data->controlRegister.bits.enableInterruptOnErrors;

        data->regs.testMode = data->controlRegister.bits.testMode ? true : false;
        data->regs.head = (uint8_t)data->controlRegister.bits.head;         /* b5-8 */
        data->regs.selectedUnit = (uint8_t)data->controlRegister.bits.unit; /* b9, max 2 units */
        data->regs.deviceOperation = (WDDeviceOperation)data->controlRegister.bits.deviceOperation;
        /* Sec 3.4.5: "If bit 14 is zero, the heads will move towards
         * cylinder 0". */
        data->regs.seekDirection = data->controlRegister.bits.direction ? WD_SEEK_OUT : WD_SEEK_IN;
        data->regs.badTrack = data->controlRegister.bits.badTrack ? true : false;

        if (data->regs.selectedUnit < data->regs.maxUnits)
        {
            data->regs.selectedDisk = &data->regs.disks[data->regs.selectedUnit];
        }
        else
        {
            data->regs.selectedDisk = NULL;
        }

        /* Device clear (bit 4). Sec 3.4: "To clear the disk drive, it may be
         * necessary to execute two consecutive device clear before reading a
         * correct status."
         *
         * Processed INLINE - it must not skip the ready/interrupt update
         * below, because a single control word may carry device clear AND the
         * interrupt enable together. Same structure as the paper-tape reader
         * (device_paper_tape.c: "processed inline, does NOT break"). */
        if (data->controlRegister.bits.deviceClear)
        {
            Wd_DeviceClear(self);
        }

        /* Activation (bit 2). Sec 3.4 line: "All device operation codes will
         * be activated when the code and bit 3 (activate device) is loaded,
         * except for M6 where no activation should be made."
         *
         * That is guidance to the PROGRAMMER; the manual never says what the
         * hardware does if you activate M6 anyway. M6 only sets additional
         * control bits (3038 only) - it is a control-bit load, not an
         * operation - so a control word carrying it takes the NON-activating
         * path below, leaving the card ready. An early return here instead
         * would leave status bit 3 clear and the card unable to interrupt.
         * The portable Pi Pico core (nd_winchester.c) does the same; the
         * cross-check trace exists to keep the two from drifting apart. */
        if (data->controlRegister.bits.active && data->regs.deviceOperation != WD_OP_LOAD_CTRL_BITS)
        {
            /* Activation is a flip-flop reset condition (sec 3.2). */
            Wd_ClearFlipFlops(&data->regs);

            data->statusRegister.bits.active = 1;
            data->statusRegister.bits.readyForTransfer = 0;
            Wd_ClearErrors(data);

            /* Sec 4.1 states BINT11 as a condition on the CURRENT status -
             * ready (bit 3) AND interrupt enabled (bit 0) - not as a pulse.
             * Activation drops bit 3, so the interrupt line drops with it and
             * comes back at completion. Without this an interrupt armed
             * before the operation stays asserted for the whole transfer.
             * Same shape as the paper-tape reader, which drops
             * readyForTransfer for the duration of the read and updates the
             * interrupt on both sides of it (device_paper_tape.c:130-149). */
            Device_SetInterruptStatus(self, false, self->interruptLevel);

            Wd_ExecuteGO(self);
            break;
        }

        /* Control word WITHOUT the activate bit: the controller stays idle and
         * is by definition ready for an operation, so status bit 3 goes to 1.
         *
         * Sec 4.1: "If the controller is ready for an operation (status bit
         * 3 = 1), and interrupt is enabled (status bit 0 has been set by
         * control bit 0 = 1), the interrupt signal BINT11 will be active,
         * giving an interrupt to level 11 ... The IDENT code may now be read
         * by an IDENT PL11 instruction."
         *
         * That is exactly how the TPE CONFIGURATION program probes for the
         * card: it enables the interrupt on an idle controller and then does
         * IDENT PL11. Without this the probe reports
         * "No identcode found on level 11D, expected identcode: 1B".
         * The SMD card does the same thing (device_smd.c LOAD_CONTROL_WORD). */
        data->statusRegister.bits.readyForTransfer = 1;
        Device_SetInterruptStatus(self,
                                  data->statusRegister.bits.interruptEnabled &&
                                      data->statusRegister.bits.readyForTransfer,
                                  self->interruptLevel);
        break;

    default:
        break;
    }
}

/*
 * Completion callback: the operation has finished, so the controller goes not
 * active and raises the interrupt if enabled.
 */
static bool WdTransferEnd(Device *self, int drive)
{
    WinchesterData *data = (WinchesterData *)self->deviceData;
    if (!data)
    {
        return false;
    }

    (void)drive;

    data->statusRegister.bits.active = 0;
    data->statusRegister.bits.readyForTransfer = 1;
    Wd_ClearFlipFlops(&data->regs);

    if (Log_IsEnabled(LOG_CAT_WD, LOG_DEBUG))
    {
        Log_Write(LOG_CAT_WD, LOG_DEBUG, "IO complete drive=%d intEnabled=%d\n", drive,
                  data->statusRegister.bits.interruptEnabled);
    }

    return data->statusRegister.bits.interruptEnabled ? true : false;
}

static void Wd_ExecuteGO(Device *self)
{
    WinchesterData *data = (WinchesterData *)self->deviceData;
    WDControllerRegs *regs = &data->regs;

    if (!regs->selectedDisk)
    {
        /* No such unit. Report a disk fault and terminate - never hang. */
        data->statusRegister.bits.diskFault = 1;
        Wd_FinishOperation(self);
        return;
    }

    WDDiskInfo *disk = regs->selectedDisk;

    /* M4 and M7 position the arm and need no image; every other operation
     * needs a mounted pack. Sec 3.4: "For M4, only Word count (= step count)
     * and unit number is necessary, and for M7, only unit number." */
    bool positioningOnly =
        (regs->deviceOperation == WD_OP_SEEK || regs->deviceOperation == WD_OP_RETURN_TO_ZERO);

    if (!WdUnitAttached(self, disk) && !positioningOnly)
    {
        disk->diskUnitNotReady = true;
        data->statusRegister.bits.diskFault = 1;
        Wd_FinishOperation(self);
        return;
    }

    if (disk->diskType == WD_DISK_TYPE_UNKNOWN)
    {
        DiskWinchester_SetDiskType(disk, WD_DISK_MICROPOLIS_1325);
    }

    self->blockSizeBytes = disk->bytesPrSector;

    switch (regs->deviceOperation)
    {
    case WD_OP_SEEK:
        /* Sec 3.4.5: a RELATIVE step seek - "The heads will travel the number
         * of cylinders as specified in the word count register, and the
         * direction is specified in control word bit 14." */
        if (regs->seekDirection == WD_SEEK_IN)
        {
            disk->cylinder -= (int32_t)regs->wordCounter;
            if (disk->cylinder < 0)
            {
                disk->cylinder = 0;
            }
        }
        else
        {
            disk->cylinder += (int32_t)regs->wordCounter;
            if (disk->cylinder > disk->maxCylinders)
            {
                disk->cylinder = disk->maxCylinders;
            }
        }
        disk->onCylinder = true;
        if (Log_IsEnabled(LOG_CAT_WD, LOG_DEBUG))
        {
            Log_Write(LOG_CAT_WD, LOG_DEBUG, "%s unit=%d dir=%s count=%d -> cylinder %d\n",
                      Wd_OpName(regs->deviceOperation), regs->selectedUnit,
                      regs->seekDirection == WD_SEEK_IN ? "in" : "out", regs->wordCounter,
                      disk->cylinder);
        }
        Device_QueueIODelay(self, IODELAY_HDD, (IODelayedCallback)WdTransferEnd, disk->unit,
                            self->interruptLevel);
        break;

    case WD_OP_RETURN_TO_ZERO:
        disk->cylinder = 0;
        disk->onCylinder = true;
        Device_QueueIODelay(self, IODELAY_HDD, (IODelayedCallback)WdTransferEnd, disk->unit,
                            self->interruptLevel);
        break;

    case WD_OP_READ_TRANSFER:
    case WD_OP_WRITE_TRANSFER:
    case WD_OP_READ_PARITY:
    case WD_OP_COMPARE:
    case WD_OP_WRITE_FORMAT:
    {
        long lba = DiskWinchester_ChsToLba(disk, regs->cylinder, regs->head, regs->sector);
        if (lba < 0)
        {
            data->statusRegister.bits.addressMismatch = 1;
            Wd_FinishOperation(self);
            return;
        }

        /* Address bound check against the drive geometry. */
        if (regs->cylinder > disk->maxCylinders || regs->head >= disk->headsPrCylinder ||
            regs->sector >= disk->sectorsPrTrack)
        {
            data->statusRegister.bits.addressMismatch = 1;
            Wd_FinishOperation(self);
            return;
        }

        if (disk->diskIsWriteProtected && (regs->deviceOperation == WD_OP_WRITE_TRANSFER ||
                                           regs->deviceOperation == WD_OP_WRITE_FORMAT))
        {
            disk->diskUnitNotReady = true;
            data->statusRegister.bits.diskFault = 1;
            Wd_FinishOperation(self);
            return;
        }

        uint32_t wordCounter = regs->wordCounter;
        uint32_t coreAddress = ((uint32_t)regs->memoryAddressHiBits << 16) | regs->memoryAddress;
        uint32_t blockCounter = (wordCounter * 2) / (uint32_t)self->blockSizeBytes;
        uint32_t buffer_ptr = 0;

        if (Log_IsEnabled(LOG_CAT_WD, LOG_DEBUG))
        {
            Log_Write(LOG_CAT_WD, LOG_DEBUG, "GO %s unit=%d C/H/S=%d/%d/%d LBA=%ld WC=%u core=%o\n",
                      Wd_OpName(regs->deviceOperation), regs->selectedUnit, regs->cylinder,
                      regs->head, regs->sector, lba, wordCounter, coreAddress);
        }

        if (blockCounter == 0 || !self->blockCallbacks.readFunc || !self->blockCallbacks.writeFunc)
        {
            /* Nothing to move, or no backing store hooked up: complete
             * cleanly rather than transferring garbage. */
            Device_QueueIODelay(self, IODELAY_HDD, (IODelayedCallback)WdTransferEnd, disk->unit,
                                self->interruptLevel);
            break;
        }

        uint8_t *buffer = (uint8_t *)malloc(blockCounter * self->blockSizeBytes);
        if (!buffer)
        {
            data->statusRegister.bits.dmaChannelError = 1;
            Wd_FinishOperation(self);
            return;
        }

        if (regs->deviceOperation == WD_OP_WRITE_TRANSFER ||
            regs->deviceOperation == WD_OP_WRITE_FORMAT)
        {
            /* Pull the words out of ND memory, then commit whole blocks. */
            while (wordCounter > 0)
            {
                uint16_t w = Device_DMARead(coreAddress);
                Device_IO_BufferWriteWord(self, buffer, buffer_ptr++, w);
                coreAddress++;
                wordCounter--;
            }
            int written =
                self->blockCallbacks.writeFunc(self, buffer, blockCounter, lba, disk->unit);
            if (written < 0 || (uint32_t)written != blockCounter)
            {
                data->statusRegister.bits.diskFault = 1;
                free(buffer);
                Wd_FinishOperation(self);
                return;
            }
        }
        else
        {
            int blocksRead =
                self->blockCallbacks.readFunc(self, buffer, blockCounter, lba, disk->unit);
            if (blocksRead < 0 || (uint32_t)blocksRead != blockCounter)
            {
                data->statusRegister.bits.diskFault = 1;
                free(buffer);
                Wd_FinishOperation(self);
                return;
            }

            /* M2 (read parity) checks the CRC without moving data to memory,
             * and M3 (compare) compares rather than writes. Sec 3.4.7:
             * "No data transfer to the computer memory is performed." */
            if (regs->deviceOperation == WD_OP_READ_TRANSFER)
            {
                while (wordCounter > 0)
                {
                    uint32_t w = Device_IO_BufferReadWord(self, buffer, buffer_ptr++);
                    Device_DMAWrite(coreAddress, (uint16_t)w);
                    coreAddress++;
                    wordCounter--;
                }
            }
            else if (regs->deviceOperation == WD_OP_COMPARE)
            {
                while (wordCounter > 0)
                {
                    uint32_t w = Device_IO_BufferReadWord(self, buffer, buffer_ptr++);
                    uint16_t m = Device_DMARead(coreAddress);
                    if ((uint16_t)w != m)
                    {
                        data->statusRegister.bits.compareError = 1;
                        break;
                    }
                    coreAddress++;
                    wordCounter--;
                }
            }
        }

        free(buffer);

        /* Write back the advanced address and the residual count, as a real
         * DMA controller leaves them. */
        regs->memoryAddress = (uint16_t)(coreAddress & 0xFFFF);
        regs->memoryAddressHiBits = (uint8_t)((coreAddress >> 16) & 0xFF);
        regs->wordCounter = (uint16_t)wordCounter;

        Device_QueueIODelay(self, IODELAY_HDD, (IODelayedCallback)WdTransferEnd, disk->unit,
                            self->interruptLevel);
        break;
    }

    case WD_OP_LOAD_CTRL_BITS:
        /* M6 is never activated; handled at the call site. */
        Wd_FinishOperation(self);
        break;

    default:
        Wd_FinishOperation(self);
        break;
    }
}

static uint16_t Wd_Tick(Device *self)
{
    Device_TickIODelay(self);
    return self->interruptBits;
}

static uint16_t Wd_Ident(Device *self, uint16_t level)
{
    WinchesterData *data = (WinchesterData *)self->deviceData;
    if (!data)
    {
        return 0;
    }

    /* Answer only when this card actually has an interrupt pending on that
     * level - a card with no pending interrupt must stay silent so the IDENT
     * goes to whoever else is waiting. Testing interruptBits, not just the
     * level number, is what the paper-tape reader does
     * (device_paper_tape.c PaperTape_Ident). */
    if ((self->interruptBits & (1u << level)) == 0)
    {
        return 0;
    }

    /* Identing clears the interrupt and the enable, as on the other ND disc
     * controllers. */
    data->statusRegister.bits.interruptEnabled = 0;
    Device_SetInterruptStatus(self, false, self->interruptLevel);
    if (Log_IsEnabled(LOG_CAT_WD, LOG_DEBUG))
    {
        Log_Write(LOG_CAT_WD, LOG_DEBUG, "IDENT answered level=%u code=%o\n", level,
                  self->identCode);
    }
    return self->identCode;
}

static void Wd_Reset(Device *self)
{
    WinchesterData *data = (WinchesterData *)self->deviceData;
    if (!data)
    {
        return;
    }

    data->statusRegister.raw = 0;
    data->controlRegister.raw = 0;

    data->regs.memoryAddress = 0;
    data->regs.memoryAddressHiBits = 0;
    data->regs.wordCounter = 0;
    data->regs.blockAddress = 0;
    data->regs.sectorCounter = 0;
    data->regs.selectedUnit = 0;
    data->regs.head = 0;
    data->regs.cylinder = 0;
    data->regs.sector = 0;
    data->regs.testMode = false;
    data->regs.badTrack = false;
    data->regs.seekDirection = WD_SEEK_IN;
    data->regs.deviceOperation = WD_OP_READ_TRANSFER;
    data->regs.selectedDisk = data->regs.disks ? &data->regs.disks[0] : NULL;

    /* Master clear is one of the four flip-flop reset conditions (sec 3.2). */
    Wd_ClearFlipFlops(&data->regs);

    Device_SetInterruptStatus(self, false, self->interruptLevel);
}

/*
 * MASS STORAGE LOAD.
 *
 * ND-06.014.2A sec 4.2.5.2 / ND-06.015.02: "When loading from mass storage,
 * 1K words will be read from mass storage address 0 into main memory starting
 * in address 0. After a successful load, the CPU is started in main memory
 * address 0."
 *
 * 1K WORDS is 2048 bytes, i.e. two 1024-byte sectors - not the 2 KW the SMD
 * boot loads. The figure is the CPU's, not the controller's, so it is taken
 * from the ND-100/ND-120 reference rather than ND-11.015.01.
 */
static int Wd_Boot(Device *self, int unit)
{
    WinchesterData *data = (WinchesterData *)self->deviceData;
    WDControllerRegs *regs = &data->regs;

    if (!self->blockCallbacks.readFunc)
    {
        return -1;
    }

    if (unit < 0 || unit >= regs->maxUnits)
    {
        LOG(LOG_CAT_WD, LOG_ERROR, "Error: Winchester boot unit %d out of range (0-%d)\n", unit,
            regs->maxUnits - 1);
        return -1;
    }

    regs->selectedUnit = (uint8_t)unit;
    regs->selectedDisk = &regs->disks[unit];
    WDDiskInfo *disk = regs->selectedDisk;

    if (!WdUnitAttached(self, disk))
    {
        LOG(LOG_CAT_WD, LOG_ERROR, "Error: no image mounted on Winchester unit %d\n", unit);
        return -1;
    }

    if (disk->diskType == WD_DISK_TYPE_UNKNOWN)
    {
        DiskWinchester_SetDiskType(disk, WD_DISK_MICROPOLIS_1325);
    }
    self->blockSizeBytes = disk->bytesPrSector;

    const int wordCounter = 1024; /* 1K words */
    uint32_t blockCounter = (uint32_t)((wordCounter * 2) / self->blockSizeBytes);
    if (blockCounter == 0)
    {
        blockCounter = 1;
    }

    uint8_t *buffer = (uint8_t *)malloc(blockCounter * self->blockSizeBytes);
    if (!buffer)
    {
        return -1;
    }

    int blocksRead = self->blockCallbacks.readFunc(self, buffer, blockCounter, 0, disk->unit);
    if (blocksRead < 0 || blocksRead != (int)blockCounter)
    {
        LOG(LOG_CAT_WD, LOG_ERROR,
            "[Winchester Boot] block read failed: got %d blocks, expected %u\n", blocksRead,
            blockCounter);
        free(buffer);
        return -1;
    }

    /* An all-zero first block means a blank or unformatted pack; loading it
     * would start the CPU on zeros rather than fail visibly. */
    bool allZero = true;
    for (uint32_t i = 0; i < blockCounter * self->blockSizeBytes; i++)
    {
        if (buffer[i] != 0)
        {
            allZero = false;
            break;
        }
    }
    if (allZero)
    {
        LOG(LOG_CAT_WD, LOG_ERROR,
            "Error: Winchester boot sector is all zeros (blank or unformatted disk)\n");
        free(buffer);
        return -1;
    }

    for (int i = 0; i < wordCounter; i++)
    {
        uint32_t readData = Device_IO_BufferReadWord(self, buffer, i);
        Device_DMAWrite((uint32_t)i, (uint16_t)readData);
    }

    free(buffer);

    /* "the CPU is started in main memory address 0" */
    return 0;
}

static void Wd_Destroy(Device *self)
{
    if (!self)
    {
        return;
    }
    WinchesterData *data = (WinchesterData *)self->deviceData;
    if (data)
    {
        if (data->regs.disks)
        {
            free(data->regs.disks);
        }
        free(data);
        self->deviceData = NULL;
    }
}

Device *CreateWinchesterDevice(uint8_t thumbwheel)
{
    Device *dev = (Device *)malloc(sizeof(Device));
    if (!dev)
    {
        return NULL;
    }

    WinchesterData *data = (WinchesterData *)malloc(sizeof(WinchesterData));
    if (!data)
    {
        free(dev);
        return NULL;
    }
    memset(data, 0, sizeof(WinchesterData));

    /* Block class with a 1024-byte sector, like the SMD and floppy DMA
     * controllers, so the machine's mount plumbing can serve it images. */
    Device_Init(dev, thumbwheel, DEVICE_CLASS_BLOCK, 1024);
    dev->deviceData = data;
    dev->type = DEVICE_TYPE_DISC_WINCHESTER;

    data->controllerType = WD_CONTR_3041; /* 5 1/4 inch ST506 */

    data->regs.maxUnits = WD_MAX_UNITS;
    data->regs.disks = (WDDiskInfo *)malloc(sizeof(WDDiskInfo) * data->regs.maxUnits);
    if (!data->regs.disks)
    {
        free(data);
        free(dev);
        return NULL;
    }
    memset(data->regs.disks, 0, sizeof(WDDiskInfo) * data->regs.maxUnits);
    for (int i = 0; i < data->regs.maxUnits; i++)
    {
        data->regs.disks[i].unit = (uint8_t)i;
        DiskWinchester_SetDiskType(&data->regs.disks[i], WD_DISK_MICROPOLIS_1325);
    }

    dev->Read = Wd_Read;
    dev->Write = Wd_Write;
    dev->Tick = Wd_Tick;
    dev->Reset = Wd_Reset;
    dev->Ident = Wd_Ident;
    dev->Boot = Wd_Boot;
    dev->Destroy = Wd_Destroy;

    /* Address block and identity, ND-11.015.01 sec 3.1 / 4.1: disk system 1 at
     * 500-507 ident 1, disk system 2 at 510-517 ident 5, both on level 11. */
    switch (thumbwheel)
    {
    case 0:
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "WINCHESTER DISC 500");
        dev->startAddress = 0500;
        dev->identCode = WD_IDENT_SYSTEM1;
        break;
    case 1:
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "WINCHESTER DISC 510");
        dev->startAddress = 0510;
        dev->identCode = WD_IDENT_SYSTEM2;
        break;
    default:
        LOG(LOG_CAT_WD, LOG_WARN, "Winchester: unknown thumbwheel value: %d\n", thumbwheel);
        free(data->regs.disks);
        free(data);
        free(dev);
        return NULL;
    }
    dev->endAddress = dev->startAddress + 7;
    dev->interruptLevel = WD_INT_LEVEL;

    Wd_Reset(dev);


    LOG(LOG_CAT_WD, LOG_INFO, "Winchester disc device created: %s ident %o level %d (%d units)\n",
        dev->memoryName, dev->identCode, dev->interruptLevel, data->regs.maxUnits);
    return dev;
}
