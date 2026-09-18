# C Code Stepping Analysis - Critical Gaps Identified

## Executive Summary

**CRITICAL FINDING:** The current step implementation has significant limitations for C source debugging:

- ❌ **Step Over** only checks MAP table, not STABS → **C line stepping won't work**
- ❌ **Step In** is instruction-only → **Can't step into C functions by source**
- ⚠️ **Step Out** depends on JPL/EXIT tracking → **May not work if C uses different calling convention**
- ❌ **Variable names** not tracked from STABS → **Can't show C local variables**
- ❌ **No C↔Assembly view switching** → **Can't see generated assembly for C code**

## Detailed Analysis

### 1. Step Over (F10) - Only Works with MAP Files

**Current Implementation:**
```c:460:473:debugger.c
// If we have symbol table and want to step by line
if (s_symbol_tables.symbol_table_map && 
    ((ctx->granularity == DAP_STEP_GRANULARITY_LINE) || 
     (ctx->granularity == DAP_STEP_GRANULARITY_STATEMENT)))
{
    // PROBLEM: Only checks MAP table, not STABS!
    target_pc = symbols_get_next_line_address(s_symbol_tables.symbol_table_map, current_pc);
    
    if (target_pc != 0 && target_pc != current_pc) {
        stepping_to_line = true;
        breakpoint_manager_add(target_pc, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
    }
}
```

**Problem:**
- Only checks `s_symbol_tables.symbol_table_map`
- **Never checks** `s_symbol_tables.symbol_table_stabs`
- C programs don't have MAP files!

**Result:** When debugging C, step over **always falls back to single instruction step**.

**Impact:** ⚠️ **HIGH** - C debugging unusable for line-level stepping

**Fix Required:**
```c
// FIXED VERSION:
// Try STABS first (for C programs)
if (s_symbol_tables.symbol_table_stabs && 
    ((ctx->granularity == DAP_STEP_GRANULARITY_LINE) || 
     (ctx->granularity == DAP_STEP_GRANULARITY_STATEMENT)))
{
    target_pc = symbols_get_next_line_address(s_symbol_tables.symbol_table_stabs, current_pc);
}

// Fallback to MAP (for assembly)
if ((!target_pc || target_pc == current_pc) && s_symbol_tables.symbol_table_map) {
    target_pc = symbols_get_next_line_address(s_symbol_tables.symbol_table_map, current_pc);
}

// If we found a different line, set temp breakpoint
if (target_pc != 0 && target_pc != current_pc) {
    breakpoint_manager_add(target_pc, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
} else {
    // Fallback: single instruction
    breakpoint_manager_step_one();
}
```

### 2. Step In (F11) - Instruction-Only

**Current Implementation:**
```c:496:505:debugger.c
// Step in - step one instruction (F11)
if (step_type == STEP_IN)
{
    snprintf(log_message, sizeof(log_message), "Stepping on instruction from %06o\n", current_pc);
    dap_server_send_output_category(server, DAP_OUTPUT_CONSOLE, log_message);

    // PROBLEM: Always single instruction, no source awareness
    breakpoint_manager_step_one();
    ensure_cpu_running();
    return 0;
}
```

**Problem:**
- No check for function calls
- No source-level stepping
- Always single instruction

**Result:** Step In works at assembly level but **not source level**.

**Impact:** ⚠️ **MEDIUM** - Works but not ideal for C debugging

**Scenarios:**

1. **C calling C function:**
   ```c
   int x = add(5, 10);  // Current line
   ```
   - **Expected:** Step into `add()` function, stop at first C line
   - **Actual:** Single instruction step, might land in compiler-generated prologue

2. **C calling Assembly function:**
   ```c
   delay(100);  // Calls assembly routine
   ```
   - **Expected:** Step into assembly, stop at function entry
   - **Actual:** Single instruction step, works but unclear

