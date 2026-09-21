/*
 * device_hdlc.c - HDLC communication controller device: registers, interrupts, modem link.
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

// Uncomment to enable HDLC debug logging (register reads/writes, interrupts, DMA commands)

#include "device_hdlc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../devices_types.h"
#include "../devices_protos.h"
#include "hdlc_frame.h"
#include "chip_com5025.h"
#include "chip_com5025_registers.h"
#include "dma_enum.h"
#include "dma_control_blocks.h"
#include "dma_receiver.h"

// clang-format off
static const char *hdlc_reg_name[] = {
    "RxDR",         // 0  IOX+0  Read Receiver Data
    "PCSARH",       // 1  IOX+1  Write Parameter Control
    "RxSR",         // 2  IOX+2  Read Receiver Status
    "SAR",          // 3  IOX+3  Write Sync/Address
    "CL",           // 4  IOX+4  Write Character Length
    "TxDR",         // 5  IOX+5  Write Transmitter Data
    "TxSR",         // 6  IOX+6  Read Transmitter Status
    "TxCW",         // 7  IOX+7  Write TX Control
    "RRTS",         // 8  IOX+10 Read RX Transfer Status
    "WRTC",         // 9  IOX+11 Write RX Transfer Control
    "RTTS",         // 10 IOX+12 Read TX Transfer Status
    "WTTC",         // 11 IOX+13 Write TX Transfer Control
    "RDMA_ADDR",    // 12 IOX+14 Read DMA Address
    "WDMA_ADDR",    // 13 IOX+15 Write DMA Address
    "RDMA_CMD",     // 14 IOX+16 Read DMA Command
    "WDMA_CMD"      // 15 IOX+17 Write DMA Command
};
// clang-format on

/* One HDLC transfer register bit and its name, for the hdlc debug log. */
typedef struct
{
    uint16_t bit;
    const char *name;
} HdlcBitName;

/* Log "  REG=0xVVVV [name name ]" as one line (hdlc category, debug level). */
static void hdlc_log_bits(const char *reg, uint16_t val, const HdlcBitName *bits, size_t count)
{
    char line[256];
    int pos = snprintf(line, sizeof(line), "  %s=0x%04X [", reg, val);
    for (size_t i = 0; i < count && pos > 0 && (size_t)pos < sizeof(line); i++)
    {
        if (val & bits[i].bit)
        {
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos, "%s ", bits[i].name);
        }
    }
    if (pos > 0 && (size_t)pos < sizeof(line))
    {
        snprintf(line + pos, sizeof(line) - (size_t)pos, "]");
    }
    log_write(LOG_CAT_HDLC, LOG_DEBUG, "%s", line);
}

#define HDLC_BITS(b) (b), (sizeof(b) / sizeof((b)[0]))

// clang-format off
static const HdlcBitName s_rrts_bits[] = {
    {1u << 0, "DataAvail"}, {1u << 1, "StatusAvail"}, {1u << 2, "RxActive"},
    {1u << 3, "SyncFlag"},  {1u << 4, "DMAReq"},      {1u << 5, "SD"},
    {1u << 6, "DSR"},       {1u << 7, "RI"},          {1u << 8, "BlockEnd"},
    {1u << 9, "FrameEnd"},  {1u << 10, "ListEnd"},    {1u << 11, "ListEmpty"},
    {1u << 15, "RxOverrun"},
};
// clang-format on

// clang-format off
static const HdlcBitName s_wrtc_bits[] = {
    {1u << 0, "DataAvailIE"},   {1u << 1, "StatusAvailIE"}, {1u << 2, "RxEnable"},
    {1u << 3, "RxDMA"},         {1u << 4, "DMAModuleIE"},   {1u << 5, "DevClear/Maint"},
    {1u << 6, "DTR"},           {1u << 7, "ModemChgIE"},    {1u << 8, "BlockEndIE"},
    {1u << 9, "FrameEndIE"},    {1u << 10, "ListEndIE"},
};
// clang-format on

// clang-format off
static const HdlcBitName s_rtts_bits[] = {
    {1u << 0, "TxBufEmpty"}, {1u << 1, "TxUnderrun"}, {1u << 2, "TxActive"},
    {1u << 4, "DMAReq"},     {1u << 6, "RFS"},        {1u << 8, "BlockEnd"},
    {1u << 9, "FrameEnd"},   {1u << 10, "ListEnd"},   {1u << 11, "TxFinished"},
    {1u << 15, "Illegal"},
};
// clang-format on

static const HdlcBitName s_wttc_bits[] = {
    {1u << 0, "TxBufEmptyIE"}, {1u << 1, "TxUnderrunIE"}, {1u << 2, "TxEnable"},
    {1u << 3, "TxDMA"},        {1u << 4, "DMAModuleIE"},  {1u << 5, "HalfDuplex"},
    {1u << 6, "RTS"},          {1u << 7, "ModemChgIE"},   {1u << 8, "BlockEndIE"},
    {1u << 9, "FrameEndIE"},   {1u << 10, "ListEndIE"},
};

#define HDLC_LOG(fmt, ...) LOG(LOG_CAT_HDLC, LOG_DEBUG, fmt, ##__VA_ARGS__)
#define HDLC_LOG_IRQ(level, reason)                                                                \
    LOG(LOG_CAT_HDLC, LOG_DEBUG, ">>> IRQ %d triggered: %s", (level), (reason))

// Forward declarations
static void hdlc_check_trigger_interrupt_request_12(Device *self);
static void hdlc_check_trigger_interrupt_request_13(Device *self);
static void hdlc_check_trigger_interrupt(Device *self);
static void hdlc_update_rqts(Device *self);

// Modem event callbacks
static void hdlc_on_modem_received_data(Device *device, const uint8_t *data, int length);
static void hdlc_on_modem_ring_indicator(Device *device, bool pin_value);
static void hdlc_on_modem_data_set_ready(Device *device, bool pin_value);
static void hdlc_on_modem_signal_detector(Device *device, bool pin_value);
static void hdlc_on_modem_clear_to_send(Device *device, bool pin_value);
static void hdlc_on_modem_request_to_send(Device *device, bool pin_value);
static void hdlc_on_modem_data_terminal_ready(Device *device, bool pin_value);

