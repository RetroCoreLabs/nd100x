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

# Combine all sources
list(APPEND SOURCES 
    ${PANEL_SOURCES} 
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
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/rtc/deviceRTC.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add terminal device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/terminal/deviceTerminal.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add papertape device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/papertape/devicePapertape.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add floppy device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/floppy/deviceFloppyDMA.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/floppy/deviceFloppyPIO.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add SMD device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/smd/deviceSMD.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/smd/diskSMD.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add Winchester device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/winchester/deviceWinchester.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/winchester/diskWinchester.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add SCSI device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/scsi/scsiBus.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/scsi/ncr5386.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/scsi/scsiDevice.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/scsi/diskSCSI.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/scsi/scsiHDD.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/scsi/deviceSCSI.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add drum device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/drum/deviceDrum.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add CDC system-disc device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/cdc/deviceCDC.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add line printer device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/lineprinter/deviceLinePrinter.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add paper tape writer device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/papertapewriter/devicePaperTapeWriter.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    # Add HDLC device sources
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/deviceHDLC.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/chipCOM5025.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/chipCOM5025Registers.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/hdlc_crc.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/modem.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/dmaEngine.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/dmaDCB.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/dmaControlBlocks.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/dmaParamBuf.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/dmaTransmitter.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/tcpReceiveBuffer.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/hdlc/dmaReceiver.c >> ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h
    DEPENDS ${SOURCES}
    COMMENT "Generating prototypes for devices"
    VERBATIM
)

# Create library
add_library(devices STATIC ${SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/devices_protos.h)
nd100x_apply_house_standard(devices)

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

# Platform-specific configurations
if(PLATFORM_LINUX)
    target_compile_definitions(devices PRIVATE PLATFORM_LINUX)
elseif(PLATFORM_WINDOWS)
    target_compile_definitions(devices PRIVATE PLATFORM_WINDOWS)
elseif(PLATFORM_WASM)
    target_compile_definitions(devices PRIVATE PLATFORM_WASM)
endif()
