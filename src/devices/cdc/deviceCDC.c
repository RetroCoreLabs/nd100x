/*
 * nd100x - ND100 Virtual Machine
 *
 * CDC / NCR cartridge system-disc controller for NORD TSS (IOX 500-507).
 * See deviceCDC.h for the full register map and bit model (with manual
 * citations) and <TSS>\docs\CDC-DISC-DEVICE.md for the wire
 * protocol. Modelled structurally on src/devices/drum/deviceDrum.c and
 * src/devices/smd/deviceSMD.c.
 *
 * Register/bit SEMANTICS come from the AUTHORITATIVE manuals:
 *   [MANUAL-N10]  ND-11.008.01 CARTRIDGE DISC SYSTEM FOR NORD-10 (PRIMARY -
 *                 this is the controller TSS drives).
 *   [MANUAL-N100] ND-06.016.01 NORD-100 I/O System pp.188-190 (status names).
 * TSS's driver is the behavioural ground truth for the bits it reads
 * (busy/error/on-cylinder). Claims tagged [VERIFIED] / [INFERRED] below.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "deviceCDC.h"
#include "devices_protos.h"

/* Forward declarations (fixed signatures from the Device struct). */
static void     Cdc_Reset(Device *self);
static uint16_t Cdc_Read(Device *self, uint32_t address);
static void     Cdc_Write(Device *self, uint32_t address, uint16_t value);
static uint16_t Cdc_Tick(Device *self);
static uint16_t Cdc_Ident(Device *self, uint16_t level);
static void     Cdc_Destroy(Device *self);
static void     Cdc_ExecuteGO(Device *self);
static bool     Cdc_End(void *context, int param);

/* Path for the next CreateCdcDevice() to attach; NULL = in-memory only. */
static char g_cdcBackingPath[1024];
static int  g_cdcHasBackingPath = 0;

void CdcDevice_SetBackingFile(const char *path)
{
    if (path && path[0])
    {
        snprintf(g_cdcBackingPath, sizeof(g_cdcBackingPath), "%s", path);
        g_cdcHasBackingPath = 1;
    }
    else
    {
        g_cdcHasBackingPath = 0;
    }
}

/* ------------------------------------------------------------------------------
 * Disc-address seam. The driver has ALREADY run the logical overlay sector
 * through DKADR (logical -> physical) before loading LBA, so the LBA value is a
 * LINEAR PHYSICAL 256-word sector number and this is the identity. See the
 * ADDRESSING NOTE in deviceCDC.h. Kept as a tiny named function so the seam is
 * unmistakable.
 * ------------------------------------------------------------------------------ */
static uint32_t cdc_lba_to_sector(uint16_t lba)
{
    return (uint32_t)lba;
}

/* ------------------------------------------------------------------------------
 * Assemble the effective DMA core address.
 * [VERIFIED - MANUAL-N10 p.14] 18-bit: low 16 bits from Load Core Address (501)
 * plus control-word bits 5-6 as address bits 16-17. TSS never sets bits 5-6, so
 * for the overlay path this returns exactly the 16-bit LCA value (no regression).
 * coreAddrHigh is the ND-100 24-bit extension and is 0 for TSS (see note in .h).
 * ------------------------------------------------------------------------------ */
static uint32_t cdc_effective_core(const CdcData *d)
{
    uint32_t addr = (uint32_t)d->coreAddrLow;                      /* bits 0-15   */
    addr |= (uint32_t)d->control.bits.addressBit16 << 16;          /* bit 16 (CW5)*/
    addr |= (uint32_t)d->control.bits.addressBit17 << 17;          /* bit 17 (CW6)*/
    addr |= (uint32_t)d->coreAddrHigh << 16;                       /* ND-100 hi-8 */
    return addr;
}

