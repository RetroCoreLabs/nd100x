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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>      /* winpthreads on MinGW; libpthread on POSIX */
#include <stdatomic.h>    /* C11 atomics - supported by MinGW-w64 GCC */

#include "net_compat.h"   /* sockets, poll, loopback wake-pair */
#include "telnetserver.h"
#include "ndlib_types.h"
#include "ndlib_protos.h"

#define TELNET_MAX_TERMINALS 16
#define TELNET_MAX_PENDING 8
#define TELNET_OUTPUT_BUF_SIZE 4096
#define TELNET_PENDING_TIMEOUT 60  // seconds before auto-disconnect

// Telnet protocol constants
#define IAC   255
#define WILL  251
#define WONT  252
#define DO    253
#define DONT  254
#define SB    250
#define SE    240

#define OPT_ECHO     1
#define OPT_SGA      3
#define OPT_LINEMODE 34

// Telnet IAC state machine states
typedef enum {
    TELNET_STATE_DATA,
    TELNET_STATE_IAC,
    TELNET_STATE_WILL,
    TELNET_STATE_WONT,
    TELNET_STATE_DO,
    TELNET_STATE_DONT,
    TELNET_STATE_SB,
    TELNET_STATE_SB_DATA,
    TELNET_STATE_SB_IAC,
} TelnetIACState;

typedef struct {
    TelnetTerminalInfo info;
    nd_socket_t clientFd;
    pthread_t clientThread;
    bool clientThreadActive;
    bool locallyActive;         // Terminal claimed by local VScreen
    char clientAddrStr[48];
    // Byte counters (total since server start, survive reconnects)
    volatile uint64_t bytesRx;  // Bytes received from client
    volatile uint64_t bytesTx;  // Bytes sent to client
    // Output ring buffer (emulation thread -> client thread)
    uint8_t outputBuf[TELNET_OUTPUT_BUF_SIZE];
    volatile int outputHead;
    volatile int outputTail;
    pthread_mutex_t outputMutex;
} RegisteredTerminal;

// Pending client: connected to server but not yet assigned to a terminal
typedef struct {
    nd_socket_t fd;
    char addrStr[48];
    time_t connectTime;
    TelnetIACState iacState;
    uint64_t bytesRx;
    uint64_t bytesTx;
    int shownMap[TELNET_MAX_TERMINALS];  // Terminal indices as shown to this client
    int shownMapCount;
} PendingClient;

struct TelnetServer {
    TelnetServerConfig config;
    RegisteredTerminal terminals[TELNET_MAX_TERMINALS];
    int terminalCount;
    PendingClient pending[TELNET_MAX_PENDING];
    int pendingCount;
    pthread_mutex_t pendingMutex;
    pthread_t acceptThread;
    nd_socket_t listenFd;
    atomic_bool shouldExit;
    bool running;
    // Loopback socket pair used to wake the accept thread at shutdown.
    // shutdownPipe[0] = read end (polled), shutdownPipe[1] = write end (signalled).
    nd_socket_t shutdownPipe[2];
};

// Telnet initialization sequence: character mode, server echo
static const uint8_t telnet_init[] = {
    IAC, WILL, OPT_ECHO,       // Server will echo
    IAC, WILL, OPT_SGA,        // Suppress go-ahead (character-at-a-time)
    IAC, DONT, OPT_LINEMODE,   // Don't use linemode
};

// Forward declarations
static void *accept_thread_func(void *arg);
static void *client_thread_func(void *arg);
static RegisteredTerminal *find_by_device(TelnetServer *server, struct Device *device);
static void remove_pending(TelnetServer *server, int idx);

// Ring buffer write (called from emulation thread via output handler)
static void ringbuf_write(RegisteredTerminal *rt, uint8_t byte)
{
    pthread_mutex_lock(&rt->outputMutex);
    int next = (rt->outputHead + 1) % TELNET_OUTPUT_BUF_SIZE;
    if (next != rt->outputTail) {
        rt->outputBuf[rt->outputHead] = byte;
        rt->outputHead = next;
    }
    // If full, drop the byte (avoid blocking emulation thread)
    pthread_mutex_unlock(&rt->outputMutex);
}

// Ring buffer read (called from client thread)
static int ringbuf_read(RegisteredTerminal *rt, uint8_t *buf, int maxlen)
{
    pthread_mutex_lock(&rt->outputMutex);
    int count = 0;
    while (count < maxlen && rt->outputTail != rt->outputHead) {
        buf[count++] = rt->outputBuf[rt->outputTail];
        rt->outputTail = (rt->outputTail + 1) % TELNET_OUTPUT_BUF_SIZE;
    }
    pthread_mutex_unlock(&rt->outputMutex);
    return count;
}

// Output handler that chains to VScreen and queues for telnet client
void telnet_output_handler(struct Device *device, char c)
{
    // We need to find the server and terminal from device pointer.
    // Since we store a reference in a static, we use per-terminal approach.
    // The server pointer is embedded via closure-like technique using the
    // registered terminal's info.origOutput for chaining.
    // We'll use a global server pointer for the lookup.
    // This is set during TelnetServer_Start.
    extern TelnetServer *g_telnetServer;
    if (!g_telnetServer) return;

    RegisteredTerminal *rt = find_by_device(g_telnetServer, device);
    if (rt) {
        if (rt->clientFd >= 0) {
            ringbuf_write(rt, (uint8_t)c);
        }
        if (rt->info.origOutput) {
            rt->info.origOutput(device, c);
        }
    }
}

