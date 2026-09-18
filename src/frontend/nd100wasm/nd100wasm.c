/*
 * nd100wasm - ND100 Virtual Machine for WebAssembly
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * This file is originated from the nd100x project.
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>

// Add Emscripten specific headers when building for WASM
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define EMSCRIPTEN_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EMSCRIPTEN_EXPORT
#endif

// TODO: Create proper nd100wasm_types.h or share types with nd100x
// #include "nd100x_types.h"
#include "nd100wasm_protos.h"

// Include Machine and CPU support
#include "../ndlib/ndlib_types.h"
#include "../ndlib/ndlib_protos.h"
#include "../machine/machine_types.h"
#include "../machine/machine_protos.h"
#include "../machine/machine_config.h"
#include "../machine/machine_config_apply.h"
#include "../machine/machine_config_json.h"
#include "../devices/terminal/deviceTerminal.h"
#include "../devices/papertape/devicePapertape.h"
#include "../devices/papertapewriter/devicePaperTapeWriter.h"
#include "../devices/scsi/deviceSCSI.h"
#include "../devices/hdlc/deviceHDLC.h"
#include "../devices/hdlc/modem.h"
#include "../devices/devices_protos.h"
#include "../ndlib/printjob.h"

// CPU internals for debugger access
#include "../cpu/cpu_types.h"
#include "../cpu/cpu_protos.h"


// Debugger support (WITH_DEBUGGER is now enabled for WASM)
#ifdef WITH_DEBUGGER
#include "../debugger/debugger.h"
#include "dap_server.h"

/* Functions from debugger.c WASM API */
extern DAPServer *dbg_get_server(void);
extern const char *dbg_get_scopes_json(void);
extern const char *dbg_get_variables_json(int scope_id);
extern const char *dbg_get_stack_trace_json(void);
extern const char *dbg_get_threads_json(void);
extern int dbg_step_in(void);
extern int dbg_step_over(void);
extern int dbg_step_out(void);
#endif

// Global variables
static int initialized = 0;
static int running = 0;
static int js_terminal_handler_enabled = 0;
static int dbg_paused = 0;

// Print job manager for PDF generation in WASM
static PrintJob *wasmPrintJob = NULL;

// Array of device references for quick access
#define MAX_TERMINALS 16
static Device* terminals[MAX_TERMINALS] = {NULL};

/** HDLC controllers 1-4 (gateway channel = index 0-3); base addrs match devicemanager */
#define HDLC_CHANNEL_COUNT 4
static Device *hdlc_devices[HDLC_CHANNEL_COUNT] = { NULL };
static const uint16_t hdlc_base_addrs_oct[HDLC_CHANNEL_COUNT] = { 01640, 01660, 01700, 01720 };

// =========================================================
// Terminal output ring buffer
// Replaces EM_ASM char-by-char callbacks with a pollable buffer.
// JS calls GetTerminalOutputCount() + reads HEAPU16 directly.
// Each entry: (identCode << 8) | (charCode & 0xFF)
// =========================================================
#define TERM_BUF_SIZE 8192
static struct {
    uint16_t entries[TERM_BUF_SIZE];
    volatile int writePos;
    volatile int readPos;
} termOutputBuf = { .writePos = 0, .readPos = 0 };

static int use_ring_buffer = 0;

// Define a function pointer type for terminal output callbacks
typedef void (*TerminalOutputCallback)(int terminalId, char c);

// Array of callbacks for terminal output
static TerminalOutputCallback terminalOutputCallbacks[MAX_TERMINALS] = {NULL};

// JavaScript terminal output handler - called from JavaScript
EMSCRIPTEN_EXPORT void TerminalOutputToJS(int identCode, char c) {
    // This is the C function that will be called from JavaScript
    // We need a special emscripten wrapper to call from JavaScript to C
    EM_ASM({
        if (typeof window.handleTerminalOutputFromC === 'function') {
            window.handleTerminalOutputFromC($0, $1);
        } else {
            console.error('handleTerminalOutputFromC not defined in JavaScript');
        }
    }, identCode, c);
}

// Enable or disable the JavaScript terminal handler
EMSCRIPTEN_EXPORT void SetJSTerminalOutputHandler(int enable) {
    js_terminal_handler_enabled = enable;
    printf("JavaScript terminal handler %s\n", enable ? "enabled" : "disabled");
}

// Enable or disable the ring buffer for terminal output
// When enabled, WasmTerminalOutputHandler writes to the ring buffer
// instead of calling EM_ASM. JS polls the buffer after each Step().
EMSCRIPTEN_EXPORT void EnableTerminalRingBuffer(int enable) {
    use_ring_buffer = enable;
    if (enable) {
        // Reset buffer positions
        termOutputBuf.writePos = 0;
        termOutputBuf.readPos = 0;
    }
    printf("Terminal ring buffer %s\n", enable ? "enabled" : "disabled");
}

// Poll next terminal output entry from the ring buffer.
// Returns packed (identCode << 8 | charCode), or -1 if empty.
// JS calls this in a loop after each Step() until it returns -1.
EMSCRIPTEN_EXPORT int PollTerminalOutput(void) {
    if (termOutputBuf.readPos == termOutputBuf.writePos) return -1;
    uint16_t entry = termOutputBuf.entries[termOutputBuf.readPos];
    termOutputBuf.readPos = (termOutputBuf.readPos + 1) % TERM_BUF_SIZE;
    return (int)entry;
}

// Write a character to the ring buffer (called from WasmTerminalOutputHandler)
static void bufferTerminalOutput(int identCode, char c) {
    int next = (termOutputBuf.writePos + 1) % TERM_BUF_SIZE;
    if (next != termOutputBuf.readPos) {
        // Pack identCode (high byte) and char (low byte) into one uint16_t
        termOutputBuf.entries[termOutputBuf.writePos] =
            (uint16_t)(((identCode & 0xFF) << 8) | (c & 0xFF));
        termOutputBuf.writePos = next;
    }
    // If buffer is full, silently drop the character
}

// Forward declarations for device-specific output handlers
static void WasmPrinterOutputHandler(Device *device, char c);
static void WasmPaperTapeWriterOutputHandler(Device *device, char c);

// Character device output handler for terminals
static void WasmTerminalOutputHandler(Device *device, char c)
{
    if (!device)
        return;

    if (use_ring_buffer) {
        // Ring buffer mode: write to buffer, JS polls after Step()
        bufferTerminalOutput(device->identCode, c);
        return;
    }

    if (js_terminal_handler_enabled) {
        // Use JavaScript handler - direct call to JS without function pointers
        TerminalOutputToJS(device->identCode, c);
        return;
    }

    // Traditional callback approach (with function pointers)
    // Find terminal ID in our array by identCode reference
    for (int i = 0; i < MAX_TERMINALS; i++) {
        Device *term = terminals[i];

        if (term)
        {
            if (term->identCode == device->identCode && term->deviceClass == device->deviceClass) {
                if (terminalOutputCallbacks[i])
                {
                    terminalOutputCallbacks[i](term->identCode , c);
                }
                else
                {
                    printf("No callback for terminal %d\n", term->identCode);
                }
                break;
            }
        }
    }
}

// Defined further down with ValidateMachineINI, where the config entry points
// live. InitWithConfig() needs it up here.
static int parse_machine_ini(const char* iniText, MachineConfig* mc, char* err, size_t errlen);

// Re-scan the device manager and attach the WASM-side handlers.
//
// Split out of Init() because it now has to happen TWICE: once for the device
// set Init() builds, and again after ApplyMachineINI() has added whatever the
// config asked for. Both are idempotent - re-binding a device that already has
// its handler just sets the same pointer again - so calling them a second time
// costs nothing and forgetting to would leave a configured terminal with no way
// to reach the browser.
static void wasm_bind_terminals(void)
{
    int termIdx = 0;
    int devCount = DeviceManager_GetDeviceCount();
    for (int i = 0; i < MAX_TERMINALS; i++) terminals[i] = NULL;
    for (int i = 0; i < devCount && termIdx < MAX_TERMINALS; i++) {
        Device *dev = DeviceManager_GetDeviceByIndex(i);
        if (dev && dev->type == DEVICE_TYPE_TERMINAL) {
            terminals[termIdx++] = dev;
        }
    }
    for (int i = 0; i < MAX_TERMINALS; i++) {
        if (terminals[i]) {
            Device_SetCharacterOutput(terminals[i], WasmTerminalOutputHandler);
        }
    }
}

// There is no host TCP in a browser, so every HDLC channel that exists has to
// be put on the WebSocket/gateway bridge, however it got added.
static void wasm_bind_hdlc(void)
{
    for (int ch = 0; ch < HDLC_CHANNEL_COUNT; ch++) {
        hdlc_devices[ch] = DeviceManager_GetDeviceByAddress(hdlc_base_addrs_oct[ch]);
        if (hdlc_devices[ch] && hdlc_devices[ch]->deviceData) {
            HDLCData *hd = (HDLCData *)hdlc_devices[ch]->deviceData;
            if (hd && hd->modem) {
                Modem_StartWasmBridge(hd->modem);
            }
        }
    }
}

