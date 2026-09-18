/*
 * Comprehensive unit tests for the NORD TSS CDC/NCR system-disc device
 * (src/devices/cdc/device_cdc.c), the cartridge disc at IOX 500-507.
 *
 * Following the test_drum.c / test_bcd.c pattern: device_cdc.c is linked in
 * DIRECTLY together with the FAKE Device_* infrastructure provided here, so the
 * disc's real register handlers and transfer engine are exercised through their
 * true entry points without pulling in the machine, the CPU memory subsystem or
 * a real disk image.
 *
 * Coverage: every function in device_cdc.c and every register/path -
 *   CreateCdcDevice, Cdc_Reset, Cdc_Read (RST/RCA two-read/RSECT/SEEK+test),
 *   Cdc_Write (LCA/LBA/LWC/LCW-activate/LCW-clear), Cdc_ExecuteGO
 *   (read/write/read-parity/compare/test-mode/bounds), Cdc_End, Cdc_Tick,
 *   Cdc_Ident, Cdc_Destroy, CdcDevice_SetBackingFile + Cdc_AttachBacking +
 *   Cdc_EnsureSurface (backing-file round-trip) - PLUS the authoritative
 *   register/bit model: control-word decode (activate/test/op 00-11/unit/
 *   core-addr bits 5-6), status-word bits, and the test-mode self-test.
 *
 * Register/bit model authority: ND-11.008.01 (NORD-10 CDC, PRIMARY) and
 * ND-06.016.01 pp.188-190 (ND-100 status names). Wire protocol:
 * <TSS>\docs\CDC-DISC-DEVICE.md and OVERLAY-DISC-SPEC.md.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device_cdc.h"
#include "devices_protos.h"

/* ---------------- fake device infrastructure ---------------------------- */

/* A physical-memory stand-in for DMA. 64 Ki words is plenty for the tests. */
#define FAKE_MEM_WORDS 65536u
static uint16_t g_fakeMem[FAKE_MEM_WORDS];

/* One pending delayed callback (the disc only ever queues one at a time). */
static IODelayedCallback g_pendingCb;
static void            *g_pendingCtx;
static int              g_pendingParam;
static uint8_t          g_pendingLevel;
static int              g_pendingSet;

