# ND-100X File Loading Architecture Analysis

**Date:** 2026-07-23  
**Task:** Task 1 - Analyze nd100x file loading  
**Status:** Complete

---

## Executive Summary

nd100x loads programs into a flat, word-addressed memory space (8 MW max) via three boot paths:
1. **BPUN** — Binary punch format (16-bit words, ASCII octal metadata)
2. **AOUT** — Unix a.out binary format (with symbolic debugging support)
3. **FLOPPY** — Floppy disk image (boots via embedded BPUN bootstrap)

All paths funnel through the same write-memory pipeline into `g_volatile_memory.n_Array[physicalAddress]`.

---

## Memory Layout

### Physical Memory Structure

**Type:** `_NDRAM_` union (word-addressed)

```c
// From cpu_types.h:272-276
typedef union ndram {
    unsigned char   c_Array[MEMPTSIZE*1024*2];  // Byte access
    ushort         n_Array[MEMPTSIZE*1024];     // Word access (main)
    ushort         n_Pages[MEMPTSIZE][1024];    // Page-organized access
} _NDRAM_;
```

**Capacity:**
- `MEMPTSIZE` = 8192 KWords (kilowords)
- **Total:** 8,388,608 words (16 MB)
- **Addresses:** 0x0000 → 0x7FFFFF (word-addressed, 21-bit physical address space)
- **Word size:** 16-bit (`uint16_t`)

**Access:** Via global `extern _NDRAM_ g_volatile_memory;`

### Memory Zones

No fixed segmentation in nd100x (unlike nd500x with domains). Instead, programs are loaded sequentially into memory:

| Zone | Address Range | Purpose | Notes |
|------|------|---------|-------|
| Bootstrap | 0x0000 — 0x1000 | CPU boot loader | Varies by boot type |
| Program text | 0x1000+ | Executable code | From BPUN/AOUT |
| Program data | Follows text | Static/global data | From BPUN/AOUT |
| Runtime stack | Top of memory | Call stack, locals | Grows downward |
| Free space | Between data & stack | Dynamic allocation | Optional for ND-100 |

**No MMU translation** — All addresses are physical (word-addressed directly into n_Array).

---

## File Format Support

### 1. BPUN (Batch Punched) — PRIMARY FORMAT

**Purpose:** Paper-tape bootstrap format; self-contained with loader metadata.

**File Location:** `src/ndlib/load_bpun.c`

**BPUN_Header Structure:**
```c
typedef struct {
    uint16_t start;              // Entry point address (octal)
    uint16_t boot;               // Bootstrap loader entry (octal)
    uint16_t address;            // Load address for data block
    uint16_t checksum;           // Stored checksum from file
    uint16_t calculatedChecksum; // Computed checksum (for verification)
    uint16_t action;             // Action field (0 = run at 'start')
    uint16_t count;              // Number of 16-bit words in code
    bool isFloMon;               // FloMon format (floppy boot variant)
} BPUN_Header;
```

**BPUN Parse States:**
```
LoadState_Preamble → LoadState_Address → LoadState_Count → 
LoadState_Data → LoadState_Checksum → LoadState_Action
```

**Format Example (text representation):**
```
/01000!        # Slash + octal = location counter (start address)
0001/          # Count (1 word)
120627         # Data (octal word values)
012345         # More data...
;              # End-of-data marker
006542         # Checksum (octal)
```

**Loading Steps (from load_bpun.c:34-82):**
1. Open file, read as binary
2. Parse BPUN_Header via LoadBPUNStream()
3. Validate checksum
4. Extract: `bpun.start` (entry point), `bpun.boot` (bootstrap addr)
5. Return `bootAddress = bpun.boot`

**Memory Write:** Words are written via callback to `WritePhysicalMemory()` during parse.

### 2. AOUT (Unix A.Out) — ADVANCED FORMAT

**Purpose:** Symbolic debugging support; includes symbol table, relocation info.

**File Location:** `src/ndlib/load_aout.c` (note: Windows not supported)

**Loading Pipeline (from machine.c:571):**
```c
bootAddress = load_aout(imageFile, verbose, write_memory, text_start, overlay_deposit);
```

