/*
 * net_eth_backend.c - Ports of the RetroCore network backend tests.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * NullEthernetBackendTests (Emulated.Tests.Chips), TcpEthernetBackendTests and
 * UdpEthernetBackendResilienceTests (Emulated.Tests/NDBusDevices). These use
 * real sockets on this host, so they are a separate binary (test_eth_net),
 * linked with ndlib for the socket shim.
 *
 * Difference in the C API: a backend never calls the card; the emulation
 * thread takes frames with eth_net_receive(). "OnPacketReceived fired" in the
 * C# becomes "eth_net_receive() returned the frame" here.
 */

#include "eth_test.h"

#include <string.h>
#include <time.h>

#include "net/eth_net.h"

static void wait_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    (void)nanosleep(&ts, NULL);
}

static EthNet *make(const char *spec)
{
    EthNetSpec s;
    if (eth_net_parse_spec(spec, &s) != 0)
    {
        return NULL;
    }
    return eth_net_create(&s);
}

/* Wait up to timeout_ms for one frame; returns its length (0 = none). */
static int wait_frame(EthNet *n, uint8_t *buf, int size, int timeout_ms)
{
    for (int waited = 0; waited <= timeout_ms; waited += 10)
    {
        int len = eth_net_receive(n, buf, size);
        if (len > 0)
        {
            return len;
        }
        wait_ms(10);
    }
    return 0;
}

static bool wait_active(EthNet *a, EthNet *b, int timeout_ms)
{
    for (int waited = 0; waited <= timeout_ms; waited += 10)
    {
        if (eth_net_is_active(a) && eth_net_is_active(b))
        {
            return true;
        }
        wait_ms(10);
    }
    return false;
}

/* ---- NullEthernetBackendTests ---- */

ETH_TEST(NullEthernetBackendTests, Test_NullBackend_StartsAndStops)
{
    EthNet *n = make("none");
    CHECK(n != NULL);
    CHECK(!eth_net_is_active(n));
    CHECK_EQ(eth_net_start(n), 0);
    CHECK(eth_net_is_active(n));
    eth_net_stop(n);
    CHECK(!eth_net_is_active(n));
    eth_net_destroy(n);
}

ETH_TEST(NullEthernetBackendTests, Test_NullBackend_SendDoesNotCrash)
{
    uint8_t p[64];
    EthNet *n = make("none");
    for (int i = 0; i < 64; i++)
    {
        p[i] = (uint8_t)i;
    }
    CHECK_EQ(eth_net_start(n), 0);
    eth_net_send(n, p, 64);
    eth_net_stop(n);
    eth_net_destroy(n);
}

ETH_TEST(NullEthernetBackendTests, Test_NullBackend_Description)
{
    EthNet *n = make("none");
    CHECK(eth_net_description(n)[0] != '\0');
    eth_net_destroy(n);
}

ETH_TEST(NullEthernetBackendTests, Test_NullBackend_NeverReceives)
{
    uint8_t p[64];
    EthNet *n = make("none");
    memset(p, 0, sizeof p);
    CHECK_EQ(eth_net_start(n), 0);
    eth_net_send(n, p, 64);
    CHECK_EQ(eth_net_receive(n, p, (int)sizeof p), 0);
    eth_net_stop(n);
    eth_net_destroy(n);
}

/* The C# test listed interfaces; here "pcap:list" logs them and opens nothing, so start fails. */
ETH_TEST(NullEthernetBackendTests, Test_PcapBackend_ListInterfaces)
{
    EthNet *n = make("pcap:list");
    CHECK(n != NULL);
    CHECK_EQ(eth_net_start(n), -1);
    eth_net_destroy(n);
}

/* ---- TcpEthernetBackendTests ---- */

ETH_TEST(TcpEthernetBackendTests, FromSpec_ParsesListenForms)
{
    EthNetSpec a;
    EthNetSpec b;
    CHECK_EQ(eth_net_parse_spec("tcp-listen:5010", &a), 0);
    CHECK_EQ(eth_net_parse_spec("listen:0", &b), 0);
    CHECK(strcmp(a.description, "tcp-listen:5010") == 0);
}

ETH_TEST(TcpEthernetBackendTests, FromSpec_ParsesConnectForms)
{
    EthNetSpec a;
    EthNetSpec b;
    CHECK_EQ(eth_net_parse_spec("tcp:192.168.1.10:5010", &a), 0);
    CHECK_EQ(eth_net_parse_spec("relay.example.com:9000", &b), 0);
    CHECK(strcmp(a.description, "tcp:192.168.1.10:5010") == 0);
    CHECK(strcmp(b.description, "tcp:relay.example.com:9000") == 0);
}

ETH_TEST(TcpEthernetBackendTests, FromSpec_RejectsJunk)
{
    EthNetSpec s;
    CHECK_EQ(eth_net_parse_spec("", &s), -1);
    CHECK_EQ(eth_net_parse_spec("nonsense", &s), -1);
    CHECK_EQ(eth_net_parse_spec("host:notaport", &s), -1);
    CHECK_EQ(eth_net_parse_spec("listen:99999", &s), -1);
}

