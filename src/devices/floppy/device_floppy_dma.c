/*
 * device_floppy_dma.c - DMA floppy disc controller: commands, autoload, status words.
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


#include "device_floppy_dma.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../devices_types.h"
#include "../devices_protos.h"

/* File-local helpers (defined below) */
static void execute_floppy_go(Device *self);
static void execute_test(Device *self, int test_data);
static void execute_autoload(Device *self, int drive);

static bool auto_load_end(Device *self, int drive);
static bool read_end(Device *self, int drive);
static bool test_end(Device *self, int test);
static bool test_error_end(Device *self, int error);
static bool test_watchdog(Device *self, int unused);
static bool stage_boot_block(Device *self);
static int32_t damage_first(FloppyDMAData *data, uint8_t unit, uint32_t first, uint32_t count,
                            uint32_t bytes_pr_sector, bool double_sided, uint8_t *error);
static uint32_t sectors_per_track(uint32_t bytes_pr_sector);
static void damage_clear(FloppyDMAData *data, uint8_t unit, uint32_t first, uint32_t count,
                         uint32_t bytes_pr_sector, bool double_sided);
static void damage_clear_unit(FloppyDMAData *data, uint8_t unit);

/// <summary>
/// Floppy Disk Controller - 3112
/// 3112 is the 8 inch and 5.25 inch floppy controller + streamer controller card
///
/// https://www.ndwiki.org/wiki/3112
///
/// Alternative: https://www.ndwiki.org/wiki/3027 (only 8 inceh and no streamer)
///
/// Thumb Wheel
/// 0 = 1560
/// 1 = 1570
/// Implemented in according to the
/// * "ND-11.015.01 Floppy Disk Controller 3027".pdf
/// * "ND-11.012.01 FLOPPY DISK SYSTEM.PDF"
///
/// Source code of SINTRAN III J VSX
/// PDF - Print page 507 (actual PDF page 508)
/// </summary>
///

/*
 https://www.ndwiki.org/wiki/ND_floppy_disks

Format  Identification  Sector size         Sectors/t   Tracks/s    Sides/Density   Capacity pages
0b      IBM SYS-32-II   512 bytes/sector    8           77          SS/SD           154
17b     Non IBM         1024 bytes/sector   8           77          DS/DD           616

* The 8" Norsk Data format 0b was 512 bytes/sector, single sided/single density, 8 sectors per track, 77 tracks.
* The total capacity was 154 pages of 2048 bytes per page. Of this 148 pages could be allocated for files.
*
* The 5 1/4" Norsk Data format 17b was 1024 bytes/sector, double sided/double density, 8 sectors per track, 77 tracks per side.
* The total capacity was 616 pages. Of this 612 pages could be allocated for files.
*
* Note that "double density" is actually so-called "1.2MB high density" format in PC terminology, sometimes called "DS/HD" or "MF2-HD". The 5 1/4" media is typically labelled "DS,HD 96TPI".
*
* The floppy controller could use any of its supported formats on any media, 8" or 5 1/4", so it is also possible to have a format 17b 8" floppy disk.
* This became feasible when media with higher density became available.
*/

static void floppy_dma_reset(Device *self)
{
    FloppyDMAData *data = (FloppyDMAData *)self->deviceData;
    if (!data)
    {
        return;
    }

    data->status1.raw = 0;
    data->status2.raw = 0;
    data->controlWord.raw = 0;

    /* Firmware reset (ROM 34300G 003FH-0045H): DLO = 8, DHI = 0. The self-test writes DLO/DHI
     * only when it fails (01CAH/01D0H), so this is what IOX +0 reads until a test runs. */
    data->dataOut = 0x0008;
    data->sector = 0;
    data->track = 0;
    data->drive = 0;
    data->z80BootBlockStaged = false;
    data->z80StackPointer = 0;
    data->watchdogActive = false;
    data->watchdogChunksLeft = 0;

    data->command = 0;
    data->pointerHI = 0;
    data->pointerLO = 0;
    data->readFormat = 0;
}

// Bit 4 "OR of errors": set whenever a non-zero error code is present (firmware @06bc: SET 4 if
// code != 0), not only for hard/deleted/retry -- e.g. CRC or write-protect set errorCode alone.
static uint16_t calculate_or_of_errors(StatusRegister1 s)
{
    return (uint16_t)(s.bits.hardError | s.bits.deletedRecord | s.bits.retryOnController |
                      (s.bits.errorCode != 0));
}

// Hardware Status Word (IOX +2 / +4 read): flags + bit 15 dual-density (how SINTRAN detects the
// 3112 DMA card), but NO numeric error code. ND-11.021.1 section 3.7 / section 3.1 Note 1 (+2 == +4).
static uint16_t calculate_hardware_status_word(Device *self)
{
    FloppyDMAData *data = (FloppyDMAData *)self->deviceData;
    if (!data)
    {
        return 0;
    }

    StatusRegister1 s;
    s.raw = data->status1.raw;
    s.bits.inclusiveOrBits = calculate_or_of_errors(s);
    s.bits.errorCode = 0;   // no numeric code on the hardware status word
    s.bits.dualDensity = 1; // always 1 on the DMA controller
    // Bit 14 "streamer interface present" (ND-11.021.1 section 3.7): hardware, set on a
    // 3106/3112 floppy-and-streamer card. The TPE FLOPPY-STREA-C03 program uses it to tell
    // a 3112 from a 3027 (it reported "3027 Floppy DMA controller" while this bit was 0).
    s.raw |= (1u << 14);
    return s.raw;
}

// Status Word 1 (command block +6 memory writeback): flags + error code in bits 9-14, with
// bit 15 CLEAR (dual-density belongs only on the IOX hardware status word). ND-11.021.1 section 3.4.
static uint16_t calculate_status_word1(Device *self)
{
    FloppyDMAData *data = (FloppyDMAData *)self->deviceData;
    if (!data)
    {
        return 0;
    }

    StatusRegister1 s;
    s.raw = data->status1.raw;
    s.bits.inclusiveOrBits = calculate_or_of_errors(s);
    s.bits.dualDensity = 0; // bit 15 clear in the memory word
    return s.raw;
}

static uint16_t floppy_dma_read(Device *self, uint32_t address)
{
    FloppyDMAData *data = (FloppyDMAData *)self->deviceData;
    if (!data)
    {
        return 0;
    }

    uint32_t reg = dev_register_address(self, address);
    uint16_t value = 0;

    switch (reg)
    {
    case FLOPPY_DMA_READ_DATA:
        // Data-out register (DLO/DHI). Cleared at the start of every test, written by tests
        // 2, 3, 5, 6, 8, 11, 23 and 24 (ROM 34300G; see execute_test). Not status word 1
        // after tests 13/14/16/17/18, although ND-11.021.1 section 7.2 says so.
        value = data->dataOut;
        break;

    case FLOPPY_DMA_READ_STATUS1:
        value = calculate_hardware_status_word(self);
        break;

    case FLOPPY_DMA_READ_STATUS2:
        // section 3.1 Note 1: +4 returns the SAME hardware status word as +2 (duplicated for the
        // ND-100 Binary Format Load / Mass Storage Load microcode) -- NOT status word 2.
        value = calculate_hardware_status_word(self);
        break;
    default:
        break;
    }

    if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
    {
        log_write(LOG_CAT_FLOPPY, LOG_DEBUG, "FloppyDMA_Read: reg=%d, value=%o\n", reg, value);
    }

    return value;
}

static void floppy_dma_write(Device *self, uint32_t address, uint16_t value)
{
    FloppyDMAData *data = (FloppyDMAData *)self->deviceData;
    if (!data)
    {
        return;
    }

    uint32_t reg = dev_register_address(self, address);

    if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
    {
        log_write(LOG_CAT_FLOPPY, LOG_DEBUG, "FloppyDMA_Write: reg=%d, value=%o\n", reg, value);
    }

    switch (reg)
    {
    case FLOPPY_DMA_LOAD_CONTROL:
    {
        data->controlWord.raw = value;

        data->status1.bits.interruptEnabled = data->controlWord.bits.enableInterrupt;

        if (data->controlWord.bits.deviceClear || data->controlWord.bits.activateAutoload ||
            data->controlWord.bits.executeCommand)
        {
            // A clear restarts the Z80; a new command's ISR runs on top of a test 1 loop and
            // ends with its own NFINI. Either way the test 1 watchdog is off. (In the ROM the
            // new command's RET lands back in the test 1 loop, 1D15H, and the watchdog keeps
            // counting; that later error 026 is not modelled.)
            data->watchdogActive = false;
        }

        if (data->controlWord.bits.deviceClear)
        {
            // Bit 4 restarts the Z80 (the TPE ACTIVATE-SELFTEST command is this bit followed
            // by a wait for the self-test). The firmware reset (ROM 003BH-0049H) writes
            // DLO = 8, DHI = 0 and NSTAT = 0 before the self-test, and a passed self-test
            // writes nothing more (01CAH/01D0H are the failure path), so IOX +0 reads 0008H
            // and the error bits are gone. The Z80 RAM is not cleared by the reset.
            data->drive = -1;
            data->dataOut = 0x0008;
            data->status1.bits.errorCode = FLOPPY_ERR_OK;
            data->status1.bits.hardError = false;
            data->status1.bits.deletedRecord = false;
            data->status1.bits.retryOnController = false;
            data->status1.bits.deviceActive = false; // also ends a test 1 "stop controller"
            data->status1.bits.readyForTransfer = true;
            data->z80StackPointer = 0;
        }

        if (data->controlWord.bits.activateAutoload || data->controlWord.bits.executeCommand)
        {
            // Loading a control word with bit 2 or bit 8 activates the controller (ND-11.021.1
            // section 3.6): RFT drops and device active rises with the IOX, so the interrupt
            // enable in the same word cannot fire on the OLD RFT. Before this was here, that
            // early interrupt was taken and IDENTed by the program, and the completion
            // interrupt was then lost (TPE FLOPPY-STREA-C03: "Interrupt expected but not
            // detected after transfer").
            data->status1.bits.readyForTransfer = false;
            data->status1.bits.deviceActive = true;
        }

        dev_set_interrupt_status(
            self, data->status1.bits.interruptEnabled && data->status1.bits.readyForTransfer,
            self->interruptLevel);

        if (data->controlWord.bits.activateAutoload)
        {
            execute_autoload(self, data->drive);
        }
        else if (data->controlWord.bits.executeCommand)
        {
            if (data->controlWord.bits.testMode)
            {
                // Test number = bits 9-15 (ROM 1CDDH-1CDFH: port 54H >> 1), not only bits 9-13.
                execute_test(self, (data->controlWord.raw >> 9) & 0x7F);
            }
            else
            {
                if (data->controlWord.bits.enableStreamer)
                {
                    // TODO: Implement streamer
                }
                else
                {
                    execute_floppy_go(self);
                }
            }
        }
    }
    break;

    case FLOPPY_DMA_LOAD_POINTER_HI:
        data->pointerHI = value;
        break;

    case FLOPPY_DMA_LOAD_POINTER_LO:
        data->pointerLO = value;
        break;
    default:
        break;
    }
}

