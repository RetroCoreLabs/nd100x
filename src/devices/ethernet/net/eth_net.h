/*
 * eth_net.h - Host network backends for the Ethernet II card (none, UDP, TCP).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Port of RetroCore Emulated.HW/Common/Network: NullEthernetBackend,
 * UdpEthernetBackend, TcpEthernetBackend and EthernetBackendFactory.FromSpec.
 * The wire formats are the RetroCore ones, so an nd100x card and a RetroCore
 * card on the same UDP group or TCP link talk to each other:
 *   UDP: one Ethernet frame (no FCS) per datagram, multicast group default
 *        239.3.9.4, port default 3094, multicast loopback on.
 *   TCP: handshake 'R','E','T','H',1 each way, then per frame a big-endian
 *        u16 length and the frame bytes (no FCS); a length of 0 or above
 *        2048 ends the link.
 *
 * Spec strings (--eth0=SPEC / [controller.eth.N] net = SPEC):
 *   none                          no network; frames are dropped
 *   udp | udp:mcast               239.3.9.4:3094
 *   udp:<port> | udp:<group> | udp:<group>:<port> | udp:mcast:<group>:<port>
 *   udp-mcast:...                 same as udp:...
 *   listen | tcp-listen           listen on 3094
 *   listen:<port> | tcp-listen:<port>   (0 = a port the OS picks)
 *   tcp:<host> | tcp:<host>:<port> | <host>:<port>   connect out
 *   tap:<ifname>                  Linux TAP interface (not a RetroCore form). The
 *                                 interface must exist and belong to the user running
 *                                 nd100x, so no root is needed at run time:
 *                                   sudo ip tuntap add dev nd0 mode tap user $USER
 *                                   sudo ip addr add 192.168.210.1/24 dev nd0
 *                                   sudo ip link set nd0 up
 *                                 The host then reaches the guest directly (telnet).
 *   gateway | gateway:<segment>   browser build only: frames go over the page's
 *                                 WebSocket to tools/nd100-gateway as message types
 *                                 0x30 (RX) / 0x31 (TX) / 0x32 (link) on that segment
 *                                 (default 0; docs/GATEWAY-PROTOCOL.md). A native
 *                                 nd100x joins the same segment with tcp:HOST:3094.
 *
 * Threads. Deviation from RetroCore, deliberate: RetroCore hands a received
 * frame to the card on the backend's thread. Here the backend's thread only
 * fills a locked ring (512 frames, as UdpEthernetBackend.ReceiveRingSize) and
 * the emulation thread takes frames out with eth_net_receive(), so the card
 * and its LANCE are only ever touched by the emulation thread. eth_net_send()
 * is called on the emulation thread.
 *
 * WASM: no socket backends; only "none" opens (the gateway backend is plan
 * todo 6.7).
 */

#ifndef ETH_NET_H
#define ETH_NET_H

#include <stdbool.h>
#include <stdint.h>

#define ETH_NET_DEFAULT_PORT 3094          /* cs: Emulated.HW/Common/Network/UdpEthernetBackend.cs:57 */
#define ETH_NET_DEFAULT_GROUP "239.3.9.4"  /* cs: Emulated.HW/Common/Network/UdpEthernetBackend.cs:78 */
#define ETH_NET_RX_RING_SIZE 512           /* cs: Emulated.HW/Common/Network/UdpEthernetBackend.cs:69 */
#define ETH_NET_UDP_MAX_FRAME 1600         /* cs: Emulated.HW/Common/Network/UdpEthernetBackend.cs:72 */
#define ETH_NET_TCP_MAX_FRAME 2048         /* cs: Emulated.HW/Common/Network/TcpEthernetBackend.cs:48 */
#define ETH_NET_MAX_FRAME ETH_NET_TCP_MAX_FRAME
#define ETH_NET_TCP_VERSION 1              /* cs: Emulated.HW/Common/Network/TcpEthernetBackend.cs:44 */

typedef enum
{
    ETH_NET_NONE = 0,
    ETH_NET_UDP,
    ETH_NET_TCP_CONNECT,
    ETH_NET_TCP_LISTEN,
    ETH_NET_TAP,
    ETH_NET_GATEWAY
} EthNetKind;

/* A parsed spec. */
typedef struct
{
    EthNetKind kind;
    char host[256];  /* UDP: group address; TCP connect: host name or address; TAP: ifname */
    int port;        /* UDP / TCP port; TCP listen 0 = the OS picks */
    char description[300];
} EthNetSpec;