// Initialize the system (hardware only, no boot).
//
// <iniText> NULL or empty gives the built-in device set this has always built:
// terminals 5-11 plus the console, and HDLC 1-4. Anything else is a machine
// configuration INI, and then the CONFIG decides - the built-in set is not
// created at all.
//
// That is deliberately not a separate "apply" step done afterwards. Adding
// terminal 5 on top of a machine that already has terminal 5 gives two devices
// answering IOX 340, which is not a machine at all. The config either builds
// the terminals and controllers or the default does; never both.
//
// Returns "" on success, or a friendly "file:line message" if the config is bad
// - in which case NOTHING was applied and the machine is left with the built-in
// set, so the caller has a working emulator to report the error from.
EMSCRIPTEN_EXPORT const char* InitWithConfig(const char* iniText)
{
    static char result[MC_ERR_LEN];
    MachineConfig mc;
    int useConfig = 0;

    result[0] = '\0';
    printf("ND100X WASM build: %s %s\n", __DATE__, __TIME__);
    printf("[Phase 1] Terminal ring buffer ready (%d entries)\n", TERM_BUF_SIZE);

    // Only initialize once
    if (initialized) {
        printf("Already initialized.\n");
        return result;
    }

    // Parse BEFORE machine_init: a bad config must not leave a half-built
    // machine behind. Nothing has been created yet at this point.
    if (iniText && iniText[0]) {
        if (!parse_machine_ini(iniText, &mc, result, sizeof(result))) return result;
        useConfig = 1;
    }

    // CPU model, FPP and RTC FIRST: machine_init() -> cpu_init() ->
    // Setup_Instructions() reads CurrentCPUType to decide which opcode groups
    // to register, so a model chosen after this point never reaches the guest.
    if (useConfig) MachineConfig_ApplyCpu(&mc, NULL);

    // [runtime] log = SPEC: per-category log levels (see log.h).
    if (useConfig && mc.runtime.log_spec[0] && Log_ParseSpec(mc.runtime.log_spec) != 0) {
        snprintf(result, sizeof(result), "[runtime] log = %s: unknown category or level",
                 mc.runtime.log_spec);
        return result;
    }
    // The vendored NCR 5386 port reads its own switch.
    scsi_debug_enabled = Log_IsEnabled(LOG_CAT_SCSI, LOG_DEBUG) ? 1 : 0;

#ifdef WITH_DEBUGGER
    // Initialize machine with debugger enabled
    int mrc = machine_init(1, 4711);
#else
    // Initialize machine components including devices
    int mrc = machine_init(0, 4711);
#endif
    if (mrc != 0) {
        snprintf(result, sizeof(result), "machine initialisation failed (out of memory)");
        return result;
    }

    if (useConfig) {
        // The devices half: terminals, controllers with their images, and
        // HDLC. The CPU half already ran, before machine_init above.
        MachineConfig_ApplyDevices(&mc);
    } else {
        // Add terminals 5-8 (Group 1) and 9-11 (Group 9)
        // Console (thumbwheel 0) is already added by DeviceManager_AddAllDevices
        // Total: 8 terminals (console + 7)
        for (uint8_t tw = 5; tw <= 11; tw++) {
            DeviceManager_AddDevice(DEVICE_TYPE_TERMINAL, tw);
        }
    }

    wasm_bind_terminals();

    // Set up character device output handler for the line printer
    Device *printer = DeviceManager_GetDeviceByAddress(0430);
    if (printer) {
        Device_SetCharacterOutput(printer, WasmPrinterOutputHandler);
    }

    // Create print job manager for PDF generation
    wasmPrintJob = PrintJob_Create(PJ_PRINTER_TEXT, PJ_FORMAT_PDF, "/prints");

    // Set up character device output handler for the paper tape writer
    Device *ptw = DeviceManager_GetDeviceByAddress(0410);
    if (ptw) {
        Device_SetCharacterOutput(ptw, WasmPaperTapeWriterOutputHandler);
    }

    // HDLC 1-4: WebSocket/gateway bridge (no host TCP in WASM). With a config,
    // MachineConfig_Apply has already added exactly the channels it asked for -
    // adding these four on top would put a second device on each address.
    if (!useConfig) {
        for (uint8_t tw = 1; tw <= HDLC_CHANNEL_COUNT; tw++) {
            DeviceManager_AddDevice(DEVICE_TYPE_HDLC, tw);
        }
    }
    // Either way every channel that now exists has to reach the browser: there
    // is no host TCP here.
    wasm_bind_hdlc();

    initialized = 1;
    return result;
}

// The original entry point, unchanged for every caller that has one: the
// built-in device set, no config.
EMSCRIPTEN_EXPORT void Init(void)
{
    InitWithConfig(NULL);
}

// Boot the system (load boot sector and set PC)
// boot_type: 0=FLOPPY, 1=SMD, 2=BPUN
// Returns: start address (PC) on success, -1 on failure
EMSCRIPTEN_EXPORT int Boot(int boot_type)
{
    if (!initialized) {
        printf("Error: System not initialized. Call Init() first.\n");
        return -1;
    }

    // Pre-mount drives so they are available for I/O regardless of boot choice.
    // autoMountDrives() inside program_load() will skip already-mounted drives.

    // Mount floppy unit 0 - try both MEMFS paths
    if (!isMounted(DRIVE_FLOPPY, 0)) {
        mount_floppy("/FLOPPY0.IMG", 0);
    }
    if (!isMounted(DRIVE_FLOPPY, 0)) {
        mount_floppy("FLOPPY0.IMG", 0);
    }

    // Mount SMD drives
    if (!isMounted(DRIVE_SMD, 0)) {
        mount_smd("SMD0.IMG", 0);
    }
    mount_smd(NULL, 1);
    mount_smd(NULL, 2);
    mount_smd(NULL, 3);

    // Log which drives are actually mounted (C-side truth)
    {
        MountedDriveInfo_t *smd_list = list_mount(DRIVE_SMD);
        if (smd_list) {
            for (int i = 0; i < 4; i++) {
                printf("[Boot] SMD unit %d: mounted=%d opfs=%d gateway=%d name=%s\n",
                       i, smd_list[i].is_mounted, smd_list[i].is_opfs,
                       smd_list[i].is_gateway, smd_list[i].name);
            }
        }
    }

    int rc;
    switch (boot_type) {
    case 1: // SMD
        rc = program_load(BOOT_SMD, 0, "SMD0.IMG", 1, 0, false);
        break;
    case 2: // BPUN
        rc = program_load(BOOT_BPUN, 0, "BPUN_UPLOAD.IMG", 1, 0, false);
        break;
    case 3: // SCSI (ID 0)
        rc = program_load(BOOT_SCSI, 0, "SCSI0.IMG", 1, 0, false);
        break;
    default: // FLOPPY (0)
        rc = program_load(BOOT_FLOPPY, 0, "FLOPPY0.IMG", 1, 0, false);
        break;
    }

    if (rc < 0) {
        return -1;
    }

    gPC = STARTADDR;
    return (int)gPC;
}

// Send a key to a specific terminal device
EMSCRIPTEN_EXPORT int SendKeyToTerminal(int identCode, int keyCode)
{
    // Find the terminal with matching ID
    Device* terminal = NULL;
    for (int i = 0; i < MAX_TERMINALS; i++) {
        if (terminals[i] && terminals[i]->identCode == identCode) {
            terminal = terminals[i];
            break;
        }
    }

    // Validate terminal was found
    if (!terminal) {
        printf("Error: Terminal with IdentCode %d not found\n", identCode);
        return 0; // Failure
    }

    // If device is a character device, use the character input function
    if (terminal->deviceClass == DEVICE_CLASS_CHARACTER &&
        terminal->charCallbacks.inputFunc) {
        Device_InputCharacter(terminal, (char)keyCode);
    }

    return 1; // Success
}

// Get the address of a terminal by ID (for debugging)
EMSCRIPTEN_EXPORT int GetTerminalAddress(int terminalId)
{
    if (terminalId < 0 || terminalId >= MAX_TERMINALS || !terminals[terminalId]) {
        return -1; // Invalid or not found
    }

    return terminals[terminalId]->startAddress;
}

// Get the identCode of a terminal by ID (for JS tab naming)
EMSCRIPTEN_EXPORT int GetTerminalIdentCode(int terminalId)
{
    if (terminalId < 0 || terminalId >= MAX_TERMINALS || !terminals[terminalId]) {
        return -1;
    }
    return terminals[terminalId]->identCode;
}

// Get the device name of a terminal by ID (returns pointer to C string)
EMSCRIPTEN_EXPORT const char* GetTerminalName(int terminalId)
{
    if (terminalId < 0 || terminalId >= MAX_TERMINALS || !terminals[terminalId]) {
        return NULL;
    }
    return terminals[terminalId]->memoryName;
}

// Get the SINTRAN logical device number of a terminal by ID
EMSCRIPTEN_EXPORT int GetTerminalLogicalDevice(int terminalId)
{
    if (terminalId < 0 || terminalId >= MAX_TERMINALS || !terminals[terminalId]) {
        return -1;
    }
    return terminals[terminalId]->logicalDevice;
}

// Set or clear carrier status on a terminal.
// flag=1: carrier missing (closing window / hanging up)
//   - sets carrierMissing bit and enqueues a space so SINTRAN detects it
// flag=0: carrier present (reopening window / reconnecting)
//   - clears carrierMissing bit
EMSCRIPTEN_EXPORT int SetTerminalCarrier(int flag, int identCode)
{
    Device* terminal = NULL;
    for (int i = 0; i < MAX_TERMINALS; i++) {
        if (terminals[i] && terminals[i]->identCode == identCode) {
            terminal = terminals[i];
            break;
        }
    }

    if (!terminal || !terminal->deviceData) {
        printf("Error: Terminal with IdentCode %d not found\n", identCode);
        return 0;
    }

    TerminalData *data = (TerminalData *)terminal->deviceData;

    if (flag) {
        // Carrier missing - terminal window closed
        data->noCarrier = true;
        data->inputStatus.bits.carrierMissing = 1;
        // Enqueue a space so SINTRAN will poll the status and notice carrier loss
        Terminal_QueueKeyCode(terminal, ' ');
        // Force an interrupt so SINTRAN wakes up and reads the status register
        if (data->inputStatus.bits.interruptEnabled) {
            data->inputStatus.bits.deviceReadyForTransfer = true;
            data->uartInputBuf = ' ';
            Device_SetInterruptStatus(terminal, true, 12);
        }
        // carrier missing signaled to SINTRAN via interrupt
    } else {
        // Carrier present - terminal window reopened
        data->noCarrier = false;
        data->inputStatus.bits.carrierMissing = 0;
        // carrier restored
    }

    return 1;
}

// Enable remote terminals (thumbwheels 12-19, TERMINAL 12-19)
// Creates 8 additional terminal devices for remote access via WebSocket gateway.
// Safe to call multiple times (no-op after first).
static int remote_terminals_enabled = 0;

EMSCRIPTEN_EXPORT int EnableRemoteTerminals(void)
{
    if (!initialized) {
        printf("Error: System not initialized. Call Init() first.\n");
        return -1;
    }

    if (remote_terminals_enabled) {
        return 0;
    }

    int added = 0;
    for (uint8_t tw = 12; tw <= 19; tw++) {
        DeviceManager_AddDevice(DEVICE_TYPE_TERMINAL, tw);
    }

    // Re-scan device manager to populate any new terminal slots
    int termIdx = 0;
    int devCount = DeviceManager_GetDeviceCount();
    for (int i = 0; i < devCount && termIdx < MAX_TERMINALS; i++) {
        Device *dev = DeviceManager_GetDeviceByIndex(i);
        if (dev && dev->type == DEVICE_TYPE_TERMINAL) {
            if (terminals[termIdx] != dev) {
                // New terminal discovered
                if (!terminals[termIdx]) added++;
                terminals[termIdx] = dev;
            }
            termIdx++;
        }
    }

    // Set output handler on all terminals (includes new ones)
    for (int i = 0; i < MAX_TERMINALS; i++) {
        if (terminals[i]) {
            Device_SetCharacterOutput(terminals[i], WasmTerminalOutputHandler);
        }
    }

    remote_terminals_enabled = 1;
    printf("Remote terminals enabled: %d new terminals added (total %d)\n", added, termIdx);
    return added;
}

// Get the count of currently active terminals
EMSCRIPTEN_EXPORT int GetTerminalCount(void)
{
    int count = 0;
    for (int i = 0; i < MAX_TERMINALS; i++) {
        if (terminals[i]) count++;
    }
    return count;
}

// Setup with configuration
EMSCRIPTEN_EXPORT void Setup(const char* config)
{
    printf("Setup called with config: %s\n", config);
    // Parse configuration string and apply settings
    // For now, just print the config
}

// Select the RTC time base: 0 = instruction ticks (default, one clock pulse per
// 10550 executed instructions), 1 = wall-clock (one pulse per 20 ms of host
// time, a real-time 50 Hz clock regardless of emulation speed). Call any time;
// intended between Init() and Boot() like the other machine settings.
EMSCRIPTEN_EXPORT void SetRTCMode(int wall)
{
    RTC_SetWallClockMode(wall != 0);
}

// Execute a specific number of steps
EMSCRIPTEN_EXPORT void Step(int steps)
{
    if (!initialized) {
        printf("Error: System not initialized. Call Init() first.\n");
        return;
    }
    machine_run(steps);
}

