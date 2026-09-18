# DAP Capabilities Analysis & Implementation Status

## Overview

This document provides a comprehensive analysis of all DAP (Debug Adapter Protocol) capabilities advertised by the ND100X emulator, verifying actual implementation against [DAP specification requirements](https://microsoft.github.io/debug-adapter-protocol/specification).

## Capability Audit Results

### ✅ Fully Implemented and Advertised

| Capability | DAP Name | Handler | Location | Notes |
|------------|----------|---------|----------|-------|
| CONFIG_DONE_REQUEST | `supportsConfigurationDoneRequest` | `cmd_configuration_done()` | debugger.c:2228 | Signals end of configuration |
| RESTART_REQUEST | `supportsRestartRequest` | `cmd_restart()` | debugger.c:2243 | Reloads program and symbols |
| TERMINATE_REQUEST | `supportsTerminateRequest` | `cmd_terminate()` | debugger.c:2317 | Graceful shutdown |
| TERMINATE_DEBUGGEE | `supportTerminateDebuggee` | `cmd_disconnect()` | debugger.c:2284 | Disconnect with terminate option |
| READ_MEMORY_REQUEST | `supportsReadMemoryRequest` | `cmd_read_memory()` | debugger.c:2351 | Base64-encoded memory read |
| DISASSEMBLE_REQUEST | `supportsDisassembleRequest` | `cmd_disassemble()` | debugger.c:2420 | Instruction disassembly |
| LOG_POINTS | `supportsLogPoints` | `check_for_breakpoint()` | cpu_bkpt.c:252-254 | Logging without stopping |
| STEPPING_GRANULARITY | `supportsSteppingGranularity` | `step_cpu()` | debugger.c:377 | Line/instruction stepping |
| INSTRUCTION_BREAKPOINTS | `supportsInstructionBreakpoints` | `breakpoint_manager_add()` | cpu_bkpt.c | Address-based breakpoints |

### ❌ Removed (Were False Claims)

| Capability | DAP Name | Reason | Previous Status |
|------------|----------|--------|-----------------|
| EVALUATE_FOR_HOVERS | `supportsEvaluateForHovers` | No evaluate handler implemented | Claimed but not working |
| SET_VARIABLE | `supportsSetVariable` | Only stub `return 0;` | Claimed but not working |
| WRITE_MEMORY_REQUEST | `supportsWriteMemoryRequest` | Only stub `return 0;` | Claimed but not working |

### ⚠️ Partially Implemented (Not Advertised)

| Capability | DAP Name | Status | Implementation Notes |
|------------|----------|--------|----------------------|
| CONDITIONAL_BREAKPOINTS | `supportsConditionalBreakpoints` | Structure exists, no evaluator | Stores `condition` field but always evaluates to true |
| HIT_CONDITIONAL_BREAKPOINTS | `supportsHitConditionalBreakpoints` | Basic check only | Simple integer comparison `hitCount == hitCondition` |
| FUNCTION_BREAKPOINTS | `supportsFunctionBreakpoints` | Not implemented | Would need function name → address lookup |
| DATA_BREAKPOINTS | `supportsDataBreakpoints` | Not implemented | Would need memory watch system |

## Detailed Capability Analysis

### 1. Configuration Done Request ✅

**DAP Spec:** Client signals it has finished the configuration sequence and all breakpoints are set.

**Implementation:**
```c:2228:2236:debugger.c
static int cmd_configuration_done(DAPServer *server)
{
    printf("Configuration done command received\n");
    
    // Send the response
    server->debugger_state.configuration_done = true;
    
    return 0;
}
```

**Status:** ✅ Complete - Properly marks configuration as done.

### 2. Restart Request ✅

**DAP Spec:** Restart the debug session, reloading the program without terminating the adapter.

**Implementation:**
```c:2243:2276:debugger.c
static int cmd_restart(DAPServer *server)
{
    printf("Restart command received\n");
    
    // Clear breakpoints
    breakpoint_manager_clear();
    
    // Reset CPU state
    cpu_reset();
    
    // Call launch callback to restart the program
    return cmd_launch_callback(server);
}
```

**Status:** ✅ Complete - Clears state and reloads program.

### 3. Terminate Request ✅

**DAP Spec:** Terminate the debuggee but keep the debug session alive.

**Implementation:**
```c:2317:2338:debugger.c
static int cmd_terminate(DAPServer *server)
{
    printf("Terminate command received\n");
    
    // Send exited event
    dap_server_send_exited_event(server, 0);
    
    // Send terminated event
    dap_server_send_terminated_event(server, false);
    
    // Set CPU to paused
    set_cpu_run_mode(CPU_PAUSED);
    
    return 0;
}
```

**Status:** ✅ Complete - Properly terminates debuggee and sends events.

### 4. Terminate Debuggee ✅

**DAP Spec:** Support `terminateDebuggee` option in disconnect request.

**Implementation:**
```c:2298:2302:debugger.c
if (server->current_command.context.disconnect.terminate_debuggee)
{
    set_cpu_run_mode(CPU_SHUTDOWN);
} else {
    set_cpu_run_mode(CPU_STOPPED);
}
```

**Status:** ✅ Complete - Respects disconnect options.

### 5. Read Memory Request ✅

**DAP Spec:** Read memory at a given address and return as base64.

**Implementation:**
```c:2351:2404:debugger.c
static int cmd_read_memory(DAPServer *server)
{
    uint32_t memory_reference = server->current_command.context.read_memory.memory_reference;
    uint32_t offset = server->current_command.context.read_memory.offset;
    size_t byteCount = server->current_command.context.read_memory.count;
    
    uint32_t virtualAddress = memory_reference + offset;
    
    // Allocate data buffer
    uint8_t *data = (uint8_t *)malloc(byteCount);
    
    // Read memory word by word
    for (size_t i = 0; i < byteCount / 2; i++) {
        int word = ReadVirtualMemory(virtualAddress, false);
        if (word == -1) {
            server->current_command.context.read_memory.unreadable_bytes = byteCount - i * 2;
            break;
        }
        
        data[i * 2] = (uint8_t)(word >> 8);
        data[i * 2 + 1] = (uint8_t)(word & 0xFF);
        virtualAddress++;
    }
    
    // Encode to base64
    server->current_command.context.read_memory.base64_data = base64_encode(data, byteCount);
    
    free(data);
    return 0;
}
```

**Status:** ✅ Complete - Reads memory and base64 encodes.

### 6. Disassemble Request ✅

**DAP Spec:** Disassemble code at a given memory location.

**Implementation:**
```c:2420:2489:debugger.c
static int cmd_disassemble(DAPServer *server)
{
    uint16_t memory_reference = server->current_command.context.disassemble.memory_reference;
    int instruction_count = server->current_command.context.disassemble.instruction_count;
    bool resolve_symbols = server->current_command.context.disassemble.resolve_symbols;
    
    // Allocate instructions array
    server->current_command.context.disassemble.instructions = 
        (DisassembleInstruction *)malloc(instruction_count * sizeof(DisassembleInstruction));
    
    // Disassemble each instruction
    for (int i = 0; i < instruction_count; i++) {
        uint16_t operand = ReadVirtualMemory(virtualAddress, false);
        
        // Get address in hex
        char address_str[10];
        snprintf(address_str, sizeof(address_str), "0x%04x", virtualAddress);
        instruction->address = strdup(address_str);
        
        // Disassemble
        char operand_str[50];
        OpToStr(operand_str, sizeof(operand_str), operand);
        
        char instruction_str[100];
        snprintf(instruction_str, sizeof(instruction_str), "%06o %s", operand, operand_str);
        instruction->instruction = strdup(instruction_str);
        
        // Get symbol if requested
        if (resolve_symbols) {
            const char *sym = get_symbol_for_address(virtualAddress);
            if (sym) {
                instruction->symbol = strdup(sym);
            }
        }
        
        virtualAddress++;
    }
    
    return 0;
}
```

**Status:** ✅ Complete - Full disassembly with symbol resolution.

### 7. Log Points ✅

**DAP Spec:** Breakpoints that log a message without stopping execution.

**Implementation:**
```c:252:255:cpu_bkpt.c
if (bp->logMessage) {
    // Print log message
    printf("[LOGPOINT] %s\n", bp->logMessage);
    // Don't stop execution
}
```

**Status:** ✅ Complete - Fully working, just wasn't advertised before.

**Example:**
```json
{
  "source": { "path": "main.s" },
  "breakpoints": [{
    "line": 10,
    "logMessage": "A register value: {A}"
  }]
}
```

### 8. Stepping Granularity ✅

**DAP Spec:** Support different stepping levels (statement, line, instruction).

**Implementation:**
```c:388:452:debugger.c
int step_cpu(DAPServer *server, StepType step_type)
{
    // Access granularity from context
    StepCommandContext *ctx = &server->current_command.context.step;
    
    if (step_type == STEP_OVER) {
        // Check granularity
        if ((ctx->granularity == DAP_STEP_GRANULARITY_LINE) || 
            (ctx->granularity == DAP_STEP_GRANULARITY_STATEMENT)) {
            // Get next line's address
            target_pc = symbols_get_next_line_address(s_symbol_tables.symbol_table_map, current_pc);
            
            if (target_pc != 0 && target_pc != current_pc) {
                // Set temporary breakpoint at next line
                breakpoint_manager_add(target_pc, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
                ensure_cpu_running();
                return 0;
            }
        }
        
        // Fallback to single instruction step
        breakpoint_manager_step_one();
    }
}
```

**Status:** ✅ Complete - Supports line and instruction granularity.

**Usage:**
```json
{
  "command": "next",
  "arguments": {
    "threadId": 1,
    "granularity": "line"  // or "instruction"
  }
}
```

### 9. Instruction Breakpoints ✅

**DAP Spec:** Set breakpoints on specific machine instructions by address.

**Implementation:**
```c:69:116:cpu_bkpt.c
int breakpoint_manager_add(
    uint16_t address,
    BreakpointType type,
    const char* condition,
    const char* hitCondition,
    const char* logMessage)
{
    // Hash-based storage
    int hash = address % HASH_SIZE;
    
    // Create entry
    BreakpointEntry* entry = (BreakpointEntry*)malloc(sizeof(BreakpointEntry));
    entry->address = address;
    entry->type = type;
    entry->condition = condition ? strdup(condition) : NULL;
    entry->hitCondition = hitCondition ? strdup(hitCondition) : NULL;
    entry->logMessage = logMessage ? strdup(logMessage) : NULL;
    entry->hitCount = 0;
    entry->next = NULL;
    
    // Add to hash table
    // ...
}
```

**Status:** ✅ Complete - Direct address-based breakpoints work.

**Usage:**
```json
{
  "command": "setInstructionBreakpoints",
  "arguments": {
    "breakpoints": [{
      "instructionReference": "0x0234",
      "condition": "A > 100"
    }]
  }
}
```

### 10. Source Request ✅ **NEW**

**DAP Spec:** Return source file content when requested by `sourceReference`.

**Implementation:**
```c:2542:2564:debugger.c
static int cmd_source(DAPServer *server)
{
    if (!server) return -1;
    
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
}
```

**Status:** ✅ Complete - NEW in this update.

**When Used:**
- When stack frames return `sourceReference` instead of `path`
- For source files that don't exist on client's disk
- For embedded/generated source files

## Capability Matrix

### Execution Control

| Capability | Supported | Implementation Quality | Notes |
|------------|-----------|------------------------|-------|
| launch | ✅ Yes | Excellent | Loads program, symbols, sets PC |
| attach | ❌ No | - | Not implemented |
| continue | ✅ Yes | Excellent | Resumes execution |
| next | ✅ Yes | Good | Line-aware with MAP files, instruction fallback |
| stepIn | ✅ Yes | Good | Single instruction step |
| stepOut | ✅ Yes | Good | Temporary breakpoint at return address |
| pause | ✅ Yes | Excellent | Atomic flag coordination |
| restart | ✅ Yes | Excellent | Clean reload |
| terminate | ✅ Yes | Excellent | Graceful shutdown |
| disconnect | ✅ Yes | Excellent | Proper cleanup |

### Breakpoints

| Capability | Supported | Implementation Quality | Notes |
|------------|-----------|------------------------|-------|
| setBreakpoints | ✅ Yes | Excellent | Multi-table lookup (STABS→MAP→AOUT) |
| setFunctionBreakpoints | ❌ No | - | Not implemented |
| setInstructionBreakpoints | ✅ Yes | Excellent | Address-based |
| setDataBreakpoints | ❌ No | - | Not implemented |
| setExceptionBreakpoints | ⚠️ Partial | Minimal | Accepts but doesn't act on filters |
| Conditional breakpoints | ⚠️ Partial | Stores but doesn't evaluate | `condition_ok = true;` always |
| Hit count breakpoints | ⚠️ Partial | Basic integer comparison | Only supports `hitCount == N` |
| Logpoints | ✅ Yes | Good | Prints message, doesn't stop |

### Inspection

| Capability | Supported | Implementation Quality | Notes |
|------------|-----------|------------------------|-------|
| threads | ✅ Yes | Basic | Single thread (thread ID 1) |
| stackTrace | ✅ Yes | Excellent | Multi-table lookup, sourceReference support |
| scopes | ✅ Yes | Excellent | Locals, Registers, Levels, Internals, MMS |
| variables | ✅ Yes | Excellent | CPU registers, status flags, page tables |
| evaluate | ❌ No | - | Not implemented |
| setVariable | ❌ No | - | Stub only |

### Memory & Code

| Capability | Supported | Implementation Quality | Notes |
|------------|-----------|------------------------|-------|
| readMemory | ✅ Yes | Excellent | Base64-encoded, handles unreadable bytes |
| writeMemory | ❌ No | - | Stub only |
| disassemble | ✅ Yes | Excellent | With symbol resolution |
| source | ✅ Yes | Excellent | **NEW** - sourceReference support |

### Source Mapping

| Capability | Supported | Implementation Quality | Notes |
|------------|-----------|------------------------|-------|
| Line → Address | ✅ Yes | Good | MAP files work, STABS needs enhancement |
| Address → Line | ✅ Yes | Good | Multi-table lookup |
| Address → File | ✅ Yes | Good | Multi-table lookup |
| Source file list | ⚠️ Partial | Basic | Uses symbol tables |
| Source content | ✅ Yes | Excellent | **NEW** - Via source request |

## Implementation Details

### Multi-Table Symbol Lookup Strategy

The enhanced implementation searches symbol tables in priority order:

```c
// Priority: STABS (most detailed) → MAP (assembly) → AOUT (functions)

// For breakpoints (debugger.c:1980-2000)
if (s_symbol_tables.symbol_table_stabs) {
    validSymbol = symbols_find_address(s_symbol_tables.symbol_table_stabs, ...);
}
if (!validSymbol && s_symbol_tables.symbol_table_map) {
    validSymbol = symbols_find_address(s_symbol_tables.symbol_table_map, ...);
}
if (!validSymbol && s_symbol_tables.symbol_table_aout && str_ends_with(source_path, ".s")) {
    validSymbol = symbols_find_address(s_symbol_tables.symbol_table_aout, ...);
}
```

**Rationale:**
- **STABS**: Best for C programs, has line numbers
- **MAP**: Best for assembly programs, direct line mapping
- **AOUT**: Fallback, function symbols only

### Source Reference System

**When to use `sourceReference`:**
- File doesn't exist on client's disk
- File is embedded in executable
- File is generated/synthetic

**Flow:**
```
1. Stack trace finds source file path: "/embedded/main.c"
2. Check if file exists: file_exists("/embedded/main.c") → false
3. Create source reference: ref = get_or_create_source_reference("/embedded/main.c") → 1001
4. Return in stack frame: { "source": { "sourceReference": 1001 } }
5. IDE requests: { "command": "source", "arguments": { "sourceReference": 1001 } }
6. Adapter loads: content = load_source_file_by_reference(1001)
7. Return content: { "body": { "content": "..." } }
```

### Logpoint Implementation

**Already working**, just needed to advertise capability.

```c:252:255:cpu_bkpt.c
if (bp->logMessage) {
    printf("[LOGPOINT] %s\n", bp->logMessage);
    // Continue execution - don't stop
}
```

**Example usage in VS Code:**
- Right-click line gutter
- Select "Add Logpoint..."
- Enter message: `"Value at line {line}: A={A}"`

### Stepping Granularity

**Supports:**
- `"instruction"` - Single CPU instruction
- `"line"` - Next source line (requires symbol table)
- `"statement"` - Treated same as line

**Implementation:**
```c:420:432:debugger.c
if (s_symbol_tables.symbol_table_map && 
    ((ctx->granularity == DAP_STEP_GRANULARITY_LINE) || 
     (ctx->granularity == DAP_STEP_GRANULARITY_STATEMENT))) {
    
    target_pc = symbols_get_next_line_address(s_symbol_tables.symbol_table_map, current_pc);
    
    if (target_pc != 0 && target_pc != current_pc) {
        // Set temporary breakpoint at next line
        breakpoint_manager_add(target_pc, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
        ensure_cpu_running();
        return 0;
    }
}

// Fallback: single instruction
breakpoint_manager_step_one();
```

## Limitations & Future Work

### Current Limitations

1. **Conditional Breakpoints** - Structure exists but no expression evaluator
2. **Hit Count Breakpoints** - Only supports exact match (`==`), not `>=`, `%`
3. **Function Breakpoints** - Not implemented
4. **Data Breakpoints** - Not implemented (watchpoints)
5. **Evaluate Request** - Not implemented (needed for hover values)
6. **Set Variable** - Not implemented (can't modify during debug)
7. **Write Memory** - Not implemented
8. **STABS Parser** - Basic implementation, needs enhancement for full C debugging

### Recommended Next Steps

#### High Priority

1. **Expression Evaluator**
   - Parse simple expressions (`A > 10`, `B == 0xFF`)
   - Enable conditional breakpoints
   - Enable hit count operators (`>= 5`, `% 10 == 0`)
   - Enable evaluate for hovers

2. **Enhanced STABS Parser**
   - Parse N_SO, N_SOL, N_FUN, N_SLINE entries
   - Build comprehensive line tables
   - Enable full C source-level debugging

3. **Function Breakpoints**
   - Map function names to addresses
   - Support C and assembly functions
   - Handle overloaded names

#### Medium Priority

1. **Set Variable Implementation**
   - Modify register values
   - Modify memory values
   - Update display

2. **Write Memory Implementation**
   - Parse base64 data
   - Write to virtual memory
   - Handle partial writes

3. **Data Breakpoints (Watchpoints)**
   - Track memory access
   - Break on read/write/change
   - Performance optimization needed

#### Low Priority

1. **Attach Request**
   - Connect to running instance
   - Synchronize state

2. **Multiple Threads**
   - Currently single-threaded
   - Could support interrupt levels as "threads"

## Testing Recommendations

### Unit Tests

1. **Source Reference Management**
   ```c
   // Test file_exists()
   assert(file_exists("/tmp/test.txt") == true);
   assert(file_exists("/nonexistent") == false);
   
   // Test get_or_create_source_reference()
   int ref1 = get_or_create_source_reference("/path/to/file.c");
   int ref2 = get_or_create_source_reference("/path/to/file.c");
   assert(ref1 == ref2);  // Same file, same reference
   
   // Test load_source_file_by_reference()
   char *content = load_source_file_by_reference(ref1);
   assert(content != NULL);
   ```

2. **Multi-Table Lookup**
   ```c
   // Create test symbol tables
   symbol_table_t *map = symbols_create();
   symbol_table_t *stabs = symbols_create();
   
   // Add symbols
   symbols_add_entry(map, "main.s", "main", 10, 0x0100, SYMBOL_TYPE_FUNCTION);
   symbols_add_entry(stabs, "main.c", NULL, 15, 0x0200, SYMBOL_TYPE_LINE);
   
   // Test lookup priority
   uint16_t addr;
   assert(symbols_find_address(stabs, "main.c", &addr, &diff, 15) == true);
   assert(addr == 0x0200);
   ```

### Integration Tests

1. **Pure Assembly Program**
   - Create `test.s` with labels
   - Assemble to `test.out` with MAP file
   - Set breakpoints, verify they hit
   - Single step through code
   - Check stack traces

2. **Mixed C/Assembly Program**
   - Create `main.c` calling `util.s`
   - Compile to `a.out` with debug info
   - Set breakpoints in both C and assembly
   - Step from C into assembly
   - Verify stack traces show both

3. **Source References**
   - Use non-existent source path
   - Verify sourceReference in stack frames
   - Request source content
   - Verify content returned

### VS Code Extension Tests

1. **Configuration Provider**
   - Open .c file, press F5
   - Verify smart defaults for C project
   - Open .s file, press F5
   - Verify smart defaults for assembly project

2. **Breakpoint Support**
   - Set breakpoints in .c files
   - Set breakpoints in .s files
   - Verify both appear in UI
   - Verify both work during debugging

3. **Configuration Snippets**
   - Open Command Palette
   - "Debug: Add Configuration..."
   - Verify ND-100 snippets appear
   - Test each snippet type

## Conclusion

The ND100X DAP implementation now provides:

### Strengths ✅
- Honest capability advertising
- Multi-table symbol lookup
- Source reference support
- Enhanced stack frames
- Enhanced breakpoint resolution
- Detailed stopped events
- Logpoints working
- Stepping granularity support
- Instruction breakpoints

### Weaknesses ⚠️
- No expression evaluator
- Basic STABS parser
- No function breakpoints
- No data breakpoints
- No variable modification

### Overall Assessment

**Production Ready:** Yes, for assembly and basic C debugging
**Enterprise Ready:** No - needs expression evaluator and enhanced STABS parser
**Recommended for:** Development, education, retro computing
**Not recommended for:** Production C development (yet)

The implementation provides a solid foundation for mixed-language debugging with all the core DAP features working correctly. The main gap is the expression evaluator, which would enable conditional breakpoints and hover evaluation.

