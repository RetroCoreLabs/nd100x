#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef __EMSCRIPTEN__
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <stdatomic.h>
#endif

/* DAP headers first. (This order once avoided a clash between the system
   ulong typedef and cpu_types.h's; cpu_types.h no longer defines ulong.) */
#ifdef WITH_DEBUGGER
#include "../../external/libdap/libdap/include/dap_server.h"
#include "../../external/libdap/libdap/include/dap_server_cmds.h"
#include "../../external/libdap/libdap/include/dap_protocol.h"
#include "../../external/libsymbols/include/symbols.h"
#include "../../external/libsymbols/include/aout.h"
#endif

#include "debugger.h"
#include "../cpu/cpu_types.h"
#include "../cpu/cpu_protos.h"
#include "../cpu/expr_eval.h"

#ifdef WITH_DEBUGGER

#include "symbols_support.h"
#include "machine_types.h"
#include "machine_protos.h"
#include "devices_protos.h"

#include "ndlib_types.h"
#include "ndlib_protos.h"

#include "debugger_protos.h"

/* ================================================================
   WASM: Stubs for libdap functions (we include headers for types
   but do not link the library for WASM builds)
   ================================================================ */
#ifdef __EMSCRIPTEN__
#include <emscripten.h>

DAPServer *dap_server_create(const DAPServerConfig *c) { (void)c; return NULL; }
void dap_server_free(DAPServer *s) { if(s) free(s); }
int dap_server_start(DAPServer *s) { (void)s; return 0; }
int dap_server_stop(DAPServer *s) { (void)s; return 0; }
int dap_server_run(DAPServer *s) { (void)s; return 0; }
void dap_server_terminate(DAPServer *s, int sig) { (void)s; (void)sig; }
int dap_server_register_command_callback(DAPServer *s, DAPCommandType cmd, DAPCommandCallback cb)
{
    if(s && cmd >= 0 && cmd < DAP_CMD_MAX)
        s->command_callbacks[cmd] = cb;
    return 0;
}
int dap_server_send_response(DAPServer *s, DAPCommandType cmd, int seq, int req_seq, bool success, cJSON *body)
{
    (void)s; (void)cmd; (void)seq; (void)req_seq; (void)success; (void)body; return 0;
}
int dap_server_send_event(DAPServer *s, const char *ev, cJSON *body) { (void)s; (void)ev; (void)body; return 0; }
int dap_server_send_output(DAPServer *s, const char *msg) { (void)s; printf("%s", msg); return 0; }
int dap_server_send_output_category(DAPServer *s, DAPOutputCategory cat, const char *msg)
{
    (void)s; (void)cat; printf("%s", msg); return 0;
}
int dap_server_send_stopped_event(DAPServer *s, const char *reason, const char *desc)
{
    (void)s; (void)reason; (void)desc; return 0;
}
int dap_server_send_stopped_event_ex(DAPServer *s, const char *reason, const char *desc,
                                      const int *hit_bp_ids, int hit_bp_count)
{
    (void)s; (void)reason; (void)desc; (void)hit_bp_ids; (void)hit_bp_count; return 0;
}
int dap_server_send_process_event(DAPServer *s, const char *name, int pid, bool local, const char *method)
{
    (void)s; (void)name; (void)pid; (void)local; (void)method; return 0;
}
int dap_server_send_thread_event(DAPServer *s, const char *reason, int tid) { (void)s; (void)reason; (void)tid; return 0; }
int dap_server_send_terminated_event(DAPServer *s, bool restart) { (void)s; (void)restart; return 0; }
int dap_server_send_exited_event(DAPServer *s, int code) { (void)s; (void)code; return 0; }
int dap_server_set_capabilities(DAPServer *s, ...) { (void)s; return 0; }

char *base64_encode(const uint8_t *data, size_t len) { (void)data; (void)len; return strdup(""); }

#endif /* __EMSCRIPTEN__ */

// Forward declarations for CPU breakpoint functions

// Define scope IDs
#define SCOPE_ID_LOCALS 1000
#define SCOPE_ID_REGISTERS 1001
#define SCOPE_ID_LEVELS 1002

#define SCOPE_ID_INTERNAL_REGISTERS_READ 1010
#define SCOPE_ID_INTERNAL_REGISTERS_WRITE 1011

#define SCOPE_ID_STATUS_FLAGS 1100

#define SCOPE_ID_MEM_MMS 1200
#define SCOPE_ID_MEM_PT 1201
#define SCOPE_ID_MEM_APT 1202

// Per-PIL register sub-scopes. The "Interrupt levels" scope (SCOPE_ID_LEVELS)
// emits one expandable child per interrupt level whose variablesReference is
// SCOPE_ID_PIL_BASE+pil. cmd_variables() handles refs in this range by
// returning the full register bank for that PIL (STS, P, B, L, A, T, X, D).
#define SCOPE_ID_PIL_BASE 1300
#define SCOPE_ID_PIL_END  1315 // inclusive: 1300..1315 = PIL 0..15

#define NUM_SCOPES 8 // Locals, Registers, Levels, Internal read, Internal write, status flags, memory PT, memory APT

// DAP server instance
DAPServer *g_dap_server;

// Global symbol table

static SymbolTables s_symbol_tables;

// Structure to hold the stack trace information
static StackTrace s_stack_trace;

// Source reference mapping for non-disk sources
typedef struct {
    int sourceReference;
    char *filepath;
    char *content;  // Cached content
} SourceReferenceMap;

static SourceReferenceMap *source_refs = NULL;
static int source_ref_count = 0;

// Console I/O capture support
#define MAX_CONSOLE_CAPTURES 8
#define CONSOLE_RING_SIZE 1024

typedef struct {
    Device *device;                         /* Terminal device */
    int terminal_address;                   /* IOX address */
    CharacterDeviceOutputFunc original_output; /* Saved original callback */
    DAPServer *server;                      /* DAP server for sending events */
    volatile unsigned char ring[CONSOLE_RING_SIZE]; /* Ring buffer for captured chars */
    volatile int ring_head;                 /* Write position (CPU thread) */
    volatile int ring_tail;                 /* Read position (debugger thread) */
} ConsoleCapture;

static ConsoleCapture console_captures[MAX_CONSOLE_CAPTURES];
static int console_capture_count = 0;

static void flush_console_output(void);

/*
 * Resolve a source filename to a full path using debugger_state.
 * Tries: exact path, source_path directory, then each source_paths entry.
 * Returns a strdup'd full path, or strdup(file) if unresolved.
 * Caller must free the result.
 */
static char *
resolve_source_path(DAPServer *server, const char *file)
{
    struct stat st;

    if (!file)
        return NULL;

    /* Already an absolute path that exists? */
    if (file[0] == '/' && stat(file, &st) == 0)
        return strdup(file);

    /* Extract basename for searching */
    const char *basename = strrchr(file, '/');
    basename = basename ? basename + 1 : file;

    /* Try the main source_path directory */
    if (server->debugger_state.source_path) {
        const char *dir = server->debugger_state.source_path;
        const char *slash = strrchr(dir, '/');
        if (slash) {
            char buf[4096];
            int dirlen = (int)(slash - dir);
            snprintf(buf, sizeof(buf), "%.*s/%s", dirlen, dir, basename);
            if (stat(buf, &st) == 0)
                return strdup(buf);
        }
    }

    /* Try each source_paths entry with full relative path first */
    for (int i = 0; i < server->debugger_state.source_paths_count; i++) {
        char buf[4096];
        snprintf(buf, sizeof(buf), "%s/%s", server->debugger_state.source_paths[i], file);
        if (stat(buf, &st) == 0)
            return strdup(buf);
    }

    /* Try each source_paths entry with basename only */
    for (int i = 0; i < server->debugger_state.source_paths_count; i++) {
        char buf[4096];
        snprintf(buf, sizeof(buf), "%s/%s", server->debugger_state.source_paths[i], basename);
        if (stat(buf, &st) == 0)
            return strdup(buf);
    }

    /* Unresolved - return as-is */
    return strdup(file);
}

#ifdef __EMSCRIPTEN__
/* WASM: no threads, no atomics - no debugger thread to stop */
#else
/* Native (POSIX and Windows via winpthreads) - unified pthread + C11 atomics. */
#include <stdatomic.h>
#include <pthread.h>
static pthread_t s_debugger_thread;

// Add atomic flag for thread termination
static atomic_bool debugger_thread_should_exit = false;
#endif /* __EMSCRIPTEN__ */

#include "debugger_protos.h"
#include "debugger.h"

#ifndef __EMSCRIPTEN__
// Signal handler for the debugger thread
static void debugger_signal_handler(int sig)
{
    if (sig == SIGINT)
    {
        atomic_store(&debugger_thread_should_exit, true);
    }
}

void *debugger_thread(void *arg)
{
    (void)arg;
#ifdef _WIN32
    // Windows: plain signal() - no sigaction on MinGW CRT.
    signal(SIGINT, debugger_signal_handler);
#else
    // POSIX: sigaction for per-thread handling.
    struct sigaction sa;
    sa.sa_handler = debugger_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
#endif

    int port = gDebuggerPort > 0 ? gDebuggerPort : 4711;
    int ret = ndx_server_init(port);
    if (ret != 0)
    {
        LOG(LOG_CAT_DAP, LOG_ERROR, "Failed to initialize DAP server\n");
        THREAD_RETURN(0);
    }

    LOG(LOG_CAT_DAP, LOG_INFO, "NDX debugger listening on port %d...\n", port);
    LOG(LOG_CAT_DAP, LOG_INFO, "Press Ctrl+C to exit\n");

    // Run the server's message processing loop with periodic checks for exit
    while (g_dap_server->is_running)
    {
        if (atomic_load(&debugger_thread_should_exit))
            break;

        if (dap_server_run(g_dap_server) != 0)
        {
            LOG(LOG_CAT_DAP, LOG_ERROR, "Error: Server message loop failed.\n");
            break;
        }
        // Small sleep to prevent busy waiting
        sleep_ms(10); // Portable (POSIX nanosleep / Windows Sleep)
    }

    // Make sure CPU exits
    set_cpu_run_mode(CPU_SHUTDOWN);

    // Stop the server and transport
    dap_server_stop(g_dap_server);

    // Clean up before exiting
    dap_server_free(g_dap_server);
    g_dap_server = NULL;
    THREAD_RETURN(0);
}

void start_debugger(void)
{
    // Start the debugger thread via pthreads (libpthread on POSIX,
    // winpthreads on MinGW-w64 under Windows).
    pthread_create(&s_debugger_thread, NULL, debugger_thread, NULL);
}

/// @brief Terminate the DAP server
/// @param exit_code
void ndx_server_terminate(int sig)
{
    (void)sig;

    // Sends a terminated event to the client and terminate the DAP server
    dap_server_terminate(g_dap_server, 0);
}

/// @brief Stop the DAP server thread
void stop_debugger_thread(void)
{
    // Signal the thread to exit
    atomic_store(&debugger_thread_should_exit, true);

    // Wait for the thread to finish
    if (s_debugger_thread)
    {
        pthread_join(s_debugger_thread, NULL);
        s_debugger_thread = 0;
    }
}

#else /* __EMSCRIPTEN__ */

/* WASM: Initialize DAP server struct in-process, no thread/transport */
static int ndx_server_init_wasm(void);

void start_debugger(void)
{
    ndx_server_init_wasm();
}

void ndx_server_terminate(int sig)
{
    (void)sig;
}

void stop_debugger_thread(void)
{
    /* no thread to stop */
}

#endif /* __EMSCRIPTEN__ */

const char *cpuStopReasonToString(CpuStopReason r)
{
    switch (r)
    {
    case STOP_REASON_STEP:
        return "step";
    case STOP_REASON_BREAKPOINT:
        return "breakpoint";
    case STOP_REASON_EXCEPTION:
        return "exception";
    case STOP_REASON_PAUSE:
        return "pause";
    case STOP_REASON_ENTRY:
        return "entry";
    case STOP_REASON_GOTO:
        return "goto";
    case STOP_REASON_FUNCTION_BREAKPOINT:
        return "function breakpoint";
    case STOP_REASON_DATA_BREAKPOINT:
        return "data breakpoint";
    case STOP_REASON_INSTRUCTION_BREAKPOINT:
        return "instruction breakpoint";
    default:
        return "pause";
    }
}

static int cmd_wait_for_debugger(DAPServer *server)
{
    (void)server;
#ifdef __EMSCRIPTEN__
    /* WASM: single-threaded, just request pause */
    (void)server;
    set_debugger_request_pause(true);
    set_debugger_control_granted(true);
    return 0;
#else
    int cnt = 0;
    int max_cnt = 10000; // 10000 * 1ms = 10s
    // Tell CPU thread we want it to pause
    if (get_cpu_run_mode() == CPU_SHUTDOWN)
        return -1; // CPU is shutting down, no need to pause

    set_debugger_request_pause(true);

    // Now wait until CPU acknowledges and grants control
    while (!get_debugger_control_granted())
    {
        sleep_ms(1); // small sleep to avoid busy spin (1ms)
        cnt++;
        if (cnt > max_cnt)
        {
            return -1;
        }
    }

    // CPU is paused; debugger now owns control
    return 0;
#endif
}

static int cmd_release_debugger(DAPServer *server)
{
    (void)server;
    // Release debugger's request to pause (let CPU decide to run/step)
    set_debugger_request_pause(false);

    // Notify CPU thread that debugger control is released
    set_debugger_control_granted(false);

    return 0;
}

static int cmd_check_cpu_events(DAPServer *server)
{
    // Flush any buffered console output (from CPU thread ring buffers)
    flush_console_output();

    CpuStopReason reason = get_cpu_stop_reason();
    if (reason != STOP_REASON_NONE)
    {
        const char *dap_reason_str = cpuStopReasonToString(reason);

        // Get current source location for detailed context
        int line = 0;
        const char *file = NULL;

        // Try to get source location from symbol tables
        if (s_symbol_tables.symbol_table_stabs) {
            line = symbols_get_line(s_symbol_tables.symbol_table_stabs, gPC);
            file = symbols_get_file(s_symbol_tables.symbol_table_stabs, gPC);
        }
        if ((!line || !file) && s_symbol_tables.symbol_table_map) {
            line = symbols_get_line(s_symbol_tables.symbol_table_map, gPC);
            file = symbols_get_file(s_symbol_tables.symbol_table_map, gPC);
        }
        if ((!line || !file) && s_symbol_tables.symbol_table_aout) {
            line = symbols_get_line(s_symbol_tables.symbol_table_aout, gPC);
            file = symbols_get_file(s_symbol_tables.symbol_table_aout, gPC);
        }

        // Create detailed stop message
        char description[256];
        if (line > 0 && file) {
            snprintf(description, sizeof(description),
                    "Stopped at %s:%d (PC=%06o)", file, line, gPC);
        } else {
            snprintf(description, sizeof(description),
                    "Stopped at PC=%06o", gPC);
        }

        if (dap_server_send_stopped_event(server, dap_reason_str, description) == 0)
        {
            server->debugger_state.has_stopped = true;
            // Message sent successfully, now clear the stop reason
            set_cpu_stop_reason(STOP_REASON_NONE);
            // Log to console with detailed info
            dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, description);
        }
    }

    return 0;
}

static void ensure_cpu_running(void)
{
    CPURunMode run_mode = get_cpu_run_mode();
    if ((run_mode == CPU_PAUSED) || (run_mode == CPU_BREAKPOINT))
    {
        // CPU is paused, so we need to resume it
        set_cpu_run_mode(CPU_RUNNING);
#ifndef __EMSCRIPTEN__
        dap_server_send_output_category(g_dap_server, DAP_OUTPUT_CONSOLE, "Switched CPU to running mode\n");
#endif
    }
}

/********************************** INSTRUCTION ANALYSIS ***********************************/

/// @brief Check if instruction is a procedure call (JPL or similar)
/// @param operand The instruction word
/// @return true if instruction is a call, false otherwise
static bool is_procedure_call(uint16_t operand)
{
    // JPL instruction - Jump and Link (procedure call)
    // Format: 0134xxx (octal), where xxx includes addressing mode and displacement
    // JPL saves return address to L register: L = PC
    // This is the primary procedure call instruction in ND-100
    if ((operand & 0xF800) == 0134000) {
        return true;
    }

    // Note: ENTR (0140135) is NOT a call instruction
    // ENTR sets up a stack frame INSIDE a function but doesn't jump/call
    // It just advances PC by 2 after setting up the frame
    // The actual call happens via JPL before ENTR is executed

    return false;
}

/// @brief Trap-free debugger read of a DATA word, mirroring the CPU's own
///        data mapping.
///
/// The plain ReadVirtualMemory() path routes through mapVirtualToPhysical ->
/// checkPageProtection, which on an unmapped/protected address raises an
/// emulated PF/MPV -> interrupt(14) -> longjmp(cpu_jmp_buf). Fired from inside
/// a DAP command handler that longjmp tears out of the handler and trips the
/// stack protector ("stack smashing detected"). These helpers use the trap-free
/// Dbg_* accessors instead (they return -1 on a bad page and raise no trap).
///
/// mapVirtualToPhysical selects the alternative page table (D-space) only when
/// (STS_PTM && UseAPT); the CPU accesses data with UseAPT=true. So a faithful
/// data read is: split-I/D on -> D-space, otherwise the normal page table
/// (which the I-space accessor selects). This matches ReadVirtualMemory(addr,
/// true) in every mode, without the trap.
static int dbg_read_data(uint16_t addr)
{
    return STS_PTM ? Dbg_ReadVirtualMemoryDSpace(addr)
                   : Dbg_ReadVirtualMemoryISpace(addr);
}

static void dbg_write_data(uint16_t addr, uint16_t value)
{
    if (STS_PTM)
        Dbg_WriteVirtualMemoryDSpace(addr, value);
    else
        Dbg_WriteVirtualMemoryISpace(addr, value);
}

/// @brief Get the target address of a JPL instruction
/// @param pc Current program counter
/// @param operand JPL instruction word
/// @return Target address (simplified P-relative calculation)
/// @note This is a simplified version. Full implementation would need CPU register access
///       for all 8 addressing modes. See New_GetEffectiveAddr() in cpu.c for complete logic.
static uint16_t get_jpl_target_address(uint16_t pc, uint16_t operand)
{
    // JPL format: bits 15-11 = opcode (0134x)
    //             bits 10-8  = addressing mode (X, I, B flags)
    //             bits 7-0   = 8-bit signed displacement

    // Extract 8-bit displacement and sign-extend to 16 bits
    int8_t disp = (int8_t)(operand & 0xFF);
    uint16_t displacement = (uint16_t)(int16_t)disp;

    // Extract addressing mode flags
    // Bit 10 (X): Index by X register
    // Bit 9  (I): Indirect addressing
    // Bit 8  (B): Base-relative (vs P-relative)
    bool flag_x = (operand >> 10) & 1;
    bool flag_i = (operand >> 9) & 1;
    bool flag_b = (operand >> 8) & 1;

    // Compute base effective address
    uint16_t ea;
    if (flag_b)
        ea = gB + displacement;  // B-relative
    else
        ea = pc + displacement;  // P-relative

    // Apply indirect: read target address from memory (trap-free; see dbg_read_data)
    if (flag_i)
        ea = dbg_read_data(ea);

    // Apply indexing: add X register
    if (flag_x)
        ea += gX;

    return ea;
}

/// @brief Find the memory address of the return address of the current stack frame.
/// @return The memory address of the return address of the current stack frame. -1 if no return address is found.
int32_t find_stack_return_address(void)
{
    // Check if we have any frames at all
    if (s_stack_trace.frame_count == 0)
    {
        // No frames, so we can't find a return address
        return -1;
    }

    // Get the return address of the previous frame
    return s_stack_trace.frames[s_stack_trace.current_frame].return_address;
}

/// @brief Update the entry point of the JPL instruction
/// @param pc Program counter of the JPL instruction
/// @param operand Operand of the JPL instruction
void debugger_update_jpl_entrypoint(uint16_t ea)
{
    if (s_stack_trace.frame_count == 0)
    {
        return;
    }

    // Update the entry point of the JPL instruction
    s_stack_trace.frames[s_stack_trace.current_frame].entry_point = ea;
}