static uint16_t floppy_dma_tick(Device *self)
{
    if (!self)
    {
        return 0;
    }

    dev_tick_io_delay(self);
    return self->interruptBits;
}

static uint16_t floppy_dma_ident(Device *self, uint16_t level)
{
    if (!self)
    {
        return 0;
    }

    if ((self->interruptBits & (1 << level)) != 0)
    {
        FloppyDMAData *data = (FloppyDMAData *)self->deviceData;
        data->status1.bits.interruptEnabled = false;
        dev_set_interrupt_status(self, false, level);
        return self->identCode;
    }
    return 0;
}

/*
 * Autoload / boot error image (native ND-100 print routine + message text).
 *
 * When an autoload/boot fails, the 3112 firmware DMAs a small self-contained native ND-100
 * program into the ND-100 first page and runs it from word 0 to print the failure on the
 * console. These are the EXACT Z80 firmware ROM bytes and the EXACT construction the firmware
 * performs (verified in Ghidra @1f2f-1f6f):
 *   - always copy the 0x3A-byte LOAD-ERROR image (code + "  ** LOAD-ERROR:    00 **"),
 *   - if wrong-bootstrap (51 oct) overlay "** WRONG BOOTSTTRAP ! **" at byte offset 0x39,
 *   - else patch the two octal error digits at byte offset 0x32/0x33,
 *   - DMA the byte buffer to the host as big-endian 16-bit words, starting at word 0.
 * Captures: RetroGhidra/N100-FLOPPY-3112/ND Code/{Load_error.txt, wrong_bootstrap.txt}.
 */

/* 0x3A-byte LOAD-ERROR image (Z80 ROM @1a92). */
static const uint8_t load_error_image[] = {
    /* ND-100 code (13 words) */
    0x50, 0x0D, 0xF1, 0x27, 0xCC, 0x69, 0xF3, 0x00, 0xE8, 0xC6, 0xFA, 0x9D, 0xA8, 0xFE, 0xC4, 0x80,
    0xC4, 0x29, 0xD2, 0x00, 0xE8, 0xC5, 0xF7, 0x01, 0xA8, 0xF8,
    /* text: FF SI CR LF "  ** LOAD-ERROR:    00 **" CR LF ' */
    0x0C, 0x0F, 0x0D, 0x0A, 0x20, 0x20, 0x2A, 0x2A, 0x20, 0x4C, 0x4F, 0x41, 0x44, 0x2D, 0x45, 0x52,
    0x52, 0x4F, 0x52, 0x3A, 0x20, 0x20, 0x20, 0x20, 0x30, 0x30, 0x20, 0x2A, 0x2A, 0x0D, 0x0A, 0x27};

/* 0x1C-byte "** WRONG BOOTSTTRAP ! **" text (Z80 ROM @1acc; firmware's "BOOTSTTRAP" typo). */
static const uint8_t wrong_bootstrap_text[] = {
    0x20, 0x2A, 0x2A, 0x20, 0x57, 0x52, 0x4F, 0x4E, 0x47, 0x20, 0x42, 0x4F, 0x4F, 0x54,
    0x53, 0x54, 0x54, 0x52, 0x41, 0x50, 0x20, 0x21, 0x20, 0x2A, 0x2A, 0x0D, 0x0A, 0x27};

// clang-format off
#define WRONG_BOOTSTRAP_OVERLAY_OFFSET 0x39 /* @1f49: DE=2239 */
#define ERROR_DIGIT_LOW_OFFSET 0x33         /* @1f54: HL=2233 */
#define WRONG_BOOTSTRAP_CODE_OCTAL 0x29     /* 51 octal = WRONG_BOOTSTRAP (@1f3f: CP 0x29) */
// clang-format on

/*
 * Build and DMA the on-card boot error image into ND-100 memory (first page), byte-for-byte as
 * the 3112 firmware does, so the ND-100 prints the failure on the console when run from word 0.
 * Returns the ND-100 word entry point (0).
 */
static int dma_autoload_error_image(int error_code_octal6bit)
{
    int c = error_code_octal6bit & 0x3F;
    uint8_t buf[WRONG_BOOTSTRAP_OVERLAY_OFFSET + sizeof(wrong_bootstrap_text)];
    int len;

    memcpy(buf, load_error_image, sizeof(load_error_image));

    if (c == WRONG_BOOTSTRAP_CODE_OCTAL)
    {
        /* Wrong-bootstrap: overlay the WRONG text after the LOAD-ERROR text. */
        memcpy(buf + WRONG_BOOTSTRAP_OVERLAY_OFFSET, wrong_bootstrap_text,
               sizeof(wrong_bootstrap_text));
        len = WRONG_BOOTSTRAP_OVERLAY_OFFSET + (int)sizeof(wrong_bootstrap_text);
    }
    else
    {
        /* Everything else: patch the two octal error digits. */
        buf[ERROR_DIGIT_LOW_OFFSET - 1] = (uint8_t)('0' + ((c >> 3) & 7)); /* high octal digit */
        buf[ERROR_DIGIT_LOW_OFFSET] = (uint8_t)('0' + (c & 7));            /* low octal digit */
        len = (int)sizeof(load_error_image);
    }

    /* DMA the byte buffer to ND-100 memory as big-endian 16-bit words, starting at word 0. */
    for (int i = 0; i < len; i += 2)
    {
        uint16_t w = (uint16_t)((buf[i] << 8) | ((i + 1 < len) ? buf[i + 1] : 0));
        dev_dma_write((uint32_t)(i >> 1), w);
    }

    return 0;
}

// Load floppy monitor (FLO-LOAD, almost like BPUN from the first sector)
static void execute_autoload(Device *self, int drive)
{
    (void)drive;
    FloppyDMAData *data = (FloppyDMAData *)self->deviceData;
    if (!data)
    {
        return;
    }

    if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
    {
        log_write(LOG_CAT_FLOPPY, LOG_DEBUG, "FloppyDMA: Executing Autoload\n");
    }

    // The firmware reads the first 2048 bytes of the diskette into its buffer at 2200H before
    // it looks for the bootstrap (digest 04, steps 1-2). Keep that part of the autoload.
    data->z80BootBlockStaged = false;
    stage_boot_block(self);

    /*
     * This C model does not yet parse the BPUN bootstrap, so it cannot complete a real floppy
     * boot. When it cannot produce a bootstrap it does exactly what the 3112 firmware does on
     * failure: DMA the LOAD-ERROR image into the ND-100 first page (error 50 oct, "no bootstrap
     * found on diskette") so the console shows "** LOAD-ERROR: 50 **", and flag the failure in
     * the status word. TODO: implement the real BPUN autoload (read track 0, scan for '!',
     * parse header, DMA image) - see NDInsight FloppyDMA docs section 4.3; then only error on failure.
     */
    data->status1.bits.errorCode = FLOPPY_ERR_NO_BOOTSTRAP; /* oct 50 */
    data->status1.bits.hardError = true;
    dma_autoload_error_image(FLOPPY_ERR_NO_BOOTSTRAP);

    dev_queue_io_delay(self, IODELAY_FLOPPY, (IODelayedCallback)auto_load_end, 0,
                       self->interruptLevel);
}

/*
 * Z80 RAM model helpers (test mode).
 */

/*
 * The first 128 bytes of the Z80 firmware ROM 34300G (0000H-007FH: RST vectors and the
 * interrupt vector table at 0076H), served to test 5 so that a program reading the ROM
 * through the controller gets the real bytes. The TPE FLOPPY-STREA-C03 program reads 0060H
 * and 0061H to name the card and its micro program. The rest of the 8 KB ROM is not held
 * here; a test 5 read elsewhere in 0000H-1FFFH returns 0.
 */