/* Grow the surface (zero-filled) so it holds at least 'sectors' sectors. */
static int Cdc_EnsureSurface(CdcData *d, uint32_t sectors)
{
    if (sectors <= d->surfaceSectors)
        return 1;
    uint32_t newWords = sectors * CDC_WORDS_PER_SECTOR;
    uint16_t *grown = (uint16_t *)realloc(d->surface, newWords * sizeof(uint16_t));
    if (!grown)
        return 0;
    /* zero the freshly added tail */
    memset(grown + d->surfaceWords, 0, (newWords - d->surfaceWords) * sizeof(uint16_t));
    d->surface = grown;
    d->surfaceSectors = sectors;
    d->surfaceWords = newWords;
    return 1;
}

/* Load a big-endian (ND word order) disc image into the surface, or create it
 * zero-filled if it does not yet exist. Sector S lives at byte offset S*512.
 * The FILE* is kept open for optional write-back in Cdc_Destroy. Returns 1 ok. */
static int Cdc_AttachBacking(CdcData *d, const char *path)
{
    FILE *f = fopen(path, "rb+");   /* existing file, read/write */
    if (!f)
    {
        /* Create a fresh, empty disc image at the current default surface size. */
        f = fopen(path, "wb+");
        if (!f)
            return 0;
        for (uint32_t i = 0; i < d->surfaceWords; i++)
        {
            putc(0, f);            /* high byte */
            putc(0, f);            /* low byte  */
        }
        fflush(f);
    }
    else
    {
        /* Measure the file so the surface can be grown to hold all of it. */
        long fileBytes = 0;
        if (fseek(f, 0, SEEK_END) == 0)
        {
            fileBytes = ftell(f);
            fseek(f, 0, SEEK_SET);
        }
        if (fileBytes > 0)
        {
            /* Round up to whole 256-word sectors and ensure the surface fits. */
            uint32_t fileSectors =
                (uint32_t)((fileBytes + CDC_SECTOR_BYTES - 1) / CDC_SECTOR_BYTES);
            if (!Cdc_EnsureSurface(d, fileSectors))
            {
                fclose(f);
                return 0;
            }
        }
        /* Load big-endian words into the surface (up to surfaceWords). */
        for (uint32_t i = 0; i < d->surfaceWords; i++)
        {
            int hi = getc(f);
            int lo = getc(f);
            if (hi == EOF || lo == EOF)
                break;             /* short file: leave the rest zeroed */
            d->surface[i] = (uint16_t)((hi << 8) | lo);
        }
    }
    d->backingFile = f;
    return 1;
}

/* ----- register read (even IOX device numbers) -------------------------- */
static uint16_t Cdc_Read(Device *self, uint32_t address)
{
    if (!self)
        return 0;
    CdcData *d = (CdcData *)self->deviceData;
    uint32_t reg = address - self->startAddress;

    switch (reg)
    {
    case CDC_REG_RST:               /* IOX 504 Read Status - the register DWAIT polls */
        /* [VERIFIED - MANUAL-N100 p.188 footnote] a read-status re-initialises
         * the two-access address read sequence. */
        d->rcaReadPhase = 0;
        return d->status.raw;

    case CDC_REG_RCA:               /* IOX 500 Read Core Address */
        /* [MANUAL-N100 p.188] two-read 24-bit form: first read = low 16 bits,
         * second consecutive read = high 8 bits. A lone read (TSS / the maint.
         * check) always yields the low 16 - so single-read callers are unharmed.
         * [MANUAL-N10 p.16] "included for maintenance ... check the CAR is
         * counting correctly". */
        if (d->rcaReadPhase == 0)
        {
            d->rcaReadPhase = 1;
            return (uint16_t)(cdc_effective_core(d) & 0xFFFFu);       /* low 16  */
        }
        d->rcaReadPhase = 0;
        return (uint16_t)((cdc_effective_core(d) >> 16) & 0x00FFu);   /* high 8  */

    case CDC_REG_RSECT:             /* IOX 502 Read Sector Counter */
        /* [MANUAL-N10 p.17] only bits 0-4 are relevant (rotational position). */
        return d->sectorCounter;

    case CDC_REG_SEEK:              /* IOX 506 Seek / (test mode) Read Block Address */
        /* [VERIFIED - MANUAL-N10 p.13 and MANUAL-N100 p.189] "Load the control
         * word with bit 3 (Test Mode) set. IOX 506 will then return the
         * previously loaded block address to the A-register." */
        if (d->control.bits.testMode)
            return d->blockAddress;
        /* Otherwise this is a Seek. An emulated fixed image is always
         * on-cylinder; positioning is a no-op, so return status so a driver that
         * reads SEEK sees a ready device (TSS's XDISK does exactly this). */
        return d->status.raw;

    default:
        return 0;
    }
}

