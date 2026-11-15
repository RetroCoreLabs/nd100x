/*
 * test_serial_modem.c - The HDLC modem's serial transport over a pseudo-terminal pair.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * POSIX only. A pseudo-terminal stands in for the USB gateway: the modem
 * opens the slave end by name exactly as it would open /dev/ttyACM0, and
 * the test plays the gateway on the master end. No real serial device is
 * ever opened.
 *
 * Checked:
 *   - every byte value 0x00-0xFF, including the HDLC flag 0x7E, the escape
 *     0x7D, XON/XOFF, CR, LF and Ctrl-C, passes unchanged both ways;
 *   - closing the master (the gateway is unplugged) drops the link and
 *     the worker neither crashes nor spins, and shuts down cleanly;
 *   - a device that does not exist leaves the link down and shuts down.
 */

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../src/devices/hdlc/modem.h"
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

/* The modem state holds two 512 KB queues: keep it off the stack. */
static ModemState s_modem;

static uint8_t s_rx[1024];
static int s_rx_len;

static void on_rx(Device *device, const uint8_t *data, int length)
{
    (void)device;
    for (int i = 0; i < length && s_rx_len < (int)sizeof(s_rx); i++)
    {
        s_rx[s_rx_len++] = data[i];
    }
}

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void wait_ms(long ms)
{
    struct timespec req = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};
    nanosleep(&req, NULL);
}

static bool wait_connected(bool want, long timeout_ms)
{
    long end = now_ms() + timeout_ms;
    while (now_ms() < end)
    {
        if (atomic_load(&s_modem.connected) == want)
        {
            return true;
        }
        wait_ms(10);
    }
    return atomic_load(&s_modem.connected) == want;
}

/* Open a pseudo-terminal pair; returns the master fd and fills the slave
 * name, or -1. */
static int open_pty(char *slave, size_t slave_len)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0)
    {
        return -1;
    }
    if (grantpt(master) != 0 || unlockpt(master) != 0)
    {
        close(master);
        return -1;
    }
    const char *name = ptsname(master);
    if (!name)
    {
        close(master);
        return -1;
    }
    snprintf(slave, slave_len, "%s", name);
    return master;
}

/* Read exactly len bytes from fd within timeout_ms. Returns bytes read. */
static int read_all(int fd, uint8_t *buf, int len, long timeout_ms)
{
    int got = 0;
    long end = now_ms() + timeout_ms;
    while (got < len && now_ms() < end)
    {
        struct pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
        if (poll(&pfd, 1, 50) > 0 && (pfd.revents & POLLIN))
        {
            ssize_t n = read(fd, buf + got, (size_t)(len - got));
            if (n > 0)
            {
                got += (int)n;
            }
        }
    }
    return got;
}

static void test_bytes_both_ways(void)
{
    char slave[128];
    uint8_t all[256];
    uint8_t back[256];

    printf("  test_bytes_both_ways...");
    int master = open_pty(slave, sizeof(slave));
    ASSERT(master >= 0, "pseudo-terminal opened");
    if (master < 0)
    {
        printf(" skipped\n");
        return;
    }
    for (int i = 0; i < 256; i++)
    {
        all[i] = (uint8_t)i;
    }

    modem_init(&s_modem, NULL);
    modem_set_received_data_callback(&s_modem, on_rx);
    s_rx_len = 0;
    modem_start_serial(&s_modem, slave, 115200);
    ASSERT(wait_connected(true, 3000), "serial link up");

    /* Emulator -> gateway. */
    modem_send_bytes(&s_modem, all, 256);
    int got = read_all(master, back, 256, 3000);
    ASSERT(got == 256, "gateway got all 256 bytes");
    ASSERT(memcmp(all, back, 256) == 0, "gateway bytes unchanged");

    /* Gateway -> emulator. */
    ASSERT(write(master, all, 256) == 256, "gateway wrote 256 bytes");
    long end = now_ms() + 3000;
    while (s_rx_len < 256 && now_ms() < end)
    {
        modem_tick(&s_modem);
        wait_ms(1);
    }
    ASSERT(s_rx_len == 256, "emulator got all 256 bytes");
    ASSERT(memcmp(all, s_rx, 256) == 0, "emulator bytes unchanged");

    /* Unplug: the gateway end goes away. */
    close(master);
    ASSERT(wait_connected(false, 3000), "link drops when the port goes away");
    wait_ms(300); /* the worker keeps retrying; it must not crash meanwhile */

    long t0 = now_ms();
    modem_destroy(&s_modem);
    ASSERT(now_ms() - t0 < 3000, "worker shut down promptly after unplug");
    printf(" done\n");
}

static void test_missing_device(void)
{
    printf("  test_missing_device...");
    modem_init(&s_modem, NULL);
    modem_start_serial(&s_modem, "/dev/nd100x-test-no-such-tty", 115200);
    wait_ms(200);
    ASSERT(!atomic_load(&s_modem.connected), "missing device stays down");
    long t0 = now_ms();
    modem_destroy(&s_modem);
    ASSERT(now_ms() - t0 < 3000, "worker shut down promptly");
    printf(" done\n");
}

int run_serial_modem_tests(void)
{
    failures = 0;
    test_bytes_both_ways();
    test_missing_device();
    return failures;
}

#endif
