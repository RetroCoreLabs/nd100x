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

#include "../devices_types.h"
#include "../devices_protos.h"

#include "device_terminal.h"


// Device definitions array
static const DeviceDefinition deviceDefinitions[] = {
    // Group 1 (starts at offset 0)
    {0300, 01, 01, "CONSOLE TERMINAL - TERMINAL 1"},
    {0310, 0121, 011, "TERMINAL 2/ TET15"},
    {0320, 0122, 042, "TERMINAL 3/ TET14"},
    {0330, 0123, 043, "TERMINAL 4/ TET13"},
    {0340, 044, 044, "TERMINAL 5/ TET12"},
    {0350, 045, 045, "TERMINAL 6/ TET11"},
    {0360, 046, 046, "TERMINAL 7/ TET10"},
    {0370, 047, 047, "TERMINAL 8/ TET9"},

    // Group 9
    {01300, 050, 060, "TERMINAL 9"},
    {01310, 051, 061, "TERMINAL 10"},
    {01320, 052, 062, "TERMINAL 11"},
    {01330, 053, 063, "TERMINAL 12"},
    {01340, 054, 064, "TERMINAL 13"},
    {01350, 055, 065, "TERMINAL 14"},
    {01360, 056, 066, "TERMINAL 15"},
    {01370, 057, 067, "TERMINAL 16"},

    // Group 17
    {0200, 060, 07, "TERMINAL 17"},
    {0210, 061, 017, "TERMINAL 18"},
    {0220, 062, 052, "TERMINAL 19"},
    {0230, 063, 053, "TERMINAL 20"},
    {0240, 064, 054, "TERMINAL 21"},
    {0250, 065, 055, "TERMINAL 22"},
    {0260, 066, 056, "TERMINAL 23"},
    {0270, 067, 057, "TERMINAL 24"},

    // Group 25
    {01200, 070, 070, "TERMINAL 25"},
    {01210, 071, 071, "TERMINAL 26"},
    {01220, 072, 072, "TERMINAL 27"},
    {01230, 073, 073, "TERMINAL 28"},
    {01240, 074, 074, "TERMINAL 29/PHOTOS.1"},
    {01250, 075, 075, "TERMINAL 30/PHOTOS.2"},
    {01260, 076, 076, "TERMINAL 31/PHOTOS.3"},
    {01270, 077, 077, "TERMINAL 32/PHOTOS.4"},

    // Group 33
    {0640, 0124, 01040, "TERMINAL 33"},
    {0650, 0125, 01041, "TERMINAL 34"},
    {0660, 0126, 01042, "TERMINAL 35"},
    {0670, 0127, 01043, "TERMINAL 36"},

    // Group 37
    {01100, 0130, 01044, "TERMINAL 37"},
    {01110, 0131, 01045, "TERMINAL 38"},
    {01120, 0132, 01046, "TERMINAL 39"},
    {01130, 0133, 01047, "TERMINAL 40"},
    {01140, 0134, 01050, "TERMINAL 41"},
    {01150, 0135, 01051, "TERMINAL 42"},
    {01160, 0136, 01052, "TERMINAL 43"},
    {01170, 0137, 01053, "TERMINAL 44"},

    // Group 45
    {01400, 0140, 01054, "TERMINAL 45"},
    {01410, 0141, 01055, "TERMINAL 46"},
    {01420, 0142, 01056, "TERMINAL 47"},
    {01430, 0143, 01057, "TERMINAL 48"},

    // Group 49
    {01500, 0144, 01060, "TERMINAL 49"},
    {01510, 0145, 01061, "TERMINAL 50"},
    {01520, 0146, 01062, "TERMINAL 51"},
    {01530, 0147, 01063, "TERMINAL 52"}};

static const size_t numDeviceDefinitions = sizeof(deviceDefinitions) / sizeof(DeviceDefinition);

// Forward declaration of WriteEnd
static bool WriteEnd(void *context, int param);