/*
 * Global server pointer for the telnet output handler.
 *
 * Thread safety: set once in TelnetServer_Start(), read by
 * telnet_output_handler() (called from emulation thread), and
 * cleared in TelnetServer_Stop(). Not protected by a mutex -
 * relies on start/stop ordering (start before emulation begins,
 * stop after emulation ends).
 */
TelnetServer *g_telnetServer = NULL;

static RegisteredTerminal *find_by_device(TelnetServer *server, struct Device *device)
{
    for (int i = 0; i < server->terminalCount; i++) {
        if (server->terminals[i].info.device == device) {
            return &server->terminals[i];
        }
    }
    return NULL;
}

TelnetServer *TelnetServer_Create(const TelnetServerConfig *config)
{
    TelnetServer *server = calloc(1, sizeof(TelnetServer));
    if (!server) return NULL;

    server->config = *config;
    if (server->config.port <= 0) server->config.port = 9000;
    if (server->config.maxConnections <= 0) server->config.maxConnections = 8;

    server->listenFd = ND_INVALID_SOCKET;
    server->shutdownPipe[0] = ND_INVALID_SOCKET;
    server->shutdownPipe[1] = ND_INVALID_SOCKET;
    atomic_store(&server->shouldExit, false);

    for (int i = 0; i < TELNET_MAX_TERMINALS; i++) {
        server->terminals[i].clientFd = ND_INVALID_SOCKET;
        pthread_mutex_init(&server->terminals[i].outputMutex, NULL);
    }

    server->pendingCount = 0;
    pthread_mutex_init(&server->pendingMutex, NULL);
    for (int i = 0; i < TELNET_MAX_PENDING; i++) {
        server->pending[i].fd = ND_INVALID_SOCKET;
    }

    return server;
}

bool TelnetServer_RegisterTerminal(TelnetServer *server, const TelnetTerminalInfo *info)
{
    if (!server || !info) return false;
    if (server->terminalCount >= TELNET_MAX_TERMINALS) return false;


    RegisteredTerminal *rt = &server->terminals[server->terminalCount];
    rt->info = *info;
    rt->clientFd = ND_INVALID_SOCKET;
    rt->clientThreadActive = false;
    rt->outputHead = 0;
    rt->outputTail = 0;
    memset(rt->clientAddrStr, 0, sizeof(rt->clientAddrStr));

    server->terminalCount++;
    return true;
}

