/*
 * nd100x - ND100 Virtual Machine
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdatomic.h>

#include "keyboard.h"
#include "nd100x_version.h"   /* generated into the build dir by cmake/git_stamp.cmake */
#include "vscreen.h"
#include "screenmenu.h"
#include "nd100x_types.h"
#include "nd100x_protos.h"   /* show_floppy_menu() from menu.c */
#include "charset.h"
#include "../../devices/devices_types.h"
#include "../../devices/devices_protos.h"
#include "../../devices/hdlc/device_hdlc.h"
#include "../../cpu/cpu_types.h"
#include "../../cpu/cpu_protos.h"

// Forward declaration for floppy menu (conditionally available).
// The floppy-DB browser in menu.c depends on ncurses + libcurl and is not
// compiled on RISC-V or Windows builds, so gate the extern the same way.
#if !defined(__riscv) && !defined(_WIN32)
#endif

// =========================================================
// Internal: set mode and draw the appropriate screen
// =========================================================

static void draw_f12(void);
static void draw_screen_select(MenuState *state, void *telnetServer);
static void draw_release_prompt(MenuState *state);
static void draw_hdlc_status(void);
static void draw_cpu_speed(void);
static void draw_charset(void);
static void draw_panel_switches(void);
static void draw_about(void);
#if !defined(__EMSCRIPTEN__)
static void draw_pending_list(void *telnetServer);
#endif

