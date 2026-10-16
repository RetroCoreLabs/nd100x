/*
 * test_qpc_time.c - Regression test for qpc_ticks_to_ns() (src/ndlib/qpc_time.h).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Background: the Windows clock conversion  ticks * 1000000000 / freq  wrapped
 * 64 bits every 18446744073 ticks (1844.67 s at 10 MHz). The returned time
 * jumped backwards, the wall-clock RTC stopped pulsing and the SINTRAN clock
 * froze (bug report 16-OCT-2026). This test pins the conversion at the old
 * wrap boundaries at several counter frequencies, and keeps a negative control
 * that proves the old formula really breaks there, so the test is known to
 * detect the bug it guards against.
 *
 * Expected values were computed with exact big-integer arithmetic
 * (python3: ticks * 10**9 // freq).
 */

#include <stdint.h>
#include <stdio.h>

#include "qpc_time.h"

static int failures = 0;

static void check(const char *what, uint64_t freq, uint64_t ticks,
                  uint64_t got, uint64_t expected)
{
    if (got != expected)
    {
        failures++;
        printf("FAIL %s: freq=%llu ticks=%llu got=%llu expected=%llu\n", what,
               (unsigned long long)freq, (unsigned long long)ticks,
               (unsigned long long)got, (unsigned long long)expected);
    }
}

/* The old formula, kept only as the negative control. */
static uint64_t old_formula(uint64_t ticks, uint64_t freq)
{
    return ticks * UINT64_C(1000000000) / freq;
}

struct exact_case
{
    uint64_t freq;
    uint64_t ticks;
    uint64_t expected_ns;
};

/* 2^64 / 1e9 = 18446744073 ticks is where the old formula wrapped. */
#define WRAP UINT64_C(18446744073)

static const struct exact_case exact_cases[] = {
    /* 10 MHz: the usual Windows QPC frequency (and the one in the report) */
    {10000000, WRAP - 1, UINT64_C(1844674407200)},
    {10000000, WRAP, UINT64_C(1844674407300)},
    {10000000, WRAP + 1, UINT64_C(1844674407400)},
    {10000000, 2 * WRAP, UINT64_C(3689348814600)},
    {10000000, 3 * WRAP + 12345, UINT64_C(5534024456400)},
    /* 3.579545 MHz: the ACPI PM timer frequency seen on older Windows */
    {3579545, WRAP - 1, UINT64_C(5153376776098)},
    {3579545, WRAP, UINT64_C(5153376776378)},
    {3579545, WRAP + 1, UINT64_C(5153376776657)},
    {3579545, 2 * WRAP, UINT64_C(10306753552756)},
    {3579545, 3 * WRAP + 12345, UINT64_C(15460133777896)},
    /* 2.441442 MHz: an HPET-derived frequency seen on some boards */
    {2441442, WRAP - 1, UINT64_C(7555675732620)},
    {2441442, WRAP, UINT64_C(7555675733029)},
    {2441442, WRAP + 1, UINT64_C(7555675733439)},
    {2441442, 2 * WRAP, UINT64_C(15111351466059)},
    {2441442, 3 * WRAP + 12345, UINT64_C(22667032255527)},
    /* 24 MHz: a TSC-derived frequency seen on some Windows hosts */
    {24000000, WRAP - 1, UINT64_C(768614336333)},
    {24000000, WRAP, UINT64_C(768614336375)},
    {24000000, WRAP + 1, UINT64_C(768614336416)},
    {24000000, 2 * WRAP, UINT64_C(1537228672750)},
    {24000000, 3 * WRAP + 12345, UINT64_C(2305843523500)},
    /* 1 GHz: ticks are already nanoseconds, full 64-bit range must pass through */
    {1000000000, WRAP, UINT64_C(18446744073)},
    {1000000000, UINT64_C(9223372036854775808), UINT64_C(9223372036854775808)},
    {1000000000, UINT64_C(18446744073709551615), UINT64_C(18446744073709551615)},
    /* 3 GHz: a raw TSC rate, frequency above 1e9 */
    {3000000000, WRAP - 1, UINT64_C(6148914690)},
    {3000000000, WRAP, UINT64_C(6148914691)},
    {3000000000, WRAP + 1, UINT64_C(6148914691)},
    {3000000000, 3 * WRAP + 12345, UINT64_C(18446748188)},
    {3000000000, UINT64_C(9223372036854775808), UINT64_C(3074457345618258602)},
    {3000000000, UINT64_C(18446744073709551615), UINT64_C(6148914691236517205)},
};

static void test_exact_values(void)
{
    size_t n = sizeof(exact_cases) / sizeof(exact_cases[0]);
    for (size_t i = 0; i < n; i++)
    {
        const struct exact_case *c = &exact_cases[i];
        check("exact", c->freq, c->ticks, qpc_ticks_to_ns(c->ticks, c->freq),
              c->expected_ns);
    }
}

/* Time must never go backwards: walk one tick at a time across the first
 * three old wrap boundaries at each frequency. */
static void test_monotonic_across_boundaries(void)
{
    static const uint64_t freqs[] = {10000000, 3579545, 2441442, 24000000,
                                     1000000000, 3000000000};
    size_t nf = sizeof(freqs) / sizeof(freqs[0]);
    for (size_t f = 0; f < nf; f++)
    {
        for (int k = 1; k <= 3; k++)
        {
            uint64_t start = (uint64_t)k * WRAP - 1000;
            uint64_t prev = qpc_ticks_to_ns(start, freqs[f]);
            for (uint64_t t = start + 1; t <= (uint64_t)k * WRAP + 1000; t++)
            {
                uint64_t cur = qpc_ticks_to_ns(t, freqs[f]);
                if (cur < prev)
                {
                    failures++;
                    printf("FAIL monotonic: freq=%llu ticks=%llu went from %llu to %llu\n",
                           (unsigned long long)freqs[f], (unsigned long long)t,
                           (unsigned long long)prev, (unsigned long long)cur);
                    break;
                }
                prev = cur;
            }
        }
    }
}

/* One tick of 100 ns at 10 MHz must advance the result by exactly 100 ns,
 * at the boundary and far past it. */
static void test_tick_step_10mhz(void)
{
    static const uint64_t points[] = {WRAP, 2 * WRAP, UINT64_C(1069911156275000)};
    for (size_t i = 0; i < sizeof(points) / sizeof(points[0]); i++)
    {
        uint64_t a = qpc_ticks_to_ns(points[i], 10000000);
        uint64_t b = qpc_ticks_to_ns(points[i] + 1, 10000000);
        check("step100ns", 10000000, points[i], b - a, 100);
    }
}

/* Negative control: the old formula must still be seen to break at the
 * 10 MHz boundary. If this stops failing, the boundary constants above are
 * wrong and the test no longer guards anything. */
static void test_old_formula_breaks(void)
{
    uint64_t before = old_formula(WRAP, 10000000);
    uint64_t after = old_formula(WRAP + 1, 10000000);
    if (!(after < before))
    {
        failures++;
        printf("FAIL negative control: old formula did not wrap (before=%llu after=%llu)\n",
               (unsigned long long)before, (unsigned long long)after);
    }
    check("old-before", 10000000, WRAP, before, UINT64_C(1844674407300));
    check("old-after", 10000000, WRAP + 1, after, UINT64_C(29));
}

int main(void)
{
    test_exact_values();
    test_monotonic_across_boundaries();
    test_tick_step_10mhz();
    test_old_formula_breaks();

    if (failures)
    {
        printf("test_qpc_time: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_qpc_time: all checks passed\n");
    return 0;
}
