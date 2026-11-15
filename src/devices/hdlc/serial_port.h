/*
 * serial_port.h - Raw host serial port (POSIX termios / Windows COM) for the HDLC modem.
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
 * The port is opened raw: 8 data bits, no parity, 1 stop bit, no flow
 * control, no character translation. Bytes pass through unchanged in both
 * directions. DTR and RTS are asserted after open, because the USB HDLC
 * gateways (HDLCGW, RC422Dev) read DTR as "a host is present".
 *
 * All platform code for the serial transport lives in serial_port.c. The
 * browser build has no serial port; there this module is not compiled in
 * (see HDLC_SERIAL_AVAILABLE in hdlc_link_spec.h).
 */

#ifndef SERIAL_PORT_H
#define SERIAL_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "hdlc_link_spec.h"

#if HDLC_SERIAL_AVAILABLE

/* How long one serial_port_read() waits for the first byte, in ms. Short, so
 * the worker thread that reads also gets to send queued bytes quickly. */
#define SERIAL_PORT_READ_TIMEOUT_MS 5

/* How long one serial_port_write() may wait for the port to take bytes. */
#define SERIAL_PORT_WRITE_TIMEOUT_MS 1000

/* One open host serial port. Owned by the modem worker thread. */
typedef struct
{
#ifdef _WIN32
    void *handle; /* HANDLE; NULL when closed */
#else
    int fd; /* -1 when closed */
#endif
} SerialPort;

/**
 * @brief Mark a port as closed. Call once before the first serial_port_open().
 * @param port Port to initialise.
 * @return void.
 */
void serial_port_init(SerialPort *port);

/**
 * @brief Open a host serial device raw at 8N1 with no flow control and
 *        assert DTR and RTS.
 * @param port Port to open; must be closed.
 * @param device Device name: "/dev/ttyACM0" on POSIX, "COM18" or
 *        "\\.\COM18" on Windows.
 * @param baud Line speed; must pass hdlc_link_spec_is_valid_baud().
 * @return 0 on success, -1 on failure (the reason is logged).
 */
int serial_port_open(SerialPort *port, const char *device, int baud);

/**
 * @brief Close the port if it is open. Safe to call on a closed port.
 * @param port Port to close.
 * @return void.
 */
void serial_port_close(SerialPort *port);

/**
 * @brief Is the port open?
 * @param port Port to test.
 * @return true if open.
 */
bool serial_port_is_open(const SerialPort *port);

/**
 * @brief Read the bytes that are waiting, waiting at most
 *        SERIAL_PORT_READ_TIMEOUT_MS for the first one.
 * @param port Open port.
 * @param buf Destination buffer.
 * @param max Size of buf in bytes.
 * @return Number of bytes read (0 = nothing arrived), or -1 when the port is
 *         gone (unplugged, hung up, I/O error). The reason is logged.
 */
int serial_port_read(SerialPort *port, uint8_t *buf, int max);

/**
 * @brief Write all bytes, waiting at most SERIAL_PORT_WRITE_TIMEOUT_MS for
 *        the port to take them.
 * @param port Open port.
 * @param buf Bytes to write.
 * @param len Number of bytes.
 * @return 0 when every byte was written, -1 when the port is gone or did
 *         not take the bytes in time. The reason is logged.
 */
int serial_port_write(SerialPort *port, const uint8_t *buf, int len);

#endif /* HDLC_SERIAL_AVAILABLE */

#endif /* SERIAL_PORT_H */