void Device_Init(Device *dev, uint8_t thumbwheel, DeviceClass deviceClass, size_t blockSize)
{
    (void)thumbwheel; (void)blockSize;
    memset(dev, 0, sizeof(Device));       /* real Device_Init zeroes the struct */
    dev->deviceClass = deviceClass;
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

/* Mimics the real Device_TickIODelay: fire the queued callback and, if it
 * returns true, raise the interrupt bit for its level. */
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

/* ---------------- tiny assert harness ----------------------------------- */

static int g_pass, g_fail;
#define CHECK(cond, msg) do {                                            \
        if (cond) { g_pass++; }                                          \
        else { g_fail++; printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

/* ---------------- helpers ----------------------------------------------- */

/* Build a control word that ACTIVATEs a transfer with the given device operation.
 * The operation is a plain 2-bit field at bits 11-12 (ND-11.008.01 / ND-06.016.01),
 * so READ is 000004, WRITE 002004, parity 004004, compare 006004. bit 2 = activate.
 * [VERIFIED live: with the mac-c SHR fix, DKTR now emits 000004 for the overlay read.] */
static uint16_t modus_go(uint16_t op)
{
    return (uint16_t)(((op & 3u) << CDC_CTRL_OP_SHIFT) | CDC_CTRL_ACTIVATE);
}

/* Program and start one transfer in the driver's register order
 * (LCA, LBA, LWC, LCW) - the LCW write triggers the transfer. */
static void program(Device *dev, uint16_t core, uint16_t sector, uint16_t wc, uint16_t control)
{
    uint32_t base = dev->startAddress;
    dev->Write(dev, base + CDC_REG_LCA, core);
    dev->Write(dev, base + CDC_REG_LBA, sector);
    dev->Write(dev, base + CDC_REG_LWC, wc);
    dev->Write(dev, base + CDC_REG_LCW, control);   /* triggers the transfer */
}
static uint16_t status(Device *dev)
{
    return dev->Read(dev, dev->startAddress + CDC_REG_RST);
}

/* Place known words directly on the disc surface (as if pre-written), so a READ
 * has something deterministic to fetch. */
static void seed_sector(CdcData *d, uint32_t sector, uint16_t base)
{
    uint32_t off = sector * CDC_WORDS_PER_SECTOR;
    for (uint16_t i = 0; i < CDC_WORDS_PER_SECTOR; i++)
        d->surface[off + i] = (uint16_t)(base + i);
}

int main(void)
{
    printf("=== CDC disc device tests ===\n");

    /* --- 0. factory + identity ------------------------------------------ */
    Device *dev = CreateCdcDevice(0);
    CHECK(dev != NULL, "device created");
    if (!dev) return 1;
    CHECK(dev->startAddress == 0500 && dev->endAddress == 0507, "address block 500-507");
    CHECK(dev->identCode == CDC_IDENT_CODE && dev->identCode == 001, "ident code 01");
    CHECK(dev->interruptLevel == CDC_INT_LEVEL && dev->interruptLevel == 11, "interrupt level 11");
    CdcData *d = (CdcData *)dev->deviceData;

    /* Default surface must cover the corrected DKADR overlay range (max
     * physical sector 458 - see CDC_DEFAULT_SECTORS in device_cdc.h). The
     * surface grows on demand (Cdc_EnsureSurface) for anything beyond it. */
    CHECK(d->surfaceSectors == CDC_DEFAULT_SECTORS && d->surfaceSectors == 512u,
          "default surface is 512 sectors");
    CHECK(d->surfaceSectors > 458u,
          "default surface covers max corrected overlay physical sector (458)");

    /* --- 0b. register offsets / names (authoritative map, MANUAL-N10 p.13) --- */
    CHECK(CDC_REG_RCA == 0 && CDC_REG_LCA == 1 && CDC_REG_RSECT == 2 && CDC_REG_LBA == 3,
          "register offsets RCA/LCA/RSECT/LBA = 0/1/2/3");
    CHECK(CDC_REG_RST == 4 && CDC_REG_LCW == 5 && CDC_REG_SEEK == 6 && CDC_REG_LWC == 7,
          "register offsets RST/LCW/SEEK/LWC = 4/5/6/7");
    CHECK(dev->startAddress + CDC_REG_RST == 0504 && dev->startAddress + CDC_REG_LCW == 0505,
          "RST is IOX 504 and LCW is IOX 505");
    CHECK(dev->startAddress + CDC_REG_SEEK == 0506 && dev->startAddress + CDC_REG_LWC == 0507,
          "SEEK is IOX 506 and LWC is IOX 507");

    /* --- 1. reset / idle status ----------------------------------------- */
    CHECK((status(dev) & CDC_STATUS_READY) != 0, "idle status has READY set (bit 14)");
    CHECK((status(dev) & CDC_STATUS_BUSY) == 0, "idle status has BUSY clear");
    CHECK((status(dev) & CDC_STATUS_ERR)  == 0, "idle status has ERR clear");
    CHECK(d->status.bits.onCylinder == 1 && d->status.bits.active == 0 && d->status.bits.errorOr == 0,
          "idle status union: onCylinder set, active/errorOr clear");

    /* --- 2. each register write latches the right state ----------------- */
    dev->Write(dev, dev->startAddress + CDC_REG_LCA, 0x1234);
    CHECK(d->coreAddrLow == 0x1234, "LCA (501) loads the core address low 16 bits");
    dev->Write(dev, dev->startAddress + CDC_REG_LBA, 0254);   /* octal */
    CHECK(d->blockAddress == 0254, "LBA (503) loads the block/sector address");
    dev->Write(dev, dev->startAddress + CDC_REG_LWC, 0400);   /* 256 dec */
    CHECK(d->wordCount == 0400, "LWC (507) loads the word count");
    dev->Write(dev, dev->startAddress + CDC_REG_LCW, 0);      /* no activate: just latch */
    CHECK(d->control.raw == 0, "LCW (505) non-activate write latches control without a transfer");

    /* --- 3. register reads (RCA / RSECT / SEEK) ------------------------- */
    CHECK(dev->Read(dev, dev->startAddress + CDC_REG_RCA) == 0x1234,
          "RCA (500) reads back the core address low 16");
    CHECK(dev->Read(dev, dev->startAddress + CDC_REG_SEEK) == status(dev),
          "SEEK (506) is a no-op returning status (always on-cylinder)");

    /* --- 4. READ transfer, sector A = OVLAY*2 + OVDK -------------------- *
     * OVDK = 0160 octal, OV19 (XSTAR) = 036 octal -> sector 0254 octal.    */
    const uint32_t OVDK  = 0160;
    const uint32_t OVLAY = 036;
    uint32_t sectorA = OVLAY * 2 + OVDK;                 /* = 0254 octal = 172 dec */
    CHECK(sectorA == 0254u, "overlay sector math OVLAY*2+OVDK == 0254 octal");

    /* addressing math asserted directly: sector -> byte offset S*256*2 */
    CHECK(sectorA * CDC_SECTOR_BYTES == sectorA * CDC_WORDS_PER_SECTOR * 2u,
          "byte offset of a sector is S*256*2");

    seed_sector(d, sectorA, 0x7000);
    memset(g_fakeMem, 0, sizeof(g_fakeMem));
    program(dev, /*core*/0x0300, (uint16_t)sectorA, CDC_WORDS_PER_SECTOR, modus_go(CDC_OP_READ));
    CHECK((status(dev) & CDC_STATUS_BUSY) != 0, "BUSY set after GO (before completion)");
    CHECK((status(dev) & CDC_STATUS_ERR) == 0, "no ERR on a good read");
    CHECK(d->status.bits.active == 1 && d->status.bits.transferOn == 1,
          "status union: active + transferOn set during a transfer");

    int okA = 1;
    for (uint16_t i = 0; i < CDC_WORDS_PER_SECTOR; i++)
        if (g_fakeMem[0x0300 + i] != (uint16_t)(0x7000 + i)) okA = 0;
    CHECK(okA, "READ DMA'd the whole 256-word sector A to the right core addresses");
    /* the mapping used was exactly surface[sectorA*256 + i] */
    CHECK(g_fakeMem[0x0300] == d->surface[sectorA * CDC_WORDS_PER_SECTOR],
          "READ fetched word 0 from surface offset sector*256");

    /* completion: Tick fires the delayed callback -> BUSY clears, IRQ raised */
    dev->Tick(dev);
    CHECK((status(dev) & CDC_STATUS_BUSY) == 0, "BUSY cleared after completion");
    CHECK(d->status.bits.transferComplete == 1 && d->status.bits.readyForTransfer == 1,
          "status union: transferComplete + readyForTransfer set after completion");
    CHECK((dev->interruptBits & (1u << CDC_INT_LEVEL)) != 0, "completion interrupt on level 11");
    CHECK(dev->Ident(dev, CDC_INT_LEVEL) == CDC_IDENT_CODE, "Ident returns the ident code");
    CHECK((dev->interruptBits & (1u << CDC_INT_LEVEL)) == 0, "Ident clears the interrupt bit");
    CHECK(dev->Read(dev, dev->startAddress + CDC_REG_RSECT) == (uint16_t)sectorA,
          "RSECT (502) reports the last sector touched");

    /* --- 5. READ a SECOND, different sector (partial word count) -------- */
    uint32_t sectorB = sectorA + 1;                    /* 0255 octal, ROV4 sector */
    seed_sector(d, sectorB, 0x9000);
    memset(g_fakeMem, 0, sizeof(g_fakeMem));
    program(dev, 0x0500, (uint16_t)sectorB, 8, modus_go(CDC_OP_READ)); /* only 8 words */
    dev->Tick(dev);
    int okB = 1;
    for (uint16_t i = 0; i < 8; i++)
        if (g_fakeMem[0x0500 + i] != (uint16_t)(0x9000 + i)) okB = 0;
    CHECK(okB, "READ of sector B copied the requested word count");
    CHECK(g_fakeMem[0x0500 + 8] == 0, "READ did not transfer beyond the word count");

    /* --- 6. WRITE transfer + round-trip READ back ----------------------- */
    uint32_t sectorC = 0100;                            /* an empty sector */
    memset(g_fakeMem, 0, sizeof(g_fakeMem));
    for (uint16_t i = 0; i < 16; i++) g_fakeMem[0x0700 + i] = (uint16_t)(0xB000 + i);
    program(dev, 0x0700, (uint16_t)sectorC, 16, modus_go(CDC_OP_WRITE));
    dev->Tick(dev);
    int okW = 1;
    uint32_t offC = sectorC * CDC_WORDS_PER_SECTOR;
    for (uint16_t i = 0; i < 16; i++)
        if (d->surface[offC + i] != (uint16_t)(0xB000 + i)) okW = 0;
    CHECK(okW, "WRITE copied memory to the correct disc sector offset");

    memset(g_fakeMem, 0, sizeof(g_fakeMem));
    program(dev, 0x0800, (uint16_t)sectorC, 16, modus_go(CDC_OP_READ));
    dev->Tick(dev);
    int okRT = 1;
    for (uint16_t i = 0; i < 16; i++)
        if (g_fakeMem[0x0800 + i] != (uint16_t)(0xB000 + i)) okRT = 0;
    CHECK(okRT, "round-trip: READ returns exactly what WRITE stored");

    /* --- 7. control-word device clear (bit 4) clears BUSY/ERR ----------- */
    d->status.raw |= (CDC_STATUS_BUSY | CDC_STATUS_ERR);
    dev->Write(dev, dev->startAddress + CDC_REG_LCW, CDC_CTRL_DEVCLEAR); /* 0o20 */
    CHECK((status(dev) & (CDC_STATUS_BUSY | CDC_STATUS_ERR)) == 0,
          "control device-clear (bit 4 = 0o20) resets BUSY and ERR");

    /* --- 8. bounds / error handling ------------------------------------- */
    /* a sector beyond the surface is out of range -> ERR, not left busy */
    uint32_t badSector = d->surfaceSectors + 5;         /* past the end */
    program(dev, 0x0100, (uint16_t)badSector, 8, modus_go(CDC_OP_READ));
    CHECK((status(dev) & CDC_STATUS_ERR) != 0, "out-of-range sector sets ERR");
    dev->Tick(dev);
    CHECK((status(dev) & CDC_STATUS_BUSY) == 0, "device not left busy after a bounds error");

    /* a word count that overruns the last valid sector -> ERR */
    uint32_t lastSector = d->surfaceSectors - 1;
    program(dev, 0x0100, (uint16_t)lastSector, CDC_WORDS_PER_SECTOR + 1, modus_go(CDC_OP_READ));
    CHECK((status(dev) & CDC_STATUS_ERR) != 0, "word count past end of surface sets ERR");
    dev->Tick(dev);

    /* --- 10. control-word decode (MANUAL-N10 p.14-15) ------------------- *
     * Latch (no activate) and inspect the bit-field union directly.        */
    dev->Write(dev, dev->startAddress + CDC_REG_LCW, CDC_CTRL_TEST); /* bit 3 only */
    CHECK(d->control.bits.testMode == 1 && d->control.bits.activate == 0,
          "control decode: test-mode bit 3");
    /* device operation: a plain 2-bit field at bits 11-12, so the TSS READ control
     * word is 000004 (verify modus_go builds exactly that), and each op round-trips
     * through deviceOperation == op. */
    CHECK(modus_go(CDC_OP_READ) == 0000004u, "modus_go(READ) = TSS DKTR read word 000004");
    dev->Write(dev, dev->startAddress + CDC_REG_LCW, (uint16_t)(modus_go(CDC_OP_READ) & ~CDC_CTRL_ACTIVATE));
    CHECK(d->control.bits.deviceOperation == CDC_OP_READ, "control decode: read -> op field = 0 (000004)");
    dev->Write(dev, dev->startAddress + CDC_REG_LCW, (uint16_t)(modus_go(CDC_OP_WRITE) & ~CDC_CTRL_ACTIVATE));
    CHECK(d->control.bits.deviceOperation == CDC_OP_WRITE, "control decode: write -> op field = 1 (002004)");
    dev->Write(dev, dev->startAddress + CDC_REG_LCW, (uint16_t)(modus_go(CDC_OP_READ_PARITY) & ~CDC_CTRL_ACTIVATE));
    CHECK(d->control.bits.deviceOperation == CDC_OP_READ_PARITY, "control decode: read-parity -> op field = 2 (004004)");
    dev->Write(dev, dev->startAddress + CDC_REG_LCW, (uint16_t)(modus_go(CDC_OP_COMPARE) & ~CDC_CTRL_ACTIVATE));
    CHECK(d->control.bits.deviceOperation == CDC_OP_COMPARE, "control decode: compare -> op field = 3 (006004)");
    /* unit select bits 9-10 */
    dev->Write(dev, dev->startAddress + CDC_REG_LCW, (uint16_t)(3u << CDC_CTRL_UNIT_SHIFT));
    CHECK(d->control.bits.unitSelect == 3, "control decode: unit select bits 9-10 = 3");
    dev->Write(dev, dev->startAddress + CDC_REG_LCW, (uint16_t)(2u << CDC_CTRL_UNIT_SHIFT));
    CHECK(d->control.bits.unitSelect == 2, "control decode: unit select bits 9-10 = 2");
    /* activate bit 2 and device-clear bit 4 mask values */
    CHECK(CDC_CTRL_ACTIVATE == 0000004u && CDC_CTRL_DEVCLEAR == 0000020u,
          "control masks: activate = bit 2 (0o4), device-clear = bit 4 (0o20)");
    dev->Write(dev, dev->startAddress + CDC_REG_LCW, 0); /* clear latched control */

    /* --- 11. core-address extension bits 5-6 (18-bit assembly) ----------- *
     * Verified through the two-read RCA form: low 16 then high 8, where the
     * high 8 carries control bits 5-6 as address bits 16-17.                */
    dev->Write(dev, dev->startAddress + CDC_REG_LCA, 0xBEEF);
    dev->Write(dev, dev->startAddress + CDC_REG_LCW, CDC_CTRL_ADDR16 | CDC_CTRL_ADDR17); /* no activate */
    CHECK(d->control.bits.addressBit16 == 1 && d->control.bits.addressBit17 == 1,
          "control decode: core-address bits 5-6 (addr 16-17)");
    uint16_t rcaLow  = dev->Read(dev, dev->startAddress + CDC_REG_RCA);
    uint16_t rcaHigh = dev->Read(dev, dev->startAddress + CDC_REG_RCA);
    CHECK(rcaLow == 0xBEEF, "RCA two-read: first read is the low 16 bits");
    CHECK(rcaHigh == 0x03,  "RCA two-read: second read is the high 8 (control bits 5-6 => 3)");
    /* a read status re-initialises the RCA read sequence (MANUAL-N100 p.188) */
    (void)status(dev);
    CHECK(dev->Read(dev, dev->startAddress + CDC_REG_RCA) == 0xBEEF,
          "read-status re-inits the RCA sequence back to the low 16 bits");
    dev->Write(dev, dev->startAddress + CDC_REG_LCW, 0); /* clear control */

    /* --- 12. TEST MODE self-test (MANUAL-N10 p.14) --------------------- *
     * Read-with-Test, block address == 125252, returns pre-wired words:     *
     * even = 125252 (octal), odd = 052525 (octal).                          */
    memset(g_fakeMem, 0, sizeof(g_fakeMem));
    program(dev, 0x0B00, (uint16_t)CDC_TESTMODE_BLOCK, 6,
            (uint16_t)(modus_go(CDC_OP_READ) | CDC_CTRL_TEST)); /* read + test mode */
    dev->Tick(dev);
    CHECK((status(dev) & CDC_STATUS_ERR) == 0, "test-mode read with block 125252 succeeds (no ERR)");
    CHECK(g_fakeMem[0x0B00 + 0] == (uint16_t)CDC_TESTMODE_EVEN &&
          g_fakeMem[0x0B00 + 2] == (uint16_t)CDC_TESTMODE_EVEN &&
          g_fakeMem[0x0B00 + 4] == (uint16_t)CDC_TESTMODE_EVEN,
          "test-mode even words are 125252 (octal)");
    CHECK(g_fakeMem[0x0B00 + 1] == (uint16_t)CDC_TESTMODE_ODD &&
          g_fakeMem[0x0B00 + 3] == (uint16_t)CDC_TESTMODE_ODD &&
          g_fakeMem[0x0B00 + 5] == (uint16_t)CDC_TESTMODE_ODD,
          "test-mode odd words are 052525 (octal)");
    CHECK((uint16_t)CDC_TESTMODE_EVEN == 0xAAAAu && (uint16_t)CDC_TESTMODE_ODD == 0x5555u,
          "test-mode words are the 0xAAAA / 0x5555 bit patterns");
    /* test mode + SEEK (506) returns the loaded block address (Read Block Address) */
    CHECK(dev->Read(dev, dev->startAddress + CDC_REG_SEEK) == (uint16_t)CDC_TESTMODE_BLOCK,
          "SEEK (506) with test mode returns the loaded block address");
    /* test-mode read with the WRONG block address flags an error */
    memset(g_fakeMem, 0, sizeof(g_fakeMem));
    program(dev, 0x0C00, 0123, 4, (uint16_t)(modus_go(CDC_OP_READ) | CDC_CTRL_TEST));
    CHECK((status(dev) & CDC_STATUS_ERR) != 0, "test-mode read with wrong block sets ERR");
    dev->Tick(dev);
    dev->Write(dev, dev->startAddress + CDC_REG_LCW, CDC_CTRL_DEVCLEAR); /* leave clean */

    /* --- 13. COMPARE operation (op 11) round-trip ---------------------- */
    {
        uint32_t sectorD = 0120;
        memset(g_fakeMem, 0, sizeof(g_fakeMem));
        for (uint16_t i = 0; i < 12; i++) g_fakeMem[0x0D00 + i] = (uint16_t)(0xD000 + i);
        /* First WRITE the pattern to the disc. */
        program(dev, 0x0D00, (uint16_t)sectorD, 12, modus_go(CDC_OP_WRITE));
        dev->Tick(dev);
        /* COMPARE the SAME memory vs disc -> no compare error. */
        program(dev, 0x0D00, (uint16_t)sectorD, 12, modus_go(CDC_OP_COMPARE));
        CHECK(d->status.bits.compareError == 0 && (status(dev) & CDC_STATUS_ERR) == 0,
              "COMPARE of matching data reports no error");
        dev->Tick(dev);
        /* Corrupt one memory word and COMPARE again -> compare error + errorOr. */
        g_fakeMem[0x0D00 + 5] ^= 0xFFFF;
        program(dev, 0x0D00, (uint16_t)sectorD, 12, modus_go(CDC_OP_COMPARE));
        CHECK(d->status.bits.compareError == 1 && (status(dev) & CDC_STATUS_ERR) != 0,
              "COMPARE of mismatching data sets compare error + inclusive-OR error");
        dev->Tick(dev);
        dev->Write(dev, dev->startAddress + CDC_REG_LCW, CDC_CTRL_DEVCLEAR);
    }

    /* --- 14. READ-PARITY operation (op 10) is a no-op success ---------- */
    {
        uint32_t sectorE = 0121;
        seed_sector(d, sectorE, 0xE000);
        memset(g_fakeMem, 0xFF, sizeof(g_fakeMem));      /* would-be-visible if it wrote */
        program(dev, 0x0E00, (uint16_t)sectorE, 8, modus_go(CDC_OP_READ_PARITY));
        dev->Tick(dev);
        CHECK((status(dev) & CDC_STATUS_ERR) == 0, "READ-PARITY succeeds (no injected CRC fault)");
        CHECK(g_fakeMem[0x0E00] == 0xFFFF,
              "READ-PARITY performs no memory transfer (core untouched)");
    }

    if (dev->Destroy) dev->Destroy(dev);

    free(dev->deviceData);   /* Device_Destroy() does this in the emulator */

    free(dev);

    /* --- 9. backing-file round-trip (AttachBacking / EnsureSurface / persist) --- */
    {
        const char *path = "test_cdc_backing.img";
        remove(path);
        CdcDevice_SetBackingFile(path);
        Device *d1 = CreateCdcDevice(0);          /* creates a fresh zero-filled image */
        CHECK(d1 != NULL, "device with new backing file created");
        if (d1) {
            CdcData *cd1 = (CdcData *)d1->deviceData;
            CHECK(cd1->backingFile != NULL, "backing file opened/created");
            /* WRITE a sector, then Destroy -> persists big-endian to the file. */
            for (uint16_t i = 0; i < 32; i++) g_fakeMem[0x0900 + i] = (uint16_t)(0xC100 + i);
            program(d1, 0x0900, 0130, 32, modus_go(CDC_OP_WRITE));
            d1->Tick(d1);
            if (d1->Destroy) d1->Destroy(d1);
            free(d1->deviceData);   /* Device_Destroy() does this in the emulator */
            free(d1);
        }

        /* Re-open the SAME file: the previously written sector must load back. */
        CdcDevice_SetBackingFile(path);
        Device *d2 = CreateCdcDevice(0);
        CHECK(d2 != NULL, "device re-created from existing backing file");
        if (d2) {
            memset(g_fakeMem, 0, sizeof(g_fakeMem));
            program(d2, 0x0A00, 0130, 32, modus_go(CDC_OP_READ));
            d2->Tick(d2);
            int okP = 1;
            for (uint16_t i = 0; i < 32; i++)
                if (g_fakeMem[0x0A00 + i] != (uint16_t)(0xC100 + i)) okP = 0;
            CHECK(okP, "backing file persisted the WRITE across close/re-open (big-endian)");
            if (d2->Destroy) d2->Destroy(d2);
            free(d2->deviceData);   /* Device_Destroy() does this in the emulator */
            free(d2);
        }
        CdcDevice_SetBackingFile(NULL);           /* leave the static clean */
        remove(path);
    }

    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
