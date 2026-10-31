/*
 * nd100x - ND-100 emulator
 *
 * ncr5386.h - NCR-5386 SCSI Protocol Controller
 *
 * This is the chip on the ND-3201/3204 SCSI card. SINTRAN's driver is the SCSI
 * initiator and drives this chip register-by-register: it loads the Own ID,
 * Destination ID and Transfer Counter, then issues commands (Select w/ATN,
 * Transfer Info, Message Accepted, ...) through the Command Register and moves
 * the data over DMA. The chip runs the initiator side of the SCSI bus; the CDB
 * that SINTRAN assembles is interpreted by the target (see scsiHDD.c).
 *
 * Ported from RetroCore:
 *   <RetroCore>\Emulated.HW\NCR\SCSI\NCR5386\*.cs
 * which is itself a port of MAME's ncr5385.cpp
 *   https://github.com/mamedev/mame/blob/master/src/devices/machine/ncr5385.cpp
 *   (in sync with git SHA 25066795caded65502be102dd9f181ccae41748b, June 28 2024)
 *   license:BSD-3-Clause  copyright-holders:Ryan Holtz
 */

#ifndef NCR5386_H
#define NCR5386_H

/* ------------------------------------------------------------------ */
/* Register map (Registers.cs / Enums.cs SCSIRegisters)               */
/* ------------------------------------------------------------------ */
typedef enum {
    NCR_REG_DATA                 = 0,  /* Data Register */
    NCR_REG_COMMAND              = 1,  /* Command Register */
    NCR_REG_CONTROL              = 2,  /* Control Register */
    NCR_REG_DESTINATION_ID       = 3,  /* Destination ID */
    NCR_REG_AUX_STATUS           = 4,  /* Auxiliary Status */
    NCR_REG_ID                   = 5,  /* Own ID */
    NCR_REG_INTERRUPT            = 6,  /* Interrupt Register */
    NCR_REG_SOURCE_ID            = 7,  /* Source ID (read-only) */
    NCR_REG_DATA_II              = 8,  /* Data Register II */
    NCR_REG_DIAGNOSTIC_STATUS    = 9,  /* Diagnostic Status (read-only) */
    NCR_REG_TRANSFER_COUNT_MSB   = 12,
    NCR_REG_TRANSFER_COUNT_MID   = 13,
    NCR_REG_TRANSFER_COUNT_LSB   = 14,
    NCR_REG_RESERVED             = 15
} NCRRegister;

/* ------------------------------------------------------------------ */
/* Command codes - Command Register bits 0-4 (CommandCode)            */
/*                                                                     */
/* Codes 0-7 are "immediate" (complete within ~3 clocks, no interrupt) */
/* and codes 8-31 are "interrupting". That split is exactly bit 3 of   */
/* the code, which is why RetroCore names it CommandRegisterFlags      */
/* .Interrupting = 1 << 3 even though bit 3 is inside the code mask.   */
/* ------------------------------------------------------------------ */
typedef enum {
    /* Immediate commands */
    NCR_CMD_CHIP_RESET         = 0,
    NCR_CMD_DISCONNECT         = 1,
    NCR_CMD_PAUSE              = 2,
    NCR_CMD_SET_ATN            = 3,
    NCR_CMD_MESSAGE_ACCEPTED   = 4,
    NCR_CMD_CHIP_DISABLE       = 5,
    NCR_CMD_RESERVED6          = 6,
    NCR_CMD_RESERVED7          = 7,

    /* Interrupting commands */
    NCR_CMD_SELECT_WITH_ATN    = 8,
    NCR_CMD_SELECT_WITHOUT_ATN = 9,
    NCR_CMD_RESELECT           = 10,
    NCR_CMD_DIAGNOSTIC         = 11,
    NCR_CMD_RECEIVE_COMMAND    = 12,
    NCR_CMD_RECEIVE_DATA       = 13,
    NCR_CMD_RECEIVE_MESSAGE_OUT= 14,
    NCR_CMD_RECEIVE_INFO_OUT   = 15,
    NCR_CMD_SEND_STATUS        = 16,
    NCR_CMD_SEND_DATA          = 17,
    NCR_CMD_SEND_MESSAGE_OUT   = 18,
    NCR_CMD_SEND_INFO_IN       = 19,
    NCR_CMD_TRANSFER_INFO      = 20,
    NCR_CMD_TRANSFER_PAD       = 21
    /* 22-31 reserved */
} NCRCommandCode;

#define NCR_COMMAND_CODE_MASK      0x1F
#define NCR_CMD_FLAG_INTERRUPTING  (1 << 3)  /* set in the code => interrupting */
#define NCR_CMD_FLAG_RESERVED      (1 << 5)
#define NCR_CMD_FLAG_SINGLE_BYTE   (1 << 6)
#define NCR_CMD_FLAG_DMA_MODE      (1 << 7)
/* The Diagnostic (Data Turnaround) command uses bit 6 for something else:
 * "good parity is generated if bit 6 of the command is off (0); bad parity is
 * generated if bit 6 is on (1)" (NCR 5385E data sheet MC-704, May 1985,
 * section 5.5.10; the 5386 sheet refers to it for the Diagnostic command). */
