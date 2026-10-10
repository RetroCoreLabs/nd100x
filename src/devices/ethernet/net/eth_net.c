/*
 * eth_net.c - Host network backends for the Ethernet II card (none, UDP, TCP).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * See eth_net.h for the spec forms, wire formats and threading. Ported from
 * RetroCore NullEthernetBackend.cs, UdpEthernetBackend.cs, TcpEthernetBackend.cs
 * and EthernetBackendFactory.cs.
 */

#ifndef __EMSCRIPTEN__
#include "../../../ndlib/net_compat.h" /* before anything that may pull windows.h */
#endif

#include "eth_net.h"

#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../../../ndlib/log.h"
#include "eth_pcap.h"

#ifndef __EMSCRIPTEN__
#include <pthread.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <time.h>
#endif
#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#endif
#endif

#define POLL_MS 500 /* cs: UdpEthernetBackend.cs:265 ReceiveTimeout = 500 */

struct EthNet
{
    EthNetSpec spec;
    char description[300];
    atomic_bool active;
    atomic_bool started;
    atomic_int listen_port;

    /* Receive ring: filled by the backend's thread, emptied by the emulation thread. */
    uint8_t (*ring)[ETH_NET_MAX_FRAME];
    int ring_length[ETH_NET_RX_RING_SIZE];
    int ring_read;
    int ring_write;
    atomic_int ring_count;

    atomic_ullong frames_sent;
    atomic_ullong frames_received;
    atomic_ullong frames_dropped_ring;
    atomic_ullong send_failures;
    atomic_ullong receive_errors;
    atomic_ullong links_up;

#ifndef __EMSCRIPTEN__
    pthread_mutex_t ring_lock;
    pthread_mutex_t send_lock; /* TCP: header and body of one frame stay together */
    pthread_t thread;
    bool thread_running;
    atomic_bool stop_requested;
    nd_socket_t sock;      /* UDP socket, or the TCP listener */
    nd_socket_t conn;      /* TCP: the connected stream */
    struct sockaddr_in udp_dest;
    int tap_fd; /* Linux TAP: the /dev/net/tun descriptor; -1 when closed */
    EthPcap *pcap; /* Npcap adapter (Windows); NULL when closed */
#endif
};

/* ---- spec parsing (EthernetBackendFactory / UdpEthernetBackend / TcpEthernetBackend.FromSpec) ---- */

/* Copy s without leading and trailing blanks into out. */
static void trim_copy(char *out, size_t size, const char *s)
{
    size_t len;

    while (isspace((unsigned char)*s))
    {
        s++;
    }
    len = strlen(s);
    while ((len > 0u) && isspace((unsigned char)s[len - 1u]))
    {
        len--;
    }
    if (len >= size)
    {
        len = size - 1u;
    }
    memcpy(out, s, len);
    out[len] = '\0';
}

/* Strict decimal number in [lo, hi]; -1 when not. */
static int parse_port(const char *s, int lo, int hi)
{
    char t[32];
    char *end = NULL;
    long v;

    trim_copy(t, sizeof t, s);
    if (t[0] == '\0')
    {
        return -1;
    }
    v = strtol(t, &end, 10);
    if ((*end != '\0') || (v < lo) || (v > hi))
    {
        return -1;
    }
    return (int)v;
}

static bool starts_with_ci(const char *s, const char *prefix)
{
    return strncasecmp(s, prefix, strlen(prefix)) == 0;
}

static bool is_ipv4(const char *s)
{
#ifdef __EMSCRIPTEN__
    unsigned a;
    unsigned b;
    unsigned c;
    unsigned d;
    char tail;
    return sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) == 4 && a < 256u && b < 256u &&
           c < 256u && d < 256u;
#else
    struct in_addr addr;
    return inet_pton(AF_INET, s, &addr) == 1;
#endif
}

/* UdpEthernetBackend.FromSpec, after the "udp"/"udp:"/"udp-mcast:" prefix was removed. */
static int parse_udp(const char *rest, EthNetSpec *out)
{
    char s[256];
    const char *colon;

    trim_copy(s, sizeof s, rest);
    if (strcasecmp(s, "mcast") == 0)
    {
        s[0] = '\0';
    }
    else if (starts_with_ci(s, "mcast:"))
    {
        memmove(s, s + 6, strlen(s + 6) + 1u);
        trim_copy(s, sizeof s, s);
    }
    out->kind = ETH_NET_UDP;
    (void)snprintf(out->host, sizeof out->host, "%s", ETH_NET_DEFAULT_GROUP);
    out->port = ETH_NET_DEFAULT_PORT;
    if (s[0] == '\0')
    {
        return 0;
    }
    colon = strrchr(s, ':');
    if (colon != NULL)
    {
        char g[256];
        size_t glen = (size_t)(colon - s);
        memcpy(g, s, glen);
        g[glen] = '\0';
        trim_copy(g, sizeof g, g);
        if ((g[0] != '\0') && !is_ipv4(g))
        {
            return -1;
        }
        if (g[0] != '\0')
        {
            (void)snprintf(out->host, sizeof out->host, "%s", g);
        }
        out->port = parse_port(colon + 1, 1, 65535);
        return (out->port < 0) ? -1 : 0;
    }
    if (strchr(s, '.') != NULL)
    {
        if (!is_ipv4(s))
        {
            return -1;
        }
        (void)snprintf(out->host, sizeof out->host, "%s", s);
        return 0;
    }
    out->port = parse_port(s, 1, 65535);
    return (out->port < 0) ? -1 : 0;
}

