/*
 * chip_com5025_registers.c - COM5025 register file: mode control, status and character lengths.
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

#include "chip_com5025_registers.h"

#include <stdlib.h>
#include <string.h>

#include "chip_com5025.h"
#include "hdlc_crc.h"

void COM5025Registers_Init(COM5025Registers *regs)
{
    if (!regs)
    {
        return;
    }

    memset(regs, 0, sizeof(COM5025Registers));
    COM5025Registers_Clear(regs);

    COM5025IOTimer_Init(&regs->txTimer);
    COM5025IOTimer_Init(&regs->rxTimer);
}

void COM5025Registers_Clear(COM5025Registers *regs)
{
    if (!regs)
    {
        return;
    }

    regs->receiverStatus = 0;
    regs->txStatusAndControl = 0;
    regs->modeControl = 0;
    regs->dataLengthSelect = 0;
    regs->rorCount = 0;

    regs->receiverDataBuffer = 0;
    regs->dataFromReceiveQueue = 0;

    // Clear receive queue
    while (regs->receiveQueue.head)
    {
        COM5025ReceiveQueueNode *node = regs->receiveQueue.head;
        regs->receiveQueue.head = node->next;
        free(node);
    }
    regs->receiveQueue.head = NULL;
    regs->receiveQueue.tail = NULL;
    regs->receiveQueue.count = 0;

    regs->transmitterDataBuffer = 0;
    regs->transmitterShiftRegister = 0;
    regs->transmitterShiftRegisterBit = 0;
    regs->tsrEnableBitStuffing = true;
    regs->tsrCountOnes = 0;

    regs->syncSecondaryAddress = 0;

    regs->transmitterState = COM5025_TX_STATE_IDLE;
    regs->receiverState = COM5025_RX_STATE_IDLE;

    COM5025Registers_ClearTXCRC(regs);
    COM5025Registers_ClearRXCRC(regs);

    COM5025IOTimer_Clear(&regs->txTimer);
    COM5025IOTimer_Clear(&regs->rxTimer);

    regs->byteStuffingDetected = false;
}

void COM5025Registers_Destroy(COM5025Registers *regs)
{
    if (!regs)
    {
        return;
    }

    // Clear receive queue
    while (regs->receiveQueue.head)
    {
        COM5025ReceiveQueueNode *node = regs->receiveQueue.head;
        regs->receiveQueue.head = node->next;
        free(node);
    }
}

void COM5025Registers_SetReceiverStatus(COM5025Registers *regs, uint16_t status)
{
    if (!regs)
    {
        return;
    }
    regs->receiverStatus = status;
}

uint8_t COM5025Registers_GetReceiverCharacterLen(COM5025Registers *regs)
{
    if (!regs)
    {
        return 0;
    }
    return (uint8_t)(regs->dataLengthSelect & 0x07);
}

uint8_t COM5025Registers_GetTransmitterCharacterLen(COM5025Registers *regs)
{
    if (!regs)
    {
        return 0;
    }
    return (uint8_t)((regs->dataLengthSelect >> 5) & 0x07);
}

void COM5025Registers_SetModeControl(COM5025Registers *regs, uint16_t modeControl)
{
    if (!regs)
    {
        return;
    }

    // Reset state machines
    regs->receiverState = COM5025_RX_STATE_IDLE;
    regs->transmitterState = COM5025_TX_STATE_IDLE;

    // Set ModeControl
    regs->modeControl = modeControl;

    // Update CRC mode based on X, Y, Z bits
    int mode = 0;
    mode |= (modeControl & COM5025_MODE_CONTROL_Z) ? (1 << 2) : 0;
    mode |= (modeControl & COM5025_MODE_CONTROL_Y) ? (1 << 1) : 0;
    mode |= (modeControl & COM5025_MODE_CONTROL_X) ? (1 << 0) : 0;
    regs->crcMode = (COM5025CrcMode)mode;
}

bool COM5025Registers_IsProtocolModeCCP(COM5025Registers *regs)
{
    if (!regs)
    {
        return false;
    }
    return (regs->modeControl & COM5025_MODE_CONTROL_PROTO) != 0;
}

void COM5025Registers_SetClockSpeed(COM5025Registers *regs, int speed)
{
    if (!regs)
    {
        return;
    }
    COM5025IOTimer_SetClockSpeed(&regs->txTimer, speed);
    COM5025IOTimer_SetClockSpeed(&regs->rxTimer, speed);
}

void COM5025Registers_Clock(COM5025Registers *regs)
{
    if (!regs)
    {
        return;
    }
    COM5025IOTimer_Clock(&regs->txTimer);
    COM5025IOTimer_Clock(&regs->rxTimer);
}

void COM5025Registers_AdjustTimer(COM5025Registers *regs, int ticks, int param,
                                  COM5025TimerFlags timer)
{
    if (!regs)
    {
        return;
    }

    if (timer & COM5025_TIMER_TX)
    {
        COM5025IOTimer_AdjustTimer(&regs->txTimer, ticks, param, false);
    }

    if (timer & COM5025_TIMER_RX)
    {
        COM5025IOTimer_AdjustTimer(&regs->rxTimer, ticks, param, false);
    }
}

bool COM5025Registers_QueueReceivedData(COM5025Registers *regs, uint16_t data)
{
    if (!regs)
    {
        return false;
    }

    data &= 0xFF; // Mask to 8 bits

    if (data == HDLC_ASYNC_ESCAPE_OCTET)
    {
        regs->byteStuffingDetected = true;
        return false;
    }

    if (regs->byteStuffingDetected)
    {
        // Invert bit 5
        data = (uint8_t)(data ^ HDLC_ASYNC_INVERT_OCTET);
        data |= 1 << 8; // Set bit 8 to indicate this data has been "escaped"
        regs->byteStuffingDetected = false;
    }

    regs->receiverDataBuffer = data;

    if (regs->receiveQueue.count < HDLC_MAX_RECEIVE_QUEUE_SIZE)
    {
        COM5025ReceiveQueueNode *node = malloc(sizeof(COM5025ReceiveQueueNode));
        if (!node)
        {
            return false;
        }

        node->data = data;
        node->next = NULL;

        if (regs->receiveQueue.tail)
        {
            regs->receiveQueue.tail->next = node;
        }
        else
        {
            regs->receiveQueue.head = node;
        }
        regs->receiveQueue.tail = node;
        regs->receiveQueue.count++;
    }
    else
    {
        // Queue is full - data lost
        return false;
    }

    return true;
}

bool COM5025Registers_DataReceived(COM5025Registers *regs)
{
    if (!regs)
    {
        return false;
    }

    if (regs->receiveQueue.count > 0)
    {
        regs->rorCount = 0;
        regs->dataFromReceiveQueue = regs->receiveQueue.head->data;
        return true;
    }
    else
    {
        regs->rorCount++;
        return false;
    }
}

bool COM5025Registers_IsNextByteSync(COM5025Registers *regs)
{
    if (!regs || regs->receiveQueue.count == 0)
    {
        return false;
    }

    uint16_t nextData = regs->receiveQueue.head->data;

    // Bit 8 set means this is byte-stuffed data
    if ((nextData & 0xFF00) != 0)
    {
        return false;
    }

    if (COM5025Registers_IsProtocolModeCCP(regs))
    {
        // Byte protocol - check for SYNC character or frame delimiter
        if ((nextData == (uint8_t)regs->syncSecondaryAddress) || (nextData == HDLC_FRAME_DELIMITER))
        {
            return true;
        }
    }
    else
    {
        // Bit protocol - check for frame delimiter
        if (nextData == HDLC_FRAME_DELIMITER)
        {
            return true;
        }
    }

    return false;
}

void COM5025Registers_MarkDataAsReceived(COM5025Registers *regs)
{
    if (!regs || regs->receiveQueue.count == 0)
    {
        return;
    }

    COM5025ReceiveQueueNode *node = regs->receiveQueue.head;
    regs->receiveQueue.head = node->next;
    if (!regs->receiveQueue.head)
    {
        regs->receiveQueue.tail = NULL;
    }
    regs->receiveQueue.count--;
    free(node);
}

uint16_t COM5025Registers_CalcCRC(COM5025Registers *regs, uint16_t crc, uint8_t data)
{
    if (!regs)
    {
        return crc;
    }

    switch (regs->crcMode)
    {
    case COM5025_CRC_MODE_CCITT_INIT_TO_1:
    case COM5025_CRC_MODE_CCITT_INIT_TO_0:
        crc = HDLC_CRC_CalcCCITT(crc, data);
        break;

    case COM5025_CRC_MODE_CRC16:
        crc = HDLC_CRC_CalcCrc16(crc, data);
        break;

    case COM5025_CRC_MODE_ODD_PARITY:
        // For parity mode, we don't accumulate CRC, just check the parity bit
        break;
    case COM5025_CRC_MODE_EVEN_PARITY:
        // For parity mode, we don't accumulate CRC, just check the parity bit
        break;

    case COM5025_CRC_MODE_INHIBIT_ERROR_DETECTION:
        // Do nothing
        break;

    default:
        break;
    }

    return crc;
}

void COM5025Registers_CalcRXCrc(COM5025Registers *regs, uint8_t data)
{
    if (!regs)
    {
        return;
    }
    regs->rxCrc = COM5025Registers_CalcCRC(regs, regs->rxCrc, data);
}

void COM5025Registers_ClearRXCRC(COM5025Registers *regs)
{
    if (!regs)
    {
        return;
    }

    if (regs->crcMode == COM5025_CRC_MODE_CCITT_INIT_TO_1)
    {
        regs->rxCrc = 0xFFFF;
    }
    else
    {
        regs->rxCrc = 0;
    }
}

bool COM5025Registers_IsRxCrcEqual(COM5025Registers *regs, uint16_t crc)
{
    if (!regs)
    {
        return false;
    }
    return (regs->rxCrc == crc);
}

void COM5025Registers_AggregateTXCrc(COM5025Registers *regs, uint8_t data)
{
    if (!regs || regs->crcMode == COM5025_CRC_MODE_INHIBIT_ERROR_DETECTION)
    {
        return;
    }
    regs->txCrc = COM5025Registers_CalcCRC(regs, regs->txCrc, data);
}

void COM5025Registers_ClearTXCRC(COM5025Registers *regs)
{
    if (!regs)
    {
        return;
    }

    if (regs->crcMode == COM5025_CRC_MODE_CCITT_INIT_TO_1)
    {
        regs->txCrc = 0xFFFF;
    }
    else
    {
        regs->txCrc = 0;
    }
}

uint16_t COM5025Registers_CalcFinalTxCrc(COM5025Registers *regs)
{
    if (!regs || regs->crcMode == COM5025_CRC_MODE_INHIBIT_ERROR_DETECTION)
    {
        return 0x0000;
    }

    return (uint16_t)(regs->txCrc ^ 0xFFFF);
}

bool COM5025Registers_IsTxCrcEqual(COM5025Registers *regs, uint16_t crc)
{
    if (!regs)
    {
        return false;
    }
    return (regs->txCrc == crc);
}

// Timer functions
void COM5025IOTimer_Init(COM5025IOTimer *timer)
{
    if (!timer)
    {
        return;
    }
    memset(timer, 0, sizeof(COM5025IOTimer));
}

void COM5025IOTimer_Clear(COM5025IOTimer *timer)
{
    if (!timer)
    {
        return;
    }
    timer->ticks = 0;
    timer->param = 0;
    timer->active = false;
}

void COM5025IOTimer_SetCallback(COM5025IOTimer *timer, void (*callback)(void *context, int param),
                                void *context)
{
    if (!timer)
    {
        return;
    }
    timer->callback = callback;
    timer->context = context;
}

void COM5025IOTimer_SetClockSpeed(COM5025IOTimer *timer, int speed)
{
    if (!timer)
    {
        return;
    }
    timer->clockSpeed = speed;
}

void COM5025IOTimer_AdjustTimer(COM5025IOTimer *timer, int ticks, int param, bool enable)
{
    if (!timer)
    {
        return;
    }
    timer->ticks = ticks;
    timer->param = param;
    timer->active = enable;
}

void COM5025IOTimer_Clock(COM5025IOTimer *timer)
{
    if (!timer || !timer->active)
    {
        return;
    }

    if (timer->ticks > 0)
    {
        timer->ticks--;
        if (timer->ticks == 0)
        {
            timer->active = false;
            if (timer->callback)
            {
                timer->callback(timer->context, timer->param);
            }
        }
    }
}
