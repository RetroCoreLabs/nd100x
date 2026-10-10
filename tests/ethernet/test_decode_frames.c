/*
 * test_decode_frames.c - The F12 packet view's one-line decoder (eth_decode.c).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Frames are built byte by byte from the RFC layouts (ARP RFC 826, IPv4
 * RFC 791, ICMP RFC 792, TCP RFC 793, UDP RFC 768) and the expected text is
 * written out in full.
 */

#include "eth_test.h"

#include <string.h>

#include "eth_decode.h"

static const uint8_t ND_MAC[6] = {0x08, 0x00, 0x26, 0xD2, 0x00, 0x00};
static const uint8_t HOST_MAC[6] = {0x4A, 0x32, 0x91, 0xF9, 0xE4, 0x69};

static int eth_header(uint8_t *f, const uint8_t *dst, const uint8_t *src, unsigned type)
{
    memcpy(f, dst, 6u);
    memcpy(f + 6, src, 6u);
    f[12] = (uint8_t)(type >> 8);
    f[13] = (uint8_t)(type & 0xFFu);
    return 14;
}

static int ipv4_header(uint8_t *ip, uint8_t proto, int payload)
{
    const int total = 20 + payload;
    memset(ip, 0, 20u);
    ip[0] = 0x45u;
    ip[2] = (uint8_t)(total >> 8);
    ip[3] = (uint8_t)(total & 0xFF);
    ip[8] = 64u;
    ip[9] = proto;
    ip[12] = 192u; ip[13] = 168u; ip[14] = 210u; ip[15] = 1u;
    ip[16] = 192u; ip[17] = 168u; ip[18] = 210u; ip[19] = 40u;
    return 20;
}

ETH_TEST(Port, Decode_MacNames)
{
    static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t mcast[6] = {0x01, 0x00, 0x5E, 0x03, 0x09, 0x04};
    char t[64];
    eth_decode_mac_name(ND_MAC, ND_MAC, t, sizeof t);
    CHECK(strcmp(t, "nd") == 0);
    eth_decode_mac_name(bcast, ND_MAC, t, sizeof t);
    CHECK(strcmp(t, "bcast") == 0);
    eth_decode_mac_name(mcast, ND_MAC, t, sizeof t);
    CHECK(strcmp(t, "mcast 01:00:5e:03:09:04") == 0);
    eth_decode_mac_name(HOST_MAC, ND_MAC, t, sizeof t);
    CHECK(strcmp(t, "4a:32:91:f9:e4:69") == 0);
}

ETH_TEST(Port, Decode_ArpRequestAndReply)
{
    static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t f[60];
    char t[160];
    uint8_t *a = f + 14;

    memset(f, 0, sizeof f);
    (void)eth_header(f, bcast, HOST_MAC, 0x0806u);
    a[1] = 1u; a[2] = 0x08u; a[4] = 6u; a[5] = 4u; a[7] = 1u; /* htype 1, ptype IPv4, request */
    memcpy(a + 8, HOST_MAC, 6u);
    a[14] = 192u; a[15] = 168u; a[16] = 210u; a[17] = 1u;
    a[24] = 192u; a[25] = 168u; a[26] = 210u; a[27] = 40u;
    eth_decode_summary(f, (int)sizeof f, t, sizeof t);
    CHECK(strcmp(t, "ARP who-has 192.168.210.40 tell 192.168.210.1") == 0);

    a[7] = 2u; /* reply: sender is the ND */
    memcpy(a + 8, ND_MAC, 6u);
    a[14] = 192u; a[15] = 168u; a[16] = 210u; a[17] = 40u;
    eth_decode_summary(f, (int)sizeof f, t, sizeof t);
    CHECK(strcmp(t, "ARP 192.168.210.40 is-at 08:00:26:d2:00:00") == 0);
}

