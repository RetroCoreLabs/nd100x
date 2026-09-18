/*
 * nd100x - ND100 Virtual Machine
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * TCP networking for HDLC modem.
 *
 * Architecture:
 *   - All socket operations run in a background worker thread.
 *   - The emulation thread (Modem_Tick / Modem_SendBytes) never touches a socket.
 *   - Communication is via two lock-protected byte queues (RX and TX).
 *   - Worker handles DNS, connect, accept, reconnect with backoff, recv, send.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>

#include "modem.h"
#include "../devices_types.h"

#if defined(__EMSCRIPTEN__)
/* nd100wasm.c - ring buffer consumed by HDLC_PollTxFrame in the JS worker */
void HDLC_QueueTxFrame(int channel, const uint8_t *data, int length);
#endif

/* net_compat.h includes <winsock2.h> on Windows, which MUST precede
 * <windows.h> - anything that might pull windows.h (cpu_types.h's
 * Sleep() path) has to come after net_compat.h to silence the
 * "#warning Please include winsock2.h before windows.h" diagnostic. */
#ifdef MODEM_HAS_NETWORKING
#include "../../ndlib/net_compat.h"   /* sockets, poll, WSAStartup */
#endif
#include "../../cpu/cpu_types.h"       /* sleep_ms() - portable Sleep/nanosleep */

#ifdef MODEM_HAS_NETWORKING
#  ifdef _WIN32
     /* gethostbyname + ioctlsocket come from winsock2.h pulled in by
      * net_compat.h; no <netdb.h> on Windows. */
#    include <windows.h>
#  else
#    include <errno.h>
#    include <fcntl.h>
#    include <unistd.h>
#    include <netdb.h>                /* gethostbyname on POSIX */
#  endif

