# Devices Module
project(devices)

# Find source files in the root directory and subdirectories
file(GLOB SOURCES "*.c")
file(GLOB HEADERS "*.h")

# Add device-specific source files
file(GLOB PANEL_SOURCES "panel/*.c")
file(GLOB RTC_SOURCES "rtc/*.c")
file(GLOB TERMINAL_SOURCES "terminal/*.c")
file(GLOB PAPERTAPE_SOURCES "papertape/*.c")
file(GLOB FLOPPY_SOURCES "floppy/*.c")
file(GLOB SMD_SOURCES "smd/*.c")
file(GLOB WINCHESTER_SOURCES "winchester/*.c")
file(GLOB SCSI_SOURCES "scsi/*.c")
file(GLOB DRUM_SOURCES "drum/*.c")
file(GLOB CDC_SOURCES "cdc/*.c")
file(GLOB HDLC_SOURCES "hdlc/*.c")
file(GLOB LINEPRINTER_SOURCES "lineprinter/*.c")
file(GLOB PAPERTAPEWRITER_SOURCES "papertapewriter/*.c")
file(GLOB OCTOBUS_SOURCES "octobus/*.c")

# Combine all sources
list(APPEND SOURCES 
    ${PANEL_SOURCES}
    ${OCTOBUS_SOURCES} 
    ${RTC_SOURCES} 
    ${TERMINAL_SOURCES} 
    ${PAPERTAPE_SOURCES} 
    ${FLOPPY_SOURCES}
    ${SMD_SOURCES}
    ${WINCHESTER_SOURCES}
    ${SCSI_SOURCES}
    ${DRUM_SOURCES}
    ${CDC_SOURCES}
    ${HDLC_SOURCES}
    ${LINEPRINTER_SOURCES}
    ${PAPERTAPEWRITER_SOURCES}
)

# Generate prototypes with mktypes
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_COMMAND} -E echo "/* AUTO-GENERATED FILE. DO NOT EDIT! */" > ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add root directory sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/device.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/devicemanager.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add panel device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/panel/panel.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add RTC device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/rtc/device_rtc.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add terminal device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/terminal/device_terminal.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add papertape device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/papertape/device_paper_tape.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add floppy device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/floppy/device_floppy_dma.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/floppy/device_floppy_pio.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add SMD device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/smd/device_smd.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/smd/disk_smd.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add Winchester device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/winchester/device_winchester.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/winchester/disk_winchester.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add SCSI device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/scsi/scsi_bus.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/scsi/ncr5386.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/scsi/scsi_device.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/scsi/disk_scsi.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/scsi/scsi_hdd.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/scsi/device_scsi.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add octobus device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/octobus/device_octobus.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add drum device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/drum/device_drum.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add CDC system-disc device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/cdc/device_cdc.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add line printer device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/lineprinter/device_line_printer.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add paper tape writer device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/papertapewriter/device_paper_tape_writer.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add HDLC device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/device_hdlc.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/chip_com5025.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/chip_com5025_registers.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/hdlc_crc.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/modem.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/dma_engine.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/dma_dcb.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/dma_control_blocks.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/dma_param_buf.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/dma_transmitter.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/tcp_receive_buffer.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/dma_receiver.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    DEPENDS ${SOURCES}
    COMMENT "Generating prototypes for devices"
    VERBATIM
)

# Create library
add_library(devices STATIC ${SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h)
nd100x_apply_house_standard(devices)
# scsi/ncr5386.c is a port of MAME's ncr5385 (BSD-3-Clause) and is exempt from
# the house standard; its printf-style logger trips clang's -Wformat-nonliteral.
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/scsi/ncr5386.c
    PROPERTIES COMPILE_OPTIONS "-Wno-format-nonliteral")

# Include directories
target_include_directories(devices
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_SOURCE_DIR}/src/ndlib
    PRIVATE ${CMAKE_SOURCE_DIR}/src/cpu
)

# Add specific device include directories
target_include_directories(devices PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/panel
    ${CMAKE_CURRENT_SOURCE_DIR}/rtc
    ${CMAKE_CURRENT_SOURCE_DIR}/terminal
    ${CMAKE_CURRENT_SOURCE_DIR}/papertape
    ${CMAKE_CURRENT_SOURCE_DIR}/floppy
    ${CMAKE_CURRENT_SOURCE_DIR}/smd
    ${CMAKE_CURRENT_SOURCE_DIR}/scsi
    ${CMAKE_CURRENT_SOURCE_DIR}/drum
    ${CMAKE_CURRENT_SOURCE_DIR}/cdc
    ${CMAKE_CURRENT_SOURCE_DIR}/hdlc
    ${CMAKE_CURRENT_SOURCE_DIR}/lineprinter
    ${CMAKE_CURRENT_SOURCE_DIR}/papertapewriter
)

# Link dependencies
target_link_libraries(devices
    PUBLIC ndlib
    PUBLIC cpu
)

# Set properties
set_target_properties(devices PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