ETH_TEST(TcpEthernetBackendTests, FromSpec_DefaultsPortTo3094)
{
    EthNetSpec l;
    EthNetSpec c;
    CHECK_EQ(eth_net_parse_spec("listen", &l), 0);
    CHECK(strcmp(l.description, "tcp-listen:3094") == 0);
    CHECK_EQ(eth_net_parse_spec("tcp:relay.example.com", &c), 0);
    CHECK(strcmp(c.description, "tcp:relay.example.com:3094") == 0);
}

ETH_TEST(TcpEthernetBackendTests, Loopback_DeliversFramesBothDirections)
{
    char spec[64];
    uint8_t a[64];
    uint8_t b[128];
    uint8_t rx[256];
    EthNet *server = make("listen:0");
    EthNet *client;

    CHECK_EQ(eth_net_start(server), 0);
    CHECK(eth_net_local_port(server) > 0);
    (void)snprintf(spec, sizeof spec, "tcp:127.0.0.1:%d", eth_net_local_port(server));
    client = make(spec);
    CHECK_EQ(eth_net_start(client), 0);
    CHECK(wait_active(server, client, 5000));

    for (int i = 0; i < 64; i++)
    {
        a[i] = (uint8_t)(0xAA + i);
    }
    eth_net_send(client, a, 64);
    CHECK_EQ(wait_frame(server, rx, (int)sizeof rx, 5000), 64);
    CHECK(memcmp(rx, a, 64u) == 0);

    for (int i = 0; i < 128; i++)
    {
        b[i] = (uint8_t)(0xBB + i);
    }
    eth_net_send(server, b, 128);
    CHECK_EQ(wait_frame(client, rx, (int)sizeof rx, 5000), 128);
    CHECK(memcmp(rx, b, 128u) == 0);

    eth_net_destroy(client);
    eth_net_destroy(server);
}

/* ---- UdpEthernetBackendResilienceTests ---- */

#define TEST_GROUP "239.3.9.77"
#define TEST_PORT 33094

/* BuildFrame(sourceSystem, marker): 64-byte broadcast 802.3 frame, marker at byte 17. */
static void build_udp_frame(uint8_t *f, uint16_t source_system, uint8_t marker)
{
    memset(f, 0, 64u);
    memset(f, 0xFF, 6u);
    f[6] = 0x08u;
    f[7] = 0x00u;
    f[8] = 0x26u;
    f[9] = (uint8_t)(source_system & 0xFFu);
    f[10] = (uint8_t)((source_system >> 8u) & 0xFFu);
    f[12] = 0x00u;
    f[13] = 0x0Eu;
    f[14] = 0xA8u;
    f[15] = 0xA8u;
    f[16] = 0x03u;
    f[17] = marker;
}

static EthNet *make_udp(int port)
{
    char spec[64];
    (void)snprintf(spec, sizeof spec, "udp:%s:%d", TEST_GROUP, port);
    return make(spec);
}

ETH_TEST(UdpEthernetBackendResilienceTests, FromSpec_StillParsesEveryDocumentedForm)
{
    EthNetSpec s;
    CHECK_EQ(eth_net_parse_spec("udp", &s), 0);
    CHECK_EQ(eth_net_parse_spec("udp:mcast", &s), 0);
    CHECK_EQ(eth_net_parse_spec("udp:239.3.9.4:3094", &s), 0);
    CHECK_EQ(eth_net_parse_spec("udp:4000", &s), 0);
    CHECK(s.kind == ETH_NET_UDP);
    CHECK_EQ(eth_net_parse_spec("tcp:127.0.0.1:5010", &s), 0);
    CHECK(s.kind != ETH_NET_UDP);
    CHECK_EQ(eth_net_parse_spec("", &s), -1);
}

/* The C# test holds the card's receive handler blocked while 20 frames arrive. Here the
 * card is "slow" by not calling eth_net_receive during the burst; the frames must be
 * waiting in the ring afterwards, i.e. the socket kept being drained. */
ETH_TEST(UdpEthernetBackendResilienceTests, SlowCard_DoesNotStopTheSocketBeingDrained)
{
    uint8_t f[64];
    uint8_t rx[256];
    EthNet *sender = make_udp(TEST_PORT);
    EthNet *receiver = make_udp(TEST_PORT);
    int delivered = 0;
    EthNetStats st;

    CHECK_EQ(eth_net_start(receiver), 0);
    CHECK_EQ(eth_net_start(sender), 0);
    CHECK(eth_net_is_active(receiver));
    CHECK(eth_net_is_active(sender));
    wait_ms(500);
    for (int i = 0; i < 21; i++)
    {
        build_udp_frame(f, 102u, (uint8_t)(1 + i));
        eth_net_send(sender, f, 64);
        wait_ms(10);
    }
    wait_ms(500);
    while (eth_net_receive(receiver, rx, (int)sizeof rx) > 0)
    {
        delivered++;
    }
    CHECK(delivered > 1);
    eth_net_get_stats(receiver, &st);
    CHECK(st.frames_received > 1u);
    eth_net_destroy(sender);
    eth_net_destroy(receiver);
}

