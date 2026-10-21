/*
 * test_floppy_dma_testmode.c - Unit tests for the 3112 floppy DMA controller test mode.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Following the test_drum.c pattern: device_floppy_dma.c is linked in DIRECTLY together with
 * FAKE Device_* infrastructure provided here (fake ND-100 memory for DMA, one pending delayed
 * callback, an in-memory floppy image behind the block callbacks), so the controller's real
 * IOX handlers run without the machine, the CPU or an image file.
 *
 * What is checked is the behaviour of the Z80 firmware ROM 34300G as read from its bytes
 * (test dispatcher 1CDCH, handlers 1D15H-1E0CH): see the comments in device_floppy_dma.c.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device_floppy_dma.h"
#include "devices_protos.h"

/* ---------------- fake device infrastructure ---------------------------- */

/* A physical-memory stand-in for DMA. 64 Ki words. */
#define FAKE_MEM_WORDS 65536u
static uint16_t fake_mem[FAKE_MEM_WORDS];

/* One pending delayed callback (the floppy queues one per command or test). */
static IODelayedCallback pending_cb;
static void *pending_ctx;
static int pending_param;
static uint8_t pending_level;
static int pending_set;

void dev_init(Device *dev, uint8_t thumbwheel, DeviceClass device_class, size_t block_size)
{
    (void)thumbwheel;
    (void)block_size;
    memset(dev, 0, sizeof(Device));
    dev->deviceClass = device_class;
}

uint32_t dev_register_address(Device *dev, uint32_t address)
{
    return address - dev->startAddress;
}

void dev_dma_write(uint32_t core_address, uint16_t data)
{
    if (core_address < FAKE_MEM_WORDS)
    {
        fake_mem[core_address] = data;
    }
}

int32_t dev_dma_read(uint32_t core_address)
{
    if (core_address < FAKE_MEM_WORDS)
    {
        return fake_mem[core_address];
    }
    return 0;
}

int32_t dev_io_buffer_read_word(Device *dev, uint8_t *buf, int32_t word_offset)
{
    (void)dev;
    return (buf[word_offset * 2] << 8) | buf[word_offset * 2 + 1];
}

int32_t dev_io_buffer_write_word(Device *dev, uint8_t *buf, int32_t word_offset, uint16_t data)
{
    (void)dev;
    buf[word_offset * 2] = (uint8_t)(data >> 8);
    buf[word_offset * 2 + 1] = (uint8_t)(data & 0xFF);
    return 0;
}

void dev_queue_io_delay(Device *dev, uint16_t ticks, IODelayedCallback cb, int param,
                        uint8_t irqlevel)
{
    (void)ticks;
    pending_cb = cb;
    pending_ctx = dev;
    pending_param = param;
    pending_level = irqlevel;
    pending_set = 1;
}

void dev_irq_changed(void);
void dev_irq_changed(void)
{
}

void dev_tick_io_delay(Device *dev)
{
    if (!pending_set)
    {
        return;
    }
    pending_set = 0;
    if (pending_cb && pending_cb(pending_ctx, pending_param))
    {
        dev->interruptBits |= (uint16_t)(1u << pending_level);
    }
}

void dev_set_interrupt_status(Device *dev, bool active, uint16_t level)
{
    if (active)
    {
        dev->interruptBits |= (uint16_t)(1u << level);
    }
    else
    {
        dev->interruptBits &= (uint16_t)~(1u << level);
    }
}

/* ---------------- fake floppy image (unit 0 only) ----------------------- */

/* ND 1024-byte double-sided format: 8 sectors x 77 tracks x 2 sides. */
#define IMAGE_BYTES 1261568u
static uint8_t fake_image[IMAGE_BYTES];

static int fake_read(Device *dev, uint8_t *buffer, size_t size, uint32_t block_address, int unit)
{
    size_t bytes = size * dev->blockSizeBytes;
    size_t offset = (size_t)block_address * dev->blockSizeBytes;
    if (unit != 0 || offset + bytes > IMAGE_BYTES)
    {
        return -1;
    }
    memcpy(buffer, fake_image + offset, bytes);
    return (int)size;
}

static int fake_write(Device *dev, const uint8_t *buffer, size_t size, uint32_t block_address,
                      int unit)
{
    size_t bytes = size * dev->blockSizeBytes;
    size_t offset = (size_t)block_address * dev->blockSizeBytes;
    if (unit != 0 || offset + bytes > IMAGE_BYTES)
    {
        return -1;
    }
    memcpy(fake_image + offset, buffer, bytes);
    return (int)size;
}

static int fake_info(Device *dev, size_t *image_size, bool *is_write_protected, int unit)
{
    (void)dev;
    *image_size = (unit == 0) ? IMAGE_BYTES : 0;
    *is_write_protected = false;
    return 0;
}

/* ---------------- tiny assert harness ----------------------------------- */

static int g_pass, g_fail;
// clang-format off
#define CHECK(cond, msg) do {                                            \
        if (cond) { g_pass++; }                                          \
        else { g_fail++; printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)
// clang-format on

/* ---------------- IOX helpers --------------------------------------------- */

