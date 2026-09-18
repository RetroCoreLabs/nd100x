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

#ifndef DEVICE_HDLC_H
#define DEVICE_HDLC_H

#include "hdlc_constants.h"
#include "chip_com5025.h"
#include "modem.h"
#include "dma_engine.h"

// HDLC device registers
typedef enum {
    HDLC_READ_RX_DATA = 0,              // IOX +0: Read Receiver Data Register (RxDR)
    HDLC_WRITE_PARAMETER_CONTROL = 1,   // IOX +1: Write Parameter Control Register (PCSARH)
    HDLC_READ_RX_STATUS = 2,            // IOX +2: Read Receiver Status Register
    HDLC_WRITE_SYNC_ADDRESS = 3,        // IOX +3: Write Sync/Address Register (SAR)
    HDLC_WRITE_CHAR_LENGTH = 4,         // IOX +4: Write Character Length (CL)
    HDLC_WRITE_TX_DATA = 5,             // IOX +5: Write Transmitter Data Register
    HDLC_READ_TX_STATUS = 6,            // IOX +6: Read Transmitter Status Register (TxSR)
    HDLC_WRITE_TX_CONTROL = 7,          // IOX +7: Write Transmitter Control Register (TxCW)
    HDLC_READ_RX_TRANSFER_STATUS = 8,   // IOX +10: Read Receiver Transfer Status (RRTS)
    HDLC_WRITE_RX_TRANSFER_CONTROL = 9, // IOX +11: Write Receiver Transfer Control
    HDLC_READ_TX_TRANSFER_STATUS = 10,  // IOX +12: Read Transmitter Transfer Status (RTTS)
    HDLC_WRITE_TX_TRANSFER_CONTROL = 11,// IOX +13: Write Transmitter Transfer Control
    HDLC_READ_DMA_ADDRESS = 12,         // IOX +14: Read DMA Address
    HDLC_WRITE_DMA_ADDRESS = 13,        // IOX +15: Write DMA Address
    HDLC_READ_DMA_COMMAND = 14,         // IOX +16: Read DMA Command Register
    HDLC_WRITE_DMA_COMMAND = 15         // IOX +17: Write DMA Command
} HDLCRegister;

// DMA Commands to the ND HDLC interface
typedef enum {
    HDLC_DMA_DEVICE_CLEAR = 0,
    HDLC_DMA_INITIALIZE = 1,
    HDLC_DMA_RECEIVER_START = 2,
    HDLC_DMA_RECEIVER_CONTINUE = 3,
    HDLC_DMA_TRANSMITTER_START = 4,
    HDLC_DMA_DUMP_DATA_MODULE = 5,
    HDLC_DMA_DUMP_REGISTERS = 6,
    HDLC_DMA_LOAD_REGISTERS = 7
} HDLCDMACommand;

// TH1 - baud rate selection thumbwheel
typedef enum {
    HDLC_BAUD_307200 = 0,     // TW0 = 307.2 kbps
    HDLC_BAUD_INVALID_1 = 1,  // Invalid choice 1
    HDLC_BAUD_INVALID_2 = 2,  // Invalid choice 2
    HDLC_BAUD_1200 = 3,       // TW3 - 1.2 kbps
    HDLC_BAUD_INVALID_4 = 4,  // Invalid choice 4
    HDLC_BAUD_INVALID_5 = 5,  // Invalid choice 5
    HDLC_BAUD_9600 = 6,       // 6 - 9.6 kbps
    HDLC_BAUD_38400 = 7,      // 7 - 38.4 kbps
    HDLC_BAUD_153600 = 8,     // 8 - 153.6 kbps
    HDLC_BAUD_76800 = 9,      // 9 - 76.8 kbps
    HDLC_BAUD_INVALID_10 = 10,// Invalid choice 10
    HDLC_BAUD_19200 = 11,     // 11 - 19.2 kbps
    HDLC_BAUD_INVALID_12 = 12,// Invalid choice 12
    HDLC_BAUD_4800 = 13,      // 13 - 4.8 kbps
    HDLC_BAUD_2400 = 14       // 14 - 2.4 kbps
} HDLCBaudRate;

