/*
 * test_hdlc_frame.c - Unit tests for HDLC frame build, byte stuffing and processing.
 *
 * Unit tests for HDLC frame processing: build, stuff/destuff, process, roundtrip.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../src/devices/hdlc/hdlc_frame.h"
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

/* --- Byte stuffing tests --- */

static void test_stuff_normal_byte(void)
{
    printf("  test_stuff_normal_byte...");
    uint8_t out[4];
    int idx = 0;
    int written = hdlc_frame_stuff_byte(0x42, out, sizeof(out), &idx);
    ASSERT_EQ(written, 1, "one byte written");
    ASSERT_EQ(out[0], 0x42, "byte unchanged");
    printf(" ok\n");
}

static void test_stuff_flag_byte(void)
{
    printf("  test_stuff_flag_byte...");
    uint8_t out[4];
    int idx = 0;
    int written = hdlc_frame_stuff_byte(HDLC_FLAG, out, sizeof(out), &idx);
    ASSERT_EQ(written, 2, "two bytes for flag");
    ASSERT_EQ(out[0], HDLC_ESCAPE, "escape prefix");
    ASSERT_EQ(out[1], HDLC_FLAG ^ HDLC_ESCAPE_MASK, "stuffed flag");
    printf(" ok\n");
}

static void test_stuff_escape_byte(void)
{
    printf("  test_stuff_escape_byte...");
    uint8_t out[4];
    int idx = 0;
    int written = hdlc_frame_stuff_byte(HDLC_ESCAPE, out, sizeof(out), &idx);
    ASSERT_EQ(written, 2, "two bytes for escape");
    ASSERT_EQ(out[0], HDLC_ESCAPE, "escape prefix");
    ASSERT_EQ(out[1], HDLC_ESCAPE ^ HDLC_ESCAPE_MASK, "stuffed escape");
    printf(" ok\n");
}

static void test_destuff_byte(void)
{
    printf("  test_destuff_byte...");
    uint8_t result = hdlc_frame_destuff_byte(HDLC_FLAG ^ HDLC_ESCAPE_MASK);
    ASSERT_EQ(result, HDLC_FLAG, "destuffed flag");

    result = hdlc_frame_destuff_byte(HDLC_ESCAPE ^ HDLC_ESCAPE_MASK);
    ASSERT_EQ(result, HDLC_ESCAPE, "destuffed escape");
    printf(" ok\n");
}

/* --- Frame building tests --- */

static void test_build_simple_frame(void)
{
    printf("  test_build_simple_frame...");
    uint8_t payload[] = {0x01, 0x02, 0x03};
    uint8_t frame[64];
    int len = hdlc_frame_build_frame(payload, 3, frame, sizeof(frame));

    ASSERT(len > 0, "frame built");
    ASSERT_EQ(frame[0], HDLC_FLAG, "starts with flag");
    ASSERT_EQ(frame[len - 1], HDLC_FLAG, "ends with flag");
    // At minimum: FLAG + 3 data + 2 CRC + FLAG = 7 bytes (no stuffing needed)
    ASSERT(len >= 7, "minimum frame size");
    printf(" ok\n");
}

static void test_build_frame_with_stuffing(void)
{
    printf("  test_build_frame_with_stuffing...");
    // Payload containing bytes that need stuffing
    uint8_t payload[] = {HDLC_FLAG, HDLC_ESCAPE, 0x42};
    uint8_t frame[64];
    int len = hdlc_frame_build_frame(payload, 3, frame, sizeof(frame));

    ASSERT(len > 0, "frame built");
    // FLAG + stuffed(7E)=2 + stuffed(7D)=2 + 42=1 + 2*CRC(maybe stuffed) + FLAG
    // At least 9 bytes
    ASSERT(len >= 9, "frame longer due to stuffing");
    printf(" ok\n");
}

/* --- Frame processing (receive) tests --- */

static void test_process_idle_ignores_non_flag(void)
{
    printf("  test_process_idle_ignores_non_flag...");
    HDLCFrame frame;
    hdlc_frame_init(&frame);

    // Random bytes before flag should be ignored
    ASSERT(!hdlc_frame_add_byte(&frame, 0x42), "ignored");
    ASSERT(!hdlc_frame_add_byte(&frame, 0xFF), "ignored");
    ASSERT_EQ(frame.frameLength, 0, "no data collected");
    printf(" ok\n");
}

