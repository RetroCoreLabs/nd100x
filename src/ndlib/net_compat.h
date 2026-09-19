/*
 * nd100x - ND100 Virtual Machine
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
typedef ssize_t nd_ssize_t;
typedef socklen_t nd_socklen_t;
typedef struct pollfd nd_pollfd_t;
#define ND_INVALID_SOCKET ((nd_socket_t) - 1)
#define ND_SOCK_NATIVE(s) ((int)(s))
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    /* Lifecycle. Safe to call multiple times (refcounted). Must be called before
 * any other socket call on Windows. No-op on POSIX. */
    int nd_net_init(void);
    void nd_net_shutdown(void);

    /* Close a socket - uses closesocket() on Windows, close() on POSIX. */
    int nd_socket_close(nd_socket_t s);

    /* poll() wrapper - WSAPoll() on Windows, poll() on POSIX. */
    int nd_poll(nd_pollfd_t *fds, unsigned nfds, int timeout_ms);

    /* Get the last socket-layer error. WSAGetLastError on Windows, errno on POSIX. */
    int nd_last_socket_error(void);

    /* Create a connected TCP-loopback socket pair:
 *   pair[0] = read end  (poll for POLLIN to detect wake signal)
 *   pair[1] = write end (send a single byte to wake the reader)
 * Portable stand-in for POSIX pipe(). Returns 0 on success, -1 on failure;
 * on failure both elements are set to ND_INVALID_SOCKET. */
    int nd_wake_pair(nd_socket_t pair[2]);

#ifdef __cplusplus
}
#endif

#endif /* NET_COMPAT_H */
