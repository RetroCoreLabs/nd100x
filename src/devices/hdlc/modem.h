/*
 * nd100x - ND100 Virtual Machine
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * This file is originated from the nd100x project and the RetroCore project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the nd100em
 * distribution in the file COPYING); if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MODEM_H
#define MODEM_H

#include <stdint.h>
#include <stdbool.h>

// Networking is available on every native target - POSIX directly, Windows
// via the net_compat.h shim over Winsock2. Only WASM is stubbed out (no
// sockets / threads in the browser).
#if !defined(__EMSCRIPTEN__)
#define MODEM_HAS_NETWORKING 1
#include <pthread.h>   /* libpthread on POSIX, winpthreads on MinGW */
#include <stdatomic.h> /* C11 atomics - works on both toolchains */
#endif

typedef struct Device Device;

// Thread-safe byte queue for RX/TX between worker thread and emulation
// Must be large enough to absorb TCP bursts without dropping bytes,
// which would break HDLC frame boundaries and cause CRC errors.
#define MODEM_QUEUE_SIZE (512 * 1024)

#ifdef MODEM_HAS_NETWORKING
// clang-format off
typedef struct {
    uint8_t buf[MODEM_QUEUE_SIZE];
    int head;           // written by producer
    int tail;           // read by consumer
    pthread_mutex_t mtx;
} ModemQueue;
// clang-format on
#endif

// Modem signal callback function types
typedef void (*ModemDataCallback)(Device *device, const uint8_t *data, int length);
typedef void (*ModemSignalCallback)(Device *device, bool pinValue);

// Modem state structure
// clang-format off
typedef struct ModemState {
    // Modem signal states (read by emulation, set by callbacks)
    bool ringIndicator;
    bool dataSetReady;
    bool signalDetector;
    bool clearToSend;
    bool requestToSend;
    bool dataTerminalReady;

    // Config (set once at startup, read-only after)
    bool isServer;
    char address[256];
    int port;

#ifdef MODEM_HAS_NETWORKING
    // Shared state between worker thread and emulation
    atomic_bool connected;      // true when TCP link is up
    atomic_bool networkStarted; // true after StartModem called
    atomic_bool shutdownReq;    // signal worker to exit

    // Thread-safe queues
    ModemQueue rxQueue;         // worker writes, Tick reads
    ModemQueue txQueue;         // SendBytes writes, worker reads

    // Worker thread handle
    pthread_t workerThread;
    bool workerRunning;
#else
    /* No atomics on non-networking platforms - single-threaded access */
    bool connected;
    bool networkStarted;
    #ifndef atomic_store
    #define atomic_store(ptr, val) (*(ptr) = (val))
    #endif
    #ifndef atomic_load
    #define atomic_load(ptr) (*(ptr))
    #endif
#endif

#if defined(__EMSCRIPTEN__)
    /** Gateway WebSocket channel index 0-3 (thumbwheel - 1); used for TX to JS. */
    int wasmBridgeChannel;
#endif

    // Traffic statistics
    uint64_t bytesTx;
    uint64_t bytesRx;
    uint64_t rxDropped;     // bytes dropped due to rxQueue overflow
    uint64_t txDropped;     // bytes dropped due to txQueue overflow

    // Callbacks to HDLC device (called from emulation thread only)
    Device *hdlcDevice;
    ModemDataCallback onReceivedData;
    ModemSignalCallback onRingIndicator;
    ModemSignalCallback onDataSetReady;
    ModemSignalCallback onSignalDetector;
    ModemSignalCallback onClearToSend;
    ModemSignalCallback onRequestToSend;
    ModemSignalCallback onDataTerminalReady;

} ModemState;
// clang-format on

void Modem_Init(ModemState *modem, Device *hdlcDevice);
void Modem_Destroy(ModemState *modem);
void Modem_StartModem(ModemState *modem, bool isServer, const char *address, int port);
void Modem_Tick(ModemState *modem);

void Modem_SetDTR(ModemState *modem, bool value);
void Modem_SetRTS(ModemState *modem, bool value);
void Modem_SetDSR(ModemState *modem, bool value);
void Modem_SetCTS(ModemState *modem, bool value);

void Modem_SendByte(ModemState *modem, uint8_t data);
void Modem_SendBytes(ModemState *modem, const uint8_t *data, int length);

void Modem_SetReceivedDataCallback(ModemState *modem, ModemDataCallback callback);
void Modem_SetRingIndicatorCallback(ModemState *modem, ModemSignalCallback callback);
void Modem_SetDataSetReadyCallback(ModemState *modem, ModemSignalCallback callback);
void Modem_SetSignalDetectorCallback(ModemState *modem, ModemSignalCallback callback);
void Modem_SetClearToSendCallback(ModemState *modem, ModemSignalCallback callback);
void Modem_SetRequestToSendCallback(ModemState *modem, ModemSignalCallback callback);
void Modem_SetDataTerminalReadyCallback(ModemState *modem, ModemSignalCallback callback);

#if defined(__EMSCRIPTEN__)
void Modem_SetWasmBridgeChannel(ModemState *modem, int channel);
void Modem_StartWasmBridge(ModemState *modem);
void Modem_SetCarrierPresent(ModemState *modem, bool present);
#endif

#endif // MODEM_H
