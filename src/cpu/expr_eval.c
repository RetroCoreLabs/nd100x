/*
 * expr_eval.c - Expression evaluator for conditional breakpoints
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Recursive descent parser for breakpoint condition expressions.
 * Evaluates against live CPU register state.
 *
 * Grammar:
 *   expr       = logic_or
 *   logic_or   = logic_and ( ("||" | "or") logic_and )*
 *   logic_and  = bitwise_or ( ("&&" | "and") bitwise_or )*
 *   bitwise_or = bitwise_xor ( "|" bitwise_xor )*
 *   bitwise_xor = bitwise_and ( "^" bitwise_and )*
 *   bitwise_and = equality ( "&" equality )*
 *   equality   = comparison ( ("==" | "=" | "!=") comparison )*
 *   comparison = additive ( (">" | "<" | ">=" | "<=") additive )*
 *   additive   = multiplicative ( ("+" | "-") multiplicative )*
 *   multiplicative = unary ( ("*" | "/" | "%") unary )*
 *   unary      = "~" unary | ("!" | "not") unary | "-" unary | primary
 *   primary    = NUMBER | REGISTER | "(" expr ")" | "[" expr "]"
 */

#include "expr_eval.h"
#include "cpu_types.h"
#include "cpu_protos.h"

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* Parser state */
typedef struct
{
    const char *input;
    const char *pos;
    const char *error;
} Parser;

/* Forward declarations */
static uint16_t parse_expr(Parser *p);
static uint16_t parse_logic_or(Parser *p);
static uint16_t parse_logic_and(Parser *p);
static uint16_t parse_bitwise_or(Parser *p);
static uint16_t parse_bitwise_xor(Parser *p);
static uint16_t parse_bitwise_and(Parser *p);
static uint16_t parse_equality(Parser *p);
static uint16_t parse_comparison(Parser *p);
static uint16_t parse_additive(Parser *p);
static uint16_t parse_multiplicative(Parser *p);
static uint16_t parse_unary(Parser *p);
static uint16_t parse_primary(Parser *p);

/* Skip whitespace */
static void skip_ws(Parser *p)
{
    while (*p->pos == ' ' || *p->pos == '\t')
    {
        p->pos++;
    }
}

/* Check if we hit an error */
static bool has_error(Parser *p)
{
    return p->error != NULL;
}

/* Set error if not already set */
static void set_error(Parser *p, const char *msg)
{
    if (!p->error)
    {
        p->error = msg;
    }
}

/* Try to match a two-character operator */
static bool match2(Parser *p, char c1, char c2)
{
    skip_ws(p);
    if (p->pos[0] == c1 && p->pos[1] == c2)
    {
        p->pos += 2;
        return true;
    }
    return false;
}

/* Try to match a single-character operator, but not if followed by another char */
static bool match1(Parser *p, char c, char not_followed_by)
{
    skip_ws(p);
    if (*p->pos == c && p->pos[1] != not_followed_by)
    {
        p->pos++;
        return true;
    }
    return false;
}

/* Try to match a keyword (case-insensitive), only if followed by non-alnum */
static bool match_keyword(Parser *p, const char *keyword)
{
    skip_ws(p);
    int len = (int)strlen(keyword);
    if (strncasecmp(p->pos, keyword, len) == 0 && !isalnum((unsigned char)p->pos[len]) &&
        p->pos[len] != '_')
    {
        p->pos += len;
        return true;
    }
    return false;
}

/* Try to match a single character exactly */
static bool match_char(Parser *p, char c)
{
    skip_ws(p);
    if (*p->pos == c)
    {
        p->pos++;
        return true;
    }
    return false;
}

/* Look up a register name, return its current value.
 * Returns true if found, false if not a register. */