/* ----- register write (odd IOX device numbers) -------------------------- */
static void Cdc_Write(Device *self, uint32_t address, uint16_t value)
{
    if (!self)
        return;
    CdcData *d = (CdcData *)self->deviceData;
    uint32_t reg = address - self->startAddress;

    switch (reg)
    {
    case CDC_REG_LCA:               /* IOX 501 Load Core Address */
        /* [VERIFIED - TSS] TSS loads the whole (16-bit) core address in ONE
         * write; the NORD-10 controller extends it to 18 bits via control-word
         * bits 5-6 (see cdc_effective_core). We deliberately do NOT model the
         * ND-100 two-write high-8/low-16 form here (a single TSS write would then
         * load only the high 8 bits and corrupt the address). A load re-inits the
         * RCA read sequence. */
        d->coreAddrLow  = value;
        d->rcaReadPhase = 0;
        break;

    case CDC_REG_LBA:               /* IOX 503 Load Block (disc) Address */
        d->blockAddress = value;
        break;

    case CDC_REG_LWC:               /* IOX 507 Load Word Count Register */
        d->wordCount = value;
        break;

    case CDC_REG_LCW:               /* IOX 505 Load Control Word - starts a transfer */
        d->control.raw = value;
        if (d->control.bits.activate)
        {
            /* [VERIFIED - MANUAL-N10 p.12/p.19] bit 2 (activate) starts the
             * transfer. There is no separate GO strobe on the N10 path.
             *
             * [INFERRED] The manual gates the CPU interrupt on control bits 0/1
             * (enable ready/error interrupt). TSS polls DWAIT and never enables
             * them, so gating is invisible to TSS; we always signal completion on
             * the interrupt line (matching the drum/SMD devices and the unit
             * test), which is behaviourally correct for TSS. */
            d->interruptEnabled = true;
            Cdc_ExecuteGO(self);
        }
        else if (d->control.bits.deviceClear)
        {
            /* [VERIFIED - MANUAL-N10 p.14 bit 4; TSS2.SYMB:557 / TSS1.SYMB:3543
             * "SAA 20; IOX LMR"] device clear (0o20 written alone) clears the
             * active flip-flop and the controller error state. */
            d->status.bits.active           = 0;
            d->status.bits.errorOr          = 0;
            d->status.bits.transferOn       = 0;
            d->status.bits.writeProtect     = 0;
            d->status.bits.timeOut          = 0;
            d->status.bits.hardwareError    = 0;
            d->status.bits.addressMismatch  = 0;
            d->status.bits.parityError      = 0;
            d->status.bits.compareError     = 0;
            d->status.bits.dmaError         = 0;
            d->interruptEnabled = false;
            d->rcaReadPhase     = 0;
        }
        break;

    default:
        break;
    }
}

