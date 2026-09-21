/*
 * chip_com5025_registers.h - COM5025 register file: register definitions and accessors.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * This file is originated from the nd100x project and the RetroCore project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the nd100em
 * distribution in the file COPYING); if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CHIP_COM5025_REGISTERS_H
#define CHIP_COM5025_REGISTERS_H

#include <stdint.h>
#include <stdbool.h>
#include "hdlc_constants.h"

// Timer flags
typedef enum
{
    COM5025_TIMER_TX = 1 << 0,
    COM5025_TIMER_RX = 1 << 1,
    COM5025_TIMER_BOTH = (COM5025_TIMER_TX | COM5025_TIMER_RX)
} COM5025TimerFlags;

// CRC Mode
typedef enum
{
    COM5025_CRC_MODE_CCITT_INIT_TO_1 = 0,
    COM5025_CRC_MODE_CCITT_INIT_TO_0 = 1,
    COM5025_CRC_MODE_NOT_USED_1 = 2,
    COM5025_CRC_MODE_CRC16 = 3,
    COM5025_CRC_MODE_ODD_PARITY = 4,
    COM5025_CRC_MODE_EVEN_PARITY = 5,
    COM5025_CRC_MODE_NOT_USED_2 = 6,
    COM5025_CRC_MODE_INHIBIT_ERROR_DETECTION = 7
} COM5025CrcMode;

// Transmitter State
typedef enum
{
    COM5025_TX_STATE_IDLE,
    COM5025_TX_STATE_SENDING_FLAGS,
    COM5025_TX_STATE_SENDING_DATA,
    COM5025_TX_STATE_SENDING_CRC,
    COM5025_TX_STATE_SENDING_CLOSING_FLAG,
    COM5025_TX_STATE_UNDERRUN
} COM5025TXState;

// Receiver State
typedef enum
{
    COM5025_RX_STATE_IDLE,
    COM5025_RX_STATE_HUNT_SYNC,
    COM5025_RX_STATE_RECEIVING_DATA,
    COM5025_RX_STATE_RECEIVING_CRC,
    COM5025_RX_STATE_MESSAGE_COMPLETE,
    COM5025_RX_STATE_ABORT_DETECTED
} COM5025RXState;

// IO Timer structure
typedef struct
{
    int ticks;
    int param;
    bool active;
    int clockSpeed;
    void (*callback)(void *context, int param);
    void *context;
} COM5025IOTimer;

// Receive queue node
typedef struct COM5025ReceiveQueueNode
{
    uint16_t data;
    struct COM5025ReceiveQueueNode *next;
} COM5025ReceiveQueueNode;

// Receive queue
typedef struct
{
    COM5025ReceiveQueueNode *head;
    COM5025ReceiveQueueNode *tail;
    int count;
} COM5025ReceiveQueue;

// Register structure
typedef struct
{
    // Status and control registers
    uint16_t receiverStatus;
    uint16_t txStatusAndControl;
    uint16_t modeControl;
    uint16_t dataLengthSelect;

    // Data length fields
    uint8_t txdl;
    uint8_t rxdl;

    // Data buffers
    uint16_t receiverDataBuffer;
    uint16_t dataFromReceiveQueue;
    uint8_t transmitterDataBuffer;

    // Transmitter shift register
    uint8_t transmitterShiftRegister;
    uint8_t transmitterShiftRegisterBit;
    bool tsrEnableBitStuffing;
    uint8_t tsrCountOnes;

    // SYNC/Secondary Address
    uint8_t syncSecondaryAddress;

    // State machines
    COM5025TXState transmitterState;
    COM5025RXState receiverState;

    // Protocol mode and CRC
    COM5025CrcMode crcMode;

    // Timers
    COM5025IOTimer txTimer;
    COM5025IOTimer rxTimer;

    // Receive queue
    COM5025ReceiveQueue receiveQueue;

    // Counters and flags
    int rorCount;
    bool byteStuffingDetected;

    // CRC calculations
    uint16_t rxCrc;
    uint16_t txCrc;
} COM5025Registers;

// Function declarations

/**
 * @brief Zero the register file, clear it to power-on defaults and init the
 *        TX/RX IO timers.
 * @param regs Register file to initialize.
 * @return void.
 */
void com5025_reg_init(COM5025Registers *regs);

/**
 * @brief Reset the register file fields (status/control registers, CRC
 *        state, receive queue flags) to their power-on values.
 * @param regs Register file to clear.
 * @return void.
 */
void com5025_reg_clear(COM5025Registers *regs);

/**
 * @brief Free every node still queued in the receive queue.
 * @param regs Register file to destroy the receive queue of.
 * @return void.
 */
void com5025_reg_destroy(COM5025Registers *regs);

// Register access

/**
 * @brief Overwrite the receiver status register with status.
 * @param regs Register file to update.
 * @param status New receiver status register value.
 * @return void.
 */
void com5025_reg_set_receiver_status(COM5025Registers *regs, uint16_t status);

/**
 * @brief Read the receiver data length select field (RXDL, bits 0-2 of the
 *        Data Length Select register).
 * @param regs Register file to read from.
 * @return 3-bit RXDL field, or 0 if regs is NULL.
 */
uint8_t com5025_reg_get_receiver_character_len(COM5025Registers *regs);

/**
 * @brief Read the transmitter data length select field (TXDL, bits 5-7 of
 *        the Data Length Select register).
 * @param regs Register file to read from.
 * @return 3-bit TXDL field, or 0 if regs is NULL.
 */
uint8_t com5025_reg_get_transmitter_character_len(COM5025Registers *regs);

