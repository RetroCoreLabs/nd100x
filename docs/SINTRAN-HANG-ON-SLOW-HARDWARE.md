# SINTRAN hang on slow hardware

**Status: root cause not found.** This document records what was measured, what
works, what does not, and what has been ruled out, so the next person does not
repeat the same tests.

Measured 2026-10-13 to 2026-10-16 on a Raspberry Pi 4 Model B (section 1),
SINTRAN M booting from `WD0-SINTRAN-M.IMG`, Ethernet II card on a TAP interface.

## 1. Hardware under test

The only machine where the hang has been measured and reproduced. It is also the
machine the service runs on.

| Item | Value |
|---|---|
| Board | Raspberry Pi 4 Model B Rev 1.4, 2 GB (board revision `b03114`) |
| SoC / CPU | Broadcom BCM2711, 4x Cortex-A72 (64-bit ARMv8), 1 thread per core, L1d 128 KiB, L2 1 MiB |
| CPU clock | 600-1800 MHz, governor `ondemand`; clock measured at 1800 MHz under load |
| Memory | 1849 MB usable, about 430 MB in use with the service running |
| Storage | 29 GB root filesystem on the SD card, 26% used |
| OS | Debian GNU/Linux 11 (bullseye), kernel 6.1.21-v8+ (64-bit), glibc 2.31 |
| Toolchain | gcc 10.2.1, cmake 3.18.4 |
| Network | Wi-Fi only (wlan0, 5 GHz, power save turned off). eth0 has no cable and is unused |
| Services | `nd100x-net`, `nd100x-garp`, `nd100x` (see `ND100X-SERVICE-AND-LAN.md`); the emulator uses about 40% of one core when idle and a full core while SINTRAN is busy |
| Temperature | 70.6 C when last read at 5 days uptime, and 80.8 C while the tests were running. The firmware flag `get_throttled` reads `0xe0000`: since boot the CPU has been frequency-capped, throttled and has hit the soft temperature limit. No under-voltage bit is set. Cooling (heatsink, fan, case) was not recorded |

Why this matters for the hang:

- The emulator's CPU thread is single-threaded. It needs a full core. Anything that
  takes CPU time from that core (other processes, heat throttling, the `ondemand`
  governor ramping up late) slows the ND.
- Measured ND speed on this Pi with the Release build and the Ethernet II card:
  about 1.17 million instructions per second when SINTRAN is idle and 2.27 million
  while `file-stat` runs. The emulator's real-time setting (`--throttle`) is 0.5275 million. When the emulator
  is held to about 0.56 million (three busy loops on its core) the hang appears.
- The other Pi models and other hosts have not been tested. The original
  reporter's WSL build printed all 101 files; its build type and speed were not
  recorded.

## 2. The symptom

`file-stat,,,` on the ND prints about 101 file entries (41,311 bytes of listing).
On some builds it stops partway, usually after 11-21 KB (20-45 files), at a
different point each time. After that:

- No more output arrives, on the telnet terminal server (port 9000) and on the
  ND's own telnet (port 23, over the Ethernet II card).
- Typing on the hung terminal gives no echo.
- The emulator process is **not** blocked. The CPU thread runs at 100% of its
  core and the ND keeps executing instructions.
- It does not recover within a minute or more. One run (section 4.3) did
  continue after about 90 seconds; that was not reproduced.

Not part of this problem: `stop-system` once took a long time to exit. That was TCP
waiting for a dead telnet session to time out, not the emulator.

## 3. Short answer: what works and what hangs

| Configuration | Ethernet card | Hang? |
|---|---|---|
| Release, normal CPU | yes | **No** (this is what the Pi service runs) |
| Release, normal CPU | no | No |
| Release, starved of CPU (3 busy loops on its core) | yes | **Yes**, every run (16 of 16, including the observation runs) |
| Release, starved, `--rtc=ticks` | yes | No, 3 of 3 |
| Release, starved (3 busy loops) | **no** | No, 3 of 3 |
| Release, starved harder (6 busy loops, estimated same speed as the card case) | **no** | No, 3 of 3 |
| Release, lightly starved (1 busy loop) | yes | No, 2 of 2 |
| Debug (`-O0`, the default `make`) | yes | **Yes**, 11 of 13 runs |
| Debug (`-O0`) | no | No (3 runs, less controlled) |
| Debug configuration compiled with `-O3` | yes | No, 6 of 6 |
| Debug with ASan + UBSan | yes | No, 1 of 1 (one run only) |

So, in these tests:

1. **A hang needs the Ethernet II card in the configuration.** Without it nothing
   hung, including at the same CPU share that hung the card case.