**Fix Required:**
```c
// ENHANCED STEP IN:
if (step_type == STEP_IN) {
    // Check if granularity is line/statement
    if ((ctx->granularity == DAP_STEP_GRANULARITY_LINE) || 
        (ctx->granularity == DAP_STEP_GRANULARITY_STATEMENT)) {
        
        // Check if next instruction is a call (JPL)
        uint16_t next_operand = ReadVirtualMemory(current_pc, false);
        bool is_call = ((next_operand & 0xF800) == 0134000);  // JPL instruction
        
        if (is_call) {
            // Get the target address of the call
            uint16_t call_target = get_jpl_target_address(current_pc, next_operand);
            
            // Find the first source line in the called function
            int target_line = 0;
            const char *target_file = NULL;
            
            // Try STABS first
            if (s_symbol_tables.symbol_table_stabs) {
                target_line = symbols_get_line(s_symbol_tables.symbol_table_stabs, call_target);
                target_file = symbols_get_file(s_symbol_tables.symbol_table_stabs, call_target);
            }
            
            // Fallback to MAP
            if ((!target_line || !target_file) && s_symbol_tables.symbol_table_map) {
                target_line = symbols_get_line(s_symbol_tables.symbol_table_map, call_target);
                target_file = symbols_get_file(s_symbol_tables.symbol_table_map, call_target);
            }
            
            if (target_line && target_file) {
                // Set temporary breakpoint at function entry
                breakpoint_manager_add(call_target, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
                ensure_cpu_running();
                return 0;
            }
        }
        
        // Not a call, or couldn't find source info - try next source line
        uint16_t next_line_addr = 0;
        
        if (s_symbol_tables.symbol_table_stabs) {
            next_line_addr = symbols_get_next_line_address(s_symbol_tables.symbol_table_stabs, current_pc);
        }
        if ((!next_line_addr || next_line_addr == current_pc) && s_symbol_tables.symbol_table_map) {
            next_line_addr = symbols_get_next_line_address(s_symbol_tables.symbol_table_map, current_pc);
        }
        
        if (next_line_addr && next_line_addr != current_pc) {
            breakpoint_manager_add(next_line_addr, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
            ensure_cpu_running();
            return 0;
        }
    }
    
    // Fallback: single instruction
    breakpoint_manager_step_one();
    ensure_cpu_running();
    return 0;
}
```

### 3. Step Out (Shift+F11) - Depends on JPL/EXIT Tracking

**Current Implementation:**
```c:508:518:debugger.c
// Step out - step to the next return address (Shift+F11)
if (step_type == STEP_OUT)
{
    // PROBLEM: Gets return from stack trace built by JPL/EXIT tracking
    uint16_t return_address = find_stack_return_address();
    if (return_address != -1)
    {
        breakpoint_manager_add(return_address, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
        ensure_cpu_running();
        return 0;
    }
}
```

**Depends On:**
```c:348:412:debugger.c
void debugger_build_stack_trace(uint16_t pc, uint16_t operand)
{
    // PROBLEM: Only recognizes ND-100 assembly calling convention
    bool is_jpl = ((operand & 0xF800) == 0134000);   // JPL instruction
    bool is_exit = (operand == 0146142);              // EXIT instruction
    
    if (is_jpl) {
        // Push frame
        uint16_t return_address = pc + 1;
        // ... add to stack
    }
    else if (is_exit) {
        // Pop frame
        // ... remove from stack
    }
}
```

**Questions:**
1. **Does your C compiler generate JPL/EXIT?**
   - If YES → Step out will work
   - If NO → Step out won't work

2. **What calling convention does C compiler use?**
   - Standard: Function prologue/epilogue
   - ND-100: JPL/EXIT instructions
   - Custom: Needs investigation

**Impact:** ⚠️ **HIGH** if C uses different calling convention

**Investigation Needed:**
```bash
# Compile simple C program
gcc -g -S -o test.s test.c

# Look for calling convention
grep -E "jpl|exit|call|ret" test.s

# Check actual instructions used
cat test.s | head -50
```