// Stop the emulation
EMSCRIPTEN_EXPORT void Stop(void)
{
    running = 0;
    //machine_stop();
}

// Check if machine has been initialized
EMSCRIPTEN_EXPORT int IsInitialized(void)
{
    return initialized;
}

// Remount a floppy drive (close old FILE*, re-open from MEMFS)
// Unit N uses "/FLOPPYN.IMG" (absolute path for MEMFS compatibility)
EMSCRIPTEN_EXPORT int RemountFloppy(int unit)
{
    if (unit < 0 || unit > 2) {
        return -1;
    }

    char filename[32];
    snprintf(filename, sizeof(filename), "/FLOPPY%d.IMG", unit);

    // Check file exists before attempting mount
    FILE *ftmp = fopen(filename, "rb");
    if (!ftmp) {
        return -1;
    }
    fclose(ftmp);

    // Unmount existing if mounted
    if (isMounted(DRIVE_FLOPPY, unit)) {
        unmount_drive(DRIVE_FLOPPY, unit);
    }

    // Mount from the (possibly updated) MEMFS file
    mount_drive(DRIVE_FLOPPY, unit, "md5-unknown", "Floppy", "Mounted floppy image", filename);

    return isMounted(DRIVE_FLOPPY, unit) ? 0 : -1;
}

// Mount an SMD drive from OPFS (Worker mode - no FILE*, block I/O via JS)
// imageSize is the full image size in bytes (for disk_info queries)
EMSCRIPTEN_EXPORT int MountSMDFromOPFS(int unit, int imageSize)
{
    if (unit < 0 || unit > 3) return -1;

    // Unmount existing if mounted
    if (isMounted(DRIVE_SMD, unit)) {
        unmount_drive(DRIVE_SMD, unit);
    }

    const char *name = (unit == 0) ? "Boot SMD (OPFS)" : "Data SMD (OPFS)";
    const char *desc = (unit == 0) ? "Boot SMD from persistent storage" : "Data SMD from persistent storage";
    mount_drive_opfs(DRIVE_SMD, unit, name, desc, (size_t)imageSize);

    return isMounted(DRIVE_SMD, unit) ? 0 : -1;
}

// Mount an SMD drive from a JS buffer (Direct mode - data in malloc'd buffer)
// The buffer is copied into a malloc'd remote_data block so writes are in-memory.
EMSCRIPTEN_EXPORT int MountSMDFromBuffer(int unit, const uint8_t *data, int size)
{
    if (unit < 0 || unit > 3 || !data || size <= 0) return -1;

    // Unmount existing if mounted
    if (isMounted(DRIVE_SMD, unit)) {
        unmount_drive(DRIVE_SMD, unit);
    }

    // Allocate and copy data
    char *buf = malloc((size_t)size);
    if (!buf) return -1;
    memcpy(buf, data, (size_t)size);

    // Ensure drive arrays exist, then get the array via API
    if (init_drive_arrays() != 0) return -1;
    MountedDriveInfo_t *drives = list_mount(DRIVE_SMD);
    if (!drives) { free(buf); return -1; }

    MountedDriveInfo_t *entry = &drives[unit];
    entry->is_mounted = true;
    entry->is_remote = true;  // uses remote_data (in-memory buffer)
    entry->is_opfs = false;
    entry->is_writeprotected = false;
    entry->data.remote_data = buf;
    entry->data_size = (size_t)size;
    entry->block_size = 1024;

    const char *name = (unit == 0) ? "Boot SMD (Buffer)" : "Data SMD (Buffer)";
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    snprintf(entry->description, sizeof(entry->description), "%s", "SMD from persistent storage buffer");
    snprintf(entry->md5, sizeof(entry->md5), "%s", "buffer");
    entry->image_path[0] = '\0';

    return 0;
}

// Get the in-memory buffer pointer for an SMD drive (Direct mode save-back)
// Returns pointer to remote_data if drive is mounted as remote (in-memory buffer), 0 otherwise.
EMSCRIPTEN_EXPORT int GetSMDBuffer(int unit)
{
    if (unit < 0 || unit > 3) return 0;
    MountedDriveInfo_t *drives = list_mount(DRIVE_SMD);
    if (!drives) return 0;
    MountedDriveInfo_t *entry = &drives[unit];
    if (!entry->is_mounted || !entry->is_remote || !entry->data.remote_data) return 0;
    return (int)(uintptr_t)entry->data.remote_data;
}

// Get the size of the in-memory buffer for an SMD drive
EMSCRIPTEN_EXPORT int GetSMDBufferSize(int unit)
{
    if (unit < 0 || unit > 3) return 0;
    MountedDriveInfo_t *drives = list_mount(DRIVE_SMD);
    if (!drives) return 0;
    MountedDriveInfo_t *entry = &drives[unit];
    if (!entry->is_mounted) return 0;
    return (int)entry->data_size;
}

// Read up to 256 sectors from an SMD drive into a static buffer.
// Works for file-backed, in-memory (remote), OPFS and gateway drives.
// Returns pointer to s_smdSectorBuf on success, 0 on failure.
#define SMD_READ_BUF_SECTORS 256
static uint8_t s_smdSectorBuf[SMD_READ_BUF_SECTORS * 1024];

#ifdef __EMSCRIPTEN__
// Defined as EM_JS in machine.c
extern int opfs_block_read_js(int driveType, int unit, uint8_t *buffer, int bytes, int offset);
extern int opfs_is_available_js(int driveType, int unit);
extern int gateway_block_read_js(int driveType, int unit, uint8_t *buffer, int bytes, int offset);
extern int gateway_is_available_js(int driveType, int unit);
#endif

EMSCRIPTEN_EXPORT int Dbg_ReadSMDSectors(int unit, int lba, int count)
{
    if (unit < 0 || unit > 3) return 0;
    if (count <= 0 || count > SMD_READ_BUF_SECTORS) return 0;
    MountedDriveInfo_t *drives = list_mount(DRIVE_SMD);
    if (!drives) return 0;
    MountedDriveInfo_t *entry = &drives[unit];
    if (!entry->is_mounted) return 0;

    size_t sector_size = entry->block_size ? entry->block_size : 1024;
    size_t byte_offset = (size_t)lba * sector_size;
    size_t byte_count  = (size_t)count * sector_size;

    if (entry->data_size && byte_offset + byte_count > entry->data_size) return 0;

#ifdef __EMSCRIPTEN__
    if (entry->is_opfs && opfs_is_available_js((int)DRIVE_SMD, unit)) {
        int rc = opfs_block_read_js((int)DRIVE_SMD, unit, s_smdSectorBuf, (int)byte_count, (int)byte_offset);
        if (rc < 0) return 0;
        if ((size_t)rc < byte_count) memset(s_smdSectorBuf + rc, 0, byte_count - rc);
        return (int)(uintptr_t)s_smdSectorBuf;
    }

    if (entry->is_gateway && gateway_is_available_js((int)DRIVE_SMD, unit)) {
        // The gateway shared buffer holds 64 KB of payload, so split larger requests.
        const size_t chunk = 32768;
        for (size_t done = 0; done < byte_count; done += chunk) {
            size_t n = (byte_count - done < chunk) ? byte_count - done : chunk;
            int rc = gateway_block_read_js((int)DRIVE_SMD, unit, s_smdSectorBuf + done,
                                           (int)n, (int)(byte_offset + done));
            if (rc < 0) return 0;
            if ((size_t)rc < n) memset(s_smdSectorBuf + done + rc, 0, n - rc);
        }
        return (int)(uintptr_t)s_smdSectorBuf;
    }
#endif

    if (entry->is_remote && entry->data.remote_data) {
        memcpy(s_smdSectorBuf, (uint8_t *)entry->data.remote_data + byte_offset, byte_count);
        return (int)(uintptr_t)s_smdSectorBuf;
    }

    if (!entry->is_opfs && entry->data.local_file) {
        if (fseek(entry->data.local_file, (long)byte_offset, SEEK_SET) != 0) return 0;
        if (fread(s_smdSectorBuf, 1, byte_count, entry->data.local_file) != byte_count) return 0;
        return (int)(uintptr_t)s_smdSectorBuf;
    }

    return 0;
}

// Remount an SMD drive (close old FILE*, re-open from MEMFS)
// Unit N uses "/SMDN.IMG" (absolute path for MEMFS compatibility)
EMSCRIPTEN_EXPORT int RemountSMD(int unit)
{
    if (unit < 0 || unit > 3) {
        return -1;
    }

    char filename[32];
    snprintf(filename, sizeof(filename), "/SMD%d.IMG", unit);

    // Unmount existing if mounted
    if (isMounted(DRIVE_SMD, unit)) {
        unmount_drive(DRIVE_SMD, unit);
    }

    // Mount from the (possibly updated) MEMFS file
    mount_drive(DRIVE_SMD, unit, "md5-unknown", "SMD", "Mounted SMD image", filename);

    return isMounted(DRIVE_SMD, unit) ? 0 : -1;
}

// Unmount a floppy drive (close FILE*, mark as not mounted)
EMSCRIPTEN_EXPORT int UnmountFloppy(int unit)
{
    if (unit < 0 || unit > 2) {
        return -1;
    }

    if (isMounted(DRIVE_FLOPPY, unit)) {
        unmount_drive(DRIVE_FLOPPY, unit);
        return 0;
    }

    return 0; // already unmounted
}

// Unmount an SMD drive (close FILE*, mark as not mounted)
EMSCRIPTEN_EXPORT int UnmountSMD(int unit)
{
    if (unit < 0 || unit > 3) {
        return -1;
    }

    if (isMounted(DRIVE_SMD, unit)) {
        unmount_drive(DRIVE_SMD, unit);
        return 0;
    }

    return 0; // already unmounted
}

// Mount an SMD drive from gateway (Worker mode - block I/O via WebSocket sub-worker)
// imageSize is the full image size in bytes (for disk_info queries)
EMSCRIPTEN_EXPORT int MountSMDFromGateway(int unit, int imageSize)
{
    if (unit < 0 || unit > 3) return -1;

    // Unmount existing if mounted
    if (isMounted(DRIVE_SMD, unit)) {
        unmount_drive(DRIVE_SMD, unit);
    }

    const char *name = (unit == 0) ? "Boot SMD (Gateway)" : "Data SMD (Gateway)";
    const char *desc = (unit == 0) ? "Boot SMD from gateway server" : "Data SMD from gateway server";
    mount_drive_gateway(DRIVE_SMD, unit, name, desc, (size_t)imageSize);

    return isMounted(DRIVE_SMD, unit) ? 0 : -1;
}

// Mount a floppy drive from gateway (Worker mode - block I/O via WebSocket sub-worker)
EMSCRIPTEN_EXPORT int MountFloppyFromGateway(int unit, int imageSize)
{
    if (unit < 0 || unit > 2) return -1;

    // Unmount existing if mounted
    if (isMounted(DRIVE_FLOPPY, unit)) {
        unmount_drive(DRIVE_FLOPPY, unit);
    }

    const char *name = "Floppy (Gateway)";
    const char *desc = "Floppy from gateway server";
    mount_drive_gateway(DRIVE_FLOPPY, unit, name, desc, (size_t)imageSize);

    return isMounted(DRIVE_FLOPPY, unit) ? 0 : -1;
}

