/*
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * nd500_wasm.h - the ND-500 functions exported from nd100x's WebAssembly
 * module (implemented in nd500_wasm.c). JavaScript calls them by name
 * (template-glass/js/emu-worker.js, emu-proxy.js); do not rename them.
 *
 * A build without an nd500x checkout (ND100X_WITH_ND500 undefined) exports
 * the same names as stubs: Nd500_Available() answers 0 and every command
 * returns -1.
 */

#ifndef ND500_WASM_H
#define ND500_WASM_H

#include <stdint.h>

/* Lifecycle */
int Nd500_Available(void);
int Nd500_SetEnv(const char *name, const char *value);
int Nd500_Create(int mem_bytes);
int Nd500_IsCreated(void);
int Nd500_IsBooted(void);
int Nd500_LoadKernel(uint8_t *data, int len);
int Nd500_LoadSegments(uint8_t *pseg, int pseg_len, uint8_t *dseg, int dseg_len);
int Nd500_MountDisk(int unit, uint8_t *data, int len, int writable);
int Nd500_UnmountDisk(int unit);
uint8_t *Nd500_GetDiskBuffer(int unit);
int Nd500_GetDiskSize(int unit);
int Nd500_Boot(void);

/* Running */
int Nd500_Step(int count);
int Nd500_GetStopReason(void);
int Nd500_IsRunning(void);
const char *Nd500_GetStopReasonText(void);
uint32_t Nd500_GetPC(void);

/* Memory */
int Nd500_ReadPhys(uint32_t addr, uint8_t *dst, int len);
int Nd500_WritePhys(uint32_t addr, const uint8_t *src, int len);
uint32_t Nd500_MemorySize(void);
uint32_t Nd500_TranslateVirt(uint32_t vaddr);

/* Console */
int Nd500_PollConsole(void);
void Nd500_SendInput(int unit, const char *text, int len);

/* Ethernet */
int Nd500_Eth_Attach(int segment);
void Nd500_Eth_Detach(void);
int Nd500_Eth_InjectRxFrame(int segment, const uint8_t *data, int len);
int Nd500_Eth_PollTxFrame(void);
int Nd500_Eth_GetLastTxSegment(void);
int Nd500_Eth_GetLastTxLength(void);
uint8_t *Nd500_Eth_GetLastTxBuffer(void);
unsigned long Nd500_Eth_GetTxDropped(void);
void Nd500_Eth_SetLink(int segment, int present);

#endif /* ND500_WASM_H */
