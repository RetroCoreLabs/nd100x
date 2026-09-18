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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#include "dmaControlBlocks.h"
#include "dmaParamBuf.h"
#include "dmaTransmitter.h"
#include "dmaReceiver.h"
#include "dmaEngine.h"
#include "chipCOM5025.h"
#include "chipCOM5025Registers.h"
#include "modem.h"
#include "dmaEnum.h"
#include "hdlc_constants.h"
#include "../devices_types.h"

// Debug flags (convert from C# #define)
// #define DEBUG_DETAIL
// #define DEBUG_DETAIL_PLUS_DESCRIPTION
// #define DMA_DEBUG

// ---------------------------------------------------------------------------
// Forwarding wrappers: bridge DMAControlBlocks/TX/RX callbacks to DMAEngine
// ---------------------------------------------------------------------------

// DMAControlBlocks read callback: context is DMAEngine*, forward to DMAEngine_DMARead
static uint16_t DMAEngine_CBReadDMA(void *context, uint32_t address)
{
    DMAEngine *dma = (DMAEngine *)context;
    int result = DMAEngine_DMARead(dma, address);
    return (result >= 0) ? (uint16_t)result : 0;
}

// DMAControlBlocks write callback: context is DMAEngine*, forward to DMAEngine_DMAWrite
static void DMAEngine_CBWriteDMA(void *context, uint32_t address, uint16_t data)
{
    DMAEngine *dma = (DMAEngine *)context;
    DMAEngine_DMAWrite(dma, address, data);
}

// DMATransmitter send frame callback: context is DMAEngine*, forward to onSendHDLCFrame
static void DMAEngine_TXSendFrame(void *context, HDLCFrame *frame)
{
    DMAEngine *dma = (DMAEngine *)context;
    if (dma && dma->onSendHDLCFrame) {
        dma->onSendHDLCFrame(dma->hdlcDevice, frame);
    }
}

// DMATransmitter interrupt callback: context is DMAEngine*, forward to onSetInterruptBit
static void DMAEngine_TXInterrupt(void *context, uint8_t bit)
{
    DMAEngine *dma = (DMAEngine *)context;
    DMAEngine_OnSetInterruptBit(dma, bit);
}

// DMAReceiver interrupt callback: context is DMAEngine*, forward to onSetInterruptBit
static void DMAEngine_RXInterrupt(void *context, uint8_t bit)
{
    DMAEngine *dma = (DMAEngine *)context;
    DMAEngine_OnSetInterruptBit(dma, bit);
}

void DMAEngine_Init(DMAEngine *dma, bool burstMode, struct Device *hdlcDevice, void *modem, void *com5025)
{
    if (!dma) return;

    memset(dma, 0, sizeof(DMAEngine));

    // Initialize DMA Control Blocks
    dma->dmaCB = malloc(sizeof(DMAControlBlocks));
    if (!dma->dmaCB) return;

    DMAControlBlocks_Init((DMAControlBlocks *)dma->dmaCB, hdlcDevice);
    // burstMode parameter kept for API compatibility but always true

    // Store references
    dma->hdlcDevice = hdlcDevice;
    dma->modem = (struct ModemState *)modem;
    dma->com5025 = (COM5025State *)com5025;

    // Initialize transmitter and receiver
    dma->transmitter = malloc(sizeof(DMATransmitter));
    dma->receiver = malloc(sizeof(DMAReceiver));

    if (dma->transmitter) {
        DMATransmitter_Init(dma->transmitter, com5025, dma->dmaCB, hdlcDevice);
        // Wire transmitter callbacks through DMAEngine forwarding wrappers
        DMATransmitter_SetSendFrameCallback(dma->transmitter, DMAEngine_TXSendFrame);
        DMATransmitter_SetInterruptCallback(dma->transmitter, DMAEngine_TXInterrupt);
        dma->transmitter->callbackContext = dma;
    }

    if (dma->receiver) {
        DMAReceiver_Init(dma->receiver, com5025, dma->dmaCB, hdlcDevice);
        // Wire receiver interrupt callback through DMAEngine forwarding wrapper
        DMAReceiver_SetInterruptCallback(dma->receiver, DMAEngine_RXInterrupt);
        dma->receiver->callbackContext = dma;
    }

    // Initialize state
    dma->enabled = false;
    dma->currentDMAAddress = 0;

    // Initialize parameter buffer and link to DMA control blocks
    ParameterBuffer_Init(&dma->parameterBuffer);
    if (dma->dmaCB) {
        ((DMAControlBlocks *)dma->dmaCB)->parameters = &dma->parameterBuffer;
    }

    // Wire DMA control blocks memory access callbacks through DMAEngine forwarding wrappers
    if (dma->dmaCB) {
        DMAControlBlocks_SetReadDMACallback(dma->dmaCB, DMAEngine_CBReadDMA, dma);
        DMAControlBlocks_SetWriteDMACallback(dma->dmaCB, DMAEngine_CBWriteDMA, dma);
    }

    // Initialize DMA registers array
    memset(dma->dmaRegisters, 0, sizeof(dma->dmaRegisters));
}