static void Terminal_Reset(Device *self)
{
    TerminalData *data = (TerminalData *)self->deviceData;
    if (!data)
    {
        return;
    }

    // Clear input status and make device active
    data->inputStatus.raw = 0;
    data->inputStatus.bits.deviceActivated = true;
    data->inputStatus.bits.deviceReadyForTransfer = false;

    // Clear output status
    data->outputStatus.raw = 0;
    data->outputStatus.bits.readyForTransfer = true;

    // Clear other
}

static uint16_t Terminal_Tick(Device *self)
{
    if (!self)
    {
        return 0;
    }

    TerminalData *data = (TerminalData *)self->deviceData;
    if (!data)
    {
        return 0;
    }

    // Process I/O delays
    Device_TickIODelay(self);

    // Check for new incoming characters

    data->checkInputQueueTick++;
    if (data->checkInputQueueTick > MAX_TICKS)
    {
        data->checkInputQueueTick = 0;

        if ((data->inputQueue.count > 0) && (!data->inputStatus.bits.deviceReadyForTransfer) &&
            (data->outputStatus.bits.readyForTransfer))
        {

            uint16_t value = data->inputQueue.buffer[data->inputQueue.head];
            data->inputQueue.head = (data->inputQueue.head + 1) % TERMINAL_QUEUE_SIZE;
            data->inputQueue.count--;

            // Process character based on length
            switch (data->inputControl.bits.characterLength) // 0=8, 1=7, 2=6, 3=5)
            {
            case 0: // 8 bits
                value &= 0xFF;
                if (Device_GetOddParity(value) == 1)
                {
                    value |= (1 << 7);
                }
                break;

            case 1: // 7 bits
                value &= 0x7F;
                if (data->inputControl.bits.parityGeneration)
                {
                    if (Device_GetOddParity(value) == 1)
                    {
                        value |= (1 << 7);
                    }
                }
                break;

            case 2: // 6 bits
                value &= 0x3F;
                break;

            case 3: // 5 bits
                value &= 0x1F;
                break;
            default:
                break;
            }

            data->uartInputBuf = value;
            data->inputStatus.bits.deviceReadyForTransfer = true;
            Device_SetInterruptStatus(self,
                                      data->inputStatus.bits.interruptEnabled &&
                                          data->inputStatus.bits.deviceReadyForTransfer,
                                      12);
        }
    }

    return self->interruptBits;
}

static uint16_t Terminal_Read(Device *self, uint32_t address)
{
    if (!self)
    {
        return 0;
    }

    TerminalData *data = (TerminalData *)self->deviceData;
    uint16_t value = 0;
    uint32_t reg = Device_RegisterAddress(self, address);

    switch (reg)
    {
    case TERMINAL_READ_INPUT_DATA:
        value = data->uartInputBuf;

        data->uartInputBuf = 0;
        data->inputStatus.bits.deviceReadyForTransfer = false;

        Device_SetInterruptStatus(self,
                                  data->inputStatus.bits.interruptEnabled &&
                                      data->inputStatus.bits.deviceReadyForTransfer,
                                  12);

        break;

    case TERMINAL_READ_INPUT_STATUS:
        value = data->inputStatus.raw;
        break;

    case TERMINAL_READ_RETURN0:
        value = 0;
        break;

    case TERMINAL_READ_OUTPUT_STATUS:
        value = data->outputStatus.raw;
        break;
    default:
        break;
    }

    if (Log_IsEnabled(LOG_CAT_TERM, LOG_DEBUG))
    {
        if (reg != TERMINAL_READ_INPUT_DATA)
        {
            Log_Write(LOG_CAT_TERM, LOG_DEBUG, "Terminal Reading from address: %o value: %o\n",
                      address, value);
        }
    }

    return value;
}

