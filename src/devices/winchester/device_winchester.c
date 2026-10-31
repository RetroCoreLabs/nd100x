/*
 * device_winchester.c - Winchester disc controller, cards 3041/3038 (ND-11.015.01).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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

#include "device_winchester.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../devices_types.h"
#include "../devices_protos.h"

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


static void wd_reset(Device *self);
static bool wd_transfer_end(Device *self, int drive);
static void wd_execute_go(Device *self);
static bool wd_unit_attached(Device *self, WDDiskInfo *disk);

static const char *wd_op_name(WDDeviceOperation op)
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
static void wd_clear_flip_flops(WDControllerRegs *regs)
{
    regs->memoryAddressWriteFF = false;
    regs->memoryAddressReadFF = false;
}

/*
 * Is a disk pack mounted on this unit? A unit with no image is a powered-off
 * drive and must report not ready. The machine is asked every time: its
 * disk-info callback reads the mounted-drive table (no file stat), and the
 * per-unit cache that used to sit here made a pack mounted after the first
 * look invisible for the rest of the session (see device_smd.c unit_attached
 * and tests/test_smd_units.c). The geometry is set from the image length
 * (SINTRAN disk layout table) whenever that length changes, so a pack swapped
 * for one of another size gets the right layout; an empty unit keeps the
 * default set at start.
 */
static bool wd_unit_attached(Device *self, WDDiskInfo *disk)
{
    if (!self || !disk)
    {
        return false;
    }
    size_t image_size = 0;
    bool is_write_protected = false;
    if (self->blockCallbacks.diskInfoFunc)
    {
        self->blockCallbacks.diskInfoFunc(self, &image_size, &is_write_protected, disk->unit);
    }
    disk->diskIsWriteProtected = is_write_protected;
    if (image_size > 0 && (size_t)disk->diskFileSize != image_size)
    {
        WDDiskType dt = wd_disk_preset_get(disk->unit);
        if (dt == WD_DISK_TYPE_UNKNOWN)
            dt = wd_disk_type_for_size(image_size);
        wd_disk_set_type(disk, dt);
    }
    disk->diskFileSize = image_size;
    return image_size > 0;
}

static void wd_clear_errors(WinchesterData *data)
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

static void wd_device_clear(Device *self)
{
    WinchesterData *data = (WinchesterData *)self->deviceData;

    data->statusRegister.bits.active = 0;
    wd_clear_errors(data);
    wd_clear_flip_flops(&data->regs);

    data->regs.memoryAddress = 0;
    data->regs.memoryAddressHiBits = 0;
    data->regs.wordCounter = 0;
    data->regs.sectorCounter = 0;
    /* The block address is NOT cleared. The SINTRAN driver's error routine
     * gives a device clear and then reads the block address register to find
     * the failing sector (WINCHESTER:NPL, IERR: "IOXIN LCON % DEVICE CLEAR"
     * ... "IOXIN RDAD % READ LAST DISK ADDRESS"); a register cleared by the
     * device clear would always give it sector 0 of cylinder 0. Master clear
     * (wd_reset) does clear it. */

    dev_set_interrupt_status(self, false, self->interruptLevel);
}

static uint16_t wd_read_status(Device *self)
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
    if (data->regs.selectedDisk && wd_unit_attached(self, data->regs.selectedDisk))
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

    /* Bit 12. Sec 3.5 calls it "0 (Not used)", but the logic diagrams say
     * otherwise, and a real 3041 reads it as 1 (the real-card trace in
     * tests/test_winchester_units.c).
     *
     * 3041 (ST506), sec 7.11: the status bus driver 11F (74LS244) takes TR00
     * on pin 13 and drives DB12 on pin 7; the bit-12 latch in 11E is not
     * wired. Sec 7.9: TR00 is the drive's Track 000 line from cable pin Ra5
     * through receiver 25A. The drive only answers on the cable when it is
     * selected, and the select lines are US0 = NOT CR9 AND TEST0 and
     * US1 = CR9 AND TEST0 (sec 7.9, 24A): control bit 9 picks the unit, and
     * test mode (control bit 3) selects no drive at all. So bit 12 is 1 when
     * the selected unit is out of test mode, attached, and has its heads on
     * cylinder 0.
     *
     * The heads reach cylinder 0 only when a seek towards it ends, but
     * nd100x sets disk->cylinder to the target when the seek starts, so the
     * arrival is taken as onCylinder coming back. Leaving cylinder 0 drops
     * TR00 at the first step, and that matches cylinder changing at once.
     *
     * 3038 (8 inch), sec 7.4: bit 12 there comes from an SB12 latch in 11E
     * whose set signal is on another sheet and has NOT been traced, so it
     * stays 0 for that card. */
    if (data->controllerType == WD_CONTR_3041 && !data->regs.testMode && data->regs.selectedDisk &&
        wd_unit_attached(self, data->regs.selectedDisk))
    {
        st.bits.track0 = (data->regs.selectedDisk->cylinder == 0 &&
                          data->regs.selectedDisk->onCylinder)
                             ? 1
                             : 0;
    }
    else
    {
        st.bits.track0 = 0;
    }

    /* Bit 15 always 0 - distinguishes this card from the 10 Mb controller. */
    st.bits.notUsed15 = 0;

    /* A status read is one of the four flip-flop reset conditions. */
    wd_clear_flip_flops(&data->regs);

    return st.raw;
}

