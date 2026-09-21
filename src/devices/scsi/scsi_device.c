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

static void scsi_target_report_condition(SCSITarget *t, uint8_t sense_key, uint16_t sense_key_code,
                                         const SCSISenseData *data);

static void scsi_target_step(SCSITarget *t, bool timeout);


/* ------------------------------------------------------------------ */
/* Big-endian accessors (SCSISupport.cs Buffer)                        */
/* ------------------------------------------------------------------ */
void scsi_device_scsi_put_u16_be(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)(value >> 8);
    buf[1] = (uint8_t)(value);
}

void scsi_device_scsi_put_u24_be(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)(value >> 16);
    buf[1] = (uint8_t)(value >> 8);
    buf[2] = (uint8_t)(value);
}

void scsi_device_scsi_put_u32_be(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)(value >> 24);
    buf[1] = (uint8_t)(value >> 16);
    buf[2] = (uint8_t)(value >> 8);
    buf[3] = (uint8_t)(value);
}

uint16_t scsi_device_scsi_get_u16_be(const uint8_t *buf)
{
    return (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
}

uint32_t scsi_device_scsi_get_u24_be(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
}

uint32_t scsi_device_scsi_get_u32_be(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | buf[3];
}


static void scsi_target_log(SCSITarget *t, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void scsi_target_log(SCSITarget *t, const char *fmt, ...)
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
    log_write(LOG_CAT_SCSI, LOG_DEBUG, "TGT%d: %s", t->dev.scsi_id, msg);
}


static void scsi_target_adjust_timer(SCSITarget *t, int ticks, bool param)
{
    t->timerEnabled = true;
    t->timerTicks = ticks;
    t->timerParam = param;
}


/* ------------------------------------------------------------------ */
/* buf_control FIFO                                                    */
/* ------------------------------------------------------------------ */
static SCSIBufControl *scsi_target_buf_control_push(SCSITarget *t)
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


static SCSIBufControl *scsi_target_buf_control_pop(SCSITarget *t)
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
uint8_t scsi_device_default_get_data(SCSITarget *t, SBUF id, int pos)
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
        scsi_target_log(t, "FATALERROR: scsi_get_data - unknown id %d", id);
        return 0;
    }
}


void scsi_device_default_put_data(SCSITarget *t, SBUF id, int pos, uint8_t data)
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
        scsi_target_log(t, "FATALERROR: scsi_put_data - unknown id %d", id);
        break;
    }
}


static uint8_t scsi_target_get_data(SCSITarget *t, SBUF id, int pos)
{
    if (t->scsi_get_data)
    {
        return t->scsi_get_data(t, id, pos);
    }
    return scsi_device_default_get_data(t, id, pos);
}


static void scsi_target_put_data(SCSITarget *t, SBUF id, int pos, uint8_t data)
{
    if (t->scsi_put_data)
    {
        t->scsi_put_data(t, id, pos, data);
    }
    else
    {
        scsi_device_default_put_data(t, id, pos, data);
    }
}


/* ------------------------------------------------------------------ */
/* Phase queueing (called from a concrete target's scsi_command)       */
/* ------------------------------------------------------------------ */
void scsi_device_data_in(SCSITarget *t, SBUF buf, int size)
{
    SCSIBufControl *c = scsi_target_buf_control_push(t);
    c->action = BC_DATA_IN;
    c->param1 = (int)buf;
    c->param2 = size;
    scsi_target_log(t, "scsi_data_in: queued size=%d buf=%d wpos=%d", size, buf,
                    t->buf_control_wpos);
}


void scsi_device_data_out(SCSITarget *t, SBUF buf, int size)
{
    SCSIBufControl *c = scsi_target_buf_control_push(t);
    c->action = BC_DATA_OUT;
    c->param1 = (int)buf;
    c->param2 = size;
}


/* Queue STATUS + COMMAND COMPLETE message + BUS FREE. */
void scsi_device_status_complete(SCSITarget *t, uint8_t status)
{
    scsi_target_log(t, "scsi_status_complete: status=0x%02X", status);

    SCSIBufControl *c = scsi_target_buf_control_push(t);
    c->action = BC_STATUS;
    c->param1 = status;

    c = scsi_target_buf_control_push(t);
    c->action = BC_MESSAGE_1;
    c->param1 = SM_COMMAND_COMPLETE;

    c = scsi_target_buf_control_push(t);
    c->action = BC_BUS_FREE;
}


