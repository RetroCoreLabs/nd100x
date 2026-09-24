# Printer module unit tests
#
# POSIX-only: the printer tests use mkdtemp, which MinGW does not provide, so
# they are skipped on Windows (see the note in the root CMakeLists.txt).
if(NOT PLATFORM_WINDOWS)
    add_executable(test_printer
        test_printer_main.c
        test_pdfwriter.c
        test_escp.c
        test_printjob.c
    )

    target_link_libraries(test_printer PRIVATE ndlib m)

    target_include_directories(test_printer PRIVATE
        ${CMAKE_SOURCE_DIR}/src/ndlib
    )

    add_test(NAME printer_tests COMMAND test_printer)
endif()

# HDLC module unit tests

add_executable(test_hdlc
    test_hdlc_main.c
    test_tcp_receive_buffer.c
    test_hdlc_crc.c
    test_hdlc_frame.c
    ${CMAKE_SOURCE_DIR}/src/devices/hdlc/tcp_receive_buffer.c
    ${CMAKE_SOURCE_DIR}/src/devices/hdlc/hdlc_frame.c
    ${CMAKE_SOURCE_DIR}/src/devices/hdlc/hdlc_crc.c
)

target_include_directories(test_hdlc PRIVATE
    ${CMAKE_SOURCE_DIR}/src/devices/hdlc
    ${CMAKE_SOURCE_DIR}/src/devices
    ${CMAKE_SOURCE_DIR}/src/ndlib
    ${CMAKE_SOURCE_DIR}/src/cpu
)

target_link_libraries(test_hdlc PRIVATE m)

add_test(NAME hdlc_tests COMMAND test_hdlc)

# ---------------------------------------------------------------------------
# nd100x --pipe TPE INSTRUCTION regression gate (ND-120), driven by tools/nd100x_expect.py
# The floppy path comes from -DNDX_TPE_FLOPPY=... (or $ND100X_TPE_FLOPPY, or the
# script default). If Python is missing the test is not registered; if the image is
# missing the script exits 2, which CMake treats as SKIPPED (SKIP_RETURN_CODE), so a
# machine without the (non-redistributable) TPE image never sees a red failure.
if(NOT PLATFORM_WASM AND NOT PLATFORM_RISCV)
    find_program(PYTHON_EXECUTABLE NAMES python3 python)
    if(PYTHON_EXECUTABLE)
        set(NDX_TPE_FLOPPY "" CACHE FILEPATH "TPE diagnostic floppy image for the nd100x pipe regression test")
        add_test(
            NAME nd120_tpe_pipe
            COMMAND ${PYTHON_EXECUTABLE}
                    ${CMAKE_SOURCE_DIR}/tools/examples/tpe_instruction_nd120.py
                    ${NDX_TPE_FLOPPY}
        )
        set_tests_properties(nd120_tpe_pipe PROPERTIES
            SKIP_RETURN_CODE 2      # exit 2 = image not found -> SKIPPED, not failed
            TIMEOUT 180             # a full TPE boot + INSTRUCTION run
            LABELS "integration;nd120;pipe"
        )
    else()
        message(STATUS "Python not found - skipping the nd120_tpe_pipe integration test")
    endif()
endif()

# 32-bit FPP instruction-level test (handoff item 6.8)
#
# Generates a tiny BPUN on the fly, boots it in the REAL emulator with --trace
# in both --fpp=32 and default (48-bit) mode, and proves from the trace that the
# 32-bit operand fetch reads exactly TWO words (poison at EA+2) and that NLZ
# leaves T untouched. Skips (exit 2) if the nd100x binary is missing.
if(NOT PLATFORM_WASM AND NOT PLATFORM_RISCV)
    find_program(PYTHON_EXECUTABLE NAMES python3 python)
    if(PYTHON_EXECUTABLE)
        add_test(
            NAME fpp32_instr
            COMMAND ${PYTHON_EXECUTABLE}
                    ${CMAKE_SOURCE_DIR}/tests/test_fpp32_instr.py
                    $<TARGET_FILE:nd100x>
        )
        set_tests_properties(fpp32_instr PROPERTIES
            SKIP_RETURN_CODE 2
            TIMEOUT 120
            LABELS "integration;fpp"
        )
    endif()
endif()

# Floppy/disk catalog API tests
#
# floppydb.c is linked in DIRECTLY together with the bundled cJSON parser and a
# stub download_file() provided by test_floppydb.c, so the catalog parse/search
# logic is exercised WITHOUT the network (libcurl) or the machine. This is why the
# test runs on every build, including Windows w64devkit where libcurl is absent.
add_executable(test_floppydb
    test_floppydb.c
    ${CMAKE_SOURCE_DIR}/src/ndlib/floppydb.c
    ${CMAKE_SOURCE_DIR}/external/cJSON/cJSON.c
)

