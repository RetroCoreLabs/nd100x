/*
 * test_winchester_units.c - the Winchester controller with both disk units.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * The Winchester card (ND-11.015.01) drives two units (control word bit 9).
 * These checks drive it one register write at a time, the way a driver does,
 * against two units with their own backing files and their own geometry:
 *
 *   - a read from unit N returns unit N's bytes, and a write through unit N
 *     changes unit N's file and no other;
 *   - each unit has its own arm, and a transfer only finds its sector on the
 *     cylinder that unit's arm is on (sec 2.2, 5.5: seek first);
 *   - each unit has its own geometry, so the same C/H/S is a different block
 *     on two drive types;
 *   - the last cylinder is maxCylinders - 1, and the step seek stops there;
 *   - master clear puts every arm on cylinder 0 (sec 3.4.8, 5.5);
 *   - a unit with no pack faults every operation, and a pack mounted later is
 *     seen;
 *   - Write Format's address fields and the bad track flag (sec 2.3, 3.4.6);
 *   - the test mode loop of sec 5.4, with the status words the manual gives;
 *   - a word count that is not a whole number of sectors.
 *
 * Every backing file is a temporary file (tmpfile()), never an image in the
 * repository. When ND100X_WD_TEST_IMAGE names a pack, unit 1 also gets a COPY
 * of it and is read through the controller and compared with the file.
 *
 * device_winchester.c is INCLUDED rather than linked so the per-unit state in
 * WinchesterData can be checked directly, as test_smd_units.c does.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "winchester/device_winchester.c"

/* The device tick clock (the CPU counts executed instructions here). The
 * Winchester model reads it for the position of the turning disk, so every
 * tick the test runs moves it on, as the CPU would. */
uint64_t g_instr_counter = 0;

/* ---- DMA memory -------------------------------------------------------- */
#define TEST_MEM_WORDS 65536
static uint16_t s_mem[TEST_MEM_WORDS];
int g_dma_access = 0;
int mms_read_physical_memory(int addr, bool priv)
{
    (void)priv;
    if (addr < 0 || addr >= TEST_MEM_WORDS)
    {
        return -1;
    }
    return s_mem[addr];
}
void mms_write_physical_memory(int addr, uint16_t v, bool priv)
{
    (void)priv;
    if (addr >= 0 && addr < TEST_MEM_WORDS)
    {
        s_mem[addr] = v;
    }
}

static int s_failed = 0;
static int s_checks = 0;