// =========================================================
// SCSI drive mounting (ND-3201/3204 controller, SCSI IDs 0-6)
// =========================================================
// Mirrors the SMD mount exports. The SCSI controller is opt-in: it is added
// lazily the first time a SCSI disk is mounted (ensure_scsi_controller), so a
// machine with no SCSI disks keeps the exact IOX map it has today. Block I/O
// for OPFS/gateway is namespaced by driveType (DRIVE_SCSI) so SCSI unit 0 does
// not alias SMD unit 0. SCSI targets are IDs 0-6; ID 7 is the controller.

// SCSI controller IOX base for thumbwheel 0.
#define SCSI_TW0_IOX_BASE 0144300

// Add the SCSI controller at thumbwheel 0 (if not already present) and mark the
// given unit as an HDD target so it answers transfers. Returns the Device* or NULL.
static Device *ensure_scsi_controller(int unit)
{
    Device *dev = DeviceManager_GetDeviceByAddress(SCSI_TW0_IOX_BASE);
    if (!dev) {
        SCSIUnitType types[SCSI_MAX_UNITS];
        for (int i = 0; i < SCSI_MAX_UNITS; i++) types[i] = SCSI_UNIT_NONE;
        DeviceManager_AddSCSIDevice_WithConfig(0, types);
        dev = DeviceManager_GetDeviceByAddress(SCSI_TW0_IOX_BASE);
    }
    if (dev && unit >= 0 && unit < SCSI_MAX_UNITS)
        SCSI_SetUnitType(dev, unit, SCSI_UNIT_HDD);
    return dev;
}

// Mount a SCSI drive from OPFS (Worker mode, block I/O via JS keyed on driveType)
EMSCRIPTEN_EXPORT int MountSCSIFromOPFS(int unit, int imageSize)
{
    if (unit < 0 || unit >= SCSI_MAX_UNITS) return -1;
    if (!ensure_scsi_controller(unit)) return -1;
    if (isMounted(DRIVE_SCSI, unit)) unmount_drive(DRIVE_SCSI, unit);

    const char *name = (unit == 0) ? "Boot SCSI (OPFS)" : "Data SCSI (OPFS)";
    const char *desc = (unit == 0) ? "Boot SCSI from persistent storage" : "Data SCSI from persistent storage";
    mount_drive_opfs(DRIVE_SCSI, unit, name, desc, (size_t)imageSize);
    return isMounted(DRIVE_SCSI, unit) ? 0 : -1;
}

// Mount a SCSI drive from gateway (Worker mode, block I/O via WebSocket sub-worker)
EMSCRIPTEN_EXPORT int MountSCSIFromGateway(int unit, int imageSize)
{
    if (unit < 0 || unit >= SCSI_MAX_UNITS) return -1;
    if (!ensure_scsi_controller(unit)) return -1;
    if (isMounted(DRIVE_SCSI, unit)) unmount_drive(DRIVE_SCSI, unit);

    const char *name = (unit == 0) ? "Boot SCSI (Gateway)" : "Data SCSI (Gateway)";
    const char *desc = (unit == 0) ? "Boot SCSI from gateway server" : "Data SCSI from gateway server";
    mount_drive_gateway(DRIVE_SCSI, unit, name, desc, (size_t)imageSize);
    return isMounted(DRIVE_SCSI, unit) ? 0 : -1;
}

// Mount a SCSI drive from a JS buffer (Direct mode, in-memory writable image)
EMSCRIPTEN_EXPORT int MountSCSIFromBuffer(int unit, const uint8_t *data, int size)
{
    if (unit < 0 || unit >= SCSI_MAX_UNITS || !data || size <= 0) return -1;
    if (!ensure_scsi_controller(unit)) return -1;
    if (isMounted(DRIVE_SCSI, unit)) unmount_drive(DRIVE_SCSI, unit);

    char *buf = malloc((size_t)size);
    if (!buf) return -1;
    memcpy(buf, data, (size_t)size);

    if (init_drive_arrays() != 0) return -1;
    MountedDriveInfo_t *drives = list_mount(DRIVE_SCSI);
    if (!drives) { free(buf); return -1; }

    MountedDriveInfo_t *entry = &drives[unit];
    entry->is_mounted = true;
    entry->is_remote = true;
    entry->is_opfs = false;
    entry->is_writeprotected = false;
    entry->data.remote_data = buf;
    entry->data_size = (size_t)size;
    entry->block_size = 1024;

    const char *name = (unit == 0) ? "Boot SCSI (Buffer)" : "Data SCSI (Buffer)";
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    snprintf(entry->description, sizeof(entry->description), "%s", "SCSI from persistent storage buffer");
    snprintf(entry->md5, sizeof(entry->md5), "%s", "buffer");
    entry->image_path[0] = '\0';
    return 0;
}

// Get the in-memory buffer pointer for a SCSI drive (Direct mode save-back)
EMSCRIPTEN_EXPORT int GetSCSIBuffer(int unit)
{
    if (unit < 0 || unit >= SCSI_MAX_UNITS) return 0;
    MountedDriveInfo_t *drives = list_mount(DRIVE_SCSI);
    if (!drives) return 0;
    MountedDriveInfo_t *entry = &drives[unit];
    if (!entry->is_mounted || !entry->is_remote || !entry->data.remote_data) return 0;
    return (int)(uintptr_t)entry->data.remote_data;
}

EMSCRIPTEN_EXPORT int GetSCSIBufferSize(int unit)
{
    if (unit < 0 || unit >= SCSI_MAX_UNITS) return 0;
    MountedDriveInfo_t *drives = list_mount(DRIVE_SCSI);
    if (!drives) return 0;
    MountedDriveInfo_t *entry = &drives[unit];
    if (!entry->is_mounted) return 0;
    return (int)entry->data_size;
}

// Remount a SCSI drive from MEMFS ("/SCSIN.IMG")
EMSCRIPTEN_EXPORT int RemountSCSI(int unit)
{
    if (unit < 0 || unit >= SCSI_MAX_UNITS) return -1;
    if (!ensure_scsi_controller(unit)) return -1;

    char filename[32];
    snprintf(filename, sizeof(filename), "/SCSI%d.IMG", unit);
    if (isMounted(DRIVE_SCSI, unit)) unmount_drive(DRIVE_SCSI, unit);
    mount_drive(DRIVE_SCSI, unit, "md5-unknown", "SCSI", "Mounted SCSI image", filename);
    return isMounted(DRIVE_SCSI, unit) ? 0 : -1;
}

// Unmount a SCSI drive
EMSCRIPTEN_EXPORT int UnmountSCSI(int unit)
{
    if (unit < 0 || unit >= SCSI_MAX_UNITS) return -1;
    if (isMounted(DRIVE_SCSI, unit)) unmount_drive(DRIVE_SCSI, unit);
    return 0;
}

// =========================================================
// Winchester drive mounting (ST506/8 inch, cards 3041/3038)
// =========================================================
// The last controller type with no way into the browser at all. The machine
// layer has had it all along - wd_drives, DRIVE_WINCHESTER, mount_winchester -
// and the native binary mounts it from --wd0/--wd1 or a [controller.wd.0]
// section. Only the WASM side was missing, so a config could name a Winchester
// image and nothing could ever hand one over. disk-types.js said as much:
// "Winchester is a reserved stub ... no mount or boot path yet".
//
// Mirrors the SCSI exports exactly, including the lazy controller: the card
// answers IOX 500-507, the SAME block as the CDC cartridge system disc, so
// adding it unconditionally would change the IOX map of every existing machine.
// It appears the first time a Winchester disk is actually mounted.
//
// Two units, per the registry (g_descriptors: wd, 2 slots) and the hardware -
// disk system 1 carries the unit in one bit of the control word.

#define WD_MAX_UNITS 2
#define WD_TW0_IOX_BASE 000500

// Add the Winchester controller at thumbwheel 0 if it is not already there.
static Device *ensure_winchester_controller(void)
{
    Device *dev = DeviceManager_GetDeviceByAddress(WD_TW0_IOX_BASE);
    if (!dev) {
        DeviceManager_AddDevice(DEVICE_TYPE_DISC_WINCHESTER, 0);
        dev = DeviceManager_GetDeviceByAddress(WD_TW0_IOX_BASE);
    }
    return dev;
}

// Mount a Winchester drive from OPFS (Worker mode, block I/O keyed on driveType)
EMSCRIPTEN_EXPORT int MountWinchesterFromOPFS(int unit, int imageSize)
{
    if (unit < 0 || unit >= WD_MAX_UNITS) return -1;
    if (!ensure_winchester_controller()) return -1;
    if (isMounted(DRIVE_WINCHESTER, unit)) unmount_drive(DRIVE_WINCHESTER, unit);

    const char *name = (unit == 0) ? "Boot Winchester (OPFS)" : "Data Winchester (OPFS)";
    const char *desc = (unit == 0) ? "Boot Winchester from persistent storage"
                                   : "Data Winchester from persistent storage";
    mount_drive_opfs(DRIVE_WINCHESTER, unit, name, desc, (size_t)imageSize);
    return isMounted(DRIVE_WINCHESTER, unit) ? 0 : -1;
}

// Mount a Winchester drive from the gateway (Worker mode, block I/O via WebSocket)
EMSCRIPTEN_EXPORT int MountWinchesterFromGateway(int unit, int imageSize)
{
    if (unit < 0 || unit >= WD_MAX_UNITS) return -1;
    if (!ensure_winchester_controller()) return -1;
    if (isMounted(DRIVE_WINCHESTER, unit)) unmount_drive(DRIVE_WINCHESTER, unit);

    const char *name = (unit == 0) ? "Boot Winchester (Gateway)" : "Data Winchester (Gateway)";
    const char *desc = (unit == 0) ? "Boot Winchester from gateway server"
                                   : "Data Winchester from gateway server";
    mount_drive_gateway(DRIVE_WINCHESTER, unit, name, desc, (size_t)imageSize);
    return isMounted(DRIVE_WINCHESTER, unit) ? 0 : -1;
}

// Mount a Winchester drive from a JS buffer (Direct mode, in-memory writable)
EMSCRIPTEN_EXPORT int MountWinchesterFromBuffer(int unit, const uint8_t *data, int size)
{
    if (unit < 0 || unit >= WD_MAX_UNITS || !data || size <= 0) return -1;
    if (!ensure_winchester_controller()) return -1;
    if (isMounted(DRIVE_WINCHESTER, unit)) unmount_drive(DRIVE_WINCHESTER, unit);

    char *buf = malloc((size_t)size);
    if (!buf) return -1;
    memcpy(buf, data, (size_t)size);

    if (init_drive_arrays() != 0) return -1;
    MountedDriveInfo_t *drives = list_mount(DRIVE_WINCHESTER);
    if (!drives) { free(buf); return -1; }

    MountedDriveInfo_t *entry = &drives[unit];
    entry->is_mounted = true;
    entry->is_remote = true;
    entry->is_opfs = false;
    entry->is_writeprotected = false;
    entry->data.remote_data = buf;
    entry->data_size = (size_t)size;
    entry->block_size = 1024;

    const char *name = (unit == 0) ? "Boot Winchester (Buffer)" : "Data Winchester (Buffer)";
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    snprintf(entry->description, sizeof(entry->description), "%s", "Winchester from persistent storage buffer");
    snprintf(entry->md5, sizeof(entry->md5), "%s", "buffer");
    entry->image_path[0] = '\0';
    return 0;
}

