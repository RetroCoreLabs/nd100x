# Debugger Module
project(debugger)


# Find source files
file(GLOB SOURCES "*.c")
file(GLOB HEADERS "*.h")

# Explicitly list source files
set(DEBUGGER_SOURCE_FILES
    debugger.c
)

# Add include directories - locate libdap if present
set(LIBDAP_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/external/libdap/libdap/include")
set(NDLIB_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/src/ndlib")

# Generate prototypes with mktypes
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/debugger_protos.h
    COMMAND ${CMAKE_COMMAND} -E echo "/* AUTO-GENERATED FILE. DO NOT EDIT! */" > ${CMAKE_CURRENT_SOURCE_DIR}/debugger_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/debugger.c >> ${CMAKE_CURRENT_SOURCE_DIR}/debugger_protos.h
    DEPENDS ${SOURCES}
    COMMENT "Generating prototypes for debugger"
    VERBATIM
)

# Create library
add_library(debugger STATIC ${SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/debugger_protos.h)
nd100x_apply_house_standard(debugger)

# Include directories
target_include_directories(debugger
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC ${CMAKE_SOURCE_DIR}/src/ndlib
    PRIVATE ${CMAKE_SOURCE_DIR}/src/cpu
    PRIVATE ${CMAKE_SOURCE_DIR}/src/machine
    PRIVATE ${CMAKE_SOURCE_DIR}/src/devices
)

# Link dependencies
target_link_libraries(debugger
    PUBLIC ndlib
    PUBLIC cpu
    PUBLIC machine
    PRIVATE cjson_objects
)

# Only define WITH_DEBUGGER when globally enabled
if(DEBUGGER_ENABLED)
    target_compile_definitions(debugger PRIVATE
        WITH_DEBUGGER
    )
    message(STATUS "Debugger module: WITH_DEBUGGER flag set")

    if(PLATFORM_WASM)
        # WASM: Include libdap/libsymbols headers for type definitions only (no linking)
        if(EXISTS "${CMAKE_SOURCE_DIR}/external/libdap/CMakeLists.txt")
            target_include_directories(debugger
                PRIVATE ${LIBDAP_INCLUDE_DIR}
            )
            message(STATUS "Debugger module: DAP headers included for WASM (no linking)")
        endif()

        if(EXISTS "${CMAKE_SOURCE_DIR}/external/libsymbols")
            set(LIBSYMBOLS_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/external/libsymbols/include")
            target_include_directories(debugger
                PRIVATE ${LIBSYMBOLS_INCLUDE_DIR}
            )
            # Link libsymbols for WASM (symbols_objects has no pthread dependency)
            target_link_libraries(debugger
                PRIVATE symbols_objects
            )
            message(STATUS "Debugger module: Symbol support enabled for WASM")
        endif()
    else()
        # Native: Full DAP and symbols library linking.
        # Gate on the target existing, which is set/cleared by the root
        # CMakeLists based on submodule presence and platform support.
        # Filesystem-existence is not sufficient — on Windows libdap is
        # present but skipped because its source is POSIX-only.
        if(TARGET dap_objects)
            # Include libdap
            target_include_directories(debugger
                PRIVATE ${LIBDAP_INCLUDE_DIR}
            )

            # Link with libdap - change from dap to dap_objects
            target_link_libraries(debugger
                PRIVATE dap_objects
                PRIVATE cjson_objects
            )

            # Add DAP_ENABLED definition
            target_compile_definitions(debugger
                PRIVATE DAP_ENABLED=1
            )

            message(STATUS "Debugger module: DAP support enabled (using libdap)")
        else()
            message(STATUS "Debugger module: DAP support disabled (libdap not built for this target)")
        endif()

        # Check if libsymbols target was built (may be absent on Windows etc.)
        if(TARGET symbols_objects)
            # Set libsymbols include directory
            set(LIBSYMBOLS_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/external/libsymbols/include")

            # Include libsymbols
            target_include_directories(debugger
                PRIVATE ${LIBSYMBOLS_INCLUDE_DIR}
            )

            # Link with libsymbols
            target_link_libraries(debugger
                PRIVATE symbols_objects
            )

            message(STATUS "Debugger module: Symbol support enabled (using libsymbols)")
        else()
            message(STATUS "Debugger module: Symbol support disabled (libsymbols not found)")
        endif()
    endif()

else()
    message(STATUS "Debugger module: WITH_DEBUGGER flag not set, using stub implementations")
endif()

# Set properties
set_target_properties(debugger PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