/* TcpEthernetBackend.FromSpec. */
static int parse_tcp(const char *s, EthNetSpec *out)
{
    const char *listen_port = NULL;
    bool had_prefix;
    char body[256];
    const char *colon;

    if ((strcasecmp(s, "listen") == 0) || (strcasecmp(s, "tcp-listen") == 0))
    {
        out->kind = ETH_NET_TCP_LISTEN;
        out->port = ETH_NET_DEFAULT_PORT;
        return 0;
    }
    if (starts_with_ci(s, "tcp-listen:"))
    {
        listen_port = s + 11;
    }
    else if (starts_with_ci(s, "listen:"))
    {
        listen_port = s + 7;
    }
    if (listen_port != NULL)
    {
        char t[32];
        trim_copy(t, sizeof t, listen_port);
        out->kind = ETH_NET_TCP_LISTEN;
        out->port = (t[0] == '\0') ? ETH_NET_DEFAULT_PORT : parse_port(t, 0, 65535);
        return (out->port < 0) ? -1 : 0;
    }

    had_prefix = starts_with_ci(s, "tcp:");
    trim_copy(body, sizeof body, had_prefix ? s + 4 : s);
    if (body[0] == '\0')
    {
        return -1;
    }
    out->kind = ETH_NET_TCP_CONNECT;
    colon = strrchr(body, ':');
    if (colon == NULL)
    {
        /* A bare word is junk unless it carries the tcp: prefix. */
        if (!had_prefix)
        {
            return -1;
        }
        (void)snprintf(out->host, sizeof out->host, "%s", body);
        out->port = ETH_NET_DEFAULT_PORT;
        return 0;
    }
    {
        char host[256];
        char port[32];
        size_t hlen = (size_t)(colon - body);
        memcpy(host, body, hlen);
        host[hlen] = '\0';
        trim_copy(out->host, sizeof out->host, host);
        trim_copy(port, sizeof port, colon + 1);
        if (out->host[0] == '\0')
        {
            return -1;
        }
        out->port = (port[0] == '\0') ? ETH_NET_DEFAULT_PORT : parse_port(port, 1, 65535);
    }
    return (out->port < 0) ? -1 : 0;
}

static void describe(EthNetSpec *s, int port)
{
    switch (s->kind)
    {
    case ETH_NET_UDP:
        (void)snprintf(s->description, sizeof s->description, "udp:mcast:%s:%d", s->host, s->port);
        break;
    case ETH_NET_TCP_CONNECT:
        (void)snprintf(s->description, sizeof s->description, "tcp:%s:%d", s->host, s->port);
        break;
    case ETH_NET_TCP_LISTEN:
        (void)snprintf(s->description, sizeof s->description, "tcp-listen:%d", port);
        break;
    case ETH_NET_TAP:
        (void)snprintf(s->description, sizeof s->description, "tap:%s", s->host);
        break;
    case ETH_NET_GATEWAY:
        (void)snprintf(s->description, sizeof s->description, "gateway:%d", s->port);
        break;
    case ETH_NET_PCAP:
        (void)snprintf(s->description, sizeof s->description, "pcap:%s", s->host);
        break;
    case ETH_NET_NONE:
        /* fall through */
    default:
        (void)snprintf(s->description, sizeof s->description, "none");
        break;
    }
}

int eth_net_parse_spec(const char *spec, EthNetSpec *out)
{
    char s[256];
    EthNetSpec r;
    int rc;

    if ((spec == NULL) || (out == NULL))
    {
        return -1;
    }
    trim_copy(s, sizeof s, spec);
    if (s[0] == '\0')
    {
        return -1;
    }
    memset(&r, 0, sizeof r);
    if (strcasecmp(s, "none") == 0)
    {
        r.kind = ETH_NET_NONE;
        rc = 0;
    }
    else if (starts_with_ci(s, "tap:"))
    {
        char ifname[256];
        trim_copy(ifname, sizeof ifname, s + 4);
        r.kind = ETH_NET_TAP;
        (void)snprintf(r.host, sizeof r.host, "%s", ifname);
        /* Linux IFNAMSIZ is 16 including the terminator. */
        rc = ((ifname[0] == '\0') || (strlen(ifname) > 15u)) ? -1 : 0;
    }
    else if ((strcasecmp(s, "gateway") == 0) || starts_with_ci(s, "gateway:"))
    {
        r.kind = ETH_NET_GATEWAY;
        r.port = (s[7] == ':') ? parse_port(s + 8, 0, 255) : 0; /* the segment number */
        rc = (r.port < 0) ? -1 : 0;
    }
    else if (starts_with_ci(s, "pcap:"))
    {
        char adapter[256];
        trim_copy(adapter, sizeof adapter, s + 5);
        r.kind = ETH_NET_PCAP;
        (void)snprintf(r.host, sizeof r.host, "%s", adapter);
        rc = (adapter[0] == '\0') ? -1 : 0;
    }
    else if (strcasecmp(s, "udp") == 0)
    {
        rc = parse_udp("", &r);
    }
    else if (starts_with_ci(s, "udp-mcast:"))
    {
        rc = parse_udp(s + 10, &r);
    }
    else if (starts_with_ci(s, "udp:"))
    {
        rc = parse_udp(s + 4, &r);
    }
    else
    {
        rc = parse_tcp(s, &r);
    }
    if (rc != 0)
    {
        return -1;
    }
    describe(&r, r.port);
    *out = r;
    return 0;
}