#define NCR_CMD_FLAG_DIAG_BAD_PARITY NCR_CMD_FLAG_SINGLE_BYTE

/* Control Register #2 (ControlRegisterFlags) */
#define NCR_CTRL_SELECT_ENABLE     (1 << 0)
#define NCR_CTRL_RESELECT_ENABLE   (1 << 1)
#define NCR_CTRL_PARITY_ENABLE     (1 << 2)
#define NCR_CTRL_PHASE_VALID_ON_REQ (1 << 3)
#define NCR_CTRL_RESERVED_SYNC_OP  (1 << 4)
#define NCR_CTRL_MASK              0x1F

/* Auxiliary Status Register #4 (AuxilaryStatusRegisterFlags).
 * MSG/CD/IO always reflect the SCSI signals and together encode the phase the
 * target is requesting - see NCRPhase. */
#define NCR_AUX_NOT_USED           (1 << 0)
#define NCR_AUX_TRANSFER_COUNT_ZERO (1 << 1)
#define NCR_AUX_PAUSED             (1 << 2)
#define NCR_AUX_IO                 (1 << 3)
#define NCR_AUX_CD                 (1 << 4)
#define NCR_AUX_MSG                (1 << 5)
#define NCR_AUX_PARITY_ERROR       (1 << 6)
#define NCR_AUX_DATA_REGISTER_FULL (1 << 7)

/* Interrupt Register #6 (InterruptRegisterFlags) */
#define NCR_INT_FUNCTION_COMPLETE  (1 << 0)
#define NCR_INT_BUS_SERVICE        (1 << 1)
#define NCR_INT_DISCONNECTED       (1 << 2)
#define NCR_INT_SELECTED           (1 << 3)
#define NCR_INT_RESELECTED         (1 << 4)
#define NCR_INT_TESTABILITY        (1 << 5)
#define NCR_INT_INVALID_COMMAND    (1 << 6)
#define NCR_INT_NOT_USED           (1 << 7)
#define NCR_INT_MASK_VALID         (NCR_INT_FUNCTION_COMPLETE | NCR_INT_BUS_SERVICE | \
                                    NCR_INT_DISCONNECTED | NCR_INT_SELECTED | \
                                    NCR_INT_RESELECTED | NCR_INT_INVALID_COMMAND)

/* Source ID #7 (SourceIDFlags) */
#define NCR_SOURCE_ID_MASK         0x07
#define NCR_SOURCE_ID_VALID        (1 << 7)

/* Diagnostic Status Register #9 (DiagnosticStatusFlags) */
#define NCR_DIAG_SELF_STATUS_MASK          0x07
#define NCR_DIAG_SUCCESSFUL_COMPLETION     0
#define NCR_DIAG_UNCONDITIONAL_BRANCH_FAIL 1
#define NCR_DIAG_DATA_REG_FULL_FAILED      2
#define NCR_DIAG_INITIAL_CONDITIONS_BAD    3
#define NCR_DIAG_INITIAL_CMD_BITS_BAD      4
#define NCR_DIAG_FLAG_FAILED               5
#define NCR_DIAG_DATA_TURNAROUND_FAILED    6
/* Bits 6-3 hold the result of the last Diagnostic command ("BIT 6543",
 * MC-704 section 5.5.10); bits 7 and 2-0 are not touched by it (section 4.9). */
#define NCR_DIAG_COMMAND_STATUS_MASK       (0x0F << 3)
#define NCR_DIAG_TURNAROUND_MISCOMPARE_INITIAL (1 << 3)
#define NCR_DIAG_TURNAROUND_MISCOMPARE_FINAL   (2 << 3)
#define NCR_DIAG_TURNAROUND_GOOD_PARITY        (3 << 3)
#define NCR_DIAG_TURNAROUND_BAD_PARITY         (4 << 3)
#define NCR_DIAG_SELF_DIAGNOSTIC_COMPLETE      (1 << 7)

/* Chip connection state (ChipState) */
typedef enum {
    NCR_CHIP_DISABLED = 0,
    NCR_CHIP_DISCONNECTED,
    NCR_CHIP_INITIATOR,
    NCR_CHIP_TARGET
} NCRChipState;

/* SCSI information-transfer phase, encoded MSG:CD:IO (Phase).
 * The numeric values ARE the MSG/CD/IO bit pattern - do not renumber. */