// DMA engine event callbacks
static void hdlc_on_dma_write_dma(Device *device, uint32_t address, uint16_t data);
static void hdlc_on_dma_read_dma(Device *device, uint32_t address, int *data);
static void hdlc_on_dma_set_interrupt_bit(Device *device, uint8_t bit);
static void hdlc_on_dma_send_hdlc_frame(Device *device, HDLCFrame *frame);
static void hdlc_on_dma_update_receiver_status(Device *device, uint16_t status);
static void hdlc_on_dma_clear_command(Device *device);

// COM5025 transmitter output callback
static void hdlc_on_com5025_transmitter_output(Device *device, uint8_t serial_output);

// COM5025 pin value changed callback
static void hdlc_on_com5025_pin_value_changed(Device *device, COM5025SignalPinOut pin, bool value);


// Device definitions array for HDLC devices
static const struct
{
    uint16_t addressBase;
    uint16_t identCode;
    const char *deviceName;
} g_hdlc_device_definitions[] = {
    {01640, 0150, "HDLC/MEGALINK 1"}, // thumbwheel 1
    {01660, 0151, "HDLC/MEGALINK 2"}, // thumbwheel 2
    {01700, 0152, "HDLC/MEGALINK 3"}, // thumbwheel 3
    {01720, 0153, "HDLC/MEGALINK 4"}, // thumbwheel 4
    {01740, 0154, "HDLC/MEGALINK 5"}, // thumbwheel 5
};

static void hdlc_reset(Device *self)
{
    HDLCData *data = (HDLCData *)self->deviceData;
    if (!data)
    {
        return;
    }

    // Clear all registers and status
    memset(&data->rxTransferStatus, 0, sizeof(HDLCReceiverTransferStatus));
    memset(&data->rxTransferControl, 0, sizeof(HDLCReceiverTransferControl));
    memset(&data->txTransferStatus, 0, sizeof(HDLCTransmitterTransferStatus));
    memset(&data->txTransferControl, 0, sizeof(HDLCTransmitterTransferControl));

    // Reset HDLC chip registers
    data->rxDataRegister = 0;
    data->rxStatusRegister = 0;
    data->txDataRegister = 0;
    data->txStatusRegister = 0;
    data->parameterControlRegister = 0;
    data->syncAddressRegister = 0;
    data->characterLength = 0;
    data->txControlRegister = 0;

    // Reset DMA state
    data->dmaAddress = 0;
    data->dmaBankBits = 0;
    data->rxBankBits = 0;
    data->txBankBits = 0;
    data->dmaCommand = 0;
    data->txState = HDLC_DMA_TX_STOPPED;
    data->blockState = HDLC_DMA_BLOCK_IDLE;

    // Reset internal state
    data->deviceActive = false;
    data->maintenanceMode = false;
    data->tickCounter = 0;

    // Initialize COM5025 chip (re-register callbacks after Init since it memsets the state)
    if (data->com5025)
    {
        com5025_init(data->com5025);
        COM5025_SetTransmitterOutputCallback(
            data->com5025, (void (*)(void *, uint8_t))hdlc_on_com5025_transmitter_output, self);
        COM5025_SetPinValueChangedCallback(
            data->com5025,
            (void (*)(void *, COM5025SignalPinOut, bool))hdlc_on_com5025_pin_value_changed, self);
        com5025_reset(data->com5025);
    }

    // Initialize modem signal flags and masks
    data->rxModemFlags.raw = 0;
    data->txModemFlags.raw = 0;
    data->rxModemFlagsMask.raw =
        (1 << 5) | (1 << 6) | (1 << 7);    // SignalDetector, DataSetReady, RingIndicator
    data->txModemFlagsMask.raw = (1 << 6); // ReadyForSending

    // Initialize clock timing
    data->cpuTicks = 0;
    data->cpuTicksPerTx = 625; // Default timing divider

    // Set default baud rate based on thumbwheel
    data->baudRate = HDLC_BAUD_9600; // Default to 9600 bps
}

static uint16_t hdlc_tick(Device *self)
{
    if (!self)
    {
        return 0;
    }

    HDLCData *data = (HDLCData *)self->deviceData;
    if (!data)
    {
        return 0;
    }

    // Process I/O delays
    dev_tick_io_delay(self);

    // Tick modem and DMA engine
    if (data->modem)
    {
        modem_tick(data->modem);
    }
    if (data->dmaEngine)
    {
        dma_engine_tick(data->dmaEngine);
    }

    // Clock COM5025 only before DMA is initialized (needed for maintenance test).
    // After INITIALIZE, burst/DMA mode handles all framing - COM5025 is unused.
    if (!data->dmaEngine->enabled)
    {
        data->cpuTicks++;
        if (data->cpuTicks >= data->cpuTicksPerTx)
        {
            data->cpuTicks = 0;
            if (data->com5025)
            {
                com5025_clock_transmitter(data->com5025);
                com5025_clock_receiver(data->com5025);
                // COM5025Registers_Clock takes the registers block, not the
                // whole chip state. The chip struct holds it behind a void*.
                com5025_reg_clock((COM5025Registers *)data->com5025->registers);
            }
        }
    }

    return self->interruptBits;
}

