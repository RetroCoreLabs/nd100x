# WebAssembly Frontend
project(nd100wasm)

# Find source files explicitly
set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/nd100wasm.c
)

# Generate prototypes with mktypes
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/nd100wasm_protos.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/mkptypes/mkptypes ${CMAKE_CURRENT_SOURCE_DIR}/nd100wasm.c > ${CMAKE_CURRENT_SOURCE_DIR}/nd100wasm_protos.h
    DEPENDS ${SOURCES}
    COMMENT "Generating prototypes for nd100wasm"
    VERBATIM
)

# Create executable
add_executable(nd100wasm ${SOURCES}
               ${CMAKE_CURRENT_SOURCE_DIR}/nd500_wasm.c
               ${CMAKE_CURRENT_SOURCE_DIR}/nd100wasm_protos.h
               ${ND100X_ND500X_DIR}/src/frontend/nd500x/ndix_ffs.c)
nd100x_apply_house_standard(nd100wasm)

# Include directories
target_include_directories(nd100wasm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/frontend/nd100x   # Include nd100x directory for nd100x_types.h
    ${CMAKE_SOURCE_DIR}/src/ndlib
    ${CMAKE_SOURCE_DIR}/src/cpu
    ${CMAKE_SOURCE_DIR}/src/devices
    ${CMAKE_SOURCE_DIR}/src/devices/panel
    ${CMAKE_SOURCE_DIR}/src/devices/rtc
    ${CMAKE_SOURCE_DIR}/src/devices/terminal
    ${CMAKE_SOURCE_DIR}/src/devices/papertape
    ${CMAKE_SOURCE_DIR}/src/devices/floppy
    ${CMAKE_SOURCE_DIR}/src/devices/smd
    ${CMAKE_SOURCE_DIR}/src/machine
    ${CMAKE_SOURCE_DIR}/src/debugger
    ${CMAKE_SOURCE_DIR}/external/libsymbols/include
    ${CMAKE_SOURCE_DIR}/external/libdap/libdap/include
)

# Modify nd100wasm.c to not include missing nd100x_types.h for now
add_custom_command(
    TARGET nd100wasm PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "// Comment out including nd100x_types.h until proper implementation" > ${CMAKE_CURRENT_BINARY_DIR}/compiler_flags.h
    COMMENT "Creating compiler flags for nd100wasm"
    VERBATIM
)

target_compile_definitions(nd100wasm PRIVATE
    ND100WASM_BUILD=1
)

# Link libraries
target_link_libraries(nd100wasm PRIVATE
    ndlib
    cpu
    devices
    machine
    debugger
    symbols_objects
)

# The ND-500, when the parent build found an nd500x checkout. See the long
# note in the root CMakeLists: shared memory (MPM5) means both CPUs must live
# in ONE wasm module, so this is a link, not a second Module.
#
# ND100X_WITH_ND500 also becomes a compile definition, because nd100wasm.c has
# to compile either way - every ND-500 export is inside #ifdef and the module
# simply does not offer them when there is no ND-500.
if(ND100X_WITH_ND500)
    # nd500::headers carries nd500x's src/ as a DIRECTORY, so its headers are
    # reached as "machine/machine_types.h" and "cpu/cpu_protos.h". Both projects
    # have files of those names and nd100x's src/machine and src/cpu are already
    # on this target's include path, so an unqualified include resolves to the
    # ND-100's - see the note at the top of nd500_wasm.c.
    #
    # This used to be a hand-written target_include_directories() naming
    # ${ND100X_ND500X_DIR}/src. It is now a property of the nd500_headers
    # INTERFACE target declared in nd500x's own CMakeLists, so the path is
    # defined once, next to the libraries it belongs with, and cannot be
    # forgotten by a target that links them.
    target_link_libraries(nd100wasm PRIVATE
        nd500::headers nd500::machine nd500::cpu nd500::ndlib)
    target_compile_definitions(nd100wasm PRIVATE ND100X_WITH_ND500=1)
