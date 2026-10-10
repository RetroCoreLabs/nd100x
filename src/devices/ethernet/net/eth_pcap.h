/*
 * eth_pcap.h - Ethernet frames through a host adapter with Npcap (Windows).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * The backend behind --eth0=pcap:ADAPTER / [controller.eth.N] net = pcap:ADAPTER.
 * On Windows it loads Npcap's wpcap.dll at run time (System32\Npcap\wpcap.dll,
 * then wpcap.dll on the search path), so nd100x starts on a PC without Npcap and
 * only this backend reports that it is missing. On every other platform the
 * functions refuse with a message (Linux has tap:).
 *
 * ADAPTER is matched against, in this order:
 *   1. a full Npcap device name, \Device\NPF_{GUID}
 *   2. a bare GUID, {GUID}
 *   3. the Windows connection name (what Get-NetAdapter calls Name and
 *      ncpa.cpl shows, e.g. "ND-Loopback"), case-insensitive
 *   4. the adapter description (e.g. "Microsoft KM-TEST Loopback Adapter"),
 *      case-insensitive
 * "list" prints every adapter and opens nothing.
 */

#ifndef ETH_PCAP_H
#define ETH_PCAP_H

#include <stdint.h>

typedef struct EthPcap EthPcap;

/**
 * @brief Open the adapter named by @p adapter for sending and receiving.
 * @param adapter  see the matching rules above; "list" lists adapters and fails
 * @return the open adapter, or NULL (the reason has been logged, with the list of
 *         adapters when the name matched none)
 */
EthPcap *eth_pcap_open(const char *adapter);

/**
 * @brief Wait up to about 100 ms for one frame.
 * @param p     open adapter
 * @param buf   receives the frame
 * @param size  size of buf; a longer frame is cut to size
 * @return frame length, 0 when none arrived in time, -1 on an adapter error
 */
int eth_pcap_receive(EthPcap *p, uint8_t *buf, int size);

/**
 * @brief Send one frame.
 * @param p      open adapter
 * @param data   frame from the destination MAC on
 * @param length bytes
 * @return 0 on success, -1 on failure
 */
int eth_pcap_send(EthPcap *p, const uint8_t *data, int length);

/**
 * @brief Close the adapter. NULL is ignored.
 * @param p  adapter from eth_pcap_open
 */
void eth_pcap_close(EthPcap *p);

/**
 * @brief The Npcap device name in use (\Device\NPF_{GUID}), for messages.
 * @param p  open adapter
 * @return the name, or "" for NULL
 */
const char *eth_pcap_device(const EthPcap *p);

#endif /* ETH_PCAP_H */
