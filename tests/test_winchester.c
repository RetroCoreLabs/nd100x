/*
 * Unit tests for the 5 1/4 inch (ST506) / 8 inch Winchester disc controller
 * (src/devices/winchester/deviceWinchester.c), cards 3041/3038 at IOX 500-507.
 *
 * Follows the test_cdc.c / test_drum.c pattern: deviceWinchester.c and
 * diskWinchester.c are linked in DIRECTLY together with the FAKE Device_*
 * infrastructure provided here, so the controller's real register handlers and
 * transfer engine are exercised through their true entry points without the
 * machine, the CPU memory subsystem or a real disk image.
 *
 * Register/bit model authority: ND-11.015.01 Winchester Disk Controller,
 * sections 3.1 - 3.5.
 *
 * The checks that matter most, because they are what distinguishes this
 * controller from the SMD one and what a port is most likely to get wrong:
 *   - memory address loads HI-then-LO but reads back LO-then-HI (sec 3.2)
 *   - the word count loads in a SINGLE access (sec 3.1) - the SMD 15 MHz card
 *     takes two, and that difference is why the ND-120 mass-load microcode
 *     works with this controller
 *   - all FOUR flip-flop reset conditions, including ACTIVATION (sec 3.2)
 *   - unit select is ONE bit, so there are exactly 2 units (sec 3.1 / 3.4)
 *   - seek direction comes from control-word bit 14, and bit 14 = 0 steps
 *     TOWARDS cylinder 0 (sec 3.4.5)
 *   - status bit 13 is the 3041/3038 discriminator and bit 15 is always 0
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* devices_types.h defines Device / DeviceClass / IODelayedCallback and pulls
 * in the per-device headers; it must come before the controller header. */
#include "devices_types.h"
#include "deviceWinchester.h"
#include "devices_protos.h"

/* ---------------- fake device infrastructure ---------------------------- */

#define FAKE_MEM_WORDS 65536u
static uint16_t g_fakeMem[FAKE_MEM_WORDS];

static IODelayedCallback g_pendingCb;
static void            *g_pendingCtx;
static int              g_pendingParam;
static uint8_t          g_pendingLevel;
static int              g_pendingSet;

/* A fake backing store: one buffer standing in for the mounted image. */
#define FAKE_DISK_BLOCKS 64u
#define FAKE_BLOCK_BYTES 1024u
static uint8_t g_fakeDisk[FAKE_DISK_BLOCKS * FAKE_BLOCK_BYTES];
static int     g_diskAttached = 1;

void Device_Init(Device *dev, uint8_t thumbwheel, DeviceClass deviceClass, size_t blockSize)
{
    (void)thumbwheel;
    memset(dev, 0, sizeof(Device));
    dev->deviceClass = deviceClass;
    dev->blockSizeBytes = blockSize;
}

void Device_DMAWrite(uint32_t coreAddress, uint16_t data)
{
    if (coreAddress < FAKE_MEM_WORDS)
        g_fakeMem[coreAddress] = data;
}

int32_t Device_DMARead(uint32_t coreAddress)
{
    if (coreAddress < FAKE_MEM_WORDS)
        return g_fakeMem[coreAddress];
    return 0;
}

void Device_QueueIODelay(Device *dev, uint16_t ticks, IODelayedCallback cb, int param, uint8_t irqlevel)
{
    (void)ticks;
    g_pendingCb = cb;
    g_pendingCtx = dev;
    g_pendingParam = param;
    g_pendingLevel = irqlevel;
    g_pendingSet = 1;
}

void Device_TickIODelay(Device *dev)
{
    if (!g_pendingSet)
        return;
    g_pendingSet = 0;
    if (g_pendingCb && g_pendingCb(g_pendingCtx, g_pendingParam))
        dev->interruptBits |= (uint16_t)(1u << g_pendingLevel);
}

void Device_SetInterruptStatus(Device *dev, bool active, uint16_t level)
{
    if (active)
        dev->interruptBits |= (uint16_t)(1u << level);
    else
        dev->interruptBits &= (uint16_t)~(1u << level);
}

uint32_t Device_RegisterAddress(Device *dev, uint32_t address)
{
    return address - dev->startAddress;
}

