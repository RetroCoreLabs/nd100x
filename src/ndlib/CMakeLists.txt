# ND Library Module
project(ndlib)

# Find source files
file(GLOB SOURCES "*.c")
file(GLOB HEADERS "*.h")

# List specific source files instead of using wildcard
set(NDLIB_SOURCE_FILES
    load_bpun.c
    ndlib.c
    log.c
    download.c
    keyboard.c
    pdfwriter.c
    escp.c
    printjob.c
    net_compat.c
)

# Telnet server: POSIX and Windows use net_compat.h to reach sockets via the
# same code. Excluded only on WASM — the browser has no sockets or pthreads.
if(NOT (PLATFORM_WASM OR BUILD_WASM))
    list(APPEND NDLIB_SOURCE_FILES telnetserver.c)
else()
    list(FILTER SOURCES EXCLUDE REGEX "telnetserver\\.c$")
    list(FILTER SOURCES EXCLUDE REGEX "net_compat\\.c$")
endif()

# Generate prototypes with mktypes
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/ndlib_protos.h
    COMMAND cat ${CMAKE_CURRENT_SOURCE_DIR}/ndlib_protos_preamble.h > ${CMAKE_CURRENT_SOURCE_DIR}/ndlib_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/load_bpun.c >> ${CMAKE_CURRENT_SOURCE_DIR}/ndlib_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/load_prog.c >> ${CMAKE_CURRENT_SOURCE_DIR}/ndlib_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/ndlib.c >> ${CMAKE_CURRENT_SOURCE_DIR}/ndlib_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/log.c >> ${CMAKE_CURRENT_SOURCE_DIR}/ndlib_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/download.c >> ${CMAKE_CURRENT_SOURCE_DIR}/ndlib_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/keyboard.c >> ${CMAKE_CURRENT_SOURCE_DIR}/ndlib_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/pdfwriter.c >> ${CMAKE_CURRENT_SOURCE_DIR}/ndlib_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/escp.c >> ${CMAKE_CURRENT_SOURCE_DIR}/ndlib_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/printjob.c >> ${CMAKE_CURRENT_SOURCE_DIR}/ndlib_protos.h
    DEPENDS ${SOURCES} ${NDLIB_SOURCE_FILES}
    COMMENT "Generating prototypes for NDLIB"
    VERBATIM
)

# Create library
add_library(ndlib STATIC ${SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/ndlib_protos.h)
nd100x_apply_house_standard(ndlib)

# Include directories
target_include_directories(ndlib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    # floppydb.c parses the online floppy catalog with the bundled cJSON. cJSON is
    # linked into every final executable via the cjson_objects OBJECT library (see the
    # top-level CMakeLists); ndlib only needs its header on the include path to compile.
    PRIVATE ${CMAKE_SOURCE_DIR}/external/cJSON
)

# Set properties
set_target_properties(ndlib PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# Link pthread for the telnet server.
# - POSIX: libpthread.
# - Windows: winpthreads (bundled with MinGW-w64 gcc).  The `pthread` CMake
#   link flag resolves to libwinpthread on MinGW, so the same line works on
#   both platforms.
# - WASM: no pthreads in the browser.
if(NOT (PLATFORM_WASM OR BUILD_WASM))
    target_link_libraries(ndlib PUBLIC pthread)
endif()

# Winsock on Windows (ws2_32 provides socket(), WSAPoll, WSAStartup, etc.)
if(PLATFORM_WINDOWS)
    target_link_libraries(ndlib PUBLIC ws2_32)
endif()

# Link CURL only when the parent find_package(CURL) succeeded.
# On Windows CURL is optional (find_package is QUIET) and download_file()
# compiles as a stub (!defined(HAVE_CURL)) when curl is missing.
if(CURL_FOUND AND NOT (PLATFORM_WASM OR BUILD_WASM OR PLATFORM_RISCV OR BUILD_RISCV))
    if(TARGET CURL::libcurl)
        target_link_libraries(ndlib PUBLIC CURL::libcurl)
    else()
        if(DEFINED CURL_INCLUDE_DIRS)
            target_include_directories(ndlib PUBLIC ${CURL_INCLUDE_DIRS})
        endif()
        if(DEFINED CURL_LIBRARIES)
            target_link_libraries(ndlib PUBLIC ${CURL_LIBRARIES})
        endif()
    endif()
endif()

# C code tests the compiler's own platform macros (__EMSCRIPTEN__, _WIN32,
# __riscv, __linux__); the PLATFORM_* names are CMake variables only.