void DMAEngine_Destroy(DMAEngine *dma)
{
    if (!dma) return;

    if (dma->transmitter) {
        DMATransmitter_Destroy(dma->transmitter);
        free(dma->transmitter);
        dma->transmitter = NULL;
    }

    if (dma->receiver) {
        DMAReceiver_Destroy(dma->receiver);
        free(dma->receiver);
        dma->receiver = NULL;
    }

    if (dma->dmaCB) {
        DMAControlBlocks_Destroy(dma->dmaCB);
        free(dma->dmaCB);
        dma->dmaCB = NULL;
    }
}

void DMAEngine_Clear(DMAEngine *dma)
{
    if (!dma) return;

    // Clear transmitter and receiver
    if (dma->transmitter) {
        DMATransmitter_Clear(dma->transmitter);
    }

    if (dma->receiver) {
        DMAReceiver_Clear(dma->receiver);
    }

    if (dma->dmaCB) {
        DMAControlBlocks_Clear(dma->dmaCB);
    }
}

void DMAEngine_Tick(DMAEngine *dma)
{
    if (!dma) return;

    // RX MUST tick BEFORE TX so that incoming ACKs (RR frames) are delivered
    // to SINTRAN before SINTRAN's T1 timer fires and triggers retransmissions.
    if (dma->receiver) {
        DMAReceiver_Tick(dma->receiver);
    }

    if (dma->transmitter) {
        DMATransmitter_Tick(dma->transmitter);
    }
}

// Memory access functions - forward to callbacks

int DMAEngine_DMARead(DMAEngine *dma, uint32_t address)
{
    if (!dma || !dma->onReadDMA) return -1;

    int data = -1;
    dma->onReadDMA(dma->hdlcDevice, address, &data);
    return data;
}

void DMAEngine_DMAWrite(DMAEngine *dma, uint32_t address, uint16_t data)
{
    if (!dma || !dma->onWriteDMA) return;

    dma->onWriteDMA(dma->hdlcDevice, address, data);
}

// Event handling functions

void DMAEngine_OnSetInterruptBit(DMAEngine *dma, uint8_t bit)
{
    if (!dma || !dma->onSetInterruptBit) return;

    dma->onSetInterruptBit(dma->hdlcDevice, bit);
}

void DMAEngine_OnWriteDMA(DMAEngine *dma, uint32_t address, uint16_t data)
{
    if (!dma || !dma->onWriteDMA) return;

    dma->onWriteDMA(dma->hdlcDevice, address, data);
}

void DMAEngine_OnReadDMA(DMAEngine *dma, uint32_t address, int *data)
{
    if (!dma || !dma->onReadDMA || !data) {
        if (data) *data = -1;
        return;
    }

    dma->onReadDMA(dma->hdlcDevice, address, data);
}

// DMA Command execution - main dispatcher

