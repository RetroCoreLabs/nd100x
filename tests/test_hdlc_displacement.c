/*
 * test_hdlc_displacement.c - Which receive displacement each DMA buffer gets.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * The HDLC DMA INITIALIZE parameter buffer gives two displacements:
 * Displacement 1 = "No. of bytes, first block in frame" and Displacement 2 =
 * "No. of bytes, other blocks in frame". The choice follows the block's place
 * in the FRAME, not the buffer's place in the list. Until 2026-11-16 nd100x
 * gave Displacement 1 to list offset 0 only; the SINTRAN K COSMOS X.25 link
 * (Displacement1 = 1, Displacement2 = 0) then read address and control one byte
 * too far in every other buffer ("Rcvd Illegal address A,C=000020", measured on
 * RetroCore, which had the same rule). Same cases as RetroCore's
 * HDLCDisplacementTests.
 *
 * The byte count written back is the bytes RECEIVED, without the displacement:
 * SINTRAN adds Displacement 1 back itself (MP-P2-HDLC-DRIV.NPL, 104615,
 * "A:=LIINT.LBYTC+DISP1=:T % RECEIVED MESSAGE SIZE").
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../src/devices/hdlc/dma_control_blocks.h"
#include "../src/devices/hdlc/dma_param_buf.h"
#include "../src/devices/hdlc/dma_enum.h"
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

#define LIST_POINTER 0x0100u /* receive list: 4 words per entry */
#define FIRST_BUFFER 0x1000u /* buffer N lives at FIRST_BUFFER + N * 0x100 */
#define BUFFERS 8

static uint16_t memory[0x2000];
static DMAControlBlocks blocks;
static ParameterBuffer params;

static uint16_t mem_read(void *context, uint32_t address)
{
    (void)context;
    return address < 0x2000u ? memory[address] : 0;
}

static void mem_write(void *context, uint32_t address, uint16_t data)
{
    (void)context;
    if (address < 0x2000u)
    {
        memory[address] = data;
    }
}

/* A list of empty receive buffers, the X.25 link's displacements (1 and 0),
 * then what RECEIVER START does: frame start, list pointer. */
static void setup(void)
{
    memset(memory, 0, sizeof(memory));
    for (uint32_t i = 0; i < BUFFERS; i++)
    {
        uint32_t entry = LIST_POINTER + i * 4;
        uint32_t buffer = FIRST_BUFFER + i * 0x100;
        memory[entry + 0] = (uint16_t)KEYFLAG_EMPTY_RECEIVER_BLOCK;
        memory[entry + 1] = 0;
        memory[entry + 2] = (uint16_t)(buffer >> 16);
        memory[entry + 3] = (uint16_t)(buffer & 0xFFFF);
    }
    memory[LIST_POINTER + BUFFERS * 4] = 0; /* end of list */

    dmacb_init(&blocks, NULL);
    dmacb_set_read_dma_callback(&blocks, mem_read, NULL);
    dmacb_set_write_dma_callback(&blocks, mem_write, NULL);
    memset(&params, 0, sizeof(params));
    params.displacement1 = 1;
    params.displacement2 = 0;
    params.maxReceiverBlockLength = 0x100;
    blocks.parameters = &params;

    blocks.rxNextBlockStartsFrame = true; /* what dma_engine's RECEIVER START sets */
    dmacb_set_rx_pointer(&blocks, LIST_POINTER, 0);
}

/* One block the way the receiver writes it: bytes, mark received with the
 * status (bit 0 RSOM, bit 1 REOM), then on to the next buffer. */
static void receive_block(const uint8_t *data, int length, uint8_t status)
{
    for (int i = 0; i < length; i++)
    {
        dmacb_write_next_byte_dma(&blocks, data[i], true);
    }
    dmacb_mark_buffer_received(&blocks, status);
    dmacb_load_next_rx_buffer(&blocks);
}