static uint16_t wd_read(Device *self, uint32_t address)
{
    WinchesterData *data = (WinchesterData *)self->deviceData;
    if (!data)
    {
        return 0;
    }

    uint32_t reg = dev_register_address(self, address);
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
        value = wd_read_status(self);
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
        log_write(LOG_CAT_WD, LOG_DEBUG, "IOX READ  addr=%o reg=%o -> %o\n", address, reg, value);
    }

    return value;
}

static void wd_write(Device *self, uint32_t address, uint16_t value)
{
    WinchesterData *data = (WinchesterData *)self->deviceData;
    if (!data)
    {
        return;
    }

    uint32_t reg = dev_register_address(self, address);

    if (Log_IsEnabled(LOG_CAT_WD, LOG_DEBUG))
    {
        log_write(LOG_CAT_WD, LOG_DEBUG, "IOX WRITE addr=%o reg=%o value=%o\n", address, reg,
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
            wd_device_clear(self);
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
            wd_clear_flip_flops(&data->regs);

            data->statusRegister.bits.active = 1;
            data->statusRegister.bits.readyForTransfer = 0;
            wd_clear_errors(data);

            /* Sec 4.1 states BINT11 as a condition on the CURRENT status -
             * ready (bit 3) AND interrupt enabled (bit 0) - not as a pulse.
             * Activation drops bit 3, so the interrupt line drops with it and
             * comes back at completion. Without this an interrupt armed
             * before the operation stays asserted for the whole transfer.
             * Same shape as the paper-tape reader, which drops
             * readyForTransfer for the duration of the read and updates the
             * interrupt on both sides of it (device_paper_tape.c:130-149). */
            dev_set_interrupt_status(self, false, self->interruptLevel);

            wd_execute_go(self);
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
        dev_set_interrupt_status(self,
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
 * active and raises the interrupt if enabled. Status bit 2 is set from control
 * word bit 2 and only a completion clears it, so every activated operation -
 * the ones that end in error too - is ended here, queued by wd_end_later or by
 * the transfer itself, or the controller stays active for ever. Sec 3.5: bit 2
 * "Controller active", bit 3 "Controller finished with a device operation";
 * an operation that ended in error has still finished.
 */
static bool wd_transfer_end(Device *self, int drive)
{
    WinchesterData *data = (WinchesterData *)self->deviceData;
    if (!data)
    {
        return false;
    }

    /* Sec 3.4.5: a seek "is completed when the heads are settled over the
     * correct tracks", so the end of an operation is also the moment the arm
     * of that unit is on cylinder again. */
    if (data->regs.disks && drive >= 0 && drive < data->regs.maxUnits)
    {
        data->regs.disks[drive].onCylinder = true;
    }

    data->statusRegister.bits.active = 0;
    data->statusRegister.bits.readyForTransfer = 1;
    wd_clear_flip_flops(&data->regs);

    if (Log_IsEnabled(LOG_CAT_WD, LOG_DEBUG))
    {
        log_write(LOG_CAT_WD, LOG_DEBUG, "IO complete drive=%d intEnabled=%d\n", drive,
                  data->statusRegister.bits.interruptEnabled);
    }

    return data->statusRegister.bits.interruptEnabled ? true : false;
}

/*
 * End an activated operation the way the card does: LATER, through the queued
 * completion, never inside the IOX that activated it. The card goes active at
 * activation and only finds out about an error while it works (the address
 * compare of sec 4.4 phase 2 happens as the sectors pass under the head). The
 * SINTRAN driver reads status straight after activating and takes "not
 * active" as an error of its own (WINCHESTER:NPL, WDACT: "IF A NBIT ACTIV GO
 * CNACT % ERROR, NOT BUSY", "CONTROLLER NOT ACTIVE AFTER ACTIVATE"); an error
 * that ended the operation at once sent SINTRAN to ERRFATAL instead of into
 * its retry and recalibrate path. The error bits are set by the caller and
 * stay set; wd_transfer_end only ends the operation.
 */
static void wd_end_later(Device *self)
{
    WinchesterData *data = (WinchesterData *)self->deviceData;
    int unit = data->regs.selectedDisk ? data->regs.selectedDisk->unit : data->regs.selectedUnit;
    dev_queue_io_delay(self, IODELAY_HDD, (IODelayedCallback)wd_transfer_end, unit,
                       self->interruptLevel);
}

/*
 * DRIVE TIMING. An operation keeps the controller ACTIVE for as long as the
 * drive needs, counted in device ticks (one tick per executed instruction;
 * TICKS_20MS in device_rtc.c gives 10550 ticks per 20 ms, so 527.5 ticks are
 * one millisecond, the same clock the SMD model uses).
 *
 * The figures are the Micropolis 1320 series', which the maker gives once for
 * every member, the 1325 (DISC-74MB) included: track-to-track seek 6 ms,
 * average seek 28 ms "including settling time", maximum seek 62 ms, 3600 rpm
 * (average latency 8.33 ms, so one revolution is 16.7 ms).
 *
 * NOT in the data sheet, and so an assumption (BUGS.md W9): the shape of the seek curve
 * between those three points. It is drawn as two straight lines through
 * (1 cylinder, 6 ms), (one third of the stroke, 28 ms) and (full stroke,
 * 62 ms), as the SMD model does. Whether the other drive types the card
 * accepts (the CDC Finch of DISC-14MB/21MB) share the figures is not known;
 * they get the same ones.
 *
 * Why the timing has to be real (measured 2026-10-30, DISC-TEMA J02
 * PARITY-CHECK on DISC-74MB-1): with every operation ending after a flat 10
 * ticks, 10 of 8088 track reads failed with "Controller not active after
 * activate" - the program reads the status right after the activate, and when
 * the 20 ms clock interrupt came in between, the 10 ticks were already over.
 * The SMD showed the same thing (device_smd.c).
 */
extern uint64_t g_instr_counter; /* cpu.c: instructions executed, the device tick clock */

#define WD_TICKS_PER_MS_X10 5275u                        /* 527.5 ticks per ms */
#define WD_MS_TO_TICKS(ms_x10) (((ms_x10) * WD_TICKS_PER_MS_X10) / 100u)
#define WD_TICKS_PER_REV WD_MS_TO_TICKS(167u)           /* 3600 rpm: 16.7 ms */
#define WD_SEEK_ONE_TRACK_TICKS WD_MS_TO_TICKS(60u)     /* 6 ms */
#define WD_SEEK_AVERAGE_TICKS WD_MS_TO_TICKS(280u)      /* 28 ms */
#define WD_SEEK_FULL_STROKE_TICKS WD_MS_TO_TICKS(620u)  /* 62 ms */

/* Ticks a seek of `distance` cylinders takes, settling included. */
static uint32_t wd_seek_ticks(const WDDiskInfo *disk, int distance)
{
    if (distance <= 0)
    {
        return 0;
    }
    int full = disk->maxCylinders - 1;
    int third = full / 3;
    if (full < 3 || third < 2)
    {
        return WD_SEEK_ONE_TRACK_TICKS;
    }
    if (distance <= third)
    {
        return WD_SEEK_ONE_TRACK_TICKS +
               (uint32_t)(((uint64_t)(WD_SEEK_AVERAGE_TICKS - WD_SEEK_ONE_TRACK_TICKS) *
                           (uint64_t)(distance - 1)) /
                          (uint64_t)(third - 1));
    }
    if (distance > full)
    {
        distance = full;
    }
    return WD_SEEK_AVERAGE_TICKS +
           (uint32_t)(((uint64_t)(WD_SEEK_FULL_STROKE_TICKS - WD_SEEK_AVERAGE_TICKS) *
                       (uint64_t)(distance - third)) /
                      (uint64_t)(full - third));
}

/* Ticks a sector takes to pass under the head. */
static uint32_t wd_sector_ticks(const WDDiskInfo *disk)
{
    int spt = disk->sectorsPrTrack > 0 ? disk->sectorsPrTrack : 1;
    return WD_TICKS_PER_REV / (uint32_t)spt;
}

/* Ticks from now until the start of `sector` is under the head. The disk turns
 * all the time, so where it is follows from the instruction counter: sector 0
 * passes the head at every multiple of a revolution. */
static uint32_t wd_rotation_wait(const WDDiskInfo *disk, int sector)
{
    int spt = disk->sectorsPrTrack > 0 ? disk->sectorsPrTrack : 1;
    uint32_t rev = wd_sector_ticks(disk) * (uint32_t)spt;
    uint32_t phase = (uint32_t)(g_instr_counter % rev);
    uint32_t start = (uint32_t)(sector % spt) * wd_sector_ticks(disk);
    return (start + rev - phase) % rev;
}

/* How long a transfer of `sectors` sectors starting at `sector` keeps the
 * controller active: the wait for the first sector to come round and one
 * sector time per sector. The arm does not move; the driver seeks first
 * (sec 2.2). Never shorter than the old flat delay. */
static uint32_t wd_transfer_ticks(const WDDiskInfo *disk, int sector, uint32_t sectors)
{
    uint32_t ticks = wd_rotation_wait(disk, sector) + sectors * wd_sector_ticks(disk);
    return ticks < IODELAY_HDD ? IODELAY_HDD : ticks;
}

/* ---- address fields (see WDTrackFormat in disk_winchester.h) ------------- */

#define WD_FIELD_BAD_TRACK_FLAG (1u << 4) /* first word, bit 4 (sec 3.4.6) */

static WDTrackFormat *wd_find_track_format(WDDiskInfo *disk, int cylinder, int head)
{
    for (int i = 0; i < disk->formattedTrackCount; i++)
    {
        if (disk->formattedTracks[i].cylinder == cylinder && disk->formattedTracks[i].head == head)
        {
            return &disk->formattedTracks[i];
        }
    }
    return NULL;
}

/* The natural field of sector 'sector' on the track at cylinder/head. */
static void wd_natural_field(int cylinder, int head, int sector, uint16_t out[2])
{
    out[0] = (uint16_t)(head & 0xF);
    out[1] = (uint16_t)(((cylinder & 0x7FF) << 5) | (sector & 0x1F));
}

/* Write Format of the track under the arm on 'head': keep the fields when they
 * are not the natural ones, forget the track when they are. */
static void wd_store_track_format(WDDiskInfo *disk, int cylinder, int head, const uint16_t *words,
                                  int sectors)
{
    bool natural = true;
    for (int s = 0; s < sectors; s++)
    {
        uint16_t nat[2];
        wd_natural_field(cylinder, head, s, nat);
        if (words[2 * s] != nat[0] || words[2 * s + 1] != nat[1])
        {
            natural = false;
            break;
        }
    }
    WDTrackFormat *t = wd_find_track_format(disk, cylinder, head);
    if (natural)
    {
        if (t)
        {
            *t = disk->formattedTracks[disk->formattedTrackCount - 1];
            disk->formattedTrackCount--;
        }
        return;
    }
    if (!t)
    {
        if (disk->formattedTrackCount == disk->formattedTrackCapacity)
        {
            int cap = disk->formattedTrackCapacity ? disk->formattedTrackCapacity * 2 : 8;
            WDTrackFormat *n =
                (WDTrackFormat *)realloc(disk->formattedTracks, sizeof(WDTrackFormat) * (size_t)cap);
            if (!n)
            {
                return;
            }
            disk->formattedTracks = n;
            disk->formattedTrackCapacity = cap;
        }
        t = &disk->formattedTracks[disk->formattedTrackCount++];
        t->cylinder = cylinder;
        t->head = head;
    }
    for (int s = 0; s < WD_MAX_SECTORS_PER_TRACK; s++)
    {
        if (s < sectors)
        {
            t->field[s][0] = words[2 * s];
            t->field[s][1] = words[2 * s + 1];
        }
        else
        {
            /* A slot M5 did not reach keeps no address the compare can match. */
            t->field[s][0] = 0xFFFF;
            t->field[s][1] = 0xFFFF;
        }
    }
}

/*
 * Does the controller find the address of every sector of a transfer?
 *
 * Sec 4.4 phase 2: "read and compare address bit for bit". The heads read the
 * address fields of the track UNDER THE ARM, so a block address with another
 * cylinder finds nothing: sec 2.2, "The software driver must do a seek to the
 * correct cylinder prior to a read or write operation", sec 5.5, "To read or
 * write on other cylinders, a seek command must be carried out", and sec 10.3,
 * illegal cylinders give address mismatch.
 *
 * A field carrying the bad track flag is a mismatch too, unless control word
 * bit 15 is set (sec 2.3: "an error (status bit 8 = address mismatch) is given
 * due to the flag bit in the address field. During the next attempt(s),
 * accepting the flag bit, the first error free sector is read"; sec 3.4: "Bit
 * 15, the bad track bit, must be used to read a reallocated track").
 *
 * A transfer of several sectors goes on to the next sector and, at the end of
 * a track, to the next head (the same layout the image file has, see
 * wd_disk_chs_to_lba). The manual does not say whether the card does that
 * (BUGS.md W3); it cannot go on to the next cylinder without a seek, so a
 * transfer that runs off the arm's cylinder ends in address mismatch.
 */
static bool wd_address_fields_found(WDDiskInfo *disk, int cylinder, int head, int sector, int count,
                                    bool accept_bad_track_flag, int *found_count)
{
    *found_count = 0;
    for (int n = 0; n < count; n++, (*found_count)++)
    {
        if (cylinder != disk->cylinder)
        {
            return false;
        }
        WDTrackFormat *t = wd_find_track_format(disk, cylinder, head);
        if (t)
        {
            uint16_t want[2];
            wd_natural_field(cylinder, head, sector, want);
            bool found = false;
            for (int s = 0; s < disk->sectorsPrTrack && s < WD_MAX_SECTORS_PER_TRACK; s++)
            {
                uint16_t w0 = t->field[s][0];
                if ((uint16_t)(w0 & ~WD_FIELD_BAD_TRACK_FLAG) == want[0] && t->field[s][1] == want[1])
                {
                    if ((w0 & WD_FIELD_BAD_TRACK_FLAG) && !accept_bad_track_flag)
                    {
                        return false;
                    }
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                return false;
            }
        }
        if (++sector >= disk->sectorsPrTrack)
        {
            sector = 0;
            if (++head >= disk->headsPrCylinder)
            {
                head = 0;
                cylinder++;
            }
        }
    }
    return true;
}

/*
 * The test mode pattern generator. Sec 5.4, "DMA Transfer In Test Mode": with
 * control word 100254 "the disk drive need not be connected", the data comes
 * "from a test pattern generator on the controller", and memory then holds
 *     1000/052525  125252  052525 ... 1776/052525  1777/125252
 * i.e. 052525 first, then the two words in turn.
 */
static uint16_t wd_test_pattern_word(uint32_t i)
{
    return (i & 1u) ? 0125252 : 0052525;
}

/*
 * The block address register after an operation. INFERRED from the SINTRAN
 * driver, not stated in ND-11.015.01 (BUGS.md W7): after an address mismatch
 * the driver reads the register and takes "register - 1" as the failing
 * sector (WINCHESTER:NPL, IERR: "A-1=:ADMIA % SAVE FAILING BLOCK ADDRESS");
 * that is only right if the card has moved the register one sector past each
 * sector it dealt with, the failing one included. The sector field (bits 4-0)
 * is advanced; the cylinder field is left alone, as the head is in the
 * control word and the driver never runs a transfer past the end of a track.
 */
static void wd_advance_block_address(WDControllerRegs *regs, int sectors)
{
    uint16_t sec = (uint16_t)((regs->blockAddress + (uint16_t)sectors) & 0x1F);
    regs->blockAddress = (uint16_t)((regs->blockAddress & ~0x1Fu) | sec);
}

static void wd_execute_go(Device *self)
{
    WinchesterData *data = (WinchesterData *)self->deviceData;
    WDControllerRegs *regs = &data->regs;

    if (!regs->selectedDisk)
    {
        /* No such unit. Report a disk fault and terminate - never hang. */
        data->statusRegister.bits.diskFault = 1;
        wd_end_later(self);
        return;
    }

    WDDiskInfo *disk = regs->selectedDisk;

    /* Test mode: the pattern generator, no drive (sec 5.4). */
    if (regs->testMode && regs->deviceOperation <= WD_OP_COMPARE)
    {
        uint32_t core_address = ((uint32_t)regs->memoryAddressHiBits << 16) | regs->memoryAddress;
        uint32_t word_counter = regs->wordCounter;
        for (uint32_t i = 0; i < word_counter; i++)
        {
            uint16_t p = wd_test_pattern_word(i);
            if (regs->deviceOperation == WD_OP_READ_TRANSFER)
            {
                dev_dma_write(core_address, p);
            }
            else if (regs->deviceOperation != WD_OP_READ_PARITY)
            {
                int32_t m = dev_dma_read(core_address);
                if (m < 0)
                {
                    data->statusRegister.bits.dmaChannelError = 1;
                    break;
                }
                /* Sec 5.4: "change one word in the memory data area. Status
                 * should then change to 43030" - compare error, bit 10. The
                 * write path (control word 114254 in the manual is M3) is
                 * checked the same way. */
                if (regs->deviceOperation == WD_OP_COMPARE && (uint16_t)m != p)
                {
                    data->statusRegister.bits.compareError = 1;
                }
            }
            core_address++;
        }
        /* Sec 5.4: "The status register (in X) should be: 41030" - finished,
         * error OR, CRC error (bit 9) and on cylinder. The generator's words
         * carry no valid check word, so every test mode transfer ends with CRC
         * error. (41030 has bit 13 clear; on a 3041 bit 13 always reads 1.) */
        data->statusRegister.bits.crcError = 1;
        regs->memoryAddress = (uint16_t)(core_address & 0xFFFF);
        regs->memoryAddressHiBits = (uint8_t)((core_address >> 16) & 0xFF);
        regs->wordCounter = 0;
        dev_queue_io_delay(self, IODELAY_HDD, (IODelayedCallback)wd_transfer_end, disk->unit,
                           self->interruptLevel);
        return;
    }

    /* A unit with no pack is a drive that is switched off or not there: no
     * ready, no clocks, no seek complete. Every operation, positioning ones
     * too, ends with "Disk fault or missing clocks" (sec 3.5 bit 7). The manual
     * does not name the bit for a seek on an absent drive (BUGS.md W4). */
    if (!wd_unit_attached(self, disk))
    {
        disk->diskUnitNotReady = true;
        data->statusRegister.bits.diskFault = 1;
        wd_end_later(self);
        return;
    }

    if (disk->diskType == WD_DISK_TYPE_UNKNOWN)
    {
        wd_disk_set_type(disk, WD_DISK_MICROPOLIS_1325);
    }

    self->blockSizeBytes = disk->bytesPrSector;

    switch (regs->deviceOperation)
    {
    case WD_OP_SEEK:
    {
        /* Sec 3.4.5: a RELATIVE step seek - "The heads will travel the number
         * of cylinders as specified in the word count register, and the
         * direction is specified in control word bit 14." */
        int from_cylinder = disk->cylinder;
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
            /* maxCylinders is the number of cylinders (sec 2.1 "Cylinders
             * total"; the layout table's MAXCY+1), so the last one is
             * maxCylinders - 1. What a real drive does when stepped past either
             * end is not in the manual (BUGS.md W5); the arm stops there. */
            disk->cylinder += (int32_t)regs->wordCounter;
            if (disk->cylinder > disk->maxCylinders - 1)
            {
                disk->cylinder = disk->maxCylinders - 1;
            }
        }
        /* Status bit 14 "On cylinder" reads 0 while the heads travel; the
         * completion puts it back. */
        disk->onCylinder = false;
        if (Log_IsEnabled(LOG_CAT_WD, LOG_DEBUG))
        {
            log_write(LOG_CAT_WD, LOG_DEBUG, "%s unit=%d dir=%s count=%d -> cylinder %d\n",
                      wd_op_name(regs->deviceOperation), regs->selectedUnit,
                      regs->seekDirection == WD_SEEK_IN ? "in" : "out", regs->wordCounter,
                      disk->cylinder);
        }
        {
            int moved = disk->cylinder - from_cylinder;
            uint32_t ticks = wd_seek_ticks(disk, moved < 0 ? -moved : moved);
            dev_queue_io_delay(self, ticks < IODELAY_HDD ? IODELAY_HDD : ticks,
                               (IODelayedCallback)wd_transfer_end, disk->unit,
                               self->interruptLevel);
        }
        break;
    }

    case WD_OP_RETURN_TO_ZERO:
    {
        /* Sec 3.4.8: the disc seeks to cylinder 0; it takes the seek time
         * from where the arm is, and is not on cylinder until it has settled. */
        uint32_t ticks = wd_seek_ticks(disk, disk->cylinder);
        disk->cylinder = 0;
        disk->onCylinder = false;
        dev_queue_io_delay(self, ticks < IODELAY_HDD ? IODELAY_HDD : ticks,
                           (IODelayedCallback)wd_transfer_end, disk->unit, self->interruptLevel);
        break;
    }

    case WD_OP_READ_TRANSFER:
    case WD_OP_WRITE_TRANSFER:
    case WD_OP_READ_PARITY:
    case WD_OP_COMPARE:
    case WD_OP_WRITE_FORMAT:
    {
        long lba = wd_disk_chs_to_lba(disk, regs->cylinder, regs->head, regs->sector);
        if (lba < 0)
        {
            data->statusRegister.bits.addressMismatch = 1;
            wd_end_later(self);
            return;
        }

        /* Address bound check against the drive geometry. maxCylinders is a
         * count, so cylinder maxCylinders is already past the end (it used to
         * be let through and read the block after the last one). No sector
         * on the pack carries such an address, so the compare finds none:
         * address mismatch. */
        if (regs->cylinder >= disk->maxCylinders || regs->head >= disk->headsPrCylinder ||
            regs->sector >= disk->sectorsPrTrack)
        {
            data->statusRegister.bits.addressMismatch = 1;
            wd_end_later(self);
            return;
        }

        if (disk->diskIsWriteProtected && (regs->deviceOperation == WD_OP_WRITE_TRANSFER ||
                                           regs->deviceOperation == WD_OP_WRITE_FORMAT))
        {
            disk->diskUnitNotReady = true;
            data->statusRegister.bits.diskFault = 1;
            wd_end_later(self);
            return;
        }

        /* Write Format: the address fields of one track on the arm's
         * cylinder and the control word's head, two words per sector from
         * memory (sec 3.4.6). Phase 5 of a formatted sector is "write dummy
         * data"; what that data is, the manual does not say, so the image's
         * data fields are left as they were (BUGS.md W2). */
        if (regs->deviceOperation == WD_OP_WRITE_FORMAT)
        {
            uint32_t addr = ((uint32_t)regs->memoryAddressHiBits << 16) | regs->memoryAddress;
            int spt = disk->sectorsPrTrack;
            int sectors = (int)(regs->wordCounter / 2u);
            if (sectors > spt)
            {
                /* More fields than the track has sectors: not described
                 * (BUGS.md W2); the first track's worth is used. */
                sectors = spt;
            }
            uint16_t words[2 * WD_MAX_SECTORS_PER_TRACK];
            for (int w = 0; w < 2 * sectors; w++)
            {
                int32_t v = dev_dma_read(addr++);
                if (v < 0)
                {
                    data->statusRegister.bits.dmaChannelError = 1;
                    wd_end_later(self);
                    return;
                }
                words[w] = (uint16_t)v;
            }
            wd_store_track_format(disk, disk->cylinder, regs->head, words, sectors);
            regs->memoryAddress = (uint16_t)(addr & 0xFFFF);
            regs->memoryAddressHiBits = (uint8_t)((addr >> 16) & 0xFF);
            regs->wordCounter = (uint16_t)(regs->wordCounter - 2u * (uint32_t)sectors);
            if (Log_IsEnabled(LOG_CAT_WD, LOG_DEBUG))
            {
                log_write(LOG_CAT_WD, LOG_DEBUG, "GO %s unit=%d C/H=%d/%d sectors=%d\n",
                          wd_op_name(regs->deviceOperation), regs->selectedUnit, disk->cylinder,
                          regs->head, sectors);
            }
            /* A format writes the whole track from sector 0 round. */
            dev_queue_io_delay(self, wd_transfer_ticks(disk, 0, (uint32_t)disk->sectorsPrTrack),
                               (IODelayedCallback)wd_transfer_end, disk->unit,
                               self->interruptLevel);
            break;
        }

        /* Every sector's address field must be found on the arm's cylinder. */
        {
            uint32_t words_per_sector = (uint32_t)disk->bytesPrSector / 2u;
            int count = (int)((regs->wordCounter + words_per_sector - 1u) / words_per_sector);
            int found = 0;
            if (!wd_address_fields_found(disk, regs->cylinder, regs->head, regs->sector, count,
                                         regs->badTrack, &found))
            {
                wd_advance_block_address(regs, found + 1);
                if (Log_IsEnabled(LOG_CAT_WD, LOG_DEBUG))
                {
                    log_write(LOG_CAT_WD, LOG_DEBUG,
                              "GO %s unit=%d C/H/S=%d/%d/%d arm at %d: address not found -> ADDRESS MISMATCH\n",
                              wd_op_name(regs->deviceOperation), regs->selectedUnit, regs->cylinder,
                              regs->head, regs->sector, disk->cylinder);
                }
                data->statusRegister.bits.addressMismatch = 1;
                dev_queue_io_delay(self, wd_transfer_ticks(disk, regs->sector, (uint32_t)found + 1u),
                                   (IODelayedCallback)wd_transfer_end, disk->unit,
                                   self->interruptLevel);
                return;
            }
        }

        uint32_t word_counter = regs->wordCounter;
        uint32_t core_address = ((uint32_t)regs->memoryAddressHiBits << 16) | regs->memoryAddress;
        /* Whole sectors: a word count that ends inside a sector still reads the
         * sector it ends in. (It was a floor division, so 100 words moved
         * nothing and 600 words read 88 words past the end of the buffer.) */
        uint32_t words_per_sector = (uint32_t)self->blockSizeBytes / 2u;
        uint32_t block_counter = (word_counter + words_per_sector - 1u) / words_per_sector;
        uint32_t buffer_ptr = 0;

        if (Log_IsEnabled(LOG_CAT_WD, LOG_DEBUG))
        {
            log_write(LOG_CAT_WD, LOG_DEBUG, "GO %s unit=%d C/H/S=%d/%d/%d LBA=%ld WC=%u core=%o\n",
                      wd_op_name(regs->deviceOperation), regs->selectedUnit, regs->cylinder,
                      regs->head, regs->sector, lba, word_counter, core_address);
        }

        if (block_counter == 0 || !self->blockCallbacks.readFunc || !self->blockCallbacks.writeFunc)
        {
            /* Nothing to move, or no backing store hooked up: complete
             * cleanly rather than transferring garbage. */
            dev_queue_io_delay(self, IODELAY_HDD, (IODelayedCallback)wd_transfer_end, disk->unit,
                               self->interruptLevel);
            break;
        }

        /* Zeroed: what the card writes after the last word of a count that
         * ends inside a sector is not in the manual (BUGS.md W6); zero is used. */
        uint8_t *buffer = (uint8_t *)calloc(block_counter, (size_t)self->blockSizeBytes);
        if (!buffer)
        {
            data->statusRegister.bits.dmaChannelError = 1;
            wd_end_later(self);
            return;
        }

        if (regs->deviceOperation == WD_OP_WRITE_TRANSFER)
        {
            /* Pull the words out of ND memory, then commit whole blocks. */
            while (word_counter > 0)
            {
                /* A failed memory cycle is a DMA channel error (sec 3.5 bit 11).
                 * It used to be cast to a word and written to the disk. */
                int32_t w = dev_dma_read(core_address);
                if (w < 0)
                {
                    data->statusRegister.bits.dmaChannelError = 1;
                    free(buffer);
                    wd_end_later(self);
                    return;
                }
                dev_io_buffer_write_word(self, buffer, buffer_ptr++, (uint16_t)w);
                core_address++;
                word_counter--;
            }
            int written =
                self->blockCallbacks.writeFunc(self, buffer, block_counter, lba, disk->unit);
            if (written < 0 || (uint32_t)written != block_counter)
            {
                data->statusRegister.bits.diskFault = 1;
                free(buffer);
                wd_end_later(self);
                return;
            }
        }
        else
        {
            int blocks_read =
                self->blockCallbacks.readFunc(self, buffer, block_counter, lba, disk->unit);
            if (blocks_read < 0 || (uint32_t)blocks_read != block_counter)
            {
                data->statusRegister.bits.diskFault = 1;
                free(buffer);
                wd_end_later(self);
                return;
            }

            /* M2 (read parity) checks the CRC without moving data to memory,
             * and M3 (compare) compares rather than writes. Sec 3.4.7:
             * "No data transfer to the computer memory is performed." */
            if (regs->deviceOperation == WD_OP_READ_TRANSFER)
            {
                while (word_counter > 0)
                {
                    uint32_t w = dev_io_buffer_read_word(self, buffer, buffer_ptr++);
                    dev_dma_write(core_address, (uint16_t)w);
                    core_address++;
                    word_counter--;
                }
            }
            else if (regs->deviceOperation == WD_OP_COMPARE)
            {
                while (word_counter > 0)
                {
                    uint32_t w = dev_io_buffer_read_word(self, buffer, buffer_ptr++);
                    int32_t m = dev_dma_read(core_address);
                    if (m < 0)
                    {
                        data->statusRegister.bits.dmaChannelError = 1;
                        break;
                    }
                    if ((uint16_t)w != (uint16_t)m)
                    {
                        data->statusRegister.bits.compareError = 1;
                        break;
                    }
                    core_address++;
                    word_counter--;
                }
            }
            else if (regs->deviceOperation == WD_OP_READ_PARITY)
            {
                /* No memory cycles (sec 3.4.3), but the address and word
                 * counters still run through the sectors as the data passes the
                 * FIFO. Not stated in the manual (BUGS.md W8); DISC-TEMA J02
                 * PARITY-CHECK, the card's checkout (sec 5.6), reads the memory
                 * address back after every M2 and reports "Memory address
                 * Register not as expected" when it has not moved ("Core address
                 * register not as specified: a disk transfer did not finish",
                 * ND-830005). The SMD model counts the same way. */
                core_address += word_counter;
                word_counter = 0;
            }
        }

        free(buffer);

        /* Write back the advanced address and the residual count, as a real
         * DMA controller leaves them. */
        regs->memoryAddress = (uint16_t)(core_address & 0xFFFF);
        regs->memoryAddressHiBits = (uint8_t)((core_address >> 16) & 0xFF);
        regs->wordCounter = (uint16_t)word_counter;
        wd_advance_block_address(regs, (int)block_counter);

        dev_queue_io_delay(self, wd_transfer_ticks(disk, regs->sector, block_counter),
                           (IODelayedCallback)wd_transfer_end, disk->unit, self->interruptLevel);
        break;
    }

    case WD_OP_LOAD_CTRL_BITS:
        /* M6 is never activated; handled at the call site. */
        wd_end_later(self);
        break;

    default:
        wd_end_later(self);
        break;
    }
}

static uint16_t wd_tick(Device *self)
{
    dev_tick_io_delay(self);
    return self->interruptBits;
}

static uint16_t wd_ident(Device *self, uint16_t level)
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
     * (device_paper_tape.c paper_tape_ident). */
    if ((self->interruptBits & (1u << level)) == 0)
    {
        return 0;
    }

    /* Identing clears the interrupt and the enable, as on the other ND disc
     * controllers. */
    data->statusRegister.bits.interruptEnabled = 0;
    dev_set_interrupt_status(self, false, self->interruptLevel);
    if (Log_IsEnabled(LOG_CAT_WD, LOG_DEBUG))
    {
        log_write(LOG_CAT_WD, LOG_DEBUG, "IDENT answered level=%u code=%o\n", level,
                  self->identCode);
    }
    return self->identCode;
}

static void wd_reset(Device *self)
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

    /* Master clear returns every drive's heads to cylinder 0: sec 3.4.8,
     * "Return to zero by pushing the master clear button", and sec 5.5,
     * "Pushing Master Clear will position the heads to cylinder 0". With a
     * transfer now needing the arm on its cylinder, this is the position a
     * boot loader or a driver starts from. */
    for (int u = 0; data->regs.disks && u < data->regs.maxUnits; u++)
    {
        data->regs.disks[u].cylinder = 0;
        data->regs.disks[u].onCylinder = true;
    }

    /* Master clear is one of the four flip-flop reset conditions (sec 3.2). */
    wd_clear_flip_flops(&data->regs);

    dev_set_interrupt_status(self, false, self->interruptLevel);
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
static int wd_boot(Device *self, int unit)
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

    if (!wd_unit_attached(self, disk))
    {
        LOG(LOG_CAT_WD, LOG_ERROR, "Error: no image mounted on Winchester unit %d\n", unit);
        return -1;
    }

    if (disk->diskType == WD_DISK_TYPE_UNKNOWN)
    {
        wd_disk_set_type(disk, WD_DISK_MICROPOLIS_1325);
    }
    self->blockSizeBytes = disk->bytesPrSector;

    const int word_counter = 1024; /* 1K words */
    uint32_t block_counter = (uint32_t)((word_counter * 2) / self->blockSizeBytes);
    if (block_counter == 0)
    {
        block_counter = 1;
    }

    uint8_t *buffer = (uint8_t *)malloc(block_counter * self->blockSizeBytes);
    if (!buffer)
    {
        return -1;
    }

    int blocks_read = self->blockCallbacks.readFunc(self, buffer, block_counter, 0, disk->unit);
    if (blocks_read < 0 || blocks_read != (int)block_counter)
    {
        LOG(LOG_CAT_WD, LOG_ERROR,
            "[Winchester Boot] block read failed: got %d blocks, expected %u\n", blocks_read,
            block_counter);
        free(buffer);
        return -1;
    }

    /* An all-zero first block means a blank or unformatted pack; loading it
     * would start the CPU on zeros rather than fail visibly. */
    bool all_zero = true;
    for (uint32_t i = 0; i < block_counter * self->blockSizeBytes; i++)
    {
        if (buffer[i] != 0)
        {
            all_zero = false;
            break;
        }
    }
    if (all_zero)
    {
        LOG(LOG_CAT_WD, LOG_ERROR,
            "Error: Winchester boot sector is all zeros (blank or unformatted disk)\n");
        free(buffer);
        return -1;
    }

    for (int i = 0; i < word_counter; i++)
    {
        uint32_t read_data = dev_io_buffer_read_word(self, buffer, i);
        dev_dma_write((uint32_t)i, (uint16_t)read_data);
    }

    free(buffer);

    /* "the CPU is started in main memory address 0" */
    return 0;
}

static void wd_destroy(Device *self)
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
            for (int u = 0; u < data->regs.maxUnits; u++)
            {
                free(data->regs.disks[u].formattedTracks);
            }
            free(data->regs.disks);
        }
        free(data);
        self->deviceData = NULL;
    }
}

Device *wd_create_winchester_device(uint8_t thumbwheel)
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
    dev_init(dev, thumbwheel, DEVICE_CLASS_BLOCK, 1024);
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
        wd_disk_set_type(&data->regs.disks[i], WD_DISK_MICROPOLIS_1325);
    }

    dev->Read = wd_read;
    dev->Write = wd_write;
    dev->Tick = wd_tick;
    dev->Reset = wd_reset;
    dev->Ident = wd_ident;
    dev->Boot = wd_boot;
    dev->Destroy = wd_destroy;

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

    wd_reset(dev);


    LOG(LOG_CAT_WD, LOG_INFO, "Winchester disc device created: %s ident %o level %d (%d units)\n",
        dev->memoryName, dev->identCode, dev->interruptLevel, data->regs.maxUnits);
    return dev;
}
