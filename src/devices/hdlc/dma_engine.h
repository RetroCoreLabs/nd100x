/*
 * dma_engine.h - HDLC DMA engine: state structure and command API.
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

#ifndef DMA_ENGINE_H
#define DMA_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "dma_param_buf.h"

// Forward declarations
struct Device;
struct COM5025State;
struct ModemState;

// Forward declarations for types that are defined in other headers
struct DMAControlBlocks;
struct DMATransmitter;
struct DMAReceiver;
struct HDLCFrame;

// DMA engine callback function types
typedef void (*DMAWriteCallback)(struct Device *device, uint32_t address, uint16_t data);
typedef void (*DMAReadCallback)(struct Device *device, uint32_t address, int *data);
typedef void (*DMASetInterruptCallback)(struct Device *device, uint8_t bit);
typedef void (*DMASendFrameCallback)(struct Device *device, struct HDLCFrame *frame);
typedef void (*DMAUpdateReceiverStatusCallback)(struct Device *device, uint16_t status);
typedef void (*DMAClearCommandCallback)(struct Device *device);

// DMA Engine state enums are defined in dma_enum.h

// DMA Engine state structure
typedef struct DMAEngine
{
    // DMA Control Blocks - main coordination structure
    struct DMAControlBlocks *dmaCB;

    // DMA components
    struct DMATransmitter *transmitter;
    struct DMAReceiver *receiver;

    // Hardware references
    struct COM5025State *com5025;
    struct ModemState *modem;
    struct Device *hdlcDevice;

    // State management
    bool enabled;

    // DMA register array (256 16-bit registers)
    uint16_t dmaRegisters[256];

    // Current DMA address for operations
    uint32_t currentDMAAddress;

    // Parameter buffer for DMA operations
    ParameterBuffer parameterBuffer;

    // Callbacks to HDLC device
    DMAWriteCallback onWriteDMA;
    DMAReadCallback onReadDMA;
    DMASetInterruptCallback onSetInterruptBit;
    DMASendFrameCallback onSendHDLCFrame;
    DMAUpdateReceiverStatusCallback onUpdateReceiverStatus;
    DMAClearCommandCallback onClearCommand;

} DMAEngine;

// Core DMA Engine functions

/**
 * @brief Zero a DMAEngine, allocate and initialize its DMAControlBlocks,
 * transmitter and receiver, wire their callbacks back through this engine,
 * and initialize the parameter buffer and DMA register array.
 * @param dma Engine structure to initialize.
 * @param burstMode Kept for API compatibility; burst mode is always used, unverified.
 * @param hdlcDevice Owning HDLC device, stored and passed to sub-components.
 * @param modem Modem state, stored as struct ModemState * (opaque here).
 * @param com5025 COM5025 chip state, stored as COM5025State * (opaque here).
 */
void DMAEngine_Init(DMAEngine *dma, bool burstMode, struct Device *hdlcDevice, void *modem,
                    void *com5025);

/**
 * @brief Destroy the transmitter, receiver and DMA control blocks and free them.
 * @param dma Engine structure to tear down.
 */
void DMAEngine_Destroy(DMAEngine *dma);

/**
 * @brief Advance one tick: ticks the receiver before the transmitter so an
 * incoming ACK (RR frame) is delivered before SINTRAN's T1 timer can fire a
 * retransmission.
 * @param dma Engine structure.
 */
void DMAEngine_Tick(DMAEngine *dma);

// DMA Command execution

/**
 * @brief No-op placeholder: command dispatch is actually done by
 * device_hdlc.c, not here. Only logs at LOG_TRACE.
 * @param dma Engine structure.
 */
void DMAEngine_ExecuteCommand(DMAEngine *dma);

// DMA Command implementations (8 commands total)

/**
 * @brief Execute the DMA Device Clear command: clear the control blocks,
 * transmitter and receiver, disable the engine, and reset the COM5025 chip.
 * @param dma Engine structure.
 */
void DMAEngine_CommandDeviceClear(DMAEngine *dma);

/**
 * @brief Execute the DMA Initialize command: read the 7-word parameter
 * block (PCR, sync/address register, character length, Displacement1/2, max
 * receiver block length, checksum) from currentDMAAddress, store it in the
 * parameter buffer and COM5025 registers, and write back the checksum
 * 0102164 (octal) if it was zero.
 * @param dma Engine structure.
 */
void DMAEngine_CommandInitialize(DMAEngine *dma);

/**
 * @brief Execute the DMA Receiver Start command: set the RX list pointer to
 * currentDMAAddress and put the receiver into its receiving state.
 * @param dma Engine structure.
 */
void DMAEngine_CommandReceiverStart(DMAEngine *dma);

/**
 * @brief Execute the DMA Receiver Continue command: same as Receiver Start -
 * set the RX list pointer to currentDMAAddress and resume the receiver.
 * @param dma Engine structure.
 */
void DMAEngine_CommandReceiverContinue(DMAEngine *dma);

