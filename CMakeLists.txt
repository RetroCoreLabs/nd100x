cmake_minimum_required(VERSION 3.14)

# Prevent in-source builds to protect the existing Makefile
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR 
        "In-source builds are not allowed!\n"
        "This would overwrite the existing Makefile in the root folder.\n"
        "Please create a build directory and run cmake from there:\n"
        "  mkdir -p build && cd build && cmake ..\n"
        "To remove files created by an attempted in-source build, run:\n"
        "  rm -rf CMakeCache.txt CMakeFiles/\n"
    )
endif()

project(nd100x VERSION 1.0.12 DESCRIPTION "ND100X Emulator" LANGUAGES C CXX)

# Options
option(BUILD_DAP_TOOLS "Build libdap test tools" OFF)
option(BUILD_WASM "Build for WebAssembly using Emscripten" OFF)
option(BUILD_RISCV "Build for RISC-V Linux using gcc-riscv64-unknown-linux-musl" OFF)
option(DEBUGGER_ENABLED "Enable debugger support" ON)

# Platform detection
# WIN32 is set by CMake on all Windows builds (MSVC, MinGW, w64devkit, MSYS2)
# so use it in addition to CMAKE_SYSTEM_NAME for reliability.
if(WIN32)
    set(PLATFORM_WINDOWS TRUE)
    message(STATUS "Building for Windows")
    # Force-disable the debugger on Windows until libdap and libsymbols have
    # their own Windows ports — without those backends the debugger module
    # has no implementation to call into. Removing this override will work
    # the moment both submodules compile on Windows.
    if(DEBUGGER_ENABLED)
        message(STATUS "DEBUGGER_ENABLED overridden to OFF on Windows "
                       "(libdap/libsymbols not ported yet)")
    endif()
    set(DEBUGGER_ENABLED OFF CACHE BOOL "Enable debugger support" FORCE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(BUILD_RISCV OR CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64")
        set(PLATFORM_RISCV TRUE)
        set(DEBUGGER_ENABLED FALSE)
        message(STATUS "Building for RISC-V Linux")
    else()
        set(PLATFORM_LINUX TRUE)
        message(STATUS "Building for Linux")
    endif()
elseif(APPLE)
    set(PLATFORM_MACOS TRUE)
    message(STATUS "Building for macOS")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(PLATFORM_WASM TRUE)
    message(STATUS "Building for WebAssembly")

    if(BUILD_WASM OR EMSCRIPTEN)
        message(STATUS "Configuring for WebAssembly (WASM) using Emscripten")
    endif()
else()
    message(WARNING "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

# Add cJSON as an object library first
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/cJSON")
    add_library(cjson_objects OBJECT
        external/cJSON/cJSON.c
    )
    target_include_directories(cjson_objects PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/external/cJSON
    )

    # The header ships as a bare cJSON.h, but callers include it as
    # <cjson/cJSON.h> - that is the path the distro packages install, and it is
    # what nd500x's src/machine/memory_map.c:40 and libdap both write.
    # So stage a cjson/ directory and export it from the target itself.
    #
    # THIS USED TO LIVE INSIDE if(DEBUGGER_ENABLED), which is why the WASM build
    # broke the moment nd500x was linked in: `make wasm` passes
    # -DDEBUGGER_ENABLED=OFF, the cjson/ directory was never created, and
    # memory_map.c died with "cjson/cJSON.h file not found" at 99% of the build.
    # It has nothing to do with the debugger - anything that links cjson_objects
    # needs it, so it belongs with the target.
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/include/cjson)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/external/cJSON/cJSON.h
        ${CMAKE_BINARY_DIR}/include/cjson/cJSON.h
        COPYONLY
    )
    target_include_directories(cjson_objects PUBLIC ${CMAKE_BINARY_DIR}/include)

    message(STATUS "cJSON library support enabled")
endif()

# Find required packages for menu functionality.
#
# - WASM / RISC-V: no menu, skip entirely.
# - Linux: CURL + ncurses are required (menu is built, URL image loads work).
# - Windows: CURL is optional; if missing, download_file() compiles as a stub.
#   ncurses is not looked up — the F12 floppy-DB menu is not ported to Windows
#   yet and is compiled out via NEED_MENU in src/frontend/nd100x/CMakeLists.txt.
if(NOT (PLATFORM_WASM OR BUILD_WASM OR PLATFORM_RISCV OR BUILD_RISCV))
    if(PLATFORM_WINDOWS)
        # Prefer a VENDORED prebuilt MinGW libcurl under external/curl (fetched by
        # scripts/fetch-curl.sh — the same "vendor a mingw devel release" approach the
        # tiki100 project uses for SDL2). This lets a plain w64devkit build link real
        # libcurl without MSYS2. curl-for-win ships no CMake config, so set the classic
        # CURL_* result variables by hand; src/ndlib/CMakeLists.txt consumes them.
        set(VENDORED_CURL_DIR "${CMAKE_SOURCE_DIR}/external/curl")
        if(EXISTS "${VENDORED_CURL_DIR}/lib/libcurl.dll.a")
            set(CURL_FOUND TRUE)
            set(CURL_INCLUDE_DIRS "${VENDORED_CURL_DIR}/include")
            set(CURL_LIBRARIES    "${VENDORED_CURL_DIR}/lib/libcurl.dll.a")
            add_definitions(-DHAVE_CURL)
            # Bundled CA file (this curl build verifies TLS against a CA bundle). Baked in
            # as a compile-time fallback path; build.bat/Makefile also stage it next to the
            # exe and download.c prefers the exe-relative copy at runtime.
            add_definitions(-DND100X_VENDORED_CA_BUNDLE="${VENDORED_CURL_DIR}/bin/curl-ca-bundle.crt")
            # Export the runtime dir so the frontend target can POST_BUILD-stage the DLL +
            # CA bundle next to the exe. Without this a plain `cmake --build` yields an exe
            # that cannot start (libcurl-x64.dll not found). CACHE INTERNAL = visible in the
            # frontend subdirectory scope.
            set(ND100X_VENDORED_CURL_BIN "${VENDORED_CURL_DIR}/bin" CACHE INTERNAL "vendored libcurl runtime dir")
            message(STATUS "CURL: using vendored MinGW libcurl at ${VENDORED_CURL_DIR}")
        else()
            # No vendored copy — fall back to a system libcurl (e.g. MSYS2 mingw-w64-curl).
            find_package(CURL QUIET)
            if(CURL_FOUND)
                message(STATUS "CURL library found (system): ${CURL_VERSION_STRING}")
                add_definitions(-DHAVE_CURL)
            else()
                message(STATUS "CURL not found on Windows — download_file() will be a stub. "
                               "Run 'make fetch-curl' to vendor libcurl, or install "
                               "mingw-w64-x86_64-curl under MSYS2.")
            endif()
        endif()
        message(STATUS "Curses lookup skipped on Windows (F12 menu disabled).")
    else()
        find_package(CURL REQUIRED)
        message(STATUS "CURL library found: ${CURL_FOUND}")
        add_definitions(-DHAVE_CURL)

        find_package(Curses REQUIRED)
        message(STATUS "Curses library found: ${CURSES_FOUND}")
    endif()
endif()

# Handle RISC-V specific configuration
if(PLATFORM_RISCV)
    message(STATUS "Using RISC-V cross-compiler: ${CMAKE_C_COMPILER}")
    
    # For RISC-V, disable DAP client tools but keep DAP support
    # Setting this to OFF to prevent building dap_debugger and dap_mock_server
    set(BUILD_DAP_TOOLS OFF CACHE BOOL "Disable DAP client tools for RISC-V" FORCE)
    set(ENABLE_CJSON_TEST OFF CACHE BOOL "Disable cJSON tests for RISC-V" FORCE)
    
    # Force static libraries for RISC-V builds
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
    
    # Add special linker flags for RISC-V
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc")
    
    # Add include path for cJSON for RISC-V builds
    include_directories(${CMAKE_SOURCE_DIR}/external/cJSON)
    add_definitions(-DCJSON_INCLUDE_PATH="${CMAKE_SOURCE_DIR}/external/cJSON")
endif()

# Handle debugger configuration
if(DEBUGGER_ENABLED)
    add_definitions(-DWITH_DEBUGGER )
    message(STATUS "Enabling debugger support with WITH_DEBUGGER flag")
    
    # Create a directory structure for cjson headers
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/include/cjson)
    
    # Copy cJSON.h to the expected location
    configure_file(
        ${CMAKE_SOURCE_DIR}/external/cJSON/cJSON.h
        ${CMAKE_BINARY_DIR}/include/cjson/cJSON.h
        COPYONLY
    )
    
    # Add this to the include path
    include_directories(${CMAKE_BINARY_DIR}/include)
    
    # Add libdap if it exists. Skipped on WASM (no sockets/pthreads in the
    # browser) and on Windows (libdap's source uses POSIX-only <sys/socket.h>
    # / <netdb.h> and needs its own Windows port — same story as libsymbols).
    # When skipped, the debugger module compiles its stub implementations.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/libdap/CMakeLists.txt"
       AND NOT PLATFORM_WASM AND NOT PLATFORM_WINDOWS)
        set(BUILD_DAP_TEST_TOOLS ${BUILD_DAP_TOOLS} CACHE BOOL "Build libdap test tools" FORCE)
        add_subdirectory(external/libdap EXCLUDE_FROM_ALL)
        if(TARGET dap_objects)
            target_link_libraries(dap_objects PRIVATE cjson_objects)
        endif()
        message(STATUS "DAP library support enabled (only for Linux/Windows builds)")
    elseif(PLATFORM_WINDOWS)
        message(STATUS "libdap skipped on Windows (port pending — DAP debugger unavailable)")
    endif()
