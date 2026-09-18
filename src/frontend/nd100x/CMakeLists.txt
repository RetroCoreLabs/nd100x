# nd100x frontend executable
set(SOURCES
    config.c
    nd100x.c
    nd100x_shell.c
    screenmenu.c
    vscreen.c
    charset.c
)

# Include menu.c (F12 floppy-DB browser) when curl + ncurses are available.
# Excluded on WASM, RISC-V, and Windows (ncurses not available on Windows
# under w64devkit; port to PDCurses not done yet — phase 5).
if(NOT (PLATFORM_WASM OR BUILD_WASM OR PLATFORM_RISCV OR BUILD_RISCV OR PLATFORM_WINDOWS))
    list(APPEND SOURCES menu.c)
    set(NEED_MENU TRUE)
else()
    set(NEED_MENU FALSE)
endif()

# Create the executable
add_executable(nd100x ${SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/nd100x_protos.h)
nd100x_apply_house_standard(nd100x)

# Build-stamp header: version + git hash(+dirty) + build time, regenerated on
# EVERY build so `nd100x --version` always tells the truth about which source
# state the binary was built from (multiple build dirs diverge in practice).
# Costs one small recompile + relink per build; that is the point.
set(ND100X_VERSION_HEADER ${CMAKE_CURRENT_BINARY_DIR}/nd100x_version.h)
add_custom_target(nd100x_version_stamp
    COMMAND ${CMAKE_COMMAND}
        -DOUT=${ND100X_VERSION_HEADER}
        -DSRC_DIR=${CMAKE_SOURCE_DIR}
        -DVERSION=${nd100x_VERSION}
        -P ${CMAKE_SOURCE_DIR}/cmake/git_stamp.cmake
    BYPRODUCTS ${ND100X_VERSION_HEADER}
    COMMENT "Stamping nd100x_version.h (git hash + build time)")
add_dependencies(nd100x nd100x_version_stamp)

# Stage the vendored libcurl runtime next to the freshly built exe, for ANY build
# method (plain `cmake --build`, not just `make`). nd100x.exe hard-depends on
# libcurl-x64.dll at load time, so without this the exe fails to start with
# "libcurl-x64.dll: cannot open shared object file". copy_if_different = cheap no-op
# on rebuilds. Guarded on the vendored-curl path (empty on system-curl / no-curl).
if(DEFINED ND100X_VENDORED_CURL_BIN AND ND100X_VENDORED_CURL_BIN)
    add_custom_command(TARGET nd100x POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ND100X_VENDORED_CURL_BIN}/libcurl-x64.dll"    "$<TARGET_FILE_DIR:nd100x>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ND100X_VENDORED_CURL_BIN}/curl-ca-bundle.crt" "$<TARGET_FILE_DIR:nd100x>"
        COMMENT "Staging vendored libcurl runtime (libcurl-x64.dll + curl-ca-bundle.crt) next to nd100x")
endif()

# Include directories
target_include_directories(nd100x PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src/cpu
    ${CMAKE_SOURCE_DIR}/src/ndlib
    ${CMAKE_SOURCE_DIR}/src/devices
    ${CMAKE_SOURCE_DIR}/src/machine
    ${CMAKE_SOURCE_DIR}/src/debugger
)

if(NEED_MENU)
    target_include_directories(nd100x PRIVATE
        ${CURL_INCLUDE_DIRS}
        ${CURSES_INCLUDE_DIRS}
    )
endif()

# Link with required libraries
target_link_libraries(nd100x PRIVATE
    machine
    devices
    cpu
    ndlib
    debugger
    cjson_objects
)

if(NEED_MENU)
    target_link_libraries(nd100x PRIVATE
        ${CURL_LIBRARIES}
        ${CURSES_LIBRARIES}
    )
endif()

# Add debug flags as needed (matching Makefile)
target_compile_definitions(nd100x PRIVATE
    _GNU_SOURCE
    DEBUG
)

# If debugger is enabled, add external library dependencies
if(DEBUGGER_ENABLED )
    # If libdap is available, link with it
    if(EXISTS "${CMAKE_SOURCE_DIR}/external/libdap/CMakeLists.txt")
        target_include_directories(nd100x PRIVATE
            ${CMAKE_SOURCE_DIR}/external/libdap/libdap/include
        )
        target_link_libraries(nd100x PRIVATE
            dap_objects
        )
    endif()
endif()

# Link libsymbols for a.out support — only when the library was actually
# enabled in the root CMakeLists (skipped on Windows pending a port).
if(TARGET symbols_objects)
    target_include_directories(nd100x PRIVATE
        ${CMAKE_SOURCE_DIR}/external/libsymbols/include
    )
    target_link_libraries(nd100x PRIVATE
        symbols_objects
    )
endif()

# Copy config file to build output directory
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nd100x.conf
    ${CMAKE_BINARY_DIR}/etc/nd100x.conf COPYONLY)

# Install nd100x binary
install(TARGETS nd100x RUNTIME DESTINATION bin)

# Create the prototypes header using mkptypes
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/nd100x_protos.h
    COMMAND ${CMAKE_COMMAND} -E echo "/* AUTO-GENERATED FILE. DO NOT EDIT! */" > ${CMAKE_CURRENT_SOURCE_DIR}/nd100x_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/config.c >> ${CMAKE_CURRENT_SOURCE_DIR}/nd100x_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/nd100x.c >> ${CMAKE_CURRENT_SOURCE_DIR}/nd100x_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/menu.c >> ${CMAKE_CURRENT_SOURCE_DIR}/nd100x_protos.h
    DEPENDS ${SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/menu.c
    COMMENT "Generating nd100x_protos.h"
    VERBATIM
)