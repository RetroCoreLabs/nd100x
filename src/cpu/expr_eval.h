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

/**
 * @brief Evaluate a breakpoint condition string against the CPU registers and report truth.
 * @param expr Condition text, for example "PIL == 7"; empty or NULL counts as a parse error.
 * @param error If non-NULL, receives a static error string, or NULL when the parse succeeded.
 * @return true when the expression evaluates to a non-zero value, false on zero or parse error.
 */
bool expr_eval_condition(const char *expr, const char **error);

/**
 * @brief Evaluate an expression against the CPU registers and return its 16-bit value.
 * @param expr Expression text; empty or NULL sets "empty expression" and yields 0.
 * @param error If non-NULL, receives a static error string, or NULL when the parse succeeded.
 * @return The value of the expression, or 0 on any parse error including trailing characters.
 */
uint16_t expr_eval_value(const char *expr, const char **error);

#endif /* EXPR_EVAL_H */
