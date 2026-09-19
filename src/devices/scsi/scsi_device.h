/*
 * nd100x - ND-100 emulator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * scsi_device.h - SCSI target-side phase state machine
 *
 * This is the target half of the bus: it answers the initiator's SELECT, walks
 * the COMMAND -> DATA -> STATUS -> MESSAGE IN -> BUS FREE phase sequence, and
 * hands the assembled CDB to a concrete target (scsi_hdd.c) for decode.
 *
 * Ported from RetroCore:
 *   <RetroCore>\Emulated.HW\Common\SCSI\SCSIFullDevice.cs
 *   <RetroCore>\Emulated.HW\Common\SCSI\SCSISupport.cs
 * which are ports of MAME's nscsi_full_device.
 *
 * TIMING: as with the NCR chip, RetroCore builds with NO_SCSI_DELAY, so every
 * delay hook here (scsi_bus_settle_delay / scsi_data_byte_period /
 * scsi_data_command_delay) evaluates to 0 and each "if (delay == 0) step()"
 * takes the immediate path. No timing model is ported.
 *
 * The C# class hierarchy
 *   SCSIHDDMicropolis : SCSIHDD : SCSIFullDevice : SCSIDevice
 * becomes a vtable: SCSITarget embeds SCSIDevice and holds function pointers
 * that the concrete target (scsi_hdd.c) fills in.
 */

#ifndef SCSI_DEVICE_H
#define SCSI_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "scsi_bus.h"

/* Data buffer ids (SCSIEnums.cs SBUF) */
// clang-format off
typedef enum {
    SBUF_MAIN  = 0,   /* scsi_cmdbuf - CDB and data staging */
    SBUF_SENSE = 1,   /* scsi_sense_buffer */
    SBUF_DATA  = 2
} SBUF;
// clang-format on

/* Queued phase actions (SCSIFullDevice.cs BC) */
typedef enum
{
    BC_MSG_OR_COMMAND = 0,
    BC_STATUS,
    BC_MESSAGE_1,
    BC_MESSAGE_2,
    BC_DATA_IN,
    BC_DATA_OUT,
    BC_BUS_FREE
} SCSIBufControlAction;

/* SCSI status codes (SCSIEnums.cs SCSIStatus) */
#define SS_GOOD              0x00
#define SS_CHECK_CONDITION   0x02
#define SS_CONDITION_MET     0x04
#define SS_BUSY              0x08
#define SS_INT_GOOD          0x10
#define SS_INT_CONDITION_MET 0x14
#define SS_RESV_CONFLICT     0x18
#define SS_TERMINATED        0x22
#define SS_QUEUE_FULL        0x28

/* SCSI messages (SCSIEnums.cs SCSIMessages) */
#define SM_COMMAND_COMPLETE 0x00

/* Sense keys (SCSIEnums.cs SCSI_SK) */
// clang-format off
#define SK_NO_SENSE          0x00
#define SK_RECOVERED_ERROR   0x01
#define SK_NOT_READY         0x02
#define SK_MEDIUM_ERROR      0x03
#define SK_HARDWARE_ERROR    0x04
#define SK_ILLEGAL_REQUEST   0x05
#define SK_UNIT_ATTENTION    0x06
#define SK_DATA_PROTECT      0x07
#define SK_BLANK_CHECK       0x08
#define SK_VENDOR_SPECIFIC   0x09
#define SK_COPY_ABORTED      0x0a
#define SK_ABORTED_COMMAND   0x0b
#define SK_EQUAL             0x0c
// clang-format on

/* Additional sense codes, encoded (ASC << 8) | ASCQ */
#define SKC_LOGICAL_UNIT_NOT_SUPPORTED     0x2500
#define SKC_INVALID_COMMAND_OPERATION_CODE 0x2000
#define SKC_INVALID_FIELD_IN_CDB           0x2400
#define SKC_POWER_ON_OR_RESET              0x2900
#define SKC_MEDIUM_NOT_PRESENT             0x3a00
#define SKC_LBA_OUT_OF_RANGE               0x2100

/* Target state machine (SCSIFullDevice.cs STATE).
 * The low byte is the main state, the high byte the byte-transfer substate. */
#define SCSI_STATE_MASK 0x00ff
#define SCSI_SUB_MASK   0xff00
#define SCSI_SUB_SHIFT  8