bool TelnetServer_Start(TelnetServer *server)
{
    if (!server || server->running) return false;
    if (server->terminalCount == 0) return false;

    // Initialise Winsock (no-op on POSIX). Refcounted - paired with
    // nd_net_shutdown() in TelnetServer_Stop.
    if (nd_net_init() != 0) {
        Log(LOG_WARNING, "telnet: nd_net_init failed (err %d)\n", nd_last_socket_error());
        return false;
    }

    // Set global pointer for output handler
    g_telnetServer = server;

    // Loopback socket pair, used to wake the accept thread on shutdown.
    if (nd_wake_pair(server->shutdownPipe) != 0) {
        Log(LOG_WARNING, "telnet: nd_wake_pair failed (err %d)\n", nd_last_socket_error());
        nd_net_shutdown();
        return false;
    }

    // Create listening socket
    server->listenFd = (nd_socket_t)socket(AF_INET, SOCK_STREAM, 0);
    if (server->listenFd == ND_INVALID_SOCKET) {
        Log(LOG_WARNING, "telnet: socket() failed (err %d)\n", nd_last_socket_error());
        nd_socket_close(server->shutdownPipe[0]); server->shutdownPipe[0] = ND_INVALID_SOCKET;
        nd_socket_close(server->shutdownPipe[1]); server->shutdownPipe[1] = ND_INVALID_SOCKET;
        nd_net_shutdown();
        return false;
    }

    int opt = 1;
    setsockopt(ND_SOCK_NATIVE(server->listenFd), SOL_SOCKET, SO_REUSEADDR,
               (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(server->config.port);

    if (bind(ND_SOCK_NATIVE(server->listenFd), (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        Log(LOG_WARNING, "telnet: bind() failed on port %d (err %d)\n",
               server->config.port, nd_last_socket_error());
        nd_socket_close(server->listenFd);
        server->listenFd = ND_INVALID_SOCKET;
        nd_socket_close(server->shutdownPipe[0]); server->shutdownPipe[0] = ND_INVALID_SOCKET;
        nd_socket_close(server->shutdownPipe[1]); server->shutdownPipe[1] = ND_INVALID_SOCKET;
        nd_net_shutdown();
        return false;
    }

    if (listen(ND_SOCK_NATIVE(server->listenFd), 4) < 0) {
        Log(LOG_WARNING, "telnet: listen() failed (err %d)\n", nd_last_socket_error());
        nd_socket_close(server->listenFd);
        server->listenFd = ND_INVALID_SOCKET;
        nd_socket_close(server->shutdownPipe[0]); server->shutdownPipe[0] = ND_INVALID_SOCKET;
        nd_socket_close(server->shutdownPipe[1]); server->shutdownPipe[1] = ND_INVALID_SOCKET;
        nd_net_shutdown();
        return false;
    }

    atomic_store(&server->shouldExit, false);

    if (pthread_create(&server->acceptThread, NULL, accept_thread_func, server) != 0) {
        Log(LOG_WARNING, "telnet: pthread_create failed\n");
        nd_socket_close(server->listenFd);
        server->listenFd = ND_INVALID_SOCKET;
        nd_socket_close(server->shutdownPipe[0]); server->shutdownPipe[0] = ND_INVALID_SOCKET;
        nd_socket_close(server->shutdownPipe[1]); server->shutdownPipe[1] = ND_INVALID_SOCKET;
        nd_net_shutdown();
        return false;
    }

    server->running = true;
    Log(LOG_INFO, "Telnet server started on port %d (%d terminals available)\n",
           server->config.port, server->terminalCount);
    return true;
}

void TelnetServer_Stop(TelnetServer *server)
{
    if (!server || !server->running) return;

    atomic_store(&server->shouldExit, true);

    // Wake accept thread by writing one byte to the loopback socket pair.
    // The read end of the pair sits in the accept-thread's poll set.
    if (server->shutdownPipe[1] != ND_INVALID_SOCKET) {
        char dummy = 'x';
        (void)send(ND_SOCK_NATIVE(server->shutdownPipe[1]), &dummy, 1, 0);
    }

    // Close listening socket to unblock accept
    if (server->listenFd != ND_INVALID_SOCKET) {
        nd_socket_close(server->listenFd);
        server->listenFd = ND_INVALID_SOCKET;
    }

    // Wait for accept thread
    pthread_join(server->acceptThread, NULL);

    // Disconnect all clients
    for (int i = 0; i < server->terminalCount; i++) {
        RegisteredTerminal *rt = &server->terminals[i];
        if (rt->clientFd != ND_INVALID_SOCKET) {
            nd_socket_close(rt->clientFd);
            rt->clientFd = ND_INVALID_SOCKET;
        }
        if (rt->clientThreadActive) {
            pthread_join(rt->clientThread, NULL);
            rt->clientThreadActive = false;
        }
    }

    // Close shutdown socket pair
    if (server->shutdownPipe[0] != ND_INVALID_SOCKET) {
        nd_socket_close(server->shutdownPipe[0]);
        server->shutdownPipe[0] = ND_INVALID_SOCKET;
    }
    if (server->shutdownPipe[1] != ND_INVALID_SOCKET) {
        nd_socket_close(server->shutdownPipe[1]);
        server->shutdownPipe[1] = ND_INVALID_SOCKET;
    }

    server->running = false;
    g_telnetServer = NULL;
    Log(LOG_INFO, "Telnet server stopped\n");
    nd_net_shutdown();
}

void TelnetServer_Destroy(TelnetServer *server)
{
    if (!server) return;

    if (server->running) {
        TelnetServer_Stop(server);
    }

    for (int i = 0; i < TELNET_MAX_TERMINALS; i++) {
        pthread_mutex_destroy(&server->terminals[i].outputMutex);
    }

    // Close any pending clients
    for (int i = 0; i < TELNET_MAX_PENDING; i++) {
        if (server->pending[i].fd != ND_INVALID_SOCKET) {
            nd_socket_close(server->pending[i].fd);
            server->pending[i].fd = ND_INVALID_SOCKET;
        }
    }
    pthread_mutex_destroy(&server->pendingMutex);

    free(server);
}

int TelnetServer_GetTerminalCount(TelnetServer *server)
{
    return server ? server->terminalCount : 0;
}

bool TelnetServer_GetTerminalStatus(TelnetServer *server, int index,
    const char **name, uint16_t *identCode, bool *connected, bool *locallyActive,
    char *clientAddr, int addrLen)
{
    if (!server || index < 0 || index >= server->terminalCount) return false;

    RegisteredTerminal *rt = &server->terminals[index];
    if (name) *name = rt->info.name;
    if (identCode) *identCode = rt->info.identCode;
    if (connected) *connected = (rt->clientFd != ND_INVALID_SOCKET);
    if (locallyActive) *locallyActive = rt->locallyActive;
    if (clientAddr && addrLen > 0) {
        strncpy(clientAddr, rt->clientAddrStr, addrLen - 1);
        clientAddr[addrLen - 1] = '\0';
    }
    return true;
}

bool TelnetServer_DisconnectTerminal(TelnetServer *server, int index)
{
    if (!server || index < 0 || index >= server->terminalCount) return false;

    RegisteredTerminal *rt = &server->terminals[index];
    if (rt->clientFd == ND_INVALID_SOCKET) return false;

    // Close socket - client thread will detect and clean up
    nd_socket_close(rt->clientFd);
    rt->clientFd = ND_INVALID_SOCKET;

    // Wait for client thread to finish
    if (rt->clientThreadActive) {
        pthread_join(rt->clientThread, NULL);
        rt->clientThreadActive = false;
    }

    // Set carrier missing
    if (rt->info.carrierFunc) {
        rt->info.carrierFunc(rt->info.device, true);
    }

    memset(rt->clientAddrStr, 0, sizeof(rt->clientAddrStr));
    Log(LOG_INFO, "Telnet: disconnected %s\n", rt->info.name);
    return true;
}

bool TelnetServer_DisconnectDevice(TelnetServer *server, struct Device *device)
{
    if (!server || !device) return false;
    for (int i = 0; i < server->terminalCount; i++) {
        if (server->terminals[i].info.device == device) {
            return TelnetServer_DisconnectTerminal(server, i);
        }
    }
    return false;
}

int TelnetServer_GetPort(TelnetServer *server)
{
    return server ? server->config.port : 0;
}

bool TelnetServer_SetTerminalLocallyActive(TelnetServer *server, int index, bool active)
{
    if (!server || index < 0 || index >= server->terminalCount) return false;
    server->terminals[index].locallyActive = active;
    return true;
}

bool TelnetServer_SetDeviceLocallyActive(TelnetServer *server, struct Device *device, bool active)
{
    if (!server || !device) return false;
    RegisteredTerminal *rt = find_by_device(server, device);
    if (!rt) return false;
    rt->locallyActive = active;
    return true;
}

bool TelnetServer_IsDeviceConnected(TelnetServer *server, struct Device *device)
{
    if (!server || !device) return false;
    RegisteredTerminal *rt = find_by_device(server, device);
    return (rt && rt->clientFd != ND_INVALID_SOCKET);
}

void TelnetServer_ClearDeviceCarrier(TelnetServer *server, struct Device *device)
{
    if (!server || !device) return;
    RegisteredTerminal *rt = find_by_device(server, device);
    if (rt && rt->info.carrierFunc) {
        rt->info.carrierFunc(rt->info.device, false);  // false = carrier present
    }
}

bool TelnetServer_GetTerminalStats(TelnetServer *server, int index,
    uint64_t *bytesRx, uint64_t *bytesTx)
{
    if (!server || index < 0 || index >= server->terminalCount) return false;
    RegisteredTerminal *rt = &server->terminals[index];
    if (bytesRx) *bytesRx = rt->bytesRx;
    if (bytesTx) *bytesTx = rt->bytesTx;
    return true;
}

const char *TelnetServer_GetDeviceClientAddr(TelnetServer *server, struct Device *device)
{
    if (!server || !device) return NULL;
    RegisteredTerminal *rt = find_by_device(server, device);
    if (!rt || rt->clientFd == ND_INVALID_SOCKET) return NULL;
    return rt->clientAddrStr;
}

int TelnetServer_GetPendingCount(TelnetServer *server)
{
    if (!server) return 0;
    pthread_mutex_lock(&server->pendingMutex);
    int count = server->pendingCount;
    pthread_mutex_unlock(&server->pendingMutex);
    return count;
}

bool TelnetServer_GetPendingInfo(TelnetServer *server, int index,
    char *addrBuf, int addrBufLen, int *ageSecs,
    uint64_t *bytesRx, uint64_t *bytesTx)
{
    if (!server) return false;
    pthread_mutex_lock(&server->pendingMutex);
    if (index < 0 || index >= server->pendingCount) {
        pthread_mutex_unlock(&server->pendingMutex);
        return false;
    }
    PendingClient *pc = &server->pending[index];
    if (addrBuf && addrBufLen > 0) {
        strncpy(addrBuf, pc->addrStr, addrBufLen - 1);
        addrBuf[addrBufLen - 1] = '\0';
    }
    if (ageSecs) {
        *ageSecs = (int)(time(NULL) - pc->connectTime);
    }
    if (bytesRx) *bytesRx = pc->bytesRx;
    if (bytesTx) *bytesTx = pc->bytesTx;
    pthread_mutex_unlock(&server->pendingMutex);
    return true;
}

bool TelnetServer_DropPending(TelnetServer *server, int index)
{
    if (!server) return false;
    pthread_mutex_lock(&server->pendingMutex);
    if (index < 0 || index >= server->pendingCount) {
        pthread_mutex_unlock(&server->pendingMutex);
        return false;
    }
    const char *msg = "\r\nDisconnected by operator.\r\n";
    send(ND_SOCK_NATIVE(server->pending[index].fd), msg, (int)strlen(msg), MSG_NOSIGNAL);
    Log(LOG_INFO, "Telnet: operator dropped pending %s\n", server->pending[index].addrStr);
    remove_pending(server, index);
    pthread_mutex_unlock(&server->pendingMutex);
    return true;
}

void TelnetServer_DropAllPending(TelnetServer *server)
{
    if (!server) return;
    pthread_mutex_lock(&server->pendingMutex);
    for (int i = server->pendingCount - 1; i >= 0; i--) {
        const char *msg = "\r\nDisconnected by operator.\r\n";
        send(ND_SOCK_NATIVE(server->pending[i].fd), msg, (int)strlen(msg), MSG_NOSIGNAL);
        Log(LOG_INFO, "Telnet: operator dropped pending %s\n", server->pending[i].addrStr);
        remove_pending(server, i);
    }
    pthread_mutex_unlock(&server->pendingMutex);
}

// Remove a pending client by index (caller must hold pendingMutex)
static void remove_pending(TelnetServer *server, int idx)
{
    if (idx < 0 || idx >= server->pendingCount) return;
    if (server->pending[idx].fd != ND_INVALID_SOCKET) {
        nd_socket_close(server->pending[idx].fd);
        server->pending[idx].fd = ND_INVALID_SOCKET;
    }
    // Shift remaining entries down
    for (int j = idx; j < server->pendingCount - 1; j++) {
        server->pending[j] = server->pending[j + 1];
    }
    server->pendingCount--;
    server->pending[server->pendingCount].fd = ND_INVALID_SOCKET;
}

// Menu index mapping: displayed menu numbers -> terminal indices
// Only available (not locally active, not connected) terminals are shown
static int menuMap[TELNET_MAX_TERMINALS];
static int menuMapCount = 0;

// Send terminal selection menu to client (only shows available terminals).
// If clientFd == ND_INVALID_SOCKET, only rebuild menuMap without sending.
// Returns number of available terminals (menuMapCount).
static int send_menu(TelnetServer *server, nd_socket_t clientFd)
{
    // Build mapping of available terminals
    menuMapCount = 0;
    for (int i = 0; i < server->terminalCount; i++) {
        RegisteredTerminal *rt = &server->terminals[i];
        if (rt->clientFd == ND_INVALID_SOCKET && !rt->locallyActive) {
            menuMap[menuMapCount++] = i;
        }
    }

    if (clientFd == ND_INVALID_SOCKET) return menuMapCount;  // Map-only rebuild

    char buf[2048];
    int pos = 0;

    pos += snprintf(buf + pos, sizeof(buf) - pos,
        "\r\n"
        "ND-100/CX Terminal Server (port %d)\r\n"
        "\r\n",
        server->config.port);

    if (menuMapCount == 0) {
        // Caller should disconnect - this path shouldn't normally be reached
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "No terminals available.\r\n");
    } else {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "Available terminals:\r\n");

        for (int m = 0; m < menuMapCount; m++) {
            RegisteredTerminal *rt = &server->terminals[menuMap[m]];
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                "  %d) %s\r\n",
                m + 1, rt->info.name);
        }

        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "\r\nSelect terminal (1-%d), ENTER for next available, or Q to quit: ",
            menuMapCount);
    }

    int sent = send(ND_SOCK_NATIVE(clientFd), buf, pos, MSG_NOSIGNAL);

    // Track tx bytes for pending clients
    if (sent > 0) {
        for (int i = 0; i < server->pendingCount; i++) {
            if (server->pending[i].fd == clientFd) {
                server->pending[i].bytesTx += sent;
                break;
            }
        }
    }

    return menuMapCount;
}

