# House C standard: language level and warning set for the project's own targets.
#
# Applied per target, not through CMAKE_C_STANDARD / CMAKE_C_FLAGS, so that the
# vendored code under external/ and the optional nd500x subdirectory keep the
# settings their own build files choose.
#
#   nd100x_apply_house_standard(<target>)
#
# pins the target to C11 with GNU extensions (the compiler default moves between
# compiler releases - gcc 15 defaults to gnu23) and turns on the Tier A warning
# set. HOUSE_WERROR=ON additionally makes every warning an error; that is meant
# for the CI job only, because a blanket -Werror ties the build to one compiler
# version.

option(HOUSE_WERROR "Treat all house-standard warnings as errors (CI only)" OFF)

set(HOUSE_TIER_A_WARNINGS
    -Wall
    -Wextra
    -Wformat=2
    -Wimplicit-fallthrough
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wold-style-definition
    -Wshadow
    -Wpointer-arith
    -Wundef
    -Wvla
    -Wwrite-strings
    -Werror=implicit-function-declaration
    -Werror=incompatible-pointer-types
    -Werror=int-conversion
    -Werror=format-security
)

function(nd100x_apply_house_standard target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS ON
    )
    # C only: the flag set contains options a C++ compiler rejects.
    foreach(flag IN LISTS HOUSE_TIER_A_WARNINGS)
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:${flag}>)
    endforeach()
    if(HOUSE_WERROR)
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:-Werror>)
    endif()
endfunction()

# Hot-path trace output in the CPU/MMS code (log categories mms, mmsmap, trap,
# pkswitch). The trace code is always compiled; with OFF the compiler removes
# it. Default: ON for native Debug builds, OFF for Release, WASM and RISC-V
# (decided by Ronny 18-SEP-2026 after measuring the cost).
if(NOT DEFINED ND100X_HOT_TRACE)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT BUILD_WASM AND NOT BUILD_RISCV)
        set(_hot_trace_default ON)
    else()
        set(_hot_trace_default OFF)
    endif()
    option(ND100X_HOT_TRACE "Compile in the CPU/MMS hot-path trace output" ${_hot_trace_default})
endif()
