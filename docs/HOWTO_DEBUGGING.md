# HOWTO: Debugging with nd100x

This guide covers the ways to stop, watch, and trace ND-100 code running under
the nd100x emulator: native CLI breakpoints/watchpoints (full speed, no client
required) and the DAP debugger (source-level, IDE-driven). It also documents the
performance characteristics so you can pick the right tool for deep, long-running
hunts.

Full path: `docs/HOWTO_DEBUGGING.md`

---

## 1. Quick reference

| Goal | Tool | Speed |
|------|------|-------|
| Stop when PC reaches an address | `-B/--breakpoint=ADDR` | full native |
| Stop when memory is read/written | `-W/--watch=SPEC` | full native |
| Stop after N instructions | `-n/--max-instr=N` | full native |
| Instruction trace to stderr | `-t/--trace` | slow (per-instr print) |
| Source-level step / inspect | `-d/--debugger` (DAP) | near native* |
| Dump last N instructions on halt | `-R/--ring-dump[=N]` | n/a |

\* Debugger-attached free-run ("continue") runs at near-native speed; only
interactive single-stepping pays a per-step cost.

Addresses everywhere accept **octal** (leading `0`, e.g. `0177700`), **hex**
(`0x...`), or **decimal**, identical to the `-B` convention.

---

## 2. Native PC breakpoint (`-B`)

Stop the moment the program counter reaches an address. Checked inline in the
CPU loop at full speed.

```bash
build/bin/nd100x --boot=smd --breakpoint=015 --max-instr=5000000
```

On a hit the CPU halts with a message and (if `-R` is set) an instruction ring
dump:

```
--- CPU stopped: breakpoint at 000015 ---
```

Only one PC breakpoint can be set this way. For multiple/conditional
breakpoints use DAP (section 5).

---

## 3. Native memory watchpoint (`-W/--watch`)

Stop when a specific memory location is read and/or written, **at full native
speed and without attaching a debugger**. This is the tool for needle-in-a-
haystack hunts: "run hundreds of millions of instructions until something
touches address X" (stack corruption, a struct field getting clobbered, an
unexpected write to a page-table or device word).

```
-W, --watch=SPEC      SPEC = [phys:]ADDR[:r|w|rw]   (repeatable, max 32)
```

- Default access type is `rw` (read and write); default address space is virtual.
- `phys:` prefix watches a **physical** address (caught regardless of current
  mapping — ideal when you suspect a write through a stale pointer and don't
  know the virtual mapping).
- Repeat `-W` for multiple watchpoints (up to 32).

### Examples

```bash
# Stop on any WRITE to virtual address 0177700
build/bin/nd100x --boot=smd --watch=0177700:w

# Stop on READ or WRITE of physical word 050
build/bin/nd100x --boot=smd --watch=phys:050:rw

# Read-only watch, with a safety cap so a non-firing run still terminates
build/bin/nd100x --boot=smd --watch=01234:r --max-instr=200000000 -R100

# Two watchpoints at once
build/bin/nd100x --boot=smd --watch=phys:050:w --watch=0177700:rw
```

### What you see on a hit (no debugger attached)

The CPU halts, printing the access type, address, and faulting PC, then dumps
the instruction ring:

```
Watchpoint armed: phys 000050 (rw)
--- CPU stopped: watchpoint read at 000050 (PC=000015) ---
```

### Practical notes

- Pair `-W` with `-n/--max-instr` so a run that never hits the address still
  stops, and with `-R[N]` to capture the instructions leading up to the hit.
- Use `r` / `w` / `rw` to distinguish "who reads this" from "who corrupts this".
- A virtual `-W` fires on the logical address as seen by the running code; a
  `phys:` watch fires on the translated physical address. If a value can be
  reached through more than one mapping, prefer `phys:`.
- `-W` requires a debugger-enabled build (the default native Linux/Windows
  build). On builds without the debugger it warns and is ignored.

---

## 4. Instruction trace and ring dump

```bash
# Full instruction trace to stderr (slow - one formatted line per instruction)
build/bin/nd100x --boot=smd --trace --max-instr=2000

# Keep a rolling ring of the last N instructions, dumped on any halt/crash
build/bin/nd100x --boot=smd --breakpoint=066123 --ring-dump=200
```

`-R/--ring-dump[=N]` (default 50, max 512) is cheap and always worth enabling
when chasing a crash: it shows how you arrived at the stop point.

---

## 5. DAP debugger (source-level)

For breakpoints with conditions, single-stepping, stack traces, variable and
memory inspection, attach the DAP debugger:

```bash
build/bin/nd100x --boot=aout --image=program.out --debugger --verbose
# default port 4711; change with -p/--port
```

