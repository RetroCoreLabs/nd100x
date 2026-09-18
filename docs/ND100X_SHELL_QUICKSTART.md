# ND-100X Interactive Shell - Quick Start Guide

**Date:** 2026-07-23  
**Status:** Ready for testing  
**Files:** `src/frontend/nd100x/nd100x_shell.{c,h}`

---

## Overview

The ND-100X interactive shell provides a command-line interface for loading and running BPUN/PROG files on the ND-100 emulator without launching the full graphical UI.

## Usage

### Launch the Shell via CLI

```bash
# Enable shell mode
./build/bin/nd100x --monitor

# Specify file root directory
./build/bin/nd100x --monitor --nd100-root=./examples

# Load and run commands from a script
./build/bin/nd100x --monitor --script=commands.sh
```

### Configure via `.ini` File

Create an INI file with `[runtime]` section:

```ini
[runtime]
shell = on
nd100_root = ./examples
script = mycommands.sh
```

Then run:
```bash
./build/bin/nd100x --config=myconfig.ini
```

**Note:** CLI flags always override INI values.

---

## Available Commands

### HELP
Shows all available commands and their abbreviations.

```
@ HELP
@ HE    (abbreviated)
```

### LIST-FILES [pattern]
List BPUN/PROG files in the file root directory.

```
@ LIST-FILES
@ LIST-FILES *.bpun
@ LI-FI :PROG    (SINTRAN-style pattern)
```

**Patterns supported:**
- `*` — all files (default)
- `*.bpun` — BPUN files only
- `*.prog` — program files only
- `file*` — files starting with "file"

### RUN-PROGRAM <filename>
Load and run a BPUN program file.

```
@ RUN-PROGRAM hello.bpun
@ RU-PR hello.bpun    (abbreviated)
```

**How it works:**
1. Reads BPUN file from disk
2. Parses binary format (octal data, checksums)
3. Loads words into ND-100 memory
4. Reports entry point address
5. (CPU execution integration: pending)

### SHOW-REGISTERS
Display current CPU register state.

```
@ SHOW-REGISTERS
@ SH-RE    (abbreviated)
```

**Output:**
- A, B, D, X, L, T — General registers (current PIL)
- P — Program counter
- STS — Status register

### EXIT
Exit the shell and return to normal mode (or quit emulator).

```
@ EXIT
@ EX    (abbreviated)
```

---

## Command Abbreviations

Commands support prefix matching with hyphen-aware abbreviation:

| Command | Abbreviations |
|---------|---------------|
| HELP | HE, HELP |
| LIST-FILES | LI-FI, LI, LIST, LIST-FILES |
| RUN-PROGRAM | RU-PR, RUN, RU-PR |
| SHOW-REGISTERS | SH-RE, SHOW, SHOW-REGISTERS |
| EXIT | EX, EXIT |

**Examples:**
```
@ LI      → LIST-FILES (prefix match)
@ LI-FI   → LIST-FILES (hyphen-aware)
@ LIST-FILES *.bpun → full name + argument
```

---

## Script Files

Execute commands in batch from a file:

**File: `test.sh`:**
```
# List available programs
LIST-FILES *.bpun

# Run a program
RUN-PROGRAM hello.bpun

# Exit shell
EXIT
```

**Execution:**
```bash
./build/bin/nd100x --monitor --script=test.sh --nd100-root=./examples
```

**Features:**
- Comments: lines starting with `#`
- Empty lines: ignored
- Command history: (if readline available)

---

## File Root Directory

The shell searches for BPUN/PROG files relative to `nd100_root`:

| Setting | Effect |
|---------|--------|
| `--nd100-root=./examples` | Search `./examples/` for files |
| `--nd100-root=/tmp` | Search `/tmp/` for files |
| (not set) | Search current directory `.` |

**Example:**
```bash
./build/bin/nd100x --monitor --nd100-root=./examples
@ LIST-FILES
  hello.bpun
  kernel.prog
  test.bpun
```

---

## Configuration Examples

### Simple BPUN Loader

**Config: `shell-simple.ini`:**
```ini
[runtime]
shell = on
nd100_root = ./examples
```

**Run:**
```bash
./build/bin/nd100x --config=shell-simple.ini
```

### Automated Script Execution

**Config: `shell-auto.ini`:**
```ini
[runtime]
shell = on
nd100_root = ./test-programs
script = /tmp/autotest.sh
```

**Script: `/tmp/autotest.sh`:**
```
LIST-FILES *.bpun
RUN-PROGRAM kernel.bpun
EXIT
```

### Override INI with CLI

**CLI takes precedence:**
```bash
# INI says: shell = off
# CLI overrides:
./build/bin/nd100x --config=myconfig.ini --monitor

# INI says: nd100_root = ./data
# CLI overrides:
./build/bin/nd100x --config=myconfig.ini --nd100-root=./programs
```

---

## Known Limitations

1. **CPU Execution:** Currently reports "not yet integrated"
   - Programs load into memory
   - Registers can be displayed
   - Actual CPU execution pending integration work

2. **PROG Files:** Compilation not yet implemented
   - Listed in file browser
   - Cannot be executed directly
   - Requires external compilation first

3. **Terminal:** Basic (no color, limited readline)
   - Readline support optional (compile-time)
   - No syntax highlighting
   - No command completion

---

## Troubleshooting

### "Cannot open directory: <path>"
- Check `--nd100-root` path exists and is readable
- Verify file path doesn't have trailing slashes or special chars

### "Failed to load program"
- Ensure file is a valid BPUN format
- Check BPUN checksum (printed if verbose)
- Verify BPUN file matches ND-100 binary format

### "Unknown command: XYZ"
- Type `HELP` to see available commands
- Check abbreviations (e.g., `LI` for `LIST-FILES`)
- Commands are case-insensitive

### Readline not working
- Optional feature (compile-time enabled)
- Falls back to basic input without it
- No command history or editing without readline

---

## Next Steps / Future Work

- ✅ Shell core (HELP, EXIT, LIST-FILES, RUN-PROGRAM)
- ✅ Command abbreviation matching
- ✅ File pattern filtering
- ✅ Script file support
- ✅ CLI integration (--monitor, --nd100-root, --script)
- ✅ INI file support (machine config)
- ⏳ CPU execution integration
- ⏳ PROG file compilation
- ⏳ Additional commands (CREATE-FILE, DELETE-FILE, etc)
- ⏳ Readline command completion
- ⏳ Telnet server integration for remote shell

---

## Technical Details

### Memory Access
- Programs loaded via `program_load()` function
- Words written to `g_volatile_memory.n_Array[address]`
- Supports word-addressed ND-100 memory (8 MW max)

### File Discovery
- Scans host filesystem using `opendir()/readdir()`
- Pattern matching on file extensions (glob)
- Supports paths with spaces and special characters

### Integration Points
- Entry: `nd100x_shell_run(nd100Root, scriptPath)` (nd100x.c line ~1095)
- Config: CLI options + INI runtime section
- File loading: reuses existing `program_load()` function
- Registers: reads global CPU state (g_reg, etc)

---

## References

- Shell implementation: `src/frontend/nd100x/nd100x_shell.c` (470 lines)
- Shell header: `src/frontend/nd100x/nd100x_shell.h`
- File loading analysis: `docs/ND100X_FILE_LOADING_ARCHITECTURE.md`
- Implementation plan: `docs/ND100X_SHELL_IMPLEMENTATION_PLAN.md`

---

**Status:** Ready for user testing and integration.