/* ------------------------------------------------------------------ */
/* Sense                                                               */
/* ------------------------------------------------------------------ */
static void scsi_target_set_sense_data(SCSITarget *t, uint8_t sense_key, uint16_t sense_key_code,
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
        scsi_device_scsi_put_u32_be(&t->scsi_sense_buffer[3], (uint32_t)data->info);
    }
    else
    {
        t->scsi_sense_buffer[0] = 0xf0;
    }

    t->scsi_sense_buffer[2] |= (uint8_t)(sense_key & 0x0f);
    t->scsi_sense_buffer[7] = 10; /* additional sense length */
    scsi_device_scsi_put_u16_be(&t->scsi_sense_buffer[12], sense_key_code); /* ASC / ASCQ */
}


void scsi_device_sense(SCSITarget *t, bool deferred, uint8_t key, int asc, int ascq)
{
    SCSISenseData s;
    memset(&s, 0, sizeof(s));
    s.deferred = deferred;

    uint16_t code = (uint16_t)((asc << 8) | ascq);
    scsi_target_set_sense_data(t, key, code, &s);
}


static void scsi_target_report_condition(SCSITarget *t, uint8_t sense_key, uint16_t sense_key_code,
                                         const SCSISenseData *data)
{
    scsi_target_set_sense_data(t, sense_key, sense_key_code, data);
    scsi_device_status_complete(t, SS_CHECK_CONDITION);
}


void scsi_device_report_bad_cmd(SCSITarget *t, uint8_t cmd)
{
    scsi_target_log(t, "cmd 0x%02X    *** BAD COMMAND", cmd);
    scsi_target_report_condition(t, SK_ILLEGAL_REQUEST, SKC_INVALID_COMMAND_OPERATION_CODE, NULL);
}


void scsi_device_report_bad_lun(SCSITarget *t, uint8_t cmd, uint8_t lun)
{
    scsi_target_log(t, "cmd 0x%02X lun=%d    *** BAD LUN", cmd, lun);
    scsi_target_report_condition(t, SK_ILLEGAL_REQUEST, SKC_LOGICAL_UNIT_NOT_SUPPORTED, NULL);
}


/* ------------------------------------------------------------------ */
/* Byte transfer helpers                                               */
/* ------------------------------------------------------------------ */
static void scsi_target_recv_byte(SCSITarget *t)
{
    scsi_bus_control_wait(t->bus, t->dev.refid, S_ACK, S_ACK);
    t->scsi_state = (t->scsi_state & SCSI_STATE_MASK) | TS_RECV_BYTE_T_WAIT_ACK_1;
    scsi_bus_control_write(t->bus, t->dev.refid, S_REQ, S_REQ);
    scsi_target_step(t, false);
}


static void scsi_target_send_byte(SCSITarget *t, uint8_t val)
{
    scsi_bus_control_wait(t->bus, t->dev.refid, S_ACK, S_ACK);
    t->scsi_state = (t->scsi_state & SCSI_STATE_MASK) | TS_SEND_BYTE_T_WAIT_ACK_1;
    scsi_bus_data_write(t->bus, t->dev.refid, val);
    scsi_bus_control_write(t->bus, t->dev.refid, S_REQ, S_REQ);
    scsi_target_step(t, false);
}


static void scsi_target_send_buffer_byte(SCSITarget *t)
{
    scsi_target_send_byte(t, scsi_target_get_data(t, t->data_buffer_id, t->data_buffer_pos++));
}


/*
 * CDB length by command group (top 3 bits of the opcode).
 * Groups 0/3/6/7 = 6 bytes, 1/2 = 10, 4 = 16, 5 = 12.
 */
