# Mixed C/Assembly Stepping - Step-by-Step Implementation Guide

## Overview

This guide breaks down the implementation of proper C/assembly mixed-language stepping into **small, manageable steps**. Each step is independent and testable.

## Prerequisites

Before starting, you need to understand:

1. **ND-100 Calling Convention** - Does C compiler use JPL/EXIT or something else?
2. **STABS Line Tables** - Must have address→line mapping working
3. **Current PC and Operand** - Available from CPU state

## ND-100 Control Flow Instructions

### Call Instructions
- **JPL (0134000)** - Jump and Link (Procedure call)
  - Mask: `0xF800` 
  - Saves return address
  - Most common for assembly functions

### Jump Instructions (Non-Call)
- **JMP (0124000)** - Unconditional jump
- **JAP (0130000)** - Jump if A-register positive
- **JAN (0130400)** - Jump if A-register negative
- **JAZ (0131000)** - Jump if A-register zero
- **JAF (0131400)** - Jump if A-register is all bits set
- **JPC (0132000)** - Jump if carry
- **JNC (0132400)** - Jump if no carry
- **JXZ (0133000)** - Jump if X-register zero
- **JXN (0133400)** - Jump if X-register negative

### Return Instructions
- **EXIT (0146142)** - Return from procedure
- May also use register operations to modify P register

### Other Control Transfer
- **SKP (0140000)** - Skip next instruction (conditional)
- **WAIT (0151000)** - Wait for interrupt
- **TRA (0150000)** - Transfer register to register

## Step 1: Add Instruction Analysis Helper Functions

**File:** `src/debugger/debugger.c`

Add after existing helper functions (around line 608):

```c
/// @brief Check if instruction is a procedure call (JPL or similar)
/// @param operand The instruction word
/// @return true if instruction is a call, false otherwise
static bool is_procedure_call(uint16_t operand)
{
    // JPL instruction - most common for function calls
    if ((operand & 0xF800) == 0134000) {
        return true;
    }
    
    // TODO: Add other call instructions if C compiler uses them
    // Examples:
    // - Some compilers might use JMP with special patterns
    // - Some might use register-based calls
    
    return false;
}

/// @brief Check if instruction is a return (EXIT or similar)
/// @param operand The instruction word
/// @return true if instruction is a return, false otherwise
static bool is_procedure_return(uint16_t operand)
{
    // EXIT instruction - standard return
    if (operand == 0146142) {
        return true;
    }
    
    // TODO: Add other return patterns if C compiler uses them
    
    return false;
}

/// @brief Get the target address of a JPL instruction
/// @param pc Current program counter
/// @param operand JPL instruction word
/// @return Target address, or 0 if can't calculate
static uint16_t get_jpl_target_address(uint16_t pc, uint16_t operand)
{
    // JPL format: 0134 ddd (octal)
    // Where ddd is displacement
    
    // Extract displacement (lower 11 bits)
    uint16_t displacement = operand & 0x07FF;
    
    // Check if negative (bit 10 set)
    if (displacement & 0x0400) {
        // Sign extend
        displacement |= 0xF800;
    }
    
    // Calculate effective address
    // JPL uses: EA = (P+1) + displacement
    uint16_t ea = (pc + 1) + displacement;
    
    return ea;
}

/// @brief Calculate effective address for any memory reference instruction
/// @param pc Current program counter
/// @param operand Instruction word
/// @return Effective address, or 0 if not applicable
static uint16_t calculate_effective_address(uint16_t pc, uint16_t operand)
{
    // This is simplified - full EA calculation is complex
    // For now, handle common cases
    
    // Check instruction type
    uint16_t opcode = operand & 0xF800;
    
    // Memory reference instructions (000000-077777, 0120000-0137777)
    if (opcode < 0140000 || (opcode >= 0120000 && opcode < 0140000)) {
        // Extract displacement (lower 11 bits)
        uint16_t displacement = operand & 0x07FF;
        
        // Sign extend if needed
        if (displacement & 0x0400) {
            displacement |= 0xF800;
        }
        
        // For simplicity, assume relative to PC
        // Real implementation would check addressing mode
        return pc + displacement;
    }
    
    return 0;
}
```

