/*
 * nd100x - ND-100 emulator
 *
 * device_scsi.c - ND-3201/3204 SCSI disk controller
 *
 * Ported from RetroCore:
 *   <RetroCore>\Emulated.HW\ND\CPU\NDBUS\NDBusDiscControllerSCSI.cs
 *
 * See device_scsi.h for the architecture note. In short: this card is a dumb
 * NCR-5386 register + DMA bridge. SINTRAN's driver is the SCSI initiator and
 * drives the chip command-by-command; the CDB it builds is interpreted by the
 * target (scsi_hdd.c), not by this card.
 *
 * CONTROL FLOW - this device is NOT shaped like device_smd.c. The SMD
 * controller moves an entire transfer synchronously inside the IOX write that
 * sets its GO bit, and only uses Device_QueueIODelay to fake a completion
 * interrupt. Here the transfer is driven from Tick(): the bus and chip are
 * clocked, and the DMA pump drains whatever bytes the NCR is asking for. The
 * completion interrupt is raised directly from SCSI_StepGoState when the NCR
 * signals its interrupt - there is no queued IO delay in that path.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#include "../devices_types.h"
#include "../devices_protos.h"

/* Read by the NCR 5386 port (ncr5386.c, vendored, not edited). The frontend
 * sets it from the scsi log category after --log / --scsi-debug. */
int scsi_debug_enabled = 0;

/* Device private state. */
typedef struct
{
    /* Per-SCSI-ID target class. Index is the SCSI ID (0-6); ID 7 is us. */
    SCSIUnitType unitType[SCSI_MAX_UNITS];

    SCSIBus       bus;
    NCR5386       ncr;
    SCSIHDDDevice disks[SCSI_MAX_UNITS];
    bool          diskPresent[SCSI_MAX_UNITS];

    /* ---- ND card registers ---- */
    uint16_t memoryAddressLSB;      /* MAR bits 0-15 */
    uint16_t memoryAddressMSB;      /* MAR bits 16-23 */
    uint16_t readWriteData;         /* IOX PIO data buffer */
    uint16_t externalWordCount;     /* 3204 only, not driven */
    uint16_t externalWordCountMSB;

    bool interruptEnabled;
    bool active;                    /* GO / busy */
    bool readyForTransfer;
    bool testMode;
    bool dmaEnable;
    bool writeNDMemory;             /* DMA direction: true = SCSI -> ND memory */
    bool resetOnSCSIBus;

    bool dataRequestFromNCR;
    bool interruptFromNCR;
    bool dataAcknowledgeToNCR;

    /* DMA byte<->word packing state. */
    uint32_t dma_bytes_read;
    uint32_t dma_bytes_written;
    int32_t  dma_read_data;         /* latched word for the odd byte */

    int bytesPrSector;
} SCSIData;