/* Big-endian word packing, matching the real Device_IO_Buffer* helpers:
 * word w = { byte 2w, byte 2w+1 }. */
int32_t Device_IO_BufferReadWord(Device *dev, uint8_t *buf, int32_t word_offset)
{
    (void)dev;
    return (int32_t)(((uint16_t)buf[word_offset * 2] << 8) | buf[word_offset * 2 + 1]);
}

int32_t Device_IO_BufferWriteWord(Device *dev, uint8_t *buf, int32_t word_offset, uint16_t data)
{
    (void)dev;
    buf[word_offset * 2] = (uint8_t)(data >> 8);
    buf[word_offset * 2 + 1] = (uint8_t)(data & 0xFF);
    return 0;
}

/* ---------------- fake block callbacks ---------------------------------- */

static int fake_read(Device *self, uint8_t *buffer, size_t blockCount, uint32_t lba, int unit)
{
    (void)self; (void)unit;
    if (lba + blockCount > FAKE_DISK_BLOCKS)
        return -1;
    memcpy(buffer, &g_fakeDisk[lba * FAKE_BLOCK_BYTES], blockCount * FAKE_BLOCK_BYTES);
    return (int)blockCount;
}

static int fake_write(Device *self, const uint8_t *buffer, size_t blockCount, uint32_t lba, int unit)
{
    (void)self; (void)unit;
    if (lba + blockCount > FAKE_DISK_BLOCKS)
        return -1;
    memcpy(&g_fakeDisk[lba * FAKE_BLOCK_BYTES], buffer, blockCount * FAKE_BLOCK_BYTES);
    return (int)blockCount;
}

static int fake_info(Device *self, size_t *size, bool *readOnly, int unit)
{
    (void)self; (void)unit;
    if (size)
        *size = g_diskAttached ? sizeof(g_fakeDisk) : 0;
    if (readOnly)
        *readOnly = false;
    return 0;
}

/* ---------------- tiny assert harness ----------------------------------- */