/* ----- the transfer engine (triggered by a control-word write with ACTIVATE) - */
static void Cdc_ExecuteGO(Device *self)
{
    CdcData *d = (CdcData *)self->deviceData;

    /* Device operation = plain 2-bit field at control bits 11-12 (00=read 01=write
     * 10=parity 11=cmp), per the ND manual (ND-11.008.01 / ND-06.016.01). [VERIFIED
     * live] with the mac-c SHR fix DKTR now emits 000004 for the overlay READ -> op 0.
     * (The earlier (op+1)<<12 model was an artifact of the mac-c SHR compiler bug that
     * had made DKTR emit 010004; that bug is fixed. See deviceCDC.h NOTE.) */
    uint16_t op      = (uint16_t)d->control.bits.deviceOperation;
    uint32_t sector  = cdc_lba_to_sector(d->blockAddress);
    uint32_t wordOff = sector * CDC_WORDS_PER_SECTOR;
    uint32_t count   = d->wordCount;
    uint32_t core    = cdc_effective_core(d);

    d->sectorCounter = (uint16_t)sector;

    /* Temporary transfer trace: set ND100X_CDC_TRACE=1 to see every request.
     * Diagnostic aid, off unless the variable is present. */
    {
        static int traceOn = -1;
        if (traceOn < 0) traceOn = getenv("ND100X_CDC_TRACE") ? 1 : 0;
        if (traceOn)
            fprintf(stderr, "[cdc] op=%u blockAddr=%06o sector=%u core=%06o "
                            "count=%u surfaceSectors=%u%s\n",
                    op, d->blockAddress, sector, core, count, d->surfaceSectors,
                    (wordOff > d->surfaceWords ||
                     count > d->surfaceWords - wordOff) ? "  <-- OUT OF RANGE" : "");
    }

    /* Fresh transfer: clear the error bits and completion, mark active/on. */
    d->status.bits.errorOr          = 0;
    d->status.bits.writeProtect     = 0;
    d->status.bits.timeOut          = 0;
    d->status.bits.hardwareError    = 0;
    d->status.bits.addressMismatch  = 0;
    d->status.bits.parityError      = 0;
    d->status.bits.compareError     = 0;
    d->status.bits.dmaError         = 0;
    d->status.bits.transferComplete = 0;
    d->status.bits.active           = 1;   /* BUSY (bit 2) */
    d->status.bits.transferOn       = 1;   /* transfer on (bit 13) */

    /* ---- TEST MODE (control bit 3): pre-wired self-test, no real surface ----
     * [VERIFIED - MANUAL-N10 p.14] A Read Transfer with Test returns pre-wired
     * data (even words 125252, odd words 052525), and a successful test transfer
     * requires the block address register == 125252. */
    if (d->control.bits.testMode && op == CDC_OP_READ)
    {
        if (d->blockAddress == CDC_TESTMODE_BLOCK)
        {
            for (uint32_t i = 0; i < count; i++)
            {
                uint16_t w = (i & 1u) ? (uint16_t)CDC_TESTMODE_ODD
                                      : (uint16_t)CDC_TESTMODE_EVEN;
                Device_DMAWrite(core + i, w);
            }
        }
        else
        {
            /* Wrong block during test -> the manual's "successful transfer"
             * precondition fails; flag address mismatch. */
            d->status.bits.addressMismatch = 1;
            d->status.bits.errorOr         = 1;
        }
        Device_QueueIODelay(self, IODELAY_HDD_SMD,
                            (IODelayedCallback)Cdc_End, 0, self->interruptLevel);
        return;
    }

    /* Bounds-check the whole transfer against the surface. A read/write past the
     * end of the image is an error (matches a real illegal-address status). */
    if (wordOff > d->surfaceWords || count > d->surfaceWords - wordOff)
    {
        d->status.bits.errorOr         = 1;
        d->status.bits.addressMismatch = 1;   /* seek/address out of range */
        /* Still raise completion so the driver takes its error exit. */
        Device_QueueIODelay(self, IODELAY_HDD_SMD,
                            (IODelayedCallback)Cdc_End, 0, self->interruptLevel);
        return;
    }

    /* ---- device operation (decoded above from the 2-bit field, bits 11-12) --- */
    switch (op)
    {
    case CDC_OP_READ: /* disc -> core (the overlay load path; control word 000004) */
        for (uint32_t i = 0; i < count; i++)
            Device_DMAWrite(core + i, d->surface[wordOff + i]);
        break;

    case CDC_OP_WRITE: /* 01: core -> disc */
        for (uint32_t i = 0; i < count; i++)
        {
            int32_t w = Device_DMARead(core + i);
            d->surface[wordOff + i] = (uint16_t)(w & 0xFFFF);
        }
        /* WRITE-THROUGH: persist the just-written sector(s) to the backing
         * file immediately (big-endian / ND word order) and flush, so a
         * non-clean stop (SIGINT, crash, kill) can NEVER lose disc writes.
         * Previously the surface was written back only in Cdc_Destroy, which
         * the SIGINT handler's exit(0) skips -> lost format/cold-start writes. */
        if (d->backingFile)
        {
            fseek(d->backingFile, (long)wordOff * 2L, SEEK_SET);
            for (uint32_t i = 0; i < count; i++)
            {
                putc((d->surface[wordOff + i] >> 8) & 0xFF, d->backingFile);
                putc(d->surface[wordOff + i] & 0xFF, d->backingFile);
            }
            fflush(d->backingFile);
        }
        break;

    case CDC_OP_READ_PARITY:
        /* [MANUAL-N10 p.15] "No data transfer to or from the computer is
         * performed"; the controller only re-reads the disc and checks the CRC.
         * The emulated surface has no injected CRC faults, so this always
         * succeeds (no parityError set). Model as a no-op success. */
        break;

    case CDC_OP_COMPARE: /* 11: compare disc vs core bit-by-bit [MANUAL-N10 p.15] */
        for (uint32_t i = 0; i < count; i++)
        {
            int32_t mem = Device_DMARead(core + i);
            if ((uint16_t)(mem & 0xFFFF) != d->surface[wordOff + i])
            {
                d->status.bits.compareError = 1; /* bit 10 */
                d->status.bits.errorOr      = 1; /* bit 4  */
                break;
            }
        }
        break;

    default:
        break;
    }

    /* Queue the delayed completion, exactly like the drum/SMD. The callback
     * clears BUSY and (returning true) raises the level-11 interrupt. */
    Device_QueueIODelay(self, IODELAY_HDD_SMD,
                        (IODelayedCallback)Cdc_End, 0, self->interruptLevel);
}