2. **It also needs a slow emulator.** The card case completes when the host gives the
   emulator enough CPU and hangs when it does not. Debug builds are slow enough
   to hit it on their own.
3. **The wall-clock RTC (`--rtc=wall`, the default) matters on a starved Release
   build.** With `--rtc=ticks` the same starved Release build completed 3 of 3.
   A Debug build still hung with `--rtc=ticks` (3 of 3), so the clock mode is not
   the whole story.
4. **No build type is hang-free under every condition.** Release hangs when starved.
   Release is simply fast enough on a normal Pi that it does not hit it.

## 4. Everything that was tested

All runs used `file-stat,,,` on a scratch copy of the image unless noted. A run
counts as hung when output stopped for 8 seconds before the full listing arrived.
"Complete" means the whole listing, 41,311 bytes (42,273 bytes counted by the
later test client, which includes the prompt and login text).

### 4.1 The original observation

| Build | Setup | Result |
|---|---|---|
| Debug of commit `b528456` (the service binary at the time) | service, Ethernet II on TAP `nd0`, ND telnet port 23 and terminal port 9000 | 21 files / 7,989 bytes, then nothing |
| Release of current main | scratch, no ethernet card, port 9001 | all 101 files, 18 s |
| Release of current main (installed to `/usr/local/bin`) | service, ND telnet port 23 | all 101 files, 41,369 bytes |

### 4.2 Commit versus build type (scratch, port 9001)

| Build | Ethernet card | Result |
|---|---|---|
| `b528456` Release | no | complete, 25 s |
| `b528456` Debug | no | complete, 45 s |
| `b528456` Debug | yes, idle spare TAP `nd1` | complete, 57 s |
| `b528456` Debug | yes, real TAP `nd0` (service stopped) | **30 files / 11,570 bytes** |
| current main Debug | no | complete, 28 s |
| current main Debug | yes, `nd0` (service stopped) | **56 files / 21,768 bytes** |

Conclusion from this table: the newer commits do not fix it, and Debug alone
does not break it. The earlier guess that "the device ticks on demand" commit
fixed it was wrong.

### 4.3 Debug builds with the card

Current main, Debug, card on a TAP (`nd0` with the service stopped, or the spare
`nd1`):

- Seven runs: five stopped at 17,695 / 11,440 (with `--rtc=ticks`) / 20,146 /
  18,625 / 19,014 bytes, and two completed.
- Debug with no busy loops, three runs each: `--rtc=wall` stopped at 20,513 /
  18,550 / 18,560 bytes, and `--rtc=ticks` stopped at 10,688 / 11,183 / 11,132
  bytes. All six hung.
- With a 90 second reader timeout, one run on `nd0` did finish, 119 s in total.
  Every other run was watched for much less than that, or for over a minute with
  no output. Treat "it resumes" as unproven.

### 4.4 Compile variants (current main, card enabled, spare TAP `nd1`)

| Variant | How it was built | Result |
|---|---|---|
| Debug, `-g` only (`-O0`) | `make debug` | hangs, see 4.3 |
| Debug configuration with `-O3` | `CFLAGS=-O3 make debug BUILD_DIR_DEBUG=...` | complete, 6 of 6 |
| Debug with ASan + UBSan | `CFLAGS="-fsanitize=undefined,address -fno-omit-frame-pointer" make debug` | complete, 1 run |
| Debug with terminal output delay 1000 (was 100) | `IODELAY_TERMINAL` raised, scratch only | hung in 4 of 8 runs, same as 100 |
| Release, `-O3 -DNDEBUG` | `make release` | complete when not starved |

UBSan reported only one harmless signed-overflow shift at `src/cpu/cpu_instr.c`
lines 1980 and 1986 (`gA << 16` in a mask-to-24-bit expression). It does not
change behaviour.

Build flag facts: Debug is `-g` with no optimisation and compiles in
`ND100X_HOT_TRACE`, which only runs when the `mms`, `mmsmap`, `trap` or
`pkswitch` trace log categories are turned on (they are off by default). Release is
`-O3 -DNDEBUG` with the hot trace compiled out.

### 4.5 Starving the Release build

The starved case runs the Release build pinned to one core
(`taskset -c 3`) together with N busy-loop processes pinned to the same core.