static const uint8_t z80_rom_head[0x80] = {
    0xDB, 0x61, 0x3E, 0x01, 0xD3, 0x74, 0x18, 0x33, 0xD7, 0x21, 0x00, 0x00, 0x39, 0xC3, 0xF8, 0x1E,
    0xE3, 0xD5, 0xC5, 0xF5, 0xDD, 0xE5, 0xE5, 0xC9, 0xE1, 0xDD, 0xE1, 0xF1, 0xC1, 0xD1, 0xE3, 0xC9,
    0xF3, 0xD5, 0x1E, 0x01, 0x16, 0x03, 0x18, 0x38, 0xF3, 0xD5, 0x1E, 0xC5, 0x16, 0x01, 0x18, 0x30,
    0xE5, 0x21, 0x10, 0x00, 0xE3, 0xED, 0x4D, 0x9B, 0xC3, 0xAE, 0x1E, 0x3E, 0x02, 0xD3, 0x70, 0x3E,
    0x08, 0xD3, 0x54, 0xAF, 0x67, 0xD3, 0x55, 0xD3, 0x56, 0x18, 0x3B, 0xF5, 0x78, 0x06, 0x00, 0xCD,
    0x57, 0x00, 0x3D, 0x20, 0xFA, 0xF1, 0xC9, 0xED, 0x4F, 0xED, 0x4F, 0x00, 0x00, 0x10, 0xF8, 0xC9,
    0xED, 0x59, 0xED, 0x51, 0xD1, 0xC9, 0xF5, 0xE5, 0x21, 0xAA, 0x20, 0x35, 0xE1, 0x28, 0x03, 0xF1,
    0xED, 0x45, 0xF1, 0xC3, 0xB0, 0x1E, 0x0C, 0x03, 0xBD, 0x18, 0x0F, 0x0D, 0x88, 0x05, 0x4A, 0x0C,
};

// Byte at a Z80 address for test 5: ROM head, RAM window, else 0.
static uint8_t z80_read_byte(const FloppyDMAData *data, uint32_t z80_address);

// Z80 address -> index into z80Ram, or -1 when outside the modelled window.
static int z80_ram_index(uint32_t z80_address)
{
    if (z80_address < FLOPPY_DMA_Z80_RAM_BASE ||
        z80_address >= FLOPPY_DMA_Z80_RAM_BASE + FLOPPY_DMA_Z80_RAM_SIZE)
    {
        return -1;
    }
    return (int)(z80_address - FLOPPY_DMA_Z80_RAM_BASE);
}

static uint8_t z80_read_byte(const FloppyDMAData *data, uint32_t z80_address)
{
    if (z80_address < sizeof(z80_rom_head))
    {
        return z80_rom_head[z80_address];
    }
    int idx = z80_ram_index(z80_address);
    return (idx >= 0) ? data->z80Ram[idx] : 0;
}

// 24-bit pointer POH:POM:POL (IOX +5 = bits 16-23, IOX +7 = bits 0-15).
static uint32_t pointer_24bit(const FloppyDMAData *data)
{
    return ((data->pointerHI & 0xFF) << 16) | (data->pointerLO & 0xFFFF);
}

/*
 * Fill the Z80 staging buffer at 2200H with the first 2048 bytes of the diskette in unit 0,
 * which is what the firmware's autoload does before it scans for the '!' bootstrap signature
 * (digest 04, steps 1-2). nd100x's --boot=floppy bypasses the autoload and loads the bootstrap
 * from the image file directly, so the buffer is filled here, on the first test that needs it
 * or on an autoload. Unit 0 is the unit nd100x boots (nd100x.ini); the real autoload unit is
 * not modelled. Returns true when the buffer holds the diskette data.
 */
static bool stage_boot_block(Device *self)
{
    FloppyDMAData *data = (FloppyDMAData *)self->deviceData;
    if (!data)
    {
        return false;
    }
    if (data->z80BootBlockStaged)
    {
        return true;
    }
    if (!self->blockCallbacks.readFunc)
    {
        return false;
    }

    int idx = z80_ram_index(FLOPPY_DMA_Z80_STAGING_BUFFER);
    if (idx < 0 || idx + FLOPPY_DMA_BOOT_BLOCK_BYTES > FLOPPY_DMA_Z80_RAM_SIZE)
    {
        return false;
    }

    self->blockSizeBytes = FLOPPY_DMA_BOOT_BLOCK_BYTES; // one block = the whole boot read
    int blocks_read = self->blockCallbacks.readFunc(self, &data->z80Ram[idx], 1, 0, 0);
    if (blocks_read != 1)
    {
        LOG(LOG_CAT_FLOPPY, LOG_WARN,
            "FloppyDMA: test mode: could not read the first %d bytes of floppy unit 0 (rc=%d)\n",
            FLOPPY_DMA_BOOT_BLOCK_BYTES, blocks_read);
        return false;
    }

    data->z80BootBlockStaged = true;
    if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
    {
        log_write(LOG_CAT_FLOPPY, LOG_DEBUG,
                  "FloppyDMA: staged the first %d bytes of unit 0 at Z80 %04XH\n",
                  FLOPPY_DMA_BOOT_BLOCK_BYTES, FLOPPY_DMA_Z80_STAGING_BUFFER);
    }
    return true;
}

/*
 * Copy byte_count bytes between Z80 RAM and ND-100 memory, the way the card's AM9517 channel 2
 * and byte/word packer do it for tests 9, 10, 13 and 14 (firmware 0474H/047AH). ND-100 words
 * are big-endian on that path: the byte at the even Z80 address is the high byte of the word
 * (firmware 043CH-0449H byte-swaps its own stores before a Z80 -> ND-100 DMA, and the command
 * parser takes the command code from the odd byte 2081H). The ND-100 side moves
 * floor(byte_count/2) words (DMA-end check at 05B5H: SRL D / RR E); what the hardware does with
 * the last byte of an odd count is not in the ROM, so an odd last byte is not moved here.
 * Returns false when the Z80 range is outside the modelled window.
 */
static bool copy_z80_nd100(Device *self, uint32_t nd_address, uint32_t z80_address,
                           uint32_t byte_count, bool to_nd100)
{
    FloppyDMAData *data = (FloppyDMAData *)self->deviceData;
    int idx = z80_ram_index(z80_address);
    if (idx < 0 || byte_count > FLOPPY_DMA_Z80_RAM_SIZE ||
        (uint32_t)idx + byte_count > FLOPPY_DMA_Z80_RAM_SIZE)
    {
        LOG(LOG_CAT_FLOPPY, LOG_WARN,
            "FloppyDMA: test mode: Z80 range %04X+%u is outside the modelled RAM window\n",
            z80_address, byte_count);
        return false;
    }

    uint32_t words = byte_count / 2;
    for (uint32_t w = 0; w < words; w++)
    {
        uint32_t i = idx + 2 * w;
        if (to_nd100)
        {
            dev_dma_write(nd_address + w, (uint16_t)((data->z80Ram[i] << 8) | data->z80Ram[i + 1]));
        }
        else
        {
            uint16_t word = dev_dma_read(nd_address + w);
            data->z80Ram[i] = (uint8_t)(word >> 8);
            data->z80Ram[i + 1] = (uint8_t)(word & 0xFF);
        }
    }
    return true;
}

/*
 * Diskette damage marks (tests 16, 17, 18). Nothing is written to the image file; the marks
 * are checked by the read command and cleared by the write and format commands.
 */

// Drive number from a FDVSEL byte: bits 0-3 are the one-hot drive selects (firmware 0B2CH-0B30H
// builds 1 << drive, 1193H-119DH turns it back into a number). No bit set: drive 0.
static uint8_t fdvsel_to_unit(uint8_t fdvsel)
{
    for (uint8_t unit = 0; unit < 4; unit++)
    {
        if (fdvsel & (1u << unit))
        {
            return unit;
        }
    }
    return 0;
}

static void damage_mark(FloppyDMAData *data, uint8_t unit, uint8_t track, uint8_t sector,
                        uint8_t error)
{
    for (int i = 0; i < FLOPPY_DMA_MAX_DAMAGED; i++)
    {
        if (!data->damaged[i].used)
        {
            data->damaged[i].used = true;
            data->damaged[i].unit = unit;
            data->damaged[i].track = track;
            data->damaged[i].sector = sector;
            data->damaged[i].error = error;
            return;
        }
    }
    LOG(LOG_CAT_FLOPPY, LOG_WARN, "FloppyDMA: damage list full, unit %u track %u sector %u not marked\n",
        unit, track, sector);
}

/*
 * Logical sector -> track, side, sector the way the firmware does it (0B4BH-0B72H): the
 * logical number is divided by the sectors per track; with a double-sided format the quotient
 * is halved and the bit shifted out is the side. So logical = (track * sides + side) * spt +
 * (sector - 1). spt is 8 for the ND 512- and 1024-byte formats (315392 and 1261568 byte
 * images); 26 for the 128- and 256-byte IBM formats is taken from the IBM 3740 layout, not
 * from the ROM (inferred).
 */
static uint32_t sectors_per_track(uint32_t bytes_pr_sector)
{
    return (bytes_pr_sector >= 512) ? 8 : 26;
}

static void logical_to_physical(uint32_t logical, uint32_t bytes_pr_sector, bool double_sided,
                                uint8_t *track, uint8_t *side, uint8_t *sector)
{
    uint32_t spt = sectors_per_track(bytes_pr_sector);
    uint32_t q = logical / spt;
    *sector = (uint8_t)(logical % spt + 1);
    *side = double_sided ? (uint8_t)(q & 1) : 0;
    *track = (uint8_t)(double_sided ? (q >> 1) : q);
}