/* ----- delayed completion: clears busy, requests the interrupt ---------- */
static bool Cdc_End(void *context, int param)
{
    (void)param;
    Device *self = (Device *)context;
    if (!self)
        return false;
    CdcData *d = (CdcData *)self->deviceData;
    if (!d)
        return false;

    /* Transfer finished: [MANUAL-N100 p.190] clear device active (bit 2) and
     * transfer-on (bit 13), set transfer-complete (bit 12) and ready-for-transfer
     * (bit 3). Error bits already set by the engine are left in place. */
    d->status.bits.active           = 0;
    d->status.bits.transferOn       = 0;
    d->status.bits.transferComplete = 1;
    d->status.bits.readyForTransfer = 1;

    /* Signal completion on the interrupt line the SAME way the KNOWN-WORKING
     * block devices do: raise it here in the delayed-completion callback via
     * Device_SetInterruptStatus, exactly like deviceFloppyDMA.c ReadEnd()
     * (deviceFloppyDMA.c:742) and deviceSMD.c SMDReadEnd() (deviceSMD.c:1147).
     *
     * We raise it UNCONDITIONALLY (not gated on d->interruptEnabled). TSS polls
     * DWAIT for completion and never sets the manual's enable-interrupt control
     * bits 0/1 (see the [INFERRED] note in Cdc_Write's LCW case), so it drives
     * completion off d->interruptEnabled = activate. The previous code returned
     * d->interruptEnabled: a device-clear (IOX 505 = 0o20) sets that false, so if
     * a device-clear ever raced an in-flight read the queued completion here
     * would be SILENTLY SWALLOWED. Raising unconditionally in the callback
     * removes that race and matches the floppy/SMD/drum template and the unit
     * test's expectation.
     *
     * Return false because we have ALREADY raised the interrupt ourselves;
     * returning true would make Device_TickIODelay raise it a SECOND time
     * (device.c:228-231). This is precisely the floppy/SMD idiom (both return
     * false after calling Device_SetInterruptStatus). */
    Device_SetInterruptStatus(self, true, self->interruptLevel);
    (void)d; /* d->interruptEnabled no longer gates completion (kept for clear) */
    return false;
}

/* ----- housekeeping ----------------------------------------------------- */
static uint16_t Cdc_Tick(Device *self)
{
    if (!self)
        return 0;
    Device_TickIODelay(self);
    return self->interruptBits;
}

