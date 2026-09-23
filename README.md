# nd100x

[![Build & Release](https://github.com/RetroCoreLabs/nd100x/actions/workflows/build-release.yml/badge.svg)](https://github.com/RetroCoreLabs/nd100x/actions/workflows/build-release.yml)
[![RISC-V (Milk-V Duo)](https://img.shields.io/github/actions/workflow/status/RetroCoreLabs/nd100x/build-release.yml?branch=main&label=RISC-V%20%28Milk-V%20Duo%29&logo=riscv)](https://github.com/RetroCoreLabs/nd100x/actions/workflows/build-release.yml)
[![Latest Release](https://img.shields.io/github/v/release/RetroCoreLabs/nd100x?include_prereleases&sort=semver)](https://github.com/RetroCoreLabs/nd100x/releases/latest)
![Platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20RISC--V%20%7C%20WebAssembly-blue)

ND-100/CX minicomputer emulator written in C. Full CPU emulation with MMS1/MMS2 memory management, SMD, Winchester, SCSI and floppy disk controllers, HDLC networking, DAP debugger, telnet server, and a glassmorphism browser UI via WebAssembly.

For more information about the ND-100 series of minicomputers: <https://www.ndwiki.org/wiki/ND-100>

[Try it online](#try-it-in-your-browser) · [Quick start](#quick-start) · [Status](#status) · [Building](#building-the-project) · [Releases](#releases-github-actions) · [About the ND-100](#about-the-nd-100)

## Try it in your browser

Run the emulator directly at **<https://nd100x.hackercorp.no/>** — no download or install required. WebAssembly build with full terminal, debugger, and SINTRAN OS inspection tools.

## Quick start

**Pre-built binaries** for Linux (x64, arm64), Windows (x64), macOS (arm64), and RISC-V (Milk-V Duo, musl) are on the [Releases page](../../releases).

**Build from source on Linux:**

```bash
sudo apt install -y build-essential cmake git libcurl4-openssl-dev libncurses-dev
git clone https://github.com/RetroCoreLabs/nd100x.git
cd nd100x && git submodule update --init --recursive
make release
build_release/bin/nd100x --boot=smd
```

## Interactive shell

Load and run BPUN files directly from the command line without the GUI:

```bash
# Quick start: load programs from a directory
./build/bin/nd100x --monitor --nd100-root=/path/to/bpun/files

# In the shell:
@ LIST-FILES *.bpun          # Browse available programs
@ RUN-PROGRAM kernel.bpun    # Load and run
@ SHOW-REGISTERS             # Inspect CPU state
@ EXIT                       # Leave shell
```

**Features:**
- ✅ CLI-driven BPUN loading (no GUI required)
- ✅ Batch automation via scripts (`--script`)
- ✅ INI file configuration (`[runtime]` section)
- ✅ Readline support (history, editing)
- ✅ Works on headless systems and CI/CD

**Documentation:** See **[SHELL_MODULE.md](docs/SHELL_MODULE.md)** for comprehensive guide, API reference, and examples. Quick-start examples at **[SHELL_EXAMPLES.md](SHELL_EXAMPLES.md)**.

## Origins

This project (nd100x) is a fork of nd100em, started in 2025 by Ronny Hansen.
It remains under the GNU General Public License (GPL v2 or later).

Based on nd100em version 0.2.4 by Per-Olof Astrom, Roger Abrahamsson, Zdravko Dimitrov, and Goran Axelsson.
The original project can be found at <https://github.com/tingox/nd100em> and <https://www.ndwiki.org/wiki/ND100_emulator_project>.

## Status

Version 1.0.10

The emulator is under active development.

* Full ND-100/CX instruction set implemented (including BCD opcodes).
* All test programs validate the CPU, Memory Management and Devices.
* Boots SINTRAN L from SMD in 6-7 seconds on a modern machine.

## Improvements

This project continues from nd100em version 0.2.4 and includes significant enhancements:

* **Opcode Improvements**
  * Added support for missing opcodes
  * Fixed bugs in several instructions
* **Memory Management**
  * Fully re-implemented with support for MMS1 and MMS2
  * Proper TRAP generation for Page Faults and Access Violations
* **IO Subsystem**
  * Complete rewrite: now single-threaded and simplified
  * Old IO code has been removed
* **Supported IO Devices**
  * Real-Time Clock (20ms tick emulation pending for full SINTRAN compatibility)
  * Console and additional terminals (up to 11 terminals, with telnet server for remote access)
  * Floppy (PIO and DMA) for 8" and 5.25" formats
  * SMD Hard Disk (75MB, 4 units)
  * Winchester Hard Disk (ST506/8" controller, cards 3041/3038, IOX 500-507, 2 units)
  * SCSI Hard Disk (ND-3201/3204 controller with NCR-5386 chip, Micropolis 1375-ND disks, SCSI IDs 0-6)
  * Paper Tape Reader (buffer-based, BPUN file loading via CLI or Glass UI upload)
  * Paper Tape Punch (output to file and Glass UI hex/ASCII display with download)
  * Line Printer (CDC 9380, output to file and Glass UI window)
  * HDLC (High-Level Data Link Control) networking
* **Codebase Modernization**
  * Removed requirement for libconfig and the supporting .conf file. All options are now given on the command line.
  * Refactored folder structure
  * Automatic function header generation via [mkptypes](https://github.com/OrangeTide/mkptypes) (by Eric R. Smith)

## Project Goals

* Focus on ND-100 CPU and controllers.
  * The newer CPU's (ND-110 and ND-120) is mostly hardware/performance improvements, with some new opcodes to support SINTRAN and COBOL running with better performance.
  * Focus on getting these opcodes into this emulator has very low priority, as SINTRAN doesnt require an ND-110 or ND-120 CPU.
* Refactor, re-structure and modernize code so its easier to extend
* Multiple frontends and build targets
  * Native Linux, Windows and macOS builds
  * WebAssembly (WASM) Glass UI for running the emulator in the browser
  * RISC-V cross-compilation
  * DAP debugger integration for step-by-step debugging
* Planned
  * Ethernet device emulation
  * SCSI tape, CD-ROM and floppy targets (the SCSI hard disk is implemented)

## Project Structure

The project is organized into several key components:

* `src/cpu/` - CPU emulation implementation
* `src/devices/` - Device emulation (I/O, peripherals)
* `src/machine/` - Machine state and main emulation loop
* `src/ndlib/` - Supporting library functions (loading BPUN and a.out formats++)
* `src/debugger/` - DAP Debugger supporting functionality
* `src/frontend/` - User interface and emulator frontend(s)
* `tools/` - Development and build tools
* `images/` - Norsk-Data SMD disk, floppy and BPUN files.
* `build/` - Build output directory

## Build Requirements

- Linux (Ubuntu 22.04 or later) or macOS (12 Monterey or later)
- GCC or Clang compiler
- CMake 3.14+
- Make build system
- mkptypes tool (automatically built during compilation)

### Installing dependencies on Ubuntu/Debian

On a clean Ubuntu install, run:

```bash
sudo apt update
sudo apt install -y build-essential cmake git libcurl4-openssl-dev libncurses-dev
```

What each package provides:

| Package | Purpose |
|---|---|
| `build-essential` | gcc, g++, make |
| `cmake` | CMake 3.14+ build system |
| `git` | needed to clone the repo and fetch submodules |
| `libcurl4-openssl-dev` | downloading floppy database images over HTTP/HTTPS |
| `libncurses-dev` | F12 floppy-database browser and terminal UI |

`libcurl` and `ncurses` are **required** on Linux builds — `find_package(CURL REQUIRED)` and `find_package(Curses REQUIRED)` in `CMakeLists.txt` will fail the configure step if they are missing. They are skipped automatically on RISC-V and WebAssembly builds.

> **Note on package names:** older docs reference `libncurses5-dev`, which is no longer available on Ubuntu 24.04+ — use `libncurses-dev` instead.

On FreeBSD:

```bash
pkg install curl ncurses
```

**Note**: The floppy database browser requires an internet connection to download the catalog at runtime.

## Building the Project

1. Clone the repository:

```bash
git clone https://github.com/RetroCoreLabs/nd100x.git
cd nd100x

git submodule update --init --recursive

```

1. Build the project:

- For debug build (default):

```bash
make debug
```

- For release build:

```bash
make release
```

- For build with sanitizers:

```bash
make sanitize
```

* For more information on the build system, see the [How to build document](docs/HOWTO_BUILD.md).
* Support for WebAssembly builds has been added, for more information see the [How to build WASM document](docs/HOWTO_BUILD_WASM.md)
* Support for Risc-V builds has been added, for more information see the [How to build RISC-V document](docs/HOWTO_BUILD_RISCV.md)

### Building on Windows

A native Windows build is produced via **MinGW-w64** — either [w64devkit](https://github.com/skeeto/w64devkit) (portable, no installer) or MSYS2 MINGW64. The CMake root picks up `_WIN32` automatically and forces the Ninja generator.

**Quick path (w64devkit):**

1. Download and extract [w64devkit](https://github.com/skeeto/w64devkit/releases) anywhere you like, and set `W64DEVKIT` to that folder. (`build.bat` also looks in a couple of usual places, so setting the variable is the reliable way rather than the only way.)
2. From a regular `cmd.exe` at the repo root:

   ```cmd
   build.bat debug
   ```

   That stages `w64devkit\bin` on PATH for the session, runs `make debug`, and leaves `build\bin\nd100x.exe` ready to run. Use `build.bat release` for an optimised build, `build.bat clean` to wipe build directories.

**Current limitations on Windows** (these are gated at build time; Linux is unaffected):

- `--debugger` (DAP server) is unavailable — `external/libdap` uses POSIX-only socket headers and hasn't been ported yet.
- `--boot=aout` is unavailable — `external/libsymbols` needs the same treatment.
- The F12 floppy-database browser is compiled out — it uses ncurses, which w64devkit doesn't ship. Use `--boot=bpun`, `--boot=floppy`, or `--boot=smd` with a local image instead.
- `libcurl` is optional but easy to get on w64devkit: run `make fetch-curl` (or let `make debug` do it automatically), which vendors a prebuilt MinGW libcurl into `external/curl/` and stages `libcurl-x64.dll` next to the exe — no MSYS2 needed. Without it, HTTP image-URL / catalog loads fall back to stubs; local disk images still work. (MSYS2 users can instead install `mingw-w64-x86_64-curl`.)

BPUN, SMD, floppy boot and the telnet server all work natively on Windows.

## Glass Web UI

The emulator includes a glassmorphism browser frontend (the "Glass UI") that runs the full ND-100 emulator in your browser. The live version at **<https://nd100x.hackercorp.no/>** has been upgraded to use this layout.

Build and run locally:

```bash
make wasm-glass-run
```

Features include draggable/resizable floating windows, authentic Norsk Data TDV-2200/TDV-2215 terminal emulation via RetroTerm (with virtual TDV keyboard, VT100 also supported, xterm.js available by config), a full CPU debugger with breakpoints and disassembly, SINTRAN III operating system inspection tools, a hardware page table viewer, Line Printer and Paper Tape device windows, and 5 switchable color themes.

The Glass UI source lives in `template-glass/` (1 HTML, 2 CSS, 35 JS modules, 1 JSON data file). For the full architecture reference, see [GLASS.md](GLASS.md).

## Gateway Server

The gateway (`tools/nd100-gateway/gateway.js`) bridges the browser-based WASM emulator to local resources that a browser cannot access directly: disk images on the local filesystem, TCP terminal clients (PuTTY, telnet), and HDLC serial links.

```
Remote terminal (PuTTY/telnet) --TCP--> Gateway --WebSocket--> WASM Worker
Disk I/O sub-worker            --WebSocket--> Gateway --fs read/write--> local .IMG files
HDLC TCP client                --TCP--> Gateway --WebSocket--> WASM Worker
```

All traffic is multiplexed over a single WebSocket connection using a binary frame protocol (first byte = message type). Disk I/O uses block-level read/write requests — full disk images are never transferred over the wire.

```bash
cd tools/nd100-gateway
npm install
node gateway.js --static ../../build_wasm_glass/bin --verbose
```

Configuration is in `tools/nd100-gateway/gateway.conf.json`: WebSocket port (default 8765), TCP terminal port (default 5001), HDLC channel ports, and paths to SMD/floppy disk images. The `--static` flag serves the Glass UI with the required COOP/COEP headers for SharedArrayBuffer support.

## Updating Git submodules

Sometimes the submodules are updated and you need to manually refresh them

* git submodule update --init --recursive

### Command line options

Every option, with examples, is in **[docs/command-line-options.md](docs/command-line-options.md)**.
It used to sit inline here and ran to over a hundred lines, which pushed everything after it
out of reach. `nd100x --help` prints the same thing.

### Boot types

* `smd`: SMD disk boot (default). An optional unit digit selects the boot unit: `smd0`-`smd3`.
* `wd`: Winchester MASS STORAGE LOAD — 1K words from mass storage address 0
  into core 0 (needs `--wd0`/`--wd1`). An optional unit digit selects the boot
  unit: `wd0`-`wd1`.
* `scsi`: SCSI disk boot. An optional unit digit selects the boot SCSI ID: `scsi0`-`scsi6`. The boot ID must be configured as an `hdd` target with `--scsiN`.
* `bpun`: BPUN paper tape, loaded the way the **ND-100 ROM loader** did.
* `tape`: paper tape loaded the way the **front-panel octal tape load** did (NORD-1 style).
* `cdc`: NORD TSS cartridge-disc boot — the front-panel LOAD button. Reads
  physical sector 0 of the `--cdc=FILE` image into core 0 and starts at
  address 0. (Quirk: argument validation currently still demands an
  `--image=` for this type; pass any file, it is not read.)
* `prog`: SINTRAN `:PROG` loadable image.
* `aout`: BSD 2.11 a.out format.
* `floppy`: Floppy disk boot.
* `bp`: reserved (loader not implemented).

#### `--boot=bpun` vs `--boot=tape` — two different historical loaders, not aliases

Both accept a paper tape that begins with an octal-ASCII leader
(`<addr>/`, one octal word per line, `<addr>!`), but they model **different
hardware load paths** and expect **incompatible content after the `!`**:

| | `--boot=bpun` (ND-100 ROM loader) | `--boot=tape` (front-panel octal load) |
|---|---|---|
| ASCII leader | parsed as **metadata only** — its words are *not* placed in memory | every word is **deposited into memory** at the running location counter |
| after the `!` | a **framed binary block**: `[load address][word count][data words][checksum][action]`, copied into memory by the emulator | **raw, unframed bytes** — the emulator does not touch them |
| start address | computed from the header cells (see the boot-entry contract in the NORD TSS docs) | the octal number immediately before the `!` |
| the tape afterwards | fully consumed | **stays mounted in the paper-tape reader (0400)**, positioned right after the `!`, so the just-started program can read the remainder itself |

Use `--boot=bpun` for every framed BPUN artifact: `mac -b` tapes, MINIT,
TSS system tapes, TPE-MON, test programs.

Use `--boot=tape` for self-loading tapes whose deposited ASCII part IS the
loader — the NORD TSS **CDBIN distribution tape** is the canonical case:
its ASCII part is `HLOAD`, which reads the binary `TBOOT` loader from the
reader, and `TBOOT` then reads load blocks and *writes the disc-tagged ones
to the system disc itself*. That only works if the tape remains in the
reader at the exact byte after the `!`, which is what this boot type
guarantees (exactly like the physical tape staying in the reader).

The two cannot be merged: after the `!` the byte streams are mechanically
indistinguishable, and a wrong guess would silently corrupt memory. As on
the real machines, the operator chooses the loader.

```bash
# framed BPUN artifact (mac -b output, MINIT, system tapes):
nd100x --boot=bpun --image=test.bpun

# self-loading CDBIN install tape against the TSS system disc:
nd100x --boot=tape --image=cdbin-boot.bpun --cdc=cdc.img

# then cold boot TSS from the disc alone (the LOAD button):
nd100x --boot=cdc --image=cdc.img --cdc=cdc.img --drum=drum.img
```

### Block devices (Floppy, SMD and SCSI)

Default image file names (looked up in the current directory):

* Floppy: FLOPPY.IMG
* SMD: SMD0.IMG, SMD1.IMG, SMD2.IMG, SMD3.IMG

SMD images can be overridden per unit with `--smd0=FILE` through `--smd3=FILE`.
Other floppy images can be mounted at runtime via the F12 menu.

The SCSI controller (ND-3201/3204) is opt-in: it is only added to the machine
when at least one `--scsi0` through `--scsi6` target is given, so existing SMD
machine configurations are unaffected. Both controllers can be enabled at the
same time with their own disks, and the `--boot` selector picks which one to
boot from.

## Running SINTRAN in the Emulator

The emulator requires a system image file (SMD0.IMG) to run. Place the image file in the project root directory.

To boot a SINTRAN image from an SMD disk

```bash
build/bin/nd100x --boot=smd 
```

Read more about [how to boot sintran](SINTRAN.md)

![Boot Animation](images/boot.gif)

## Booting TPE-MON from floppy and running test programs

The emulator requires a floppy image to boot from. Place the image file in the current directory.

```bash
cp images/Nd-210523I01-XX-01D.img FLOPPY.IMG
build/bin/nd100x --boot=floppy
```

Now you have access to test programs like CONFIG, PAGING, INSTRUCTION and more.

## I/O Devices

### Paper Tape Reader (I/O 0400-0403)
Reads BPUN tape images loaded via the `--tape` CLI option or the Glass UI file upload. Used by TPE and SINTRAN as logical device 2.

`--boot=tape` also mounts here: after the octal-ASCII leader is deposited
and started, the **remainder of the boot tape stays in this reader**,
positioned right after the `!`, so the loaded program can read the rest of
its own tape — see "Boot types" above.

### Paper Tape Punch (I/O 0410-0413)
Accumulates punched output in memory. In native mode, output is saved to files in the tape directory (default: `./tapes/`). In the Glass UI, a hex/ASCII display shows punched bytes with a download button.

### Line Printer (I/O 0430-0433)
CDC 9380 line printer emulation. In native mode, output is saved to files in the print directory (default: `./prints/`). In the Glass UI, a dedicated window shows printer output in real-time.

### HDLC Controller
COM 5025-based HDLC (High-Level Data Link Control) communication controller. Up to 4 HDLC devices can be configured, each operating as either a TCP server or client for point-to-point serial links. Supports DMA transfers, CRC calculation, and full interrupt handling. Enable with `--hdlc=N:PORT` (server) or `--hdlc=N:HOST:PORT` (client). Live status monitoring available via the F12 menu.

### Virtual Screen Switching (Native)
Press **Alt+1** through **Alt+9** to switch directly between virtual screens (Console, terminals, Line Printer, Paper Tape Punch, Log). Press **F12** for the unified menu offering Floppy Database Browser, Virtual Screen Selector, HDLC Status, CPU Speed, Character Set, Pending Connections viewer, and About screen. Only terminal screens accept keyboard input; device screens are output-only.

### National Character Set (Native)
The emulated terminal speaks 7-bit ISO 646, where national variants reuse the ASCII positions `[ \ ] { | }` for accented letters. Select a variant with `--charset=` (off, norwegian, swedish, german) or at runtime via **F12 > Character Set**, which also shows the exact byte mappings. When active, the local console renders those positions as the national letters (e.g. Norwegian `{ | }` -> `æ ø å`, `[ \ ]` -> `Æ Ø Å`) and maps national keystrokes (UTF-8 or Latin-1) back to the matching 7-bit code. This is a local-console presentation layer only — telnet/TCP traffic is always passed through as raw 7-bit and is never translated.

### Telnet Server (Native)
Enable with `--telnet[=PORT]` (default port 9000). Provides remote terminal access to terminals 8-11. Multiple clients can connect simultaneously and select from available terminals. Features include:
* Non-blocking accept with concurrent pending client handling
* 60-second auto-disconnect for idle pending connections
* Live monitoring of pending connections with rx/tx byte stats (via F12 menu)
* Per-terminal rx/tx byte counters visible in the Virtual Screen Selector
* Press Enter to auto-connect to the first available terminal
* Race-safe selection: if two clients pick the same terminal, the second gets a "busy" message and a refreshed list instead of a silent redirect
* Immediate disconnect with message when no terminals are free

## Floppy Menu

The emulator includes a built-in floppy disk browser that allows you to browse and mount floppy disk images from the ND100 floppy database.

### Accessing the Menu

While the emulator is running, press **F12** to open the floppy menu.

![Menu Animation](images/menu.gif)

### Menu Features

* **Browse Database**: View all available floppy disk images from the online database
* **Search Functionality**: Search for floppies by name, reference, or directory content
* **Detailed Information**: View detailed information about each floppy including description, reference and directory content
* **Mount Capability**: Mount selected floppy disks to floppy drive units 0, 1 or 2

### Requirements

* Internet connection (required to download floppy database).
* Terminal that supports F12 key

Floppy database is available directly at <https://ndlib.hackercorp.no/>

## Automation & the floppy catalog (`--pipe`)

For scripted runs — automated installs, regression tests, CI — start the emulator with `--pipe` and drive it from Python with the pexpect-style helper `tools/nd100x_expect.py`:

```python
from nd100x_expect import Nd100x

with Nd100x(boot="floppy", image="TPE.img", extra_args=["--mms1"]) as vm:
    vm.expect("TPE>", timeout=60)
    vm.send("PAGING-C02\r"); vm.expect("Test number", timeout=30)
    vm.send("2\r")
    vm.expect("End of test", abort=["ABORTED"])
```

The driver also reaches the **online floppy catalog** (the same database the F12 menu uses), so a script can search it, download an image by md5 or SINTRAN directory name, and hot-swap it into the running machine — e.g. to feed successive install floppies:

```python
vm.mount_catalog(0, md5="2cb9ffb6b37d367bc9d0426cb56fdfda")   # download + mount on unit 0
hits = vm.catalog_matches(directory="N-10-102-I")            # md5 is unique; a dir name may repeat
```

Local files can be hot-swapped too (`vm.mount(unit, path)` / `vm.eject(unit)`), and the C side exposes the same catalog over the `--pipe` control channel (`dbmount` / `dblist`). Full guide and runnable examples: **`docs/pipe-automation.md`** and **`tools/examples/`** (`mount_catalog_demo.py`, `tpe_instruction_nd120.py`).

## Assembling your own programs

Using the assembly tool from Ragge [nd100-as](https://github.com/ragge0/norsk_data/tree/main/nd100-as) you can compile ND assembly to a.out format and load into the emulator.

This assembler is following the AT&T syntax so it differs a bit from the ND-100 MAC assembler.

```asm
lda foo
sta bar
opcom


bar: .word 11
foo: .word 22
```

Remember to end your code with 'opcom' to make the emulator stop executing your code.

Overview of all [assembly instructions](docs/cpu_documentation.md)

## Known unfinished work

Every `TODO` and `FIXME` left in the source without an owner is catalogued in
the [TODO and FIXME inventory](docs/TODO-INVENTORY.md) - 51 of them across 16
files, with the file, line, enclosing function and the comment itself, so each
can be judged on its own.

## Multiple systems

The nd100x emulator has been compuiled and tested on multiple different systems.
For more information, read the [Tested systems](docs/SYSTEMS.md) document

## About the ND-100

The ND-100 was a 16-bit minicomputer from **Norsk Data**, Norway's computer manufacturer, first
shipped in 1975 and sold into the early 1990s. It ran **SINTRAN III**, Norsk Data's own
operating system, and it ran a great deal of Scandinavia: universities, hospitals, banks,
shipping, the oil industry and the military. CERN bought them by the dozen. At its height Norsk
Data was the second largest company in Norway.

The architecture is worth a look on its own terms. It is a 16-bit machine with 48-bit floating
point, memory management that can be either MMS1 (four page tables, as on the NORD-10) or MMS2
(sixteen), and an instruction set compact enough that the whole CPU fits in a microcoded
processor you can read. Programs arrive as **BPUN** paper tapes, and the front panel's LOAD
button reads sector zero off a disc - both of which this emulator still implements, because
that is how the surviving software boots.

A fair number of these machines still exist and still work, and people still run them. What
does not survive is the software distribution: the tapes, the discs and the manuals are
scattered. So an emulator is how most of it gets run now, and why this one tries to be exact
about the hardware rather than approximately right.

The company is gone - it broke up in the early 1990s - but the documentation, the microcode and
the source code have been recovered in large part, and that is what the emulator is built from.
For more: <https://www.ndwiki.org/wiki/ND-100>

## Releases (GitHub Actions)

Pre-built binaries for Linux, Windows, macOS, and RISC-V (Milk-V Duo) are produced automatically by `.github/workflows/build-release.yml`. The workflow runs on every push to `main` and on pull requests as a CI check, and publishes a GitHub Release whenever a tag matching `v*` is pushed.

| Artifact | Target | How it's built |
|----------|--------|----------------|
| `nd100x-linux-x64.tar.gz` | Linux x86_64 (glibc 2.35+) | `ubuntu-22.04`, apt-installed `build-essential cmake libcurl4-openssl-dev libncurses-dev`, `make release` |
| `nd100x-linux-arm64.tar.gz` | Linux aarch64 (glibc 2.35+) | `ubuntu-22.04-arm` (GitHub-hosted ARM runner), same toolchain as x64 |
| `nd100x-linux-riscv64.tar.gz` | RISC-V 64 Linux musl (Milk-V Duo, CV1800B/C906) | `ubuntu-22.04` cross-compile using Milk-V's `host-tools` toolchain (`riscv64-unknown-linux-musl-gcc`, `-march=rv64gc -mabi=lp64d`); smoke-tested with `qemu-user-static` |
| `nd100x-windows-x64.zip` | Windows 64-bit | `windows-latest`, MSYS2 MINGW64 (`mingw-w64-x86_64-gcc` + `ninja`), `make release` |
| `nd100x-macos-arm64.tar.gz` | macOS Apple Silicon | `macos-latest`, Homebrew cmake, `make release` |
| `SHA256SUMS.txt` | All of the above | `sha256sum` over every release artifact — verify with `sha256sum -c SHA256SUMS.txt` |

The Windows zip contains `nd100x.exe` plus every non-system DLL the exe depends on (libcurl + its transitive deps, `libgcc_s_seh-1`, `libwinpthread-1`, ...), the repo's bundled `images/` directory, `README.md`, and `LICENSE`. DLL discovery is driven by `ldd` so the bundle tracks whatever MSYS2 ships — no hardcoded list to maintain.

The Linux and macOS tarballs bundle just the `nd100x` binary plus `images/`, `README.md`, and `LICENSE`. They link dynamically against `libcurl` and `libncurses`; install those at runtime via your distro's package manager (see [Installing dependencies on Ubuntu/Debian](#installing-dependencies-on-ubuntudebian)).

### Cutting a release

Tag-driven, no manual button pressing:

```bash
git tag v1.0.10
git push origin v1.0.10
```

The tag push fans out to all build jobs in parallel (Linux x64/arm64, Windows x64, macOS arm64, and RISC-V Milk-V Duo). Once they finish, the `release` job downloads every artifact, repackages each one in its native format (`.zip` for Windows, `.tar.gz` for Linux/macOS/RISC-V), generates `SHA256SUMS.txt`, and publishes a GitHub Release with auto-generated release notes (commit log since the previous tag, contributor list, "What's Changed" links).

If a build job fails the release is **not** published — `needs: [build-linux, build-windows, build-macos, build-riscv]` blocks the publish step until every platform succeeds, and `fail_on_unmatched_files` aborts if any expected artifact is missing.

### Manual dispatch and CI runs

* **Push to `main`** — runs every build job as a CI check; no release is created.
* **Pull request to `main`** — same, plus `concurrency.cancel-in-progress: true` cancels superseded PR builds to save runner minutes.
* **`Actions → Build & Release → Run workflow`** (`workflow_dispatch`) — manual dry build, no release published.

## TODO

* OPCOM implementation
  * Emulation of OPCOM for memory inspection when CPU is in STOP mode.
  * Currently STOP mode exits the emulator
* Clean up code and standardise on 8,16,32 and 64 bit signed and unsigned names for types

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Documentation follows the house standards: run
`python tools/check-docs.py` before opening a pull request.

## License

GNU General Public License, version 2 or later - inherited from nd100em, see
[Origins](#origins) above. See the [LICENSE](LICENSE) file for the full text.
