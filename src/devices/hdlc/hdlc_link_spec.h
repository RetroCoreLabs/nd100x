/*
 * hdlc_link_spec.h - Parse the --hdlc=... link text (TCP server, TCP client or serial port).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2026 Ronny Hansen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Accepted forms (N = HDLC thumbwheel 1-4):
 *   N:PORT                     TCP server, listen on PORT
 *   N:HOST:PORT                TCP client, connect to HOST:PORT
 *   N:serial:DEVICE[:BAUD]     host serial port, e.g. 1:serial:/dev/ttyACM0
 *                              or 1:serial:COM18:115200
 * "serial" is a keyword: a TCP host named "serial" cannot be used here.
 * This file holds no platform code, so the unit tests link it on its own.
 */

#ifndef HDLC_LINK_SPEC_H
#define HDLC_LINK_SPEC_H

#include <stdbool.h>
#include <stddef.h>

/* The serial transport needs a host serial port and a thread; the browser
 * build has neither. Config code tests this to reject mode = serial there. */
#if defined(__EMSCRIPTEN__)
#define HDLC_SERIAL_AVAILABLE 0
#else
#define HDLC_SERIAL_AVAILABLE 1
#endif

/* Line speed used when none is given. */
#define HDLC_SERIAL_DEFAULT_BAUD 115200

/* Longest host name or serial device name, including the terminating 0. */
#define HDLC_LINK_TEXT_LEN 256

typedef enum
{
    HDLC_LINK_SERVER,
    HDLC_LINK_CLIENT,
    HDLC_LINK_SERIAL
} HdlcLinkMode;

typedef struct
{
    int device_num;                    /* thumbwheel 1-4 */
    HdlcLinkMode mode;
    char address[HDLC_LINK_TEXT_LEN];  /* client host or serial device; "" for server */
    int port;                          /* TCP port (server, client); 0 for serial */
    int baud;                          /* serial line speed; 0 for TCP */
} HdlcLinkSpec;

/**
 * @brief Parse one --hdlc link text into a spec.
 * @param text The text after "--hdlc=".
 * @param out Filled on success; left in an unspecified state on failure.
 * @param err Buffer for a one-line reason on failure; may be NULL.
 * @param errlen Size of err.
 * @return 0 on success, -1 on a malformed text (reason in err).
 */
int hdlc_link_spec_parse(const char *text, HdlcLinkSpec *out, char *err, size_t errlen);

/**
 * @brief Is this a line speed the serial transport can set on every host?
 *        The list is the standard rates 1200 .. 921600.
 * @param baud Line speed in bits per second.
 * @return true if supported.
 */
bool hdlc_link_spec_is_valid_baud(int baud);

#endif /* HDLC_LINK_SPEC_H */