/**
 * @brief Store a new Mode Control register value, reset both the receiver
 *        and transmitter state machines to idle, and derive crcMode from the
 *        X, Y, Z CRC SELECT bits.
 * @param regs Register file to update.
 * @param modeControl New Mode Control register value.
 * @return void.
 */
void com5025_reg_set_mode_control(COM5025Registers *regs, uint16_t mode_control);

/**
 * @brief Test the PROTO bit of the Mode Control register.
 * @param regs Register file to read from.
 * @return true if protocol mode is CCP (character-oriented), false for BOP
 *         or if regs is NULL.
 */
bool com5025_reg_is_protocol_mode_ccp(COM5025Registers *regs);

// Timer functions

/**
 * @brief Set the clock speed of both the TX and RX IO timers.
 * @param regs Register file whose timers to update.
 * @param speed Clock speed value passed through to each timer.
 * @return void.
 */
void com5025_reg_set_clock_speed(COM5025Registers *regs, int speed);

/**
 * @brief Advance both the TX and RX IO timers by one clock tick.
 * @param regs Register file whose timers to clock.
 * @return void.
 */
void com5025_reg_clock(COM5025Registers *regs);

/**
 * @brief Adjust the tick count and parameter of the TX and/or RX IO timer,
 *        selected by the timer mask.
 * @param regs Register file whose timers to adjust.
 * @param ticks New tick count.
 * @param param New timer parameter.
 * @param timer Which timer(s) to adjust (COM5025TimerFlags: TX, RX or both).
 * @return void.
 */
void com5025_reg_adjust_timer(COM5025Registers *regs, int ticks, int param,
                              COM5025TimerFlags timer);

// Receive queue functions

/**
 * @brief Push one received byte onto the receive queue, first applying HDLC
 *        async byte-unstuffing (drop the escape octet, then invert bit 5 of
 *        the following byte and tag it with bit 8).
 * @param regs Register file whose receive queue to push onto.
 * @param data Raw received byte (only the low 8 bits are used).
 * @return true if the byte was queued (or consumed as an escape marker),
 *         false if regs is NULL or the queue node allocation failed.
 */
bool com5025_reg_queue_received_data(COM5025Registers *regs, uint16_t data);

/**
 * @brief Test whether data is available at the head of the receive queue and
 *        latch it into dataFromReceiveQueue; tracks the overrun counter
 *        (rorCount) when the queue is empty.
 * @param regs Register file to inspect.
 * @return true if data was available, false otherwise or if regs is NULL.
 */
bool com5025_reg_data_received(COM5025Registers *regs);

/**
 * @brief Test whether the head of the receive queue is a non-stuffed sync or
 *        frame delimiter character (checked against the sync/secondary
 *        address in CCP mode, or the frame delimiter in BOP mode).
 * @param regs Register file to inspect.
 * @return true if the next queued byte is a sync/flag character, false
 *         otherwise, if the queue is empty, or if regs is NULL.
 */
bool com5025_reg_is_next_byte_sync(COM5025Registers *regs);

/**
 * @brief Pop and free the node at the head of the receive queue.
 * @param regs Register file whose receive queue to pop from.
 * @return void.
 */
void com5025_reg_mark_data_as_received(COM5025Registers *regs);

// CRC functions

/**
 * @brief Accumulate one received byte into the running RX CRC, per the
 *        register file's configured CRC mode.
 * @param regs Register file whose RX CRC to update.
 * @param data Byte to accumulate.
 * @return void.
 */
void com5025_reg_calc_rx_crc(COM5025Registers *regs, uint8_t data);

/**
 * @brief Compare the accumulated RX CRC against an expected value.
 * @param regs Register file to read the RX CRC from.
 * @param crc Expected CRC value.
 * @return true if the accumulated RX CRC equals crc, false otherwise or if
 *         regs is NULL.
 */
bool com5025_reg_is_rx_crc_equal(COM5025Registers *regs, uint16_t crc);

/**
 * @brief Accumulate one transmitted byte into the running TX CRC, per the
 *        register file's configured CRC mode; a no-op when CRC checking is
 *        inhibited.
 * @param regs Register file whose TX CRC to update.
 * @param data Byte to accumulate.
 * @return void.
 */
void com5025_reg_aggregate_tx_crc(COM5025Registers *regs, uint8_t data);

/**
 * @brief Finalize the accumulated TX CRC by inverting it (one's complement),
 *        as required before appending the FCS bytes to a frame.
 * @param regs Register file to read the TX CRC from.
 * @return Final TX CRC value, or 0x0000 if regs is NULL or CRC checking is
 *         inhibited.
 */
uint16_t com5025_reg_calc_final_tx_crc(COM5025Registers *regs);

/**
 * @brief Compare the accumulated (non-finalized) TX CRC against an expected
 *        value.
 * @param regs Register file to read the TX CRC from.
 * @param crc Expected CRC value.
 * @return true if the accumulated TX CRC equals crc, false otherwise or if
 *         regs is NULL.
 */
bool com5025_reg_is_tx_crc_equal(COM5025Registers *regs, uint16_t crc);

// Internal helper functions


// Timer callback setup

/**
 * @brief Register the callback invoked when an IO timer fires.
 * @param timer Timer to update.
 * @param callback Function to call with the timer's stored parameter.
 * @param context Opaque pointer passed back to callback.
 * @return void.
 */
void com5025_reg_set_callback(COM5025IOTimer *timer, void (*callback)(void *context, int param),
                              void *context);


#endif // CHIP_COM5025_REGISTERS_H