/// @brief Called by the CPU before executing an instruction
/// @details This function is called by the CPU before executing an instruction.
/// It builds the stack trace and sends it to the DAP server.
void debugger_build_stack_trace(uint16_t pc, uint16_t operand)
{

    // Check for different call types.
    // 'operand' is the instruction already fetched at 'pc' by the CPU, so we
    // decode it directly here instead of re-reading memory (two MMU-translated
    // I-space reads per instruction) via is_c_function_prologue/epilogue().
    bool is_jpl = ((operand & 0xF800) == 0134000);
    bool is_exit = (operand == 0146142);

    // ND-100 C calling convention: ENTR (0140135) enters a stack frame,
    // LEAVE (0140136) / ELEAV (0140137) return from one.
    bool is_c_call = (operand == 0140135);
    bool is_c_return = (operand == 0140136 || operand == 0140137);

    // Is this the first frame?
    if (s_stack_trace.frame_count == 0)
    {
        s_stack_trace.frame_count = 1;
        s_stack_trace.current_frame = 0;

        // Add the root frame to our circular buffer
        s_stack_trace.frames[s_stack_trace.current_frame].operand = operand;
        s_stack_trace.frames[s_stack_trace.current_frame].return_address = pc;
        s_stack_trace.frames[s_stack_trace.current_frame].entry_point = pc;
        s_stack_trace.frames[s_stack_trace.current_frame].pc = pc;
    }

    // Handle function calls (JPL or C calling convention)
    if (is_jpl || is_c_call)
    {
        // Create new stack frame
        // Calculate the return address (next instruction after call)
        uint16_t return_address = pc + 1;  // Both JPL and typical C calls return to next instruction

        s_stack_trace.current_frame = (s_stack_trace.current_frame + 1) % MAX_STACK_FRAMES;
        if (s_stack_trace.frame_count < MAX_STACK_FRAMES)
        {
            s_stack_trace.frame_count++;
        }

        // Add the new frame to our circular buffer
        s_stack_trace.frames[s_stack_trace.current_frame].pc = pc;
        s_stack_trace.frames[s_stack_trace.current_frame].operand = operand;
        s_stack_trace.frames[s_stack_trace.current_frame].return_address = return_address;
        s_stack_trace.frames[s_stack_trace.current_frame].entry_point = 0; // the address where JPL will jump to (will be updated when JPL is executed)

        return;
    }
    // Handle function returns (EXIT or C return)
    else if (is_exit || is_c_return)
    {
        // Remove the last frame from stack
        if (s_stack_trace.frame_count > 1)
        {
            // Clear the current frame
            s_stack_trace.frames[s_stack_trace.current_frame].pc = 0;
            s_stack_trace.frames[s_stack_trace.current_frame].operand = 0;
            s_stack_trace.frames[s_stack_trace.current_frame].return_address = 0;
            s_stack_trace.frames[s_stack_trace.current_frame].entry_point = 0;

            // Move back one frame
            s_stack_trace.current_frame = (s_stack_trace.current_frame - 1 + MAX_STACK_FRAMES) % MAX_STACK_FRAMES;
            s_stack_trace.frame_count--;
        }

        return;
    }
    else
    {
        s_stack_trace.frames[s_stack_trace.current_frame].pc = pc;
    }
}

/// @brief Step the CPU by the given step type
/// @param server The DAP server instance
/// @param step_type The type of step to take
/// @return 0 on success, -1 on failure
int step_cpu(DAPServer *server, StepType step_type)
{
    if (!server)
    {
        return -1;
    }

    uint16_t current_pc = gPC;
    uint16_t target_pc = 0;
    bool stepping_to_line = false;

    // Access the step command context
    StepCommandContext *ctx = &server->current_command.context.step;

    char log_message[256];

    // Step over - step to next line (F10)
    if (step_type == STEP_OVER)
    {

        // Implement special handling for EXIT instruction (P = L)
        if (g_reg->myreg_IR == 014614)
        {
            // EXIT copies L to P, so L IS the return address
            uint16_t return_address = gL;
            if (return_address == 0)
                return -1;

            breakpoint_manager_add(return_address, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
            ensure_cpu_running();
            return 0;
        }

        // Implement special handling for JMP, Conditional Jumps and SKP
        if (cpu_instruction_is_jump())
        {
            // Tell breakpoint manager to step one instruction
            breakpoint_manager_step_one();
            ensure_cpu_running();
            return 0;
        }

        // Check if current instruction is a procedure call (JPL)
        uint16_t current_operand = Dbg_ReadVirtualMemoryISpace(current_pc);
        if (is_procedure_call(current_operand)) {
            // Resolve JPL target to determine return address.
            // C calls via csav: JPL sets L = pc+1 (pointing to .word NNN),
            // csav increments L past the .word, EXIT returns to pc+2.
            // Leaf calls: JPL sets L = pc+1, EXIT returns to pc+1.
            uint16_t call_target = get_jpl_target_address(current_pc, current_operand);
            uint16_t return_addr = current_pc + 1;

            // Check if calling csav (C calling convention)
            const symbol_entry_t *csav_sym = NULL;
            if (s_symbol_tables.symbol_table_aout)
                csav_sym = symbols_lookup_by_name(s_symbol_tables.symbol_table_aout, "csav");
            if (!csav_sym && s_symbol_tables.symbol_table_map)
                csav_sym = symbols_lookup_by_name(s_symbol_tables.symbol_table_map, "csav");

            if (csav_sym && call_target == csav_sym->address)
                return_addr = current_pc + 2;  // skip JPL + .word NNN

            snprintf(log_message, sizeof(log_message),
                    "Stepping over function call, return at %06o\n", return_addr);
            dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, log_message);

            breakpoint_manager_add(return_addr, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
            ensure_cpu_running();
            return 0;
        }

        // If we have symbol table and want to step by line
        // CRITICAL FIX: Check STABS first, then MAP
        if ((s_symbol_tables.symbol_table_stabs || s_symbol_tables.symbol_table_map) &&
            ((ctx->granularity == DAP_STEP_GRANULARITY_LINE) || (ctx->granularity == DAP_STEP_GRANULARITY_STATEMENT)))
        {
            // Try STABS first (for C programs with STABS debug info)
            if (s_symbol_tables.symbol_table_stabs) {
                target_pc = symbols_get_next_line_address(s_symbol_tables.symbol_table_stabs, current_pc);

                if (target_pc != 0 && target_pc != current_pc) {
                    stepping_to_line = true;
                    snprintf(log_message, sizeof(log_message),
                            "Stepping to next line at address %06o (from STABS)\n", target_pc);
                    dap_server_send_output(server, log_message);
                }
            }

            // Try MAP if STABS didn't work (for assembly programs)
            if ((!stepping_to_line) && s_symbol_tables.symbol_table_map) {
                target_pc = symbols_get_next_line_address(s_symbol_tables.symbol_table_map, current_pc);

                if (target_pc != 0 && target_pc != current_pc) {
                    stepping_to_line = true;
                    snprintf(log_message, sizeof(log_message),
                            "Stepping to next line at address %06o (from MAP)\n", target_pc);
                    dap_server_send_output(server, log_message);
                }
            }
        }

        // Guard: symbols_get_next_line_address() matches the next line entry by
        // FILENAME only, unbounded across the global line table. In an overlaid
        // kernel a file has line entries at several disjoint address ranges, so
        // the "next line" can resolve to a different overlay copy of the same
        // file far from here (observed: step_over jumping into a monitor/overlay
        // routine). If the target leaves the current C function's range, don't
        // trust it -- fall back to a single instruction step.
        if (stepping_to_line && target_pc != 0 && s_symbol_tables.debug_info)
        {
            symbol_function_t *cur_fn = symbols_find_function_at(
                s_symbol_tables.debug_info, current_pc);
            if (cur_fn &&
                (target_pc < cur_fn->start_address || target_pc > cur_fn->end_address))
            {
                snprintf(log_message, sizeof(log_message),
                        "Next-line target %06o left function [%06o..%06o]; single-stepping instead\n",
                        target_pc, cur_fn->start_address, cur_fn->end_address);
                dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, log_message);
                stepping_to_line = false;
                target_pc = 0;
            }
        }

        // Set a temporary breakpoint at the target address if we're stepping to a line
        if (stepping_to_line && target_pc != 0 && target_pc != current_pc)
        {
            snprintf(log_message, sizeof(log_message), "Setting temporary breakpoint at address %06o\n", target_pc);
            dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, log_message);

            // Use the CPU's breakpoint system to set a temporary breakpoint
            // This breakpoint will be automatically removed when hit
            breakpoint_manager_add(target_pc, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
        }
        else
        {
            // Tell breakpoint manager to step one instruction
            breakpoint_manager_step_one();
        }

        ensure_cpu_running();
        return 0;
    }

    // Step in - intelligent source-level stepping (F11)
    if (step_type == STEP_IN)
    {
        snprintf(log_message, sizeof(log_message), "Step In from %06o\n", current_pc);
        dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, log_message);

        // Check granularity - are we stepping by line or instruction?
        bool is_line_granularity =
            (ctx->granularity == DAP_STEP_GRANULARITY_LINE) ||
            (ctx->granularity == DAP_STEP_GRANULARITY_STATEMENT);

        if (is_line_granularity) {
            // Source-level Step In
            // Scan all instructions in the current source line for a procedure call.
            // A C source line like "result = test_break(5)" compiles to multiple
            // instructions (push args, JPL, store result). The JPL may not be the
            // first instruction at the line's address.

            // Find the next line address to know the range of the current line
            uint16_t next_line_addr = 0;
            if (s_symbol_tables.symbol_table_stabs) {
                next_line_addr = symbols_get_next_line_address(s_symbol_tables.symbol_table_stabs, current_pc);
            }
            if ((!next_line_addr || next_line_addr == current_pc) && s_symbol_tables.symbol_table_map) {
                next_line_addr = symbols_get_next_line_address(s_symbol_tables.symbol_table_map, current_pc);
            }

            // Scan instructions from current_pc to next_line_addr for a JPL
            // When next_line_addr is unknown, scan up to 8 instructions ahead
            // to cover multi-instruction C statements (arg push + call)
            uint16_t scan_limit = (next_line_addr && next_line_addr > current_pc) ? next_line_addr : current_pc + 8;
            uint16_t jpl_addr = 0;
            uint16_t jpl_operand = 0;

            for (uint16_t addr = current_pc; addr < scan_limit; addr++) {
                uint16_t operand = Dbg_ReadVirtualMemoryISpace(addr);
                if (is_procedure_call(operand)) {
                    jpl_addr = addr;
                    jpl_operand = operand;
                    break;
                }
            }

            if (jpl_addr != 0) {
                // Found a procedure call in this source line
                uint16_t call_target = get_jpl_target_address(jpl_addr, jpl_operand);

                if (call_target != 0) {
                    // Try to find the first source line in the called function
                    int target_line = 0;
                    const char *target_file = NULL;

                    // Try STABS first (for C functions)
                    if (s_symbol_tables.symbol_table_stabs) {
                        target_line = symbols_get_line(s_symbol_tables.symbol_table_stabs, call_target);
                        target_file = symbols_get_file(s_symbol_tables.symbol_table_stabs, call_target);
                    }

                    // Try MAP if STABS didn't work (for assembly functions)
                    if ((!target_line || !target_file) && s_symbol_tables.symbol_table_map) {
                        target_line = symbols_get_line(s_symbol_tables.symbol_table_map, call_target);
                        target_file = symbols_get_file(s_symbol_tables.symbol_table_map, call_target);
                    }

                    if (target_line && target_file) {
                        // Found source info - step into the function
                        snprintf(log_message, sizeof(log_message),
                                "Stepping into %s:%d (address %06o)\n",
                                target_file, target_line, call_target);
                        dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, log_message);

                        // Set temporary breakpoint at function entry
                        breakpoint_manager_add(call_target, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
                        ensure_cpu_running();
                        return 0;
                    }

                    // No source info - still step into, but at instruction level
                    snprintf(log_message, sizeof(log_message),
                            "Stepping into function at %06o (no source info)\n", call_target);
                    dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, log_message);

                    breakpoint_manager_add(call_target, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
                    ensure_cpu_running();
                    return 0;
                }
            }

            // No call found in this line - step to next source line
            // next_line_addr already computed above
            if (next_line_addr && next_line_addr != current_pc) {
                snprintf(log_message, sizeof(log_message),
                        "Stepping to next line at %06o\n", next_line_addr);
                dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, log_message);

                breakpoint_manager_add(next_line_addr, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
                ensure_cpu_running();
                return 0;
            }
        }

        // Fallback: instruction-level step
        snprintf(log_message, sizeof(log_message),
                "Step In (instruction level) from %06o\n", current_pc);
        dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, log_message);

        breakpoint_manager_step_one();
        ensure_cpu_running();
        return 0;
    }

    // Step out - step to the return address (Shift+F11)
    if (step_type == STEP_OUT)
    {
        uint16_t return_addr = 0;

        // Look up csav/cret addresses for special handling
        const symbol_entry_t *csav_sym = NULL;
        const symbol_entry_t *cret_sym = NULL;
        if (s_symbol_tables.symbol_table_aout) {
            csav_sym = symbols_lookup_by_name(s_symbol_tables.symbol_table_aout, "csav");
            cret_sym = symbols_lookup_by_name(s_symbol_tables.symbol_table_aout, "cret");
        }
        if (!csav_sym && s_symbol_tables.symbol_table_map)
            csav_sym = symbols_lookup_by_name(s_symbol_tables.symbol_table_map, "csav");
        if (!cret_sym && s_symbol_tables.symbol_table_map)
            cret_sym = symbols_lookup_by_name(s_symbol_tables.symbol_table_map, "cret");

        // Special case: inside csav (function prologue helper).
        // L points to .word NNN (frame size parameter). The function body
        // resumes at L+1. Using L directly would set a breakpoint on data
        // and crash, so we must skip past the .word.
        if (csav_sym && cret_sym &&
            gPC >= csav_sym->address && gPC < cret_sym->address) {
            return_addr = gL + 1;
            snprintf(log_message, sizeof(log_message),
                    "Stepping out of csav to function body at %06o\n", return_addr);
            dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, log_message);
            breakpoint_manager_add(return_addr, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
            ensure_cpu_running();
            return 0;
        }

        // Special case: inside cret (function epilogue helper).
        // B still points to the current frame (not yet restored).
        // B[1] = return address to the function's caller.
        if (cret_sym && gPC >= cret_sym->address &&
            gPC < cret_sym->address + 8) {
            return_addr = dbg_read_data(gB + 1);
            snprintf(log_message, sizeof(log_message),
                    "Stepping out of cret to caller at %06o\n", return_addr);
            dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, log_message);
            breakpoint_manager_add(return_addr, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
            ensure_cpu_running();
            return 0;
        }

        // Determine return address using both L register and B-chain,
        // picking the right one based on whether csav has executed.
        //
        // Before csav: L = return address (set by JPL), B = caller's frame
        // After csav:  L = garbage (.word addr), B = our frame, B[1] = return addr
        //
        // Strategy: if B[1] and L both look valid, we need to know which
        // is correct. We detect "after csav" by checking if B[1] points
        // into the calling function (the function that contains the address
        // L held at function entry). If B[1] looks like a return to our
        // direct caller and B[0] is a valid old-B, prefer B[1].
        // Otherwise, use L.
        {
            uint16_t b_ret = 0;
            uint16_t l_ret = gL;
            bool b_chain_valid = false;

            // Check B-chain
            if (s_symbol_tables.debug_info) {
                symbol_function_t *fn = symbols_find_function_at(
                    s_symbol_tables.debug_info, gPC);
                if (fn && fn->start_address != gPC) {
                    // Trap-free debugger reads (see dbg_read_data); preserve the
                    // old "unmapped -> 0 -> skip" semantics so the heuristic below
                    // is unchanged (dbg_read_data returns -1 on a bad page).
                    int saved_ret_w = dbg_read_data(gB + 1);
                    int old_b_w = dbg_read_data(gB);
                    uint16_t saved_ret = (saved_ret_w < 0) ? 0 : (uint16_t)saved_ret_w;
                    uint16_t old_b = (old_b_w < 0) ? 0 : (uint16_t)old_b_w;
                    // B-chain is valid if: B[0] != B (frame pointer changed),
                    // B[0] > B (old frame is higher in stack), and B[1]
                    // points to a different function than we're in now.
                    if (saved_ret > 0 && old_b > 0 && old_b != gB &&
                        old_b > gB) {
                        // Verify B[1] doesn't point into our own function
                        // (which would mean csav hasn't run yet and B is
                        // still the caller's frame)
                        symbol_function_t *ret_fn = symbols_find_function_at(
                            s_symbol_tables.debug_info, saved_ret);
                        if (!ret_fn || ret_fn != fn) {
                            b_ret = saved_ret;
                            b_chain_valid = true;
                        }
                    }
                }
            }

            // If B-chain is valid AND L looks like it points into our
            // own function (meaning csav overwrote it), use B-chain.
            // Otherwise prefer L (still contains the return address).
            if (b_chain_valid) {
                symbol_function_t *l_fn = s_symbol_tables.debug_info ?
                    symbols_find_function_at(s_symbol_tables.debug_info, l_ret) :
                    NULL;
                symbol_function_t *cur_fn = s_symbol_tables.debug_info ?
                    symbols_find_function_at(s_symbol_tables.debug_info, gPC) :
                    NULL;

                if (l_fn && l_fn == cur_fn) {
                    // L points into our own function - csav overwrote it.
                    // Use B-chain.
                    return_addr = b_ret;
                } else {
                    // L points elsewhere - it's still the return address.
                    // This means csav hasn't run yet despite being past
                    // fn->start_address (pre-csav prologue instructions).
                    return_addr = l_ret;
                }
            }

            if (return_addr == 0)
                return_addr = l_ret;
        }

        if (return_addr != 0) {
            snprintf(log_message, sizeof(log_message),
                    "Stepping out to %06o\n", return_addr);
            dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, log_message);
            breakpoint_manager_add(return_addr, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
            ensure_cpu_running();
            return 0;
        }
    }

    return -1;
}

/********************************** HELPER FUNCTIONS **********************************/

