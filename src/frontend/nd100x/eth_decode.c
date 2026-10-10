/*
 * eth_decode.c - One-line summaries of Ethernet frames for the F12 packet view.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * See eth_decode.h.
 */

#include "eth_decode.h"

#include <stdio.h>
#include <string.h>

static unsigned be16(const uint8_t *p)
{
    return ((unsigned)p[0] << 8u) | p[1];
}

void eth_decode_mac_name(const uint8_t *mac, const uint8_t *own, char *out, size_t size)
{
    static const uint8_t bcast[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};

    if ((own != NULL) && (memcmp(mac, own, 6u) == 0))
    {
        (void)snprintf(out, size, "nd");
    }
    else if (memcmp(mac, bcast, 6u) == 0)
    {
        (void)snprintf(out, size, "bcast");
    }
    else if ((mac[0] & 0x01u) != 0u)
    {
        /* group bit set (IEEE 802 individual/group bit, first octet bit 0) */
        (void)snprintf(out, size, "mcast %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
                       mac[3], mac[4], mac[5]);
    }
    else
    {
        (void)snprintf(out, size, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3],
                       mac[4], mac[5]);
    }
}

/* ARP for IPv4 over Ethernet (RFC 826): htype 1, ptype 0x0800, hlen 6, plen 4. */
static void decode_arp(const uint8_t *a, int len, char *out, size_t size)
{
    unsigned op;

    if (len < 28)
    {
        (void)snprintf(out, size, "ARP short");
        return;
    }
    if ((be16(a) != 1u) || (be16(a + 2) != 0x0800u) || (a[4] != 6u) || (a[5] != 4u))
    {
        (void)snprintf(out, size, "ARP htype %u ptype %04X", be16(a), be16(a + 2));
        return;
    }
    op = be16(a + 6);
    if (op == 1u)
    {
        (void)snprintf(out, size, "ARP who-has %u.%u.%u.%u tell %u.%u.%u.%u", a[24], a[25], a[26],
                       a[27], a[14], a[15], a[16], a[17]);
    }
    else if (op == 2u)
    {
        (void)snprintf(out, size, "ARP %u.%u.%u.%u is-at %02x:%02x:%02x:%02x:%02x:%02x", a[14], a[15],
                       a[16], a[17], a[8], a[9], a[10], a[11], a[12], a[13]);
    }
    else
    {
        (void)snprintf(out, size, "ARP op %u", op);
    }
}

static void decode_ipv4(const uint8_t *ip, int len, char *out, size_t size)
{
    int ihl;
    const uint8_t *p;
    int plen;
    char addr[40];
    unsigned frag;

    if ((len < 20) || ((ip[0] >> 4u) != 4u))
    {
        (void)snprintf(out, size, "IPv4 short or not version 4");
        return;
    }
    ihl = (int)(ip[0] & 0x0Fu) * 4;
    if ((ihl < 20) || (len < ihl))
    {
        (void)snprintf(out, size, "IPv4 bad header length");
        return;
    }
    (void)snprintf(addr, sizeof addr, "%u.%u.%u.%u > %u.%u.%u.%u", ip[12], ip[13], ip[14], ip[15],
                   ip[16], ip[17], ip[18], ip[19]);
    frag = be16(ip + 6) & 0x1FFFu;
    p = ip + ihl;
    plen = len - ihl;
    if (frag != 0u)
    {
        (void)snprintf(out, size, "IPv4 %s fragment offset %u", addr, frag * 8u);
        return;
    }
    switch (ip[9])
    {
    case 1u: /* ICMP */
        if (plen < 8)
        {
            (void)snprintf(out, size, "ICMP %s short", addr);
        }
        else if ((p[0] == 8u) || (p[0] == 0u))
        {
            (void)snprintf(out, size, "ICMP echo %s %s id %u seq %u", (p[0] == 8u) ? "request" : "reply",
                           addr, be16(p + 4), be16(p + 6));
        }
        else
        {
            (void)snprintf(out, size, "ICMP type %u code %u %s", p[0], p[1], addr);
        }
        break;
    case 6u: /* TCP */
        if (plen < 20)
        {
            (void)snprintf(out, size, "TCP %s short", addr);
        }
        else
        {
            /* tcpdump order and letters: SYN FIN RST PSH, ACK as '.', URG */
            static const uint8_t bit[6] = {0x02u, 0x01u, 0x04u, 0x08u, 0x10u, 0x20u};
            static const char letter[6] = {'S', 'F', 'R', 'P', '.', 'U'};
            char flags[8];
            int k = 0;
            for (int b = 0; b < 6; b++)
            {
                if ((p[13] & bit[b]) != 0u)
                {
                    flags[k++] = letter[b];
                }
            }
            flags[k] = '\0';
            (void)snprintf(out, size, "TCP %u.%u.%u.%u:%u > %u.%u.%u.%u:%u [%s] len %d", ip[12], ip[13],
                           ip[14], ip[15], be16(p), ip[16], ip[17], ip[18], ip[19], be16(p + 2), flags,
                           (int)be16(ip + 2) - ihl - (int)((p[12] >> 4u) * 4u));
        }
        break;
    case 17u: /* UDP */
        if (plen < 8)
        {
            (void)snprintf(out, size, "UDP %s short", addr);
        }
        else
        {
            (void)snprintf(out, size, "UDP %u.%u.%u.%u:%u > %u.%u.%u.%u:%u len %u", ip[12], ip[13],
                           ip[14], ip[15], be16(p), ip[16], ip[17], ip[18], ip[19], be16(p + 2),
                           be16(p + 4));
        }
        break;
    default:
        (void)snprintf(out, size, "IPv4 proto %u %s", ip[9], addr);
        break;
    }
}

void eth_decode_summary(const uint8_t *frame, int length, char *out, size_t size)
{
    unsigned type;

    if ((frame == NULL) || (length < 14))
    {
        (void)snprintf(out, size, "short frame (%d bytes)", length);
        return;
    }
    type = be16(frame + 12);
    if (type <= 1500u)
    {
        /* IEEE 802.3: the field is a length; an 802.2 LLC header follows */
        if (length < 17)
        {
            (void)snprintf(out, size, "802.3 len %u short", type);
        }
        else
        {
            (void)snprintf(out, size, "802.3 len %u LLC dsap %02X ssap %02X ctl %02X", type, frame[14],
                           frame[15], frame[16]);
        }
        return;
    }
    switch (type)
    {
    case 0x0806u:
        decode_arp(frame + 14, length - 14, out, size);
        break;
    case 0x0800u:
        decode_ipv4(frame + 14, length - 14, out, size);
        break;
    default:
        (void)snprintf(out, size, "type %04X", type);
        break;
    }
}