static uint16_t hdlc_read(Device *self, uint32_t address)
{
    if (!self)
    {
        return 0;
    }

    HDLCData *data = (HDLCData *)self->deviceData;
    uint16_t value = 0;
    uint32_t reg = dev_register_address(self, address);

    switch (reg)
    {
    case HDLC_READ_RX_DATA: // IOX +0: Read Receiver Data Register
        value = com5025_read_byte(data->com5025, COM5025_REG_BYTE_RECEIVER_DATA_BUFFER);
        break;

    case HDLC_READ_RX_STATUS: // IOX +2: Read Receiver Status Register
        value = com5025_read_byte(data->com5025, COM5025_REG_BYTE_RECEIVER_STATUS);
        break;

    case HDLC_WRITE_CHAR_LENGTH: // IOX +4: Character Length (read operation)
        com5025_write_byte(data->com5025, COM5025_REG_BYTE_DATA_LENGTH_SELECT, 0);
        value = 0;
        break;

    case HDLC_READ_TX_STATUS: // IOX +6: Read Transmitter Status Register
        value = com5025_read_byte(data->com5025, COM5025_REG_BYTE_TRANSMITTER_STATUS_CONTROL);
        break;

    case HDLC_READ_RX_TRANSFER_STATUS: // IOX +10: Read Receiver Transfer Status
        data->rxTransferStatus.bits.dmaModuleRequest = 0;

        // Latch modem signals (RI, SD, DSR) on read of this register
        data->rxTransferStatus.raw &= ~(data->rxModemFlagsMask.raw); // Clear old modem bits
        data->rxTransferStatus.raw |=
            (data->rxModemFlags.raw & data->rxModemFlagsMask.raw); // Latch new bits

        value = data->rxTransferStatus.raw;

        // Clear DMA bits 8-14 after read (bit 15 receiverOverrun is NOT auto-cleared)
        data->rxTransferStatus.bits.blockEnd = 0;
        data->rxTransferStatus.bits.frameEnd = 0;
        data->rxTransferStatus.bits.listEnd = 0;
        data->rxTransferStatus.bits.listEmpty = 0;
        data->rxTransferStatus.bits.undefined12 = 0;
        data->rxTransferStatus.bits.x21d = 0;
        data->rxTransferStatus.bits.x21s = 0;
        break;

    case HDLC_READ_TX_TRANSFER_STATUS: // IOX +12: Read Transmitter Transfer Status
        data->txTransferStatus.bits.dmaModuleRequest = 0;

        // Latch ReadyForSending (CTS) on read of this register
        data->txTransferStatus.raw &= ~(data->txModemFlagsMask.raw); // Clear old modem bits
        data->txTransferStatus.raw |=
            (data->txModemFlags.raw & data->txModemFlagsMask.raw); // Latch new bits

        value = data->txTransferStatus.raw;

        // Clear DMA bits 8-14 after read (bit 15 is NOT auto-cleared)
        data->txTransferStatus.bits.blockEnd = 0;
        data->txTransferStatus.bits.frameEnd = 0;
        data->txTransferStatus.bits.listEnd = 0;
        data->txTransferStatus.bits.transmissionFinished = 0;
        data->txTransferStatus.bits.undefined12 = 0;
        data->txTransferStatus.bits.undefined13 = 0;
        data->txTransferStatus.bits.undefined14 = 0;
        break;

    case HDLC_READ_DMA_ADDRESS: // IOX +14: Read DMA Address
        value = data->dmaAddress;
        break;

    case HDLC_READ_DMA_COMMAND: // IOX +16: Read DMA Command Register
        value = ((uint16_t)data->dmaCommand) << 8;
        break;

    default:
        break;
    }

    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
    {
        HDLC_LOG("READ  IOX+%o %-10s => 0x%04X", reg, reg < 16 ? hdlc_reg_name[reg] : "?", value);
        if (reg == HDLC_READ_RX_TRANSFER_STATUS)
        {
            hdlc_log_bits("RRTS", value, HDLC_BITS(s_rrts_bits));
        }
        if (reg == HDLC_READ_TX_TRANSFER_STATUS)
        {
            hdlc_log_bits("RTTS", value, HDLC_BITS(s_rtts_bits));
        }
    }

    return value;
}

