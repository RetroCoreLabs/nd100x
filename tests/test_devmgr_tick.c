/*
 * test_devmgr_tick.c - devmgr_tick() calls a device only when it has work.
 *
 * devmgr_tick() runs after every ND-100 instruction. It used to call every
 * device's Tick every time; now it calls a device only while it has a queued
 * I/O delay or says it needs a call (terminal: a character in the input queue),
 * plus the devices that asked for every instruction (tickAlways).
 *
 * What must not change, and what this file checks:
 *   - a delay of N fires on the Nth call after it was queued (0 and 1 both on
 *     the first), whatever else is or is not pending;
 *   - delays on two devices run independently;
 *   - a callback that queues a new delay gets it counted from the next call;
 *   - the return value is the OR of the devices' interrupt bits, and follows
 *     a bit that is set or cleared between two calls;
 *   - a character queued on a terminal reaches the UART within MAX_TICKS + 1
 *     calls, also when it was queued after a long stretch with nothing to do.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cpu_types.h"
#include "devices_types.h"
#include "devices_protos.h"
#include "terminal/device_terminal.h"

static int s_checks = 0;
static int s_failed = 0;

#define CHECK(cond, msg)                                              \
    do                                                                \
    {                                                                 \
        s_checks++;                                                   \
        if (!(cond))                                                  \
        {                                                             \
            s_failed++;                                               \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        }                                                             \
    } while (0)

static int s_fired[8];
static Device *s_requeue_on = NULL;
static int s_requeue_ticks = 0;

static bool fire(void *context, int param)
{
    (void)context;
    s_fired[param]++;
    return true; /* raise the delay's interrupt level */
}

static bool fire_and_requeue(void *context, int param)
{
    (void)context;
    s_fired[param]++;
    if (s_requeue_on != NULL)
    {
        dev_queue_io_delay(s_requeue_on, (uint16_t)s_requeue_ticks, fire, param + 1, 0);
    }
    return false;
}

/* Calls devmgr_tick() until s_fired[slot] is non-zero; returns the number of
 * calls, or -1 after `limit` calls. */
static int ticks_until_fired(int slot, int limit)
{
    for (int n = 1; n <= limit; n++)
    {
        (void)devmgr_tick();
        if (s_fired[slot] != 0)
        {
            return n;
        }
    }
    return -1;
}