// First logical sector in [first, first + count) of this unit that carries a damage mark,
// or -1; *error gets the mark's error code. Marks are recorded for side 0 only (the side a
// test 16/17/18 write lands on is not modelled).
static int32_t damage_first(FloppyDMAData *data, uint8_t unit, uint32_t first, uint32_t count,
                            uint32_t bytes_pr_sector, bool double_sided, uint8_t *error)
{
    for (uint32_t logical = first; logical < first + count; logical++)
    {
        uint8_t track, side, sector;
        logical_to_physical(logical, bytes_pr_sector, double_sided, &track, &side, &sector);
        if (side != 0)
        {
            continue;
        }
        for (int i = 0; i < FLOPPY_DMA_MAX_DAMAGED; i++)
        {
            FloppyDamagedMark *m = &data->damaged[i];
            if (m->used && m->unit == unit && m->track == track &&
                (m->sector == 0 || m->sector == sector))
            {
                if (error)
                {
                    *error = m->error;
                }
                return (int32_t)logical;
            }
        }
    }
    return -1;
}

// Clear the marks of the sectors in [first, first + count) of this unit (a write rewrites the
// CRC). A whole-track mark is cleared when any sector of the track is written (simplification).
static void damage_clear(FloppyDMAData *data, uint8_t unit, uint32_t first, uint32_t count,
                         uint32_t bytes_pr_sector, bool double_sided)
{
    for (uint32_t logical = first; logical < first + count; logical++)
    {
        uint8_t track, side, sector;
        logical_to_physical(logical, bytes_pr_sector, double_sided, &track, &side, &sector);
        if (side != 0)
        {
            continue;
        }
        for (int i = 0; i < FLOPPY_DMA_MAX_DAMAGED; i++)
        {
            FloppyDamagedMark *m = &data->damaged[i];
            if (m->used && m->unit == unit && m->track == track &&
                (m->sector == 0 || m->sector == sector))
            {
                m->used = false;
            }
        }
    }
}

static void damage_clear_unit(FloppyDMAData *data, uint8_t unit)
{
    for (int i = 0; i < FLOPPY_DMA_MAX_DAMAGED; i++)
    {
        if (data->damaged[i].unit == unit)
        {
            data->damaged[i].used = false;
        }
    }
}

/*
 * Test mode, modelled on the Z80 firmware ROM 34300G (test dispatcher at 1CDCH, jump table at
 * 1CFBH, handlers 1D15H-1E0CH). The manual (ND-11.021.1 chapter 7) is used only where the ROM
 * does the same thing; where they differ the ROM wins and the case says so.
 *
 * Entry (1CDCH-1CFAH): the test number is control word bits 9-15 ((CW >> 9) & 7FH, port 54H
 * shifted right once), DLO/DHI are cleared for EVERY test (1CE3H-1CE6H), then C = POL, B = POM,
 * A = POH and the handler is jumped to. There is no range check in the ROM: numbers 26-127
 * index past the table into code bytes; the model logs them and finishes.
 *
 * Exit: every handler except test 1 (loops forever) and test 15 (jumps away) ends at 1D94H,
 * OUT (57H),0 = NFINI: RFT set, no error bits. The firmware never looks at the interrupt enable
 * bit; the interrupt on RFT with control word bit 1 set is card hardware (test_end()).
 *
 * The data-out register (IOX +0) is written by tests 2, 3, 5, 6, 8, 11, 23 and 24 only (the
 * complete list of OUT (54H)/(55H) in the ROM is at 0041H, 0045H, 01CAH, 01D0H, 071CH, 0721H,
 * 1BF9H, 1BFBH, 1BFFH, 1CE4H, 1CE6H, 1D2BH, 1D2EH, 1D3DH, 1D40H). Tests 13, 14, 16, 17 and 18
 * do NOT write status word 1 there, although the manual (section 7.2) says they do.
 */
