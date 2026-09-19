/*
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * net_compat.c - implementation of the cross-platform net compat layer.
 * See net_compat.h for the design rationale.
 */

#include "net_compat.h"
#include <string.h>

#ifdef _WIN32
/* -------------------------------------------------------------------------- */
/* Windows (Winsock2)                                                         */
/* -------------------------------------------------------------------------- */

#include <windows.h>

static volatile LONG g_net_init_refs = 0;

int nd_net_init(void)
{
    LONG prev = InterlockedIncrement(&g_net_init_refs);
    if (prev == 1)
    {
        WSADATA wsa;
        int r = WSAStartup(MAKEWORD(2, 2), &wsa);
        if (r != 0)
        {
            InterlockedDecrement(&g_net_init_refs);
            return -1;
        }
    }
    return 0;
}

void nd_net_shutdown(void)
{
    LONG prev = InterlockedDecrement(&g_net_init_refs);
    if (prev == 0)
    {
        WSACleanup();
    }
    else if (prev < 0)
    {
        /* Over-released - clamp back to zero to keep the refcount sane if
         * someone calls shutdown more times than init. */
        InterlockedIncrement(&g_net_init_refs);
    }
}

int nd_socket_close(nd_socket_t s)
{
    return closesocket(ND_SOCK_NATIVE(s));
}

int nd_poll(nd_pollfd_t *fds, unsigned nfds, int timeout_ms)
{
    return WSAPoll(fds, (ULONG)nfds, timeout_ms);
}

int nd_last_socket_error(void)
{
    return WSAGetLastError();
}

#else
/* -------------------------------------------------------------------------- */
/* POSIX                                                                      */
/* -------------------------------------------------------------------------- */

int nd_net_init(void)
{
    return 0;
}
void nd_net_shutdown(void)
{
}

int nd_socket_close(nd_socket_t s)
{
    return close(ND_SOCK_NATIVE(s));
}

int nd_poll(nd_pollfd_t *fds, unsigned nfds, int timeout_ms)
{
    return poll(fds, (nfds_t)nfds, timeout_ms);
}

int nd_last_socket_error(void)
{
    return errno;
}

#endif

/* -------------------------------------------------------------------------- */
/* Portable: loopback socket pair for inter-thread wake-up.                   */
/*                                                                            */
/* Strategy: bind a listener to 127.0.0.1:0 (kernel-assigned port), connect   */
/* a second socket to it, accept the accepted socket as the read end, and     */
/* keep the connecting socket as the write end. Close the listener.           */
/*                                                                            */
/* Works identically on POSIX and Windows. On POSIX we could use pipe() or    */
/* socketpair(AF_UNIX), but a TCP loopback pair is what WSAPoll can watch on  */
/* Windows, so we use the same mechanism everywhere for simplicity.           */
/* -------------------------------------------------------------------------- */

int nd_wake_pair(nd_socket_t pair[2])
{
    pair[0] = ND_INVALID_SOCKET;
    pair[1] = ND_INVALID_SOCKET;

    nd_socket_t listener = (nd_socket_t)socket(AF_INET, SOCK_STREAM, 0);
    if (listener == ND_INVALID_SOCKET)
    {
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; /* kernel-assigned */

    if (bind(ND_SOCK_NATIVE(listener), (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        nd_socket_close(listener);
        return -1;
    }
    if (listen(ND_SOCK_NATIVE(listener), 1) != 0)
    {
        nd_socket_close(listener);
        return -1;
    }

    nd_socklen_t alen = sizeof(addr);
    if (getsockname(ND_SOCK_NATIVE(listener), (struct sockaddr *)&addr, &alen) != 0)
    {
        nd_socket_close(listener);
        return -1;
    }

    nd_socket_t writer = (nd_socket_t)socket(AF_INET, SOCK_STREAM, 0);
    if (writer == ND_INVALID_SOCKET)
    {
        nd_socket_close(listener);
        return -1;
    }

    if (connect(ND_SOCK_NATIVE(writer), (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        nd_socket_close(writer);
        nd_socket_close(listener);
        return -1;
    }

    nd_socket_t reader = (nd_socket_t)accept(ND_SOCK_NATIVE(listener), NULL, NULL);
    if (reader == ND_INVALID_SOCKET)
    {
        nd_socket_close(writer);
        nd_socket_close(listener);
        return -1;
    }

    nd_socket_close(listener);
    pair[0] = reader;
    pair[1] = writer;
    return 0;
}