**Fix Required (if C doesn't use JPL/EXIT):**
```c
// Detect C function prologue/epilogue
bool is_c_function_entry(uint16_t pc, uint16_t operand) {
    // Check for common C prologue patterns
    // Example: PUSH registers, subtract from SP, etc.
    return false;  // TODO: Implement based on compiler output
}

bool is_c_function_exit(uint16_t pc, uint16_t operand) {
    // Check for common C epilogue patterns
    // Example: POP registers, return
    return false;  // TODO: Implement based on compiler output
}

// Update debugger_build_stack_trace()
void debugger_build_stack_trace(uint16_t pc, uint16_t operand) {
    bool is_jpl = ((operand & 0xF800) == 0134000);
    bool is_exit = (operand == 0146142);
    
    // NEW: Check for C calling convention
    bool is_c_call = is_c_function_entry(pc, operand);
    bool is_c_return = is_c_function_exit(pc, operand);
    
    if (is_jpl || is_c_call) {
        // Push frame
    }
    else if (is_exit || is_c_return) {
        // Pop frame
    }
}
```

### 4. Variable Name Tracking - Not Implemented

**Current State:**
```c:23:39:debugger.h
/// @brief Variable in the current stack frame
/// TODO: Not yet implemented and supported by the DAP
typedef struct {
    char *name;
    char *value;
    char *type;
} Variable;

typedef struct {
    int number_of_variables;  // ALWAYS 0 - never set!
    Variable *variables;      // ALWAYS NULL - never allocated!
} LocalVariables;
```

**Problem:**
- Structure exists but never populated
- STABS has variable info (N_LSYM, N_PSYM, N_RSYM) but not parsed
- Variables scope shows "dummy" variable only

**Impact:** ⚠️ **HIGH** - Can't inspect C local variables

**What's Missing:**

1. **Parse STABS variable entries:**
   ```
   N_PSYM (0xA0) - Function parameters
   N_LSYM (0x80) - Local variables
   N_RSYM (0x40) - Register variables
   N_GSYM (0x20) - Global variables
   ```

2. **Track variable scope:**
   ```
   N_LBRAC (0xC0) - Begin block (nesting level in n_desc)
   N_RBRAC (0xE0) - End block
   ```

3. **Store in stack frame:**
   ```c
   stack_trace.frames[current_frame].variables.number_of_variables = 3;
   stack_trace.frames[current_frame].variables.variables = [
       { "x", "10", "int" },
       { "y", "20", "int" },
       { "result", "30", "int" }
   ];
   ```

**Fix Required:** Enhanced STABS parser must:
- Parse N_LSYM, N_PSYM, N_RSYM entries
- Associate variables with function scope
- Store type information
- Track stack offsets or register locations

### 5. C ↔ Assembly View Switching - Not Implemented

**Question:** Can you see assembly code when debugging C source?

**Current Capabilities:**

1. **Disassemble Request** ✅ - Works
   ```json
   {
     "command": "disassemble",
     "arguments": {
       "memoryReference": "0x0100",
       "instructionCount": 10
     }
   }
   ```
   Returns assembly instructions.

2. **Stack Frame `instructionPointerReference`** ✅ - Set
   ```c:1841:debugger.c
   frame->instruction_pointer_reference = memory_reference;
   ```
   This enables VS Code disassembly view.

3. **VS Code Disassembly View** ✅ - Should work
   - Right-click in editor → "Open Disassembly View"
   - Shows generated assembly
   - **BUT:** Only works if DAP adapter implements `disassemble` (which we do!)

**Testing Needed:**
```
1. Debug C program
2. Set breakpoint in C file
3. When stopped, right-click → "Open Disassembly View"
4. Should show generated ND-100 assembly
5. Should be able to set instruction breakpoints
6. Should show both views side-by-side
```

**If Doesn't Work:**
- Check that `instructionPointerReference` is set in stack frames
- Verify `supportsDisassembleRequest` capability is advertised
- Check VS Code version (disassembly view added in v1.65+)

## Critical Issues Summary

| Issue | Severity | Impact | Fix Complexity |
|-------|----------|--------|----------------|
| Step Over only uses MAP | 🔴 **CRITICAL** | C line stepping broken | Medium (2-3 hours) |
| Step In is instruction-only | 🟡 **HIGH** | Poor C debugging UX | Medium (3-4 hours) |
| Step Out may not work for C | 🟡 **HIGH** | Depends on calling convention | High (requires investigation + 4-6 hours) |
| Variable names not tracked | 🟡 **HIGH** | Can't inspect C variables | High (part of STABS parser, 8-10 hours) |
| No explicit view switching | 🟢 **LOW** | Disassembly view should work | None (test existing feature) |

## Required Fixes

### Fix 1: Multi-Table Step Over ⚠️ **CRITICAL**

**File:** `src/debugger/debugger.c:460-473`

```c
// CURRENT (BROKEN FOR C):
if (s_symbol_tables.symbol_table_map && ...) {
    target_pc = symbols_get_next_line_address(s_symbol_tables.symbol_table_map, current_pc);
}

// FIXED (WORKS FOR C AND ASSEMBLY):
if ((s_symbol_tables.symbol_table_stabs || s_symbol_tables.symbol_table_map) && ...) {
    // Try STABS first (C programs)
    if (s_symbol_tables.symbol_table_stabs) {
        target_pc = symbols_get_next_line_address(s_symbol_tables.symbol_table_stabs, current_pc);
    }
    
    // Fallback to MAP (assembly programs)
    if ((!target_pc || target_pc == current_pc) && s_symbol_tables.symbol_table_map) {
        target_pc = symbols_get_next_line_address(s_symbol_tables.symbol_table_map, current_pc);
    }
}
```

### Fix 2: Source-Aware Step In

**File:** `src/debugger/debugger.c:496-505`

Add function call detection and source-level stepping:

```c
if (step_type == STEP_IN) {
    // For line/statement granularity, try source-level stepping
    if ((ctx->granularity == DAP_STEP_GRANULARITY_LINE) || 
        (ctx->granularity == DAP_STEP_GRANULARITY_STATEMENT)) {
        
        // Check if next instruction is a call
        uint16_t current_operand = ReadVirtualMemory(current_pc, false);
        bool is_call = ((current_operand & 0xF800) == 0134000);  // JPL
        
        if (is_call) {
            // Get call target
            uint16_t target_addr = calculate_jpl_target(current_pc, current_operand);
            
            // Set temp breakpoint at function entry
            breakpoint_manager_add(target_addr, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
            ensure_cpu_running();
            return 0;
        }
        
        // Not a call - step to next source line
        uint16_t next_line_addr = 0;
        
        if (s_symbol_tables.symbol_table_stabs) {
            next_line_addr = symbols_get_next_line_address(s_symbol_tables.symbol_table_stabs, current_pc);
        }
        if ((!next_line_addr || next_line_addr == current_pc) && s_symbol_tables.symbol_table_map) {
            next_line_addr = symbols_get_next_line_address(s_symbol_tables.symbol_table_map, current_pc);
        }
        
        if (next_line_addr && next_line_addr != current_pc) {
            breakpoint_manager_add(next_line_addr, BP_TYPE_TEMPORARY, NULL, NULL, NULL);
            ensure_cpu_running();
            return 0;
        }
    }
    
    // Fallback: single instruction
    breakpoint_manager_step_one();
    ensure_cpu_running();
    return 0;
}
```

### Fix 3: Investigate C Calling Convention

**Required Investigation:**

1. **Compile test program:**
   ```bash
   # Create test C file
   cat > test.c << 'EOF'
   int add(int a, int b) {
       return a + b;
   }
   
   int main() {
       int result = add(5, 10);
       return result;
   }
   EOF
   
   # Compile with debug info
   gcc -g -S -o test.s test.c
   
   # Look for calling convention
   cat test.s | grep -A5 -B5 "add\|main"
   ```

2. **Check for JPL/EXIT:**
   ```bash
   grep -i "jpl\|exit" test.s
   ```

3. **Identify actual calling convention:**
   - Look for PUSH/POP patterns
   - Look for stack pointer manipulation
   - Look for return instruction pattern

**If C uses JPL/EXIT:** ✅ No changes needed

**If C uses different convention:** Must update `debugger_build_stack_trace()`

### Fix 4: Variable Name Tracking

**Requires Enhanced STABS Parser**

Must parse these entries:

```
N_PSYM (0xA0) - Parameters:
  name: "a:i"           (parameter 'a', type integer)
  n_value: stack offset or register
  n_desc: register number

N_LSYM (0x80) - Local variables:
  name: "result:i"      (variable 'result', type integer)
  n_value: stack offset
  n_desc: 0

N_RSYM (0x40) - Register variables:
  name: "temp:i"
  n_value: 0
  n_desc: register number (0-15)
```

**Implementation:**
```c
// During STABS parsing
case N_PSYM:  // Parameter
case N_LSYM:  // Local variable
case N_RSYM:  // Register variable
    {
        // Parse "name:type" format
        char *colon = strchr(name, ':');
        if (colon) {
            char *var_name = strndup(name, colon - name);
            char *type_info = strdup(colon + 1);
            
            // Add to current function's variable list
            add_variable_to_function(current_function, var_name, 
                                   type_info, value, desc);
        }
    }
    break;
```

**Then in `cmd_variables()`:**
```c
case SCOPE_ID_LOCALS:
{
    // Get current function from stack frame
    const char *function_name = get_current_function_name();
    
    // Get variables for this function
    Variable *vars = get_function_variables(function_name);
    int var_count = get_function_variable_count(function_name);
    
    // Read actual values from memory/registers
    for (int i = 0; i < var_count; i++) {
        char value_str[32];
        
        if (vars[i].is_register) {
            // Read from register
            uint16_t reg_val = get_register_value(vars[i].location);
            snprintf(value_str, sizeof(value_str), "%06o", reg_val);
        } else {
            // Read from stack
            uint16_t stack_val = ReadVirtualMemory(gB + vars[i].location, false);
            snprintf(value_str, sizeof(value_str), "%06o", stack_val);
        }
        
        add_variable_to_array(server, vars[i].name, value_str, vars[i].type, ...);
    }
}
```

### Fix 5: C ↔ Assembly View Switching

**Current Status:**

The infrastructure exists but needs testing:

1. **Disassemble command:** ✅ Implemented
2. **instructionPointerReference:** ✅ Set in stack frames
3. **supportsDisassembleRequest:** ✅ Advertised

**VS Code Features:**

1. **Disassembly View** (VS Code 1.65+)
   - Right-click in editor → "Open Disassembly View"
   - Shows generated assembly inline
   - Can set instruction breakpoints
   - Updates with current execution

2. **Toggle Between Views**
   - Editor shows C source
   - Disassembly view shows generated assembly
   - Both views synchronized

**Testing Steps:**

```
1. Open main.c in VS Code
2. Start debugging (F5)
3. Set breakpoint in C file
4. When stopped:
   a. Right-click in editor
   b. Select "Open Disassembly View"
   c. Should see generated ND-100 assembly
   d. Should show current PC highlighted
5. Step in C view → disassembly view updates
6. Step in disassembly view → C view updates
```

**If Disassembly View Doesn't Appear:**

1. **Check VS Code version:**
   ```bash
   code --version
   # Need 1.65 or later
   ```

2. **Check capability:**
   ```json
   // In initialize response, verify:
   "supportsDisassembleRequest": true
   ```

3. **Check stack frame:**
   ```json
   // In stackTrace response, verify:
   {
     "instructionPointerReference": "0x0100"  // Must be string!
   }
   ```

4. **Enable feature:**
   ```json
   // settings.json
   {
     "debug.disassemblyView.showSourceCode": true
   }
   ```

## Workarounds (Until Fixes Implemented)

### For C Debugging

1. **Use instruction-level stepping:**
   ```json
   {
     "command": "next",
     "arguments": {
       "threadId": 1,
       "granularity": "instruction"  // Explicit instruction mode
     }
   }
   ```

2. **Set breakpoints on multiple lines:**
   - Instead of stepping, set breakpoints on every line
   - Continue between breakpoints

3. **Use disassembly view:**
   - Step at assembly level
   - Manually correlate with C source

### For Variable Inspection

Use memory view and registers:

```
1. View Registers scope → see A, D, B, etc.
2. Use readMemory with stack pointer
3. Manually calculate variable locations
4. Use evaluate (when implemented) with addresses
```

## Recommendations

### Immediate Action Required

1. **Investigate C calling convention** (2 hours)
   - Compile sample C program
   - Analyze generated assembly
   - Determine if JPL/EXIT used
   - Document findings

2. **Fix Step Over for STABS** (2-3 hours)
   - Change to check STABS table
   - Add fallback to MAP
   - Test with C program

3. **Enhance Step In** (3-4 hours)
   - Add function call detection
   - Add source-line stepping for non-calls
   - Test C and assembly

### Medium Priority

1. **Fix Step Out** (if needed) (4-6 hours)
   - Depends on calling convention investigation
   - May need to detect different patterns
   - Update stack frame tracking

2. **Implement Variable Tracking** (8-10 hours)
   - Part of STABS parser enhancement
   - Parse N_LSYM, N_PSYM, N_RSYM
   - Track scopes with N_LBRAC/N_RBRAC
   - Populate LocalVariables structure

### Test View Switching

1. **Test disassembly view** (30 minutes)
   - Follow testing steps above
   - Document if it works
   - File bug if it doesn't

## Conclusion

### Current State

**Assembly Debugging:** ✅ **Fully Functional**
- Step over/in/out work perfectly
- Stack traces accurate
- Variables visible (registers)

**C Debugging:** ⚠️ **PARTIALLY BROKEN**
- Step over falls back to instruction-level (unusable)
- Step in works but not source-aware
- Step out depends on calling convention (unknown)
- Variables not tracked (can only see registers)
- View switching should work but untested

### Critical Path

```
1. Investigate C calling convention (2 hours)
   ├─ If uses JPL/EXIT → 2. Fix step over
   └─ If different → 2a. Fix stack tracking + 2b. Fix step over

2. Fix step over multi-table (2-3 hours)

3. Enhance step in (3-4 hours)

4. Test view switching (30 minutes)

5. Implement variable tracking (8-10 hours, part of STABS parser)
```

**Total:** 15-20 hours for full C debugging support

### Recommendation

**Phase 1: Quick Win (4-6 hours)**
1. Investigate calling convention
2. Fix step over multi-table
3. Test basic C debugging

**Phase 2: Full Support (10-15 hours)**
1. Enhance step in
2. Fix step out (if needed)
3. Enhanced STABS parser with variables

This approach gives usable C debugging quickly, then enhances to full professional quality.

---

**Status:** Analysis complete  
**Action Required:** Investigation of C calling convention before proceeding  
**Priority:** HIGH - C debugging currently limited