target_include_directories(test_floppydb PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ndlib
    ${CMAKE_SOURCE_DIR}/src/machine
    ${CMAKE_SOURCE_DIR}/src/cpu
    ${CMAKE_SOURCE_DIR}/external/cJSON
)

target_link_libraries(test_floppydb PRIVATE m)

if(PLATFORM_WINDOWS)
    # machine_types.h -> cpu_types.h pulls in windows.h / timeBeginPeriod().
    target_link_libraries(test_floppydb PRIVATE winmm)
endif()

add_test(NAME floppydb_tests COMMAND test_floppydb)

# DRUM device unit tests
#
# device_drum.c is linked in DIRECTLY together with fake Device_* infrastructure
# provided by test_drum.c (fake DMA memory + delayed-IO), so the drum's register
# handlers and transfer engine are exercised without the machine or a disk image.
add_executable(test_drum
    test_drum.c
    ${CMAKE_SOURCE_DIR}/src/devices/drum/device_drum.c
)

target_include_directories(test_drum PRIVATE
    ${CMAKE_SOURCE_DIR}/src/devices/drum
    ${CMAKE_SOURCE_DIR}/src/devices
    ${CMAKE_SOURCE_DIR}/src/ndlib
    ${CMAKE_SOURCE_DIR}/src/cpu
)

target_link_libraries(test_drum PRIVATE m)

# device_drum.c #includes the generated devices_protos.h; force the devices
# target (which runs mkptypes to emit it) to build first.
add_dependencies(test_drum devices)

add_test(NAME drum_tests COMMAND test_drum)

# CDC system-disc device unit tests
#
# device_cdc.c is linked in DIRECTLY together with fake Device_* infrastructure
# provided by test_cdc.c (fake DMA memory + delayed-IO), so the disc's register
# handlers, transfer engine and backing-file logic are exercised without the
# machine or the CPU memory subsystem.
add_executable(test_cdc
    test_cdc.c
    ${CMAKE_SOURCE_DIR}/src/devices/cdc/device_cdc.c
)

target_include_directories(test_cdc PRIVATE
    ${CMAKE_SOURCE_DIR}/src/devices/cdc
    ${CMAKE_SOURCE_DIR}/src/devices
    ${CMAKE_SOURCE_DIR}/src/ndlib
    ${CMAKE_SOURCE_DIR}/src/cpu
)

target_link_libraries(test_cdc PRIVATE m)

# device_cdc.c #includes the generated devices_protos.h; force the devices
# target (which runs mkptypes to emit it) to build first.
add_dependencies(test_cdc devices)

add_test(NAME cdc_tests COMMAND test_cdc)
# CPU packed-decimal (BCD) instruction tests
#
# bcd.c is linked in DIRECTLY (not via libcpu) together with fake
# MemoryRead/MemoryWrite and a fake register file provided by test_bcd.c, so the
# decimal engine is exercised through its real ndfunc_* entry points without
# pulling in the machine, the devices or a disk image.
add_executable(test_bcd
    test_bcd.c
    ${CMAKE_SOURCE_DIR}/src/cpu/bcd.c
)

target_include_directories(test_bcd PRIVATE
    ${CMAKE_SOURCE_DIR}/src/cpu
    ${CMAKE_SOURCE_DIR}/src/ndlib
)

target_link_libraries(test_bcd PRIVATE m)

if(PLATFORM_WINDOWS)
    # cpu_types.h pulls in windows.h and timeBeginPeriod() for sleep_ms().
    target_link_libraries(test_bcd PRIVATE winmm)
endif()

# bcd.c #includes the generated cpu_protos.h; force the cpu target (which
# runs mkptypes to emit it) to build first.
add_dependencies(test_bcd cpu)

add_test(NAME bcd_tests COMMAND test_bcd)

# CPU floating point tests
#
# float.c is linked in DIRECTLY (not via libcpu) together with a fake register
# file and a fake setbit() provided by test_float.c, so the FP engine is
# exercised without the machine, the devices or a disk image. Contains the
# 48-bit regression lock (captured 2026-07-27) plus the 32-bit FPP tests.
add_executable(test_float
    test_float.c
    ${CMAKE_SOURCE_DIR}/src/cpu/float.c
)

target_include_directories(test_float PRIVATE
    ${CMAKE_SOURCE_DIR}/src/cpu
    ${CMAKE_SOURCE_DIR}/src/ndlib
)