**Parameters:**
- `imageFile` — Path to a.out file
- `write_memory` — Callback: `void write_memory(uint32_t address, uint16_t value)`
- `text_start` — Load address override (0 = use a.out header)
- `overlay_deposit` — Boolean: if true, overlay at specified address; if false, place at a.out origin

**Returns:** Entry point address (from a.out header)

**Advantages over BPUN:**
- Symbol table loaded (for debugger)
- Relocation records used
- Better for cross-compiler toolchains (e.g., nd100-pcc)

### 3. FLOPPY BOOT

**Purpose:** Boot from ND-100 floppy disk image.

**Process (from machine.c:584-599):**
1. Mount disk image: `mount_floppy(imageFile, 0)`
2. Extract embedded BPUN: `LoadBPUN(imageFile, verbose)`
3. Boot address = BPUN bootstrap entry
4. Set `g_start_addr = bootAddress`

**Note:** Floppy image must contain valid BPUN bootstrap sector.

---

## Program Loading Flow

### Boot Path Selection (from machine.c:532-606)

```c
int program_load(BOOT_TYPE bootType, int bootUnit, const char *imageFile, 
                 bool verbose, uint16_t text_start, bool overlay_deposit)
{
    int bootAddress = 0;
    
    switch (bootType) {
        case BOOT_BPUN:
            bootAddress = LoadBPUN(imageFile, verbose);
            g_start_addr = bootAddress;
            break;
            
        case BOOT_AOUT:
            bootAddress = load_aout(imageFile, verbose, write_memory, 
                                   text_start, overlay_deposit);
            g_start_addr = bootAddress;
            break;
            
        case BOOT_FLOPPY:
            mount_floppy(imageFile, 0);
            bootAddress = LoadBPUN(imageFile, verbose);
            g_start_addr = bootAddress;
            break;
    }
    
    return bootAddress;
}
```

### Memory Write Path

```
LoadBPUN() / load_aout()
  ↓
  Extracts address + data from file
  ↓
write_memory(uint32_t address, uint16_t value)
  ↓
WritePhysicalMemory(int physicalAddress, uint16_t value, bool privileged)
  [in cpu_mms.c:949]
  ↓
WritePhysicalMemoryWM(int physicalAddress, uint16_t value, bool privileged, WriteMode wm)
  [in cpu_mms.c:956]
  ↓
g_volatile_memory.n_Array[physicalAddress] = value
  [Direct word write]
  ↓
Returns to machine_load()
```

### CPU Initialization

**After file load completes:**
```c
gPC = 0;              // Set program counter to 0
g_start_addr = bootAddress;  // Store entry point for debugger/UI
```

**Note:** `gPC` is NOT immediately set to `bootAddress`. It starts at 0. The bootstrap loader at 0x0000 must jump to the actual program entry point.

---

## Command-Line Integration

### Current Boot Options (from config.c:782)

```c
--image=FILE        // Image file to load (aout, bpun, floppy only)
--boot-type=TYPE    // BPUN, AOUT, or FLOPPY
--boot-unit=N       // Boot unit (for multi-drive systems)
--text-start=ADDR   // Load address override (AOUT only, octal)
--overlay-deposit   // If set, overlay at specified address (AOUT only)
```

### Current Entry Point Sequence

1. `Config_ParseCommandLine()` — Parse CLI flags
2. `main()` — Initialize CPU, machine state
3. `program_load()` — Load file per boot type
4. Loop: Fetch → Decode → Execute instructions
5. Or: Enter debugger if `--debug` flag set

---

## File Discovery for Shell

### Directory Search Strategy

The shell must locate BPUN/PROG files in a working directory. Current nd100x has **no built-in file listing**, so the shell will need to:

1. **Scan host filesystem:** Use `opendir() / readdir()` to list files
2. **Filter by extension:**
   - `.bpun` → BPUN format
   - `.prog` → Program source or pre-compiled binary
3. **Convert to SINTRAN names:** `FILE.BPUN` ↔ host `FILE.bpun`

### ndmonlib Integration Point

