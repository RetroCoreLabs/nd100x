/*
 * nd100x - ND100 Virtual Machine
 *
 * Copyright (c) 2025 Ronny Hansen
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

#ifndef TELNET_SERVER_H
#define TELNET_SERVER_H

#include <stdint.h>
#include <stdbool.h>

struct Device; // Forward declaration - no dependency on devices_types.h

typedef enum
{
    TRANSPORT_TELNET = 0,
    // TRANSPORT_SSH = 1,  // Phase 2
} TransportType;

// Function pointer types for terminal I/O
typedef void (*TelnetInputFunc)(struct Device *device, uint8_t keycode);
typedef void (*TelnetOutputFunc)(struct Device *device, char c);
typedef void (*CarrierFunc)(struct Device *device, bool missing);

// Terminal registration info
typedef struct
{
    struct Device *device;
    uint16_t identCode;
    uint16_t ioAddress;
    const char *name;
    TelnetInputFunc inputFunc;
    TelnetOutputFunc origOutput; // Original VScreen handler (for chaining)
    CarrierFunc carrierFunc;
} TelnetTerminalInfo;

// Server configuration
typedef struct
{
    int port;           // Default 9000
    int maxConnections; // Default 8
    TransportType transport;
} TelnetServerConfig;

typedef struct TelnetServer TelnetServer;

// Lifecycle
TelnetServer *TelnetServer_Create(const TelnetServerConfig *config);
bool TelnetServer_RegisterTerminal(TelnetServer *server, const TelnetTerminalInfo *info);
bool TelnetServer_Start(TelnetServer *server);
void TelnetServer_Stop(TelnetServer *server);
void TelnetServer_Destroy(TelnetServer *server);

// Status query (for F12 menu)
int TelnetServer_GetTerminalCount(TelnetServer *server);
bool TelnetServer_GetTerminalStatus(TelnetServer *server, int index, const char **name,
                                    uint16_t *identCode, bool *connected, bool *locallyActive,
                                    char *clientAddr, int addrLen);
bool TelnetServer_GetTerminalStats(TelnetServer *server, int index, uint64_t *bytesRx,
                                   uint64_t *bytesTx);
bool TelnetServer_DisconnectTerminal(TelnetServer *server, int index);
bool TelnetServer_DisconnectDevice(TelnetServer *server, struct Device *device);

/**
 * @brief Character-output callback that routes a terminal device's output to
 *        the telnet client attached to it (install with Device_SetCharacterOutput).
 * @param device The terminal device producing the character.
 * @param c      The character.
 */
void telnet_output_handler(struct Device *device, char c);
int TelnetServer_GetPort(TelnetServer *server);

// Local activity state (mutual exclusion with VScreen)
bool TelnetServer_SetTerminalLocallyActive(TelnetServer *server, int index, bool active);
bool TelnetServer_SetDeviceLocallyActive(TelnetServer *server, struct Device *device, bool active);
bool TelnetServer_IsDeviceConnected(TelnetServer *server, struct Device *device);
void TelnetServer_ClearDeviceCarrier(TelnetServer *server, struct Device *device);

// Get the IP:port string for the client connected to a device (NULL if not connected)
const char *TelnetServer_GetDeviceClientAddr(TelnetServer *server, struct Device *device);

// Pending client management (connected but not yet assigned to a terminal)
int TelnetServer_GetPendingCount(TelnetServer *server);
bool TelnetServer_GetPendingInfo(TelnetServer *server, int index, char *addrBuf, int addrBufLen,
                                 int *ageSecs, uint64_t *bytesRx, uint64_t *bytesTx);
bool TelnetServer_DropPending(TelnetServer *server, int index);
void TelnetServer_DropAllPending(TelnetServer *server);

#endif // TELNET_SERVER_H
