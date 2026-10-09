# ND100X Makefile - CMake wrapper
# This Makefile provides backward compatibility by calling CMake

# Build directories
BUILD_DIR = build
BUILD_DIR_DEBUG = build
BUILD_DIR_RELEASE = build_release
BUILD_DIR_SANITIZE = build_sanitize
BUILD_DIR_WASM = build_wasm
BUILD_DIR_WASM_GLASS = build_wasm_glass
BUILD_DIR_RISCV = build_riscv

# Commands with full paths
CMAKE = cmake
EMCMAKE = emcmake
NODE = /usr/bin/node

# By default, enable debugger in Linux/Windows builds
DEBUGGER_ENABLED ?= ON

# Host-OS detection — Windows via MSYS/MinGW/w64devkit vs. Unix-likes.
UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
    HOST_WINDOWS := 1
endif
ifeq ($(findstring MSYS,$(UNAME_S)),MSYS)
    HOST_WINDOWS := 1
endif
ifeq ($(OS),Windows_NT)
    HOST_WINDOWS := 1
endif

# macOS sed requires an empty-string backup argument for in-place edits.
ifeq ($(UNAME_S),Darwin)
    SED_INPLACE := sed -i ''
else
    SED_INPLACE := sed -i
endif

# CMake generator selection.
# On Windows (w64devkit / MSYS2) force Ninja — otherwise CMake defaults to
# the Visual Studio generator, which needs MSVC and breaks under MinGW.
# Elsewhere, let CMake pick its default (Unix Makefiles).
ifdef HOST_WINDOWS
    CMAKE_GENERATOR_FLAG := -G Ninja
else
    CMAKE_GENERATOR_FLAG :=
endif

# Parallel build: nproc is POSIX-only. Fall back to NUMBER_OF_PROCESSORS on
# Windows, and a safe default of 4 if neither is available.
ifdef HOST_WINDOWS
    JOBS := $(or $(NUMBER_OF_PROCESSORS),4)
else
    JOBS := $(shell nproc 2>/dev/null || echo 4)
endif

# Vendored libcurl (Windows only). On a Windows host, native builds depend on
# fetch-curl so external/curl (a prebuilt MinGW libcurl) is present before CMake
# configures — mirrors how the tiki100 project vendors SDL2. Elsewhere the build
# uses a system libcurl, so this is empty.
ifdef HOST_WINDOWS
    CURL_PREREQ := fetch-curl
else
    CURL_PREREQ :=
endif

# Default target is debug
.PHONY: all
all: debug

# Fetch the vendored MinGW libcurl into external/curl if it is not already there.
.PHONY: fetch-curl
fetch-curl:
	@if [ ! -d external/curl ]; then \
		echo "external/curl not found — fetching vendored MinGW libcurl..."; \
		sh scripts/fetch-curl.sh; \
	fi

# Stage the libcurl runtime DLL + CA bundle next to a freshly built .exe (Windows
# only; a no-op if the vendored copy is absent). $(1) = build dir.
define stage_curl_runtime
	@if [ -f external/curl/bin/libcurl-x64.dll ]; then \
		cp -f external/curl/bin/libcurl-x64.dll   $(1)/bin/ 2>/dev/null || true; \
		cp -f external/curl/bin/curl-ca-bundle.crt $(1)/bin/ 2>/dev/null || true; \
		echo "Staged libcurl-x64.dll + curl-ca-bundle.crt into $(1)/bin"; \
	fi
endef

# Check for required dependencies
.PHONY: check-deps
check-deps:
	@echo "Checking dependencies..."
	@command -v cmake >/dev/null 2>&1 || { echo "Error: cmake not found. Please install cmake."; exit 1; }
	@command -v gcc >/dev/null 2>&1 || { echo "Error: gcc not found. Please install gcc."; exit 1; }
ifdef HOST_WINDOWS
	@command -v ninja >/dev/null 2>&1 || { echo "Error: ninja not found on PATH. w64devkit and MSYS2 ship it — make sure the toolchain bin dir is on PATH."; exit 1; }
endif
	@echo "All core dependencies found."

# RISC-V toolchain location. Override via env: MILKV_HOST_TOOLS=/path make riscv
#
# The default is derived from $(HOME) rather than written out: a checked-in
# absolute path only works on the machine it was written on, and this one named
# a specific developer's home directory. check-riscv-deps below says what to set
# when the toolchain is somewhere else.
MILKV_HOST_TOOLS ?= $(HOME)/milkv/host-tools
MILKV_RISCV_BIN  := $(MILKV_HOST_TOOLS)/gcc/riscv64-linux-musl-x86_64/bin