/**
 * @brief Execute the DMA Transmitter Start command: set the TX list pointer
 * to currentDMAAddress, log the TX frame list, and set the transmitter to
 * DMA_SENDER_BLOCK_READY_TO_SEND.
 * @param dma Engine structure.
 */
void DMAEngine_CommandTransmitterStart(DMAEngine *dma);

/**
 * @brief Execute the DMA Dump Data Module command: write the COM5025 mode
 * control, sync/address, character length, receiver status and transmitter
 * status/control bytes to the 5 memory words at currentDMAAddress.
 * @param dma Engine structure.
 */
void DMAEngine_CommandDumpDataModule(DMAEngine *dma);

/**
 * @brief Execute the DMA Dump Registers command: read a (firstReg, numreg)
 * pair from currentDMAAddress and write that many dmaRegisters[] entries to
 * memory, or the 16 bit-slice register indices if both are zero.
 * @param dma Engine structure.
 */
void DMAEngine_CommandDumpRegisters(DMAEngine *dma);

/**
 * @brief Execute the DMA Load Registers command: read a (firstReg, numreg)
 * pair from currentDMAAddress and load that many dmaRegisters[] entries from
 * the following memory words.
 * @param dma Engine structure.
 */
void DMAEngine_CommandLoadRegisters(DMAEngine *dma);

// Memory access functions


// Utility functions

/**
 * @brief Set the current DMA address used by the next command implementation.
 * @param dma Engine structure.
 * @param address Memory address to store in currentDMAAddress.
 */
void DMAEngine_SetDMAAddress(DMAEngine *dma, uint32_t address);

/**
 * @brief Read the raw key value of the list entry at listPointer + offset*4.
 * @param dma Engine structure.
 * @param listPointer Base memory address of the buffer list.
 * @param offset Offset (in 4-word blocks) of the entry to read.
 * @return Key value read from memory, or 0 if dma is NULL.
 */
uint16_t DMAEngine_GetBufferKeyVault(DMAEngine *dma, uint32_t listPointer, uint16_t offset);

/**
 * @brief Scan forward from start in 4-word steps for the next TX list entry
 * whose key is KEYFLAG_BLOCK_TO_BE_TRANSMITTED or KEYFLAG_NEW_LIST_POINTER.
 * @param dma Engine structure.
 * @param start Memory address to start scanning from.
 * @return Address of the matching entry, or 0 if dma is NULL or an empty
 * key (end of list) is reached first.
 */
uint32_t DMAEngine_ScanNextTXBuffer(DMAEngine *dma, uint32_t start);

// Event handling functions

/**
 * @brief Forward a DMA memory write to the engine's onWriteDMA callback.
 * @param dma Engine structure.
 * @param address Memory address to write.
 * @param data 16-bit value to write.
 */
void DMAEngine_OnWriteDMA(DMAEngine *dma, uint32_t address, uint16_t data);

/**
 * @brief Forward a DMA memory read to the engine's onReadDMA callback.
 * @param dma Engine structure.
 * @param address Memory address to read.
 * @param data Out parameter receiving the read value, or -1 on failure or if dma/onReadDMA is NULL.
 */
void DMAEngine_OnReadDMA(DMAEngine *dma, uint32_t address, int *data);

// Callback setup functions

/**
 * @brief Install the DMA memory-write callback.
 * @param dma Engine structure.
 * @param callback Function to call for a DMA memory write.
 */
void DMAEngine_SetWriteDMACallback(DMAEngine *dma, DMAWriteCallback callback);

/**
 * @brief Install the DMA memory-read callback.
 * @param dma Engine structure.
 * @param callback Function to call for a DMA memory read.
 */
void DMAEngine_SetReadDMACallback(DMAEngine *dma, DMAReadCallback callback);

/**
 * @brief Install the callback used to raise an interrupt bit.
 * @param dma Engine structure.
 * @param callback Function to call with the interrupt bit number.
 */
void DMAEngine_SetInterruptCallback(DMAEngine *dma, DMASetInterruptCallback callback);

/**
 * @brief Install the callback used to hand a completed HDLC frame onward.
 * @param dma Engine structure.
 * @param callback Function to call with the assembled frame.
 */
void DMAEngine_SetSendFrameCallback(DMAEngine *dma, DMASendFrameCallback callback);

/**
 * @brief Install the callback used to OR receiver status bits into the
 * device's receiver dataflow status register.
 * @param dma Engine structure.
 * @param callback Function to call with the receiver status byte.
 */
void DMAEngine_SetUpdateReceiverStatusCallback(DMAEngine *dma,
                                               DMAUpdateReceiverStatusCallback callback);

/**
 * @brief Install the callback used to clear the device's pending DMA command.
 * @param dma Engine structure.
 * @param callback Function to call to clear the command.
 */
void DMAEngine_SetClearCommandCallback(DMAEngine *dma, DMAClearCommandCallback callback);

// Debug functions


#endif // DMA_ENGINE_H