// clang-format off
typedef enum {
    TS_IDLE                          = 0,
    TS_TARGET_SELECT_WAIT_BUS_SETTLE = 1,
    TS_TARGET_SELECT_WAIT_SEL_0      = 2,
    TS_TARGET_NEXT_CONTROL           = 3,
    TS_TARGET_WAIT_MSG_BYTE          = 4,
    TS_TARGET_WAIT_CMD_BYTE          = 5,
    TS_TARGET_WAIT_DATA_IN_BYTE      = 6,
    TS_TARGET_WAIT_DATA_OUT_BYTE     = 7,

    TS_RECV_BYTE_T_WAIT_ACK_0        = 1 << SCSI_SUB_SHIFT,
    TS_RECV_BYTE_T_WAIT_ACK_1        = 2 << SCSI_SUB_SHIFT,
    TS_SEND_BYTE_T_WAIT_ACK_0        = 3 << SCSI_SUB_SHIFT,
    TS_SEND_BYTE_T_WAIT_ACK_1        = 4 << SCSI_SUB_SHIFT
} SCSITargetState;
// clang-format on

#define SCSI_CMDBUF_SIZE       4096
#define SCSI_SENSE_BUFFER_SIZE 18
#define SCSI_BUF_CONTROL_SIZE  32

typedef struct
{
    SCSIBufControlAction action;
    int param1;
    int param2;
} SCSIBufControl;

/* Optional fields for set_sense_data (SCSISupport.cs sense_data). */
typedef struct
{
    bool invalid;
    bool deferred;
    bool filemark;
    bool eom;
    bool bad_len;
    int info;
} SCSISenseData;

struct SCSITarget;

// clang-format off
typedef struct SCSITarget {
    SCSIDevice dev;
    SCSIBus   *bus;

    uint16_t scsi_state;
    int      scsi_cmdsize;
    uint8_t  scsi_identify;
    int      scsi_initiator_id;

    SBUF     data_buffer_id;
    int      data_buffer_size;
    int      data_buffer_pos;

    SCSIBufControl buf_control[SCSI_BUF_CONTROL_SIZE];
    int      buf_control_rpos;
    int      buf_control_wpos;

    bool     timerEnabled;
    int      timerTicks;
    bool     timerParam;

    uint8_t  scsi_cmdbuf[SCSI_CMDBUF_SIZE];
    uint8_t  scsi_sense_buffer[SCSI_SENSE_BUFFER_SIZE];

    /* ---- concrete target hooks (filled in by scsi_hdd.c) ---- */
    /* Decode and act on the CDB now sitting in scsi_cmdbuf. */
    void    (*scsi_command)(struct SCSITarget *self);
    /* Fetch/store a byte of a DATA IN / DATA OUT phase. Both may be NULL, in
     * which case the default cmdbuf/sense-buffer behaviour is used. */
    uint8_t (*scsi_get_data)(struct SCSITarget *self, SBUF id, int pos);
    void    (*scsi_put_data)(struct SCSITarget *self, SBUF id, int pos, uint8_t data);

    void *impl;   /* concrete target state (SCSIHDDData) */
} SCSITarget;
// clang-format on

/* Wire a target onto the bus at the given SCSI id. */
void SCSITarget_Init(SCSITarget *t, SCSIBus *bus, uint8_t scsi_id, const char *name);
void SCSITarget_DeviceReset(SCSITarget *t);

/* Queue phase actions from inside a scsi_command() implementation. */
void SCSITarget_DataIn(SCSITarget *t, SBUF buf, int size);
void SCSITarget_DataOut(SCSITarget *t, SBUF buf, int size);
void SCSITarget_StatusComplete(SCSITarget *t, uint8_t status);

/* Sense handling. */
void SCSITarget_Sense(SCSITarget *t, bool deferred, uint8_t key, int asc, int ascq);
void SCSITarget_ReportCondition(SCSITarget *t, uint8_t sense_key, uint16_t sense_key_code,
                                const SCSISenseData *data);
void SCSITarget_ReportBadCmd(SCSITarget *t, uint8_t cmd);
void SCSITarget_ReportBadLun(SCSITarget *t, uint8_t cmd, uint8_t lun);

/* Default buffer accessors - concrete targets call these for the ids they do
 * not handle themselves. */
uint8_t SCSITarget_DefaultGetData(SCSITarget *t, SBUF id, int pos);
void SCSITarget_DefaultPutData(SCSITarget *t, SBUF id, int pos, uint8_t data);

/*
 * Big-endian accessors (SCSISupport.cs Buffer).
 *
 * EVERY multi-byte field in a SCSI CDB or data-in payload is big-endian (MSB
 * first) - READ CAPACITY, INQUIRY lengths, LBAs, sense ASC/ASCQ. These are the
 * only correct way to touch them; do not hand-roll shifts at the call sites.
 */
void scsi_put_u16be(uint8_t *buf, uint16_t value);
void scsi_put_u24be(uint8_t *buf, uint32_t value);
void scsi_put_u32be(uint8_t *buf, uint32_t value);
uint16_t scsi_get_u16be(const uint8_t *buf);
uint32_t scsi_get_u24be(const uint8_t *buf);
uint32_t scsi_get_u32be(const uint8_t *buf);

#endif // SCSI_DEVICE_H