/* Internal registers by name (PANS, PANC, OPR, ... ECCR), buf upper case. */
static bool lookup_internal_register(const char *buf, uint16_t *value)
{
    if (strcmp(buf, "PANS") == 0)
    {
        *value = gPANS;
        return true;
    }
    if (strcmp(buf, "PANC") == 0)
    {
        *value = gPANC;
        return true;
    }
    if (strcmp(buf, "OPR") == 0)
    {
        *value = gOPR;
        return true;
    }
    if (strcmp(buf, "LMP") == 0)
    {
        *value = gLMP;
        return true;
    }
    if (strcmp(buf, "PGS") == 0)
    {
        *value = gPGS;
        return true;
    }
    if (strcmp(buf, "PVL") == 0)
    {
        *value = gPVL;
        return true;
    }
    if (strcmp(buf, "IIC") == 0)
    {
        *value = gIIC;
        return true;
    }
    if (strcmp(buf, "IID") == 0)
    {
        *value = gIID;
        return true;
    }
    if (strcmp(buf, "PID") == 0)
    {
        *value = gPID;
        return true;
    }
    if (strcmp(buf, "PIE") == 0)
    {
        *value = gPIE;
        return true;
    }
    if (strcmp(buf, "CSR") == 0)
    {
        *value = gCSR;
        return true;
    }
    if (strcmp(buf, "CCL") == 0)
    {
        *value = gCCL;
        return true;
    }
    if (strcmp(buf, "ALD") == 0)
    {
        *value = gALD;
        return true;
    }
    if (strcmp(buf, "PES") == 0)
    {
        *value = gPES;
        return true;
    }
    if (strcmp(buf, "PGC") == 0)
    {
        *value = gPGC;
        return true;
    }
    if (strcmp(buf, "PEA") == 0)
    {
        *value = gPEA;
        return true;
    }
    if (strcmp(buf, "ECCR") == 0)
    {
        *value = gECCR;
        return true;
    }
    return false;
}

static bool lookup_register(const char *name, int len, uint16_t *value)
{
    /* Build a null-terminated copy */
    char buf[16];
    int i;

    if (len <= 0 || len >= (int)sizeof(buf))
    {
        return false;
    }

    for (i = 0; i < len; i++)
    {
        buf[i] = (char)toupper((unsigned char)name[i]);
    }
    buf[len] = '\0';

    /* Main registers (current PIL level) */
    if (strcmp(buf, "A") == 0)
    {
        *value = gA;
        return true;
    }
    if (strcmp(buf, "B") == 0)
    {
        *value = gB;
        return true;
    }
    if (strcmp(buf, "D") == 0)
    {
        *value = gD;
        return true;
    }
    if (strcmp(buf, "T") == 0)
    {
        *value = gT;
        return true;
    }
    if (strcmp(buf, "X") == 0)
    {
        *value = gX;
        return true;
    }
    if (strcmp(buf, "L") == 0)
    {
        *value = gL;
        return true;
    }
    if (strcmp(buf, "P") == 0)
    {
        *value = gPC;
        return true;
    }
    if (strcmp(buf, "PC") == 0)
    {
        *value = gPC;
        return true;
    }
    if (strcmp(buf, "STS") == 0)
    {
        *value = gSTSr;
        return true;
    }
    if (strcmp(buf, "PIL") == 0)
    {
        *value = (uint16_t)gPIL;
        return true;
    }
    if (strcmp(buf, "EA") == 0)
    {
        *value = gEA;
        return true;
    }

    /* Internal registers */
    if (lookup_internal_register(buf, value))
    {
        return true;
    }

    /* Scratch registers U0-U7 (current PIL level) */
    if (buf[0] == 'U' && len == 2 && buf[1] >= '0' && buf[1] <= '7')
    {
        int idx = buf[1] - '0';
        *value = g_reg->reg[gPIL][_U0 + idx];
        return true;
    }

    return false;
}

/* Parse a number literal: decimal, octal (0-prefix), hex (0x-prefix) */
static bool parse_number(Parser *p, uint16_t *value)
{
    skip_ws(p);
    const char *start = p->pos;
    char *end;
    unsigned long v;

    if (!isdigit((unsigned char)*start))
    {
        return false;
    }

    if (start[0] == '0' && (start[1] == 'x' || start[1] == 'X'))
    {
        /* Hex */
        v = strtoul(start, &end, 16);
    }
    else if (start[0] == '0' && isdigit((unsigned char)start[1]))
    {
        /* Octal */
        v = strtoul(start, &end, 8);
    }
    else
    {
        /* Decimal */
        v = strtoul(start, &end, 10);
    }

    if (end == start)
    {
        return false;
    }

    *value = (uint16_t)(v & 0xFFFF);
    p->pos = end;
    return true;
}

/* Parse an identifier (register name) */
static bool parse_identifier(Parser *p, uint16_t *value)
{
    skip_ws(p);
    const char *start = p->pos;

    if (!isalpha((unsigned char)*start) && *start != '_')
    {
        return false;
    }

    while (isalnum((unsigned char)*p->pos) || *p->pos == '_')
    {
        p->pos++;
    }

    int len = (int)(p->pos - start);

    if (lookup_register(start, len, value))
    {
        return true;
    }

    /* Not a known register - restore position and error */
    p->pos = start;
    set_error(p, "unknown register name");
    return false;
}