// In-memory buffer pointer / size, for the Direct-mode save-back path.
EMSCRIPTEN_EXPORT int GetWinchesterBuffer(int unit)
{
    if (unit < 0 || unit >= WD_MAX_UNITS) return 0;
    MountedDriveInfo_t *drives = list_mount(DRIVE_WINCHESTER);
    if (!drives) return 0;
    MountedDriveInfo_t *entry = &drives[unit];
    if (!entry->is_mounted || !entry->is_remote || !entry->data.remote_data) return 0;
    return (int)(uintptr_t)entry->data.remote_data;
}

EMSCRIPTEN_EXPORT int GetWinchesterBufferSize(int unit)
{
    if (unit < 0 || unit >= WD_MAX_UNITS) return 0;
    MountedDriveInfo_t *drives = list_mount(DRIVE_WINCHESTER);
    if (!drives) return 0;
    MountedDriveInfo_t *entry = &drives[unit];
    if (!entry->is_mounted) return 0;
    return (int)entry->data_size;
}

// Remount a Winchester drive from MEMFS ("/WDN.IMG")
EMSCRIPTEN_EXPORT int RemountWinchester(int unit)
{
    if (unit < 0 || unit >= WD_MAX_UNITS) return -1;
    if (!ensure_winchester_controller()) return -1;

    char filename[32];
    snprintf(filename, sizeof(filename), "/WD%d.IMG", unit);
    if (isMounted(DRIVE_WINCHESTER, unit)) unmount_drive(DRIVE_WINCHESTER, unit);
    mount_drive(DRIVE_WINCHESTER, unit, "md5-unknown", "Winchester",
                "Mounted Winchester image", filename);
    return isMounted(DRIVE_WINCHESTER, unit) ? 0 : -1;
}

EMSCRIPTEN_EXPORT int UnmountWinchester(int unit)
{
    if (unit < 0 || unit >= WD_MAX_UNITS) return -1;
    if (isMounted(DRIVE_WINCHESTER, unit)) unmount_drive(DRIVE_WINCHESTER, unit);
    return 0;
}

// =========================================================
// Floppy: the two ways in it was missing
// =========================================================
// Floppy could only be mounted from the gateway. SMD, SCSI and now Winchester
// can each come from OPFS (Worker mode) or from a JS buffer (Direct mode), and
// there was no reason floppy could not - the machine layer is type-generic and
// the only thing missing was these functions. Without them a floppy image
// stored in the browser could not be inserted at all unless a gateway was
// running, which is the one configuration that needs no browser storage.
//
// Three units, matching MountFloppyFromGateway and DRIVE_UNIT_COUNT.floppy.
// Block size 512, not 1024: a floppy sector is half a disc sector (see the
// block_size math in src/machine/machine.c and DRIVE_BLOCK_SIZE in
// template-glass/js/disk-types.js).

#define FLOPPY_MAX_UNITS 3

EMSCRIPTEN_EXPORT int MountFloppyFromOPFS(int unit, int imageSize)
{
    if (unit < 0 || unit >= FLOPPY_MAX_UNITS) return -1;
    if (isMounted(DRIVE_FLOPPY, unit)) unmount_drive(DRIVE_FLOPPY, unit);

    mount_drive_opfs(DRIVE_FLOPPY, unit, "Floppy (OPFS)",
                     "Floppy from persistent storage", (size_t)imageSize);
    return isMounted(DRIVE_FLOPPY, unit) ? 0 : -1;
}

EMSCRIPTEN_EXPORT int MountFloppyFromBuffer(int unit, const uint8_t *data, int size)
{
    if (unit < 0 || unit >= FLOPPY_MAX_UNITS || !data || size <= 0) return -1;
    if (isMounted(DRIVE_FLOPPY, unit)) unmount_drive(DRIVE_FLOPPY, unit);

    char *buf = malloc((size_t)size);
    if (!buf) return -1;
    memcpy(buf, data, (size_t)size);

    if (init_drive_arrays() != 0) return -1;
    MountedDriveInfo_t *drives = list_mount(DRIVE_FLOPPY);
    if (!drives) { free(buf); return -1; }

    MountedDriveInfo_t *entry = &drives[unit];
    entry->is_mounted = true;
    entry->is_remote = true;
    entry->is_opfs = false;
    entry->is_writeprotected = false;
    entry->data.remote_data = buf;
    entry->data_size = (size_t)size;
    entry->block_size = 512;

    snprintf(entry->name, sizeof(entry->name), "%s", "Floppy (Buffer)");
    snprintf(entry->description, sizeof(entry->description), "%s", "Floppy from persistent storage buffer");
    snprintf(entry->md5, sizeof(entry->md5), "%s", "buffer");
    entry->image_path[0] = '\0';
    return 0;
}

EMSCRIPTEN_EXPORT int GetFloppyBuffer(int unit)
{
    if (unit < 0 || unit >= FLOPPY_MAX_UNITS) return 0;
    MountedDriveInfo_t *drives = list_mount(DRIVE_FLOPPY);
    if (!drives) return 0;
    MountedDriveInfo_t *entry = &drives[unit];
    if (!entry->is_mounted || !entry->is_remote || !entry->data.remote_data) return 0;
    return (int)(uintptr_t)entry->data.remote_data;
}

EMSCRIPTEN_EXPORT int GetFloppyBufferSize(int unit)
{
    if (unit < 0 || unit >= FLOPPY_MAX_UNITS) return 0;
    MountedDriveInfo_t *drives = list_mount(DRIVE_FLOPPY);
    if (!drives) return 0;
    MountedDriveInfo_t *entry = &drives[unit];
    if (!entry->is_mounted) return 0;
    return (int)entry->data_size;
}

// =========================================================
// Machine configuration (INI) validation for the Machine Setup window
// =========================================================
// Reuses the native MachineConfig INI parser + validator (machine_config.c) so
// the browser gets identical, friendly error messages. The INI text is written
// to a MEMFS temp file and parsed with the same MachineConfig_LoadFile path the
// native binary uses. Returns "" (empty string) when the config is valid, or a
// "file:line message" describing the first problem.
// Parse and validate INI text into <mc>. Returns 1 on success; on failure
// returns 0 and fills <err>. The INI goes through a MEMFS temp file because
// MachineConfig_LoadFile takes a path - the point is to use the SAME parser and
// the SAME validator as the native binary, so the browser cannot drift into
// accepting a config the real machine would reject, or vice versa.
static int parse_machine_ini(const char* iniText, MachineConfig* mc, char* err, size_t errlen)
{
    const char* tmp = "/machine-setup.ini";
    FILE* f = fopen(tmp, "w");
    if (!f) { snprintf(err, errlen, "internal error: cannot create temp file"); return 0; }
    fputs(iniText ? iniText : "", f);
    fclose(f);

    MachineConfig_InitBaseline(mc);
    if (!MachineConfig_LoadFile(mc, tmp, err, errlen)) return 0;
    if (!MachineConfig_Validate(mc, err, errlen)) return 0;
    return 1;
}

// Describe a machine INI as JSON: what CPU, which controllers, which images.
//
// The Machine Setup FORM needs to show what a configuration currently says, and
// the one way it must not find that out is by parsing the INI in JavaScript. A
// second parser drifts from the first, and then the form shows one machine
// while the emulator builds another. This is the same MachineConfig_LoadFile
// the native binary uses, handed back as JSON.
//
// Returns a JSON object, or {"error":"..."} - always something parseable, so
// the caller never has to guess whether it got a config or a message.
EMSCRIPTEN_EXPORT const char* DescribeMachineINI(const char* iniText)
{
    static char result[8192];
    MachineConfig mc;
    char err[MC_ERR_LEN];

    if (!parse_machine_ini(iniText, &mc, err, sizeof(err))) {
        // Hand the parser's own words back, escaped, rather than a generic
        // "could not read": the form shows this to the user.
        char esc[MC_ERR_LEN * 2];
        size_t i, j = 0;
        for (i = 0; err[i] && j + 2 < sizeof(esc); i++) {
            if (err[i] == '"' || err[i] == '\\') esc[j++] = '\\';
            esc[j++] = err[i];
        }
        esc[j] = '\0';
        snprintf(result, sizeof(result), "{\"error\":\"%s\"}", esc);
        return result;
    }

    MachineConfig_ToJson(&mc, result, sizeof(result));
    return result;
}

EMSCRIPTEN_EXPORT const char* ValidateMachineINI(const char* iniText)
{
    static char result[MC_ERR_LEN];
    MachineConfig mc;
    if (!parse_machine_ini(iniText, &mc, result, sizeof(result))) return result;
    result[0] = '\0';   // valid
    return result;
}


// =========================================================
// HDLC frame bridge (gateway WebSocket <-> COM5025 / DMA)
// =========================================================
// RX: gateway -> HDLC_InjectRxFrame -> DMAReceiver_ReceiveDataFromModem
// TX: Modem_SendBytes -> HDLC_QueueTxFrame -> worker polls HDLC_PollTxFrame
// Carrier: gateway TCP accept/close -> HDLC_SetCarrier -> Modem_SetCarrierPresent

#define HDLC_TX_RING_SIZE 16
/* Larger than hdlcFrame.h HDLC_MAX_FRAME_SIZE; distinct name avoids macro clash */
#define HDLC_WASM_TX_BUF 2048

static struct {
    int channel;
    int length;
    uint8_t data[HDLC_WASM_TX_BUF];
} hdlc_tx_ring[HDLC_TX_RING_SIZE];

static int hdlc_tx_head = 0;
static int hdlc_tx_tail = 0;
static int hdlc_last_tx_channel = 0;
static int hdlc_last_tx_length = 0;
static uint8_t *hdlc_last_tx_buffer = NULL;

// Inject bytes from gateway TCP (via WebSocket 0x10) into the HDLC DMA receiver path
EMSCRIPTEN_EXPORT void HDLC_InjectRxFrame(int channel, const uint8_t *data, int length)
{
    if (!data || length <= 0) return;
    if (channel < 0 || channel >= HDLC_CHANNEL_COUNT || !hdlc_devices[channel]) return;
    HDLC_BridgeInjectRx(hdlc_devices[channel], data, length);
}

// Poll for a transmitted HDLC frame from the emulator
// Returns 1 if a frame is available, 0 if not
EMSCRIPTEN_EXPORT int HDLC_PollTxFrame(void)
{
    if (hdlc_tx_head == hdlc_tx_tail) return 0;

    hdlc_last_tx_channel = hdlc_tx_ring[hdlc_tx_tail].channel;
    hdlc_last_tx_length = hdlc_tx_ring[hdlc_tx_tail].length;
    hdlc_last_tx_buffer = hdlc_tx_ring[hdlc_tx_tail].data;
    hdlc_tx_tail = (hdlc_tx_tail + 1) % HDLC_TX_RING_SIZE;
    return 1;
}

EMSCRIPTEN_EXPORT int HDLC_GetLastTxChannel(void) { return hdlc_last_tx_channel; }
EMSCRIPTEN_EXPORT int HDLC_GetLastTxLength(void) { return hdlc_last_tx_length; }
EMSCRIPTEN_EXPORT uint8_t* HDLC_GetLastTxBuffer(void) { return hdlc_last_tx_buffer; }