| Busy loops | RTC | Ethernet card | Result | ND speed |
|---|---|---|---|---|
| 0 | wall | yes | complete | 1.17 M instr/s idle, 2.27 M while listing |
| 1 | wall | yes | complete, 2 of 2 | not measured |
| 3 | wall | yes | **hung**, every run (16 of 16; stops at 11.0-13.5 KB) | about 555-565 k instr/s |
| 3 | wall | yes, TAP down and IPv6 off (no packets possible) | **hung**, 3 of 3 | about 550 k instr/s |
| 3 | ticks | yes | complete, 3 of 3 | not measured |
| 3 | wall | no | complete, 3 of 3 | about 950 k instr/s |
| 6 | wall | no | complete, 3 of 3 | about 545 k instr/s (estimated from CPU share, not measured) |

The card costs the emulator about 40% of its speed at the same CPU share,
because the 68000 on the card is stepped on every ND instruction. Even so, the
no-card case at the matched estimated speed did not hang, so the hang is not
just "too slow". The card and low CPU share together are required.

## 5. What the emulator is doing during the hang

Read with `gdb` attached to a starved Release build that had been compiled with
`-g3`, in a hung run and in a healthy run at the same point of `file-stat`.

### 5.1 The clock is fine

- The RTC fires exactly 50 times per second in healthy and in starved runs. No
  pulses are lost and none pile up.
- The starved ND executes about 11,000 instructions per pulse (healthy: 23,000 at
  idle, 45,000 while listing).
- RTC status and control are identical in both states (`status=1`, `control=6401`,
  interrupt enabled). The RTC device is registered at level 13. The CPU was
  observed at PIL 14 and at PIL 13 while hung and while input interrupts were being
  raised.

### 5.2 Interrupt state

| Item | Hung | Healthy, during `file-stat` |
|---|---|---|
| Interrupts on (`IONI`) | on (1) | on |
| Interrupt enable mask `PIE` | `7c3f` | `7c3f` (same) |
| Levels the CPU is seen at | 1, 2, 14 (never 0, never 10) | mostly 10 and 4, also 0, 2, 13 |
| Pending bits `PID` | `0002`, `0006`, `4002` | `0412`, `0402`, `0012`, `0006`... (bit 10 set) |
| Terminal output interrupt (level 10) | **never seen** | dominant: one per character |
| Any device interrupt bit pending | none | present |

In the hung state SINTRAN stops writing characters to the terminal. The level-10
terminal output handler does not run at all. The CPU instead runs code at levels
1 and 2 at many different addresses (octal around 32000-40000 and 110000-111000).
It is not stuck in a tight loop. That code has not been identified.

### 5.3 Devices at the hang

- Every terminal: output status `8` (ready for transfer), input status `5`,
  interrupt bits `0`, no queued I/O delay. Nothing is waiting on a terminal
  device.
- Winchester disc controller: status `8` (ready), control `65`, no interrupt
  bits, no queued delay.
- Ethernet card: interrupt bits `0`. The ND's reads and writes into the card's
  shared memory window do **not change** while hung (reads 266,044, writes
  787,403, constant) and are also constant in a healthy run during `file-stat`.
  So SINTRAN is not busy-waiting on the card. The 68000 on the card keeps running
  at a low rate.

### 5.4 Keyboard input on the hung terminal

The hung line is the emulator's device `TERMINAL 8/ TET9` (ident 47, shown as
"Terminal 39" in the telnet menu). With breakpoints on the input queue and on
interrupt generation, three typed keys were queued and the terminal raised
**level-12 input interrupt requests (four in total)**. Two were raised while the CPU
was at level 13 and two while it was at level 1. The input interrupt enable was on.
The terminal delivers the next character only after the previous one has been read,
so SINTRAN read at least some of the input. There was still no echo and no output.

One stray queued key (code 32, a space) appeared that the test client did not send;
it is unexplained.

Not measured: whether other logged-in terminals also get level-12 interrupts, and
whether they are hung at the same time. The attempt failed on a bug in the test
client (it sent ESC before the telnet server had attached the terminal).

## 6. Ruled out

| Suspect | How it was ruled out |
|---|---|
| RTC losing interrupts | 50 pulses/s in every run, CPU takes the clock interrupt, IONI and PIE unchanged |
| Terminal "ready" flag set too early | the terminal already clears ready on write and sets it after `IODELAY_TERMINAL` = 100 ticks; 1000 changed nothing |
| Telnet server | client thread alive in its normal `poll`; hung on the direct terminal port as well |
| Network packets | hang with the TAP down and IPv6 off |
| Which TAP | hangs on `nd0` and on `nd1` |
| Debug-only trace code | guarded by trace log categories that are off |
| Compiler undefined behaviour | UBSan clean apart from one harmless shift |
| SINTRAN polling the card | card window read/write counters constant during the hang |
| Wall-clock RTC as the only cause | Debug build hangs with `--rtc=ticks` too |

## 7. What this means

