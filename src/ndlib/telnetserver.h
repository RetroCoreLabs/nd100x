/*
 * telnetserver.h - Telnet server: configuration, terminal registration and status API.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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

#ifndef TELNETSERVER_H
#define TELNETSERVER_H

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

/**
 * @brief Allocate a telnet server, copy the configuration and initialise the
 *        terminal and pending-client tables and their mutexes.
 * @details A port of 0 or less becomes 9000 and a maxConnections of 0 or less
 *          becomes 8. No socket is opened until TelnetServer_Start().
 * @param config Configuration to copy; must not be NULL.
 * @return New server the caller frees with TelnetServer_Destroy(), or NULL if
 *         allocation failed.
 */
TelnetServer *telnet_create(const TelnetServerConfig *config);

/**
 * @brief Add one terminal device to the server's table of terminals a client
 *        can be attached to.
 * @param server The server.
 * @param info   Registration record; copied into the server.
 * @return true on success; false if server or info is NULL, or the table is
 *         already full (TELNET_MAX_TERMINALS entries).
 */
bool telnet_register_terminal(TelnetServer *server, const TelnetTerminalInfo *info);

/**
 * @brief Open the listening TCP socket on the configured port and start the
 *        accept thread.
 * @details Initialises Winsock on Windows, creates a loopback socket pair used
 *          to wake the accept thread on shutdown, binds with SO_REUSEADDR to
 *          INADDR_ANY and listens with a backlog of 4.
 * @param server The server; must have at least one registered terminal and
 *               must not already be running.
 * @return true when the server is listening; false on any setup failure, in
 *         which case every socket opened here is closed again.
 */
bool telnet_start(TelnetServer *server);

/**
 * @brief Stop the server: wake and join the accept thread, close the listening
 *        socket, disconnect every client and join its thread, close the wake
 *        socket pair and release Winsock.
 * @param server The server; NULL or a server that is not running is ignored.
 */
void telnet_stop(TelnetServer *server);

/**
 * @brief Stop the server if it is still running, destroy the per-terminal and
 *        pending mutexes, close any pending client sockets and free the
 *        server.
 * @param server The server; NULL is ignored.
 */
void telnet_destroy(TelnetServer *server);

/**
 * @brief Number of terminals registered with the server.
 * @param server The server; NULL yields 0.
 * @return Registered terminal count.
 */
int telnet_get_terminal_count(TelnetServer *server);

/**
 * @brief Read one terminal's name, IDENT code, connection state, local
 *        activity flag and client address. Used by the F12 menu.
 * @param server        The server.
 * @param index         Terminal index in [0, TelnetServer_GetTerminalCount()).
 * @param name          Receives the terminal name, owned by the server; may
 *                      be NULL.
 * @param identCode     Receives the IDENT code; may be NULL.
 * @param connected     Receives true when a telnet client holds the terminal;
 *                      may be NULL.
 * @param locallyActive Receives true when the local screen holds it; may be
 *                      NULL.
 * @param clientAddr    Receives the client "IP:port" string, truncated to
 *                      fit; may be NULL.
 * @param addrLen       Size of clientAddr in bytes.
 * @return true on success; false if server is NULL or index is out of range.
 */
bool TelnetServer_GetTerminalStatus(TelnetServer *server, int index, const char **name,
                                    uint16_t *ident_code, bool *connected, bool *locally_active,
                                    char *client_addr, int addr_len);

/**
 * @brief Read one terminal's byte counters.
 * @param server  The server.
 * @param index   Terminal index in [0, TelnetServer_GetTerminalCount()).
 * @param bytesRx Receives bytes received from the client; may be NULL.
 * @param bytesTx Receives bytes sent to the client; may be NULL.
 * @return true on success; false if server is NULL or index is out of range.
 */
bool telnet_get_terminal_stats(TelnetServer *server, int index, uint64_t *bytes_rx,
                               uint64_t *bytes_tx);

/**
 * @brief Disconnect the telnet client attached to a device: close its socket,
 *        join its client thread, signal carrier missing and clear the stored
 *        client address.
 * @param server The server.
 * @param device The terminal device whose client should be dropped.
 * @return true if a client was disconnected; false if server or device is
 *         NULL, the device is not registered, or it had no client.
 */