/* Counters. Written by the backend's threads, read by anyone. */
typedef struct
{
    uint64_t frames_sent;
    uint64_t frames_received;      /* frames put in the receive ring */
    uint64_t frames_dropped_ring;  /* received while the ring was full */
    uint64_t send_failures;
    uint64_t receive_errors;
    uint64_t links_up;             /* TCP: times a link came up */
} EthNetStats;

typedef struct EthNet EthNet;

/**
 * @brief Parse a spec string (the forms are listed at the top of this header).
 * @param spec Spec text; leading and trailing blanks are ignored.
 * @param out  Receives the result; untouched on failure.
 * @return 0 on success, -1 when spec is NULL, empty or not a valid form.
 */
int eth_net_parse_spec(const char *spec, EthNetSpec *out);

/**
 * @brief Create a backend for a parsed spec. It is not started.
 * @param spec The spec; copied.
 * @return The backend, or NULL when spec is NULL or memory ran out.
 */
EthNet *eth_net_create(const EthNetSpec *spec);

/**
 * @brief Start the backend: open sockets and start its threads. A second call does nothing.
 * @param n The backend.
 * @return 0 on success (for TCP connect: the dial loop runs, the link may come up later),
 *         -1 when the socket could not be opened or the platform has no sockets.
 */
int eth_net_start(EthNet *n);

/**
 * @brief Stop the backend: end its threads and close its sockets. Safe to call twice.
 * @param n The backend; NULL is ignored.
 */
void eth_net_stop(EthNet *n);

/**
 * @brief Stop and free a backend.
 * @param n The backend; NULL is ignored.
 */
void eth_net_destroy(EthNet *n);

/**
 * @brief Send one frame (no FCS). Dropped when not active or length is out of range.
 * @param n      The backend.
 * @param data   Frame bytes.
 * @param length Frame length.
 */
void eth_net_send(EthNet *n, const uint8_t *data, int length);

/**
 * @brief Take the oldest received frame out of the receive ring. Never blocks.
 * @param n      The backend.
 * @param buffer Receives the frame.
 * @param size   Size of buffer; a longer frame is cut to size.
 * @return The frame length, or 0 when the ring is empty.
 */
int eth_net_receive(EthNet *n, uint8_t *buffer, int size);

/**
 * @brief Cheap check, without taking the ring lock, whether a frame is waiting.
 * @param n The backend.
 * @return true when eth_net_receive() would return a frame.
 */
bool eth_net_has_frame(const EthNet *n);

/**
 * @brief Whether the backend carries traffic: "none" and UDP once started, TCP while linked.
 * @param n The backend.
 * @return true when active.
 */
bool eth_net_is_active(const EthNet *n);

/**
 * @brief The backend's description, e.g. "udp:mcast:239.3.9.4:3094" or "tcp-listen:3094".
 * @param n The backend.
 * @return The description; "" for NULL.
 */
const char *eth_net_description(const EthNet *n);

/**
 * @brief The TCP port bound in listen mode (the OS-picked one when the spec said 0).
 * @param n The backend.
 * @return The port; 0 when not a started listener.
 */
int eth_net_local_port(const EthNet *n);

/**
 * @brief Copy the counters.
 * @param n   The backend.
 * @param out Receives the counters.
 */
void eth_net_get_stats(const EthNet *n, EthNetStats *out);

/* ---- browser gateway backend (called by the WASM exports in nd100wasm.c) ---- */

#define ETH_NET_GATEWAY_TX_RING 32 /* frames waiting for the page to send */

/**
 * @brief Take the oldest frame the card sent through the started gateway backend.
 * @param data    receives a pointer to the frame, valid until the next call
 * @param length  receives the frame length
 * @param segment receives the gateway segment
 * @return 1 when a frame was taken, 0 when none is waiting or no gateway backend runs
 */
int eth_net_gateway_poll_tx(const uint8_t **data, int *length, int *segment);

/**
 * @brief A frame arrived from the gateway (type 0x30); queue it for the card.
 * @param segment the segment it arrived on; frames for other segments are ignored
 * @param data    frame bytes
 * @param length  frame length
 * @return 0 when queued, -1 when not for this card or no gateway backend runs
 */
int eth_net_gateway_inject_rx(int segment, const uint8_t *data, int length);

/**
 * @brief Gateway link status (type 0x32): anything else on the segment or not.
 * @param segment the segment
 * @param present 1 = another member is on the segment
 */
void eth_net_gateway_set_link(int segment, int present);

#endif /* ETH_NET_H */