/* ---- life cycle ---------------------------------------------------------------------------- */

EthNet *eth_net_create(const EthNetSpec *spec)
{
    EthNet *n;

    if (spec == NULL)
    {
        return NULL;
    }
    n = calloc(1u, sizeof *n);
    if (n == NULL)
    {
        return NULL;
    }
    n->ring = calloc(ETH_NET_RX_RING_SIZE, sizeof *n->ring);
    if (n->ring == NULL)
    {
        free(n);
        return NULL;
    }
    n->spec = *spec;
    (void)snprintf(n->description, sizeof n->description, "%s", spec->description);
#ifndef __EMSCRIPTEN__
    pthread_mutex_init(&n->ring_lock, NULL);
    pthread_mutex_init(&n->send_lock, NULL);
    n->sock = ND_INVALID_SOCKET;
    n->conn = ND_INVALID_SOCKET;
    n->tap_fd = -1;
#endif
    return n;
}

const char *eth_net_description(const EthNet *n)
{
    return (n == NULL) ? "" : n->description;
}

bool eth_net_is_active(const EthNet *n)
{
    return (n != NULL) && atomic_load(&n->active);
}

int eth_net_local_port(const EthNet *n)
{
    return (n == NULL) ? 0 : atomic_load(&n->listen_port);
}

void eth_net_get_stats(const EthNet *n, EthNetStats *out)
{
    if (out == NULL)
    {
        return;
    }
    memset(out, 0, sizeof *out);
    if (n == NULL)
    {
        return;
    }
    out->frames_sent = atomic_load(&n->frames_sent);
    out->frames_received = atomic_load(&n->frames_received);
    out->frames_dropped_ring = atomic_load(&n->frames_dropped_ring);
    out->send_failures = atomic_load(&n->send_failures);
    out->receive_errors = atomic_load(&n->receive_errors);
    out->links_up = atomic_load(&n->links_up);
}

bool eth_net_has_frame(const EthNet *n)
{
    return (n != NULL) && (atomic_load(&n->ring_count) > 0);
}

/* The receive ring is shared with the backend's thread on native builds; the
 * browser build is single-threaded (no pthreads in the WASM link), so the lock
 * is a no-op there. */
static void ring_lock(EthNet *n)
{
#ifdef __EMSCRIPTEN__
    (void)n;
#else
    pthread_mutex_lock(&n->ring_lock);
#endif
}

static void ring_unlock(EthNet *n)
{
#ifdef __EMSCRIPTEN__
    (void)n;
#else
    pthread_mutex_unlock(&n->ring_lock);
#endif
}

/* Copy one received frame into the ring, dropping it when the ring is full
 * (UdpEthernetBackend.Enqueue). */
static void ring_put(EthNet *n, const uint8_t *data, int length)
{
    if (length > ETH_NET_MAX_FRAME)
    {
        length = ETH_NET_MAX_FRAME;
    }
    ring_lock(n);
    if (atomic_load(&n->ring_count) >= ETH_NET_RX_RING_SIZE)
    {
        unsigned long long dropped = atomic_fetch_add(&n->frames_dropped_ring, 1u) + 1u;
        ring_unlock(n);
        if ((dropped == 1u) || ((dropped % 1000u) == 0u))
        {
            LOG(LOG_CAT_NET, LOG_WARN, "Ethernet %s: receive ring full - %llu frame(s) dropped\n",
                n->description, dropped);
        }
        return;
    }
    memcpy(n->ring[n->ring_write], data, (size_t)length);
    n->ring_length[n->ring_write] = length;
    n->ring_write = (n->ring_write + 1) % ETH_NET_RX_RING_SIZE;
    atomic_fetch_add(&n->ring_count, 1);
    atomic_fetch_add(&n->frames_received, 1u);
    ring_unlock(n);
}

int eth_net_receive(EthNet *n, uint8_t *buffer, int size)
{
    int length;

    if ((n == NULL) || (buffer == NULL) || (size <= 0) || (atomic_load(&n->ring_count) == 0))
    {
        return 0;
    }
    ring_lock(n);
    if (atomic_load(&n->ring_count) == 0)
    {
        ring_unlock(n);
        return 0;
    }
    length = n->ring_length[n->ring_read];
    if (length > size)
    {
        length = size;
    }
    memcpy(buffer, n->ring[n->ring_read], (size_t)length);
    n->ring_read = (n->ring_read + 1) % ETH_NET_RX_RING_SIZE;
    atomic_fetch_sub(&n->ring_count, 1);
    ring_unlock(n);
    return length;
}

#ifdef __EMSCRIPTEN__

/* The browser's gateway backend: one card (plan decision D6), so one slot. */
static EthNet *s_gateway;
static struct
{
    int length;
    uint8_t data[ETH_NET_MAX_FRAME];
} s_gw_tx[ETH_NET_GATEWAY_TX_RING];
static int s_gw_tx_head;
static int s_gw_tx_tail;
static uint8_t s_gw_last[ETH_NET_MAX_FRAME];