The shell can use **ndmonlib's file table** to:
- List files with pattern matching (`:BPUN`, `PROG*`, etc)
- Track open files (useful for future PROG file handling)
- Convert SINTRAN file specs to host paths
- Leverage existing `mon_file_table.c` functions

**Functions available:**
- `mon_list_files(const char* pattern)` — List files matching pattern
- `mon_translate_path_lookup()` — Resolve own-dir → SYSTEM fallback
- `mon_open_file()` / `mon_close_file()` — File table management

---

## Entry Points for Shell Integration

### Option A: Shell After Boot (Recommended)

1. Load program via existing `program_load()`
2. Set up shell environment (file root, current user)
3. Present shell prompt
4. User selects/loads next file

**Advantage:** Reuses existing loader; minimal disruption  
**File:** Modify `nd100x.c` main loop to detect shell mode

### Option B: Shell Before Boot

1. Enter shell immediately (skip default boot)
2. User loads file explicitly
3. Shell provides file picker UI

**Advantage:** More like SINTRAN interactive environment  
**File:** Add shell invocation before `program_load()`

### Recommended Approach

**Hybrid:** Detect `--monitor` flag in config; if set, after boot completes (or skip boot), enter shell loop instead of returning to caller.

---

## Current Limitations (For Shell Design)

| Limitation | Impact | Workaround |
|------------|--------|-----------|
| No file listing API | Can't enumerate BPUN files | Use host `opendir()/readdir()` or ndmonlib |
| No user directory concept | Can't do own-dir fallback | Use explicit ndmonlib path resolution |
| No interactive readline | No command history | Link libreadline (optional, like nd500x) |
| AOUT not on Windows | Can't load a.out on Windows | BPUN only on Windows |
| Memory always maps to 0x0000 | Bootstrap hardcoded at 0 | Work within this constraint |

---

## Key Code Locations

| Concept | File | Lines |
|---------|------|-------|
| Memory structure | `src/cpu/cpu_types.h` | 272-276 |
| BPUN parser | `src/ndlib/load_bpun.c` | 34-240 |
| AOUT loader | `src/ndlib/load_aout.c` | All |
| Boot dispatcher | `src/machine/machine.c` | 532-606 |
| Memory write | `src/cpu/cpu_mms.c` | 949-1020 |
| Config & CLI | `src/frontend/nd100x/config.c` | 775-850 |
| Main entry | `src/frontend/nd100x/nd100x.c` | 772-900 |

---

## Summary for Shell Implementation

### What the shell needs to do:

1. **List files:** Scan host directory (or use ndmonlib) for `.bpun` / `.prog` files
2. **Load file:** Call existing `program_load()` with file path
3. **Set entry point:** Extract `bootAddress` from load result
4. **Run program:** Let CPU execute from address
5. **Repeat:** Offer command prompt after program exits

### What's already available:

- ✅ `program_load()` function (loads BPUN/AOUT/FLOPPY)
- ✅ `write_memory()` callback (writes to g_volatile_memory)
- ✅ `g_volatile_memory.n_Array[]` (direct word access)
- ✅ ndmonlib `mon_file_table.c` (file listing + SINTRAN semantics)
- ✅ CPU registers in global scope (`gPC`, `gSTAT`, etc)

### What needs to be added:

- Shell REPL loop (dispatcher, command parser)
- File selection UI (list + filter)
- Program execution controller (run program, detect completion)
- readline integration (optional)
- Config options (`--monitor`, `--shell`, `--nd100-root`)

---

## Testing / Validation

To verify this architecture:

```bash
# 1. Check memory structure
grep -n "g_volatile_memory" src/cpu/cpu_types.h

# 2. Trace BPUN load flow
grep -n "LoadBPUN\|write_memory" src/machine/machine.c

# 3. Verify word write
objdump -t build/bin/nd100x | grep g_volatile_memory

# 4. Test existing loader
./build/bin/nd100x --boot-type=BPUN --image=examples/hello.bpun --debug
  > In debugger: d 0x1000 32    (disassemble loaded code)
```

---

**END OF ANALYSIS — Ready for shell implementation.**
