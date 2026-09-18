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

#ifndef DMA_ENUM_H
#define DMA_ENUM_H

#include <stdint.h>

/**
 * @brief DMA Commands to the ND HDLC interface
 */
typedef enum {
    // Initialization
    DMA_CMD_DEVICE_CLEAR = 0,
    DMA_CMD_INITIALIZE = 1,

    // Data Transfer
    DMA_CMD_RECEIVER_START = 2,
    DMA_CMD_RECEIVER_CONTINUE = 3,
    DMA_CMD_TRANSMITTER_START = 4,

    // Maintenance
    DMA_CMD_DUMP_DATA_MODULE = 5,
    DMA_CMD_DUMP_REGISTERS = 6,
    DMA_CMD_LOAD_REGISTERS = 7,
} DMACommands;

/**
 * @brief TH1 - baud rate selection thumbwheel
 */
typedef enum {
    /// TW0 = 307.2 kbps
    TH1_TW0_307200 = 0,

    /// Invalid choice 1
    TH1_TW1_INVALID = 1,

    /// Invalid choice 2
    TH1_TW2_INVALID = 2,

    /// TW3 - 1.2 kbps
    TH1_TW3_1200 = 3,

    /// Invalid choice 4
    TH1_TW4_INVALID = 4,

    /// Invalid choice 5
    TH1_TW5_INVALID = 5,

    /// 6 - 9.6 kbps
    TH1_TW6_9600 = 6,

    /// 7 - 38.4 kbps
    TH1_TW7_38400 = 7,

    /// 8 - 153.6 kbps
    TH1_TW8_153600 = 8,

    /// 9 - 76.8 kbps
    TH1_TW9_76800 = 9,

    /// Invalid choice 10
    TH1_TW10_INVALID = 10,

    /// 11 - 19.2 kbps
    TH1_TW11_19200 = 11,

    /// Invalid choice 12
    TH1_TW12_INVALID = 12,

    /// 13 - 4.8 kbps
    TH1_TW13_4800 = 13,

    /// 14 - 2.4 kbps
    TH1_TW14_2400 = 14
} TH1Wheel;

/**
 * @brief DMA receive operation status results
 */
typedef enum {
    DMA_RX_STATUS_OK = 0,
    DMA_RX_STATUS_FAILED = 1,
    DMA_RX_STATUS_BUFFER_FULL = 2,
    DMA_RX_STATUS_NO_BUFFER = 3
} DmaReceiveStatus;

/**
 * @brief DMA sender state machine states
 */
typedef enum {
    /// DMA transmission is stopped
    DMA_SENDER_STOPPED = 0,

    /// Block ready to send, waiting for enable flags
    DMA_SENDER_BLOCK_READY_TO_SEND = 1,

    /// Currently sending a block
    DMA_SENDER_SENDING_BLOCK = 2,

    /// Frame has been sent
    DMA_SENDER_FRAME_SENT = 3
} DmaEngineSenderState;

/**
 * @brief DMA block sending states
 */
typedef enum {
    /// Not sending data
    DMA_BLOCK_IDLE = 0,

    /// Starting a new block
    DMA_BLOCK_START_BLOCK = 1,

    /// Sending data from buffer
    DMA_BLOCK_SEND_DATA = 2,

    /// Sending frame end
    DMA_BLOCK_SEND_FRAME_END = 3
} DmaBlockSendState;

/**
 * @brief Receiver Transfer Status Bits (RRTS register, IOX+10)
 *
 * The low byte is the receiver transfer status from the data modules.
 * The high byte is the transfer status from the DMA module, and is not used unless the DMA module is installed.
 */
