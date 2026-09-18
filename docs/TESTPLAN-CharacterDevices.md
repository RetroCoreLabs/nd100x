# Character Devices Port — Test Plan

## Overview

This document covers the validation of three character devices ported from RetroCore to nd100x, plus supporting infrastructure changes (VScreen, keyboard handling, WASM frontend, template-glass UI).

### Devices Added/Changed

| Device | I/O Address | Ident | Level | SINTRAN LD | Status |
|--------|-------------|-------|-------|------------|--------|
| Paper Tape Reader | 0400-0403 | 02 | 12 | 2 | Rewritten (buffer-based) |
| Paper Tape Punch | 0410-0413 | 02 | 10 | 3 | New |
| Line Printer | 0430-0433 | 03 | 10 | 5 | New |

### Files Changed

**New files (8):**
- `src/devices/lineprinter/device_line_printer.h` / `.c`
- `src/devices/papertapewriter/device_paper_tape_writer.h` / `.c`
- `src/frontend/nd100x/vscreen.h` / `.c`
- `template-glass/js/line-printer.js`
- `template-glass/js/paper-tape.js`

**Modified files (17):**
- `src/devices/papertape/device_paper_tape.h` / `.c` — full rewrite
- `src/devices/devices_types.h` — new device types, IO delay constants
- `src/devices/devicemanager.c` — factory cases, device registration
- `src/devices/CMakeLists.txt` — build integration
- `src/frontend/nd100x/nd100x.c` — VScreen, output handlers, Alt+digit/F12
- `src/frontend/nd100x/nd100x_types.h` — CLI config fields
- `src/frontend/nd100x/config.c` — new CLI options
- `src/frontend/nd100x/CMakeLists.txt` — vscreen.c
- `src/frontend/nd100wasm/nd100wasm.c` — separate ring buffers, exports
- `src/ndlib/keyboard.h` / `.c` — Alt+digit key detection, escape sequence timing
- `template-glass/index.html` — new windows, menus, script tags
- `template-glass/js/emu-proxy.js` — printer/tape polling, loadPaperTape
- `template-glass/js/emu-proxy-worker.js` — device class routing
- `template-glass/js/emu-worker.js` — printer/tape ring buffer draining
- `template-glass/js/toolbar.js` — menu click handlers

---

## Part 1: Automated Tests

Two automated test scripts validate the core functionality without manual interaction.

### T1.1 — CONFIG Hardware Test (`tests/test_configure.sh`)

Boots from floppy, loads CONFIG via TPE, runs the hardware investigation, and validates the output.

```bash
./tests/test_configure.sh
```

**What it tests (18 checks):**
- All 3 devices created with correct addresses and identcodes
- CONFIG detects Paper Tape Reader, Paper Tape Punch, and Line Printer
- Paper Tape Punch passes with identcode 2 on level 10
- Line Printer passes with identcode 3 on level 10
- Paper Tape Reader fails as expected (no tape loaded — correct behavior)
- No errors for Paper Tape Punch or Line Printer
- Correct logical device numbers (Punch=3, Printer=5)
- Correct interrupt priority assignments
- CONFIG completes with exactly 1 error (the expected reader error)

### T1.2 — Glass UI Window Tests (`tests/test_glass_windows.sh`)

Runs headless Chrome against a test HTML page that exercises the Line Printer and Paper Tape JavaScript modules with a mock DOM.

```bash
./tests/test_glass_windows.sh
```

**What it tests (22 checks):**

*Line Printer module:*
- `handlePrinterOutput` exported as global function
- Window registered with windowManager as "Line Printer"
- ASCII character output renders correctly
- LF newline handling
- Line trimming at 2000-line limit
- Close button calls `closeWindow` with correct window ID

*Paper Tape module:*
- `handlePaperTapeWriterOutput` exported as global function
- Window registered with windowManager as "Paper Tape"
- Writer byte count status updates
- Hex display shows correct byte values
- ASCII display alongside hex
- 16-byte row formatting with line breaks
- Close button works
- Reader shows "No tape loaded" default status
- Load button triggers file input click
- Download button exists
- Download with no emulator data shows alert

---

## Part 2: Build Verification

### T2.1 — Native debug build
```bash
make clean && make debug
```
- [ ] Compiles without errors
- [ ] Only pre-existing warnings (panel.c, machine.c)

### T2.2 — Native release build
```bash
make release
```
- [ ] Compiles without errors

### T2.3 — WASM glass build
```bash
make wasm-glass
```
- [ ] Compiles without errors
- [ ] Output: `build_wasm_glass/bin/nd100wasm.js` and `.wasm` exist

---

## Part 3: Device Registration and CONFIG

CONFIG is a TPE command run from floppy boot, NOT a SINTRAN command.

### T3.1 — Device startup messages
```bash
build/bin/nd100x --boot=floppy
```
- [ ] Startup output shows all three devices created:
  - `Paper Tape Reader created: PAPER TAPE READER 1 CODE[2] ADDRESS[400-403]`
  - `Paper Tape Punch created: PAPER TAPE PUNCH 1 CODE[2] ADDRESS[410-413]`
  - `Line Printer device created: LINE PRINTER 1 CODE[3] ADDRESS[430-433]`