/* primary = NUMBER | REGISTER | "(" expr ")" | "[" expr "]" */
static uint16_t parse_primary(Parser *p)
{
    uint16_t val;

    if (has_error(p))
    {
        return 0;
    }

    skip_ws(p);

    /* Parenthesized expression */
    if (match_char(p, '('))
    {
        val = parse_expr(p);
        if (!match_char(p, ')'))
        {
            set_error(p, "expected ')'");
        }
        return val;
    }

    /* Memory dereference [addr] */
    if (match_char(p, '['))
    {
        val = parse_expr(p);
        if (!match_char(p, ']'))
        {
            set_error(p, "expected ']'");
        }
        if (!has_error(p))
        {
            return (uint16_t)ReadVirtualMemory(val, false);
        }
        return 0;
    }

    /* Number literal */
    if (parse_number(p, &val))
    {
        return val;
    }

    /* Register name */
    if (parse_identifier(p, &val))
    {
        return val;
    }

    set_error(p, "unexpected token");
    return 0;
}

/* unary = "~" unary | "!" unary | "-" unary | primary */
static uint16_t parse_unary(Parser *p)
{
    if (has_error(p))
    {
        return 0;
    }

    skip_ws(p);

    if (match_char(p, '~'))
    {
        return ~parse_unary(p);
    }

    if (match1(p, '!', '='))
    {
        return parse_unary(p) ? 0 : 1;
    }

    if (match_keyword(p, "not"))
    {
        return parse_unary(p) ? 0 : 1;
    }

    if (match1(p, '-', '\0'))
    {
        /* Check it's not a negative sign before a number handled elsewhere */
        return (uint16_t)(-(int16_t)parse_unary(p));
    }

    return parse_primary(p);
}

/* multiplicative = unary ( ("*" | "/" | "%") unary )* */
static uint16_t parse_multiplicative(Parser *p)
{
    uint16_t left = parse_unary(p);
    if (has_error(p))
    {
        return 0;
    }

    for (;;)
    {
        skip_ws(p);
        if (match_char(p, '*'))
        {
            left = left * parse_unary(p);
        }
        else if (match_char(p, '/'))
        {
            uint16_t right = parse_unary(p);
            if (right == 0)
            {
                set_error(p, "division by zero");
                return 0;
            }
            left = left / right;
        }
        else if (match_char(p, '%'))
        {
            uint16_t right = parse_unary(p);
            if (right == 0)
            {
                set_error(p, "modulo by zero");
                return 0;
            }
            left = left % right;
        }
        else
        {
            break;
        }
        if (has_error(p))
        {
            return 0;
        }
    }
    return left;
}

/* additive = multiplicative ( ("+" | "-") multiplicative )* */
static uint16_t parse_additive(Parser *p)
{
    uint16_t left = parse_multiplicative(p);
    if (has_error(p))
    {
        return 0;
    }

    for (;;)
    {
        skip_ws(p);
        if (match_char(p, '+'))
        {
            left = left + parse_multiplicative(p);
        }
        else if (match_char(p, '-'))
        {
            left = left - parse_multiplicative(p);
        }
        else
        {
            break;
        }
        if (has_error(p))
        {
            return 0;
        }
    }
    return left;
}

/* comparison = additive ( (">" | "<" | ">=" | "<=") additive )* */
static uint16_t parse_comparison(Parser *p)
{
    uint16_t left = parse_additive(p);
    if (has_error(p))
    {
        return 0;
    }

    for (;;)
    {
        skip_ws(p);
        if (match2(p, '>', '='))
        {
            left = (left >= parse_additive(p)) ? 1 : 0;
        }
        else if (match2(p, '<', '='))
        {
            left = (left <= parse_additive(p)) ? 1 : 0;
        }
        else if (match1(p, '>', '='))
        {
            left = (left > parse_additive(p)) ? 1 : 0;
        }
        else if (match1(p, '<', '='))
        {
            left = (left < parse_additive(p)) ? 1 : 0;
        }
        else
        {
            break;
        }
        if (has_error(p))
        {
            return 0;
        }
    }
    return left;
}