typedef enum {
    // DATA MODULE (bits 0-7)

    /// Data Available (bit 0) - CRITICAL for packet processing
    /// Indicates that a character has been assembled and may be read from the Receiver Data Register (RDSRL).
    /// Interrupt on level 13 if enabled.
    RTS_DATA_AVAILABLE = 1 << 0,

    /// Status Available (bit 1)
    /// Indicates that status information is available in the Receiver Status Register (RDSRH).
    /// Interrupt on level 13 if enabled.
    RTS_STATUS_AVAILABLE = 1 << 1,

    /// Receiver Active (bit 2)
    /// The receiver has seen the start of a frame, but not the end.
    /// This means that the receiver is active within a frame.
    RTS_RECEIVER_ACTIVE = 1 << 2,

    /// Sync Flag Received (bit 3)
    /// At least one SYNC character or FLAG has been received after the last reading of
    /// Receiver Transfer Status or Master Clear/Device Clear.
    RTS_SYNC_FLAG_RECEIVED = 1 << 3,

    /// DMA Module Request (bit 4) - Hardware handshaking
    /// This bit is activated by the DMA module. Auto-cleared when RTSR register is read.
    RTS_DMA_MODULE_REQUEST = 1 << 4,

    /// Signal Detector (SD) - bit 5 - COMPLETELY IGNORED BY SINTRAN
    /// Status of the Signal Detector (CCITT circuit 109) from the Data Communication Equipment.
    RTS_SIGNAL_DETECTOR = 1 << 5,

    /// Data Set Ready/I (DSR) - bit 6 - COMPLETELY IGNORED BY SINTRAN
    /// Status of the Data Set Ready (CCITT circuit 107) signal from the Data Communication Equipment.
    RTS_DATA_SET_READY = 1 << 6,

    /// Ring Indicator (RI) (bit 7)
    /// Status of the Ring Indicator (CCITT circuit 125) from the Data Communication Equipment.
    RTS_RING_INDICATOR = 1 << 7,

    // DMA MODULE (bits 8-15, auto-cleared on read)

    /// Block End Status bit from DMA module (bit 8) - IGNORED BY SINTRAN FOR FLOW CONTROL
    /// Auto-cleared when RTSR register is read.
    RTS_BLOCK_END = 1 << 8,

    /// Frame End Status bit from DMA module (bit 9) - IGNORED BY SINTRAN FOR FLOW CONTROL
    /// Auto-cleared when RTSR register is read.
    RTS_FRAME_END = 1 << 9,

    /// List End Status bit from DMA module (bit 10)
    /// Auto-cleared when RTSR register is read.
    RTS_LIST_END = 1 << 10,

    /// List Empty Status bit from DMA module (bit 11) - CRITICAL
    /// Forces receiver shutdown, stops all processing.
    /// Note: List Empty always gives a DMA Module Request (Bit 4) when set.
    RTS_LIST_EMPTY = 1 << 11,

    /// Undefined (bit 12)
    RTS_UNDEFINED12 = 1 << 12,

    /// X21D - X.21 Data Indication Error (bit 13)
    /// Triggers X.21 error handling.
    RTS_X21D = 1 << 13,

    /// X21S - X.21 Call Setup/Clear Indication Error (bit 14)
    /// Indicates connection termination procedures.
    RTS_X21S = 1 << 14,

    /// Receiver Overrun (bit 15) - Buffer overrun condition
    RTS_RECEIVER_OVERRUN = 1 << 15,

    /// Used for clearing DMA info after read
    RTS_DMA_CLEAR_BITS = RTS_BLOCK_END | RTS_FRAME_END | RTS_LIST_END | RTS_LIST_EMPTY | RTS_UNDEFINED12
} RTSBits;

/**
 * @brief Receiver Transfer Control Bits (Written to by WRTC)
 *
 * The low byte is for interrupt and data enabling on the data module and also some
 * Data Communication Equipment control signals.
 * The high byte is for DMA module control signal.
 */
typedef enum {
    /// Data Available Interrupt Enable (Bit 0)
    RTC_DATA_AVAILABLE_IE = 1 << 0,

    /// Status Available Interrupt Enable (Bit 1)
    RTC_STATUS_AVAILABLE_IE = 1 << 1,

    /// Enable Receiver (RXE) (Bit 2)
    RTC_ENABLE_RECEIVER = 1 << 2,

    /// Enable Receiver DMA (Bit 3)
    RTC_ENABLE_RECEIVER_DMA = 1 << 3,

    /// DMA Module Interrupt Enable (Bit 4)
    RTC_DMA_MODULE_IE = 1 << 4,

    /// Device Clear / Select Maintenance (Bit 5)
    RTC_DEVICE_CLEAR_SELECT_MAINTENANCE = 1 << 5,

    /// Data Terminal Ready/C (DTR) (Bit 6)
    RTC_DTR = 1 << 6,

    /// Modem Status Change Interrupt Enable (Bit 7)
    RTC_MODEM_STATUS_CHANGE_IE = 1 << 7,

    // DMA MODULE ONLY (bits 8-15)

    /// Block End Interrupt Enable (Bit 8)
    RTC_BLOCK_END_IE = 1 << 8,

    /// Frame End Interrupt Enable (Bit 9)
    RTC_FRAME_END_IE = 1 << 9,

    /// List End Interrupt Enable (Bit 10)
    RTC_LIST_END_IE = 1 << 10,

    /// Bit15 - Always 1 after IOX + 11 if inspected after a DUMP command (M11)
    RTC_BIT15 = 1 << 15,

    /// Mask for clearing interrupt enable bits on IDENT
    RTC_MASK_CLEAR_IDENT = ~(RTC_DATA_AVAILABLE_IE | RTC_STATUS_AVAILABLE_IE | RTC_DMA_MODULE_IE | RTC_MODEM_STATUS_CHANGE_IE),

    /// Mask for clearing interrupt enable bits on DEVICE CLEAR
    RTC_MASK_CLEAR_DEVICE_CLEAR = ~(RTC_DATA_AVAILABLE_IE | RTC_STATUS_AVAILABLE_IE | RTC_DMA_MODULE_IE | RTC_MODEM_STATUS_CHANGE_IE)
} RTCBits;