// DMA receive operation status results
typedef enum {
    HDLC_DMA_RX_OK = 0,
    HDLC_DMA_RX_FAILED = 1,
    HDLC_DMA_RX_BUFFER_FULL = 2,
    HDLC_DMA_RX_NO_BUFFER = 3
} HDLCDMAReceiveStatus;

// DMA sender state machine states
typedef enum {
    HDLC_DMA_TX_STOPPED = 0,
    HDLC_DMA_TX_BLOCK_READY = 1,
    HDLC_DMA_TX_SENDING_BLOCK = 2,
    HDLC_DMA_TX_FRAME_SENT = 3
} HDLCDMATxState;

// DMA block sending states
typedef enum {
    HDLC_DMA_BLOCK_IDLE = 0,
    HDLC_DMA_BLOCK_START = 1,
    HDLC_DMA_BLOCK_SEND_DATA = 2,
    HDLC_DMA_BLOCK_SEND_FRAME_END = 3
} HDLCDMABlockState;

// Receiver Transfer Status Bits (RRTS register, IOX+10)
typedef union {
    uint16_t raw;
    struct {
        // DATA MODULE (bits 0-7)
        uint16_t dataAvailable : 1;         // Bit 0: Data Available - CRITICAL for packet processing
        uint16_t statusAvailable : 1;       // Bit 1: Status Available
        uint16_t receiverActive : 1;        // Bit 2: Receiver active within frame
        uint16_t syncFlagReceived : 1;      // Bit 3: SYNC character or FLAG received
        uint16_t dmaModuleRequest : 1;      // Bit 4: DMA Module Request (auto-cleared on read)
        uint16_t signalDetector : 1;        // Bit 5: Signal Detector (SD) - IGNORED BY SINTRAN
        uint16_t dataSetReady : 1;          // Bit 6: Data Set Ready/I (DSR) - IGNORED BY SINTRAN
        uint16_t ringIndicator : 1;         // Bit 7: Ring Indicator (RI)

        // DMA MODULE (bits 8-15, auto-cleared on read)
        uint16_t blockEnd : 1;              // Bit 8: Block End Status - IGNORED BY SINTRAN FOR FLOW CONTROL
        uint16_t frameEnd : 1;              // Bit 9: Frame End Status - IGNORED BY SINTRAN FOR FLOW CONTROL
        uint16_t listEnd : 1;               // Bit 10: List End Status
        uint16_t listEmpty : 1;             // Bit 11: List Empty Status - CRITICAL: Forces receiver shutdown
        uint16_t undefined12 : 1;           // Bit 12: Undefined
        uint16_t x21d : 1;                  // Bit 13: X.21 Data Indication Error
        uint16_t x21s : 1;                  // Bit 14: X.21 Call Setup/Clear Indication Error
        uint16_t receiverOverrun : 1;       // Bit 15: Receiver Overrun
    } bits;
} HDLCReceiverTransferStatus;

// Receiver Transfer Control Bits (Written to by WRTC)
typedef union {
    uint16_t raw;
    struct {
        // DATA MODULE (bits 0-7)
        uint16_t dataAvailableIE : 1;       // Bit 0: Data Available Interrupt Enable
        uint16_t statusAvailableIE : 1;     // Bit 1: Status Available Interrupt Enable
        uint16_t enableReceiver : 1;        // Bit 2: Enable Receiver (RXE)
        uint16_t enableReceiverDMA : 1;     // Bit 3: Enable Receiver DMA
        uint16_t dmaModuleIE : 1;           // Bit 4: DMA Module Interrupt Enable
        uint16_t deviceClearSelectMaint : 1;// Bit 5: Device Clear / Select Maintenance
        uint16_t dtr : 1;                   // Bit 6: Data Terminal Ready/C (DTR)
        uint16_t modemStatusChangeIE : 1;   // Bit 7: Modem Status Change Interrupt Enable

        // DMA MODULE (bits 8-15)
        uint16_t blockEndIE : 1;            // Bit 8: Block End Interrupt Enable
        uint16_t frameEndIE : 1;            // Bit 9: Frame End Interrupt Enable
        uint16_t listEndIE : 1;             // Bit 10: List End Interrupt Enable
        uint16_t reserved11_14 : 4;         // Bits 11-14: Reserved
        uint16_t bit15 : 1;                 // Bit 15: Always 1 after IOX + 11 if inspected after DUMP
    } bits;
} HDLCReceiverTransferControl;

