/*
 * selftest_must_fail.c - Proves the Ethernet II test runner reports failures.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Linked only into test_eth_runner_selftest, which CTest expects to FAIL
 * (WILL_FAIL). If the runner ever stopped reporting a failed check or a test
 * that checks nothing, that CTest entry would turn red.
 */

#include "eth_test.h"

ETH_TEST(Port, SelfTest_FailingCheckIsReported)
{
    CHECK_EQ(1, 2);
}

ETH_TEST(Port, SelfTest_TestWithoutChecksIsReported)
{
}