static void SCSI_Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void SCSI_Log(const char *fmt, ...)
{
    if (!Log_IsEnabled(LOG_CAT_SCSI, LOG_DEBUG))
        return;

    char msg[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    Log_Write(LOG_CAT_SCSI, LOG_DEBUG, "%s", msg);
}


/* MAR is a 24-bit WORD address. */
static uint32_t SCSI_GetMAR(SCSIData *data)
{
    return ((uint32_t)data->memoryAddressMSB << 16) | data->memoryAddressLSB;
}

static void SCSI_IncrementMAR(SCSIData *data)
{
    uint32_t m = SCSI_GetMAR(data) + 1;
    data->memoryAddressLSB = (uint16_t)(m & 0xFFFF);
    data->memoryAddressMSB = (uint16_t)((m >> 16) & 0xFF);
}


SCSIUnitType SCSI_ParseUnitType(const char *name)
{
    if (!name)
        return SCSI_UNIT_NONE;

    if (strcmp(name, "hdd") == 0)
        return SCSI_UNIT_HDD;
    if (strcmp(name, "tape") == 0)
        return SCSI_UNIT_TAPE;
    if (strcmp(name, "cdrom") == 0)
        return SCSI_UNIT_CDROM;
    if (strcmp(name, "floppy") == 0)
        return SCSI_UNIT_FLOPPY;

    return SCSI_UNIT_NONE;
}


const char *SCSI_UnitTypeName(SCSIUnitType type)
{
    switch (type)
    {
    case SCSI_UNIT_HDD:    return "hdd";
    case SCSI_UNIT_TAPE:   return "tape";
    case SCSI_UNIT_CDROM:  return "cdrom";
    case SCSI_UNIT_FLOPPY: return "floppy";
    case SCSI_UNIT_NONE:
    default:               return "none";
    }
}


bool SCSI_SetUnitType(Device *dev, int unit, SCSIUnitType type)
{
    if (!dev || !dev->deviceData)
        return false;
    if (unit < 0 || unit >= SCSI_MAX_UNITS)
        return false;

    /* Only the hard disk target is implemented. Reject the rest loudly rather
     * than silently mounting an image that nothing will ever answer for. */
    if (type != SCSI_UNIT_HDD && type != SCSI_UNIT_NONE)
    {
        LOG(LOG_CAT_SCSI, LOG_ERROR, "SCSI: unit %d type '%s' is not implemented yet (only 'hdd')\n",
               unit, SCSI_UnitTypeName(type));
        return false;
    }

    SCSIData *data = (SCSIData *)dev->deviceData;
    data->unitType[unit] = type;

    if (type == SCSI_UNIT_HDD && !data->diskPresent[unit])
    {
        SCSIHDD_Init(&data->disks[unit], &data->bus, (uint8_t)unit, dev, unit,
                     SCSI_DISK_MICROPOLIS_1375_ND);
        data->diskPresent[unit] = true;
        SCSI_Log("unit %d attached as hdd (Micropolis 1375-ND)", unit);
    }

    return true;
}


/* ------------------------------------------------------------------ */
/* NCR callbacks                                                       */
/* ------------------------------------------------------------------ */
static void SCSI_OnNCRInterrupt(void *context, uint8_t state)
{
    Device *self = (Device *)context;
    SCSIData *data = (SCSIData *)self->deviceData;

    if (state != 0)
    {
        /* Only latch. The flag is consumed in SCSI_StepGoState, and cleared
         * when the ND reads RITRG - never by reading RSTAU. */
        data->interruptFromNCR = true;
        SCSI_Log("NCR interrupt raised active=%d intEnabled=%d",
                 data->active, data->interruptEnabled);
    }
}


static void SCSI_OnNCRDataRequest(void *context, uint8_t state)
{
    Device *self = (Device *)context;
    SCSIData *data = (SCSIData *)self->deviceData;

    data->dataRequestFromNCR = (state != 0);
}


/* ------------------------------------------------------------------ */
/* DMA byte <-> ND word packing                                        */
/* ------------------------------------------------------------------ */
/*
 * Bytes pack into 16-bit ND words BIG-ENDIAN: the even byte of a word is the
 * HIGH byte, the odd byte is the LOW byte. This is the same convention as
 * Device_IO_BufferReadWord/WriteWord used by the SMD driver, so the two agree
 * byte-for-byte.
 *
 * NOTE the asymmetry, which is easy to get wrong: the READ path fetches the
 * whole word on the EVEN byte and increments the MAR there, then serves the odd
 * byte from the latch. The WRITE path read-modify-writes the target word twice
 * and increments the MAR on the ODD byte. Both match RetroCore exactly.
 *
 * NOTE on addressing: the MAR is a WORD address and is passed to
 * Device_DMAWrite/Device_DMARead unshifted. RetroCore shifts it (MAR << 1)
 * inside its own DMAWrite/DMARead helpers because its DMA bus is byte-
 * addressed; nd100x's physical memory is word-addressed, so shifting here would
 * put every transfer at twice the right address.
 */
static uint8_t SCSI_ReadNextByteDMA(SCSIData *data)
{
    uint8_t byteval;

    if ((data->dma_bytes_read % 2) == 0)
    {
        data->dma_read_data = Device_DMARead(SCSI_GetMAR(data));
        byteval = (uint8_t)((data->dma_read_data >> 8) & 0xFF);
        SCSI_IncrementMAR(data);
    }
    else
    {
        byteval = (uint8_t)(data->dma_read_data & 0xFF);
    }

    data->dma_bytes_read++;
    return byteval;
}


static void SCSI_WriteNextByteDMA(SCSIData *data, uint8_t byteval)
{
    uint32_t mar = SCSI_GetMAR(data);
    int32_t memData = Device_DMARead(mar);
    uint16_t writeData;

    if ((data->dma_bytes_written % 2) == 0)
    {
        /* even byte -> HIGH byte, preserve the low byte */
        writeData = (uint16_t)((memData & 0x00FF) | ((byteval & 0xFF) << 8));
        Device_DMAWrite(mar, writeData);
    }
    else
    {
        /* odd byte -> LOW byte, preserve the high byte, then advance */
        writeData = (uint16_t)((memData & 0xFF00) | (byteval & 0xFF));
        Device_DMAWrite(mar, writeData);
        SCSI_IncrementMAR(data);
    }

    data->dma_bytes_written++;
}


/* ------------------------------------------------------------------ */
/* GO state                                                            */
/* ------------------------------------------------------------------ */
/*
 * Called every tick while the card is active.
 *
 * Completion is event driven: when the NCR has signalled its interrupt the card
 * drops active, sets ready-for-transfer, and raises ND interrupt level 11 if
 * enabled. Then any pending DMA bytes are drained - ALL of them, not one per
 * tick, matching RetroCore's inner while loop.
 */
static void SCSI_StepGoState(Device *self)
{
    SCSIData *data = (SCSIData *)self->deviceData;

    if (!data->active)
        return;

    if (data->interruptFromNCR)
    {
        data->active = false;
        data->readyForTransfer = true;

        SCSI_Log("completion: active->false rft->true intEnabled=%d -> %s",
                 data->interruptEnabled,
                 data->interruptEnabled ? "INTERRUPT" : "no IRQ (int disabled)");

        if (data->interruptEnabled)
            Device_GenerateInterrupt(self, self->interruptLevel);
    }

    if (!data->dmaEnable)
        return;

    uint32_t startWritten = data->dma_bytes_written;

    /* Transfer ALL pending DMA bytes immediately (not one per clock). */
    while (data->dataRequestFromNCR && data->active)
    {
        if (data->writeNDMemory)
        {
            /* SCSI -> ND memory */
            uint8_t byteval = NCR5386_DMARead(&data->ncr);
            SCSI_WriteNextByteDMA(data, byteval);
        }
        else
        {
            /* ND memory -> SCSI */
            uint8_t byteval = SCSI_ReadNextByteDMA(data);
            NCR5386_DMAWrite(&data->ncr, byteval);
        }
    }

    if (Log_IsEnabled(LOG_CAT_SCSI, LOG_DEBUG) && data->dma_bytes_written > startWritten)
        SCSI_Log("DMA->ND xfer bytes=%u totalWritten=%u MAR=0x%06X",
                 data->dma_bytes_written - startWritten, data->dma_bytes_written,
                 SCSI_GetMAR(data));
}


static void SCSI_Reset(Device *self)
{
    SCSIData *data = (SCSIData *)self->deviceData;
    if (!data)
        return;

    self->blockSizeBytes = 1024; /* ND SCSI disk sector = 1024 bytes = 512 ND words */
    data->bytesPrSector = 1024;

    data->memoryAddressLSB = 0;
    data->memoryAddressMSB = 0;
    data->readWriteData = 0;
    data->interruptEnabled = false;
    data->active = false;
    data->readyForTransfer = false;
    data->testMode = false;
    data->dmaEnable = false;
    data->writeNDMemory = false;
    data->resetOnSCSIBus = false;
    data->dataRequestFromNCR = false;
    data->interruptFromNCR = false;
    data->dataAcknowledgeToNCR = false;
    data->dma_bytes_read = 0;
    data->dma_bytes_written = 0;
    data->dma_read_data = 0;

    /* NOTE: unitType[] is deliberately NOT cleared here. It is configuration
     * from the command line, not device state, and SCSI_Reset runs on every
     * master clear. */

    if (data->ncr.bus)
    {
        NCR5386_DeviceReset(&data->ncr);
        /* Chip own-ID plumbing (alignment 2026-07-17, kept in sync with
         * RetroCore NDBusDiscControllerSCSI.SetSCSIIdNumber):
         * On the real ND-3201 board TW1 straps the NCR 5386's own SCSI ID (7).
         * Per the datasheet the own ID lives in the ID Register (reg 5,
         * "strapped ID" mode) and the Source ID register (reg 7) is READ-ONLY
         * (it latches the ID of a (re)selecting device). The SINTRAN driver
         * never writes the own ID; it only READS ROIDN (own id) and RSOUI
         * (reconnect path). Both emulator chip cores use sourceID as the
         * arbitration own-ID (NCR_StepState / StateHandling.cs), so set BOTH:
         *  - ID Register = 7 -> ROIDN reads back 7 (documented readback)
         *  - Source ID   = 7 -> arbitration asserts bit 1<<7 (live-verified
         *    RetroCore behaviour vs SINTRAN)
         * If target-initiated reselection is ever implemented, sourceID must
         * instead be latched from the reselecting target's ID. */
        NCR5386_Write(&data->ncr, NCR_REG_ID, SCSI_CONTROLLER_ID);
        NCR5386_Write(&data->ncr, NCR_REG_SOURCE_ID, SCSI_CONTROLLER_ID);
    }
}


/* ------------------------------------------------------------------ */
/* IOX                                                                 */
/* ------------------------------------------------------------------ */
static uint16_t SCSI_Read(Device *self, uint32_t address)
{
    SCSIData *data = (SCSIData *)self->deviceData;
    uint32_t reg = Device_RegisterAddress(self, address);
    uint16_t rval = 0;

    switch (reg)
    {
    case SCSI_REG_RLMAR:
        rval = data->memoryAddressLSB;
        /* Test mode (alignment 2026-07-17, from RetroCore Read RLMAR):
         * reading RLMAR in test mode auto-increments the MAR. SINTRAN's NEWPH
         * odd-byte recovery relies on this (WCONT=150 octal test mode, RLMAR
         * read to "FORCE LAST BYTE TO MEMORY", then the MAR check expects the
         * incremented value); without it an odd-byte data-in stop fails the
         * MAR check -> MARER -> SCSI bus reset. Dormant with 1024-byte
         * sectors (all transfers even-length), but matches the hardware. */
        if (data->testMode)
            SCSI_IncrementMAR(data);
        break;

    case SCSI_REG_REDAT:
        rval = data->readWriteData;
        break;

    case SCSI_REG_RSTAU:
        /*
         * Bit 4 (OR of errors), 11 (BERROR) can never happen in the emulator,
         * and 7 (single ended) / 15 (differential) describe the physical driver
         * type - none of them are ever set.
         *
         * Reading this register does NOT clear the NCR interrupt: that only
         * happens on a RITRG read.
         */
        if (data->interruptEnabled)             rval |= SCSI_STAT_INTERRUPT_ENABLED;
        if (data->active)                       rval |= SCSI_STAT_ACTIVE;
        if (data->readyForTransfer)             rval |= SCSI_STAT_READY_FOR_TRANSFER;
        if (data->resetOnSCSIBus)               rval |= SCSI_STAT_RESET_ON_SCSI_BUS;
        if (NCR5386_ChipDisabled(&data->ncr))   rval |= SCSI_STAT_NCR_DISABLED;
        if (data->dataRequestFromNCR)           rval |= SCSI_STAT_DATA_REQUEST;
        if (data->interruptFromNCR)             rval |= SCSI_STAT_INTERRUPT_FROM_NCR;
        if (data->dataAcknowledgeToNCR)         rval |= SCSI_STAT_DATA_ACKNOWLEDGE;
        if (NCR5386_SCSI_BSY(&data->ncr))       rval |= SCSI_STAT_SCSI_BSY;
        if (NCR5386_SCSI_REQ(&data->ncr))       rval |= SCSI_STAT_SCSI_REQ;
        if (NCR5386_SCSI_ACK(&data->ncr))       rval |= SCSI_STAT_SCSI_ACK;
        break;

    case SCSI_REG_RHMAR:
        rval = data->memoryAddressMSB;
        break;

    case SCSI_REG_RXWC_HI:
        rval = data->externalWordCountMSB;
        break;
    case SCSI_REG_RXWC:
        rval = data->externalWordCount;
        break;

    /* ---- NCR chip registers ---- */
    case SCSI_REG_RNDAT: rval = NCR5386_Read(&data->ncr, NCR_REG_DATA); break;
    case SCSI_REG_RNCOM: rval = NCR5386_Read(&data->ncr, NCR_REG_COMMAND); break;
    case SCSI_REG_RNCNT: rval = NCR5386_Read(&data->ncr, NCR_REG_CONTROL); break;
    case SCSI_REG_RDESI: rval = NCR5386_Read(&data->ncr, NCR_REG_DESTINATION_ID); break;
    case SCSI_REG_RAUXS: rval = NCR5386_Read(&data->ncr, NCR_REG_AUX_STATUS); break;
    case SCSI_REG_ROIDN: rval = NCR5386_Read(&data->ncr, NCR_REG_ID); break;

    case SCSI_REG_RITRG:
        /* Reading the interrupt register is the ONLY thing that acknowledges
         * the NCR interrupt on this card. */
        rval = NCR5386_Read(&data->ncr, NCR_REG_INTERRUPT);
        data->interruptFromNCR = false;
        break;

    case SCSI_REG_RSOUI: rval = NCR5386_Read(&data->ncr, NCR_REG_SOURCE_ID); break;
    case SCSI_REG_RDIST: rval = NCR5386_Read(&data->ncr, NCR_REG_DIAGNOSTIC_STATUS); break;
    case SCSI_REG_RTCM:  rval = NCR5386_Read(&data->ncr, NCR_REG_TRANSFER_COUNT_MSB); break;
    case SCSI_REG_RTC2:  rval = NCR5386_Read(&data->ncr, NCR_REG_TRANSFER_COUNT_MID); break;
    case SCSI_REG_RTCL:  rval = NCR5386_Read(&data->ncr, NCR_REG_TRANSFER_COUNT_LSB); break;

    default:
        /* Unused IOX offsets give undefined data, not an IOX error. */
        break;
    }

    if (Log_IsEnabled(LOG_CAT_SCSI, LOG_DEBUG))
        Log_Write(LOG_CAT_SCSI, LOG_DEBUG, "IOX READ  addr=%o reg=%o -> value=%o (0x%04X)\n",
                address, reg, rval, rval);

    return rval;
}


static void SCSI_Write(Device *self, uint32_t address, uint16_t value)
{
    SCSIData *data = (SCSIData *)self->deviceData;
    uint32_t reg = Device_RegisterAddress(self, address);

    if (Log_IsEnabled(LOG_CAT_SCSI, LOG_DEBUG))
        Log_Write(LOG_CAT_SCSI, LOG_DEBUG, "IOX WRITE addr=%o reg=%o value=%o (0x%04X)\n",
                address, reg, value, value);

    switch (reg)
    {
    case SCSI_REG_WLMAR:
        data->memoryAddressLSB = value;
        break;

    case SCSI_REG_WHMAR:
        data->memoryAddressMSB = value & 0xFF;
        break;

    case SCSI_REG_WRDAT:
        data->readWriteData = value;
        break;

    case SCSI_REG_WCONT:
        data->interruptEnabled = (value & SCSI_CTRL_ENABLE_INTERRUPT) != 0;
        data->active           = (value & SCSI_CTRL_ACTIVATE) != 0;
        data->testMode         = (value & SCSI_CTRL_TEST_MODE) != 0;
        data->dmaEnable        = (value & SCSI_CTRL_DMA_ENABLE) != 0;
        data->writeNDMemory    = (value & SCSI_CTRL_WRITE_ND_MEMORY) != 0;

        if (Log_IsEnabled(LOG_CAT_SCSI, LOG_DEBUG))
            SCSI_Log("CONTROL WORD=%o (0x%04X) IntEn=%d Active=%d Test=%d DMA=%d WriteND=%d",
                     value, value, data->interruptEnabled, data->active,
                     data->testMode, data->dmaEnable, data->writeNDMemory);

        /* Test mode does a single PIO word through the DMA path. */
        if (data->testMode)
        {
            uint32_t dma_address = SCSI_GetMAR(data);
            if (data->writeNDMemory)
                Device_DMAWrite(dma_address, data->readWriteData);
            else
                data->readWriteData = (uint16_t)Device_DMARead(dma_address);
        }

        /* Clear device: zeroes the MAR and buffer pointers, resets the NCR and
         * sets ready-for-transfer. The SCSI bus RST signal is NOT affected. */
        if (value & SCSI_CTRL_CLEAR_DEVICE)
        {
            data->memoryAddressLSB = 0;
            data->memoryAddressMSB = 0;
            data->dma_bytes_read = 0;
            data->dma_bytes_written = 0;
            NCR5386_DeviceReset(&data->ncr);
            /* Re-assert the strapped own ID after the chip reset (a strap
             * cannot be reset) - both ID Register (ROIDN readback) and
             * Source ID (arbitration). See SCSI_Reset for the full rationale. */
            NCR5386_Write(&data->ncr, NCR_REG_ID, SCSI_CONTROLLER_ID);
            NCR5386_Write(&data->ncr, NCR_REG_SOURCE_ID, SCSI_CONTROLLER_ID);
            data->readyForTransfer = true;
        }

        data->resetOnSCSIBus = (value & SCSI_CTRL_RESET_SCSI_BUS) != 0;
        if (data->resetOnSCSIBus)
            NCR5386_InitiateResetSCSIBus(&data->ncr);

        /* Writing the activate bit starts the transfer and clears
         * ready-for-transfer. */
        if (data->active)
            data->readyForTransfer = false;
        else if (data->interruptEnabled && data->readyForTransfer)
        {
            /* "Interrupt when ready" (alignment 2026-07-17, ported from
             * RetroCore NDBusDiscControllerSCSI.cs Write WCONT else-branch,
             * live-verified against SINTRAN): a control word with bit 0
             * (enable interrupt) set but bit 2 (activate) CLEAR raises the
             * level-11 interrupt immediately when the controller is already
             * ready for transfer. The ND doc's bit-0 text says interrupt is
             * given "as soon as the controller is ready". Every observed
             * SINTRAN WCONT enable also sets activate, so this branch is
             * normally dormant - but a driver that enables interrupts while
             * idle (e.g. after Clear Device, which sets readyForTransfer)
             * would otherwise hang waiting for an interrupt that never comes. */
            Device_GenerateInterrupt(self, self->interruptLevel);
        }
        break;

    /* ---- NCR chip registers ---- */
    case SCSI_REG_WNDAT: NCR5386_Write(&data->ncr, NCR_REG_DATA, (uint8_t)value); break;
    case SCSI_REG_WNCOM: NCR5386_Write(&data->ncr, NCR_REG_COMMAND, (uint8_t)value); break;
    case SCSI_REG_WNCNT: NCR5386_Write(&data->ncr, NCR_REG_CONTROL, (uint8_t)value); break;
    case SCSI_REG_WDESI: NCR5386_Write(&data->ncr, NCR_REG_DESTINATION_ID, (uint8_t)value); break;
    case SCSI_REG_WAUXS: NCR5386_Write(&data->ncr, NCR_REG_AUX_STATUS, (uint8_t)value); break;
    case SCSI_REG_WOIDN: NCR5386_Write(&data->ncr, NCR_REG_ID, (uint8_t)value); break;
    case SCSI_REG_WTCM:  NCR5386_Write(&data->ncr, NCR_REG_TRANSFER_COUNT_MSB, (uint8_t)value); break;
    case SCSI_REG_WTC2:  NCR5386_Write(&data->ncr, NCR_REG_TRANSFER_COUNT_MID, (uint8_t)value); break;
    case SCSI_REG_WTCL:  NCR5386_Write(&data->ncr, NCR_REG_TRANSFER_COUNT_LSB, (uint8_t)value); break;

    default:
        break;
    }
}


/*
 * Tick.
 *
 * Unlike SMD_Tick, this does real work: RetroCore drives the whole SCSI
 * transaction from Clock() as
 *     scsi_bus.Clock(); if (regs.active) StepGoState();
 * IO_Tick() runs once per CPU instruction (cpu.c), which is our clock.
 */
static uint16_t SCSI_Tick(Device *self)
{
    if (!self)
        return 0;

    SCSIData *data = (SCSIData *)self->deviceData;
    if (!data)
        return 0;

    Device_TickIODelay(self);

    SCSIBus_Clock(&data->bus);

    if (data->active)
        SCSI_StepGoState(self);

    return self->interruptBits;
}


static uint16_t SCSI_Ident(Device *self, uint16_t level)
{
    if (!self)
        return 0;

    if (Log_IsEnabled(LOG_CAT_SCSI, LOG_DEBUG))
        Log_Write(LOG_CAT_SCSI, LOG_DEBUG, "IDENT level=%d identCode=%o\n", level, self->identCode);

    if ((self->interruptBits & (1 << level)) != 0)
    {
        SCSIData *data = (SCSIData *)self->deviceData;
        data->interruptEnabled = false;
        Device_SetInterruptStatus(self, false, level);
        return self->identCode;
    }
    return 0;
}


/*
 * Boot.
 *
 * This deliberately bypasses the SCSI register/NCR/bus path entirely and reads
 * the boot blocks straight through the block callback, exactly as SMD_Boot
 * does. That is not a shortcut: on real hardware the boot load is performed by
 * CPU microcode, and nd100x does not execute microcode, so modelling it as a
 * direct block read is the faithful choice at this level of emulation.
 *
 * The cheat only covers the first 2048 words. What gets loaded is the disk
 * bootstrap, which the CPU then executes - and that bootstrap is a real SCSI
 * driver that will drive this card's registers for real.
 */
static int SCSI_Boot(Device *self, int unit)
{
    if (!self)
        return -1;

    if (!self->blockCallbacks.readFunc)
        return -1; /* Need callbacks hooked up */

    SCSIData *data = (SCSIData *)self->deviceData;
    if (!data)
        return -1;

    if (unit < 0 || unit >= SCSI_MAX_UNITS)
    {
        LOG(LOG_CAT_SCSI, LOG_ERROR, "Error: SCSI boot unit %d out of range (0-%d)\n", unit, SCSI_MAX_UNITS - 1);
        return -1;
    }

    if (data->unitType[unit] != SCSI_UNIT_HDD)
    {
        LOG(LOG_CAT_SCSI, LOG_ERROR, "Error: SCSI boot needs a 'hdd' target on unit %d (unit %d is '%s')\n",
               unit, unit, SCSI_UnitTypeName(data->unitType[unit]));
        return -1;
    }

    self->blockSizeBytes = data->bytesPrSector;

    /* 4 blocks of 1024 bytes = 4096 bytes = 2048 ND words, loaded to address 0.
     * TODO(phase 6): confirm the real ND SCSI boot load length against the
     * bootstrap on SCSI-K.image - this currently mirrors SMD_Boot's 4 blocks. */
    const uint32_t blockCounter = 4;
    const int wordCounter = 2048;

    uint8_t *buffer = (uint8_t *)malloc(blockCounter * self->blockSizeBytes);
    if (!buffer)
        return -1;

    int blocksRead = self->blockCallbacks.readFunc(self, buffer, blockCounter, 0, unit);
    if ((blocksRead < 0) || (blocksRead != (int)blockCounter))
    {
        LOG(LOG_CAT_SCSI, LOG_ERROR, "[SCSI Boot] Block read failed: got %d blocks, expected %d\n",
               blocksRead, blockCounter);
        free(buffer);
        return -1;
    }

    /* Reject a blank/unformatted disk rather than executing zeros. */
    {
        int allZero = 1;
        for (uint32_t i = 0; i < blockCounter * self->blockSizeBytes; i++)
        {
            if (buffer[i] != 0)
            {
                allZero = 0;
                break;
            }
        }
        if (allZero)
        {
            LOG(LOG_CAT_SCSI, LOG_ERROR, "Error: SCSI boot sector is all zeros (blank or unformatted disk)\n");
            free(buffer);
            return -1;
        }
    }

    /* The image is stored big-endian / ND word order: word N is at byte offset
     * N*2, MSB first. Device_IO_BufferReadWord does that unpacking, and it is
     * the same convention the DMA pump uses when it packs SCSI bytes into ND
     * words (even byte -> high). */
    for (int i = 0; i < wordCounter; i++)
    {
        uint32_t readData = Device_IO_BufferReadWord(self, buffer, i);
        Device_DMAWrite(i, (uint16_t)readData);
    }

    free(buffer);

    if (Log_IsEnabled(LOG_CAT_SCSI, LOG_DEBUG))
        Log_Write(LOG_CAT_SCSI, LOG_DEBUG, "Boot loaded %d words from unit %d to address 0\n",
                wordCounter, unit);

    /* Return boot address. */
    return 0;
}


void SCSI_Destroy(Device *dev)
{
    if (!dev)
        return;

    /* The bus, chip and targets are all embedded in SCSIData - nothing to free
     * beyond deviceData itself, which the generic teardown handles. */
}


Device *CreateSCSIDevice(uint8_t thumbwheel)
{
    Device *dev = (Device *)malloc(sizeof(Device));
    if (!dev)
        return NULL;

    SCSIData *data = (SCSIData *)malloc(sizeof(SCSIData));
    if (!data)
    {
        free(dev);
        return NULL;
    }

    memset(dev, 0, sizeof(Device));
    memset(data, 0, sizeof(SCSIData));

    /* Initialize device base structure. Allocates ioDelays - without it
     * Device_QueueIODelay silently no-ops. */
    Device_Init(dev, thumbwheel, DEVICE_CLASS_BLOCK, 1024);

    dev->deviceData = data;

    dev->Read = SCSI_Read;
    dev->Write = SCSI_Write;
    dev->Tick = SCSI_Tick;
    dev->Reset = SCSI_Reset;
    dev->Ident = SCSI_Ident;
    dev->Boot = SCSI_Boot;
    dev->Destroy = SCSI_Destroy;

    /*
     * Thumbwheel TW2 selects IOX base / IDENT / logical device.
     * From NDBusDiscControllerSCSI.cs. The literals there ("144300", "140440")
     * are parsed as OCTAL by Numeric.TryParseUInt32: its rule is
     *   value.StartsWith('1') && value.Length == 6  ->  Octal
     * even though that helper's global default presentation is Hex.
     *
     * NOTE: identCode 0140440 (= 49440 decimal) is far larger than every other
     * ident in this emulator (SMD uses 017). It is what RetroCore uses and it
     * fits uint16_t, but it should be re-checked against real ND-3201 docs
     * before trusting the IDENT handshake.
     */
    switch (thumbwheel & 0x03)
    {
    case 0: /* TW2 = 0/4/8/C - logical device 2202 */
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "SCSI 144300");
        dev->startAddress = 0144300;
        dev->identCode = 0140440;
        dev->logicalDevice = 02202;
        break;
    case 1: /* TW2 = 1/5/9/D - logical device 2203 */
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "SCSI 144400");
        dev->startAddress = 0144400;
        dev->identCode = 0140441;
        dev->logicalDevice = 02203;
        break;
    case 2: /* TW2 = 2/6/A/E - logical device 2204 */
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "SCSI 144500");
        dev->startAddress = 0144500;
        dev->identCode = 0140442;
        dev->logicalDevice = 02204;
        break;
    case 3: /* TW2 = 3/7/B/F - logical device 2205 */
        snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", "SCSI 144600");
        dev->startAddress = 0144600;
        dev->identCode = 0140443;
        dev->logicalDevice = 02205;
        break;
    }

    dev->interruptLevel = 11; /* Output channel interrupt = 11 (disk) */

    /* The card decodes 64 IOX addresses (NDBusAddressLength = 63 in RetroCore).
     * Device_IsInAddress is a generic inclusive [start,end] range, so a span
     * wider than the usual 8 is fine (HDLC already spans 16). */
    dev->endAddress = dev->startAddress + 63;

    /* Build the SCSI bus and put our NCR-5386 on it as SCSI ID 7. The disk
     * targets are attached later by SCSI_SetUnitType, once the command line
     * has said which units exist. */
    SCSIBus_Init(&data->bus);
    NCR5386_Init(&data->ncr, &data->bus, SCSI_CONTROLLER_ID,
                 SCSI_OnNCRInterrupt, SCSI_OnNCRDataRequest, dev);

    SCSI_Reset(dev);

    LOG(LOG_CAT_SCSI, LOG_INFO, "SCSI Device object created at IOX %o (ident %o).\n",
           dev->startAddress, dev->identCode);

    return dev;
}
