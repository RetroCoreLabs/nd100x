/*
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * net_compat.h - cross-platform networking shim.
 *
 * The telnetserver code was originally written against BSD sockets + pipe()
 * + poll(). This header lets the same logic compile on both POSIX and
 * Winsock2 by wrapping the parts that diverge:
 *
 *   - Socket descriptor type: int on POSIX, SOCKET (unsigned) on Windows.
 *     nd_socket_t is signed-safe (intptr_t on Windows) so the existing
 *     "< 0" / ">= 0" comparisons that the codebase uses can stay meaningful.
 *   - close() on a socket: closesocket() on Windows.
 *   - pipe() for inter-thread wake-ups: replaced with a loopback socketpair
 *     that WSAPoll can watch alongside the listen socket.
 *   - poll(): WSAPoll() on Windows (Vista+).
 *   - WSAStartup/WSACleanup: refcounted init/shutdown functions.
 *   - MSG_NOSIGNAL: does not exist on Windows (no SIGPIPE); defined to 0.
 */

#ifndef NET_COMPAT_H
#define NET_COMPAT_H

#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef intptr_t nd_socket_t; /* signed so "< 0" comparisons work */
typedef int nd_ssize_t;
typedef int nd_socklen_t;
typedef WSAPOLLFD nd_pollfd_t;
#define ND_INVALID_SOCKET ((nd_socket_t) - 1)
/* Convert nd_socket_t storage to the native Winsock SOCKET type (UINT_PTR).
     * Needed because casting intptr_t to int would truncate on x64. */
#define ND_SOCK_NATIVE(s) ((SOCKET)(s))
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
typedef int nd_socket_t;
typedef ssize_t NdSsizeT;
typedef socklen_t nd_socklen_t;
typedef struct pollfd nd_pollfd_t;
#define ND_INVALID_SOCKET ((nd_socket_t) - 1)
#define ND_SOCK_NATIVE(s) ((int)(s))
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    /**
 * @brief Start the socket layer: WSAStartup(2,2) on Windows, nothing on POSIX.
 * @details Refcounted, so it is safe to call several times; each call must be
 *          paired with one nd_net_shutdown(). Must be called before any other
 *          socket call on Windows.
 * @return 0 on success, -1 if WSAStartup failed.
 */
    int nd_net_init(void);

    /**
 * @brief Release the socket layer: drops the refcount and calls WSACleanup()
 *        on Windows when it reaches zero. Does nothing on POSIX.
 * @details An extra call that would drive the refcount below zero is ignored
 *          and the count is clamped back to zero.
 */
    void nd_net_shutdown(void);

    /**
 * @brief Close a socket: closesocket() on Windows, close() on POSIX.
 * @param s The socket descriptor.
 * @return 0 on success, -1 on error (as the platform call reports it).
 */
    int nd_socket_close(nd_socket_t s);

    /**
 * @brief Wait for events on a set of sockets: WSAPoll() on Windows, poll()
 *        on POSIX.
 * @param fds        Array of descriptors and their requested events.
 * @param nfds       Number of entries in fds.
 * @param timeout_ms Timeout in milliseconds; -1 waits forever.
 * @return Number of descriptors with events, 0 on timeout, -1 on error.
 */
    int nd_poll(nd_pollfd_t *fds, unsigned nfds, int timeout_ms);

    /**
 * @brief Last socket-layer error: WSAGetLastError() on Windows, errno on
 *        POSIX.
 * @return The platform error code.
 */
    int nd_last_socket_error(void);

    /**
 * @brief Create a connected TCP-loopback socket pair used to wake a thread
 *        that is sitting in nd_poll().
 * @details pair[0] is the read end (poll for POLLIN to see the wake signal)
 *          and pair[1] the write end (send one byte to wake the reader). A
 *          portable stand-in for POSIX pipe(): a listener is bound to
 *          127.0.0.1 on a kernel-assigned port, a second socket connects to
 *          it, the accepted socket becomes the read end and the listener is
 *          closed.
 * @param pair Receives the two socket descriptors.
 * @return 0 on success; -1 on failure, with both elements set to
 *         ND_INVALID_SOCKET.
 */
    int nd_wake_pair(nd_socket_t pair[2]);

#ifdef __cplusplus
}
#endif

#endif /* NET_COMPAT_H */