/// @brief Check if a string ends with a specific suffix
/// @param str The string to check
/// @param suffix The suffix to look for
/// @return true if str ends with suffix, false otherwise
static bool str_ends_with(const char *str, const char *suffix)
{
    if (!str || !suffix) return false;

    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (suffix_len > str_len) return false;

    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

/// @brief Free all source references
static void free_source_references(void)
{
    for (int i = 0; i < source_ref_count; i++) {
        if (source_refs[i].filepath) free(source_refs[i].filepath);
        if (source_refs[i].content) free(source_refs[i].content);
    }
    if (source_refs) free(source_refs);
    source_refs = NULL;
    source_ref_count = 0;
}

/********************************** CALLBACKS **********************************/

/**
 * @brief Step Next command handler
 *
 * Handles the 'next' command by stepping over the current line/statement
 * VS Code F10
 *
 * @param server The DAP server instance
 * @return int 0 on success, non-zero on failure
 */
static int cmd_next(DAPServer *server)
{
    return step_cpu(server, STEP_OVER);
}

/**
 * @brief Step In command handler
 *
 * Handles the 'stepIn' command by stepping into a function call
 * VS Code F11
 *
 * @param server The DAP server instance
 * @return int 0 on success, non-zero on failure
 */
static int cmd_step_in(DAPServer *server)
{
    return step_cpu(server, STEP_IN);
}

/**
 * @brief Step Out command handler
 *
 * Handles the 'stepOut' command by stepping out of the current function
 * VS Code Shift+F11
 * @param server The DAP server instance
 * @return int 0 on success, non-zero on failure
 */
static int cmd_step_out(DAPServer *server)
{
    return step_cpu(server, STEP_OUT);
}

static int cmd_continue(DAPServer *server)
{
    (void)server;

    CPURunMode run_mode = get_cpu_run_mode();
    if ((run_mode == CPU_PAUSED) || (run_mode == CPU_BREAKPOINT))
    {
        set_cpu_run_mode(CPU_RUNNING);
        return 0;
    }

    return 1; // Error
}

/**
 * @brief Handle DAP scopes request
 *
 * This function creates scope objects for the variables visible in the current stack frame.
 * It defines several scopes:
 * 1. Locals - Local variables
 * 2. CPU Registers - CPU register values
 * 3. Memory - Memory regions
 *
 * @param server The DAP server instance
 * @return int 0 on success, non-zero on failure
 */
// Last frame_id requested by scopes - used by add_local_variables
// to show the correct function's variables, not always the top frame.
static int scopes_active_frame_id = 0;

static int cmd_scopes(DAPServer *server)
{
    if (!server)
    {
        return -1;
    }

    // Extract frame_id from the command context
    int frame_id = server->current_command.context.scopes.frame_id;
    scopes_active_frame_id = frame_id;

    // Allocate memory for the scopes
    DAPScope *scopes = (DAPScope *)calloc(NUM_SCOPES, sizeof(DAPScope));
    if (!scopes)
    {
        dap_server_send_output_category(server, DAP_OUTPUT_STDERR,
                                        "Error: Failed to allocate memory for scopes\n");
        return -1;
    }

    // Set up Locals scope
    int scope_index = 0;

    // Show Locals scope if we have C debug info for the requested frame's PC
    {
        bool has_c_locals = false;
        int c_var_count = 0;

        // Use the requested frame's PC, not gPC (which is always the top frame)
        uint16_t frame_pc = gPC;
        if (frame_id >= 0 && frame_id < s_stack_trace.frame_count)
            frame_pc = s_stack_trace.frames[frame_id].pc;

        if (s_symbol_tables.debug_info)
        {
            symbol_function_t *func = symbols_find_function_at(
                s_symbol_tables.debug_info, frame_pc);
            if (func && func->variable_count > 0)
            {
                has_c_locals = true;
                c_var_count = func->variable_count;
            }
        }

        if (has_c_locals ||
            s_stack_trace.frames[s_stack_trace.current_frame].variables.number_of_variables > 0)
        {
            int named_vars = has_c_locals ? c_var_count :
                s_stack_trace.frames[s_stack_trace.current_frame].variables.number_of_variables;

            scopes[scope_index].name = strdup("Locals");
            scopes[scope_index].variables_reference = SCOPE_ID_LOCALS;
            scopes[scope_index].named_variables = named_vars;
            scopes[scope_index].indexed_variables = 0;
            scopes[scope_index].expensive = false;
            scopes[scope_index].source_path = NULL;
            scopes[scope_index].line = 0;
            scopes[scope_index].column = 0;
            scopes[scope_index].end_line = 0;
            scopes[scope_index].end_column = 0;
            scope_index++;
        }
    }

    // Set up CPU Registers scope
    scopes[scope_index].name = strdup("CPU Registers");
    scopes[scope_index].variables_reference = SCOPE_ID_REGISTERS;
    scopes[scope_index].named_variables = 9; // STS, D, P, B, L, A, T, X + EA
    scopes[scope_index].indexed_variables = 0;
    scopes[scope_index].expensive = false;
    // Source location fields are optional, set to 0/NULL
    scopes[scope_index].source_path = NULL;
    scopes[scope_index].line = 0;
    scopes[scope_index].column = 0;
    scopes[scope_index].end_line = 0;
    scopes[scope_index].end_column = 0;

    // Set up Levels registers
    scope_index++;
    scopes[scope_index].name = strdup("Interrupt levels");
    scopes[scope_index].variables_reference = SCOPE_ID_LEVELS;
    scopes[scope_index].named_variables = 0; // 16 levels + PIL/PVL
    scopes[scope_index].indexed_variables = 0;
    scopes[scope_index].expensive = false;
    // Source location fields are optional, set to 0/NULL
    scopes[scope_index].source_path = NULL;
    scopes[scope_index].line = 0;
    scopes[scope_index].column = 0;
    scopes[scope_index].end_line = 0;
    scopes[scope_index].end_column = 0;

    // Set up Internal Registers - Read scope
    scope_index++;
    scopes[scope_index].name = strdup("Internal Registers - Read");
    scopes[scope_index].variables_reference = SCOPE_ID_INTERNAL_REGISTERS_READ;
    scopes[scope_index].named_variables = 1; // 13 internal register
    scopes[scope_index].indexed_variables = 0;
    // Source location fields are optional, set to 0/NULL
    scopes[scope_index].source_path = NULL;
    scopes[scope_index].line = 0;
    scopes[scope_index].column = 0;
    scopes[scope_index].end_line = 0;
    scopes[scope_index].end_column = 0;

    // Set up Internal Registers - Write scope
    scope_index++;
    scopes[scope_index].name = strdup("Internal Registers - Write");
    scopes[scope_index].variables_reference = SCOPE_ID_INTERNAL_REGISTERS_WRITE;
    scopes[scope_index].named_variables = 10; // num internal registers
    scopes[scope_index].indexed_variables = 0;
    // Source location fields are optional, set to 0/NULL
    scopes[scope_index].source_path = NULL;
    scopes[scope_index].line = 0;
    scopes[scope_index].column = 0;
    scopes[scope_index].end_line = 0;
    scopes[scope_index].end_column = 0;

    // Set up Memory scope
    scope_index++;
    scopes[scope_index].name = strdup("MMS");
    scopes[scope_index].variables_reference = SCOPE_ID_MEM_MMS;
    scopes[scope_index].named_variables = 3; // Number of memory regions
    scopes[scope_index].indexed_variables = 0;
    scopes[scope_index].expensive = true; // Memory access is expensive
    // Source location fields are optional, set to 0/NULL
    scopes[scope_index].source_path = NULL;
    scopes[scope_index].line = 0;
    scopes[scope_index].column = 0;
    scopes[scope_index].end_line = 0;
    scopes[scope_index].end_column = 0;

    // Store the scopes in the command context for the DAP server to use
    server->current_command.context.scopes.scopes = scopes;
    server->current_command.context.scopes.scope_count = NUM_SCOPES;

    return 0;
}

/**
 * @brief Helper function to add a variable to the server's variable array
 *
 * @param server The DAP server instance
 * @param name Variable name
 * @param value Variable value
 * @param type Variable type
 * @param variables_reference Reference for child variables (0 for leaf variables)
 * @param memory_reference Optional memory reference
 * @param kind Enum of variable kind (property, method, etc.)
 * @param attributes Enum of attributes
 * @return DAPVariable* Pointer to the newly added variable or NULL on failure
 */
static DAPVariable *add_variable_to_array(
    DAPServer *server,
    const char *name,
    const char *value,
    const char *type,
    uint32_t memory_reference,

    int variables_reference,
    DAPVariableKind kind,
    DAPVariableAttributes attributes)
{
    if (!server || !name || !value)
    {
        return NULL;
    }

    // Increase the count and reallocate the array
    server->current_command.context.variables.variable_count++;
    server->current_command.context.variables.variable_array = realloc(
        server->current_command.context.variables.variable_array,
        server->current_command.context.variables.variable_count * sizeof(DAPVariable));

    if (!server->current_command.context.variables.variable_array)
    {
        server->current_command.context.variables.variable_count--;
        return NULL;
    }

    // Get a pointer to the newly added variable
    DAPVariable *var = &server->current_command.context.variables.variable_array[server->current_command.context.variables.variable_count - 1];

    // Initialize the variable with the provided values
    var->name = name ? strdup(name) : NULL;
    var->value = value ? strdup(value) : NULL;
    var->type = type ? strdup(type) : NULL;
    var->memory_reference = memory_reference;
    var->variables_reference = variables_reference;
    var->named_variables = 0;
    var->indexed_variables = 0;
    var->evaluate_name = NULL;

    var->presentation_hint.kind = kind;
    var->presentation_hint.attributes = attributes;
    var->presentation_hint.visibility = DAP_VARIABLE_VISIBILITY_NONE;

    return var;
}

/**
 * @brief Add local variables to the variables array
 *
 * @param server The DAP server instance
 * @param info_message Buffer to write info message
 * @param info_message_size Size of info message buffer
 */
static void add_local_variables(DAPServer *server, char *info_message, size_t info_message_size)
{
    char value_str[64];

    // Check if we have C debug info and are in a C function
    if (s_symbol_tables.debug_info)
    {
        // Use the frame's PC and B register, not the top frame's
        uint16_t frame_pc = gPC;
        uint16_t frame_b = gB;
        int fid = scopes_active_frame_id;
        if (fid >= 0 && fid < s_stack_trace.frame_count)
        {
            frame_pc = s_stack_trace.frames[fid].pc;
            frame_b = s_stack_trace.frames[fid].b_reg;
        }

        symbol_function_t *func = symbols_find_function_at(
            s_symbol_tables.debug_info, frame_pc);

        if (func)
        {
            int var_count = 0;
            symbol_variable_t *vars = symbols_get_variables(func, &var_count);

            for (int i = 0; i < var_count; i++)
            {
                // Read value from memory relative to the frame's B register
                // Parameters: B + offset (positive offsets, e.g. B+2)
                // Locals: B + offset (negative offsets, e.g. B-1)
                uint16_t addr = (uint16_t)((int16_t)frame_b + vars[i].offset);
                // Trap-free read (see dbg_read_data); returns -1 on a bad frame
                // address, making the guard below live.
                int word = dbg_read_data(addr);

                if (word != -1)
                {
                    snprintf(value_str, sizeof(value_str), "%d (%06o)",
                             (int16_t)word, (uint16_t)word);
                }
                else
                {
                    snprintf(value_str, sizeof(value_str), "<unreadable>");
                }

                add_variable_to_array(
                    server,
                    vars[i].name,
                    value_str,
                    vars[i].type_name ? vars[i].type_name : "int",
                    addr,
                    0,
                    vars[i].is_parameter ? DAP_VARIABLE_KIND_DATA
                                         : DAP_VARIABLE_KIND_PROPERTY,
                    DAP_VARIABLE_ATTR_NONE);
            }

            snprintf(info_message, info_message_size,
                     "Function: %s (%d variables)\n",
                     func->name, var_count);
            return;
        }
    }

    // Fallback: show any variables from the stack trace frame
    // (assembly-level local variables, if any were set)
    if (s_stack_trace.frames[s_stack_trace.current_frame].variables.number_of_variables > 0)
    {
        LocalVariables *lv = &s_stack_trace.frames[s_stack_trace.current_frame].variables;
        for (int i = 0; i < lv->number_of_variables; i++)
        {
            add_variable_to_array(
                server,
                lv->variables[i].name ? lv->variables[i].name : "?",
                lv->variables[i].value ? lv->variables[i].value : "0",
                lv->variables[i].type ? lv->variables[i].type : "integer",
                0,
                0,
                DAP_VARIABLE_KIND_PROPERTY,
                DAP_VARIABLE_ATTR_NONE);
        }
    }
}

static void add_level_variables(DAPServer *server, char *info_message, size_t info_message_size)
{
    (void)info_message; (void)info_message_size;

    char value_str[100];

    // ADD PIL
    snprintf(value_str, sizeof(value_str), "%06o", gPIL);
    add_variable_to_array(
        server,
        "PIL",                      // name
        value_str,                  // value
        "integer",                  // type
        0,                          // variablesReference
        0,                          // memoryReference (no memory reference for locals)
        DAP_VARIABLE_KIND_PROPERTY, // kind
        DAP_VARIABLE_ATTR_NONE      // attributes
    );

    // ADD PVL
    snprintf(value_str, sizeof(value_str), "%06o", gPVL);
    add_variable_to_array(
        server,
        "PVL",                      // name
        value_str,                  // value
        "integer",                  // type
        0,                          // variablesReference
        0,                          // memoryReference (no memory reference for locals)
        DAP_VARIABLE_KIND_PROPERTY, // kind
        DAP_VARIABLE_ATTR_NONE      // attributes
    );

    for (int i = 0; i < 16; i++)
    {
        uint16_t rP = g_reg->reg[i][_P];
        uint16_t rPCR = g_reg->reg_PCR[i];
        uint16_t pt = 0, apt = 0;

        // decode PCR
        uint16_t ring = rPCR & 0x03;
        if (rPCR & (1 << 2))
        {
            // Sixteen page table mode
            pt = (rPCR >> 11) & 0x0F;
            apt = (rPCR >> 7) & 0x0F;
        }
        else
        {
            // Four page table mode
            pt = (rPCR >> 9) & 0x03;
            apt = (rPCR >> 7) & 0x03;
        }
        char pcr_str[100];
        snprintf(pcr_str, sizeof(pcr_str), "Ring[%d] PT[%d] APT[%d] P[%06d]", ring, pt, apt, rP);

        char name[20];
        snprintf(name, sizeof(name), "Level %d", i);

        char memory_reference[100];
        snprintf(memory_reference, sizeof(memory_reference), "0x%04x", rP);

        add_variable_to_array(
            server,
            name,                       // name
            pcr_str,                    // value
            "integer",                  // type
            rP,                         // memoryReference
            SCOPE_ID_PIL_BASE + i,      // variablesReference -> expandable per-PIL bank
            DAP_VARIABLE_KIND_DATA,     // kind
            DAP_VARIABLE_ATTR_NONE      // attributes
        );
    }
}

/// @brief Emit the full register bank for a single interrupt level (PIL).
/// Used by the per-PIL sub-scope so the user can inspect any level's
/// registers, not just the current one.
static void add_pil_register_variables(DAPServer *server, int pil)
{
    if (!server || pil < 0 || pil > 15) return;

    char value_str[32];
    static const struct { const char *name; int idx; } regs[] = {
        {"STS", _STS}, {"P", _P}, {"B", _B}, {"L", _L},
        {"A", _A},     {"T", _T}, {"X", _X}, {"D", 1},
    };
    // Note: "D" lives at index 1 in reg[level][] (slot between STS and P).

    for (size_t r = 0; r < sizeof(regs)/sizeof(regs[0]); r++) {
        uint16_t v = g_reg->reg[pil][regs[r].idx];
        snprintf(value_str, sizeof(value_str), "%06o", v);
        add_variable_to_array(
            server,
            regs[r].name,
            value_str,
            "integer",
            v,
            0,
            DAP_VARIABLE_KIND_DATA,
            DAP_VARIABLE_ATTR_NONE);
    }

    // Also include the per-level paging control register so callers can
    // verify ring/PT/APT/priority/PTM-readiness for that level.
    snprintf(value_str, sizeof(value_str), "%06o", g_reg->reg_PCR[pil]);
    add_variable_to_array(
        server, "PCR", value_str, "integer", 0, 0,
        DAP_VARIABLE_KIND_DATA, DAP_VARIABLE_ATTR_NONE);
}

/**
 * @brief Add CPU register variables to the variables array
 *
 * @param server The DAP server instance
 * @param info_message Buffer to write info message
 * @param info_message_size Size of info message buffer
 */
static void add_register_variables(DAPServer *server, char *info_message, size_t info_message_size)
{
    snprintf(info_message, info_message_size,
             "Loading CPU registers\n");

    // Property kind with no attributes

    server->current_command.context.variables.variable_count = 0;

    // Register formatting
    char value_str[32];

    // Add the ST register (Status register)
    snprintf(value_str, sizeof(value_str), "%06o", gSTSr);
    add_variable_to_array(
        server,
        "STS",     // name
        value_str, // value
        //"bitmap",                  // type
        "integer",
        -1,                     // memoryReference  (-1 = not present)
        SCOPE_ID_STATUS_FLAGS,  // variablesReference
        DAP_VARIABLE_KIND_DATA, // kind
        DAP_VARIABLE_ATTR_NONE  // attributes
    );

    // Add the D register (Data register)
    snprintf(value_str, sizeof(value_str), "%06o", gD);
    add_variable_to_array(
        server,
        "D",                    // name
        value_str,              // value
        "integer",              // type
        gD,                     // memoryReference
        0,                      // variablesReference
        DAP_VARIABLE_KIND_DATA, // kind
        DAP_VARIABLE_ATTR_NONE  // attributes
    );

    // Add the P register (Program Counter)
    snprintf(value_str, sizeof(value_str), "%06o", gPC);
    add_variable_to_array(
        server,
        "P",                    // name
        value_str,              // value
        "integer",              // type
        gPC,                    // memoryReference
        0,                      // variablesReference
        DAP_VARIABLE_KIND_DATA, // kind
        DAP_VARIABLE_ATTR_NONE  // attributes
    );

    // Add the B register (B register)
    snprintf(value_str, sizeof(value_str), "%06o", gB);
    add_variable_to_array(
        server,
        "B",                    // name
        value_str,              // value
        "integer",              // type
        gB,                     // memoryReference
        0,                      // variablesReference
        DAP_VARIABLE_KIND_DATA, // kind
        DAP_VARIABLE_ATTR_NONE  // attributes
    );

    // Add the L register (Link register)
    snprintf(value_str, sizeof(value_str), "%06o", gL);
    add_variable_to_array(
        server,
        "L",                    // name
        value_str,              // value
        "integer",              // type
        gL,                     // memoryReference
        0,                      // variablesReference
        DAP_VARIABLE_KIND_DATA, // kind
        DAP_VARIABLE_ATTR_NONE  // attributes
    );

    // Add the A register (Accumulator)
    snprintf(value_str, sizeof(value_str), "%06o", gA);
    add_variable_to_array(
        server,
        "A",                    // name
        value_str,              // value
        "integer",              // type
        gA,                     // memoryReference
        0,                      // variablesReference
        DAP_VARIABLE_KIND_DATA, // kind
        DAP_VARIABLE_ATTR_NONE  // attributes
    );

    // Add the T register (T register)
    snprintf(value_str, sizeof(value_str), "%06o", gT);
    add_variable_to_array(
        server,
        "T",                    // name
        value_str,              // value
        "integer",              // type
        gT,                     // memoryReference
        0,                      // variablesReference
        DAP_VARIABLE_KIND_DATA, // kind
        DAP_VARIABLE_ATTR_NONE  // attributes
    );

    // Add the X register (Index register)
    snprintf(value_str, sizeof(value_str), "%06o", gX);
    add_variable_to_array(
        server,
        "X",                    // name
        value_str,              // value
        "integer",              // type
        gX,                     // memoryReference
        0,                      // variablesReference
        DAP_VARIABLE_KIND_DATA, // kind
        DAP_VARIABLE_ATTR_NONE  // attributes
    );

    // Add the Effective address register
    snprintf(value_str, sizeof(value_str), "%06o", gEA);
    add_variable_to_array(
        server,
        "EA",                   // name
        value_str,              // value
        "integer",              // type
        gEA,                    // memoryReference
        0,                      // variablesReference
        DAP_VARIABLE_KIND_DATA, // kind
        DAP_VARIABLE_ATTR_NONE  // attributes
    );
}

/**
 * @brief Add internal CPU register variables to the variables array
 *
 * @param server The DAP server instance
 * @param info_message Buffer to write info message
 * @param info_message_size Size of info message buffer
 */
static void add_internal_registers_read_variables(DAPServer *server, char *info_message, size_t info_message_size)
{
    snprintf(info_message, info_message_size,
             "Loading internal CPU registers\n");

    // Property kind with readonly attribute

    server->current_command.context.variables.variable_count = 0;

    // Register formatting
    char value_str[32];

    // Define the internal registers with their addresses
    struct
    {
        const char *name;
        uint16_t *reg_ptr;
        const char *type;
    } internal_regs[] = {
        {"PANS", &g_reg->reg_PANS, "octal"}, // Panel status
        {"OPR", &g_reg->reg_OPR, "octal"},   // Operator register
        {"PGS", &g_reg->reg_PGS, "octal"},   // Paging status register
        {"PVL", &g_reg->reg_PVL, "octal"},   // Page violation limit register
        {"IIC", &g_reg->reg_IIC, "octal"},   // Internal interrupt code register
        {"IID", &g_reg->reg_IID, "octal"},   // Internal interrupt detect register
        {"PID", &g_reg->reg_PID, "octal"},   // Priority interrupt detect register
        {"PIE", &g_reg->reg_PIE, "octal"},   // Priority interrupt enable register
        {"CSR", &g_reg->reg_CSR, "octal"},   // Control store register
        {"ALD", &g_reg->reg_ALD, "octal"},   // Auto-load descriptor register
        {"PES", &g_reg->reg_PES, "octal"},   // Page error status register
        {"PGC", &g_reg->reg_PGC, "octal"},   // Paging Control Register
        {"PEA", &g_reg->reg_PEA, "octal"},   // Page error address register
    };

    const int num_regs = sizeof(internal_regs) / sizeof(internal_regs[0]);

    // Add each internal register to the variable array
    for (int i = 0; i < num_regs; i++)
    {
        // Format the register value in octal
        snprintf(value_str, sizeof(value_str), "%06o", *(internal_regs[i].reg_ptr));

        // Add the register to the variable array
        add_variable_to_array(
            server,
            internal_regs[i].name,  // name
            value_str,              // value
            internal_regs[i].type,  // type
            -1,                     // memoryReference - safely truncate to 32-bit if needed
            0,                      // variablesReference (no children)
            DAP_VARIABLE_KIND_DATA, // kind
            DAP_VARIABLE_ATTR_NONE  // attributes
        );
    }
}

/**
 * @brief Add internal CPU register variables to the variables array
 *
 * @param server The DAP server instance
 * @param info_message Buffer to write info message
 * @param info_message_size Size of info message buffer
 */
static void add_internal_registers_write_variables(DAPServer *server, char *info_message, size_t info_message_size)
{
    snprintf(info_message, info_message_size,
             "Loading internal CPU registers\n");

    // Property kind with readonly attribute

    server->current_command.context.variables.variable_count = 0;

    // Register formatting
    char value_str[32];

    // Define the internal registers with their addresses
    struct
    {
        const char *name;
        uint16_t *reg_ptr;
        const char *type;
    } internal_regs[] = {
        {"PANC", &g_reg->reg_PANC, "octal"},     // Panel control
        {"LMP", &g_reg->reg_LMP, "octal"},       // Panel data display buffer register
        {"PCR", &g_reg->reg_PCR[gPIL], "octal"}, // Paging Control Register
        {"IIE", &g_reg->reg_IIE, "octal"},       // Internal interrupt enable register
        {"PID", &g_reg->reg_PID, "octal"},       // Priority interrupt detect register
        {"PIE", &g_reg->reg_PIE, "octal"},       // Priority interrupt enable register
        {"CCL", &g_reg->reg_CCL, "octal"},       // Cache clear register
        {"LCIL", &g_reg->reg_LCIL, "octal"},     // Lower cache inhibit limit register
        {"UCIL", &g_reg->reg_UCIL, "octal"},     // Upper cache inhibit limit register
        {"ECCR", &g_reg->reg_ECCR, "octal"},     // Error correction control register
    };

    const int num_regs = sizeof(internal_regs) / sizeof(internal_regs[0]);

    // Add each internal register to the variable array
    for (int i = 0; i < num_regs; i++)
    {
        // Format the register value in octal
        snprintf(value_str, sizeof(value_str), "%06o", *(internal_regs[i].reg_ptr));

        // Add the register to the variable array
        add_variable_to_array(
            server,
            internal_regs[i].name,  // name
            value_str,              // value
            internal_regs[i].type,  // type
            -1,                     // memoryReference (-1 = not present)
            0,                      // variablesReference (no children)
            DAP_VARIABLE_KIND_DATA, // kind
            DAP_VARIABLE_ATTR_NONE  // attributes
        );
    }
}
/**
 * @brief Add status flags from the STS register to the variables array
 *
 * @param server The DAP server instance
 * @param info_message Buffer to write info message
 * @param info_message_size Size of info message buffer
 */
static void add_status_flag_variables(DAPServer *server, char *info_message, size_t info_message_size)
{
    snprintf(info_message, info_message_size,
             "Loading CPU status flags\n");

    // Property kind with readonly attribute

    server->current_command.context.variables.variable_count = 0;

    // Status flag definitions with bit positions
    struct
    {
        const char *name;
        int bit_pos;
    } status_flags[] = {
        {"P", 0},      // Page Table Mode
        {"T", 1},      // Rounging flag for floating point operations
        {"K", 2},      // One bit accumulato
        {"Z", 3},      // Error flag
        {"Q", 4},      // Dynamic overflow flag
        {"O", 5},      // Overflow flag
        {"C", 6},      // Carry flag
        {"M", 7},      // Multishift flag
        {"PIL", 8},    // Program level
        {"N100", 12},  // N100 flag (always 1)
        {"SEXI", 13},  // Extended flag
        {"PONI", 14},  // Memory management on flag
        {"IONI", 15}}; // Interrupt system on flag

    const int num_flags = sizeof(status_flags) / sizeof(status_flags[0]);

    // Get the current STS register value
    uint16_t sts_value = gSTSr;

    // Add each flag to the variable array
    for (int i = 0; i < num_flags; i++)
    {
        // Get the bit value (special handling for PL which is 4 bits)
        bool value;
        char value_str[32];

        if (strcmp(status_flags[i].name, "PIL") == 0)
        {
            // Extract 4-bit PL field
            int pl_value = (sts_value >> 8) & 0x0F;
            snprintf(value_str, sizeof(value_str), "%d", pl_value);
        }
        else
        {
            // Extract 1-bit flags
            value = (sts_value >> status_flags[i].bit_pos) & 0x01;
            snprintf(value_str, sizeof(value_str), "%d", value);
        }

        // Create a display name with description
        char display_name[64];
        snprintf(display_name, sizeof(display_name), "%s",
                 status_flags[i].name);

        // Add the flag to the variable array
        add_variable_to_array(
            server,
            display_name,                                                     // name with description
            value_str,                                                        // value (true/false or numeric for PL)
            strcmp(status_flags[i].name, "PIL") == 0 ? "integer" : "boolean", // type
            -1,                                                               // memoryReference (-1 = not present)
            0,                                                                // variablesReference (no children)
            DAP_VARIABLE_KIND_PROPERTY,                                       // kind
            DAP_VARIABLE_ATTR_NONE                                            // attributes
        );
    }
}

/// @brief Get informaiton about a page table entry
/// @param PTe
/// @return
char *GetPageTableEntryInfo(uint32_t PTe)
{

    static char debugInfo[256];
    debugInfo[0] = '\0';

    char memoryRange[50];

    // Map to physical page
    uint16_t PPN = 0;
    if (STS_SEXI)
    {
        // Use lower 14-bit
        PPN = (uint16_t)(PTe & 0x3FFF);
    }
    else
    {
        // "normal" mode, use only the lower 9-bits
        PPN = (uint16_t)(PTe & 0x1FF);
    }

    PTe = PTe >> 16;

    if ((PTe & 1 << 15) != 0)
        snprintf(debugInfo + strlen(debugInfo), sizeof(debugInfo) - strlen(debugInfo), "%s", "[WPM]");
    if ((PTe & 1 << 14) != 0)
        snprintf(debugInfo + strlen(debugInfo), sizeof(debugInfo) - strlen(debugInfo), "%s", "[RPM]");
    if ((PTe & 1 << 13) != 0)
        snprintf(debugInfo + strlen(debugInfo), sizeof(debugInfo) - strlen(debugInfo), "%s", "[FPM]");
    if ((PTe & 1 << 12) != 0)
        snprintf(debugInfo + strlen(debugInfo), sizeof(debugInfo) - strlen(debugInfo), "%s", "[WIP]");
    if ((PTe & 1 << 11) != 0)
        snprintf(debugInfo + strlen(debugInfo), sizeof(debugInfo) - strlen(debugInfo), "%s", "[PGU]");

    int ring = (int)((PTe >> 9) & 0x03);
    snprintf(debugInfo, sizeof(debugInfo), "[R:%d]", ring);

    char ppnStr[16];
    snprintf(ppnStr, sizeof(ppnStr), "[PPN:0x%04X]", PPN);
    snprintf(debugInfo + strlen(debugInfo), sizeof(debugInfo) - strlen(debugInfo), "%s", ppnStr);

    uint32_t start = PPN << 10;
    uint32_t end = start | 0x3FFF;

    snprintf(memoryRange, sizeof(memoryRange), " MEM[0x%06X:0x%06X]", start, end);
    snprintf(debugInfo + strlen(debugInfo), sizeof(debugInfo) - strlen(debugInfo), "%s", memoryRange);

    return debugInfo;
}

char *GetPageTableMemoryRange(uint32_t PTe)
{
    static char debugInfo[256];
    debugInfo[0] = '\0';
    (void)PTe; /* decoding the entry is not implemented; callers get "" */

    return debugInfo;
}

static void add_page_mms_entries(DAPServer *server, char *info_message, size_t info_message_size)
{
    (void)info_message; (void)info_message_size;

    // Property kind with readonly attribute

    // Get PCR for current runlevel
    uint16_t rPCR = g_reg->reg_PCR[gPIL];
    uint16_t pt = 0, apt = 0;

    // decode PT and APT PCR
    if (rPCR & (1 << 2))
    {
        // Sixteen page table mode
        pt = (rPCR >> 11) & 0x0F;
        apt = (rPCR >> 7) & 0x0F;
    }
    else
    {
        // Four page table mode
        pt = (rPCR >> 9) & 0x03;
        apt = (rPCR >> 7) & 0x03;
    }

    char display_name[64];
    if (pt == apt)
    {
        snprintf(display_name, sizeof(display_name), "PT %d APT %d", pt, apt);
    }
    else
    {
        snprintf(display_name, sizeof(display_name), "PT %d", pt);
    }

    char pt_str[32];
    snprintf(pt_str, sizeof(pt_str), "%d", pt);

    add_variable_to_array(
        server,
        display_name,               // name with description
        pt_str,                     // value
        "memory",                   // type
        -1,                         // memoryReference (-1 = not present)
        SCOPE_ID_MEM_PT,            // variablesReference (no children)
        DAP_VARIABLE_KIND_PROPERTY, // kind
        DAP_VARIABLE_ATTR_NONE      // attributes
    );

    if (apt != pt)
    {
        snprintf(display_name, sizeof(display_name), "APT %d", apt);
        snprintf(pt_str, sizeof(pt_str), "%d", pt);

        add_variable_to_array(
            server,
            display_name,               // name with description
            pt_str,                     // value
            "memory",                   // type
            -1,                         // memoryReference (-1 = not present)
            SCOPE_ID_MEM_APT,           // variablesReference (no children)
            DAP_VARIABLE_KIND_PROPERTY, // kind
            DAP_VARIABLE_ATTR_NONE      // attributes
        );
    }
}

/**
 * @brief Add page table entries to the variables array
 *
 * @param server The DAP server instance
 * @param info_message Buffer to write info message
 * @param info_message_size Size of info message buffer
 */
static void add_page_table_entries(DAPServer *server, char *info_message, size_t info_message_size, bool useAPT)
{
    (void)info_message; (void)info_message_size;

    // Property kind with readonly attribute

    // Get PCR for current runlevel
    uint16_t rPCR = g_reg->reg_PCR[gPIL];
    uint16_t pt = 0, apt = 0;
    PageTableMode ptm = Four; // Default to four page tables

    // decode PT and APT PCR
    if (rPCR & (1 << 2))
    {
        // Sixteen page table mode
        pt = (rPCR >> 11) & 0x0F;
        apt = (rPCR >> 7) & 0x0F;
        ptm = Sixteen;
    }
    else
    {
        // Four page table mode
        pt = (rPCR >> 9) & 0x03;
        apt = (rPCR >> 7) & 0x03;
        ptm = Four;
    }

    if (useAPT)
    {
        pt = apt;
    }

    for (int vpn = 0; vpn < 64; vpn++)
    {
        uint32_t pageTableEntry = GetPageTableEntry(pt, vpn, ptm);

        char vpn_str[32];
        snprintf(vpn_str, sizeof(vpn_str), "%d", vpn);

        add_variable_to_array(
            server,
            vpn_str,                                    // name
            GetPageTableEntryInfo(pageTableEntry), // value
            "memory",                                   // type
            -1,                                         // memoryReference (-1 = not present)
            0,                                          // variablesReference
            DAP_VARIABLE_KIND_PROPERTY,                 // kind
            DAP_VARIABLE_ATTR_NONE                      // attributes
        );
    }
}

/**
 * @brief Handle DAP variables request
 *
 * This function provides variables for the requested container (scope).
 * It creates variables based on CPU state, memory content, etc.
 *
 * @param server The DAP server instance
 * @return int 0 on success, non-zero on failure
 */
static int cmd_variables(DAPServer *server)
{
    if (!server)
    {
        return -1;
    }

    // Extract variables reference from the command context
    int variables_reference = server->current_command.context.variables.variables_reference;


    // Buffer for informational messages
    char info_message[256] = {0};

    // Reset variable array count
    server->current_command.context.variables.variable_count = 0;
    server->current_command.context.variables.variable_array = NULL;

    // Handle different variable reference types
    switch (variables_reference)
    {
    case SCOPE_ID_LOCALS:
    {
        // Use our helper function for local variables (C debug info or assembly)
        add_local_variables(server, info_message, sizeof(info_message));
        break;
    }

    case SCOPE_ID_REGISTERS:
    {
        // Use our helper function for register variables
        add_register_variables(server, info_message, sizeof(info_message));
        break;
    }

    case SCOPE_ID_LEVELS:
    {
        // Use our helper function for level variables
        add_level_variables(server, info_message, sizeof(info_message));
        break;
    }
    case SCOPE_ID_INTERNAL_REGISTERS_READ:
    {
        // Use our helper function for internal registers variables
        add_internal_registers_read_variables(server, info_message, sizeof(info_message));
        break;
    }
    case SCOPE_ID_INTERNAL_REGISTERS_WRITE:
    {
        // Use our helper function for internal registers variables
        add_internal_registers_write_variables(server, info_message, sizeof(info_message));
        break;
    }

    case SCOPE_ID_STATUS_FLAGS:
    {
        // Use our helper function for status flag variables
        add_status_flag_variables(server, info_message, sizeof(info_message));
        break;
    }

    case SCOPE_ID_MEM_MMS:
    {
        // Fetch page table entries for PT
        add_page_mms_entries(server, info_message, sizeof(info_message));
        break;
    }

    case SCOPE_ID_MEM_PT:
    {
        // Fetch page table entries for PT
        add_page_table_entries(server, info_message, sizeof(info_message), false);
        break;
    }

    case SCOPE_ID_MEM_APT:
    {
        // Fetch page table entries for APT
        add_page_table_entries(server, info_message, sizeof(info_message), true);
        break;
    }

    default:
        if (variables_reference >= SCOPE_ID_PIL_BASE && variables_reference <= SCOPE_ID_PIL_END) {
            add_pil_register_variables(server, variables_reference - SCOPE_ID_PIL_BASE);
        } else {
            dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, "Unknown variable reference\n");
        }
        break;
    }

    // Output info message if we have one
    if (info_message[0] != '\0')
    {
        dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, info_message);
    }

    // The variables will be used by handle_variables and converted to JSON
    // We don't need to clean up here, as the caller (handle_variables) will handle it
    // This is intentional to allow the response to be sent before freeing the memory

    return 0;
}

