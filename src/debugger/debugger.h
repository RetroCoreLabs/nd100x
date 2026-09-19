/*
 * debugger.h - DAP debugger interface: server start/stop and JSON query functions.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2025 Ronny Hansen
 */

#ifndef DEBUGGER_H
#define DEBUGGER_H

#include <stdint.h>

#ifdef WITH_DEBUGGER
#include "symbols.h"
#endif

/* Native thread entry uses pthreads on both POSIX and Windows (via
 * winpthreads from MinGW-w64). The THREAD_* macros are kept for the
 * debugger_thread return-statement so the file reads uniformly. */
#define THREAD_FUNC        void *
#define THREAD_RETURN(val) return (void *)(uintptr_t)(val)


/// @brief Variable in the current stack frame
/// TODO: Not yet implemented and supported by the DAP
typedef struct
{
    /// @brief Name of the variable
    char *name;
    /// @brief Value of the variable
    char *value;
    /// @brief Type of the variable
    char *type;
} Variable;

typedef struct
{
    /// @brief Number of local variables in the current stack frame
    int number_of_variables;
    /// @brief Array of local variables
    Variable *variables;
} LocalVariables;

// Define the maximum number of stack frames we'll track
#define MAX_STACK_FRAMES 20

// Static variables to maintain stack frame state
typedef struct
{
    /// @brief Program counter of the stack frame
    uint16_t pc;
    /// @brief Operand that made the call
    uint16_t operand;
    /// @brief Return address of the stack frame (where it was called from)
    uint16_t return_address;
    /// @brief Entry point of the stack frame
    uint16_t entry_point;
    /// @brief B register value for this frame (used for variable access)
    uint16_t b_reg;

    /// @brief Local variables in the current stack frame
    LocalVariables variables;
} StackFrame;


#ifdef WITH_DEBUGGER
typedef struct
{
    StackFrame frames[MAX_STACK_FRAMES];
    int current_frame;
    int frame_count;

} StackTrace;


typedef struct
{
    symbol_table_t *symbol_table_map;
    symbol_table_t *symbol_table_aout;
    symbol_table_t *symbol_table_stabs;
    symbol_debug_info_t *debug_info; /* C-level debug info from .srcmap */
} SymbolTables;
#endif


/// @brief Step types for the debugger. Maps to DAP
typedef enum
{

    /// @brief DAP "next" command
    STEP_OVER,

    /// @brief DAP "stepIn" command
    STEP_IN,

    /// @brief DAP "stepOut" command
    STEP_OUT,
} StepType;


typedef enum
{
    SYMBOL_TYPE_MAP,
    SYMBOL_TYPE_AOUT,
    SYMBOL_TYPE_STABS,
} SymbolType;

// Function declarations
void start_debugger(void);
int ndx_server_init(int port);
int ndx_server_stop(void);
void debugger_kbd_input(char c);

#ifdef __EMSCRIPTEN__
/* In-process debugger API for the WASM frontend (debugger.c). The JSON
 * strings are owned by the debugger and valid until the next call. */
const char *dbg_get_scopes_json(void);
const char *dbg_get_variables_json(int scope_id);
const char *dbg_get_stack_trace_json(void);
const char *dbg_get_threads_json(void);
int dbg_step_in(void);
int dbg_step_over(void);
int dbg_step_out(void);
#endif

#endif