int eth_net_start(EthNet *n)
{
    if ((n == NULL) || ((n->spec.kind != ETH_NET_NONE) && (n->spec.kind != ETH_NET_GATEWAY)))
    {
        LOG(LOG_CAT_NET, LOG_ERROR,
            "Ethernet: '%s' needs sockets; in the browser use net = gateway[:SEGMENT]\n",
            eth_net_description(n));
        return -1;
    }
    if (n->spec.kind == ETH_NET_GATEWAY)
    {
        if ((s_gateway != NULL) && (s_gateway != n))
        {
            LOG(LOG_CAT_NET, LOG_ERROR, "Ethernet: a gateway backend is already running\n");
            return -1;
        }
        s_gateway = n;
        s_gw_tx_head = 0;
        s_gw_tx_tail = 0;
    }
    atomic_store(&n->active, true);
    atomic_store(&n->started, true);
    return 0;
}

void eth_net_stop(EthNet *n)
{
    if (n != NULL)
    {
        atomic_store(&n->active, false);
        atomic_store(&n->started, false);
        if (s_gateway == n)
        {
            s_gateway = NULL;
        }
    }
}

void eth_net_send(EthNet *n, const uint8_t *data, int length)
{
    int next;

    if ((n == NULL) || (data == NULL) || (length <= 0) || (length > ETH_NET_MAX_FRAME) ||
        (n != s_gateway) || !atomic_load(&n->active))
    {
        return; /* "none": dropped */
    }
    next = (s_gw_tx_head + 1) % ETH_NET_GATEWAY_TX_RING;
    if (next == s_gw_tx_tail)
    {
        /* The page is not draining; drop and count rather than stall the guest. */
        atomic_fetch_add(&n->send_failures, 1u);
        return;
    }
    s_gw_tx[s_gw_tx_head].length = length;
    memcpy(s_gw_tx[s_gw_tx_head].data, data, (size_t)length);
    s_gw_tx_head = next;
    atomic_fetch_add(&n->frames_sent, 1u);
}

int eth_net_gateway_poll_tx(const uint8_t **data, int *length, int *segment)
{
    if ((s_gateway == NULL) || (s_gw_tx_head == s_gw_tx_tail) || (data == NULL) || (length == NULL) ||
        (segment == NULL))
    {
        return 0;
    }
    *length = s_gw_tx[s_gw_tx_tail].length;
    memcpy(s_gw_last, s_gw_tx[s_gw_tx_tail].data, (size_t)*length);
    *data = s_gw_last;
    *segment = s_gateway->spec.port;
    s_gw_tx_tail = (s_gw_tx_tail + 1) % ETH_NET_GATEWAY_TX_RING;
    return 1;
}

int eth_net_gateway_inject_rx(int segment, const uint8_t *data, int length)
{
    if ((s_gateway == NULL) || (segment != s_gateway->spec.port) || (data == NULL) || (length <= 0))
    {
        return -1;
    }
    ring_put(s_gateway, data, length);
    return 0;
}

void eth_net_gateway_set_link(int segment, int present)
{
    if ((s_gateway != NULL) && (segment == s_gateway->spec.port) && (present != 0))
    {
        atomic_fetch_add(&s_gateway->links_up, 1u);
    }
}

void eth_net_destroy(EthNet *n)
{
    if (n != NULL)
    {
        eth_net_stop(n);
        free(n->ring);
        free(n);
    }
}

#else /* sockets */

static void sleep_ms_local(int ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    (void)nanosleep(&ts, NULL);
#endif
}

/* Wait up to POLL_MS for fd to become readable. 1 = readable, 0 = timeout, -1 = error. */
static int wait_readable(nd_socket_t fd)
{
    nd_pollfd_t p;
    int r;

    memset(&p, 0, sizeof p);
    p.fd = ND_SOCK_NATIVE(fd);
    p.events = POLLIN;
    r = nd_poll(&p, 1u, POLL_MS);
    if (r < 0)
    {
        return -1;
    }
    if (r == 0)
    {
        return 0;
    }
    return ((p.revents & (POLLIN | POLLHUP | POLLERR)) != 0) ? 1 : 0;
}

/* ---- UDP ---- */

/* UdpEthernetBackend.SelectMulticastInterface: a running, multicast-capable,
 * non-loopback IPv4 interface. Deviation: RetroCore prefers wired/wireless
 * adapter types; the interface type is not portable here, so the first
 * suitable interface is used. Windows: INADDR_ANY (the kernel's route), not
 * yet ported. The choice is logged. */
static struct in_addr select_multicast_interface(void)
{
    struct in_addr any;
    any.s_addr = htonl(INADDR_ANY);
#ifndef _WIN32
    {
        struct ifaddrs *list = NULL;
        struct in_addr chosen = any;
        if (getifaddrs(&list) != 0)
        {
            return any;
        }
        for (struct ifaddrs *i = list; i != NULL; i = i->ifa_next)
        {
            const unsigned need = IFF_UP | IFF_RUNNING | IFF_MULTICAST;
            if ((i->ifa_addr == NULL) || (i->ifa_addr->sa_family != AF_INET) ||
                ((i->ifa_flags & need) != need) || ((i->ifa_flags & IFF_LOOPBACK) != 0u))
            {
                continue;
            }
            chosen = ((struct sockaddr_in *)(void *)i->ifa_addr)->sin_addr;
            break;
        }
        freeifaddrs(list);
        return chosen;
    }
#else
    return any;
#endif
}