/// @brief Update a DAP response stack frame with the given memory reference
/// @param server DAP server instance
/// @param frame_index Index of the frame in the stack trace
/// @param frame_id Unique ID of the frame
/// @param memory_reference Memory reference of the frame
/// @param entry_point Entry point of the frame
/// @return 0 on success, -1 on failure
void update_stack_frame(DAPServer *server, int frame_index, int frame_id, uint16_t memory_reference, uint16_t entry_point)
{
    // Initialize the frame
    DAPStackFrame *frame = &server->current_command.context.stack_trace.frames[frame_index];
    frame->id = frame_id;
    frame->name = NULL;
    frame->source_path = NULL;
    frame->source_name = NULL;
    frame->line = 0;
    frame->column = 0;
    frame->end_line = 0;
    frame->end_column = 0;
    frame->can_restart = false;
    frame->instruction_pointer_reference = -1; // -1 means no instruction pointer reference
    frame->module_id = NULL;
    frame->presentation_hint = DAP_FRAME_PRESENTATION_NORMAL;

    // Try to get symbol information if we have a symbol table
    // Get source location information
    int line = symbols_get_line(s_symbol_tables.symbol_table_map, memory_reference);
    const char *file = symbols_get_file(s_symbol_tables.symbol_table_map, memory_reference);
    const symbol_entry_t *symbol = symbols_lookup_by_address(s_symbol_tables.symbol_table_aout, entry_point);

    if (line > 0 && file)
    {
        // We found source information
        frame->line = line;
        frame->source_path = resolve_source_path(server, file);

        // Extract source name from resolved path
        const char *name = frame->source_path ? strrchr(frame->source_path, '/') : NULL;
        if (name)
        {
            frame->source_name = strdup(name + 1);
        }
        else
        {
            frame->source_name = strdup(file);
        }

        // Create a frame name from the symbol or PC

        char frame_name[256];

        // If we have a symbol, use it to create a frame name
        if (symbol && symbol->name)
        {
            snprintf(frame_name, sizeof(frame_name), "%s at %06o", symbol->name, memory_reference);
        }
        else
        {
            // No symbol, use PC as frame name
            if (symbol && symbol->name)
            {
                snprintf(frame_name, sizeof(frame_name), "%s at %06o", symbol->name, memory_reference);
            }
            else
            {
                snprintf(frame_name, sizeof(frame_name), "frame at %06o", memory_reference);
            }
        }

        frame->name = strdup(frame_name);

        // Log the source mapping
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "PC %06o mapped to %s:%d\n",
                 memory_reference, file, line);
        dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, log_msg);
    }
    else
    {
        // No source information found, use PC as frame name
        char frame_name[256];
        snprintf(frame_name, sizeof(frame_name), "frame at %06o", memory_reference);
        frame->name = strdup(frame_name);
    }

    // Set instruction pointer reference (Enables disassembly of the current instruction)
    frame->instruction_pointer_reference = memory_reference;
}

/// @brief Floor line-table lookup constrained to a function's [lo,hi] range.
///
/// Mirrors libsymbols' find_source_entry (highest LINE entry whose address is
/// <= addr) but rejects any entry outside [lo,hi]. Used for stackTrace frame
/// attribution so a PC sitting in a gap between functions is not labelled with
/// an adjacent compilation unit's file/line (the line table is global; a plain
/// nearest-match bleeds across object boundaries). Returns NULL if no LINE
/// entry of this function precedes addr.
static const symbol_entry_t *
frame_line_entry_in_range(const symbol_table_t *t, uint16_t addr,
                          uint16_t lo, uint16_t hi)
{
    if (!t)
        return NULL;
    const symbol_entry_t *floor = NULL;
    for (size_t i = 0; i < t->count; i++)
    {
        const symbol_entry_t *e = &t->entries[i];
        if (e->type != SYMBOL_TYPE_LINE)
            continue;
        if (e->address > addr)          // only entries at/below the query PC
            continue;
        if (e->address < lo || e->address > hi)  // must belong to this function
            continue;
        if (!floor || e->address > floor->address ||
            (e->address == floor->address && e->line > floor->line))
            floor = e;
    }
    return floor;
}

/**
 * @brief Walk the B-register chain to build a C-level call stack.
 *
 * The ND-100 csav/cret calling convention stores:
 *   B[0] = saved old B (frame link)
 *   B[1] = return address
 *
 * We walk this chain from the current B register value to
 * reconstruct the full C call stack. Results are written into
 * the stack_trace circular buffer, replacing the existing frames.
 */
static void rebuild_stack_from_b_chain(void)
{
    if (!s_symbol_tables.debug_info)
        return;

    /* Only rebuild if current PC is inside a known C function */
    symbol_function_t *cur_func = symbols_find_function_at(
        s_symbol_tables.debug_info, gPC);
    if (!cur_func)
        return;

    /*
     * First pass: collect frames into a temporary array.
     * Frame 0 = current (newest), Frame N = oldest.
     */
    struct {
        uint16_t pc;
        uint16_t entry_point;
        uint16_t return_address;
        uint16_t b_reg;
    } tmp_frames[MAX_STACK_FRAMES];
    int nframes = 0;

    /* Frame 0: current location */
    tmp_frames[0].pc = gPC;
    tmp_frames[0].entry_point = cur_func->start_address;
    tmp_frames[0].return_address = 0;
    tmp_frames[0].b_reg = gB;
    nframes = 1;

    /* Walk B-register chain */
    uint16_t current_b = gB;

    while (nframes < MAX_STACK_FRAMES && current_b != 0)
    {
        // Trap-free debugger reads (see dbg_read_data). A corrupted B-chain
        // points at unmapped/protected pages; the plain ReadVirtualMemory() path
        // would raise an emulated PF/MPV -> interrupt(14) -> longjmp(cpu_jmp_buf),
        // tearing out of this DAP handler and tripping the stack protector.
        // dbg_read_data returns -1 on a bad page, making the guard below fire.
        int old_b_word = dbg_read_data(current_b);
        int ret_addr_word = dbg_read_data(current_b + 1);

        if (old_b_word == -1 || ret_addr_word == -1)
            break;

        uint16_t old_b = (uint16_t)old_b_word;
        uint16_t ret_addr = (uint16_t)ret_addr_word;

        /* Sanity: B should not point to itself */
        if (old_b == current_b)
            break;

        /* Return address in the calling function */
        tmp_frames[nframes].pc = ret_addr;
        tmp_frames[nframes].return_address = ret_addr;
        tmp_frames[nframes].b_reg = old_b;

        symbol_function_t *fn = symbols_find_function_at(
            s_symbol_tables.debug_info, ret_addr);
        tmp_frames[nframes].entry_point =
            fn ? fn->start_address : ret_addr;

        nframes++;

        if (old_b == 0)
            break;
        current_b = old_b;
    }

    /*
     * Second pass: store into the circular buffer in the order
     * the existing cmd_stack_trace expects.
     * Index 0 = oldest, current_frame = newest.
     */
    memset(&s_stack_trace, 0, sizeof(s_stack_trace));
    s_stack_trace.frame_count = nframes;

    for (int i = 0; i < nframes; i++)
    {
        /* Oldest frame (tmp_frames[nframes-1]) goes to index 0,
         * newest (tmp_frames[0]) goes to index nframes-1. */
        int dst = nframes - 1 - i;
        s_stack_trace.frames[dst].pc = tmp_frames[i].pc;
        s_stack_trace.frames[dst].entry_point = tmp_frames[i].entry_point;
        s_stack_trace.frames[dst].return_address = tmp_frames[i].return_address;
        s_stack_trace.frames[dst].b_reg = tmp_frames[i].b_reg;
        s_stack_trace.frames[dst].operand = 0;
    }
    s_stack_trace.current_frame = nframes - 1;
}

/**
 * @brief Handle DAP stackTrace request
 *
 * This function creates a stack trace response with the current call stack,
 * including source file locations and line numbers. In this implementation,
 * we create at least one stack frame for the current PC location.
 *
 * @param server The DAP server instance
 * @return int 0 on success, -1 on failure
 */