int main(void)
{
    printf("devmgr_tick - devices are called only when they have work\n");

    CHECK(devmgr_init() == 0, "the device manager starts");
    CHECK(devmgr_add_device(DEVICE_TYPE_PAPER_TAPE, 0), "a paper tape reader");
    CHECK(devmgr_add_device(DEVICE_TYPE_PAPER_TAPE_WRITER, 0), "a paper tape punch");
    CHECK(devmgr_add_device(DEVICE_TYPE_TERMINAL, 0), "a terminal");
    Device *reader = devmgr_get_device_by_index(0);
    Device *punch = devmgr_get_device_by_index(1);
    Device *term = devmgr_get_device_by_index(2);
    CHECK(reader != NULL && punch != NULL && term != NULL, "all three are there");
    if (reader == NULL || punch == NULL || term == NULL)
    {
        return 1;
    }
    CHECK(!reader->tickAlways && !punch->tickAlways && !term->tickAlways,
          "none of them asks for a call on every instruction");

    /* Nothing pending: many calls, nothing happens, no bits. */
    for (int i = 0; i < 1000; i++)
    {
        CHECK(devmgr_tick() == 0, "no interrupt bits with nothing pending");
    }

    /* A delay of N fires on the Nth call; 0 fires on the first. */
    static const int lengths[] = {0, 1, 2, 3, 63, 64, 65, 1000};
    for (unsigned k = 0; k < sizeof(lengths) / sizeof(lengths[0]); k++)
    {
        memset(s_fired, 0, sizeof(s_fired));
        dev_queue_io_delay(reader, (uint16_t)lengths[k], fire, 0, 0);
        int expect = lengths[k] == 0 ? 1 : lengths[k];
        int got = ticks_until_fired(0, 5000);
        if (got != expect)
        {
            printf("    delay %d fired on call %d, expected %d\n", lengths[k], got, expect);
        }
        CHECK(got == expect, "a delay fires on the call it always fired on");
        CHECK(reader->ioDelayCount == 0, "and is gone afterwards");
    }

    /* The same after a long stretch with nothing to do. */
    for (int i = 0; i < 5000; i++)
    {
        (void)devmgr_tick();
    }
    memset(s_fired, 0, sizeof(s_fired));
    dev_queue_io_delay(reader, 7, fire, 0, 0);
    CHECK(ticks_until_fired(0, 100) == 7, "a delay queued after an idle stretch is exact too");

    /* Two devices, two delays, independent. */
    memset(s_fired, 0, sizeof(s_fired));
    dev_queue_io_delay(reader, 5, fire, 0, 0);
    dev_queue_io_delay(punch, 9, fire, 1, 0);
    CHECK(ticks_until_fired(0, 100) == 5, "the reader's delay of 5 fires on call 5");
    CHECK(s_fired[1] == 0, "the punch's delay of 9 has not fired yet");
    CHECK(ticks_until_fired(1, 100) == 4, "and fires 4 calls later");

    /* A callback that queues a delay on a LATER device: that device is called
     * in the same pass, so the new delay loses its first count at once. On an
     * EARLIER device the first count comes with the next call. Both as before. */
    memset(s_fired, 0, sizeof(s_fired));
    s_requeue_on = punch;
    s_requeue_ticks = 3;
    dev_queue_io_delay(reader, 2, fire_and_requeue, 0, 0);
    CHECK(ticks_until_fired(0, 100) == 2, "the first delay fires on call 2");
    CHECK(ticks_until_fired(1, 100) == 2, "a delay of 3 queued on a later device fires 2 calls on");
    memset(s_fired, 0, sizeof(s_fired));
    s_requeue_on = reader;
    dev_queue_io_delay(punch, 2, fire_and_requeue, 0, 0);
    CHECK(ticks_until_fired(0, 100) == 2, "the first delay fires on call 2");
    CHECK(ticks_until_fired(1, 100) == 3, "a delay of 3 queued on an earlier device fires 3 calls on");
    s_requeue_on = NULL;

    /* Interrupt bits: a delay with a level raises it, and the OR follows. */
    memset(s_fired, 0, sizeof(s_fired));
    dev_queue_io_delay(reader, 4, fire, 0, 12);
    uint16_t bits = 0;
    for (int i = 0; i < 3; i++)
    {
        bits = devmgr_tick();
    }
    CHECK(bits == 0, "no bit before the delay runs out");
    bits = devmgr_tick();
    CHECK(bits == (1u << 12), "level 12 on the call the delay runs out");
    CHECK(devmgr_tick() == (1u << 12), "and it stays");
    dev_set_interrupt_status(punch, true, 10);
    CHECK(devmgr_tick() == ((1u << 12) | (1u << 10)), "a bit set between two calls is in the OR");
    dev_set_interrupt_status(reader, false, 12);
    CHECK(devmgr_tick() == (1u << 10), "a bit cleared between two calls is out of it");
    dev_set_interrupt_status(punch, false, 10);
    CHECK(devmgr_tick() == 0, "and with both cleared nothing is left");

    /* Terminal input: a queued character reaches the UART. */
    for (int round = 0; round < 3; round++)
    {
        for (int i = 0; i < 777; i++)
        {
            (void)devmgr_tick();
        }
        terminal_queue_key_code(term, (uint8_t)('A' + round));
        int calls = 0;
        uint16_t status = 0;
        do
        {
            (void)devmgr_tick();
            calls++;
            status = term->Read(term, term->startAddress + TERMINAL_READ_INPUT_STATUS);
        } while ((status & (1u << 3)) == 0 && calls < 500);
        CHECK(calls <= MAX_TICKS + 1, "the character is in the UART within MAX_TICKS + 1 calls");
        uint16_t ch = term->Read(term, term->startAddress + TERMINAL_READ_INPUT_DATA);
        CHECK((ch & 0x7Fu) == (uint16_t)('A' + round), "and it is the character that was queued");
    }

    devmgr_destroy();

    printf("\n%d check(s), %d failed\n", s_checks, s_failed);
    if (s_failed != 0)
    {
        printf("FAIL\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