## Step 2: Enhance Step In to Find Target Address

**File:** `src/debugger/debugger.c:496-505`

Replace current Step In implementation:

```c
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
        
        // Get the current instruction
        uint16_t current_operand = ReadVirtualMemory(current_pc, false);
        
        // Check if this is a procedure call
        if (is_procedure_call(current_operand)) {
            // Calculate where the call will jump to
            uint16_t call_target = get_jpl_target_address(current_pc, current_operand);
            
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
        
        // Not a call - step to next source line in current function
        uint16_t next_line_addr = 0;
        
        // Try STABS first
        if (s_symbol_tables.symbol_table_stabs) {
            next_line_addr = symbols_get_next_line_address(s_symbol_tables.symbol_table_stabs, current_pc);
        }
        
        // Try MAP if STABS didn't work
        if ((!next_line_addr || next_line_addr == current_pc) && s_symbol_tables.symbol_table_map) {
            next_line_addr = symbols_get_next_line_address(s_symbol_tables.symbol_table_map, current_pc);
        }
        
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
```

## Step 3: Fix Step Over for Multi-Table

**File:** `src/debugger/debugger.c:460-492`

Replace the symbol table check:

```c
// Step over - step to next line (F10)
if (step_type == STEP_OVER)
{
    // Handle special cases first
    
    // 1. EXIT instruction - step out
    if (g_reg->myreg_IR == 0146142)
    {
        int return_address = find_stack_return_address();
        if (return_address < 0)
            return -1;

        breakpoint_manager_add(return_address, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
        ensure_cpu_running();
        return 0;
    }

    // 2. JMP, Conditional Jumps, SKP - single step
    if (cpu_instruction_is_jump())
    {
        breakpoint_manager_step_one();
        ensure_cpu_running();
        return 0;
    }
    
    // 3. Procedure call (JPL) - step over it
    uint16_t current_operand = ReadVirtualMemory(current_pc, false);
    if (is_procedure_call(current_operand)) {
        // For calls, we want to step OVER, not INTO
        // Set breakpoint at return address (next instruction after call)
        uint16_t return_addr = current_pc + 1;  // JPL is 1 word
        
        snprintf(log_message, sizeof(log_message),
                "Stepping over function call, return at %06o\n", return_addr);
        dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, log_message);
        
        breakpoint_manager_add(return_addr, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
        ensure_cpu_running();
        return 0;
    }

    // 4. Normal instruction - step by line if symbol table available
    bool is_line_granularity = 
        (ctx->granularity == DAP_STEP_GRANULARITY_LINE) || 
        (ctx->granularity == DAP_STEP_GRANULARITY_STATEMENT);
    
    if (is_line_granularity && 
        (s_symbol_tables.symbol_table_stabs || s_symbol_tables.symbol_table_map))
    {
        uint16_t target_pc = 0;
        
        // Try STABS first (for C programs with STABS debug info)
        if (s_symbol_tables.symbol_table_stabs) {
            target_pc = symbols_get_next_line_address(s_symbol_tables.symbol_table_stabs, current_pc);
            
            if (target_pc && target_pc != current_pc) {
                snprintf(log_message, sizeof(log_message),
                        "Stepping to next line at %06o (from STABS)\n", target_pc);
                dap_server_send_output(server, log_message);
            }
        }

        // Try MAP if STABS didn't work (for assembly programs)
        if ((!target_pc || target_pc == current_pc) && s_symbol_tables.symbol_table_map) {
            target_pc = symbols_get_next_line_address(s_symbol_tables.symbol_table_map, current_pc);
            
            if (target_pc && target_pc != current_pc) {
                snprintf(log_message, sizeof(log_message),
                        "Stepping to next line at %06o (from MAP)\n", target_pc);
                dap_server_send_output(server, log_message);
            }
        }
        
        // If we found a different line, set temp breakpoint
        if (target_pc && target_pc != current_pc) {
            snprintf(log_message, sizeof(log_message), 
                    "Setting temporary breakpoint at address %06o\n", target_pc);
            dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, log_message);

            breakpoint_manager_add(target_pc, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
            ensure_cpu_running();
            return 0;
        }
    }
    
    // 5. Fallback: single instruction step
    snprintf(log_message, sizeof(log_message), 
            "Step Over (instruction level) from %06o\n", current_pc);
    dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, log_message);
    
    breakpoint_manager_step_one();
    ensure_cpu_running();
    return 0;
}
```

