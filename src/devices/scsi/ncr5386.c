/*
 * nd100x - ND-100 emulator
 *
 * ncr5386.c - NCR-5386 SCSI Protocol Controller
 *
 * Ported from RetroCore:
 *   <RetroCore>\Emulated.HW\NCR\SCSI\NCR5386\NCR5386SCSI.cs
 *   <RetroCore>\Emulated.HW\NCR\SCSI\NCR5386\NCR5386SCSI.CommandHandling.cs
 *   <RetroCore>\Emulated.HW\NCR\SCSI\NCR5386\NCR5386SCSI.StateHandling.cs
 *   <RetroCore>\Emulated.HW\NCR\SCSI\NCR5386\Registers.cs
 * which are a port of MAME's ncr5385.cpp (BSD-3-Clause, Ryan Holtz),
 * git SHA 25066795caded65502be102dd9f181ccae41748b (June 28 2024).
 *
 * TIMING: RetroCore builds this with NO_SCSI_DELAY defined, which makes
 * SCSIDevice.CalcTimeToTicks() return 0 for every delay. So all of MAME's
 * nanosecond timing constants (SCSI_ARB_DELAY, SCSI_SEL_TIMEOUT, ...) are dead
 * in the configuration that is known to work, and the state machine advances
 * exactly one state per Clock(). This port therefore keeps no timing model:
 * the delay returned by StepState only matters by its sign, where negative
 * means "stall, wait for an external event" (a bus change, a register access
 * or a DMA byte) rather than re-arming the step timer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#include "../devices_types.h"
#include "../devices_protos.h"

/* Step timer: a negative delay from NCR_StepState means "stall". */
#define NCR_DELAY_STALL (-1)


/* Odd parity over 8 bits (RetroCore Parity.IsOddParity). */
bool SCSI_IsOddParity(uint8_t value)
{
    value ^= (uint8_t)(value >> 4);
    value ^= (uint8_t)(value >> 2);
    value ^= (uint8_t)(value >> 1);
    return (value & 1) != 0;
}


static void NCR_UpdateInt(NCR5386 *ncr);
static void NCR_ExecuteLoadedCommand(NCR5386 *ncr);


