/*
 * nd100x - ND100 Virtual Machine
 *
 * Swapping DRUM controller for NORD TSS (IOX 540-547).
 * See deviceDrum.h and docs/DRUM-DEVICE-SPEC.md for the wire protocol.
 * Modelled on src/devices/smd/deviceSMD.c (the DMA device template).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "deviceDrum.h"
#include "devices_protos.h"

/* Forward declarations */
static void     Drum_Reset(Device *self);
static uint16_t Drum_Read(Device *self, uint32_t address);
static void     Drum_Write(Device *self, uint32_t address, uint16_t value);
static uint16_t Drum_Tick(Device *self);
static uint16_t Drum_Ident(Device *self, uint16_t level);
static void     Drum_Destroy(Device *self);
static void     Drum_ExecuteGO(Device *self);
static bool     Drum_End(void *context, int param);

/* Path for the next CreateDrumDevice() to attach; NULL = in-memory only. */
static char g_drumBackingPath[1024];
static int  g_drumHasBackingPath = 0;

void DrumDevice_SetBackingFile(const char *path)
{
    if (path && path[0])
    {
        snprintf(g_drumBackingPath, sizeof(g_drumBackingPath), "%s", path);
        g_drumHasBackingPath = 1;
    }
    else
    {
        g_drumHasBackingPath = 0;
    }
}

/* Load a big-endian (ND word order) drum image into the surface, or create it
 * zero-filled at the full DRUM_MAX_WORDS size if it does not yet exist.
 * Keeps the FILE* open for write-back in Drum_Destroy. Returns 1 on success. */
