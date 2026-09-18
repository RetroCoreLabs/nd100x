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

#ifndef CHIP_COM5025_H
#define CHIP_COM5025_H

#include <stdint.h>
#include <stdbool.h>
#include "hdlc_constants.h"

// Register address selection where "BYTE OP" = 0 (data port 16 bits wide)
// Ignores A0
typedef enum {
    COM5025_REG_WORD_RECEIVER_STATUS = 0,
    COM5025_REG_WORD_TRANSMITTER_STATUS = 1,
    COM5025_REG_WORD_MODE_CONTROL_SYNC_ADDRESS = 2,
    COM5025_REG_WORD_DATA_LENGTH_SELECT = 3
} COM5025RegistersWord;

// Register address selection where "BYTE OP" = 1 (8 bit data)
typedef enum {
    COM5025_REG_BYTE_RECEIVER_DATA_BUFFER = 0,
    COM5025_REG_BYTE_RECEIVER_STATUS = 1,
    COM5025_REG_BYTE_TRANSMITTER_DATA = 2,
    COM5025_REG_BYTE_TRANSMITTER_STATUS_CONTROL = 3,
    COM5025_REG_BYTE_SYNC_ADDRESS = 4,
    COM5025_REG_BYTE_MODE_CONTROL = 5,
    COM5025_REG_BYTE_NOT_USED = 6,
    COM5025_REG_BYTE_DATA_LENGTH_SELECT = 7
} COM5025RegistersByte;

// Receiver Status (Read only)
typedef enum {
    // RSOM - Receiver Start of Message (read-only)
    // BOP: Set when a FLAG followed by a non-FLAG has been received
    // and the latter character matches the secondary station address if SAM = 1.
    COM5025_RX_STATUS_RSOM = 1 << 8,

    // REOM - Receiver End of Message (read only)
    // BOP: Set when the closing FLAG is detected and the last data character
    // is loaded into RxDB or when an ABORT/GA character is received.
    COM5025_RX_STATUS_REOM = 1 << 9,

    // RAB_GA - Received ABORT or GO AHEAD character (read only)
    // BOP: Set when the receiver senses an ABORT character if SS/GA = 0
    // or a GA character if SS/GA = 1.
    COM5025_RX_STATUS_RAB_GA = 1 << 10,

    // ROR - Receiver Overrun (RX overflow)
    // Set when processor has not read the last character in the RxDR
    // within one character time. Subsequent characters will be lost.
    COM5025_RX_STATUS_ROR = 1 << 11,

    // ABC - Assembled Bit Count (bits 12-14)
    // BOP: Specifies the number of bits in the last received data character
    // of a message. Examine when REOM = 1.
    COM5025_RX_STATUS_ABC_A = 1 << 12,
    COM5025_RX_STATUS_ABC_B = 1 << 13,
    COM5025_RX_STATUS_ABC_C = 1 << 14,

    // RERR - Error Check (read only)
    // BOP: RERR = 1 indicates FCS error (CRC != F0B8)
    //      RERR = 0 indicates FCS received correctly (CRC = F0B8)
    COM5025_RX_STATUS_ERR_CHK = 1 << 15,

    // Masks for clearing flags
    COM5025_RX_STATUS_MASK_CLEAR_ON_RSR = ~(COM5025_RX_STATUS_REOM | COM5025_RX_STATUS_RAB_GA |
                                           COM5025_RX_STATUS_ROR | COM5025_RX_STATUS_ABC_A |
                                           COM5025_RX_STATUS_ABC_B | COM5025_RX_STATUS_ABC_C),

    COM5025_RX_STATUS_MASK_CLEAR_ON_RECEIVER_DISABLE = ~(COM5025_RX_STATUS_REOM | COM5025_RX_STATUS_RAB_GA |
                                                         COM5025_RX_STATUS_ROR | COM5025_RX_STATUS_ABC_A |
                                                         COM5025_RX_STATUS_ABC_B | COM5025_RX_STATUS_ABC_C)
} COM5025ReceiverStatusFlags;

