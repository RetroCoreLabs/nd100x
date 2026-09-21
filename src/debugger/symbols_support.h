/*
 * symbols_support.h - Symbol lookup helpers: symbol name and source line for an address.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2025 Ronny Hansen
 */

#ifndef SYMBOLS_SUPPORT_H
#define SYMBOLS_SUPPORT_H

#include "../../external/libsymbols/include/symbols.h"
#include "../../external/libdap/libdap/include/dap_server.h"

// Symbol loading and management

/**
 * @brief Look up the symbol covering an address in the given symbol table.
 * @param symtab Symbol table to search; NULL yields NULL.
 * @param address Memory address to look up.
 * @return The symbol name, or NULL when the table is NULL or nothing matches.
 */
const char *debugger_find_symbol_by_address(symbol_table_t *symtab, uint16_t address);

// Helper functions for common symbol operations

/**
 * @brief Look up a symbol name in the loaded a.out symbol table.
 * @param address Memory address to look up.
 * @return The symbol name, or NULL when nothing matches.
 */
const char *debugger_get_symbol_for_address(uint16_t address);

/**
 * @brief Look up the source file and line for an address in the loaded .map/.srcmap
 *        symbol table, for DAP stackTrace source information.
 * @param address Memory address to look up.
 * @param line Receives the source line number; must not be NULL.
 * @return The source file name, or NULL when no map table is loaded, line is NULL,
 *         or the address has no source entry.
 */
const char *debugger_get_source_location(uint16_t address, int *line);


#endif // SYMBOLS_SUPPORT_H
