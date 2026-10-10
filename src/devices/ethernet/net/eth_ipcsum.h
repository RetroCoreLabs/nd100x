/*
 * eth_ipcsum.h - Repair IPv4 / TCP / UDP checksums on frames from a host network.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Port of RetroCore Emulated.HW/Common/Network/IpChecksumRepair.cs.
 * A host NIC with TX checksum offload fills in the IPv4 header, TCP and UDP
 * checksums in hardware, after a capture tap has seen the frame, so frames the
 * capturing host sends arrive unfinished (RetroCore measured IPv4 checksum
 * 0000 from the host on COSMOS TCP/IP D02, and SINTRAN logged
 * "TCPP0-TCP Error in checksum"). A checksum is recomputed ONLY when the one
 * present does not verify; frames that verify are left byte for byte.
 * Anything that is not an unfragmented IPv4 TCP/UDP datagram is left alone.
 * One level of 802.1Q tagging is understood. Works in place, no allocation.
 */

#ifndef ETH_IPCSUM_H
#define ETH_IPCSUM_H

#include <stdint.h>

/* Bits in the eth_ipcsum_repair() result. */
#define ETH_IPCSUM_NONE 0x00u      /* cs: Emulated.HW/Common/Network/IpChecksumRepair.cs Repaired.None */
#define ETH_IPCSUM_IP_HEADER 0x01u /* cs: Emulated.HW/Common/Network/IpChecksumRepair.cs Repaired.IpHeader */
#define ETH_IPCSUM_TCP 0x02u       /* cs: Emulated.HW/Common/Network/IpChecksumRepair.cs Repaired.Tcp */
#define ETH_IPCSUM_UDP 0x04u       /* cs: Emulated.HW/Common/Network/IpChecksumRepair.cs Repaired.Udp */

/**
 * @brief Verify and, where wrong, recompute the IPv4 header and TCP/UDP checksums of one
 *        Ethernet frame, in place.
 * @param frame  The whole frame from the destination MAC on. NULL is ignored.
 * @param length Number of valid bytes in frame.
 * @return ETH_IPCSUM_* bits naming what was rewritten; ETH_IPCSUM_NONE when nothing was.
 */
unsigned eth_ipcsum_repair(uint8_t *frame, int length);

#endif /* ETH_IPCSUM_H */
