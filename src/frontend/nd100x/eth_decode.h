/*
 * eth_decode.h - One-line summaries of Ethernet frames for the F12 packet view.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Decodes only what the frame bytes say, by the published layouts:
 * Ethernet II / IEEE 802.3 header, ARP (RFC 826), IPv4 (RFC 791), ICMP
 * (RFC 792), TCP (RFC 793), UDP (RFC 768), 802.2 LLC. Anything else is shown
 * as its EtherType or 802.3 length in hex. A frame too short for the layout
 * it claims is shown as "short".
 */

#ifndef ETH_DECODE_H
#define ETH_DECODE_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Name a MAC address for the list: "nd" for the card's own, "bcast", "mcast",
 *        else the address as aa:bb:cc:dd:ee:ff.
 * @param mac    the address (6 bytes)
 * @param own    the card's own address (6 bytes); NULL = no "nd" naming
 * @param out    receives the text
 * @param size   size of out
 */
void eth_decode_mac_name(const uint8_t *mac, const uint8_t *own, char *out, size_t size);

/**
 * @brief Decode the protocol part of one frame into a short text ("what" column).
 * @param frame  frame bytes from the destination MAC on
 * @param length bytes available
 * @param out    receives the text
 * @param size   size of out
 */
void eth_decode_summary(const uint8_t *frame, int length, char *out, size_t size);

#endif /* ETH_DECODE_H */