// Carrier from gateway when a TCP client connects/disconnects (WebSocket 0x12)
EMSCRIPTEN_EXPORT void HDLC_SetCarrier(int channel, int present)
{
    if (channel < 0 || channel >= HDLC_CHANNEL_COUNT || !hdlc_devices[channel]) return;
    HDLCData *hd = (HDLCData *)hdlc_devices[channel]->deviceData;
    if (!hd || !hd->modem) return;
    Modem_SetCarrierPresent(hd->modem, present != 0);
}

// Called from modem.c (Emscripten) when the HDLC DMA path sends a frame to the wire
void HDLC_QueueTxFrame(int channel, const uint8_t *data, int length)
{
    int next = (hdlc_tx_head + 1) % HDLC_TX_RING_SIZE;
    if (next == hdlc_tx_tail) return;  // ring full, drop frame

    if (length > HDLC_WASM_TX_BUF) length = HDLC_WASM_TX_BUF;
    hdlc_tx_ring[hdlc_tx_head].channel = channel;
    hdlc_tx_ring[hdlc_tx_head].length = length;
    memcpy(hdlc_tx_ring[hdlc_tx_head].data, data, length);
    hdlc_tx_head = next;
}

// Set a callback for terminal output (traditional callback approach)
EMSCRIPTEN_EXPORT void SetTerminalOutputCallback(int identCode, void (*callback)(int identCode, char c))
{
    for (int i = 0; i < MAX_TERMINALS; i++) {
        Device *term = terminals[i];

        if (term)
        {
            if (term->identCode == identCode) {
                terminalOutputCallbacks[i] = callback;
                return;
            }
        }
    }
}

// =========================================================
// Character Device Output Ring Buffers
// =========================================================
// Separate ring buffers per device class so JS can poll each independently.
// Each entry: (deviceType << 8) | (charCode & 0xFF)
// deviceType encodes the class: TERMINAL=0, PRINTER=1, PAPERTAPE_WRITER=2, HDLC=3

#define CHARDEV_BUF_SIZE 4096

// Device class codes for JS routing
#define DEVCLASS_TERMINAL        0
#define DEVCLASS_PRINTER         1
#define DEVCLASS_PAPERTAPE_WRITER 2

// Printer output ring buffer
static struct {
    uint16_t entries[CHARDEV_BUF_SIZE];
    volatile int writePos;
    volatile int readPos;
} printerOutputBuf = { .writePos = 0, .readPos = 0 };

// Paper tape writer output ring buffer
static struct {
    uint16_t entries[CHARDEV_BUF_SIZE];
    volatile int writePos;
    volatile int readPos;
} ptWriterOutputBuf = { .writePos = 0, .readPos = 0 };

// Printer character device output handler
static void WasmPrinterOutputHandler(Device *device, char c)
{
    (void)device;
    int next = (printerOutputBuf.writePos + 1) % CHARDEV_BUF_SIZE;
    if (next != printerOutputBuf.readPos) {
        printerOutputBuf.entries[printerOutputBuf.writePos] = (uint16_t)(c & 0xFF);
        printerOutputBuf.writePos = next;
    }
    // Feed into PDF pipeline
    if (wasmPrintJob) {
        PrintJob_PutChar(wasmPrintJob, c);
    }
}

// Paper tape writer character device output handler
static void WasmPaperTapeWriterOutputHandler(Device *device, char c)
{
    (void)device;
    int next = (ptWriterOutputBuf.writePos + 1) % CHARDEV_BUF_SIZE;
    if (next != ptWriterOutputBuf.readPos) {
        ptWriterOutputBuf.entries[ptWriterOutputBuf.writePos] = (uint16_t)(c & 0xFF);
        ptWriterOutputBuf.writePos = next;
    }
}

// Poll printer output - returns charCode or -1 if empty
EMSCRIPTEN_EXPORT int PollPrinterOutput(void)
{
    if (printerOutputBuf.readPos == printerOutputBuf.writePos) return -1;
    uint16_t entry = printerOutputBuf.entries[printerOutputBuf.readPos];
    printerOutputBuf.readPos = (printerOutputBuf.readPos + 1) % CHARDEV_BUF_SIZE;
    return (int)entry;
}

// Poll paper tape writer output - returns charCode or -1 if empty
EMSCRIPTEN_EXPORT int PollPaperTapeWriterOutput(void)
{
    if (ptWriterOutputBuf.readPos == ptWriterOutputBuf.writePos) return -1;
    uint16_t entry = ptWriterOutputBuf.entries[ptWriterOutputBuf.readPos];
    ptWriterOutputBuf.readPos = (ptWriterOutputBuf.readPos + 1) % CHARDEV_BUF_SIZE;
    return (int)entry;
}

// =========================================================
// Printer PDF Pipeline Exports
// =========================================================

// Check for job timeout - returns 1 if a job was flushed, 0 otherwise
EMSCRIPTEN_EXPORT int PrinterCheckTimeout(void)
{
    if (!wasmPrintJob) return 0;
    return PrintJob_CheckTimeout(wasmPrintJob) ? 1 : 0;
}

// Force flush the current job
EMSCRIPTEN_EXPORT void PrinterFlushJob(void)
{
    if (wasmPrintJob) PrintJob_Flush(wasmPrintJob);
}

// Last completed job metadata
EMSCRIPTEN_EXPORT int PrinterGetLastCompletedJob(void)
{
    return wasmPrintJob ? wasmPrintJob->lastCompletedJob : 0;
}

EMSCRIPTEN_EXPORT int PrinterGetLastJobStartTime(void)
{
    return wasmPrintJob ? (int)wasmPrintJob->lastJobStartTime : 0;
}

EMSCRIPTEN_EXPORT int PrinterGetLastJobEndTime(void)
{
    return wasmPrintJob ? (int)wasmPrintJob->lastJobEndTime : 0;
}

EMSCRIPTEN_EXPORT int PrinterGetLastJobBytes(void)
{
    return wasmPrintJob ? wasmPrintJob->lastJobBytes : 0;
}

EMSCRIPTEN_EXPORT int PrinterGetLastJobLines(void)
{
    return wasmPrintJob ? wasmPrintJob->lastJobLines : 0;
}

// Active job metadata
EMSCRIPTEN_EXPORT int PrinterGetActiveJobBytes(void)
{
    return wasmPrintJob ? wasmPrintJob->jobByteCount : 0;
}

EMSCRIPTEN_EXPORT int PrinterGetActiveJobLines(void)
{
    return wasmPrintJob ? wasmPrintJob->jobLineCount : 0;
}

EMSCRIPTEN_EXPORT int PrinterIsJobActive(void)
{
    return (wasmPrintJob && wasmPrintJob->jobActive) ? 1 : 0;
}

EMSCRIPTEN_EXPORT int PrinterGetJobNumber(void)
{
    return wasmPrintJob ? wasmPrintJob->jobNumber : 0;
}

// Printer type management
EMSCRIPTEN_EXPORT int PrinterGetType(void)
{
    return wasmPrintJob ? (int)wasmPrintJob->printerType : 0;
}

EMSCRIPTEN_EXPORT void PrinterSetType(int type)
{
    if (!wasmPrintJob) return;
    if (type < 0 || type > 1) return;
    if ((int)wasmPrintJob->printerType == type) return;

    // Flush current job, preserve job counter, recreate with new type
    int savedJobNumber = wasmPrintJob->jobNumber;
    char *savedDir = strdup(wasmPrintJob->outputDir);
    if (!savedDir) return;   /* keep the current job rather than lose its directory */
    PrintJob_Destroy(wasmPrintJob);
    wasmPrintJob = PrintJob_Create((PjPrinterType)type, PJ_FORMAT_PDF, savedDir);
    if (wasmPrintJob) {
        wasmPrintJob->jobNumber = savedJobNumber;
    }
    free(savedDir);
}

// =========================================================
// Paper Tape Reader - Load tape data from JS
// =========================================================
EMSCRIPTEN_EXPORT void LoadPaperTape(uint8_t *data, int length)
{
    Device *ptr = DeviceManager_GetDeviceByAddress(0400);
    if (ptr && data && length > 0) {
        PaperTape_LoadTape(ptr, data, (size_t)length);
    }
}

// =========================================================
// Paper Tape Writer - Get punched tape data for JS download
// =========================================================
EMSCRIPTEN_EXPORT int GetPaperTapeWriterDataLength(void)
{
    Device *ptw = DeviceManager_GetDeviceByAddress(0410);
    if (!ptw) return 0;
    size_t length = 0;
    PaperTapeWriter_GetTapeData(ptw, &length);
    return (int)length;
}

EMSCRIPTEN_EXPORT uint8_t* GetPaperTapeWriterDataPtr(void)
{
    Device *ptw = DeviceManager_GetDeviceByAddress(0410);
    if (!ptw) return NULL;
    size_t length = 0;
    return (uint8_t*)PaperTapeWriter_GetTapeData(ptw, &length);
}

/* =========================================================
   Debugger API - Uses DAP debugger infrastructure for WASM
   Single-threaded, calls through debugger.c public API
   ========================================================= */

// --- Execution Control ---

EMSCRIPTEN_EXPORT void Dbg_SetPaused(int paused)
{
    dbg_paused = paused;
#ifdef WITH_DEBUGGER
    if (paused) {
        /* Request pause - set mode directly for single-threaded WASM */
        set_cpu_run_mode(CPU_PAUSED);
        set_debugger_request_pause(false);
        set_debugger_control_granted(false);
    } else {
        /* Resume - clear all debugger flags and set running */
        set_debugger_request_pause(false);
        set_debugger_control_granted(false);
        set_cpu_run_mode(CPU_RUNNING);
    }
#endif
}

EMSCRIPTEN_EXPORT int Dbg_IsPaused(void)
{
#ifdef WITH_DEBUGGER
    CPURunMode mode = get_cpu_run_mode();
    return (mode == CPU_PAUSED || mode == CPU_BREAKPOINT) ? 1 : 0;
#else
    return dbg_paused;
#endif
}

EMSCRIPTEN_EXPORT int Dbg_StepOne(void)
{
    if (!initialized) return -1;
#ifdef WITH_DEBUGGER
    /* Clear debugger pause flags so machine_run won't get stuck */
    set_debugger_request_pause(false);
    set_debugger_control_granted(false);
    set_cpu_run_mode(CPU_RUNNING);

    dbg_step_in();
    /* step_cpu sets up step breakpoint and calls ensure_cpu_running */
    machine_run(1);

    /* Ensure we end in a paused state after stepping */
    if (get_cpu_run_mode() == CPU_RUNNING) {
        set_cpu_run_mode(CPU_PAUSED);
    }
    return (int)gPC;
#else
    machine_run(1);
    IO_Tick();
    return (int)gPC;
#endif
}

EMSCRIPTEN_EXPORT int Dbg_StepOver(void)
{
    if (!initialized) return -1;
#ifdef WITH_DEBUGGER
    /* Clear debugger pause flags so machine_run won't get stuck */
    set_debugger_request_pause(false);
    set_debugger_control_granted(false);
    set_cpu_run_mode(CPU_RUNNING);

    dbg_step_over();
    /* step_cpu sets temp breakpoint at return addr for calls, or step_one for simple instr */
    machine_run(1000);

    /* Ensure we end in a paused state after stepping */
    if (get_cpu_run_mode() == CPU_RUNNING) {
        set_cpu_run_mode(CPU_PAUSED);
    }
    return (int)gPC;
#else
    machine_run(1);
    return (int)gPC;
#endif
}

