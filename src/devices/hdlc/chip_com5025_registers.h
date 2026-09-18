/*
 * nd100x - ND100 Virtual Machine
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
typedef enum {
    COM5025_TIMER_TX = 1 << 0,
    COM5025_TIMER_RX = 1 << 1,
    COM5025_TIMER_BOTH = (COM5025_TIMER_TX | COM5025_TIMER_RX)
} COM5025TimerFlags;

// CRC Mode
typedef enum {
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
typedef enum {
    COM5025_TX_STATE_IDLE,
    COM5025_TX_STATE_SENDING_FLAGS,
    COM5025_TX_STATE_SENDING_DATA,
    COM5025_TX_STATE_SENDING_CRC,
    COM5025_TX_STATE_SENDING_CLOSING_FLAG,
    COM5025_TX_STATE_UNDERRUN
} COM5025TXState;

// Receiver State
typedef enum {
    COM5025_RX_STATE_IDLE,
    COM5025_RX_STATE_HUNT_SYNC,
    COM5025_RX_STATE_RECEIVING_DATA,
    COM5025_RX_STATE_RECEIVING_CRC,
    COM5025_RX_STATE_MESSAGE_COMPLETE,
    COM5025_RX_STATE_ABORT_DETECTED
} COM5025RXState;

// IO Timer structure
typedef struct {
    int ticks;
    int param;
    bool active;
    int clockSpeed;
    void (*callback)(void *context, int param);
    void *context;
} COM5025IOTimer;

// Receive queue node
typedef struct COM5025ReceiveQueueNode {
    uint16_t data;
    struct COM5025ReceiveQueueNode *next;
} COM5025ReceiveQueueNode;

// Receive queue
typedef struct {
    COM5025ReceiveQueueNode *head;
    COM5025ReceiveQueueNode *tail;
    int count;
} COM5025ReceiveQueue;

// Register structure
typedef struct {
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
void COM5025Registers_Init(COM5025Registers *regs);
void COM5025Registers_Clear(COM5025Registers *regs);
void COM5025Registers_Destroy(COM5025Registers *regs);

// Register access
void COM5025Registers_SetReceiverStatus(COM5025Registers *regs, uint16_t status);
uint8_t COM5025Registers_GetReceiverCharacterLen(COM5025Registers *regs);
uint8_t COM5025Registers_GetTransmitterCharacterLen(COM5025Registers *regs);
void COM5025Registers_SetModeControl(COM5025Registers *regs, uint16_t modeControl);
bool COM5025Registers_IsProtocolModeCCP(COM5025Registers *regs);

// Timer functions
void COM5025Registers_SetClockSpeed(COM5025Registers *regs, int speed);
void COM5025Registers_Clock(COM5025Registers *regs);
void COM5025Registers_AdjustTimer(COM5025Registers *regs, int ticks, int param, COM5025TimerFlags timer);

// Receive queue functions
bool COM5025Registers_QueueReceivedData(COM5025Registers *regs, uint16_t data);
bool COM5025Registers_DataReceived(COM5025Registers *regs);
bool COM5025Registers_IsNextByteSync(COM5025Registers *regs);
void COM5025Registers_MarkDataAsReceived(COM5025Registers *regs);

// CRC functions
void COM5025Registers_CalcRXCrc(COM5025Registers *regs, uint8_t data);
void COM5025Registers_ClearRXCRC(COM5025Registers *regs);
bool COM5025Registers_IsRxCrcEqual(COM5025Registers *regs, uint16_t crc);

void COM5025Registers_AggregateTXCrc(COM5025Registers *regs, uint8_t data);
void COM5025Registers_ClearTXCRC(COM5025Registers *regs);
uint16_t COM5025Registers_CalcFinalTxCrc(COM5025Registers *regs);
bool COM5025Registers_IsTxCrcEqual(COM5025Registers *regs, uint16_t crc);

// Internal helper functions
uint16_t COM5025Registers_CalcCRC(COM5025Registers *regs, uint16_t crc, uint8_t data);

// Timer callback setup
void COM5025IOTimer_Init(COM5025IOTimer *timer);
void COM5025IOTimer_Clear(COM5025IOTimer *timer);
void COM5025IOTimer_SetCallback(COM5025IOTimer *timer, void (*callback)(void *context, int param), void *context);
void COM5025IOTimer_SetClockSpeed(COM5025IOTimer *timer, int speed);
void COM5025IOTimer_AdjustTimer(COM5025IOTimer *timer, int ticks, int param, bool enable);
void COM5025IOTimer_Clock(COM5025IOTimer *timer);

#endif // CHIP_COM5025_REGISTERS_H