static void hdlc_write(Device *self, uint32_t address, uint16_t value)
{
    if (!self)
    {
        return;
    }

    HDLCData *data = (HDLCData *)self->deviceData;
    uint32_t reg = dev_register_address(self, address);

    if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
    {
        HDLC_LOG("WRITE IOX+%o %-10s <= 0x%04X", reg, reg < 16 ? hdlc_reg_name[reg] : "?", value);
    }

    switch (reg)
    {
    case HDLC_WRITE_PARAMETER_CONTROL: // IOX +1: Write Parameter Control Register
        data->parameterControlRegister = (uint8_t)value;
        // Configure COM5025 MODE register
        com5025_write_byte(data->com5025, COM5025_REG_BYTE_MODE_CONTROL, (uint8_t)value);
        break;

    case HDLC_WRITE_SYNC_ADDRESS: // IOX +3: Write Sync/Address Register
        data->syncAddressRegister = (uint8_t)value;
        // Configure COM5025 SYNC/Address register
        com5025_write_byte(data->com5025, COM5025_REG_BYTE_SYNC_ADDRESS, (uint8_t)value);
        break;

    case HDLC_WRITE_TX_DATA: // IOX +5: Write Transmitter Data Register
        data->txDataRegister = (uint8_t)value;
        // Send data to COM5025 transmitter
        com5025_write_byte(data->com5025, COM5025_REG_BYTE_TRANSMITTER_DATA, (uint8_t)value);
        break;

    case HDLC_WRITE_TX_CONTROL: // IOX +7: Write Transmitter Control Register
        data->txControlRegister = (uint8_t)value;
        // Configure COM5025 transmitter control
        com5025_write_byte(data->com5025, COM5025_REG_BYTE_TRANSMITTER_STATUS_CONTROL,
                           (uint8_t)value);
        break;

    case HDLC_WRITE_RX_TRANSFER_CONTROL: // IOX +11: Write Receiver Transfer Control
        data->rxTransferControl.raw = value;
        if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
        {
            hdlc_log_bits("WRTC", value, HDLC_BITS(s_wrtc_bits));
        }
        if (value == 0)
        {
            // Clear maintenance/loopback mode
            data->maintenanceMode = false;
        }

        // Handle device clear/maintenance mode
        if (!data->maintenanceMode && data->rxTransferControl.bits.deviceClearSelectMaint)
        {
            HDLC_LOG("Device Clear");
            hdlc_reset(self);
            data->rxTransferControl.bits.deviceClearSelectMaint = 0;
            data->maintenanceMode = true;
        }

        // Configure COM5025 receiver enable pin
        com5025_set_input_pin(data->com5025, COM5025_PIN_IN_RXENA,
                              data->rxTransferControl.bits.enableReceiver);

        // Set maintenance select pin
        if (data->maintenanceMode)
        {
            com5025_set_input_pin(data->com5025, COM5025_PIN_IN_MSEL, true);
            hdlc_update_rqts(self);
        }

        // Handle DTR signal to modem (equivalent to C# modem.SetDTR())
        if (data->modem)
        {
            modem_set_dtr(data->modem, data->rxTransferControl.bits.dtr);
        }

        // Check for interrupt triggers
        hdlc_check_trigger_interrupt_request_13(self);
        hdlc_check_trigger_interrupt(self);
        break;

    case HDLC_WRITE_TX_TRANSFER_CONTROL: // IOX +13: Write Transmitter Transfer Control
        data->iox13WriteCount++;
        data->txTransferControl.raw = value;
        if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
        {
            hdlc_log_bits("WTTC", value, HDLC_BITS(s_wttc_bits));
        }

        // Configure COM5025 transmitter enable pin
        com5025_set_input_pin(data->com5025, COM5025_PIN_IN_TXENA,
                              data->txTransferControl.bits.transmitterEnabled);

        // Update RQTS signal logic
        hdlc_update_rqts(self);

        // Check modem status change -> IRQ 12 (matches C#: CheckModemStatusChangeTriggerIRQ12)
        hdlc_check_trigger_interrupt_request_12(self);

        // Check if DMAModuleIE is being enabled while DMAModuleRequest is already pending
        // (matches C# - does NOT check TBMT/underrun here, only DMA completion state)
        if (data->txTransferControl.bits.dmaModuleIE &&
            data->txTransferStatus.bits.dmaModuleRequest)
        {
            data->irq12Count++;
            dev_set_interrupt_status(self, true, 12);
        }
        break;

    case HDLC_WRITE_DMA_ADDRESS: // IOX +15: Write DMA Address
        data->dmaAddress = value;
        // Address is stored locally; combined with bank bits when IOX+17 command is written
        break;

    case HDLC_WRITE_DMA_COMMAND: // IOX +17: Write DMA Command
        data->dmaCommand = (value >> 8) & 0x07;
        {
            uint8_t new_bank = (uint8_t)(value & 0x0F);
            // Bank bits ride in this IOX+17 word with the command, paired with the low
            // address written at IOX+15. D>0 latches the carried bank for that channel;
            // D=0 reuses the bank last latched for the SAME channel (XSSDATA sets D=0 to
            // reuse the bank a preceding XSSND put on the transmit channel).
            //
            // Keep the transmit and receive banks SEPARATE so a transmitter bank can
            // never bleed into a receiver command. This was the SINTRAN K li-route
            // crash: TRANSMITTER_START latched bank 4, then RECEIVER_START arrived with
            // D=0; the old single shared latch reused 4, so the RX descriptor list was
            // read from bank 4 (garbage at 0x04A30E) instead of bank 0 (0x00A30E) and the
            // reply frame was DMA'd to an unmapped address -> crash on -C:li-rout.
            switch (data->dmaCommand)
            {
            case DMA_CMD_RECEIVER_START:
            case DMA_CMD_RECEIVER_CONTINUE:
                if (new_bank != 0)
                {
                    data->rxBankBits = new_bank;
                }
                data->dmaBankBits = data->rxBankBits;
                break;
            case DMA_CMD_TRANSMITTER_START:
                if (new_bank != 0)
                {
                    data->txBankBits = new_bank;
                }
                data->dmaBankBits = data->txBankBits;
                break;
            default:
                // INITIALIZE / DUMP / LOAD / DEVICE_CLEAR: use the carried bank,
                // falling back to the last resolved bank when D=0 (INITIALIZE
                // carries bank=1). Unchanged from before.
                if (new_bank != 0)
                {
                    data->dmaBankBits = new_bank;
                }
                break;
            }
        }
        if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
        {
            {
                static const char *cmd_names[] = {"DEVICE_CLEAR", "INITIALIZE", "RX_START",
                                                  "RX_CONTINUE",  "TX_START",   "DUMP_DATA",
                                                  "DUMP_REGS",    "LOAD_REGS"};
                uint32_t full_addr = ((uint32_t)data->dmaBankBits << 16) | data->dmaAddress;
                HDLC_LOG("DMA %s addr=0x%05X (bank=%d lo=0x%04X)", cmd_names[data->dmaCommand & 7],
                         full_addr, data->dmaBankBits, data->dmaAddress);
            }
        }
        // Execute DMA command
        if (data->dmaEngine)
        {
            // Combine bank bits with 16-bit DMA address to form 20-bit physical address
            uint32_t full_dma_address = ((uint32_t)data->dmaBankBits << 16) | data->dmaAddress;
            dma_engine_set_dma_address(data->dmaEngine, full_dma_address);

            switch (data->dmaCommand)
            {
            case DMA_CMD_DEVICE_CLEAR:
                dma_engine_command_device_clear(data->dmaEngine);
                break;
            case DMA_CMD_INITIALIZE:
                dma_engine_command_initialize(data->dmaEngine);
                // COM5025 keeps clocking (matching C#), so TBMT/RSA/RDA
                // are managed naturally by the chip. No manual overrides needed.
                break;
            case DMA_CMD_RECEIVER_START:
                dma_engine_command_receiver_start(data->dmaEngine);
                break;
            case DMA_CMD_RECEIVER_CONTINUE:
                dma_engine_command_receiver_continue(data->dmaEngine);
                break;
            case DMA_CMD_TRANSMITTER_START:
                data->txStarts++;
                data->txLastListPtr = full_dma_address;
                dma_engine_command_transmitter_start(data->dmaEngine);
                break;
            case DMA_CMD_DUMP_DATA_MODULE:
                dma_engine_command_dump_data_module(data->dmaEngine);
                break;
            case DMA_CMD_DUMP_REGISTERS:
                dma_engine_command_dump_registers(data->dmaEngine);
                break;
            case DMA_CMD_LOAD_REGISTERS:
                dma_engine_command_load_registers(data->dmaEngine);
                break;
            default:
                if (Log_IsEnabled(LOG_CAT_HDLC, LOG_DEBUG))
                {
                    log_write(LOG_CAT_HDLC, LOG_DEBUG, "HDLC: Unknown DMA command: %d\n",
                              data->dmaCommand);
                }
                break;
            }
        }
        break;

    default:
        // Invalid register
        break;
    }
}

