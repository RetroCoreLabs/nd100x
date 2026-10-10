/*
 * eth_ipcsum.c - Repair IPv4 / TCP / UDP checksums on frames from a host network.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * See eth_ipcsum.h. Line-for-line port of IpChecksumRepair.RepairInPlace.
 */

#include "eth_ipcsum.h"

#include <stdbool.h>
#include <stddef.h>

#define ETHERTYPE_IPV4 0x0800u /* cs: Emulated.HW/Common/Network/IpChecksumRepair.cs EtherTypeIpv4 */
#define ETHERTYPE_VLAN 0x8100u /* cs: Emulated.HW/Common/Network/IpChecksumRepair.cs EtherTypeVlan */
#define PROTOCOL_TCP 6u        /* cs: Emulated.HW/Common/Network/IpChecksumRepair.cs ProtocolTcp */
#define PROTOCOL_UDP 17u       /* cs: Emulated.HW/Common/Network/IpChecksumRepair.cs ProtocolUdp */

/* Raw 16-bit one's-complement sum, big-endian, odd trailing byte padded with zero. */
static uint32_t sum16(const uint8_t *data, int length)
{
    uint32_t sum = 0u;
    int i = 0;

    while (i < (length - 1))
    {
        sum += ((uint32_t)data[i] << 8u) | (uint32_t)data[i + 1];
        i += 2;
    }
    if (i < length)
    {
        sum += (uint32_t)data[i] << 8u;
    }
    return sum;
}

/* Fold a 32-bit accumulator down to 16 bits. */
static uint16_t fold(uint32_t sum)
{
    while ((sum >> 16u) != 0u)
    {
        sum = (sum & 0xFFFFu) + (sum >> 16u);
    }
    return (uint16_t)sum;
}

/* TCP/UDP pseudo header: source and destination address, protocol, transport length. */
static uint32_t pseudo_header_sum(const uint8_t *ip, uint8_t protocol, int transport_length)
{
    uint32_t sum = 0u;

    sum += ((uint32_t)ip[12] << 8u) | ip[13];
    sum += ((uint32_t)ip[14] << 8u) | ip[15];
    sum += ((uint32_t)ip[16] << 8u) | ip[17];
    sum += ((uint32_t)ip[18] << 8u) | ip[19];
    sum += protocol;
    sum += (uint32_t)transport_length;
    return sum;
}

/* Transport (TCP/UDP) part of the repair; ip points at the IPv4 header. */
static unsigned repair_transport(uint8_t *ip, int ihl, int ip_total_length)
{
    const bool more_fragments = (ip[6] & 0x20u) != 0u;
    const int fragment_offset = (int)((((unsigned)ip[6] & 0x1Fu) << 8u) | ip[7]);
    const uint8_t protocol = ip[9];
    int payload_length;
    int minimum_header;
    int checksum_offset;
    uint8_t *payload;
    uint16_t saved;
    uint16_t computed;

    if (more_fragments || (fragment_offset != 0))
    {
        return ETH_IPCSUM_NONE;
    }
    if ((protocol != PROTOCOL_TCP) && (protocol != PROTOCOL_UDP))
    {
        return ETH_IPCSUM_NONE;
    }
    payload_length = ip_total_length - ihl;
    minimum_header = (protocol == PROTOCOL_TCP) ? 20 : 8;
    if (payload_length < minimum_header)
    {
        return ETH_IPCSUM_NONE;
    }
    payload = ip + ihl;
    checksum_offset = (protocol == PROTOCOL_TCP) ? 16 : 6;

    /* A UDP checksum of zero ("not computed") cannot be told apart from an
     * unfinished offloaded one, so it is computed as well (RetroCore does the same). */
    if (fold(pseudo_header_sum(ip, protocol, payload_length) + sum16(payload, payload_length)) == 0xFFFFu)
    {
        return ETH_IPCSUM_NONE;
    }
    saved = (uint16_t)(((unsigned)payload[checksum_offset] << 8u) | payload[checksum_offset + 1]);
    payload[checksum_offset] = 0u;
    payload[checksum_offset + 1] = 0u;
    computed = (uint16_t)~fold(pseudo_header_sum(ip, protocol, payload_length) +
                               sum16(payload, payload_length));
    /* RFC 768: a computed UDP checksum of zero is sent as all ones. */
    if ((computed == 0u) && (protocol == PROTOCOL_UDP))
    {
        computed = 0xFFFFu;
    }
    payload[checksum_offset] = (uint8_t)(computed >> 8u);
    payload[checksum_offset + 1] = (uint8_t)(computed & 0xFFu);
    if (computed == saved)
    {
        return ETH_IPCSUM_NONE;
    }
    return (protocol == PROTOCOL_TCP) ? ETH_IPCSUM_TCP : ETH_IPCSUM_UDP;
}

unsigned eth_ipcsum_repair(uint8_t *frame, int length)
{
    int ip_offset = 14;
    unsigned result = ETH_IPCSUM_NONE;
    uint16_t ether_type;
    uint8_t *ip;
    int ip_length;
    int ihl;
    int ip_total_length;

    if ((frame == NULL) || (length < 14))
    {
        return ETH_IPCSUM_NONE;
    }
    ether_type = (uint16_t)(((unsigned)frame[12] << 8u) | frame[13]);
    if (ether_type == ETHERTYPE_VLAN)
    {
        if (length < 18)
        {
            return ETH_IPCSUM_NONE;
        }
        ether_type = (uint16_t)(((unsigned)frame[16] << 8u) | frame[17]);
        ip_offset = 18;
    }
    if ((ether_type != ETHERTYPE_IPV4) || (length < (ip_offset + 20)))
    {
        return ETH_IPCSUM_NONE;
    }
    ip = frame + ip_offset;
    ip_length = length - ip_offset;

    if ((ip[0] >> 4u) != 4u)
    {
        return ETH_IPCSUM_NONE;
    }
    ihl = (int)(ip[0] & 0x0Fu) * 4;
    if ((ihl < 20) || (ip_length < ihl))
    {
        return ETH_IPCSUM_NONE;
    }
    /* Trust the IP total length over the frame length: Ethernet padding is not
     * part of the datagram. */
    ip_total_length = (int)(((unsigned)ip[2] << 8u) | ip[3]);
    if (ip_total_length < ihl)
    {
        return ETH_IPCSUM_NONE;
    }
    if (ip_total_length > ip_length)
    {
        /* Truncated capture: only the header, which is all present, can be repaired. */
        ip_total_length = ihl;
    }

    /* IPv4 header. A correct header sums to 0xFFFF including its checksum field. */
    if (fold(sum16(ip, ihl)) != 0xFFFFu)
    {
        uint16_t hdr;
        ip[10] = 0u;
        ip[11] = 0u;
        hdr = (uint16_t)~fold(sum16(ip, ihl));
        ip[10] = (uint8_t)(hdr >> 8u);
        ip[11] = (uint8_t)(hdr & 0xFFu);
        result |= ETH_IPCSUM_IP_HEADER;
    }
    return result | repair_transport(ip, ihl, ip_total_length);
}