EMSCRIPTEN_EXPORT int Dbg_StepOut(void)
{
    if (!initialized) return -1;
#ifdef WITH_DEBUGGER
    /* Clear debugger pause flags so machine_run won't get stuck */
    set_debugger_request_pause(false);
    set_debugger_control_granted(false);
    set_cpu_run_mode(CPU_RUNNING);

    dbg_step_out();
    /* step_cpu sets breakpoint at return address from stack trace */
    machine_run(10000);

    /* Ensure we end in a paused state after stepping */
    if (get_cpu_run_mode() == CPU_RUNNING) {
        set_cpu_run_mode(CPU_PAUSED);
    }
    return (int)gPC;
#else
    machine_run(1);
    return (int)gPC;
#endif
}

EMSCRIPTEN_EXPORT int Dbg_RunWithBreakpoints(int maxSteps)
{
    if (!initialized) return 0;

    /* Ensure clean state for running */
    set_debugger_request_pause(false);
    set_debugger_control_granted(false);

    if (get_cpu_run_mode() != CPU_RUNNING) {
        set_cpu_run_mode(CPU_RUNNING);
    }

    for (int i = 0; i < maxSteps; i++) {
        machine_run(1);
        /* machine_run(1)->cpu_run(1) already checks breakpoints internally
           and sets cpu_run_mode to CPU_BREAKPOINT if hit */
        CPURunMode mode = get_cpu_run_mode();
        if (mode != CPU_RUNNING) {
            return maxSteps - i;
        }
    }
    return 0; /* all steps consumed */
}

// --- Register Access (current runlevel) ---

EMSCRIPTEN_EXPORT int Dbg_GetPC(void)     { return (int)gPC; }
EMSCRIPTEN_EXPORT int Dbg_GetRegA(void)   { return (int)gA; }
EMSCRIPTEN_EXPORT int Dbg_GetRegD(void)   { return (int)gD; }
EMSCRIPTEN_EXPORT int Dbg_GetRegB(void)   { return (int)gB; }
EMSCRIPTEN_EXPORT int Dbg_GetRegT(void)   { return (int)gT; }
EMSCRIPTEN_EXPORT int Dbg_GetRegL(void)   { return (int)gL; }
EMSCRIPTEN_EXPORT int Dbg_GetRegX(void)   { return (int)gX; }
EMSCRIPTEN_EXPORT int Dbg_GetSTS(void)    { return (int)gSTSr; }
EMSCRIPTEN_EXPORT int Dbg_GetPIL(void)    { return (int)gPIL; }
EMSCRIPTEN_EXPORT int Dbg_GetEA(void)     { return (int)gEA; }

// --- Register Write (current runlevel) ---

EMSCRIPTEN_EXPORT void Dbg_SetPC(int val)   { gPC  = (ushort)(val & 0xFFFF); }
EMSCRIPTEN_EXPORT void Dbg_SetRegA(int val)  { gA   = (ushort)(val & 0xFFFF); }
EMSCRIPTEN_EXPORT void Dbg_SetRegD(int val)  { gD   = (ushort)(val & 0xFFFF); }
EMSCRIPTEN_EXPORT void Dbg_SetRegB(int val)  { gB   = (ushort)(val & 0xFFFF); }
EMSCRIPTEN_EXPORT void Dbg_SetRegT(int val)  { gT   = (ushort)(val & 0xFFFF); }
EMSCRIPTEN_EXPORT void Dbg_SetRegL(int val)  { gL   = (ushort)(val & 0xFFFF); }
EMSCRIPTEN_EXPORT void Dbg_SetRegX(int val)  { gX   = (ushort)(val & 0xFFFF); }
EMSCRIPTEN_EXPORT void Dbg_SetSTS(int val)   {
    /* STS MSB is shared, LSB is per-level */
    gReg->reg_STS = (ushort)(val & 0xFF00);
    gReg->reg[gPIL][_STS] = (ushort)(val & 0x00FF);
}

// --- Register access for any runlevel ---

EMSCRIPTEN_EXPORT int Dbg_GetRegAtLevel(int level, int regIndex)
{
    if (level < 0 || level > 15 || regIndex < 0 || regIndex > 15) return -1;
    if (regIndex == _STS) {
        /* STS is split: MSB shared, LSB per-level */
        return (int)((gReg->reg_STS & 0xFF00) | (gReg->reg[level][_STS] & 0x00FF));
    }
    return (int)gReg->reg[level][regIndex];
}

// --- Privileged System Registers (read-only) ---

EMSCRIPTEN_EXPORT int Dbg_GetPANS(void)   { return (int)gPANS; }
EMSCRIPTEN_EXPORT int Dbg_GetOPR(void)    { return (int)gOPR; }
EMSCRIPTEN_EXPORT int Dbg_GetPGS(void)    { return (int)gPGS; }
EMSCRIPTEN_EXPORT int Dbg_GetPVL(void)    { return (int)gPVL; }
EMSCRIPTEN_EXPORT int Dbg_GetIIC(void)    { return (int)gIIC; }
EMSCRIPTEN_EXPORT int Dbg_GetIID(void)    { return (int)gIID; }
EMSCRIPTEN_EXPORT int Dbg_GetPID(void)    { return (int)gPID; }
EMSCRIPTEN_EXPORT int Dbg_GetPIE(void)    { return (int)gPIE; }
EMSCRIPTEN_EXPORT int Dbg_GetCSR(void)    { return (int)gCSR; }
EMSCRIPTEN_EXPORT int Dbg_GetALD(void)    { return (int)gALD; }
EMSCRIPTEN_EXPORT int Dbg_GetPES(void)    { return (int)gPES; }
EMSCRIPTEN_EXPORT int Dbg_GetPGC(void)    { return (int)gPGC; }
EMSCRIPTEN_EXPORT int Dbg_GetPEA(void)    { return (int)gPEA; }
EMSCRIPTEN_EXPORT int Dbg_GetPCR(int level)
{
    if (level < 0 || level > 15) return -1;
    return (int)gReg->reg_PCR[level];
}

// --- Privileged System Registers (write-only but readable from struct) ---

EMSCRIPTEN_EXPORT int Dbg_GetPANC(void)   { return (int)gPANC; }
EMSCRIPTEN_EXPORT int Dbg_GetLMP(void)    { return (int)gLMP; }
EMSCRIPTEN_EXPORT int Dbg_GetIIE(void)    { return (int)gIIE; }
EMSCRIPTEN_EXPORT int Dbg_GetCCL(void)    { return (int)gCCL; }
EMSCRIPTEN_EXPORT int Dbg_GetLCIL(void)   { return (int)gLCIL; }
EMSCRIPTEN_EXPORT int Dbg_GetUCIL(void)   { return (int)gUCIL; }
EMSCRIPTEN_EXPORT int Dbg_GetECCR(void)   { return (int)gECCR; }

// --- Instruction Counter ---

EMSCRIPTEN_EXPORT double Dbg_GetInstrCount(void)
{
    /* Return as double since JS numbers can hold 53-bit integers */
    return (double)instr_counter;
}

// --- CPU State ---

EMSCRIPTEN_EXPORT int Dbg_GetRunMode(void)
{
    return (int)get_cpu_run_mode();
}

EMSCRIPTEN_EXPORT int Dbg_GetStopReason(void)
{
    return (int)get_cpu_stop_reason();
}

// --- Memory Access ---

EMSCRIPTEN_EXPORT int Dbg_ReadMemory(int addr)
{
    return (int)MemoryRead((ushort)(addr & 0xFFFF), false);
}

// --- Bulk Memory Read (for SINTRAN data structure inspection) ---

static uint16_t mem_block_buffer[4096];

EMSCRIPTEN_EXPORT int Dbg_ReadMemoryBlock(int startAddr, int count)
{
    if (count <= 0 || count > 4096) count = 4096;
    for (int i = 0; i < count; i++) {
        mem_block_buffer[i] = MemoryRead((ushort)((startAddr + i) & 0xFFFF), false);
    }
    return (int)(uintptr_t)mem_block_buffer;
}

EMSCRIPTEN_EXPORT void Dbg_WriteMemory(int addr, int val)
{
    MemoryWrite((ushort)val, (ushort)(addr & 0xFFFF), false, 2);
}

// --- Physical Memory Dump (raw physical memory, bypasses MMS) ---

EMSCRIPTEN_EXPORT int Dbg_DumpPhysicalMemory(int wordCount)
{
    if (wordCount <= 0 || wordCount > (int)(sizeof(VolatileMemory) / sizeof(ushort)))
        wordCount = 256 * 1024;

    FILE *f = fopen("/nd100_physmem.bin", "wb");
    if (!f) return -1;

    for (int i = 0; i < wordCount; i++) {
        ushort w = VolatileMemory.n_Array[i];
        unsigned char hi = (w >> 8) & 0xFF;
        unsigned char lo = w & 0xFF;
        fputc(hi, f);
        fputc(lo, f);
    }
    fclose(f);
    return 0;
}

EMSCRIPTEN_EXPORT int Dbg_GetPhysMemWords(void)
{
    return (int)(sizeof(VolatileMemory) / sizeof(ushort));
}

// --- Breakpoints ---

EMSCRIPTEN_EXPORT void Dbg_AddBreakpoint(int addr)
{
    breakpoint_manager_add((uint16_t)(addr & 0xFFFF), BP_TYPE_USER, NULL, NULL, NULL);
}

EMSCRIPTEN_EXPORT void Dbg_RemoveBreakpoint(int addr)
{
    breakpoint_manager_remove((uint16_t)(addr & 0xFFFF), BP_TYPE_USER);
}

EMSCRIPTEN_EXPORT void Dbg_ClearBreakpoints(void)
{
    breakpoint_manager_clear();
}

// --- Breakpoint Listing ---

static char bp_list_buffer[4096];

extern BreakpointManager *mgr;

EMSCRIPTEN_EXPORT const char* Dbg_GetBreakpointList(void)
{
    int pos = 0;
    bp_list_buffer[0] = '\0';

    if (!mgr) return bp_list_buffer;

    for (int h = 0; h < HASH_SIZE; h++) {
        BreakpointEntry *curr = mgr->buckets[h];
        while (curr && pos < (int)sizeof(bp_list_buffer) - 64) {
            int n = snprintf(bp_list_buffer + pos, sizeof(bp_list_buffer) - pos,
                "%d %d %d\n", curr->address, curr->type, curr->hitCount);
            if (n > 0) pos += n;
            curr = curr->next;
        }
    }
    bp_list_buffer[pos] = '\0';
    return bp_list_buffer;
}

// --- Watchpoints (memory access breakpoints) ---

EMSCRIPTEN_EXPORT int Dbg_AddWatchpoint(int addr, int type)
{
    return watchpoint_add((uint16_t)(addr & 0xFFFF), (WatchpointType)type, WATCH_SPACE_ANY, -1);
}

EMSCRIPTEN_EXPORT void Dbg_RemoveWatchpoint(int addr)
{
    watchpoint_remove((uint16_t)(addr & 0xFFFF));
}

EMSCRIPTEN_EXPORT void Dbg_ClearWatchpoints(void)
{
    watchpoint_clear();
}

