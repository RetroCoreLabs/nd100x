/*
 * chip_com5025.c - COM5025 multi-protocol communications chip emulation used by HDLC.
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

#include "chip_com5025.h"

#include <string.h>
#include <stdio.h>

#include "chip_com5025_registers.h"
#include "hdlc_crc.h"
static void com5025_process_bit(COM5025State *, bool);


// Static function declarations
static void com5025_set_output_pin(COM5025State *chip, COM5025SignalPinOut pin, bool value);
static void com5025_clear_all_input_pins(COM5025State *chip);
static void com5025_set_transmitter_buffer_empty(COM5025State *chip);
static void com5025_clear_transmitter_buffer_empty(COM5025State *chip);
static void com5025_write_transmitter_data_buffer(COM5025State *chip, uint8_t data);
static void com5025_move_data_buffer_to_shift_register(COM5025State *chip);
static void com5025_write_data_to_shift_register(COM5025State *chip, uint8_t data);
static void com5025_transmit_byte_output(COM5025State *chip, uint8_t data, bool is_data);
static void com5025_send_one_byte(COM5025State *chip, uint8_t data);
static bool com5025_shift_register_empty(COM5025State *chip);
static uint8_t com5025_map_bits_to_character_length(uint8_t bits);
// Global state for the chip (could be per-device instance)
static COM5025Registers registers;

void COM5025_Init(COM5025State *chip)
{
    if (!chip)
    {
        return;
    }

    memset(chip, 0, sizeof(COM5025State));
    COM5025Registers_Init(&registers);

    chip->mode = COM5025_MODE_BOP;
    chip->characterLength = 8;
    chip->crcRegister = 0xFFFF;

    // Initialize register pointer
    chip->registers = &registers;
}

void COM5025_Reset(COM5025State *chip)
{
    if (!chip)
    {
        return;
    }

    // RESET SIGNAL
    // This input should be pulsed high after power turn on. This will: clear all flags, and
    // status conditions, set TBMT = 1, TSO = 1 and place the device in the primary
    // BOP mode with 8 bit TX/ RX data length, CRC CCITT initialized to all 1's.

    COM5025Registers_Clear(&registers);

    // Clear input pins
    com5025_clear_all_input_pins(chip);

    // RSI stays 0 after ClearAllInputPins - matches C# exactly

    // Transmitter buffer empty
    com5025_set_transmitter_buffer_empty(chip);

    // Transmitter Serial Output => "MARK" (high)
    com5025_set_output_pin(chip, COM5025_PIN_OUT_TSO, true);
}

static void com5025_master_reset(COM5025State *chip)
{
    if (!chip)
    {
        return;
    }

    COM5025_Reset(chip);

    chip->inputPins[COM5025_PIN_IN_MR] = false;
    chip->inputPins[COM5025_PIN_IN_RXENA] = false;
    chip->inputPins[COM5025_PIN_IN_TXENA] = false;
    chip->inputPins[COM5025_PIN_IN_MSEL] = false;
}

uint8_t COM5025_ReadByte(COM5025State *chip, COM5025RegistersByte reg)
{
    if (!chip)
    {
        return 0;
    }

    uint8_t data = 0;

    switch (reg)
    {
    case COM5025_REG_BYTE_RECEIVER_DATA_BUFFER:
        data = (uint8_t)registers.receiverDataBuffer;
        com5025_set_output_pin(chip, COM5025_PIN_OUT_RDA,
                               false); // turn off "Receiver Data Available"
        break;

    case COM5025_REG_BYTE_RECEIVER_STATUS:
        data = (uint8_t)(registers.receiverStatus >> 8);
        COM5025_SetReceiverStatus(chip,
                                  registers.receiverStatus & COM5025_RX_STATUS_MASK_CLEAR_ON_RSR);
        com5025_set_output_pin(chip, COM5025_PIN_OUT_RSA, false);
        break;

    case COM5025_REG_BYTE_TRANSMITTER_DATA:
        data = registers.transmitterDataBuffer;
        break;

    case COM5025_REG_BYTE_TRANSMITTER_STATUS_CONTROL:
        data = (uint8_t)(registers.txStatusAndControl >> 8);
        break;

    case COM5025_REG_BYTE_SYNC_ADDRESS:
        data = registers.syncSecondaryAddress;
        break;

    case COM5025_REG_BYTE_MODE_CONTROL:
        data = (uint8_t)(registers.modeControl >> 8);
        break;

    case COM5025_REG_BYTE_NOT_USED:
        data = 0;
        break;

    case COM5025_REG_BYTE_DATA_LENGTH_SELECT:
        data = (uint8_t)(registers.dataLengthSelect >> 8);
        break;

    default:
        break;
    }

    return data;
}

void COM5025_WriteByte(COM5025State *chip, COM5025RegistersByte reg, uint8_t value)
{
    if (!chip)
    {
        return;
    }

    switch (reg)
    {
    case COM5025_REG_BYTE_RECEIVER_DATA_BUFFER:
        // The Receiver Data Buffer is a read-only register
        break;

    case COM5025_REG_BYTE_RECEIVER_STATUS:
        // Register is Read only!
        break;

    case COM5025_REG_BYTE_TRANSMITTER_DATA:
        com5025_write_transmitter_data_buffer(chip, value);
        break;

    case COM5025_REG_BYTE_TRANSMITTER_STATUS_CONTROL:
        // TERR bit is READ ONLY
        value &= 0x7F;
        if (registers.txStatusAndControl & COM5025_TX_STATUS_TERR)
        {
            value |= 1 << 7;
        }

        registers.txStatusAndControl = (uint16_t)value << 8;

        if (registers.txStatusAndControl & COM5025_TX_STATUS_TSOM)
        {
            com5025_set_output_pin(chip, COM5025_PIN_OUT_TSA, false); // Clear underflow
            registers.txStatusAndControl &= ~COM5025_TX_STATUS_TERR;
        }
        break;

    case COM5025_REG_BYTE_SYNC_ADDRESS:
        registers.syncSecondaryAddress = value;
        break;

    case COM5025_REG_BYTE_MODE_CONTROL:
        COM5025Registers_SetModeControl(&registers, (uint16_t)value << 8);
        break;

    case COM5025_REG_BYTE_NOT_USED:
        break;

    case COM5025_REG_BYTE_DATA_LENGTH_SELECT:
        registers.dataLengthSelect = (uint16_t)value << 8;
        registers.txdl = com5025_map_bits_to_character_length((value >> 5) & 0x07);
        registers.rxdl = com5025_map_bits_to_character_length(value & 0x07);
        break;

    default:
        break;
    }
}

uint16_t COM5025_ReadWord(COM5025State *chip, COM5025RegistersWord reg)
{
    if (!chip)
    {
        return 0;
    }

    uint16_t data = 0;

    switch (reg)
    {
    case COM5025_REG_WORD_RECEIVER_STATUS:
        data = (uint16_t)(registers.receiverStatus | registers.receiverDataBuffer);
        com5025_set_output_pin(chip, COM5025_PIN_OUT_RDA,
                               false); // turn off "Receiver Data Available"
        com5025_set_output_pin(chip, COM5025_PIN_OUT_RSA,
                               false); // turn off "Receiver Status Available"
        break;

    case COM5025_REG_WORD_TRANSMITTER_STATUS:
        data = (uint16_t)(registers.txStatusAndControl | (uint8_t)registers.transmitterDataBuffer);
        break;

    case COM5025_REG_WORD_MODE_CONTROL_SYNC_ADDRESS:
        data = (uint16_t)(registers.modeControl | registers.syncSecondaryAddress);
        break;

    case COM5025_REG_WORD_DATA_LENGTH_SELECT:
        data = registers.dataLengthSelect;
        break;

    default:
        break;
    }

    return data;
}

void COM5025_WriteWord(COM5025State *chip, COM5025RegistersWord reg, uint16_t value)
{
    if (!chip)
    {
        return;
    }

    uint8_t lo = (uint8_t)(value & 0xFF);
    uint16_t hi = value & 0xFFFF;

    switch (reg)
    {
    case COM5025_REG_WORD_RECEIVER_STATUS:
        // It's read-only, so do nothing here
        break;

    case COM5025_REG_WORD_TRANSMITTER_STATUS:
        com5025_write_transmitter_data_buffer(chip, lo);

        // TERR bit is READ ONLY - make sure it doesn't change
        uint16_t flags = hi;
        if (registers.txStatusAndControl & COM5025_TX_STATUS_TERR)
        {
            flags |= COM5025_TX_STATUS_TERR;
        }
        else
        {
            flags &= ~COM5025_TX_STATUS_TERR;
        }

        registers.txStatusAndControl = flags;
        break;

    case COM5025_REG_WORD_MODE_CONTROL_SYNC_ADDRESS:
        registers.syncSecondaryAddress = lo;
        COM5025Registers_SetModeControl(&registers, hi);
        break;

    case COM5025_REG_WORD_DATA_LENGTH_SELECT:
        registers.dataLengthSelect = hi;
        break;
    default:
        break;
    }
}

void COM5025_SetInputPin(COM5025State *chip, COM5025SignalPinIn pin, bool value)
{
    if (!chip || pin >= COM5025_MAX_IN_PINS)
    {
        return;
    }

    bool old_value = chip->inputPins[pin];
    chip->inputPins[pin] = value;

    switch (pin)
    {
    case COM5025_PIN_IN_MR:
        if (value && !old_value)
        {
            com5025_master_reset(chip);
        }
        break;

    case COM5025_PIN_IN_RXENA:
        if (!value && old_value)
        {
            // Disable receiver
            // A low level disables the RDP and resets RDA, RSA and RXACT.
            com5025_set_output_pin(chip, COM5025_PIN_OUT_RDA, false);
            com5025_set_output_pin(chip, COM5025_PIN_OUT_RSA, false);
            com5025_set_output_pin(chip, COM5025_PIN_OUT_RXACT, false);

            COM5025_SetReceiverStatus(chip, registers.receiverStatus &
                                                COM5025_RX_STATUS_MASK_CLEAR_ON_RECEIVER_DISABLE);
        }
        break;

    case COM5025_PIN_IN_TXENA:
        if (value)
        {
            com5025_set_output_pin(chip, COM5025_PIN_OUT_TXACT, true);
        }
        else
        {
            com5025_set_output_pin(chip, COM5025_PIN_OUT_TXACT, false);
        }
        break;

    case COM5025_PIN_IN_MSEL:
        chip->maintenanceMode = value;
        break;

    case COM5025_PIN_IN_RCP:
    case COM5025_PIN_IN_RSI:
        break;
    default:
        break;
    }
}

bool COM5025_GetInputPin(COM5025State *chip, COM5025SignalPinIn pin)
{
    if (!chip || pin >= COM5025_MAX_IN_PINS)
    {
        return false;
    }
    return chip->inputPins[pin];
}

bool COM5025_GetOutputPin(COM5025State *chip, COM5025SignalPinOut pin)
{
    if (!chip || pin >= COM5025_MAX_OUT_PINS)
    {
        return false;
    }
    return chip->outputPins[pin];
}

void COM5025_ClockReceiver(COM5025State *chip)
{
    if (!chip || !chip->inputPins[COM5025_PIN_IN_RXENA])
    {
        return;
    }

    bool bit = chip->inputPins[COM5025_PIN_IN_RSI];
    com5025_process_bit(chip, bit);
}

void COM5025_ClockTransmitter(COM5025State *chip)
{
    if (!chip || !chip->inputPins[COM5025_PIN_IN_TXENA])
    {
        return;
    }

    // Transmitter is enabled AND active
    if (chip->inputPins[COM5025_PIN_IN_TXENA] && chip->outputPins[COM5025_PIN_OUT_TXACT])
    {
        if (com5025_shift_register_empty(chip))
        { // TSR is empty
            // Do we have data to fill up the TSR? Is transmitbufferEmpty = False?
            if (!chip->outputPins[COM5025_PIN_OUT_TBMT])
            {
                com5025_move_data_buffer_to_shift_register(
                    chip); // also sets TransmitBufferEmpty = true
            }
        }

        if (registers.transmitterShiftRegisterBit > 0)
        {
            // Check for five consecutive 1s for bit stuffing
            bool found5ones = (registers.tsrCountOnes == 5);

            // Send LSB
            bool transmit_serial_output_pin = (registers.transmitterShiftRegister & 1) != 0;

            // If we are sending DATA, bit-stuffing is enabled. When sending Flags not.
            if (registers.tsrEnableBitStuffing)
            {
                if (transmit_serial_output_pin)
                {
                    registers.tsrCountOnes++;
                }
                else
                {
                    registers.tsrCountOnes = 0;
                }
            }

            if (found5ones)
            {
                // Send a Zero!
                registers.tsrCountOnes = 0;
                com5025_set_output_pin(chip, COM5025_PIN_OUT_TSO, false);
            }
            else
            {
                // Normal shift
                com5025_set_output_pin(chip, COM5025_PIN_OUT_TSO, transmit_serial_output_pin);

                // shift TSR
                registers.transmitterShiftRegister =
                    (uint8_t)(registers.transmitterShiftRegister >> 1);

                // Reduce remaining number of bits to send
                registers.transmitterShiftRegisterBit--;

                if (registers.transmitterShiftRegisterBit == 0)
                {
                    // Clear shift register
                    registers.transmitterShiftRegister = 0;

                    // trigger DMA after SYN byte has been sent
                    if (!registers.tsrEnableBitStuffing)
                    {
                        com5025_set_output_pin(chip, COM5025_PIN_OUT_TBMT, true);
                    }
                }
            }
        }
        else
        {
            // Transmitter is not enabled or active: TSO => "MARK" (high)
            com5025_set_output_pin(chip, COM5025_PIN_OUT_TSO, true);
        }
    }

    chip->clockCounter++;
}

static void com5025_process_bit(COM5025State *chip, bool bit)
{
    if (!chip)
    {
        return;
    }

    chip->receiverShiftRegister = (chip->receiverShiftRegister << 1) | (bit ? 1 : 0);
    chip->bitCounter++;

    if (chip->mode == COM5025_MODE_BOP)
    {
        if ((chip->receiverShiftRegister & 0xFF) == HDLC_FRAME_DELIMITER)
        {
            chip->flagDetected = true;
            com5025_set_output_pin(chip, COM5025_PIN_OUT_SFR, true);

            if (chip->bitCounter >= chip->characterLength)
            {
                chip->receiverStatusRegister |= COM5025_RX_STATUS_REOM;
                com5025_set_output_pin(chip, COM5025_PIN_OUT_RSA, true);
            }

            chip->bitCounter = 0;
        }
        else if ((chip->receiverShiftRegister & 0xFF) == HDLC_GO_AHEAD)
        {
            chip->abortDetected = true;
            chip->receiverStatusRegister |= COM5025_RX_STATUS_RAB_GA;
            com5025_set_output_pin(chip, COM5025_PIN_OUT_RSA, true);
            chip->bitCounter = 0;
        }
    }

    if (chip->bitCounter >= chip->characterLength)
    {
        // In BOP mode, only assemble characters AFTER a FLAG has been detected.
        // Before FLAG, the receiver is in hunt mode - bits are discarded.
        if (chip->mode != COM5025_MODE_BOP || chip->flagDetected)
        {
            chip->receiverDataBuffer =
                chip->receiverShiftRegister & ((1 << chip->characterLength) - 1);
            com5025_set_output_pin(chip, COM5025_PIN_OUT_RDA, true);
            com5025_set_output_pin(chip, COM5025_PIN_OUT_RXACT, true);
        }

        chip->bitCounter = 0;
        chip->receiverShiftRegister = 0;
    }
}

void COM5025_ReceiveData(COM5025State *chip, const uint8_t *data, int length)
{
    if (!chip || !data || length <= 0)
    {
        return;
    }

    for (int i = 0; i < length; i++)
    {
        uint8_t byte = data[i];
        COM5025Registers_QueueReceivedData(&registers, byte);
    }
}

void COM5025_TransmitData(COM5025State *chip, uint8_t data)
{
    if (!chip)
    {
        return;
    }

    chip->transmitterDataRegister = data;
    chip->transmitterShiftRegister = data;
    chip->bitCounter = chip->characterLength;
    com5025_set_output_pin(chip, COM5025_PIN_OUT_TBMT, false);
    com5025_set_output_pin(chip, COM5025_PIN_OUT_TXACT, true);
}

// Static helper functions

static void com5025_set_output_pin(COM5025State *chip, COM5025SignalPinOut pin, bool value)
{
    if (!chip || pin >= COM5025_MAX_OUT_PINS)
    {
        return;
    }

    bool old_value = chip->outputPins[pin];
    chip->outputPins[pin] = value;

    // Notify callback of pin value change
    if (old_value != value && chip->onPinValueChanged)
    {
        chip->onPinValueChanged(chip->callbackContext, pin, value);
    }
}

static void com5025_clear_all_input_pins(COM5025State *chip)
{
    if (!chip)
    {
        return;
    }
    for (int i = 0; i < COM5025_MAX_IN_PINS; i++)
    {
        chip->inputPins[i] = false;
    }
}

static void com5025_set_transmitter_buffer_empty(COM5025State *chip)
{
    if (!chip)
    {
        return;
    }
    // let host know Transmit buffer is empty and ready for filling
    com5025_set_output_pin(chip, COM5025_PIN_OUT_TBMT, true);
}

static void com5025_clear_transmitter_buffer_empty(COM5025State *chip)
{
    if (!chip)
    {
        return;
    }
    // Transmitter buffer NOT empty anymore (we have data!)
    com5025_set_output_pin(chip, COM5025_PIN_OUT_TBMT, false);
}

static void com5025_write_transmitter_data_buffer(COM5025State *chip, uint8_t data)
{
    if (!chip)
    {
        return;
    }

    registers.transmitterDataBuffer = data;

    // When you write a byte to the Transmitter Data Buffer (TDB), the TBMT signal is cleared
    // TBMT = 0 on any write access to TDB or 'TX Status and Control Register'
    com5025_clear_transmitter_buffer_empty(chip);
}

static void com5025_move_data_buffer_to_shift_register(COM5025State *chip)
{
    if (!chip)
    {
        return;
    }

    com5025_write_data_to_shift_register(chip, registers.transmitterDataBuffer);
    registers.transmitterDataBuffer = 0;

    // Tell host that TDB is empty and ready to accept the next byte
    com5025_set_transmitter_buffer_empty(chip);
}

static void com5025_write_data_to_shift_register(COM5025State *chip, uint8_t data)
{
    if (!chip)
    {
        return;
    }

    // Transmit data for the BYTE oriented connection, and calculate CRC
    com5025_transmit_byte_output(chip, data, true);

    // Shift register logic starts here
    registers.transmitterShiftRegister = data;
    registers.transmitterShiftRegisterBit = 8;
    registers.tsrCountOnes = 0;
    registers.tsrEnableBitStuffing = true; // enable bit stuffing
}

static void com5025_transmit_byte_output(COM5025State *chip, uint8_t data, bool is_data)
{
    if (!chip)
    {
        return;
    }

    if (is_data)
    {
        COM5025Registers_AggregateTXCrc(&registers, data);
    }

    // if we are sending data, we might need to do byte stuffing
    if (is_data)
    {
        // If the byte is a control octet (Frame Boundary or Escape Octet), it needs to be escaped
        if ((COM5025Registers_IsProtocolModeCCP(&registers) == false) &&
            (data == HDLC_FRAME_DELIMITER || data == HDLC_ASYNC_ESCAPE_OCTET))
        {
            // Send Escape Octet
            com5025_send_one_byte(chip, HDLC_ASYNC_ESCAPE_OCTET);

            // Send the data byte with bit 5 inverted
            com5025_send_one_byte(chip, (uint8_t)(data ^ HDLC_ASYNC_INVERT_OCTET));
        }
        else
        {
            // For any other data, send as-is
            com5025_send_one_byte(chip, data);
        }
    }
    else
    {
        com5025_send_one_byte(chip, data);
    }
}

static void com5025_send_one_byte(COM5025State *chip, uint8_t data)
{
    if (!chip)
    {
        return;
    }

    // Loopback?
    if (chip->inputPins[COM5025_PIN_IN_MSEL])
    { // maintenance mode?
        COM5025Registers_QueueReceivedData(&registers, data);
    }

    // Send it! (even if maintenance mode is enabled, we want to know the output)
    if (chip->onTransmitterOutput)
    {
        chip->onTransmitterOutput(chip->callbackContext, data);
    }
}

static bool com5025_shift_register_empty(COM5025State *chip)
{
    if (!chip)
    {
        return true;
    }
    return registers.transmitterShiftRegisterBit == 0;
}

static uint8_t com5025_map_bits_to_character_length(uint8_t bits)
{
    if (bits == 0)
    {
        return 8;
    }
    else
    {
        return bits;
    }
}

void COM5025_SetReceiverStatus(COM5025State *chip, uint16_t new_rx_status)
{
    if (!chip)
    {
        return;
    }

    // Apply the mask to ignore RSOM in both statuses
    uint16_t masked_original_status = registers.receiverStatus & ~COM5025_RX_STATUS_RSOM;
    uint16_t masked_new_status = new_rx_status & ~COM5025_RX_STATUS_RSOM;

    // Find bits that were 0 and are now 1, excluding RSOM
    uint16_t bits_from0_to1 = (~masked_original_status & masked_new_status);

    COM5025Registers_SetReceiverStatus(&registers, new_rx_status);

    // Do we have bits going to 1?
    if (bits_from0_to1 != 0)
    {
        com5025_set_output_pin(chip, COM5025_PIN_OUT_RSA, true); // trigger RSA
    }
}

// Callback setup functions
void COM5025_SetTransmitterOutputCallback(COM5025State *chip,
                                          void (*callback)(void *context, uint8_t data),
                                          void *context)
{
    if (!chip)
    {
        return;
    }
    chip->onTransmitterOutput = callback;
    chip->callbackContext = context;
}

void COM5025_SetPinValueChangedCallback(COM5025State *chip,
                                        void (*callback)(void *context, COM5025SignalPinOut pin,
                                                         bool value),
                                        void *context)
{
    if (!chip)
    {
        return;
    }
    chip->onPinValueChanged = callback;
    chip->callbackContext = context;
}