static void execute_test(Device *self, int test)
{
    FloppyDMAData *data = (FloppyDMAData *)self->deviceData;
    if (!data)
    {
        return;
    }

    if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
    {
        log_write(LOG_CAT_FLOPPY, LOG_DEBUG,
                  "FloppyDMA: Executing test %d (POH=%02X POL/POM=%04X)\n", test,
                  data->pointerHI & 0xFF, data->pointerLO & 0xFFFF);
    }

    // Busy while the test runs; test_end() clears it after the usual floppy delay.
    data->status1.bits.deviceActive = true;
    data->status1.bits.readyForTransfer = false;
    dev_set_interrupt_status(self, false, self->interruptLevel);

    // 030EH-030FH: the host-command ISR clears the error code (2194H) for every control word.
    data->status1.bits.errorCode = FLOPPY_ERR_OK;
    // 1CE3H-1CE6H: DHI = DLO = 0 before any test runs.
    data->dataOut = 0;

    uint32_t pointer = pointer_24bit(data);
    uint16_t pol_pom = (uint16_t)(pointer & 0xFFFF); // BC in the firmware
    uint8_t poh = (uint8_t)((pointer >> 16) & 0xFF);  // A in the firmware
    uint8_t pol = (uint8_t)(pol_pom & 0xFF);
    uint8_t pom = (uint8_t)(pol_pom >> 8);
    bool finishes = true;
    int idx;

    switch (test)
    {
    case 0: // 1D94H: NFINI only ("do nothing, set RFT")
        break;

    case 1: // 1D15H: JR to itself with interrupts on; no NFINI, so RFT never comes from the
            // handler. The NMI watchdog (0066H-0075H) ends it: (20AAH) counts down to 0 and
            // the stub 1EB0H raises error 026 (hard class -> re-init, status word 1 to DLO/DHI
            // and NFINI with the OR-of-errors bit, 1FA7H -> 06B3H). Until then the card is
            // busy; a new control word or a device clear cancels the wait (see the write
            // handler). TPE PROVOKE-ERRORS code 26 relies on this.
        finishes = false;
        data->watchdogActive = true;
        data->watchdogChunksLeft = FLOPPY_DMA_WATCHDOG_CHUNKS;
        dev_queue_io_delay(self, FLOPPY_DMA_WATCHDOG_CHUNK_TICKS, (IODelayedCallback)test_watchdog,
                           0, self->interruptLevel);
        break;

    case 2: // 1D2AH: DHI = POM, DLO = POL
        data->dataOut = pol_pom;
        break;

    case 3: // 1D2EH: DLO = POH (DHI stays 0)
        data->dataOut = poh;
        break;

    case 4: // 1D32H: Z80 memory[POM:POL] = POH
        idx = z80_ram_index(pol_pom);
        if (idx >= 0)
        {
            data->z80Ram[idx] = poh;
        }
        break;

    case 5: // 1D35H: DLO = Z80 memory[POM:POL] (ROM head, RAM window, else 0)
        stage_boot_block(self);
        data->dataOut = z80_read_byte(data, pol_pom);
        break;

    case 6: // 1D38H: DLO/DHI = (20AFH), set by the RAM probe at 02DCH-02E8H: it walks up from
            // 2000H writing 55H and stops at the first byte that does not hold it, so the
            // value is the first address ABOVE the RAM. ROM: data-out register, not POL/POM
            // as the manual says (those are input ports). The real card's RAM size is not
            // known here; the modelled window gives 6000H.
        data->dataOut = FLOPPY_DMA_Z80_RAM_BASE + FLOPPY_DMA_Z80_RAM_SIZE;
        break;

    case 7: // 1D44H: OUT (POL),POM. The Z80 I/O ports (CTC, AM9517, FD1797, display, ...) are
            // not modelled, so the write has no effect here.
        break;

    case 8: // 1D48H: DLO = IN (POL). No Z80 ports in the model: reads as 0.
        data->dataOut = 0;
        break;

    case 9:  // 1D4CH: Z80 -> ND-100, 128 bytes (047AH)
    case 10: // 1D54H: ND-100 -> Z80, 128 bytes (0474H)
    {
        // Block address from 1E78H-1E83H: 2000H + (POH >> 1) * 100H + (POH even ? 80H : 0).
        // That is the manual's 2000H + 80H * (POH - 1) for odd POH only; for even POH the ROM
        // address is 100H higher. ND-100 word address = POM:POL, bits 16-23 forced 0 (1E84H).
        // The DMA flag is not checked afterwards (1D52H/1D5AH): a failure is silent.
        stage_boot_block(self);
        uint32_t z80_address = FLOPPY_DMA_Z80_RAM_BASE + ((uint32_t)(poh >> 1) << 8) +
                               ((poh & 1) ? 0 : 0x80);
        copy_z80_nd100(self, pol_pom, z80_address, FLOPPY_DMA_TEST_BLOCK_BYTES, test == 9);
        break;
    }

    case 11: // 1D5CH: compare the 128 bytes at POM:POL with the 128 bytes at POM:POL + 80H.
    {
        // The firmware feeds the second block to the compare hardware by AM9517 channel 3 and
        // LDIRs the first block to F000H; on a mismatch the LDIR is interrupted and
        // DHI:DLO = BC = the LDIR count left (1D79H -> 1D2AH), 0 when all 128 bytes matched.
        // LDIR decrements BC after each byte, so the earliest the interrupt can be taken is
        // after the mismatching byte itself has gone through: count left = 127 - index of the
        // first mismatch (inferred from the LDIR semantics; the ROM's abort path is an open
        // point, see the firmware report). A mismatch only in the last byte therefore reads 0.
        stage_boot_block(self);
        idx = z80_ram_index(pol_pom);
        data->dataOut = 0;
        if (idx >= 0 && idx + 2 * FLOPPY_DMA_TEST_BLOCK_BYTES <= FLOPPY_DMA_Z80_RAM_SIZE)
        {
            for (int i = 0; i < FLOPPY_DMA_TEST_BLOCK_BYTES; i++)
            {
                if (data->z80Ram[idx + i] != data->z80Ram[idx + FLOPPY_DMA_TEST_BLOCK_BYTES + i])
                {
                    data->dataOut = (uint16_t)(FLOPPY_DMA_TEST_BLOCK_BYTES - 1 - i);
                    break;
                }
            }
        }
        break;
    }

    case 12: // 1D7BH: display counts 0-9. No display in this model.
        break;

    case 13: // 1D9AH: ND-100 -> Z80 (0474H) with the parameter field from 1E1AH
    case 14: // 1DA2H: Z80 -> ND-100 (047AH), same field
    {
        // 1E1AH: 3 ND-100 words are DMAed from the full 24-bit pointer POH:POM:POL (a word
        // address) to Z80 209EH-20A3H: +0 ND-100 address (bits 16-23 forced 0 at 1E2BH),
        // +1 Z80 address, +2 byte count; each word high byte first. The data then goes
        // directly between that Z80 address and ND-100 memory; the 2200H buffer is not
        // involved unless the field points into it. A count of 0 is 65536 bytes (DEC DE wraps
        // at 07DBH). A DMA failure is recorded in Z80 RAM only (error 040/042, class 0) and
        // the test still ends with NFINI = 0: silent for the ND-100.
        stage_boot_block(self);
        uint16_t nd_address = dev_dma_read(pointer);
        uint16_t z80_address = dev_dma_read(pointer + 1);
        uint32_t byte_count = dev_dma_read(pointer + 2);
        if (byte_count == 0)
        {
            byte_count = 0x10000;
        }
        if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
        {
            log_write(LOG_CAT_FLOPPY, LOG_DEBUG,
                      "FloppyDMA: test %d: ND-100 %06o <-> Z80 %04XH, %u bytes\n", test,
                      nd_address, z80_address, byte_count);
        }
        copy_z80_nd100(self, nd_address, z80_address, byte_count, test == 14);
        break; // DLO/DHI stay 0 (ROM), the manual's "status word 1 to data-out" is not done
    }

    case 15: // 1DAFH: PUSH BC / RET = jump to Z80 address POM:POL. No Z80 CPU in the model.
    {
        // One case is known from the ROM: an FFH opcode is RST 38H, and 0038H jumps to the
        // stub 1EAEH = error 025 "microprogram out of range" (hard class: re-init, status
        // word 1 to DLO/DHI, NFINI with the OR-of-errors bit). Memory that does not exist
        // reads FFH on the Z80 bus (manual section 3.10), so a jump outside the modelled RAM
        // and ROM head is treated the same. TPE PROVOKE-ERRORS code 25 writes FFH at 2A00H
        // with test 4 and then starts it with test 15. Any other target cannot be run here.
        bool outside = pol_pom >= sizeof(z80_rom_head) && z80_ram_index(pol_pom) < 0;
        if (outside || z80_read_byte(data, pol_pom) == 0xFF)
        {
            finishes = false;
            dev_queue_io_delay(self, IODELAY_FLOPPY, (IODelayedCallback)test_error_end,
                               FLOPPY_ERR_MICROPROGRAM_OUT_OF_RANGE, self->interruptLevel);
        }
        else
        {
            LOG(LOG_CAT_FLOPPY, LOG_WARN,
                "FloppyDMA: test 15 (start Z80 program at %04XH) cannot be modelled; finishing\n",
                pol_pom);
        }
        break;
    }

    case 16: // 1DB1H: select drive POH (1E4EH), seek track POM, sector register = POL, write
             // sector (ACH) and clear the FD1797 in the middle of it -> CRC error on that sector.
        damage_mark(data, fdvsel_to_unit(poh), pom, pol, FLOPPY_ERR_CRC);
        LOG(LOG_CAT_FLOPPY, LOG_INFO,
            "FloppyDMA: test 16: unit %u track %u sector %u marked as CRC error (image not written)\n",
            fdvsel_to_unit(poh), pom, pol);
        break;

    case 17: // 1DC0H: write track (F0H) on track POM, run to completion -> whole track
             // destroyed; a later read or check reports 07 track not found.
        damage_mark(data, fdvsel_to_unit(poh), pom, 0, FLOPPY_ERR_TRACK_NOT_FOUND);
        LOG(LOG_CAT_FLOPPY, LOG_INFO,
            "FloppyDMA: test 17: unit %u track %u marked as destroyed (image not written)\n",
            fdvsel_to_unit(poh), pom);
        break;

    case 18: // 1DCDH: write track (F0H) on track POM, delay (004BH, B = 14H), then clear the
             // FD1797 -> the write stops after about one sector. Which physical sector that is
             // depends on the index hole and the delay; the first sector of the track is marked
             // (inferred). A later read or check reports 06 sector not found.
        damage_mark(data, fdvsel_to_unit(poh), pom, 1, FLOPPY_ERR_SECTOR_NOT_FOUND);
        LOG(LOG_CAT_FLOPPY, LOG_INFO,
            "FloppyDMA: test 18: unit %u track %u sector 1 marked as destroyed (image not written)\n",
            fdvsel_to_unit(poh), pom);
        break;

    case 19: // 1DDEH: POH times OUT (74H),0FH / delay / OUT (74H),0 (tap-tap on the drive
             // selects). Nothing to observe from the ND-100.
    case 20: // 1DF6H: RST 20H with C = 17H, stops CTC channel 7 (display timer).
    case 21: // 1DFBH: I = 0, OUT (10H),76H: interrupt vector base to the PROM table at 0076H.
        break;

    case 22: // 1E07H: SP = POM:POL, then 1D94H pops the saved registers from the new stack.
        data->z80StackPointer = pol_pom;
        break;

    case 23: // 1D24H: HL = SP; DHI = high byte, DLO = low byte of SP. ROM: data-out register,
             // the manual does not say where it goes.
        data->dataOut = data->z80StackPointer;
        break;

    case 24: // 1D17H: OUT (73H) = POH (FD1797 data register), OUT (70H) = POL (command), wait
             // for the chip (1075H), then DHI = track register (IN 71H), DLO = status.
    {
        // There is no FD1797 in the model. The type I commands that change the track register
        // are applied to the drive position the device keeps (data->track); the status byte is
        // built from that (type I layout: bit 2 track 0, bit 6 write protect, bit 7 not ready;
        // inferred from the WD1797 data sheet, not from the ROM). Other commands: status 0.
        uint8_t fdc_status = 0;
        if ((pol & 0x80) == 0)
        {
            switch (pol >> 4)
            {
            case 0x0: // restore
                data->track = 0;
                break;
            case 0x1: // seek: track register = data register
                data->track = poh;
                break;
            case 0x4: // step in, bit 4 = update track register
            case 0x5:
                if ((pol & 0x10) && data->track < 0xFF)
                {
                    data->track++;
                }
                break;
            case 0x6: // step out
            case 0x7:
                if ((pol & 0x10) && data->track > 0)
                {
                    data->track--;
                }
                break;
            default: // step (0x2X/0x3X): direction not kept, no change
                break;
            }
            if (data->track == 0)
            {
                fdc_status |= 0x04;
            }
            if (self->blockCallbacks.diskInfoFunc)
            {
                size_t image_size = 0;
                bool write_protected = false;
                // Drive 0 when no command has selected one (the self-test selects and
                // restores drive 0, ND-11.021.1 section 6.1).
                int unit = (data->drive < 4) ? data->drive : 0;
                self->blockCallbacks.diskInfoFunc(self, &image_size, &write_protected, unit);
                if (write_protected)
                {
                    fdc_status |= 0x40;
                }
                if (image_size == 0)
                {
                    fdc_status |= 0x80;
                }
            }
        }
        data->dataOut = (uint16_t)((data->track << 8) | fdc_status);
        break;
    }

    case 25: // 1E0CH: (20AEH) = FFH, disables the idle RAM self-test. Not modelled.
        break;

    default: // No range check in the ROM (1CF0H-1CFAH): the jump goes into code bytes.
        LOG(LOG_CAT_FLOPPY, LOG_WARN, "FloppyDMA: test %d is outside the ROM's table 0-25\n",
            test);
        break;
    }

    if (finishes)
    {
        dev_queue_io_delay(self, IODELAY_FLOPPY, (IODelayedCallback)test_end, test,
                           self->interruptLevel);
    }
}

// A test ended in a hard-class error (RST 08H with class 80H -> 1EF8H -> 1FA7H: re-init and
// the completion routine 06B3H): status word 1 with the error code goes to DLO/DHI, NFINI
// sets RFT with the OR-of-errors bit. Used for error 025 (test 15 into FFH) and 026 (test 1
// watchdog).
static bool test_error_end(Device *self, int error)
{
    FloppyDMAData *data = (FloppyDMAData *)self->deviceData;
    if (!data)
    {
        return false;
    }
    data->status1.bits.errorCode = (uint16_t)error;
    data->status1.bits.deviceActive = false;
    data->status1.bits.readyForTransfer = true;
    data->dataOut = calculate_status_word1(self);
    dev_set_interrupt_status(
        self, data->status1.bits.interruptEnabled && data->status1.bits.readyForTransfer,
        self->interruptLevel);
    return false;
}