bool telnet_disconnect_device(TelnetServer *server, struct Device *device);

/**
 * @brief Character-output callback that routes a terminal device's output to
 *        the telnet client attached to it (install with Device_SetCharacterOutput).
 * @param device The terminal device producing the character.
 * @param c      The character.
 */
void telnet_output_handler(struct Device *device, char c);
/**
 * @brief TCP port the server listens on.
 * @param server The server; NULL yields 0.
 * @return The configured port number.
 */
int telnet_get_port(TelnetServer *server);

/**
 * @brief Mark a terminal as held by the local screen, which excludes it from
 *        the telnet terminal picker (mutual exclusion with VScreen).
 * @param server The server.
 * @param index  Terminal index in [0, TelnetServer_GetTerminalCount()).
 * @param active true when the local screen holds the terminal.
 * @return true on success; false if server is NULL or index is out of range.
 */
bool telnet_set_terminal_locally_active(TelnetServer *server, int index, bool active);

/**
 * @brief Same as TelnetServer_SetTerminalLocallyActive() but naming the
 *        terminal by its device.
 * @param server The server.
 * @param device The terminal device.
 * @param active true when the local screen holds the terminal.
 * @return true on success; false if server or device is NULL or the device is
 *         not registered.
 */
bool telnet_set_device_locally_active(TelnetServer *server, struct Device *device, bool active);

/**
 * @brief Whether a telnet client is currently attached to a device.
 * @param server The server.
 * @param device The terminal device.
 * @return true if the device is registered and holds an open client socket;
 *         false otherwise.
 */
bool telnet_is_device_connected(TelnetServer *server, struct Device *device);

/**
 * @brief Call the device's carrier callback with "carrier present", used when
 *        a client takes the terminal.
 * @param server The server; NULL is ignored.
 * @param device The terminal device; NULL, or one with no carrier callback,
 *               is ignored.
 */
void telnet_clear_device_carrier(TelnetServer *server, struct Device *device);

/**
 * @brief Address of the telnet client attached to a device.
 * @param server The server.
 * @param device The terminal device.
 * @return "IP:port" string owned by the server, or NULL when server or device
 *         is NULL, the device is not registered, or no client is attached.
 */
const char *telnet_get_device_client_addr(TelnetServer *server, struct Device *device);

/**
 * @brief Number of clients that have connected but are not yet assigned to a
 *        terminal.
 * @param server The server; NULL yields 0.
 * @return Pending client count, read under the pending mutex.
 */
int telnet_get_pending_count(TelnetServer *server);

/**
 * @brief Read one pending client's address, age and byte counters.
 * @param server     The server.
 * @param index      Pending index in [0, TelnetServer_GetPendingCount()).
 * @param addrBuf    Receives the "IP:port" string, truncated to fit; may be
 *                   NULL.
 * @param addrBufLen Size of addrBuf in bytes.
 * @param ageSecs    Receives seconds since the client connected; may be NULL.
 * @param bytesRx    Receives bytes received from the client; may be NULL.
 * @param bytesTx    Receives bytes sent to the client; may be NULL.
 * @return true on success; false if server is NULL or index is out of range.
 */
bool telnet_get_pending_info(TelnetServer *server, int index, char *addr_buf, int addr_buf_len,
                             int *age_secs, uint64_t *bytes_rx, uint64_t *bytes_tx);

/**
 * @brief Send "Disconnected by operator." to one pending client and remove it
 *        from the pending list.
 * @param server The server.
 * @param index  Pending index in [0, TelnetServer_GetPendingCount()).
 * @return true on success; false if server is NULL or index is out of range.
 */
bool telnet_drop_pending(TelnetServer *server, int index);

/**
 * @brief Send "Disconnected by operator." to every pending client and empty
 *        the pending list.
 * @param server The server; NULL is ignored.
 */
void telnet_drop_all_pending(TelnetServer *server);

#endif // TELNETSERVER_H