// Load the input control word: interrupt/activate bits into the input
// status, device clear, and clearing of the error bits.
static void terminal_write_input_control(Device *self, TerminalData *data, uint16_t value)
{
    // make a copy of the control word
    data->inputControl.raw = value;

    // Update status register
    data->inputStatus.bits.interruptEnabled = data->inputControl.bits.interruptEnabled;
    data->inputStatus.bits.deviceActivated = data->inputControl.bits.deviceActivated;

    // Trigger interrupt ?
    Device_SetInterruptStatus(self,
                              data->inputStatus.bits.interruptEnabled &&
                                  data->inputStatus.bits.deviceReadyForTransfer,
                              12);

    if (data->inputControl.bits.deviceClear)
    {
        // Clear input status
        data->inputStatus.raw = 0;
        data->inputStatus.bits.deviceActivated = true;

        // Clear output status
        data->outputStatus.raw = 0;
        data->outputStatus.bits.readyForTransfer = true;
    }

    // Clear errors
    data->inputStatus.bits.framingError = false;
    data->inputStatus.bits.parityError = false;
    data->inputStatus.bits.overrunError = false;
}

static void Terminal_Write(Device *self, uint32_t address, uint16_t value)
{
    if (!self)
    {
        return;
    }

    TerminalData *data = (TerminalData *)self->deviceData;
    uint32_t reg = Device_RegisterAddress(self, address);

    if (Log_IsEnabled(LOG_CAT_TERM, LOG_DEBUG))
    {
        if (reg != TERMINAL_WRITE_DATA)
        {
            Log_Write(LOG_CAT_TERM, LOG_DEBUG, "Terminal Writing value: %o to address: %o\n", value,
                      address);
        }
    }

    switch (reg)
    {
    case TERMINAL_WRITE_NO_OPERATION:
        // Do nothing
        break;

    case TERMINAL_WRITE_INPUT_CONTROL:
        terminal_write_input_control(self, data, value);
        break;

    case TERMINAL_WRITE_DATA:
        if (value == 0)
        {
            break;
        }

        char c = (char)(value);

        // seems like we need to strip the parity bit always
        //if (data->inputControl.bits.characterLength != 0)

        c &= 0x7F;

        // Clear interrupt status
        data->outputStatus.bits.readyForTransfer = false;
        Device_SetInterruptStatus(self,
                                  data->outputStatus.bits.interruptEnabled &&
                                      data->outputStatus.bits.readyForTransfer,
                                  10);

        if (data->inputControl.bits.testMode)
        {
            // In test mode, echo the character back with parity
            Terminal_QueueKeyCode(self, c);
        }
        else
        {
            // Use character device callback if available
            if (self->deviceClass == DEVICE_CLASS_CHARACTER && self->charCallbacks.outputFunc)
            {
                Device_OutputCharacter(self, c);
            }
            else
            {
                // Fallback to printf if no callback is set
                printf("%c", c);
            }
        }

        // Simulate transfer delay
        Device_QueueIODelay(self, IODELAY_TERMINAL, WriteEnd, self->identCode,
                            self->interruptLevel);
        break;

    case TERMINAL_WRITE_SET_OUTPUT_CONTROL:
        // make a copy of the control word
        data->outputControl.raw = value;

        // Update status register
        data->outputStatus.bits.interruptEnabled = data->outputControl.bits.interruptEnabled;

        // Trigger interrupt ?
        Device_SetInterruptStatus(self,
                                  data->outputStatus.bits.interruptEnabled &&
                                      data->outputStatus.bits.readyForTransfer,
                                  10);
        break;
    default:
        break;
    }
}

static uint16_t Terminal_Ident(Device *self, uint16_t level)
{
    if (!self)
    {
        return 0;
    }

    if ((self->interruptBits & (1 << level)) != 0)
    {
        TerminalData *data = (TerminalData *)self->deviceData;
        if (level == 12)
        { // input
            data->inputStatus.bits.interruptEnabled = false;
        }
        else if (level == 10)
        { // output
            data->outputStatus.bits.interruptEnabled = false;
        }
        Device_SetInterruptStatus(self, false, level);
        return self->identCode;
    }
    return 0;
}