typedef enum {
    NCR_PHASE_DATA_OUT    = 0,  /* MSG=0 CD=0 IO=0 */
    NCR_PHASE_DATA_IN     = 1,  /* MSG=0 CD=0 IO=1 */
    NCR_PHASE_COMMAND     = 2,  /* MSG=0 CD=1 IO=0 */
    NCR_PHASE_STATUS      = 3,  /* MSG=0 CD=1 IO=1 */
    NCR_PHASE_UNKNOWN4    = 4,
    NCR_PHASE_UNKNOWN5    = 5,
    NCR_PHASE_MESSAGE_OUT = 6,  /* MSG=1 CD=1 IO=0 */
    NCR_PHASE_MESSAGE_IN  = 7   /* MSG=1 CD=1 IO=1 */
} NCRPhase;

/* Internal state machine (State) */
typedef enum {
    NCR_STATE_IDLE = 0,
    NCR_STATE_DIAGNOSTIC,
    NCR_STATE_ARBITRATE_BUS_FREE,
    NCR_STATE_ARBITRATE_STARTED,
    NCR_STATE_ARBITRATE_EVALUATE,
    NCR_STATE_SELECTION_START,
    NCR_STATE_SELECTION_DELAY,
    NCR_STATE_SELECTION_WAIT_BSY,
    NCR_STATE_SELECTION_COMPLETE,
    NCR_STATE_SELECTION_WAIT_REQ,
    NCR_STATE_XFI_START,
    NCR_STATE_XFI_IN_REQ,
    NCR_STATE_XFI_IN_DRQ,
    NCR_STATE_XFI_IN_ACK,
    NCR_STATE_XFI_OUT_REQ,
    NCR_STATE_XFI_OUT_DRQ,
    NCR_STATE_XFI_OUT_ACK,
    NCR_STATE_XFI_OUT_PAD,
    NCR_STATE_MESSAGE_OUT_START
} NCRState;

/* INT / DREQ callback into the ND adapter. state is 1 = asserted, 0 = cleared. */
typedef void (*NCRSignalCallback)(void *context, uint8_t state);

typedef struct {
    SCSIDevice dev;             /* must be first-ish; dev.impl points back here */
    SCSIBus   *bus;

    /* Step timer. Under NO_SCSI_DELAY semantics every delay is 0, so this is
     * really just "step again on the next Clock". */
    bool     timerEnabled;
    int      timerTicks;

    uint8_t  m_dat;             /* Data Register (reg 0) */
    uint8_t  command_reg_flags; /* Command Register raw value (reg 1) */
    uint8_t  command_code;      /* command_reg_flags & NCR_COMMAND_CODE_MASK */
    bool     commandCodeLoaded; /* an interrupting command is pending execution */
    uint8_t  ctrl_reg;          /* Control Register (reg 2) */
    bool     controlRegisterWritten;
    uint8_t  destinationID;     /* reg 3 */
    uint8_t  aux_status_reg;    /* reg 4 */
    uint8_t  id_register;       /* reg 5 - own ID */
    uint8_t  int_reg;           /* reg 6 */
    uint8_t  sourceID;          /* reg 7 */
    uint8_t  diag_status_reg;   /* reg 9 */
    uint32_t transferCounter;   /* regs 12-14, 24-bit */

    NCRChipState chipState;
    NCRState     currentState;
    uint32_t     phase;         /* latched SCSI phase for the current transfer */
    bool         sbx;           /* single-byte-transfer outstanding */
    bool         pauseRequested;

    bool     m_int_state;
    bool     m_dreq_state;

    NCRSignalCallback onInterrupt;
    NCRSignalCallback onDataRequest;
    void             *callbackContext;
} NCR5386;

void    NCR5386_Init(NCR5386 *ncr, SCSIBus *bus, uint8_t own_id,
                     NCRSignalCallback onInterrupt, NCRSignalCallback onDataRequest,
                     void *callbackContext);
void    NCR5386_DeviceReset(NCR5386 *ncr);
bool    NCR5386_ChipDisabled(NCR5386 *ncr);
void    NCR5386_InitiateResetSCSIBus(NCR5386 *ncr);
uint8_t NCR5386_Read(NCR5386 *ncr, uint8_t address);
void    NCR5386_Write(NCR5386 *ncr, uint8_t address, uint8_t value);
uint8_t NCR5386_DMARead(NCR5386 *ncr);
void    NCR5386_DMAWrite(NCR5386 *ncr, uint8_t data);

/* Odd-parity test used by the chip's Diagnostic command (Parity.IsOddParity). */
bool    SCSI_IsOddParity(uint8_t value);

/* Raw SCSI bus signals, surfaced in the ND card's status word (RSTAU bits
 * 12/13/14). */
bool    NCR5386_SCSI_BSY(NCR5386 *ncr);
bool    NCR5386_SCSI_REQ(NCR5386 *ncr);
bool    NCR5386_SCSI_ACK(NCR5386 *ncr);

#endif // NCR5386_H