static void *udp_thread(void *arg)
{
    EthNet *n = (EthNet *)arg;
    uint8_t scratch[ETH_NET_UDP_MAX_FRAME];
    int consecutive_errors = 0;

    while (!atomic_load(&n->stop_requested))
    {
        int r = wait_readable(n->sock);
        nd_ssize_t got;
        if (r == 0)
        {
            continue;
        }
        if (r > 0)
        {
            got = recvfrom(ND_SOCK_NATIVE(n->sock), (char *)scratch, (int)sizeof scratch, 0, NULL, NULL);
            if (got > 0)
            {
                consecutive_errors = 0;
                ring_put(n, scratch, (int)got);
                continue;
            }
        }
        if (atomic_load(&n->stop_requested))
        {
            break;
        }
        /* A persistent error must not spin a core (UdpEthernetBackend defect 4). */
        atomic_fetch_add(&n->receive_errors, 1u);
        consecutive_errors++;
        if ((consecutive_errors == 1) || ((consecutive_errors % 100) == 0))
        {
            LOG(LOG_CAT_NET, LOG_WARN, "Ethernet %s: receive error (%d in a row)\n", n->description,
                consecutive_errors);
        }
        sleep_ms_local((consecutive_errors < 10) ? 1 : 50);
    }
    return NULL;
}

static int udp_open(EthNet *n)
{
    const int on = 1;
    const unsigned char loop = 1u;
    struct sockaddr_in bind_addr;
    struct ip_mreq mreq;
    struct in_addr outgoing;
    char out_text[INET_ADDRSTRLEN];

    n->sock = (nd_socket_t)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (n->sock == ND_INVALID_SOCKET)
    {
        return -1;
    }
    /* Several emulators on one host bind the same port. */
    (void)setsockopt(ND_SOCK_NATIVE(n->sock), SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof on);
#ifdef SO_REUSEPORT
    (void)setsockopt(ND_SOCK_NATIVE(n->sock), SOL_SOCKET, SO_REUSEPORT, (const char *)&on, sizeof on);
#endif
    memset(&bind_addr, 0, sizeof bind_addr);
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons((uint16_t)n->spec.port);
    if (bind(ND_SOCK_NATIVE(n->sock), (struct sockaddr *)&bind_addr, sizeof bind_addr) != 0)
    {
        return -1;
    }
    outgoing = select_multicast_interface();
    if (setsockopt(ND_SOCK_NATIVE(n->sock), IPPROTO_IP, IP_MULTICAST_IF, (const char *)&outgoing,
                   sizeof outgoing) != 0)
    {
        return -1;
    }
    memset(&mreq, 0, sizeof mreq);
    if (inet_pton(AF_INET, n->spec.host, &mreq.imr_multiaddr) != 1)
    {
        return -1;
    }
    mreq.imr_interface = outgoing;
    if (setsockopt(ND_SOCK_NATIVE(n->sock), IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mreq,
                   sizeof mreq) != 0)
    {
        return -1;
    }
    /* Same-host peers must see each other; the card drops its own echo by source MAC. */
    (void)setsockopt(ND_SOCK_NATIVE(n->sock), IPPROTO_IP, IP_MULTICAST_LOOP, (const char *)&loop,
                     sizeof loop);
    memset(&n->udp_dest, 0, sizeof n->udp_dest);
    n->udp_dest.sin_family = AF_INET;
    n->udp_dest.sin_addr = mreq.imr_multiaddr;
    n->udp_dest.sin_port = htons((uint16_t)n->spec.port);
    if (inet_ntop(AF_INET, &outgoing, out_text, sizeof out_text) == NULL)
    {
        (void)snprintf(out_text, sizeof out_text, "?");
    }
    LOG(LOG_CAT_NET, LOG_INFO, "Ethernet: joined %s:%d via interface %s\n", n->spec.host,
        n->spec.port, out_text);
    return 0;
}

/* ---- TCP ---- */

/* Read exactly count bytes; false on EOF, error or stop. */
static bool read_fully(EthNet *n, nd_socket_t fd, uint8_t *buffer, int count)
{
    int got = 0;

    while (got < count)
    {
        int r = wait_readable(fd);
        nd_ssize_t k;
        if (atomic_load(&n->stop_requested))
        {
            return false;
        }
        if (r == 0)
        {
            continue;
        }
        if (r < 0)
        {
            return false;
        }
        k = recv(ND_SOCK_NATIVE(fd), (char *)buffer + got, count - got, 0);
        if (k <= 0)
        {
            return false;
        }
        got += (int)k;
    }
    return true;
}

static bool write_fully(nd_socket_t fd, const uint8_t *data, int count)
{
    int sent = 0;

    while (sent < count)
    {
        nd_ssize_t k = send(ND_SOCK_NATIVE(fd), (const char *)data + sent, count - sent, MSG_NOSIGNAL);
        if (k <= 0)
        {
            return false;
        }
        sent += (int)k;
    }
    return true;
}

/* TcpEthernetBackend.Handshake: send ours, read and check the peer's. */
static bool tcp_handshake(EthNet *n, nd_socket_t fd)
{
    const uint8_t hello[5] = {0x52u, 0x45u, 0x54u, 0x48u, ETH_NET_TCP_VERSION};
    uint8_t peer[5];

    if (!write_fully(fd, hello, 5))
    {
        return false;
    }
    if (!read_fully(n, fd, peer, 5))
    {
        return false;
    }
    return (memcmp(peer, hello, 4u) == 0) && (peer[4] >= 1u);
}