## Step 4: Enhanced Stack Frame Tracking

**Problem:** Need to track calls from both C and assembly.

**Solution:** Detect multiple calling patterns.

### Phase 4A: Add C Calling Convention Detection

**File:** `src/debugger/debugger.c`

Add detection functions:

```c
/// @brief Detect if current instruction sequence is a C function prologue
/// @param pc Program counter
/// @return true if looks like C function entry
static bool is_c_function_prologue(uint16_t pc)
{
    // TODO: This depends on your C compiler's code generation
    // Common patterns:
    // 1. PUSH registers
    // 2. Modify stack pointer (B register)
    // 3. Allocate stack space
    
    // Example for GCC-like compiler:
    // Check for: PUSH B; STA B,SP; SUB #n,SP
    
    uint16_t instr0 = ReadVirtualMemory(pc, false);
    uint16_t instr1 = ReadVirtualMemory(pc + 1, false);
    uint16_t instr2 = ReadVirtualMemory(pc + 2, false);
    
    // This is a placeholder - MUST BE CUSTOMIZED to your compiler!
    // For now, return false
    return false;
}

/// @brief Detect if current instruction sequence is a C function epilogue
/// @param pc Program counter  
/// @return true if looks like C function exit
static bool is_c_function_epilogue(uint16_t pc)
{
    // TODO: Detect C return sequence
    // Common patterns:
    // 1. Deallocate stack space
    // 2. POP registers
    // 3. Return instruction
    
    // Placeholder
    return false;
}
```

### Phase 4B: Enhance Stack Trace Building

**File:** `src/debugger/debugger.c:348-412`

Update `debugger_build_stack_trace()`:

```c
void debugger_build_stack_trace(uint16_t pc, uint16_t operand)
{
    // Check for different call types
    bool is_jpl = ((operand & 0xF800) == 0134000);
    bool is_exit = (operand == 0146142);
    
    // NEW: Check for C calling convention
    bool is_c_call = is_c_function_prologue(pc);
    bool is_c_return = is_c_function_epilogue(pc);
    
    // Is this the first frame?
    if (stack_trace.frame_count == 0)
    {
        stack_trace.frame_count = 1;
        stack_trace.current_frame = 0;

        stack_trace.frames[stack_trace.current_frame].operand = operand;
        stack_trace.frames[stack_trace.current_frame].return_address = pc;
        stack_trace.frames[stack_trace.current_frame].entry_point = pc;
        stack_trace.frames[stack_trace.current_frame].pc = pc;
    }

    // Handle function calls (JPL or C)
    if (is_jpl || is_c_call)
    {
        // Create new stack frame
        uint16_t return_address;
        
        if (is_jpl) {
            // JPL: return address is next instruction
            return_address = pc + 1;
        } else {
            // C call: return address depends on calling convention
            // TODO: Determine based on call instruction pattern
            return_address = pc + 1;  // Placeholder
        }

        stack_trace.current_frame = (stack_trace.current_frame + 1) % MAX_STACK_FRAMES;
        if (stack_trace.frame_count < MAX_STACK_FRAMES)
        {
            stack_trace.frame_count++;
        }

        // Add the new frame
        stack_trace.frames[stack_trace.current_frame].pc = pc;
        stack_trace.frames[stack_trace.current_frame].operand = operand;
        stack_trace.frames[stack_trace.current_frame].return_address = return_address;
        stack_trace.frames[stack_trace.current_frame].entry_point = 0;
        
        // Log the call
        const symbol_entry_t *symbol = symbols_lookup_by_address(
            s_symbol_tables.symbol_table_aout, 
            get_jpl_target_address(pc, operand)
        );
        
        if (symbol && symbol->name) {
            snprintf(log_message, sizeof(log_message),
                    "Function call: %s (frame %d)\n", 
                    symbol->name, stack_trace.frame_count);
            dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, log_message);
        }

        return;
    }
    
    // Handle function returns (EXIT or C)
    else if (is_exit || is_c_return)
    {
        // Remove stack frame
        if (stack_trace.frame_count > 1)
        {
            const symbol_entry_t *symbol = symbols_lookup_by_address(
                s_symbol_tables.symbol_table_aout,
                stack_trace.frames[stack_trace.current_frame].entry_point
            );
            
            if (symbol && symbol->name) {
                snprintf(log_message, sizeof(log_message),
                        "Function return: %s (frame %d)\n", 
                        symbol->name, stack_trace.frame_count);
                dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, log_message);
            }
            
            // Clear the current frame
            stack_trace.frames[stack_trace.current_frame].pc = 0;
            stack_trace.frames[stack_trace.current_frame].operand = 0;
            stack_trace.frames[stack_trace.current_frame].return_address = 0;
            stack_trace.frames[stack_trace.current_frame].entry_point = 0;

            // Move back one frame
            stack_trace.current_frame = (stack_trace.current_frame - 1 + MAX_STACK_FRAMES) % MAX_STACK_FRAMES;
            stack_trace.frame_count--;
        }

        return;
    }
    
    // Regular instruction - just update PC
    else
    {
        stack_trace.frames[stack_trace.current_frame].pc = pc;
    }
}
```

