# ND-100X Interactive Shell - Usage Examples

**Files included:**
- `example-shell-config.ini` — INI configuration template
- `example-shell-commands.sh` — Script with sample commands

---

## Quick Start: 3 Ways to Run

### Method 1: CLI Flags (Simplest)

```bash
cd /path/to/nd100x

# Point to your BPUN files
./build/bin/nd100x --monitor --nd100-root=/path/to/bpun
```

Then in the shell:
```
@ LIST-FILES
@ LIST-FILES *.bpun
@ RUN-PROGRAM <filename>.bpun
@ SHOW-REGISTERS
@ EXIT
```

### Method 2: INI File (Configured)

```bash
cd /path/to/nd100x

# Use the example config (edit nd100_root path if needed)
./build/bin/nd100x --config=tools/examples/shell/example-shell-config.ini
```

**Edit first:**
```bash
# Change the path in the INI file
nano tools/examples/shell/example-shell-config.ini
# Find line: nd100_root = /path/to/bpun
# Change to your path if needed
```

### Method 3: Batch Script (Automation)

```bash
cd /path/to/nd100x

# Run commands automatically from script
./build/bin/nd100x --monitor \
  --nd100-root=/path/to/bpun \
  --script=tools/examples/shell/example-shell-commands.sh
```

**Edit script first:**
```bash
# Uncomment or edit commands in the script
nano tools/examples/shell/example-shell-commands.sh
```

---

## Working with a BPUN directory of your own

**Yes, you can absolutely use any path!**

```bash
# Point to your exact directory
./build/bin/nd100x --monitor --nd100-root=/path/to/bpun

# In the shell:
@ LIST-FILES
  (shows all files in /path/to/bpun)

@ LIST-FILES *.bpun
  (shows only .bpun files)

@ RUN-PROGRAM <filename>.bpun
  (loads from /path/to/bpun/<filename>.bpun)
```

**Path notes:**
- ✅ Absolute paths: `/mnt/data/bpun`, `/srv/nd/bpun`
- ✅ Relative paths: `./examples`, `../bpun-files`
- ✅ Home paths: `~/nd100-files`
- ✅ Current dir: `.` (default if omitted)
- ✅ Paths with spaces: `/path/to/My Files/bpun`

---

## Complete Example Workflow

### Step 1: Build (if not already built)

```bash
cd /path/to/nd100x
mkdir -p build
cd build
cmake ..
make -j4
```

### Step 2: Launch Shell

```bash
cd /path/to/nd100x

# Method A: Direct CLI
./build/bin/nd100x --monitor --nd100-root=/path/to/bpun
```

### Step 3: Use the Shell

```
@ HELP
ND-100 Interactive Shell - Available Commands:

  HELP                  Show this help message
      (abbrev: HE)
  LIST-FILES            List BPUN/PROG files (supports glob: *.bpun)
      (abbrev: LI-FI)
  RUN-PROGRAM           Load and run a BPUN file
      (abbrev: RU-PR)
  SHOW-REGISTERS        Display CPU state (PC, registers, STS)
      (abbrev: SH-RE)
  EXIT                  Exit the shell
      (abbrev: EX)

Note: Commands are case-insensitive and support abbreviation.
      File names use host extensions (.bpun, .prog, etc)

@ LIST-FILES
Files in /path/to/bpun matching '*':
  kernel.bpun
  hello.bpun
  test.prog
  diag.bpun

@ LI-FI *.bpun
Files in /path/to/bpun matching '*.bpun':
  kernel.bpun
  hello.bpun
  diag.bpun

@ RUN-PROGRAM hello.bpun
Loading /path/to/bpun/hello.bpun...
Program loaded at entry point: 0o001000
(CPU execution not yet integrated with shell)

@ SH-RE
CPU Registers:
  A:     0o000000
  B:     0o000000
  D:     0o000000
  X:     0o000000
  L:     0o000000
  T:     0o000000
  P:     0o001000
  STS:   0o000000

@ EXIT
Exiting shell.
Shell exited.
```

---

## Example: Custom INI File

**File: `/tmp/my-nd100-shell.ini`**

```ini
[machine]
cpu = 100

[boot]
device = none

[runtime]
shell = on
nd100_root = /path/to/bpun
# Optional: auto-run script on startup
# script = /tmp/commands.sh
memory = 4
```

The section is `[machine]` with the key `cpu`; a `[cpu]` section is rejected
with "unknown section [cpu]". `device = none` powers the machine on with
nothing loaded, which is what the shell wants; a disc boot needs a
`[controller.smd.0]` section with a `disk0`.

**Run:**
```bash
./build/bin/nd100x --config=/tmp/my-nd100-shell.ini
```

---

## Example: Batch Automation Script

**File: `/tmp/test-all-bpun.sh`**

```bash
# List what we're testing
LIST-FILES *.bpun

# Test kernel
RUN-PROGRAM kernel.bpun
SHOW-REGISTERS

# Test hello world
RUN-PROGRAM hello.bpun
SHOW-REGISTERS

# Done
EXIT
```

**Run:**
```bash
./build/bin/nd100x --monitor \
  --nd100-root=/path/to/bpun \
  --script=/tmp/test-all-bpun.sh
```

---

## Troubleshooting

### Error: "Cannot open directory: /path/to/bpun"

**Solutions:**
```bash
# Check path exists and is readable
ls -la /path/to/bpun

# If path doesn't exist, try alternative:
./build/bin/nd100x --monitor --nd100-root=./examples

# Create test directory if needed
mkdir -p /path/to/bpun
cp some-file.bpun /path/to/bpun/
```

### No files show up in LIST-FILES

**Check:**
```bash
# Verify files exist with correct extension
ls /path/to/bpun/*.bpun

# Check if shell is looking in right place
@ LIST-FILES
# Should show path in output like:
# Files in /path/to/bpun matching '*':
```

### Command not recognized

**Help:**
```bash
@ HELP
# Shows all valid commands and abbreviations

@ LI    # Abbreviated LIST-FILES - works
@ LISTFILES  # Wrong - needs hyphen
@ LIST-FILES  # Correct - full name
```

---

## File Locations (Created Files)

**Example files (all in `tools/examples/shell/`):**
- `example-shell-config.ini`
- `example-shell-commands.sh`
- `SHELL_EXAMPLES.md` - this file

**Quick Start Guide:**
- `docs/ND100X_SHELL_QUICKSTART.md`

**Shell Implementation:**
- `src/frontend/nd100x/nd100x_shell.c` (470 lines)
- `src/frontend/nd100x/nd100x_shell.h` (Public API)

---

## Next Steps

1. **Build:** `make -j4` in build/ directory (if not done)
2. **Test:** Run with your path: `./build/bin/nd100x --monitor --nd100-root=/path/to/bpun`
3. **Explore:** Try LIST-FILES, RUN-PROGRAM, SHOW-REGISTERS
4. **Automate:** Create scripts to batch-test your BPUN files
5. **Report:** Any issues or missing features

---

**Ready to go! Your path `/path/to/bpun` is fully supported.**