/* TcpEthernetBackend.RunConnection: handshake, then frames until the link drops. */
static void tcp_run_connection(EthNet *n, nd_socket_t fd)
{
    uint8_t len_buf[2];
    uint8_t *frame = malloc(ETH_NET_TCP_MAX_FRAME);
    const int on = 1;

    if (frame == NULL)
    {
        (void)nd_socket_close(fd);
        return;
    }
    (void)setsockopt(ND_SOCK_NATIVE(fd), IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof on);
    if (!tcp_handshake(n, fd))
    {
        LOG(LOG_CAT_NET, LOG_WARN, "Ethernet %s: handshake rejected - closing link\n", n->description);
        (void)nd_socket_close(fd);
        free(frame);
        return;
    }
    pthread_mutex_lock(&n->send_lock);
    n->conn = fd;
    pthread_mutex_unlock(&n->send_lock);
    atomic_store(&n->active, true);
    atomic_fetch_add(&n->links_up, 1u);
    LOG(LOG_CAT_NET, LOG_INFO, "Ethernet %s: link up\n", n->description);

    while (!atomic_load(&n->stop_requested))
    {
        int len;
        if (!read_fully(n, fd, len_buf, 2))
        {
            break;
        }
        len = (len_buf[0] << 8) | len_buf[1];
        if ((len <= 0) || (len > ETH_NET_TCP_MAX_FRAME) || !read_fully(n, fd, frame, len))
        {
            break;
        }
        ring_put(n, frame, len);
    }

    atomic_store(&n->active, false);
    pthread_mutex_lock(&n->send_lock);
    if (n->conn == fd)
    {
        n->conn = ND_INVALID_SOCKET;
    }
    pthread_mutex_unlock(&n->send_lock);
    (void)nd_socket_close(fd);
    free(frame);
    LOG(LOG_CAT_NET, LOG_INFO, "Ethernet %s: link down\n", n->description);
}

static void *tcp_listen_thread(void *arg)
{
    EthNet *n = (EthNet *)arg;

    while (!atomic_load(&n->stop_requested))
    {
        int r = wait_readable(n->sock);
        nd_socket_t c;
        if (r <= 0)
        {
            if (r < 0)
            {
                sleep_ms_local(100);
            }
            continue;
        }
        c = (nd_socket_t)accept(ND_SOCK_NATIVE(n->sock), NULL, NULL);
        if (c == ND_INVALID_SOCKET)
        {
            sleep_ms_local(100);
            continue;
        }
        tcp_run_connection(n, c);
    }
    return NULL;
}

/* Connect to host:port; ND_INVALID_SOCKET on failure. */
static nd_socket_t tcp_dial(const char *host, int port)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    char port_text[16];
    nd_socket_t fd = ND_INVALID_SOCKET;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    (void)snprintf(port_text, sizeof port_text, "%d", port);
    if (getaddrinfo(host, port_text, &hints, &res) != 0)
    {
        return ND_INVALID_SOCKET;
    }
    for (struct addrinfo *a = res; a != NULL; a = a->ai_next)
    {
        fd = (nd_socket_t)socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd == ND_INVALID_SOCKET)
        {
            continue;
        }
        if (connect(ND_SOCK_NATIVE(fd), a->ai_addr, (nd_socklen_t)a->ai_addrlen) == 0)
        {
            break;
        }
        (void)nd_socket_close(fd);
        fd = ND_INVALID_SOCKET;
    }
    freeaddrinfo(res);
    return fd;
}

/* TcpEthernetBackend.ConnectLoop: dial with capped exponential backoff 250..4000 ms. */
static void *tcp_connect_thread(void *arg)
{
    EthNet *n = (EthNet *)arg;
    int backoff_ms = 250;

    while (!atomic_load(&n->stop_requested))
    {
        nd_socket_t c = tcp_dial(n->spec.host, n->spec.port);
        if (c == ND_INVALID_SOCKET)
        {
            for (int waited = 0; (waited < backoff_ms) && !atomic_load(&n->stop_requested); waited += 50)
            {
                sleep_ms_local(50);
            }
            backoff_ms = (backoff_ms * 2 > 4000) ? 4000 : backoff_ms * 2;
            continue;
        }
        backoff_ms = 250;
        tcp_run_connection(n, c);
        if (!atomic_load(&n->stop_requested))
        {
            sleep_ms_local(backoff_ms);
        }
    }
    return NULL;
}

static int tcp_open_listener(EthNet *n)
{
    const int on = 1;
    struct sockaddr_in a;
    nd_socklen_t alen = (nd_socklen_t)sizeof a;

    n->sock = (nd_socket_t)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (n->sock == ND_INVALID_SOCKET)
    {
        return -1;
    }
    (void)setsockopt(ND_SOCK_NATIVE(n->sock), SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof on);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((uint16_t)n->spec.port);
    if ((bind(ND_SOCK_NATIVE(n->sock), (struct sockaddr *)&a, sizeof a) != 0) ||
        (listen(ND_SOCK_NATIVE(n->sock), 1) != 0) ||
        (getsockname(ND_SOCK_NATIVE(n->sock), (struct sockaddr *)&a, &alen) != 0))
    {
        return -1;
    }
    atomic_store(&n->listen_port, (int)ntohs(a.sin_port));
    describe(&n->spec, atomic_load(&n->listen_port));
    (void)snprintf(n->description, sizeof n->description, "%s", n->spec.description);
    LOG(LOG_CAT_NET, LOG_INFO, "Ethernet: listening on port %d\n", atomic_load(&n->listen_port));
    return 0;
}

/* ---- TAP (Linux) ---- */

#ifdef __linux__
/* Attach to an existing TAP interface: IFF_TAP (ethernet frames), IFF_NO_PI (no packet
 * information header, so each read/write is exactly one frame). */
