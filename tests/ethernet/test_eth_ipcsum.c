/*
 * test_eth_ipcsum.c - Port of RetroCore Emulated.Tests.Chips/IpChecksumRepairTests.cs.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Checksum repair for frames captured from a host NIC that does TX checksum
 * offload. Helpers BuildFrame / Ones16 / IpHeaderVerifies / TransportVerifies
 * are line-for-line ports of the C# helpers.
 */

#include "eth_test.h"

#include <string.h>

#include "net/eth_ipcsum.h"

#define MAX_FRAME 128

/* BuildFrame: Ethernet + IPv4 + TCP/UDP with zeroed checksums. Returns the length. */
static int build_frame(uint8_t *f, uint8_t protocol, int payload_bytes, bool with_vlan)
{
    const int eth_len = with_vlan ? 18 : 14;
    const int transport_header = (protocol == 6u) ? 20 : 8;
    const int ip_total = 20 + transport_header + payload_bytes;
    const int ip = eth_len;
    const int tp = ip + 20;

    memset(f, 0, MAX_FRAME);
    for (int i = 0; i < 6; i++)
    {
        f[i] = (uint8_t)(0x08 + i);
        f[6 + i] = (uint8_t)(0x40 + i);
    }
    if (with_vlan)
    {
        f[12] = 0x81u;
        f[13] = 0x00u;
        f[14] = 0x00u;
        f[15] = 0x64u;
        f[16] = 0x08u;
        f[17] = 0x00u;
    }
    else
    {
        f[12] = 0x08u;
        f[13] = 0x00u;
    }
    f[ip + 0] = 0x45u;
    f[ip + 2] = (uint8_t)(ip_total >> 8);
    f[ip + 3] = (uint8_t)(ip_total & 0xFF);
    f[ip + 4] = 0x32u;
    f[ip + 5] = 0x0Cu;
    f[ip + 6] = 0x40u;
    f[ip + 8] = 0x80u;
    f[ip + 9] = protocol;
    f[ip + 12] = 192u;
    f[ip + 13] = 168u;
    f[ip + 14] = 1u;
    f[ip + 15] = 180u;
    f[ip + 16] = 192u;
    f[ip + 17] = 168u;
    f[ip + 18] = 1u;
    f[ip + 19] = 40u;
    f[tp + 0] = 0x1Fu;
    f[tp + 1] = 0x90u;
    f[tp + 2] = 0x00u;
    f[tp + 3] = 0x17u;
    if (protocol == 6u)
    {
        f[tp + 12] = 0x50u;
        f[tp + 13] = 0x10u;
    }
    else
    {
        const int udp_len = 8 + payload_bytes;
        f[tp + 4] = (uint8_t)(udp_len >> 8);
        f[tp + 5] = (uint8_t)(udp_len & 0xFF);
    }
    for (int i = 0; i < payload_bytes; i++)
    {
        f[tp + transport_header + i] = (uint8_t)(i + 1);
    }
    return eth_len + ip_total;
}

static uint16_t ones16(const uint8_t *d, int len)
{
    uint32_t sum = 0u;
    int i = 0;
    while (i < (len - 1))
    {
        sum += ((uint32_t)d[i] << 8u) | d[i + 1];
        i += 2;
    }
    if (i < len)
    {
        sum += (uint32_t)d[i] << 8u;
    }
    while ((sum >> 16u) != 0u)
    {
        sum = (sum & 0xFFFFu) + (sum >> 16u);
    }
    return (uint16_t)sum;
}

static bool ip_header_verifies(const uint8_t *frame, int ip_offset)
{
    const int ihl = (frame[ip_offset] & 0x0F) * 4;
    return ones16(frame + ip_offset, ihl) == 0xFFFFu;
}

static bool transport_verifies(const uint8_t *frame, int ip_offset)
{
    const int ihl = (frame[ip_offset] & 0x0F) * 4;
    const int ip_total = (frame[ip_offset + 2] << 8) | frame[ip_offset + 3];
    const uint8_t protocol = frame[ip_offset + 9];
    const int tp = ip_offset + ihl;
    const int len = ip_total - ihl;
    uint32_t sum = 0u;
    int i = 0;

    for (int k = 12; k < 20; k += 2)
    {
        sum += ((uint32_t)frame[ip_offset + k] << 8u) | frame[ip_offset + k + 1];
    }
    sum += protocol;
    sum += (uint32_t)len;
    while (i < (len - 1))
    {
        sum += ((uint32_t)frame[tp + i] << 8u) | frame[tp + i + 1];
        i += 2;
    }
    if (i < len)
    {
        sum += (uint32_t)frame[tp + i] << 8u;
    }
    while ((sum >> 16u) != 0u)
    {
        sum = (sum & 0xFFFFu) + (sum >> 16u);
    }
    return (uint16_t)sum == 0xFFFFu;
}

ETH_TEST(IpChecksumRepairTests, RepairsAnUnfilledIpHeaderChecksum)
{
    uint8_t f[MAX_FRAME];
    const int n = build_frame(f, 6u, 4, false);
    CHECK(!ip_header_verifies(f, 14));
    CHECK((eth_ipcsum_repair(f, n) & ETH_IPCSUM_IP_HEADER) != 0u);
    CHECK(ip_header_verifies(f, 14));
}