/* equality = comparison ( ("==" | "!=") comparison )* */
static uint16_t parse_equality(Parser *p)
{
    uint16_t left = parse_comparison(p);
    if (has_error(p))
    {
        return 0;
    }

    for (;;)
    {
        skip_ws(p);
        if (match2(p, '=', '='))
        {
            left = (left == parse_comparison(p)) ? 1 : 0;
        }
        else if (match1(p, '=', '='))
        {
            left = (left == parse_comparison(p)) ? 1 : 0;
        }
        else if (match2(p, '!', '='))
        {
            left = (left != parse_comparison(p)) ? 1 : 0;
        }
        else
        {
            break;
        }
        if (has_error(p))
        {
            return 0;
        }
    }
    return left;
}

/* bitwise_and = equality ( "&" equality )* */
static uint16_t parse_bitwise_and(Parser *p)
{
    uint16_t left = parse_equality(p);
    if (has_error(p))
    {
        return 0;
    }

    for (;;)
    {
        skip_ws(p);
        /* & but not && */
        if (match1(p, '&', '&'))
        {
            left = left & parse_equality(p);
        }
        else
        {
            break;
        }
        if (has_error(p))
        {
            return 0;
        }
    }
    return left;
}

/* bitwise_xor = bitwise_and ( "^" bitwise_and )* */
static uint16_t parse_bitwise_xor(Parser *p)
{
    uint16_t left = parse_bitwise_and(p);
    if (has_error(p))
    {
        return 0;
    }

    for (;;)
    {
        skip_ws(p);
        if (match_char(p, '^'))
        {
            left = left ^ parse_bitwise_and(p);
        }
        else
        {
            break;
        }
        if (has_error(p))
        {
            return 0;
        }
    }
    return left;
}

/* bitwise_or = bitwise_xor ( "|" bitwise_xor )* */
static uint16_t parse_bitwise_or(Parser *p)
{
    uint16_t left = parse_bitwise_xor(p);
    if (has_error(p))
    {
        return 0;
    }

    for (;;)
    {
        skip_ws(p);
        /* | but not || */
        if (match1(p, '|', '|'))
        {
            left = left | parse_bitwise_xor(p);
        }
        else
        {
            break;
        }
        if (has_error(p))
        {
            return 0;
        }
    }
    return left;
}

/* logic_and = bitwise_or ( "&&" bitwise_or )* */
static uint16_t parse_logic_and(Parser *p)
{
    uint16_t left = parse_bitwise_or(p);
    if (has_error(p))
    {
        return 0;
    }

    for (;;)
    {
        skip_ws(p);
        if (match2(p, '&', '&'))
        {
            uint16_t right = parse_bitwise_or(p);
            left = (left && right) ? 1 : 0;
        }
        else if (match_keyword(p, "and"))
        {
            uint16_t right = parse_bitwise_or(p);
            left = (left && right) ? 1 : 0;
        }
        else
        {
            break;
        }
        if (has_error(p))
        {
            return 0;
        }
    }
    return left;
}

/* logic_or = logic_and ( ("||" | "or") logic_and )* */
static uint16_t parse_logic_or(Parser *p)
{
    uint16_t left = parse_logic_and(p);
    if (has_error(p))
    {
        return 0;
    }

    for (;;)
    {
        skip_ws(p);
        if (match2(p, '|', '|'))
        {
            uint16_t right = parse_logic_and(p);
            left = (left || right) ? 1 : 0;
        }
        else if (match_keyword(p, "or"))
        {
            uint16_t right = parse_logic_and(p);
            left = (left || right) ? 1 : 0;
        }
        else
        {
            break;
        }
        if (has_error(p))
        {
            return 0;
        }
    }
    return left;
}

/* expr = logic_or */
static uint16_t parse_expr(Parser *p)
{
    return parse_logic_or(p);
}

/*
 * Public API
 */

uint16_t expr_eval_value(const char *expr, const char **error)
{
    Parser p;
    uint16_t result;

    if (!expr || !*expr)
    {
        if (error)
        {
            *error = "empty expression";
        }
        return 0;
    }

    p.input = expr;
    p.pos = expr;
    p.error = NULL;

    result = parse_expr(&p);

    /* Check for trailing garbage */
    skip_ws(&p);
    if (!has_error(&p) && *p.pos != '\0')
    {
        set_error(&p, "unexpected characters after expression");
    }

    if (error)
    {
        *error = p.error;
    }

    return has_error(&p) ? 0 : result;
}

bool expr_eval_condition(const char *expr, const char **error)
{
    uint16_t val = expr_eval_value(expr, error);
    if (error && *error)
    {
        return false;
    }
    return val != 0;
}