static int cmd_stack_trace(DAPServer *server)
{
    if (!server)
    {
        dap_server_send_output_category(server, DAP_OUTPUT_STDERR,
                                        "Error: Invalid server instance\n");
        return -1;
    }

    /* If we have C debug info, rebuild stack from B-register chain */
    if (s_symbol_tables.debug_info)
    {
        rebuild_stack_from_b_chain();
    }

    // Validate stack trace availability
    if (s_stack_trace.frame_count == 0)
    {
        dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE,
                                        "No stack trace available\n");
        return -1;
    }

    // Extract and validate request parameters
    int stack_levels = server->current_command.context.stack_trace.levels;
    int stack_start_frame = server->current_command.context.stack_trace.start_frame;
    int frame_count = s_stack_trace.frame_count;

    // Validate input parameters
    if (stack_start_frame < 0)
    {
        dap_server_send_output_category(server, DAP_OUTPUT_STDERR,
                                        "Error: Invalid start frame requested\n");
        return -1;
    }

    // Calculate number of frames to return
    // Per DAP spec: levels == 0 means return all frames
    int frames_to_return;
    if (stack_levels <= 0)
    {
        frames_to_return = frame_count - stack_start_frame;
    }
    else
    {
        frames_to_return = stack_levels;
        if (frames_to_return > (frame_count - stack_start_frame))
        {
            frames_to_return = frame_count - stack_start_frame;
        }
    }

    // If no frames to return after calculations, return empty result
    if (frames_to_return <= 0)
    {
        server->current_command.context.stack_trace.frame_count = 0;
        server->current_command.context.stack_trace.total_frames = frame_count;
        server->current_command.context.stack_trace.frames = NULL;
        return 0;
    }

    // Allocate memory for the stack frames
    server->current_command.context.stack_trace.frame_count = frames_to_return;
    server->current_command.context.stack_trace.total_frames = frame_count;
    server->current_command.context.stack_trace.frames = malloc(sizeof(DAPStackFrame) * frames_to_return);

    // Check for allocation failure
    if (!server->current_command.context.stack_trace.frames)
    {
        dap_server_send_output_category(server, DAP_OUTPUT_STDERR,
                                        "Error: Failed to allocate memory for stack frames\n");
        return -1;
    }

    // Fill in the stack frames in reverse order (most recent first)
    for (int i = 0; i < frames_to_return; i++)
    {
        // Calculate frame index - we want to go from newest to oldest
        // current_frame points to the newest frame, so we start there and go backwards
        int frame_idx = (s_stack_trace.current_frame - i + MAX_STACK_FRAMES) % MAX_STACK_FRAMES;

        // For the currrent frame, update PC
        if (frame_idx == s_stack_trace.current_frame)
        {
            s_stack_trace.frames[frame_idx].pc = gPC;
        }

        uint16_t memory_reference = s_stack_trace.frames[frame_idx].pc;
        uint16_t entry_point = s_stack_trace.frames[frame_idx].entry_point;

        // Update frame with enhanced information
        DAPStackFrame *frame = &server->current_command.context.stack_trace.frames[i];
        frame->id = frame_idx;
        frame->name = NULL;
        frame->source_path = NULL;
        frame->source_name = NULL;
        frame->line = 0;
        frame->column = 0;
        frame->end_line = 0;
        frame->end_column = 0;
        frame->can_restart = false;
        frame->instruction_pointer_reference = memory_reference;
        frame->module_id = NULL;
        frame->presentation_hint = DAP_FRAME_PRESENTATION_NORMAL;

        // symbols_dump_all(symbol_tables.symbol_table_aout);

        // Try to get symbol information - check multiple symbol tables
        const symbol_entry_t *symbol = NULL;
        const char *c_func_name = NULL;

        // Try C debug info first (most specific for C programs)
        if (s_symbol_tables.debug_info) {
            symbol_function_t *cfn = symbols_find_function_at(
                s_symbol_tables.debug_info, entry_point);
            if (cfn)
                c_func_name = cfn->name;
        }

        // Try AOUT symbols (most common for binaries)
        if (!c_func_name && s_symbol_tables.symbol_table_aout) {
            symbol = symbols_lookup_by_address(s_symbol_tables.symbol_table_aout, entry_point);
        }

        // Try MAP symbols (for assembly programs)
        if (!c_func_name && (!symbol || !symbol->name) && s_symbol_tables.symbol_table_map) {
            symbol = symbols_lookup_by_address(s_symbol_tables.symbol_table_map, entry_point);
        }

        // Try STABS symbols as last resort
        if (!c_func_name && (!symbol || !symbol->name) && s_symbol_tables.symbol_table_stabs) {
            symbol = symbols_lookup_by_address(s_symbol_tables.symbol_table_stabs, entry_point);
        }

        if (c_func_name)
        {
            frame->name = strdup(c_func_name);
            frame->valid_symbol = true;
            frame->symbol_entry_point = entry_point;
        }
        else if (symbol && symbol->name)
        {
            frame->name = strdup(symbol->name);
            frame->valid_symbol = true;
            frame->symbol_entry_point = entry_point;
        }
        else
        {
            frame->name = strdup("<unknown>");
            frame->valid_symbol = false;
        }

        /*
        for (int i = 0; i < symbol_tables.symbol_table_aout->count; i++)
        {
            const symbol_entry_t *symbol = &symbol_tables.symbol_table_aout->entries[i];
            if (symbol->address == entry_point)
            {
                LOG(LOG_CAT_DAP, LOG_DEBUG, "Symbol found: %s at %06o\n", symbol->name, symbol->address);
            }
        }
        */

        // Get source location information.
        int line = 0;
        const char *file = NULL;

        // If we have C debug info, resolve the file/line WITHIN the containing
        // function's address range. The line table is global, so a plain
        // nearest-match attributes a PC in an inter-function gap (or a garbage
        // frame from a corrupted stack) to an adjacent unit's file/line. Bound
        // it to [start_address, end_address] to keep frame attribution honest.
        symbol_function_t *lfn = s_symbol_tables.debug_info ?
            symbols_find_function_at(s_symbol_tables.debug_info, memory_reference) :
            NULL;

        if (s_symbol_tables.debug_info && !lfn)
        {
            // C debug info present, but this PC is outside every known C
            // function (garbage / assembly-rooted frame): leave source blank
            // rather than mislabel it with a neighbouring symbol's line.
        }
        else if (lfn)
        {
            const symbol_table_t *tbls[3] = {
                s_symbol_tables.symbol_table_stabs,   // C line entries live here
                s_symbol_tables.symbol_table_map,
                s_symbol_tables.symbol_table_aout,
            };
            for (int t = 0; t < 3 && (!line || !file); t++)
            {
                const symbol_entry_t *e = frame_line_entry_in_range(
                    tbls[t], memory_reference,
                    lfn->start_address, lfn->end_address);
                if (e) { line = e->line; file = e->filename; }
            }
        }
        else
        {
            // No C debug info (assembly / SINTRAN): original nearest-match
            // across MAP -> STABS -> AOUT.
            if (s_symbol_tables.symbol_table_map) {
                line = symbols_get_line(s_symbol_tables.symbol_table_map, memory_reference);
                file = symbols_get_file(s_symbol_tables.symbol_table_map, memory_reference);
            }
            if ((!line || !file) && s_symbol_tables.symbol_table_stabs) {
                line = symbols_get_line(s_symbol_tables.symbol_table_stabs, memory_reference);
                file = symbols_get_file(s_symbol_tables.symbol_table_stabs, memory_reference);
            }
            if ((!line || !file) && s_symbol_tables.symbol_table_aout) {
                line = symbols_get_line(s_symbol_tables.symbol_table_aout, memory_reference);
                file = symbols_get_file(s_symbol_tables.symbol_table_aout, memory_reference);
            }
        }

        if (line > 0 && file)
        {
            // We found source information
            frame->line = line;

            // Resolve basename to full path using source_paths
            frame->source_path = resolve_source_path(server, file);

            // Extract source name from resolved path
            const char *name = frame->source_path ? strrchr(frame->source_path, '/') : NULL;
            if (name)
            {
                frame->source_name = strdup(name + 1);
            }
            else
            {
                frame->source_name = strdup(file);
            }
        }

        // Set presentation hint based on frame type
        if (symbol && symbol->type == SYMBOL_TYPE_FUNCTION)
        {
            frame->presentation_hint = DAP_FRAME_PRESENTATION_NORMAL;
        }
        else
        {
            frame->presentation_hint = DAP_FRAME_PRESENTATION_LABEL;
        }
    }

    return 0;
}

/**
 * @brief Callback for setting exception breakpoints
 *
 * @param server The DAP server
 * @return int 0 on success, non-zero on failure
 */
static int on_set_exception_breakpoints(DAPServer *server)
{
    if (!server)
    {
        return -1;
    }

    // Access filter data from the server's current command context
    size_t filter_count = server->current_command.context.exception.filter_count;
    size_t condition_count = server->current_command.context.exception.condition_count;

    // Log the received exception filters
    LOG(LOG_CAT_DAP, LOG_DEBUG, "Received %zu exception filters and %zu conditions\n", filter_count, condition_count);

    // TODO: Implement exception breakpoint handling
    return 0;
}

/**
 * @brief Command callback for setting breakpoints
 *
 * @param server DAP server instance
 * @return int 0 on success, non-zero on failure
 */
static int cmd_set_breakpoints(DAPServer *server)
{
    if (!server)
    {
        return -1;
    }

    // Extract source file info from breakpoint context
    const char *source_path = server->current_command.context.breakpoint.source_path;
    int breakpoint_count = server->current_command.context.breakpoint.breakpoint_count;

    if (!source_path || breakpoint_count <= 0)
    {
        LOG(LOG_CAT_DAP, LOG_WARN, "Missing required breakpoint information\n");
        return -1;
    }

    // Get filename from path
    const char *source_name = strrchr(source_path, '/');
    if (source_name)
    {
        source_name++; // Skip the slash
    }
    else
    {
        source_name = source_path; // No slash found, use the whole path
    }

    LOG(LOG_CAT_DAP, LOG_DEBUG, "Setting %d breakpoints in %s\n", breakpoint_count, source_path);

    // Clear existing source (user) breakpoints only, preserve instruction/function/data BPs
    breakpoint_manager_clear_type(BP_TYPE_USER);

    // Process each breakpoint
    for (int i = 0; i < breakpoint_count; i++)
    {
        DAPBreakpoint *bp = &server->current_command.context.breakpoint.breakpoints[i];

        bool validSymbol = false;
        uint16_t address = 0;
        uint16_t diff = 0;

        // Try multiple symbol tables in order of preference

        // 1. Try STABS (most detailed for C/mixed programs)
        if (!validSymbol && s_symbol_tables.symbol_table_stabs) {
            validSymbol = symbols_find_address(s_symbol_tables.symbol_table_stabs,
                                              source_path, &address, &diff, bp->line);
        }

        // 2. Try MAP file (reliable for assembly)
        if (!validSymbol && s_symbol_tables.symbol_table_map) {
            validSymbol = symbols_find_address(s_symbol_tables.symbol_table_map,
                                              source_path, &address, &diff, bp->line);
        }

        // 3. Try AOUT (last resort - function symbols)
        if (!validSymbol && s_symbol_tables.symbol_table_aout && str_ends_with(source_path, ".s")) {
            // For assembly files, try to find by label/function name
            // This is a fallback for when line mapping doesn't work
            validSymbol = symbols_find_address(s_symbol_tables.symbol_table_aout,
                                              source_path, &address, &diff, bp->line);
        }

        if (!validSymbol)
        {
            char msg[256];
            snprintf(msg, sizeof(msg),
                    "Warning: Could not map %s line %d to memory address (no symbol table entry)\n",
                    source_path, bp->line);
            dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, msg);

            bp->verified = false;
            bp->message = strdup(msg);
            continue;
        }

        /* Accept inexact matches: the srcmap often has multiple source lines
         * mapping to the same address (e.g., function prologue), and the symbol
         * table deduplicates by address, keeping only the first line.  A small
         * diff is normal and the resolved address is still correct. */
        if (diff != 0 && diff <= 10)
        {
            char msg[256];
            snprintf(msg, sizeof(msg),
                    "Mapped %s line %d to nearest address %06o (nearest line differs by %d)\n",
                    source_path, bp->line, address, diff);
            dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, msg);
        }
        else if (diff > 10)
        {
            char msg[256];
            snprintf(msg, sizeof(msg),
                    "Warning: Could not map %s line %d to memory address (nearest line differs by %d)\n",
                    source_path, bp->line, diff);
            dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, msg);

            bp->verified = false;
            bp->message = strdup(msg);
            continue;
        }

        bp->verified = true;

        // Add the breakpoint to the manager
        breakpoint_manager_add(
            address,           // Memory address
            BP_TYPE_USER,      // Type of breakpoint
            bp->condition,     // Optional condition expression
            bp->hit_condition, // Optional hit condition
            bp->log_message    // Optional log message
        );

        // Log the breakpoint addition
        char msg[256];
        snprintf(msg, sizeof(msg), "Added breakpoint at address %06o (line %d)\n",
                 address, bp->line);
        dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, msg);
    }

    // Send console output about the setup
    char output_msg[256];
    snprintf(output_msg, sizeof(output_msg), "Set %d breakpoints in %s\n",
             breakpoint_count, source_name);
    dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, output_msg);

    return 0;
}

static int cmd_set_instruction_breakpoints(DAPServer *server)
{
    if (!server)
    {
        return -1;
    }

    InstructionBreakpointCommandContext *ctx = &server->current_command.context.instruction_breakpoint;
    int count = ctx->breakpoint_count;

    if (count <= 0)
    {
        // Clear instruction breakpoints only, preserve source/function/data BPs
        breakpoint_manager_clear_type(BP_TYPE_INSTRUCTION);
        dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE,
                                        "Cleared all instruction breakpoints\n");
        return 0;
    }

    // Clear existing instruction breakpoints and set new ones
    breakpoint_manager_clear_type(BP_TYPE_INSTRUCTION);

    for (int i = 0; i < count; i++)
    {
        uint16_t address = (uint16_t)(ctx->addresses[i] + (ctx->offsets ? ctx->offsets[i] : 0));

        breakpoint_manager_add(
            address,
            BP_TYPE_INSTRUCTION,
            ctx->conditions ? ctx->conditions[i] : NULL,
            NULL, // hit condition
            NULL  // log message
        );

        ctx->breakpoints[i].verified = true;
        ctx->breakpoints[i].instruction_reference = address;

        char msg[256];
        snprintf(msg, sizeof(msg), "Added instruction breakpoint at address %06o\n", address);
        dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, msg);
    }

    char output_msg[256];
    snprintf(output_msg, sizeof(output_msg), "Set %d instruction breakpoints\n", count);
    dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, output_msg);

    return 0;
}

/**
 * @brief Handle dataBreakpointInfo request - query if a data breakpoint is supported
 *
 * For nd100x, we support watchpoints on any 16-bit memory address.
 * The dataId is the address as an octal string (matching nd100x convention).
 */
static int cmd_data_breakpoint_info(DAPServer *server)
{
    if (!server)
    {
        return -1;
    }

    DataBreakpointInfoCommandContext *ctx = &server->current_command.context.data_breakpoint_info;

    // Resolve name to address: try symbol lookup first, then numeric parse
    uint32_t address = 0;
    bool valid = false;
    const char *resolved_name = NULL;

    if (ctx->name)
    {
        // Strip address space prefix if present
        const char *lookup_name = ctx->name;
        if (strncmp(lookup_name, "phys:", 5) == 0) lookup_name += 5;
        else if (strncmp(lookup_name, "P:", 2) == 0) lookup_name += 2;
        else if (strncmp(lookup_name, "ispace:", 7) == 0) lookup_name += 7;
        else if (strncmp(lookup_name, "I:", 2) == 0) lookup_name += 2;
        else if (strncmp(lookup_name, "dspace:", 7) == 0) lookup_name += 7;
        else if (strncmp(lookup_name, "D:", 2) == 0) lookup_name += 2;

        // Try symbol lookup across all loaded symbol tables
        const symbol_entry_t *sym = NULL;
        if (!sym && s_symbol_tables.symbol_table_aout)
        {
            sym = symbols_lookup_by_name(s_symbol_tables.symbol_table_aout, lookup_name);
        }
        if (!sym && s_symbol_tables.symbol_table_map)
        {
            sym = symbols_lookup_by_name(s_symbol_tables.symbol_table_map, lookup_name);
        }
        if (!sym && s_symbol_tables.symbol_table_stabs)
        {
            sym = symbols_lookup_by_name(s_symbol_tables.symbol_table_stabs, lookup_name);
        }

        if (sym)
        {
            address = sym->address;
            resolved_name = sym->name;
            valid = true;
        }
        else
        {
            // Fall back to parsing as numeric address (hex 0x..., octal, decimal)
            char *endptr = NULL;
            unsigned long val = strtoul(lookup_name, &endptr, 0);
            if (endptr && endptr != lookup_name && *endptr == '\0')
            {
                address = (uint32_t)val;
                valid = true;
            }
        }
    }

    if (valid)
    {
        // Determine address space from prefix
        char space_char = 'V';
        const char *space = "virtual";
        if (ctx->name) {
            if (strncmp(ctx->name, "phys:", 5) == 0 || strncmp(ctx->name, "P:", 2) == 0) {
                space_char = 'P'; space = "physical";
            } else if (strncmp(ctx->name, "ispace:", 7) == 0 || strncmp(ctx->name, "I:", 2) == 0) {
                space_char = 'I'; space = "ispace";
            } else if (strncmp(ctx->name, "dspace:", 7) == 0 || strncmp(ctx->name, "D:", 2) == 0) {
                space_char = 'D'; space = "dspace";
            }
        }

        // Return a dataId that encodes address space + address (octal)
        // Format: "V:000040" virtual, "P:000040" physical, "I:000040" ispace, "D:000040" dspace
        char data_id[32];
        snprintf(data_id, sizeof(data_id), "%c:%06o", space_char, address);
        ctx->data_id = strdup(data_id);

        char desc[128];
        if (resolved_name)
        {
            snprintf(desc, sizeof(desc), "Watch '%s' at %s address %06o", resolved_name, space, address);
        }
        else
        {
            snprintf(desc, sizeof(desc), "Watch %s memory at address %06o", space, address);
        }
        ctx->description = strdup(desc);

        // nd100x supports all access types
        ctx->supports_read = true;
        ctx->supports_write = true;
        ctx->supports_read_write = true;
        ctx->can_persist = false;
    }
    else
    {
        // Not a valid symbol or address
        ctx->data_id = NULL;
        char desc[256];
        snprintf(desc, sizeof(desc), "Unknown symbol or address: '%s'", ctx->name ? ctx->name : "");
        ctx->description = strdup(desc);
        ctx->supports_read = false;
        ctx->supports_write = false;
        ctx->supports_read_write = false;
        ctx->can_persist = false;
    }

    return 0;
}

/**
 * @brief Handle setDataBreakpoints request - set memory watchpoints
 *
 * Uses the nd100x watchpoint API (watchpoint_add/remove/clear) to set
 * hardware-style memory watchpoints for read, write, or readWrite access.
 */
static int cmd_set_data_breakpoints(DAPServer *server)
{
    if (!server)
    {
        return -1;
    }

    SetDataBreakpointsCommandContext *ctx = &server->current_command.context.set_data_breakpoints;
    int count = ctx->breakpoint_count;

    // Always clear existing watchpoints first (DAP spec: setDataBreakpoints replaces all)
    watchpoint_clear();
    phys_watchpoint_clear();

    if (count <= 0)
    {
        dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE,
                                        "Cleared all data breakpoints (watchpoints)\n");
        return 0;
    }

    for (int i = 0; i < count; i++)
    {
        if (!ctx->data_ids[i])
        {
            ctx->breakpoints[i].verified = false;
            ctx->breakpoints[i].message = strdup("Missing dataId");
            continue;
        }

        // Parse dataId: "V:000040", "P:000040", "I:000040", "D:000040"
        // Optional @PIL suffix: "V:000040@1", "I:135140@0"
        bool is_physical = false;
        WatchpointSpace wp_space = WATCH_SPACE_ANY;
        const char *addr_str = ctx->data_ids[i];

        if (addr_str[0] == 'P' && addr_str[1] == ':')
        {
            is_physical = true;
            addr_str += 2;
        }
        else if (addr_str[0] == 'I' && addr_str[1] == ':')
        {
            wp_space = WATCH_SPACE_ISPACE;
            addr_str += 2;
        }
        else if (addr_str[0] == 'D' && addr_str[1] == ':')
        {
            wp_space = WATCH_SPACE_DSPACE;
            addr_str += 2;
        }
        else if (addr_str[0] == 'V' && addr_str[1] == ':')
        {
            addr_str += 2;
        }

        char *endptr = NULL;
        unsigned long val = strtoul(addr_str, &endptr, 8); // octal
        if (!endptr || endptr == addr_str)
        {
            ctx->breakpoints[i].verified = false;
            ctx->breakpoints[i].message = strdup("Invalid dataId format");
            continue;
        }

        // Parse optional @PIL suffix
        int8_t wp_pil = -1;
        if (endptr && *endptr == '@')
        {
            int p = (int)strtol(endptr + 1, NULL, 10);
            if (p >= 0 && p <= 15) wp_pil = (int8_t)p;
        }

        // Determine watchpoint type from accessType string
        WatchpointType wp_type = WATCH_WRITE; // default
        if (ctx->access_types[i])
        {
            if (strcmp(ctx->access_types[i], "read") == 0)
            {
                wp_type = WATCH_READ;
            }
            else if (strcmp(ctx->access_types[i], "readWrite") == 0)
            {
                wp_type = WATCH_READWRITE;
            }
        }

        int result;
        if (is_physical)
        {
            result = phys_watchpoint_add((uint32_t)val, wp_type, wp_pil);
        }
        else
        {
            result = watchpoint_add((uint16_t)(val & 0xFFFF), wp_type, wp_space, wp_pil);
        }

        if (result < 0)
        {
            ctx->breakpoints[i].verified = false;
            ctx->breakpoints[i].message = strdup("Failed to add watchpoint (limit reached?)");
        }
        else
        {
            ctx->breakpoints[i].verified = true;

            const char *type_str = "write";
            if (wp_type == WATCH_READ) type_str = "read";
            else if (wp_type == WATCH_READWRITE) type_str = "readWrite";

            const char *space = is_physical ? "physical" : "virtual";
            char msg[256];
            snprintf(msg, sizeof(msg), "Watchpoint set at %s address %06lo (access: %s)\n",
                     space, val, type_str);
            dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, msg);
        }
    }

    char output_msg[256];
    snprintf(output_msg, sizeof(output_msg), "Set %d data breakpoints (watchpoints)\n", count);
    dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, output_msg);

    return 0;
}

void free_symbol_table(void)
{
    if (s_symbol_tables.symbol_table_map)
    {
        symbols_free(s_symbol_tables.symbol_table_map);
        s_symbol_tables.symbol_table_map = NULL;
    }

    if (s_symbol_tables.symbol_table_aout)
    {
        symbols_free(s_symbol_tables.symbol_table_aout);
        s_symbol_tables.symbol_table_aout = NULL;
    }

    if (s_symbol_tables.symbol_table_stabs)
    {
        symbols_free(s_symbol_tables.symbol_table_stabs);
        s_symbol_tables.symbol_table_stabs = NULL;
    }

    if (s_symbol_tables.debug_info)
    {
        symbols_debug_info_free(s_symbol_tables.debug_info);
        s_symbol_tables.debug_info = NULL;
    }
}

