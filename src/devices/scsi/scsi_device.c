/*
 * nd100x - ND-100 emulator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * scsi_device.c - SCSI target-side phase state machine
 *
 * Ported from RetroCore:
 *   <RetroCore>\Emulated.HW\Common\SCSI\SCSIFullDevice.cs
 * which is a port of MAME's nscsi_full_device.
 *
 * See scsi_device.h for the timing note (all delay hooks are 0).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#include "../devices_types.h"
#include "../devices_protos.h"

static void SCSITarget_Step(SCSITarget *t, bool timeout);


/* ------------------------------------------------------------------ */
/* Big-endian accessors (SCSISupport.cs Buffer)                        */
/* ------------------------------------------------------------------ */
void scsi_put_u16be(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)(value >> 8);
    buf[1] = (uint8_t)(value);
}

void scsi_put_u24be(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)(value >> 16);
    buf[1] = (uint8_t)(value >> 8);
    buf[2] = (uint8_t)(value);
}

void scsi_put_u32be(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)(value >> 24);
    buf[1] = (uint8_t)(value >> 16);
    buf[2] = (uint8_t)(value >> 8);
    buf[3] = (uint8_t)(value);
}

uint16_t scsi_get_u16be(const uint8_t *buf)
{
    return (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
}

uint32_t scsi_get_u24be(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
}

uint32_t scsi_get_u32be(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | buf[3];
}


static void SCSITarget_Log(SCSITarget *t, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void SCSITarget_Log(SCSITarget *t, const char *fmt, ...)
{
    if (!Log_IsEnabled(LOG_CAT_SCSI, LOG_DEBUG))
    {
        return;
    }

    char msg[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    Log_Write(LOG_CAT_SCSI, LOG_DEBUG, "TGT%d: %s", t->dev.scsi_id, msg);
}


static void SCSITarget_AdjustTimer(SCSITarget *t, int ticks, bool param)
{
    t->timerEnabled = true;
    t->timerTicks = ticks;
    t->timerParam = param;
}


/* ------------------------------------------------------------------ */
/* buf_control FIFO                                                    */
/* ------------------------------------------------------------------ */
static SCSIBufControl *SCSITarget_BufControlPush(SCSITarget *t)
{
    if (t->buf_control_wpos == SCSI_BUF_CONTROL_SIZE)
    {
        /* RetroCore throws here. There is no sane recovery - the phase queue is
         * corrupt - so log loudly and reuse the last slot rather than smashing
         * the stack. */
        LOG(LOG_CAT_SCSI, LOG_ERROR, "TGT%d: FATAL buf_control overflow", t->dev.scsi_id);
        return &t->buf_control[SCSI_BUF_CONTROL_SIZE - 1];
    }

    SCSIBufControl *c = &t->buf_control[t->buf_control_wpos];
    c->action = 0;
    c->param1 = 0;
    c->param2 = 0;
    t->buf_control_wpos++;
    return c;
}


static SCSIBufControl *SCSITarget_BufControlPop(SCSITarget *t)
{
    if (t->buf_control_rpos == t->buf_control_wpos)
    {
        LOG(LOG_CAT_SCSI, LOG_ERROR, "TGT%d: FATAL buf_control underflow", t->dev.scsi_id);
        return &t->buf_control[0];
    }

    SCSIBufControl *c = &t->buf_control[t->buf_control_rpos];
    t->buf_control_rpos++;
    if (t->buf_control_rpos == t->buf_control_wpos)
    {
        t->buf_control_rpos = t->buf_control_wpos = 0;
    }
    return c;
}


/* ------------------------------------------------------------------ */
/* Data buffer accessors                                               */
/* ------------------------------------------------------------------ */
uint8_t SCSITarget_DefaultGetData(SCSITarget *t, SBUF id, int pos)
{
    switch (id)
    {
    case SBUF_MAIN:
        if (pos < 0 || pos >= SCSI_CMDBUF_SIZE)
        {
            return 0;
        }
        return t->scsi_cmdbuf[pos];
    case SBUF_SENSE:
        if (pos < 0 || pos >= SCSI_SENSE_BUFFER_SIZE)
        {
            return 0;
        }
        return t->scsi_sense_buffer[pos];
    default:
        SCSITarget_Log(t, "FATALERROR: scsi_get_data - unknown id %d", id);
        return 0;
    }
}


void SCSITarget_DefaultPutData(SCSITarget *t, SBUF id, int pos, uint8_t data)
{
    switch (id)
    {
    case SBUF_MAIN:
        if (pos >= 0 && pos < SCSI_CMDBUF_SIZE)
        {
            t->scsi_cmdbuf[pos] = data;
        }
        break;
    case SBUF_SENSE:
        if (pos >= 0 && pos < SCSI_SENSE_BUFFER_SIZE)
        {
            t->scsi_sense_buffer[pos] = data;
        }
        break;
    default:
        SCSITarget_Log(t, "FATALERROR: scsi_put_data - unknown id %d", id);
        break;
    }
}


static uint8_t SCSITarget_GetData(SCSITarget *t, SBUF id, int pos)
{
    if (t->scsi_get_data)
    {
        return t->scsi_get_data(t, id, pos);
    }
    return SCSITarget_DefaultGetData(t, id, pos);
}


static void SCSITarget_PutData(SCSITarget *t, SBUF id, int pos, uint8_t data)
{
    if (t->scsi_put_data)
    {
        t->scsi_put_data(t, id, pos, data);
    }
    else
    {
        SCSITarget_DefaultPutData(t, id, pos, data);
    }
}


/* ------------------------------------------------------------------ */
/* Phase queueing (called from a concrete target's scsi_command)       */
/* ------------------------------------------------------------------ */
void SCSITarget_DataIn(SCSITarget *t, SBUF buf, int size)
{
    SCSIBufControl *c = SCSITarget_BufControlPush(t);
    c->action = BC_DATA_IN;
    c->param1 = (int)buf;
    c->param2 = size;
    SCSITarget_Log(t, "scsi_data_in: queued size=%d buf=%d wpos=%d", size, buf,
                   t->buf_control_wpos);
}


void SCSITarget_DataOut(SCSITarget *t, SBUF buf, int size)
{
    SCSIBufControl *c = SCSITarget_BufControlPush(t);
    c->action = BC_DATA_OUT;
    c->param1 = (int)buf;
    c->param2 = size;
}


/* Queue STATUS + COMMAND COMPLETE message + BUS FREE. */
void SCSITarget_StatusComplete(SCSITarget *t, uint8_t status)
{
    SCSITarget_Log(t, "scsi_status_complete: status=0x%02X", status);

    SCSIBufControl *c = SCSITarget_BufControlPush(t);
    c->action = BC_STATUS;
    c->param1 = status;

    c = SCSITarget_BufControlPush(t);
    c->action = BC_MESSAGE_1;
    c->param1 = SM_COMMAND_COMPLETE;

    c = SCSITarget_BufControlPush(t);
    c->action = BC_BUS_FREE;
}


/* ------------------------------------------------------------------ */
/* Sense                                                               */
/* ------------------------------------------------------------------ */
static void SCSITarget_SetSenseData(SCSITarget *t, uint8_t sense_key, uint16_t sense_key_code,
                                    const SCSISenseData *data)
{
    /*
     * NOTE: RetroCore's set_sense_data() clear loop is
     *     for (int i = 0; i < 18; i++) scsi_sense_buffer[0] = 0;
     * which only ever zeroes byte 0 - the index is a typo. It is harmless in C#
     * because arrays are zero-initialised, so the buffer is clean in practice
     * and the verified hardware trace comes out all-zero outside the fields set
     * below. Porting that loop literally into C would instead leave stale bytes
     * and diverge from the observed behaviour, so we clear all 18 bytes: this
     * matches what RetroCore actually produces, which is what parity means here.
     */
    memset(t->scsi_sense_buffer, 0, SCSI_SENSE_BUFFER_SIZE);

    if (data)
    {
        /* Even though SCSI-2 section 8.2.14 implies the valid bit should always
         * be set, other sections such as 10.2.12 disagree. */
        t->scsi_sense_buffer[0] =
            (uint8_t)((data->invalid ? 0 : 0x80) | (data->deferred ? 0x71 : 0x70));
        t->scsi_sense_buffer[2] = (uint8_t)((data->filemark ? 0x80 : 0) | (data->eom ? 0x40 : 0) |
                                            (data->bad_len ? 0x20 : 0));
        /* information field, bytes 3-6, big-endian */
        scsi_put_u32be(&t->scsi_sense_buffer[3], (uint32_t)data->info);
    }
    else
    {
        t->scsi_sense_buffer[0] = 0xf0;
    }

    t->scsi_sense_buffer[2] |= (uint8_t)(sense_key & 0x0f);
    t->scsi_sense_buffer[7] = 10;                              /* additional sense length */
    scsi_put_u16be(&t->scsi_sense_buffer[12], sense_key_code); /* ASC / ASCQ */
}


void SCSITarget_Sense(SCSITarget *t, bool deferred, uint8_t key, int asc, int ascq)
{
    SCSISenseData s;
    memset(&s, 0, sizeof(s));
    s.deferred = deferred;

    uint16_t code = (uint16_t)((asc << 8) | ascq);
    SCSITarget_SetSenseData(t, key, code, &s);
}


void SCSITarget_ReportCondition(SCSITarget *t, uint8_t sense_key, uint16_t sense_key_code,
                                const SCSISenseData *data)
{
    SCSITarget_SetSenseData(t, sense_key, sense_key_code, data);
    SCSITarget_StatusComplete(t, SS_CHECK_CONDITION);
}


void SCSITarget_ReportBadCmd(SCSITarget *t, uint8_t cmd)
{
    SCSITarget_Log(t, "cmd 0x%02X    *** BAD COMMAND", cmd);
    SCSITarget_ReportCondition(t, SK_ILLEGAL_REQUEST, SKC_INVALID_COMMAND_OPERATION_CODE, NULL);
}


void SCSITarget_ReportBadLun(SCSITarget *t, uint8_t cmd, uint8_t lun)
{
    SCSITarget_Log(t, "cmd 0x%02X lun=%d    *** BAD LUN", cmd, lun);
    SCSITarget_ReportCondition(t, SK_ILLEGAL_REQUEST, SKC_LOGICAL_UNIT_NOT_SUPPORTED, NULL);
}


/* ------------------------------------------------------------------ */
/* Byte transfer helpers                                               */
/* ------------------------------------------------------------------ */
static void SCSITarget_RecvByte(SCSITarget *t)
{
    SCSIBus_ControlWait(t->bus, t->dev.refid, S_ACK, S_ACK);
    t->scsi_state = (t->scsi_state & SCSI_STATE_MASK) | TS_RECV_BYTE_T_WAIT_ACK_1;
    SCSIBus_ControlWrite(t->bus, t->dev.refid, S_REQ, S_REQ);
    SCSITarget_Step(t, false);
}


static void SCSITarget_SendByte(SCSITarget *t, uint8_t val)
{
    SCSIBus_ControlWait(t->bus, t->dev.refid, S_ACK, S_ACK);
    t->scsi_state = (t->scsi_state & SCSI_STATE_MASK) | TS_SEND_BYTE_T_WAIT_ACK_1;
    SCSIBus_DataWrite(t->bus, t->dev.refid, val);
    SCSIBus_ControlWrite(t->bus, t->dev.refid, S_REQ, S_REQ);
    SCSITarget_Step(t, false);
}


static void SCSITarget_SendBufferByte(SCSITarget *t)
{
    SCSITarget_SendByte(t, SCSITarget_GetData(t, t->data_buffer_id, t->data_buffer_pos++));
}


/*
 * CDB length by command group (top 3 bits of the opcode).
 * Groups 0/3/6/7 = 6 bytes, 1/2 = 10, 4 = 16, 5 = 12.
 */
static bool SCSITarget_CommandDone(SCSITarget *t, uint8_t command, int length)
{
    (void)t;
    if (length == 0)
    {
        return false;
    }

    uint8_t commandGroup = command >> 5;
    switch (commandGroup)
    {
    case 0:
        return length == 6;
    case 1:
        return length == 10;
    case 2:
        return length == 10;
    case 3:
        return length == 6;
    case 4:
        return length == 16;
    case 5:
        return length == 12;
    case 6:
        return length == 6;
    case 7:
        return length == 6;
    default:
        break;
    }
    return true;
}


/* An IDENTIFY message has bit 7 set; anything else is logged and ignored. */
static void SCSITarget_Message(SCSITarget *t)
{
    if (t->scsi_cmdbuf[0] & 0x80)
    {
        t->scsi_identify = t->scsi_cmdbuf[0];
        return;
    }

    SCSITarget_Log(t, "Unknown message 0x%02X", t->scsi_cmdbuf[0]);
}


/* ------------------------------------------------------------------ */
/* The state machine                                                   */
/* ------------------------------------------------------------------ */
static void SCSITarget_Step(SCSITarget *t, bool timeout)
{
    if (!t->bus)
    {
        return;
    }

    uint32_t ctrl = SCSIBus_ControlRead(t->bus);
    uint8_t data = SCSIBus_DataRead(t->bus);

    (void)timeout;

    if (ctrl & S_RST)
    {
        SCSITarget_Log(t, "scsi bus reset");
        SCSITarget_DeviceReset(t);
        return;
    }

    uint16_t state = t->scsi_state & SCSI_SUB_MASK;
    if (state == 0)
    {
        state = t->scsi_state & SCSI_STATE_MASK;
    }

    switch (state)
    {
    case TS_IDLE:
        /* Selected? SEL asserted, BSY clear, and our ID bit on the data bus. */
        if (((ctrl & (S_SEL | S_BSY)) == S_SEL) && (t->dev.scsi_id != -1) &&
            ((data & (1 << t->dev.scsi_id)) != 0))
        {
            t->scsi_state = TS_TARGET_SELECT_WAIT_BUS_SETTLE;
            /* scsi_bus_settle_delay() is 0 under NO_SCSI_DELAY. */
            SCSITarget_AdjustTimer(t, 0, true);
        }
        break;

    case TS_TARGET_SELECT_WAIT_BUS_SETTLE:
        if ((ctrl & (S_SEL | S_BSY)) == S_SEL)
        {
            t->scsi_state = TS_TARGET_SELECT_WAIT_SEL_0;
            SCSIBus_ControlWrite(t->bus, t->dev.refid, S_BSY, S_BSY);
        }
        else
        {
            t->scsi_state = TS_IDLE;
        }
        break;

    case TS_TARGET_SELECT_WAIT_SEL_0:
        if (ctrl & S_SEL)
        {
            break;
        }
        {
            SCSIBufControl *c = SCSITarget_BufControlPush(t);
            c->action = BC_MSG_OR_COMMAND;
        }
        t->scsi_state = TS_TARGET_NEXT_CONTROL;
        SCSITarget_Step(t, false);
        break;

    case TS_RECV_BYTE_T_WAIT_ACK_1:
        if (ctrl & S_ACK)
        {
            SCSITarget_PutData(t, t->data_buffer_id, t->data_buffer_pos++,
                               SCSIBus_DataRead(t->bus));
            t->scsi_state = (t->scsi_state & SCSI_STATE_MASK) | TS_RECV_BYTE_T_WAIT_ACK_0;
            SCSIBus_ControlWrite(t->bus, t->dev.refid, 0, S_REQ);
        }
        break;

    case TS_RECV_BYTE_T_WAIT_ACK_0:
        if (!(ctrl & S_ACK))
        {
            t->scsi_state &= SCSI_STATE_MASK;
            SCSIBus_ControlWait(t->bus, t->dev.refid, 0, S_ACK);
            /* scsi_data_byte_period() is 0 -> step immediately. */
            SCSITarget_Step(t, false);
        }
        break;

    case TS_SEND_BYTE_T_WAIT_ACK_1:
        if (ctrl & S_ACK)
        {
            t->scsi_state = (t->scsi_state & SCSI_STATE_MASK) | TS_SEND_BYTE_T_WAIT_ACK_0;
            SCSIBus_DataWrite(t->bus, t->dev.refid, 0);
            SCSIBus_ControlWrite(t->bus, t->dev.refid, 0, S_REQ);
        }
        break;

    case TS_SEND_BYTE_T_WAIT_ACK_0:
        if (!(ctrl & S_ACK))
        {
            t->scsi_state &= SCSI_STATE_MASK;
            SCSIBus_ControlWait(t->bus, t->dev.refid, 0, S_ACK);
            SCSITarget_Step(t, false);
        }
        break;

    case TS_TARGET_NEXT_CONTROL:
    {
        SCSIBufControl *ctl = SCSITarget_BufControlPop(t);

        switch (ctl->action)
        {
        case BC_MSG_OR_COMMAND:
            t->data_buffer_id = SBUF_MAIN;
            t->data_buffer_pos = 0;
            if (ctrl & S_ATN)
            {
                t->scsi_state = TS_TARGET_WAIT_MSG_BYTE;
                SCSIBus_ControlWrite(t->bus, t->dev.refid, S_PHASE_MSG_OUT, S_PHASE_MASK);
            }
            else
            {
                t->scsi_state = TS_TARGET_WAIT_CMD_BYTE;
                SCSIBus_ControlWrite(t->bus, t->dev.refid, S_PHASE_COMMAND, S_PHASE_MASK);
            }
            SCSITarget_RecvByte(t);
            break;

        case BC_STATUS:
            SCSIBus_ControlWrite(t->bus, t->dev.refid, S_PHASE_STATUS, S_PHASE_MASK);
            SCSITarget_SendByte(t, (uint8_t)ctl->param1);
            break;

        case BC_DATA_IN:
            SCSIBus_ControlWrite(t->bus, t->dev.refid, S_PHASE_DATA_IN, S_PHASE_MASK);
            t->data_buffer_id = (SBUF)ctl->param1;
            t->data_buffer_size = ctl->param2;
            t->data_buffer_pos = 0;
            t->scsi_state =
                (t->data_buffer_size > 0) ? TS_TARGET_WAIT_DATA_IN_BYTE : TS_TARGET_NEXT_CONTROL;
            SCSITarget_Step(t, false);
            break;

        case BC_DATA_OUT:
            SCSIBus_ControlWrite(t->bus, t->dev.refid, S_PHASE_DATA_OUT, S_PHASE_MASK);
            t->data_buffer_id = (SBUF)ctl->param1;
            t->data_buffer_size = ctl->param2;
            t->data_buffer_pos = 0;
            t->scsi_state =
                (t->data_buffer_size > 0) ? TS_TARGET_WAIT_DATA_OUT_BYTE : TS_TARGET_NEXT_CONTROL;
            SCSITarget_Step(t, false);
            break;

        case BC_MESSAGE_1:
            SCSIBus_ControlWrite(t->bus, t->dev.refid, S_PHASE_MSG_IN, S_PHASE_MASK);
            SCSITarget_SendByte(t, (uint8_t)ctl->param1);
            break;

        case BC_BUS_FREE:
            SCSIBus_DataWrite(t->bus, t->dev.refid, 0);
            SCSIBus_ControlWait(t->bus, t->dev.refid, S_BSY | S_SEL | S_RST, S_ALL);
            SCSIBus_ControlWrite(t->bus, t->dev.refid, 0, S_ALL);
            t->scsi_state = TS_IDLE;
            break;

        default:
            break;
        }
        break;
    }

    case TS_TARGET_WAIT_DATA_IN_BYTE:
        if (t->data_buffer_pos == t->data_buffer_size - 1)
        {
            t->scsi_state = TS_TARGET_NEXT_CONTROL;
        }
        SCSITarget_SendBufferByte(t);
        break;

    case TS_TARGET_WAIT_DATA_OUT_BYTE:
        if (t->data_buffer_pos == t->data_buffer_size - 1)
        {
            t->scsi_state = TS_TARGET_NEXT_CONTROL;
        }
        SCSITarget_RecvByte(t);
        break;

    case TS_TARGET_WAIT_MSG_BYTE:
        if (ctrl & S_SEL)
        {
            return;
        }
        if (!(ctrl & S_ATN))
        {
            t->scsi_cmdsize = t->data_buffer_pos;
            SCSITarget_Message(t);
            t->data_buffer_id = SBUF_MAIN;
            t->data_buffer_pos = 0;
            t->scsi_state = TS_TARGET_WAIT_CMD_BYTE;
            SCSIBus_ControlWrite(t->bus, t->dev.refid, S_PHASE_COMMAND, S_PHASE_MASK);
        }
        SCSITarget_RecvByte(t);
        break;

    case TS_TARGET_WAIT_CMD_BYTE:
        if (ctrl & S_SEL)
        {
            return;
        }
        if (ctrl & S_ATN)
        {
            SCSITarget_Log(t, "Parity error? Say what?");
            t->scsi_state = TS_IDLE;
            break;
        }
        if (SCSITarget_CommandDone(t, t->scsi_cmdbuf[0], t->data_buffer_pos))
        {
            t->scsi_cmdsize = t->data_buffer_pos;
            SCSIBus_ControlWait(t->bus, t->dev.refid, 0, S_ACK);
            if (t->scsi_command)
            {
                t->scsi_command(t);
            }
            t->scsi_state = TS_TARGET_NEXT_CONTROL;
            /* scsi_data_command_delay() is 0 -> step immediately. */
            SCSITarget_Step(t, false);
        }
        else
        {
            SCSITarget_RecvByte(t);
        }
        break;

    default:
        SCSITarget_Log(t, "step() unexpected state 0x%04X", t->scsi_state);
        break;
    }
}


/* ------------------------------------------------------------------ */
/* SCSIDevice vtable hooks                                             */
/* ------------------------------------------------------------------ */
static void SCSITarget_Clock(SCSIDevice *self)
{
    SCSITarget *t = (SCSITarget *)self->impl;
    if (!t || !t->timerEnabled)
    {
        return;
    }

    /* NOTE: post-decrement then compare, matching SCSIFullDevice.Clock():
     *   if (regs.timerTicks-- <= 0) { ... step(regs.timerParam); } */
    if (t->timerTicks-- <= 0)
    {
        t->timerEnabled = false;
        SCSITarget_Step(t, t->timerParam);
    }
}


static void SCSITarget_CtrlChanged(SCSIDevice *self)
{
    SCSITarget *t = (SCSITarget *)self->impl;
    if (t)
    {
        SCSITarget_Step(t, false);
    }
}


void SCSITarget_DeviceReset(SCSITarget *t)
{
    if (!t)
    {
        return;
    }

    t->scsi_state = TS_IDLE;
    t->buf_control_rpos = t->buf_control_wpos = 0;
    t->scsi_identify = 0;
    t->data_buffer_size = 0;
    t->data_buffer_pos = 0;
    t->timerEnabled = false;
    t->timerTicks = 0;

    if (!t->bus)
    {
        return;
    }

    SCSIBus_DataWrite(t->bus, t->dev.refid, 0);
    SCSIBus_ControlWrite(t->bus, t->dev.refid, 0, S_ALL);
    SCSIBus_ControlWait(t->bus, t->dev.refid, S_SEL | S_BSY | S_RST, S_ALL);
    SCSITarget_Sense(t, false, SK_NO_SENSE, 0, 0);
}


void SCSITarget_Init(SCSITarget *t, SCSIBus *bus, uint8_t scsi_id, const char *name)
{
    /* NOTE: does not memset - the caller owns the struct and may already have
     * filled in the concrete-target hooks. Only the base fields are set here. */
    t->dev.name = name;
    t->dev.scsi_id = scsi_id;
    t->dev.Clock = SCSITarget_Clock;
    t->dev.ctrl_changed = SCSITarget_CtrlChanged;
    t->dev.impl = t;
    t->dev.refid = -1;

    SCSIBus_AddDevice(bus, &t->dev);
    t->bus = bus;

    SCSITarget_DeviceReset(t);
}