/**
 * @brief Read Transmitter Transfer Status Flags (RTTS) (IOX+12)
 *
 * The low byte is the transmitter transfer status from the data module.
 * The high byte is the transfer status from the DMA module if installed.
 */
typedef enum {
    /// TXBE - Transmit Buffer Empty (Bit 0)
    TTS_TRANSMIT_BUFFER_EMPTY = 1 << 0,

    /// TXU - Transmitter Underrun (Bit 1)
    TTS_TRANSMITTER_UNDERRUN = 1 << 1,

    /// TXA - Transmitter Active (Bit 2)
    TTS_TRANSMITTER_ACTIVE = 1 << 2,

    /// DMA RQ - DMA Module Request (Bit 4)
    /// Auto-cleared when RTTS register is read.
    TTS_DMA_MODULE_REQUEST = 1 << 4,

    /// RFS - Ready for Sending (Bit 6)
    /// Status signal from the Data Communication Equipment (CCITT circuit 106).
    TTS_READY_FOR_SENDING = 1 << 6,

    /// BE - Block End status Bit from DMA module (bit 8)
    /// Auto-cleared when RTTS register is read.
    TTS_BLOCK_END = 1 << 8,

    /// FE - Frame End status Bit from DMA module (bit 9)
    /// Auto-cleared when RTTS register is read.
    TTS_FRAME_END = 1 << 9,

    /// LE - List End status Bit from DMA module (bit 10)
    /// Auto-cleared when RTTS register is read.
    TTS_LIST_END = 1 << 10,

    /// TRFIN - Transmission Finished status bit from the DMA module (bit 11)
    /// Auto-cleared when RTTS register is read.
    TTS_TRANSMISSION_FINISHED = 1 << 11,

    /// Undefined bits 12-14
    TTS_UNDEFINED12 = 1 << 12,
    TTS_UNDEFINED13 = 1 << 13,
    TTS_UNDEFINED14 = 1 << 14,

    /// ERR - Illegal Key or Illegal Format in Transmitter Buffer Descriptor (Bit 15)
    /// Not auto-cleared.
    TTS_ILLEGAL = 1 << 15,

    /// Mask for clearing on DEVICE CLEAR
    TTS_MASK_CLEAR_DEVICE_CLEAR = ~(TTS_TRANSMITTER_UNDERRUN),

    /// Used for clearing DMA info after read
    TTS_DMA_CLEAR_BITS = TTS_BLOCK_END | TTS_FRAME_END | TTS_LIST_END | TTS_TRANSMISSION_FINISHED | TTS_UNDEFINED12 | TTS_UNDEFINED13 | TTS_UNDEFINED14
} TTSBits;

/**
 * @brief Transmitter Transfer Control (Written to by WTTC) IOX+13
 *
 * The low byte is for interrupt and data enabling on the data module and also two signals
 * concerning the connection to the Data Communication Equipment.
 * The high byte is for the DMA module.
 */
