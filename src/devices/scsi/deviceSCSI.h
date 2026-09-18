/*
 * nd100x - ND-100 emulator
 *
 * deviceSCSI.h - ND-3201/3204 SCSI disk controller
 *
 * The ND-3201 (and the ND-3204 with external word count) is an NCR-5386 SCSI
 * protocol chip plus a DMA engine onto the ND-100 IO bus. The card itself does
 * NOT decode SCSI Command Descriptor Blocks - it is a register + DMA bridge.
 * SINTRAN's driver is the SCSI initiator: it loads the NCR's Own ID /
 * Destination ID / Transfer Counter, then issues chip commands (Select w/ATN,
 * Transfer Info, Message Accepted, ...) through the WNCOM register and pumps
 * data over DMA. The CDB is assembled by SINTRAN and interpreted by the target
 * device (the disk).
 *
 * Ported from RetroCore (C#):
 *   Emulated.HW/ND/CPU/NDBUS/NDBusDiscControllerSCSI.cs  - this file
 *   Emulated.HW/NCR/SCSI/NCR5386/(all .cs files)                   - ncr5386.c
 *   Emulated.HW/Common/SCSI/SCSIBus.cs                   - scsiBus.c
 *   Emulated.HW/Common/SCSI/SCSIFullDevice.cs            - scsiDevice.c
 *   Emulated.HW/Common/SCSI/SCSIHDD.cs                   - scsiHDD.c
 *   Emulated.HW/Common/SCSI/SCSIHDDMicropolis.cs         - diskSCSI.c
 */

#ifndef DEVICE_SCSI_H
#define DEVICE_SCSI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../devices_types.h"

/* Max SCSI targets on one controller. The card is SCSI ID 7, so targets are
 * IDs 0-6. Kept in sync with MAX_SCSI_UNITS in machine_types.h. */
#define SCSI_MAX_UNITS 7

/* Own SCSI ID of the ND-3201/3204 controller. */
#define SCSI_CONTROLLER_ID 7

/*
 * Per-unit target class. Selected from the command line as
 *   --scsi0=hdd:/path/to/image
 * and handed to the controller via DeviceManager_AddSCSIDevice_WithConfig().
 *
 * Only SCSI_UNIT_HDD is implemented today. The others are accepted by the
 * command line and reserved here so the target vtable and the CLI grammar do
 * not have to change when they land. RetroCore's ND path only ever
 * instantiates SCSIHDDMicropolis, so there is no validated ND-100 reference
 * behaviour for the other classes yet.
 */
typedef enum {
    SCSI_UNIT_NONE = 0,  /* no target at this SCSI ID */
    SCSI_UNIT_HDD,       /* Micropolis 1375-ND hard disk (implemented) */
    SCSI_UNIT_TAPE,      /* streamer tape (not implemented) */
    SCSI_UNIT_CDROM,     /* CD-ROM (not implemented) */
    SCSI_UNIT_FLOPPY     /* SCSI floppy (not implemented) */
} SCSIUnitType;

/* Enable the SCSI controller debug log to stderr (--scsi-debug).
 * Set by the frontend from the scsi log category (--log=scsi:debug). */
extern int scsi_debug_enabled;

/*
 * IOX register map, as an offset from the card's IOX base.
 * Even = read, odd = write. The octal name in the comment is the ND
 * documentation's name for that offset.
 *
 * NOTE: unused offsets in this range do NOT raise an IOX error on real
 * hardware - they give a "connect error" and undefined data. The card claims
 * all 64 addresses, so nd100x's unmapped-address IOX trap never fires for them.
 */