// Transmitter Transfer Status Flags (RTTS) (IOX+12)
typedef union {
    uint16_t raw;
    struct {
        // DATA MODULE (bits 0-7)
        uint16_t transmitBufferEmpty : 1;   // Bit 0: TXBE - Transmit Buffer Empty
        uint16_t transmitterUnderrun : 1;   // Bit 1: TXU - Transmitter Underrun
        uint16_t transmitterActive : 1;     // Bit 2: TXA - Transmitter Active
        uint16_t reserved3 : 1;             // Bit 3: Not used
        uint16_t dmaModuleRequest : 1;      // Bit 4: DMA Module Request (auto-cleared on read)
        uint16_t reserved5 : 1;             // Bit 5: Not used
        uint16_t readyForSending : 1;       // Bit 6: RFS - Ready for Sending
        uint16_t reserved7 : 1;             // Bit 7: Not used

        // DMA MODULE (bits 8-15, auto-cleared on read except bit 15)
        uint16_t blockEnd : 1;              // Bit 8: BE - Block End status
        uint16_t frameEnd : 1;              // Bit 9: FE - Frame End status
        uint16_t listEnd : 1;               // Bit 10: LE - List End status
        uint16_t transmissionFinished : 1;  // Bit 11: TRFIN - Transmission Finished
        uint16_t undefined12 : 1;           // Bit 12: Undefined (auto-cleared)
        uint16_t undefined13 : 1;           // Bit 13: Undefined (auto-cleared)
        uint16_t undefined14 : 1;           // Bit 14: Undefined (auto-cleared)
        uint16_t illegal : 1;               // Bit 15: ERR - Illegal Key or Format (NOT auto-cleared)
    } bits;
} HDLCTransmitterTransferStatus;

// Transmitter Transfer Control (Written to by WTTC) (IOX+13)
typedef union {
    uint16_t raw;
    struct {
        // DATA MODULE (bits 0-7)
        uint16_t transmitBufferEmptyIE : 1; // Bit 0: Transmit Buffer Empty Interrupt Enable
        uint16_t transmitterUnderrunIE : 1; // Bit 1: Transmitter Underrun Interrupt Enable
        uint16_t transmitterEnabled : 1;    // Bit 2: Transmitter Enabled (TXE)
        uint16_t enableTransmitterDMA : 1;  // Bit 3: Enable Transmitter DMA
        uint16_t dmaModuleIE : 1;           // Bit 4: DMA Module Interrupt Enable
        uint16_t halfDuplex : 1;            // Bit 5: Half Duplex
        uint16_t requestToSend : 1;         // Bit 6: Request to Send (RQTS)
        uint16_t modemStatusChangeIE : 1;   // Bit 7: Modem Status Change Interrupt Enable

        // DMA MODULE (bits 8-15)
        uint16_t blockEndIE : 1;            // Bit 8: Block End Interrupt Enable
        uint16_t frameEndIE : 1;            // Bit 9: Frame End Interrupt Enable
        uint16_t listEndIE : 1;             // Bit 10: List End Interrupt Enable
        uint16_t reserved11_14 : 4;         // Bits 11-14: Reserved
        uint16_t bit15 : 1;                 // Bit 15: Always 1 after IOX + 13 if inspected after DUMP
    } bits;
} HDLCTransmitterTransferControl;

// IDENT masks to clear interrupt enable bits
#define HDLC_RTC_MASK_CLEAR_IDENT  (~((1 << 0) | (1 << 1) | (1 << 4) | (1 << 7)))  // Clear dataAvailableIE, statusAvailableIE, dmaModuleIE, modemStatusChangeIE
#define HDLC_TTC_MASK_CLEAR_IDENT  (~((1 << 0) | (1 << 1) | (1 << 4) | (1 << 7)))  // Clear transmitBufferEmptyIE, transmitterUnderrunIE, dmaModuleIE, modemStatusChangeIE

