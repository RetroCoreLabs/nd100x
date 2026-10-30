/*
 * test_smd_units.c - the SMD controller with more than one disk unit.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * SINTRAN with several SMD units had never been checked in the emulators.
 * On 2026-10-29 an @ENTER-DIRECTORY of a pack on unit 1 never came back,
 * while the same pack on unit 0 worked. These checks drive the controller
 * exactly the way the SINTRAN driver does, one register write at a time,
 * against four units that each have their own backing file:
 *
 *   - a read from unit N returns unit N's bytes, for N = 0..3;
 *   - a write through unit N changes unit N's file and no other;
 *   - the seek sequence (Initiate Seek, Seek Complete Search, transfer)
 *     keeps a seek-complete bit per unit, so seeks on two units can be
 *     outstanding at once;
 *   - a unit with no pack reads NOT READY, and a GO against it is a
 *     hardware error that still ends the operation;
 *   - a pack mounted AFTER the controller first looked at the unit is seen.
 *
 * Every backing file is a temporary file (tmpfile()), never an image in the
 * repository. When ND100X_SMD_TEST_IMAGE names a pack, units 1 and 3 get a
 * COPY of it so the same checks run on real SINTRAN pages; without it they
 * run on small synthetic files, and the result line says which it was.
 *
 * device_smd.c is INCLUDED rather than linked so the per-unit state in the
 * file-local SMDData can be checked directly, as test_smd.c does.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "smd/device_smd.c"

/* The device tick clock: the CPU counts executed instructions here, and the
 * drive model reads it to know where the turning pack is. */
uint64_t g_instr_counter = 0;

/* The longest single operation the drive model can take: a full-stroke seek,
 * a whole revolution of waiting, and a few tracks of transfer. Every wait in
 * this file uses it, so none of them depends on the model's exact figures. */
#define OP_LIMIT (SMD_SEEK_FULL_STROKE_TICKS + 8u * SMD_TICKS_PER_REV)

/* ---- DMA memory: the controller reads and writes physical memory through
 * these two. A real array, so a read transfer can be checked word by word. */
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

/* ---- the four units' backing files ------------------------------------- */
#define UNITS 4
#define BLOCK 1024

typedef struct
{
    FILE *f;      /* NULL = no pack on this unit */
    size_t size;  /* bytes */
} UnitFile;

static UnitFile s_units[UNITS];

static int unit_read(Device *dev, uint8_t *buffer, size_t blocks, uint32_t block_address,
                     int unit)
{
    (void)dev;
    if (unit < 0 || unit >= UNITS || !s_units[unit].f)
    {
        return -1;
    }
    if (fseek(s_units[unit].f, (long)block_address * BLOCK, SEEK_SET) != 0)
    {
        return -1;
    }
    size_t got = fread(buffer, BLOCK, blocks, s_units[unit].f);
    return (int)got;
}