static void test_process_empty_frame(void)
{
    printf("  test_process_empty_frame...");
    HDLCFrame frame;
    hdlc_frame_init(&frame);

    // FLAG followed immediately by FLAG = no data, reset
    hdlc_frame_add_byte(&frame, HDLC_FLAG);
    bool complete = hdlc_frame_add_byte(&frame, HDLC_FLAG);
    ASSERT(!complete, "empty frame not complete");
    printf(" ok\n");
}

static void test_process_too_short_frame(void)
{
    printf("  test_process_too_short_frame...");
    HDLCFrame frame;
    hdlc_frame_init(&frame);

    // FLAG + 1 byte + FLAG = too short (need at least 2 for CRC)
    hdlc_frame_add_byte(&frame, HDLC_FLAG);
    hdlc_frame_add_byte(&frame, 0x42);
    bool complete = hdlc_frame_add_byte(&frame, HDLC_FLAG);
    ASSERT(!complete, "1-byte frame too short");
    printf(" ok\n");
}

static void test_process_escape_handling(void)
{
    printf("  test_process_escape_handling...");
    HDLCFrame frame;
    hdlc_frame_init(&frame);

    hdlc_frame_add_byte(&frame, HDLC_FLAG);                    // Start
    hdlc_frame_add_byte(&frame, 0x01);                         // Normal byte
    hdlc_frame_add_byte(&frame, HDLC_ESCAPE);                  // Escape prefix
    hdlc_frame_add_byte(&frame, HDLC_FLAG ^ HDLC_ESCAPE_MASK); // Stuffed 0x7E

    // frame should have: [0x01, 0x7E]
    ASSERT_EQ(frame.frameLength, 2, "two bytes after destuffing");
    ASSERT_EQ(frame.frameBuffer[0], 0x01, "first byte");
    ASSERT_EQ(frame.frameBuffer[1], HDLC_FLAG, "destuffed second byte");
    printf(" ok\n");
}

static void test_process_error_recovery(void)
{
    printf("  test_process_error_recovery...");
    HDLCFrame frame;
    hdlc_frame_init(&frame);

    // Fill frame to max to trigger error state
    hdlc_frame_add_byte(&frame, HDLC_FLAG);
    for (int i = 0; i < HDLC_MAX_FRAME_SIZE; i++)
    {
        hdlc_frame_add_byte(&frame, 0x42);
    }
    // Next byte overflows -> ERROR state
    hdlc_frame_add_byte(&frame, 0x42);
    ASSERT_EQ(frame.state, HDLC_STATE_ERROR, "in error state");

    // FLAG should recover
    hdlc_frame_add_byte(&frame, HDLC_FLAG);
    ASSERT_EQ(frame.state, HDLC_STATE_RECEIVING, "recovered after flag");
    ASSERT_EQ(frame.frameLength, 0, "frame reset");
    printf(" ok\n");
}

/* --- Roundtrip test: build -> process -> verify --- */

static void test_roundtrip(void)
{
    printf("  test_roundtrip...");

    // Build a frame from payload
    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t wire[128];
    int wire_len = hdlc_frame_build_frame(payload, 5, wire, sizeof(wire));
    ASSERT(wire_len > 0, "frame built");

    // Feed wire bytes into receiver
    HDLCFrame rx;
    hdlc_frame_init(&rx);
    bool complete = false;
    for (int i = 0; i < wire_len; i++)
    {
        complete = hdlc_frame_add_byte(&rx, wire[i]);
        if (complete)
        {
            break;
        }
    }

    ASSERT(complete, "frame complete");
    ASSERT(hdlc_frame_is_crc_valid(&rx), "CRC valid");

    // Frame data should be payload + 2 CRC bytes
    int rx_len = hdlc_frame_get_frame_length(&rx);
    ASSERT_EQ(rx_len, 7, "payload(5) + CRC(2)");

    const uint8_t *rx_data = hdlc_frame_get_frame_data(&rx);
    for (int i = 0; i < 5; i++)
    {
        ASSERT_EQ(rx_data[i], payload[i], "payload match");
    }
    printf(" ok\n");
}