EMSCRIPTEN_EXPORT int Dbg_GetWatchpointCount(void)
{
    return watchpoint_get_count();
}

EMSCRIPTEN_EXPORT int Dbg_GetWatchpointAddr(int index)
{
    uint16_t addr;
    int type;
    if (watchpoint_get(index, &addr, &type) == 0)
        return (int)addr;
    return -1;
}

EMSCRIPTEN_EXPORT int Dbg_GetWatchpointType(int index)
{
    uint16_t addr;
    int type;
    if (watchpoint_get(index, &addr, &type) == 0)
        return type;
    return -1;
}

// --- Disassembly ---

static char disasm_buffer[8192];

EMSCRIPTEN_EXPORT const char* Dbg_Disassemble(int startAddr, int count)
{
    char line[128];
    char mnemonic[64];
    int pos = 0;

    disasm_buffer[0] = '\0';

    for (int i = 0; i < count && pos < (int)sizeof(disasm_buffer) - 128; i++) {
        ushort addr = (ushort)((startAddr + i) & 0xFFFF);
        ushort word = MemoryRead(addr, false);

        OpToStr(mnemonic, sizeof(mnemonic), word);

        int n = snprintf(line, sizeof(line), "%06o %06o %s\n", addr, word, mnemonic);
        if (n > 0 && pos + n < (int)sizeof(disasm_buffer)) {
            memcpy(disasm_buffer + pos, line, n);
            pos += n;
        }
    }
    disasm_buffer[pos] = '\0';
    return disasm_buffer;
}

// --- Inspect buffer disassembly (for segment disassembler window) ---

#define INSPECT_BUF_WORDS 65536
static uint16_t s_inspectBuf[INSPECT_BUF_WORDS];
static int      s_inspectWords = 0;
static int      s_inspectBase  = 0;
/* Worst case is one line (~48 bytes) per word for a full 65536-word segment. */
static char     s_inspectOut[4 * 1024 * 1024];  /* 4MB output buffer */

EMSCRIPTEN_EXPORT void Dbg_LoadInspectBuffer(int jsPtr, int wordCount, int baseAddr)
{
    int n = wordCount < INSPECT_BUF_WORDS ? wordCount : INSPECT_BUF_WORDS;
    s_inspectWords = n;
    s_inspectBase  = baseAddr;
    if (n > 0 && jsPtr != 0) {
        memcpy(s_inspectBuf, (void *)(uintptr_t)jsPtr, (size_t)n * 2);
    }
}

EMSCRIPTEN_EXPORT const char* Dbg_DisassembleFromBuffer(int startWord, int count)
{
    char line[128];
    char mnemonic[64];
    int  pos = 0;

    s_inspectOut[0] = '\0';

    if (count <= 0) count = s_inspectWords - startWord;
    if (startWord < 0) startWord = 0;

    for (int i = 0; i < count; i++) {
        int idx = startWord + i;
        if (idx >= s_inspectWords) break;
        if (pos >= (int)sizeof(s_inspectOut) - 128) break;

        uint16_t word = s_inspectBuf[idx];
        int addr = s_inspectBase + idx;

        OpToStr(mnemonic, sizeof(mnemonic), word);

        int n = snprintf(line, sizeof(line), "%06o %06o %s\n", addr & 0xFFFF, word, mnemonic);
        if (n > 0 && pos + n < (int)sizeof(s_inspectOut)) {
            memcpy(s_inspectOut + pos, line, n);
            pos += n;
        }
    }
    s_inspectOut[pos] = '\0';
    return s_inspectOut;
}

// --- Level info (for thread/runlevel view) ---

static char levels_buffer[4096];

EMSCRIPTEN_EXPORT const char* Dbg_GetLevelInfo(void)
{
    int pos = 0;
    levels_buffer[0] = '\0';

    for (int lev = 0; lev < 16; lev++) {
        ushort pcr = gReg->reg_PCR[lev];
        int ring = pcr & 0x03;
        int pt = (pcr >> 11) & 0x0F;
        int apt = (pcr >> 7) & 0x0F;
        ushort p_reg = gReg->reg[lev][_P];
        ushort sts_lsb = gReg->reg[lev][_STS] & 0xFF;

        int n = snprintf(levels_buffer + pos, sizeof(levels_buffer) - pos,
            "%d %06o %03o R%d PT%d APT%d\n",
            lev, p_reg, sts_lsb, ring, pt, apt);
        if (n > 0) pos += n;
    }
    levels_buffer[pos] = '\0';
    return levels_buffer;
}

/* =========================================================
   DAP-based variable/scope/stack/thread inspection
   Returns JSON strings for JS consumption
   ========================================================= */

#ifdef WITH_DEBUGGER
EMSCRIPTEN_EXPORT const char* Dbg_GetScopes(void)
{
    return dbg_get_scopes_json();
}

EMSCRIPTEN_EXPORT const char* Dbg_GetVariables(int scopeId)
{
    return dbg_get_variables_json(scopeId);
}

EMSCRIPTEN_EXPORT const char* Dbg_GetThreads(void)
{
    return dbg_get_threads_json();
}

EMSCRIPTEN_EXPORT const char* Dbg_GetStackTrace(void)
{
    return dbg_get_stack_trace_json();
}
#else
EMSCRIPTEN_EXPORT const char* Dbg_GetScopes(void) { return "[]"; }
EMSCRIPTEN_EXPORT const char* Dbg_GetVariables(int scopeId) { (void)scopeId; return "[]"; }
EMSCRIPTEN_EXPORT const char* Dbg_GetThreads(void) { return "[]"; }
EMSCRIPTEN_EXPORT const char* Dbg_GetStackTrace(void) { return "[]"; }
#endif

// --- Physical Memory Access ---
// Dbg_ReadPhysicalMemory is implemented in cpu_mms.c (same TU as native debugger).

static uint16_t phys_block_buffer[4096];

EMSCRIPTEN_EXPORT int Dbg_ReadPhysicalMemoryBlock(int startAddr, int count)
{
    if (count <= 0 || count > 4096) count = 4096;
    int maxAddr = (int)ND_Memsize;
    for (int i = 0; i < count; i++) {
        int addr = startAddr + i;
        if (addr >= 0 && addr < maxAddr)
            phys_block_buffer[i] = VolatileMemory.n_Array[addr];
        else
            phys_block_buffer[i] = 0;
    }
    return (int)(uintptr_t)phys_block_buffer;
}

// --- Page Table Access ---

EMSCRIPTEN_EXPORT int Dbg_GetPageTableCount(void)
{
    /* MMS1 = 4 page tables, MMS2 = 16 page tables */
    return (mmsType == MMS1) ? 4 : 16;
}

EMSCRIPTEN_EXPORT int Dbg_GetPageTableEntryRaw(int pageTable, int vpn)
{
    if (pageTable < 0 || pageTable >= 16) return 0;
    if (vpn < 0 || vpn >= 64) return 0;

    /* Use debugger reader which checks mmsType instead of STS_SEXI,
       so we can read all 16 page tables even when paused at a level without SEXI. */
    PageTableMode ptm = (mmsType == MMS2) ? Sixteen : Four;
    uint pte = GetPageTableEntryForDebugger((uint)pageTable, (uint)vpn, ptm);
    return (int)pte;
}

EMSCRIPTEN_EXPORT int Dbg_GetExtendedMode(void)
{
    return STS_SEXI ? 1 : 0;
}

// --- Drive Info (unified mount registry query) ---

EMSCRIPTEN_EXPORT const char* GetDriveInfo(void)
{
    static char buf[4096];
    int pos = 0;

    (void)init_drive_arrays();   /* a NULL table is handled below */

    MountedDriveInfo_t *smd = list_mount(DRIVE_SMD);
    MountedDriveInfo_t *floppy = list_mount(DRIVE_FLOPPY);
    MountedDriveInfo_t *scsi = list_mount(DRIVE_SCSI);
    MountedDriveInfo_t *wd = list_mount(DRIVE_WINCHESTER);

    pos += snprintf(buf + pos, sizeof(buf) - pos, "[");

    for (int i = 0; i < 4 && pos < (int)sizeof(buf) - 256; i++) {
        if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
        MountedDriveInfo_t *d = smd ? &smd[i] : NULL;
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "{\"type\":\"smd\",\"unit\":%d,\"mounted\":%s,\"name\":\"%s\",\"opfs\":%s,\"gateway\":%s,\"size\":%d}",
            i,
            (d && d->is_mounted) ? "true" : "false",
            (d && d->is_mounted) ? d->name : "",
            (d && d->is_opfs) ? "true" : "false",
            (d && d->is_gateway) ? "true" : "false",
            (d && d->is_mounted) ? (int)d->data_size : 0);
    }

    for (int i = 0; i < 2 && pos < (int)sizeof(buf) - 256; i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
        MountedDriveInfo_t *d = floppy ? &floppy[i] : NULL;
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "{\"type\":\"floppy\",\"unit\":%d,\"mounted\":%s,\"name\":\"%s\",\"opfs\":%s,\"gateway\":%s,\"size\":%d}",
            i,
            (d && d->is_mounted) ? "true" : "false",
            (d && d->is_mounted) ? d->name : "",
            (d && d->is_opfs) ? "true" : "false",
            (d && d->is_gateway) ? "true" : "false",
            (d && d->is_mounted) ? (int)d->data_size : 0);
    }

    for (int i = 0; i < SCSI_MAX_UNITS && pos < (int)sizeof(buf) - 256; i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
        MountedDriveInfo_t *d = scsi ? &scsi[i] : NULL;
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "{\"type\":\"scsi\",\"unit\":%d,\"mounted\":%s,\"name\":\"%s\",\"opfs\":%s,\"gateway\":%s,\"size\":%d}",
            i,
            (d && d->is_mounted) ? "true" : "false",
            (d && d->is_mounted) ? d->name : "",
            (d && d->is_opfs) ? "true" : "false",
            (d && d->is_gateway) ? "true" : "false",
            (d && d->is_mounted) ? (int)d->data_size : 0);
    }

    // Winchester, 2 units. Without these rows the UI cannot tell a mounted
    // Winchester from an absent one, so a disk could be mounted and still look
    // empty on screen.
    for (int i = 0; i < 2 && pos < (int)sizeof(buf) - 256; i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
        MountedDriveInfo_t *d = wd ? &wd[i] : NULL;
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "{\"type\":\"winchester\",\"unit\":%d,\"mounted\":%s,\"name\":\"%s\",\"opfs\":%s,\"gateway\":%s,\"size\":%d}",
            i,
            (d && d->is_mounted) ? "true" : "false",
            (d && d->is_mounted) ? d->name : "",
            (d && d->is_opfs) ? "true" : "false",
            (d && d->is_gateway) ? "true" : "false",
            (d && d->is_mounted) ? (int)d->data_size : 0);
    }

    pos += snprintf(buf + pos, sizeof(buf) - pos, "]");
    buf[sizeof(buf) - 1] = '\0';
    return buf;
}

// Main function for both Emscripten and non-Emscripten builds
int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
#ifdef __EMSCRIPTEN__
    return 0;
#else
    printf("nd100wasm: This program is intended to be compiled to WebAssembly.\n");
    printf("Please use emscripten to compile this program.\n");
    return 0;
#endif
}