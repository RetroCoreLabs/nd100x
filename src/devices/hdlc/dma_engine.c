/*
 * dma_engine.c - HDLC DMA engine: command execution, TX/RX interrupts and ticks.
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

#include "dma_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#include "dma_control_blocks.h"
#include "dma_param_buf.h"
#include "dma_transmitter.h"
#include "dma_receiver.h"
#include "chip_com5025.h"
#include "chip_com5025_registers.h"
#include "modem.h"
#include "dma_enum.h"
#include "hdlc_constants.h"
#include "../devices_types.h"
static void dma_engine_clear_dma_command(DMAEngine *);
static int dma_engine_dma_read(DMAEngine *, uint32_t);
static void dma_engine_dma_write(DMAEngine *, uint32_t, uint16_t);
static void dma_engine_log(DMAEngine *, const char *, ...) __attribute__((format(printf, 2, 3)));
static void dma_engine_on_set_interrupt_bit(DMAEngine *, uint8_t);


// Debug flags (convert from C# #define)

// ---------------------------------------------------------------------------
// Forwarding wrappers: bridge DMAControlBlocks/TX/RX callbacks to DMAEngine
// ---------------------------------------------------------------------------

// DMAControlBlocks read callback: context is DMAEngine*, forward to dma_engine_dma_read
static uint16_t dma_engine_cb_read_dma(void *context, uint32_t address)
{
    DMAEngine *dma = (DMAEngine *)context;
    int result = dma_engine_dma_read(dma, address);
    return (result >= 0) ? (uint16_t)result : 0;
}

// DMAControlBlocks write callback: context is DMAEngine*, forward to dma_engine_dma_write
static void dma_engine_cb_write_dma(void *context, uint32_t address, uint16_t data)
{
    DMAEngine *dma = (DMAEngine *)context;
    dma_engine_dma_write(dma, address, data);
}

// DMATransmitter send frame callback: context is DMAEngine*, forward to onSendHDLCFrame
static void dma_engine_tx_send_frame(void *context, HDLCFrame *frame)
{
    DMAEngine *dma = (DMAEngine *)context;
    if (dma && dma->onSendHDLCFrame)
    {
        dma->onSendHDLCFrame(dma->hdlcDevice, frame);
    }
}

// DMATransmitter interrupt callback: context is DMAEngine*, forward to onSetInterruptBit
static void dma_engine_tx_interrupt(void *context, uint8_t bit)
{
    DMAEngine *dma = (DMAEngine *)context;
    dma_engine_on_set_interrupt_bit(dma, bit);
}

// DMAReceiver interrupt callback: context is DMAEngine*, forward to onSetInterruptBit
static void dma_engine_rx_interrupt(void *context, uint8_t bit)
{
    DMAEngine *dma = (DMAEngine *)context;
    dma_engine_on_set_interrupt_bit(dma, bit);
}

void dma_engine_init(DMAEngine *dma, bool burst_mode, struct Device *hdlc_device, void *modem,
                     void *com5025)
{
    (void)burst_mode;
    if (!dma)
    {
        return;
    }

    memset(dma, 0, sizeof(DMAEngine));

    // Initialize DMA Control Blocks
    dma->dmaCB = malloc(sizeof(DMAControlBlocks));
    if (!dma->dmaCB)
    {
        return;
    }

    dmacb_init((DMAControlBlocks *)dma->dmaCB, hdlc_device);
    // burstMode parameter kept for API compatibility but always true

    // Store references
    dma->hdlcDevice = hdlc_device;
    dma->modem = (struct ModemState *)modem;
    dma->com5025 = (COM5025State *)com5025;

    // Initialize transmitter and receiver
    dma->transmitter = malloc(sizeof(DMATransmitter));
    dma->receiver = malloc(sizeof(DMAReceiver));

    if (dma->transmitter)
    {
        dma_tx_init(dma->transmitter, com5025, dma->dmaCB, hdlc_device);
        // Wire transmitter callbacks through DMAEngine forwarding wrappers
        dma_tx_set_send_frame_callback(dma->transmitter, dma_engine_tx_send_frame);
        dma_tx_set_interrupt_callback(dma->transmitter, dma_engine_tx_interrupt);
        dma->transmitter->callbackContext = dma;
    }

    if (dma->receiver)
    {
        dma_rx_init(dma->receiver, com5025, dma->dmaCB, hdlc_device);
        // Wire receiver interrupt callback through DMAEngine forwarding wrapper
        dma_rx_set_interrupt_callback(dma->receiver, dma_engine_rx_interrupt);
        dma->receiver->callbackContext = dma;
    }

    // Initialize state
    dma->enabled = false;
    dma->currentDMAAddress = 0;

    // Initialize parameter buffer and link to DMA control blocks
    dma_params_init(&dma->parameterBuffer);
    if (dma->dmaCB)
    {
        ((DMAControlBlocks *)dma->dmaCB)->parameters = &dma->parameterBuffer;
    }

    // Wire DMA control blocks memory access callbacks through DMAEngine forwarding wrappers
    if (dma->dmaCB)
    {
        dmacb_set_read_dma_callback(dma->dmaCB, dma_engine_cb_read_dma, dma);
        dmacb_set_write_dma_callback(dma->dmaCB, dma_engine_cb_write_dma, dma);
    }

    // Initialize DMA registers array
    memset(dma->dmaRegisters, 0, sizeof(dma->dmaRegisters));
}

void dma_engine_destroy(DMAEngine *dma)
{
    if (!dma)
    {
        return;
    }

    if (dma->transmitter)
    {
        dma_tx_destroy(dma->transmitter);
        free(dma->transmitter);
        dma->transmitter = NULL;
    }

    if (dma->receiver)
    {
        dma_rx_destroy(dma->receiver);
        free(dma->receiver);
        dma->receiver = NULL;
    }

    if (dma->dmaCB)
    {
        dmacb_destroy(dma->dmaCB);
        free(dma->dmaCB);
        dma->dmaCB = NULL;
    }
}

static void dma_engine_clear(DMAEngine *dma)
{
    if (!dma)
    {
        return;
    }

    // Clear transmitter and receiver
    if (dma->transmitter)
    {
        dma_tx_clear(dma->transmitter);
    }

    if (dma->receiver)
    {
        dma_rx_clear(dma->receiver);
    }

    if (dma->dmaCB)
    {
        dmacb_clear(dma->dmaCB);
    }
}

void dma_engine_tick(DMAEngine *dma)
{
    if (!dma)
    {
        return;
    }

    // RX MUST tick BEFORE TX so that incoming ACKs (RR frames) are delivered
    // to SINTRAN before SINTRAN's T1 timer fires and triggers retransmissions.
    if (dma->receiver)
    {
        dma_rx_tick(dma->receiver);
    }

    if (dma->transmitter)
    {
        dma_tx_tick(dma->transmitter);
    }
}

// Memory access functions - forward to callbacks

static int dma_engine_dma_read(DMAEngine *dma, uint32_t address)
{
    if (!dma || !dma->onReadDMA)
    {
        return -1;
    }

    int data = -1;
    dma->onReadDMA(dma->hdlcDevice, address, &data);
    return data;
}

static void dma_engine_dma_write(DMAEngine *dma, uint32_t address, uint16_t data)
{
    if (!dma || !dma->onWriteDMA)
    {
        return;
    }

    dma->onWriteDMA(dma->hdlcDevice, address, data);
}

// Event handling functions

static void dma_engine_on_set_interrupt_bit(DMAEngine *dma, uint8_t bit)
{
    if (!dma || !dma->onSetInterruptBit)
    {
        return;
    }

    dma->onSetInterruptBit(dma->hdlcDevice, bit);
}

void dma_engine_on_write_dma(DMAEngine *dma, uint32_t address, uint16_t data)
{
    if (!dma || !dma->onWriteDMA)
    {
        return;
    }

    dma->onWriteDMA(dma->hdlcDevice, address, data);
}

void dma_engine_on_read_dma(DMAEngine *dma, uint32_t address, int *data)
{
    if (!dma || !dma->onReadDMA || !data)
    {
        if (data)
        {
            *data = -1;
        }
        return;
    }

    dma->onReadDMA(dma->hdlcDevice, address, data);
}

// DMA Command execution - main dispatcher

void dma_engine_execute_command(DMAEngine *dma)
{
    // Note: This function is not currently used as the HDLC device
    // handles command dispatch directly. In the C# implementation,
    // this function was called by the HDLC device to execute commands.
    //
    // The C implementation handles command execution in device_hdlc.c
    // for better integration with the device register system.

    if (!dma)
    {
        return;
    }

    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_TRACE))
    {
        dma_engine_log(dma,
                       "DMAEngine_ExecuteCommand called (not implemented - see device_hdlc.c)");
    }

    // This function is intentionally not implemented as command execution
    // is handled by the HDLC device to maintain proper access to
    // device registers and state.
}

// DMA Command implementations

void dma_engine_command_device_clear(DMAEngine *dma)
{
    if (!dma)
    {
        return;
    }

    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
    {
        dma_engine_log(dma, "DMA Device Clear command");
    }

    // Clear all components
    dma_engine_clear(dma);
    dma->enabled = false; // allow COM5025 clocking for maintenance test

    if (dma->com5025)
    {
        com5025_reset(dma->com5025);
    }

    dma_engine_clear_dma_command(dma);
}

void dma_engine_command_initialize(DMAEngine *dma)
{
    if (!dma)
    {
        return;
    }

    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
    {
        dma_engine_log(dma, "DMA Initialize command");
    }

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

    if (dma_address == 0)
    {
        dma_engine_clear_dma_command(dma);
        return;
    }

    // Read parameter buffer from memory
    int parameter_control_register = dma_engine_dma_read(dma, dma_address++);
    int sync_address_register = dma_engine_dma_read(dma, dma_address++);
    int character_length = dma_engine_dma_read(dma, dma_address++);
    int displacement1 = dma_engine_dma_read(dma, dma_address++);
    int displacement2 = dma_engine_dma_read(dma, dma_address++);
    int max_receiver_block_length = dma_engine_dma_read(dma, dma_address++);
    int checksum = dma_engine_dma_read(dma, dma_address);

    // Store parameters in parameter buffer
    dma_params_set_control_register(&dma->parameterBuffer, parameter_control_register);
    dma_params_set_sync_address_register(&dma->parameterBuffer, sync_address_register);
    dma_params_set_character_length(&dma->parameterBuffer, character_length);
    dma_params_set_displacement_1(&dma->parameterBuffer, displacement1);
    dma_params_set_displacement_2(&dma->parameterBuffer, displacement2);
    dma_params_set_max_receiver_block_length(&dma->parameterBuffer, max_receiver_block_length);

    // Configure COM5025 with parameters
    if (dma->com5025)
    {
        com5025_write_byte(dma->com5025, COM5025_REG_BYTE_MODE_CONTROL,
                           (uint8_t)(parameter_control_register & 0xFF));
        com5025_write_byte(dma->com5025, COM5025_REG_BYTE_SYNC_ADDRESS,
                           (uint8_t)(sync_address_register & 0xFF));
        com5025_write_byte(dma->com5025, COM5025_REG_BYTE_DATA_LENGTH_SELECT,
                           (uint8_t)(character_length & 0xFF));
    }

    // Store displacement parameters in DMA registers
    dma->dmaRegisters[5] = (uint16_t)displacement1;
    dma->dmaRegisters[6] = (uint16_t)displacement2;
    dma->dmaRegisters[7] = (uint16_t)max_receiver_block_length;

    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
    {
        dma_engine_log(
            dma, "--------------------------------------------------------------------------");
        dma_engine_log(dma, "DMA CommandInitialize    : 0x%06X", dma->currentDMAAddress);
        dma_engine_log(dma, "ParameterControlRegister : 0x%04X", parameter_control_register);
        dma_engine_log(dma, "Sync_AddressRegister     : 0x%04X", sync_address_register);
        dma_engine_log(dma, "CharacterLength          : 0x%04X", character_length);
        dma_engine_log(dma, "Displacement1            : 0x%04X", displacement1);
        dma_engine_log(dma, "Displacement2            : 0x%04X", displacement2);
        dma_engine_log(dma, "MaxReceiverBlockLength   : 0x%04X", max_receiver_block_length);
        dma_engine_log(
            dma, "--------------------------------------------------------------------------");
    }

    // Write back checksum if current checksum is 0
    if (checksum == 0)
    {
        dma_engine_dma_write(dma, dma_address, 0x8474); // 0102164 octal = 0x8474 hex
    }

    // DMA is now initialized - COM5025 clocking can be stopped
    // (burst mode handles all framing via DMA engine + HDLCFrame)
    dma->enabled = true;

    dma_engine_clear_dma_command(dma);
}

void dma_engine_command_receiver_start(DMAEngine *dma)
{
    if (!dma)
    {
        return;
    }

    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
    {
        dma_engine_log(dma, "DMA Receiver Start command");
    }

    // Set RX pointer to current DMA address
    if (dma->dmaCB)
    {
        dmacb_set_rx_pointer(dma->dmaCB, dma->currentDMAAddress, 0);
    }

    if (dma->receiver)
    {
        dma_rx_set_receiver_state(dma->receiver);
    }

    dma_engine_clear_dma_command(dma);
}

void dma_engine_command_receiver_continue(DMAEngine *dma)
{
    if (!dma)
    {
        return;
    }

    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
    {
        dma_engine_log(dma, "DMA Receiver Continue command");
    }

    // Set RX pointer to current DMA address
    if (dma->dmaCB)
    {
        dmacb_set_rx_pointer(dma->dmaCB, dma->currentDMAAddress, 0);
    }

    if (dma->receiver)
    {
        dma_rx_set_receiver_state(dma->receiver);
    }

    dma_engine_clear_dma_command(dma);
}

void dma_engine_command_transmitter_start(DMAEngine *dma)
{
    if (!dma)
    {
        return;
    }

    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
    {
        dma_engine_log(dma, "DMA Transmitter Start command");
    }

    // Set TX pointer to current DMA address
    if (dma->dmaCB)
    {
        dmacb_set_tx_pointer(dma->dmaCB, dma->currentDMAAddress, 0);
        dmacb_debug_tx_frames(dma->dmaCB);
    }

    if (dma->transmitter)
    {
        dma_tx_set_sender_state(dma->transmitter, DMA_SENDER_BLOCK_READY_TO_SEND);
    }

    dma_engine_clear_dma_command(dma);
}

void dma_engine_command_dump_data_module(DMAEngine *dma)
{
    if (!dma)
    {
        return;
    }

    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
    {
        dma_engine_log(dma, "DMA Dump Data Module command");
    }

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

    if (dma_address == 0)
    {
        dma_engine_clear_dma_command(dma);
        return;
    }

    if (dma->com5025)
    {
        uint16_t data;

        // 1. Parameter Control Register
        data = com5025_read_byte(dma->com5025, COM5025_REG_BYTE_MODE_CONTROL);
        dma_engine_dma_write(dma, dma_address++, (uint16_t)data);

        // 2. Sync/Address Register
        data = com5025_read_byte(dma->com5025, COM5025_REG_BYTE_SYNC_ADDRESS);
        dma_engine_dma_write(dma, dma_address++, (uint16_t)data);

        // 3. Character Length
        data = com5025_read_byte(dma->com5025, COM5025_REG_BYTE_DATA_LENGTH_SELECT);
        dma_engine_dma_write(dma, dma_address++, (uint16_t)data);

        // 4. Receiver Status Register
        data = com5025_read_byte(dma->com5025, COM5025_REG_BYTE_RECEIVER_STATUS);
        dma_engine_dma_write(dma, dma_address++, (uint16_t)data);

        // OR the Receiver Status Register into the Receiver Dataflow Status Register to prevent loss of information
        if (dma->onUpdateReceiverStatus)
        {
            dma->onUpdateReceiverStatus(dma->hdlcDevice, data);
        }

        // 5. Transmitter Status Register
        data = com5025_read_byte(dma->com5025, COM5025_REG_BYTE_TRANSMITTER_STATUS_CONTROL);
        dma_engine_dma_write(dma, dma_address++, (uint16_t)data);
    }

    dma_engine_clear_dma_command(dma);
}

void dma_engine_command_dump_registers(DMAEngine *dma)
{
    if (!dma)
    {
        return;
    }

    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
    {
        dma_engine_log(dma, "DMA Dump Registers command");
    }

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

    if (dma_address == 0)
    {
        dma_engine_clear_dma_command(dma);
        return;
    }

    uint16_t first_reg = (uint16_t)(dma_engine_dma_read(dma, dma_address++) & 0x00FF);
    uint16_t numreg = (uint16_t)(dma_engine_dma_read(dma, dma_address++) & 0x00FF);

    if ((first_reg == 0) && (numreg == 0))
    {
        // If both values are zero, the contents of the 16 registers in the Bit Slice are written into memory
        for (uint8_t i = 0; i < 16; i++)
        {
            uint16_t data = i; // Basic register index for bit slice
            dma_engine_dma_write(dma, dma_address++, data);
            if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
            {
                dma_engine_log(dma, "DUMP BIT SLICE REGISTER %d = 0x%04X", i, data);
            }
        }
    }
    else
    {
        for (uint16_t i = 0; i < numreg; i++)
        {
            int offset = first_reg + i;
            if (offset < 256)
            {
                uint16_t data = dma->dmaRegisters[offset];
                dma_engine_dma_write(dma, dma_address++, data);
                if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
                {
                    dma_engine_log(dma, "DUMP REGISTER %d = 0x%04X", offset, data);
                }
            }
        }
    }

    dma_engine_clear_dma_command(dma);
}

void dma_engine_command_load_registers(DMAEngine *dma)
{
    if (!dma)
    {
        return;
    }

    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
    {
        dma_engine_log(dma, "DMA Load Registers command");
    }

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

    if (dma_address == 0)
    {
        dma_engine_clear_dma_command(dma);
        return;
    }

    uint16_t first_reg = (uint16_t)(dma_engine_dma_read(dma, dma_address++) & 0x00FF);
    uint16_t numreg = (uint16_t)(dma_engine_dma_read(dma, dma_address++) & 0x00FF);

    for (uint16_t i = 0; i < numreg; i++)
    {
        int offset = first_reg + i;
        if (offset < 256)
        {
            int read_val = dma_engine_dma_read(dma, dma_address++); // returns -1 if it fails
            if (read_val >= 0)
            {
                dma->dmaRegisters[offset] = (uint16_t)read_val;
                if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
                {
                    dma_engine_log(dma, "LOAD REGISTER %d = 0x%04X", offset, read_val);
                }
            }
        }
    }

    dma_engine_clear_dma_command(dma);
}

// Utility functions

void dma_engine_set_dma_address(DMAEngine *dma, uint32_t address)
{
    if (!dma)
    {
        return;
    }
    dma->currentDMAAddress = address;
}

static void dma_engine_clear_dma_command(DMAEngine *dma)
{
    if (!dma)
    {
        return;
    }

    // Clear DMA command via callback to HDLC device
    if (dma->onClearCommand)
    {
        dma->onClearCommand(dma->hdlcDevice);
    }
}

uint16_t dma_engine_get_buffer_key_vault(DMAEngine *dma, uint32_t list_pointer, uint16_t offset)
{
    if (!dma)
    {
        return 0;
    }

    uint32_t current_list_pointer = list_pointer + (uint32_t)(offset * 4);
    uint16_t key_value = (uint16_t)dma_engine_dma_read(dma, current_list_pointer);
    return key_value;
}

uint32_t dma_engine_scan_next_tx_buffer(DMAEngine *dma, uint32_t start)
{
    if (!dma)
    {
        return 0;
    }

    uint32_t next_mem = start;

    while (true)
    {
        int memkey = dma_engine_dma_read(dma, next_mem);
        if (memkey == 0)
        {
            return 0; // end of pointers
        }

        KeyFlags key = (KeyFlags)(memkey)&KEYFLAG_MASK_KEY;

        if (key == KEYFLAG_BLOCK_TO_BE_TRANSMITTED)
        {
            return next_mem;
        }
        if (key == KEYFLAG_NEW_LIST_POINTER)
        {
            return next_mem;
        }

        next_mem += 4; // next memory
    }

    return 0;
}

// Callback setup functions

void dma_engine_set_write_dma_callback(DMAEngine *dma, DMAWriteCallback callback)
{
    if (!dma)
    {
        return;
    }
    dma->onWriteDMA = callback;
}

void dma_engine_set_read_dma_callback(DMAEngine *dma, DMAReadCallback callback)
{
    if (!dma)
    {
        return;
    }
    dma->onReadDMA = callback;
}

void dma_engine_set_interrupt_callback(DMAEngine *dma, DMASetInterruptCallback callback)
{
    if (!dma)
    {
        return;
    }
    dma->onSetInterruptBit = callback;
}

void dma_engine_set_send_frame_callback(DMAEngine *dma, DMASendFrameCallback callback)
{
    if (!dma)
    {
        return;
    }
    dma->onSendHDLCFrame = callback;
}

void dma_engine_set_update_receiver_status_callback(DMAEngine *dma,
                                                    DMAUpdateReceiverStatusCallback callback)
{
    if (!dma)
    {
        return;
    }
    dma->onUpdateReceiverStatus = callback;
}

void dma_engine_set_clear_command_callback(DMAEngine *dma, DMAClearCommandCallback callback)
{
    if (!dma)
    {
        return;
    }
    dma->onClearCommand = callback;
}

// Debug functions

static void dma_engine_log(DMAEngine *dma, const char *format, ...)
{
    if (!dma || !format)
    {
        return;
    }
    if (!Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
    {
        return;
    }

    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    log_write(LOG_CAT_HDLC, LOG_DEBUG, "DMAEngine: %s", buffer);
}