- **For the Pi service:** run the Release build (`make release`, `sudo make
  install`). It completes on a normal Pi. It could hang if the Pi is starved
  (heat throttling, other load) while the Ethernet II card is enabled. That has
  not been seen on the service. `--rtc=ticks` (INI `[machine] rtc = ticks`)
  protected the starved Release build in testing, but it has not been tried on the
  real service and did not protect the Debug build.
- **For Debug builds with the card:** expect hangs. Use `-O3` in a Debug-style
  build (`CFLAGS=-O3 make debug`) if a debug build is needed.
- **For the root cause:** the hang is a SINTRAN-side wait that does not recover
  by itself. The emulator raises the interrupts it should. Open questions:
  - What code runs at levels 1 and 2 during the hang, and what is it waiting for?
  - Why does the presence of the card (without any traffic touching it during the
    hang) change the outcome at equal emulator speed?
  - Why does a Debug build still hang with a tick-driven clock?
  - Does the level-12 handler run to completion when a key is typed, and do other
    terminals hang too?

## 8. How to reproduce

1. Build a Release binary: `make release`.
2. Copy the image and use an INI with `[controller.eth.0]` enabled (any TAP, even
   a spare one with no peer) and `telnet = 9001`.
3. Start three busy loops on the same core as the emulator:
   `taskset -c 3 bash -c 'while :; do :; done' &` (three times), then
   `taskset -c 3 build_release/bin/nd100x --config <ini>`.
4. After SINTRAN has started (the log shows `FTPRT started`), connect to the
   terminal port, pick a terminal, press ESC, log in as SYSTEM (empty password) and
   run `file-stat,,,`. The listing stops at about 11-14 KB.
5. Remove the card from the INI, or `--rtc=ticks`, or the busy loops, and the same
   run completes.

For the state dump, build with `CFLAGS=-g3 make release BUILD_DIR_RELEASE=...` and
attach `gdb` to the running emulator. Useful expressions: `gPIL`, `gPID`, `gPIE`,
`gPK`, `gSTSr`, `g_reg->reg_STS`, and the device table `device_manager.devices[]`
with `interruptBits` and `ioDelayCount`. Breakpoints on `terminal_queue_key_code`
and `dev_generate_interrupt` show terminal input and interrupt generation.

## 9. Caveats

- Sample sizes are small: single runs for the ASan/UBSan build, the no-card Debug
  builds and the "resumed after 90 s" run. Counts such as 16 of 16 and 11 of 13
  are real counts, not rates.
- Hung is defined by 8 seconds without output; the longest watch of a hung run
  was a little over a minute.
- The no-card speed at 6 busy loops is estimated from CPU share, not measured.
- Host heat and other load on the Pi varied during the testing.

## 10. Next steps

In order of value:

1. **Find what SINTRAN does during the hang.** Identify the code running at levels
   1 and 2 (octal addresses around 32000-40000 and 110000-111000) and what it is
   waiting for. Needs a SINTRAN symbol map for this image, or a short instruction
   trace (`--trace`, or the ring dump) of a hung run, compared with a healthy run at
   the same point. This is the route to the root cause.
2. **Finish the second-terminal test.** Log in on two terminals, start `file-stat`
   on one, and when it hangs check whether the other still answers and whether
   keys on it raise level-12 interrupts. The earlier attempt failed because the
   test client sent ESC before the telnet server had attached the terminal; wait
   for "Connected to Terminal" first. This shows whether the whole terminal
   subsystem is stuck or only one line.
3. **Try `--rtc=ticks` on the service** (INI `[machine] rtc = ticks`) so a loaded or
   throttled Pi cannot hang it. It protected the starved Release build in testing,
   but not the Debug build, and it has not been tried on the real service. Test
   before relying on it: run `file-stat` repeatedly, with the Pi under load.
4. **Keep the Pi from starving the emulator.** Record the cooling, consider a
   heatsink or fan, and check `vcgencmd get_throttled` after long runs. The Pi was at
   80 C during testing and has been throttled before.
5. **Bring the repository up to date.** The local clone is 10 commits behind GitHub,
   including the move of `ND5000.ini` into `config/` and the new `--host-cpu` option.
   Pull them (carrying the local `ND5000.ini` edit over to its new path), rebuild
   with `make release`, install with `sudo make install` and restart the service.
   Then repeat the tests above on the new build, because `--host-cpu=low|full` may
   change how much CPU the emulator takes and so how it behaves on slow hardware.

A fix is done when the starved Release build with the card passes `file-stat,,,`
repeatedly (the reproduction in section 8 no longer hangs), and a Debug build with
the card no longer hangs either.