/**
 * @brief Initialize symbol table support for the debugger
 *
 * This function loads symbols from a specified file, trying all supported formats:
 * 1. Map file format
 * 2. a.out binary format
 * 3. STABS .s file format
 *
 * @param filename Path to the symbol file
 * @return int 0 on success, non-zero on failure
 */

int init_symbol_support(const char *filename, SymbolType symbol_type)
{
    if (!filename)
    {
        return -1;
    }

    // Allocate symbol table if it doesn't exist
    if (s_symbol_tables.symbol_table_map == NULL)
    {
        s_symbol_tables.symbol_table_map = symbols_create();
    }

    if (s_symbol_tables.symbol_table_aout == NULL)
    {
        s_symbol_tables.symbol_table_aout = symbols_create();
    }

    if (s_symbol_tables.symbol_table_stabs == NULL)
    {
        s_symbol_tables.symbol_table_stabs = symbols_create();
    }

    if (s_symbol_tables.symbol_table_map == NULL || s_symbol_tables.symbol_table_aout == NULL || s_symbol_tables.symbol_table_stabs == NULL)
    {
        LOG(LOG_CAT_DAP, LOG_ERROR, "Error: Failed to create symbol table\n");
        free_symbol_table();
        return -1;
    }

    // Try loading symbols from the file in different formats
    bool result = false;

    switch (symbol_type)
    {
    case SYMBOL_TYPE_MAP:
        result = symbols_load_map(s_symbol_tables.symbol_table_map, filename);
        break;

    case SYMBOL_TYPE_AOUT:
        result = symbols_load_aout(s_symbol_tables.symbol_table_aout, filename);
        break;

    case SYMBOL_TYPE_STABS:
        result = symbols_load_stabs(s_symbol_tables.symbol_table_stabs, filename);
        break;
    default:
        LOG(LOG_CAT_DAP, LOG_ERROR, "Error: Unsupported symbol type: %d\n", symbol_type);
        return -1;
    }

    if (result)
    {
        LOG(LOG_CAT_DAP, LOG_INFO, "Loaded symbols from %s\n", filename);
        return 0;
    }

    return -1;
}

/**
 * @brief Handle the launch request from DAP
 *
 * This function initializes the debugger environment for the specified program:
 * 1. Sets up necessary debugger state with program and source information
 * 2. Initializes execution context and resets debugger state
 * 3. Loads symbol information if map file is provided
 * 4. Sends appropriate events to the client
 *
 * @param server The DAP server instance
 * @return int 0 on success, non-zero on failure
 */
static int cmd_launch_callback(DAPServer *server)
{
    if (!server)
    {
        return -1;
    }

    LOG(LOG_CAT_DAP, LOG_DEBUG, "Launch command received\n");

    // Extract launch parameters from debugger state
    const char *program_path = server->debugger_state.program_path;
    const char *source_path = server->debugger_state.source_path;
    const char *map_path = server->debugger_state.map_path;
    bool stop_at_entry = server->debugger_state.stop_at_entry;
    bool no_debug = server->debugger_state.no_debug;

    // Log command line arguments if provided
    char **args = server->debugger_state.args;
    int args_count = server->debugger_state.args_count;

    if (!program_path)
    {
        LOG(LOG_CAT_DAP, LOG_ERROR, "Error: Missing program path in debugger state\n");
        return -1;
    }

    LOG(LOG_CAT_DAP, LOG_DEBUG, "Launching program: %s\n", program_path ? program_path : "(null)");
    LOG(LOG_CAT_DAP, LOG_DEBUG, "Source path: %s\n", source_path ? source_path : "(not specified)");
    LOG(LOG_CAT_DAP, LOG_DEBUG, "Map file: %s\n", map_path ? map_path : "(not specified)");
    LOG(LOG_CAT_DAP, LOG_DEBUG, "Stop at entry: %s\n", stop_at_entry ? "yes" : "no");
    LOG(LOG_CAT_DAP, LOG_DEBUG, "No debug: %s\n", no_debug ? "yes" : "no");

    // Log command line arguments if present
    if (args && args_count > 0)
    {
        char arg_log[1024] = "Command line arguments:";
        size_t log_pos = strlen(arg_log);

        for (int i = 0; i < args_count && i < 10; i++)
        { // Limit to 10 args in log
            if (args[i])
            {
                int written = snprintf(arg_log + log_pos, sizeof(arg_log) - log_pos,
                                       " '%s'", args[i]);
                if (written > 0)
                {
                    log_pos += written;
                }
            }
        }

        if (args_count > 10)
        {
            snprintf(arg_log + log_pos, sizeof(arg_log) - log_pos, " ... (%d more)",
                     args_count - 10);
        }

        LOG(LOG_CAT_DAP, LOG_DEBUG, "%s\n", arg_log);
    }

    // Reset CPU state to appropriate values
    // In a real debugger, this would be where we would initialize the CPU
    // with the program's binary data

    // Set debugger state
    server->is_running = true;
    server->attached = true;
    server->debugger_state.has_stopped = true;
    server->debugger_state.current_thread_id = 1; // make sure we have a thread id

    server->debugger_state.source_line = 1;   // Start at line 1
    server->debugger_state.source_column = 1; // Start at column 1
    // Try to load symbols using different approaches
    bool symbols_loaded = false;

    // Load program - only if it's a valid a.out binary.
    // For other boot types (floppy, smd, bpun) the emulator was already
    // booted via the command line before the debugger connected.
    bool is_aout = false;
    if (program_path)
    {
        FILE *probe = fopen(program_path, "rb");
        if (probe) {
            aout_header_t probe_hdr;
            if (load_header(probe, &probe_hdr, false) == 0) {
                switch (probe_hdr.a_magic) {
                case A_MAGIC1: case A_MAGIC2: case A_MAGIC3:
                case A_MAGIC4: case A_MAGIC5: case A_MAGIC6:
                    is_aout = true;
                    break;
                default:
                    break;
                }
            }
            fclose(probe);
        }

        if (is_aout) {
            LOG(LOG_CAT_DAP, LOG_DEBUG, "Attempting to load a.out program: %s\n", program_path);
            dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE,
                                            "Loading a.out program...\n");
            if (program_load(BOOT_AOUT, 0, program_path, true,
                             (uint16_t)server->debugger_state.text_start, false) < 0) {
                dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE,
                                                "Loading the a.out program failed.\n");
                return -1;
            }
            gPC = g_start_addr;
        } else {
            LOG(LOG_CAT_DAP, LOG_DEBUG, "Program file is not a.out format (skipping load, using existing boot): %s\n", program_path);
            dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE,
                                            "Program is not a.out format - attaching to running system.\n");
        }
    }

    // Clear old symbols and source references
    free_symbol_table();
    free_source_references();
    // Load symbols !!

    if (map_path)
    {
        LOG(LOG_CAT_DAP, LOG_DEBUG, "Attempting to load symbols from map file: %s\n", map_path);
        dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE,
                                        "Loading symbols from map file...\n");

        if (init_symbol_support(map_path, SYMBOL_TYPE_MAP) == 0)
        {
            symbols_loaded = true;

            // Send a detailed message about the symbols
            char message[256];
            snprintf(message, sizeof(message), "Successfully loaded symbols from map file: %s\n", map_path);
            dap_server_send_output_category(server, DAP_OUTPUT_IMPORTANT, message);

            // Try to load extended C debug info (FUNC/PARAM/LOCAL) from same srcmap
            s_symbol_tables.debug_info = symbols_debug_info_create();
            if (s_symbol_tables.debug_info &&
                symbols_load_srcmap_debug(s_symbol_tables.debug_info, map_path))
            {
                snprintf(message, sizeof(message),
                         "Loaded C debug info: %d functions\n",
                         s_symbol_tables.debug_info->function_count);
                dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, message);
            }
        }
    }

    // If we have a valid a.out file, try to update symbols from it
    if (program_path && is_aout)
    {
        LOG(LOG_CAT_DAP, LOG_DEBUG, "Attempting to load symbols from program binary: %s\n", program_path);
        dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE,
                                        "Loading symbols from program binary...\n");

        if (init_symbol_support(program_path, SYMBOL_TYPE_AOUT) == 0)
        {
            symbols_loaded = true;

            // Send a detailed message about the symbols
            char message[256];
            snprintf(message, sizeof(message), "Successfully loaded symbols from binary: %s\n", program_path);
            dap_server_send_output_category(server, DAP_OUTPUT_IMPORTANT, message);
        }
    }

    // Check for potential STABS file (look for .s file with same base name as program)
    if (!symbols_loaded && program_path)
    {
        char stabs_path[600] = {0};

        // Get base program name without extension
        const char *program_basename = strrchr(program_path, '/');
        if (program_basename)
        {
            program_basename++; // Skip the slash
        }
        else
        {
            program_basename = program_path;
        }

        // Find extension
        const char *extension = strrchr(program_basename, '.');
        if (extension)
        {
            // Take only the base name
            size_t base_len = extension - program_basename;
            char base_name[256] = {0};
            snprintf(base_name, sizeof(base_name), "%.*s", (int)(base_len < 255 ? base_len : 255), program_basename);

            // Create potential STABS file path (same directory, .s extension)
            char dir_path[256] = {0};
            if (program_basename > program_path)
            {
                // Copy directory part
                size_t dir_len = program_basename - program_path - 1; // -1 to exclude the slash
                snprintf(dir_path, sizeof(dir_path), "%.*s", (int)(dir_len < 255 ? dir_len : 255), program_path);
            }

            // Construct full STABS path
            if (dir_path[0])
            {
                snprintf(stabs_path, sizeof(stabs_path), "%s/%s.s", dir_path, base_name);
            }
            else
            {
                snprintf(stabs_path, sizeof(stabs_path), "%s.s", base_name);
            }

            // Try to load STABS file
            LOG(LOG_CAT_DAP, LOG_DEBUG, "Attempting to load symbols from STABS file: %s\n", stabs_path);
            dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE,
                                            "Looking for STABS debug file...\n");

            // Check if file exists before trying to load
            FILE *f = fopen(stabs_path, "r");
            if (f)
            {
                fclose(f);

                dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE,
                                                "Found STABS file, loading symbols...\n");

                if (init_symbol_support(stabs_path, SYMBOL_TYPE_STABS) == 0)
                {
                    symbols_loaded = true;

                    // Send a detailed message about the symbols
                    char message[1024];
                    snprintf(message, sizeof(message), "Successfully loaded symbols from STABS: %s\n", stabs_path);
                    dap_server_send_output_category(server, DAP_OUTPUT_IMPORTANT, message);
                }
            }
            else
            {
                dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE,
                                                "No STABS debug file found\n");
            }
        }
    }

    // If no symbols loaded from any source, warn the user
    if (!symbols_loaded)
    {
        dap_server_send_output_category(server, DAP_OUTPUT_STDERR,
                                        "Warning: No symbols loaded. Source level debugging may not work correctly.\n");
    }
    // Update the server's program counter from the CPU
    server->debugger_state.program_counter = gPC;

    // Create initial stack trace
    debugger_build_stack_trace(gPC, 0);

    // If we have symbols, try to map initial PC to a source line
    int line = symbols_get_line(s_symbol_tables.symbol_table_map, gPC);
    const char *file = symbols_get_file(s_symbol_tables.symbol_table_map, gPC);

    if (line > 0 && file)
    {
        server->debugger_state.source_line = line;
        // Show initial source mapping
        char message[256];
        snprintf(message, sizeof(message), "Initial PC %06o mapped to %s:%d\n",
                 gPC, file, line);
        dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, message);
    }

    // Send process event to indicate execution started
    dap_server_send_process_event(server, program_path, 1, true, "launch");

    // Output a confirmation message to the debug console
    char message[256];
    snprintf(message, sizeof(message), "Launched program: %s\n", program_path);
    dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, message);

    // Send stopped event if stopAtEntry is true
    if (stop_at_entry)
    {
        dap_server_send_stopped_event(server, "entry", "Stopped at program entry");
        LOG(LOG_CAT_DAP, LOG_DEBUG, "Stopped at entry point\n");
        set_cpu_run_mode(CPU_PAUSED);
        set_cpu_stop_reason(STOP_REASON_ENTRY);
    }
    else
    {
        // Send thread started event
        dap_server_send_thread_event(server, "started", 1);
        set_cpu_run_mode(CPU_RUNNING);
        // Keep libdap's view in sync with the CPU: attach set has_stopped=true,
        // and only 'continue' clears it. Launching without stop-on-entry leaves
        // the CPU genuinely running, so clear it here too -- otherwise a later
        // 'pause' is rejected by handle_pause() as "Debugger already paused",
        // surfacing to the client as a generic "Unknown error".
        server->debugger_state.has_stopped = false;
    }

    return 0; // Return success to ensure the response is properly set
}

static int cmd_configuration_done(DAPServer *server)
{
    LOG(LOG_CAT_DAP, LOG_DEBUG, "Configuration done command received\n");

    // Send the response
    server->debugger_state.configuration_done = true;

    return 0;
}

/// @brief Handle the pause request from DAP
/// @param server The DAP server instance
/// @return 0 if successful, -1 if error
static int cmd_pause(DAPServer *server)
{
    // Request the CPU to pause (same mechanism as cmd_wait_for_debugger)
    return cmd_wait_for_debugger(server);
}

/// @brief Handle the restart request from DAP
/// @param server The DAP server instance
/// @return 0 if successful, -1 if error
/// @details This function handles the restart request from DAP.
/// It sends a response to the client with success=true.
static int cmd_restart(DAPServer *server)
{
    LOG(LOG_CAT_DAP, LOG_DEBUG, "Restart command received\n");

    // Respond to the client with success=true

    // Create response body
    cJSON *body = cJSON_CreateObject();
    if (!body)
    {
        return -1;
    }

    // Add success status
    cJSON_AddBoolToObject(body, "success", true);

    // Send the response
    dap_server_send_response(server, DAP_CMD_RESTART, server->sequence++,
                             server->current_command.request_seq, true, body);

    // Clean up
    cJSON_Delete(body);

    // Clear breakpoints
    breakpoint_manager_clear();

    // Reset CPU state
    cpu_reset();

    // Call launch callback to restart the program
    return cmd_launch_callback(server);

    return 0;
}

/// @brief Handle the disconnect request from DAP
/// @param server The DAP server instance
/// @return 0 if successful, -1 if error
/// @details This function handles the disconnect request from DAP.
/// It sends an exited event to the client, a terminated event to the client,
/// and a response to the client.
static int cmd_disconnect(DAPServer *server)
{
    LOG(LOG_CAT_DAP, LOG_DEBUG, "Disconnect command received\n");

    dap_server_send_exited_event(server, 0);

    // Send a terminated event to the client
    dap_server_send_terminated_event(server, false);

    // Send the response
    dap_server_send_response(server, DAP_CMD_DISCONNECT, server->sequence++,
                             server->current_command.request_seq, true, NULL);

    if (server->current_command.context.disconnect.terminate_debuggee)
    {
        // Kill your process/emulator/thread safely
        set_cpu_run_mode(CPU_SHUTDOWN);
    }
    else
    {
        // Detach: the DAP spec says a disconnect WITHOUT terminateDebuggee
        // leaves the debuggee running. CPU_STOPPED is wrong here - the main
        // loop escalates it to CPU_SHUTDOWN (cpu.c "WAS STOPPED, SHUTTING
        // DOWN"), so every client disconnect killed the emulator even when
        // the client asked terminateDebuggee=false. Resume instead; a new
        // client can re-attach later.
        set_cpu_run_mode(CPU_RUNNING);
    }

    return 0;
}

/// @brief Handle the terminate request from DAP

/// @param server The DAP server instance
/// @return 0 if successful, -1 if error
/// @details This function handles the terminate request from DAP.
/// It sends a response to the client with success=true.
/// Please stop the debuggee, but don't tear down the debug session completely yet.
static int cmd_terminate(DAPServer *server)
{
    LOG(LOG_CAT_DAP, LOG_DEBUG, "Terminate command received\n");

    // Send the response
    dap_server_send_response(server, DAP_CMD_TERMINATE, server->sequence++,
                             server->current_command.request_seq, true, NULL);

    // Send an exited event to the client
    dap_server_send_exited_event(server, 0);

    // Send a terminated event to the client
    dap_server_send_terminated_event(server, false);

    // DAP Adapter should now
    // 1. Stop/kill the emulator or target process
    // 2. Free any target-specific resources (RAM, CPU, threads, memory mappings, file handles...)
    // Here we do:
    // Set the CPU run mode to paused
    set_cpu_run_mode(CPU_PAUSED);
    return 0;
}

/// @brief DAP command to set a variable value (register or C local)
/// @param server
/// @return
static int cmd_set_variable(DAPServer *server)
{
    int ref = server->current_command.context.set_variable.variables_reference;
    const char *name = server->current_command.context.set_variable.name;
    const char *value_str = server->current_command.context.set_variable.value;

    if (!name || !value_str)
        return -1;

    /* Parse the value - support octal (0-prefix), hex (0x-prefix), decimal */
    char *endp;
    unsigned long val;
    if (value_str[0] == '0' && (value_str[1] == 'x' || value_str[1] == 'X'))
        val = strtoul(value_str, &endp, 16);
    else if (value_str[0] == '0' && value_str[1] >= '0' && value_str[1] <= '7')
        val = strtoul(value_str, &endp, 8);
    else
        val = strtoul(value_str, &endp, 10);

    uint16_t word = (uint16_t)(val & 0xFFFF);

    /* Registers scope */
    if (ref == SCOPE_ID_REGISTERS) {
        if (strcmp(name, "A") == 0) { gA = word; }
        else if (strcmp(name, "B") == 0) { gB = word; }
        else if (strcmp(name, "D") == 0) { gD = word; }
        else if (strcmp(name, "T") == 0) { gT = word; }
        else if (strcmp(name, "X") == 0) { gX = word; }
        else if (strcmp(name, "L") == 0) { gL = word; }
        else if (strcmp(name, "P") == 0) { gPC = word; }
        else { return -1; }

        /* Set the return value in octal */
        char buf[16];
        snprintf(buf, sizeof(buf), "%06o", word);
        server->current_command.context.set_variable.new_value = strdup(buf);
        server->current_command.context.set_variable.type = strdup("integer");
        return 0;
    }

    /* Locals scope - write to memory via B-register offset */
    if (ref == SCOPE_ID_LOCALS && s_symbol_tables.debug_info) {
        symbol_function_t *fn = symbols_find_function_at(s_symbol_tables.debug_info, gPC);
        if (fn) {
            int var_count;
            symbol_variable_t *vars = symbols_get_variables(fn, &var_count);
            for (int i = 0; i < var_count; i++) {
                if (strcmp(vars[i].name, name) == 0) {
                    uint16_t addr = gB + vars[i].offset;
                    // Trap-free debugger write (see dbg_write_data): a plain
                    // WriteVirtualMemory() would raise an emulated MPV/PF and
                    // longjmp() out of this DAP handler on a bad address.
                    dbg_write_data(addr, word);
                    char buf[16];
                    snprintf(buf, sizeof(buf), "%06o", word);
                    server->current_command.context.set_variable.new_value = strdup(buf);
                    server->current_command.context.set_variable.type = strdup(vars[i].type_name);
                    return 0;
                }
            }
        }
    }

    return -1;
}

/// @brief DAP command to evaluate an expression
/// @param server
/// @return
static int cmd_evaluate(DAPServer *server)
{
    const char *expr = server->current_command.context.evaluate.expression;
    if (!expr)
        return -1;

    const char *err = NULL;
    uint16_t val = expr_eval_value(expr, &err);

    if (err) {
        /* Return the error as the result string */
        char buf[128];
        snprintf(buf, sizeof(buf), "Error: %s", err);
        server->current_command.context.evaluate.result = strdup(buf);
        server->current_command.context.evaluate.type = strdup("error");
        return 0;
    }

    /* Format result as both octal and decimal */
    char buf[64];
    snprintf(buf, sizeof(buf), "%06o (%u)", val, val);
    server->current_command.context.evaluate.result = strdup(buf);
    server->current_command.context.evaluate.type = strdup("integer");
    server->current_command.context.evaluate.memory_reference = val;
    return 0;
}

