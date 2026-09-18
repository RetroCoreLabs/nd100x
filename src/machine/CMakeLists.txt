# Machine Module
project(machine)

# Find source files
file(GLOB SOURCES "*.c")
file(GLOB HEADERS "*.h")

# Explicitly list source files
set(MACHINE_SOURCE_FILES
    machine.c
    io.c
)

# Generate prototypes with mkptypes.
# The generated header is NOT listed as a source of add_library — ninja's
# dep analysis would otherwise tangle on Windows because machine PUBLIC-
# links cpu and cpu.c also includes machine_protos.h, producing a cycle:
#   machine_protos.h -> libcpu.a -> cpu.c.obj -> machine_protos.h
# Using add_custom_target + add_dependencies gives a clean target-level
# ordering without the header appearing in any library's source list.
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/machine_protos.h
    COMMAND ${CMAKE_COMMAND} -E echo "/* AUTO-GENERATED FILE. DO NOT EDIT! */" > ${CMAKE_CURRENT_SOURCE_DIR}/machine_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/machine.c >> ${CMAKE_CURRENT_SOURCE_DIR}/machine_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/io.c >> ${CMAKE_CURRENT_SOURCE_DIR}/machine_protos.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/machine.c ${CMAKE_CURRENT_SOURCE_DIR}/io.c
    COMMENT "Generating prototypes for machine"
    VERBATIM
)
add_custom_target(machine_protos_gen
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/machine_protos.h
)

# Create library
add_library(machine STATIC ${SOURCES})
nd100x_apply_house_standard(machine)
add_dependencies(machine machine_protos_gen)

# Include directories
target_include_directories(machine
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_SOURCE_DIR}/src/ndlib
    PRIVATE ${CMAKE_SOURCE_DIR}/src/cpu
    PRIVATE ${CMAKE_SOURCE_DIR}/src/devices
)

# Link dependencies
target_link_libraries(machine
    PUBLIC ndlib
    PUBLIC cpu
    PUBLIC devices
)

# Set properties
set_target_properties(machine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