// TX Status and Control (Read/Write)
typedef enum {
    // TSOM - Transmitter Start of Message (W/R bit)
    // Provided TXENA=1, TSOM initiates start of message.
    // In BOP: TSOM=1 generates FLAG and continues to send FLAG's until TSOM=0, then begin data.
    COM5025_TX_STATUS_TSOM = 1 << 8,

    // TEOM - Transmit End of Message (W/R bit)
    // Used to terminate a message.
    // In BOP mode, TEOM = 1 sends CRC, then FLAG
    COM5025_TX_STATUS_TEOM = 1 << 9,

    // TXAB - Transmitter Abort (W/R bit)
    // In BOP mode only, TXAB=1 finish present character then transmit ABORT or FLAG
    COM5025_TX_STATUS_TXAB = 1 << 10,

    // TXGA - Transmit Go Ahead (W/R bit)
    // In BOP mode only, modifies character called for by TEOM.
    // GA sent in place of FLAG. Allows loop termination-GA character.
    COM5025_TX_STATUS_TXGA = 1 << 11,

    // TERR - Transmitter Error (read only bit)
    // Underflow, set high when TDB not loaded in time to maintain continuous transmission.
    COM5025_TX_STATUS_TERR = 1 << 15
} COM5025TXStatusFlags;

// Mode Control (Read/Write)
typedef enum {
    // CRC SELECT bits X, Y and Z = Error Control Mode
    COM5025_MODE_CONTROL_X = 1 << 8,  // CRC SELECT X
    COM5025_MODE_CONTROL_Y = 1 << 9,  // CRC SELECT Y
    COM5025_MODE_CONTROL_Z = 1 << 10, // CRC SELECT Z

    // IDLE MODE SELECTION
    // Determines line fill character to be used if transmitter underrun occurs
    COM5025_MODE_CONTROL_IDLE = 1 << 11,

    // SELECT SECONDARY ADDRESS MODE
    // Used in ring networks to select secondary station
    COM5025_MODE_CONTROL_SAM = 1 << 12,

    // STRIP GO AHEAD/SYNC
    // Used in ring networks (BOP) to terminate message (frame)
    COM5025_MODE_CONTROL_SS_GA = 1 << 13,

    // PROTOCOL SELECTION
    // PROTO = 0: BOP (Bit Oriented Protocols: SDLC, HDLC, ADCCP)
    // PROTO = 1: CCP (Control Character Protocols: BiSync, DDCMP)
    COM5025_MODE_CONTROL_PROTO = 1 << 14,

    // ALL PARTIES ADDRESS
    // Used in ring networks to enable all connected computers as receivers
    COM5025_MODE_CONTROL_APA = 1 << 15
} COM5025ModeControlFlags;

// Data Length Select (Read/Write)
typedef enum {
    COM5025_DATA_LENGTH_RXDL1 = 1 << 8,
    COM5025_DATA_LENGTH_RXDL2 = 1 << 9,
    COM5025_DATA_LENGTH_RXDL3 = 1 << 10,
    COM5025_DATA_LENGTH_EXCON = 1 << 11,
    COM5025_DATA_LENGTH_EXADD = 1 << 12,
    COM5025_DATA_LENGTH_TXDL1 = 1 << 13,
    COM5025_DATA_LENGTH_TXDL2 = 1 << 14,
    COM5025_DATA_LENGTH_TXDL3 = 1 << 15
} COM5025DataLengthSelectFlags;

// Chip pin OUTPUT signals
typedef enum {
    // Sync/Flag received (SFR) - pin 4
    // Set high for 1 clock time each time a sync or flag character is received
    COM5025_PIN_OUT_SFR = 0,

    // Receiver Status Available (RSA)
    // Set high in event of receiver overrun, parity error, CRC error, REOM or RAB/GA
    COM5025_PIN_OUT_RSA = 1,

    // Receiver Active - pin 5
    // Asserted when RDP presents the first data character of the message
    COM5025_PIN_OUT_RXACT = 2,

    // Receiver Data Available - pin 6
    // Set high when RDP has assembled an entire character and transferred it into RDB
    COM5025_PIN_OUT_RDA = 3,

    // Transmitter Active - Pin 34
    // Indicates the status of the TDP
    COM5025_PIN_OUT_TXACT = 4,

    // Transmitter Buffer Empty - Pin 35
    // High level when TDB or TX Status and Control Register may be loaded with new data
    COM5025_PIN_OUT_TBMT = 5,

    // Transmitter Status Available - Pin 36
    // TERR bit, indicating transmitter underflow
    COM5025_PIN_OUT_TSA = 6,

    // Transmitter Serial Output (TSO) - pin 38
    // The transmitted character (bit by bit)
    COM5025_PIN_OUT_TSO = 7,

    COM5025_MAX_OUT_PINS = 8
} COM5025SignalPinOut;

