/*
 * hdlc_link_spec.c - Parse the --hdlc=... link text (TCP server, TCP client or serial port).
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
 */

#include "hdlc_link_spec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The serial keyword, followed by its colon. */
#define SERIAL_PREFIX "serial:"

static int spec_error(char *err, size_t errlen, const char *msg)
{
    if (err && errlen > 0)
    {
        (void)snprintf(err, errlen, "%s", msg);
    }
    return -1;
}

/* Parse a whole decimal number in [lo, hi]. false on anything else. */
static bool parse_int_range(const char *s, int lo, int hi, int *out)
{
    char *end = NULL;
    long v;

    if (!s || *s == '\0')
    {
        return false;
    }
    v = strtol(s, &end, 10);
    if (*end != '\0' || v < lo || v > hi)
    {
        return false;
    }
    *out = (int)v;
    return true;
}

bool hdlc_link_spec_is_valid_baud(int baud)
{
    // clang-format off
    static const int rates[] = { 1200, 2400, 4800, 9600, 19200, 38400, 57600,
                                 115200, 230400, 460800, 921600 };
    // clang-format on
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++)
    {
        if (rates[i] == baud)
        {
            return true;
        }
    }
    return false;
}

/* "DEVICE[:BAUD]" after the serial keyword. */
static int parse_serial(const char *rest, HdlcLinkSpec *out, char *err, size_t errlen)
{
    const char *colon = strchr(rest, ':');
    size_t dev_len = colon ? (size_t)(colon - rest) : strlen(rest);

    if (!HDLC_SERIAL_AVAILABLE)
    {
        return spec_error(err, errlen, "HDLC serial mode is not available in this build");
    }
    if (dev_len == 0)
    {
        return spec_error(err, errlen, "HDLC serial mode needs a device: N:serial:DEVICE[:BAUD]");
    }
    if (dev_len >= sizeof(out->address))
    {
        return spec_error(err, errlen, "HDLC serial device name is too long");
    }
    memcpy(out->address, rest, dev_len);
    out->address[dev_len] = '\0';

    out->baud = HDLC_SERIAL_DEFAULT_BAUD;
    if (colon)
    {
        int baud = 0;
        if (!parse_int_range(colon + 1, 1, 100000000, &baud) || !hdlc_link_spec_is_valid_baud(baud))
        {
            return spec_error(err, errlen,
                              "HDLC serial baud must be one of 1200 2400 4800 9600 19200 38400 "
                              "57600 115200 230400 460800 921600");
        }
        out->baud = baud;
    }
    out->mode = HDLC_LINK_SERIAL;
    out->port = 0;
    return 0;
}

int hdlc_link_spec_parse(const char *text, HdlcLinkSpec *out, char *err, size_t errlen)
{
    const char *first_colon;
    const char *rest;
    const char *second_colon;
    char num[8];
    size_t num_len;

    if (!text || !out)
    {
        return spec_error(err, errlen, "no HDLC config given");
    }
    memset(out, 0, sizeof(*out));

    first_colon = strchr(text, ':');
    if (!first_colon)
    {
        return spec_error(err, errlen,
                          "HDLC config must start with device number: N:PORT, N:HOST:PORT or "
                          "N:serial:DEVICE[:BAUD]");
    }
    num_len = (size_t)(first_colon - text);
    if (num_len == 0 || num_len >= sizeof(num))
    {
        return spec_error(err, errlen, "HDLC device number must be 1-4");
    }
    memcpy(num, text, num_len);
    num[num_len] = '\0';
    if (!parse_int_range(num, 1, 4, &out->device_num))
    {
        return spec_error(err, errlen, "HDLC device number must be 1-4");
    }

    rest = first_colon + 1;
    if (strncmp(rest, SERIAL_PREFIX, strlen(SERIAL_PREFIX)) == 0)
    {
        return parse_serial(rest + strlen(SERIAL_PREFIX), out, err, errlen);
    }

    second_colon = strchr(rest, ':');
    if (second_colon)
    {
        /* Client: HOST:PORT. An empty HOST is kept as before: the client
         * worker then connects to localhost. */
        size_t host_len = (size_t)(second_colon - rest);
        if (host_len >= sizeof(out->address))
        {
            return spec_error(err, errlen, "HDLC client host is too long");
        }
        memcpy(out->address, rest, host_len);
        out->address[host_len] = '\0';
        if (!parse_int_range(second_colon + 1, 1, 65535, &out->port))
        {
            return spec_error(err, errlen, "HDLC port must be 1-65535");
        }
        out->mode = HDLC_LINK_CLIENT;
        return 0;
    }

    /* Server: PORT */
    if (!parse_int_range(rest, 1, 65535, &out->port))
    {
        return spec_error(err, errlen, "HDLC port must be 1-65535");
    }
    out->mode = HDLC_LINK_SERVER;
    return 0;
}
