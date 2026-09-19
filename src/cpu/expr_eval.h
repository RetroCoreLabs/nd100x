/*
 * expr_eval.h - Expression evaluator for conditional breakpoints
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Evaluates expressions like "PIL == 7", "A > 100", "(STS & 0x00FF) != 0"
 * against CPU register state. Used by the breakpoint manager.
 *
 * Supported tokens:
 *   Registers: STS, D, P, B, L, A, T, X, PIL, EA
 *              PANS, OPR, PGS, PVL, IIC, IID, PID, PIE, CSR, ALD, PES, PGC, PEA
 *   Numbers:   123 (decimal), 0777 (octal), 0xFF (hex)
 *   Operators: == != > < >= <= & | ^ + - * / % ~ ! && ||
 *   Grouping:  ( )
 *   Memory:    [addr] reads word at virtual address
 */

#ifndef EXPR_EVAL_H
#define EXPR_EVAL_H

#include <stdint.h>
#include <stdbool.h>

/* Evaluate a condition expression string.
 * Returns true if the expression evaluates to non-zero.
 * On parse error, sets *error to a static error string and returns false.
 */
bool expr_eval_condition(const char *expr, const char **error);

/* Evaluate an expression and return its numeric value.
 * On parse error, sets *error to a static error string and returns 0.
 */
uint16_t expr_eval_value(const char *expr, const char **error);

#endif /* EXPR_EVAL_H */