static uint16_t hdlc_ident(Device *self, uint16_t level)
{
    if (!self)
    {
        return 0;
    }

    // Only respond if OUR interrupt bit is set for this level.
    // Level 13 is shared with RTC - must not claim RTC's interrupts.
    if (!(self->interruptBits & (1 << level)))
    {
        return 0; // Not our interrupt
    }

    HDLC_LOG("IDENT level %d", level);

    HDLCData *data = (HDLCData *)self->deviceData;

    if (level == 12)
    {
        data->identCount12++;
        data->txTransferControl.raw &= HDLC_TTC_MASK_CLEAR_IDENT;
    }

    if (level == 13)
    {
        data->identCount13++;
        data->rxTransferControl.raw &= HDLC_RTC_MASK_CLEAR_IDENT;
    }

    dev_set_interrupt_status(self, false, level);
    return self->identCode;
}

static void hdlc_destroy(Device *self)
{
    if (!self)
    {
        return;
    }

    HDLCData *data = (HDLCData *)self->deviceData;
    if (data)
    {
        // Clean up DMA engine
        if (data->dmaEngine)
        {
            dma_engine_destroy(data->dmaEngine);
            free(data->dmaEngine);
            data->dmaEngine = NULL;
        }

        // Clean up modem
        if (data->modem)
        {
            modem_destroy(data->modem);
            free(data->modem);
            data->modem = NULL;
        }

        // Clean up COM5025
        if (data->com5025)
        {
            free(data->com5025);
            data->com5025 = NULL;
        }
    }

    // Base Device_Destroy will handle freeing deviceData
}

Device *hdlc_create_device(uint8_t thumbwheel)
{
    // Validate thumbwheel value
    if (thumbwheel < 1 || thumbwheel > 5)
    {
        LOG(LOG_CAT_HDLC, LOG_ERROR, "HDLC: Invalid thumbwheel value %d (must be 1-5)\n",
            thumbwheel);
        return NULL;
    }

    // Allocate device structure
    Device *dev = malloc(sizeof(Device));
    if (!dev)
    {
        LOG(LOG_CAT_HDLC, LOG_ERROR, "HDLC: Failed to allocate device structure\n");
        return NULL;
    }

    // Initialize device with STANDARD class (could be CHARACTER if needed)
    dev_init(dev, thumbwheel, DEVICE_CLASS_STANDARD, 0);

    // Allocate device-specific data
    HDLCData *data = malloc(sizeof(HDLCData));
    if (!data)
    {
        LOG(LOG_CAT_HDLC, LOG_ERROR, "HDLC: Failed to allocate device data\n");
        free(dev);
        return NULL;
    }

    // Clear device data
    memset(data, 0, sizeof(HDLCData));

    // Allocate COM5025 chip state
    data->com5025 = malloc(sizeof(COM5025State));
    if (!data->com5025)
    {
        LOG(LOG_CAT_HDLC, LOG_ERROR, "HDLC: Failed to allocate COM5025 state\n");
        free(data);
        free(dev);
        return NULL;
    }

    // Allocate modem state
    data->modem = malloc(sizeof(ModemState));
    if (!data->modem)
    {
        LOG(LOG_CAT_HDLC, LOG_ERROR, "HDLC: Failed to allocate modem state\n");
        free(data->com5025);
        free(data);
        free(dev);
        return NULL;
    }

    // Allocate DMA engine state
    data->dmaEngine = malloc(sizeof(DMAEngine));
    if (!data->dmaEngine)
    {
        LOG(LOG_CAT_HDLC, LOG_ERROR, "HDLC: Failed to allocate DMA engine state\n");
        free(data->modem);
        free(data->com5025);
        free(data);
        free(dev);
        return NULL;
    }

    // Set up device configuration based on thumbwheel
    int dev_index = thumbwheel - 1;

    // Configure device address range
    dev->startAddress = g_hdlc_device_definitions[dev_index].addressBase;
    dev->endAddress = g_hdlc_device_definitions[dev_index].addressBase + 15; // 16 registers (0-15)

    // Configure device identification
    dev->identCode = g_hdlc_device_definitions[dev_index].identCode;
    dev->logicalDevice = 1360 + (thumbwheel - 1) * 2; // SINTRAN logical device numbers
    dev->interruptLevel = 12;                         // Default to transmitter interrupt level

    // Set device name
    snprintf(dev->memoryName, MAX_DEVICE_NAME, "%s",
             g_hdlc_device_definitions[dev_index].deviceName);

    // Configure device-specific data
    data->thumbwheel = thumbwheel;
    data->baudRate = HDLC_BAUD_9600; // Default baud rate

    // Set device function pointers
    dev->Reset = hdlc_reset;
    dev->Tick = hdlc_tick;
    dev->Read = hdlc_read;
    dev->Write = hdlc_write;
    dev->Ident = hdlc_ident;
    dev->Destroy = hdlc_destroy;
    dev->Boot = NULL; // HDLC devices don't support booting

    // Link device data
    dev->deviceData = data;

    // Initialize COM5025, modem, and DMA engine
    modem_init(data->modem, dev);
#ifdef __EMSCRIPTEN__
    modem_set_wasm_bridge_channel(data->modem, dev_index);
#endif
    dma_engine_init(data->dmaEngine, true, dev, data->modem, data->com5025);

    // Set up COM5025 callbacks
    COM5025_SetTransmitterOutputCallback(
        data->com5025, (void (*)(void *, uint8_t))hdlc_on_com5025_transmitter_output, dev);
    COM5025_SetPinValueChangedCallback(
        data->com5025,
        (void (*)(void *, COM5025SignalPinOut, bool))hdlc_on_com5025_pin_value_changed, dev);

    // Set up modem callbacks (equivalent to C# event subscriptions)
    modem_set_received_data_callback(data->modem, hdlc_on_modem_received_data);
    modem_set_ring_indicator_callback(data->modem, hdlc_on_modem_ring_indicator);
    modem_set_data_set_ready_callback(data->modem, hdlc_on_modem_data_set_ready);
    modem_set_signal_detector_callback(data->modem, hdlc_on_modem_signal_detector);
    modem_set_clear_to_send_callback(data->modem, hdlc_on_modem_clear_to_send);
    modem_set_request_to_send_callback(data->modem, hdlc_on_modem_request_to_send);
    modem_set_data_terminal_ready_callback(data->modem, hdlc_on_modem_data_terminal_ready);

    // Set up DMA engine callbacks (equivalent to C# event subscriptions)
    dma_engine_set_write_dma_callback(data->dmaEngine, hdlc_on_dma_write_dma);
    dma_engine_set_read_dma_callback(data->dmaEngine, hdlc_on_dma_read_dma);
    dma_engine_set_interrupt_callback(data->dmaEngine, hdlc_on_dma_set_interrupt_bit);
    dma_engine_set_send_frame_callback(data->dmaEngine, hdlc_on_dma_send_hdlc_frame);
    dma_engine_set_update_receiver_status_callback(data->dmaEngine,
                                                   hdlc_on_dma_update_receiver_status);
    dma_engine_set_clear_command_callback(data->dmaEngine, hdlc_on_dma_clear_command);

    HDLC_LOG("Created device thumbwheel=%d address=%o-%o ident=%o", thumbwheel, dev->startAddress,
             dev->endAddress, dev->identCode);

    return dev;
}