static int tap_open(EthNet *n)
{
    struct ifreq ifr;
    int fd = open("/dev/net/tun", O_RDWR | O_CLOEXEC);

    if (fd < 0)
    {
        return -1;
    }
    memset(&ifr, 0, sizeof ifr);
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    (void)snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%.15s", n->spec.host); /* checked <= 15 at parse */
    if (ioctl(fd, TUNSETIFF, &ifr) != 0)
    {
        const int err = errno;
        if (err == EBUSY)
        {
            LOG(LOG_CAT_NET, LOG_ERROR,
                "Ethernet tap:%s: cannot attach (%s) - another program already has it open\n",
                n->spec.host, strerror(err));
        }
        else
        {
            LOG(LOG_CAT_NET, LOG_ERROR,
                "Ethernet tap:%s: cannot attach (%s). It must exist and belong to you: "
                "sudo ip tuntap add dev %s mode tap user $USER\n",
                n->spec.host, strerror(err), n->spec.host);
        }
        (void)close(fd);
        return -1;
    }
    n->tap_fd = fd;
    LOG(LOG_CAT_NET, LOG_INFO, "Ethernet: attached to TAP interface %s\n", ifr.ifr_name);
    return 0;
}

static void *tap_thread(void *arg)
{
    EthNet *n = (EthNet *)arg;
    uint8_t frame[ETH_NET_MAX_FRAME];

    while (!atomic_load(&n->stop_requested))
    {
        struct pollfd p;
        int r;
        ssize_t got;
        p.fd = n->tap_fd;
        p.events = POLLIN;
        p.revents = 0;
        r = poll(&p, 1u, POLL_MS);
        if (r == 0)
        {
            continue;
        }
        if (r > 0)
        {
            got = read(n->tap_fd, frame, sizeof frame);
            if (got > 0)
            {
                ring_put(n, frame, (int)got);
                continue;
            }
        }
        if (atomic_load(&n->stop_requested))
        {
            break;
        }
        atomic_fetch_add(&n->receive_errors, 1u);
        sleep_ms_local(50);
    }
    return NULL;
}
#endif /* __linux__ */

/* ---- pcap (Windows, Npcap; see eth_pcap.c) ---- */

static void *pcap_thread(void *arg)
{
    EthNet *n = (EthNet *)arg;
    uint8_t frame[ETH_NET_MAX_FRAME];

    while (!atomic_load(&n->stop_requested))
    {
        const int got = eth_pcap_receive(n->pcap, frame, (int)sizeof frame);
        if (got > 0)
        {
            ring_put(n, frame, got);
        }
        else if (got < 0)
        {
            atomic_fetch_add(&n->receive_errors, 1u);
            sleep_ms_local(50);
        }
    }
    return NULL;
}

int eth_net_start(EthNet *n)
{
    void *(*thread_fn)(void *) = NULL;
    int rc = 0;

    if (n == NULL)
    {
        return -1;
    }
    if (atomic_load(&n->started))
    {
        return 0;
    }
    atomic_store(&n->stop_requested, false);
    if (n->spec.kind == ETH_NET_NONE)
    {
        atomic_store(&n->active, true);
        atomic_store(&n->started, true);
        return 0;
    }
    if (nd_net_init() != 0)
    {
        return -1;
    }
    switch (n->spec.kind)
    {
    case ETH_NET_UDP:
        rc = udp_open(n);
        thread_fn = udp_thread;
        break;
    case ETH_NET_TCP_LISTEN:
        rc = tcp_open_listener(n);
        thread_fn = tcp_listen_thread;
        break;
    case ETH_NET_TCP_CONNECT:
        thread_fn = tcp_connect_thread;
        break;
    case ETH_NET_GATEWAY:
        LOG(LOG_CAT_NET, LOG_ERROR,
            "Ethernet %s: the gateway backend is for the browser build; natively join the "
            "gateway's segment with tcp:GATEWAYHOST:3094\n", n->description);
        rc = -1;
        break;
    case ETH_NET_PCAP:
        n->pcap = eth_pcap_open(n->spec.host);
        rc = (n->pcap != NULL) ? 0 : -1;
        thread_fn = pcap_thread;
        break;
    case ETH_NET_TAP:
#ifdef __linux__
        rc = tap_open(n);
        thread_fn = tap_thread;
#else
        LOG(LOG_CAT_NET, LOG_ERROR, "Ethernet %s: TAP is only built for Linux\n", n->description);
        rc = -1;
#endif
        break;
    case ETH_NET_NONE:
        /* fall through */
    default:
        rc = -1;
        break;
    }
    if ((rc == 0) && (pthread_create(&n->thread, NULL, thread_fn, n) != 0))
    {
        rc = -1;
    }
    if (rc != 0)
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Ethernet %s: start failed (socket error %d)\n", n->description,
            nd_last_socket_error());
        if (n->sock != ND_INVALID_SOCKET)
        {
            (void)nd_socket_close(n->sock);
            n->sock = ND_INVALID_SOCKET;
        }
#ifdef __linux__
        if (n->tap_fd >= 0)
        {
            (void)close(n->tap_fd);
            n->tap_fd = -1;
        }
#endif
        eth_pcap_close(n->pcap);
        n->pcap = NULL;
        nd_net_shutdown();
        return -1;
    }
    n->thread_running = true;
    atomic_store(&n->started, true);
    if ((n->spec.kind == ETH_NET_UDP) || (n->spec.kind == ETH_NET_TAP) || (n->spec.kind == ETH_NET_PCAP))
    {
        atomic_store(&n->active, true);
    }
    return 0;
}