static uint16_t Cdc_Ident(Device *self, uint16_t level)
{
    if (!self)
        return 0;
    if ((self->interruptBits & (1 << level)) != 0)
    {
        /* IDENT acknowledges the interrupt: clear the pending bit for this level
         * AND drop interruptEnabled, exactly like the working devices'
         * Ident (deviceFloppyDMA.c:248 clears status1.interruptEnabled then
         * Device_SetInterruptStatus(false); deviceSMD.c:675 the same). Leaving
         * interruptEnabled armed after the acknowledge is harmless here (a fresh
         * activate re-arms it), but clearing it keeps the CDC discipline
         * bit-for-bit identical to floppy/SMD. */
        CdcData *d = (CdcData *)self->deviceData;
        if (d)
            d->interruptEnabled = false;
        Device_SetInterruptStatus(self, false, level);
        return self->identCode;
    }
    return 0;
}

static void Cdc_Reset(Device *self)
{
    if (!self)
        return;
    CdcData *d = (CdcData *)self->deviceData;
    d->coreAddrLow = 0;
    d->coreAddrHigh = 0;
    d->rcaReadPhase = 0;
    d->blockAddress = 0;
    d->wordCount = 0;
    d->control.raw = 0;
    d->sectorCounter = 0;
    d->interruptEnabled = false;
    /* Idle status: ON CYLINDER / READY set (so the pre-transfer poll
     * BSKP ONE 160 proceeds), all BUSY/ERR bits clear. */
    d->status.raw = 0;
    d->status.bits.onCylinder = 1;   /* bit 14 = READY */
    self->interruptBits = 0;
}

static void Cdc_Destroy(Device *self)
{
    if (!self)
        return;
    CdcData *d = (CdcData *)self->deviceData;
    if (d)
    {
        if (d->backingFile)
        {
            /* Persist the surface as big-endian (ND word order) before closing. */
            fseek(d->backingFile, 0, SEEK_SET);
            for (uint32_t i = 0; i < d->surfaceWords; i++)
            {
                putc((d->surface[i] >> 8) & 0xFF, d->backingFile); /* high byte */
                putc(d->surface[i] & 0xFF, d->backingFile);        /* low byte  */
            }
            fflush(d->backingFile);
            fclose(d->backingFile);
            d->backingFile = NULL;
        }
        free(d->surface);
        d->surface = NULL;
    }
}

/* ----- factory ---------------------------------------------------------- */

/* ----- boot: read sector 0 into core and start there ------------------------
 * On a real NORD-10 the LOAD button plus microcode reads the first sector of
 * the selected device into core and starts at address 0. TSS relies on exactly
 * that layout: its own LOAD-SYSTEM command (LOADV, src/TSS5.SYMB:604) does
 * SDISK(core 0, disc 0, read) followed by RCLR DP, i.e. P := 0. The sector
 * therefore has to be self-starting at word 0, which TSS's DKRST is.
 *
 * One 256-word sector is transferred, matching the CDC transfer unit. */
static int Cdc_Boot(Device *self, int unit)
{
    (void)unit;                       /* single-unit controller */
    CdcData *d = (CdcData *)self->deviceData;

    if (!d || !d->surface || d->surfaceWords < CDC_WORDS_PER_SECTOR)
    {
        printf("Error: CDC boot - no disc surface attached (use --cdc=FILE)\n");
        return -1;
    }

    /* A blank or unformatted disc has no bootstrap; starting at 0 would run
     * whatever happens to be in core. Refuse, as the SMD boot does. */
    int allZero = 1;
    for (uint32_t i = 0; i < CDC_WORDS_PER_SECTOR; i++)
    {
        if (d->surface[i] != 0) { allZero = 0; break; }
    }
    if (allZero)
    {
        printf("Error: CDC boot sector (sector 0) is all zeros - the disc "
               "carries no bootstrap\n");
        return -1;
    }

    for (uint32_t i = 0; i < CDC_WORDS_PER_SECTOR; i++)
    {
        Device_DMAWrite(i, d->surface[i]);
    }

    return 0;                         /* start address: core 0 */
}