void hdlc_bridge_inject_rx(Device *device, const uint8_t *data, int length)
{
    if (!device || !data || length <= 0)
    {
        return;
    }

    HDLCData *hdlc_data = (HDLCData *)device->deviceData;
    if (!hdlc_data || !hdlc_data->dmaEngine || !hdlc_data->dmaEngine->receiver)
    {
        return;
    }

    // C# guard: ignore data when receiver is not active (e.g. after LIST_EMPTY)
    if (!hdlc_data->rxTransferStatus.bits.receiverActive)
    {
        return;
    }

    dma_rx_receive_data_from_modem(hdlc_data->dmaEngine->receiver, data, length);
}

// Modem event callback implementations (equivalent to C# event handlers)

static void hdlc_on_modem_received_data(Device *device, const uint8_t *data, int length)
{
    if (!device || !data)
    {
        return;
    }

    HDLCData *hdlc_data = (HDLCData *)device->deviceData;
    if (!hdlc_data || !hdlc_data->dmaEngine || !hdlc_data->dmaEngine->receiver)
    {
        return;
    }

    // C# guard: ignore data when receiver is not active (e.g. after LIST_EMPTY)
    if (!hdlc_data->rxTransferStatus.bits.receiverActive)
    {
        return;
    }

    // Non-blocking enqueue into ring buffer.
    // Processed pull-based by DMAReceiver_Tick.
    dma_rx_receive_data_from_modem(hdlc_data->dmaEngine->receiver, data, length);
}

static void hdlc_on_modem_ring_indicator(Device *device, bool pin_value)
{
    if (!device)
    {
        return;
    }

    HDLCData *data = (HDLCData *)device->deviceData;
    if (!data)
    {
        return;
    }

    if (pin_value)
    {
        data->rxModemFlags.bits.ringIndicator = 1;
    }
    else
    {
        data->rxModemFlags.bits.ringIndicator = 0;
    }
    hdlc_check_trigger_interrupt_request_13(device);
}

static void hdlc_on_modem_data_set_ready(Device *device, bool pin_value)
{
    if (!device)
    {
        return;
    }

    HDLCData *data = (HDLCData *)device->deviceData;
    if (!data)
    {
        return;
    }

    if (pin_value)
    {
        data->rxModemFlags.bits.dataSetReady = 1;
    }
    else
    {
        data->rxModemFlags.bits.dataSetReady = 0;
    }
    hdlc_check_trigger_interrupt_request_13(device);
}

static void hdlc_on_modem_signal_detector(Device *device, bool pin_value)
{
    if (!device)
    {
        return;
    }

    HDLCData *data = (HDLCData *)device->deviceData;
    if (!data)
    {
        return;
    }

    if (pin_value)
    {
        data->rxModemFlags.bits.signalDetector = 1;
    }
    else
    {
        data->rxModemFlags.bits.signalDetector = 0;
    }
    hdlc_check_trigger_interrupt_request_13(device);
}

static void hdlc_on_modem_clear_to_send(Device *device, bool pin_value)
{
    if (!device)
    {
        return;
    }

    HDLCData *data = (HDLCData *)device->deviceData;
    if (!data)
    {
        return;
    }

    // Equivalent to C# Modem_OnClearToSend
    if (pin_value)
    {
        data->txModemFlags.bits.readyForSending = 1;
    }
    else
    {
        data->txModemFlags.bits.readyForSending = 0;
    }
    hdlc_check_trigger_interrupt_request_12(device);
}

static void hdlc_on_modem_request_to_send(Device *device, bool pin_value)
{
    if (!device)
    {
        return;
    }

    HDLCData *data = (HDLCData *)device->deviceData;
    if (!data || !data->modem)
    {
        return;
    }

    // Equivalent to C# Modem_OnRequestToSend
    // In point-to-point setup, RTS connects to CTS
    modem_set_cts(data->modem, pin_value);
}

static void hdlc_on_modem_data_terminal_ready(Device *device, bool pin_value)
{
    if (!device)
    {
        return;
    }

    HDLCData *data = (HDLCData *)device->deviceData;
    if (!data || !data->modem)
    {
        return;
    }

    // Equivalent to C# Modem_OnDataTerminalReady
    // In point-to-point setup, DTR connects to DSR
    modem_set_dsr(data->modem, pin_value);
}

// DMA engine event callback implementations (equivalent to C# event handlers)

static void hdlc_on_dma_write_dma(Device *device, uint32_t address, uint16_t data)
{
    if (!device)
    {
        return;
    }
    dev_dma_write(address, data);
}

static void hdlc_on_dma_read_dma(Device *device, uint32_t address, int *data)
{
    if (!device || !data)
    {
        return;
    }

    // Equivalent to C# DmaEngine_OnReadDMA
    *data = dev_dma_read(address);
}

static void hdlc_on_dma_set_interrupt_bit(Device *device, uint8_t bit)
{
    if (!device)
    {
        return;
    }

    HDLCData *hd = (HDLCData *)device->deviceData;
    if (hd)
    {
        if (bit == 12)
        {
            hd->irq12Count++;
        }
        if (bit == 13)
        {
            hd->irq13Count++;
            hd->irq13_dma++;
        }
    }

    HDLC_LOG_IRQ(bit, bit == 12 ? "DMA TX" : bit == 13 ? "DMA RX" : "DMA ?");
    dev_set_interrupt_status(device, true, bit);
}