`--debugger` starts the DAP server and waits for a client to connect. DAP
exposes the **same** watchpoint engine as `-W`, with richer addressing via the
data breakpoint `dataId` prefixes:

- `P:ADDR` — physical address
- `V:ADDR` — virtual address (default)
- `I:ADDR` — I-space only (instruction fetch / `UseAPT=false`)
- `D:ADDR` — D-space only (data access / `UseAPT=true`)
- optional `@PIL` suffix — fire only on a specific interrupt level, e.g. `I:135140@1`

Use `-W` to *reach* a deep hit at full speed, then attach DAP at the same
address for source-level context once you know roughly where the problem is.

### Pausing a freely-running CPU

A DAP `pause` request stops the CPU on the next instruction boundary and emits a
`stopped` event with `reason: pause`, after which `stackTrace` / `scopes` /
`variables` work and you can read PC and registers. This works even when the CPU
is spinning in a tight fault loop (e.g. a repeated trap-14 page fault during a
kernel `startup()`), so you can pause into a hang instead of capturing a giant
instruction trace.

The pause request is polled on the emulated machine's own timebase (~200 ms of
emulated time), not per host instruction, so latency scales with the configured
CPU speed / throttle. The poll runs at the *top* of the CPU run loop, so a page
fault or protection violation (which longjmp()s back to the top of the loop)
cannot starve it — a fault-on-every-instruction loop still honours pause.

Typical flow (over the MCP DAP bridge or any DAP client):

```
initialize -> attach/connect -> launch (stopOnEntry=false) -> run -> pause
```

After `pause` succeeds you get the `stopped`/`pause` event, then normal
inspection. Note: `launch` with `stopOnEntry=false` leaves the CPU genuinely
running (no `stopped` event until you pause or hit a breakpoint); `stopOnEntry=true`
stops immediately with `reason: entry`.

See also: `docs/DAP_INTEGRATION_GUIDE.md`, `docs/STEPPING_ANALYSIS.md`,
`docs/MIXED_STEPPING_IMPLEMENTATION_GUIDE.md`.

---

## 6. Performance characteristics

Understanding the cost model tells you which tool to reach for on a long hunt.

- **Native `-B` / `-W` run at full speed (~4.5 MIPS on a typical host).** A
  watchpoint adds no measurable per-access cost: an armed `phys:` watch measured
  identical to a no-watch run (13.34s vs 13.38s for 60M instructions).
- **The watchpoint check is gated, not scanned.** Logical watchpoints use a
  counter gate plus an 8 KB L1-resident bitmap; physical watchpoints use a
  page-granular bitmap; PC breakpoints use a PC bitmap. When nothing is armed
  the per-instruction cost is a single compare.
- **Debugger-attached free-run ("continue") is near native** (~13s/60M, on par
  with native). The async pause request is polled on the emulated machine's own
  timebase (~200 ms of emulated time), not per instruction, so it scales with
  whatever CPU speed / throttle is configured.
- **Interactive single-stepping** over the DAP socket is the only slow mode,
  because each step is a client round-trip. Do not single-step toward a target
  that is hundreds of millions of instructions away — set a `-W` watchpoint or a
  breakpoint and `continue` instead.
- **Each DAP request briefly pauses the CPU** (the pause/release handshake
  quiesces the machine while the command runs). This is invisible for normal
  debugging, but it skews measurements of guest-visible clocks: if you sample a
  counter over the socket, keep the reads sparse. The default `--rtc=wall` keeps
  the guest clock in real time regardless of emulation speed; `--rtc=ticks`
  makes it follow the instruction count instead.
- **Historical note (fixed 27-JUL-2026):** `machine_run()` used to sleep 100 ms
  after every 5000-instruction batch whenever `--debugger` was enabled, capping
  any debugger session at ~50k instructions/s (a TSS guest read its 50 Hz clock
  at 4.9 Hz: 50000/10550 = 4.74). If a debugger-attached run measures far below
  native speed, you are on a build older than that fix.

### Detach semantics

A DAP `disconnect` with `terminateDebuggee=false` (the default for attach-mode
clients) **detaches and leaves the emulator running**; you can re-attach later.
Only `terminateDebuggee=true` (or the `terminate` request) shuts the emulator
down. Before 27-JUL-2026 every disconnect killed the emulator regardless of the
flag.

### Rule of thumb

> To reach a deep event (millions of instructions in), use native `-B`/`-W` (or
> DAP breakpoint + continue). Reserve single-stepping for the last few
> instructions once you are already close.

---

## 7. Build notes

The debugger (and therefore `-B`, `-W`, and DAP) is enabled in the default
native Linux/Windows build (`WITH_DEBUGGER`). WASM Glass UI builds expose the
debugger via JavaScript `Dbg_*` functions instead of the DAP socket. RISC-V
builds disable the debugger.