static int Drum_AttachBacking(DrumData *d, const char *path)
{
    FILE *f = fopen(path, "rb+");   /* existing file, read/write */
    if (!f)
    {
        /* Create a fresh, empty drum image of the maximum size. */
        f = fopen(path, "wb+");
        if (!f)
            return 0;
        /* The surface is already calloc'd to zero; persist that as the image. */
        for (uint32_t i = 0; i < d->surfaceWords; i++)
        {
            putc(0, f);            /* high byte */
            putc(0, f);            /* low byte  */
        }
        fflush(f);
    }
    else
    {
        /* Load existing big-endian words into the surface (up to surfaceWords). */
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
static uint16_t Drum_Read(Device *self, uint32_t address)
{
    if (!self)
        return 0;
    DrumData *d = (DrumData *)self->deviceData;
    uint32_t reg = address - self->startAddress;

    switch (reg)
    {
    case DRUM_REG_READ_STATUS:      /* 544 RSX - the only register TSS reads */
        return d->status;
    case DRUM_REG_READ_CORE:        /* 540 RCX - defined but XDRUM never uses */
        return (uint16_t)(d->coreAddress & 0xFFFF);
    default:
        return 0;
    }
}

/* ----- register write (odd IOX device numbers) -------------------------- */
static void Drum_Write(Device *self, uint32_t address, uint16_t value)
{
    if (!self)
        return;
    DrumData *d = (DrumData *)self->deviceData;
    uint32_t reg = address - self->startAddress;

    switch (reg)
    {
    case DRUM_REG_LOAD_CORE:        /* 541 LCX - low 16 bits of core address */
        /* high bits 16-17 are supplied by the control word, keep them */
        d->coreAddress = (d->coreAddress & 0x30000u) | value;
        break;
    case DRUM_REG_LOAD_BLOCK:       /* 543 LBX - drum block address          */
        d->blockAddress = value;
        break;
    case DRUM_REG_LOAD_WORDCNT:     /* 547 LWX - word count                  */
        d->wordCount = value;
        break;
    case DRUM_REG_LOAD_CONTROL:     /* 545 LCR - control; may trigger a go   */
        d->control = value;
        /* Fold in the extended core-address bits 16-17 from the control word. */
        d->coreAddress = (d->coreAddress & 0xFFFFu)
                       | ((uint32_t)DRUM_CTRL_ADDR_HI(value) << 16);
        if ((value & DRUM_CTRL_GO_MASK) == DRUM_CTRL_GO_VALUE)
        {
            /* bits 0-2 == 7 : activate transfer, interrupt on completion */
            d->interruptEnabled = true;
            Drum_ExecuteGO(self);
        }
        else if (value == DRUM_CTRL_CLEAR)
        {
            /* control == 4 (error path): stop/clear the device */
            d->status &= (uint16_t)~DRUM_STATUS_DVA;
            d->interruptEnabled = false;
        }
        break;
    default:
        break;
    }
}

/* ----- the transfer engine (triggered by a "go" control write) ---------- */
static void Drum_ExecuteGO(Device *self)
{
    DrumData *d = (DrumData *)self->deviceData;

    uint16_t func   = DRUM_CTRL_FUNC(d->control);
    uint16_t sector = (uint16_t)DRUM_BLOCK_SECTOR(d->blockAddress);
    uint16_t track  = (uint16_t)DRUM_BLOCK_TRACK(d->blockAddress);

    /* Linear word offset into the drum surface.
     * [VERIFIED sector/track split] + [PROVISIONAL words-per-sector], see .h */
    uint32_t wordOffset = ((uint32_t)track * DRUM_SECTORS_PER_TRACK + sector)
                          * DRUM_WORDS_PER_SECTOR;
    uint32_t count = d->wordCount;

    /* Clear any previous error, mark the device active. */
    d->status &= (uint16_t)~DRUM_STATUS_ERR;
    d->status |= DRUM_STATUS_DVA;

    /* Bounds-check the whole transfer against the drum surface. */
    if (wordOffset > d->surfaceWords || count > d->surfaceWords - wordOffset)
    {
        d->status |= DRUM_STATUS_ERR;
        /* Still raise completion so the driver takes its error exit. */
        Device_QueueIODelay(self, IODELAY_HDD_SMD,
                            (IODelayedCallback)Drum_End, 0, self->interruptLevel);
        return;
    }

    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t core = d->coreAddress + i;   /* DMA into physical memory */
        switch (func)
        {
        case DRUM_FUNC_READ:                  /* drum -> memory */
            Device_DMAWrite(core, d->surface[wordOffset + i]);
            break;
        case DRUM_FUNC_WRITE:                 /* memory -> drum */
        {
            int32_t w = Device_DMARead(core);
            d->surface[wordOffset + i] = (uint16_t)(w & 0xFFFF);
            break;
        }
        case DRUM_FUNC_READ_TEST:             /* read drum, no store */
            (void)d->surface[wordOffset + i];
            break;
        case DRUM_FUNC_COMPARE:               /* compare drum vs memory */
        {
            int32_t w = Device_DMARead(core);
            if ((uint16_t)(w & 0xFFFF) != d->surface[wordOffset + i])
                d->status |= DRUM_STATUS_ERR;
            break;
        }
        default:
            break;
        }
    }

    /* WRITE-THROUGH: for a WRITE, persist the transferred range to the backing
     * file immediately (big-endian) and flush, so no non-clean stop can lose
     * drum writes (previously written back only in Drum_Destroy). */
    if (func == DRUM_FUNC_WRITE && d->backingFile)
    {
        fseek(d->backingFile, (long)wordOffset * 2L, SEEK_SET);
        for (uint32_t i = 0; i < count; i++)
        {
            putc((d->surface[wordOffset + i] >> 8) & 0xFF, d->backingFile);
            putc(d->surface[wordOffset + i] & 0xFF, d->backingFile);
        }
        fflush(d->backingFile);
    }

    /* Queue the delayed completion, exactly like SMD. The callback clears DVA
     * and (returning true) raises the level-11 interrupt. */
    Device_QueueIODelay(self, IODELAY_HDD_SMD,
                        (IODelayedCallback)Drum_End, 0, self->interruptLevel);
}

/* ----- delayed completion: clears busy, requests the interrupt ---------- */
static bool Drum_End(void *context, int param)
{
    (void)param;
    Device *self = (Device *)context;
    if (!self)
        return false;
    DrumData *d = (DrumData *)self->deviceData;
    if (!d)
        return false;

    d->status &= (uint16_t)~DRUM_STATUS_DVA;   /* no longer active */

    /* Returning true tells Device_TickIODelay to generate the interrupt. */
    return d->interruptEnabled;
}

/* ----- housekeeping ----------------------------------------------------- */
static uint16_t Drum_Tick(Device *self)
{
    if (!self)
        return 0;
    Device_TickIODelay(self);
    return self->interruptBits;
}

static uint16_t Drum_Ident(Device *self, uint16_t level)
{
    if (!self)
        return 0;
    if ((self->interruptBits & (1 << level)) != 0)
    {
        Device_SetInterruptStatus(self, false, level);
        return self->identCode;
    }
    return 0;
}

static void Drum_Reset(Device *self)
{
    if (!self)
        return;
    DrumData *d = (DrumData *)self->deviceData;
    d->coreAddress = 0;
    d->blockAddress = 0;
    d->wordCount = 0;
    d->control = 0;
    d->status = 0;
    d->interruptEnabled = false;
    self->interruptBits = 0;
}

static void Drum_Destroy(Device *self)
{
    if (!self)
        return;
    DrumData *d = (DrumData *)self->deviceData;
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
Device *CreateDrumDevice(uint8_t thumbwheel)
{
    Device *dev = (Device *)malloc(sizeof(Device));
    if (!dev)
        return NULL;

    DrumData *d = (DrumData *)malloc(sizeof(DrumData));
    if (!d)
    {
        free(dev);
        return NULL;
    }
    memset(d, 0, sizeof(DrumData));

    /* Standard (non-block) class: the drum DMAs directly from its own surface,
     * so it needs no block callbacks and avoids the type-keyed mount plumbing. */
    Device_Init(dev, thumbwheel, DEVICE_CLASS_STANDARD, 0);
    dev->deviceData = d;
    dev->type = DEVICE_TYPE_DRUM;

    /* Allocate and zero the drum surface at the MAX addressable size (8 MiB),
     * so any block address the driver can form is in range. */
    d->surfaceWords = DRUM_MAX_WORDS;
    d->surface = (uint16_t *)calloc(d->surfaceWords, sizeof(uint16_t));
    if (!d->surface)
    {
        free(d);
        free(dev);
        return NULL;
    }
    d->backingFile = NULL;
    /* Attach a drum.img backing file if one was requested (create/load). */
    if (g_drumHasBackingPath)
    {
        if (!Drum_AttachBacking(d, g_drumBackingPath))
            printf("DRUM: WARNING could not open backing file '%s' (in-memory only)\n",
                   g_drumBackingPath);
    }

    dev->Read = Drum_Read;
    dev->Write = Drum_Write;
    dev->Tick = Drum_Tick;
    dev->Reset = Drum_Reset;
    dev->Ident = Drum_Ident;
    dev->Destroy = Drum_Destroy;
    Drum_Reset(dev);

    /* Address block and identity. thumbwheel 0 -> the standard TSS drum @ 540. */
    switch (thumbwheel)
    {
    case 0:
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "DRUM 540");
        dev->startAddress = 0540;
        break;
    default:
        printf("DRUM: unknown thumbwheel value: %d\n", thumbwheel);
        free(d->surface);
        free(d);
        free(dev);
        return NULL;
    }
    dev->endAddress = dev->startAddress + 7;
    dev->identCode = DRUM_IDENT_CODE;   /* PROVISIONAL - see deviceDrum.h */
    dev->interruptLevel = DRUM_INT_LEVEL;

    printf("DRUM device created: %s ident %o level %d (%u words surface)\n",
           dev->memoryName, dev->identCode, dev->interruptLevel, d->surfaceWords);
    return dev;
}