// Key Flags for the DMA Data Buffer
typedef enum {
    // RCOST flags (identical to Receiver Status Register in the MPCC)
    HDLC_KEY_RCOST_RSOM = 1 << 0,       // Receiver Start of Message
    HDLC_KEY_RCOST_REOM = 1 << 1,       // Receiver End of Message
    HDLC_KEY_RCOST_RX_GAAB = 1 << 2,    // Receiver Go-Ahead or Abort
    HDLC_KEY_RCOST_RX_OVR = 1 << 3,     // Receiver Overflow (same bit as XBLDN)
    HDLC_KEY_RCOST_RX_ERR = 1 << 7,     // Receiver Error

    // External Block Done (XBLDN) - same bit position as RCOST_RX_OVR (bit 3)
    // Context determines meaning: RCOST context = RX_OVR, LKEY context = XBLDN
    HDLC_KEY_XBLDN = 1 << 3,            // External Block Done

    // Key Information (bits 8-10)
    HDLC_KEY_EMPTY_RX_BLOCK = 0x2 << 8,     // Empty Receiver Block (010)
    HDLC_KEY_FULL_RX_BLOCK = 0x3 << 8,      // Full Receiver Block (011)
    HDLC_KEY_BLOCK_TO_TX = 0x4 << 8,        // Block To be transmitted (100)
    HDLC_KEY_ALREADY_TX_BLOCK = 0x5 << 8,   // Already transmitted block (101)
    HDLC_KEY_NEW_LIST_PTR = 0x6 << 8,       // New list pointer (110)

    // Block done bit (both for RX and TX blocks)
    HDLC_KEY_BLOCK_DONE_BIT = 0x1 << 8,     // Block done bit

    // Masks
    HDLC_KEY_MASK_KEY = 0x7 << 8,           // Key mask (bits 8-10)
    HDLC_KEY_MASK_DATAFLOW_COST = 0xFF      // Dataflow COST mask (bits 0-7)
} HDLCKeyFlags;

