/*
 * test_tcp_receive_buffer.c - Unit tests for the TCP receive ring buffer.
 *
 * Unit tests for TcpReceiveBuffer (ring buffer).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../src/devices/hdlc/tcp_receive_buffer.h"
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

#define ASSERT_EQ(a, b, msg)                                                                       \
    do                                                                                             \
    {                                                                                              \
        if ((a) != (b))                                                                            \
        {                                                                                          \
            printf("  FAIL: %s: expected %d, got %d (line %d)\n", msg, (int)(b), (int)(a),         \
                   __LINE__);                                                                      \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void test_init_and_destroy(void)
{
    printf("  test_init_and_destroy...");
    TcpReceiveBuffer buf;
    rxbuf_init(&buf, 256);
    ASSERT_EQ(rxbuf_available(&buf), 0, "empty after init");
    rxbuf_destroy(&buf);
    printf(" ok\n");
}

static void test_enqueue_dequeue_simple(void)
{
    printf("  test_enqueue_dequeue_simple...");
    TcpReceiveBuffer buf;
    rxbuf_init(&buf, 256);

    uint8_t data[] = {0x7E, 0x01, 0x02, 0x03, 0x7E};
    int enqueued = rxbuf_enqueue(&buf, data, 5);
    ASSERT_EQ(enqueued, 5, "enqueued all");
    ASSERT_EQ(rxbuf_available(&buf), 5, "5 available");

    uint8_t out;
    for (int i = 0; i < 5; i++)
    {
        bool ok = rxbuf_dequeue_byte(&buf, &out);
        ASSERT(ok, "dequeue succeeded");
        ASSERT_EQ(out, data[i], "correct byte");
    }

    ASSERT_EQ(rxbuf_available(&buf), 0, "empty after drain");

    bool ok = rxbuf_dequeue_byte(&buf, &out);
    ASSERT(!ok, "dequeue from empty fails");

    rxbuf_destroy(&buf);
    printf(" ok\n");
}

static void test_wraparound(void)
{
    printf("  test_wraparound...");
    TcpReceiveBuffer buf;
    rxbuf_init(&buf, 8); // Tiny buffer to force wrap

    // Fill 6 of 8 bytes
    uint8_t data1[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    int enqueued = rxbuf_enqueue(&buf, data1, 6);
    ASSERT_EQ(enqueued, 6, "enqueued 6");

    // Dequeue 4, freeing space at the start
    uint8_t out;
    for (int i = 0; i < 4; i++)
    {
        rxbuf_dequeue_byte(&buf, &out);
    }
    ASSERT_EQ(rxbuf_available(&buf), 2, "2 remaining");

    // Enqueue 5 more - this wraps around
    uint8_t data2[] = {0x11, 0x22, 0x33, 0x44, 0x55};
    enqueued = rxbuf_enqueue(&buf, data2, 5);
    ASSERT_EQ(enqueued, 5, "enqueued 5 with wrap");
    ASSERT_EQ(rxbuf_available(&buf), 7, "7 available");

    // Dequeue all and verify order
    uint8_t expected[] = {0xEE, 0xFF, 0x11, 0x22, 0x33, 0x44, 0x55};
    for (int i = 0; i < 7; i++)
    {
        rxbuf_dequeue_byte(&buf, &out);
        ASSERT_EQ(out, expected[i], "wrap order correct");
    }

    rxbuf_destroy(&buf);
    printf(" ok\n");
}

static void test_full_buffer(void)
{
    printf("  test_full_buffer...");
    TcpReceiveBuffer buf;
    rxbuf_init(&buf, 4);

    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    int enqueued = rxbuf_enqueue(&buf, data, 4);
    ASSERT_EQ(enqueued, 4, "buffer full");
    ASSERT_EQ(rxbuf_available(&buf), 4, "4 available");

    // Try to enqueue more - should return 0
    uint8_t extra = 0xFF;
    enqueued = rxbuf_enqueue(&buf, &extra, 1);
    ASSERT_EQ(enqueued, 0, "rejected when full");

    rxbuf_destroy(&buf);
    printf(" ok\n");
}

static void test_partial_enqueue(void)
{
    printf("  test_partial_enqueue...");
    TcpReceiveBuffer buf;
    rxbuf_init(&buf, 4);

    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    int enqueued = rxbuf_enqueue(&buf, data, 6);
    ASSERT_EQ(enqueued, 4, "only 4 of 6 enqueued");

    rxbuf_destroy(&buf);
    printf(" ok\n");
}

static void test_clear(void)
{
    printf("  test_clear...");
    TcpReceiveBuffer buf;
    rxbuf_init(&buf, 64);

    uint8_t data[] = {0x01, 0x02, 0x03};
    rxbuf_enqueue(&buf, data, 3);
    ASSERT_EQ(rxbuf_available(&buf), 3, "3 before clear");

    rxbuf_clear(&buf);
    ASSERT_EQ(rxbuf_available(&buf), 0, "0 after clear");

    // Can still enqueue after clear
    int enqueued = rxbuf_enqueue(&buf, data, 3);
    ASSERT_EQ(enqueued, 3, "enqueue after clear works");

    rxbuf_destroy(&buf);
    printf(" ok\n");
}

static void test_large_capacity(void)
{
    printf("  test_large_capacity...");
    TcpReceiveBuffer buf;
    rxbuf_init(&buf, TCP_RECV_BUF_DEFAULT_CAPACITY);

    // Fill with 32KB of data
    uint8_t block[1024];
    for (int i = 0; i < 1024; i++)
    {
        block[i] = (uint8_t)(i & 0xFF);
    }

    for (int i = 0; i < 32; i++)
    {
        int enqueued = rxbuf_enqueue(&buf, block, 1024);
        ASSERT_EQ(enqueued, 1024, "1K block enqueued");
    }

    ASSERT_EQ(rxbuf_available(&buf), 32768, "32K available");

    // Drain and verify first byte of each block
    uint8_t out;
    for (int b = 0; b < 32; b++)
    {
        rxbuf_dequeue_byte(&buf, &out);
        ASSERT_EQ(out, 0x00, "block start byte");
        for (int i = 1; i < 1024; i++)
        {
            rxbuf_dequeue_byte(&buf, &out);
        }
    }

    ASSERT_EQ(rxbuf_available(&buf), 0, "empty after drain");

    rxbuf_destroy(&buf);
    printf(" ok\n");
}

int run_tcp_receive_buffer_tests(void)
{
    failures = 0;

    test_init_and_destroy();
    test_enqueue_dequeue_simple();
    test_wraparound();
    test_full_buffer();
    test_partial_enqueue();
    test_clear();
    test_large_capacity();

    if (failures == 0)
    {
        printf("  All tcp_receive_buffer tests passed.\n");
    }
    return failures;
}
