/*
 * nd100x - ND-100 emulator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * scsi_bus.h - SCSI bus and the device base "class" that sits on it
 *
 * Ported from RetroCore:
 *   <RetroCore>\Emulated.HW\Common\SCSI\SCSIBus.cs
 *   <RetroCore>\Emulated.HW\Common\SCSI\SCSIDevice.cs
 * which are ports of MAME's nscsi_bus.
 *
 * The bus wire-ORs the control lines and the data lines of every attached
 * device: a line is asserted on the bus if ANY device asserts it. When the
 * OR'ed control word changes, every device that registered interest in one of
 * the changed lines (via SCSIBus_ControlWait) gets an scsi_ctrl_changed()
 * callback - except the device that caused the change.
 *
 * C has no classes, so the C# "SCSIDevice" base becomes a small vtable struct
 * (SCSIDevice) with a void *impl pointing at the concrete device state.
 */

#ifndef SCSI_BUS_H
#define SCSI_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* SCSI control lines (SCSIEnums.cs SCSIControl)                       */
/*                                                                     */
/* NOTE: the low three bits are I/O, C/D and MSG, so                   */
/* (ctrl & S_PHASE_MASK) is numerically identical to the NCRPhase enum */
/* in ncr5386.h. No translation between the two is needed.             */
/* ------------------------------------------------------------------ */
// clang-format off
#define S_INP  0x0001   /* I/O  */
#define S_CTL  0x0002   /* C/D  */
#define S_MSG  0x0004   /* MSG  */
#define S_BSY  0x0008
#define S_SEL  0x0010
#define S_REQ  0x0020
#define S_ACK  0x0040
#define S_ATN  0x0080
#define S_RST  0x0100
#define S_ALL  0x01ff
// clang-format on

#define S_PHASE_DATA_OUT 0
#define S_PHASE_DATA_IN  (S_INP)
#define S_PHASE_COMMAND  (S_CTL)
#define S_PHASE_STATUS   (S_CTL | S_INP)
#define S_PHASE_MSG_OUT  (S_MSG | S_CTL)
#define S_PHASE_MSG_IN   (S_MSG | S_CTL | S_INP)
#define S_PHASE_MASK     (S_MSG | S_CTL | S_INP)

#define SCSI_BUS_MAX_DEVICES 16

struct SCSIBus;

/* Base "class" for anything on the bus (the NCR chip, and each target). */
// clang-format off
typedef struct SCSIDevice {
    struct SCSIBus *bus;
    int   refid;                /* index into the bus device table, -1 = detached */
    int   scsi_id;              /* SCSI ID 0-7 */
    const char *name;

    /* Called once per bus clock. May be NULL. */
    void (*Clock)(struct SCSIDevice *self);
    /* Called when a control line this device waits on changes. May be NULL. */
    void (*ctrl_changed)(struct SCSIDevice *self);

    void *impl;                 /* concrete device state (NCR5386 / target) */
} SCSIDevice;
// clang-format on

// clang-format off
typedef struct {
    SCSIDevice *dev;
    uint32_t    ctrl;           /* lines this device is asserting */
    uint32_t    wait_ctrl;      /* lines this device wants to be notified about */
    uint8_t     data;           /* data this device is driving */
} SCSIBusDevice;
// clang-format on

// clang-format off
typedef struct SCSIBus {
    SCSIBusDevice devices[SCSI_BUS_MAX_DEVICES];
    int      devCnt;
    uint8_t  data;              /* OR of all devices' data */
    uint32_t ctrl;              /* OR of all devices' control lines */
} SCSIBus;
// clang-format on

/**
 * @brief Reset a SCSI bus to its empty, all-lines-idle state.
 * @param bus Bus to reset; ignored if NULL.
 */
void scsi_bus_init(SCSIBus *bus);

/**
 * @brief Attach a device to the next free slot on a SCSI bus.
 * @param bus Bus to attach to.
 * @param dev Device to attach; its bus and refid fields are set on success.
 * @return The assigned refid (0-based slot index), or -1 if bus/dev is NULL
 *         or the bus already has SCSI_BUS_MAX_DEVICES devices.
 */
int scsi_bus_add_device(SCSIBus *bus, SCSIDevice *dev);

/**
 * @brief Clock every attached device's Clock() callback for one bus tick.
 * @param bus Bus whose devices are clocked; ignored if NULL.
 */
void scsi_bus_clock(SCSIBus *bus);

/**
 * @brief Read the current OR of all devices' control lines.
 * @param bus Bus to read; NULL returns 0.
 * @return Bitwise OR of every device's control line word.
 */
uint32_t scsi_bus_control_read(SCSIBus *bus);

/**
 * @brief Drive one device's control lines and notify devices waiting on the
 *        lines that changed.
 * @param bus Bus to update; ignored if NULL.
 * @param refid Slot index of the driving device, as returned by
 *        SCSIBus_AddDevice.
 * @param lines New line values, masked by mask.
 * @param mask Bits of lines that are being changed by this call.
 */
void scsi_bus_control_write(SCSIBus *bus, int refid, uint32_t lines, uint32_t mask);

/**
 * @brief Register which control lines a device wants ctrl_changed() callbacks
 *        for.
 * @param bus Bus to update; ignored if NULL.
 * @param refid Slot index of the device, as returned by SCSIBus_AddDevice.
 * @param lines New wait-mask bit values, masked by mask.
 * @param mask Bits of the wait mask being changed by this call.
 */
void scsi_bus_control_wait(SCSIBus *bus, int refid, uint32_t lines, uint32_t mask);

/**
 * @brief Read the current OR of all devices' data lines.
 * @param bus Bus to read; NULL returns 0.
 * @return Bitwise OR of every device's driven data byte.
 */
uint8_t scsi_bus_data_read(SCSIBus *bus);

/**
 * @brief Drive one device's data byte onto the bus and recompute the OR.
 * @param bus Bus to update; ignored if NULL.
 * @param refid Slot index of the driving device, as returned by
 *        SCSIBus_AddDevice.
 * @param data Data byte this device is driving.
 */
void scsi_bus_data_write(SCSIBus *bus, int refid, uint8_t data);

/**
 * @brief Name a SCSI bus phase for logging ("DATA OUT" / "COMMAND" / ...).
 * @param phase Phase value; only the bits under S_PHASE_MASK are used.
 * @return Static string naming the phase, or "*" for a reserved phase value.
 */
const char *scsi_bus_phase_name(uint32_t phase);

#endif // SCSI_BUS_H
