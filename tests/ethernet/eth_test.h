/*
 * eth_test.h - Minimal test framework for the Ethernet II port.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * A test is defined with ETH_TEST(CsClass, CsMethod) where CsClass and
 * CsMethod are the RetroCore NUnit class and method the test is ported from
 * (tools/ethport/test_parity.py matches them one-for-one). The macro
 * registers the test before main() runs (GCC/Clang constructor attribute),
 * so a defined test cannot be left out of the run by forgetting a table
 * entry. Tests that are not ports of a RetroCore test use the class name
 * Port (ETH_TEST(Port, Name)).
 *
 * Markers on the line of the ETH_TEST, as in RetroCore:
 *   cs-ignore: <reason>   the C# test is [Ignore]; reported, not run
 *   cs-explicit           the C# test is [Explicit]; run only with --explicit
 *   cs-cases: N           the C# method has N [TestCase] rows; all N are ported
 *   cs-fails-in-retrocore the C# test fails against RetroCore (ETH_TEST_EXPECT_FAIL)
 * The markers are read from the source by test_parity.py; at run time the
 * flags below carry the same information.
 */

#ifndef ETH_TEST_H
#define ETH_TEST_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef void (*EthTestFn)(void);

typedef struct
{
    const char *cs_class;
    const char *cs_method;
    const char *file;
    int line;
    EthTestFn fn;
    unsigned flags;
} EthTestCase;

#define ETH_TEST_IGNORE      0x01u
#define ETH_TEST_EXPLICIT    0x02u
/* The RetroCore original of this test FAILS against RetroCore itself
 * (docs/ethernet-port/RETROCORE-RESULTS.csv). The port keeps the C#
 * behaviour, so the C test is expected to fail too: it still runs, a failure
 * is reported as XFAIL, and an unexpected pass is an error. Marker on the
 * ETH_TEST line: cs-fails-in-retrocore. */
#define ETH_TEST_EXPECT_FAIL 0x04u

/**
 * @brief Register a test case (called from the constructor ETH_TEST makes).
 * @param tc  test description; must stay valid for the whole run
 */
void eth_test_register(const EthTestCase *tc);

/**
 * @brief Record one check result; prints the failure with file and line.
 * @param ok    the check's outcome
 * @param expr  the checked expression as text
 * @param file  source file of the check
 * @param line  source line of the check
 */
void eth_test_check(bool ok, const char *expr, const char *file, int line);

/**
 * @brief Record an equality check of two integers; prints both on failure.
 */
void eth_test_check_eq(long long actual, long long expected, const char *expr, const char *file,
                       int line);

#define ETH_TEST_FLAGS(cs_class, cs_method, flags)                                                 \
    static void test_##cs_class##__##cs_method(void);                                             \
    static const EthTestCase s_tc_##cs_class##__##cs_method = {                                    \
        #cs_class, #cs_method, __FILE__, __LINE__, test_##cs_class##__##cs_method, (flags)};       \
    __attribute__((constructor)) static void reg_##cs_class##__##cs_method(void)                   \
    {                                                                                              \
        eth_test_register(&s_tc_##cs_class##__##cs_method);                                        \
    }                                                                                              \
    static void test_##cs_class##__##cs_method(void)

#define ETH_TEST(cs_class, cs_method) ETH_TEST_FLAGS(cs_class, cs_method, 0u)

#define CHECK(cond) eth_test_check((cond) ? true : false, #cond, __FILE__, __LINE__)
#define CHECK_EQ(actual, expected)                                                                 \
    eth_test_check_eq((long long)(actual), (long long)(expected), #actual " == " #expected,       \
                      __FILE__, __LINE__)

#endif /* ETH_TEST_H */