static void test_roundtrip_with_special_bytes(void)
{
    printf("  test_roundtrip_with_special_bytes...");

    // Payload containing FLAG and ESCAPE bytes
    uint8_t payload[] = {HDLC_FLAG, 0x00, HDLC_ESCAPE, 0xFF, HDLC_FLAG};
    uint8_t wire[128];
    int wire_len = hdlc_frame_build_frame(payload, 5, wire, sizeof(wire));
    ASSERT(wire_len > 0, "frame built with special bytes");

    HDLCFrame rx;
    hdlc_frame_init(&rx);
    bool complete = false;
    for (int i = 0; i < wire_len; i++)
    {
        complete = hdlc_frame_add_byte(&rx, wire[i]);
        if (complete)
        {
            break;
        }
    }

    ASSERT(complete, "frame complete");
    ASSERT(hdlc_frame_is_crc_valid(&rx), "CRC valid");

    const uint8_t *rx_data = hdlc_frame_get_frame_data(&rx);
    ASSERT_EQ(rx_data[0], HDLC_FLAG, "0x7E survived roundtrip");
    ASSERT_EQ(rx_data[2], HDLC_ESCAPE, "0x7D survived roundtrip");
    printf(" ok\n");
}

static void test_roundtrip_multiple_frames(void)
{
    printf("  test_roundtrip_multiple_frames...");

    // Build two frames back-to-back
    uint8_t p1[] = {0xAA, 0xBB};
    uint8_t p2[] = {0xCC, 0xDD, 0xEE};
    uint8_t wire1[64];
    uint8_t wire2[64];
    int len1 = hdlc_frame_build_frame(p1, 2, wire1, sizeof(wire1));
    int len2 = hdlc_frame_build_frame(p2, 3, wire2, sizeof(wire2));

    // Concatenate into one stream
    uint8_t stream[128];
    memcpy(stream, wire1, (size_t)len1);
    memcpy(stream + len1, wire2, (size_t)len2);
    int total_len = len1 + len2;

    // Process stream, expecting two complete frames
    HDLCFrame rx;
    hdlc_frame_init(&rx);
    int frames_received = 0;

    for (int i = 0; i < total_len; i++)
    {
        bool complete = hdlc_frame_add_byte(&rx, stream[i]);
        if (complete)
        {
            frames_received++;
            ASSERT(hdlc_frame_is_crc_valid(&rx), "frame CRC valid");

            if (frames_received == 1)
            {
                ASSERT_EQ(hdlc_frame_get_frame_length(&rx), 4, "frame1: 2+2 CRC");
                ASSERT_EQ(hdlc_frame_get_frame_data(&rx)[0], 0xAA, "frame1 data");
            }
            else if (frames_received == 2)
            {
                ASSERT_EQ(hdlc_frame_get_frame_length(&rx), 5, "frame2: 3+2 CRC");
                ASSERT_EQ(hdlc_frame_get_frame_data(&rx)[0], 0xCC, "frame2 data");
            }

            hdlc_frame_reset(&rx);
        }
    }

    ASSERT_EQ(frames_received, 2, "two frames received");
    printf(" ok\n");
}

static void test_roundtrip_large_payload(void)
{
    printf("  test_roundtrip_large_payload...");

    // 256 byte payload
    uint8_t payload[256];
    for (int i = 0; i < 256; i++)
    {
        payload[i] = (uint8_t)i;
    }

    uint8_t wire[1024];
    int wire_len = hdlc_frame_build_frame(payload, 256, wire, sizeof(wire));
    ASSERT(wire_len > 0, "large frame built");

    HDLCFrame rx;
    hdlc_frame_init(&rx);
    bool complete = false;
    for (int i = 0; i < wire_len; i++)
    {
        complete = hdlc_frame_add_byte(&rx, wire[i]);
        if (complete)
        {
            break;
        }
    }

    ASSERT(complete, "large frame complete");
    ASSERT(hdlc_frame_is_crc_valid(&rx), "large frame CRC valid");

    int rx_len = hdlc_frame_get_frame_length(&rx);
    ASSERT_EQ(rx_len, 258, "256 payload + 2 CRC");

    const uint8_t *rx_data = hdlc_frame_get_frame_data(&rx);
    for (int i = 0; i < 256; i++)
    {
        ASSERT_EQ(rx_data[i], (uint8_t)i, "payload byte match");
    }
    printf(" ok\n");
}

int run_hdlc_frame_tests(void)
{
    failures = 0;

    test_stuff_normal_byte();
    test_stuff_flag_byte();
    test_stuff_escape_byte();
    test_destuff_byte();

    test_build_simple_frame();
    test_build_frame_with_stuffing();

    test_process_idle_ignores_non_flag();
    test_process_empty_frame();
    test_process_too_short_frame();
    test_process_escape_handling();
    test_process_error_recovery();

    test_roundtrip();
    test_roundtrip_with_special_bytes();
    test_roundtrip_multiple_frames();
    test_roundtrip_large_payload();

    if (failures == 0)
    {
        printf("  All hdlc_frame tests passed.\n");
    }
    return failures;
}