ETH_TEST(IpChecksumRepairTests, RepairsTcpAndUdpChecksums)
{
    uint8_t tcp[MAX_FRAME];
    uint8_t udp[MAX_FRAME];
    const int nt = build_frame(tcp, 6u, 10, false);
    const int nu = build_frame(udp, 17u, 10, false);
    CHECK((eth_ipcsum_repair(tcp, nt) & ETH_IPCSUM_TCP) != 0u);
    CHECK(transport_verifies(tcp, 14));
    CHECK((eth_ipcsum_repair(udp, nu) & ETH_IPCSUM_UDP) != 0u);
    CHECK(transport_verifies(udp, 14));
}

ETH_TEST(IpChecksumRepairTests, LeavesAnAlreadyCorrectFrameByteForByteUntouched)
{
    uint8_t f[MAX_FRAME];
    uint8_t before[MAX_FRAME];
    const int n = build_frame(f, 6u, 8, false);
    (void)eth_ipcsum_repair(f, n);
    memcpy(before, f, sizeof before);
    CHECK_EQ(eth_ipcsum_repair(f, n), ETH_IPCSUM_NONE);
    CHECK(memcmp(f, before, sizeof before) == 0);
}

ETH_TEST(IpChecksumRepairTests, HandlesAPartialPseudoHeaderSumNotJustZero)
{
    uint8_t f[MAX_FRAME];
    const int n = build_frame(f, 6u, 6, false);
    f[14 + 20 + 16] = 0x12u;
    f[14 + 20 + 17] = 0x34u;
    CHECK((eth_ipcsum_repair(f, n) & ETH_IPCSUM_TCP) != 0u);
    CHECK(transport_verifies(f, 14));
}

ETH_TEST(IpChecksumRepairTests, HandlesAnOddLengthPayload)
{
    uint8_t f[MAX_FRAME];
    const int n = build_frame(f, 17u, 7, false);
    (void)eth_ipcsum_repair(f, n);
    CHECK(transport_verifies(f, 14));
}

ETH_TEST(IpChecksumRepairTests, HandlesVlanTaggedFrames)
{
    uint8_t f[MAX_FRAME];
    const int n = build_frame(f, 6u, 4, true);
    CHECK((eth_ipcsum_repair(f, n) & ETH_IPCSUM_IP_HEADER) != 0u);
    CHECK(ip_header_verifies(f, 18));
    CHECK(transport_verifies(f, 18));
}

ETH_TEST(IpChecksumRepairTests, IgnoresNonIpv4AndShortFrames)
{
    uint8_t arp[60];
    uint8_t before[60];
    uint8_t runt[8];
    memset(arp, 0, sizeof arp);
    arp[12] = 0x08u;
    arp[13] = 0x06u;
    memcpy(before, arp, sizeof before);
    CHECK_EQ(eth_ipcsum_repair(arp, (int)sizeof arp), ETH_IPCSUM_NONE);
    CHECK(memcmp(arp, before, sizeof arp) == 0);
    memset(runt, 0, sizeof runt);
    CHECK_EQ(eth_ipcsum_repair(runt, (int)sizeof runt), ETH_IPCSUM_NONE);
}

ETH_TEST(IpChecksumRepairTests, LeavesFragmentsAlone)
{
    uint8_t f[MAX_FRAME];
    const int n = build_frame(f, 6u, 8, false);
    unsigned what;
    f[14 + 6] = 0x20u;
    f[14 + 7] = 0x00u;
    what = eth_ipcsum_repair(f, n);
    CHECK((what & ETH_IPCSUM_IP_HEADER) != 0u);
    CHECK((what & ETH_IPCSUM_TCP) == 0u);
    CHECK(ip_header_verifies(f, 14));
}

ETH_TEST(IpChecksumRepairTests, IgnoresEthernetPaddingBeyondTheIpTotalLength)
{
    uint8_t small[MAX_FRAME];
    uint8_t padded[60];
    const int n = build_frame(small, 6u, 0, false);
    memcpy(padded, small, (size_t)n);
    for (int i = n; i < (int)sizeof padded; i++)
    {
        padded[i] = 0xAAu;
    }
    (void)eth_ipcsum_repair(padded, (int)sizeof padded);
    CHECK(ip_header_verifies(padded, 14));
    CHECK(transport_verifies(padded, 14));
}

ETH_TEST(IpChecksumRepairTests, ReproducesTheMeasuredNdFrame)
{
    uint8_t f[] = {
        0x6E, 0x63, 0xF8, 0x3F, 0x1D, 0xD3, 0x04, 0x42, 0x1A, 0xEC, 0xD0, 0xF9, 0x08, 0x00,
        0x45, 0x00, 0x00, 0x28, 0x32, 0x0C, 0x40, 0x00, 0x80, 0x06, 0x00, 0x00,
        0xC0, 0xA8, 0x01, 0xB4, 0xA0, 0x4F, 0x68, 0x0A,
        0x5E, 0xCF, 0x01, 0xBB, 0xBD, 0xFE, 0xBE, 0x11, 0xD5, 0xAF, 0x07, 0xAC,
        0x50, 0x10, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00,
    };
    CHECK(!ip_header_verifies(f, 14));
    CHECK((eth_ipcsum_repair(f, (int)sizeof f) & ETH_IPCSUM_IP_HEADER) != 0u);
    CHECK(ip_header_verifies(f, 14));
    CHECK(transport_verifies(f, 14));
}