void DMAEngine_ExecuteCommand(DMAEngine *dma)
{
    // Note: This function is not currently used as the HDLC device
    // handles command dispatch directly. In the C# implementation,
    // this function was called by the HDLC device to execute commands.
    //
    // The C implementation handles command execution in deviceHDLC.c
    // for better integration with the device register system.

    if (!dma) return;

#ifdef DEBUG_DETAIL
    DMAEngine_Log(dma, "DMAEngine_ExecuteCommand called (not implemented - see deviceHDLC.c)");
#endif

    // This function is intentionally not implemented as command execution
    // is handled by the HDLC device to maintain proper access to
    // device registers and state.
}

// DMA Command implementations

void DMAEngine_CommandDeviceClear(DMAEngine *dma)
{
    if (!dma) return;

#ifdef DMA_DEBUG
    DMAEngine_Log(dma, "DMA Device Clear command");
#endif

    // Clear all components
    DMAEngine_Clear(dma);
    dma->enabled = false; // allow COM5025 clocking for maintenance test

    if (dma->com5025) {
        COM5025_Reset(dma->com5025);
    }

    DMAEngine_ClearDMACommand(dma);
}

void DMAEngine_CommandInitialize(DMAEngine *dma)
{
    if (!dma) return;

#ifdef DMA_DEBUG
    DMAEngine_Log(dma, "DMA Initialize command");
#endif

    /*
     * The Initialize sequence uses 7 locations in memory. The contents of the locations are:
     *
     * 1. Parameter Control Reg.    (8 least significant bits)
     * 2. Sync/Address Register     (8 least significant bits)
     * 3. Character Length          (8 least significant bits)
     * 4. Displacement 1            (No. of bytes, first block in frame)
     * 5. Displacement 2            (No. of bytes, other blocks in frame)
     * 6. Max. Rec. Block Length    (No. of bytes, including displacement)
     * 7. Checksum (0102164 is written back from interface)
     */

    uint32_t dma_address = dma->currentDMAAddress;

    if (dma_address == 0) {
        DMAEngine_ClearDMACommand(dma);
        return;
    }

    // Read parameter buffer from memory
    int parameterControlRegister = DMAEngine_DMARead(dma, dma_address++);
    int syncAddressRegister = DMAEngine_DMARead(dma, dma_address++);
    int characterLength = DMAEngine_DMARead(dma, dma_address++);
    int displacement1 = DMAEngine_DMARead(dma, dma_address++);
    int displacement2 = DMAEngine_DMARead(dma, dma_address++);
    int maxReceiverBlockLength = DMAEngine_DMARead(dma, dma_address++);
    int checksum = DMAEngine_DMARead(dma, dma_address);

    // Store parameters in parameter buffer
    ParameterBuffer_SetParameterControlRegister(&dma->parameterBuffer, parameterControlRegister);
    ParameterBuffer_SetSyncAddressRegister(&dma->parameterBuffer, syncAddressRegister);
    ParameterBuffer_SetCharacterLength(&dma->parameterBuffer, characterLength);
    ParameterBuffer_SetDisplacement1(&dma->parameterBuffer, displacement1);
    ParameterBuffer_SetDisplacement2(&dma->parameterBuffer, displacement2);
    ParameterBuffer_SetMaxReceiverBlockLength(&dma->parameterBuffer, maxReceiverBlockLength);

    // Configure COM5025 with parameters
    if (dma->com5025) {
        COM5025_WriteByte(dma->com5025, COM5025_REG_BYTE_MODE_CONTROL, (uint8_t)(parameterControlRegister & 0xFF));
        COM5025_WriteByte(dma->com5025, COM5025_REG_BYTE_SYNC_ADDRESS, (uint8_t)(syncAddressRegister & 0xFF));
        COM5025_WriteByte(dma->com5025, COM5025_REG_BYTE_DATA_LENGTH_SELECT, (uint8_t)(characterLength & 0xFF));
    }

    // Store displacement parameters in DMA registers
    dma->dmaRegisters[5] = (uint16_t)displacement1;
    dma->dmaRegisters[6] = (uint16_t)displacement2;
    dma->dmaRegisters[7] = (uint16_t)maxReceiverBlockLength;

#ifdef DMA_DEBUG
    DMAEngine_Log(dma, "--------------------------------------------------------------------------");
    DMAEngine_Log(dma, "DMA CommandInitialize    : 0x%06X", dma->currentDMAAddress);
    DMAEngine_Log(dma, "ParameterControlRegister : 0x%04X", parameterControlRegister);
    DMAEngine_Log(dma, "Sync_AddressRegister     : 0x%04X", syncAddressRegister);
    DMAEngine_Log(dma, "CharacterLength          : 0x%04X", characterLength);
    DMAEngine_Log(dma, "Displacement1            : 0x%04X", displacement1);
    DMAEngine_Log(dma, "Displacement2            : 0x%04X", displacement2);
    DMAEngine_Log(dma, "MaxReceiverBlockLength   : 0x%04X", maxReceiverBlockLength);
    DMAEngine_Log(dma, "--------------------------------------------------------------------------");
#endif

    // Write back checksum if current checksum is 0
    if (checksum == 0) {
        DMAEngine_DMAWrite(dma, dma_address, 0x8474); // 0102164 octal = 0x8474 hex
    }

    // DMA is now initialized - COM5025 clocking can be stopped
    // (burst mode handles all framing via DMA engine + HDLCFrame)
    dma->enabled = true;

    DMAEngine_ClearDMACommand(dma);
}