target_link_libraries(test_float PRIVATE m)

if(PLATFORM_WINDOWS)
    # cpu_types.h pulls in windows.h and timeBeginPeriod() for sleep_ms().
    target_link_libraries(test_float PRIVATE winmm)
endif()

# float.c #includes the generated cpu_protos.h; force the cpu target (which
# runs mkptypes to emit it) to build first.
add_dependencies(test_float cpu)

add_test(NAME float_tests COMMAND test_float)

# Machine config (INI model) unit tests
#
# machine_config.c is linked in DIRECTLY; it is self-contained (headers only),
# so the [machine] fpp key's defaults / round trip / validation are exercised
# without the machine or any device. POSIX-only (mkdtemp), like test_printer.
if(NOT PLATFORM_WINDOWS)
    add_executable(test_machine_config
        test_machine_config.c
        ${CMAKE_SOURCE_DIR}/src/machine/machine_config.c
        # The JSON export is the browser's view of the SAME config, so it is
        # tested beside the parser rather than left to be noticed in a UI.
        ${CMAKE_SOURCE_DIR}/src/machine/machine_config_json.c
        # machine_config.c resolves 'cpu = ND110CX' through the model table.
        # A table of names, nothing else - it keeps this test free of the
        # machine and the devices, which is the point of building it this way.
        ${CMAKE_SOURCE_DIR}/src/cpu/cpu_model.c
    )

    target_include_directories(test_machine_config PRIVATE
        ${CMAKE_SOURCE_DIR}/src/machine
        ${CMAKE_SOURCE_DIR}/src/cpu
        ${CMAKE_SOURCE_DIR}/src/ndlib
        ${CMAKE_SOURCE_DIR}/src/devices
        ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(test_machine_config PRIVATE m)

    # machine_config.h -> machine_types.h #includes the generated cpu_protos.h;
    # force the cpu target (which runs mkptypes to emit it) to build first.
    add_dependencies(test_machine_config cpu)

    add_test(NAME machine_config_tests COMMAND test_machine_config)
endif()

# RTC time-base unit tests
#
# device_rtc.c is linked in DIRECTLY with the four Device_* helpers stubbed in
# the test, so no device manager or machine is involved. Verifies the ticks
# mode pulse period (10550 calls) and the wall mode ~50 Hz host-time rate.
# POSIX-only (clock_gettime/nanosleep usage in the test), like test_printer.
if(NOT PLATFORM_WINDOWS)
    add_executable(test_rtc
        test_rtc.c
        ${CMAKE_SOURCE_DIR}/src/devices/rtc/device_rtc.c
    )

    target_include_directories(test_rtc PRIVATE
        ${CMAKE_SOURCE_DIR}/src/devices
        ${CMAKE_SOURCE_DIR}/src/ndlib
        ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(test_rtc PRIVATE m)

    # device_rtc.c #includes the generated devices_protos.h; force the devices
    # target (which runs mkptypes to emit it) to build first.
    add_dependencies(test_rtc devices)

    add_test(NAME rtc_tests COMMAND test_rtc)
endif()

# Shell unit tests
#
# test_shell.c #includes the REAL src/frontend/nd100x/nd100x_shell.c so the
# file-static command functions are tested exactly as shipped; program_load()
# and the gReg register file are stubbed inside the test, so no machine/CPU
# library is linked. HAVE_READLINE is intentionally left undefined so the shell
# uses its plain-fgets input path.
add_executable(test_shell
    test_shell.c
)

target_include_directories(test_shell PRIVATE
    ${CMAKE_SOURCE_DIR}/src/frontend/nd100x
    ${CMAKE_SOURCE_DIR}/src/machine
    ${CMAKE_SOURCE_DIR}/src/cpu
    ${CMAKE_SOURCE_DIR}/src/ndlib
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(test_shell PRIVATE m)

if(PLATFORM_WINDOWS)
    # cpu_types.h pulls in windows.h and timeBeginPeriod() for sleep_ms().
    target_link_libraries(test_shell PRIVATE winmm)
endif()

# nd100x_shell.c #includes the generated ndlib_protos.h, machine_protos.h and
# cpu_protos.h; force the targets that run mkptypes to build first.
add_dependencies(test_shell cpu machine_protos_gen ndlib)

add_test(NAME shell_tests COMMAND test_shell)

# :PROG loader unit tests
#
# test_load_prog.c #includes the REAL src/ndlib/load_prog.c and stubs
# WritePhysicalMemory + the disasm hook, so the header parse and bank load math
# are tested without the machine or CPU.
add_executable(test_load_prog
    test_load_prog.c
)

target_include_directories(test_load_prog PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ndlib
    ${CMAKE_SOURCE_DIR}/src/cpu
    ${CMAKE_SOURCE_DIR}/src/machine
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(test_load_prog PRIVATE m)

if(PLATFORM_WINDOWS)
    target_link_libraries(test_load_prog PRIVATE winmm)
endif()

# load_prog.c #includes the generated ndlib_protos.h; force the ndlib target
# (which runs mkptypes to emit it) to build first.
add_dependencies(test_load_prog ndlib)

add_test(NAME load_prog_tests COMMAND test_load_prog)


# Winchester disc controller unit tests
#
# device_winchester.c + disk_winchester.c are linked in DIRECTLY together with the
# fake Device_* infrastructure in test_winchester.c, so the controller's real
# register handlers and transfer engine run without the machine or the CPU
# memory subsystem. Same pattern as the CDC and drum tests above.
add_executable(test_winchester
    test_winchester.c
    ${CMAKE_SOURCE_DIR}/src/devices/winchester/device_winchester.c
    ${CMAKE_SOURCE_DIR}/src/devices/winchester/disk_winchester.c
)

target_include_directories(test_winchester PRIVATE
    ${CMAKE_SOURCE_DIR}/src/devices/winchester
    ${CMAKE_SOURCE_DIR}/src/devices
    ${CMAKE_SOURCE_DIR}/src/ndlib
    ${CMAKE_SOURCE_DIR}/src/cpu
)

target_link_libraries(test_winchester PRIVATE m)

add_dependencies(test_winchester devices)

add_test(NAME winchester_tests COMMAND test_winchester)

# Winchester conformance-trace driver.
#
# Walks the shared sequence in wd_conformance_seq.h against this repository's
# synchronous Winchester model and prints a canonical trace. Its counterpart
# is the portable Pi Pico core's test/wd_trace.c, which walks a byte-identical
# copy of that header against an asynchronous phase machine. DIFFING the two
# traces is what proves the port is bit-identical rather than merely similar.
#
# Deliberately NOT an add_test(): it has no verdict of its own - the diff is
# the verdict, and the other half of it lives in a different repository.
add_executable(wd_trace
    wd_trace.c
    ${CMAKE_SOURCE_DIR}/src/devices/winchester/device_winchester.c
    ${CMAKE_SOURCE_DIR}/src/devices/winchester/disk_winchester.c
)

target_include_directories(wd_trace PRIVATE
    ${CMAKE_SOURCE_DIR}/src/devices/winchester
    ${CMAKE_SOURCE_DIR}/src/devices
    ${CMAKE_SOURCE_DIR}/src/ndlib
    ${CMAKE_SOURCE_DIR}/src/cpu
)

target_link_libraries(wd_trace PRIVATE m)

add_dependencies(wd_trace devices)

# ---------------------------------------------------------------------------
# machine-profiles.js - the named machine configuration store for the Glass UI.
# A node test, not a headless-browser one like test_glass_windows: the module
# touches nothing but window and localStorage, both stubbed in the test, so a
# browser would only add a dependency CI may not have. Skipped, not failed,
# where node is absent - a missing tool is not a broken store.
# ---------------------------------------------------------------------------
find_program(NODE_EXECUTABLE NAMES node nodejs)
if(NODE_EXECUTABLE)
    add_test(NAME machine_profiles_tests
             COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_machine_profiles.js)
    # machine-form.js carries over every INI section the form does not edit.
    # Get that wrong and Save deletes [runtime] - telnet port, throttle,
    # charset, drum and CDC images, memory size - with nothing to say so.
    add_test(NAME machine_form_tests
             COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_machine_form.js)
else()
    message(STATUS "tests: node not found - skipping machine_profiles_tests")
endif()

# Logger unit tests (category/level logger in src/ndlib/log.c)
add_executable(test_log
    test_log.c
    ${CMAKE_SOURCE_DIR}/src/ndlib/log.c
)
target_include_directories(test_log PRIVATE ${CMAKE_SOURCE_DIR}/src/ndlib)
if(NOT BUILD_WASM)
    find_package(Threads REQUIRED)
    target_link_libraries(test_log PRIVATE Threads::Threads)
endif()
add_test(NAME log_tests COMMAND test_log)

# Tests that compile source files on their own (without the ndlib library,
# which contains the logger) need log.c for the LOG() calls in those files.
get_property(_log_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_t IN LISTS _log_test_targets)
    get_target_property(_type ${_t} TYPE)
    get_target_property(_libs ${_t} LINK_LIBRARIES)
    if(_type STREQUAL "EXECUTABLE" AND NOT _t STREQUAL "test_log" AND NOT "ndlib" IN_LIST _libs)
        target_sources(${_t} PRIVATE ${CMAKE_SOURCE_DIR}/src/ndlib/log.c)
        if(NOT BUILD_WASM)
            target_link_libraries(${_t} PRIVATE Threads::Threads)
        endif()
    endif()
endforeach()

# House C standard for every test target defined in this directory.
get_property(_house_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_t IN LISTS _house_test_targets)
    get_target_property(_type ${_t} TYPE)
    if(_type STREQUAL "EXECUTABLE")
        nd100x_apply_house_standard(${_t})
        # The tests check with assert(); a Release build defines NDEBUG and
        # would compile every check away, so keep them in all build types.
        target_compile_options(${_t} PRIVATE -UNDEBUG)
    endif()
endforeach()

# Physical memory bank table + LOCAL/MPM5 ECC probe unit tests
#
# cpu_mms.c is linked in DIRECTLY (it is the file under test) ahead of libcpu, so
# the test always exercises the source tree and never a stale archive member. The
# rest of the emulator comes from the static libraries because the physical
# read/write path reaches cpu_interrupt() and the log; the test itself calls no
# device and never starts the CPU (it installs a register set by hand instead of
# cpu_init(), which would also build the paging and dispatch tables).
add_executable(test_memory_banks
    test_memory_banks_main.c
    test_memory_banks.c
    ${CMAKE_SOURCE_DIR}/src/cpu/cpu_mms.c
)

target_include_directories(test_memory_banks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/cpu
    ${CMAKE_SOURCE_DIR}/src/machine
    ${CMAKE_SOURCE_DIR}/src/devices
    ${CMAKE_SOURCE_DIR}/src/ndlib
)

# The link is a GROUP: cpu calls into devices and the debugger, devices calls
# back into machine, and machine calls cpu. A single pass over the archives in
# any order leaves some of those unresolved.
target_link_libraries(test_memory_banks PRIVATE
    -Wl,--start-group cpu machine devices debugger ndlib -Wl,--end-group m)

# cpu_types.h #includes the generated cpu_protos.h; force the cpu target (which
# runs mkptypes to emit it) to build first.
add_dependencies(test_memory_banks cpu)

add_test(NAME memory_bank_tests COMMAND test_memory_banks)

# ND-100 octobus interface card unit tests
#
# device_octobus.c is linked in DIRECTLY with device.c for dev_init, so no
# device manager and no machine are involved - a failure here is the card's.
# The addresses and ident codes are the point: see device_octobus.h for the two
# plausible ident claims that turned out to be wrong.
add_executable(test_octobus
    test_octobus.c
    ${CMAKE_SOURCE_DIR}/src/devices/octobus/device_octobus.c
    ${CMAKE_SOURCE_DIR}/src/devices/device.c
)

target_include_directories(test_octobus PRIVATE
    ${CMAKE_SOURCE_DIR}/src/devices
    ${CMAKE_SOURCE_DIR}/src/cpu
    ${CMAKE_SOURCE_DIR}/src/machine
    ${CMAKE_SOURCE_DIR}/src/ndlib
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_octobus PRIVATE -Wl,--start-group ndlib -Wl,--end-group m)
add_dependencies(test_octobus cpu)

add_test(NAME octobus_tests COMMAND test_octobus)

# The MFbus bridge: the ND-100 / ND-5000 shared-memory join.
#
# Only when the ND-500 is built - the bridge compiles to nothing otherwise. Uses
# the SAME group link as test_memory_banks, for the same reason: cpu, devices,
# machine and the debugger call into each other.
if(ND100X_WITH_ND500)
    add_executable(test_mfbus_bridge
        test_mfbus_bridge.c
        ${CMAKE_SOURCE_DIR}/src/cpu/cpu_mms.c
    )

    target_compile_definitions(test_mfbus_bridge PRIVATE ND100X_WITH_ND500=1)

    target_include_directories(test_mfbus_bridge PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/cpu
        ${CMAKE_SOURCE_DIR}/src/machine
        ${CMAKE_SOURCE_DIR}/src/devices
        ${CMAKE_SOURCE_DIR}/src/ndlib
    )

    target_link_libraries(test_mfbus_bridge PRIVATE
        -Wl,--start-group cpu machine devices debugger ndlib -Wl,--end-group
        nd500::ndbus nd500::machine nd500::cpu m)

    add_dependencies(test_mfbus_bridge cpu)

    add_test(NAME mfbus_bridge_tests COMMAND test_mfbus_bridge)
endif()