# RISC-V build type — Debug for local dev (matches old behaviour), CI exports
# RISCV_BUILD_TYPE=Release for optimised release artifacts.
RISCV_BUILD_TYPE ?= Debug

# Check for RISC-V specific dependencies
.PHONY: check-riscv-deps
check-riscv-deps:
	@echo "Checking RISC-V dependencies..."
	@COMPILER_PATH="$(MILKV_RISCV_BIN)/riscv64-unknown-linux-musl-gcc"; \
	if [ -x "$$COMPILER_PATH" ]; then \
		echo "RISC-V compiler found at $$COMPILER_PATH"; \
		echo "Compiler version: $$("$$COMPILER_PATH" --version | head -n1)"; \
	else \
		echo "Error: RISC-V compiler not found at expected location: $$COMPILER_PATH"; \
		echo "Hint: set MILKV_HOST_TOOLS to the host-tools root."; \
		exit 1; \
	fi

# Build the mkptypes tool
.PHONY: mkptypes
mkptypes:
	@echo "Building mkptypes tool..."
	$(MAKE) -C tools/mkptypes

# Build targets
# Build RetroTermWeb from submodule and copy dist to lib
.PHONY: retroterm-build
retroterm-build:
	@if [ -f template-glass/external/RetroTermWeb/package.json ]; then \
		echo "Building RetroTermWeb from submodule..."; \
		cd template-glass/external/RetroTermWeb && npm install --silent && npm run build; \
		echo "Copying RetroTermWeb dist to template-glass/lib/retroterm/..."; \
		mkdir -p ../../lib/retroterm; \
		cp -r dist/* ../../lib/retroterm/; \
	else \
		echo "Error: RetroTermWeb submodule not found. Run: git submodule update --init --recursive"; \
		exit 1; \
	fi

# Build the norskdata-ndfs browser bundle (IIFE, global NdfsLib) from the
# submodule's TypeScript source. Output is a single self-contained file the
# Glass UI includes via <script>.
.PHONY: ndfs-build
ndfs-build:
	@if [ -f template-glass/external/norskdata-ndfs/ndfs-ts/package.json ]; then \
		echo "Building norskdata-ndfs browser bundle..."; \
		cd template-glass/external/norskdata-ndfs/ndfs-ts && npm install --silent; \
		mkdir -p ../../../lib/ndfs; \
		npx esbuild src/index.ts --bundle --format=iife --global-name=NdfsLib \
			--platform=browser --outfile=../../../lib/ndfs/ndfs-browser-bundle.js; \
		echo "NDFS bundle -> template-glass/lib/ndfs/ndfs-browser-bundle.js"; \
	else \
		echo "Error: norskdata-ndfs submodule not found. Run: git submodule update --init --recursive"; \
		exit 1; \
	fi

# Compile TypeScript terminal sources (if tsc available, else use pre-compiled JS)
.PHONY: ts-compile
ts-compile:
	@if npx tsc --version >/dev/null 2>&1; then \
		echo "Compiling TypeScript terminal modules..."; \
		npx tsc -p template-glass/ts/tsconfig.json; \
	else \
		echo "TypeScript not available, using pre-compiled JS files."; \
	fi

.PHONY: debug release sanitize wasm wasm-run wasm-glass wasm-glass-run riscv clean install run help gateway-install gateway gateway-run gateway-test gateway-test-wasm wasm-glass-gateway test submodules boot-smd boot-wd boot-floppy

debug: check-deps mkptypes $(CURL_PREREQ)
	@echo "Building debug version..."
	@mkdir -p $(BUILD_DIR_DEBUG)
	cd $(BUILD_DIR_DEBUG) && $(CMAKE) .. $(CMAKE_GENERATOR_FLAG) -DCMAKE_BUILD_TYPE=Debug -DDEBUGGER_ENABLED=$(DEBUGGER_ENABLED)
	cd $(BUILD_DIR_DEBUG) && $(CMAKE) --build . -- -j$(JOBS)
	$(call stage_curl_runtime,$(BUILD_DIR_DEBUG))

release: check-deps mkptypes $(CURL_PREREQ)
	@echo "Building release version..."
	@mkdir -p $(BUILD_DIR_RELEASE)
	cd $(BUILD_DIR_RELEASE) && $(CMAKE) .. $(CMAKE_GENERATOR_FLAG) -DCMAKE_BUILD_TYPE=Release -DDEBUGGER_ENABLED=$(DEBUGGER_ENABLED)
	cd $(BUILD_DIR_RELEASE) && $(CMAKE) --build . -- -j$(JOBS)
	$(call stage_curl_runtime,$(BUILD_DIR_RELEASE))

sanitize: check-deps mkptypes
	@echo "Building with sanitizers..."
	@mkdir -p $(BUILD_DIR_SANITIZE)
	cd $(BUILD_DIR_SANITIZE) && $(CMAKE) .. $(CMAKE_GENERATOR_FLAG) -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer" -DDEBUGGER_ENABLED=$(DEBUGGER_ENABLED)
	cd $(BUILD_DIR_SANITIZE) && $(CMAKE) --build . -- -j$(JOBS)

wasm: check-deps mkptypes
	@echo "Building WebAssembly version..."
	@command -v emcmake >/dev/null 2>&1 || { echo "Error: emcmake not found. Please install and activate Emscripten SDK."; exit 1; }
	@mkdir -p $(BUILD_DIR_WASM)
	cd $(BUILD_DIR_WASM) && $(EMCMAKE) $(CMAKE) .. -DBUILD_WASM=ON -DDEBUGGER_ENABLED=OFF
	cd $(BUILD_DIR_WASM) && $(CMAKE) --build . -- -j$$(nproc 2>/dev/null || echo 4)
	@echo "Copying index.html template to WASM build directory..."
	@mkdir -p $(BUILD_DIR_WASM)/bin
	@cp template/index.html $(BUILD_DIR_WASM)/bin/
	@echo ""
	@echo "WebAssembly build completed successfully!"
	@echo "To test in a browser, serve the build directory via HTTP:"
	@echo "  cd $(BUILD_DIR_WASM)/bin"
	@echo "  python3 -m http.server"
	@echo "Then open http://localhost:8000/index.html in your browser."

wasm-glass: check-deps mkptypes retroterm-build ndfs-build ts-compile
	@echo "Building WebAssembly version (glassmorphism UI)..."
	@command -v emcmake >/dev/null 2>&1 || { echo "Error: emcmake not found. Please install and activate Emscripten SDK."; exit 1; }
	@mkdir -p $(BUILD_DIR_WASM_GLASS)
	cd $(BUILD_DIR_WASM_GLASS) && $(EMCMAKE) $(CMAKE) .. -DBUILD_WASM=ON -DDEBUGGER_ENABLED=ON
	cd $(BUILD_DIR_WASM_GLASS) && $(CMAKE) --build . -- -j$$(nproc 2>/dev/null || echo 4)
	@echo "Copying glassmorphism UI and shared assets to build directory..."
	@mkdir -p $(BUILD_DIR_WASM_GLASS)/bin
	@cp template-glass/index.html $(BUILD_DIR_WASM_GLASS)/bin/index.html
	@cp template-glass/terminal-popout.html $(BUILD_DIR_WASM_GLASS)/bin/terminal-popout.html 2>/dev/null || true
	@cp -r template-glass/css $(BUILD_DIR_WASM_GLASS)/bin/ 2>/dev/null || true
	@cp -r template-glass/js $(BUILD_DIR_WASM_GLASS)/bin/ 2>/dev/null || true
	@cp -r template-glass/data $(BUILD_DIR_WASM_GLASS)/bin/ 2>/dev/null || true
	@cp -r template-glass/lib $(BUILD_DIR_WASM_GLASS)/bin/ 2>/dev/null || true
	@cp template-glass/disk-catalog.json $(BUILD_DIR_WASM_GLASS)/bin/ 2>/dev/null || true
	@cp template-glass/staticwebapp.config.json $(BUILD_DIR_WASM_GLASS)/bin/ 2>/dev/null || true
	@cp template/Logo_ND.png template/favicon.ico template/favicon.png $(BUILD_DIR_WASM_GLASS)/bin/ 2>/dev/null || true
	@cp -r template/floppies $(BUILD_DIR_WASM_GLASS)/bin/ 2>/dev/null || true
	@cp docs/SINTRAN-Commands.html $(BUILD_DIR_WASM_GLASS)/bin/ 2>/dev/null || true
	@cp -r template-glass/PDF $(BUILD_DIR_WASM_GLASS)/bin/ 2>/dev/null || true
	@# The disc images disk-catalog.json points at, served next to the page so
	@# "Copy to Library" works and the shipped machines boot. EVERY
	@# images/*.IMG.bz2 is unpacked into the run folder on EVERY build and
	@# replaces the served copy: what is in images/ is what is served, and
	@# nothing else overrides it and the repo root is never read (a clean
	@# checkout and a developer tree get the same ND-100, ND-5000, BSD 2.11,
	@# NDIX and TSS images).
	@for bz in images/*.IMG.bz2; do \
	  [ -f "$$bz" ] || continue; \
	  img=$(BUILD_DIR_WASM_GLASS)/bin/$$(basename "$$bz" .bz2); \
	  echo "Unpacking $$bz -> $$img"; \
	  bunzip2 -k -c "$$bz" > "$$img.tmp" && mv "$$img.tmp" "$$img" || { rm -f "$$img.tmp"; echo "  failed"; }; \
	done
	@# Generate version.js so the Glass UI tracks the CMake project version.
	@NDVER=$$(grep -oE 'project\(nd100x VERSION [0-9.]+' CMakeLists.txt | grep -oE '[0-9]+\.[0-9]+\.[0-9]+'); \
	NDBUILD=$$(date '+%Y-%m-%d %H:%M'); \
	printf 'window.ND100X_VERSION="%s";\nwindow.ND100X_BUILD="%s";\n' "$$NDVER" "$$NDBUILD" > $(BUILD_DIR_WASM_GLASS)/bin/version.js; \
	echo "Version: $$NDVER (built $$NDBUILD)"
	@# Cache-bust: append ?v=TIMESTAMP to all local src/href in HTML files
	@BUILD_TS=$$(date +%s); \
	$(SED_INPLACE) \
	  -e "s|src=\"js/|src=\"js/|g" \
	  -e "s|src=\"version.js\"|src=\"version.js?v=$$BUILD_TS\"|g" \
	  -e "s|src=\"nd100wasm.js\"|src=\"nd100wasm.js?v=$$BUILD_TS\"|g" \
	  -e "s|href=\"css/styles.css\"|href=\"css/styles.css?v=$$BUILD_TS\"|g" \
	  -e "s|href=\"css/themes.css\"|href=\"css/themes.css?v=$$BUILD_TS\"|g" \
	  -e "s|src=\"js/\([^\"]*\)\"|src=\"js/\1?v=$$BUILD_TS\"|g" \
	  -e "s|href=\"lib/retroterm/retroterm.css\"|href=\"lib/retroterm/retroterm.css?v=$$BUILD_TS\"|g" \
	  -e "s|src=\"lib/retroterm/retroterm.js\"|src=\"lib/retroterm/retroterm.js?v=$$BUILD_TS\"|g" \
	  -e "s|src=\"lib/ndfs/ndfs-browser-bundle.js\"|src=\"lib/ndfs/ndfs-browser-bundle.js?v=$$BUILD_TS\"|g" \
	  -e "s|href=\"lib/xterm/xterm.min.css\"|href=\"lib/xterm/xterm.min.css?v=$$BUILD_TS\"|g" \
	  -e "s|src=\"lib/xterm/\([^\"]*\)\"|src=\"lib/xterm/\1?v=$$BUILD_TS\"|g" \
	  $(BUILD_DIR_WASM_GLASS)/bin/index.html; \
	$(SED_INPLACE) \
	  -e "s|href=\"css/styles.css\"|href=\"css/styles.css?v=$$BUILD_TS\"|g" \
	  -e "s|href=\"css/themes.css\"|href=\"css/themes.css?v=$$BUILD_TS\"|g" \
	  -e "s|src=\"js/\([^\"]*\)\"|src=\"js/\1?v=$$BUILD_TS\"|g" \
	  -e "s|href=\"lib/xterm/xterm.min.css\"|href=\"lib/xterm/xterm.min.css?v=$$BUILD_TS\"|g" \
	  -e "s|src=\"lib/xterm/\([^\"]*\)\"|src=\"lib/xterm/\1?v=$$BUILD_TS\"|g" \
	  $(BUILD_DIR_WASM_GLASS)/bin/terminal-popout.html; \
	echo "Cache-bust: v=$$BUILD_TS"
	@echo ""
	@echo "WebAssembly build (glassmorphism UI) completed successfully!"
	@echo "To test in a browser, serve the build directory via HTTP:"
	@echo "  cd $(BUILD_DIR_WASM_GLASS)/bin"
	@echo "  python3 -m http.server"
	@echo "Then open http://localhost:8000/index.html in your browser."

wasm-run: wasm
	@echo "Starting HTTP server for standard WASM build..."
	cd $(BUILD_DIR_WASM)/bin && node $(CURDIR)/tools/serve-coop.mjs 8000

wasm-glass-run: wasm-glass
	@echo "Starting HTTP server for glassmorphism WASM build..."
	cd $(BUILD_DIR_WASM_GLASS)/bin && node $(CURDIR)/tools/serve-coop.mjs 8000

riscv: check-riscv-deps mkptypes
	@echo "Building RISC-V Linux version ($(RISCV_BUILD_TYPE)) for Milk-V Duo..."
	@echo "  MILKV_HOST_TOOLS = $(MILKV_HOST_TOOLS)"
	@mkdir -p $(BUILD_DIR_RISCV)

	@# Configure. PATH and MILKV_HOST_TOOLS are exported so the toolchain
	@# file picks the right compiler.
	cd $(BUILD_DIR_RISCV) && \
	MILKV_HOST_TOOLS="$(MILKV_HOST_TOOLS)" \
	PATH="$$PATH:$(MILKV_RISCV_BIN)" \
	$(CMAKE) .. -DCMAKE_BUILD_TYPE=$(RISCV_BUILD_TYPE) -DBUILD_RISCV=ON \
		-DCMAKE_TOOLCHAIN_FILE=../riscv64-toolchain.cmake \
		-DBUILD_DAP_TOOLS=OFF -DENABLE_CJSON_TEST=OFF \
		-DCMAKE_CXX_COMPILER_WORKS=TRUE -DCMAKE_C_COMPILER_WORKS=TRUE

	@# Build.
	cd $(BUILD_DIR_RISCV) && \
	MILKV_HOST_TOOLS="$(MILKV_HOST_TOOLS)" \
	PATH="$$PATH:$(MILKV_RISCV_BIN)" \
	$(CMAKE) --build . -- -j$$(nproc 2>/dev/null || echo 4)
	
	@echo ""
	@if [ -f $(BUILD_DIR_RISCV)/bin/nd100x ]; then \
		echo "RISC-V build completed successfully!"; \
		echo "Executable information:"; \
		file $(BUILD_DIR_RISCV)/bin/nd100x; \
		if file $(BUILD_DIR_RISCV)/bin/nd100x | grep -q "RISC-V"; then \
			echo "✅ Successfully built RISC-V executable with debug symbols!"; \
		else \
			echo "❌ Warning: The executable doesn't appear to be a RISC-V binary."; \
		fi; \
	else \
		echo "❌ Build failed. Check the error messages above."; \
	fi

dap-tools: check-deps mkptypes
	@echo "Building with DAP tools..."
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && $(CMAKE) .. $(CMAKE_GENERATOR_FLAG) -DBUILD_DAP_TOOLS=ON -DDEBUGGER_ENABLED=ON
	cd $(BUILD_DIR) && $(CMAKE) --build . -- -j$(JOBS)

clean:
	@echo "Cleaning build directories..."
	rm -rf $(BUILD_DIR) $(BUILD_DIR_DEBUG) $(BUILD_DIR_RELEASE) $(BUILD_DIR_SANITIZE) $(BUILD_DIR_WASM) $(BUILD_DIR_WASM_GLASS) $(BUILD_DIR_RISCV)


submodules:
	@echo "Updating all git submodules..."
	git submodule update --init --recursive

update-libdap:
	cd external/libdap && git fetch && git checkout origin/main
	git add external/libdap
	git commit -m "Update libdap submodule to latest commit"

	
install:
	@echo "Installing..."
	$(CMAKE) --install $(BUILD_DIR)

# Default run arguments - you can override these by setting environment variables
# e.g., BOOT_TYPE=floppy make run
BOOT_TYPE ?= smd
IMAGE_FILE ?=
START_ADDR ?= 0
VERBOSE ?= 0
DEBUGGER ?= 0
DISASM ?= 0

run: debug
	@echo "Running nd100x..."
	@if [ -n "$(IMAGE_FILE)" ]; then \
		IMAGE_ARG="--image=$(IMAGE_FILE)"; \
	else \
		IMAGE_ARG=""; \
	fi; \
	VERBOSE_ARG=$$([ "$(VERBOSE)" = "1" ] && echo "--verbose" || echo ""); \
	DEBUGGER_ARG=$$([ "$(DEBUGGER)" = "1" ] && echo "--debugger" || echo ""); \
	DISASM_ARG=$$([ "$(DISASM)" = "1" ] && echo "--disasm" || echo ""); \
	$(BUILD_DIR)/bin/nd100x --boot=$(BOOT_TYPE) $$IMAGE_ARG --start=$(START_ADDR) $$VERBOSE_ARG $$DEBUGGER_ARG $$DISASM_ARG


# Boot shortcuts: build debug, then boot from a specific device. The image is
# passed explicitly and checked before launch, so a missing file fails with a
# clear message instead of booting an unmounted drive.
# Override per run, e.g.:  make boot-wd WD0_IMAGE=WD0-M.IMG
SMD0_IMAGE   ?= SMD0.IMG
WD0_IMAGE    ?= WD0.IMG
FLOPPY_IMAGE ?= FLOPPY.IMG

boot-smd: debug
	@test -f "$(SMD0_IMAGE)" || { echo "boot-smd: image '$(SMD0_IMAGE)' not found. Use: make boot-smd SMD0_IMAGE=<file>"; exit 1; }
	$(BUILD_DIR)/bin/nd100x --boot=smd --smd0=$(SMD0_IMAGE)

boot-wd: debug
	@test -f "$(WD0_IMAGE)" || { echo "boot-wd: image '$(WD0_IMAGE)' not found. Use: make boot-wd WD0_IMAGE=<file>"; exit 1; }
	$(BUILD_DIR)/bin/nd100x --boot=wd --wd0=$(WD0_IMAGE)

boot-floppy: debug
	@test -f "$(FLOPPY_IMAGE)" || { echo "boot-floppy: image '$(FLOPPY_IMAGE)' not found. Use: make boot-floppy FLOPPY_IMAGE=<file>"; exit 1; }
	$(BUILD_DIR)/bin/nd100x --boot=floppy --image=$(FLOPPY_IMAGE)

test: debug
	@echo "Running tests..."
	cd $(BUILD_DIR) && ctest --output-on-failure

runv: debug
	@echo "Running with valgrind.."
	valgrind --leak-check=full  $(BUILD_DIR)/bin/nd100x -d -v

gateway-install:
	@echo "Installing gateway dependencies..."
	cd tools/nd100-gateway && npm install

gateway: gateway-install
	@echo "Starting ND-100 Terminal Gateway..."
	$(NODE) tools/nd100-gateway/gateway.js $(GATEWAY_ARGS)

gateway-run: gateway

# Both suites here are self-contained: each starts its own gateway on ports
# nothing else uses (test-ethernet.js takes 13094/18765 rather than the default
# 3094/8765 precisely so a real gateway can keep running alongside it), so
# neither needs a build, a browser or a disk image.
gateway-test: gateway-install
	@echo "Running gateway tests..."
	$(NODE) tools/nd100-gateway/test-gateway.js
	$(NODE) tools/nd100-gateway/test-ethernet.js

# Separate target because this one is the exception: it loads a BUILT wasm
# module under node, so it cannot run until `make wasm-glass` (or `make wasm`)
# has produced one. Kept out of gateway-test so that target stays runnable on a
# clean checkout.
gateway-test-wasm: gateway-install
	@echo "Running ND-500 ethernet export tests against the wasm build..."
	@if [ -f $(BUILD_DIR_WASM_GLASS)/bin/nd100wasm.js ]; then \
		$(NODE) tools/nd100-gateway/test-eth-wasm.js $(BUILD_DIR_WASM_GLASS)/bin/nd100wasm.js; \
	elif [ -f $(BUILD_DIR_WASM)/bin/nd100wasm.js ]; then \
		$(NODE) tools/nd100-gateway/test-eth-wasm.js $(BUILD_DIR_WASM)/bin/nd100wasm.js; \
	else \
		echo "No wasm module built yet - run 'make wasm-glass' first."; \
		exit 1; \
	fi

wasm-glass-gateway: wasm-glass gateway-install
	@echo "Starting glassmorphism WASM build + Gateway (unified server)..."
	@echo "Open http://localhost:8765/?worker=1 in your browser."
	@echo "Static files served with COOP/COEP headers (SharedArrayBuffer support)."
	@echo ""
	$(NODE) tools/nd100-gateway/gateway.js --static $(BUILD_DIR_WASM_GLASS)/bin


help:
	@echo "ND100X Makefile Help"
	@echo "-------------------------------------------------------------------------------"
	@echo "Targets:"
	@echo "  all (default) - Same as 'debug'"
	@echo "  debug         - Build debug version"
	@echo "  release       - Build release version"
	@echo "  sanitize      - Build with address sanitizer"
	@echo "  wasm          - Build WebAssembly version (standard UI)"
	@echo "  wasm-run      - Build and serve standard WASM version"
	@echo "  wasm-glass    - Build WebAssembly version (glassmorphism UI)"
	@echo "  wasm-glass-run - Build and serve glassmorphism WASM version"
	@echo "  riscv         - Build RISC-V Linux version with DAP support"
	@echo "                  (Requires the Milk-V host-tools; set MILKV_HOST_TOOLS to its"
	@echo "                   root if it is not at \$$HOME/milkv/host-tools)"
	@echo "  dap-tools     - Build with DAP tools (dap_debugger and dap_mock_server)"
	@echo "                  (Requires libdap)"
	@echo "  gateway-install - Install gateway server dependencies (npm)"
	@echo "  gateway       - Start the terminal gateway server"
	@echo "  gateway-run   - Start the terminal gateway server (alias)"
	@echo "  gateway-test  - Run gateway unit + ethernet segment tests (27 tests)"
	@echo "  gateway-test-wasm - Run ND-500 ethernet export tests (17, needs a wasm build)"
	@echo "  wasm-glass-gateway - Build glass UI + start gateway (unified server)"
	@echo "  test          - Build and run unit tests (ctest)"
	@echo "  submodules    - Init and update all git submodules (recursive)"
	@echo "  clean         - Remove build directories"
	@echo "  install       - Install the build"
	@echo "  run           - Build and run nd100x (uses defaults below)"
	@echo "  boot-smd      - Build and boot from SMD unit 0 (SMD0_IMAGE, default SMD0.IMG)"
	@echo "  boot-wd       - Build and boot from Winchester unit 0 (WD0_IMAGE, default WD0.IMG)"
	@echo "  boot-floppy   - Build and boot from floppy (FLOPPY_IMAGE, default FLOPPY.IMG)"
	@echo "                  e.g. make boot-wd WD0_IMAGE=WD0-M.IMG"
	@echo "  runv          - Build and run with valgrind"
	@echo "  help          - Show this help"
	@echo ""
	@echo "Run options (environment variables):"
	@echo "  BOOT_TYPE=smd|floppy|bpun|aout|bp   Boot type (default: smd)"
	@echo "  IMAGE_FILE=path                     Image file to load"
	@echo "  START_ADDR=0                        Start address (default: 0)"
	@echo "  VERBOSE=1                           Enable verbose output"
	@echo "  DEBUGGER=1                          Enable DAP debugger"
	@echo "  DISASM=1                            Enable disassembly output"
	@echo ""
	@echo "Build options (environment variables):"
	@echo "  DEBUGGER_ENABLED=ON|OFF             Enable/disable debugger support in build"
	@echo "                                      (Default: ON for most builds, OFF for WASM)"
	@echo ""
	@echo "WebAssembly options:"
	@echo "  The wasm target builds a browser-compatible version of the emulator."
	@echo "  After building, serve the $(BUILD_DIR_WASM)/bin directory via HTTP"
	@echo "  and open index.html in a browser."
	@echo ""
	@echo "Example:"
	@echo "  BOOT_TYPE=floppy IMAGE_FILE=FLOPPY.IMG make run"
	@echo "  DEBUGGER_ENABLED=OFF make release    # Build without debugger support"
	@echo "  make riscv                           # Build for RISC-V Linux"
	@echo ""
	@echo "This Makefile is a wrapper around CMake. If you prefer, you can use CMake directly:"
	@echo "  ./build.sh          - For more build options"
	@echo "  ./build_riscv.sh    - For RISC-V Linux builds"
	@echo ""