void DMAEngine_CommandReceiverStart(DMAEngine *dma)
{
    if (!dma) return;

#ifdef DMA_DEBUG
    DMAEngine_Log(dma, "DMA Receiver Start command");
#endif

    // Set RX pointer to current DMA address
    if (dma->dmaCB) {
        DMAControlBlocks_SetRXPointer(dma->dmaCB, dma->currentDMAAddress, 0);
    }

    if (dma->receiver) {
        DMAReceiver_SetReceiverState(dma->receiver);
    }

    DMAEngine_ClearDMACommand(dma);
}

void DMAEngine_CommandReceiverContinue(DMAEngine *dma)
{
    if (!dma) return;

#ifdef DMA_DEBUG
    DMAEngine_Log(dma, "DMA Receiver Continue command");
#endif

    // Set RX pointer to current DMA address
    if (dma->dmaCB) {
        DMAControlBlocks_SetRXPointer(dma->dmaCB, dma->currentDMAAddress, 0);
    }

    if (dma->receiver) {
        DMAReceiver_SetReceiverState(dma->receiver);
    }

    DMAEngine_ClearDMACommand(dma);
}

void DMAEngine_CommandTransmitterStart(DMAEngine *dma)
{
    if (!dma) return;

#ifdef DMA_DEBUG
    DMAEngine_Log(dma, "DMA Transmitter Start command");
#endif

    // Set TX pointer to current DMA address
    if (dma->dmaCB) {
        DMAControlBlocks_SetTXPointer(dma->dmaCB, dma->currentDMAAddress, 0);
        DMAControlBlocks_DebugTXFrames(dma->dmaCB);
    }

    if (dma->transmitter) {
        DMATransmitter_SetSenderState(dma->transmitter, DMA_SENDER_BLOCK_READY_TO_SEND);
    }

    DMAEngine_ClearDMACommand(dma);
}