// Send menu to a pending client and snapshot the mapping
static int send_menu_to_pending(TelnetServer *server, PendingClient *pc)
{
    int avail = send_menu(server, pc->fd);
    // Snapshot the menu map so we validate against what was shown
    pc->shownMapCount = menuMapCount;
    memcpy(pc->shownMap, menuMap, menuMapCount * sizeof(int));
    return avail;
}

// Try to assign a pending client's selection to a terminal
static bool try_assign_pending(TelnetServer *server, PendingClient *pc, int selection)
{
    if (selection < 0 || selection >= menuMapCount) return false;

    int termIdx = menuMap[selection];
    RegisteredTerminal *rt = &server->terminals[termIdx];
    if (rt->clientFd != ND_INVALID_SOCKET || rt->locallyActive) return false;

    // Wait for previous client thread to finish if needed
    if (rt->clientThreadActive) {
        pthread_join(rt->clientThread, NULL);
        rt->clientThreadActive = false;
    }

    // Assign client to terminal
    rt->clientFd = pc->fd;
    strncpy(rt->clientAddrStr, pc->addrStr, sizeof(rt->clientAddrStr) - 1);
    rt->clientAddrStr[sizeof(rt->clientAddrStr) - 1] = '\0';

    // Clear the ring buffer
    pthread_mutex_lock(&rt->outputMutex);
    rt->outputHead = 0;
    rt->outputTail = 0;
    pthread_mutex_unlock(&rt->outputMutex);

    // Send connected message
    char connMsg[128];
    snprintf(connMsg, sizeof(connMsg),
             "\r\nConnected to %s\r\n\r\n", rt->info.name);
    send(ND_SOCK_NATIVE(pc->fd), connMsg, (int)strlen(connMsg), MSG_NOSIGNAL);

    // Clear carrier missing
    if (rt->info.carrierFunc) {
        rt->info.carrierFunc(rt->info.device, false);
    }

    Log(LOG_INFO, "Telnet: %s connected to %s\n", pc->addrStr, rt->info.name);

    // Spawn client I/O thread
    if (pthread_create(&rt->clientThread, NULL, client_thread_func, rt) != 0) {
        Log(LOG_WARNING, "telnet: client thread create failed\n");
        nd_socket_close(pc->fd);
        rt->clientFd = ND_INVALID_SOCKET;
        memset(rt->clientAddrStr, 0, sizeof(rt->clientAddrStr));
        return false;
    }
    rt->clientThreadActive = true;

    // Mark fd as transferred (don't close in remove_pending)
    pc->fd = ND_INVALID_SOCKET;
    return true;
}