// HDLC device data structure
typedef struct {
    // Device configuration
    HDLCBaudRate baudRate;
    uint8_t thumbwheel;

    // Register states
    HDLCReceiverTransferStatus rxTransferStatus;
    HDLCReceiverTransferControl rxTransferControl;
    HDLCTransmitterTransferStatus txTransferStatus;
    HDLCTransmitterTransferControl txTransferControl;

    // DMA state
    uint16_t dmaAddress;
    uint8_t dmaBankBits;        // 4 bits for bank select (bits 16-19 of physical address); bank DMA_Address currently resolves through
    uint8_t rxBankBits;         // bank last latched by a RECEIVER command; kept separate from TX so a transmitter bank can never leak into a receiver command
    uint8_t txBankBits;         // bank last latched by a TRANSMITTER command; the bank XSSDATA (D=0) reuses from a preceding XSSND on the transmit channel
    uint8_t dmaCommand;
    HDLCDMATxState txState;
    HDLCDMABlockState blockState;

    // HDLC chip registers
    uint8_t rxDataRegister;
    uint8_t rxStatusRegister;
    uint8_t txDataRegister;
    uint8_t txStatusRegister;
    uint8_t parameterControlRegister;
    uint8_t syncAddressRegister;
    uint8_t characterLength;
    uint8_t txControlRegister;

    // Internal state
    bool deviceActive;
    bool maintenanceMode;
    int tickCounter;

    // COM5025 chip state
    COM5025State *com5025;

    // Modem and DMA engine
    ModemState *modem;
    DMAEngine *dmaEngine;

    // Modem signal flags
    HDLCReceiverTransferStatus rxModemFlags;      // Current modem signal states
    HDLCTransmitterTransferStatus txModemFlags;   // Current modem signal states
    HDLCReceiverTransferStatus rxModemFlagsMask;  // Mask for modem-related bits
    HDLCTransmitterTransferStatus txModemFlagsMask; // Mask for modem-related bits

    // Clock timing
    int cpuTicks;
    int cpuTicksPerTx;

    // Statistics
    uint64_t framesTx;
    uint64_t framesRx;
    uint64_t framesRxErrors;

    // TX diagnostics (detect duplicate sends)
    uint64_t txStarts;          // TRANSMITTER_START commands received
    uint64_t txSendCalls;       // SendAllBuffers calls
    uint64_t txAlreadySent;     // AlreadyTransmittedBlock skips (duplicate detection)
    uint32_t txLastListPtr;     // last TX list pointer address
    uint16_t txLastKeyBefore;   // key value before MarkBufferSent

    // DCB counters
    uint64_t dcbTxMarked;       // MarkBufferSent calls (DCBs marked as transmitted)
    uint64_t dcbRxMarked;       // MarkBufferReceived calls (DCBs marked as received)

    // Interrupt diagnostics
    uint64_t identCount12;      // IDENT calls on level 12 (TX)
    uint64_t identCount13;      // IDENT calls on level 13 (RX)
    uint64_t irq12Count;        // IRQ 12 raised (TX)
    uint64_t irq13Count;        // IRQ 13 raised (RX)
    uint64_t irq13_dataAvail;   // from CheckTriggerInterrupt dataAvailable path
    uint64_t irq13_statusAvail; // from CheckTriggerInterrupt statusAvailable path
    uint64_t irq13_modem;       // from CheckTriggerIRQ13 modem status change
    uint64_t irq13_dma;         // from SetRXDMAFlag dmaModuleRequest path
    uint64_t iox13WriteCount;   // IOX+13 (WTTC) writes
    uint64_t iox11WriteCount;   // IOX+11 (WRTC) writes

    // Last 10 TX frame history (ring buffer)
    #define HDLC_TX_HISTORY_SIZE 10
    #define HDLC_TX_HISTORY_DATA_SIZE 20
    struct {
        uint32_t listPtr;       // DCB list pointer address
        uint32_t dataAddr;      // data buffer address from DCB
        uint16_t byteCount;     // data bytes in this DCB
        uint16_t keyBefore;     // key value when DCB was loaded (before MarkBufferSent)
        uint16_t frameSize;     // HDLC frame size on wire (with flags+stuffing+CRC)
        uint8_t  data[HDLC_TX_HISTORY_DATA_SIZE]; // first 20 bytes of wire frame
        uint8_t  dataLen;       // actual bytes stored (min of frameSize, 20)
    } txHistory[HDLC_TX_HISTORY_SIZE];
    int txHistoryIdx;           // next write index (wraps)
} HDLCData;

// HDLC status for external inspection (menu, debugging)
typedef struct {
    // RX status
    int state;          // HDLCReceiveState: 0=IDLE, 1=RECEIVING, 2=ESCAPE, 3=ERROR
    int frameLength;    // bytes accumulated in current frame
    int tcpQueueUsed;   // bytes waiting in TCP receive buffer
    bool rxDmaEnabled;  // receiver DMA enabled by SINTRAN
    bool rxEnabled;     // receiver enabled (RXE bit)
    bool rxDcbReady;    // RX DMA control block available

    // TX status
    bool txDmaEnabled;  // transmitter DMA enabled by SINTRAN
    bool txEnabled;     // transmitter enabled (TXE bit)
    int txSenderState;  // DmaEngineSenderState: 0=STOPPED, 1=READY, 2=SENDING, 3=SENT
    int txWaitTicks;    // dmaWaitTicks countdown (-1 = disabled)
    int txQueueUsed;    // bytes waiting in modem TX queue

    // TX diagnostics
    uint64_t txStarts;      // TRANSMITTER_START commands
    uint64_t txSendCalls;   // SendAllBuffers invocations
    uint64_t txAlreadySent; // AlreadyTransmittedBlock skips
} HDLCRxFrameStatus;

// Function declarations
Device* CreateHDLCDevice(uint8_t thumbwheel);
bool HDLC_GetRxFrameStatus(const HDLCData *data, HDLCRxFrameStatus *status);
void HDLC_BridgeInjectRx(Device *device, const uint8_t *data, int length);

#endif // DEVICE_HDLC_H