typedef enum {
    SCSI_REG_RLMAR  = 0x00,  /* read  Memory Address Register bits 0-15 */
    SCSI_REG_WLMAR  = 0x01,  /* write MAR bits 0-15 */
    SCSI_REG_REDAT  = 0x02,  /* read  data buffer (IOX PIO mode) */
    SCSI_REG_WRDAT  = 0x03,  /* write data buffer (IOX PIO mode) */
    SCSI_REG_RSTAU  = 0x04,  /* read  status word */
    SCSI_REG_WCONT  = 0x05,  /* write control word */
    SCSI_REG_RHMAR  = 0x06,  /* read  MAR bits 16-23 */
    SCSI_REG_WHMAR  = 0x07,  /* write MAR bits 16-23 */
    SCSI_REG_RXWC_HI= 0x08,  /* read  external word count high (3204 only) */
    SCSI_REG_RXWC   = 0x0A,  /* read  external word count (3204 only) */

    SCSI_REG_RNDAT  = 0x20,  /* o40 read  NCR data register */
    SCSI_REG_WNDAT  = 0x21,  /* o41 write NCR data register */
    SCSI_REG_RNCOM  = 0x22,  /* o42 read  NCR command register */
    SCSI_REG_WNCOM  = 0x23,  /* o43 write NCR command register */
    SCSI_REG_RNCNT  = 0x24,  /* o44 read  NCR control register */
    SCSI_REG_WNCNT  = 0x25,  /* o45 write NCR control register */
    SCSI_REG_RDESI  = 0x26,  /* o46 read  destination ID */
    SCSI_REG_WDESI  = 0x27,  /* o47 write destination ID */
    SCSI_REG_RAUXS  = 0x28,  /* o50 read  aux status */
    SCSI_REG_WAUXS  = 0x29,  /* o51 write aux status */
    SCSI_REG_ROIDN  = 0x2A,  /* o52 read  own ID */
    SCSI_REG_WOIDN  = 0x2B,  /* o53 write own ID */
    SCSI_REG_RITRG  = 0x2C,  /* o54 read  interrupt register (clears NCR int) */
    SCSI_REG_RSOUI  = 0x2E,  /* o56 read  source ID */
    SCSI_REG_RDIST  = 0x32,  /* o62 read  diagnostic status */
    SCSI_REG_RTCM   = 0x38,  /* o70 read  transfer counter MSB */
    SCSI_REG_WTCM   = 0x39,  /* o71 write transfer counter MSB */
    SCSI_REG_RTC2   = 0x3A,  /* o72 read  transfer counter middle */
    SCSI_REG_WTC2   = 0x3B,  /* o73 write transfer counter middle */
    SCSI_REG_RTCL   = 0x3C,  /* o74 read  transfer counter LSB */
    SCSI_REG_WTCL   = 0x3D   /* o75 write transfer counter LSB */
    /* o76-o77 (0x3E-0x3F) not used */
} SCSIRegisters;

/* Status word (RSTAU, read). Bits 4, 7, 11 and 15 are never set by the
 * emulator: bit 4/11 are DMA/bus errors that cannot happen, and 7/15 report
 * the physical SCSI driver type. */
#define SCSI_STAT_INTERRUPT_ENABLED  (1 << 0)
#define SCSI_STAT_ACTIVE             (1 << 2)
#define SCSI_STAT_READY_FOR_TRANSFER (1 << 3)
#define SCSI_STAT_OR_OF_ERRORS       (1 << 4)
#define SCSI_STAT_RESET_ON_SCSI_BUS  (1 << 5)
#define SCSI_STAT_NCR_DISABLED       (1 << 6)
#define SCSI_STAT_SINGLE_ENDED       (1 << 7)
#define SCSI_STAT_DATA_REQUEST       (1 << 8)
#define SCSI_STAT_INTERRUPT_FROM_NCR (1 << 9)
#define SCSI_STAT_DATA_ACKNOWLEDGE   (1 << 10)
#define SCSI_STAT_BERROR             (1 << 11)
#define SCSI_STAT_SCSI_BSY           (1 << 12)
#define SCSI_STAT_SCSI_REQ           (1 << 13)
#define SCSI_STAT_SCSI_ACK           (1 << 14)
#define SCSI_STAT_DIFFERENTIAL       (1 << 15)

/* Control word (WCONT, write). */
#define SCSI_CTRL_ENABLE_INTERRUPT   (1 << 0)
#define SCSI_CTRL_ACTIVATE           (1 << 2)  /* GO */
#define SCSI_CTRL_TEST_MODE          (1 << 3)
#define SCSI_CTRL_CLEAR_DEVICE       (1 << 4)
#define SCSI_CTRL_DMA_ENABLE         (1 << 5)
#define SCSI_CTRL_WRITE_ND_MEMORY    (1 << 6)  /* 1 = SCSI->ND mem, 0 = ND mem->SCSI */
#define SCSI_CTRL_RESET_SCSI_BUS     (1 << 10)

/* Parse a unit type name ("hdd", "tape", "cdrom", "floppy") as used by the
 * --scsiN=TYPE:FILE option. Returns SCSI_UNIT_NONE if the name is unknown. */
SCSIUnitType SCSI_ParseUnitType(const char *name);

/* Human-readable name for a unit type, for help text and logging. */
const char *SCSI_UnitTypeName(SCSIUnitType type);

/* Factory. Only expose the factory - everything else is static. */
Device *CreateSCSIDevice(uint8_t thumbwheel);

/* Set the target class for one SCSI ID. Must be called before the first
 * transfer. Returns false on a bad unit or an unimplemented type. */
bool SCSI_SetUnitType(Device *dev, int unit, SCSIUnitType type);

#endif // DEVICE_SCSI_H