/* Portable non-blocking-mode toggle for a socket. */
static void nd_set_nonblocking(nd_socket_t fd, bool nonblocking)
{
#ifdef _WIN32
    u_long mode = nonblocking ? 1UL : 0UL;
    ioctlsocket(ND_SOCK_NATIVE(fd), FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return;
    if (nonblocking) flags |=  O_NONBLOCK;
    else             flags &= ~O_NONBLOCK;
    fcntl(fd, F_SETFL, flags);
#endif
}

/* Is the last socket error the "connect is still in progress" one?
 * POSIX returns EINPROGRESS; Winsock returns WSAEWOULDBLOCK. */
static bool nd_connect_in_progress(void)
{
#ifdef _WIN32
    return nd_last_socket_error() == WSAEWOULDBLOCK;
#else
    return errno == EINPROGRESS;
#endif
}
#endif

// ============================================================================
// Thread-safe queue operations (POSIX only)
// ============================================================================

#ifdef MODEM_HAS_NETWORKING

static void queue_init(ModemQueue *q)
{
    q->head = 0;
    q->tail = 0;
    pthread_mutex_init(&q->mtx, NULL);
}

static void queue_destroy(ModemQueue *q)
{
    pthread_mutex_destroy(&q->mtx);
}

// Returns number of bytes actually enqueued (may be less if full)
static int queue_write(ModemQueue *q, const uint8_t *data, int len)
{
    if (len <= 0) return 0;
    pthread_mutex_lock(&q->mtx);

    int written = 0;
    for (int i = 0; i < len; i++) {
        int next = (q->head + 1) % MODEM_QUEUE_SIZE;
        if (next == q->tail) break; // full
        q->buf[q->head] = data[i];
        q->head = next;
        written++;
    }

    pthread_mutex_unlock(&q->mtx);
    return written;
}

// Enqueue ALL bytes, retrying with short sleep until queue has space.
// Never drops data. Used for RX path where losing bytes corrupts HDLC frames.
static int queue_write_all(ModemQueue *q, const uint8_t *data, int len, uint64_t *dropped)
{
    if (len <= 0) return 0;
    (void)dropped; // tracked for stats but we never actually drop

    int totalWritten = 0;
    while (totalWritten < len) {
        int n = queue_write(q, data + totalWritten, len - totalWritten);
        totalWritten += n;
        if (totalWritten < len) {
#if defined(_WIN32) || defined(_WIN64)
            Sleep(0); // yield timeslice on Windows
#else
            usleep(50); // 50us yield on POSIX
#endif
        }
    }
    return totalWritten;
}

// Returns number of bytes read into dst
static int queue_read(ModemQueue *q, uint8_t *dst, int maxlen)
{
    if (maxlen <= 0) return 0;
    pthread_mutex_lock(&q->mtx);

    int count = 0;
    while (count < maxlen && q->tail != q->head) {
        dst[count++] = q->buf[q->tail];
        q->tail = (q->tail + 1) % MODEM_QUEUE_SIZE;
    }

    pthread_mutex_unlock(&q->mtx);
    return count;
}

// Check if queue has data (lock-free peek for hot path)
static bool queue_has_data(ModemQueue *q)
{
    return q->head != q->tail;
}

#endif /* MODEM_HAS_NETWORKING */

// ============================================================================
// Socket helpers
// ============================================================================

#ifdef MODEM_HAS_NETWORKING

static void set_nodelay(nd_socket_t fd)
{
    int flag = 1;
    setsockopt(ND_SOCK_NATIVE(fd), IPPROTO_TCP, TCP_NODELAY,
               (const char *)&flag, sizeof(flag));
}

static void close_fd(nd_socket_t *fd)
{
    if (*fd != ND_INVALID_SOCKET) {
        nd_socket_close(*fd);
        *fd = ND_INVALID_SOCKET;
    }
}

// ============================================================================
// Worker thread: handles ALL networking
// ============================================================================

// Resolve hostname. Returns 0 on success, -1 on failure.
// This may block for DNS - that's fine, we're in the worker thread.
static int resolve_address(const char *host, int port, struct sockaddr_in *out)
{
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons((uint16_t)port);

    // Try numeric IP first (instant)
    if (inet_pton(AF_INET, host, &out->sin_addr) == 1) {
        return 0;
    }

    // DNS lookup (may take seconds - worker thread, so that's fine)
    struct hostent *he = gethostbyname(host);
    if (!he) {
        return -1;
    }
    memcpy(&out->sin_addr, he->h_addr_list[0], (size_t)he->h_length);
    return 0;
}

// Try to connect. Returns connected fd or ND_INVALID_SOCKET.
// Uses poll with timeout so it never blocks indefinitely.
static nd_socket_t try_connect(struct sockaddr_in *addr, int timeout_ms)
{
    nd_socket_t fd = (nd_socket_t)socket(AF_INET, SOCK_STREAM, 0);
    if (fd == ND_INVALID_SOCKET) return ND_INVALID_SOCKET;

    // Set non-blocking for connect so we can give it a timeout.
    nd_set_nonblocking(fd, true);

    int ret = connect(ND_SOCK_NATIVE(fd), (struct sockaddr *)addr, sizeof(*addr));
    if (ret == 0) {
        // Connected instantly
        nd_set_nonblocking(fd, false); // restore blocking
        set_nodelay(fd);
        return fd;
    }

    if (!nd_connect_in_progress()) {
        nd_socket_close(fd);
        return ND_INVALID_SOCKET;
    }

    // Wait for connect with timeout
    nd_pollfd_t pfd;
    pfd.fd = ND_SOCK_NATIVE(fd);
    pfd.events = POLLOUT;
    pfd.revents = 0;
    ret = nd_poll(&pfd, 1, timeout_ms);

    if (ret <= 0) {
        // Timeout or error
        nd_socket_close(fd);
        return ND_INVALID_SOCKET;
    }

    if (pfd.revents & (POLLERR | POLLHUP)) {
        nd_socket_close(fd);
        return ND_INVALID_SOCKET;
    }

    // Check SO_ERROR to confirm the connect actually succeeded.
    int err = 0;
    nd_socklen_t errlen = sizeof(err);
    getsockopt(ND_SOCK_NATIVE(fd), SOL_SOCKET, SO_ERROR, (char *)&err, &errlen);
    if (err != 0) {
        nd_socket_close(fd);
        return ND_INVALID_SOCKET;
    }

    nd_set_nonblocking(fd, false); // restore blocking
    set_nodelay(fd);
    return fd;
}

// Sleep helper that checks shutdown flag, returns true if shutdown requested
static bool sleep_check_shutdown(ModemState *modem, int seconds)
{
    for (int i = 0; i < seconds * 10; i++) {
        if (atomic_load(&modem->shutdownReq)) return true;
        sleep_ms(100); // 100ms (portable - Sleep/nanosleep)
    }
    return atomic_load(&modem->shutdownReq);
}

// ---- SERVER WORKER ----
static void *server_worker(void *arg)
{
    ModemState *modem = (ModemState *)arg;

    nd_socket_t listenFd = (nd_socket_t)socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd == ND_INVALID_SOCKET) {
        fprintf(stderr, "Modem: Failed to create listen socket (err %d)\n",
                nd_last_socket_error());
        return NULL;
    }

    int reuse = 1;
    setsockopt(ND_SOCK_NATIVE(listenFd), SOL_SOCKET, SO_REUSEADDR,
               (const char *)&reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)modem->port);

    if (bind(ND_SOCK_NATIVE(listenFd), (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Modem: Failed to bind port %d (err %d)\n",
                modem->port, nd_last_socket_error());
        nd_socket_close(listenFd);
        return NULL;
    }

    if (listen(ND_SOCK_NATIVE(listenFd), 1) < 0) {
        fprintf(stderr, "Modem: Failed to listen (err %d)\n", nd_last_socket_error());
        nd_socket_close(listenFd);
        return NULL;
    }

    fprintf(stderr, "Modem: Listening on port %d\n", modem->port);

    while (!atomic_load(&modem->shutdownReq)) {
        // Accept with timeout so we can check shutdown
        nd_pollfd_t pfd;
        pfd.fd = ND_SOCK_NATIVE(listenFd);
        pfd.events = POLLIN;
        pfd.revents = 0;
        int ret = nd_poll(&pfd, 1, 500); // 500ms timeout
        if (ret <= 0) continue;

        struct sockaddr_in peer;
        nd_socklen_t peerlen = sizeof(peer);
        nd_socket_t clientFd = (nd_socket_t)accept(ND_SOCK_NATIVE(listenFd),
                                                   (struct sockaddr *)&peer, &peerlen);
        if (clientFd == ND_INVALID_SOCKET) continue;

        set_nodelay(clientFd);
        fprintf(stderr, "Modem: Accepted connection from %s:%d\n",
                inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
        atomic_store(&modem->connected, true);

        // Service this connection: shuttle data between socket and queues
        while (!atomic_load(&modem->shutdownReq)) {
            nd_pollfd_t fds;
            fds.fd = ND_SOCK_NATIVE(clientFd);
            fds.events = POLLIN;
            fds.revents = 0;

            // Check if we have TX data to send
            if (queue_has_data(&modem->txQueue)) {
                fds.events |= POLLOUT;
            }

            int pr = nd_poll(&fds, 1, 100); // 100ms
            if (pr < 0) break;

            // Read from socket -> RX queue
            if (fds.revents & POLLIN) {
                uint8_t buf[4096];
                int n = recv(ND_SOCK_NATIVE(clientFd), (char *)buf, (int)sizeof(buf), 0);
                if (n <= 0) break; // disconnect or error
                queue_write_all(&modem->rxQueue, buf, n, &modem->rxDropped);
            }

            // Write TX queue -> socket
            if (fds.revents & POLLOUT) {
                uint8_t buf[4096];
                int n = queue_read(&modem->txQueue, buf, sizeof(buf));
                if (n > 0) {
                    int sent = 0;
                    while (sent < n) {
                        int w = send(ND_SOCK_NATIVE(clientFd),
                                     (const char *)(buf + sent),
                                     n - sent, MSG_NOSIGNAL);
                        if (w <= 0) break;
                        sent += w;
                    }
                }
            }

            if (fds.revents & (POLLERR | POLLHUP)) break;
        }

        // Connection ended
        nd_socket_close(clientFd);
        atomic_store(&modem->connected, false);
        fprintf(stderr, "Modem: Client disconnected, waiting for new connection...\n");
    }

    nd_socket_close(listenFd);
    return NULL;
}

// ---- CLIENT WORKER ----
static void *client_worker(void *arg)
{
    ModemState *modem = (ModemState *)arg;
    const char *host = modem->address[0] ? modem->address : "localhost";
    int attempt = 0;

    // Resolve address (may block for DNS - fine, we're in worker thread)
    struct sockaddr_in addr;
    if (resolve_address(host, modem->port, &addr) < 0) {
        fprintf(stderr, "Modem: Failed to resolve '%s' - giving up\n", host);
        return NULL;
    }
    fprintf(stderr, "Modem: Resolved %s -> %s\n", host, inet_ntoa(addr.sin_addr));

    while (!atomic_load(&modem->shutdownReq)) {
        attempt++;
        fprintf(stderr, "Modem: Connecting to %s:%d (attempt %d)...\n", host, modem->port, attempt);

        nd_socket_t clientFd = try_connect(&addr, 5000); // 5 second timeout
        if (clientFd == ND_INVALID_SOCKET) {
            // Backoff: 1s, 2s, 4s, 8s, 16s, 30s max
            int delay = 1;
            for (int i = 1; i < attempt && i < 5; i++) delay *= 2;
            if (delay > 30) delay = 30;
            fprintf(stderr, "Modem: Connect failed, retry in %d seconds\n", delay);
            if (sleep_check_shutdown(modem, delay)) break;
            continue;
        }

        fprintf(stderr, "Modem: Connected to %s:%d\n", host, modem->port);
        atomic_store(&modem->connected, true);
        attempt = 0; // reset backoff on success

        // Service connection
        while (!atomic_load(&modem->shutdownReq)) {
            nd_pollfd_t fds;
            fds.fd = ND_SOCK_NATIVE(clientFd);
            fds.events = POLLIN;
            fds.revents = 0;

            if (queue_has_data(&modem->txQueue)) {
                fds.events |= POLLOUT;
            }

            int pr = nd_poll(&fds, 1, 100);
            if (pr < 0) break;

            if (fds.revents & POLLIN) {
                uint8_t buf[4096];
                int n = recv(ND_SOCK_NATIVE(clientFd), (char *)buf, (int)sizeof(buf), 0);
                if (n <= 0) break;
                queue_write_all(&modem->rxQueue, buf, n, &modem->rxDropped);
            }

            if (fds.revents & POLLOUT) {
                uint8_t buf[4096];
                int n = queue_read(&modem->txQueue, buf, sizeof(buf));
                if (n > 0) {
                    int sent = 0;
                    while (sent < n) {
                        int w = send(ND_SOCK_NATIVE(clientFd),
                                     (const char *)(buf + sent),
                                     n - sent, MSG_NOSIGNAL);
                        if (w <= 0) goto disconnected;
                        sent += w;
                    }
                }
            }

            if (fds.revents & (POLLERR | POLLHUP)) break;
        }

disconnected:
        nd_socket_close(clientFd);
        atomic_store(&modem->connected, false);
        fprintf(stderr, "Modem: Disconnected from %s:%d\n", host, modem->port);

        if (!atomic_load(&modem->shutdownReq)) {
            fprintf(stderr, "Modem: Will reconnect in 1 second\n");
            if (sleep_check_shutdown(modem, 1)) break;
        }
    }

    return NULL;
}

#endif /* MODEM_HAS_NETWORKING */

// ============================================================================
// Public API - called from emulation thread
// ============================================================================

void Modem_Init(ModemState *modem, Device *hdlcDevice)
{
    if (!modem) return;
    memset(modem, 0, sizeof(ModemState));
    modem->hdlcDevice = hdlcDevice;

#ifdef MODEM_HAS_NETWORKING
    // Refcounted - on Windows this calls WSAStartup; no-op on POSIX.
    // Paired with nd_net_shutdown() in Modem_Destroy.
    nd_net_init();
    queue_init(&modem->rxQueue);
    queue_init(&modem->txQueue);
    atomic_store(&modem->connected, false);
    atomic_store(&modem->networkStarted, false);
    atomic_store(&modem->shutdownReq, false);
#endif
}

void Modem_Destroy(ModemState *modem)
{
    if (!modem) return;

#ifdef MODEM_HAS_NETWORKING
    // Signal worker to stop and wait for it
    atomic_store(&modem->shutdownReq, true);
    if (modem->workerRunning) {
        pthread_join(modem->workerThread, NULL);
        modem->workerRunning = false;
    }
    queue_destroy(&modem->rxQueue);
    queue_destroy(&modem->txQueue);
    nd_net_shutdown();
#endif

    atomic_store(&modem->connected, false);
    atomic_store(&modem->networkStarted, false);
}

void Modem_StartModem(ModemState *modem, bool isServer, const char *address, int port)
{
    if (!modem) return;

    modem->isServer = isServer;
    modem->port = port;
    if (address) {
        strncpy(modem->address, address, sizeof(modem->address) - 1);
        modem->address[sizeof(modem->address) - 1] = '\0';
    }

#ifdef MODEM_HAS_NETWORKING
    atomic_store(&modem->networkStarted, true);

    // Spawn worker thread - ALL socket ops happen there
    int err;
    if (isServer) {
        err = pthread_create(&modem->workerThread, NULL, server_worker, modem);
    } else {
        err = pthread_create(&modem->workerThread, NULL, client_worker, modem);
    }

    if (err != 0) {
        fprintf(stderr, "Modem: Failed to create worker thread: %s\n", strerror(err));
    } else {
        modem->workerRunning = true;
    }
#else
    /* Emscripten: no host TCP; WASM Init calls Modem_StartWasmBridge */
    atomic_store(&modem->networkStarted, true);
    atomic_store(&modem->connected, false);
#endif

#if defined(MODEM_HAS_NETWORKING)
    /* Native: line signals up once modem worker is started */
    modem->dataSetReady = true;
    modem->signalDetector = true;
    if (modem->onDataSetReady) modem->onDataSetReady(modem->hdlcDevice, true);
    if (modem->onSignalDetector) modem->onSignalDetector(modem->hdlcDevice, true);
#elif defined(__EMSCRIPTEN__)
    /* Browser: defer DSR/SD until gateway TCP client connects (0x12 carrier) */
    modem->dataSetReady = false;
    modem->signalDetector = false;
#endif
}

// Called from CPU loop. Never touches a socket. Just drains RX queue.
void Modem_Tick(ModemState *modem)
{
    if (!modem || !atomic_load(&modem->networkStarted)) return;

#ifdef MODEM_HAS_NETWORKING
    // Drain RX queue into HDLC receiver (no syscalls, just memcpy from queue)
    if (queue_has_data(&modem->rxQueue) && modem->onReceivedData) {
        uint8_t buf[4096];
        int n = queue_read(&modem->rxQueue, buf, sizeof(buf));
        if (n > 0) {
            modem->bytesRx += n;
            modem->onReceivedData(modem->hdlcDevice, buf, n);
        }
    }
#endif
}

// Called from emulation. Enqueues to TX queue, worker sends it.
void Modem_SendByte(ModemState *modem, uint8_t data)
{
    if (!modem) return;
    if (!atomic_load(&modem->connected)) {
        // Not connected - byte dropped (SINTRAN may send before TCP connects)
        return;
    }

#ifdef MODEM_HAS_NETWORKING
    queue_write_all(&modem->txQueue, &data, 1, &modem->txDropped);
    modem->bytesTx++;
#elif defined(__EMSCRIPTEN__)
    HDLC_QueueTxFrame(modem->wasmBridgeChannel, &data, 1);
    modem->bytesTx++;
#endif
}

void Modem_SendBytes(ModemState *modem, const uint8_t *data, int length)
{
    if (!modem || !data || length <= 0 || !atomic_load(&modem->connected)) return;

#ifdef MODEM_HAS_NETWORKING
    queue_write_all(&modem->txQueue, data, length, &modem->txDropped);
    modem->bytesTx += length;
#elif defined(__EMSCRIPTEN__)
    HDLC_QueueTxFrame(modem->wasmBridgeChannel, data, length);
    modem->bytesTx += length;
#endif
}

// ============================================================================
// Modem signal functions - called from emulation thread
// ============================================================================

void Modem_SetDTR(ModemState *modem, bool value)
{
    if (!modem || modem->dataTerminalReady == value) return;
    modem->dataTerminalReady = value;
    Modem_SetDSR(modem, value);
    if (modem->onDataTerminalReady) modem->onDataTerminalReady(modem->hdlcDevice, value);
}

void Modem_SetRTS(ModemState *modem, bool value)
{
    if (!modem || modem->requestToSend == value) return;
    modem->requestToSend = value;
    Modem_SetCTS(modem, value);
    if (modem->onRequestToSend) modem->onRequestToSend(modem->hdlcDevice, value);
}

void Modem_SetDSR(ModemState *modem, bool value)
{
    if (!modem || modem->dataSetReady == value) return;
    modem->dataSetReady = value;
    if (modem->onDataSetReady) modem->onDataSetReady(modem->hdlcDevice, value);
}

void Modem_SetCTS(ModemState *modem, bool value)
{
    if (!modem || modem->clearToSend == value) return;
    modem->clearToSend = value;
    if (modem->onClearToSend) modem->onClearToSend(modem->hdlcDevice, value);
}

// ============================================================================
// Callback setup
// ============================================================================

void Modem_SetReceivedDataCallback(ModemState *modem, ModemDataCallback callback)     { if (modem) modem->onReceivedData = callback; }
void Modem_SetRingIndicatorCallback(ModemState *modem, ModemSignalCallback callback)  { if (modem) modem->onRingIndicator = callback; }
void Modem_SetDataSetReadyCallback(ModemState *modem, ModemSignalCallback callback)   { if (modem) modem->onDataSetReady = callback; }
void Modem_SetSignalDetectorCallback(ModemState *modem, ModemSignalCallback callback) { if (modem) modem->onSignalDetector = callback; }
void Modem_SetClearToSendCallback(ModemState *modem, ModemSignalCallback callback)    { if (modem) modem->onClearToSend = callback; }
void Modem_SetRequestToSendCallback(ModemState *modem, ModemSignalCallback callback)  { if (modem) modem->onRequestToSend = callback; }
void Modem_SetDataTerminalReadyCallback(ModemState *modem, ModemSignalCallback callback) { if (modem) modem->onDataTerminalReady = callback; }

#if defined(__EMSCRIPTEN__)
void Modem_SetWasmBridgeChannel(ModemState *modem, int channel)
{
    if (!modem) return;
    modem->wasmBridgeChannel = channel;
}

void Modem_StartWasmBridge(ModemState *modem)
{
    if (!modem) return;
    atomic_store(&modem->networkStarted, true);
    atomic_store(&modem->connected, false);
    modem->dataSetReady = false;
    modem->signalDetector = false;
}

void Modem_SetCarrierPresent(ModemState *modem, bool present)
{
    if (!modem) return;
    atomic_store(&modem->connected, present);
    modem->dataSetReady = present;
    modem->signalDetector = present;
    if (modem->onDataSetReady) modem->onDataSetReady(modem->hdlcDevice, present);
    if (modem->onSignalDetector) modem->onSignalDetector(modem->hdlcDevice, present);
}
#endif
