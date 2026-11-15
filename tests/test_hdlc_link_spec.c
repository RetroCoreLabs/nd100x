/*
 * test_hdlc_link_spec.c - Unit tests for the --hdlc=... link text parser.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Covers the three forms (TCP server, TCP client, serial port), the serial
 * baud default and list, and the malformed texts that must be refused.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "../src/devices/hdlc/hdlc_link_spec.h"
#include "test_suites.h"

static int failures = 0;

#define ASSERT(cond, msg)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            printf("  FAIL: %s (line %d)\n", msg, __LINE__);                                       \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static bool parse_ok(const char *text, HdlcLinkSpec *spec)
{
    char err[160];
    return hdlc_link_spec_parse(text, spec, err, sizeof(err)) == 0;
}

static void test_server(void)
{
    HdlcLinkSpec s;
    printf("  test_server...");
    ASSERT(parse_ok("1:5000", &s), "1:5000 parses");
    ASSERT(s.device_num == 1, "device 1");
    ASSERT(s.mode == HDLC_LINK_SERVER, "server mode");
    ASSERT(s.port == 5000, "port 5000");
    ASSERT(s.address[0] == '\0', "no address");
    printf(" done\n");
}

static void test_client(void)
{
    HdlcLinkSpec s;
    printf("  test_client...");
    ASSERT(parse_ok("2:192.168.1.10:5001", &s), "client parses");
    ASSERT(s.device_num == 2, "device 2");
    ASSERT(s.mode == HDLC_LINK_CLIENT, "client mode");
    ASSERT(strcmp(s.address, "192.168.1.10") == 0, "host");
    ASSERT(s.port == 5001, "port");
    ASSERT(parse_ok("3:localhost:65535", &s), "port 65535 accepted");
    printf(" done\n");
}

static void test_serial(void)
{
    HdlcLinkSpec s;
    printf("  test_serial...");
#if HDLC_SERIAL_AVAILABLE
    ASSERT(parse_ok("1:serial:/dev/ttyACM0", &s), "linux device parses");
    ASSERT(s.mode == HDLC_LINK_SERIAL, "serial mode");
    ASSERT(strcmp(s.address, "/dev/ttyACM0") == 0, "device kept");
    ASSERT(s.baud == HDLC_SERIAL_DEFAULT_BAUD, "default baud");
    ASSERT(s.port == 0, "no TCP port");

    ASSERT(parse_ok("4:serial:COM18:921600", &s), "windows device with baud parses");
    ASSERT(s.device_num == 4, "device 4");
    ASSERT(strcmp(s.address, "COM18") == 0, "COM18 kept");
    ASSERT(s.baud == 921600, "baud 921600");

    ASSERT(parse_ok("1:serial:\\\\.\\COM3:9600", &s), "\\\\.\\COM3 parses");
    ASSERT(strcmp(s.address, "\\\\.\\COM3") == 0, "\\\\.\\COM3 kept");
    ASSERT(s.baud == 9600, "baud 9600");

    ASSERT(!parse_ok("1:serial:", &s), "empty device refused");
    ASSERT(!parse_ok("1:serial::115200", &s), "empty device with baud refused");
    ASSERT(!parse_ok("1:serial:COM18:307200", &s), "non-standard baud refused");
    ASSERT(!parse_ok("1:serial:COM18:fast", &s), "text baud refused");
    ASSERT(!parse_ok("1:serial:COM18:", &s), "empty baud refused");
#else
    ASSERT(!parse_ok("1:serial:COM18", &s), "serial refused where not available");
#endif
    printf(" done\n");
}

static void test_bad(void)
{
    HdlcLinkSpec s;
    char err[160];
    printf("  test_bad...");
    ASSERT(!parse_ok("5000", &s), "no device number refused");
    ASSERT(!parse_ok("0:5000", &s), "device 0 refused");
    ASSERT(!parse_ok("5:5000", &s), "device 5 refused");
    ASSERT(!parse_ok("x:5000", &s), "text device refused");
    ASSERT(!parse_ok("1:0", &s), "port 0 refused");
    ASSERT(!parse_ok("1:65536", &s), "port 65536 refused");
    ASSERT(!parse_ok("1:host:port", &s), "text port refused");
    ASSERT(!parse_ok("1:", &s), "empty port refused");
    err[0] = '\0';
    ASSERT(hdlc_link_spec_parse("9:1", &s, err, sizeof(err)) != 0, "9:1 refused");
    ASSERT(err[0] != '\0', "reason given");
    ASSERT(hdlc_link_spec_parse(NULL, &s, NULL, 0) != 0, "NULL text refused");
    printf(" done\n");
}

static void test_baud_list(void)
{
    printf("  test_baud_list...");
    ASSERT(hdlc_link_spec_is_valid_baud(115200), "115200");
    ASSERT(hdlc_link_spec_is_valid_baud(1200), "1200");
    ASSERT(hdlc_link_spec_is_valid_baud(921600), "921600");
    ASSERT(!hdlc_link_spec_is_valid_baud(0), "0");
    ASSERT(!hdlc_link_spec_is_valid_baud(-9600), "negative");
    ASSERT(!hdlc_link_spec_is_valid_baud(307200), "307200");
    printf(" done\n");
}

int run_hdlc_link_spec_tests(void)
{
    failures = 0;
    test_server();
    test_client();
    test_serial();
    test_bad();
    test_baud_list();
    return failures;
}