#define CHECK(cond, msg)                                                                 \
    do                                                                                   \
    {                                                                                    \
        s_checks++;                                                                      \
        if (!(cond))                                                                     \
        {                                                                                \
            printf("  FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);                   \
            s_failed++;                                                                  \
        }                                                                                \
    } while (0)

/* ---- the two units' backing files -------------------------------------- */
#define UNITS 2
#define BLOCK 1024
#define WORDS (BLOCK / 2)

typedef struct
{
    FILE *f;     /* NULL = no pack on this unit */
    size_t size; /* bytes */
} UnitFile;

static UnitFile s_units[UNITS];

static int unit_read(Device *dev, uint8_t *buffer, size_t blocks, uint32_t block_address, int unit)
{
    (void)dev;
    if (unit < 0 || unit >= UNITS || !s_units[unit].f)
    {
        return -1;
    }
    if ((size_t)(block_address + blocks) * BLOCK > s_units[unit].size)
    {
        return -1;
    }
    if (fseek(s_units[unit].f, (long)block_address * BLOCK, SEEK_SET) != 0)
    {
        return -1;
    }
    return (int)fread(buffer, BLOCK, blocks, s_units[unit].f);
}

static int unit_write(Device *dev, const uint8_t *buffer, size_t blocks, uint32_t block_address,
                      int unit)
{
    (void)dev;
    if (unit < 0 || unit >= UNITS || !s_units[unit].f)
    {
        return -1;
    }
    if ((size_t)(block_address + blocks) * BLOCK > s_units[unit].size)
    {
        return -1;
    }
    if (fseek(s_units[unit].f, (long)block_address * BLOCK, SEEK_SET) != 0)
    {
        return -1;
    }
    size_t put = fwrite(buffer, BLOCK, blocks, s_units[unit].f);
    fflush(s_units[unit].f);
    return (int)put;
}

static int unit_info(Device *dev, size_t *image_size, bool *is_write_protected, int unit)
{
    (void)dev;
    *is_write_protected = false;
    *image_size = (unit >= 0 && unit < UNITS && s_units[unit].f) ? s_units[unit].size : 0;
    return *image_size > 0 ? 0 : -1;
}

/* A synthetic pack: every word of block b on unit u is (u << 12) | b, so a
 * word from the wrong unit or the wrong block shows it. */
static void make_synthetic_unit(int unit, int blocks)
{
    FILE *f = tmpfile();
    if (!f)
    {
        printf("  cannot create a temporary file\n");
        exit(1);
    }
    uint8_t buf[BLOCK];
    for (int b = 0; b < blocks; b++)
    {
        uint16_t w = (uint16_t)((unit << 12) | (b & 0xFFF));
        for (int i = 0; i < BLOCK; i += 2)
        {
            buf[i] = (uint8_t)(w >> 8);
            buf[i + 1] = (uint8_t)w;
        }
        fwrite(buf, 1, BLOCK, f);
    }
    fflush(f);
    s_units[unit].f = f;
    s_units[unit].size = (size_t)blocks * BLOCK;
}

static bool copy_pack_to_unit(int unit, const char *path)
{
    FILE *src = fopen(path, "rb");
    if (!src)
    {
        return false;
    }
    FILE *f = tmpfile();
    if (!f)
    {
        fclose(src);
        return false;
    }
    static uint8_t buf[1 << 20];
    size_t total = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
    {
        if (fwrite(buf, 1, n, f) != n)
        {
            fclose(src);
            fclose(f);
            return false;
        }
        total += n;
    }
    fclose(src);
    fflush(f);
    s_units[unit].f = f;
    s_units[unit].size = total;
    return true;
}

static void close_units(void)
{
    for (int u = 0; u < UNITS; u++)
    {
        if (s_units[u].f)
        {
            fclose(s_units[u].f);
            s_units[u].f = NULL;
            s_units[u].size = 0;
        }
    }
}

static uint16_t file_word(int unit, uint32_t block, int word)
{
    uint8_t b[2] = {0, 0};
    fseek(s_units[unit].f, (long)block * BLOCK + 2L * word, SEEK_SET);
    (void)fread(b, 1, 2, s_units[unit].f);
    return (uint16_t)((b[0] << 8) | b[1]);
}

/* ---- driving the card --------------------------------------------------- */

/* Register offsets (ND-11.015.01 sec 3.1). */
#define R_MA      0
#define W_MA      1
#define W_BLOCK   3
#define R_STATUS  4
#define W_CONTROL 5
#define R_BLOCK   6
#define W_WC      7

/* Control word (sec 3.4). */
#define CW_ACTIVE      (1u << 2)
#define CW_TEST        (1u << 3)
#define CW_DEV_CLEAR   (1u << 4)
#define CW_OUT         (1u << 14) /* seek away from cylinder 0 */
#define CW_BAD_TRACK   (1u << 15)

/* Status (sec 3.5). */
#define ST_ACTIVE      (1u << 2)
#define ST_FINISHED    (1u << 3)
#define ST_DISK_FAULT  (1u << 7)
#define ST_ADDR_MISM   (1u << 8)
#define ST_CRC         (1u << 9)
#define ST_COMPARE     (1u << 10)
#define ST_ON_CYLINDER (1u << 14)

static Device *s_card;

static void wr(int reg, uint16_t v)
{
    s_card->Write(s_card, (uint32_t)(s_card->startAddress + reg), v);
}

static uint16_t rd(int reg)
{
    return (uint16_t)s_card->Read(s_card, (uint32_t)(s_card->startAddress + reg));
}

static uint16_t cw(int unit, int op, int head, uint16_t more)
{
    return (uint16_t)(((unit & 1) << 9) | ((op & 7) << 11) | ((head & 0xF) << 5) | more);
}

/* Run the card's queued completion; returns the status once not active. */
static uint16_t wait_done(void)
{
    for (int t = 0; t < 100000; t++)
    {
        uint16_t st = rd(R_STATUS);
        if (!(st & ST_ACTIVE))
        {
            return st;
        }
        (void)s_card->Tick(s_card);
        g_instr_counter++;
    }
    return 0xFFFF;
}

/* Ticks the card stays active from now on (0xFFFFFFFF if it never ends). */
static uint32_t ticks_active(void)
{
    for (uint32_t t = 0; t < 200000u; t++)
    {
        if (!(rd(R_STATUS) & ST_ACTIVE))
        {
            return t;
        }
        (void)s_card->Tick(s_card);
        g_instr_counter++;
    }
    return 0xFFFFFFFFu;
}

static void device_clear(void)
{
    wr(W_CONTROL, CW_DEV_CLEAR);
    (void)rd(R_STATUS);
}

/* M4: step `count` cylinders, outwards or towards 0. */
static uint16_t step(int unit, int count, bool out)
{
    wr(W_WC, (uint16_t)count);
    wr(W_CONTROL, cw(unit, WD_OP_SEEK, 0, (uint16_t)(CW_ACTIVE | (out ? CW_OUT : 0))));
    return wait_done();
}

static uint16_t rtz(int unit)
{
    wr(W_CONTROL, cw(unit, WD_OP_RETURN_TO_ZERO, 0, CW_ACTIVE));
    return wait_done();
}

static uint16_t transfer(int unit, int op, int cylinder, int head, int sector, uint32_t mem,
                         uint16_t words, uint16_t more)
{
    (void)rd(R_STATUS); /* sets the upper/lower flip-flop */
    wr(W_MA, (uint16_t)(mem >> 16));
    wr(W_MA, (uint16_t)mem);
    wr(W_BLOCK, (uint16_t)((cylinder << 5) | sector));
    wr(W_WC, words);
    wr(W_CONTROL, cw(unit, op, head, (uint16_t)(CW_ACTIVE | more)));
    return wait_done();
}

static WDDiskInfo *disk_of(int unit)
{
    return &((WinchesterData *)s_card->deviceData)->regs.disks[unit];
}

static uint32_t lba_of(int unit, int c, int h, int s)
{
    WDDiskInfo *d = disk_of(unit);
    return (uint32_t)((c * d->headsPrCylinder + h) * d->sectorsPrTrack + s);
}

static Device *new_card(void)
{
    Device *dev = wd_create_winchester_device(0);
    dev->blockCallbacks.readFunc = unit_read;
    dev->blockCallbacks.writeFunc = unit_write;
    dev->blockCallbacks.diskInfoFunc = unit_info;
    return dev;
}

/* ---- the checks -------------------------------------------------------- */

static void run_checks(void)
{
    /* Unit 0: a 1325 (8 heads, 9 sectors). Unit 1: forced to the DISC-14MB
     * layout (3 heads, 12 sectors), so the two units have different
     * geometry. Only the first 4 cylinders exist in the files. */
    wd_disk_preset_format(0, WD_DISK_TYPE_UNKNOWN);
    wd_disk_preset_format(1, WD_DISK_DT014);
    make_synthetic_unit(0, 4 * 8 * 9);
    make_synthetic_unit(1, 4 * 3 * 12);
    s_card = new_card();

    /* ---- the real 3041 trace --------------------------------------------
     * Taken on a real 3041 card with a Micropolis 1325 connected, DISC-TEMA
     * device start-up for DISC-74MB-1 (kept at the end of RetroCore's
     * NDBusDiscControllerWinchester.cs): after power-on,
     *   write 505 = 000011  -> read 504 = 060011
     *   write 505 = 100000  -> read 504 = 070010
     *   write 505 = 000000  -> read 504 = 070010
     * A measurement of the hardware, so the card must answer the same.
     * Bit 12 is TR00 from the selected drive (wd_read_status): 000011 sets
     * test mode (bit 3), which selects no drive, so it reads 0; the next two
     * select unit 0, whose heads are on cylinder 0 after power-on, so 1. */
    {
        s_card->Reset(s_card);
        wr(W_CONTROL, 000011);
        uint16_t st1 = rd(R_STATUS);
        wr(W_CONTROL, 0100000);
        uint16_t st2 = rd(R_STATUS);
        wr(W_CONTROL, 0);
        uint16_t st3 = rd(R_STATUS);
        char msg[160];
        snprintf(msg, sizeof(msg), "real 3041: control 000011 gives status 060011 (got %06o)", st1);
        CHECK(st1 == 060011, msg);
        snprintf(msg, sizeof(msg), "real 3041: control 100000 gives status 070010 (got %06o)", st2);
        CHECK(st2 == 070010, msg);
        snprintf(msg, sizeof(msg), "real 3041: control 000000 then gives status 070010 (got %06o)", st3);
        CHECK(st3 == 070010, msg);
        device_clear();
    }

    /* ---- every unit reads its own file, at its own geometry ------------ */
    for (int u = 0; u < UNITS; u++)
    {
        uint16_t st = transfer(u, WD_OP_READ_TRANSFER, 0, 1, 3, 0x100, WORDS, 0);
        uint32_t b = lba_of(u, 0, 1, 3);
        char msg[120];
        snprintf(msg, sizeof(msg), "unit %d: C/H/S 0/1/3 is block %u of its own file", u, b);
        CHECK(st != 0xFFFF && !(st & 0x0FF0) && s_mem[0x100] == (uint16_t)((u << 12) | b) &&
                  s_mem[0x100 + WORDS - 1] == (uint16_t)((u << 12) | b),
              msg);
    }
    CHECK(lba_of(0, 0, 1, 3) != lba_of(1, 0, 1, 3),
          "the two drive types put C/H/S 0/1/3 in different blocks");

    /* ---- each unit has its own arm ------------------------------------- */
    CHECK(!(step(0, 2, true) & 0x0FF0) && disk_of(0)->cylinder == 2 && disk_of(1)->cylinder == 0,
          "a seek on unit 0 moves unit 0's arm only");
    {
        uint16_t st = transfer(0, WD_OP_READ_TRANSFER, 0, 0, 0, 0x100, WORDS, 0);
        CHECK(st & ST_ADDR_MISM, "unit 0 at cylinder 2: a read of cylinder 0 is address mismatch "
                                 "(sec 5.5: seek first)");
        device_clear();
        st = transfer(0, WD_OP_READ_TRANSFER, 2, 4, 5, 0x100, WORDS, 0);
        CHECK(!(st & 0x0FF0) && s_mem[0x100] == (uint16_t)lba_of(0, 2, 4, 5),
              "unit 0 at cylinder 2: a read of cylinder 2 returns that block");
        st = transfer(1, WD_OP_READ_TRANSFER, 0, 2, 11, 0x100, WORDS, 0);
        CHECK(!(st & 0x0FF0) && s_mem[0x100] == (uint16_t)((1 << 12) | lba_of(1, 0, 2, 11)),
              "unit 1 is still on cylinder 0 and reads there");
    }

    /* ---- a write lands on its own unit only ---------------------------- */
    {
        for (int i = 0; i < WORDS; i++)
        {
            s_mem[0x400 + i] = (uint16_t)(0xA000 + i);
        }
        uint32_t b1 = lba_of(1, 0, 0, 7);
        uint16_t other = file_word(0, b1, 0);
        uint16_t st = transfer(1, WD_OP_WRITE_TRANSFER, 0, 0, 7, 0x400, WORDS, 0);
        CHECK(!(st & 0x0FF0) && file_word(1, b1, 0) == 0xA000 &&
                  file_word(1, b1, WORDS - 1) == (uint16_t)(0xA000 + WORDS - 1),
              "a write through unit 1 reaches unit 1's file");
        CHECK(file_word(0, b1, 0) == other, "the same block on unit 0 is untouched");
        st = transfer(1, WD_OP_COMPARE, 0, 0, 7, 0x400, WORDS, 0);
        CHECK(!(st & ST_COMPARE), "compare of the written block finds no difference");
        s_mem[0x400 + 9] ^= 1;
        st = transfer(1, WD_OP_COMPARE, 0, 0, 7, 0x400, WORDS, 0);
        CHECK(st & ST_COMPARE, "compare after one memory bit changed sets compare error");
        device_clear();
    }

    /* ---- several sectors go on to the next head ------------------------ */
    {
        uint16_t st = transfer(0, WD_OP_READ_TRANSFER, 2, 0, 8, 0x1000, 2 * WORDS, 0);
        CHECK(!(st & 0x0FF0) && s_mem[0x1000] == (uint16_t)lba_of(0, 2, 0, 8) &&
                  s_mem[0x1000 + WORDS] == (uint16_t)lba_of(0, 2, 1, 0),
              "two sectors from the last sector of a track: the second is head 1, sector 0");
        st = transfer(0, WD_OP_READ_TRANSFER, 2, 7, 8, 0x1000, 2 * WORDS, 0);
        CHECK(st & ST_ADDR_MISM,
              "a transfer that runs off the arm's cylinder ends in address mismatch");
        device_clear();
    }

    /* ---- a word count that is not whole sectors ------------------------ */
    {
        for (int i = 0; i < 2 * WORDS; i++)
        {
            s_mem[0x2000 + i] = 0x5555;
        }
        uint16_t st = transfer(0, WD_OP_READ_TRANSFER, 2, 3, 1, 0x2000, 600, 0);
        CHECK(!(st & 0x0FF0) && s_mem[0x2000 + 512] == (uint16_t)lba_of(0, 2, 3, 2) &&
                  s_mem[0x2000 + 599] == (uint16_t)lba_of(0, 2, 3, 2) &&
                  s_mem[0x2000 + 600] == 0x5555,
              "600 words: 512 from the first sector, 88 from the second, nothing after");
        st = transfer(0, WD_OP_READ_TRANSFER, 2, 3, 1, 0x2000, 100, 0);
        CHECK(!(st & 0x0FF0) && s_mem[0x2000] == (uint16_t)lba_of(0, 2, 3, 1),
              "100 words: they are read (a short count used to move nothing)");
    }

    /* ---- the last cylinder --------------------------------------------- */
    {
        WDDiskInfo *d = disk_of(1);
        (void)step(1, 5000, true);
        CHECK(d->cylinder == d->maxCylinders - 1, "a step seek stops on cylinder maxCylinders - 1");
        uint16_t st = transfer(1, WD_OP_READ_TRANSFER, d->maxCylinders, 0, 0, 0x100, WORDS, 0);
        CHECK(st & ST_ADDR_MISM, "cylinder maxCylinders does not exist: address mismatch");
        device_clear();
    }

    /* ---- master clear puts every arm on cylinder 0 --------------------- */
    {
        CHECK(disk_of(0)->cylinder != 0 && disk_of(1)->cylinder != 0, "both arms are away from 0");
        s_card->Reset(s_card);
        CHECK(disk_of(0)->cylinder == 0 && disk_of(1)->cylinder == 0,
              "master clear: both arms on cylinder 0 (sec 3.4.8)");
        uint16_t st = transfer(1, WD_OP_READ_TRANSFER, 0, 0, 0, 0x100, WORDS, 0);
        CHECK(!(st & 0x0FF0), "after master clear unit 1 reads cylinder 0 without a seek");
    }

    /* ---- Write Format: address fields and the bad track flag ----------- */
    {
        (void)rtz(0);
        (void)step(0, 1, true); /* cylinder 1, head 2 */
        int spt = disk_of(0)->sectorsPrTrack;
        for (int s = 0; s < spt; s++)
        {
            s_mem[0x3000 + 2 * s] = (uint16_t)(2 | (s == 3 ? WD_FIELD_BAD_TRACK_FLAG : 0));
            s_mem[0x3000 + 2 * s + 1] = (uint16_t)((1 << 5) | s);
        }
        uint16_t st = transfer(0, WD_OP_WRITE_FORMAT, 1, 2, 0, 0x3000, (uint16_t)(2 * spt), 0);
        CHECK(!(st & 0x0FF0), "Write Format of cylinder 1 head 2 ends without error");
        st = transfer(0, WD_OP_READ_TRANSFER, 1, 2, 3, 0x100, WORDS, 0);
        CHECK(st & ST_ADDR_MISM, "the sector with the bad track flag: address mismatch (sec 2.3)");
        device_clear();

        /* The SINTRAN driver's recovery: device clear, read the block address
         * (IOX 506), "register - 1" is the failing sector (BUGS.md W7). Two
         * sectors from sector 2: sector 2 is found, sector 3 fails. */
        st = transfer(0, WD_OP_READ_TRANSFER, 1, 2, 2, 0x100, 2 * WORDS, 0);
        CHECK(st & ST_ADDR_MISM, "two sectors over the flagged one: address mismatch");
        device_clear();
        CHECK(rd(R_BLOCK) == (uint16_t)((1 << 5) | 4),
              "after the mismatch and a device clear the block address is failing sector + 1");
        st = transfer(0, WD_OP_READ_TRANSFER, 1, 2, 5, 0x100, 2 * WORDS, 0);
        CHECK(!(st & 0x0FF0) && rd(R_BLOCK) == (uint16_t)((1 << 5) | 7),
              "after a good transfer of 2 sectors from sector 5 the block address is sector 7");
        st = transfer(0, WD_OP_READ_TRANSFER, 1, 2, 3, 0x100, WORDS, CW_BAD_TRACK);
        CHECK(!(st & 0x0FF0) && s_mem[0x100] == (uint16_t)lba_of(0, 1, 2, 3),
              "with control word bit 15 the flagged sector is read");
        st = transfer(0, WD_OP_READ_TRANSFER, 1, 2, 4, 0x100, WORDS, 0);
        CHECK(!(st & 0x0FF0), "an unflagged sector on the same track reads");
        st = transfer(0, WD_OP_READ_TRANSFER, 1, 3, 3, 0x100, WORDS, 0);
        CHECK(!(st & 0x0FF0), "the same sector on another head was not formatted and reads");

        /* Sectors numbered 040 and up: no sector 0 on the track any more. */
        for (int s = 0; s < spt; s++)
        {
            s_mem[0x3000 + 2 * s] = 2;
            s_mem[0x3000 + 2 * s + 1] = (uint16_t)((1 << 5) | ((s + 16) & 0x1F));
        }
        (void)transfer(0, WD_OP_WRITE_FORMAT, 1, 2, 0, 0x3000, (uint16_t)(2 * spt), 0);
        st = transfer(0, WD_OP_READ_TRANSFER, 1, 2, 0, 0x100, WORDS, CW_BAD_TRACK);
        CHECK(st & ST_ADDR_MISM, "a track without the wanted sector address: address mismatch");
        device_clear();

        /* Natural fields again: the track reads as before. */
        for (int s = 0; s < spt; s++)
        {
            s_mem[0x3000 + 2 * s] = 2;
            s_mem[0x3000 + 2 * s + 1] = (uint16_t)((1 << 5) | s);
        }
        (void)transfer(0, WD_OP_WRITE_FORMAT, 1, 2, 0, 0x3000, (uint16_t)(2 * spt), 0);
        st = transfer(0, WD_OP_READ_TRANSFER, 1, 2, 3, 0x100, WORDS, 0);
        CHECK(!(st & 0x0FF0) && disk_of(0)->formattedTrackCount == 0,
              "reformatted with natural fields: the track reads and is forgotten");
    }

    /* ---- test mode, the loop of sec 5.4 -------------------------------- */
    {
        /* 13/BLOCK 125252, 14/MEMUP 0, 15/MEMLO 1000, 16/WORDC 1000,
         * 17/CONTR 100254. A 3041 also has bit 13, so 41030 reads 61030. */
        memset(&s_mem[01000], 0, 01000 * sizeof(uint16_t));
        wr(W_CONTROL, 020);
        wr(W_BLOCK, 0125252);
        wr(W_MA, 0);
        wr(W_MA, 01000);
        wr(W_WC, 01000);
        wr(W_CONTROL, 0100254);
        uint16_t st = wait_done();
        CHECK(s_mem[01000] == 0052525 && s_mem[01001] == 0125252 && s_mem[01776] == 0052525 &&
                  s_mem[01777] == 0125252,
              "test mode M0: memory 1000-1777 holds 052525, 125252, ... (sec 5.4)");
        CHECK(st == 061030, "test mode M0: status 41030, plus bit 13 on a 3041");

        wr(W_CONTROL, 020);
        wr(W_BLOCK, 0125252);
        wr(W_MA, 0);
        wr(W_MA, 01000);
        wr(W_WC, 01000);
        wr(W_CONTROL, 0114254);
        st = wait_done();
        CHECK(st == 061030, "test mode, control word 114254: status still 41030 (+ bit 13)");

        s_mem[01234] ^= 0100;
        wr(W_CONTROL, 020);
        wr(W_BLOCK, 0125252);
        wr(W_MA, 0);
        wr(W_MA, 01000);
        wr(W_WC, 01000);
        wr(W_CONTROL, 0114254);
        st = wait_done();
        CHECK(st == 063030, "one memory word changed: status 43030 (+ bit 13), compare error");
        device_clear();
    }

    /* ---- the drive's time, and Read Parity's counters ------------------ */
    {
        WinchesterData *wd = (WinchesterData *)s_card->deviceData;
        int arm_before = disk_of(0)->cylinder; /* put back at the end */
        device_clear();
        (void)rtz(0);

        /* M4, 3 cylinders out: sec 3.4.5 "completed when the heads are settled
         * over the correct tracks" - active and NOT on cylinder meanwhile. */
        wr(W_WC, 3);
        wr(W_CONTROL, cw(0, WD_OP_SEEK, 0, (uint16_t)(CW_ACTIVE | CW_OUT)));
        uint16_t st = rd(R_STATUS);
        CHECK((st & ST_ACTIVE) && !(st & ST_ON_CYLINDER),
              "M4: active and not on cylinder straight after the activate");
        uint32_t t = ticks_active();
        uint32_t want = wd_seek_ticks(disk_of(0), 3);
        CHECK(t + 1u >= want && t <= want + 1u,
              "M4 of 3 cylinders takes the seek time of the Micropolis curve");
        CHECK(rd(R_STATUS) & ST_ON_CYLINDER, "M4: on cylinder again once the heads have settled");

        /* M7 from cylinder 3 takes as long as a 3-cylinder seek. */
        wr(W_CONTROL, cw(0, WD_OP_RETURN_TO_ZERO, 0, CW_ACTIVE));
        CHECK(!(rd(R_STATUS) & ST_ON_CYLINDER), "M7: not on cylinder while moving");
        t = ticks_active();
        CHECK(t + 1u >= want && t <= want + 1u && disk_of(0)->cylinder == 0,
              "M7 from cylinder 3 takes the 3-cylinder seek time and ends on cylinder 0");

        /* A one-sector read lasts at least one sector time and at most a
         * revolution plus one sector (the wait for it to come round). */
        (void)rd(R_STATUS);
        wr(W_MA, 0);
        wr(W_MA, 0x100);
        wr(W_BLOCK, (uint16_t)((0 << 5) | 4));
        wr(W_WC, WORDS);
        wr(W_CONTROL, cw(0, WD_OP_READ_TRANSFER, 0, CW_ACTIVE));
        t = ticks_active();
        CHECK(t >= wd_sector_ticks(disk_of(0)) && t <= WD_TICKS_PER_REV + wd_sector_ticks(disk_of(0)),
              "a one-sector read keeps the card active for the disk's time, not 10 ticks");

        /* M2, 2 sectors: no memory cycles, but the address and the word count
         * run through (DISC-TEMA PARITY-CHECK and the SINTRAN driver both read
         * the address back; BUGS.md W8). */
        s_mem[0x400] = 0xBEEF;
        s_mem[0x400 + 2 * WORDS - 1] = 0xCAFE;
        st = transfer(0, WD_OP_READ_PARITY, 0, 2, 0, 0x400, (uint16_t)(2 * WORDS), 0);
        CHECK(!(st & 0x0FF0) && s_mem[0x400] == 0xBEEF && s_mem[0x400 + 2 * WORDS - 1] == 0xCAFE,
              "M2 moves no data to memory (sec 3.4.3)");
        CHECK(wd->regs.memoryAddress == (uint16_t)(0x400 + 2 * WORDS) && wd->regs.wordCounter == 0,
              "M2 leaves the memory address at start + word count and the word count at 0");
        device_clear();
        if (arm_before > 0)
        {
            (void)step(0, arm_before, true);
        }
    }

    /* ---- a unit with no pack, and a pack mounted later ------------------ */
    {
        fclose(s_units[1].f);
        s_units[1].f = NULL;
        s_units[1].size = 0;
        uint16_t st = transfer(1, WD_OP_READ_TRANSFER, 0, 0, 0, 0x100, WORDS, 0);
        CHECK((st & ST_DISK_FAULT) && !(st & ST_ACTIVE),
              "empty unit 1: a read ends with disk fault and the card not active");
        device_clear();
        st = step(1, 3, true);
        CHECK((st & ST_DISK_FAULT) && !(st & ST_ON_CYLINDER),
              "empty unit 1: a seek ends with disk fault, not on cylinder");
        device_clear();
        st = transfer(0, WD_OP_READ_TRANSFER, 1, 0, 0, 0x100, WORDS, 0);
        CHECK(!(st & 0x0FF0), "unit 0 is not affected by the empty unit 1");

        wd_disk_preset_format(1, WD_DISK_TYPE_UNKNOWN);
        make_synthetic_unit(1, 2 * 8 * 9);
        s_card->Reset(s_card);
        st = transfer(1, WD_OP_READ_TRANSFER, 0, 1, 1, 0x100, WORDS, 0);
        CHECK(!(st & 0x0FF0) && s_mem[0x100] == (uint16_t)((1 << 12) | lba_of(1, 0, 1, 1)),
              "a pack mounted on unit 1 later is read, at the geometry of its size");
    }

    dev_destroy(s_card); /* as devmgr_destroy: the card's data and I/O delays, then the Device */
    free(s_card);
    s_card = NULL;
    close_units();
}

/* A copy of a real pack on unit 1: read blocks through the controller, with
 * the seeks a driver would do, and compare them with the file. */
static void run_real_pack(const char *image)
{
    wd_disk_preset_format(0, WD_DISK_TYPE_UNKNOWN);
    wd_disk_preset_format(1, WD_DISK_TYPE_UNKNOWN);
    make_synthetic_unit(0, 72);
    bool ok = copy_pack_to_unit(1, image);
    CHECK(ok, "the pack named by ND100X_WD_TEST_IMAGE can be copied to a temporary file");
    if (!ok)
    {
        close_units();
        return;
    }
    s_card = new_card();
    WDDiskInfo *d = disk_of(1);
    (void)rd(R_STATUS);
    (void)transfer(1, WD_OP_READ_TRANSFER, 0, 0, 0, 0x100, WORDS, 0); /* sets the geometry */
    int cyls[3] = {0, d->maxCylinders / 2, d->maxCylinders - 2};
    int at = 0;
    bool all = true;
    for (int i = 0; i < 3; i++)
    {
        if (cyls[i] != at)
        {
            (void)step(1, cyls[i] > at ? cyls[i] - at : at - cyls[i], cyls[i] > at);
            at = cyls[i];
        }
        uint32_t b = lba_of(1, cyls[i], d->headsPrCylinder - 1, d->sectorsPrTrack - 1);
        if ((size_t)(b + 1) * BLOCK > s_units[1].size)
        {
            continue;
        }
        uint16_t st = transfer(1, WD_OP_READ_TRANSFER, cyls[i], d->headsPrCylinder - 1,
                               d->sectorsPrTrack - 1, 0x100, WORDS, 0);
        for (int w = 0; w < WORDS && !(st & 0x0FF0); w++)
        {
            if (s_mem[0x100 + w] != file_word(1, b, w))
            {
                all = false;
                break;
            }
        }
        if (st & 0x0FF0)
        {
            all = false;
        }
    }
    CHECK(all, "real pack on unit 1: the last sector of three cylinders matches the file");
    dev_destroy(s_card); /* as devmgr_destroy: the card's data and I/O delays, then the Device */
    free(s_card);
    s_card = NULL;
    close_units();
}

int main(void)
{
    printf("ND-100 Winchester controller: two disk units\n");
    printf("============================================\n\n");

    run_checks();

    const char *image = getenv("ND100X_WD_TEST_IMAGE");
    if (image && image[0])
    {
        run_real_pack(image);
    }
    else
    {
        printf("NOT RUN: the real-pack check (set ND100X_WD_TEST_IMAGE to a pack image)\n");
    }

    printf("\n%d checks, %d failed\n", s_checks, s_failed);
    printf("TB_RESULT: %s\n", s_failed == 0 ? "PASS" : "FAIL");
    return s_failed == 0 ? 0 : 1;
}