static bool WriteEnd(void *context, int param)
{
    (void)param;
    Device *self = (Device *)context;
    if (!self)
    {
        return false;
    }

    TerminalData *data = (TerminalData *)self->deviceData;
    if (!data)
    {
        return false;
    }

    data->outputStatus.bits.readyForTransfer = true;
    data->checkInputQueueTick = 0; // Make sure input check is delayed

    Device_SetInterruptStatus(
        self, data->outputStatus.bits.interruptEnabled && data->outputStatus.bits.readyForTransfer,
        10);

    return false;
}

// Add QueueKeyCode implementation
void Terminal_QueueKeyCode(Device *self, uint8_t keycode)
{
    if (!self)
    {
        return;
    }

    TerminalData *data = (TerminalData *)self->deviceData;
    if (!data)
    {
        return;
    }

    // Check if queue is full
    if (data->inputQueue.count >= TERMINAL_QUEUE_SIZE)
    {
        data->inputStatus.bits.overrunError = true;
        return;
    }

    // Add keycode to queue
    data->inputQueue.buffer[data->inputQueue.tail] = keycode;
    data->inputQueue.tail = (data->inputQueue.tail + 1) % TERMINAL_QUEUE_SIZE;
    data->inputQueue.count++;
}

// Character input handler for terminal devices
static void Terminal_InputFunction(Device *device, char c)
{
    if (!device)
    {
        return;
    }
    Terminal_QueueKeyCode(device, (uint8_t)c);
}


Device *CreateTerminalDevice(uint8_t thumbwheel)
{
    Device *dev = malloc(sizeof(Device));
    if (!dev)
    {
        return NULL;
    }

    TerminalData *data = malloc(sizeof(TerminalData));
    if (!data)
    {
        free(dev);
        return NULL;
    }

    // Initialize device base structure as a character device
    Device_Init(dev, thumbwheel, DEVICE_CLASS_CHARACTER, 0);

    // Set up device-specific data
    memset(data, 0, sizeof(TerminalData));

    // Find device definition
    const DeviceDefinition *def = NULL;
    if (thumbwheel < numDeviceDefinitions)
    {
        if (thumbwheel > 0)
        {
            thumbwheel--; // Offset into definitions is 0
        }
        def = &deviceDefinitions[thumbwheel];
    }
    else
    {
        LOG(LOG_CAT_TERM, LOG_WARN, "Unexpected thumbwheel code %d\n", thumbwheel);
        free(data);
        free(dev);
        return NULL;
    }

    // Set up device properties
    dev->identCode = def->identCode;
    dev->startAddress = def->addressBase;
    dev->logicalDevice = def->logicalDevice;
    dev->endAddress = def->addressBase + 7;
    dev->interruptLevel = 10; // 10 = output, 12 = input
    snprintf(dev->memoryName, sizeof(dev->memoryName), "%s", def->deviceName);

    // Set up device function pointers
    dev->Reset = Terminal_Reset;
    dev->Tick = Terminal_Tick;
    dev->Read = Terminal_Read;
    dev->Write = Terminal_Write;
    dev->Ident = Terminal_Ident;
    dev->deviceData = data;

    // Initialize input queue
    data->inputQueue.head = 0;
    data->inputQueue.tail = 0;
    data->inputQueue.count = 0;

    // Ready to be written to
    data->outputStatus.bits.readyForTransfer = true;


    // hook up  Device_SetCharacterInput to Terminal_QueueKeyCode
    Device_SetCharacterInput(dev, Terminal_InputFunction);

    LOG(LOG_CAT_TERM, LOG_INFO, "Terminal %d created (%s, ident %o, address %o)\n",
        dev->logicalDevice, dev->memoryName, dev->identCode, dev->startAddress);
    return dev;
}