endif()

# Emscripten-specific configurations
if(PLATFORM_WASM)
    # Make sure the output goes to the bin directory
    set_target_properties(nd100wasm PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        SUFFIX ".js"
    )
    
    # Add Emscripten-specific compiler and linker flags
    set(EMSCRIPTEN_LINK_FLAGS 
        "-s WASM=1 \
         -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS','addFunction','UTF8ToString','stringToUTF8','lengthBytesUTF8','noExitRuntime','preloadPlugins','STACK_SIZE','HEAPU8'] \
         -s EXPORTED_FUNCTIONS=['_main','_malloc','_free','_Init','_Boot','_Step','_Stop','_IsInitialized','_Setup','_SetRTCMode','_IO_Tick','_SendKeyToTerminal','_GetTerminalAddress','_SetTerminalOutputCallback','_GetTerminalIdentCode','_GetTerminalName','_GetTerminalLogicalDevice','_TerminalOutputToJS','_SetJSTerminalOutputHandler','_RemountFloppy','_RemountSMD','_UnmountFloppy','_UnmountSMD','_Dbg_SetPaused','_Dbg_IsPaused','_Dbg_StepOne','_Dbg_StepOver','_Dbg_StepOut','_Dbg_RunWithBreakpoints','_Dbg_GetPC','_Dbg_GetRegA','_Dbg_GetRegD','_Dbg_GetRegB','_Dbg_GetRegT','_Dbg_GetRegL','_Dbg_GetRegX','_Dbg_GetSTS','_Dbg_GetPIL','_Dbg_GetEA','_Dbg_SetPC','_Dbg_SetRegA','_Dbg_SetRegD','_Dbg_SetRegB','_Dbg_SetRegT','_Dbg_SetRegL','_Dbg_SetRegX','_Dbg_SetSTS','_Dbg_GetRegAtLevel','_Dbg_GetPANS','_Dbg_GetOPR','_Dbg_GetPGS','_Dbg_GetPVL','_Dbg_GetIIC','_Dbg_GetIID','_Dbg_GetPID','_Dbg_GetPIE','_Dbg_GetCSR','_Dbg_GetALD','_Dbg_GetPES','_Dbg_GetPGC','_Dbg_GetPEA','_Dbg_GetPCR','_Dbg_GetPANC','_Dbg_GetLMP','_Dbg_GetIIE','_Dbg_GetCCL','_Dbg_GetLCIL','_Dbg_GetUCIL','_Dbg_GetECCR','_Dbg_GetInstrCount','_Dbg_GetRunMode','_Dbg_GetStopReason','_Dbg_ReadMemory','_Dbg_ReadMemoryBlock','_Dbg_WriteMemory','_Dbg_DumpPhysicalMemory','_Dbg_AddBreakpoint','_Dbg_RemoveBreakpoint','_Dbg_ClearBreakpoints','_Dbg_Disassemble','_Dbg_GetLevelInfo','_Dbg_GetScopes','_Dbg_GetVariables','_Dbg_GetThreads','_Dbg_GetStackTrace','_Dbg_GetBreakpointList','_Dbg_AddWatchpoint','_Dbg_RemoveWatchpoint','_Dbg_ClearWatchpoints','_Dbg_GetWatchpointCount','_Dbg_GetWatchpointAddr','_Dbg_GetWatchpointType','_Dbg_GetPageTableCount','_Dbg_GetPageTableEntryRaw','_Dbg_GetExtendedMode','_Dbg_ReadPhysicalMemory','_Dbg_ReadPhysicalMemoryBlock','_SetTerminalCarrier','_EnableTerminalRingBuffer','_PollTerminalOutput','_EnableRemoteTerminals','_GetTerminalCount','_MountSMDFromOPFS','_MountSMDFromBuffer','_GetSMDBuffer','_GetSMDBufferSize','_MountSMDFromGateway','_MountFloppyFromGateway','_MountSCSIFromOPFS','_MountSCSIFromGateway','_MountSCSIFromBuffer','_GetSCSIBuffer','_GetSCSIBufferSize','_RemountSCSI','_UnmountSCSI','_MountWinchesterFromOPFS','_MountWinchesterFromGateway','_MountWinchesterFromBuffer','_GetWinchesterBuffer','_GetWinchesterBufferSize','_RemountWinchester','_UnmountWinchester','_MountFloppyFromOPFS','_MountFloppyFromBuffer','_GetFloppyBuffer','_GetFloppyBufferSize','_ValidateMachineINI','_DescribeMachineINI','_InitWithConfig','_HDLC_InjectRxFrame','_HDLC_PollTxFrame','_HDLC_GetLastTxChannel','_HDLC_GetLastTxLength','_HDLC_GetLastTxBuffer','_HDLC_SetCarrier','_GetDriveInfo','_Nd500_Available','_Nd500_SetEnv','_Nd500_Create','_Nd500_IsCreated','_Nd500_IsBooted','_Nd500_LoadKernel','_Nd500_LoadSegments','_Nd500_MountDisk','_Nd500_UnmountDisk','_Nd500_GetDiskBuffer','_Nd500_GetDiskSize','_Nd500_Boot','_Nd500_Step','_Nd500_GetStopReason','_Nd500_IsRunning','_Nd500_GetStopReasonText','_Nd500_GetPC','_Nd500_ReadPhys','_Nd500_WritePhys','_Nd500_MemorySize','_Nd500_TranslateVirt','_Nd500_PollConsole','_Nd500_SendInput','_Nd500_Eth_Attach','_Nd500_Eth_Detach','_Nd500_Eth_InjectRxFrame','_Nd500_Eth_PollTxFrame','_Nd500_Eth_GetLastTxSegment','_Nd500_Eth_GetLastTxLength','_Nd500_Eth_GetLastTxBuffer','_Nd500_Eth_GetTxDropped','_Nd500_Eth_SetLink'] \
         -s ENVIRONMENT='web,worker,node' \
         -s MODULARIZE=0 \
         -s ALLOW_TABLE_GROWTH=1 \
         -s ALLOW_MEMORY_GROWTH=1"
    )
    
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        # DEMANGLE_SUPPORT was removed from emscripten ("No longer supported")
        # and made every Debug wasm link fail outright. C symbols are not
        # mangled anyway, so nothing was lost with it.
        set(EMSCRIPTEN_LINK_FLAGS "${EMSCRIPTEN_LINK_FLAGS} -s ASSERTIONS=1")
    endif()
    
    # Apply Emscripten flags
    set_target_properties(nd100wasm PROPERTIES
        LINK_FLAGS "${EMSCRIPTEN_LINK_FLAGS}"
    )
    
    # Check if a shell.html template exists
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
        set_target_properties(nd100wasm PROPERTIES
            LINK_FLAGS "${EMSCRIPTEN_LINK_FLAGS} --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
        )
        
        # Install HTML and JS files to bin directory
        install(FILES 
            "${CMAKE_BINARY_DIR}/bin/nd100wasm.js"
            "${CMAKE_BINARY_DIR}/bin/nd100wasm.wasm"
            "${CMAKE_BINARY_DIR}/bin/nd100wasm.html"
            DESTINATION bin
        )
    else()
        # Install JS and WASM files to bin directory
        install(FILES 
            "${CMAKE_BINARY_DIR}/bin/nd100wasm.js"
            "${CMAKE_BINARY_DIR}/bin/nd100wasm.wasm"
            DESTINATION bin
        )
    endif()
    
    # Don't copy index.html here - the Makefile handles it:
    # - wasm target copies template/index.html
    # - wasm-glass target copies template-glass/index.html
    # Copying here would overwrite the Makefile's choice.
endif()

# Install the executable (for non-WASM platforms)
if(NOT PLATFORM_WASM)
    install(TARGETS nd100wasm
        RUNTIME DESTINATION bin
    )
endif() 