static bool scsi_target_command_done(SCSITarget *t, uint8_t command, int length)
{
    (void)t;
    if (length == 0)
    {
        return false;
    }

    uint8_t command_group = command >> 5;
    switch (command_group)
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
static void scsi_target_message(SCSITarget *t)
{
    if (t->scsi_cmdbuf[0] & 0x80)
    {
        t->scsi_identify = t->scsi_cmdbuf[0];
        return;
    }

    scsi_target_log(t, "Unknown message 0x%02X", t->scsi_cmdbuf[0]);
}


/* ------------------------------------------------------------------ */
/* The state machine                                                   */
/* ------------------------------------------------------------------ */
static void scsi_target_step(SCSITarget *t, bool timeout)
{
    if (!t->bus)
    {
        return;
    }

    uint32_t ctrl = scsi_bus_control_read(t->bus);
    uint8_t data = scsi_bus_data_read(t->bus);

    (void)timeout;

    if (ctrl & S_RST)
    {
        scsi_target_log(t, "scsi bus reset");
        scsi_device_device_reset(t);
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
            scsi_target_adjust_timer(t, 0, true);
        }
        break;

    case TS_TARGET_SELECT_WAIT_BUS_SETTLE:
        if ((ctrl & (S_SEL | S_BSY)) == S_SEL)
        {
            t->scsi_state = TS_TARGET_SELECT_WAIT_SEL_0;
            scsi_bus_control_write(t->bus, t->dev.refid, S_BSY, S_BSY);
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
            SCSIBufControl *c = scsi_target_buf_control_push(t);
            c->action = BC_MSG_OR_COMMAND;
        }
        t->scsi_state = TS_TARGET_NEXT_CONTROL;
        scsi_target_step(t, false);
        break;

    case TS_RECV_BYTE_T_WAIT_ACK_1:
        if (ctrl & S_ACK)
        {
            scsi_target_put_data(t, t->data_buffer_id, t->data_buffer_pos++,
                                 scsi_bus_data_read(t->bus));
            t->scsi_state = (t->scsi_state & SCSI_STATE_MASK) | TS_RECV_BYTE_T_WAIT_ACK_0;
            scsi_bus_control_write(t->bus, t->dev.refid, 0, S_REQ);
        }
        break;

    case TS_RECV_BYTE_T_WAIT_ACK_0:
        if (!(ctrl & S_ACK))
        {
            t->scsi_state &= SCSI_STATE_MASK;
            scsi_bus_control_wait(t->bus, t->dev.refid, 0, S_ACK);
            /* scsi_data_byte_period() is 0 -> step immediately. */
            scsi_target_step(t, false);
        }
        break;

    case TS_SEND_BYTE_T_WAIT_ACK_1:
        if (ctrl & S_ACK)
        {
            t->scsi_state = (t->scsi_state & SCSI_STATE_MASK) | TS_SEND_BYTE_T_WAIT_ACK_0;
            scsi_bus_data_write(t->bus, t->dev.refid, 0);
            scsi_bus_control_write(t->bus, t->dev.refid, 0, S_REQ);
        }
        break;

    case TS_SEND_BYTE_T_WAIT_ACK_0:
        if (!(ctrl & S_ACK))
        {
            t->scsi_state &= SCSI_STATE_MASK;
            scsi_bus_control_wait(t->bus, t->dev.refid, 0, S_ACK);
            scsi_target_step(t, false);
        }
        break;

    case TS_TARGET_NEXT_CONTROL:
    {
        SCSIBufControl *ctl = scsi_target_buf_control_pop(t);

        switch (ctl->action)
        {
        case BC_MSG_OR_COMMAND:
            t->data_buffer_id = SBUF_MAIN;
            t->data_buffer_pos = 0;
            if (ctrl & S_ATN)
            {
                t->scsi_state = TS_TARGET_WAIT_MSG_BYTE;
                scsi_bus_control_write(t->bus, t->dev.refid, S_PHASE_MSG_OUT, S_PHASE_MASK);
            }
            else
            {
                t->scsi_state = TS_TARGET_WAIT_CMD_BYTE;
                scsi_bus_control_write(t->bus, t->dev.refid, S_PHASE_COMMAND, S_PHASE_MASK);
            }
            scsi_target_recv_byte(t);
            break;

        case BC_STATUS:
            scsi_bus_control_write(t->bus, t->dev.refid, S_PHASE_STATUS, S_PHASE_MASK);
            scsi_target_send_byte(t, (uint8_t)ctl->param1);
            break;

        case BC_DATA_IN:
            scsi_bus_control_write(t->bus, t->dev.refid, S_PHASE_DATA_IN, S_PHASE_MASK);
            t->data_buffer_id = (SBUF)ctl->param1;
            t->data_buffer_size = ctl->param2;
            t->data_buffer_pos = 0;
            t->scsi_state =
                (t->data_buffer_size > 0) ? TS_TARGET_WAIT_DATA_IN_BYTE : TS_TARGET_NEXT_CONTROL;
            scsi_target_step(t, false);
            break;

        case BC_DATA_OUT:
            scsi_bus_control_write(t->bus, t->dev.refid, S_PHASE_DATA_OUT, S_PHASE_MASK);
            t->data_buffer_id = (SBUF)ctl->param1;
            t->data_buffer_size = ctl->param2;
            t->data_buffer_pos = 0;
            t->scsi_state =
                (t->data_buffer_size > 0) ? TS_TARGET_WAIT_DATA_OUT_BYTE : TS_TARGET_NEXT_CONTROL;
            scsi_target_step(t, false);
            break;

        case BC_MESSAGE_1:
            scsi_bus_control_write(t->bus, t->dev.refid, S_PHASE_MSG_IN, S_PHASE_MASK);
            scsi_target_send_byte(t, (uint8_t)ctl->param1);
            break;

        case BC_BUS_FREE:
            scsi_bus_data_write(t->bus, t->dev.refid, 0);
            scsi_bus_control_wait(t->bus, t->dev.refid, S_BSY | S_SEL | S_RST, S_ALL);
            scsi_bus_control_write(t->bus, t->dev.refid, 0, S_ALL);
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
        scsi_target_send_buffer_byte(t);
        break;

    case TS_TARGET_WAIT_DATA_OUT_BYTE:
        if (t->data_buffer_pos == t->data_buffer_size - 1)
        {
            t->scsi_state = TS_TARGET_NEXT_CONTROL;
        }
        scsi_target_recv_byte(t);
        break;

    case TS_TARGET_WAIT_MSG_BYTE:
        if (ctrl & S_SEL)
        {
            return;
        }
        if (!(ctrl & S_ATN))
        {
            t->scsi_cmdsize = t->data_buffer_pos;
            scsi_target_message(t);
            t->data_buffer_id = SBUF_MAIN;
            t->data_buffer_pos = 0;
            t->scsi_state = TS_TARGET_WAIT_CMD_BYTE;
            scsi_bus_control_write(t->bus, t->dev.refid, S_PHASE_COMMAND, S_PHASE_MASK);
        }
        scsi_target_recv_byte(t);
        break;

    case TS_TARGET_WAIT_CMD_BYTE:
        if (ctrl & S_SEL)
        {
            return;
        }
        if (ctrl & S_ATN)
        {
            scsi_target_log(t, "Parity error? Say what?");
            t->scsi_state = TS_IDLE;
            break;
        }
        if (scsi_target_command_done(t, t->scsi_cmdbuf[0], t->data_buffer_pos))
        {
            t->scsi_cmdsize = t->data_buffer_pos;
            scsi_bus_control_wait(t->bus, t->dev.refid, 0, S_ACK);
            if (t->scsi_command)
            {
                t->scsi_command(t);
            }
            t->scsi_state = TS_TARGET_NEXT_CONTROL;
            /* scsi_data_command_delay() is 0 -> step immediately. */
            scsi_target_step(t, false);
        }
        else
        {
            scsi_target_recv_byte(t);
        }
        break;

    default:
        scsi_target_log(t, "step() unexpected state 0x%04X", t->scsi_state);
        break;
    }
}


/* ------------------------------------------------------------------ */
/* SCSIDevice vtable hooks                                             */
/* ------------------------------------------------------------------ */
static void scsi_target_clock(SCSIDevice *self)
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
        scsi_target_step(t, t->timerParam);
    }
}


static void scsi_target_ctrl_changed(SCSIDevice *self)
{
    SCSITarget *t = (SCSITarget *)self->impl;
    if (t)
    {
        scsi_target_step(t, false);
    }
}


void scsi_device_device_reset(SCSITarget *t)
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

    scsi_bus_data_write(t->bus, t->dev.refid, 0);
    scsi_bus_control_write(t->bus, t->dev.refid, 0, S_ALL);
    scsi_bus_control_wait(t->bus, t->dev.refid, S_SEL | S_BSY | S_RST, S_ALL);
    scsi_device_sense(t, false, SK_NO_SENSE, 0, 0);
}


void scsi_device_init(SCSITarget *t, SCSIBus *bus, uint8_t scsi_id, const char *name)
{
    /* NOTE: does not memset - the caller owns the struct and may already have
     * filled in the concrete-target hooks. Only the base fields are set here. */
    t->dev.name = name;
    t->dev.scsi_id = scsi_id;
    t->dev.Clock = scsi_target_clock;
    t->dev.ctrl_changed = scsi_target_ctrl_changed;
    t->dev.impl = t;
    t->dev.refid = -1;

    scsi_bus_add_device(bus, &t->dev);
    t->bus = bus;

    scsi_device_device_reset(t);
}