#define CW_INTERRUPT 0x0002 /* bit 1: interrupt on RFT */
#define CW_TEST_MODE 0x0008 /* bit 3 */
#define CW_CLEAR 0x0010     /* bit 4 */
#define CW_EXECUTE 0x0100   /* bit 8 */

/* Control word for a test: ND-11.021.1 Table 2 gives e.g. 016410 octal for test 14. */
static uint16_t test_cw(int test, bool irq)
{
    return (uint16_t)((test << 9) | CW_EXECUTE | CW_TEST_MODE | (irq ? CW_INTERRUPT : 0));
}

static uint16_t status(Device *dev)
{
    return dev->Read(dev, dev->startAddress + FLOPPY_DMA_READ_STATUS1);
}

static uint16_t data_out(Device *dev)
{
    return dev->Read(dev, dev->startAddress + FLOPPY_DMA_READ_DATA);
}

/* Load POH (IOX +5), POM:POL (IOX +7) and start the test (IOX +3). */
static void start_test(Device *dev, int test, uint8_t poh, uint16_t pom_pol, bool irq)
{
    dev->Write(dev, dev->startAddress + FLOPPY_DMA_LOAD_POINTER_HI, poh);
    dev->Write(dev, dev->startAddress + FLOPPY_DMA_LOAD_POINTER_LO, pom_pol);
    dev->Write(dev, dev->startAddress + FLOPPY_DMA_LOAD_CONTROL, test_cw(test, irq));
}

/* Start a test and let it finish (the floppy delay callback). */
static void run_test(Device *dev, int test, uint8_t poh, uint16_t pom_pol)
{
    start_test(dev, test, poh, pom_pol, false);
    dev->Tick(dev);
}

#define ST_ACTIVE 0x0004 /* hardware status bit 2: device active */
#define ST_RFT 0x0008    /* bit 3: ready for transfer */
#define ST_OR 0x0010     /* bit 4: OR of errors */

/* Build a command block at cb_addr and run a normal floppy command (bit 8 only).
 * Format 3 = 1024 bytes/sector, bit 2 = double sided. */
static void run_command(Device *dev, uint32_t cb_addr, uint16_t command, uint16_t disk_address,
                        uint16_t mem_address, uint16_t sector_count)
{
    fake_mem[cb_addr + 0] = (uint16_t)(command | (0x7 << 8)); /* format 7: 1024 B/s, DS */
    fake_mem[cb_addr + 1] = disk_address;
    fake_mem[cb_addr + 2] = 0;
    fake_mem[cb_addr + 3] = mem_address;
    fake_mem[cb_addr + 4] = 0; /* bit 15 clear: sector count */
    fake_mem[cb_addr + 5] = sector_count;
    fake_mem[cb_addr + 6] = 0;
    fake_mem[cb_addr + 7] = 0;
    dev->Write(dev, dev->startAddress + FLOPPY_DMA_LOAD_POINTER_HI, 0);
    dev->Write(dev, dev->startAddress + FLOPPY_DMA_LOAD_POINTER_LO, (uint16_t)cb_addr);
    dev->Write(dev, dev->startAddress + FLOPPY_DMA_LOAD_CONTROL, CW_EXECUTE);
    dev->Tick(dev);
}

/* Error code from status word 1 (command block +6), bits 9-14. */
static int error_code(uint32_t cb_addr)
{
    return (fake_mem[cb_addr + 6] >> 9) & 0x3F;
}