## Step 5: Testing Strategy

### Phase 5A: Test Assembly-Only (Should Still Work)

```bash
# 1. Create simple assembly program
cat > test.s << 'EOF'
main:
    LDA 5,0
    LDA 10,0
    JPL add
    STOP

add:
    ADD 0,0
    EXIT
EOF

# 2. Assemble
ndasm -o test.out -m test.map test.s

# 3. Debug
dap_debugger -f test.out -m test.map -s test.s -e

# 4. Test steps:
(dap) break main
(dap) continue
(dap) next        # Should step over LDA instructions
(dap) step        # Should step INTO add function
(dap) backtrace   # Should show 2 frames: add, main
(dap) step-out    # Should return to main
(dap) backtrace   # Should show 1 frame: main
```

**Expected:** All should work (no regressions).

### Phase 5B: Test C Function Calls

**First: Determine C calling convention:**

```bash
# 1. Create simple C program
cat > test.c << 'EOF'
int add(int a, int b) {
    return a + b;
}

int main() {
    int x = 5;
    int y = 10;
    int z = add(x, y);
    return z;
}
EOF

# 2. Compile to assembly
gcc -S -g -o test.s test.c

# 3. Analyze generated code
cat test.s | head -100

# 4. Look for:
grep -E "add:|main:|jpl|exit|call|ret|push|pop" test.s
```

**What to look for:**

1. **If you see JPL instructions:**
   - ✅ Good! Stack tracking will work
   - No changes needed to frame tracking

2. **If you see other patterns:**
   - Document the pattern
   - Update `is_c_function_prologue()`
   - Update `is_c_function_epilogue()`
   - Update `debugger_build_stack_trace()`

### Phase 5C: Test Mixed C/Assembly

```bash
# 1. Create C main
cat > main.c << 'EOF'
extern void delay(int ms);

int main() {
    int x = 100;
    delay(x);  // Calls assembly function
    return 0;
}
EOF

# 2. Create assembly delay
cat > delay.s << 'EOF'
.global delay
delay:
    ; Parameter in A register
    LDA 0,0
loop:
    SUB 1,0
    JAZ done
    JMP loop
done:
    EXIT
EOF

# 3. Compile and link
gcc -c -g -o main.o main.c
ndasm -o delay.o delay.s
ld -o mixed.out main.o delay.o

# 4. Debug
dap_debugger -f mixed.out -s "main.c,delay.s" -e

# 5. Test:
(dap) break main.c:5    # C breakpoint
(dap) break delay.s:6   # Assembly breakpoint
(dap) continue
(dap) step              # From C into assembly
(dap) backtrace         # Should show: delay (asm), main (C)
(dap) step-out          # Back to C
(dap) backtrace         # Should show: main (C)
```