static void hdlc_on_dma_send_hdlc_frame(Device *device, HDLCFrame *frame)
{
    if (!device || !frame)
    {
        return;
    }

    HDLCData *data = (HDLCData *)device->deviceData;
    if (!data || !data->modem)
    {
        return;
    }

    // Equivalent to C# DmaEngine_OnSendHDLCFrame
    if (frame->frameLength > 0)
    {
        data->framesTx++;
        modem_send_bytes(data->modem, frame->frameBuffer, frame->frameLength);
    }
}

static void hdlc_on_dma_update_receiver_status(Device *device, uint16_t status)
{
    if (!device)
    {
        return;
    }

    HDLCData *data = (HDLCData *)device->deviceData;
    if (!data)
    {
        return;
    }

    // OR the COM5025 receiver status into the receiver transfer status to prevent loss of information
    // Map COM5025 status bits to HDLC receiver transfer status bits
    data->rxTransferStatus.raw |= (status & 0xFF); // Only use lower 8 bits
}

static void hdlc_on_dma_clear_command(Device *device)
{
    if (!device)
    {
        return;
    }

    HDLCData *data = (HDLCData *)device->deviceData;
    if (!data)
    {
        return;
    }

    // Clear the DMA command register
    data->dmaCommand = 0;
}

// COM5025 transmitter output callback implementation

static void hdlc_on_com5025_transmitter_output(Device *device, uint8_t serial_output)
{
    if (!device)
    {
        return;
    }

    // In burst mode (always active), the DMA engine handles all transmission
    // via Modem_SendBytes. The COM5025 character-mode output must NOT be sent
    // to the modem, as it would mix idle flags (0x7E) into the TCP stream
    // and corrupt the byte-stuffed HDLC frames.
    //
    // This callback is only needed for character mode (non-DMA), which is not used.
    (void)serial_output;
}

// COM5025 pin value changed callback implementation
static void hdlc_on_com5025_pin_value_changed(Device *device, COM5025SignalPinOut pin, bool value)
{
    if (!device)
    {
        return;
    }

    HDLCData *data = (HDLCData *)device->deviceData;
    if (!data)
    {
        return;
    }

    // Equivalent to C# Com5025_OnPinValueChanged
    // Handle pin value changes that affect HDLC controller state
    // Matches C# Com5025_OnPinValueChanged exactly:
    // - SFR, RXACT: update status only, NO CheckTriggerInterrupt
    // - RDA, RSA: update status + CheckTriggerInterrupt
    // - TXACT: update status + UpdateRQTS, NO CheckTriggerInterrupt
    // - TBMT, TSA: update status + CheckTriggerInterrupt
    switch (pin)
    {
    case COM5025_PIN_OUT_SFR: // Sync/Flag received - no interrupt check
        data->rxTransferStatus.bits.syncFlagReceived = value ? 1 : 0;
        break;

    case COM5025_PIN_OUT_RXACT: // Receiver Active - no interrupt check
        data->rxTransferStatus.bits.receiverActive = value ? 1 : 0;
        break;

    case COM5025_PIN_OUT_RDA: // Receiver Data Available - triggers interrupt check on rising edge only (matches C#)
        if (value)
        {
            data->rxTransferStatus.bits.dataAvailable = 1;
            hdlc_check_trigger_interrupt(device);
        }
        else
        {
            data->rxTransferStatus.bits.dataAvailable = 0;
        }
        break;

    case COM5025_PIN_OUT_TXACT: // Transmitter Active - updates RQTS only
        data->txTransferStatus.bits.transmitterActive = value ? 1 : 0;
        hdlc_update_rqts(device);
        break;

    case COM5025_PIN_OUT_TBMT: // Transmitter Buffer Empty - triggers interrupt check
        data->txTransferStatus.bits.transmitBufferEmpty = value ? 1 : 0;
        hdlc_check_trigger_interrupt(device);
        break;

    case COM5025_PIN_OUT_TSA: // Transmitter Status Available - triggers interrupt check
        data->txTransferStatus.bits.transmitterUnderrun = value ? 1 : 0;
        hdlc_check_trigger_interrupt(device);
        break;

    case COM5025_PIN_OUT_RSA: // Receiver Status Available - triggers interrupt check on rising edge only (matches C#)
        if (value)
        {
            data->rxTransferStatus.bits.statusAvailable = 1;
            hdlc_check_trigger_interrupt(device);
        }
        else
        {
            data->rxTransferStatus.bits.statusAvailable = 0;
        }
        break;

    case COM5025_PIN_OUT_TSO: // Transmitter Serial Output
        break;

    default:
        break;
    }
}

static void hdlc_check_trigger_interrupt_request_12(Device *self)
{
    if (!self)
    {
        return;
    }

    HDLCData *data = (HDLCData *)self->deviceData;
    if (!data)
    {
        return;
    }

    // Check if TRANSMIT interrupt is enabled for modem status change
    if (!data->txTransferControl.bits.modemStatusChangeIE)
    {
        return;
    }

    // Check if there's a signal difference between modem signals and status register
    uint16_t write_signals = data->txTransferStatus.raw & data->txModemFlagsMask.raw;
    uint16_t modem_signals = data->txModemFlags.raw & data->txModemFlagsMask.raw;

    // TMCS (Transmit Modem Status Change) set => Trigger interrupt 12 on change
    bool tmcs = (write_signals != modem_signals);
    if (tmcs)
    {
        HDLC_LOG_IRQ(12, "TX modem status change");
        data->irq12Count++;
        dev_set_interrupt_status(self, true, 12);
    }
}