// Accept thread: listens for new connections, manages pending clients
static void *accept_thread_func(void *arg)
{
    TelnetServer *server = (TelnetServer *)arg;

    while (!atomic_load(&server->shouldExit)) {
        // Build poll set: listen socket + shutdown wake-pair + all pending clients
        int nfds = 2;
        nd_pollfd_t pfds[2 + TELNET_MAX_PENDING];
        pfds[0].fd = ND_SOCK_NATIVE(server->listenFd);
        pfds[0].events = POLLIN;
        pfds[1].fd = ND_SOCK_NATIVE(server->shutdownPipe[0]);
        pfds[1].events = POLLIN;

        pthread_mutex_lock(&server->pendingMutex);
        int pc_count = server->pendingCount;
        for (int i = 0; i < pc_count; i++) {
            pfds[2 + i].fd = ND_SOCK_NATIVE(server->pending[i].fd);
            pfds[2 + i].events = POLLIN;
        }
        pthread_mutex_unlock(&server->pendingMutex);
        nfds += pc_count;

        int ret = nd_poll(pfds, (unsigned)nfds, 1000);
        if (ret < 0) {
            // Transient error: retry. EINTR on POSIX; WSAPoll seldom returns it.
            continue;
        }

        // Shutdown signal
        if (pfds[1].revents & POLLIN) break;

        // Check for timeout on pending clients
        pthread_mutex_lock(&server->pendingMutex);
        time_t now = time(NULL);
        for (int i = server->pendingCount - 1; i >= 0; i--) {
            if (server->pending[i].fd != ND_INVALID_SOCKET &&
                (now - server->pending[i].connectTime) >= TELNET_PENDING_TIMEOUT) {
                const char *timeout_msg = "\r\nConnection timed out.\r\n";
                send(ND_SOCK_NATIVE(server->pending[i].fd), timeout_msg,
                     (int)strlen(timeout_msg), MSG_NOSIGNAL);
                Log(LOG_INFO, "Telnet: %s timed out (no terminal selected)\n",
                       server->pending[i].addrStr);
                remove_pending(server, i);
            }
        }
        pthread_mutex_unlock(&server->pendingMutex);

        // Process input from pending clients
        pthread_mutex_lock(&server->pendingMutex);
        for (int i = server->pendingCount - 1; i >= 0; i--) {
            if (i + 2 >= nfds) continue;
            if (!(pfds[2 + i].revents & POLLIN)) continue;

            uint8_t inputBuf[64];
            int n = recv(ND_SOCK_NATIVE(server->pending[i].fd),
                         (char *)inputBuf, (int)sizeof(inputBuf), 0);
            if (n <= 0) {
                Log(LOG_INFO, "Telnet: %s disconnected during menu\n",
                       server->pending[i].addrStr);
                remove_pending(server, i);
                continue;
            }
            server->pending[i].bytesRx += n;

            // Process through IAC state machine
            bool quit = false;
            int selection = -1;
            for (int b = 0; b < n && !quit; b++) {
                uint8_t byte = inputBuf[b];

                switch (server->pending[i].iacState) {
                case TELNET_STATE_DATA:
                    if (byte == IAC) {
                        server->pending[i].iacState = TELNET_STATE_IAC;
                    } else if (byte == 'q' || byte == 'Q') {
                        const char *bye = "\r\nGoodbye.\r\n";
                        send(ND_SOCK_NATIVE(server->pending[i].fd), bye,
                             (int)strlen(bye), MSG_NOSIGNAL);
                        Log(LOG_INFO, "Telnet: %s quit\n", server->pending[i].addrStr);
                        quit = true;
                    } else if (byte >= '1' && byte <= '9') {
                        selection = byte - '1';
                    } else if (byte == '\r' || byte == '\n') {
                        selection = 0;  // First available terminal
                    }
                    break;
                case TELNET_STATE_IAC:
                    switch (byte) {
                    case WILL: server->pending[i].iacState = TELNET_STATE_WILL; break;
                    case WONT: server->pending[i].iacState = TELNET_STATE_WONT; break;
                    case DO:   server->pending[i].iacState = TELNET_STATE_DO;   break;
                    case DONT: server->pending[i].iacState = TELNET_STATE_DONT; break;
                    case SB:   server->pending[i].iacState = TELNET_STATE_SB;   break;
                    case IAC:  server->pending[i].iacState = TELNET_STATE_DATA; break;
                    default:   server->pending[i].iacState = TELNET_STATE_DATA; break;
                    }
                    break;
                case TELNET_STATE_WILL:
                case TELNET_STATE_WONT:
                case TELNET_STATE_DO:
                case TELNET_STATE_DONT:
                    server->pending[i].iacState = TELNET_STATE_DATA;
                    break;
                case TELNET_STATE_SB:
                    server->pending[i].iacState = TELNET_STATE_SB_DATA;
                    break;
                case TELNET_STATE_SB_DATA:
                    if (byte == IAC) server->pending[i].iacState = TELNET_STATE_SB_IAC;
                    break;
                case TELNET_STATE_SB_IAC:
                    if (byte == SE) server->pending[i].iacState = TELNET_STATE_DATA;
                    else server->pending[i].iacState = TELNET_STATE_SB_DATA;
                    break;
                }
            }

            if (quit) {
                remove_pending(server, i);
                continue;
            }

            if (selection >= 0) {
                PendingClient *pc = &server->pending[i];

                // Validate against the menu THIS client was shown
                if (selection >= pc->shownMapCount) {
                    const char *err = "\r\nInvalid selection.\r\n";
                    send(ND_SOCK_NATIVE(pc->fd), err, (int)strlen(err), MSG_NOSIGNAL);
                    pc->bytesTx += strlen(err);
                    continue;
                }

                // Look up the terminal the client intended to select
                int termIdx = pc->shownMap[selection];
                RegisteredTerminal *rt = &server->terminals[termIdx];

                if (rt->clientFd != ND_INVALID_SOCKET || rt->locallyActive) {
                    // Terminal taken since menu was shown - busy message + fresh list
                    const char *err = "\r\nTerminal busy (taken by another client).\r\n";
                    send(ND_SOCK_NATIVE(pc->fd), err, (int)strlen(err), MSG_NOSIGNAL);
                    pc->bytesTx += strlen(err);
                    int avail = send_menu_to_pending(server, pc);
                    if (avail == 0) {
                        const char *bye = "All terminals are now in use. Disconnecting.\r\n";
                        send(ND_SOCK_NATIVE(pc->fd), bye, (int)strlen(bye), MSG_NOSIGNAL);
                        Log(LOG_INFO, "Telnet: %s disconnected (no free terminals)\n",
                               pc->addrStr);
                        remove_pending(server, i);
                    }
                } else {
                    // Use menuMap for try_assign - set it to match the client's intended terminal
                    menuMap[0] = termIdx;
                    menuMapCount = 1;
                    if (try_assign_pending(server, pc, 0)) {
                        // Remove from pending (fd already transferred)
                        for (int j = i; j < server->pendingCount - 1; j++) {
                            server->pending[j] = server->pending[j + 1];
                        }
                        server->pendingCount--;
                        server->pending[server->pendingCount].fd = ND_INVALID_SOCKET;
                    }
                }
            }
        }
        pthread_mutex_unlock(&server->pendingMutex);

        // Accept new connection
        if (pfds[0].revents & POLLIN) {
            struct sockaddr_in clientAddr;
            nd_socklen_t addrLen = sizeof(clientAddr);
            nd_socket_t clientFd = (nd_socket_t)accept(ND_SOCK_NATIVE(server->listenFd),
                                                       (struct sockaddr *)&clientAddr,
                                                       &addrLen);
            if (clientFd == ND_INVALID_SOCKET) {
                // Transient accept failure; retry. This includes our own
                // shutdown-triggered close of listenFd - the outer while loop
                // condition will pick that up on the next iteration.
                continue;
            }

            // Enable TCP_NODELAY
            int opt = 1;
            setsockopt(ND_SOCK_NATIVE(clientFd), IPPROTO_TCP, TCP_NODELAY,
                       (const char *)&opt, sizeof(opt));

            // Send telnet negotiation
            send(ND_SOCK_NATIVE(clientFd), (const char *)telnet_init,
                 (int)sizeof(telnet_init), MSG_NOSIGNAL);

            char addrStr[48];
            snprintf(addrStr, sizeof(addrStr), "%s:%d",
                     inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));

            Log(LOG_INFO, "Telnet: connection from %s\n", addrStr);

            pthread_mutex_lock(&server->pendingMutex);
            if (server->pendingCount >= TELNET_MAX_PENDING) {
                const char *full = "\r\nToo many pending connections. Try again later.\r\n";
                send(ND_SOCK_NATIVE(clientFd), full, (int)strlen(full), MSG_NOSIGNAL);
                nd_socket_close(clientFd);
                Log(LOG_INFO, "Telnet: %s rejected (pending slots full)\n", addrStr);
            } else {
                // Check if any terminals are available before adding to pending
                send_menu(server, ND_INVALID_SOCKET);  // Rebuild menuMap only
                if (menuMapCount == 0) {
                    const char *noterm =
                        "\r\nND-100/CX Terminal Server\r\n\r\n"
                        "No terminals available. All are in use.\r\n"
                        "Disconnecting.\r\n";
                    send(ND_SOCK_NATIVE(clientFd), noterm, (int)strlen(noterm), MSG_NOSIGNAL);
                    nd_socket_close(clientFd);
                    Log(LOG_INFO, "Telnet: %s rejected (no free terminals)\n", addrStr);
                } else {
                    PendingClient *pc = &server->pending[server->pendingCount];
                    pc->fd = clientFd;
                    strncpy(pc->addrStr, addrStr, sizeof(pc->addrStr) - 1);
                    pc->addrStr[sizeof(pc->addrStr) - 1] = '\0';
                    pc->connectTime = time(NULL);
                    pc->iacState = TELNET_STATE_DATA;
                    pc->bytesRx = 0;
                    pc->bytesTx = sizeof(telnet_init);  // Count initial negotiation
                    server->pendingCount++;

                    // Send menu and snapshot mapping
                    send_menu_to_pending(server, pc);
                }
            }
            pthread_mutex_unlock(&server->pendingMutex);
        }
    }

    // Clean up pending clients on shutdown
    pthread_mutex_lock(&server->pendingMutex);
    for (int i = server->pendingCount - 1; i >= 0; i--) {
        remove_pending(server, i);
    }
    pthread_mutex_unlock(&server->pendingMutex);

    return NULL;
}