## Implementation Phases

### Phase 1: Basic Infrastructure (2-3 hours) ✅ DONE

- ✅ Helper functions for instruction analysis
- ✅ Multi-table symbol lookup in breakpoints
- ✅ Multi-table symbol lookup in stack frames
- ✅ Source reference system

### Phase 2: Fix Step Over (2-3 hours) 🔴 CRITICAL

**Files:** `src/debugger/debugger.c:460-492`

**Steps:**
1. Add call detection before line stepping
2. Change from MAP-only to STABS-first
3. Add proper logging
4. Test with assembly (should still work)
5. Test with C (should now work)

**Code:** See Step 3 above

### Phase 3: Enhance Step In (3-4 hours) 🟡 HIGH

**Files:** `src/debugger/debugger.c:496-505`

**Steps:**
1. Add call detection
2. Calculate call target address
3. Find source info at target
4. Set breakpoint at function entry
5. Fallback to line step if not a call
6. Test scenarios

**Code:** See Step 2 above

### Phase 4: C Calling Convention Support (4-6 hours) ⚠️ DEPENDS ON INVESTIGATION

**Files:** `src/debugger/debugger.c:348-412`

**Steps:**
1. **INVESTIGATE** C compiler output (2 hours)
2. Document calling patterns found
3. Implement detection functions (1-2 hours)
4. Update stack trace building (1-2 hours)
5. Test C programs (1 hour)

**Code:** See Step 4 above

### Phase 5: STABS Parser Enhancement (11-15 hours) ⏰ LONG TERM

See `docs/STABS_PARSER_IMPLEMENTATION.md` for complete guide.

## Quick Win Path (Start Here!)

### Minimum Viable Implementation (4-5 hours)

**Goal:** Get basic C stepping working

**Steps:**

1. **Add helper functions** (30 minutes)
   - `is_procedure_call()`
   - `is_procedure_return()`
   - `get_jpl_target_address()`

2. **Fix Step Over** (2 hours)
   - Add call detection
   - Change to multi-table lookup
   - Test

3. **Enhance Step In** (2-3 hours)
   - Add call target calculation
   - Add source lookup at target
   - Test

4. **Test thoroughly** (1 hour)
   - Assembly programs
   - Simple C programs (if compiler uses JPL)
   - Document limitations

**Result:** Usable C/assembly debugging (80% solution)

## Advanced Features (Later)

### Variable Inspection in Stack Frames

**Requires:** Enhanced STABS parser

**Implementation:**

```c
// During STABS parsing, track variables per function
typedef struct {
    char *function_name;
    Variable *variables;
    int variable_count;
} FunctionScope;

static FunctionScope *function_scopes = NULL;
static int function_scope_count = 0;

// When parsing N_LSYM, N_PSYM, N_RSYM:
void add_variable_to_function(const char *func_name, 
                             const char *var_name,
                             const char *type,
                             uint16_t location,
                             bool is_register)
{
    // Find or create function scope
    FunctionScope *scope = find_or_create_scope(func_name);
    
    // Add variable
    scope->variable_count++;
    scope->variables = realloc(scope->variables, 
                              scope->variable_count * sizeof(Variable));
    
    Variable *var = &scope->variables[scope->variable_count - 1];
    var->name = strdup(var_name);
    var->type = strdup(type);
    var->location = location;
    var->is_register = is_register;
}

// In cmd_variables() for SCOPE_ID_LOCALS:
Variable *vars = get_function_variables(current_function_name);
for (int i = 0; i < var_count; i++) {
    char value_str[32];
    
    if (vars[i].is_register) {
        // Read from CPU register
        uint16_t val = get_register_value(vars[i].location);
        snprintf(value_str, sizeof(value_str), "%06o", val);
    } else {
        // Read from stack frame
        uint16_t val = ReadVirtualMemory(gB + vars[i].location, false);
        snprintf(value_str, sizeof(value_str), "%06o", val);
    }
    
    add_variable_to_array(server, vars[i].name, value_str, vars[i].type, ...);
}
```