void eth_net_stop(EthNet *n)
{
    if ((n == NULL) || !atomic_load(&n->started))
    {
        return;
    }
    atomic_store(&n->stop_requested, true);
    atomic_store(&n->active, false);
    /* Join before closing: the thread polls with a POLL_MS timeout and sees the stop flag
     * (UdpEthernetBackend defect 4: never close a socket the loop still uses). */
    if (n->thread_running)
    {
        (void)pthread_join(n->thread, NULL);
        n->thread_running = false;
    }
    if (n->sock != ND_INVALID_SOCKET)
    {
        (void)nd_socket_close(n->sock);
        n->sock = ND_INVALID_SOCKET;
    }
#ifdef __linux__
    if (n->tap_fd >= 0)
    {
        (void)close(n->tap_fd);
        n->tap_fd = -1;
    }
#endif
    eth_pcap_close(n->pcap);
    n->pcap = NULL;
    if (n->spec.kind != ETH_NET_NONE)
    {
        nd_net_shutdown();
    }
    atomic_store(&n->started, false);
    LOG(LOG_CAT_NET, LOG_INFO, "Ethernet %s: stopped (rx %llu, tx %llu, dropped %llu)\n",
        n->description, (unsigned long long)atomic_load(&n->frames_received),
        (unsigned long long)atomic_load(&n->frames_sent),
        (unsigned long long)atomic_load(&n->frames_dropped_ring));
}

void eth_net_destroy(EthNet *n)
{
    if (n == NULL)
    {
        return;
    }
    eth_net_stop(n);
    pthread_mutex_destroy(&n->ring_lock);
    pthread_mutex_destroy(&n->send_lock);
    free(n->ring);
    free(n);
}

void eth_net_send(EthNet *n, const uint8_t *data, int length)
{
    if ((n == NULL) || (data == NULL) || (length <= 0) || !atomic_load(&n->active))
    {
        return;
    }
    switch (n->spec.kind)
    {
    case ETH_NET_UDP:
        if (sendto(ND_SOCK_NATIVE(n->sock), (const char *)data, length, 0,
                   (const struct sockaddr *)&n->udp_dest, sizeof n->udp_dest) == (nd_ssize_t)length)
        {
            atomic_fetch_add(&n->frames_sent, 1u);
        }
        else if (atomic_fetch_add(&n->send_failures, 1u) == 0u)
        {
            /* A dropped datagram is acceptable (COSMOS retransmits) but is counted, and the
             * first failure is logged (UdpEthernetBackend defect 3). */
            LOG(LOG_CAT_NET, LOG_WARN, "Ethernet %s: send failed (error %d) - further failures counted only\n",
                n->description, nd_last_socket_error());
        }
        break;
    case ETH_NET_TCP_CONNECT:
    case ETH_NET_TCP_LISTEN:
    {
        uint8_t header[2];
        bool ok;
        if (length > ETH_NET_TCP_MAX_FRAME)
        {
            return;
        }
        header[0] = (uint8_t)((unsigned)length >> 8u);
        header[1] = (uint8_t)((unsigned)length & 0xFFu);
        pthread_mutex_lock(&n->send_lock);
        ok = (n->conn != ND_INVALID_SOCKET) && write_fully(n->conn, header, 2) &&
             write_fully(n->conn, data, length);
        if (!ok && (n->conn != ND_INVALID_SOCKET))
        {
            /* Peer went away mid-write: drop the link; the thread's read fails and it
             * re-dials or re-accepts. */
            (void)shutdown(ND_SOCK_NATIVE(n->conn), 2);
        }
        pthread_mutex_unlock(&n->send_lock);
        if (ok)
        {
            atomic_fetch_add(&n->frames_sent, 1u);
        }
        else
        {
            atomic_fetch_add(&n->send_failures, 1u);
            atomic_store(&n->active, false);
        }
        break;
    }
    case ETH_NET_PCAP:
        if (eth_pcap_send(n->pcap, data, length) == 0)
        {
            atomic_fetch_add(&n->frames_sent, 1u);
        }
        else if (atomic_fetch_add(&n->send_failures, 1u) == 0u)
        {
            LOG(LOG_CAT_NET, LOG_WARN, "Ethernet %s: send failed - further failures counted only\n",
                n->description);
        }
        break;
    case ETH_NET_TAP:
#ifdef __linux__
        if (write(n->tap_fd, data, (size_t)length) == (ssize_t)length)
        {
            atomic_fetch_add(&n->frames_sent, 1u);
        }
        else if (atomic_fetch_add(&n->send_failures, 1u) == 0u)
        {
            LOG(LOG_CAT_NET, LOG_WARN, "Ethernet %s: write failed (%s) - further failures counted only\n",
                n->description, strerror(errno));
        }
#endif
        break;
    case ETH_NET_NONE:
        /* fall through */
    default:
        break; /* NullEthernetBackend: dropped */
    }
}

int eth_net_gateway_poll_tx(const uint8_t **data, int *length, int *segment)
{
    (void)data;
    (void)length;
    (void)segment;
    return 0;
}

int eth_net_gateway_inject_rx(int segment, const uint8_t *data, int length)
{
    (void)segment;
    (void)data;
    (void)length;
    return -1;
}

void eth_net_gateway_set_link(int segment, int present)
{
    (void)segment;
    (void)present;
}

#endif /* sockets */