static int g_pass, g_fail;
#define CHECK(cond, msg) do {                                            \
        if (cond) { g_pass++; }                                          \
        else { g_fail++; printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

/* ---------------- register offsets (ND-11.015.01 sec 3.1) --------------- */
#define R_READ_MA    0
#define R_LOAD_MA    1
#define R_READ_SECT  2
#define R_LOAD_BA    3
#define R_READ_ST    4
#define R_LOAD_CW    5
#define R_READ_BA    6
#define R_LOAD_WC    7

/* Control-word bits (sec 3.4). */
#define CW_INT_NOT_ACTIVE (1u << 0)
#define CW_INT_ERRORS     (1u << 1)
#define CW_ACTIVATE       (1u << 2)
#define CW_TEST_MODE      (1u << 3)
#define CW_DEVICE_CLEAR   (1u << 4)
#define CW_HEAD_SHIFT     5
#define CW_UNIT_SHIFT     9
#define CW_OP_SHIFT       11
#define CW_DIRECTION      (1u << 14)
#define CW_BAD_TRACK      (1u << 15)

/* Status bits (sec 3.5). */
#define ST_ACTIVE       (1u << 2)
#define ST_FINISHED     (1u << 3)
#define ST_ERROR_OR     (1u << 4)
#define ST_DISK_FAULT   (1u << 7)
#define ST_ADDR_MISM    (1u << 8)
#define ST_COMPARE_ERR  (1u << 10)
#define ST_CONTROLLER_ID (1u << 13)
#define ST_ON_CYLINDER  (1u << 14)
#define ST_BIT15        (1u << 15)

static uint16_t rd(Device *dev, uint32_t reg) { return dev->Read(dev, dev->startAddress + reg); }
static void     wr(Device *dev, uint32_t reg, uint16_t v) { dev->Write(dev, dev->startAddress + reg, v); }
static uint16_t status(Device *dev) { return rd(dev, R_READ_ST); }

int main(void)
{
    printf("=== Winchester disc controller tests ===\n");

    Device *dev = CreateWinchesterDevice(0);
    CHECK(dev != NULL, "device created");
    if (!dev)
        return 1;

    dev->blockCallbacks.readFunc = fake_read;
    dev->blockCallbacks.writeFunc = fake_write;
    dev->blockCallbacks.diskInfoFunc = fake_info;

    WinchesterData *data = (WinchesterData *)dev->deviceData;

    /* --- 1. identity and address block (sec 3.1 / 4.1) ------------------- */
    CHECK(dev->startAddress == 0500 && dev->endAddress == 0507, "address block 500-507");
    CHECK(dev->identCode == 001, "ident code 1 (disk system 1)");
    CHECK(dev->interruptLevel == 11, "interrupt level 11");
    CHECK(data->regs.maxUnits == 2, "two units - unit select is ONE control-word bit");

    /* Disk system 2 answers 510-517 with ident 5. */
    Device *dev2 = CreateWinchesterDevice(1);
    CHECK(dev2 != NULL && dev2->startAddress == 0510 && dev2->identCode == 005,
          "disk system 2 at 510 ident 5");
    if (dev2)
        dev2->Destroy(dev2);

    /* --- 2. memory address: write HI-then-LO, read LO-then-HI (sec 3.2) -- */
    wr(dev, R_LOAD_MA, 0x0012);   /* first write  -> upper 8 bits  */
    wr(dev, R_LOAD_MA, 0x3456);   /* second write -> lower 16 bits */
    CHECK(data->regs.memoryAddressHiBits == 0x12, "first MA write loaded the HIGH byte");
    CHECK(data->regs.memoryAddress == 0x3456, "second MA write loaded the LOW 16 bits");

    /* Reads come back in the OPPOSITE order - this asymmetry is explicit in
     * the manual and is the single easiest thing to get wrong. */
    CHECK(rd(dev, R_READ_MA) == 0x3456, "first MA read returns the LOW 16 bits");
    CHECK(rd(dev, R_READ_MA) == 0x0012, "second MA read returns the HIGH 8 bits");

    /* --- 3. a status read resets the flip-flop (sec 3.2) ----------------- */
    wr(dev, R_LOAD_MA, 0x0077);   /* leaves the write FF mid-sequence */
    (void)status(dev);            /* one of the four reset conditions */
    wr(dev, R_LOAD_MA, 0x0011);   /* must be taken as the HIGH byte again */
    wr(dev, R_LOAD_MA, 0x2222);
    CHECK(data->regs.memoryAddressHiBits == 0x11 && data->regs.memoryAddress == 0x2222,
          "status read re-synchronised the MA write flip-flop");

    /* --- 4. word count loads in a SINGLE access (sec 3.1) ---------------- */
    wr(dev, R_LOAD_WC, 02000);    /* 1024 words, as the ND-120 microcode writes */
    CHECK(data->regs.wordCounter == 02000,
          "word count takes ONE write - not the SMD two-access protocol");

    /* --- 5. control word decode (sec 3.4) -------------------------------- */
    wr(dev, R_LOAD_CW, (uint16_t)((3u << CW_HEAD_SHIFT) | (1u << CW_UNIT_SHIFT) | CW_TEST_MODE));
    CHECK(data->regs.head == 3, "head from control-word bits 5-8");
    CHECK(data->regs.selectedUnit == 1, "unit from control-word bit 9");
    CHECK(data->regs.testMode, "test mode from control-word bit 3");
    CHECK(data->regs.selectedDisk == &data->regs.disks[1], "unit 1 selected");

    /* --- 6. block address split (sec 3.3): cylinder b15-5, sector b4-0 --- */
    wr(dev, R_LOAD_BA, (uint16_t)((17u << 5) | 6u));
    CHECK(data->regs.cylinder == 17, "cylinder from block address bits 15-5");
    CHECK(data->regs.sector == 6, "sector from block address bits 4-0");

    /* --- 7. status identity bits (sec 3.5) ------------------------------- */
    {
        uint16_t st = status(dev);
        CHECK((st & ST_CONTROLLER_ID) != 0, "bit 13 set: this is a 3041");
        CHECK((st & ST_BIT15) == 0, "bit 15 always 0 - not the 10 Mb controller");
    }

    /* --- 8. M0 read transfer moves real data into memory ----------------- */
    {
        /* Seed the fake image: LBA 0, one block, ascending words. */
        for (uint32_t i = 0; i < FAKE_BLOCK_BYTES / 2; i++)
        {
            g_fakeDisk[i * 2]     = (uint8_t)((0x1000 + i) >> 8);
            g_fakeDisk[i * 2 + 1] = (uint8_t)((0x1000 + i) & 0xFF);
        }
        memset(g_fakeMem, 0, sizeof(g_fakeMem));

        /* Unit 0, head 0, cylinder 0, sector 0 -> LBA 0. */
        wr(dev, R_LOAD_CW, 0);                    /* select unit 0, clear test mode */
        (void)status(dev);                        /* resync the MA flip-flop */
        wr(dev, R_LOAD_MA, 0x0000);               /* HI */
        wr(dev, R_LOAD_MA, 0x1000);               /* LO -> core address 0x1000 */
        wr(dev, R_LOAD_BA, 0);                    /* cylinder 0, sector 0 */
        wr(dev, R_LOAD_WC, 512);                  /* one 1024-byte sector */
        wr(dev, R_LOAD_CW, CW_ACTIVATE | (WD_OP_READ_TRANSFER << CW_OP_SHIFT));

        CHECK(g_fakeMem[0x1000] == 0x1000, "M0 read: first word landed in memory");
        CHECK(g_fakeMem[0x1000 + 511] == 0x1000 + 511, "M0 read: last word landed in memory");
        CHECK(g_fakeMem[0x1000 + 512] == 0, "M0 read: did not overrun the word count");

        /* The operation completes on the queued delay, not instantly. */
        Device_TickIODelay(dev);
        uint16_t st = status(dev);
        CHECK((st & ST_ACTIVE) == 0, "M0: controller not active after completion");
        CHECK((st & ST_FINISHED) != 0, "M0: finished bit set");
        CHECK((st & ST_ERROR_OR) == 0, "M0: no error reported");
    }

    /* --- 9. M1 write transfer round-trips through the image -------------- */
    {
        for (uint32_t i = 0; i < 512; i++)
            g_fakeMem[0x2000 + i] = (uint16_t)(0xA000 + i);
        memset(g_fakeDisk, 0, sizeof(g_fakeDisk));

        wr(dev, R_LOAD_CW, 0);
        (void)status(dev);
        wr(dev, R_LOAD_MA, 0x0000);
        wr(dev, R_LOAD_MA, 0x2000);
        wr(dev, R_LOAD_BA, 0);
        wr(dev, R_LOAD_WC, 512);
        wr(dev, R_LOAD_CW, CW_ACTIVATE | (WD_OP_WRITE_TRANSFER << CW_OP_SHIFT));
        Device_TickIODelay(dev);

        uint16_t w0 = (uint16_t)((g_fakeDisk[0] << 8) | g_fakeDisk[1]);
        uint16_t wl = (uint16_t)((g_fakeDisk[1022] << 8) | g_fakeDisk[1023]);
        CHECK(w0 == 0xA000, "M1 write: first word reached the image");
        CHECK(wl == (uint16_t)(0xA000 + 511), "M1 write: last word reached the image");
    }

    /* --- 10. M4 seek is RELATIVE, direction from bit 14 (sec 3.4.5) ------ */
    {
        wr(dev, R_LOAD_CW, 0);                  /* unit 0 */
        data->regs.disks[0].cylinder = 100;

        /* bit 14 = 0 -> towards cylinder 0 */
        wr(dev, R_LOAD_WC, 10);                 /* step count */
        wr(dev, R_LOAD_CW, CW_ACTIVATE | (WD_OP_SEEK << CW_OP_SHIFT));
        Device_TickIODelay(dev);
        CHECK(data->regs.disks[0].cylinder == 90, "M4 with bit 14 = 0 steps TOWARDS cylinder 0");

        /* bit 14 = 1 -> away from cylinder 0 */
        wr(dev, R_LOAD_WC, 5);
        wr(dev, R_LOAD_CW, CW_ACTIVATE | CW_DIRECTION | (WD_OP_SEEK << CW_OP_SHIFT));
        Device_TickIODelay(dev);
        CHECK(data->regs.disks[0].cylinder == 95, "M4 with bit 14 = 1 steps AWAY from cylinder 0");

        /* Seeking past cylinder 0 clamps the CYLINDER, and must not disturb
         * the head - the C# original wrote the clamp into the head register. */
        uint8_t headBefore = data->regs.head;
        wr(dev, R_LOAD_WC, 1000);
        wr(dev, R_LOAD_CW, CW_ACTIVATE | (WD_OP_SEEK << CW_OP_SHIFT));
        Device_TickIODelay(dev);
        CHECK(data->regs.disks[0].cylinder == 0, "M4 clamps at cylinder 0");
        CHECK(data->regs.head == headBefore, "M4 clamp does not corrupt the head register");
    }

    /* --- 11. M7 return to zero ------------------------------------------ */
    {
        data->regs.disks[0].cylinder = 400;
        wr(dev, R_LOAD_CW, CW_ACTIVATE | (WD_OP_RETURN_TO_ZERO << CW_OP_SHIFT));
        Device_TickIODelay(dev);
        CHECK(data->regs.disks[0].cylinder == 0, "M7 returns the arm to cylinder 0");
    }

    /* --- 12. address bound check ---------------------------------------- */
    {
        wr(dev, R_LOAD_CW, 0);
        (void)status(dev);
        wr(dev, R_LOAD_MA, 0x0000);
        wr(dev, R_LOAD_MA, 0x3000);
        /* sector 30 is past sectorsPrTrack (9) for every Winchester geometry */
        wr(dev, R_LOAD_BA, 30u);
        wr(dev, R_LOAD_WC, 512);
        wr(dev, R_LOAD_CW, CW_ACTIVATE | (WD_OP_READ_TRANSFER << CW_OP_SHIFT));

        uint16_t st = status(dev);
        CHECK((st & ST_ADDR_MISM) != 0, "out-of-range sector raises address mismatch");
        CHECK((st & ST_ACTIVE) == 0, "address mismatch still TERMINATES the operation");
    }

    /* --- 13. device clear (control-word bit 4) --------------------------- */
    {
        wr(dev, R_LOAD_WC, 1234);
        wr(dev, R_LOAD_CW, CW_DEVICE_CLEAR);
        CHECK(data->regs.wordCounter == 0, "device clear zeroes the word counter");
        uint16_t st = status(dev);
        CHECK((st & ST_ACTIVE) == 0, "device clear drops the active bit");
        CHECK((st & ST_ERROR_OR) == 0, "device clear clears the error bits");
    }

    /* --- 14. M6 is NEVER activated (sec 3.4) ----------------------------- */
    {
        wr(dev, R_LOAD_CW, CW_ACTIVATE | (WD_OP_LOAD_CTRL_BITS << CW_OP_SHIFT));
        uint16_t st = status(dev);
        CHECK((st & ST_ACTIVE) == 0, "M6 does not activate the controller");
    }

    /* --- 15. a powered-off unit reports a fault, never a hang ------------ */
    {
        g_diskAttached = 0;
        data->regs.disks[0].unitAttachChecked = false;

        wr(dev, R_LOAD_CW, 0);
        (void)status(dev);
        wr(dev, R_LOAD_MA, 0x0000);
        wr(dev, R_LOAD_MA, 0x4000);
        wr(dev, R_LOAD_BA, 0);
        wr(dev, R_LOAD_WC, 512);
        wr(dev, R_LOAD_CW, CW_ACTIVATE | (WD_OP_READ_TRANSFER << CW_OP_SHIFT));

        uint16_t st = status(dev);
        CHECK((st & ST_DISK_FAULT) != 0, "unattached unit raises a disk fault");
        CHECK((st & ST_ACTIVE) == 0, "unattached unit still terminates the operation");
        CHECK((st & ST_ON_CYLINDER) == 0, "unattached unit is not on cylinder");
        g_diskAttached = 1;
    }

    /* --- 16. interrupt + IDENT PL11, the TPE CONFIGURATION probe ---------
     *
     * Sec 4.1: "If the controller is ready for an operation (status bit 3 = 1),
     * and interrupt is enabled (status bit 0 has been set by control bit 0 =
     * 1), the interrupt signal BINT11 will be active ... The IDENT code may
     * now be read by an IDENT PL11 instruction."
     *
     * This is how TPE CONFIGURATION detects the card. It went untested at
     * first and the controller failed the probe with "No identcode found on
     * level 11D, expected identcode: 1B", because the interrupt was only ever
     * raised at the END of a device operation, never on an idle card. */
    {
        const uint16_t L11 = (uint16_t)(1u << 11);

        /* Start from a known quiet state. */
        wr(dev, R_LOAD_CW, CW_DEVICE_CLEAR);
        (void)status(dev);
        CHECK((dev->interruptBits & L11) == 0, "device clear leaves level 11 quiet");

        /* Enable the interrupt on an IDLE controller - no activate bit. */
        wr(dev, R_LOAD_CW, CW_INT_NOT_ACTIVE);
        uint16_t st = status(dev);
        CHECK((st & ST_FINISHED) != 0, "idle controller reports ready (status bit 3)");
        CHECK((st & 1u) != 0, "interrupt-enable reaches status bit 0");
        CHECK((dev->interruptBits & L11) != 0,
              "ready + interrupt enabled asserts BINT11 on an idle controller");

        /* IDENT PL11 answers with code 1, and identing clears the interrupt. */
        CHECK(dev->Ident(dev, 11) == 001, "IDENT PL11 returns ident code 1");
        CHECK((dev->interruptBits & L11) == 0, "IDENT cleared the pending interrupt");
        CHECK(dev->Ident(dev, 11) == 0, "a second IDENT with nothing pending stays silent");

        /* An IDENT for a different level must never be answered - level 11 is
         * shared with the floppy (ident 21) and the SMD card (ident 17). */
        wr(dev, R_LOAD_CW, CW_INT_NOT_ACTIVE);
        CHECK((dev->interruptBits & L11) != 0, "interrupt re-armed");
        CHECK(dev->Ident(dev, 10) == 0, "IDENT on the wrong level is not answered");
        CHECK(dev->Ident(dev, 13) == 0, "IDENT on level 13 is not answered");
        CHECK((dev->interruptBits & L11) != 0, "a wrong-level IDENT left level 11 pending");

        /* Device clear and interrupt enable in ONE control word: the clear
         * must not swallow the interrupt update. This is the usual probe
         * opening, and an early return on device clear breaks it. */
        (void)dev->Ident(dev, 11);
        wr(dev, R_LOAD_CW, CW_DEVICE_CLEAR | CW_INT_NOT_ACTIVE);
        CHECK((dev->interruptBits & L11) != 0,
              "device clear + interrupt enable in one word still interrupts");

        /* Dropping the enable takes the interrupt away again. */
        wr(dev, R_LOAD_CW, 0);
        CHECK((dev->interruptBits & L11) == 0, "clearing the enable drops BINT11");

        /* An ACTIVATED operation must not leave the card ready mid-flight;
         * the interrupt belongs at completion, not at activation. */
        g_diskAttached = 1;
        data->regs.disks[0].unitAttachChecked = false;
        wr(dev, R_LOAD_CW, CW_INT_NOT_ACTIVE);
        (void)status(dev);
        wr(dev, R_LOAD_MA, 0x0000);
        wr(dev, R_LOAD_MA, 0x4000);
        wr(dev, R_LOAD_BA, 0);
        wr(dev, R_LOAD_WC, 512);
        wr(dev, R_LOAD_CW, CW_INT_NOT_ACTIVE | CW_ACTIVATE |
                           (WD_OP_READ_TRANSFER << CW_OP_SHIFT));
        CHECK((dev->interruptBits & L11) == 0,
              "activation itself does not interrupt - completion does");

        /* The operation completes on the queued delay, not instantly. */
        Device_TickIODelay(dev);
        st = status(dev);
        CHECK((st & ST_ACTIVE) == 0 && (st & ST_FINISHED) != 0,
              "the transfer completed and the card is ready again");
        CHECK((dev->interruptBits & L11) != 0, "completion raised the interrupt");
        CHECK(dev->Ident(dev, 11) == 001, "IDENT after a transfer returns code 1");
    }

    dev->Destroy(dev);

    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    if (g_fail == 0)
        printf("TB_RESULT: PASS\n");
    else
        printf("TB_RESULT: FAIL (%d checks failed)\n", g_fail);
    return g_fail ? 1 : 0;
}
