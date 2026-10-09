/*
 * eth_test_main.c - Runner for the Ethernet II port tests.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Usage: test_ethernet [--list] [--explicit] [Class__Method ...]
 *   --list      print every registered test as Class__Method flags file:line, then exit
 *   --explicit  also run tests marked ETH_TEST_EXPLICIT
 *   names       run only the named tests
 * Exit status: 0 when every run test passed, 1 otherwise.
 */

#include "eth_test.h"

#include <stdlib.h>
#include <string.h>

#define MAX_TESTS 2048

static const EthTestCase *s_tests[MAX_TESTS];
static int s_test_count;
static int s_check_failures;
static int s_checks;

void eth_test_register(const EthTestCase *tc)
{
    if (s_test_count >= MAX_TESTS)
    {
        fprintf(stderr, "eth_test: more than %d tests, raise MAX_TESTS\n", MAX_TESTS);
        exit(2);
    }
    s_tests[s_test_count] = tc;
    s_test_count++;
}

void eth_test_check(bool ok, const char *expr, const char *file, int line)
{
    s_checks++;
    if (!ok)
    {
        s_check_failures++;
        printf("    FAILED %s:%d: %s\n", file, line, expr);
    }
}

void eth_test_check_eq(long long actual, long long expected, const char *expr, const char *file,
                       int line)
{
    s_checks++;
    if (actual != expected)
    {
        s_check_failures++;
        printf("    FAILED %s:%d: %s (actual %lld / 0x%llx, expected %lld / 0x%llx)\n", file, line,
               expr, actual, (unsigned long long)actual, expected, (unsigned long long)expected);
    }
}

static int compare_tests(const void *a, const void *b)
{
    const EthTestCase *ta = *(const EthTestCase *const *)a;
    const EthTestCase *tb = *(const EthTestCase *const *)b;
    int c = strcmp(ta->cs_class, tb->cs_class);
    return (c != 0) ? c : strcmp(ta->cs_method, tb->cs_method);
}

static bool is_selected(const EthTestCase *tc, int argc, char **argv)
{
    bool any_name = false;
    for (int i = 1; i < argc; i++)
    {
        char name[256];
        if (argv[i][0] == '-')
        {
            continue;
        }
        any_name = true;
        (void)snprintf(name, sizeof name, "%s__%s", tc->cs_class, tc->cs_method);
        if (strcmp(name, argv[i]) == 0)
        {
            return true;
        }
    }
    return !any_name;
}

int main(int argc, char **argv)
{
    bool list = false;
    bool explicit_too = false;
    int run = 0;
    int failed = 0;
    int ignored = 0;
    int skipped_explicit = 0;
    int xfail = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--list") == 0)
        {
            list = true;
        }
        else if (strcmp(argv[i], "--explicit") == 0)
        {
            explicit_too = true;
        }
    }

    qsort(s_tests, (size_t)s_test_count, sizeof s_tests[0], compare_tests);

    if (list)
    {
        for (int i = 0; i < s_test_count; i++)
        {
            printf("%s__%s %u %s:%d\n", s_tests[i]->cs_class, s_tests[i]->cs_method,
                   s_tests[i]->flags, s_tests[i]->file, s_tests[i]->line);
        }
        return 0;
    }

    for (int i = 0; i < s_test_count; i++)
    {
        const EthTestCase *tc = s_tests[i];
        int before = s_check_failures;
        int checks_before = s_checks;

        if (!is_selected(tc, argc, argv))
        {
            continue;
        }
        if ((tc->flags & ETH_TEST_IGNORE) != 0u)
        {
            printf("IGNORED %s__%s\n", tc->cs_class, tc->cs_method);
            ignored++;
            continue;
        }
        if (((tc->flags & ETH_TEST_EXPLICIT) != 0u) && !explicit_too)
        {
            skipped_explicit++;
            continue;
        }
        tc->fn();
        run++;
        if (s_checks == checks_before)
        {
            /* A test that checks nothing proves nothing (plan rule R5). */
            printf("    FAILED %s:%d: test made no checks\n", tc->file, tc->line);
            s_check_failures++;
        }
        if ((tc->flags & ETH_TEST_EXPECT_FAIL) != 0u)
        {
            if (s_check_failures != before)
            {
                xfail++;
                printf("XFAIL %s__%s\n", tc->cs_class, tc->cs_method);
            }
            else
            {
                failed++;
                printf("UNEXPECTED-PASS %s__%s (fails in RetroCore)\n", tc->cs_class, tc->cs_method);
            }
            continue;
        }
        if (s_check_failures != before)
        {
            failed++;
            printf("FAIL %s__%s\n", tc->cs_class, tc->cs_method);
        }
        else
        {
            printf("PASS %s__%s\n", tc->cs_class, tc->cs_method);
        }
    }

    printf("test_ethernet: %d run, %d failed, %d expected failures, %d ignored, %d explicit not run, "
           "%d checks\n",
           run, failed, xfail, ignored, skipped_explicit, s_checks);
    return (failed != 0) ? 1 : 0;
}