ETH_TEST(Port, Decode_IcmpEcho)
{
    uint8_t f[98];
    char t[160];
    int o;

    memset(f, 0, sizeof f);
    o = eth_header(f, ND_MAC, HOST_MAC, 0x0800u);
    o += ipv4_header(f + o, 1u, 64);
    f[o] = 8u;                 /* echo request */
    f[o + 5] = 7u;             /* id 7 */
    f[o + 7] = 1u;             /* seq 1 */
    eth_decode_summary(f, (int)sizeof f, t, sizeof t);
    CHECK(strcmp(t, "ICMP echo request 192.168.210.1 > 192.168.210.40 id 7 seq 1") == 0);
    f[o] = 0u;
    eth_decode_summary(f, (int)sizeof f, t, sizeof t);
    CHECK(strcmp(t, "ICMP echo reply 192.168.210.1 > 192.168.210.40 id 7 seq 1") == 0);
}

ETH_TEST(Port, Decode_TcpSynAndData)
{
    uint8_t f[14 + 20 + 20 + 5];
    char t[200];
    int o;

    memset(f, 0, sizeof f);
    o = eth_header(f, ND_MAC, HOST_MAC, 0x0800u);
    o += ipv4_header(f + o, 6u, 20);
    f[o] = 0xC8u; f[o + 1] = 0x22u;  /* 51234 */
    f[o + 3] = 23u;                  /* telnet */
    f[o + 12] = 0x50u;               /* data offset 5 words */
    f[o + 13] = 0x02u;               /* SYN */
    eth_decode_summary(f, 14 + 40, t, sizeof t);
    CHECK(strcmp(t, "TCP 192.168.210.1:51234 > 192.168.210.40:23 [S] len 0") == 0);

    /* PSH+ACK with 5 data bytes: total length grows by 5 */
    f[14 + 3] = 45u;
    f[o + 13] = 0x18u;
    eth_decode_summary(f, (int)sizeof f, t, sizeof t);
    CHECK(strcmp(t, "TCP 192.168.210.1:51234 > 192.168.210.40:23 [P.] len 5") == 0);
}

ETH_TEST(Port, Decode_Udp)
{
    uint8_t f[14 + 20 + 8 + 4];
    char t[160];
    int o;

    memset(f, 0, sizeof f);
    o = eth_header(f, ND_MAC, HOST_MAC, 0x0800u);
    o += ipv4_header(f + o, 17u, 12);
    f[o] = 0x00u; f[o + 1] = 53u;    /* 53 */
    f[o + 2] = 0x04u; f[o + 3] = 0x00u; /* 1024 */
    f[o + 5] = 12u;
    eth_decode_summary(f, (int)sizeof f, t, sizeof t);
    CHECK(strcmp(t, "UDP 192.168.210.1:53 > 192.168.210.40:1024 len 12") == 0);
}

ETH_TEST(Port, Decode_Ieee8023Llc)
{
    uint8_t f[64];
    char t[160];

    memset(f, 0, sizeof f);
    (void)eth_header(f, ND_MAC, HOST_MAC, 0x000Eu); /* a length, not an EtherType */
    f[14] = 0xA8u;
    f[15] = 0xA8u;
    f[16] = 0x03u;
    eth_decode_summary(f, (int)sizeof f, t, sizeof t);
    CHECK(strcmp(t, "802.3 len 14 LLC dsap A8 ssap A8 ctl 03") == 0);
}

ETH_TEST(Port, Decode_OtherAndShort)
{
    uint8_t f[60];
    char t[160];

    memset(f, 0, sizeof f);
    (void)eth_header(f, ND_MAC, HOST_MAC, 0x86DDu);
    eth_decode_summary(f, (int)sizeof f, t, sizeof t);
    CHECK(strcmp(t, "type 86DD") == 0);
    eth_decode_summary(f, 10, t, sizeof t);
    CHECK(strcmp(t, "short frame (10 bytes)") == 0);
    (void)eth_header(f, ND_MAC, HOST_MAC, 0x0806u);
    eth_decode_summary(f, 20, t, sizeof t);
    CHECK(strcmp(t, "ARP short") == 0);
}