void DMAEngine_CommandDumpDataModule(DMAEngine *dma)
{
    if (!dma) return;

#ifdef DMA_DEBUG
    DMAEngine_Log(dma, "DMA Dump Data Module command");
#endif

    /*
     * This command is mainly for maintenance purpose.
     * It requires 5 locations in memory, where the contents of the following registers are stored:
     * 1. Parameter Control Register (8 least sign. bits)
     * 2. Sync/Address Register (8 least sign. bits)
     * 3. Character Length (8 least sign. bits)
     * 4. Receiver Status Register (8 least sign. bits, not accumulated)
     * 5. Transmitter Status Register (8 least sign. bits, not accumulated)
     */

    uint32_t dma_address = dma->currentDMAAddress;

    if (dma_address == 0) {
        DMAEngine_ClearDMACommand(dma);
        return;
    }

    if (dma->com5025) {
        uint16_t data;

        // 1. Parameter Control Register
        data = COM5025_ReadByte(dma->com5025, COM5025_REG_BYTE_MODE_CONTROL);
        DMAEngine_DMAWrite(dma, dma_address++, (uint16_t)data);

        // 2. Sync/Address Register
        data = COM5025_ReadByte(dma->com5025, COM5025_REG_BYTE_SYNC_ADDRESS);
        DMAEngine_DMAWrite(dma, dma_address++, (uint16_t)data);

        // 3. Character Length
        data = COM5025_ReadByte(dma->com5025, COM5025_REG_BYTE_DATA_LENGTH_SELECT);
        DMAEngine_DMAWrite(dma, dma_address++, (uint16_t)data);

        // 4. Receiver Status Register
        data = COM5025_ReadByte(dma->com5025, COM5025_REG_BYTE_RECEIVER_STATUS);
        DMAEngine_DMAWrite(dma, dma_address++, (uint16_t)data);

        // OR the Receiver Status Register into the Receiver Dataflow Status Register to prevent loss of information
        if (dma->onUpdateReceiverStatus) {
            dma->onUpdateReceiverStatus(dma->hdlcDevice, data);
        }

        // 5. Transmitter Status Register
        data = COM5025_ReadByte(dma->com5025, COM5025_REG_BYTE_TRANSMITTER_STATUS_CONTROL);
        DMAEngine_DMAWrite(dma, dma_address++, (uint16_t)data);
    }

    DMAEngine_ClearDMACommand(dma);
}

void DMAEngine_CommandDumpRegisters(DMAEngine *dma)
{
    if (!dma) return;

#ifdef DMA_DEBUG
    DMAEngine_Log(dma, "DMA Dump Registers command");
#endif

    /*
     * This command can be used to dump the contents of any number of the 256 random access memory registers in the DMA module.
     * Required space in memory is 2 locations plus one location for each register to be dumped.
     *
     * The contents of the two locations are:
     * 1. First Register Address
     * 2. Number of Registers
     *
     * If both values are zero, the contents of the 16 registers in the Bit Slice are written into memory.
     */

    uint32_t dma_address = dma->currentDMAAddress;

    if (dma_address == 0) {
        DMAEngine_ClearDMACommand(dma);
        return;
    }

    uint16_t firstReg = (uint16_t)(DMAEngine_DMARead(dma, dma_address++) & 0x00FF);
    uint16_t numreg = (uint16_t)(DMAEngine_DMARead(dma, dma_address++) & 0x00FF);

    if ((firstReg == 0) && (numreg == 0)) {
        // If both values are zero, the contents of the 16 registers in the Bit Slice are written into memory
        for (uint8_t i = 0; i < 16; i++) {
            uint16_t data = i; // Basic register index for bit slice
            DMAEngine_DMAWrite(dma, dma_address++, data);
#ifdef DMA_DEBUG
            DMAEngine_Log(dma, "DUMP BIT SLICE REGISTER %d = 0x%04X", i, data);
#endif
        }
    } else {
        for (uint16_t i = 0; i < numreg; i++) {
            int offset = firstReg + i;
            if (offset < 256) {
                uint16_t data = dma->dmaRegisters[offset];
                DMAEngine_DMAWrite(dma, dma_address++, data);
#ifdef DMA_DEBUG
                DMAEngine_Log(dma, "DUMP REGISTER %d = 0x%04X", offset, data);
#endif
            }
        }
    }

    DMAEngine_ClearDMACommand(dma);
}

