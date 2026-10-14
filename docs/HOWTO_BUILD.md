# Building nd100x

One document for every platform. Pick your operating system in the table of
contents, follow the numbered steps, and you end up with a runnable `nd100x`
binary. The common part (clone, submodules, `make` targets, checking the
result) is described once at the top and referenced from each platform
section.

Contents

1. [What the build needs](#1-what-the-build-needs)
2. [Common steps on every platform](#2-common-steps-on-every-platform)
3. [Linux](#3-linux)
4. [macOS](#4-macos)
5. [FreeBSD, OpenBSD, NetBSD](#5-freebsd-openbsd-netbsd)
6. [Windows](#6-windows)
7. [WebAssembly (browser)](#7-webassembly-browser)
8. [RISC-V Linux (Milk-V Duo)](#8-risc-v-linux-milk-v-duo)
9. [Build options and other targets](#9-build-options-and-other-targets)
10. [Checking the result and troubleshooting](#10-checking-the-result-and-troubleshooting)

---

## 1. What the build needs

The emulator is plain C, built with CMake behind a Makefile wrapper. Beyond
the C toolchain it needs two things from the operating system:

| Need | Used for | Linux / BSD / macOS | Windows |
|---|---|---|---|
| HTTP/HTTPS client | downloading the floppy-database catalog and images (`src/ndlib/download.c`) | libcurl from the OS or distro | WinHTTP, part of Windows, nothing to install |
| ncurses | the F12 floppy-database browser (`src/frontend/nd100x/menu.c`) | ncurses from the OS or distro | not used, the browser is compiled out on Windows |

Everything else (cJSON, Musashi, nd500x, RetroTermWeb, libdap, libsymbols,
mkptypes) is a git submodule or is built from the tree. The exact package
names per OS are in each platform section.

Minimum tool versions: CMake 3.14, a C11 compiler (GCC or Clang), GNU make,
git. Ninja is required on Windows and optional elsewhere.

---

## 2. Common steps on every platform

These steps are the same everywhere. Each platform section below tells you
only what differs (which packages to install, which shell to use).

1. Clone the repository and fetch the submodules:

   ```bash
   git clone https://github.com/RetroCoreLabs/nd100x.git
   cd nd100x
   git submodule update --init --recursive
   ```

   `make submodules` does the same submodule step later if a pull adds one.
   `--recursive` also descends into `external/nd500x`, whose own submodules
   include `ndmonlib`, a repository that is not public; nd100x does not use
   it (it configures nd500x with `ND500X_WITH_NDMON=OFF`). If the recursive
   step fails on `ndmonlib`, initialise only what nd100x needs, which is what
   the release workflow does:

   ```bash
   git submodule update --init external/cJSON external/libdap external/libsymbols \
       external/Musashi external/nd500x template-glass/external/RetroTermWeb
   ```

2. Build. The Makefile wraps CMake and puts each configuration in its own
   directory:

   | Command | Build directory | What you get |
   |---|---|---|
   | `make debug` (same as `make`) | `build/` | debug symbols, no optimisation |
   | `make release` | `build_release/` | optimised binary |
   | `make sanitize` | `build_sanitize/` | AddressSanitizer build for memory bugs |
   | `make test` | `build/` | debug build plus the unit tests under CTest |
   | `make clean` | all of the above | removes the build directories |

   The binary is `build/bin/nd100x` (or `build_release/bin/nd100x`,
   `nd100x.exe` on Windows).

3. Get a disk to boot. The emulator looks for `SMD0.IMG` and `FLOPPY.IMG`
   in the directory it is started from (both names are ignored by git, they
   are your working copies). The repository ships a packed SINTRAN system
   disk and a set of floppy images under `images/`:

   ```bash
   bunzip2 -kc images/SMD0.IMG.bz2 > SMD0.IMG      # SINTRAN III system disk
   cp images/Nd-210523I01-XX-01D.img FLOPPY.IMG    # one of the floppy images
   ```

4. Run it:

   ```bash
   build/bin/nd100x --boot=smd --smd0=SMD0.IMG
   build/bin/nd100x --boot=floppy --image=FLOPPY.IMG
   build/bin/nd100x --help
   ```

5. Check that the binary you run is the one you built:

   ```bash
   build/bin/nd100x --version
   ```

   This prints the version, the git hash (with `-dirty` if the tree had
   uncommitted tracked changes) and the build time. More than one build
   directory can exist at once, so check this first whenever behaviour does
   not match the source.

`make help` lists every target, including the run helpers (`make run`,
`make boot-smd`, `make boot-floppy`, `make boot-wd`) and their environment
variables.

---

## 3. Linux

Tested on Ubuntu 22.04 and 24.04; the release binaries are built on
Ubuntu 22.04 (x64 and arm64).

1. Install the toolchain and the two libraries:

   | Distribution | Build packages | Runtime packages (for a downloaded release binary) |
   |---|---|---|
   | Ubuntu 24.04+, Debian 13 | `sudo apt install -y build-essential cmake git pkg-config libcurl4-openssl-dev libncurses-dev` | `sudo apt install libcurl4t64 libncurses6` |
   | Ubuntu 22.04, Debian 12 | same as above | `sudo apt install libcurl4 libncurses6` |
   | Fedora, RHEL | `sudo dnf install gcc cmake git pkgconf libcurl-devel ncurses-devel` | `sudo dnf install libcurl ncurses-libs` |
   | Arch | `sudo pacman -S base-devel cmake git curl ncurses` | `sudo pacman -S curl ncurses` |
   | Alpine | `apk add build-base cmake git pkgconf curl-dev ncurses-dev` | `apk add libcurl ncurses-libs` |

   Every name in the table was checked against the package index of its
   distribution (packages.debian.org for Debian 12 and 13, archlinux.org for
   Arch, Ubuntu 24.04, Fedora and Alpine as well) on 14-OCT-2026. On Debian 13
   `pkg-config` is a small package that only pulls in `pkgconf`; installing
   either works. Older documents mention `libncurses5-dev`; that package no
   longer exists on Ubuntu 24.04, use `libncurses-dev`.

   `libcurl` and `ncurses` are required: `find_package(CURL REQUIRED)` and
   `find_package(Curses REQUIRED)` in `CMakeLists.txt` stop the configure
   step if they are missing.

2. Follow [the common steps](#2-common-steps-on-every-platform).

What you get: a dynamically linked `nd100x` that uses the distro's libcurl
and ncurses. The DAP debugger (`--debugger`) and a.out loading are included.

Optional: `make runv` runs the debug build under valgrind.

---

## 4. macOS

Tested on macOS 12 Monterey and later; the release binary is built on
`macos-latest` (Apple Silicon).

1. Install the Xcode command line tools and CMake:

   ```bash
   xcode-select --install
   brew install cmake
   ```

   libcurl and ncurses are part of macOS, nothing else is needed. Apple's
   libcurl verifies HTTPS against the Keychain.

2. Follow [the common steps](#2-common-steps-on-every-platform).

---

## 5. FreeBSD, OpenBSD, NetBSD

The BSDs are built the same way as Linux. They are not CI targets, so this
is what the project expects rather than what it tests on every push.

1. Install the packages:

   | System | Build packages | Runtime packages |
   |---|---|---|
   | FreeBSD | `pkg install cmake git pkgconf curl ncurses` | `pkg install curl ncurses` |
   | OpenBSD | `pkg_add cmake git curl` (ncurses is in the base system) | `pkg_add curl` |
   | NetBSD | `pkgin install cmake git curl` (ncurses is in the base system) | `pkgin install curl` |

   Use `gmake` where `make` is BSD make.

2. Follow [the common steps](#2-common-steps-on-every-platform).

---

## 6. Windows

A native 64-bit `nd100x.exe` is built with a MinGW-w64 GCC. Two toolchains
work. w64devkit is the recommended one for a local build; MSYS2 is what the
release workflow uses.

The Windows exe needs nothing installed to run: downloads go through WinHTTP
(a Windows system DLL), TLS is validated against the Windows certificate
store, and the MinGW runtime is linked statically. The release zip is the exe
alone. See the troubleshooting section for how to confirm that on your own
build.

### 6.1 Option 1: w64devkit (recommended for local builds)

[w64devkit](https://github.com/skeeto/w64devkit) is a portable MinGW-w64
toolchain: GCC, GNU make, CMake, Ninja, GDB, BusyBox (a `sh` and the usual
unix commands), pkg-config. It needs no installer, no admin rights and makes
no system changes. It is an MSVCRT toolchain. The x64 kit runs on Windows 7
and later.

**Minimum version: w64devkit 2.7.0** (14 April 2026). It is the first
release that bundles mingw-w64 14.0.0, which added the `mkdtemp()` function
that four of the unit tests use. With an older kit `make release` stops with
`implicit declaration of function 'mkdtemp'` in `tests/test_shell.c`.
w64devkit 2.10.0 (GCC 16.2.0, mingw-w64 14.0.0) is the version this document
was verified with.

1. Download the x64 kit from the
   [releases page](https://github.com/skeeto/w64devkit/releases):
   the file is `w64devkit-x64-<version>.7z.exe`, about 65 MB. It is a
   self-extracting 7-Zip archive; run it (or open it with 7-Zip) and it
   unpacks a single folder named `w64devkit`.

2. Put that folder where you keep tools, for example a `Utils` folder on your
   system drive. Nothing is registered anywhere; to uninstall, delete the
   folder. To upgrade later, extract the new version next to the old one and
   rename.

3. Tell `build.bat` where the kit is. Either set the environment variable
   `W64DEVKIT` to the folder that contains `bin\gcc.exe`:

   ```cmd
   set W64DEVKIT=D:\tools\w64devkit
   ```

   or put the kit at the default location named in the header comment of
   `build.bat` at the repository root. The script checks that `bin\gcc.exe`
   exists there and prints what to do if it does not.

4. Get the source. Use Git for Windows or the `git` inside the kit's shell:

   ```cmd
   git clone https://github.com/RetroCoreLabs/nd100x.git
   cd nd100x
   git submodule update --init --recursive
   ```

5. Build. Two equivalent ways:

   **a) From a normal `cmd.exe` prompt** at the repository root:

   ```cmd
   build.bat            (debug build, same as build.bat debug)
   build.bat release    (optimised build)
   build.bat clean      (delete build\ and build_release\)
   ```

   `build.bat` prepends `w64devkit\bin` to PATH for the duration of the
   build, runs `make debug` or `make release`, and prints where the exe is.
   It does not change your PATH permanently.

   **b) From the w64devkit shell.** Run `w64devkit.exe` in the kit folder.
   It opens a console with GCC, make, CMake and Ninja on PATH; `sh -l`
   inside it starts a unix shell. Change to the repository folder and build
   exactly like on Linux:

   ```sh
   make debug
   make release
   make test              # builds and runs the unit tests under CTest
   ```

6. Run (after unpacking a disk as in step 3 of the common steps; on
   Windows, 7-Zip opens `images\SMD0.IMG.bz2`):

   ```cmd
   build\bin\nd100x.exe --boot=floppy --image=FLOPPY.IMG
   build_release\bin\nd100x.exe --boot=smd --smd0=SMD0.IMG
   ```

What is different on Windows, all gated at build time:

- `--debugger` (the DAP server) is unavailable: `external/libdap` uses
  POSIX-only socket headers and is not ported yet.
- `--boot=aout` is unavailable: `external/libsymbols` needs the same port.
- The F12 floppy-database browser is compiled out (it needs ncurses). The
  catalog itself still works through the `--pipe` control channel, see
  `docs/pipe-automation.md`.
- BPUN, SMD, Winchester and floppy boot, the telnet server, the Ethernet II
  card (`pcap:` with Npcap, `docs/ETHERNET.md`) and catalog downloads all
  work.

### 6.2 Option 2: MSYS2 MINGW64 (what the release workflow uses)

1. Install [MSYS2](https://www.msys2.org/) and open the **MINGW64** shell
   (not the MSYS or UCRT64 one; the release zip is built with MINGW64).

2. Install the toolchain:

   ```sh
   pacman -S --needed base-devel git mingw-w64-x86_64-gcc \
       mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-pkgconf
   ```

   No curl package is needed; Windows builds use WinHTTP.

3. Clone, init submodules and run `make release` as in
   [the common steps](#2-common-steps-on-every-platform). The exe is
   `build_release/bin/nd100x.exe`.

This is the exact recipe of the `build-windows` job in
`.github/workflows/build-release.yml`; read that job when in doubt.

---

## 7. WebAssembly (browser)

The emulator also builds to WebAssembly and runs in a browser with an
xterm.js terminal. Two front ends exist: the plain template (`template/`)
and the glassmorphism UI with the built-in debugger (`template-glass/`).

1. Install CMake (see your platform section) and the Emscripten SDK.
   Install emsdk in its own folder, not inside the nd100x tree:

   ```bash
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   ./emsdk install latest
   ./emsdk activate latest
   source ./emsdk_env.sh        # Linux/macOS; on Windows: emsdk_env.bat
   ```

   This puts `emcc` and `emcmake` on PATH for the current shell. Re-run the
   `source` line in every new shell, or add it to your profile.

2. Build one of the two front ends from the nd100x tree:

   | Command | Output | Front end |
   |---|---|---|
   | `make wasm` | `build_wasm/bin/` | plain template, no debugger |
   | `make wasm-glass` | `build_wasm_glass/bin/` | glass UI, debugger on, SINTRAN inspectors |
   | `make wasm-run`, `make wasm-glass-run` | same | build, then serve on `http://localhost:8000` (needs Node.js, uses `tools/serve-coop.mjs`) |

   Direct CMake equivalent of `make wasm`:

   ```bash
   emcmake cmake -B build_wasm -S . -DBUILD_WASM=ON
   cmake --build build_wasm
   ```

3. Give the browser build a disk. The WASM build uses Emscripten's in-memory
   file system (MEMFS). Copy `SMD0.IMG` and/or `FLOPPY.IMG` (see step 3 of
   the common steps) into the output `bin/` folder; the glass UI can also
   load images from the browser.

4. Serve the output folder over HTTP (browsers refuse to load `.wasm` from
   `file://`) and open it:

   ```bash
   cd build_wasm/bin
   python3 -m http.server
   ```

   Then open `http://localhost:8000`. The plain template opens the console
   terminal plus four more terminals; run `@SET-AVAILABLE` in SINTRAN to
   enable login on the extra terminals.

The glass UI, its terminal gateway (`make gateway`, `make wasm-glass-gateway`)
and the browser debugger are documented in `GLASS.md` at the repository root
and in `docs/GATEWAY-PROTOCOL.md`.

---

## 8. RISC-V Linux (Milk-V Duo)

A static, musl-linked RISC-V 64 build for tiny Linux boards. Tested on the
Milk-V Duo (CV1800B, T-Head C906, 64 MB RAM). The debugger is compiled out
and the floppy-database download is a stub on this target.

1. Get the Milk-V `host-tools` toolchain. It must be the musl toolchain
   from Milk-V; the distro packages `gcc-riscv64-linux-gnu` and
   `gcc-riscv64-unknown-elf` compile the code but produce binaries the Duo's
   musl userspace cannot run.

   ```bash
   mkdir -p "$HOME/milkv" && cd "$HOME/milkv"
   wget https://sophon-file.sophon.cn/sophon-prod-s3/drive/23/03/07/16/host-tools.tar.gz
   tar xf host-tools.tar.gz
   ```

   The compiler is then at
   `$HOME/milkv/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-gcc`.
   The source of the tarball is https://github.com/milkv-duo/host-tools.

2. Point the build at it. `riscv64-toolchain.cmake` reads the environment
   variable `MILKV_HOST_TOOLS` and falls back to `$HOME/milkv/host-tools`,
   so with the layout above nothing needs to be set. Otherwise:

   ```bash
   export MILKV_HOST_TOOLS=/path/to/host-tools
   ```

3. Build:

   ```bash
   make riscv
   ```

   Output: `build_riscv/bin/nd100x`, built with `-march=rv64gc -mabi=lp64d`
   and static linking. `RISCV_BUILD_TYPE` selects the CMake build type (the
   Makefile prints the one in use). Direct CMake equivalent:

   ```bash
   cmake -B build_riscv -S . -DBUILD_RISCV=ON \
         -DCMAKE_TOOLCHAIN_FILE=riscv64-toolchain.cmake \
         -DCMAKE_BUILD_TYPE=Release
   cmake --build build_riscv
   ```

4. Deploy to the board (the Duo's USB network address is `192.168.42.1` by
   default):

   ```bash
   ssh root@192.168.42.1 mkdir -p nd
   scp build_riscv/bin/nd100x images/SMD0.IMG images/FLOPPY.IMG root@192.168.42.1:/root/nd
   ```

   Later updates of the executable alone:

   ```bash
   scp build_riscv/bin/nd100x root@192.168.42.1:/root/nd
   ```

Memory on the Duo: with 64 MB of RAM the emulator was killed within seconds
when configured with 16 MB of emulated memory; 8 MB works. If it dies,
`dmesg | tail -n 50` on the board shows the out-of-memory killer at work:

```
Out of memory: Killed process 330 (nd100x) total-vm:18480kB, anon-rss:10492kB ...
oom_reaper: reaped process 330 (nd100x), now anon-rss:0kB ...
```

Milk-V documentation: https://milkv.io/docs/duo/getting-started and
https://github.com/milkv-duo.

---

## 9. Build options and other targets

Environment variables understood by the Makefile (see `make help`):

| Variable | Effect |
|---|---|
| `DEBUGGER_ENABLED=ON\|OFF` | include the DAP debugger (default ON on Linux/macOS, OFF on WASM, not available on Windows yet) |
| `BOOT_TYPE`, `IMAGE_FILE`, `START_ADDR`, `VERBOSE=1`, `DEBUGGER=1`, `DISASM=1` | options for `make run` |
| `SMD0_IMAGE`, `WD0_IMAGE`, `FLOPPY_IMAGE` | images for `make boot-smd`, `make boot-wd`, `make boot-floppy` |
| `MILKV_HOST_TOOLS`, `RISCV_BUILD_TYPE` | RISC-V toolchain root and build type |

Other targets: `make dap-tools` builds the libdap test tools
(`dap_debugger`, `dap_mock_server`; Linux only), `make retroterm-build`
builds the RetroTermWeb submodule, the `gateway*` targets run the terminal
gateway server for the glass UI.

Direct CMake, for people who do not want the Makefile:

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug      # or Release
cmake --build build
cmake --install build --prefix /path/to/install    # optional
```

Useful cache options: `-DDEBUGGER_ENABLED=OFF`, `-DBUILD_DAP_TOOLS=ON`,
`-DBUILD_WASM=ON` (with `emcmake`), `-DBUILD_RISCV=ON` (with the toolchain
file). `./build.sh` at the repository root wraps the same calls
(`--release`, `--wasm`, `--with-dap-tools`, `--build-dir`,
`--install-prefix`; `./build.sh --help`).

Generated files: `*_protos.h` headers are produced by the `mkptypes` tool
during the build and `src/frontend/nd100x/nd100x_version.h` by
`cmake/git_stamp.cmake`. Never create or edit them by hand; a hand-made copy
makes CMake skip regeneration.

---

## 10. Checking the result and troubleshooting

- **Which binary am I running?** `nd100x --version` prints version, git
  hash (`-dirty` if the tree had uncommitted changes) and build time. Stale
  binaries in a second build directory have caused phantom bugs more than
  once; check this before debugging.

- **Windows: does the exe really need no DLLs?** In the kit shell or MSYS2:

  ```sh
  objdump -p build_release/bin/nd100x.exe | grep "DLL Name"
  ```

  Expected: `KERNEL32.dll`, `WINHTTP.dll`, `WINMM.dll`, `WS2_32.dll`,
  `msvcrt.dll` (MSYS2 builds may list the `api-ms-win-crt-*` set instead of
  `msvcrt.dll`). If `libwinpthread-1.dll`, `libgcc_s_seh-1.dll` or any
  `libcurl*.dll` appears, the build did not pick up the `-static` link
  option in `src/frontend/nd100x/CMakeLists.txt`. The release workflow
  fails on exactly this check.

- **Windows: `implicit declaration of function 'mkdtemp'`** in
  `tests/test_shell.c` or `tests/test_mfbus_bridge.c`: the w64devkit is
  older than 2.7.0 (mingw-w64 13 or earlier). Upgrade the kit; see 6.1.

- **Windows: `w64devkit not found at ...`** from `build.bat`: set
  `W64DEVKIT` to the folder that contains `bin\gcc.exe`, or move the kit to
  the default location printed in the message.

- **Linux: configure stops at `find_package(CURL REQUIRED)` or
  `find_package(Curses REQUIRED)`**: install the `-dev` packages from
  section 3.

- **CMake skipped a `*_protos.h` regeneration**: delete the hand-created or
  touched header; generation is timestamp-gated.

- **Clean parallel build fails but incremental works**: a target that
  includes a `*_protos.h` (directly or through `machine_types.h`) is missing
  its `add_dependencies(<target> <module>_protos_gen)` line. The CI build is
  a clean parallel build and finds these.