static int unit_write(Device *dev, const uint8_t *buffer, size_t blocks, uint32_t block_address,
                      int unit)
{
    (void)dev;
    if (unit < 0 || unit >= UNITS || !s_units[unit].f)
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

/* A synthetic pack: `blocks` blocks where every word is (unit << 12) | block,
 * so a word read through the wrong unit or from the wrong block shows it. */
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

/* A temporary copy of a real pack. Returns false if the file cannot be read. */
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

/* Read `blocks` blocks straight from a unit's file, bypassing the controller. */
static void file_read(int unit, uint32_t block_address, uint8_t *out, int blocks)
{
    memset(out, 0, (size_t)blocks * BLOCK);
    if (!s_units[unit].f)
    {
        return;
    }
    fseek(s_units[unit].f, (long)block_address * BLOCK, SEEK_SET);
    (void)fread(out, BLOCK, (size_t)blocks, s_units[unit].f);
}

/* ---- driving the card the way the SINTRAN driver does ------------------- */

/* Register offsets from the card's base (ND-11.020.01 sec 2). */
#define R_MAR      0
#define W_MAR      1
#define R_SEEK     2
#define W_BLOCK    3
#define R_STATUS   4
#define W_CONTROL  5
#define R_BLOCK    6
#define W_WC       7

#define CW_INT_NOT_ACTIVE 0000001
#define CW_ACTIVE         0000004
#define CW_DEVICE_CLEAR   0000020
#define CW_MULTIPLEX      0100000

#define ST_ACTIVE          (1 << 2)
#define ST_HW_ERROR        (1 << 4)
#define ST_ILLEGAL_LOAD    (1 << 5)
#define ST_HW_ERROR2       (1 << 7)
#define ST_NOT_READY       (1 << 13)
#define ST_ON_CYLINDER     (1 << 14)

static Device *s_card;

static void wr(int reg, uint16_t v)
{
    s_card->Write(s_card, (int)(s_card->startAddress + reg), v);
}

static uint16_t rd(int reg)
{
    return (uint16_t)s_card->Read(s_card, (int)(s_card->startAddress + reg));
}

static uint16_t control_word(int unit, int op, uint16_t more)
{
    return (uint16_t)(((unit & 7) << 7) | ((op & 017) << 11) | more);
}

/* Let the card's queued completion run, as the CPU's instruction loop would.
 * Returns the number of ticks it took for ACTIVE to drop, or -1. */
static int run_until_not_active(int limit)
{
    for (int t = 0; t < limit; t++)
    {
        if (!(rd(R_STATUS) & ST_ACTIVE))
        {
            return t;
        }
        g_instr_counter++;
        (void)dev_tick(s_card);
    }
    return -1;
}

static void device_clear(void)
{
    wr(W_CONTROL, CW_DEVICE_CLEAR);
    (void)rd(R_STATUS);
}

/* Select a unit (a control word without GO) and read the status for it. */
static uint16_t status_of_unit(int unit)
{
    wr(W_CONTROL, control_word(unit, 0, 0));
    return rd(R_STATUS);
}

/* The driver's seek: Initiate Seek to a cylinder, then Seek Complete Search.
 * Returns the status word after the search ended. */
static uint16_t seek_unit(int unit, int cylinder)
{
    wr(W_CONTROL, control_word(unit, 0, 0));
    wr(W_BLOCK, 0);                                   /* block address I: head 0, sector 0 */
    wr(W_CONTROL, control_word(unit, 0, CW_MULTIPLEX));
    wr(W_BLOCK, (uint16_t)cylinder);                  /* block address II: cylinder */
    wr(W_CONTROL, control_word(unit, DEVICE_OP_INITIATE_SEEK, CW_ACTIVE));
    if (run_until_not_active((int)OP_LIMIT) < 0)
    {
        return 0xFFFF;
    }
    wr(W_CONTROL, control_word(unit, DEVICE_OP_SEEK_COMPLETE_SEARCH, CW_ACTIVE));
    if (run_until_not_active((int)(SMD_TIMEOUT_TICKS + OP_LIMIT)) < 0)
    {
        return 0xFFFF;
    }
    return rd(R_STATUS);
}

/* One transfer of `words` words between memory address `mem` and the block at
 * (cylinder, head, sector) on `unit`. Returns the status after it ended, or
 * 0xFFFF if the controller never went not-active. */
static uint16_t transfer(int unit, int op, int cylinder, int head, int sector, uint32_t mem,
                         uint32_t words)
{
    wr(W_CONTROL, control_word(unit, 0, 0));
    (void)rd(R_STATUS);                               /* resets the MAR/WC flip-flop */
    wr(W_MAR, (uint16_t)(mem >> 16));
    wr(W_MAR, (uint16_t)mem);
    wr(W_BLOCK, (uint16_t)((head << 8) | sector));
    wr(W_CONTROL, control_word(unit, 0, CW_MULTIPLEX));
    wr(W_BLOCK, (uint16_t)cylinder);
    wr(W_CONTROL, control_word(unit, 0, 0));
    wr(W_WC, (uint16_t)(words >> 16));
    wr(W_WC, (uint16_t)words);
    wr(W_CONTROL, control_word(unit, op, CW_ACTIVE));
    if (run_until_not_active((int)OP_LIMIT) < 0)
    {
        return 0xFFFF;
    }
    return rd(R_STATUS);
}

/* The block number the card computes for a CHS on the selected unit, from the
 * unit's own geometry. Kept here so a check can read the file at the same
 * place the card did. */
static uint32_t lba_of(int unit, int cylinder, int head, int sector)
{
    SMDData *data = (SMDData *)s_card->deviceData;
    DiskInfo *d = &data->regs.disks[unit];
    return (uint32_t)((cylinder * d->headsPrCylinder + head) * d->sectorsPrTrack + sector);
}

static bool mem_equals_file(int unit, uint32_t block_address, uint32_t mem, int blocks)
{
    static uint8_t buf[64 * BLOCK];
    file_read(unit, block_address, buf, blocks);
    for (int i = 0; i < blocks * BLOCK / 2; i++)
    {
        uint16_t w = (uint16_t)((buf[2 * i] << 8) | buf[2 * i + 1]);
        if (s_mem[mem + (uint32_t)i] != w)
        {
            return false;
        }
    }
    return true;
}

/* ---- the checks, run once per backing-file set ------------------------- */

static void run_checks(const char *what, const bool present[UNITS])
{
    printf("%s\n", what);
    uint8_t before[2][BLOCK];
    uint8_t after[BLOCK];

    s_card = smd_create_device(0);
    CHECK(s_card != NULL, "a card is created");
    if (!s_card)
    {
        return;
    }
    dev_set_block_read(s_card, unit_read, NULL);
    dev_set_block_write(s_card, unit_write, NULL);
    dev_set_block_disk_info(s_card, unit_info, NULL);
    device_clear();

    /* ---- status per unit: ready with a pack, not ready without ---------- */
    for (int u = 0; u < UNITS; u++)
    {
        uint16_t st = status_of_unit(u);
        char msg[96];
        snprintf(msg, sizeof(msg), "unit %d status: %s", u,
                 present[u] ? "READY and ON CYLINDER" : "NOT READY");
        if (present[u])
        {
            CHECK(!(st & ST_NOT_READY) && (st & ST_ON_CYLINDER), msg);
        }
        else
        {
            CHECK((st & ST_NOT_READY) != 0, msg);
        }
    }

    /* ---- a read from each unit returns that unit's bytes --------------- */
    for (int u = 0; u < UNITS; u++)
    {
        if (!present[u])
        {
            continue;
        }
        uint32_t mem = 0x1000 + (uint32_t)u * 0x800;
        memset(&s_mem[mem], 0xEE, 2 * BLOCK);
        uint16_t st = transfer(u, DEVICE_OP_READ_TRANSFER, 0, 0, 0, mem, BLOCK);
        char msg[96];
        snprintf(msg, sizeof(msg), "unit %d: read of block 0 ends without error", u);
        CHECK(st != 0xFFFF && !(st & ST_HW_ERROR), msg);
        snprintf(msg, sizeof(msg), "unit %d: the two blocks in memory are unit %d's block 0-1", u,
                 u);
        CHECK(mem_equals_file(u, 0, mem, 2), msg);
    }

    /* ---- the SINTRAN driver's seek on two units, both outstanding ------ */
    {
        int first = -1, second = -1;
        for (int u = 0; u < UNITS; u++)
        {
            if (present[u])
            {
                if (first < 0)
                    first = u;
                else if (second < 0)
                    second = u;
            }
        }
        if (first >= 0 && second >= 0)
        {
            SMDData *data = (SMDData *)s_card->deviceData;
            int cyl1 = 5, cyl2 = 7;
            uint16_t st1 = seek_unit(first, cyl1);
            CHECK(st1 != 0xFFFF && !(st1 & ST_HW_ERROR), "seek on the first unit completes");
            uint16_t seekc = rd(R_SEEK);
            CHECK((seekc & (1 << first)) != 0, "seek condition: first unit's bit is set");
            uint16_t st2 = seek_unit(second, cyl2);
            CHECK(st2 != 0xFFFF && !(st2 & ST_HW_ERROR), "seek on the second unit completes");
            /* The search is "independent of the unit select code" and the
             * first unit's pulse "will appear once per revolution" until a
             * transfer consumes it (ND-11.020.01 sec 2.3, M6), so this search
             * may well have ended on the FIRST unit's pulse while the second
             * unit's heads are still moving. The driver (WSEEK) reads the seek
             * condition to see which unit it was; here we wait for the second
             * unit's own pulse, which must arrive within its seek time. */
            {
                int t;
                for (t = 0; t < (int)OP_LIMIT; t++)
                {
                    if (rd(R_SEEK) & (1 << second))
                        break;
                    g_instr_counter++;
                    (void)dev_tick(s_card);
                }
            }
            seekc = rd(R_SEEK);
            CHECK((seekc & (1 << second)) != 0, "seek condition: second unit's bit is set");
            CHECK((seekc & (1 << first)) != 0,
                  "seek condition: the first unit's bit survives the second unit's seek");
            CHECK(((seekc >> 8) & 7) == second, "seek condition bits 8-10 name the unit selected");
            CHECK(data->regs.disks[first].onCylinder && data->regs.disks[second].onCylinder,
                  "both units are on cylinder after their searches");

            /* A transfer on the first unit consumes its seek, the second's stays. */
            uint32_t mem = 0x4000;
            uint16_t st = transfer(first, DEVICE_OP_READ_TRANSFER, cyl1, 0, 0, mem, BLOCK / 2);
            CHECK(st != 0xFFFF && !(st & ST_HW_ERROR), "read on the first unit after its seek");
            CHECK(mem_equals_file(first, lba_of(first, cyl1, 0, 0), mem, 1),
                  "the block read on the first unit is the block at its cylinder");
            seekc = rd(R_SEEK);
            CHECK((seekc & (1 << first)) == 0, "the first unit's seek-complete bit is consumed");
            CHECK((seekc & (1 << second)) != 0, "the second unit's seek-complete bit remains");

            st = transfer(second, DEVICE_OP_READ_TRANSFER, cyl2, 1, 3, mem, BLOCK / 2);
            CHECK(st != 0xFFFF && !(st & ST_HW_ERROR), "read on the second unit after its seek");
            CHECK(mem_equals_file(second, lba_of(second, cyl2, 1, 3), mem, 1),
                  "the block read on the second unit is the block at its cylinder/head/sector");
        }
        else
        {
            printf("  (fewer than two units present: the two-unit seek check is not run)\n");
        }
    }

    /* ---- the SINTRAN driver's seek, exactly as BSEEK issues it -----------
     * (IP-P2-DISK-START.NPL:436-467 and WSEEK 055473-055526): Initiate Seek
     * (M4, no interrupt), then
     * WSEEK's poll of the status register until the controller is not busy
     * and finished (at most 101 polls), then the control word 030005 |
     * unit<<7 (M6 seek complete search, active, interrupt enable), then a
     * wait for the level-11 interrupt. ND-11.020.01 on M4: "as soon as this
     * function is accepted by the disk, the operation will be completed", so
     * the poll ends long before the drive has moved its heads. */
    {
        int u = -1;
        for (int k = UNITS - 1; k >= 0; k--)
        {
            if (present[k])
            {
                u = k;
                break;
            }
        }
        int cyl = 11;
        wr(W_CONTROL, control_word(u, 0, 0));
        wr(W_BLOCK, 0);
        wr(W_CONTROL, control_word(u, 0, CW_MULTIPLEX));
        wr(W_BLOCK, (uint16_t)cyl);
        wr(W_CONTROL, control_word(u, DEVICE_OP_INITIATE_SEEK, CW_ACTIVE));
        /* WSEEK: poll the status register until the controller is not busy
         * (bit 2 clear) and finished (bit 3 set); the driver gives up after
         * 101 polls. One tick per poll, as the CPU's instruction loop would. */
        {
            int polls;
            uint16_t st_poll = 0;
            for (polls = 0; polls < 101; polls++)
            {
                st_poll = rd(R_STATUS);
                if (!(st_poll & ST_ACTIVE) && (st_poll & (1 << 3)))
                    break;
                g_instr_counter++;
                (void)dev_tick(s_card);
            }
            CHECK(polls < 101, "WSEEK: the controller finishes M4 within the driver's 101 polls");
            CHECK(!(st_poll & ST_HW_ERROR), "WSEEK: no error after M4");
        }
        wr(W_CONTROL, control_word(u, DEVICE_OP_SEEK_COMPLETE_SEARCH, CW_ACTIVE | CW_INT_NOT_ACTIVE));
        uint16_t st = rd(R_STATUS);
        CHECK(!(st & ST_ILLEGAL_LOAD), "the M6 after the WSEEK poll is not an illegal load");
        int clocks = run_until_not_active((int)(SMD_TIMEOUT_TICKS + OP_LIMIT));
        CHECK(clocks >= 0, "the seek complete search ends");
        CHECK(s_card->interruptBits != 0, "the search's end raises the interrupt the driver waits for");
        uint16_t seekc = rd(R_SEEK);
        CHECK((seekc & (1 << u)) != 0, "seek condition: the unit's bit is set after its seek");
        st = rd(R_STATUS);
        CHECK((st & ST_ON_CYLINDER) != 0, "the unit is on cylinder after the seek");
        CHECK(!(st & (1 << 6)), "no timeout");
        uint32_t mem = 0x5000;
        st = transfer(u, DEVICE_OP_READ_TRANSFER, cyl, 0, 2, mem, BLOCK / 2);
        CHECK(st != 0xFFFF && !(st & ST_HW_ERROR), "the read after the driver's seek");
        CHECK(mem_equals_file(u, lba_of(u, cyl, 0, 2), mem, 1),
              "the read returns the block at the sought cylinder");
        wr(W_CONTROL, control_word(u, 0, 0)); /* interrupt enable off */
    }

    /* ---- M6 with no seek outstanding: the search window, then timeout ---- */
    {
        int u = -1;
        for (int k = 0; k < UNITS; k++)
        {
            if (present[k])
            {
                u = k;
                break;
            }
        }
        wr(W_CONTROL, control_word(u, DEVICE_OP_SEEK_COMPLETE_SEARCH, CW_ACTIVE));
        uint16_t st = rd(R_STATUS);
        CHECK((st & ST_ACTIVE) != 0, "the search is active while it looks for a pulse");
        int clocks = run_until_not_active((int)(SMD_TIMEOUT_TICKS + OP_LIMIT));
        CHECK(clocks > 100, "the search runs its window before giving up");
        st = rd(R_STATUS);
        CHECK((st & (1 << 6)) != 0, "a search with no seek outstanding ends in timeout (b6)");
        device_clear();
    }

    /* ---- a write through unit N lands in unit N's file only ------------- */
    {
        int target = -1, other = -1;
        for (int u = UNITS - 1; u >= 0; u--)
        {
            if (present[u])
            {
                if (target < 0)
                    target = u;
                else if (other < 0)
                    other = u;
            }
        }
        if (target >= 0 && other >= 0)
        {
            int cyl = 3, head = 1, sector = 2;
            uint32_t lba_t = lba_of(target, cyl, head, sector);
            uint32_t lba_o = lba_of(other, cyl, head, sector);
            file_read(target, lba_t, before[0], 1);
            file_read(other, lba_o, before[1], 1);

            uint32_t mem = 0x6000;
            for (int i = 0; i < BLOCK / 2; i++)
            {
                s_mem[mem + (uint32_t)i] = (uint16_t)(0xA500 | (i & 0xFF));
            }
            uint16_t st = transfer(target, DEVICE_OP_WRITE_TRANSFER, cyl, head, sector, mem,
                                   BLOCK / 2);
            char msg[96];
            snprintf(msg, sizeof(msg), "unit %d: write transfer ends without error", target);
            CHECK(st != 0xFFFF && !(st & ST_HW_ERROR), msg);

            file_read(target, lba_t, after, 1);
            bool changed = memcmp(after, before[0], BLOCK) != 0;
            bool is_pattern = true;
            for (int i = 0; i < BLOCK / 2; i++)
            {
                uint16_t w = (uint16_t)((after[2 * i] << 8) | after[2 * i + 1]);
                if (w != (uint16_t)(0xA500 | (i & 0xFF)))
                {
                    is_pattern = false;
                    break;
                }
            }
            snprintf(msg, sizeof(msg), "unit %d: its file holds the written block", target);
            CHECK(changed && is_pattern, msg);

            file_read(other, lba_o, after, 1);
            snprintf(msg, sizeof(msg), "unit %d: the same block on unit %d is untouched", target,
                     other);
            CHECK(memcmp(after, before[1], BLOCK) == 0, msg);

            /* and reading it back through the controller gives the new data */
            memset(&s_mem[0x7000], 0, BLOCK);
            st = transfer(target, DEVICE_OP_READ_TRANSFER, cyl, head, sector, 0x7000, BLOCK / 2);
            CHECK(st != 0xFFFF && !(st & ST_HW_ERROR), "read-back of the written block");
            CHECK(memcmp(&s_mem[0x7000], &s_mem[mem], BLOCK) == 0,
                  "read-back through the controller returns the written words");
        }
    }

    /* ---- a head past the last one is an address error -------------------
     * The old code compared the HEAD against maxCylinders, so a head beyond
     * the drive but below the cylinder count read some other block without a
     * word of complaint. */
    for (int u = 0; u < UNITS; u++)
    {
        if (!present[u])
        {
            continue;
        }
        SMDData *data = (SMDData *)s_card->deviceData;
        int bad_head = data->regs.disks[u].headsPrCylinder;
        uint16_t st = transfer(u, DEVICE_OP_READ_TRANSFER, 0, bad_head, 0, 0x100, BLOCK / 2);
        CHECK(st != 0xFFFF && (st & ST_HW_ERROR),
              "a head past the last one is an address error, not a silent read elsewhere");
        CHECK(!(st & ST_ACTIVE), "the controller is not left active after the address error");
        device_clear();
        break;
    }

    /* ---- a GO against a unit with no pack ------------------------------- */
    for (int u = 0; u < UNITS; u++)
    {
        if (present[u])
        {
            continue;
        }
        uint16_t st = transfer(u, DEVICE_OP_READ_TRANSFER, 0, 0, 0, 0x100, BLOCK / 2);
        char msg[96];
        snprintf(msg, sizeof(msg), "unit %d (no pack): GO ends with hardware error and not ready",
                 u);
        CHECK(st != 0xFFFF && (st & ST_HW_ERROR2) && (st & ST_NOT_READY), msg);
        snprintf(msg, sizeof(msg), "unit %d (no pack): the controller is not left active", u);
        CHECK(!(st & ST_ACTIVE), msg);
        device_clear();
        break;
    }

    /* ---- a pack mounted after the controller first looked at the unit --- */
    for (int u = 0; u < UNITS; u++)
    {
        if (present[u])
        {
            continue;
        }
        uint16_t st = status_of_unit(u);
        CHECK((st & ST_NOT_READY) != 0, "empty unit reads NOT READY before the mount");
        make_synthetic_unit(u, 8);
        st = status_of_unit(u);
        char msg[96];
        snprintf(msg, sizeof(msg), "unit %d: READY after a pack is mounted on it", u);
        CHECK(!(st & ST_NOT_READY), msg);
        memset(&s_mem[0x100], 0, BLOCK);
        st = transfer(u, DEVICE_OP_READ_TRANSFER, 0, 0, 1, 0x100, BLOCK / 2);
        snprintf(msg, sizeof(msg), "unit %d: a read after the late mount returns its block 1", u);
        CHECK(st != 0xFFFF && !(st & ST_HW_ERROR) && mem_equals_file(u, 1, 0x100, 1), msg);

        /* and the reverse: the pack is taken away again */
        fclose(s_units[u].f);
        s_units[u].f = NULL;
        s_units[u].size = 0;
        st = status_of_unit(u);
        snprintf(msg, sizeof(msg), "unit %d: NOT READY again after the pack is removed", u);
        CHECK((st & ST_NOT_READY) != 0, msg);
        break;
    }

    /* ---- ECC control: one write, and the long bit ------------------------ */
    for (int u = 0; u < UNITS; u++)
    {
        if (!present[u])
        {
            continue;
        }
        SMDData *data = (SMDData *)s_card->deviceData;

        /* ND-11.020.01 sec 2.8: only the word count is "loaded by two successive
         * instructions"; the MAR/WC flip-flop does not toggle on the ECC control
         * register. One write loads it, and a word count loaded around it still
         * takes its two halves in order. */
        (void)rd(R_STATUS);
        wr(W_CONTROL, control_word(u, 0, CW_MULTIPLEX));
        wr(W_WC, 4);                                  /* ECC control: long */
        CHECK(data->regs.eccControl == 4, "one write loads the whole ECC control register");
        wr(W_CONTROL, control_word(u, 0, 0));
        wr(W_WC, 0);
        wr(W_WC, 01000);
        CHECK(data->regs.wordCounterHI == 0 && data->regs.wordCounter == 01000,
              "an ECC control load does not move the word count flip-flop");

        /* A long read (BIGFUNC item 11: word count 1004B, ECC control bit 2):
         * 512 data words, then 4 more (56-bit ECC and the end-of-record byte of
         * eight ones), and data error, status bit 9. */
        for (int i = 0; i < 520; i++)
        {
            s_mem[0x400 + i] = 0123456;
        }
        uint16_t st = transfer(u, DEVICE_OP_READ_TRANSFER, 0, 0, 2, 0x400, 01004);
        CHECK(st != 0xFFFF && (st & (1 << 9)), "a long read ends with data error (status bit 9)");
        CHECK(mem_equals_file(u, lba_of(u, 0, 0, 2), 0x400, 1),
              "a long read delivers the sector's 512 data words first");
        CHECK((s_mem[0x400 + 515] & 0377) == 0377,
              "the last of the 4 extra words ends with the end-of-record byte 377");
        CHECK(s_mem[0x400 + 516] == 0123456, "a long read of 1004B words writes no more than that");
        device_clear();

        /* With the long bit cleared, the same read is an ordinary one again. */
        wr(W_CONTROL, control_word(u, 0, CW_MULTIPLEX));
        wr(W_WC, 0);
        st = transfer(u, DEVICE_OP_READ_TRANSFER, 0, 0, 2, 0x400, BLOCK / 2);
        CHECK(st != 0xFFFF && !(st & (1 << 9)), "an ordinary read has no data error");
        break;
    }

    s_card->Destroy(s_card);
    s_card = NULL;
}

int main(void)
{
    printf("ND-100 SMD controller: several disk units\n");
    printf("=========================================\n\n");

    /* 1. Four synthetic units; unit 2 has no pack. */
    {
        bool present[UNITS] = {true, true, false, true};
        make_synthetic_unit(0, 64);
        make_synthetic_unit(1, 64);
        make_synthetic_unit(3, 64);
        run_checks("synthetic packs on units 0, 1 and 3; unit 2 empty", present);
        close_units();
    }

    /* 2. Copies of a real pack on units 1 and 3, a synthetic one on unit 0. */
    const char *image = getenv("ND100X_SMD_TEST_IMAGE");
    if (image && image[0])
    {
        bool ok = copy_pack_to_unit(1, image) && copy_pack_to_unit(3, image);
        CHECK(ok, "the pack named by ND100X_SMD_TEST_IMAGE can be copied to temporary files");
        if (ok)
        {
            bool present[UNITS] = {true, true, false, true};
            make_synthetic_unit(0, 64);
            char what[600];
            snprintf(what, sizeof(what), "\ncopies of %s on units 1 and 3; synthetic unit 0; unit 2 empty",
                     image);
            run_checks(what, present);
        }
        close_units();
    }
    else
    {
        printf("\nNOT RUN: the real-pack checks (set ND100X_SMD_TEST_IMAGE to a pack image)\n");
    }

    printf("\n%d checks, %d failed\n", s_checks, s_failed);
    printf("TB_RESULT: %s\n", s_failed == 0 ? "PASS" : "FAIL");
    return s_failed == 0 ? 0 : 1;
}