// Client I/O thread: handles bidirectional data between telnet client and terminal
static void *client_thread_func(void *arg)
{
    RegisteredTerminal *rt = (RegisteredTerminal *)arg;
    if (!rt || !g_telnetServer) return NULL;

    TelnetServer *server = g_telnetServer;
    nd_socket_t fd = rt->clientFd;
    TelnetIACState iacState = TELNET_STATE_DATA;

    nd_pollfd_t pfds[2];
    pfds[0].fd = ND_SOCK_NATIVE(fd);
    pfds[0].events = POLLIN;
    pfds[1].fd = ND_SOCK_NATIVE(server->shutdownPipe[0]);
    pfds[1].events = POLLIN;

    while (!atomic_load(&server->shouldExit) && rt->clientFd != ND_INVALID_SOCKET) {
        int ret = nd_poll(pfds, 2, 50);  // 50ms timeout to check ring buffer

        if (ret < 0) {
            // Transient error - retry. (POSIX EINTR; WSAPoll seldom returns it.)
            continue;
        }

        // Shutdown signal
        if (pfds[1].revents & POLLIN) break;

        // Handle incoming data from client
        if (ret > 0 && (pfds[0].revents & POLLIN)) {
            uint8_t buf[256];
            int n = recv(ND_SOCK_NATIVE(fd), (char *)buf, (int)sizeof(buf), 0);
            if (n <= 0) {
                // Client disconnected
                break;
            }
            rt->bytesRx += n;

            // Process through IAC state machine, pass data bytes to terminal
            for (int i = 0; i < n; i++) {
                uint8_t byte = buf[i];

                switch (iacState) {
                case TELNET_STATE_DATA:
                    if (byte == IAC) {
                        iacState = TELNET_STATE_IAC;
                    } else {
                        // Map CR to CR (ND doesn't like LF)
                        if (byte == '\n') byte = '\r';
                        if (rt->info.inputFunc) {
                            rt->info.inputFunc(rt->info.device, byte);
                        }
                    }
                    break;
                case TELNET_STATE_IAC:
                    switch (byte) {
                    case WILL: iacState = TELNET_STATE_WILL; break;
                    case WONT: iacState = TELNET_STATE_WONT; break;
                    case DO:   iacState = TELNET_STATE_DO;   break;
                    case DONT: iacState = TELNET_STATE_DONT; break;
                    case SB:   iacState = TELNET_STATE_SB;   break;
                    case IAC:
                        // Escaped 255 - pass as data
                        if (rt->info.inputFunc) {
                            rt->info.inputFunc(rt->info.device, byte);
                        }
                        iacState = TELNET_STATE_DATA;
                        break;
                    default:
                        iacState = TELNET_STATE_DATA;
                        break;
                    }
                    break;
                case TELNET_STATE_WILL:
                case TELNET_STATE_WONT:
                case TELNET_STATE_DO:
                case TELNET_STATE_DONT:
                    iacState = TELNET_STATE_DATA;
                    break;
                case TELNET_STATE_SB:
                    iacState = TELNET_STATE_SB_DATA;
                    break;
                case TELNET_STATE_SB_DATA:
                    if (byte == IAC) iacState = TELNET_STATE_SB_IAC;
                    break;
                case TELNET_STATE_SB_IAC:
                    if (byte == SE) iacState = TELNET_STATE_DATA;
                    else iacState = TELNET_STATE_SB_DATA;
                    break;
                }
            }
        }

        // Drain output ring buffer and send to client
        uint8_t outbuf[512];
        int outlen = ringbuf_read(rt, outbuf, sizeof(outbuf));
        if (outlen > 0 && rt->clientFd != ND_INVALID_SOCKET) {
            int sent = send(ND_SOCK_NATIVE(fd), (const char *)outbuf, outlen, MSG_NOSIGNAL);
            if (sent < 0) {
                break;  // Client disconnected
            }
            if (sent > 0) rt->bytesTx += sent;
        }
    }

    // Clean up
    if (rt->clientFd != ND_INVALID_SOCKET) {
        nd_socket_close(rt->clientFd);
        rt->clientFd = ND_INVALID_SOCKET;
    }
    memset(rt->clientAddrStr, 0, sizeof(rt->clientAddrStr));

    // Set carrier missing
    if (rt->info.carrierFunc) {
        rt->info.carrierFunc(rt->info.device, true);
    }

    Log(LOG_INFO, "Telnet: client disconnected from %s\n", rt->info.name);
    return NULL;
}