/* Byte index of buffer n (even byte = high half of the word). */
static uint8_t buffer_byte(int n, int index)
{
    uint16_t word = memory[FIRST_BUFFER + (uint32_t)n * 0x100 + (uint32_t)(index / 2)];
    return (index % 2) == 0 ? (uint8_t)(word >> 8) : (uint8_t)(word & 0xFF);
}

static uint16_t byte_count(int n)
{
    return memory[LIST_POINTER + (uint32_t)n * 4 + 1];
}

static void test_every_frame_start_gets_displacement1(void)
{
    printf("  test_every_frame_start_gets_displacement1...");
    setup();
    static const uint8_t ua[] = {0x01, 0x73};
    static const uint8_t rr[] = {0x03, 0x01};
    receive_block(ua, 2, 0x03);
    receive_block(rr, 2, 0x03);

    ASSERT(buffer_byte(0, 1) == 0x01, "buffer 0 address byte after one free byte");
    ASSERT(buffer_byte(0, 2) == 0x73, "buffer 0 control byte");
    ASSERT(byte_count(0) == 2, "buffer 0 byte count: the 2 frame bytes, not the free byte");
    ASSERT(buffer_byte(1, 1) == 0x03, "buffer 1 starts a frame too: one free byte");
    ASSERT(buffer_byte(1, 2) == 0x01, "buffer 1 control byte");
    ASSERT(byte_count(1) == 2, "buffer 1 byte count");
    dmacb_destroy(&blocks);
    printf(" done\n");
}

static void test_continuation_block_gets_displacement2(void)
{
    printf("  test_continuation_block_gets_displacement2...");
    setup();
    static const uint8_t part1[] = {0x03, 0x00, 0x10};
    static const uint8_t part2[] = {0x00, 0xFB};
    static const uint8_t ua[] = {0x01, 0x73};
    receive_block(part1, 3, 0x01); /* filled up, no REOM: the frame goes on */
    receive_block(part2, 2, 0x02); /* end of the frame */

    ASSERT(buffer_byte(0, 1) == 0x03, "first block: one free byte");
    ASSERT(buffer_byte(0, 3) == 0x10, "first block last byte");
    ASSERT(byte_count(0) == 3, "3 bytes received, the free byte not counted");
    ASSERT(buffer_byte(1, 0) == 0x00, "continuation: data from byte 0");
    ASSERT(buffer_byte(1, 1) == 0xFB, "continuation second byte");
    ASSERT(byte_count(1) == 2, "continuation byte count");

    receive_block(ua, 2, 0x03); /* after a frame end, a new frame */
    ASSERT(buffer_byte(2, 1) == 0x01, "next frame start: one free byte");
    ASSERT(buffer_byte(2, 2) == 0x73, "next frame control byte");
    dmacb_destroy(&blocks);
    printf(" done\n");
}

static void test_receiver_continue_mid_frame_keeps_displacement2(void)
{
    printf("  test_receiver_continue_mid_frame_keeps_displacement2...");
    setup();
    static const uint8_t part1[] = {0x03, 0x00, 0x10};
    static const uint8_t part2[] = {0x00, 0xFB};
    receive_block(part1, 3, 0x01);

    /* RECEIVER CONTINUE: only a new list pointer, no frame-start reset. */
    dmacb_set_rx_pointer(&blocks, LIST_POINTER + 8, 0);
    receive_block(part2, 2, 0x02);

    ASSERT(buffer_byte(2, 0) == 0x00, "continuation after CONTINUE: data from byte 0");
    ASSERT(buffer_byte(2, 1) == 0xFB, "continuation second byte");
    ASSERT(byte_count(2) == 2, "continuation byte count");
    dmacb_destroy(&blocks);
    printf(" done\n");
}

int run_hdlc_displacement_tests(void)
{
    failures = 0;
    test_every_frame_start_gets_displacement1();
    test_continuation_block_gets_displacement2();
    test_receiver_continue_mid_frame_keeps_displacement2();
    return failures;
}