int main(void)
{
    Device *dev = floppy_dma_create_device(0);
    if (!dev)
    {
        printf("FAIL: could not create the floppy DMA device\n");
        return 1;
    }
    FloppyDMAData *d = (FloppyDMAData *)dev->deviceData;
    dev->blockCallbacks.readFunc = fake_read;
    dev->blockCallbacks.writeFunc = fake_write;
    dev->blockCallbacks.diskInfoFunc = fake_info;

    /* A recognisable diskette: byte k of the image = (k * 7 + 3) & 0xFF. */
    for (uint32_t k = 0; k < IMAGE_BYTES; k++)
    {
        fake_image[k] = (uint8_t)(k * 7 + 3);
    }

    CHECK(dev->startAddress == 01560, "thumbwheel 0 is IOX 1560");
    /* Master clear, as the machine does it (floppy_dma_create_device runs its reset before
       deviceData is attached, so the create alone leaves the registers at 0). */
    dev->Reset(dev);
    CHECK(data_out(dev) == 0x0008, "after reset IOX +0 reads DLO=8, DHI=0 (ROM 003FH-0045H)");

    /* --- 1. Test number = control word bits 9-15. --- */
    run_test(dev, 2, 0x11, 0x2233);
    CHECK(data_out(dev) == 0x2233, "T2 via (CW >> 9) & 0x7F: DLO = POL, DHI = POM");
    /* Test 66 = 64 + 2: bit 15 set. Only bits 9-13 would make this test 2 again. */
    run_test(dev, 66, 0x11, 0x2233);
    CHECK(data_out(dev) == 0, "test 66 is not test 2: bits 9-15 are the test number");
    CHECK((status(dev) & ST_RFT) != 0, "an out-of-table test number still finishes");

    /* --- 2. Busy / finish / interrupt behaviour (T0, T1). --- */
    start_test(dev, 0, 0, 0, false);
    CHECK((status(dev) & ST_ACTIVE) != 0 && (status(dev) & ST_RFT) == 0,
          "T0 started: device active, RFT clear");
    CHECK(dev->interruptBits == 0, "no interrupt while a test runs");
    dev->Tick(dev);
    CHECK((status(dev) & ST_ACTIVE) == 0 && (status(dev) & ST_RFT) != 0,
          "T0 finished: device idle, RFT set");
    CHECK(dev->interruptBits == 0, "T0 without control word bit 1: no interrupt");
    start_test(dev, 0, 0, 0, true);
    dev->Tick(dev);
    CHECK((dev->interruptBits & (1u << 11)) != 0, "T0 with bit 1: interrupt on level 11");
    CHECK(dev->Ident(dev, 11) == 021, "ident code 021 answers the interrupt");

    start_test(dev, 1, 0, 0, true);
    CHECK((status(dev) & ST_ACTIVE) != 0 && (status(dev) & ST_RFT) == 0, "T1: busy");
    dev->Tick(dev);
    CHECK((status(dev) & ST_ACTIVE) != 0 && (status(dev) & ST_RFT) == 0,
          "T1 does not finish by itself (JR 1D15H): still busy after the floppy delay");
    CHECK(dev->interruptBits == 0, "T1: no interrupt even with bit 1");
    {
        /* The watchdog: one more chunk per Tick in this harness; it must end within the
           chained chunk count with error 026, RFT and the interrupt. */
        int ticks = 0;
        while ((status(dev) & ST_RFT) == 0 && ticks < FLOPPY_DMA_WATCHDOG_CHUNKS + 5)
        {
            dev->Tick(dev);
            ticks++;
        }
        CHECK(ticks == FLOPPY_DMA_WATCHDOG_CHUNKS - 1,
              "T1 ends after the watchdog chunks (ROM 0066H NMI counter -> 1EB0H)");
        CHECK(((data_out(dev) >> 9) & 0x3F) == FLOPPY_ERR_TIMEOUT && (data_out(dev) & ST_RFT) != 0,
              "T1 watchdog: IOX +0 reads status word 1 with error 026 and RFT");
        CHECK((status(dev) & ST_OR) != 0 && (dev->interruptBits & (1u << 11)) != 0,
              "T1 watchdog: OR-of-errors set and the interrupt raised");
        dev->Ident(dev, 11);
    }
    start_test(dev, 1, 0, 0, true);
    dev->Tick(dev);
    CHECK((status(dev) & ST_RFT) == 0, "T1 again: busy");
    dev->Write(dev, dev->startAddress + FLOPPY_DMA_LOAD_CONTROL, CW_CLEAR);
    CHECK((status(dev) & ST_ACTIVE) == 0 && (status(dev) & ST_RFT) != 0,
          "device clear ends the T1 loop");
    start_test(dev, 1, 0, 0, false);
    run_test(dev, 2, 0x00, 0x1234);
    CHECK(data_out(dev) == 0x1234 && (status(dev) & ST_RFT) != 0,
          "the next control word after T1 runs its test normally");

    /* --- 3. Register tests T2, T3, T6, T22/T23. --- */
    run_test(dev, 2, 0xAB, 0xBEEF);
    CHECK(data_out(dev) == 0xBEEF, "T2: DHI:DLO = POM:POL");
    run_test(dev, 3, 0xAB, 0xBEEF);
    CHECK(data_out(dev) == 0x00AB, "T3: DLO = POH, DHI = 0");
    run_test(dev, 6, 0xAB, 0xBEEF);
    CHECK(data_out(dev) == 0x6000,
          "T6: first address above the RAM in DHI:DLO (ROM 1D38H, probe 02DCH), not in POL/POM");
    run_test(dev, 22, 0x00, 0x27F0);
    CHECK(data_out(dev) == 0, "T22 writes nothing to the data-out register");
    run_test(dev, 23, 0x00, 0x0000);
    CHECK(data_out(dev) == 0x27F0, "T23: DHI:DLO = SP loaded by T22");

    /* --- 4. Z80 byte access T4 / T5. --- */
    run_test(dev, 4, 0x5A, 0x3000);
    run_test(dev, 4, 0xA5, 0x3001);
    run_test(dev, 5, 0x00, 0x3000);
    CHECK(data_out(dev) == 0x005A, "T5 reads back the byte T4 wrote at 3000H");
    run_test(dev, 5, 0x00, 0x3001);
    CHECK(data_out(dev) == 0x00A5, "T5 reads back the byte T4 wrote at 3001H");
    run_test(dev, 5, 0x00, 0x2200);
    CHECK(data_out(dev) == fake_image[0], "T5 at 2200H: the staging buffer holds diskette byte 0");
    run_test(dev, 5, 0x00, 0x2200 + 0x140);
    CHECK(data_out(dev) == fake_image[0x140], "T5 at 2340H: diskette byte 140H");
    run_test(dev, 5, 0x00, 0x1000);
    CHECK(data_out(dev) == 0, "T5 outside the modelled window reads 0");
    run_test(dev, 5, 0x00, 0x0060);
    CHECK(data_out(dev) == 0xED, "T5 at ROM 0060H reads the 34300G byte EDH");
    run_test(dev, 5, 0x00, 0x0061);
    CHECK(data_out(dev) == 0x59, "T5 at ROM 0061H reads the 34300G byte 59H");
    run_test(dev, 5, 0x00, 0x0000);
    CHECK(data_out(dev) == 0xDB, "T5 at ROM 0000H reads IN A,(61H) = DBH");

    /* --- 5. T9 / T10 128-byte block DMA with the ROM address formula. --- */
    /* POH = 2 -> Z80 2180H (ROM 1E78H), not the manual's 2080H. */
    for (int w = 0; w < 64; w++)
    {
        fake_mem[0x1000 + w] = (uint16_t)(0xC000 + w * 0x0101);
    }
    run_test(dev, 10, 2, 0x1000); /* ND-100 1000H -> Z80 block 2 */
    run_test(dev, 5, 0, 0x2180);
    CHECK(data_out(dev) == 0xC0, "T10 POH=2 landed at Z80 2180H (high byte of word 0)");
    run_test(dev, 5, 0, 0x2181);
    CHECK(data_out(dev) == 0x00, "T10: low byte of word 0 at 2181H");
    run_test(dev, 5, 0, 0x2180 + 127);
    CHECK(data_out(dev) == 0x3F, "T10: last byte of the block (word 63 low byte)");
    run_test(dev, 5, 0, 0x2080);
    CHECK(data_out(dev) == 0, "T10 POH=2 did not use the manual's 2080H (still empty)");
    memset(&fake_mem[0x2000], 0, 64 * sizeof(uint16_t));
    run_test(dev, 9, 2, 0x2000); /* Z80 block 2 -> ND-100 2000H */
    CHECK(memcmp(&fake_mem[0x2000], &fake_mem[0x1000], 64 * sizeof(uint16_t)) == 0,
          "T9 POH=2 copied the block back word for word");
    /* POH = 1 -> 2000H, POH = 3 -> 2100H, POH = 0 -> 2080H, POH = 5 -> 2200H (staging). */
    run_test(dev, 10, 1, 0x1000);
    CHECK(d->z80Ram[0x2000 - FLOPPY_DMA_Z80_RAM_BASE] == 0xC0, "T10 POH=1 -> 2000H");
    run_test(dev, 10, 3, 0x1000);
    CHECK(d->z80Ram[0x2100 - FLOPPY_DMA_Z80_RAM_BASE] == 0xC0, "T10 POH=3 -> 2100H");
    run_test(dev, 10, 0, 0x1000);
    CHECK(d->z80Ram[0x2080 - FLOPPY_DMA_Z80_RAM_BASE] == 0xC0, "T10 POH=0 -> 2080H");
    memset(&fake_mem[0x2000], 0, 64 * sizeof(uint16_t));
    run_test(dev, 9, 5, 0x2000);
    CHECK(fake_mem[0x2000] == (uint16_t)((fake_image[0] << 8) | fake_image[1]),
          "T9 POH=5 reads the staging buffer at 2200H as big-endian words");
    CHECK(data_out(dev) == 0, "T9/T10 leave the data-out register at 0");

    /* --- 6. T11 compare. --- */
    for (int i = 0; i < 128; i++)
    {
        d->z80Ram[0x3100 - FLOPPY_DMA_Z80_RAM_BASE + i] = (uint8_t)(i * 3);
        d->z80Ram[0x3180 - FLOPPY_DMA_Z80_RAM_BASE + i] = (uint8_t)(i * 3);
    }
    run_test(dev, 11, 0, 0x3100);
    CHECK(data_out(dev) == 0, "T11: equal blocks -> 0");
    d->z80Ram[0x3180 - FLOPPY_DMA_Z80_RAM_BASE + 5] ^= 0xFF; /* first mismatch at index 5 */
    d->z80Ram[0x3180 - FLOPPY_DMA_Z80_RAM_BASE + 9] ^= 0xFF;
    run_test(dev, 11, 0, 0x3100);
    CHECK(data_out(dev) == 127 - 5, "T11: mismatch at byte 5 -> LDIR count left = 122");
    d->z80Ram[0x3180 - FLOPPY_DMA_Z80_RAM_BASE + 5] ^= 0xFF;
    d->z80Ram[0x3180 - FLOPPY_DMA_Z80_RAM_BASE + 9] ^= 0xFF;
    d->z80Ram[0x3100 - FLOPPY_DMA_Z80_RAM_BASE] ^= 0x01; /* mismatch at byte 0 */
    run_test(dev, 11, 0, 0x3100);
    CHECK(data_out(dev) == 127, "T11: mismatch at byte 0 -> 127");

    /* --- 7. T13 / T14 with the parameter field, both directions, odd count. --- */
    /* Field at ND-100 word 0x0700 (via POH:POM:POL = 000700H): +0 ND-100 address 0x0800,
       +1 Z80 address 0x3400, +2 byte count 7 -> 3 words moved, 7th byte not moved. */
    fake_mem[0x0700] = 0x0800;
    fake_mem[0x0701] = 0x3400;
    fake_mem[0x0702] = 7;
    fake_mem[0x0800] = 0x1122;
    fake_mem[0x0801] = 0x3344;
    fake_mem[0x0802] = 0x5566;
    fake_mem[0x0803] = 0x7788;
    memset(&d->z80Ram[0x3400 - FLOPPY_DMA_Z80_RAM_BASE], 0xEE, 8);
    run_test(dev, 13, 0x00, 0x0700);
    {
        const uint8_t *z = &d->z80Ram[0x3400 - FLOPPY_DMA_Z80_RAM_BASE];
        CHECK(z[0] == 0x11 && z[1] == 0x22, "T13: word +0 high byte first at the even Z80 address");
        CHECK(z[2] == 0x33 && z[3] == 0x44 && z[4] == 0x55 && z[5] == 0x66,
              "T13: words 1 and 2 big-endian");
        CHECK(z[6] == 0xEE && z[7] == 0xEE, "T13: odd count 7 -> floor(7/2) = 3 words, byte 6 untouched");
    }
    CHECK(data_out(dev) == 0, "T13: IOX +0 reads 0 afterwards (no status word 1)");
    /* Z80 -> ND-100: change the Z80 bytes, read them back into another ND-100 area. */
    {
        uint8_t *z = &d->z80Ram[0x3400 - FLOPPY_DMA_Z80_RAM_BASE];
        z[0] = 0xA1; z[1] = 0xB2; z[2] = 0xC3; z[3] = 0xD4; z[4] = 0xE5; z[5] = 0xF6; z[6] = 0x07;
    }
    fake_mem[0x0700] = 0x0900;
    fake_mem[0x0702] = 7;
    fake_mem[0x0900] = fake_mem[0x0901] = fake_mem[0x0902] = fake_mem[0x0903] = 0xFFFF;
    run_test(dev, 14, 0x00, 0x0700);
    CHECK(fake_mem[0x0900] == 0xA1B2 && fake_mem[0x0901] == 0xC3D4 && fake_mem[0x0902] == 0xE5F6,
          "T14: 3 words, even Z80 byte = ND-100 high byte");
    CHECK(fake_mem[0x0903] == 0xFFFF, "T14: odd count -> the 4th word is not written");
    CHECK(data_out(dev) == 0, "IOX +0 reads 0 after T14 (ROM: no status word 1 write)");
    CHECK((status(dev) & ST_RFT) != 0 && (status(dev) & ST_OR) == 0,
          "T14 ends with RFT set and no error bits");
    /* 24-bit pointer: field at ND-100 word 0x1_0400 is outside the 64 Ki fake memory, so it
       reads as zeros -> count 0 = 65536 bytes, rejected by the window check, nothing moved. */
    fake_mem[0x0A00] = 0x55AA;
    run_test(dev, 14, 0x01, 0x0400);
    CHECK(fake_mem[0x0A00] == 0x55AA, "T14 with POH=1 fetches the field from word 10400H, not 0400H");
    /* The SINTRAN floppy monitor's real use: ND-100 words 0120 octal, Z80 2340H, 06C0H bytes. */
    fake_mem[0x0700] = 0120;
    fake_mem[0x0701] = 0x2340;
    fake_mem[0x0702] = 0x06C0;
    run_test(dev, 14, 0x00, 0x0700);
    CHECK(fake_mem[0120] == (uint16_t)((fake_image[0x140] << 8) | fake_image[0x141]),
          "T14 from 2340H: first word = diskette bytes 140H,141H");
    CHECK(fake_mem[0120 + 863] == (uint16_t)((fake_image[0x140 + 1726] << 8) | fake_image[0x140 + 1727]),
          "T14 from 2340H: word 863 = diskette bytes 7FEH,7FFH (end of the staging buffer)");

    /* --- 8. T24 FD1797 command: restore / seek / step. --- */
    run_test(dev, 24, 0x00, 0x0008); /* restore */
    CHECK(data_out(dev) == 0x0004, "T24 restore: track 0, status bit 2 (track 0)");
    run_test(dev, 24, 0x2A, 0x0018); /* seek to track 42 */
    CHECK(data_out(dev) == 0x2A00, "T24 seek: DHI = track register 42, status 0");
    run_test(dev, 24, 0x00, 0x0058); /* step in with update */
    CHECK(data_out(dev) == 0x2B00, "T24 step in (update): track 43");
    run_test(dev, 24, 0x00, 0x0078); /* step out with update */
    CHECK(data_out(dev) == 0x2A00, "T24 step out (update): track 42");
    run_test(dev, 24, 0x00, 0x0088); /* type II read sector: no drive model, status 0 */
    CHECK(data_out(dev) == 0x2A00, "T24 type II command: track unchanged, status 0");

    /* --- 9. T16 / T17 / T18 damage marks and the read command. --- */
    /* Unit 0 (FDVSEL bit 0), track 1, sector 3: logical (1*2+0)*8 + 2 = 18. */
    const uint32_t cb = 0x0600;
    run_command(dev, cb, FLOPPY_FUNC_READ_DATA, 18, 0x4000, 1);
    CHECK(error_code(cb) == 0, "read of logical sector 18 is clean before T16");
    CHECK(fake_mem[0x4000] == (uint16_t)((fake_image[18 * 1024] << 8) | fake_image[18 * 1024 + 1]),
          "read of logical sector 18 delivers the image data");
    run_test(dev, 16, 0x01, (uint16_t)((1 << 8) | 3));
    CHECK(data_out(dev) == 0, "T16: IOX +0 reads 0 (ROM: no status word 1)");
    run_command(dev, cb, FLOPPY_FUNC_READ_DATA, 18, 0x4000, 1);
    CHECK(error_code(cb) == FLOPPY_ERR_CRC, "read of the T16 sector: error 05 CRC error");
    CHECK((status(dev) & ST_OR) != 0, "CRC error sets OR-of-errors in the hardware status");
    CHECK(data_out(dev) == fake_mem[cb + 6] && ((data_out(dev) >> 9) & 0x3F) == FLOPPY_ERR_CRC,
          "after a command IOX +0 reads status word 1 (ROM 06B3H: 071CH/0721H)");
    run_command(dev, cb, FLOPPY_FUNC_READ_DATA, 17, 0x4000, 1);
    CHECK(error_code(cb) == 0, "the sector before it is still clean");
    run_command(dev, cb, FLOPPY_FUNC_READ_DATA, 16, 0x4000, 4);
    CHECK(error_code(cb) == FLOPPY_ERR_CRC, "a multi-sector read crossing the damaged sector fails");
    fake_mem[0x4000] = 0x1234;
    for (int w = 1; w < 512; w++)
    {
        fake_mem[0x4000 + w] = 0;
    }
    run_command(dev, cb, FLOPPY_FUNC_WRITE_DATA, 18, 0x4000, 1);
    CHECK(error_code(cb) == 0, "writing the damaged sector succeeds");
    run_command(dev, cb, FLOPPY_FUNC_READ_DATA, 18, 0x5000, 1);
    CHECK(error_code(cb) == 0 && fake_mem[0x5000] == 0x1234,
          "after the write the sector reads clean with the new data");
    /* T18: sector 1 of track 5 = logical 80. T17: whole track 6 = logical 96..103. */
    run_test(dev, 18, 0x01, (uint16_t)(5 << 8));
    run_command(dev, cb, FLOPPY_FUNC_READ_DATA, 80, 0x4000, 1);
    CHECK(error_code(cb) == FLOPPY_ERR_SECTOR_NOT_FOUND,
          "T18 track 5: sector 1 (logical 80) reads as 06 sector not found");
    run_command(dev, cb, FLOPPY_FUNC_READ_DATA, 81, 0x4000, 1);
    CHECK(error_code(cb) == 0, "T18 track 5: sector 2 (logical 81) is clean");
    run_test(dev, 17, 0x01, (uint16_t)(6 << 8));
    run_command(dev, cb, FLOPPY_FUNC_READ_DATA, 103, 0x4000, 1);
    CHECK(error_code(cb) == FLOPPY_ERR_TRACK_NOT_FOUND,
          "T17 track 6: its last sector on side 0 (logical 103) reads as 07 track not found");
    run_command(dev, cb, FLOPPY_FUNC_READ_DATA, 104, 0x4000, 1);
    CHECK(error_code(cb) == 0, "T17 track 6: side 1 (logical 104) is not marked");
    /* Unit 1 (FDVSEL bit 1) marks do not touch unit 0. */
    run_test(dev, 16, 0x02, (uint16_t)((1 << 8) | 3));
    run_command(dev, cb, FLOPPY_FUNC_READ_DATA, 18, 0x4000, 1);
    CHECK(error_code(cb) == 0, "a T16 on unit 1 leaves unit 0's sector clean");
    CHECK(fake_image[80 * 1024] == (uint8_t)(80 * 1024 * 7 + 3) &&
              fake_image[96 * 1024] == (uint8_t)(96 * 1024 * 7 + 3),
          "T16/T17/T18 did not write the image");

    /* --- 10. A command with control word bit 1: no interrupt on the old RFT at the IOX,
       the interrupt comes with the completion. --- */
    fake_mem[cb + 0] = FLOPPY_FUNC_READ_FORMAT | (0x7 << 8);
    fake_mem[cb + 1] = 0;
    fake_mem[cb + 5] = 0;
    dev->Write(dev, dev->startAddress + FLOPPY_DMA_LOAD_POINTER_HI, 0);
    dev->Write(dev, dev->startAddress + FLOPPY_DMA_LOAD_POINTER_LO, (uint16_t)cb);
    CHECK((status(dev) & ST_RFT) != 0, "idle before the command: RFT set");
    dev->Write(dev, dev->startAddress + FLOPPY_DMA_LOAD_CONTROL, CW_EXECUTE | CW_INTERRUPT | 0x2000);
    CHECK((status(dev) & ST_RFT) == 0 && (status(dev) & ST_ACTIVE) != 0,
          "the control word drops RFT and raises device active at once");
    CHECK(dev->interruptBits == 0, "no interrupt on the old RFT when the command starts");
    dev->Tick(dev);
    CHECK((status(dev) & ST_RFT) != 0 && (dev->interruptBits & (1u << 11)) != 0,
          "completion: RFT set and the interrupt raised");
    CHECK(dev->Ident(dev, 11) == 021, "IDENT answers it");

    /* --- 11. Read extended status (036 octal), what the TPE FLOPPY-STREA program uses to
       name the card: word count 1 -> the board number only; count 0 -> all 23 words. --- */
    fake_mem[cb + 0] = FLOPPY_FUNC_READ_EXTENDED_STATUS;
    fake_mem[cb + 1] = 0;
    fake_mem[cb + 2] = 0;
    fake_mem[cb + 3] = 0x4800;
    fake_mem[cb + 4] = 0x8000; /* word count */
    fake_mem[cb + 5] = 1;
    fake_mem[0x4800] = 0xFFFF;
    fake_mem[0x4801] = 0xFFFF;
    dev->Write(dev, dev->startAddress + FLOPPY_DMA_LOAD_POINTER_LO, (uint16_t)cb);
    dev->Write(dev, dev->startAddress + FLOPPY_DMA_LOAD_CONTROL, CW_EXECUTE);
    dev->Tick(dev);
    CHECK(fake_mem[0x4800] == 0x0C28 && fake_mem[0x4801] == 0xFFFF,
          "036 with word count 1: board number 3112 only");
    CHECK(error_code(cb) == 0 && (status(dev) & ST_RFT) != 0, "036 completes with RFT, no error");
    fake_mem[cb + 5] = 0;
    for (int w = 0; w < 24; w++)
    {
        fake_mem[0x4800 + w] = 0xFFFF;
    }
    dev->Write(dev, dev->startAddress + FLOPPY_DMA_LOAD_CONTROL, CW_EXECUTE);
    dev->Tick(dev);
    CHECK(fake_mem[0x4801] == 0x0006, "036 word 1: sub version 0, micro program version 6 (G)");
    CHECK(fake_mem[0x4802] == 0x2200 && fake_mem[0x4803] == 0x4000,
          "036 words 2-3: buffer start 2200H, RAM size 4000H");
    CHECK(((fake_mem[0x480C] >> 9) & 0x3F) == 0 && (fake_mem[0x480C] & 0x8000) == 0,
          "036 word 12: status word 1 with no error code and bit 15 clear");
    CHECK(fake_mem[0x4816] == 0 && fake_mem[0x4817] == 0xFFFF,
          "036 with count 0: 23 words written, the 24th untouched");
    CHECK(fake_mem[cb + 11] == 23, "036 status block: 23 words transferred");

    /* --- 12. Device clear (control word bit 4) restarts the Z80: the firmware reset leaves
       DLO = 8, DHI = 0 (what TPE ACTIVATE-SELFTEST reads as "self-test error 0"). --- */
    run_test(dev, 6, 0, 0);
    CHECK(data_out(dev) == 0x6000, "T6 left 6000H in the data-out register");
    run_test(dev, 16, 0x01, (uint16_t)((2 << 8) | 1));
    run_command(dev, cb, FLOPPY_FUNC_READ_DATA, 32, 0x4000, 1);
    CHECK((status(dev) & ST_OR) != 0, "a CRC error is pending before the clear");
    dev->Write(dev, dev->startAddress + FLOPPY_DMA_LOAD_CONTROL, CW_CLEAR);
    CHECK(data_out(dev) == 0x0008, "after device clear IOX +0 reads 0008H (ROM 003FH-0045H)");
    CHECK((status(dev) & ST_OR) == 0 && (status(dev) & ST_RFT) != 0,
          "after device clear: no error bits, RFT set");
    run_command(dev, cb, FLOPPY_FUNC_READ_DATA, 32, 0x4000, 1);
    CHECK(error_code(cb) == FLOPPY_ERR_CRC, "the clear does not repair the diskette (T16 mark stays)");
    run_command(dev, cb, FLOPPY_FUNC_WRITE_DATA, 32, 0x4000, 1);

    /* --- 13. CHECK FLOPPY and FORMAT TRACK, the way TPE PROVOKE-ERRORS code 5 uses them:
       format the test track, T16 on it, CHECK FLOPPY must give error 05 with the failing
       sector in LAST MEMADDR, FORMAT TRACK repairs it. Track 38 = logical 608. --- */
    run_command(dev, cb, FLOPPY_FUNC_FORMAT_TRACK, 608, 0, 1);
    run_command(dev, cb, FLOPPY_FUNC_CHECK_FLOPPY, 608, 0, 1);
    CHECK(error_code(cb) == 0, "CHECK FLOPPY on a clean sector: no error");
    run_test(dev, 16, 0x01, (uint16_t)((38 << 8) | 1));
    run_command(dev, cb, FLOPPY_FUNC_CHECK_FLOPPY, 608, 0, 1);
    CHECK(error_code(cb) == FLOPPY_ERR_CRC, "CHECK FLOPPY on the T16 sector: error 05");
    CHECK(fake_mem[cb + 9] == ((38 << 8) | 1) && fake_mem[cb + 8] == 0,
          "CHECK FLOPPY: LAST MEMADDR = side 0, track 38, sector 1");
    run_command(dev, cb, FLOPPY_FUNC_CHECK_FLOPPY, 600, 0, 16);
    CHECK(error_code(cb) == FLOPPY_ERR_CRC && fake_mem[cb + 9] == ((38 << 8) | 1),
          "CHECK FLOPPY over 16 sectors stops at the first damaged one");
    run_command(dev, cb, FLOPPY_FUNC_FORMAT_TRACK, 608, 0, 1);
    run_command(dev, cb, FLOPPY_FUNC_CHECK_FLOPPY, 608, 0, 1);
    CHECK(error_code(cb) == 0, "FORMAT TRACK repaired the T16 sector");
    run_test(dev, 18, 0x01, (uint16_t)((38 << 8) | 1));
    run_command(dev, cb, FLOPPY_FUNC_CHECK_FLOPPY, 608, 0, 1);
    CHECK(error_code(cb) == FLOPPY_ERR_SECTOR_NOT_FOUND, "CHECK FLOPPY after T18: error 06");
    run_command(dev, cb, FLOPPY_FUNC_FORMAT_TRACK, 608, 0, 1);
    run_test(dev, 17, 0x01, (uint16_t)((38 << 8) | 1));
    run_command(dev, cb, FLOPPY_FUNC_CHECK_FLOPPY, 608, 0, 1);
    CHECK(error_code(cb) == FLOPPY_ERR_TRACK_NOT_FOUND, "CHECK FLOPPY after T17: error 07");
    run_command(dev, cb, FLOPPY_FUNC_FORMAT_TRACK, 608, 0, 1);
    run_command(dev, cb, FLOPPY_FUNC_CHECK_FLOPPY, 608, 0, 1);
    CHECK(error_code(cb) == 0, "FORMAT TRACK repaired the T17 track");

    /* --- 14. TPE PROVOKE-ERRORS codes 25, 30 and 43. --- */
    run_test(dev, 4, 0xFF, 0x2A00);  /* FFH = RST 38H at 2A00H */
    start_test(dev, 15, 0xFF, 0x2A00, false);
    CHECK((status(dev) & ST_RFT) == 0, "T15 into FFH: busy until the error completion");
    dev->Tick(dev);
    CHECK(((data_out(dev) >> 9) & 0x3F) == FLOPPY_ERR_MICROPROGRAM_OUT_OF_RANGE &&
              (status(dev) & ST_RFT) != 0 && (status(dev) & ST_OR) != 0,
          "T15 into FFH: error 025 microprogram out of range in IOX +0, RFT and OR set");
    run_test(dev, 4, 0x00, 0x2A00);  /* 00H = NOP: not runnable here, finishes clean */
    run_test(dev, 15, 0x00, 0x2A00);
    CHECK((status(dev) & ST_OR) == 0 && data_out(dev) == 0, "T15 into a NOP: finishes without error");
    run_command(dev, cb, FLOPPY_FUNC_CHECK_FLOPPY, 1232, 0, 1); /* track 77 */
    CHECK(error_code(cb) == FLOPPY_ERR_TRACK_OUT_OF_RANGE, "CHECK FLOPPY at track 77: error 030");
    run_command(dev, cb, FLOPPY_FUNC_READ_DATA, 1216, 0x4000, 1); /* track 76, last good */
    CHECK(error_code(cb) == 0, "read at track 76: in range");
    run_command(dev, cb, 0x3A, 608, 0x4000, 1);
    CHECK(error_code(cb) == FLOPPY_ERR_ILLEGAL_COMMAND && (status(dev) & ST_RFT) != 0,
          "function code 072: error 043 illegal command, RFT set");

    /* --- 15. Tests with no observable effect still finish. --- */
    {
        const int quiet[] = {7, 12, 15, 19, 20, 21, 25};
        int all_finished = 1;
        for (size_t i = 0; i < sizeof(quiet) / sizeof(quiet[0]); i++)
        {
            run_test(dev, quiet[i], 0x01, 0x0000);
            if ((status(dev) & ST_RFT) == 0 || data_out(dev) != 0)
            {
                all_finished = 0;
            }
        }
        CHECK(all_finished, "T7/T12/T15/T19/T20/T21/T25 finish with RFT set and data-out 0");
        run_test(dev, 8, 0x00, 0x0071);
        CHECK(data_out(dev) == 0 && (status(dev) & ST_RFT) != 0, "T8: no Z80 ports -> DLO 0");
    }

    if (dev->Destroy)
    {
        dev->Destroy(dev);
    }
    free(dev->deviceData);
    free(dev);

    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