/// @brief DAP command to set function breakpoints by name
/// @param server
/// @return
static int cmd_set_function_breakpoints(DAPServer *server)
{
    int count = server->current_command.context.function_breakpoint.count;

    /* Clear existing function breakpoints only, preserve source/instruction/data BPs */
    breakpoint_manager_clear_type(BP_TYPE_FUNCTION);

    /* Allocate result array */
    DAPBreakpoint *results = calloc(count, sizeof(DAPBreakpoint));
    if (!results && count > 0) {
        server->current_command.context.function_breakpoint.breakpoints = NULL;
        server->current_command.context.function_breakpoint.breakpoint_count = 0;
        return -1;
    }
    server->current_command.context.function_breakpoint.breakpoints = results;
    server->current_command.context.function_breakpoint.breakpoint_count = count;

    for (int i = 0; i < count; i++) {
        const char *fname = server->current_command.context.function_breakpoint.names[i];
        results[i].id = i + 1;
        results[i].verified = false;

        if (!fname) continue;

        /* Look up function name in debug_info */
        if (s_symbol_tables.debug_info) {
            for (int f = 0; f < s_symbol_tables.debug_info->function_count; f++) {
                symbol_function_t *fn = &s_symbol_tables.debug_info->functions[f];
                if (fn->name && strcmp(fn->name, fname) == 0) {
                    uint16_t addr = fn->start_address;
                    const char *cond = server->current_command.context.function_breakpoint.conditions ?
                                       server->current_command.context.function_breakpoint.conditions[i] : NULL;
                    const char *hit_cond = server->current_command.context.function_breakpoint.hit_conditions ?
                                           server->current_command.context.function_breakpoint.hit_conditions[i] : NULL;
                    breakpoint_manager_add(addr, BP_TYPE_FUNCTION, cond, hit_cond, NULL);
                    results[i].verified = true;
                    results[i].line = addr;
                    break;
                }
            }
        }

        /* Also try symbol table label lookup */
        if (!results[i].verified) {
            symbol_table_t *tables[] = {
                s_symbol_tables.symbol_table_stabs,
                s_symbol_tables.symbol_table_map,
                s_symbol_tables.symbol_table_aout
            };
            for (int t = 0; t < 3 && !results[i].verified; t++) {
                if (!tables[t]) continue;
                const symbol_entry_t *entry = symbols_lookup_by_name(tables[t], fname);
                if (entry) {
                    uint16_t addr = entry->address;
                    const char *cond = server->current_command.context.function_breakpoint.conditions ?
                                       server->current_command.context.function_breakpoint.conditions[i] : NULL;
                    const char *hit_cond = server->current_command.context.function_breakpoint.hit_conditions ?
                                           server->current_command.context.function_breakpoint.hit_conditions[i] : NULL;
                    breakpoint_manager_add(addr, BP_TYPE_FUNCTION, cond, hit_cond, NULL);
                    results[i].verified = true;
                    results[i].line = addr;
                }
            }
        }
    }

    return 0;
}

/// @brief DAP command to read memory
/// @param server
/// @return
static int cmd_read_memory(DAPServer *server)
{
    // Extract parameters from the command context
    uint32_t memory_reference = server->current_command.context.read_memory.memory_reference;
    uint32_t offset = server->current_command.context.read_memory.offset;
    size_t byteCount = server->current_command.context.read_memory.count;
    bool is_physical = (server->current_command.context.read_memory.address_space
                        == DAP_DATA_BP_ADDR_PHYSICAL);

    uint32_t address = memory_reference + offset;

    server->current_command.context.read_memory.base64_data = NULL;
    server->current_command.context.read_memory.unreadable_bytes = byteCount;

    if (byteCount == 0)
    {
        server->current_command.context.read_memory.base64_data = strdup("");
        server->current_command.context.read_memory.unreadable_bytes = 0;
        return 0;
    }

    // read memory from memory_reference + offset, count bytes
    uint8_t *data = (uint8_t *)malloc(byteCount);
    if (!data)
    {
        return 0;
    }

    memset(data, 0, byteCount);

    // Address space selection with optional PIL override:
    //   physical - bypass MMU entirely (PIL ignored)
    //   ispace   - I-space via PT field of PCR, optional PIL
    //   dspace   - D-space via APT field of PCR, optional PIL
    //   virtual  - default: ReadVirtualMemory with UseAPT=false
    DAPDataBreakpointAddressSpace as = server->current_command.context.read_memory.address_space;
    int8_t pil = server->current_command.context.read_memory.pil;

    // Only the bytes we actually managed to read are returned. A word that
    // cannot be translated (page not present, address outside installed
    // memory) ends the read - everything from there on is reported as
    // unreadable and is NOT included in the returned buffer. Handing back a
    // zero-filled buffer for those bytes would be indistinguishable from a
    // region of memory that genuinely contains zeroes.
    size_t bytesRead = 0;

    while (bytesRead < byteCount)
    {
        int word;
        if (is_physical)
            word = Dbg_ReadPhysicalMemory(address);
        else if (as == DAP_DATA_BP_ADDR_ISPACE)
            word = Dbg_ReadVirtualMemoryISpace_PIL(address, pil);
        else if (as == DAP_DATA_BP_ADDR_DSPACE)
            word = Dbg_ReadVirtualMemoryDSpace_PIL(address, pil);
        else
            word = Dbg_ReadVirtualMemoryISpace_PIL(address, pil);
        if (word == -1)
        {
            break;
        }

        data[bytesRead++] = (uint8_t)(word >> 8);
        if (bytesRead < byteCount)
        {
            data[bytesRead++] = (uint8_t)(word & 0xFF);
        }
        address++;
    }

    server->current_command.context.read_memory.unreadable_bytes = byteCount - bytesRead;

    // encode the readable part only to base64
    server->current_command.context.read_memory.base64_data = base64_encode(data, bytesRead);

    // free data
    free(data);

    // return success
    return 0;
}

/// @brief Decode base64 data into a byte buffer.
/// @param input Base64-encoded string
/// @param output Output buffer
/// @param max_output_len Maximum bytes to write
/// @return Number of bytes decoded
static int debugger_base64_decode(const char *input, uint8_t *output, int max_output_len)
{
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int input_len, output_len = 0, i = 0;

    if (!input || !output) return 0;
    input_len = strlen(input);

    while (i < input_len && output_len < max_output_len - 3) {
        int values[4] = {0};
        int valid = 0;
        for (int j = 0; j < 4 && i < input_len; j++, i++) {
            if (input[i] == '=') {
                values[j] = 0;
            } else {
                char *pos = strchr(b64, input[i]);
                if (pos) { values[j] = pos - b64; valid++; }
            }
        }
        if (valid >= 2) {
            output[output_len++] = (values[0] << 2) | (values[1] >> 4);
            if (valid >= 3 && output_len < max_output_len)
                output[output_len++] = (values[1] << 4) | (values[2] >> 2);
            if (valid >= 4 && output_len < max_output_len)
                output[output_len++] = (values[2] << 6) | values[3];
        }
    }
    return output_len;
}

/// @brief DAP command to write memory
/// @param server
/// @return
static int cmd_write_memory(DAPServer *server)
{
    uint32_t memory_reference = server->current_command.context.write_memory.memory_reference;
    uint32_t offset = server->current_command.context.write_memory.offset;
    char *data_b64 = server->current_command.context.write_memory.data;

    server->current_command.context.write_memory.bytes_written = 0;

    if (!data_b64 || data_b64[0] == '\0')
        return 0;

    /* Decode base64 data */
    int max_bytes = strlen(data_b64); /* decoded is always smaller */
    uint8_t *buf = (uint8_t *)malloc(max_bytes);
    if (!buf)
        return -1;

    int byte_count = debugger_base64_decode(data_b64, buf, max_bytes);
    if (byte_count <= 0) {
        free(buf);
        return 0;
    }

    uint32_t addr = memory_reference + offset;
    int words_written = 0;
    DAPDataBreakpointAddressSpace as_w = server->current_command.context.write_memory.address_space;
    int8_t pil_w = server->current_command.context.write_memory.pil;

    /* Write word-by-word (ND-100 is word-addressed, 2 bytes per word) */
    for (int i = 0; i + 1 < byte_count; i += 2) {
        uint16_t word = ((uint16_t)buf[i] << 8) | buf[i + 1];
        if (as_w == DAP_DATA_BP_ADDR_PHYSICAL) {
            if (Dbg_WritePhysicalMemory(addr, word) < 0)
                break;
        } else if (as_w == DAP_DATA_BP_ADDR_ISPACE) {
            if (Dbg_WriteVirtualMemoryISpace_PIL(addr, word, pil_w) < 0)
                break;
        } else if (as_w == DAP_DATA_BP_ADDR_DSPACE) {
            if (Dbg_WriteVirtualMemoryDSpace_PIL(addr, word, pil_w) < 0)
                break;
        } else {
            Dbg_WriteVirtualMemoryISpace_PIL(addr, word, pil_w);
        }
        addr++;
        words_written++;
    }

    free(buf);
    server->current_command.context.write_memory.bytes_written = words_written * 2;
    return 0;
}

/// @brief DAP command to retrieve source file content
/// @param server DAP server instance
/// @return 0 on success, -1 on error
static int cmd_source(DAPServer *server)
{
    if (!server) return -1;

    // NOTE: context.source is not available in current libdap API
    // This command needs to be reimplemented when libdap adds source context support
    dap_server_send_output_category(server, DAP_OUTPUT_STDERR,
                                    "Error: Source command not yet implemented in current DAP library version\n");
    return -1;

    /* TODO: Reimplement when libdap adds source context
    int sourceReference = server->current_command.context.source.source_reference;

    // Load source file content
    char *content = load_source_file_by_reference(sourceReference);

    if (!content) {
        dap_server_send_output_category(server, DAP_OUTPUT_STDERR,
                                        "Error: Could not load source file\n");
        return -1;
    }

    // Store in context for response
    server->current_command.context.source.content = content;

    return 0;
    */
}

// cpu/cpu_disasm.c
void OpToStr(char *return_string, uint16_t max_len, uint16_t operand);

/// @brief DAP command to disassemble
/// @param server
/// @return
static int cmd_disassemble(DAPServer *server)
{
    uint16_t memory_reference = server->current_command.context.disassemble.memory_reference;
    int offset = server->current_command.context.disassemble.offset;                         // Offset in bytes
    int instruction_offset = server->current_command.context.disassemble.instruction_offset; // Offset in instructions (relative to the memory reference)
    int instruction_count = server->current_command.context.disassemble.instruction_count;   // Number of instructions to disassemble
    bool resolve_symbols = server->current_command.context.disassemble.resolve_symbols;      // Whether to resolve symbols
    DAPDataBreakpointAddressSpace addr_space = server->current_command.context.disassemble.address_space;
    int8_t pil = server->current_command.context.disassemble.pil;

    int virtualAddress = memory_reference + offset + instruction_offset;

    // Allocate memory for the instructions
    server->current_command.context.disassemble.instructions = (DisassembleInstruction *)malloc(instruction_count * sizeof(DisassembleInstruction));
    if (!server->current_command.context.disassemble.instructions)
    {
        return -1;
    }

    // Clear the instructions
    memset(server->current_command.context.disassemble.instructions, 0, instruction_count * sizeof(DisassembleInstruction));

    // Set the actual instruction count to 0
    server->current_command.context.disassemble.actual_instruction_count = 0;

    // Loop through the instructions and disassemble each one
    for (int i = 0; i < instruction_count; i++)
    {
        // Get pointer to the instruction
        DisassembleInstruction *instruction = &server->current_command.context.disassemble.instructions[server->current_command.context.disassemble.actual_instruction_count];
        if (virtualAddress < 0)
        {
            // Might happen if the instruction offset is too large
            continue;
        }
        else
        {

            // Read instruction word using the requested address space + PIL
            int word;
            switch (addr_space) {
            case DAP_DATA_BP_ADDR_PHYSICAL:
                word = Dbg_ReadPhysicalMemory((uint32_t)virtualAddress);
                break;
            case DAP_DATA_BP_ADDR_DSPACE:
                word = Dbg_ReadVirtualMemoryDSpace_PIL(virtualAddress, pil);
                break;
            case DAP_DATA_BP_ADDR_ISPACE:
            default:
                // Default: I-space (instructions are always in I-space)
                word = Dbg_ReadVirtualMemoryISpace_PIL(virtualAddress, pil);
                break;
            }
            // Get the address of the instruction (DAP SPEC says it must be hex)
            char address_str[10];
            snprintf(address_str, sizeof(address_str), "0x%04x", virtualAddress);
            instruction->address = strdup(address_str);

            char instruction_str[100];
            if (word < 0)
            {
                // The word could not be read (page not present, or address
                // outside installed memory). Say so - substituting a zero here
                // would disassemble as a perfectly plausible "000000 STZ 0".
                snprintf(instruction_str, sizeof(instruction_str), "?????? <unreadable>");
            }
            else
            {
                // Disassemble the instruction
                uint16_t operand = (uint16_t)word;
                char operand_str[50];
                OpToStr(operand_str, sizeof(operand_str), operand);
                snprintf(instruction_str, sizeof(instruction_str), "%06o %s", operand, operand_str);
            }

            instruction->instruction = strdup(instruction_str);
            instruction->symbol = NULL;
            if (resolve_symbols)
            {
                const char *sym = get_symbol_for_address(virtualAddress);
                if (sym)
                {
                    instruction->symbol = strdup(sym);
                }
            }
        }

        // Add the instruction to the instructions array
        server->current_command.context.disassemble.actual_instruction_count++;
        virtualAddress++;
    }
    return 0;
}

// Console output callback - buffers characters for later sending from debugger thread.
// This runs in the CPU thread so must NOT call dap_server_send_event directly.
static void debugger_console_output(Device *dev, char c)
{
    for (int i = 0; i < console_capture_count; i++) {
        if (console_captures[i].device == dev) {
            // Buffer the character in the ring buffer (lock-free single producer)
            int next = (console_captures[i].ring_head + 1) % CONSOLE_RING_SIZE;
            if (next != console_captures[i].ring_tail) {
                console_captures[i].ring[(int)console_captures[i].ring_head] = (unsigned char)c;
                console_captures[i].ring_head = next;
            }
            // else: ring full, drop character

            // Also call original callback so terminal still works normally
            if (console_captures[i].original_output) {
                console_captures[i].original_output(dev, c);
            }
            return;
        }
    }
}

// Flush buffered console output as DAP events. Called from debugger thread
// via cmd_check_cpu_events. Batches all buffered characters per terminal
// into a single event to avoid flooding the DAP connection.
static void flush_console_output(void)
{
    for (int i = 0; i < console_capture_count; i++) {
        ConsoleCapture *cap = &console_captures[i];

        if (cap->ring_tail == cap->ring_head) {
            continue;  // Nothing buffered for this terminal
        }

        // Collect all buffered characters into text and hex strings
        char text_buf[4096];
        char hex_buf[8192];
        int text_len = 0;
        int hex_len = 0;

        while (cap->ring_tail != cap->ring_head) {
            unsigned char c = cap->ring[(int)cap->ring_tail];
            cap->ring_tail = (cap->ring_tail + 1) % CONSOLE_RING_SIZE;

            // Append to hex buffer
            if (hex_len + 2 < (int)sizeof(hex_buf)) {
                hex_len += snprintf(hex_buf + hex_len, sizeof(hex_buf) - hex_len, "%02X", c);
            }

            // Append to text buffer (printable or whitespace as-is, others as '.')
            if (text_len + 1 < (int)sizeof(text_buf)) {
                if ((c >= 32 && c < 127) || c == '\r' || c == '\n' || c == '\t') {
                    text_buf[text_len++] = (char)c;
                } else {
                    text_buf[text_len++] = '.';
                }
            }
        }

        text_buf[text_len] = '\0';
        hex_buf[hex_len] = '\0';

        // Build the data JSON string with terminal address and full hex payload
        char data_str[8256];
        snprintf(data_str, sizeof(data_str), "{\"terminal\":%d,\"hex\":\"%s\"}",
                 cap->terminal_address, hex_buf);

        // Send one batched event per terminal
        cJSON *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "category", "stdout");
        cJSON_AddStringToObject(body, "output", text_buf);
        cJSON_AddStringToObject(body, "data", data_str);
        dap_server_send_event(cap->server, "output", body);
        // body ownership transferred to dap_server_send_event, do NOT delete
    }
}

// DAP command callback: enable/disable console capture on a terminal
static int cmd_console_enable(DAPServer *server)
{
    if (!server) return -1;

    ConsoleEnableContext *ctx = &server->current_command.context.console_enable;
    int addr = ctx->terminal;
    bool enable = ctx->enable;

    if (enable) {
        // Check if already capturing this terminal
        for (int i = 0; i < console_capture_count; i++) {
            if (console_captures[i].terminal_address == addr) {
                return 0; // Already enabled
            }
        }

        if (console_capture_count >= MAX_CONSOLE_CAPTURES) {
            return -1;
        }

        Device *dev = DeviceManager_GetDeviceByAddress(addr);
        if (!dev) {
            return -1;
        }

        // Save original callback and install our hook
        ConsoleCapture *cap = &console_captures[console_capture_count++];
        cap->device = dev;
        cap->terminal_address = addr;
        cap->original_output = dev->charCallbacks.outputFunc;
        cap->server = server;
        cap->ring_head = 0;
        cap->ring_tail = 0;

        Device_SetCharacterOutput(dev, debugger_console_output);
    } else {
        // Disable: restore original callback
        for (int i = 0; i < console_capture_count; i++) {
            if (console_captures[i].terminal_address == addr) {
                Device_SetCharacterOutput(console_captures[i].device,
                                          console_captures[i].original_output);
                // Remove from array by shifting
                for (int j = i; j < console_capture_count - 1; j++) {
                    console_captures[j] = console_captures[j + 1];
                }
                console_capture_count--;
                break;
            }
        }
    }

    return 0;
}

// DAP command callback: write input to a terminal
static int cmd_console_write(DAPServer *server)
{
    if (!server) return -1;

    ConsoleWriteContext *ctx = &server->current_command.context.console_write;
    int addr = ctx->terminal;

    Device *dev = DeviceManager_GetDeviceByAddress(addr);
    if (!dev) {
        return -1;
    }

    if (ctx->hex && ctx->input) {
        // Hex mode: parse pairs of hex digits
        const char *p = ctx->input;
        while (*p) {
            if (*(p+1)) {
                char hex[3] = { p[0], p[1], '\0' };
                unsigned int byte_val;
                if (sscanf(hex, "%02x", &byte_val) == 1) {
                    Terminal_QueueKeyCode(dev, (uint8_t)byte_val);
                }
                p += 2;
            } else {
                break;
            }
        }
    } else if (ctx->input) {
        // Text mode: send each character
        const char *p = ctx->input;
        while (*p) {
            Terminal_QueueKeyCode(dev, (uint8_t)*p);
            p++;
        }
    }

    return 0;
}

/**
 * @brief Map libsymbols type to DAP symbol type string
 */
static const char *symbol_type_to_dap_string(symbol_type_t type)
{
    switch (type) {
    case SYMBOL_TYPE_FUNCTION: return "function";
    case SYMBOL_TYPE_VARIABLE: return "variable";
    default:                   return "label";
    }
}

/**
 * @brief Symbol list callback - returns all symbols from all symbol tables
 */
static int cmd_symbol_list(DAPServer *server)
{
    if (!server) return -1;

    /* Count total symbols across all tables */
    size_t total = 0;

    if (s_symbol_tables.debug_info) {
        for (int f = 0; f < s_symbol_tables.debug_info->function_count; f++) {
            total++;  /* the function itself */
            total += (size_t)s_symbol_tables.debug_info->functions[f].variable_count;
        }
    }

    symbol_table_t *flat_tables[] = {
        s_symbol_tables.symbol_table_stabs,
        s_symbol_tables.symbol_table_map,
        s_symbol_tables.symbol_table_aout,
    };
    for (int t = 0; t < 3; t++) {
        if (flat_tables[t])
            total += flat_tables[t]->count;
    }

    if (total == 0) {
        server->current_command.context.symbol_list.symbols = NULL;
        server->current_command.context.symbol_list.symbol_count = 0;
        return 0;
    }

    DAPSymbol *syms = calloc(total, sizeof(DAPSymbol));
    if (!syms) return -1;

    int n = 0;

    /* 1. C debug info: functions and their variables */
    if (s_symbol_tables.debug_info) {
        for (int f = 0; f < s_symbol_tables.debug_info->function_count; f++) {
            symbol_function_t *func = &s_symbol_tables.debug_info->functions[f];
            syms[n].name        = func->name ? strdup(func->name) : strdup("(unknown)");
            syms[n].address     = func->start_address;
            syms[n].type        = strdup("function");
            syms[n].source_path = NULL;
            syms[n].line        = 0;
            n++;

            for (int v = 0; v < func->variable_count; v++) {
                symbol_variable_t *var = &func->variables[v];
                syms[n].name        = var->name ? strdup(var->name) : strdup("(unknown)");
                syms[n].address     = func->start_address;
                syms[n].type        = strdup("variable");
                syms[n].source_path = NULL;
                syms[n].line        = 0;
                n++;
            }
        }
    }

    /* 2. Flat symbol tables (stabs, map, aout) */
    for (int t = 0; t < 3; t++) {
        symbol_table_t *tbl = flat_tables[t];
        if (!tbl) continue;

        for (size_t i = 0; i < tbl->count; i++) {
            symbol_entry_t *e = &tbl->entries[i];

            /* Skip file and line entries - not useful as symbols */
            if (e->type == SYMBOL_TYPE_FILE || e->type == SYMBOL_TYPE_LINE)
                continue;

            syms[n].name        = e->name ? strdup(e->name) : strdup("(unknown)");
            syms[n].address     = e->address;
            syms[n].type        = strdup(symbol_type_to_dap_string(e->type));
            syms[n].source_path = e->filename ? strdup(e->filename) : NULL;
            syms[n].line        = e->line;
            n++;
        }
    }

    server->current_command.context.symbol_list.symbols      = syms;
    server->current_command.context.symbol_list.symbol_count  = n;

    return 0;
}