### T3.2 — CONFIG device test
Boot from floppy, at TPE prompt run:
```
CONFIG
RUN
```
Verify:
- [ ] **Paper Tape Punch**: No errors. Shows `PAPER TAPE PUNCH 1  410  413` with identcode 2, level 10
- [ ] **Line Printer**: No errors. Shows `LINE PRINTER 1  430  433` with identcode 3, level 10
- [ ] **Paper Tape Reader**: Expected error "Device never ready for transfer" (no tape loaded)
- [ ] Only 1 error detected total (the Paper Tape Reader)

### T3.3 — Interrupt handling verification
In CONFIG output, verify interrupt priority table:
- [ ] Paper Tape Punch responds to IDENT on level 10 with identcode 2
- [ ] Line Printer responds to IDENT on level 10 with identcode 3

---

## Part 4: Keyboard and VScreen (Native)

### T4.1 — F12 unified menu
Boot from floppy or SMD, then press F12:
- [ ] Unified menu appears with [1] Floppy Database Browser and [2] Virtual Screen Selector
- [ ] Pressing 1 opens the floppy database menu
- [ ] Pressing 2 opens the virtual screen selector
- [ ] ESC exits back to emulator
- [ ] Terminal output resumes normally after exit

### T4.2 — Alt+digit screen switching
- [ ] Alt+1 switches to Console (screen 1)
- [ ] Alt+2 through Alt+6 switch to respective screens
- [ ] Alt+9 is ignored (beyond screen count)
- [ ] After switching away and back, terminal output is intact
- [ ] Shows all screens via F12 > 2: Console, Terminal 5, Terminal 6, Terminal 7, Line Printer, Paper Tape Punch
- [ ] Active screen marked with `*`
- [ ] Console marked as input-capable
- [ ] Line Printer and Paper Tape Punch marked as `(output only)`

### T4.3 — Terminal settings cleanup
```bash
build/bin/nd100x --boot=floppy
# Press Ctrl-C to exit
echo "can you see this?"
```
- [ ] Echo is restored after Ctrl-C exit
- [ ] Terminal is in normal mode (not raw/cbreak)

---

## Part 5: CLI Options (Native)

### T5.1 — Help text
```bash
build/bin/nd100x --help
```
- [ ] Shows `--printdir` / `-P` option with description
- [ ] Shows `--tapedir` / `-T` option with description
- [ ] Shows `--tape` / `-t` option with description

### T5.2 — Print directory option
```bash
build/bin/nd100x --boot=smd --printdir=/tmp/nd100x-prints
```
- [ ] No error on startup
- [ ] When printer output occurs, files go to `/tmp/nd100x-prints/`

### T5.3 — Tape directory option
```bash
build/bin/nd100x --boot=smd --tapedir=/tmp/nd100x-tapes
```
- [ ] No error on startup

### T5.4 — Paper tape file loading
```bash
build/bin/nd100x --boot=bpun --tape=images/test.bpun
```
- [ ] Paper tape file is loaded (message: `Paper tape loaded: NNN bytes`)

---

## Part 6: Line Printer Output (Native)

### T6.1 — Printer output to file
Boot SINTRAN and send output to the line printer (SINTRAN logical device 5):
```
@LIST-FILE filename,5
```
or any command that outputs to logical device 5.

- [ ] Output does NOT appear on console terminal
- [ ] File `./prints/print-1.txt` is created (or custom dir if --printdir used)
- [ ] File contains the printed output
- [ ] Message appears: `[Printer job 1 saved to ./prints/print-1.txt]`

### T6.2 — Print job boundaries
- [ ] After 5 seconds of no printer output, job is auto-closed
- [ ] Form feed character (014) in output triggers immediate job close
- [ ] Subsequent print output creates `print-2.txt`, `print-3.txt`, etc.

### T6.3 — Printer VScreen
Press Alt+5 (or F12 > 2) to switch to Line Printer screen:
- [ ] Any buffered printer output is visible
- [ ] New printer output appears in real-time when on this screen
- [ ] Switching back to Console shows console output intact

---

## Part 7: Paper Tape Reader (Native)

### T7.1 — Load BPUN file via CLI
```bash
build/bin/nd100x --boot=smd --tape=images/FILSYS-INV-Q04.BPUN
```
- [ ] Startup shows: `Paper tape loaded: NNN bytes`
- [ ] SINTRAN can read from the paper tape reader (logical device 2)

### T7.2 — Test mode (CONFIG)
In CONFIG, the paper tape reader test mode should work:
- [ ] Writing control word with Test + ReadActive bits causes readyForTransfer
- [ ] Data register increments on each test read

---

## Part 8: Paper Tape Punch (Native)