else()    
    message(STATUS "Debugger support disabled")
endif()

# Add libsymbols if it exists - always include this for a.out support.
# Excluded on Windows: libsymbols' source uses POSIX-only constructs
# (strndup, DATA_START_SPLIT_ID derived from system a.out.h) that need a
# separate port. Without it, .aout boot is unavailable on Windows; bp/bpun/
# floppy/smd boot types still work.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/libsymbols/CMakeLists.txt"
   AND NOT PLATFORM_WINDOWS)
    add_subdirectory(external/libsymbols EXCLUDE_FROM_ALL)
    message(STATUS "Symbol library support enabled")
elseif(PLATFORM_WINDOWS)
    message(STATUS "libsymbols skipped on Windows (port pending — .aout boot unavailable)")
endif()

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Build mkptypes tool first before we need it.
#
# A Windows configure copies mkptypes.exe onto the extension-less name
# `mkptypes` (see the WIN32 block below). That PE file is then newer than
# mkptypes.c, so a later build on Linux in the same working tree sees the tool
# as up to date, leaves the Windows binary in place, and every prototype
# generation step dies with "Exec format error". Force the rebuild on
# non-Windows so `mkptypes` is always a native binary here; it is one small C
# file, so the cost is a fraction of a second.
message(STATUS "Building mkptypes tool...")
if(WIN32)
    set(MKPTYPES_MAKE_ARGS)