static void NCR_Log(const char *fmt, ...)
{
    /* Kept as a single choke point so the whole chip log can be switched off
     * with --scsi-debug. Matches the "SCSI: " prefix convention of deviceSMD. */
    if (!scsi_debug_enabled)
        return;

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "SCSI/NCR: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}


/* Arm the step timer. delay is in Clock() ticks; 0 = step on the next Clock. */
static void NCR_AdjustTimer(NCR5386 *ncr, int ticks)
{
    ncr->timerEnabled = true;
    ncr->timerTicks = ticks;
}


static void NCR_GenerateInterrupt(NCR5386 *ncr, uint8_t intr)
{
    if (ncr->onInterrupt)
        ncr->onInterrupt(ncr->callbackContext, intr);
}


static void NCR_SetDreq(NCR5386 *ncr, bool dreq)
{
    if (ncr->m_dreq_state == dreq)
        return;

    ncr->m_dreq_state = dreq;
    if (ncr->onDataRequest)
        ncr->onDataRequest(ncr->callbackContext, dreq ? 1 : 0);
}


static void NCR_UpdateTCAux(NCR5386 *ncr)
{
    if (ncr->transferCounter == 0)
        ncr->aux_status_reg |= NCR_AUX_TRANSFER_COUNT_ZERO;
    else
        ncr->aux_status_reg &= ~NCR_AUX_TRANSFER_COUNT_ZERO;
}


/*
 * remaining() from StateHandling.cs.
 *   count < 0  -> "is there anything left"  (TransferCounter != 0)
 *   count >= 0 -> "is exactly count left"   (TransferCounter == count)
 * In single-byte-transfer mode the transfer counter is not used at all and the
 * sbx flag stands in for it.
 */
static bool NCR_Remaining(NCR5386 *ncr, long count)
{
    if (ncr->command_reg_flags & NCR_CMD_FLAG_SINGLE_BYTE)
        return ncr->sbx;

    if (count >= 0)
        return ncr->transferCounter == (uint32_t)count;

    return ncr->transferCounter != 0;
}


static void NCR_HandleError(NCR5386 *ncr, const char *description)
{
    ncr->int_reg |= NCR_INT_INVALID_COMMAND;
    NCR_UpdateInt(ncr);
    NCR_Log("Error: %s", description);
}


static void NCR_CommandChipReset(NCR5386 *ncr)
{
    ncr->command_reg_flags = 0;
    ncr->command_code = 0;
    ncr->controlRegisterWritten = false;
    ncr->ctrl_reg = 0;
    ncr->destinationID = 0;
    ncr->aux_status_reg = NCR_AUX_TRANSFER_COUNT_ZERO;
    ncr->int_reg = 0;
    /* NOTE: sourceID is deliberately NOT cleared - RetroCore has the
     * "regs.sourceID = 0;" line commented out in CommandChipReset(). */
    /* The chip re-reports a completed, successful self-diagnostic after every
     * reset. SINTRAN's SCSI bootstrap reads RDIST straight after issuing Chip
     * Reset and checks bit 7 here, so this must be set. */
    ncr->diag_status_reg = NCR_DIAG_SELF_DIAGNOSTIC_COMPLETE;
    ncr->transferCounter = 0;
    ncr->chipState = NCR_CHIP_DISCONNECTED;
    ncr->currentState = NCR_STATE_IDLE;
    ncr->commandCodeLoaded = false;
    ncr->pauseRequested = false;
    ncr->sbx = false;
    ncr->m_dat = 0;
    ncr->timerEnabled = false;
    ncr->timerTicks = 0;
}


/*
 * UpdateInt from StateHandling.cs.
 *
 * On an interrupt going active the chip clears the command register and latches
 * the phase (MSG/CD/IO) that the target is currently requesting, so the driver
 * can read it out of the Auxiliary Status Register while servicing.
 */
static void NCR_UpdateInt(NCR5386 *ncr)
{
    if (!ncr->bus)
        return;

    bool int_state = (ncr->int_reg & NCR_INT_MASK_VALID) != 0;
    if (ncr->m_int_state == int_state)
        return;

    ncr->aux_status_reg &= ~(NCR_AUX_MSG | NCR_AUX_CD | NCR_AUX_IO);

    if (int_state)
    {
        ncr->command_reg_flags = 0;

        /* latch current phase */
        uint32_t ctrl = scsi_bus_control_read(ncr->bus);
        if (ctrl & S_MSG)
            ncr->aux_status_reg |= NCR_AUX_MSG;
        if (ctrl & S_CTL)
            ncr->aux_status_reg |= NCR_AUX_CD;
        if (ctrl & S_INP)
            ncr->aux_status_reg |= NCR_AUX_IO;
    }

    ncr->m_int_state = int_state;
    NCR_GenerateInterrupt(ncr, int_state ? 1 : 0);
}


/* ------------------------------------------------------------------ */
/* Command execution (CommandHandling.cs ExecuteLoadedCommand)         */
/* ------------------------------------------------------------------ */
static void NCR_ExecuteLoadedCommand(NCR5386 *ncr)
{
    NCR_Log("Executing command: %d", ncr->command_code);

    switch (ncr->command_code)
    {
    /* ---- Immediate commands (do NOT raise an interrupt) ---- */
    case NCR_CMD_CHIP_RESET:
        NCR_Log("Reset");
        NCR_CommandChipReset(ncr);
        return;

    case NCR_CMD_DISCONNECT:
        if (ncr->chipState == NCR_CHIP_INITIATOR || ncr->chipState == NCR_CHIP_TARGET)
        {
            NCR_Log("Disconnect");
            ncr->chipState = NCR_CHIP_DISCONNECTED;
        }
        else
            NCR_HandleError(ncr, "Disconnect in wrong state");
        return;

    case NCR_CMD_PAUSE:
        if (ncr->chipState == NCR_CHIP_DISCONNECTED || ncr->chipState == NCR_CHIP_TARGET)
        {
            NCR_Log("Pause");
            ncr->pauseRequested = true;
            ncr->aux_status_reg |= NCR_AUX_PAUSED;
        }
        else
            NCR_HandleError(ncr, "Pause in wrong state");
        return;

    case NCR_CMD_SET_ATN:
        if (ncr->chipState == NCR_CHIP_INITIATOR)
        {
            NCR_Log("Set ATN");
            scsi_bus_control_write(ncr->bus, ncr->dev.refid, S_ATN, S_ATN);
        }
        else
            NCR_HandleError(ncr, "Set ATN in wrong state");
        return;

    case NCR_CMD_MESSAGE_ACCEPTED:
        if (ncr->chipState == NCR_CHIP_INITIATOR)
        {
            NCR_Log("Message accepted");
            scsi_bus_control_write(ncr->bus, ncr->dev.refid, 0, S_ACK);
        }
        else
            NCR_HandleError(ncr, "Message accepted in wrong state");
        return;

    case NCR_CMD_CHIP_DISABLE:
        NCR_Log("Chip Disabled");
        ncr->chipState = NCR_CHIP_DISABLED;
        return;

    case NCR_CMD_RESERVED6:
    case NCR_CMD_RESERVED7:
        NCR_HandleError(ncr, "Reserved command");
        return;

    /* ---- Interrupting commands ---- */
    case NCR_CMD_SELECT_WITH_ATN:
        if (ncr->chipState == NCR_CHIP_DISCONNECTED)
        {
            NCR_Log("Select w ATN device=%d", ncr->destinationID);
            ncr->currentState = NCR_STATE_ARBITRATE_BUS_FREE;
            NCR_AdjustTimer(ncr, 0);
        }
        else
            NCR_HandleError(ncr, "Select w/ATN in wrong state");
        break;

    case NCR_CMD_SELECT_WITHOUT_ATN:
        if (ncr->chipState == NCR_CHIP_DISCONNECTED)
        {
            NCR_Log("Select w/o ATN device=%d", ncr->destinationID);
            ncr->currentState = NCR_STATE_ARBITRATE_BUS_FREE;
            NCR_AdjustTimer(ncr, 0);
        }
        else
            NCR_HandleError(ncr, "Select w/o ATN in wrong state");
        break;

    case NCR_CMD_RESELECT:
        if (ncr->chipState == NCR_CHIP_DISCONNECTED)
        {
            /* TODO: try to re-select? RetroCore does not implement it either. */
            NCR_Log("Reselect");
            ncr->int_reg |= NCR_INT_DISCONNECTED;
            NCR_UpdateInt(ncr);
        }
        else
            NCR_HandleError(ncr, "Reselect in wrong state");
        break;

    case NCR_CMD_DIAGNOSTIC:
        if (ncr->chipState == NCR_CHIP_DISCONNECTED)
        {
            NCR_Log("Diagnostic");
            ncr->aux_status_reg &= ~NCR_AUX_DATA_REGISTER_FULL;
            ncr->currentState = NCR_STATE_DIAGNOSTIC;
            /* Expect a DATA write to finish the Diagnostic state. */
        }
        else
            NCR_HandleError(ncr, "Diagnostic in wrong state");
        break;

    /*
     * Target-role commands. The ND-3201 is always the initiator, so the chip
     * never reaches NCR_CHIP_TARGET and these always take the error path.
     * RetroCore leaves them as stubs that raise Disconnected; kept identical so
     * behaviour matches if SINTRAN ever issues one.
     */
    case NCR_CMD_RECEIVE_COMMAND:
    case NCR_CMD_RECEIVE_DATA:
    case NCR_CMD_RECEIVE_MESSAGE_OUT:
    case NCR_CMD_RECEIVE_INFO_OUT:
        if (ncr->chipState == NCR_CHIP_TARGET)
        {
            NCR_Log("Receive (target role, not implemented)");
            ncr->int_reg |= NCR_INT_DISCONNECTED;
            NCR_UpdateInt(ncr);
        }
        else
            NCR_HandleError(ncr, "Receive command in wrong state");

        /* RetroCore only does this for ReceiveData. */
        if (ncr->command_code == NCR_CMD_RECEIVE_DATA && ncr->pauseRequested)
            ncr->aux_status_reg |= NCR_AUX_PAUSED;
        break;

    case NCR_CMD_SEND_STATUS:
        if (ncr->chipState == NCR_CHIP_TARGET)
        {
            NCR_Log("Send Status");
            ncr->aux_status_reg &= ~NCR_AUX_DATA_REGISTER_FULL;
            ncr->aux_status_reg |= NCR_AUX_IO | NCR_AUX_CD;
            ncr->aux_status_reg &= ~NCR_AUX_MSG;
            ncr->int_reg |= NCR_INT_DISCONNECTED;
            NCR_UpdateInt(ncr);
        }
        else
            NCR_HandleError(ncr, "Send Status in wrong state");
        break;

    case NCR_CMD_SEND_DATA:
        if (ncr->chipState == NCR_CHIP_TARGET)
        {
            NCR_Log("Send Data");
            ncr->aux_status_reg &= ~NCR_AUX_DATA_REGISTER_FULL;
            ncr->aux_status_reg |= NCR_AUX_IO;
            ncr->aux_status_reg &= ~(NCR_AUX_CD | NCR_AUX_MSG);
            ncr->int_reg |= NCR_INT_DISCONNECTED;
            NCR_UpdateInt(ncr);
        }
        else
            NCR_HandleError(ncr, "Send Data in wrong state");
        break;

    case NCR_CMD_SEND_MESSAGE_OUT:
        if (ncr->chipState == NCR_CHIP_TARGET)
        {
            NCR_Log("Send MessageOut");
            ncr->aux_status_reg &= ~NCR_AUX_DATA_REGISTER_FULL;
            ncr->aux_status_reg |= NCR_AUX_IO | NCR_AUX_CD | NCR_AUX_MSG;
            ncr->int_reg |= NCR_INT_DISCONNECTED;
            NCR_UpdateInt(ncr);
        }
        else
            NCR_HandleError(ncr, "Send MessageOut in wrong state");
        break;

    case NCR_CMD_SEND_INFO_IN:
        if (ncr->chipState == NCR_CHIP_TARGET)
        {
            NCR_Log("Send InfoIn");
            ncr->aux_status_reg &= ~NCR_AUX_DATA_REGISTER_FULL;
            ncr->aux_status_reg |= NCR_AUX_IO | NCR_AUX_MSG;
            ncr->aux_status_reg &= ~NCR_AUX_CD;
            ncr->int_reg |= NCR_INT_DISCONNECTED;
            NCR_UpdateInt(ncr);
        }
        else
            NCR_HandleError(ncr, "Send InfoIn in wrong state");
        break;

    case NCR_CMD_TRANSFER_INFO:
    case NCR_CMD_TRANSFER_PAD:
        if (ncr->chipState == NCR_CHIP_INITIATOR)
        {
            NCR_Log("%s (%s, %s count=%u)",
                    (ncr->command_code == NCR_CMD_TRANSFER_INFO) ? "transfer info" : "transfer pad",
                    (ncr->command_reg_flags & NCR_CMD_FLAG_DMA_MODE) ? "dma" : "pio",
                    (ncr->command_reg_flags & NCR_CMD_FLAG_SINGLE_BYTE) ? "single byte" : "",
                    ncr->transferCounter);

            ncr->sbx = (ncr->command_reg_flags & NCR_CMD_FLAG_SINGLE_BYTE) != 0;
            ncr->currentState = NCR_STATE_XFI_START;
            NCR_AdjustTimer(ncr, 0);
        }
        else
            NCR_HandleError(ncr, "Transfer in wrong state");
        break;

    default:
        /* 22-31 reserved */
        NCR_HandleError(ncr, "Reserved command");
        return;
    }
}


/* ------------------------------------------------------------------ */
/* State machine (StateHandling.cs StepState)                          */
/* Returns NCR_DELAY_STALL to stop stepping, >= 0 to keep stepping.    */
/* ------------------------------------------------------------------ */
static int NCR_StepState(NCR5386 *ncr)
{
    if (!ncr->bus)
        return NCR_DELAY_STALL;

    int delay = 0;

    /*
     * Own ID for arbitration.
     *
     * NOTE: this uses sourceID, NOT id_register - copied verbatim from
     * StateHandling.cs. (Alignment 2026-07-17: an earlier comment here claimed
     * "RetroCore never writes sourceID, so the chip arbitrates as ID 0" - that
     * was STALE. RetroCore's ND adapter DOES write TW1=7 into SourceID, and
     * as of the 2026-07-17 alignment both the ND adapters (deviceSCSI.c
     * SCSI_Reset / Clear Device, and NDBusDiscControllerSCSI.SetSCSIIdNumber)
     * write the strapped own ID 7 into BOTH the ID Register (ROIDN readback)
     * and SourceID (this arbitration path), so both emulators arbitrate as
     * ID 7 / data bit 0x80. With a single initiator arbitration is
     * uncontested either way.) If target-initiated reselection is ever
     * implemented, sourceID must be latched from the reselecting target's ID
     * per the datasheet (the Source ID register is read-only on real HW).
     */
    uint8_t oid = (uint8_t)(1 << (ncr->sourceID & NCR_SOURCE_ID_MASK));
    uint8_t tid = (uint8_t)(1 << ncr->destinationID);
    uint32_t ctrl = scsi_bus_control_read(ncr->bus);

    /* Execute a loaded (interrupting) command before stepping the machine. */
    if (ncr->commandCodeLoaded)
    {
        NCR_ExecuteLoadedCommand(ncr);
        ncr->commandCodeLoaded = false;
        if (ncr->int_reg != 0)
            NCR_GenerateInterrupt(ncr, 1);
        return 2;
    }

    switch (ncr->currentState)
    {
    case NCR_STATE_IDLE:
        break;

    case NCR_STATE_DIAGNOSTIC:
        /* Wait for data to be written */
        break;

    /* ---------------- Arbitration ---------------- */
    case NCR_STATE_ARBITRATE_BUS_FREE:
        if (!(ctrl & (S_SEL | S_BSY | S_RST)))
        {
            ncr->currentState = NCR_STATE_ARBITRATE_STARTED;
        }
        break;

    case NCR_STATE_ARBITRATE_STARTED:
        ncr->currentState = NCR_STATE_ARBITRATE_EVALUATE;
        /* assert own ID and BSY */
        scsi_bus_data_write(ncr->bus, ncr->dev.refid, oid);
        scsi_bus_control_write(ncr->bus, ncr->dev.refid, S_BSY, S_BSY);
        break;

    case NCR_STATE_ARBITRATE_EVALUATE:
    {
        /* check if SEL asserted, or if there is a higher ID on the bus */
        uint8_t bus_id = scsi_bus_data_read(ncr->bus);
        uint8_t id_mask = (uint8_t)~((oid - 1) | oid);
        if ((ctrl & S_SEL) || (bus_id & id_mask) != 0)
        {
            NCR_Log("arbitration: lost");
            ncr->currentState = NCR_STATE_ARBITRATE_BUS_FREE;
            scsi_bus_data_write(ncr->bus, ncr->dev.refid, 0);
            scsi_bus_control_write(ncr->bus, ncr->dev.refid, 0, S_BSY);
        }
        else
        {
            NCR_Log("arbitration: won");
            ncr->currentState = NCR_STATE_SELECTION_START;
        }
        /* NOTE: RetroCore ("RH 2003") deliberately does NOT raise
         * FunctionComplete on arbitration won/lost - the lines are commented
         * out there. Do not add it back. */
        break;
    }

    /* ---------------- Selection ---------------- */
    case NCR_STATE_SELECTION_START:
        NCR_Log("selection: SEL asserted");
        ncr->currentState = NCR_STATE_SELECTION_DELAY;
        /* assert own and target ID and SEL */
        scsi_bus_data_write(ncr->bus, ncr->dev.refid, (uint8_t)(oid | tid));
        scsi_bus_control_write(ncr->bus, ncr->dev.refid, S_SEL, S_SEL);
        break;

    case NCR_STATE_SELECTION_DELAY:
        NCR_Log("selection: BSY cleared");
        ncr->currentState = NCR_STATE_SELECTION_WAIT_BSY;
        /* clear BSY, optionally assert ATN.
         * Select w/ATN is code 8 (bit0=0) and Select w/o ATN is code 9
         * (bit0=1), so bit 0 of the command code selects ATN. */
        if ((ncr->command_code & 0x01) == 0)
            scsi_bus_control_write(ncr->bus, ncr->dev.refid, S_ATN, S_BSY | S_ATN);
        else
            scsi_bus_control_write(ncr->bus, ncr->dev.refid, 0, S_BSY);
        break;

    case NCR_STATE_SELECTION_WAIT_BSY:
        if (ctrl & S_BSY)
        {
            NCR_Log("selection: BSY asserted by target");
            ncr->currentState = NCR_STATE_SELECTION_COMPLETE;
        }
        else
        {
            NCR_Log("selection: timed out");
            ncr->currentState = NCR_STATE_IDLE;
            scsi_bus_control_write(ncr->bus, ncr->dev.refid, 0, S_ATN | S_SEL);
            ncr->int_reg |= NCR_INT_DISCONNECTED;
            NCR_UpdateInt(ncr);
        }
        break;

    case NCR_STATE_SELECTION_COMPLETE:
        NCR_Log("selection: complete");
        ncr->chipState = NCR_CHIP_INITIATOR;
        ncr->currentState = NCR_STATE_SELECTION_WAIT_REQ;
        delay = NCR_DELAY_STALL;
        scsi_bus_data_write(ncr->bus, ncr->dev.refid, 0);
        scsi_bus_control_write(ncr->bus, ncr->dev.refid, 0, S_SEL);
        ncr->int_reg |= NCR_INT_FUNCTION_COMPLETE;
        NCR_UpdateInt(ncr);
        break;

    /* ---------------- Message Out ---------------- */
    case NCR_STATE_SELECTION_WAIT_REQ:
        /* ATN was raised while SEL was true but before BSY went false, per the
         * SCSI spec, so the target should ask for MESSAGE OUT to get IDENTIFY.
         *
         * NOTE the !m_int_state guard: do not raise Bus Service until the
         * Function Complete from selection has been read/cleared. */
        if ((ctrl & S_REQ) && !ncr->m_int_state)
        {
            NCR_Log("selection: REQ asserted by target");
            ncr->int_reg |= NCR_INT_BUS_SERVICE;
            ncr->currentState = NCR_STATE_IDLE;
            NCR_UpdateInt(ncr);
        }
        else
            delay = NCR_DELAY_STALL;
        break;

    case NCR_STATE_MESSAGE_OUT_START:
        NCR_Log("Started to send message to device");
        delay = NCR_DELAY_STALL;
        break;

    /* ---------------- Transfer Info ---------------- */
    case NCR_STATE_XFI_START:
        ncr->phase = ctrl & S_PHASE_MASK;
        ncr->currentState = (ctrl & S_INP) ? NCR_STATE_XFI_IN_REQ : NCR_STATE_XFI_OUT_REQ;
        break;

    case NCR_STATE_XFI_IN_REQ:
        /* TODO: disconnect */
        if (ctrl & S_REQ)
        {
            if (NCR_Remaining(ncr, -1) && (ctrl & S_PHASE_MASK) == ncr->phase)
            {
                ncr->currentState = NCR_STATE_XFI_IN_DRQ;
                /* transfer pad in does not transfer any data. Transfer Info is
                 * code 20 (bit0=0), Transfer Pad is 21 (bit0=1). */
                if ((ncr->command_code & 0x01) == 0)
                {
                    ncr->aux_status_reg |= NCR_AUX_DATA_REGISTER_FULL;
                    ncr->m_dat = scsi_bus_data_read(ncr->bus);
                    if (ncr->command_reg_flags & NCR_CMD_FLAG_DMA_MODE)
                        NCR_SetDreq(ncr, true);
                    delay = NCR_DELAY_STALL;
                }
            }
            else
            {
                NCR_Log("xfi_in: %s", NCR_Remaining(ncr, -1) ? "phase change" : "transfer complete");
                ncr->int_reg |= NCR_INT_BUS_SERVICE;
                ncr->currentState = NCR_STATE_IDLE;
                NCR_UpdateInt(ncr);
            }
        }
        else
            delay = NCR_DELAY_STALL;
        break;

    case NCR_STATE_XFI_IN_DRQ:
        ncr->currentState = NCR_STATE_XFI_IN_ACK;
        scsi_bus_control_write(ncr->bus, ncr->dev.refid, S_ACK, S_ACK);
        break;

    case NCR_STATE_XFI_IN_ACK:
        if (!(ctrl & S_REQ))
        {
            ncr->currentState = NCR_STATE_XFI_IN_REQ;
            if (!(ncr->command_reg_flags & NCR_CMD_FLAG_SINGLE_BYTE))
            {
                ncr->transferCounter--;
                if (ncr->transferCounter == 0)
                    ncr->aux_status_reg |= NCR_AUX_TRANSFER_COUNT_ZERO;
            }
            else
                ncr->sbx = false;

            /* clear ACK except after the last byte of message input phase */
            if (!NCR_Remaining(ncr, -1) && (ctrl & S_PHASE_MASK) == S_PHASE_MSG_IN)
            {
                ncr->int_reg |= NCR_INT_FUNCTION_COMPLETE;
                ncr->currentState = NCR_STATE_IDLE;
                NCR_UpdateInt(ncr);
            }
            else
                scsi_bus_control_write(ncr->bus, ncr->dev.refid, 0, S_ACK);
        }
        else
            delay = NCR_DELAY_STALL;
        break;

    case NCR_STATE_XFI_OUT_REQ:
        if (ctrl & S_REQ)
        {
            /* TODO: disconnect */
            if (NCR_Remaining(ncr, -1) && (ctrl & S_PHASE_MASK) == ncr->phase)
            {
                ncr->currentState = NCR_STATE_XFI_OUT_DRQ;
                /* FIXME (from RetroCore): only one byte dma for transfer pad */
                if (ncr->command_reg_flags & NCR_CMD_FLAG_DMA_MODE)
                    NCR_SetDreq(ncr, true);
                if (!(ncr->aux_status_reg & NCR_AUX_DATA_REGISTER_FULL))
                    delay = NCR_DELAY_STALL;
            }
            else
            {
                NCR_Log("xfi_out: %s", NCR_Remaining(ncr, -1) ? "phase change" : "transfer complete");
                ncr->int_reg |= NCR_INT_BUS_SERVICE;
                ncr->currentState = NCR_STATE_IDLE;
                NCR_UpdateInt(ncr);
            }
        }
        else
            delay = NCR_DELAY_STALL;
        break;

    case NCR_STATE_XFI_OUT_DRQ:
        ncr->currentState = NCR_STATE_XFI_OUT_ACK;
        ncr->aux_status_reg &= ~NCR_AUX_DATA_REGISTER_FULL;
        /* assert data and ACK; drop ATN on the last byte of message out */
        scsi_bus_data_write(ncr->bus, ncr->dev.refid, ncr->m_dat);
        if (NCR_Remaining(ncr, 1) && (ctrl & S_PHASE_MASK) == S_PHASE_MSG_OUT)
            scsi_bus_control_write(ncr->bus, ncr->dev.refid, S_ACK, S_ACK | S_ATN);
        else
            scsi_bus_control_write(ncr->bus, ncr->dev.refid, S_ACK, S_ACK);
        break;

    case NCR_STATE_XFI_OUT_ACK:
        if (!(ctrl & S_REQ))
        {
            if ((ncr->command_code & 0x01) != 0)
                ncr->currentState = NCR_STATE_XFI_OUT_PAD;
            else
                ncr->currentState = NCR_STATE_XFI_OUT_REQ;

            if (!(ncr->command_reg_flags & NCR_CMD_FLAG_SINGLE_BYTE))
            {
                ncr->transferCounter--;
                if (ncr->transferCounter == 0)
                    ncr->aux_status_reg |= NCR_AUX_TRANSFER_COUNT_ZERO;
            }
            else
                ncr->sbx = false;

            scsi_bus_data_write(ncr->bus, ncr->dev.refid, 0);
            scsi_bus_control_write(ncr->bus, ncr->dev.refid, 0, S_ACK);
        }
        else
            delay = NCR_DELAY_STALL;
        break;

    case NCR_STATE_XFI_OUT_PAD:
        if (ctrl & S_REQ)
        {
            /* TODO: disconnect */
            if (NCR_Remaining(ncr, -1) && (ctrl & S_PHASE_MASK) == ncr->phase)
                ncr->currentState = NCR_STATE_XFI_OUT_DRQ;
            else
            {
                NCR_Log("xfi_out: %s", NCR_Remaining(ncr, -1) ? "phase change" : "transfer complete");
                ncr->int_reg |= NCR_INT_BUS_SERVICE;
                ncr->currentState = NCR_STATE_IDLE;
                NCR_UpdateInt(ncr);
            }
        }
        break;

    default:
        break;
    }

    return delay;
}


/* ------------------------------------------------------------------ */
/* SCSIDevice vtable hooks                                             */
/* ------------------------------------------------------------------ */
static void NCR_Clock(SCSIDevice *self)
{
    NCR5386 *ncr = (NCR5386 *)self->impl;
    if (!ncr || !ncr->timerEnabled)
        return;

    if (ncr->timerTicks > 0)
    {
        ncr->timerTicks--;
        return;
    }

    ncr->timerEnabled = false;

    int delay = NCR_StepState(ncr);

    /* Negative delay = data stall: something external (a bus change, a register
     * read/write, or a DMA byte) has to re-arm the timer. */
    if (delay < 0)
        return;

    /* NO_SCSI_DELAY: CalcTimeToTicks() is a hard 0 in RetroCore, so the delay
     * value never becomes a tick count - we just step again next Clock. */
    if (ncr->currentState != NCR_STATE_IDLE)
        NCR_AdjustTimer(ncr, 0);
}


static void NCR_CtrlChanged(SCSIDevice *self)
{
    NCR5386 *ncr = (NCR5386 *)self->impl;
    if (!ncr || !ncr->bus)
        return;

    uint32_t ctrl = scsi_bus_control_read(ncr->bus);

    if ((ctrl & S_BSY) && !(ctrl & S_SEL))
    {
        if (ncr->currentState != NCR_STATE_IDLE)
            NCR_AdjustTimer(ncr, 0);
    }
    else if (ctrl & S_BSY)
    {
        /* arbitration / selection in progress */
    }
    else
    {
        NCR_Log("bus free");
        if (ncr->chipState == NCR_CHIP_INITIATOR)
        {
            ncr->chipState = NCR_CHIP_DISCONNECTED;
            ncr->int_reg |= NCR_INT_DISCONNECTED;
            NCR_UpdateInt(ncr);
        }
    }
}


/* ------------------------------------------------------------------ */
/* Public chip interface                                               */
/* ------------------------------------------------------------------ */
void NCR5386_Init(NCR5386 *ncr, SCSIBus *bus, uint8_t own_id,
                  NCRSignalCallback onInterrupt, NCRSignalCallback onDataRequest,
                  void *callbackContext)
{
    memset(ncr, 0, sizeof(NCR5386));

    ncr->dev.name = "NCR5386";
    ncr->dev.scsi_id = own_id;
    ncr->dev.Clock = NCR_Clock;
    ncr->dev.ctrl_changed = NCR_CtrlChanged;
    ncr->dev.impl = ncr;
    ncr->dev.refid = -1;

    ncr->onInterrupt = onInterrupt;
    ncr->onDataRequest = onDataRequest;
    ncr->callbackContext = callbackContext;

    scsi_bus_add_device(bus, &ncr->dev);
    ncr->bus = bus;

    NCR_CommandChipReset(ncr);
    ncr->diag_status_reg = NCR_DIAG_SELF_DIAGNOSTIC_COMPLETE;
}


void NCR5386_DeviceReset(NCR5386 *ncr)
{
    if (!ncr)
        return;

    NCR_CommandChipReset(ncr);

    if (ncr->bus)
    {
        /* monitor all control lines (this device has no RST line) */
        uint32_t p = S_ALL & ~S_RST;
        scsi_bus_control_wait(ncr->bus, ncr->dev.refid, p, p);
    }

    NCR_UpdateInt(ncr);
}


bool NCR5386_ChipDisabled(NCR5386 *ncr)
{
    return ncr && (ncr->chipState == NCR_CHIP_DISABLED);
}


/* Raw bus signals for the ND card's status word. */
bool NCR5386_SCSI_BSY(NCR5386 *ncr)
{
    return ncr && ncr->bus && (scsi_bus_control_read(ncr->bus) & S_BSY) != 0;
}

bool NCR5386_SCSI_REQ(NCR5386 *ncr)
{
    return ncr && ncr->bus && (scsi_bus_control_read(ncr->bus) & S_REQ) != 0;
}

bool NCR5386_SCSI_ACK(NCR5386 *ncr)
{
    return ncr && ncr->bus && (scsi_bus_control_read(ncr->bus) & S_ACK) != 0;
}


void NCR5386_InitiateResetSCSIBus(NCR5386 *ncr)
{
    if (!ncr || !ncr->bus)
        return;

    scsi_bus_control_write(ncr->bus, ncr->dev.refid, 0, S_ALL);      /* clear all */
    scsi_bus_control_write(ncr->bus, ncr->dev.refid, S_RST, S_RST);
    scsi_bus_control_write(ncr->bus, ncr->dev.refid, 0, S_ALL);      /* clear all */
}


static void NCR_WriteDataRegister(NCR5386 *ncr, uint8_t value)
{
    if (ncr->aux_status_reg & NCR_AUX_DATA_REGISTER_FULL)
    {
        NCR_Log("Data register full");
    }
    else
    {
        ncr->m_dat = value;
        ncr->aux_status_reg |= NCR_AUX_DATA_REGISTER_FULL;
        if (ncr->currentState != NCR_STATE_IDLE)
            NCR_AdjustTimer(ncr, 0);
    }

    /* A data write also terminates the Diagnostic state. */
    if (ncr->currentState == NCR_STATE_DIAGNOSTIC)
    {
        ncr->m_dat = value;
        uint8_t diag_command = ncr->command_reg_flags & NCR_DIAG_COMMAND_STATUS_MASK;

        switch (diag_command)
        {
        case NCR_DIAG_TURNAROUND_MISCOMPARE_INITIAL:
            ncr->aux_status_reg |= NCR_AUX_DATA_REGISTER_FULL;
            ncr->diag_status_reg = NCR_DIAG_SELF_DIAGNOSTIC_COMPLETE;
            if (SCSI_IsOddParity(value))
                ncr->diag_status_reg |= NCR_DIAG_TURNAROUND_GOOD_PARITY;
            break;
        case NCR_DIAG_TURNAROUND_MISCOMPARE_FINAL:
            ncr->diag_status_reg = NCR_DIAG_SELF_DIAGNOSTIC_COMPLETE;
            ncr->aux_status_reg |= NCR_AUX_DATA_REGISTER_FULL;
            break;
        case NCR_DIAG_TURNAROUND_GOOD_PARITY:
            ncr->aux_status_reg &= ~NCR_AUX_PARITY_ERROR;
            ncr->aux_status_reg |= NCR_AUX_DATA_REGISTER_FULL;
            ncr->diag_status_reg = NCR_DIAG_SELF_DIAGNOSTIC_COMPLETE | NCR_DIAG_TURNAROUND_GOOD_PARITY;
            break;
        case NCR_DIAG_TURNAROUND_BAD_PARITY:
            ncr->aux_status_reg |= NCR_AUX_PARITY_ERROR | NCR_AUX_DATA_REGISTER_FULL;
            ncr->diag_status_reg = NCR_DIAG_SELF_DIAGNOSTIC_COMPLETE | NCR_DIAG_TURNAROUND_BAD_PARITY;
            break;
        default:
            break;
        }

        ncr->currentState = NCR_STATE_IDLE;
        ncr->int_reg |= NCR_INT_FUNCTION_COMPLETE;
        NCR_UpdateInt(ncr);
    }
}


static void NCR_WriteCommandRegister(NCR5386 *ncr, uint8_t value)
{
    ncr->command_reg_flags = value;
    ncr->command_code = value & NCR_COMMAND_CODE_MASK;

    if (ncr->command_code >= NCR_CMD_SELECT_WITH_ATN)
    {
        /* NOTE: this is "&= Paused", NOT "&= ~Paused" - it masks the aux status
         * down to ONLY the Paused bit, clearing everything else. Copied
         * verbatim from WriteCommandRegister() in NCR5386SCSI.cs. */
        ncr->aux_status_reg &= NCR_AUX_PAUSED;
        ncr->aux_status_reg &= ~NCR_AUX_DATA_REGISTER_FULL;
    }

    /* Immediate commands (code < 8) complete within ~3 clocks of the write.
     * Diagnostic is an interrupting command but is also started immediately. */
    if ((ncr->command_code < NCR_CMD_SELECT_WITH_ATN) || (ncr->command_code == NCR_CMD_DIAGNOSTIC))
    {
        NCR_ExecuteLoadedCommand(ncr);
    }
    else
    {
        ncr->commandCodeLoaded = true;
        NCR_AdjustTimer(ncr, 2); /* step the machine after 2 clock ticks */
    }
}


static uint8_t NCR_ReadInterruptRegister(NCR5386 *ncr)
{
    uint8_t value = ncr->int_reg;

    /* Reading the interrupt register clears it (and the parity error). */
    ncr->aux_status_reg &= ~NCR_AUX_PARITY_ERROR;
    ncr->int_reg = 0;
    NCR_UpdateInt(ncr);

    if (ncr->currentState != NCR_STATE_IDLE)
        NCR_AdjustTimer(ncr, 0);

    return value;
}


void NCR5386_Write(NCR5386 *ncr, uint8_t address, uint8_t value)
{
    if (!ncr)
        return;

    NCRRegister r = (NCRRegister)(address & 0x0F);

    switch (r)
    {
    case NCR_REG_DATA:
        NCR_WriteDataRegister(ncr, value);
        break;
    case NCR_REG_COMMAND:
        NCR_WriteCommandRegister(ncr, value);
        break;
    case NCR_REG_CONTROL:
        ncr->ctrl_reg = value & NCR_CTRL_MASK;
        ncr->controlRegisterWritten = true;
        break;
    case NCR_REG_DESTINATION_ID:
        ncr->destinationID = value & 0x07;
        break;
    case NCR_REG_AUX_STATUS:
        NCR_Log("Unexpected - Write to AuxilaryStatus");
        break;
    case NCR_REG_ID:
        ncr->id_register = value;
        break;
    case NCR_REG_INTERRUPT:
        NCR_Log("Unexpected - Write to InterruptRegister");
        break;
    case NCR_REG_SOURCE_ID:
        ncr->sourceID = value;
        break;
    case NCR_REG_DATA_II:
        /* WriteDataRegisterII is empty in RetroCore */
        break;
    case NCR_REG_DIAGNOSTIC_STATUS:
        break;
    case NCR_REG_TRANSFER_COUNT_MSB:
        ncr->transferCounter = (ncr->transferCounter & 0x00FFFF) | ((uint32_t)value << 16);
        NCR_UpdateTCAux(ncr);
        break;
    case NCR_REG_TRANSFER_COUNT_MID:
        ncr->transferCounter = (ncr->transferCounter & 0xFF00FF) | ((uint32_t)value << 8);
        NCR_UpdateTCAux(ncr);
        break;
    case NCR_REG_TRANSFER_COUNT_LSB:
        ncr->transferCounter = (ncr->transferCounter & 0xFFFF00) | (uint32_t)value;
        NCR_UpdateTCAux(ncr);
        break;
    case NCR_REG_RESERVED:
        NCR_Log("Reserved / invalid cmd");
        ncr->int_reg |= NCR_INT_INVALID_COMMAND;
        NCR_UpdateInt(ncr);
        break;
    default:
        break;
    }
}


uint8_t NCR5386_Read(NCR5386 *ncr, uint8_t address)
{
    if (!ncr)
        return 0;

    uint8_t value = 0;
    NCRRegister r = (NCRRegister)(address & 0x0F);

    switch (r)
    {
    case NCR_REG_DATA:
        if (ncr->bus)
        {
            if (ncr->aux_status_reg & NCR_AUX_DATA_REGISTER_FULL)
            {
                ncr->aux_status_reg &= ~NCR_AUX_DATA_REGISTER_FULL;
                if (ncr->currentState != NCR_STATE_IDLE)
                    NCR_AdjustTimer(ncr, 0);
            }
            else
                NCR_Log("ERROR: data register empty");
            value = ncr->m_dat;
        }
        break;

    case NCR_REG_COMMAND:
        /* The chip resets the command register when it sets an interrupt, so
         * the driver cannot rely on reading back an interrupting command. */
        value = ncr->command_reg_flags;
        break;

    case NCR_REG_CONTROL:
        value = ncr->ctrl_reg;
        break;

    case NCR_REG_DESTINATION_ID:
        value = ncr->destinationID;
        break;

    case NCR_REG_AUX_STATUS:
    {
        uint8_t aux = ncr->aux_status_reg;

        /* NOTE (RetroCore, "Ronny 22.03.2024"): the phase MUST be re-latched on
         * every read, not just while an interrupt is pending - "if not ND
         * SINTRAN and tools doesnt work". This is a deliberate divergence from
         * MAME. Do not gate it on int_reg. */
        uint32_t ctrl = scsi_bus_control_read(ncr->bus);
        if (ctrl & S_MSG)
            aux |= NCR_AUX_MSG;
        if (ctrl & S_CTL)
            aux |= NCR_AUX_CD;
        if (ctrl & S_INP)
            aux |= NCR_AUX_IO;

        value = aux;
        break;
    }

    case NCR_REG_ID:
        value = ncr->id_register;
        break;

    case NCR_REG_INTERRUPT:
        value = NCR_ReadInterruptRegister(ncr);
        break;

    case NCR_REG_SOURCE_ID:
        value = ncr->sourceID;
        break;

    case NCR_REG_DATA_II:
        break;

    case NCR_REG_DIAGNOSTIC_STATUS:
        value = ncr->diag_status_reg;
        break;

    case NCR_REG_TRANSFER_COUNT_MSB:
        value = (uint8_t)((ncr->transferCounter >> 16) & 0xFF);
        break;
    case NCR_REG_TRANSFER_COUNT_MID:
        value = (uint8_t)((ncr->transferCounter >> 8) & 0xFF);
        break;
    case NCR_REG_TRANSFER_COUNT_LSB:
        value = (uint8_t)(ncr->transferCounter & 0xFF);
        break;

    case NCR_REG_RESERVED:
    default:
        break;
    }

    return value;
}


/* DMA side: the adapter pulls a byte out of the chip (SCSI -> ND memory). */
uint8_t NCR5386_DMARead(NCR5386 *ncr)
{
    uint8_t data = ncr->m_dat;
    ncr->aux_status_reg &= ~NCR_AUX_DATA_REGISTER_FULL;
    NCR_SetDreq(ncr, false);
    NCR_AdjustTimer(ncr, 0);
    return data;
}


/* DMA side: the adapter pushes a byte into the chip (ND memory -> SCSI). */
void NCR5386_DMAWrite(NCR5386 *ncr, uint8_t data)
{
    ncr->m_dat = data;
    ncr->aux_status_reg |= NCR_AUX_DATA_REGISTER_FULL;
    NCR_SetDreq(ncr, false);
    NCR_AdjustTimer(ncr, 0);
}
