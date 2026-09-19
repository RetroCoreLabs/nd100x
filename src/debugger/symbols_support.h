#ifndef SYMBOLS_SUPPORT_H
#define SYMBOLS_SUPPORT_H

#include "../../external/libsymbols/include/symbols.h"
#include "../../external/libdap/libdap/include/dap_server.h"

// Symbol loading and management
const char *find_symbol_by_address(symbol_table_t *symtab, uint16_t address);

// Helper functions for common symbol operations
const char *get_symbol_for_address(uint16_t address);
const char *get_source_location(uint16_t address, int *line);


#endif // SYMBOLS_SUPPORT_H