## Debugging Your Implementation

### Add Verbose Logging

```c
// At top of debugger.c
#define DEBUG_STEPPING 1

#ifdef DEBUG_STEPPING
#define STEP_LOG(fmt, ...) \
    do { \
        char msg[256]; \
        snprintf(msg, sizeof(msg), "[STEP] " fmt "\n", ##__VA_ARGS__); \
        dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, msg); \
    } while(0)
#else
#define STEP_LOG(fmt, ...) do {} while(0)
#endif
```

**Use in step functions:**

```c
STEP_LOG("Step Over: PC=%06o, operand=%06o", current_pc, current_operand);
STEP_LOG("Detected call to %06o", call_target);
STEP_LOG("Setting breakpoint at %06o", target_pc);
STEP_LOG("Stack depth: %d frames", stack_trace.frame_count);
```

### Test Each Scenario

Create test matrix:

| Scenario | StepOver | StepIn | StepOut | Stack | Status |
|----------|----------|--------|---------|-------|--------|
| Assembly → Assembly | ✅ | ✅ | ✅ | ✅ | Works |
| Assembly → C | ? | ? | ? | ? | Test |
| C → Assembly | ? | ? | ? | ? | Test |
| C → C | ? | ? | ? | ? | Test |
| Nested calls | ? | ? | ? | ? | Test |

## Example Debug Session

### Scenario: C Calls Assembly Function

**Program:**
```c
// main.c
extern int delay(int ms);

int main() {
    int ms = 100;      // Line 4
    delay(ms);         // Line 5 - Call to assembly
    return 0;          // Line 6
}
```

```asm
; delay.s
.global delay
delay:              ; Line 1
    LDA 0,A         ; Line 2 - Get parameter
loop:               ; Line 3
    SUB 1,0         ; Line 4
    JAZ done        ; Line 5
    JMP loop        ; Line 6
done:               ; Line 7
    EXIT            ; Line 8
```

**Debug Session:**

```
(dap) break main.c:5
Breakpoint 1 set at main.c:5 (address 0x0104)

(dap) continue
Running...
Stopped at main.c:5 (PC=0x0104)

(dap) backtrace
Frame 0: main at main.c:5 (PC=0x0104)

(dap) info locals
  ms = 000144 (100 decimal)

(dap) step
[STEP] Step In: PC=000104, operand=134234
[STEP] Detected JPL call to address 000234
[STEP] Found source: delay.s:1
[STEP] Setting breakpoint at 000234
Running...
Stopped at delay.s:1 (PC=000234)

(dap) backtrace
Frame 0: delay at delay.s:1 (PC=000234)
Frame 1: main at main.c:5 (PC=0x0104)

(dap) next
[STEP] Step Over: PC=000234, not a call
[STEP] Next line from MAP: 000235
Running...
Stopped at delay.s:2 (PC=000235)

(dap) step-out
[STEP] Step Out: Setting breakpoint at return address 000105
Running...
Stopped at main.c:6 (PC=000105)

(dap) backtrace
Frame 0: main at main.c:6 (PC=000105)
```

## Common Pitfalls & Solutions

### Pitfall 1: Breakpoint Set at Wrong Address

**Problem:** Step In sets breakpoint at function call, not function entry.

**Solution:**
```c
// WRONG:
breakpoint_manager_add(current_pc, BP_TYPE_TEMPORARY, ...);

// RIGHT:
uint16_t call_target = get_jpl_target_address(current_pc, operand);
breakpoint_manager_add(call_target, BP_TYPE_TEMPORARY, ...);
```

### Pitfall 2: Infinite Loop with Single Step