// One chunk of the test 1 watchdog wait has passed.
static bool test_watchdog(Device *self, int unused)
{
    (void)unused;
    FloppyDMAData *data = (FloppyDMAData *)self->deviceData;
    if (!data || !data->watchdogActive)
    {
        return false; // cancelled by a later control word or a device clear
    }
    if (--data->watchdogChunksLeft > 0)
    {
        dev_queue_io_delay(self, FLOPPY_DMA_WATCHDOG_CHUNK_TICKS, (IODelayedCallback)test_watchdog,
                           0, self->interruptLevel);
        return false;
    }
    data->watchdogActive = false;
    LOG(LOG_CAT_FLOPPY, LOG_INFO, "FloppyDMA: test 1 ended by the watchdog: error 026 timeout\n");
    return test_error_end(self, FLOPPY_ERR_TIMEOUT);
}

// A test has finished (1D94H NFINI): controller idle, ready for transfer, interrupt if
// control word bit 1 is set. Nothing is written to the data-out register here (see
// execute_test: the ROM does not write status word 1 for tests 13, 14, 16, 17, 18).
static bool test_end(Device *self, int test)
{
    (void)test;
    FloppyDMAData *data = (FloppyDMAData *)self->deviceData;
    if (!data)
    {
        return false;
    }

    data->status1.bits.deviceActive = false;
    data->status1.bits.readyForTransfer = true;
    dev_set_interrupt_status(
        self, data->status1.bits.interruptEnabled && data->status1.bits.readyForTransfer,
        self->interruptLevel);
    return false;
}