/* ----- NORD-1 compatible access (the IOT instruction) -----------------------
 * The same controller, reached over the NORD-1 I/O channel instead of the
 * NORD-10 flat register file. TSS defines both mappings itself, side by side,
 * in src/TSS1.SYMB:3356-3379:
 *
 *   "CDC NN10                        "CDC N10
 *   DISC = DCHN+44  start transfer   (no equivalent: activate is a control bit)
 *   DCT  = DCHN+45  control port     RCA=+0 LCA=+1 RSECT=+2 LBA=+3
 *   LCA  = DCT+SNI                   RST=+4 LMR=+5 SEEK=+6  LWC=+7
 *   LBA  = DCT+ACT
 *   LMR  = DCT+SKA
 *   RST  = DCT+PIN        "READ STATUS REGISTER, SKIP IF OK"
 *   RCA  = DCT+PIN+ACT
 *   RSECT= DCT+PIN+SKA
 *   RDC  = DCT+PIN+SKA+ACT  "RESET DISK CONTROLLER"
 *   SEEK = DCT+SKA+ACT
 *
 * The eight function-bit combinations on DCT are a bijection onto the eight
 * NORD-10 registers, which is what shows these are one controller with two
 * address decodes rather than two controllers. DCHN ("DISK CHANNEL NUMBER",
 * TSS1.SYMB:43) is 100 on NORD-1 and 500 on NORD-10, so the NORD-1 numbers
 * are 144 (start) and 145 (control port).
 *
 * [INFERRED, from TSS's own comment and its use in RDKOP] RST skips when the
 * status is good; DISC+SKA skips when the controller is not busy. Everything
 * else routes to the identical register the NORD-10 path uses. */
static bool Cdc_IotOp(Device *self, uint8_t devno, uint8_t func,
                      uint16_t *regA, bool *skip)
{
    CdcData *d = (CdcData *)self->deviceData;
    enum { FN_ACT = 1, FN_SKA = 2, FN_PIN = 4 };   /* IOT bits 8,9,10 */

    if (devno == CDC_N1_DISC)               /* 144 - start transfer / ready test */
    {
        /* PIN is "prepare interrupt: turn on the interrupt system of the
         * specified device" (NORD-1 Reference Manual sec 3.7). Only PIN
         * enables interrupts. RDKOP never sets it - it polls with SKA - so
         * enabling them on ACT, as this first did, raised a completion
         * interrupt the bootstrap never asked for and was not ready to take:
         * LOADV has just done MCL PIE / INTDS and the vectors are not set up
         * again until INIT runs. */
        if (func & FN_PIN)
            d->interruptEnabled = true;

        if (func & FN_SKA)                  /* "skip if start acceptable" */
            *skip = (d->status.bits.active == 0);

        if (func & FN_ACT)                  /* activate: the same transfer the
                                             * control-word activate bit starts */
            Cdc_ExecuteGO(self);

        return true;
    }

    if (devno != CDC_N1_DCT)                /* 145 - the control port */
        return false;

    switch (func)
    {
    case 0:                                  /* SNI          -> LCA  (501) */
        Cdc_Write(self, self->startAddress + CDC_REG_LCA, *regA);   break;
    case FN_ACT:                             /* ACT          -> LBA  (503) */
        Cdc_Write(self, self->startAddress + CDC_REG_LBA, *regA);   break;
    case FN_SKA:                             /* SKA -> LMR, "load modus register"
                                              *
                                              * [INFERRED - and this is the one
                                              * place the two decodes are NOT a
                                              * straight 1:1 map.] The NORD-1
                                              * side has no separate word-count
                                              * register; the NORD-10 side has
                                              * both LMR (505, control) and LWC
                                              * (507, word count). RDKOP loads
                                              * 0400 = 256 here, exactly one
                                              * sector, and then starts the
                                              * transfer with ACT on device 144
                                              * - so this value is the transfer
                                              * length, not a control word.
                                              * Mapping it to LCW instead left
                                              * wordCount at 0, every transfer
                                              * moved nothing, and the reloaded
                                              * system span in its disc driver. */
        Cdc_Write(self, self->startAddress + CDC_REG_LWC, *regA);   break;
    case (uint8_t)(FN_SKA | FN_ACT):         /* SEEK         -> 506        */
        *regA = Cdc_Read(self, self->startAddress + CDC_REG_SEEK);  break;
    case FN_PIN:                             /* RST, skip if OK -> 504     */
        *regA = Cdc_Read(self, self->startAddress + CDC_REG_RST);
        *skip = (d->status.bits.errorOr == 0);
        break;
    case (uint8_t)(FN_PIN | FN_ACT):         /* RCA          -> 500        */
        *regA = Cdc_Read(self, self->startAddress + CDC_REG_RCA);   break;
    case (uint8_t)(FN_PIN | FN_SKA):         /* RSECT        -> 502        */
        *regA = Cdc_Read(self, self->startAddress + CDC_REG_RSECT); break;
    case (uint8_t)(FN_PIN | FN_SKA | FN_ACT):/* RDC: reset the controller  */
        Cdc_Reset(self);
        break;
    default:
        return false;
    }
    return true;
}