void DMAEngine_CommandLoadRegisters(DMAEngine *dma)
{
    if (!dma) return;

#ifdef DMA_DEBUG
    DMAEngine_Log(dma, "DMA Load Registers command");
#endif

    /*
     * This command can be used to load any number of the 256 random access memory registers in the DMA module.
     *
     * Required space in memory is 2 locations plus one location for each register to be loaded.
     * The contents of the two locations are:
     * 1. First register address
     * 2. Number of Registers
     *
     * The Load Register command is similar to Dump Register, except that data is moved in the opposite direction.
     * It is not possible to load the registers in the Bit Slice by this command.
     */

    uint32_t dma_address = dma->currentDMAAddress;

    if (dma_address == 0) {
        DMAEngine_ClearDMACommand(dma);
        return;
    }

    uint16_t firstReg = (uint16_t)(DMAEngine_DMARead(dma, dma_address++) & 0x00FF);
    uint16_t numreg = (uint16_t)(DMAEngine_DMARead(dma, dma_address++) & 0x00FF);

    for (uint16_t i = 0; i < numreg; i++) {
        int offset = firstReg + i;
        if (offset < 256) {
            int readVal = DMAEngine_DMARead(dma, dma_address++); // returns -1 if it fails
            if (readVal >= 0) {
                dma->dmaRegisters[offset] = (uint16_t)readVal;
#ifdef DMA_DEBUG
                DMAEngine_Log(dma, "LOAD REGISTER %d = 0x%04X", offset, readVal);
#endif
            }
        }
    }

    DMAEngine_ClearDMACommand(dma);
}

// Utility functions

void DMAEngine_SetDMAAddress(DMAEngine *dma, uint32_t address)
{
    if (!dma) return;
    dma->currentDMAAddress = address;
}

void DMAEngine_ClearDMACommand(DMAEngine *dma)
{
    if (!dma) return;

    // Clear DMA command via callback to HDLC device
    if (dma->onClearCommand) {
        dma->onClearCommand(dma->hdlcDevice);
    }
}

uint16_t DMAEngine_GetBufferKeyVault(DMAEngine *dma, uint32_t listPointer, uint16_t offset)
{
    if (!dma) return 0;

    uint32_t currentListPointer = listPointer + (uint32_t)(offset * 4);
    uint16_t keyValue = (uint16_t)DMAEngine_DMARead(dma, currentListPointer);
    return keyValue;
}

uint32_t DMAEngine_ScanNextTXBuffer(DMAEngine *dma, uint32_t start)
{
    if (!dma) return 0;

    uint32_t nextMem = start;

    while (true) {
        int memkey = DMAEngine_DMARead(dma, nextMem);
        if (memkey == 0) return 0; // end of pointers

        KeyFlags key = (KeyFlags)(memkey) & KEYFLAG_MASK_KEY;

        if (key == KEYFLAG_BLOCK_TO_BE_TRANSMITTED) return nextMem;
        if (key == KEYFLAG_NEW_LIST_POINTER) return nextMem;

        nextMem += 4; // next memory
    }

    return 0;
}

// Callback setup functions

void DMAEngine_SetWriteDMACallback(DMAEngine *dma, DMAWriteCallback callback)
{
    if (!dma) return;
    dma->onWriteDMA = callback;
}

void DMAEngine_SetReadDMACallback(DMAEngine *dma, DMAReadCallback callback)
{
    if (!dma) return;
    dma->onReadDMA = callback;
}

void DMAEngine_SetInterruptCallback(DMAEngine *dma, DMASetInterruptCallback callback)
{
    if (!dma) return;
    dma->onSetInterruptBit = callback;
}

void DMAEngine_SetSendFrameCallback(DMAEngine *dma, DMASendFrameCallback callback)
{
    if (!dma) return;
    dma->onSendHDLCFrame = callback;
}

void DMAEngine_SetUpdateReceiverStatusCallback(DMAEngine *dma, DMAUpdateReceiverStatusCallback callback)
{
    if (!dma) return;
    dma->onUpdateReceiverStatus = callback;
}

void DMAEngine_SetClearCommandCallback(DMAEngine *dma, DMAClearCommandCallback callback)
{
    if (!dma) return;
    dma->onClearCommand = callback;
}

// Debug functions

void DMAEngine_Log(DMAEngine *dma, const char *format, ...)
{
    if (!dma || !format) return;

    va_list args;
    va_start(args, format);
    printf("DMAEngine: ");
    vprintf(format, args);
    printf("\n");
    va_end(args);
}