### T8.1 — Punch output to file
If SINTRAN writes to logical device 3 (paper tape punch):
- [ ] Output is accumulated in the device buffer
- [ ] After 5 seconds of silence, file is saved as `./tapes/tape-1.bpun`
- [ ] Message appears: `[Paper tape job 1 saved: ./tapes/tape-1.bpun (NNN bytes)]`

### T8.2 — Shutdown flush
- [ ] On program exit, any pending tape data is flushed to file

---

## Part 9: WASM Glass UI

### T9.1 — Start WASM glass
```bash
make wasm-glass-run
# Open http://localhost:8000/index.html in browser
```
- [ ] Page loads without JavaScript errors (check browser console)
- [ ] Emulator boots SINTRAN

### T9.2 — Menu items
- [ ] View menu shows "Line Printer" menu item
- [ ] View menu shows "Paper Tape" menu item

### T9.3 — Line Printer window
Click View > Line Printer:
- [ ] Line Printer window opens
- [ ] Window is draggable (via header)
- [ ] Window is resizable (via corner handle)
- [ ] Close button works
- [ ] Window position persists across page reloads (localStorage)
- [ ] When SINTRAN prints to logical device 5, output appears in the window
- [ ] Output auto-scrolls
- [ ] Output is in monospace font (`<pre>` element)

### T9.4 — Paper Tape window
Click View > Paper Tape:
- [ ] Paper Tape window opens with Reader and Writer sections
- [ ] Window is draggable, resizable, closeable
- [ ] Window position persists across page reloads

### T9.5 — Paper Tape Reader (upload)
In the Paper Tape window, Reader section:
- [ ] "Load Tape" button exists
- [ ] Clicking it opens a file picker
- [ ] Selecting a .BPUN file shows filename and byte count in status
- [ ] The file is loaded into the emulator (can be read by SINTRAN)

### T9.6 — Paper Tape Writer (output + download)
When SINTRAN writes to logical device 3:
- [ ] Hex + ASCII display in the Writer section shows punched bytes
- [ ] Byte count status updates
- [ ] "Download" button exists
- [ ] Clicking Download saves a `.bpun` file with the accumulated output

### T9.7 — Separate output channels
- [ ] Printer output goes ONLY to Line Printer window (not terminal)
- [ ] Paper tape writer output goes ONLY to Paper Tape window (not terminal)
- [ ] Terminal output is unaffected by printer/tape activity
- [ ] No cross-contamination between output channels

### T9.8 — Worker mode compatibility
If using worker mode (`emu-proxy-worker.js`):
- [ ] Printer output is routed correctly via device class encoding
- [ ] Paper tape writer output is routed correctly
- [ ] Terminal output still works normally

---

## Part 10: Regression Tests

### T10.1 — Terminal output
- [ ] Console terminal (logical device 1) works as before
- [ ] Characters display correctly
- [ ] Input echoing works
- [ ] Terminals 5-7 receive output when addressed

### T10.2 — Floppy operations
- [ ] Floppy boot works (`--boot=floppy`)
- [ ] SMD boot works (`--boot=smd`)
- [ ] F12 floppy menu works (see T4.1)

### T10.3 — Debugger
```bash
build/bin/nd100x --boot=smd --debugger
```
- [ ] DAP debugger starts on configured port
- [ ] Breakpoints, step, continue work
- [ ] Register inspection works

### T10.4 — BPUN boot
```bash
build/bin/nd100x --boot=bpun --image=images/test.bpun
```
- [ ] BPUN file loads and executes

---

## Part 11: Known Limitations

1. **Paper Tape Reader without tape**: If no tape file is loaded (no `--tape` option), the reader will never become ready for transfer when ReadActive is set. This is correct behavior — there's no tape to read. CONFIG will report this as an error.

2. **Alt+digit in some terminals**: Alt+digit may be intercepted by the terminal emulator for tab switching. Use the F12 unified menu as a fallback.

3. **F12 on compact keyboards**: The code accepts both standard (`\x1B[24~`) and alternate (`\x1B[6~`) escape sequences.

4. **Printer escape codes**: No printer escape code interpretation (Epson, etc.) is implemented yet. Raw characters are written to file.

5. **VScreen buffer**: Each virtual screen buffers up to 200 lines. Older output scrolls off the buffer and is lost when switching screens.

---

## Quick Smoke Test Checklist

For a fast validation, run these in order:

1. [ ] `make clean && make debug` — builds OK
2. [ ] `./tests/test_configure.sh` — all 18 automated tests pass
3. [ ] `./tests/test_glass_windows.sh` — all 22 automated UI tests pass
4. [ ] `make wasm-glass` — builds OK
5. [ ] Boot from floppy: `build/bin/nd100x --boot=floppy` — TPE prompt appears
6. [ ] Press F12 — unified menu appears with Floppy and Screen Selector options
7. [ ] Press Alt+1 through Alt+6 — direct screen switching works
8. [ ] Press Ctrl-C — terminal echo is restored
9. [ ] Open WASM build in browser — Line Printer and Paper Tape windows accessible from View menu