typedef enum {
    // DATA MODULE (bits 0-7)

    /// Transmit Buffer Empty Interrupt Enable (Bit 0)
    TTC_TRANSMIT_BUFFER_EMPTY_IE = 1 << 0,

    /// Transmitter Underrun Interrupt Enabled (Bit 1)
    TTC_TRANSMITTER_UNDERRUN_IE = 1 << 1,

    /// Transmitter Enabled (TXE) (Bit 2)
    TTC_TRANSMITTER_ENABLED = 1 << 2,

    /// Enable Transmitter DMA (Bit 3)
    TTC_ENABLE_TRANSMITTER_DMA = 1 << 3,

    /// DMA Module Interrupt Enable (Bit 4)
    TTC_DMA_MODULE_IE = 1 << 4,

    /// Half Duplex (Bit 5)
    TTC_HALF_DUPLEX = 1 << 5,

    /// Request to Send (RQTS) (Bit 6)
    TTC_REQUEST_TO_SEND = 1 << 6,

    /// Modem Status Change Interrupt Enable (Bit 7)
    TTC_MODEM_STATUS_CHANGE_IE = 1 << 7,

    // DMA MODULE (bits 8-15)

    /// Block End Interrupt Enable (Bit 8)
    TTC_BLOCK_END_IE = 1 << 8,

    /// Frame End Interrupt Enable (Bit 9)
    TTC_FRAME_END_IE = 1 << 9,

    /// List End Interrupt Enable (Bit 10)
    TTC_LIST_END_IE = 1 << 10,

    /// Bit 15 - Always 1 after IOX GP + 13 if inspected after a DUMP command (M15)
    TTC_BIT15 = 1 << 15,

    /// Mask for clearing interrupt enable bits on IDENT
    TTC_MASK_CLEAR_IDENT = ~(TTC_TRANSMIT_BUFFER_EMPTY_IE | TTC_TRANSMITTER_UNDERRUN_IE | TTC_DMA_MODULE_IE | TTC_MODEM_STATUS_CHANGE_IE),

    /// Mask for clearing interrupt enable bits on DEVICE CLEAR
    TTC_MASK_CLEAR_DEVICE_CLEAR = ~(TTC_TRANSMIT_BUFFER_EMPTY_IE | TTC_TRANSMITTER_UNDERRUN_IE | TTC_TRANSMITTER_ENABLED | TTC_DMA_MODULE_IE | TTC_HALF_DUPLEX | TTC_REQUEST_TO_SEND | TTC_MODEM_STATUS_CHANGE_IE)
} TTCBits;


/**
 * @brief Key Flags for the DMA Data Buffer
 */
typedef enum {
    // RCOST flags (identical to Receiver Status Register in the MPCC)
    KEYFLAG_RCOST_RSOM = 1 << 0,        // Receiver Start of Message
    KEYFLAG_RCOST_REOM = 1 << 1,        // Receiver End of Message
    KEYFLAG_RCOST_RX_GAAB = 1 << 2,     // Receiver Go-Ahead or Abort
    KEYFLAG_RCOST_RX_OVR = 1 << 3,      // Receiver Overflow

    KEYFLAG_RCOST_RX_ERR = 1 << 7,      // Receiver Error

    // DUAL-PURPOSE BIT 3
    /// External Block Done (XBLDN) - bit 3 in LKEY
    /// NOTE: This is the SAME bit position as RCOST_RX_OVR (bit 3)
    /// Context determines meaning:
    /// - In RCOST context: RX_OVR = receiver overflow error
    /// - In LKEY control context: XBLDN = external block done
    KEYFLAG_XBLDN = 1 << 3,

    // KEY INFORMATION (bits 8-10)
    /// Empty Receiver Block - Legal for RECEIVER
    KEYFLAG_EMPTY_RECEIVER_BLOCK = 0b010 << 8,

    /// Full Receiver Block
    KEYFLAG_FULL_RECEIVER_BLOCK = 0b011 << 8,

    /// Block To be transmitted - Legal for TRANSMITTER
    KEYFLAG_BLOCK_TO_BE_TRANSMITTED = 0b100 << 8,

    /// Already transmitted block
    KEYFLAG_ALREADY_TRANSMITTED_BLOCK = 0b101 << 8,

    /// New list pointer - Legal for RECEIVER and TRANSMITTER
    KEYFLAG_NEW_LIST_POINTER = 0b110 << 8,

    /// Block done bit (both for RX and TX blocks)
    KEYFLAG_BLOCK_DONE_BIT = 0x1 << 8,

    // Masks
    KEYFLAG_MASK_KEY = 0b111 << 8,            // Key mask (bits 8-10)
    KEYFLAG_MASK_DATAFLOW_COST = 0xFF       // Dataflow COST mask (bits 0-7)
} KeyFlags;

#endif // DMA_ENUM_H