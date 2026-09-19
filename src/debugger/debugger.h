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

/* Opaque DAP server handle (defined by libdap's dap_server.h). */
struct DAPServer;

/**
 * @brief Start the debugger thread, which creates the DAP server and runs its
 *        message loop until stop_debugger_thread() or CPU shutdown.
 */
void start_debugger(void);

/**
 * @brief Create the DAP server on a TCP transport, register every request
 *        callback (launch, continue, stackTrace, variables, readMemory, ...),
 *        set the supported capabilities and start listening.
 * @param port TCP port to listen on.
 * @return 0 on success, non-zero on error (-1 when the server cannot be created,
 *         otherwise the dap_server_start() error code).
 */
int ndx_server_init(int port);

/**
 * @brief Send a DAP "terminated" event to the client and terminate the server.
 * @param sig Signal number that triggered the termination; ignored.
 */
void ndx_server_terminate(int);

/**
 * @brief Ask the debugger thread to leave its message loop and join it.
 */
void stop_debugger_thread(void);

/**
 * @brief Return the return address recorded for the current stack frame.
 * @return The return address of the current frame, or -1 when no frame is tracked.
 */
int32_t find_stack_return_address(void);

/**
 * @brief Record the entry point reached by a JPL call in the current stack frame.
 * @param ea Effective address the JPL jumped to.
 */
void debugger_update_jpl_entrypoint(uint16_t);

/**
 * @brief Maintain the debugger's stack frame list from the instruction the CPU is
 *        about to execute, recognising JPL, EXIT, ENTR, LEAVE and ELEAV.
 * @param pc Program counter of the instruction.
 * @param operand The instruction word already fetched at pc.
 */
void debugger_build_stack_trace(uint16_t, uint16_t);

/**
 * @brief Run the CPU for one DAP step request (next, stepIn or stepOut).
 * @param server The DAP server instance holding the step command context.
 * @param step_type Which step to perform: STEP_OVER, STEP_IN or STEP_OUT.
 * @return 0 on success, -1 on failure (including a NULL server).
 */
int step_cpu(struct DAPServer *, StepType);

/**
 * @brief Format the memory range covered by a page table entry for the DAP
 *        variables view. Decoding is not implemented, so this returns an empty
 *        string in a static buffer.
 * @param PTe Page table entry; ignored.
 * @return Pointer to a static empty string buffer.
 */
char *GetPageTableMemoryRange(uint32_t);

/**
 * @brief Fill one frame of the DAP stackTrace response: id, name, source file and
 *        line looked up from the symbol table, and presentation hint.
 * @param server The DAP server instance holding the stackTrace command context.
 * @param frame_index Index of the frame in the response frame array.
 * @param frame_id Unique id given to the frame.
 * @param memory_reference Address used as the frame's code location.
 * @param entry_point Entry point address of the routine owning the frame.
 */
void update_stack_frame(struct DAPServer *server, int frame_index, int frame_id,
                        uint16_t memory_reference, uint16_t entry_point);

/**
 * @brief Stop the DAP server and free it.
 * @return 0 on success, -1 when no server is running.
 */
int ndx_server_stop(void);

/**
 * @brief Handle one key typed on the emulator console while the debugger is
 *        active: 'q' shuts the CPU down, '.' prints PC and run mode, space
 *        single-steps, 'd' disassembles ten instructions from PC.
 * @param c The key character.
 */
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