// Chip pin INPUT signals
typedef enum {
    // Receiver Clock (RCP) - Pin 2
    // Positive-going edge shifts data into the receiver shift register
    COM5025_PIN_IN_RCP = 0,

    // Receiver Serial Input (RSI) - Pin 3
    // Accepts the serial bit input stream
    COM5025_PIN_IN_RSI = 1,

    // Receiver Enable - Pin 8
    // High level allows processing of RSI data
    COM5025_PIN_IN_RXENA = 2,

    // Master Reset - Pin 33
    // Should be pulsed high after power turn on
    COM5025_PIN_IN_MR = 3,

    // Transmitter Enable - Pin 37
    // High level allows processing of transmitter data
    COM5025_PIN_IN_TXENA = 4,

    // Maintenance Select - Pin 40
    // Enable loopback - RSI becomes TSO and RCP becomes TCP
    COM5025_PIN_IN_MSEL = 5,

    COM5025_MAX_IN_PINS = 6
} COM5025SignalPinIn;

// Multi-Protocol mode selection
typedef enum {
    COM5025_MODE_BOP = 0,  // Bit Oriented Protocols: SDLC, HDLC, ADCCP
    COM5025_MODE_CCP = 1   // Control Character Protocols: BiSync, DDCMP
} COM5025MPCCMode;

// COM5025 chip state structure
typedef struct COM5025State {
    // Registers
    uint8_t receiverDataBuffer;
    uint16_t receiverStatusRegister;
    uint8_t transmitterDataRegister;
    uint16_t transmitterStatusControlRegister;
    uint8_t syncAddressRegister;
    uint16_t modeControlRegister;
    uint16_t dataLengthSelectRegister;

    // Pin states
    bool inputPins[COM5025_MAX_IN_PINS];
    bool outputPins[COM5025_MAX_OUT_PINS];

    // Internal state
    COM5025MPCCMode mode;
    bool maintenanceMode;
    uint16_t crcRegister;
    uint8_t receiverShiftRegister;
    uint8_t transmitterShiftRegister;
    int bitCounter;
    int characterLength;

    // Timing
    int clockCounter;
    bool flagDetected;
    bool abortDetected;

    // Register state pointer
    void *registers;

    // Callback function pointers
    void (*onTransmitterOutput)(void *context, uint8_t data);
    void (*onPinValueChanged)(void *context, COM5025SignalPinOut pin, bool value);
    void *callbackContext;
} COM5025State;

// Function declarations
void COM5025_Init(COM5025State *chip);
void COM5025_Reset(COM5025State *chip);
void COM5025_MasterReset(COM5025State *chip);

uint8_t COM5025_ReadByte(COM5025State *chip, COM5025RegistersByte reg);
void COM5025_WriteByte(COM5025State *chip, COM5025RegistersByte reg, uint8_t value);

uint16_t COM5025_ReadWord(COM5025State *chip, COM5025RegistersWord reg);
void COM5025_WriteWord(COM5025State *chip, COM5025RegistersWord reg, uint16_t value);

void COM5025_SetInputPin(COM5025State *chip, COM5025SignalPinIn pin, bool value);
bool COM5025_GetInputPin(COM5025State *chip, COM5025SignalPinIn pin);

bool COM5025_GetOutputPin(COM5025State *chip, COM5025SignalPinOut pin);

void COM5025_ClockReceiver(COM5025State *chip);
void COM5025_ClockTransmitter(COM5025State *chip);
void COM5025_ProcessBit(COM5025State *chip, bool bit);

void COM5025_ReceiveData(COM5025State *chip, const uint8_t *data, int length);
void COM5025_TransmitData(COM5025State *chip, uint8_t data);

// Callback setup functions
void COM5025_SetReceiverStatus(COM5025State *chip, uint16_t newRxStatus);

void COM5025_SetTransmitterOutputCallback(COM5025State *chip, void (*callback)(void *context, uint8_t data), void *context);
void COM5025_SetPinValueChangedCallback(COM5025State *chip, void (*callback)(void *context, COM5025SignalPinOut pin, bool value), void *context);

#endif // CHIP_COM5025_H