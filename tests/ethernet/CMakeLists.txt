# Ethernet II port tests (docs/ETHERNET-II-PORT-PLAN.md).
#
# test_ethernet holds the ports of the RetroCore tests; every test is an
# ETH_TEST(CsClass, CsMethod) that registers itself, and
# tools/ethport/test_parity.py --binary=<test_ethernet> checks that the
# registered list equals the source list and the RetroCore list.

set(ETH_TEST_RUNNER ${CMAKE_CURRENT_SOURCE_DIR}/eth_test_main.c)

# Runner self-test: one binary that must pass, one that must fail.
add_executable(test_eth_runner_pass ${ETH_TEST_RUNNER} selftest_must_pass.c)
add_test(NAME eth_runner_pass COMMAND test_eth_runner_pass)

add_executable(test_eth_runner_fail ${ETH_TEST_RUNNER} selftest_must_fail.c)
add_test(NAME eth_runner_must_fail COMMAND test_eth_runner_fail)
set_tests_properties(eth_runner_must_fail PROPERTIES WILL_FAIL TRUE)

# Ported tests. Sources are added here as each phase ports them.
file(GLOB ETH_PORT_TESTS ${CMAKE_CURRENT_SOURCE_DIR}/test_eth_*.c)
list(REMOVE_ITEM ETH_PORT_TESTS ${CMAKE_CURRENT_SOURCE_DIR}/test_eth_window.c) # own target below
# The card sources under test are listed here as they are written.
set(ETH_CARD_SRC
    ${CMAKE_SOURCE_DIR}/src/devices/ethernet/eth_irq.c
    ${CMAKE_SOURCE_DIR}/src/devices/ethernet/eth_m68k.c
    ${CMAKE_SOURCE_DIR}/src/devices/ethernet/eth_mfp.c
    ${CMAKE_SOURCE_DIR}/src/devices/ethernet/eth_mfp_regs.c
    ${CMAKE_SOURCE_DIR}/src/devices/ethernet/eth_mfp_timer.c
    ${CMAKE_SOURCE_DIR}/src/devices/ethernet/eth_mfp_usart.c
    ${CMAKE_SOURCE_DIR}/src/devices/ethernet/net/eth_ipcsum.c)
if(ETH_PORT_TESTS)
    add_executable(test_ethernet ${ETH_TEST_RUNNER} ${ETH_PORT_TESTS} ${ETH_CARD_SRC})
    target_include_directories(test_ethernet PRIVATE ${CMAKE_SOURCE_DIR}/src/devices/ethernet)
    target_link_libraries(test_ethernet PRIVATE musashi)
    add_test(NAME ethernet_tests COMMAND test_ethernet)
endif()

# Musashi checks. Each binary defines its own m68k_read/write_memory_*
# callbacks, so they compile the Musashi sources directly (Musashi's own
# default m68kconf.h) instead of linking the card's musashi library.
set(MUSASHI_SRC
    ${CMAKE_SOURCE_DIR}/external/Musashi/m68kcpu.c
    ${CMAKE_SOURCE_DIR}/external/Musashi/m68kdasm.c
    ${CMAKE_SOURCE_DIR}/external/Musashi/softfloat/softfloat.c
    ${CMAKE_SOURCE_DIR}/src/devices/ethernet/m68k/generated/m68kops.c)
set(MUSASHI_INC
    ${CMAKE_SOURCE_DIR}/external/Musashi
    ${CMAKE_SOURCE_DIR}/src/devices/ethernet/m68k/generated)

add_executable(test_eth_musashi ${ETH_TEST_RUNNER} test_musashi_buserr.c ${MUSASHI_SRC})
target_include_directories(test_eth_musashi PRIVATE ${MUSASHI_INC})
set_source_files_properties(${MUSASHI_SRC} PROPERTIES COMPILE_OPTIONS "-w")
if(NOT WIN32)
    target_link_libraries(test_eth_musashi PRIVATE m)
endif()
add_test(NAME eth_musashi COMMAND test_eth_musashi)

# Musashi's own instruction suite (test/mc68000/*.bin) with its own driver,
# as shipped (the driver selects a 68040 core; plan Phase 2.2).
add_executable(musashi_test_driver
    ${CMAKE_SOURCE_DIR}/external/Musashi/test/test_driver.c ${MUSASHI_SRC})
target_include_directories(musashi_test_driver PRIVATE ${MUSASHI_INC})
set_source_files_properties(${CMAKE_SOURCE_DIR}/external/Musashi/test/test_driver.c
    PROPERTIES COMPILE_OPTIONS "-w")
if(NOT WIN32)
    target_link_libraries(musashi_test_driver PRIVATE m)
endif()
foreach(t abcd adda add_i addq add addx andi_to_ccr andi_to_sr and
          bcc bchg bclr bool_i bset bsr btst chk cmpa cmpm cmp dbcc divs divu
          eori_to_ccr eori_to_sr eor exg ext lea_pea lea_tas lea_tst links
          movem movep moveq move move_usp move_xxx_flags muls mulu nbcd negs
          op_cmp_i ori_to_ccr ori_to_sr or rox roxx rtr sbcd scc shifts2 shifts
          suba sub_i subq sub subx swap trapv)
    add_test(NAME eth_musashi_suite_${t}
             COMMAND musashi_test_driver test/mc68000/${t}.bin
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/external/Musashi)
    set_tests_properties(eth_musashi_suite_${t} PROPERTIES
        PASS_REGULAR_EXPRESSION "test_fail_count = 0"
        FAIL_REGULAR_EXPRESSION "test_fail_count = [1-9]")
endforeach()

# The card's DRAM window through the CPU's real physical memory path. Links the
# real cpu/machine/devices libraries like tests/test_memory_banks does.
add_executable(test_eth_window ${ETH_TEST_RUNNER} test_eth_window.c)
target_include_directories(test_eth_window PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/cpu ${CMAKE_SOURCE_DIR}/src/machine
    ${CMAKE_SOURCE_DIR}/src/devices ${CMAKE_SOURCE_DIR}/src/devices/ethernet ${CMAKE_SOURCE_DIR}/src/ndlib)
target_link_libraries(test_eth_window PRIVATE
    ${ND_LINK_GROUP_BEGIN} cpu machine devices debugger ndlib ${ND_LINK_GROUP_END} m)
add_dependencies(test_eth_window cpu devices)
add_test(NAME eth_window COMMAND test_eth_window)

# Host network backends (none / UDP / TCP) on real sockets of this host.
add_executable(test_eth_net ${ETH_TEST_RUNNER} net_eth_backend.c
    ${CMAKE_SOURCE_DIR}/src/devices/ethernet/net/eth_net.c
    ${CMAKE_SOURCE_DIR}/src/devices/ethernet/net/eth_pcap.c)
target_include_directories(test_eth_net PRIVATE ${CMAKE_SOURCE_DIR}/src/devices/ethernet)
find_package(Threads REQUIRED)
target_link_libraries(test_eth_net PRIVATE ndlib Threads::Threads)
if(WIN32)
    target_link_libraries(test_eth_net PRIVATE ws2_32)
endif()
add_test(NAME eth_net COMMAND test_eth_net)

# The F12 packet view's frame decoder (frontend code, no device state).
add_executable(test_eth_decode ${ETH_TEST_RUNNER} test_decode_frames.c
    ${CMAKE_SOURCE_DIR}/src/frontend/nd100x/eth_decode.c)
target_include_directories(test_eth_decode PRIVATE ${CMAKE_SOURCE_DIR}/src/frontend/nd100x)
add_test(NAME eth_decode COMMAND test_eth_decode)