static void hdlc_check_trigger_interrupt_request_13(Device *self)
{
    if (!self)
    {
        return;
    }

    HDLCData *data = (HDLCData *)self->deviceData;
    if (!data)
    {
        return;
    }

    // Check if RECEIVE interrupt is enabled for modem status change
    if (!data->rxTransferControl.bits.modemStatusChangeIE)
    {
        return;
    }

    // Check if there's a signal difference between modem signals and status register
    uint16_t read_signals = data->rxTransferStatus.raw & data->rxModemFlagsMask.raw;
    uint16_t modem_signals = data->rxModemFlags.raw & data->rxModemFlagsMask.raw;

    // RMSC (Receive Modem Status Change) set => Trigger interrupt 13 on change
    bool rmsc = (read_signals != modem_signals);
    if (rmsc)
    {
        HDLC_LOG_IRQ(13, "RX modem status change");
        data->irq13_modem++;

        // Latch modem flags into status register NOW so the mismatch resolves.
        // Without this, the same mismatch fires on every IOX+11 write (because
        // SINTRAN re-enables modemStatusChangeIE before reading IOX+10).
        // IOX+10 read also latches - this is the same operation done proactively.
        data->rxTransferStatus.raw &= ~(data->rxModemFlagsMask.raw);
        data->rxTransferStatus.raw |= (data->rxModemFlags.raw & data->rxModemFlagsMask.raw);

        dev_set_interrupt_status(self, true, 13);
    }
}

static void hdlc_check_trigger_interrupt(Device *self)
{
    if (!self)
    {
        return;
    }

    HDLCData *data = (HDLCData *)self->deviceData;
    if (!data)
    {
        return;
    }

    /*** LEVEL 13 RX ***/
    if (data->rxTransferStatus.bits.dataAvailable)
    {
        if (data->rxTransferControl.bits.enableReceiverDMA)
        {
            data->rxTransferStatus.bits.dataAvailable = 0;
        }
        if (data->rxTransferControl.bits.dataAvailableIE)
        {
            data->irq13_dataAvail++;
            dev_set_interrupt_status(self, true, 13);
        }
    }

    if (data->rxTransferStatus.bits.statusAvailable)
    {
        if (data->rxTransferControl.bits.enableReceiverDMA)
        {
            data->rxTransferStatus.bits.statusAvailable = 0;
        }
        if (data->rxTransferControl.bits.statusAvailableIE)
        {
            data->irq13_statusAvail++;
            dev_set_interrupt_status(self, true, 13);
        }
    }

    /*** LEVEL 12 TX ***/
    if (data->txTransferStatus.bits.transmitBufferEmpty &&
        data->txTransferControl.bits.transmitBufferEmptyIE)
    {
        HDLC_LOG_IRQ(12, "TX BufferEmpty + BufferEmptyIE");
        data->irq12Count++;
        dev_set_interrupt_status(self, true, 12);
    }

    if (data->txTransferStatus.bits.transmitterUnderrun &&
        data->txTransferControl.bits.transmitterUnderrunIE)
    {
        HDLC_LOG_IRQ(12, "TX Underrun + UnderrunIE");
        dev_set_interrupt_status(self, true, 12);
    }
}

static void hdlc_update_rqts(Device *self)
{
    if (!self)
    {
        return;
    }

    HDLCData *data = (HDLCData *)self->deviceData;
    if (!data)
    {
        return;
    }

    // In maintenance mode, always set RTS
    if (data->maintenanceMode)
    {
        // Set modem RTS signal to true (equivalent to C# modem.SetRTS(true))
        if (data->modem)
        {
            modem_set_rts(data->modem, true);
        }
        return;
    }

    // A = Negated Signal Detect
    bool a = !(data->rxModemFlags.bits.signalDetector);

    // B = RequestToSend OR TransmitterActive, then negated
    bool b =
        data->txTransferControl.bits.requestToSend || data->txTransferStatus.bits.transmitterActive;
    b = !b; // Negate B

    // C = Half Duplex mode
    bool c = data->txTransferControl.bits.halfDuplex;

    // RQTS is a 3-input Negated-OR (NOR) from A, B and C
    bool new_rqts = !(a || b || c);

    // Set modem RTS signal to new_rqts value (equivalent to C# modem.SetRTS(new_rqts))
    if (data->modem)
    {
        modem_set_rts(data->modem, new_rqts);
    }
}

// Core HDLC functionality implementation complete.
// =========================================================
// Public accessors for external inspection (menu, debugging)
// =========================================================

bool hdlc_get_rx_frame_status(const HDLCData *data, HDLCRxFrameStatus *status)
{
    if (!data || !status)
    {
        return false;
    }
    memset(status, 0, sizeof(*status));

    if (!data->dmaEngine || !data->dmaEngine->dmaCB)
    {
        return false;
    }

    DMAControlBlocks *cb = (DMAControlBlocks *)data->dmaEngine->dmaCB;

    // RX status
    status->rxDmaEnabled = data->rxTransferControl.bits.enableReceiverDMA;
    status->rxEnabled = data->rxTransferControl.bits.enableReceiver;
    HDLCFrame *rx_frame = cb->hdlcReceiveFrame;
    if (rx_frame)
    {
        status->state = (int)rx_frame->state;
        status->frameLength = rx_frame->frameLength;
    }
    status->rxDcbReady = (cb->rxDCB != NULL);

    DMAReceiver *rx = (DMAReceiver *)data->dmaEngine->receiver;
    if (rx)
    {
        status->tcpQueueUsed = rxbuf_available(&rx->tcpReceiveBuffer);
    }

    // TX status
    status->txDmaEnabled = data->txTransferControl.bits.enableTransmitterDMA;
    status->txEnabled = data->txTransferControl.bits.transmitterEnabled;
    status->txSenderState = cb->dmaSenderState;
    status->txWaitTicks = cb->dmaWaitTicks;

    // TX diagnostics
    status->txStarts = data->txStarts;
    status->txSendCalls = data->txSendCalls;
    status->txAlreadySent = data->txAlreadySent;

#ifdef MODEM_HAS_NETWORKING
    if (data->modem)
    {
        // Lock-free read - head/tail are simple ints, safe for approximate queue depth.
        // Do NOT lock the mutex here: this runs in the emulation thread during F12 display,
        // and locking can deadlock with queue_write_all spinning on the worker thread.
        int h = data->modem->txQueue.head;
        int t = data->modem->txQueue.tail;
        status->txQueueUsed = (h - t + MODEM_QUEUE_SIZE) % MODEM_QUEUE_SIZE;
    }
#endif

    return true;
}

// Additional functions are implemented in their respective modules:
// - COM5025 chip: chip_com5025.c
// - Modem: modem.c
// - DMA Engine: dma_engine.c, dma_transmitter.c, dma_receiver.c
// - Control Blocks: dma_control_blocks.c