ETH_TEST(UdpEthernetBackendResilienceTests, Frame_ReachesAnotherMember_AndCountersMove)
{
    uint8_t f[64];
    uint8_t rx[256];
    EthNet *sender = make_udp(TEST_PORT + 1);
    EthNet *receiver = make_udp(TEST_PORT + 1);
    EthNetStats ss;
    EthNetStats rs;
    int len;

    CHECK_EQ(eth_net_start(receiver), 0);
    CHECK_EQ(eth_net_start(sender), 0);
    wait_ms(500);
    build_udp_frame(f, 103u, 0x5Au);
    eth_net_send(sender, f, 64);
    len = wait_frame(receiver, rx, (int)sizeof rx, 10000);
    CHECK(len >= 18);
    CHECK_EQ(rx[17], 0x5A);
    eth_net_get_stats(sender, &ss);
    eth_net_get_stats(receiver, &rs);
    CHECK(ss.frames_sent > 0u);
    CHECK(rs.frames_received > 0u);
    eth_net_destroy(sender);
    eth_net_destroy(receiver);
}

ETH_TEST(UdpEthernetBackendResilienceTests, Stop_IsIdempotentAndClean)
{
    uint8_t f[64];
    EthNet *n = make_udp(TEST_PORT + 2);
    CHECK_EQ(eth_net_start(n), 0);
    CHECK(eth_net_is_active(n));
    eth_net_stop(n);
    CHECK(!eth_net_is_active(n));
    eth_net_stop(n);
    CHECK(!eth_net_is_active(n));
    build_udp_frame(f, 100u, 1u);
    eth_net_send(n, f, 64);
    eth_net_destroy(n);
}

/* ---- TAP spec (nd100x addition; the interface itself needs root to create) ---- */

ETH_TEST(Port, Tap_SpecParses)
{
    EthNetSpec s;
    CHECK_EQ(eth_net_parse_spec("tap:nd0", &s), 0);
    CHECK(s.kind == ETH_NET_TAP);
    CHECK(strcmp(s.host, "nd0") == 0);
    CHECK(strcmp(s.description, "tap:nd0") == 0);
    CHECK_EQ(eth_net_parse_spec("tap:", &s), -1);
    CHECK_EQ(eth_net_parse_spec("tap:abcdefghijklmnop", &s), -1); /* 16 chars > IFNAMSIZ-1 */
}

ETH_TEST(Port, Tap_MissingInterfaceFailsToStart)
{
    EthNet *n = make("tap:ndxnotthere9");
    CHECK(n != NULL);
    /* Not existing and not ours to create without CAP_NET_ADMIN: start must fail cleanly. */
    CHECK_EQ(eth_net_start(n), -1);
    CHECK(!eth_net_is_active(n));
    eth_net_destroy(n);
}

/* ---- gateway spec (browser build backend; native refuses to start it) ---- */

ETH_TEST(Port, Gateway_SpecParsesAndNativeRefuses)
{
    EthNetSpec s;
    EthNet *n;
    CHECK_EQ(eth_net_parse_spec("gateway", &s), 0);
    CHECK(s.kind == ETH_NET_GATEWAY);
    CHECK_EQ(s.port, 0);
    CHECK(strcmp(s.description, "gateway:0") == 0);
    CHECK_EQ(eth_net_parse_spec("gateway:3", &s), 0);
    CHECK_EQ(s.port, 3);
    CHECK_EQ(eth_net_parse_spec("gateway:256", &s), -1);
    CHECK_EQ(eth_net_parse_spec("gateway:x", &s), -1);
    n = make("gateway:3");
    CHECK_EQ(eth_net_start(n), -1); /* sockets build: use tcp:HOST:3094 instead */
    eth_net_destroy(n);
}

ETH_TEST(Port, Pcap_SpecParsesAndUnknownAdapterRefuses)
{
    EthNetSpec s;
    EthNet *n;
    CHECK_EQ(eth_net_parse_spec("pcap:ND-Loopback", &s), 0);
    CHECK(s.kind == ETH_NET_PCAP);
    CHECK(strcmp(s.host, "ND-Loopback") == 0);
    CHECK(strcmp(s.description, "pcap:ND-Loopback") == 0);
    CHECK_EQ(eth_net_parse_spec("pcap: Microsoft KM-TEST Loopback Adapter ", &s), 0);
    CHECK(strcmp(s.host, "Microsoft KM-TEST Loopback Adapter") == 0);
    CHECK_EQ(eth_net_parse_spec("pcap:", &s), -1);
    /* Windows: no adapter of that name; elsewhere: no pcap backend at all */
    n = make("pcap:no-such-adapter-xyzzy");
    CHECK_EQ(eth_net_start(n), -1);
    eth_net_destroy(n);
}