else()
    set(MKPTYPES_MAKE_ARGS -B)
endif()
# CC=gcc on the make command line: under emcmake this process inherits
# CC=<emsdk>/emcc from the environment, so without it the tool is built as
# JavaScript over the native binary and every prototype step then fails with
# "Permission denied". A command-line CC overrides the environment one. Same
# line in nd500x (decided by Ronny 18-SEP-2026 for both repos).
execute_process(
    COMMAND make ${MKPTYPES_MAKE_ARGS} CC=gcc -C ${CMAKE_SOURCE_DIR}/tools/mkptypes
    RESULT_VARIABLE MKTYPES_RESULT
)
if(NOT MKTYPES_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to build mkptypes tool. Error: ${MKTYPES_RESULT}")
endif()

# On Windows, the Makefile emits mkptypes.exe, but every downstream
# CMakeLists references the tool as `${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes`
# (no extension). Copy the binary to the extension-less name so those 30+
# existing references keep working. Windows' CreateProcess will execute a
# valid PE file regardless of whether its name ends in .exe.
if(WIN32)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes.exe
            ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes
        RESULT_VARIABLE MKTYPES_COPY_RESULT
    )
    if(NOT MKTYPES_COPY_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to copy mkptypes.exe -> mkptypes")
    endif()
endif()

# House C standard (language level + warning set) for the project's own targets
include(${CMAKE_SOURCE_DIR}/cmake/house_standard.cmake)

# Add subdirectories in the correct order for header generation
add_subdirectory(src/ndlib)
add_subdirectory(src/machine)
add_subdirectory(src/cpu)
add_subdirectory(src/devices)
add_subdirectory(src/debugger)

# Cross-module generated-header dependencies.
#
# cpu.c #includes machine/machine_protos.h, so the cpu library needs the
# machine protos generated first. On Ninja with Windows, this dep plus the
# fact that machine PUBLIC-links cpu used to create a cycle — that's why
# machine's CMakeLists wraps the generator in a custom_target rather than
# listing machine_protos.h as a library source.
if(TARGET cpu AND TARGET machine_protos_gen)
    add_dependencies(cpu machine_protos_gen)
endif()

# ---------------------------------------------------------------------------
# The ND-500, optionally, as a library inside this build.
#
# WHY IN-PROCESS AND NOT A SECOND MODULE. An ND-100 + ND-500 machine has SHARED
# MEMORY (MPM5): the ND-500 reads and writes the ND-100's memory directly.
# In a browser that means one WebAssembly.Memory, and a Module owns its memory,
# so the two CPUs have to live in ONE module. Loading nd500wasm.js next to
# nd100wasm.js cannot express the hardware.
#
# OPT-IN, AND SILENT WHEN ABSENT. nd500x is a separate repository - and with
# its ndmonlib submodule a private one - so a missing checkout must not break
# anybody's build. No directory, no ND-500, everything else identical.
#
# ND500X_WITH_NDMON=OFF is deliberate and is the configuration nd500x documents
# for exactly this: nd500_cpu + nd500_machine with no ndmonlib, which still
# boots NDIX (NDIX needs only MON 600, served by nd500_fecall.c). SINTRAN DOM
# programs are what that leaves out, and those are not what the browser runs.
# WHERE nd500x COMES FROM, AND WHY THE SIBLING CHECKOUT STILL WINS.
#
# nd500x is now a submodule at external/nd500x, so a fresh clone plus
# `git submodule update --init external/nd500x` builds the ND-500 with no
# machine-specific path anywhere.
#
# The DEFAULT is still the sibling checkout ../nd500x when one exists, and that
# is deliberate: this repository is developed alongside a working nd500x tree,
# and silently building the submodule's pinned commit instead would compile code
# that is several changes behind the one being worked on - without saying so.
# That failure does not error, it just answers a different question.
#
# So: sibling if present, submodule otherwise, and ND100X_ND500X_DIR overrides
# both. The chosen path is printed below, so which one is in use is never a
# guess.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../nd500x/CMakeLists.txt")
    set(ND100X_ND500X_DEFAULT "${CMAKE_CURRENT_LIST_DIR}/../nd500x")
else()
    set(ND100X_ND500X_DEFAULT "${CMAKE_CURRENT_LIST_DIR}/external/nd500x")
endif()

set(ND100X_ND500X_DIR "${ND100X_ND500X_DEFAULT}"
    CACHE PATH "Path to the nd500x checkout (empty or missing = no ND-500)")

set(ND100X_WITH_ND500 OFF)
if(ND100X_ND500X_DIR AND EXISTS "${ND100X_ND500X_DIR}/CMakeLists.txt")
    set(ND100X_WITH_ND500 ON)
    # nd500x's libraries are all nd500_-prefixed, so nothing collides with
    # ndlib/cpu/machine/devices/debugger here.
    set(ND500X_WITH_NDMON OFF CACHE BOOL "" FORCE)
    # Both repositories vendor external/libdap, and two add_subdirectory()'d
    # trees creating the same target is a hard CMake error. nd100x's copy is
    # the one that stays; nd500x does not need it here, since DAP belongs to
    # its debugger and that is off in the WITH_NDMON=OFF configuration above.
    set(SKIP_LIBDAP ON CACHE BOOL "" FORCE)
    add_subdirectory(${ND100X_ND500X_DIR} ${CMAKE_BINARY_DIR}/nd500x EXCLUDE_FROM_ALL)

    # The MFbus bridge, src/machine/mfbus_bridge.c, joins the shared ND-5000
    # memory pool to the ND-100 bank table. It compiles to NOTHING without this
    # definition, so a machine built with no ND-500 needs none of nd500x's
    # headers.
    #
    # Applied HERE and not in src/machine/CMakeLists.txt: that directory is
    # added further up this file, before nd500x is detected, so a test of
    # ND100X_WITH_ND500 there is always false and never says so.
    target_compile_definitions(machine PRIVATE ND100X_WITH_ND500=1)
    # mfbus_bridge.c creates a real Nd500Machine per ND-5000 station, so the
    # ND-500 CPU comes along. nd500x's libraries are all nd500_-prefixed, so
    # nothing collides with nd100x's own cpu/machine/ndlib.
    target_link_libraries(machine PUBLIC nd500::ndbus nd500::machine nd500::cpu)
    # nd500::headers puts nd500x's src/ on the include path so mfbus_bridge.c can
    # say "cpu/cpu_protos.h" and reach nd500x's, not nd100x's file of the same
    # basename.
    target_link_libraries(machine PRIVATE nd500::headers)
    # machine_config_apply.c builds the MFbus, the octobus card and the ND-5000
    # stations from the .ini, and is in the same library.
    message(STATUS "ND-500 ENABLED from ${ND100X_ND500X_DIR}")
    if(ND100X_ND500X_DIR STREQUAL "${CMAKE_CURRENT_LIST_DIR}/external/nd500x")
        message(STATUS "  (the external/nd500x submodule, at its pinned commit)")
    endif()
else()
    message(STATUS "ND-500 not built (no checkout at ${ND100X_ND500X_DIR})"
                   " - run 'git submodule update --init external/nd500x',"
                   " or set ND100X_ND500X_DIR to a checkout")
endif()

# Add frontend subdirectories conditionally
if(BUILD_WASM)
    add_subdirectory(src/frontend/nd100wasm)
else()
    add_subdirectory(src/frontend/nd100x)
endif()

# Add test subdirectory for every NATIVE build (Windows included).
#
# Windows used to be excluded wholesale because the printer tests use POSIX
# mkdtemp, which has no MinGW equivalent without further work. That also hid
# the CPU tests from the only platform most of this is developed on, so the
# per-test guard now lives in tests/CMakeLists.txt instead: only test_printer
# is skipped on Windows, everything else (test_hdlc, test_bcd) builds and runs.
if(NOT (BUILD_WASM OR BUILD_RISCV))
    enable_testing()
    add_subdirectory(tests)
endif()

# Emscripten-specific settings are applied in src/frontend/nd100wasm/CMakeLists.txt
# Do NOT override LINK_FLAGS here - the subdirectory sets EXPORTED_FUNCTIONS etc.

# Display helper messages
if(PLATFORM_WASM OR BUILD_WASM)
    message(STATUS "")
    message(STATUS "======================= WebAssembly Build Notes =======================")
    message(STATUS "To test in a browser, serve the build directory via HTTP server:")
    message(STATUS "    cd ${CMAKE_BINARY_DIR}")
    message(STATUS "    python3 -m http.server")
    message(STATUS "Then open http://localhost:8000 in your browser.")
    message(STATUS "====================================================================")
    message(STATUS "")
endif()

# Display RISC-V specific instructions
if(BUILD_RISCV)
    message(STATUS "")
    message(STATUS "======================= RISC-V Build Notes ==========================")
    message(STATUS "Building for RISC-V Linux using gcc-riscv64-unknown-linux-musl")
    message(STATUS "To build the RISC-V target:")
    message(STATUS "    mkdir -p build_riscv && cd build_riscv")
    message(STATUS "    cmake -DBUILD_RISCV=ON ..")
    message(STATUS "    make")
    message(STATUS "====================================================================")
    message(STATUS "")
endif()

# Display build configuration
message(STATUS "")
message(STATUS "======================= Build Configuration Notes =====================")
message(STATUS "Debugger support: ${DEBUGGER_ENABLED}")
if(DEBUGGER_ENABLED)
    message(STATUS "- WITH_DEBUGGER flag is set for CPU and debugger modules")
endif()
message(STATUS "====================================================================")
message(STATUS "") 