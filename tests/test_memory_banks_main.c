/*
 * test_memory_banks_main.c - Test main for the memory bank / ECC probe suite.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 */

#include <stdio.h>

#include "test_suites.h"

int main(void)
{
    printf("=== memory bank tests ===\n");
    int failed = run_memory_bank_tests();
    if (failed != 0)
    {
        printf("FAILED: %d check(s)\n", failed);
        return 1;
    }
    printf("All memory bank tests passed\n");
    return 0;
}