static void execute_floppy_go(Device *self)
{
    if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
    {
        // Diagnostic: log first few floppy commands
        {
            static int floppy_go_log = 0;
            if (floppy_go_log < 5)
            {
                floppy_go_log++;
                log_write(LOG_CAT_FLOPPY, LOG_DEBUG,
                          "[FLOPPY-DIAG] ExecuteFloppyGo called (readFunc=%s writeFunc=%s)\n",
                          self && self->blockCallbacks.readFunc ? "ok" : "NULL",
                          self && self->blockCallbacks.writeFunc ? "ok" : "NULL");
            }
        }
    }

    if (!self)
    {
        return;
    }

    if ((!self->blockCallbacks.readFunc) || (!self->blockCallbacks.writeFunc))
    {
        return;
    }

    FloppyDMAData *data = (FloppyDMAData *)self->deviceData;
    if (!data)
    {
        return;
    }

    // Set Default floppy values
    uint32_t bytes_pr_sector = 512;

    // Find ND100 memory address
    data->commandBlockAddress = data->pointerLO | (data->pointerHI << 16);

    // Read command block
    data->commandBlock.fields.commandWord = dev_dma_read(data->commandBlockAddress);
    data->commandBlock.fields.diskAddress = dev_dma_read(data->commandBlockAddress + 1);
    data->commandBlock.fields.memoryAddressHi =
        dev_dma_read(data->commandBlockAddress + 2) & 0xFF; // Bits 23-16
    data->commandBlock.fields.memoryAddressLo =
        dev_dma_read(data->commandBlockAddress + 3); // Bits 15-0

    uint32_t mem_address = data->commandBlock.fields.memoryAddressLo |
                           (data->commandBlock.fields.memoryAddressHi
                            << 16); // Calculate memory address for read/write IO

    data->commandBlock.fields.optionsWordCountHi =
        dev_dma_read(data->commandBlockAddress +
                     4); // Bit 15 = 1 => WordCount, 0=SectorCount. Bit 7-0 WordCountHi
    bool is_wc = (data->commandBlock.fields.optionsWordCountHi & (1 << 15)) != 0;

    data->commandBlock.fields.wordSectorCount =
        dev_dma_read(data->commandBlockAddress + 5); // Word count / Sector count

    data->commandBlock.fields.status1 = dev_dma_read(data->commandBlockAddress + 6);
    uint32_t word_count = data->commandBlock.fields.wordSectorCount |
                          (data->commandBlock.fields.optionsWordCountHi & 0xFF) << 16;

    data->command = (FloppyFunction)(data->commandBlock.fields.commandWord & 0b111111);
    data->drive = (data->commandBlock.fields.commandWord >> 6) & 0b11;
    uint32_t floppy_format = (data->commandBlock.fields.commandWord >> 8) & 0b11;
    // Format bit 2 = double sided (ND-11.021.1 format table; firmware 0B65H BIT 2 when it
    // splits a logical sector into track/side). Used only for the test 16-18 damage marks.
    bool double_sided = ((data->commandBlock.fields.commandWord >> 8) & 0b0100) != 0;

    // Get information on file size and readonly
    if (self->blockCallbacks.diskInfoFunc)
    {
        bool is_write_protected = false;
        size_t image_size = 0;

        self->blockCallbacks.diskInfoFunc(self, &image_size, &is_write_protected, data->drive);
        data->diskFileSize = image_size;
        data->readOnly = is_write_protected;
    }

    // Calculate sector size based on format
    switch (floppy_format)
    {
    case 0:
        bytes_pr_sector = 512; // 8" disk
        break;
    case 1:
        bytes_pr_sector = 256;
        break;
    case 2:
        bytes_pr_sector = 123;
        break;
    case 3:
        bytes_pr_sector = 1024; // 5.25" 1.2MB disk
        break;
    default:
        break;
    }

    // Reflect current sector size on the device so block callbacks know bytes per block
    self->blockSizeBytes = bytes_pr_sector;


    uint32_t position = data->commandBlock.fields.diskAddress * bytes_pr_sector;
    uint32_t words_to_read = word_count;
    if (!is_wc)
    {
        words_to_read *= (bytes_pr_sector >> 1);
    }

    data->status1.bits.errorCode = FLOPPY_ERR_OK;
    data->status1.bits.readyForTransfer = false;
    dev_set_interrupt_status(
        self, data->status1.bits.interruptEnabled && data->status1.bits.readyForTransfer,
        self->interruptLevel);

    data->status1.bits.readyForTransfer = false;
    dev_set_interrupt_status(
        self, data->status1.bits.interruptEnabled && data->status1.bits.readyForTransfer,
        self->interruptLevel);

    if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
    {
        log_write(LOG_CAT_FLOPPY, LOG_DEBUG, "Command block received from memory at 0x%08X\n",
                  data->commandBlockAddress);
        log_write(LOG_CAT_FLOPPY, LOG_DEBUG, "------------------------------------------------\n");
        for (int i = 0; i < 12; i++)
        {
            log_write(LOG_CAT_FLOPPY, LOG_DEBUG, "Command block %d: 0x%4X\n", i,
                      data->commandBlock.raw[i]);
        }
        log_write(LOG_CAT_FLOPPY, LOG_DEBUG, "------------------------------------------------\n");
    }

    data->commandBlock.fields.status1 = 0;
    data->commandBlock.fields.status2 =
        (data->drive << 8); // Selected unit is reported back in bits 8-9
    int words_transfered = 0;

    // Number of blocks to transfer where each block is blockSizeBytes bytes (typically 512/1024)
    uint32_t block_counter = (words_to_read * 2) / self->blockSizeBytes;
    uint32_t buffer_ptr = 0;

    uint8_t *buffer = NULL;
    int blocks_read = -1;
    uint8_t damage_error = FLOPPY_ERR_OK; // code of a test 16-18 mark hit by this command

    // Start track check (ROM 0B4BH-0B85H): the logical sector is split into track/side and
    // a track >= 77 (>= 153 with control word bit 12, 96 TPI) is error 030. Applied here to
    // the read, write and check commands.
    bool track_out_of_range = false;
    {
        uint8_t track, side, sector;
        logical_to_physical(data->commandBlock.fields.diskAddress, bytes_pr_sector, double_sided,
                            &track, &side, &sector);
        uint32_t track_limit = (data->controlWord.raw & (1u << 12)) ? 153 : 77;
        track_out_of_range = track >= track_limit;
    }

    // Allocate memory buffer if we need to read or write blocks of data
    if (block_counter > 0)
    {
        buffer = (uint8_t *)malloc(block_counter * self->blockSizeBytes);
        if (!buffer)
        {
            data->status1.bits.errorCode = RAM_ERROR;
            data->status1.bits.deviceActive = false;
            data->status1.bits.readyForTransfer = true;
            dev_set_interrupt_status(
                self, data->status1.bits.interruptEnabled && data->status1.bits.readyForTransfer,
                self->interruptLevel);
        }
    }

    if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_DEBUG))
    {
        log_write(LOG_CAT_FLOPPY, LOG_DEBUG, "FloppyDMA: Command: %d\n", data->command);
    }

    switch (data->command)
    {
    case FLOPPY_FUNC_READ_DATA:
        // Data is read from the floppy disk to the ND-100 memory.The start address is given as the logical sector address,
        // and a choice can be made between the word count and the number of sectors to indicate the length of the transfer.
        // The parameters for the transfer are given in the command field in the ND - 100 memory.
        //  The transfer will always start at the beginning of a sector, but the number of words to be read may be preset to any number of words.

        if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_TRACE))
        {
            log_write(LOG_CAT_FLOPPY, LOG_TRACE,
                      "Starting ReadData on drive position %d, wordsToRead: %d\r\n", position,
                      words_to_read);
        }

        if (buffer && track_out_of_range)
        {
            data->status1.bits.errorCode = FLOPPY_ERR_TRACK_OUT_OF_RANGE;
        }
        else if (buffer && damage_first(data, data->drive, data->commandBlock.fields.diskAddress,
                                        block_counter, bytes_pr_sector, double_sided,
                                        &damage_error) >= 0)
        {
            // A sector damaged by test 16, 17 or 18: the read fails with the mark's code
            // (05, 06 or 07). The real controller stops at the failing sector; the model
            // transfers nothing.
            data->status1.bits.errorCode = damage_error;
        }
        else if (buffer)
        {
            // Read all blocks from floppy  disk file into buffer
            blocks_read = self->blockCallbacks.readFunc(
                self, buffer, block_counter, data->commandBlock.fields.diskAddress, data->drive);
            if ((blocks_read < 0) || ((uint32_t)blocks_read != block_counter))
            {
                data->status1.bits.errorCode = DRIVE_NOT_READY;
                data->status1.bits.deviceActive = false;
                data->status1.bits.readyForTransfer = true;
            }

            while (words_to_read > 0)
            {
                // Read word from disk buffer (or 0 if no blocks was read to buffer)
                uint32_t read_data = 0;
                if (blocks_read > 0)
                {
                    read_data = dev_io_buffer_read_word(self, buffer, buffer_ptr++);
                }

                // DMA Write to RAM memory
                dev_dma_write(mem_address, (uint16_t)read_data);

                mem_address++;
                words_to_read--;
                words_transfered++;
            }
        }
        dev_queue_io_delay(self, IODELAY_FLOPPY, (IODelayedCallback)read_end, data->drive,
                           self->interruptLevel);
        break;

    case FLOPPY_FUNC_WRITE_DATA:
        // Same procedure as READ DATA, except that transfer is now from the ND - 100 to the diskette.
        if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_TRACE))
        {
            log_write(LOG_CAT_FLOPPY, LOG_TRACE, "Starting WriteData on drive position %d\r\n",
                      position);
        }

        if (track_out_of_range)
        {
            data->status1.bits.errorCode = FLOPPY_ERR_TRACK_OUT_OF_RANGE;
        }
        else if (data->readOnly)
        {
            // Disk is write protected
            data->status1.bits.errorCode = WRITE_PROTECTED;
            data->status1.bits.deviceActive = false;
            data->status1.bits.readyForTransfer = true;
            dev_set_interrupt_status(
                self, data->status1.bits.interruptEnabled && data->status1.bits.readyForTransfer,
                self->interruptLevel);
        }
        else
        {
            if (buffer)
            {
                while (words_to_read > 0)
                {
                    uint16_t word = dev_dma_read(mem_address);

                    // Write word to disk buffer
                    if (dev_io_buffer_write_word(self, buffer, buffer_ptr++, (uint16_t)word) < 0)
                    {
                        data->status1.bits.errorCode = DRIVE_NOT_READY;
                        data->status1.bits.deviceActive = false;
                        data->status1.bits.readyForTransfer = true;
                        dev_set_interrupt_status(self,
                                                 data->status1.bits.interruptEnabled &&
                                                     data->status1.bits.readyForTransfer,
                                                 self->interruptLevel);
                        break;
                    }

                    mem_address++;
                    words_to_read--;
                    words_transfered++;
                }

                // Write all blocks to floppy disk file from buffer
                int blocks_write = self->blockCallbacks.writeFunc(
                    self, buffer, block_counter, data->commandBlock.fields.diskAddress,
                    data->drive);
                if ((blocks_write >= 0) && ((uint32_t)blocks_write == block_counter))
                {
                    // Rewritten sectors get a fresh CRC: test 16-18 marks are gone.
                    damage_clear(data, data->drive, data->commandBlock.fields.diskAddress,
                                 block_counter, bytes_pr_sector, double_sided);
                }
                if ((blocks_write < 0) || ((uint32_t)blocks_write != block_counter))
                {
                    data->status1.bits.errorCode = DRIVE_NOT_READY;
                    data->status1.bits.deviceActive = false;
                    data->status1.bits.readyForTransfer = true;
                    dev_set_interrupt_status(self,
                                             data->status1.bits.interruptEnabled &&
                                                 data->status1.bits.readyForTransfer,
                                             self->interruptLevel);
                }
            }
        }
        dev_queue_io_delay(self, IODELAY_FLOPPY, (IODelayedCallback)read_end, data->drive,
                           self->interruptLevel);
        break;

    case FLOPPY_FUNC_READ_EXTENDED_STATUS:
    {
        // Firmware 03F8H-0462H builds this block at Z80 2200H and DMAs it like a read:
        //  +0 board number (0C28H = 3112 when port 77H bit 7 is set, else 0C22H = 3106)
        //  +1 sub version : micro program version = 0006H (version 6 = "G" in 34300G)
        //  +2 buffer start 2200H, +3 (20B1H) = RAM size found by the probe (top - 2000H)
        //  +4..+11 Z80 IX, AF, BC, DE, HL, SP, PC, IY copied from the save area 2184H
        //  +12..+17 status 1, status 2, last address hi/lo, remaining words hi/lo (2178H)
        //  +18..+20 streamer status bytes 0-5 (2164H), +21..+22 two words from 2174H
        // The word count is the command block count; 0 or >= 23 becomes 23 (044BH-045EH).
        // The manual says 32 octal; the ROM says 27 octal. The register and status save
        // areas are not traced here: those words are 0 except status 1 and 2.
        uint16_t ext[FLOPPY_DMA_EXT_STATUS_WORDS];
        memset(ext, 0, sizeof(ext));
        ext[0] = FLOPPY_DMA_BOARD_NUMBER;
        ext[1] = FLOPPY_DMA_MICROPROGRAM_VERSION;
        ext[2] = FLOPPY_DMA_Z80_STAGING_BUFFER;
        ext[3] = FLOPPY_DMA_Z80_RAM_SIZE;
        ext[12] = calculate_status_word1(self);
        ext[13] = data->commandBlock.fields.status2;
        uint32_t n = words_to_read;
        if (n == 0 || n >= FLOPPY_DMA_EXT_STATUS_WORDS)
        {
            n = FLOPPY_DMA_EXT_STATUS_WORDS;
        }
        for (uint32_t w = 0; w < n; w++)
        {
            dev_dma_write(mem_address, ext[w]);
            mem_address++;
            words_transfered++;
        }
        dev_queue_io_delay(self, IODELAY_FLOPPY, (IODelayedCallback)read_end, data->drive,
                           self->interruptLevel);
        break;
    }

    case FLOPPY_FUNC_FIND_EOF:
        LOG(LOG_CAT_FLOPPY, LOG_DEBUG, "Starting FindEOF on drive position %d\r\n", position);
        dev_queue_io_delay(self, IODELAY_FLOPPY, (IODelayedCallback)read_end, data->drive,
                           self->interruptLevel);
        break;

    case FLOPPY_FUNC_WRITE_EOF:
        LOG(LOG_CAT_FLOPPY, LOG_DEBUG, "Starting WriteEOF on drive position %d\r\n", position);
        dev_queue_io_delay(self, IODELAY_FLOPPY, (IODelayedCallback)read_end, data->drive,
                           self->interruptLevel);
        break;

    case FLOPPY_FUNC_FORMAT_FLOPPY:
        LOG(LOG_CAT_FLOPPY, LOG_DEBUG, "Starting FormatFloppy on drive position %d\r\n", position);
        damage_clear_unit(data, data->drive); // a format rewrites every sector (marks only)
        dev_queue_io_delay(self, IODELAY_FLOPPY, (IODelayedCallback)read_end, data->drive,
                           self->interruptLevel);
        break;

    case FLOPPY_FUNC_READ_FORMAT:
        if (Log_IsEnabled(LOG_CAT_FLOPPY, LOG_TRACE))
        {
            log_write(LOG_CAT_FLOPPY, LOG_TRACE, "Starting ReadFormat on drive position %d\r\n",
                      position);
        }
        //*"Read format" returns format in Status word 2
        // Format read from diskette, valid for read format command or when eror 12
        // Bit 0-1 Bytes pr sector
        //  00 = 512 bytes /sector
        //  01 = 256 Bytes /sector
        //  10 = 123 bytes /sector
        //  11 = 1024 bytes/sector

        if (data->diskFileSize == 0) // No floppy mounted
        {
            // No disk file loaded, return error
            data->status1.bits.errorCode = DRIVE_NOT_READY;
            data->status1.bits.deviceActive = false;
            data->status1.bits.readyForTransfer = true;
            dev_set_interrupt_status(
                self, data->status1.bits.interruptEnabled && data->status1.bits.readyForTransfer,
                self->interruptLevel);
            break;
        }

        // Calculate format based on file size
        if (data->diskFileSize == 315392)
        {
            // 8" disk
            data->commandBlock.fields.status2 = 0; // 512 bytes/sector
        }
        else if (data->diskFileSize >= 1261568)
        {
            // 5.25" 1.2MB disk
            data->commandBlock.fields.status2 |= 0x3;      // 1024 bytes/sector
            data->commandBlock.fields.status2 |= (1 << 2); // Double sided
            data->commandBlock.fields.status2 |= (1 << 3); // Double density
        }
        dev_queue_io_delay(self, IODELAY_FLOPPY, (IODelayedCallback)read_end, data->drive,
                           self->interruptLevel);
        break;

    case FLOPPY_FUNC_READ_DELETED:
        LOG(LOG_CAT_FLOPPY, LOG_DEBUG, "Starting ReadDeletedRecord on drive position %d\r\n",
            position);
        dev_queue_io_delay(self, IODELAY_FLOPPY, (IODelayedCallback)read_end, data->drive,
                           self->interruptLevel);
        break;

    case FLOPPY_FUNC_WRITE_DELETED:
        LOG(LOG_CAT_FLOPPY, LOG_DEBUG, "Starting WriteDeletedRecord on drive position %d\r\n",
            position);
        dev_queue_io_delay(self, IODELAY_FLOPPY, (IODelayedCallback)read_end, data->drive,
                           self->interruptLevel);
        break;

    case FLOPPY_FUNC_COPY_FLOPPY:
        LOG(LOG_CAT_FLOPPY, LOG_DEBUG, "Starting CopyFloppy on drive position %d\r\n", position);
        dev_queue_io_delay(self, IODELAY_FLOPPY, (IODelayedCallback)read_end, data->drive,
                           self->interruptLevel);
        break;

    case FLOPPY_FUNC_FORMAT_TRACK:
        LOG(LOG_CAT_FLOPPY, LOG_DEBUG, "Starting FormatTrack on drive position %d\r\n", position);
        // The disk address is the logical address of the track's first sector; the track is
        // rewritten, so its test 16-18 marks go (TPE PROVOKE-ERRORS repairs its test track
        // this way after a provoked CRC error).
        damage_clear(data, data->drive, data->commandBlock.fields.diskAddress,
                     sectors_per_track(bytes_pr_sector), bytes_pr_sector, double_sided);
        dev_queue_io_delay(self, IODELAY_FLOPPY, (IODelayedCallback)read_end, data->drive,
                           self->interruptLevel);
        break;

    case FLOPPY_FUNC_CHECK_FLOPPY:
        /*
            Data is read to the controller's local memory to test for CRC-errors.
            The test halts with the first error discovered. The address of the erroneous sector is held in LAST MEMADDR in the status field.

            The failing sector is in the least significant part of LAST MEMADDR.

            Status field
            +------+-----------------+--------------------+
            | 15   | 14     -      8 | 7                0 |
            +------+-----------------+--------------------+
            | Side |   Track Number  |   Sector number    |
            +------+-----------------+--------------------+

         */
        LOG(LOG_CAT_FLOPPY, LOG_DEBUG, "Starting CheckFloppy on drive position %d\r\n", position);
        {
            // A sector damaged by test 16, 17 or 18 fails the check with the mark's code
            // (05, 06 or 07), and the failing sector is given as side/track/sector in LAST
            // MEMADDR (table above). The sectors before it would have been read; the model
            // does not read any.
            int32_t bad = damage_first(data, data->drive, data->commandBlock.fields.diskAddress,
                                       block_counter, bytes_pr_sector, double_sided,
                                       &damage_error);
            if (track_out_of_range)
            {
                data->status1.bits.errorCode = FLOPPY_ERR_TRACK_OUT_OF_RANGE;
            }
            else if (bad >= 0)
            {
                uint8_t track, side, sector;
                logical_to_physical((uint32_t)bad, bytes_pr_sector, double_sided, &track, &side,
                                    &sector);
                data->status1.bits.errorCode = damage_error;
                mem_address = ((uint32_t)side << 15) | ((uint32_t)track << 8) | sector;
            }
        }
        dev_queue_io_delay(self, IODELAY_FLOPPY, (IODelayedCallback)read_end, data->drive,
                           self->interruptLevel);
        break;

    case FLOPPY_FUNC_IDENTIFY:
        LOG(LOG_CAT_FLOPPY, LOG_DEBUG, "Starting Identify on drive position %d\r\n", position);
        dev_queue_io_delay(self, IODELAY_FLOPPY, (IODelayedCallback)read_end, data->drive,
                           self->interruptLevel);
        break;

    default:
        // A function code the firmware does not know is answered with error 043 "illegal
        // command" (ND-11.021.1 section 3.9; TPE PROVOKE-ERRORS code 43 sends 072 octal).
        LOG(LOG_CAT_FLOPPY, LOG_WARN, "FloppyDMA: Unknown command: %d\n", data->command);
        data->status1.bits.errorCode = FLOPPY_ERR_ILLEGAL_COMMAND;
        dev_queue_io_delay(self, IODELAY_FLOPPY, (IODelayedCallback)read_end, data->drive,
                           self->interruptLevel);
        break;
    }

    // Clean up allocated buffer
    if (buffer)
    {
        free(buffer);
        buffer = NULL;
    }
    /**************************/
    /* Update command block   */
    /**************************/

    // Status 1 (memory writeback: error code in bits 9-14, bit 15 clear)
    data->commandBlock.fields.status1 = calculate_status_word1(self);
    dev_dma_write(data->commandBlockAddress + 6, data->commandBlock.fields.status1);

    // Status 2
    dev_dma_write(data->commandBlockAddress + 7, data->commandBlock.fields.status2);

    // Send back last mem address
    dev_dma_write(data->commandBlockAddress + 8, (uint16_t)((mem_address >> 16) & 0xFF)); // HI
    dev_dma_write(data->commandBlockAddress + 9, (uint16_t)(mem_address & 0xFFFF));       // LO

    // Update words to read
    dev_dma_write(data->commandBlockAddress + 10,
                  (uint16_t)((words_transfered >> 16) & 0xFF));                           // HI
    dev_dma_write(data->commandBlockAddress + 11, (uint16_t)(words_transfered & 0xFFFF)); // LO

    // For now, just simulate completion
}