**Problem:** Setting step_count but also setting breakpoint.

**Solution:** Use ONLY one method:
```c
// Either:
breakpoint_manager_step_one();  // Sets step_count

// OR:
breakpoint_manager_add(target_pc, BP_TYPE_TEMPORARY, ...);  // Sets breakpoint

// NEVER both!
```

### Pitfall 3: Stack Corruption

**Problem:** Pushing frames without popping on return.

**Solution:** Always match calls with returns:
```c
// Count frames in debug log
STEP_LOG("Stack: %d frames (current=%d)", 
         stack_trace.frame_count, 
         stack_trace.current_frame);

// Verify after each step-out
assert(stack_trace.frame_count > 0);
```

### Pitfall 4: Wrong Symbol Table

**Problem:** C program using MAP symbols, assembly using STABS.

**Solution:** Always try multiple tables:
```c
// Always search in priority order:
// 1. STABS (best for C)
// 2. MAP (best for assembly)  
// 3. AOUT (fallback)
```

## Testing Checklist

### Unit Tests Needed

- [ ] `is_procedure_call()` with JPL instructions
- [ ] `is_procedure_call()` with non-call instructions
- [ ] `get_jpl_target_address()` with various displacements
- [ ] Multi-table symbol lookup priority
- [ ] Stack frame push/pop balance

### Integration Tests

- [ ] Step Over in assembly
- [ ] Step Over in C
- [ ] Step In from assembly to assembly
- [ ] Step In from C to C
- [ ] Step In from C to assembly
- [ ] Step In from assembly to C
- [ ] Step Out from assembly
- [ ] Step Out from C
- [ ] Nested calls (3+ levels deep)
- [ ] Recursive functions

### Edge Cases

- [ ] Step Over at last line of function
- [ ] Step In when no symbol info
- [ ] Step Out from main()
- [ ] Multiple calls on one line
- [ ] Inline assembly in C
- [ ] Indirect calls (via register)
- [ ] Tail calls

## Summary

### Implementation Order

**Week 1: Quick Wins (4-5 hours)**
1. Add helper functions
2. Fix Step Over multi-table
3. Test assembly (regression check)

**Week 2: Enhanced Stepping (5-7 hours)**
1. Investigate C calling convention
2. Enhance Step In
3. Update stack tracking if needed
4. Test C programs

**Week 3: Variable Support (11-15 hours)**
1. Enhanced STABS parser
2. Variable tracking
3. Scope management
4. Complete testing

### Critical Success Factors

1. **Investigate first** - Know your C compiler's conventions
2. **Test incrementally** - Don't break working assembly debug
3. **Log verbosely** - Debug stepping is complex
4. **Match calls/returns** - Stack integrity is critical

### Files to Modify

| File | What to Change | Lines | Effort |
|------|----------------|-------|--------|
| `debugger.c` | Add helpers | +120 lines | 1 hour |
| `debugger.c` | Fix Step Over | ~40 lines | 2 hours |
| `debugger.c` | Enhance Step In | ~80 lines | 3 hours |
| `debugger.c` | Update stack tracking | ~60 lines | 2-4 hours |

**Total:** ~300 lines, 8-10 hours

### Expected Results

**After Phase 2 (Step Over fix):**
- ✅ C line stepping works (if STABS has line tables)
- ✅ Assembly still works
- ⚠️ Step In still basic

**After Phase 3 (Step In enhancement):**
- ✅ C→C stepping works
- ✅ C→Assembly stepping works
- ✅ Assembly→C stepping works
- ⚠️ Variables still limited

**After Phase 4 (STABS parser):**
- ✅ Everything works professionally
- ✅ Variable inspection works
- ✅ Complete C debugging support

---

**Ready to implement:** Start with Phase 1 (helper functions), then Phase 2 (Step Over fix). These give immediate value with minimal risk.

**Documentation:** `STEPPING_ANALYSIS.md`, `CRITICAL_ISSUES_FOUND.md`

**Next Step:** Apply quick wins from this guide!

