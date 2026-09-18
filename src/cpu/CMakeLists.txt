# CPU Module
project(cpu)

# Find source files
file(GLOB SOURCES "*.c")
file(GLOB HEADERS "*.h")

# List specific source files instead of using wildcard
set(CPU_SOURCE_FILES
    cpu.c
    cpu_instr.c
    cpu_mopc.c
    cpu_disasm.c
    float.c
    bcd.c
    cpu_regs.c
    cpu_mms.c
    cpu_bkpt.c
    expr_eval.c
)

# Generate prototypes with mktypes - use string replacement instead of wildcard
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/cpu_protos.h
    COMMAND ${CMAKE_COMMAND} -E echo "/* AUTO-GENERATED FILE. DO NOT EDIT! */" > ${CMAKE_CURRENT_SOURCE_DIR}/cpu_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/cpu.c >> ${CMAKE_CURRENT_SOURCE_DIR}/cpu_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/cpu_instr.c >> ${CMAKE_CURRENT_SOURCE_DIR}/cpu_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/cpu_mopc.c >> ${CMAKE_CURRENT_SOURCE_DIR}/cpu_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/cpu_disasm.c >> ${CMAKE_CURRENT_SOURCE_DIR}/cpu_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/float.c >> ${CMAKE_CURRENT_SOURCE_DIR}/cpu_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/bcd.c >> ${CMAKE_CURRENT_SOURCE_DIR}/cpu_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/cpu_regs.c >> ${CMAKE_CURRENT_SOURCE_DIR}/cpu_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/cpu_mms.c >> ${CMAKE_CURRENT_SOURCE_DIR}/cpu_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/cpu_bkpt.c >> ${CMAKE_CURRENT_SOURCE_DIR}/cpu_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/expr_eval.c >> ${CMAKE_CURRENT_SOURCE_DIR}/cpu_protos.h
    DEPENDS ${SOURCES}
    COMMENT "Generating prototypes for CPU"
    VERBATIM
)

# Create library
add_library(cpu STATIC ${SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/cpu_protos.h)
nd100x_apply_house_standard(cpu)
if(ND100X_HOT_TRACE)
    target_compile_definitions(cpu PRIVATE ND100X_HOT_TRACE=1)
endif()

# Include directories
target_include_directories(cpu
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_SOURCE_DIR}/src/ndlib
)

# Link dependencies
target_link_libraries(cpu
    PUBLIC ndlib
    PRIVATE m
)

# Windows: link winmm for timeBeginPeriod(1) used by sleep_ms() to raise
# the system timer resolution from ~15.6ms default to 1ms.
if(PLATFORM_WINDOWS)
    target_link_libraries(cpu PUBLIC winmm)
endif()

# Add debugger support for Linux and Windows builds when the root project
# has not explicitly disabled it via -DDEBUGGER_ENABLED=OFF.
if((PLATFORM_LINUX OR PLATFORM_WINDOWS) AND DEBUGGER_ENABLED)
    target_compile_definitions(cpu PRIVATE WITH_DEBUGGER)
    message(STATUS "CPU module: Debugger support enabled")
endif()

# Set properties
set_target_properties(cpu PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
