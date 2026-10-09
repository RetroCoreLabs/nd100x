/*
 * eth_m68kconf.h - Musashi configuration for the Ethernet II card's 68000.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Selected through MUSASHI_CNF (external/Musashi/m68k.h), which replaces
 * Musashi's own m68kconf.h, so every option Musashi reads is defined here.
 * Each setting names the RetroCore behaviour it has to match (the card is
 * ported from RetroCore NDBusEthernetII.cs, commit 935163f, where the CPU is
 * Cpu68K with CpuType.MC68000, NDBusEthernetII.cs:669).
 */

#ifndef ETH_M68KCONF_H
#define ETH_M68KCONF_H

/* cs: none - Musashi option values (external/Musashi/m68kconf.h) */
#define M68K_OPT_OFF             0
#define M68K_OPT_ON              1 /* cs: none - Musashi option value */
#define M68K_OPT_SPECIFY_HANDLER 2 /* cs: none - Musashi option value */

#define M68K_COMPILE_FOR_MAME M68K_OPT_OFF

/* The card has a 68000 (RetroCore CpuType.MC68000). With every later model
 * off, Musashi's CPU_TYPE_IS_000() is the constant 1 (m68kcpu.h). */
#define M68K_EMULATE_010   M68K_OPT_OFF
#define M68K_EMULATE_EC020 M68K_OPT_OFF
#define M68K_EMULATE_020   M68K_OPT_OFF
#define M68K_EMULATE_030   M68K_OPT_OFF
#define M68K_EMULATE_040   M68K_OPT_OFF

/* Program and data reads go through the same callbacks, as in RetroCore,
 * where opcode fetches and data reads both use NDEthernetMemory.ReadMemory
 * (seen in the reference trace: reset vector reads arrive as R8, fc 6). */
#define M68K_SEPARATE_READS M68K_OPT_OFF

/* Off: a move.l to -(An) writes the long as one m68k_write_memory_32 call,
 * which the card splits high word first (address, then address + 2) - the
 * order RetroCore uses for every long write (MachineMemory.cs:500-516,
 * big-endian branch). */
#define M68K_SIMULATE_PD_WRITES M68K_OPT_OFF

/* On: the card answers interrupt acknowledge cycles per level (RetroCore
 * Cpu_OnInterruptAck, NDBusEthernetII.cs:742-778): autovector, MFP vector or
 * spurious. */
#define M68K_EMULATE_INT_ACK     M68K_OPT_SPECIFY_HANDLER
#define M68K_INT_ACK_CALLBACK(A) eth_m68k_int_ack(A)

#define M68K_EMULATE_BKPT_ACK    M68K_OPT_OFF
#define M68K_BKPT_ACK_CALLBACK() ((void)0)

/* On: a 68000 takes the trace exception when SR.T is set; RetroCore handles
 * TrapVector.Trace (Instructionset.Helpers.cs HandleTrap). */
#define M68K_EMULATE_TRACE M68K_OPT_ON

/* Off: the RESET instruction does not reach the card's peripherals.
 * RetroCore deliberately leaves Cpu68K.OnReset unconnected
 * (NDBusEthernetII.cs:672-676). */
#define M68K_EMULATE_RESET    M68K_OPT_OFF
#define M68K_RESET_CALLBACK() ((void)0)

#define M68K_CMPILD_HAS_CALLBACK  M68K_OPT_OFF
#define M68K_CMPILD_CALLBACK(v, r) ((void)0)
#define M68K_RTE_HAS_CALLBACK     M68K_OPT_OFF
#define M68K_RTE_CALLBACK()       ((void)0)
#define M68K_TAS_HAS_CALLBACK     M68K_OPT_OFF
#define M68K_TAS_CALLBACK()       ((void)0)
#define M68K_ILLG_HAS_CALLBACK    M68K_OPT_OFF
#define M68K_ILLG_CALLBACK(opcode) ((void)0)
#define M68K_TRAP_HAS_CALLBACK    M68K_OPT_OFF
#define M68K_TRAP_CALLBACK(trap)  ((void)0)

/* On: the card needs the function code of every access - for the bus error
 * frame (m68k_set_bus_error_info) and the differential trace fc field. */
#define M68K_EMULATE_FC         M68K_OPT_SPECIFY_HANDLER
#define M68K_SET_FC_CALLBACK(A) eth_m68k_set_fc(A)

#define M68K_MONITOR_PC          M68K_OPT_OFF
#define M68K_SET_PC_CALLBACK(A)  ((void)0)

/* On: per-instruction hook for the optional INSN trace line. */
#define M68K_INSTRUCTION_HOOK         M68K_OPT_SPECIFY_HANDLER
#define M68K_INSTRUCTION_CALLBACK(pc) eth_m68k_instr_hook(pc)

/* On: RetroCore's runaway guard counts every exception except interrupts
 * (Cpu68K.cs:537-575; all of them are thrown Cpu68KTrapException there). */
#define M68K_EXCEPTION_HOOK        M68K_OPT_SPECIFY_HANDLER
#define M68K_EXCEPTION_CALLBACK(V) eth_m68k_exception_hook(V)

/* Off: no prefetch queue model. Whether RetroCore's Cpu68K models one is
 * unverified; the differential trace shows it if it does. */
#define M68K_EMULATE_PREFETCH M68K_OPT_OFF

/* On: a word or long access at an odd address raises address error, as
 * RetroCore does (Instructionset.Helpers.cs:643-699, AddressError). */
#define M68K_EMULATE_ADDRESS_ERROR M68K_OPT_ON

#define M68K_LOG_ENABLE     M68K_OPT_OFF
#define M68K_LOG_1010_1111  M68K_OPT_OFF
#define M68K_LOG_TRAP       M68K_OPT_OFF
#define M68K_LOG_FILEHANDLE stderr

#define M68K_EMULATE_PMMU M68K_OPT_OFF

#define M68K_USE_64_BIT M68K_OPT_ON

/* Callbacks implemented by the card (src/devices/ethernet/eth_m68k.c). */
int eth_m68k_int_ack(int int_level);
void eth_m68k_set_fc(unsigned int fc);
void eth_m68k_instr_hook(unsigned int pc);
void eth_m68k_exception_hook(unsigned int vector);

#endif /* ETH_M68KCONF_H */
