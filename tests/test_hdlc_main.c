/*
 * test_hdlc_main.c - Test runner for the HDLC unit tests (ring buffer, frame, CRC).
 *
 * Main test runner for HDLC module unit tests.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Runs: ring buffer, HDLC frame, CRC tests.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

/* Linker stubs - HDLC tests don't need CPU memory access. Prototypes match
 * the real functions in src/cpu (normally declared in cpu_protos.h). */
int ReadPhysicalMemory(int physicalAddress, bool privileged);
void WritePhysicalMemory(int physicalAddress, uint16_t value, bool privileged);
void interrupt(uint16_t lvl, uint16_t sub);
int ReadPhysicalMemory(int physicalAddress, bool privileged)
{
    (void)physicalAddress;
    (void)privileged;
    return 0;
}
void WritePhysicalMemory(int physicalAddress, uint16_t value, bool privileged)
{
    (void)physicalAddress;
    (void)value;
    (void)privileged;
}
void interrupt(uint16_t lvl, uint16_t sub)
{
    (void)lvl;
    (void)sub;
}

/* Suite runners */
#include "test_suites.h"

int main(void)
{
    int total_failures = 0;

    printf("[tcp_receive_buffer]\n");
    total_failures += run_tcp_receive_buffer_tests();
    printf("\n");

    printf("[hdlc_crc]\n");
    total_failures += run_hdlc_crc_tests();
    printf("\n");

    printf("[hdlc_frame]\n");
    total_failures += run_hdlc_frame_tests();
    printf("\n");

    if (total_failures == 0)
    {
        printf("All HDLC tests PASSED.\n");
    }
    else
    {
        printf("%d HDLC test(s) FAILED.\n", total_failures);
    }

    return total_failures ? 1 : 0;
}