// Human-readable byte count (e.g. "1.2 KB", "3.4 MB")
static void format_bytes(uint64_t bytes, char *buf, int buflen)
{
    if (bytes < 1024)
        snprintf(buf, buflen, "%" PRIu64 " B", bytes);
    else if (bytes < 1024 * 1024)
        snprintf(buf, buflen, "%.1f KB", (double)bytes / 1024.0);
    else
        snprintf(buf, buflen, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
}

static void menu_set_mode(MenuState *state, MenuMode mode, void *telnetServer)
{
    state->mode = mode;
    switch (mode) {
    case MENU_NONE:
        VScreen_Redraw(&state->screens[*state->activeScreen]);
        break;
    case MENU_F12:
        draw_f12();
        break;
    case MENU_SCREEN_SELECT:
        draw_screen_select(state, telnetServer);
        break;
    case MENU_SCREEN_RELEASE:
        draw_release_prompt(state);
        break;
    case MENU_HDLC_STATUS:
        state->lastRefresh = time(NULL);
        draw_hdlc_status();
        break;
    case MENU_CPU_SPEED:
        state->lastRefresh = time(NULL);
        draw_cpu_speed();
        break;
    case MENU_CHARSET:
        draw_charset();
        break;
    case MENU_PANEL_SWITCHES:
        draw_panel_switches();
        break;
    case MENU_ABOUT:
        draw_about();
        break;
    case MENU_PENDING_LIST:
#if !defined(__EMSCRIPTEN__)
        state->lastRefresh = time(NULL);
        draw_pending_list(telnetServer);
#endif
        break;
    case MENU_MESSAGE:
        break;
    default:
        break;
    }
}

static void menu_show_message(MenuState *state, const char *msg, MenuMode returnTo)
{
    printf("\n%s\n", msg);
    fflush(stdout);
    state->mode = MENU_MESSAGE;
    state->returnTo = returnTo;
    state->messageExpiry = time(NULL) + 1;
}

// =========================================================
// Draw functions
// =========================================================

// CPU speed measurement state
static uint64_t cpu_speed_last_instr = 0;
static struct timespec cpu_speed_last_time;
static bool cpu_speed_initialized = false;

static void draw_cpu_speed(void)
{
    printf("\033[H\033[J");
    printf("=== CPU Speed ===\n\n");

    // Calculate actual speed
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    double actual_mhz = 0;
    if (cpu_speed_initialized) {
        double elapsed = (now.tv_sec - cpu_speed_last_time.tv_sec)
                       + (now.tv_nsec - cpu_speed_last_time.tv_nsec) / 1e9;
        uint64_t delta_instr = g_instr_counter - cpu_speed_last_instr;
        if (elapsed > 0.01) {
            actual_mhz = (delta_instr / elapsed) / 1e6;
        }
    }
    cpu_speed_last_instr = g_instr_counter;
    cpu_speed_last_time = now;
    cpu_speed_initialized = true;

    bool throttle_on = cpu_throttle_get_enabled();

    printf("  Instructions executed: %" PRIu64 "\n", g_instr_counter);
    printf("  Actual speed:          %.3f MHz (%.1f KIPS)\n", actual_mhz, actual_mhz * 1000.0);
    printf("\n");
    printf("  Throttle:              %s\n", throttle_on ? "ON" : "OFF");

    if (throttle_on) {
        double target = cpu_throttle_get_mhz();
        printf("  Target speed:          %.3f MHz\n", target);
        double ratio = actual_mhz > 0 ? actual_mhz / target : 0;
        printf("  Speed ratio:           %.1f%%\n", ratio * 100.0);
    }

    printf("\n  Keys:\n");
    printf("    [T] Toggle throttle on/off\n");
    printf("    [+] Increase target speed 10%%\n");
    printf("    [-] Decrease target speed 10%%\n");
    printf("    [A] Auto-calibrate (set target = current actual speed)\n");
    printf("    [ESC] Back\n");
    printf("\n  Refreshes every second.\n");
    fflush(stdout);
}

static void draw_about(void)
{
    printf("\033[2J\033[H");
    printf("=== About ND100X ===\n\n");
    printf("  ND100X - ND-100/CX Minicomputer Emulator\n");
    printf("  Version %s (git %s)\n", ND100X_VERSION, ND100X_GIT_HASH);
    printf("  Built: %s\n\n", ND100X_BUILD_TIME);
    printf("  A fork of nd100em, started in 2025 by Ronny Hansen.\n");
    printf("  Licensed under the GNU General Public License (GPL v2 or later).\n\n");
    printf("  Based on the nd100em project (version 0.2.4) by:\n");
    printf("    Per-Olof Astrom, Roger Abrahamsson,\n");
    printf("    Zdravko Dimitrov, Goran Axelsson\n\n");
    printf("  Features:\n");
    printf("    - Full ND-100 CPU emulation (MMS1 and MMS2)\n");
    printf("    - SMD and floppy disk support\n");
    printf("    - HDLC networking\n");
    printf("    - DAP debugger integration\n");
    printf("    - Telnet server for remote terminals\n");
    printf("    - WebAssembly browser build\n\n");
    printf("  https://www.ndwiki.org/wiki/ND-100\n\n");
    printf("  Press ESC to return.\n");
    fflush(stdout);
}

static void draw_charset(void)
{
    CharsetVariant active = charset_get();

    printf("\033[2J\033[H");
    printf("=== Character Set (local console only) ===\n\n");
    printf("  National 7-bit ISO 646 charset for the local screen and\n");
    printf("  keyboard. Telnet / TCP traffic is NEVER translated.\n\n");

    // Selectable list
    for (CharsetVariant v = CHARSET_OFF; v < CHARSET_COUNT; v++) {
        printf("  [%d] %s%-10s%s %s\n",
               (int)v + 1,
               (v == active) ? "* " : "  ",
               charset_name(v),
               (v == active) ? " (active)" : "",
               (v == CHARSET_OFF) ? "ASCII passthrough { | } [ \\ ]" : "");
    }

    // Mapping detail for the active variant
    printf("\n  Mappings for %s:\n", charset_name(active));
    int n = charset_mapping_count(active);
    if (n == 0) {
        printf("    (none - 7-bit codes shown as literal ASCII)\n");
    } else {
        printf("    7-bit byte  ASCII  ->  shown as / typed as\n");
        for (int i = 0; i < n; i++) {
            uint8_t byte; const char *glyph; const char *utf8;
            if (charset_mapping_at(active, i, &byte, &glyph, &utf8)) {
                printf("      %03o (0x%02X)   %-2s        %s\n",
                       byte, byte, glyph, utf8);
            }
        }
    }

    printf("\n  Press 1-%d to select, ESC to return.\n", (int)CHARSET_COUNT);
    fflush(stdout);
}

// Operator's-panel switch register (OPR) editor. On a real ND-100 this is the
// bank of 16 front-panel DATA switches read by the "TRA OPR" instruction; nd100x
// has no physical panel so this screen edits gReg->reg_OPR directly. NORD TSS
// samples OPR at cold start (131313 -> create SYSTEM user, TSS1.SYMB:3180) and
// while running (111111 -> verbose disc-error diagnostics, TSS2.SYMB:577/585; and
// on NORD-10 the low 15 bits select a memory word shown in the LEV4 register
// block, TSS1.SYMB:4031). Full reference: docs/TSS-CONTROL-PANEL-SWITCHES.md.
static void draw_panel_switches(void)
{
    uint16_t opr = (g_reg != NULL) ? gOPR : 0;

    printf("\033[2J\033[H");
    printf("=== Control Panel Switches (OPR register / TRA OPR) ===\n\n");
    printf("  The 16 operator's-panel DATA switches, read by 'TRA OPR'.\n");
    printf("  NORD TSS samples this at cold start and while running.\n\n");

    printf("  Current OPR = %06o (octal)   0x%04X   %u (dec)\n\n",
           opr, opr, (unsigned)opr);

    // 16-bit switch display, bit 15 (MSB) down to bit 0 (LSB).
    printf("  bit:");
    for (int b = 15; b >= 0; b--) printf(" %2d", b);
    printf("\n  sw :");
    for (int b = 15; b >= 0; b--) printf("  %c", (opr & (1u << b)) ? '1' : '0');
    printf("\n\n");

    // Decode against the exact values NORD TSS tests (see the doc + source refs).
    printf("  TSS meaning of the current value:\n");
    if (opr == 0131313)
        printf("    131313 -> COLD START: create the SYSTEM user (SINIT/CRUSE)\n");
    else if (opr == 0111111)
        printf("    111111 -> verbose disc-error diagnostics (XDISK error path)\n");
    else if (opr == 025252)
        printf("    025252 -> (NORD-1 only) panel memory examine/deposit tool\n");
    else if (opr == 0)
        printf("    000000 -> normal run (no cold-start action)\n");
    else
        printf("    (no special cold-start action; on NORD-10 the low 15 bits\n"
               "     select the memory word shown in the LEV4 register display)\n");

    printf("\n  Keys:\n");
    printf("    [0-7] shift an octal digit into OPR (builds right-to-left)\n");
    printf("    [C]   clear OPR to 000000\n");
    printf("    [S]   set 131313  (cold start: create the SYSTEM user)\n");
    printf("    [D]   set 111111  (verbose disc-error diagnostics)\n");
    printf("    [ESC] Back\n");
    printf("\n  Takes effect the next time TSS executes 'TRA OPR'.\n");
    fflush(stdout);
}

static void draw_f12(void)
{
    printf("\033[2J\033[H");
    printf("=== ND100X Menu ===\n\n");
    printf("  [1] Floppy Database Browser\n");
    printf("  [2] Virtual Screen Selector\n");
    printf("  [3] HDLC Status\n");
    printf("  [4] CPU Speed\n");
    printf("  [5] Character Set  (local console: %s)\n", charset_name(charset_get()));
    printf("  [6] Control Panel Switches  (OPR = %06o)\n", (unsigned)((g_reg != NULL) ? gOPR : 0));
    printf("  [A] About\n");
    printf("\nPress 1-6/A to select, ESC to cancel: ");
    fflush(stdout);
}

static const char *tx_sender_state_name(int state)
{
    switch (state) {
    case 0: return "Stopped";
    case 1: return "Ready";
    case 2: return "Sending";
    case 3: return "Sent";
    default: return "?";
    }
}

static void draw_hdlc_status(void)
{
    // Home cursor and clear entire screen (repaint in place)
    printf("\033[H\033[J");
    printf("=== HDLC Device Status ===\n\n");

    int count = DeviceManager_GetDeviceCount();
    int found = 0;

    for (int i = 0; i < count; i++) {
        Device *dev = DeviceManager_GetDeviceByIndex(i);
        if (!dev || dev->type != DEVICE_TYPE_HDLC) continue;

        HDLCData *data = (HDLCData *)dev->deviceData;
        if (!data) continue;

        found++;

        char addrStr[16];
        snprintf(addrStr, sizeof(addrStr), "%04o-%04o", dev->startAddress, dev->endAddress);

        bool connected = data->modem ? atomic_load(&data->modem->connected) : false;

        char txBytesStr[16], rxBytesStr[16];
        format_bytes(data->modem ? data->modem->bytesTx : 0, txBytesStr, sizeof(txBytesStr));
        format_bytes(data->modem ? data->modem->bytesRx : 0, rxBytesStr, sizeof(rxBytesStr));

        HDLCRxFrameStatus st;
        bool hasStatus = HDLC_GetRxFrameStatus(data, &st);

        // Device header
        printf("  HDLC #%d  [%s]  Connected: %s\n", data->thumbwheel, addrStr, connected ? "Yes" : "No");

        // Fixed-width snprintf buffers so columns stay aligned regardless of value length
        {
            char c_dma[8], c_bytes[16], c_frames[12], c_ena[16], c_errs[18], c_state[20], c_queue[12];
            char tmpBuf[24];

            // --- RX line ---
            snprintf(c_dma, sizeof(c_dma), "%-3s", (hasStatus && st.rxDmaEnabled) ? "On" : "Off");
            snprintf(c_bytes, sizeof(c_bytes), "%-8s", rxBytesStr);
            snprintf(c_frames, sizeof(c_frames), "%-8" PRIu64, data->framesRx);
            snprintf(c_ena, sizeof(c_ena), "RXE=%-6s", (hasStatus && st.rxEnabled) ? "On" : "Off");
            snprintf(c_errs, sizeof(c_errs), "Errs=%-7" PRIu64, data->framesRxErrors);

            // RX frame assembly state
            const char *rxState = "Idle";
            if (hasStatus) {
                if (!st.rxDmaEnabled) {
                    rxState = "DMA off";
                } else if (!st.rxDcbReady) {
                    rxState = "No DCB";
                } else {
                    switch (st.state) {
                    case 1: case 2:
                        snprintf(tmpBuf, sizeof(tmpBuf), "Rx %d B", st.frameLength);
                        rxState = tmpBuf;
                        break;
                    case 3:
                        rxState = "Error";
                        break;
                    }
                }
            }
            snprintf(c_state, sizeof(c_state), "State=%-10.13s", rxState);

            if (hasStatus) {
                format_bytes(st.tcpQueueUsed, c_queue, sizeof(c_queue));
            } else {
                snprintf(c_queue, sizeof(c_queue), "-");
            }

            printf("    RX: DMA=%s  Bytes=%s  Frames=%s  %s  %s  %s  Queue=%s\n",
                   c_dma, c_bytes, c_frames, c_ena, c_errs, c_state, c_queue);

            // --- TX line ---
            snprintf(c_dma, sizeof(c_dma), "%-3s", (hasStatus && st.txDmaEnabled) ? "On" : "Off");
            snprintf(c_bytes, sizeof(c_bytes), "%-8s", txBytesStr);
            snprintf(c_frames, sizeof(c_frames), "%-8" PRIu64, data->framesTx);
            snprintf(c_ena, sizeof(c_ena), "TXE=%-6s", (hasStatus && st.txEnabled) ? "On" : "Off");
            snprintf(c_errs, sizeof(c_errs), "%-12s", "");  // blank to align with RX Errs column

            const char *txState = hasStatus ? tx_sender_state_name(st.txSenderState) : "Stopped";
            snprintf(c_state, sizeof(c_state), "State=%-10s", txState);

            if (hasStatus) {
                format_bytes(st.txQueueUsed, c_queue, sizeof(c_queue));
            } else {
                snprintf(c_queue, sizeof(c_queue), "-");
            }

            printf("    TX: DMA=%s  Bytes=%s  Frames=%s  %s  %s  %s  Queue=%s\n",
                   c_dma, c_bytes, c_frames, c_ena, c_errs, c_state, c_queue);

            // DCB and TX diagnostics
            if (hasStatus && st.txStarts > 0) {
                printf("    DCB: TX=%-8" PRIu64 " RX=%-8" PRIu64 "  |  Starts=%-6" PRIu64 " Sent=%-6" PRIu64 " Skip=%" PRIu64 "\n",
                       data->dcbTxMarked, data->dcbRxMarked,
                       st.txStarts, data->framesTx, st.txAlreadySent);
                printf("    IRQ: 12=%-6" PRIu64 " 13=%-6" PRIu64 " IDENT13=%-6" PRIu64 " TBMT=%d\n",
                       data->irq12Count, data->irq13Count,
                       data->identCount13,
                       data->txTransferStatus.bits.transmitBufferEmpty);
                printf("    I13: DMA=%-6" PRIu64 " DataAv=%-6" PRIu64 " StatAv=%-6" PRIu64 " Modem=%" PRIu64 "\n",
                       data->irq13_dma, data->irq13_dataAvail,
                       data->irq13_statusAvail, data->irq13_modem);
            }

            // Last TX frames history
            if (data->txHistoryIdx > 0) {
                int total = data->txHistoryIdx < HDLC_TX_HISTORY_SIZE ? data->txHistoryIdx : HDLC_TX_HISTORY_SIZE;
                int start = data->txHistoryIdx >= HDLC_TX_HISTORY_SIZE ? data->txHistoryIdx % HDLC_TX_HISTORY_SIZE : 0;
                printf("    Last %d TX frames:\n", total);
                for (int t = 0; t < total; t++) {
                    int idx = (start + t) % HDLC_TX_HISTORY_SIZE;
                    printf("      #%-3d LP=%06X DA=%06X K=%04X BC=%-3d W=%-3d ",
                           data->txHistoryIdx - total + t + 1,
                           data->txHistory[idx].listPtr,
                           data->txHistory[idx].dataAddr,
                           data->txHistory[idx].keyBefore,
                           data->txHistory[idx].byteCount,
                           data->txHistory[idx].frameSize);
                    for (int b = 0; b < data->txHistory[idx].dataLen; b++) {
                        printf("%02x ", data->txHistory[idx].data[b]);
                    }
                    printf("\n");
                }
            }

            // Dropped bytes line (only shown if any drops occurred)
            uint64_t rxDrop = data->modem ? data->modem->rxDropped : 0;
            uint64_t txDrop = data->modem ? data->modem->txDropped : 0;
            if (rxDrop > 0 || txDrop > 0) {
                char rxDropStr[16], txDropStr[16];
                format_bytes(rxDrop, rxDropStr, sizeof(rxDropStr));
                format_bytes(txDrop, txDropStr, sizeof(txDropStr));
                printf("    ** DROPPED: RX=%s  TX=%s\n", rxDropStr, txDropStr);
            }
        }

        printf("\n");
    }

    if (found == 0) {
        printf("  No HDLC devices configured.\n");
        printf("  Use --hdlc=N:PORT (server) or --hdlc=N:HOST:PORT (client)\n");
    }

    printf("  Refreshes every second. Press ESC to return.\n");
    fflush(stdout);
}

static void draw_screen_select(MenuState *state, void *telnetServer)
{
    bool hasTelnet = (telnetServer != NULL);

    printf("\033[2J\033[H");

#if !defined(__EMSCRIPTEN__)
    if (hasTelnet) {
        TelnetServer *ts = (TelnetServer *)telnetServer;
        int pending = TelnetServer_GetPendingCount(ts);
        if (pending > 0) {
            printf("=== Virtual Screens (telnet port %d, %d pending) ===\n\n",
                   TelnetServer_GetPort(ts), pending);
        } else {
            printf("=== Virtual Screens (telnet port %d) ===\n\n",
                   TelnetServer_GetPort(ts));
        }
    } else
#endif
    {
        printf("=== Virtual Screen Selector ===\n\n");
    }

    for (int i = 0; i < state->screenCount; i++) {
        char statusBuf[128];
        const char *status = "";

        if (i == *state->activeScreen) {
            status = " *";
        } else if (!state->screens[i].isInputCapable) {
            status = " (output only)";
        }
#if !defined(__EMSCRIPTEN__)
        else if (hasTelnet) {
            TelnetServer *ts = (TelnetServer *)telnetServer;
            if (TelnetServer_IsDeviceConnected(ts, state->screens[i].device)) {
                const char *addr = TelnetServer_GetDeviceClientAddr(ts, state->screens[i].device);

                // Find terminal index in server for byte stats
                uint64_t rx = 0, tx = 0;
                int tcount = TelnetServer_GetTerminalCount(ts);
                for (int t = 0; t < tcount; t++) {
                    const char *tname = NULL;
                    bool conn = false;
                    TelnetServer_GetTerminalStatus(ts, t, &tname, NULL, &conn, NULL, NULL, 0);
                    if (conn && tname && strcmp(tname, state->screens[i].name) == 0) {
                        TelnetServer_GetTerminalStats(ts, t, &rx, &tx);
                        break;
                    }
                }

                char rxStr[16], txStr[16];
                format_bytes(rx, rxStr, sizeof(rxStr));
                format_bytes(tx, txStr, sizeof(txStr));

                if (addr && addr[0]) {
                    snprintf(statusBuf, sizeof(statusBuf),
                             " [Telnet %s] rx:%s tx:%s", addr, rxStr, txStr);
                } else {
                    snprintf(statusBuf, sizeof(statusBuf),
                             " [Telnet] rx:%s tx:%s", rxStr, txStr);
                }
                status = statusBuf;
            } else if (!state->screens[i].localActive) {
                status = " [Inactive]";
            } else if (state->screens[i].isInputCapable && i > 0) {
                status = " [Virtual]";
            }
        }
#else
        else if (!state->screens[i].localActive) {
            status = " [Inactive]";
        }
        (void)telnetServer;
#endif

        if (i < 9)
            printf("  [%d] %-24s%s\n", i + 1, state->screens[i].name, status);
        else
            printf("  [%c] %-24s%s\n", 'a' + (i - 9), state->screens[i].name, status);
    }

#if !defined(__EMSCRIPTEN__)
    if (hasTelnet) {
        printf("\n  [R] Release terminal (virtual->inactive, or disconnect telnet)");
        printf("\n  [P] Pending connections (live view)");
        printf("\n\nPress 1-%d/a to switch, R/P for options, ESC to cancel: ",
               state->screenCount > 9 ? 9 : state->screenCount);
    } else
#endif
    {
        printf("\nPress 1-%d/a to switch, ESC to cancel: ",
               state->screenCount > 9 ? 9 : state->screenCount);
    }
    fflush(stdout);
}

static void draw_release_prompt(MenuState *state)
{
    printf("\nEnter terminal to release (2-%d): ",
           state->screenCount > 9 ? 9 : state->screenCount);
    fflush(stdout);
}

#if !defined(__EMSCRIPTEN__)
static void draw_pending_list(void *telnetServer)
{
    TelnetServer *ts = (TelnetServer *)telnetServer;
    int count = TelnetServer_GetPendingCount(ts);

    printf("\033[2J\033[H");
    printf("=== Pending Telnet Connections (live, port %d) ===\n\n",
           TelnetServer_GetPort(ts));

    if (count == 0) {
        printf("  No pending connections.\n");
    } else {
        printf("  #  %-24s  %-10s  %-10s  %s\n", "Address", "RX", "TX", "Age");
        printf("  -  %-24s  %-10s  %-10s  %s\n", "-------", "--", "--", "---");
        for (int i = 0; i < count; i++) {
            char addr[48];
            int age = 0;
            uint64_t rx = 0, tx = 0;
            if (TelnetServer_GetPendingInfo(ts, i, addr, sizeof(addr), &age, &rx, &tx)) {
                char rxStr[16], txStr[16];
                format_bytes(rx, rxStr, sizeof(rxStr));
                format_bytes(tx, txStr, sizeof(txStr));
                printf("  %d) %-24s  %-10s  %-10s  %ds / 60s\n",
                       i + 1, addr, rxStr, txStr, age);
            }
        }
    }

    printf("\n  [D] Drop connection  [A] Drop all  [ESC] Back\n");
    printf("\nRefreshes every 2 seconds. Press key: ");
    fflush(stdout);
}
#endif

// =========================================================
// Public API
// =========================================================

void menu_init(MenuState *state, VScreen *screens, int screenCount, int *activeScreen)
{
    memset(state, 0, sizeof(MenuState));
    state->screens = screens;
    state->screenCount = screenCount;
    state->activeScreen = activeScreen;
}

#if !defined(__EMSCRIPTEN__)
void menu_enter(MenuState *state, TelnetServer *telnetServer)
#else
void menu_enter(MenuState *state, void *telnetServer)
#endif
{
    menu_set_mode(state, MENU_F12, telnetServer);
}

#if !defined(__EMSCRIPTEN__)
void menu_tick(MenuState *state, TelnetServer *telnetServer)
#else
void menu_tick(MenuState *state, void *telnetServer)
#endif
{
    if (state->mode == MENU_MESSAGE && time(NULL) >= state->messageExpiry) {
        menu_set_mode(state, state->returnTo, telnetServer);
    }
#if !defined(__EMSCRIPTEN__)
    // Live refresh for pending list view (every 2 seconds)
    if (state->mode == MENU_PENDING_LIST && telnetServer) {
        time_t now = time(NULL);
        if (now - state->lastRefresh >= 2) {
            state->lastRefresh = now;
            draw_pending_list(telnetServer);
        }
    }
#endif
    // Live refresh for HDLC status view (every 1 second)
    if (state->mode == MENU_HDLC_STATUS) {
        time_t now = time(NULL);
        if (now - state->lastRefresh >= 1) {
            state->lastRefresh = now;
            draw_hdlc_status();
        }
    }
    // Live refresh for CPU speed view (every 1 second)
    if (state->mode == MENU_CPU_SPEED) {
        time_t now = time(NULL);
        if (now - state->lastRefresh >= 1) {
            state->lastRefresh = now;
            draw_cpu_speed();
        }
    }
}

// =========================================================
// Key handler - processes one keypress per call
// =========================================================

#if !defined(__EMSCRIPTEN__)
void menu_process_key(MenuState *state, const KeyEvent *key, TelnetServer *telnetServer)
#else
void menu_process_key(MenuState *state, const KeyEvent *key, void *telnetServer)
#endif
{
    if (!key || key->type == KEY_NONE) return;

    // The menu only cares about ESC or typed characters. Function keys and
    // multi-byte unknown sequences are ignored.
    bool is_esc = (key->type == KEY_ESCAPE);
    char ch     = (key->type == KEY_CHAR) ? key->ch : '\0';

    switch (state->mode) {

    // ----- F12 top-level menu -----
    case MENU_F12:
        if (is_esc) {
            menu_set_mode(state, MENU_NONE, telnetServer);
        } else if (ch == '1') {
#if defined(__riscv) || defined(_WIN32)
            printf("\nFloppy menu not available on this build\n");
            fflush(stdout);
#else
            int ret = show_floppy_menu();
            if (ret == -1) printf("Failed to show floppy menu\n");
#endif
            menu_set_mode(state, MENU_NONE, telnetServer);
        } else if (ch == '2') {
            menu_set_mode(state, MENU_SCREEN_SELECT, telnetServer);
        } else if (ch == '3') {
            menu_set_mode(state, MENU_HDLC_STATUS, telnetServer);
        } else if (ch == '4') {
            cpu_speed_initialized = false;
            menu_set_mode(state, MENU_CPU_SPEED, telnetServer);
        } else if (ch == '5') {
            menu_set_mode(state, MENU_CHARSET, telnetServer);
        } else if (ch == '6') {
            menu_set_mode(state, MENU_PANEL_SWITCHES, telnetServer);
        } else if (ch == 'a' || ch == 'A') {
            menu_set_mode(state, MENU_ABOUT, telnetServer);
        }
        break;

    // ----- Screen selector -----
    case MENU_SCREEN_SELECT:
        if (is_esc) {
            menu_set_mode(state, MENU_NONE, telnetServer);
            return;
        }
#if !defined(__EMSCRIPTEN__)
        if ((ch == 'r' || ch == 'R') && telnetServer) {
            menu_set_mode(state, MENU_SCREEN_RELEASE, telnetServer);
            return;
        }
        if ((ch == 'p' || ch == 'P') && telnetServer) {
            menu_set_mode(state, MENU_PENDING_LIST, telnetServer);
            return;
        }
#endif
        {
            int choice = -1;
            if (ch >= '1' && ch <= '9')
                choice = ch - '1';
            else if (ch >= 'a' && ch <= 'z')
                choice = 9 + (ch - 'a');

            if (choice >= 0 && choice < state->screenCount) {
#if !defined(__EMSCRIPTEN__)
                if (telnetServer && TelnetServer_IsDeviceConnected(
                        telnetServer, state->screens[choice].device)) {
                    menu_show_message(state, "Terminal is in use by telnet client.",
                                      MENU_SCREEN_SELECT);
                    return;
                }

                // If not locally active, re-activate it and clear carrier
                if (telnetServer && state->screens[choice].isInputCapable &&
                    !state->screens[choice].localActive) {
                    state->screens[choice].localActive = true;
                    TelnetServer_SetDeviceLocallyActive(telnetServer,
                        state->screens[choice].device, true);
                    TelnetServer_ClearDeviceCarrier(telnetServer,
                        state->screens[choice].device);
                }
#endif
                *state->activeScreen = choice;
            }
            menu_set_mode(state, MENU_NONE, telnetServer);
        }
        break;

    // ----- Release prompt (unified close/disconnect) -----
    case MENU_SCREEN_RELEASE:
        if (is_esc) {
            menu_set_mode(state, MENU_SCREEN_SELECT, telnetServer);
            return;
        }
        {
            int choice = -1;
            if (ch >= '1' && ch <= '9')
                choice = ch - '1';
            else if (ch >= 'a' && ch <= 'z')
                choice = 9 + (ch - 'a');

            if (choice < 0 || choice >= state->screenCount) {
                menu_show_message(state, "Invalid selection.",
                                  MENU_SCREEN_SELECT);
                return;
            }
            if (choice == 0) {
                menu_show_message(state, "Cannot release the Console.",
                                  MENU_SCREEN_SELECT);
                return;
            }
            if (!state->screens[choice].isInputCapable) {
                menu_show_message(state, "Cannot release output-only screens.",
                                  MENU_SCREEN_SELECT);
                return;
            }

#if !defined(__EMSCRIPTEN__)
            if (telnetServer) {
                // If telnet-connected: disconnect the client
                if (TelnetServer_IsDeviceConnected(telnetServer, state->screens[choice].device)) {
                    TelnetServer_DisconnectDevice(telnetServer, state->screens[choice].device);
                    char msg[64];
                    snprintf(msg, sizeof(msg), "%s telnet client disconnected.",
                             state->screens[choice].name);
                    menu_show_message(state, msg, MENU_SCREEN_SELECT);
                    return;
                }

                // If locally active: release for telnet
                if (state->screens[choice].localActive) {
                    if (choice == *state->activeScreen) {
                        menu_show_message(state, "Cannot release the active screen. Switch first.",
                                          MENU_SCREEN_SELECT);
                        return;
                    }
                    state->screens[choice].localActive = false;
                    TelnetServer_SetDeviceLocallyActive(telnetServer,
                        state->screens[choice].device, false);
                    char msg[64];
                    snprintf(msg, sizeof(msg), "%s released for telnet.",
                             state->screens[choice].name);
                    menu_show_message(state, msg, MENU_SCREEN_SELECT);
                    return;
                }

                // Already inactive
                menu_show_message(state, "Terminal is already inactive.",
                                  MENU_SCREEN_SELECT);
            }
#endif
        }
        break;

#if !defined(__EMSCRIPTEN__)
    // ----- Pending connections (live view) -----
    case MENU_PENDING_LIST:
        if (is_esc) {
            menu_set_mode(state, MENU_SCREEN_SELECT, telnetServer);
            return;
        }
        if (telnetServer) {
            if (ch == 'd' || ch == 'D') {
                int count = TelnetServer_GetPendingCount(telnetServer);
                if (count == 0) {
                    menu_show_message(state, "No pending connections to drop.",
                                      MENU_PENDING_LIST);
                } else if (count == 1) {
                    TelnetServer_DropPending(telnetServer, 0);
                    menu_show_message(state, "Dropped pending connection.",
                                      MENU_PENDING_LIST);
                } else {
                    printf("\nDrop which connection (1-%d)? ", count);
                    fflush(stdout);
                    // We'll handle the digit on the next keypress via a simple approach:
                    // For now, just prompt. The next digit key will be caught here.
                }
            } else if (ch >= '1' && ch <= '9') {
                int idx = ch - '1';
                if (TelnetServer_DropPending(telnetServer, idx)) {
                    draw_pending_list(telnetServer);
                    state->lastRefresh = time(NULL);
                }
            } else if (ch == 'a' || ch == 'A') {
                TelnetServer_DropAllPending(telnetServer);
                menu_show_message(state, "All pending connections dropped.",
                                  MENU_PENDING_LIST);
            }
        }
        break;
#endif

    // ----- HDLC status (live view) -----
    case MENU_HDLC_STATUS:
        if (is_esc) {
            menu_set_mode(state, MENU_F12, telnetServer);
        }
        break;

    case MENU_CPU_SPEED:
        if (is_esc) {
            menu_set_mode(state, MENU_F12, telnetServer);
        } else if (ch == 't' || ch == 'T') {
            cpu_throttle_set_enabled(!cpu_throttle_get_enabled());
        } else if (ch == '+' || ch == '=') {
            cpu_throttle_set_mhz(cpu_throttle_get_mhz() * 1.1);
        } else if (ch == '-') {
            cpu_throttle_set_mhz(cpu_throttle_get_mhz() * 0.9);
        } else if (ch == 'a' || ch == 'A') {
            // Auto-calibrate: measure current actual speed and set as target
            // This only makes sense when throttle is OFF (measuring unthrottled speed)
            // The user should then enable throttle to lock to this speed
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (cpu_speed_initialized) {
                double elapsed = (now.tv_sec - cpu_speed_last_time.tv_sec)
                               + (now.tv_nsec - cpu_speed_last_time.tv_nsec) / 1e9;
                uint64_t delta = g_instr_counter - cpu_speed_last_instr;
                if (elapsed > 0.1) {
                    double actual = (delta / elapsed) / 1e6;
                    cpu_throttle_set_mhz(actual);
                    cpu_throttle_set_enabled(true);
                }
            }
        }
        break;

    case MENU_CHARSET:
        if (is_esc) {
            menu_set_mode(state, MENU_F12, telnetServer);
        } else if (ch >= '1' && ch <= ('0' + CHARSET_COUNT)) {
            charset_set((CharsetVariant)(ch - '1'));
            // Repaint in place so the new selection + mappings show immediately
            draw_charset();
        }
        break;

    // ----- Operator's-panel switch register (OPR) editor -----
    // Edits gReg->reg_OPR live; TSS sees it on its next "TRA OPR". Digits shift in
    // from the right just like keying the physical ND panel data switches.
    case MENU_PANEL_SWITCHES:
        if (is_esc) {
            menu_set_mode(state, MENU_F12, telnetServer);
        } else if (g_reg != NULL) {
            if (ch >= '0' && ch <= '7') {
                gOPR = (uint16_t)(((gOPR << 3) | (uint16_t)(ch - '0')) & 0xFFFFu);
                draw_panel_switches();
            } else if (ch == 'c' || ch == 'C') {
                gOPR = 0;              // clear all switches
                draw_panel_switches();
            } else if (ch == 's' || ch == 'S') {
                gOPR = 0131313;        // cold start: create the SYSTEM user (SINIT)
                draw_panel_switches();
            } else if (ch == 'd' || ch == 'D') {
                gOPR = 0111111;        // verbose disc-error diagnostics (XDISK)
                draw_panel_switches();
            }
        }
        break;

    case MENU_ABOUT:
        if (is_esc) {
            menu_set_mode(state, MENU_F12, telnetServer);
        }
        break;

    case MENU_MESSAGE:
        break;

    default:
        break;
    }
}
