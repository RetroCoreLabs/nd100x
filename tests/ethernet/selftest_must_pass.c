/*
 * selftest_must_pass.c - Proves the Ethernet II test runner passes good tests.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 */

#include "eth_test.h"

ETH_TEST(Port, SelfTest_PassingChecks)
{
    CHECK(1 + 1 == 2);
    CHECK_EQ(0x5473, 052163);
}