/// @brief Initialize the DAP server
/// @param port The port to listen on
/// @return 0 if successful, -1 if error
/// @details This function initializes the DAP server.
/// It creates a new DAP server instance and registers the necessary callbacks.
/// It then starts the server and returns the result.
int ndx_server_init(int port)
{
    // Initialize DAP server
    DAPServerConfig config = {
        .transport = {
            .type = DAP_TRANSPORT_TCP,
            .config = {
                .tcp = {
                    .host = "localhost",
                    .port = port}}},
    };

    g_dap_server = dap_server_create(&config);
    if (!g_dap_server)
    {
        return -1;
    }

    // Hook up callbacks

    // Register launch callback
    dap_server_register_command_callback(g_dap_server, DAP_CMD_LAUNCH, cmd_launch_callback);

    // Register configuration done callback
    dap_server_register_command_callback(g_dap_server, DAP_CMD_CONFIGURATION_DONE, cmd_configuration_done);

    // Register restart callback
    dap_server_register_command_callback(g_dap_server, DAP_CMD_RESTART, cmd_restart);

    // Disconnect request
    dap_server_register_command_callback(g_dap_server, DAP_CMD_DISCONNECT, cmd_disconnect);

    // Terminate request
    dap_server_register_command_callback(g_dap_server, DAP_CMD_TERMINATE, cmd_terminate);

    // Hook up commands for stopping and starting the debugger's access to the CPU
    dap_server_register_command_callback(g_dap_server, DAP_WAIT_FOR_DEBUGGER, cmd_wait_for_debugger);
    dap_server_register_command_callback(g_dap_server, DAP_RELEASE_DEBUGGER, cmd_release_debugger);
    dap_server_register_command_callback(g_dap_server, DAP_CHECK_CPU_EVENTS, cmd_check_cpu_events);

    // Set up stepping callbacks through command callbacks only
    // Register command-specific implementations using the wrapper functions
    dap_server_register_command_callback(g_dap_server, DAP_CMD_NEXT, cmd_next);
    dap_server_register_command_callback(g_dap_server, DAP_CMD_STEP_IN, cmd_step_in);
    dap_server_register_command_callback(g_dap_server, DAP_CMD_STEP_OUT, cmd_step_out);

    // Register pause callback
    dap_server_register_command_callback(g_dap_server, DAP_CMD_PAUSE, cmd_pause);

    // Register continue callback
    dap_server_register_command_callback(g_dap_server, DAP_CMD_CONTINUE, cmd_continue);

    // Register exception breakpoint callback
    dap_server_register_command_callback(g_dap_server, DAP_CMD_SET_EXCEPTION_BREAKPOINTS, on_set_exception_breakpoints);

    // Register breakpoint callbacks
    dap_server_register_command_callback(g_dap_server, DAP_CMD_SET_BREAKPOINTS, cmd_set_breakpoints);
    dap_server_register_command_callback(g_dap_server, DAP_CMD_SET_INSTRUCTION_BREAKPOINTS, cmd_set_instruction_breakpoints);
    dap_server_register_command_callback(g_dap_server, DAP_CMD_DATA_BREAKPOINT_INFO, cmd_data_breakpoint_info);
    dap_server_register_command_callback(g_dap_server, DAP_CMD_SET_DATA_BREAKPOINTS, cmd_set_data_breakpoints);

    // Register stack trace callback
    dap_server_register_command_callback(g_dap_server, DAP_CMD_STACK_TRACE, cmd_stack_trace);

    // Register scopes callback
    dap_server_register_command_callback(g_dap_server, DAP_CMD_SCOPES, cmd_scopes);

    // Register variables callback
    dap_server_register_command_callback(g_dap_server, DAP_CMD_VARIABLES, cmd_variables);

    // Register set variable callback
    dap_server_register_command_callback(g_dap_server, DAP_CMD_SET_VARIABLE, cmd_set_variable);

    // Expression evaluation
    dap_server_register_command_callback(g_dap_server, DAP_CMD_EVALUATE, cmd_evaluate);

    // Function breakpoints
    dap_server_register_command_callback(g_dap_server, DAP_CMD_SET_FUNCTION_BREAKPOINTS, cmd_set_function_breakpoints);

    // Register read memory callback
    dap_server_register_command_callback(g_dap_server, DAP_CMD_READ_MEMORY, cmd_read_memory);

    // Register write memory callback
    dap_server_register_command_callback(g_dap_server, DAP_CMD_WRITE_MEMORY, cmd_write_memory);

    // Register disassemble callback
    dap_server_register_command_callback(g_dap_server, DAP_CMD_DISASSEMBLE, cmd_disassemble);

    // Register source callback
    dap_server_register_command_callback(g_dap_server, DAP_CMD_SOURCE, cmd_source);

    // Console I/O
    dap_server_register_command_callback(g_dap_server, DAP_CMD_CONSOLE_ENABLE, cmd_console_enable);
    dap_server_register_command_callback(g_dap_server, DAP_CMD_CONSOLE_WRITE, cmd_console_write);

    // Symbol listing
    dap_server_register_command_callback(g_dap_server, DAP_CMD_SYMBOL_LIST, cmd_symbol_list);

    // Configure which capabilities are supported
    set_default_dap_capabilities(g_dap_server);

    // Start server and transport layer
    int result = dap_server_start(g_dap_server);
    if (result != 0)
    {
        return result;
    }

    return 0;
}

int ndx_server_stop(void)
{
    if (!g_dap_server)
    {
        return -1;
    }

    dap_server_stop(g_dap_server);
    dap_server_free(g_dap_server);
    g_dap_server = NULL;
    return 0;
}

void debugger_kbd_input(char c)
{

    if (c == 'q')
    {
        set_cpu_run_mode(CPU_SHUTDOWN);
    }

    // Print the current PC and run mode
    if (c == '.')
    {
        int runMode = get_cpu_run_mode();

        printf("P=%6o  RunMode=%d\n", gPC, runMode);
    }

    // Step the CPU
    if (c == ' ')
    {
        cpu_run(1);
        printf("%6o\n", gPC);
    }

    // Disassemble the instruction at the current PC
    if (c == 'd')
    {
        int runMode = get_cpu_run_mode();
        printf("P=%6o  RunMode=%d\n\n", gPC, runMode);

        int virtualAddress = gPC;
        for (int i = 0; i < 10; i++)
        {

            uint16_t operand = Dbg_ReadVirtualMemoryISpace(virtualAddress);

            // Get the address of the instruction (DAP SPEC says it must be hex)
            printf("[%06o] ", virtualAddress);

            // Get the instruction

            // Disassemble the instruction
            char operand_str[50];
            OpToStr(operand_str, sizeof(operand_str), operand);

            printf("%06o %s", operand, operand_str);

            const char *sym = get_symbol_for_address(virtualAddress);
            if (sym)
            {
                printf("    (%s)", sym);
            }

            printf("\n");

            virtualAddress++;
        }
    }
}

/* ================================================================
   WASM: In-process DAP server init and public API functions
   ================================================================ */
#ifdef __EMSCRIPTEN__

/// @brief Initialize DAP server struct in-process for WASM (no transport, no thread)
static int ndx_server_init_wasm(void)
{
    /* Allocate server struct directly (no dap_server_create which needs transport) */
    g_dap_server = (DAPServer *)calloc(1, sizeof(DAPServer));
    if (!g_dap_server)
    {
        LOG(LOG_CAT_DAP, LOG_ERROR, "Failed to allocate DAP server struct for WASM\n");
        return -1;
    }

    g_dap_server->is_initialized = true;
    g_dap_server->is_running = true;
    g_dap_server->attached = true;
    g_dap_server->debugger_state.has_stopped = false;
    g_dap_server->debugger_state.current_thread_id = 1;

    /* Register all the same callbacks as native */
    dap_server_register_command_callback(g_dap_server, DAP_CMD_NEXT, cmd_next);
    dap_server_register_command_callback(g_dap_server, DAP_CMD_STEP_IN, cmd_step_in);
    dap_server_register_command_callback(g_dap_server, DAP_CMD_STEP_OUT, cmd_step_out);
    dap_server_register_command_callback(g_dap_server, DAP_CMD_CONTINUE, cmd_continue);
    dap_server_register_command_callback(g_dap_server, DAP_CMD_STACK_TRACE, cmd_stack_trace);
    dap_server_register_command_callback(g_dap_server, DAP_CMD_SCOPES, cmd_scopes);
    dap_server_register_command_callback(g_dap_server, DAP_CMD_VARIABLES, cmd_variables);
    dap_server_register_command_callback(g_dap_server, DAP_CMD_DISASSEMBLE, cmd_disassemble);
    dap_server_register_command_callback(g_dap_server, DAP_CMD_READ_MEMORY, cmd_read_memory);
    dap_server_register_command_callback(g_dap_server, DAP_CMD_CONSOLE_ENABLE, cmd_console_enable);
    dap_server_register_command_callback(g_dap_server, DAP_CMD_CONSOLE_WRITE, cmd_console_write);
    dap_server_register_command_callback(g_dap_server, DAP_CMD_SYMBOL_LIST, cmd_symbol_list);

    LOG(LOG_CAT_DAP, LOG_INFO, "WASM DAP debugger initialized (in-process, no transport)\n");
    return 0;
}

/// @brief Get the DAP server pointer for direct struct access
DAPServer *dbg_get_server(void) { return g_dap_server; }

/* --- Helper: Free a variable array filled by add_variable_to_array --- */
static void dbg_free_variable_array(void)
{
    if (!g_dap_server) return;
    DAPVariable *arr = g_dap_server->current_command.context.variables.variable_array;
    int count = g_dap_server->current_command.context.variables.variable_count;
    if (arr)
    {
        for (int i = 0; i < count; i++)
        {
            if (arr[i].name) free(arr[i].name);
            if (arr[i].value) free(arr[i].value);
            if (arr[i].type) free(arr[i].type);
            if (arr[i].evaluate_name) free(arr[i].evaluate_name);
        }
        free(arr);
        g_dap_server->current_command.context.variables.variable_array = NULL;
    }
    g_dap_server->current_command.context.variables.variable_count = 0;
}

/* JSON buffer for returning data to JS */
static char dbg_json_buf[32768];

/// @brief Get scopes as JSON string
/// @return JSON array of scope objects
const char *dbg_get_scopes_json(void)
{
    if (!g_dap_server) return "[]";

    /* Call the scopes callback directly */
    g_dap_server->current_command.context.scopes.frame_id = 0;
    cmd_scopes(g_dap_server);

    /* Build JSON from the scopes array */
    int pos = 0;
    pos += snprintf(dbg_json_buf + pos, sizeof(dbg_json_buf) - pos, "[");

    DAPScope *scopes = g_dap_server->current_command.context.scopes.scopes;
    int count = g_dap_server->current_command.context.scopes.scope_count;

    for (int i = 0; i < count && pos < (int)sizeof(dbg_json_buf) - 256; i++)
    {
        if (i > 0) pos += snprintf(dbg_json_buf + pos, sizeof(dbg_json_buf) - pos, ",");
        pos += snprintf(dbg_json_buf + pos, sizeof(dbg_json_buf) - pos,
            "{\"name\":\"%s\",\"variablesReference\":%d,\"expensive\":%s}",
            scopes[i].name ? scopes[i].name : "",
            scopes[i].variables_reference,
            scopes[i].expensive ? "true" : "false");
    }
    pos += snprintf(dbg_json_buf + pos, sizeof(dbg_json_buf) - pos, "]");

    /* Free scope names */
    if (scopes)
    {
        for (int i = 0; i < count; i++)
        {
            if (scopes[i].name) free(scopes[i].name);
            if (scopes[i].source_path) free(scopes[i].source_path);
        }
        free(scopes);
        g_dap_server->current_command.context.scopes.scopes = NULL;
    }

    return dbg_json_buf;
}

/// @brief Get variables for a scope as JSON string
/// @param scope_id The scope/variables reference ID
/// @return JSON array of variable objects
const char *dbg_get_variables_json(int scope_id)
{
    if (!g_dap_server) return "[]";

    /* Set up context and call variables callback */
    g_dap_server->current_command.context.variables.variables_reference = scope_id;
    g_dap_server->current_command.context.variables.variable_count = 0;
    g_dap_server->current_command.context.variables.variable_array = NULL;
    cmd_variables(g_dap_server);

    /* Build JSON from the variable array */
    int pos = 0;
    pos += snprintf(dbg_json_buf + pos, sizeof(dbg_json_buf) - pos, "[");

    DAPVariable *vars = g_dap_server->current_command.context.variables.variable_array;
    int count = g_dap_server->current_command.context.variables.variable_count;

    for (int i = 0; i < count && pos < (int)sizeof(dbg_json_buf) - 512; i++)
    {
        if (i > 0) pos += snprintf(dbg_json_buf + pos, sizeof(dbg_json_buf) - pos, ",");
        pos += snprintf(dbg_json_buf + pos, sizeof(dbg_json_buf) - pos,
            "{\"name\":\"%s\",\"value\":\"%s\",\"type\":\"%s\",\"variablesReference\":%d,\"memoryReference\":%d}",
            vars[i].name ? vars[i].name : "",
            vars[i].value ? vars[i].value : "",
            vars[i].type ? vars[i].type : "",
            vars[i].variables_reference,
            vars[i].memory_reference);
    }
    pos += snprintf(dbg_json_buf + pos, sizeof(dbg_json_buf) - pos, "]");

    /* Free the variable array */
    dbg_free_variable_array();

    return dbg_json_buf;
}

/// @brief Get stack trace as JSON string
/// @return JSON array of stack frame objects
const char *dbg_get_stack_trace_json(void)
{
    if (!g_dap_server) return "[]";

    /* Set up context and call stack trace callback */
    g_dap_server->current_command.context.stack_trace.start_frame = 0;
    g_dap_server->current_command.context.stack_trace.levels = MAX_STACK_FRAMES;
    g_dap_server->current_command.context.stack_trace.frames = NULL;
    g_dap_server->current_command.context.stack_trace.frame_count = 0;
    cmd_stack_trace(g_dap_server);

    /* Build JSON */
    int pos = 0;
    pos += snprintf(dbg_json_buf + pos, sizeof(dbg_json_buf) - pos, "[");

    DAPStackFrame *frames = g_dap_server->current_command.context.stack_trace.frames;
    int count = g_dap_server->current_command.context.stack_trace.frame_count;

    for (int i = 0; i < count && pos < (int)sizeof(dbg_json_buf) - 512; i++)
    {
        if (i > 0) pos += snprintf(dbg_json_buf + pos, sizeof(dbg_json_buf) - pos, ",");
        pos += snprintf(dbg_json_buf + pos, sizeof(dbg_json_buf) - pos,
            "{\"id\":%d,\"name\":\"%s\",\"instructionPointerReference\":%d,\"line\":%d,\"source\":\"%s\"}",
            frames[i].id,
            frames[i].name ? frames[i].name : "",
            frames[i].instruction_pointer_reference,
            frames[i].line,
            frames[i].source_name ? frames[i].source_name : "");
    }
    pos += snprintf(dbg_json_buf + pos, sizeof(dbg_json_buf) - pos, "]");

    /* Free frames */
    if (frames)
    {
        for (int i = 0; i < count; i++)
        {
            if (frames[i].name) free(frames[i].name);
            if (frames[i].source_path) free(frames[i].source_path);
            if (frames[i].source_name) free(frames[i].source_name);
            if (frames[i].module_id) free(frames[i].module_id);
        }
        free(frames);
        g_dap_server->current_command.context.stack_trace.frames = NULL;
    }

    return dbg_json_buf;
}

/// @brief Get thread/runlevel info as JSON string
/// @return JSON array of thread objects (16 runlevels)
const char *dbg_get_threads_json(void)
{
    int pos = 0;
    pos += snprintf(dbg_json_buf + pos, sizeof(dbg_json_buf) - pos, "[");

    for (int lev = 0; lev < 16; lev++)
    {
        if (lev > 0) pos += snprintf(dbg_json_buf + pos, sizeof(dbg_json_buf) - pos, ",");

        uint16_t pcr = g_reg->reg_PCR[lev];
        int ring = pcr & 0x03;
        int pt, apt;
        if (pcr & (1 << 2))
        {
            pt = (pcr >> 11) & 0x0F;
            apt = (pcr >> 7) & 0x0F;
        }
        else
        {
            pt = (pcr >> 9) & 0x03;
            apt = (pcr >> 7) & 0x03;
        }
        uint16_t p_reg = g_reg->reg[lev][_P];
        bool is_current = (lev == gPIL);

        pos += snprintf(dbg_json_buf + pos, sizeof(dbg_json_buf) - pos,
            "{\"id\":%d,\"name\":\"Level %d\",\"pc\":%d,\"ring\":%d,\"pt\":%d,\"apt\":%d,\"current\":%s}",
            lev, lev, p_reg, ring, pt, apt,
            is_current ? "true" : "false");
    }
    pos += snprintf(dbg_json_buf + pos, sizeof(dbg_json_buf) - pos, "]");

    return dbg_json_buf;
}

/// @brief Step in (single instruction into calls)
int dbg_step_in(void)
{
    if (!g_dap_server) return -1;
    g_dap_server->current_command.context.step.granularity = DAP_STEP_GRANULARITY_INSTRUCTION;
    return step_cpu(g_dap_server, STEP_IN);
}

/// @brief Step over (step past calls)
int dbg_step_over(void)
{
    if (!g_dap_server) return -1;
    g_dap_server->current_command.context.step.granularity = DAP_STEP_GRANULARITY_INSTRUCTION;
    return step_cpu(g_dap_server, STEP_OVER);
}

/// @brief Step out (run until return)
int dbg_step_out(void)
{
    if (!g_dap_server) return -1;
    return step_cpu(g_dap_server, STEP_OUT);
}

#endif /* __EMSCRIPTEN__ */

// Add this before the end of the #ifdef WITH_DEBUGGER section
#ifdef WITH_DEBUGGER
/**
 * @brief Find a symbol by address
 *
 * @param symtab Symbol table to search
 * @param address Memory address to look up
 * @return const char* Symbol name or NULL if not found
 */
const char *find_symbol_by_address(symbol_table_t *symtab, uint16_t address)
{
    if (!symtab)
    {
        return NULL;
    }

    const symbol_entry_t *entry = symbols_lookup_by_address(symtab, address);
    if (entry && entry->name)
    {
        return entry->name;
    }

    return NULL;
}

/**
 * @brief Helper function to find a symbol by address using the global symbol table
 *
 * @param address Memory address to look up
 * @return const char* Symbol name or NULL if not found
 */
const char *get_symbol_for_address(uint16_t address)
{
    return find_symbol_by_address(s_symbol_tables.symbol_table_aout, address);
}

/**
 * @brief Get source file and line for an address
 *
 * @param address Memory address to look up
 * @param line Pointer to store the line number
 * @return const char* Source filename or NULL if not found
 */
const char *get_source_location(uint16_t address, int *line)
{
    if (!s_symbol_tables.symbol_table_map || !line)
    {
        return NULL;
    }

    *line = symbols_get_line(s_symbol_tables.symbol_table_map, address);
    return symbols_get_file(s_symbol_tables.symbol_table_map, address);
}

/**
 * @brief Set up the default capabilities for the mock server
 *
 * This function configures which DAP capabilities our mock server
 * actually supports based on our implementation.
 *
 * @param server The DAP server instance
 * @return int The number of capabilities set
 */
int set_default_dap_capabilities(DAPServer *server)
{
    if (!server)
    {
        return -1;
    }

    return dap_server_set_capabilities(server,
                                       // Core session management
                                       DAP_CAP_CONFIG_DONE_REQUEST, true,
                                       DAP_CAP_RESTART_REQUEST, true,
                                       DAP_CAP_TERMINATE_REQUEST, true,
                                       DAP_CAP_TERMINATE_DEBUGGEE, true,

                                       // Memory operations
                                       DAP_CAP_READ_MEMORY_REQUEST, true,
                                       DAP_CAP_WRITE_MEMORY_REQUEST, true,
                                       DAP_CAP_DISASSEMBLE_REQUEST, true,

                                       // Breakpoint features
                                       DAP_CAP_LOG_POINTS, true,  // Already works!
                                       DAP_CAP_STEPPING_GRANULARITY, true,  // Line/instruction stepping
                                       DAP_CAP_INSTRUCTION_BREAKPOINTS, true,  // Assembly breakpoints
                                       DAP_CAP_DATA_BREAKPOINTS, true,         // Memory watchpoints

                                       // Expression evaluation and variable modification
                                       DAP_CAP_EVALUATE_FOR_HOVERS, true,
                                       DAP_CAP_SET_VARIABLE, true,
                                       DAP_CAP_CONDITIONAL_BREAKPOINTS, true,
                                       DAP_CAP_HIT_CONDITIONAL_BREAKPOINTS, true,
                                       DAP_CAP_FUNCTION_BREAKPOINTS, true,

                                       DAP_CAP_COUNT // End of list
    );
}
#endif // WITH_DEBUGGER

#endif // WITH_DEBUGGER

// Empty implementations when debugger is not enabled
#ifndef WITH_DEBUGGER

void start_debugger(void)
{
    // Do nothing when debugger is not enabled
}

int ndx_server_init(int port)
{
    (void)port;
    return -1; // Not implemented
}

int ndx_server_stop(void)
{
    return -1; // Not implemented
}

void debugger_kbd_input(char c)
{
    (void)c; // Unused parameter
}

#endif // !WITH_DEBUGGER
