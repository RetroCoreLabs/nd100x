/*
 * test_terminal_backlog.c - keys pushed in faster than the terminal takes them are held, not lost.
 *
 * The browser build gets a paste or a WebSocket frame in all at once. The terminal's input
 * queue holds TERMINAL_QUEUE_SIZE keys; the backlog holds the rest and moves them on as the
 * queue empties (BUGS.md T1). What this file checks:
 *   - a drain moves no more keys than the queue has room for, so the overrun bit stays clear;
 *   - 3000 keys pushed at once, drained every 10000 instructions as the browser's Step() does,
 *     all reach the UART, in order;
 *   - the backlog grows past its first size while its ring has wrapped, and keeps the order;
 *   - an empty backlog and NULL arguments do nothing.
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
#include "terminal/terminal_backlog.h"

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

#define KEYS 3000
#define STEP 10000 /* instructions per Step() call in template-glass/js/emu-worker.js */

/* The key number n as a 7-bit character, so the order can be checked after the UART. */
static uint8_t key_of(int n)
{
    return (uint8_t)(' ' + (n % 64));
}

int main(void)
{
    printf("terminal_backlog - keys pushed in all at once are held, not lost\n");

    CHECK(devmgr_init() == 0, "the device manager starts");
    CHECK(devmgr_add_device(DEVICE_TYPE_TERMINAL, 0), "a terminal");
    Device *term = devmgr_get_device_by_index(0);
    CHECK(term != NULL, "it is there");
    if (term == NULL)
    {
        return 1;
    }

    /* NULL and empty: nothing happens. */
    TerminalBacklog backlog;
    memset(&backlog, 0, sizeof(backlog));
    CHECK(!terminal_backlog_push(NULL, 'x'), "a push to no backlog fails");
    CHECK(terminal_backlog_drain(NULL, term) == 0, "a drain of no backlog moves nothing");
    CHECK(terminal_backlog_drain(&backlog, NULL) == 0, "a drain to no terminal moves nothing");
    CHECK(terminal_backlog_drain(&backlog, term) == 0, "an empty backlog moves nothing");
    terminal_backlog_clear(NULL);

    /* 3000 keys at once: the first drain fills the queue and no more. */
    bool pushed = true;
    for (int n = 0; n < KEYS; n++)
    {
        pushed = pushed && terminal_backlog_push(&backlog, key_of(n));
    }
    CHECK(pushed, "all 3000 keys are taken");
    CHECK(backlog.count == KEYS, "and all 3000 wait");
    CHECK(terminal_backlog_drain(&backlog, term) == TERMINAL_QUEUE_SIZE,
          "the first drain moves exactly as many as the queue holds");
    CHECK(terminal_input_room(term) == 0, "the queue is full");
    CHECK(terminal_backlog_drain(&backlog, term) == 0, "a second drain at once moves none");

    /* Run as the browser does: drain, then 10000 instructions, read each key as it comes. */
    int got = 0;
    bool order = true;
    int steps = 0;
    while (got < KEYS && steps < 1000)
    {
        (void)terminal_backlog_drain(&backlog, term);
        for (int i = 0; i < STEP; i++)
        {
            (void)devmgr_tick();
            uint16_t status = term->Read(term, term->startAddress + TERMINAL_READ_INPUT_STATUS);
            if ((status & (1u << 3)) != 0)
            {
                uint16_t ch = term->Read(term, term->startAddress + TERMINAL_READ_INPUT_DATA);
                if ((ch & 0x7Fu) != key_of(got))
                {
                    order = false;
                }
                got++;
            }
        }
        steps++;
    }
    CHECK(got == KEYS, "all 3000 keys reach the UART");
    CHECK(order, "in the order they were pushed");
    CHECK(backlog.count == 0, "and the backlog is empty");
    uint16_t status = term->Read(term, term->startAddress + TERMINAL_READ_INPUT_STATUS);
    CHECK((status & (1u << 7)) == 0, "the overrun bit was never set");

    /* Growth while the ring has wrapped: fill the first 1024, take 1000 out, push 2000 more. */
    terminal_backlog_clear(&backlog);
    for (int n = 0; n < 1024; n++)
    {
        (void)terminal_backlog_push(&backlog, (uint8_t)(n & 0xFF));
    }
    CHECK(backlog.cap == 1024, "the first ring is 1024 keys");
    /* Take 1000 off the front by hand, so the head sits near the end of the ring and the
     * next pushes wrap round to its start before the ring has to grow. */
    bool firstOrder = true;
    for (int n = 0; n < 1000; n++)
    {
        if (backlog.buf[backlog.head] != (uint8_t)(n & 0xFF))
        {
            firstOrder = false;
        }
        backlog.head = (backlog.head + 1) % backlog.cap;
        backlog.count--;
    }
    CHECK(firstOrder, "the first 1000 come out in order");
    for (int n = 1024; n < 3024; n++)
    {
        (void)terminal_backlog_push(&backlog, (uint8_t)(n & 0xFF));
    }
    CHECK(backlog.count == 2024, "24 left plus 2000 new wait");
    CHECK(backlog.cap >= 2024, "the ring grew");
    bool grownOrder = true;
    for (size_t k = 0; k < backlog.count; k++)
    {
        if (backlog.buf[(backlog.head + k) % backlog.cap] != (uint8_t)((1000 + (int)k) & 0xFF))
        {
            grownOrder = false;
        }
    }
    CHECK(grownOrder, "and every waiting key is still in order");
    terminal_backlog_clear(&backlog);
    CHECK(backlog.buf == NULL && backlog.count == 0 && backlog.cap == 0, "clear frees it all");

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