Device *CreateCdcDevice(uint8_t thumbwheel)
{
    Device *dev = (Device *)malloc(sizeof(Device));
    if (!dev)
        return NULL;

    CdcData *d = (CdcData *)malloc(sizeof(CdcData));
    if (!d)
    {
        free(dev);
        return NULL;
    }
    memset(d, 0, sizeof(CdcData));

    /* Standard (non-block) class: the disc DMAs directly from its own surface,
     * so it needs no machine block callbacks and avoids the type-keyed mount
     * plumbing (which only knows DRIVE_SMD/DRIVE_FLOPPY). */
    Device_Init(dev, thumbwheel, DEVICE_CLASS_STANDARD, 0);
    dev->deviceData = d;
    dev->type = DEVICE_TYPE_CDC;
    dev->Boot = Cdc_Boot;
    dev->IotOp = Cdc_IotOp;                 /* NORD-1 IOT access */
    dev->nord1Device = CDC_N1_DISC;         /* 144 and 145       */
    dev->nord1DeviceCount = 2;

    /* Allocate and zero the default surface; a larger backing file grows it. */
    d->surfaceSectors = CDC_DEFAULT_SECTORS;
    d->surfaceWords = d->surfaceSectors * CDC_WORDS_PER_SECTOR;
    d->surface = (uint16_t *)calloc(d->surfaceWords, sizeof(uint16_t));
    if (!d->surface)
    {
        free(d);
        free(dev);
        return NULL;
    }
    d->backingFile = NULL;
    if (g_cdcHasBackingPath)
    {
        if (!Cdc_AttachBacking(d, g_cdcBackingPath))
            printf("CDC: WARNING could not open backing file '%s' (in-memory only)\n",
                   g_cdcBackingPath);
    }

    dev->Read = Cdc_Read;
    dev->Write = Cdc_Write;
    dev->Tick = Cdc_Tick;
    dev->Reset = Cdc_Reset;
    dev->Ident = Cdc_Ident;
    dev->Destroy = Cdc_Destroy;
    Cdc_Reset(dev);

    /* Address block and identity. thumbwheel 0 -> the TSS system disc @ 500. */
    switch (thumbwheel)
    {
    case 0:
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "CDC DISC 500");
        dev->startAddress = 0500;
        break;
    default:
        printf("CDC: unknown thumbwheel value: %d\n", thumbwheel);
        free(d->surface);
        free(d);
        free(dev);
        return NULL;
    }
    dev->endAddress = dev->startAddress + 7;   /* answers 500..507 */
    dev->identCode = CDC_IDENT_CODE;           /* [VERIFIED] ND 500-slot ident 01 */
    dev->interruptLevel = CDC_INT_LEVEL;       /* [VERIFIED] ND 500-slot level 11 */

    printf("CDC disc device created: %s ident %o level %d (%u sectors, %u words surface)\n",
           dev->memoryName, dev->identCode, dev->interruptLevel,
           d->surfaceSectors, d->surfaceWords);
    return dev;
}