static bool read_end(Device *self, int drive)
{
    (void)drive;
    FloppyDMAData *data = (FloppyDMAData *)self->deviceData;
    if (!data)
    {
        return false;
    }

    data->status1.bits.deviceActive = false;
    data->status1.bits.readyForTransfer = true;

    data->commandBlock.fields.status1 = calculate_status_word1(self);
    dev_dma_write(data->commandBlockAddress + 6, data->commandBlock.fields.status1);
    // Command completion (ROM 06B3H): after the status block DMA the firmware writes the
    // error code to DHI (071CH) and the low status byte to DLO (0721H), so IOX +0 reads
    // status word 1 after every command. TPE FLOPPY-STREA reads it there.
    data->dataOut = data->commandBlock.fields.status1;

    dev_set_interrupt_status(
        self, data->status1.bits.interruptEnabled && data->status1.bits.readyForTransfer,
        self->interruptLevel);
    return false;
}

static bool auto_load_end(Device *self, int drive)
{
    (void)drive;
    FloppyDMAData *data = (FloppyDMAData *)self->deviceData;
    if (!data)
    {
        return false;
    }

    data->status1.bits.deviceActive = false;
    data->status1.bits.readyForTransfer = true;
    // The autoload failure path ends in the same completion routine 06B3H (via 1FA7H):
    // status word 1 with the error code goes to the data-out register.
    data->dataOut = calculate_status_word1(self);
    dev_set_interrupt_status(
        self, data->status1.bits.interruptEnabled && data->status1.bits.readyForTransfer,
        self->interruptLevel);
    return false;
}

Device *floppy_dma_create_device(uint8_t thumbwheel)
{
    Device *dev = (Device *)malloc(sizeof(Device));
    if (!dev)
    {
        return NULL;
    }

    FloppyDMAData *data = (FloppyDMAData *)malloc(sizeof(FloppyDMAData));
    if (!data)
    {
        free(dev);
        return NULL;
    }

    memset(dev, 0, sizeof(Device));
    memset(data, 0, sizeof(FloppyDMAData));

    // Initialize device base structure
    dev_init(dev, thumbwheel, DEVICE_CLASS_BLOCK, 1024);

    // Initialize device state
    floppy_dma_reset(dev);

    // Set up device address and interrupt settings based on thumbwheel
    switch (thumbwheel)
    {
    case 0:
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "Floppy DMA 0");
        dev->identCode = 021; // octal 021
        dev->startAddress = 01560;
        dev->endAddress = 01567;
        break;
    case 1:
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "Floppy DMA 1");
        dev->identCode = 022; // Octal 022
        dev->startAddress = 01570;
        dev->endAddress = 01577;
        break;
    default:
        LOG(LOG_CAT_FLOPPY, LOG_WARN, "Floppy DMA: Unknown thumbwheel value: %d\n", thumbwheel);
        free(data);
        free(dev);
        return NULL;
    }

    dev->interruptLevel = 11; // floppy
    data->diskFileSize = 0;   // unknown size

    // Set up function pointers
    dev->Read = floppy_dma_read;
    dev->Write = floppy_dma_write;
    dev->Tick = floppy_dma_tick;
    dev->Reset = floppy_dma_reset;
    dev->Ident = floppy_dma_ident;
    dev->deviceData = data;

    LOG(LOG_CAT_FLOPPY, LOG_INFO,
        "Floppy DMA [%s] Device object created. Address[%o-%o] Ident code: [%o] Level: [%d]\n",
        dev->memoryName, dev->startAddress, dev->endAddress, dev->identCode, dev->interruptLevel);

    return dev;
}


/*

 TEST PROGRAM FAILS!!

    FLOPPY-STREAM - Version: C03 - 1988-11-08


==TPE42=> UNEXPECTED INTERNAL INTERRUPT
========> Interruped level.......: 0D
========> Internal interrupt code: 9D (Memory out of range)
========> Address................: 125012B
========> Instruction............: 150417B
========> Memory address(PEA)....: 000001B
========> Bank(PES 0-7)..........: 40B
========> Error type(PES 13-15)..:
========> Error code(PES 8-12)...: 0B

WAIT when IONI is off PIL[14] PC[ 54555] PID[0x6000] PIE[0x6001] IONI[0] PONI[0] STS_HI[1E00] STS_LO[  20]

*/
