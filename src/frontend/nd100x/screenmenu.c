/*
 * screenmenu.c - F12 menu state machine: screen select, status and settings pages.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "screenmenu.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <time.h>
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include "keyboard.h"
#include "nd100x_version.h" /* generated into the build dir by cmake/git_stamp.cmake */
#include "vscreen.h"
#include "nd100x_types.h"
#include "nd100x_protos.h" /* show_floppy_menu() from menu.c */
#include "charset.h"
#include "eth_decode.h"
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
static void draw_screen_select(MenuState *state, void *telnet_server);
static void draw_release_prompt(MenuState *state);
static void draw_hdlc_status(void);
static void draw_eth_status(void);
static void draw_eth_frames(void);
static void draw_eth_frame_dump(void);
static void draw_cpu_speed(void);
static void draw_charset(void);
static void draw_panel_switches(void);
static void draw_about(void);
#if !defined(__EMSCRIPTEN__)
static void draw_pending_list(void *telnet_server);
#endif

// Human-readable byte count (e.g. "1.2 KB", "3.4 MB")
static void format_bytes(uint64_t bytes, char *buf, int buflen)
{
    if (bytes < 1024)
    {
        snprintf(buf, buflen, "%" PRIu64 " B", bytes);
    }
    else if (bytes < 1024 * 1024)
    {
        snprintf(buf, buflen, "%.1f KB", (double)bytes / 1024.0);
    }
    else
    {
        snprintf(buf, buflen, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    }
}

static void menu_set_mode(MenuState *state, MenuMode mode, void *telnet_server)
{
    state->mode = mode;
    switch (mode)
    {
    case MENU_NONE:
        vscreen_redraw(&state->screens[*state->activeScreen]);
        break;
    case MENU_F12:
        draw_f12();
        break;
    case MENU_SCREEN_SELECT:
        draw_screen_select(state, telnet_server);
        break;
    case MENU_SCREEN_RELEASE:
        draw_release_prompt(state);
        break;
    case MENU_HDLC_STATUS:
        state->lastRefresh = time(NULL);
        draw_hdlc_status();
        break;
    case MENU_ETH_STATUS:
        state->lastRefresh = time(NULL);
        draw_eth_status();
        break;
    case MENU_ETH_FRAMES:
        state->lastRefresh = time(NULL);
        draw_eth_frames();
        break;
    case MENU_ETH_FRAME_DUMP:
        draw_eth_frame_dump();
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
        draw_pending_list(telnet_server);
#endif
        break;
    case MENU_MESSAGE:
        break;
    default:
        break;
    }
}

static void menu_show_message(MenuState *state, const char *msg, MenuMode return_to)
{
    printf("\n%s\n", msg);
    fflush(stdout);
    state->mode = MENU_MESSAGE;
    state->returnTo = return_to;
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
    if (cpu_speed_initialized)
    {
        double elapsed = (now.tv_sec - cpu_speed_last_time.tv_sec) +
                         (now.tv_nsec - cpu_speed_last_time.tv_nsec) / 1e9;
        uint64_t delta_instr = g_instr_counter - cpu_speed_last_instr;
        if (elapsed > 0.01)
        {
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

    if (throttle_on)
    {
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
    printf("    - CPU models ND-1 to ND-120, MMS1/MMS2, 32/48-bit FPP, 1-16 MB\n");
    printf("    - Disks: SMD, Winchester, SCSI, floppy; TSS CDC disc and drum\n");
    printf("    - Paper tape reader/punch, line printer (text or PDF)\n");
    printf("    - Terminals 5-11, telnet server, national character sets\n");
    printf("    - HDLC networking, up to 4 links\n");
    printf("    - ND-5000 CPU beside the ND-100 for ND-500 programs\n");
    printf("    - INI machine config, DAP debugger, trace and watchpoints\n");
    printf("    - WebAssembly browser build (Glass)\n\n");
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
    for (CharsetVariant v = CHARSET_OFF; v < CHARSET_COUNT; v++)
    {
        printf("  [%d] %s%-10s%s %s\n", (int)v + 1, (v == active) ? "* " : "  ", charset_name(v),
               (v == active) ? " (active)" : "",
               (v == CHARSET_OFF) ? "ASCII passthrough { | } [ \\ ]" : "");
    }

    // Mapping detail for the active variant
    printf("\n  Mappings for %s:\n", charset_name(active));
    int n = charset_mapping_count(active);
    if (n == 0)
    {
        printf("    (none - 7-bit codes shown as literal ASCII)\n");
    }
    else
    {
        printf("    7-bit byte  ASCII  ->  shown as / typed as\n");
        for (int i = 0; i < n; i++)
        {
            uint8_t byte;
            const char *glyph;
            const char *utf8;
            if (charset_mapping_at(active, i, &byte, &glyph, &utf8))
            {
                printf("      %03o (0x%02X)   %-2s        %s\n", byte, byte, glyph, utf8);
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

    printf("  Current OPR = %06o (octal)   0x%04X   %u (dec)\n\n", opr, opr, (unsigned)opr);

    // 16-bit switch display, bit 15 (MSB) down to bit 0 (LSB).
    printf("  bit:");
    for (int b = 15; b >= 0; b--)
    {
        printf(" %2d", b);
    }
    printf("\n  sw :");
    for (int b = 15; b >= 0; b--)
    {
        printf("  %c", (opr & (1u << b)) ? '1' : '0');
    }
    printf("\n\n");

    // Decode against the exact values NORD TSS tests (see the doc + source refs).
    printf("  TSS meaning of the current value:\n");
    if (opr == 0131313)
    {
        printf("    131313 -> COLD START: create the SYSTEM user (SINIT/CRUSE)\n");
    }
    else if (opr == 0111111)
    {
        printf("    111111 -> verbose disc-error diagnostics (XDISK error path)\n");
    }
    else if (opr == 025252)
    {
        printf("    025252 -> (NORD-1 only) panel memory examine/deposit tool\n");
    }
    else if (opr == 0)
    {
        printf("    000000 -> normal run (no cold-start action)\n");
    }
    else
    {
        printf("    (no special cold-start action; on NORD-10 the low 15 bits\n"
               "     select the memory word shown in the LEV4 register display)\n");
    }

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
    printf("  [7] Ethernet Status\n");
    printf("  [A] About\n");
    printf("\nPress 1-7/A to select, ESC to cancel: ");
    fflush(stdout);
}

// Ethernet II status page. Every value comes from devmgr_get_ethernet_status(), a copy of
// the card's own state; the menu runs between machine_run() slices on the emulation thread.
static void draw_eth_status(void)
{
    // Am7990 CSR0 bit names, bit 0 first (same bits as CSR0_FLAGS_* in
    // src/devices/ethernet/eth_lance.h).
    static const char *const csr0_names[16] = {"INIT", "STRT", "STOP", "TDMD", "TXON", "RXON",
                                               "INEA", "INTR", "IDON", "TINT", "RINT", "MERR",
                                               "MISS", "CERR", "BABL", "ERR"};
    EthernetStatus st;
    int n = 0;

    printf("\033[H\033[J");
    printf("=== Ethernet II Status ===\n\n");

    while (devmgr_get_ethernet_status(n, &st))
    {
        char tx[16];
        char rx[16];

        printf("  Card %d  IOX %o-%o  Ident %o  Level %d  DRAM bank %u (ND-100 byte 0x%X)\n",
               st.thumbwheel, (unsigned)st.iox_start, (unsigned)st.iox_end, (unsigned)st.ident,
               st.level, st.memory_bank, (unsigned)st.window_byte_address);
        printf("    ND-100 side : int enable %-3s  INT12 pending %-3s  halt %-3s  reset %-3s\n",
               st.interrupt_enabled ? "on" : "off", st.interrupt_pending ? "yes" : "no",
               st.halt ? "yes" : "no", st.reset ? "yes" : "no");
        printf("                  window reads %" PRIu64 "  writes %" PRIu64 "\n", st.nd_window_reads,
               st.nd_window_writes);
        printf("    68000       : %-7s  PC %06X  SR %04X  (IPL %u)\n",
               st.m68k_halted ? "HALTED" : (!st.m68k_running ? "held" : (st.m68k_stopped ? "STOP" : "running")),
               (unsigned)st.m68k_pc, (unsigned)(st.m68k_sr & 0xFFFFu),
               (unsigned)((st.m68k_sr >> 8u) & 7u));
        printf("    LANCE       : %s  MAC %02x:%02x:%02x:%02x:%02x:%02x  RX queued %d\n",
               st.lance_initialized ? "initialized" : "not initialized", st.mac[0], st.mac[1],
               st.mac[2], st.mac[3], st.mac[4], st.mac[5], st.lance_rx_queued);
        printf("                  CSR0 %04X ", (unsigned)st.lance_csr0);
        for (int b = 15; b >= 0; b--)
        {
            if ((st.lance_csr0 & (1u << (unsigned)b)) != 0u)
            {
                printf(" %s", csr0_names[b]);
            }
        }
        printf("\n");
        format_bytes((uint64_t)st.tx_bytes, tx, sizeof tx);
        format_bytes((uint64_t)st.rx_bytes, rx, sizeof rx);
        printf("    Frames      : TX %" PRId64 " (%s)  RX %" PRId64 " (%s)\n", st.tx_packets, tx,
               st.rx_packets, rx);
        printf("                  padded to 60 %" PRId64 "  own echoes dropped %" PRId64
               "  checksums repaired %" PRId64 "\n",
               st.runt_frames_padded, st.own_echoes_dropped, st.checksums_repaired);
        if (st.net_attached)
        {
            printf("    Network     : %s  %s\n", st.net_description, st.net_active ? "ACTIVE" : "not active");
            printf("                  sent %" PRIu64 "  received %" PRIu64 "  ring full drops %" PRIu64
                   "  send failures %" PRIu64 "  rx errors %" PRIu64 "  links up %" PRIu64 "\n",
                   st.net_frames_sent, st.net_frames_received, st.net_frames_dropped_ring,
                   st.net_send_failures, st.net_receive_errors, st.net_links_up);
        }
        else
        {
            printf("    Network     : none attached\n");
        }
        printf("\n");
        n++;
    }
    if (n == 0)
    {
        printf("  No Ethernet II card configured. Use --eth0=<spec> or [controller.eth.0].\n\n");
    }
    printf("Refreshes every second. P: packet view%s   ESC: back", (n > 1) ? " (then card number)" : "");
    fflush(stdout);
}

// ---- Ethernet packet view --------------------------------------------------------------

static int s_eth_card;           // card whose frames are shown
static bool s_eth_paused;        // list frozen; frames are still recorded by the card
static int s_eth_selected;       // index into s_eth_frames while paused
static int s_eth_count;          // frames in s_eth_frames
static bool s_eth_choose_card;   // waiting for a card digit after P
static EthernetFrame s_eth_frames[ETHERNET_FRAME_LOG_SIZE];

// Terminal size; 80x24 when it cannot be read.
static void eth_term_size(int *rows, int *cols)
{
    *rows = 24;
    *cols = 80;
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    {
        struct winsize ws;
        if ((ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) && (ws.ws_row > 0) && (ws.ws_col > 0))
        {
            *rows = ws.ws_row;
            *cols = ws.ws_col;
        }
    }
#endif
}

// One list line for frame f, cut to cols characters.
static void eth_frame_line(const EthernetFrame *f, const uint8_t *own_mac, bool selected, int cols)
{
    char src[32];
    char dst[32];
    char what[160];
    char line[512];
    char clock[16];
    time_t secs = (time_t)(f->time_ms / 1000);
    struct tm tmv;

#if defined(_WIN32)
    localtime_s(&tmv, &secs);
#else
    localtime_r(&secs, &tmv);
#endif
    (void)strftime(clock, sizeof clock, "%H:%M:%S", &tmv);
    if (f->captured >= 14)
    {
        eth_decode_mac_name(f->data + 6, own_mac, src, sizeof src);
        eth_decode_mac_name(f->data, own_mac, dst, sizeof dst);
    }
    else
    {
        (void)snprintf(src, sizeof src, "?");
        (void)snprintf(dst, sizeof dst, "?");
    }
    eth_decode_summary(f->data, f->captured, what, sizeof what);
    (void)snprintf(line, sizeof line, "%c%5" PRIu64 " %s.%03d %s %4d  %s > %s  %s", selected ? '>' : ' ',
                   f->seq, clock, (int)(f->time_ms % 1000), f->is_tx ? "TX" : "RX", f->length, src,
                   dst, what);
    if ((cols > 1) && ((int)strlen(line) > cols - 1))
    {
        line[cols - 1] = '\0';
    }
    printf("%s\n", line);
}

static void draw_eth_frames(void)
{
    EthernetStatus st;
    int rows;
    int cols;
    int list_rows;
    int first;

    eth_term_size(&rows, &cols);
    printf("\033[H\033[J");
    if (!devmgr_get_ethernet_status(s_eth_card, &st))
    {
        printf("=== Ethernet II packets ===\n\n  Card %d not configured.\n\nESC: back", s_eth_card);
        fflush(stdout);
        return;
    }
    if (!s_eth_paused)
    {
        s_eth_count = devmgr_get_ethernet_frames(s_eth_card, s_eth_frames, ETHERNET_FRAME_LOG_SIZE);
    }
    printf("=== Ethernet II card %d packets  (%s)  TX %" PRId64 "  RX %" PRId64 "  %s ===\n",
           st.thumbwheel, st.net_attached ? st.net_description : "no network", st.tx_packets,
           st.rx_packets, s_eth_paused ? "PAUSED" : "live");
    printf("    #  time         dir  len  src > dst  what\n");

    list_rows = rows - 4; // title, column line, blank, key line
    if (list_rows < 1)
    {
        list_rows = 1;
    }
    // Live: newest at the bottom. Paused: keep the selected frame in view.
    first = s_eth_count - list_rows;
    if (s_eth_paused && (s_eth_selected < first))
    {
        first = s_eth_selected;
    }
    if (first < 0)
    {
        first = 0;
    }
    if (s_eth_count == 0)
    {
        printf("  (no frames yet)\n");
    }
    for (int i = first; (i < s_eth_count) && (i < first + list_rows); i++)
    {
        eth_frame_line(&s_eth_frames[i], st.mac, s_eth_paused && (i == s_eth_selected), cols);
    }
    printf("\n%s", s_eth_paused ? "SPACE resume  Up/Down or k/j select  ENTER hex dump  C clear  ESC back"
                                 : "SPACE pause  C clear  ESC back");
    fflush(stdout);
}

static void draw_eth_frame_dump(void)
{
    const EthernetFrame *f;
    char what[160];

    printf("\033[H\033[J");
    if ((s_eth_selected < 0) || (s_eth_selected >= s_eth_count))
    {
        printf("No frame selected.\n\nESC: back");
        fflush(stdout);
        return;
    }
    f = &s_eth_frames[s_eth_selected];
    eth_decode_summary(f->data, f->captured, what, sizeof what);
    printf("=== Frame #%" PRIu64 "  %s  %d bytes%s ===\n%s\n\n", f->seq, f->is_tx ? "TX (card -> network)" : "RX (network -> card)",
           f->length, (f->captured < f->length) ? " (cut)" : "", what);
    for (int off = 0; off < f->captured; off += 16)
    {
        printf("  %04X  ", off);
        for (int i = 0; i < 16; i++)
        {
            if (off + i < f->captured)
            {
                printf("%02x ", f->data[off + i]);
            }
            else
            {
                printf("   ");
            }
        }
        printf(" ");
        for (int i = 0; (i < 16) && (off + i < f->captured); i++)
        {
            const uint8_t b = f->data[off + i];
            putchar(((b >= 0x20u) && (b < 0x7Fu)) ? (int)b : '.');
        }
        printf("\n");
    }
    printf("\nESC: back to the list");
    fflush(stdout);
}

static const char *tx_sender_state_name(int state)
{
    switch (state)
    {
    case 0:
        return "Stopped";
    case 1:
        return "Ready";
    case 2:
        return "Sending";
    case 3:
        return "Sent";
    default:
        return "?";
    }
}

static void draw_hdlc_status(void)
{
    // Home cursor and clear entire screen (repaint in place)
    printf("\033[H\033[J");
    printf("=== HDLC Device Status ===\n\n");

    int count = devmgr_get_device_count();
    int found = 0;

    for (int i = 0; i < count; i++)
    {
        Device *dev = devmgr_get_device_by_index(i);
        if (!dev || dev->type != DEVICE_TYPE_HDLC)
        {
            continue;
        }

        HDLCData *data = (HDLCData *)dev->deviceData;
        if (!data)
        {
            continue;
        }

        found++;

        char addr_str[16];
        snprintf(addr_str, sizeof(addr_str), "%04o-%04o", dev->startAddress, dev->endAddress);

        bool connected = data->modem ? atomic_load(&data->modem->connected) : false;

        char tx_bytes_str[16];
        char rx_bytes_str[16];
        format_bytes(data->modem ? data->modem->bytesTx : 0, tx_bytes_str, sizeof(tx_bytes_str));
        format_bytes(data->modem ? data->modem->bytesRx : 0, rx_bytes_str, sizeof(rx_bytes_str));

        HDLCRxFrameStatus st;
        bool has_status = hdlc_get_rx_frame_status(data, &st);

        // Device header
        printf("  HDLC #%d  [%s]  Connected: %s\n", data->thumbwheel, addr_str,
               connected ? "Yes" : "No");

        // Fixed-width snprintf buffers so columns stay aligned regardless of value length
        {
            char c_dma[8];
            char c_bytes[16];
            char c_frames[12];
            char c_ena[16];
            char c_errs[18];
            char c_state[20];
            char c_queue[12];
            char tmp_buf[24];

            // --- RX line ---
            snprintf(c_dma, sizeof(c_dma), "%-3s", (has_status && st.rxDmaEnabled) ? "On" : "Off");
            snprintf(c_bytes, sizeof(c_bytes), "%-8s", rx_bytes_str);
            snprintf(c_frames, sizeof(c_frames), "%-8" PRIu64, data->framesRx);
            snprintf(c_ena, sizeof(c_ena), "RXE=%-6s", (has_status && st.rxEnabled) ? "On" : "Off");
            snprintf(c_errs, sizeof(c_errs), "Errs=%-7" PRIu64, data->framesRxErrors);

            // RX frame assembly state
            const char *rx_state = "Idle";
            if (has_status)
            {
                if (!st.rxDmaEnabled)
                {
                    rx_state = "DMA off";
                }
                else if (!st.rxDcbReady)
                {
                    rx_state = "No DCB";
                }
                else
                {
                    switch (st.state)
                    {
                    case 1:
                    case 2:
                        snprintf(tmp_buf, sizeof(tmp_buf), "Rx %d B", st.frameLength);
                        rx_state = tmp_buf;
                        break;
                    case 3:
                        rx_state = "Error";
                        break;
                    default:
                        break;
                    }
                }
            }
            snprintf(c_state, sizeof(c_state), "State=%-10.13s", rx_state);

            if (has_status)
            {
                format_bytes(st.tcpQueueUsed, c_queue, sizeof(c_queue));
            }
            else
            {
                snprintf(c_queue, sizeof(c_queue), "-");
            }

            printf("    RX: DMA=%s  Bytes=%s  Frames=%s  %s  %s  %s  Queue=%s\n", c_dma, c_bytes,
                   c_frames, c_ena, c_errs, c_state, c_queue);

            // --- TX line ---
            snprintf(c_dma, sizeof(c_dma), "%-3s", (has_status && st.txDmaEnabled) ? "On" : "Off");
            snprintf(c_bytes, sizeof(c_bytes), "%-8s", tx_bytes_str);
            snprintf(c_frames, sizeof(c_frames), "%-8" PRIu64, data->framesTx);
            snprintf(c_ena, sizeof(c_ena), "TXE=%-6s", (has_status && st.txEnabled) ? "On" : "Off");
            snprintf(c_errs, sizeof(c_errs), "%-12s", ""); // blank to align with RX Errs column

            const char *tx_state = has_status ? tx_sender_state_name(st.txSenderState) : "Stopped";
            snprintf(c_state, sizeof(c_state), "State=%-10s", tx_state);

            if (has_status)
            {
                format_bytes(st.txQueueUsed, c_queue, sizeof(c_queue));
            }
            else
            {
                snprintf(c_queue, sizeof(c_queue), "-");
            }

            printf("    TX: DMA=%s  Bytes=%s  Frames=%s  %s  %s  %s  Queue=%s\n", c_dma, c_bytes,
                   c_frames, c_ena, c_errs, c_state, c_queue);

            // DCB and TX diagnostics
            if (has_status && st.txStarts > 0)
            {
                printf("    DCB: TX=%-8" PRIu64 " RX=%-8" PRIu64 "  |  Starts=%-6" PRIu64
                       " Sent=%-6" PRIu64 " Skip=%" PRIu64 "\n",
                       data->dcbTxMarked, data->dcbRxMarked, st.txStarts, data->framesTx,
                       st.txAlreadySent);
                printf("    IRQ: 12=%-6" PRIu64 " 13=%-6" PRIu64 " IDENT13=%-6" PRIu64 " TBMT=%d\n",
                       data->irq12Count, data->irq13Count, data->identCount13,
                       data->txTransferStatus.bits.transmitBufferEmpty);
                printf("    I13: DMA=%-6" PRIu64 " DataAv=%-6" PRIu64 " StatAv=%-6" PRIu64
                       " Modem=%" PRIu64 "\n",
                       data->irq13_dma, data->irq13_dataAvail, data->irq13_statusAvail,
                       data->irq13_modem);
            }

            // Last TX frames history
            if (data->txHistoryIdx > 0)
            {
                int total = data->txHistoryIdx < HDLC_TX_HISTORY_SIZE ? data->txHistoryIdx
                                                                      : HDLC_TX_HISTORY_SIZE;
                int start = data->txHistoryIdx >= HDLC_TX_HISTORY_SIZE
                                ? data->txHistoryIdx % HDLC_TX_HISTORY_SIZE
                                : 0;
                printf("    Last %d TX frames:\n", total);
                for (int t = 0; t < total; t++)
                {
                    int idx = (start + t) % HDLC_TX_HISTORY_SIZE;
                    printf("      #%-3d LP=%06X DA=%06X K=%04X BC=%-3d W=%-3d ",
                           data->txHistoryIdx - total + t + 1, data->txHistory[idx].listPtr,
                           data->txHistory[idx].dataAddr, data->txHistory[idx].keyBefore,
                           data->txHistory[idx].byteCount, data->txHistory[idx].frameSize);
                    for (int b = 0; b < data->txHistory[idx].dataLen; b++)
                    {
                        printf("%02x ", data->txHistory[idx].data[b]);
                    }
                    printf("\n");
                }
            }

            // Dropped bytes line (only shown if any drops occurred)
            uint64_t rx_drop = data->modem ? data->modem->rxDropped : 0;
            uint64_t tx_drop = data->modem ? data->modem->txDropped : 0;
            if (rx_drop > 0 || tx_drop > 0)
            {
                char rx_drop_str[16];
                char tx_drop_str[16];
                format_bytes(rx_drop, rx_drop_str, sizeof(rx_drop_str));
                format_bytes(tx_drop, tx_drop_str, sizeof(tx_drop_str));
                printf("    ** DROPPED: RX=%s  TX=%s\n", rx_drop_str, tx_drop_str);
            }
        }

        printf("\n");
    }

    if (found == 0)
    {
        printf("  No HDLC devices configured.\n");
        printf("  Use --hdlc=N:PORT (server) or --hdlc=N:HOST:PORT (client)\n");
    }

    printf("  Refreshes every second. Press ESC to return.\n");
    fflush(stdout);
}

#if !defined(__EMSCRIPTEN__)
// Byte counters of the connected telnet terminal named name; *rx and *tx
// are left unchanged if no connected terminal has that name.
static void telnet_screen_stats(TelnetServer *ts, const char *name, uint64_t *rx, uint64_t *tx)
{
    int tcount = telnet_get_terminal_count(ts);
    for (int t = 0; t < tcount; t++)
    {
        const char *tname = NULL;
        bool conn = false;
        TelnetServer_GetTerminalStatus(ts, t, &tname, NULL, &conn, NULL, NULL, 0);
        if (conn && tname && strcmp(tname, name) == 0)
        {
            telnet_get_terminal_stats(ts, t, rx, tx);
            break;
        }
    }
}
#endif

// The status suffix shown after screen i's name: active marker, output
// only, telnet client with byte counters, inactive or virtual. buf holds
// the text when it has to be formatted.
static const char *screen_status_text(MenuState *state, int i, void *telnet_server, char *buf,
                                      size_t buf_size)
{
    bool has_telnet = (telnet_server != NULL);
    const char *status = "";

    if (i == *state->activeScreen)
    {
        status = " *";
    }
    else if (!state->screens[i].isInputCapable)
    {
        status = " (output only)";
    }
#if !defined(__EMSCRIPTEN__)
    else if (has_telnet)
    {
        TelnetServer *ts = (TelnetServer *)telnet_server;
        if (telnet_is_device_connected(ts, state->screens[i].device))
        {
            const char *addr = telnet_get_device_client_addr(ts, state->screens[i].device);

            // Find terminal index in server for byte stats
            uint64_t rx = 0;
            uint64_t tx = 0;
            telnet_screen_stats(ts, state->screens[i].name, &rx, &tx);

            char rx_str[16];
            char tx_str[16];
            format_bytes(rx, rx_str, sizeof(rx_str));
            format_bytes(tx, tx_str, sizeof(tx_str));

            if (addr && addr[0])
            {
                snprintf(buf, buf_size, " [Telnet %s] rx:%s tx:%s", addr, rx_str, tx_str);
            }
            else
            {
                snprintf(buf, buf_size, " [Telnet] rx:%s tx:%s", rx_str, tx_str);
            }
            status = buf;
        }
        else if (!state->screens[i].localActive)
        {
            status = " [Inactive]";
        }
        else if (state->screens[i].isInputCapable && i > 0)
        {
            status = " [Virtual]";
        }
    }
#else
    else if (!state->screens[i].localActive)
    {
        status = " [Inactive]";
    }
    (void)telnet_server;
#endif
#if defined(__EMSCRIPTEN__)
    (void)has_telnet;
    (void)buf;
    (void)buf_size;
#endif
    return status;
}

static void draw_screen_select(MenuState *state, void *telnet_server)
{
    bool has_telnet = (telnet_server != NULL);

    printf("\033[2J\033[H");

#if !defined(__EMSCRIPTEN__)
    if (has_telnet)
    {
        TelnetServer *ts = (TelnetServer *)telnet_server;
        int pending = telnet_get_pending_count(ts);
        if (pending > 0)
        {
            printf("=== Virtual Screens (telnet port %d, %d pending) ===\n\n", telnet_get_port(ts),
                   pending);
        }
        else
        {
            printf("=== Virtual Screens (telnet port %d) ===\n\n", telnet_get_port(ts));
        }
    }
    else
#endif
    {
        printf("=== Virtual Screen Selector ===\n\n");
    }

    for (int i = 0; i < state->screenCount; i++)
    {
        char status_buf[128];
        const char *status =
            screen_status_text(state, i, telnet_server, status_buf, sizeof(status_buf));

        if (i < 9)
        {
            printf("  [%d] %-24s%s\n", i + 1, state->screens[i].name, status);
        }
        else
        {
            printf("  [%c] %-24s%s\n", 'a' + (i - 9), state->screens[i].name, status);
        }
    }

#if !defined(__EMSCRIPTEN__)
    if (has_telnet)
    {
        printf("\n  [R] Release terminal (virtual->inactive, or disconnect telnet)");
        printf("\n  [P] Pending connections (live view)");
        printf("\n\nPress 1-%d/a to switch, R/P for options, ESC to cancel: ",
               state->screenCount > 9 ? 9 : state->screenCount);
    }
    else
#endif
    {
        printf("\nPress 1-%d/a to switch, ESC to cancel: ",
               state->screenCount > 9 ? 9 : state->screenCount);
    }
    fflush(stdout);
}

static void draw_release_prompt(MenuState *state)
{
    printf("\nEnter terminal to release (2-%d): ", state->screenCount > 9 ? 9 : state->screenCount);
    fflush(stdout);
}

#if !defined(__EMSCRIPTEN__)
static void draw_pending_list(void *telnet_server)
{
    TelnetServer *ts = (TelnetServer *)telnet_server;
    int count = telnet_get_pending_count(ts);

    printf("\033[2J\033[H");
    printf("=== Pending Telnet Connections (live, port %d) ===\n\n", telnet_get_port(ts));

    if (count == 0)
    {
        printf("  No pending connections.\n");
    }
    else
    {
        printf("  #  %-24s  %-10s  %-10s  %s\n", "Address", "RX", "TX", "Age");
        printf("  -  %-24s  %-10s  %-10s  %s\n", "-------", "--", "--", "---");
        for (int i = 0; i < count; i++)
        {
            char addr[48];
            int age = 0;
            uint64_t rx = 0;
            uint64_t tx = 0;
            if (telnet_get_pending_info(ts, i, addr, sizeof(addr), &age, &rx, &tx))
            {
                char rx_str[16];
                char tx_str[16];
                format_bytes(rx, rx_str, sizeof(rx_str));
                format_bytes(tx, tx_str, sizeof(tx_str));
                printf("  %d) %-24s  %-10s  %-10s  %ds / 60s\n", i + 1, addr, rx_str, tx_str, age);
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

void menu_init(MenuState *state, VScreen *screens, int screen_count, int *active_screen)
{
    memset(state, 0, sizeof(MenuState));
    state->screens = screens;
    state->screenCount = screen_count;
    state->activeScreen = active_screen;
}

#if !defined(__EMSCRIPTEN__)
void menu_enter(MenuState *state, TelnetServer *telnet_server)
#else
void menu_enter(MenuState *state, void *telnetServer)
#endif
{
    menu_set_mode(state, MENU_F12, telnet_server);
}

#if !defined(__EMSCRIPTEN__)
void menu_tick(MenuState *state, TelnetServer *telnet_server)
#else
void menu_tick(MenuState *state, void *telnetServer)
#endif
{
    if (state->mode == MENU_MESSAGE && time(NULL) >= state->messageExpiry)
    {
        menu_set_mode(state, state->returnTo, telnet_server);
    }
#if !defined(__EMSCRIPTEN__)
    // Live refresh for pending list view (every 2 seconds)
    if (state->mode == MENU_PENDING_LIST && telnet_server)
    {
        time_t now = time(NULL);
        if (now - state->lastRefresh >= 2)
        {
            state->lastRefresh = now;
            draw_pending_list(telnet_server);
        }
    }
#endif
    // Live refresh for HDLC status view (every 1 second)
    if (state->mode == MENU_HDLC_STATUS)
    {
        time_t now = time(NULL);
        if (now - state->lastRefresh >= 1)
        {
            state->lastRefresh = now;
            draw_hdlc_status();
        }
    }
    // Live refresh for Ethernet status view and packet list (every 1 second)
    if ((state->mode == MENU_ETH_STATUS) || (state->mode == MENU_ETH_FRAMES))
    {
        time_t now = time(NULL);
        if (now - state->lastRefresh >= 1)
        {
            state->lastRefresh = now;
            if (state->mode == MENU_ETH_STATUS)
            {
                draw_eth_status();
            }
            else
            {
                draw_eth_frames();
            }
        }
    }
    // Live refresh for CPU speed view (every 1 second)
    if (state->mode == MENU_CPU_SPEED)
    {
        time_t now = time(NULL);
        if (now - state->lastRefresh >= 1)
        {
            state->lastRefresh = now;
            draw_cpu_speed();
        }
    }
}

// =========================================================
// Key handler - processes one keypress per call
// =========================================================

#if !defined(__EMSCRIPTEN__)
void menu_process_key(MenuState *state, const KeyEvent *key, TelnetServer *telnet_server)
#else
void menu_process_key(MenuState *state, const KeyEvent *key, void *telnetServer)
#endif
{
    if (!key || key->type == KEY_NONE)
    {
        return;
    }

    // The menu only cares about ESC or typed characters. Function keys and
    // multi-byte unknown sequences are ignored.
    bool is_esc = (key->type == KEY_ESCAPE);
    char ch = (key->type == KEY_CHAR) ? key->ch : '\0';

    switch (state->mode)
    {

    // ----- F12 top-level menu -----
    case MENU_F12:
        if (is_esc)
        {
            menu_set_mode(state, MENU_NONE, telnet_server);
        }
        else if (ch == '1')
        {
#if defined(__riscv) || defined(_WIN32)
            printf("\nFloppy menu not available on this build\n");
            fflush(stdout);
#else
            int ret = show_floppy_menu();
            if (ret == -1)
            {
                printf("Failed to show floppy menu\n");
            }
#endif
            menu_set_mode(state, MENU_NONE, telnet_server);
        }
        else if (ch == '2')
        {
            menu_set_mode(state, MENU_SCREEN_SELECT, telnet_server);
        }
        else if (ch == '3')
        {
            menu_set_mode(state, MENU_HDLC_STATUS, telnet_server);
        }
        else if (ch == '4')
        {
            cpu_speed_initialized = false;
            menu_set_mode(state, MENU_CPU_SPEED, telnet_server);
        }
        else if (ch == '5')
        {
            menu_set_mode(state, MENU_CHARSET, telnet_server);
        }
        else if (ch == '6')
        {
            menu_set_mode(state, MENU_PANEL_SWITCHES, telnet_server);
        }
        else if (ch == '7')
        {
            menu_set_mode(state, MENU_ETH_STATUS, telnet_server);
        }
        else if (ch == 'a' || ch == 'A')
        {
            menu_set_mode(state, MENU_ABOUT, telnet_server);
        }
        break;

    // ----- Screen selector -----
    case MENU_SCREEN_SELECT:
        if (is_esc)
        {
            menu_set_mode(state, MENU_NONE, telnet_server);
            return;
        }
#if !defined(__EMSCRIPTEN__)
        if ((ch == 'r' || ch == 'R') && telnet_server)
        {
            menu_set_mode(state, MENU_SCREEN_RELEASE, telnet_server);
            return;
        }
        if ((ch == 'p' || ch == 'P') && telnet_server)
        {
            menu_set_mode(state, MENU_PENDING_LIST, telnet_server);
            return;
        }
#endif
        {
            int choice = -1;
            if (ch >= '1' && ch <= '9')
            {
                choice = ch - '1';
            }
            else if (ch >= 'a' && ch <= 'z')
            {
                choice = 9 + (ch - 'a');
            }

            if (choice >= 0 && choice < state->screenCount)
            {
#if !defined(__EMSCRIPTEN__)
                if (telnet_server &&
                    telnet_is_device_connected(telnet_server, state->screens[choice].device))
                {
                    menu_show_message(state, "Terminal is in use by telnet client.",
                                      MENU_SCREEN_SELECT);
                    return;
                }

                // If not locally active, re-activate it and clear carrier
                if (telnet_server && state->screens[choice].isInputCapable &&
                    !state->screens[choice].localActive)
                {
                    state->screens[choice].localActive = true;
                    telnet_set_device_locally_active(telnet_server, state->screens[choice].device,
                                                     true);
                    telnet_clear_device_carrier(telnet_server, state->screens[choice].device);
                }
#endif
                *state->activeScreen = choice;
            }
            menu_set_mode(state, MENU_NONE, telnet_server);
        }
        break;

    // ----- Release prompt (unified close/disconnect) -----
    case MENU_SCREEN_RELEASE:
        if (is_esc)
        {
            menu_set_mode(state, MENU_SCREEN_SELECT, telnet_server);
            return;
        }
        {
            int choice = -1;
            if (ch >= '1' && ch <= '9')
            {
                choice = ch - '1';
            }
            else if (ch >= 'a' && ch <= 'z')
            {
                choice = 9 + (ch - 'a');
            }

            if (choice < 0 || choice >= state->screenCount)
            {
                menu_show_message(state, "Invalid selection.", MENU_SCREEN_SELECT);
                return;
            }
            if (choice == 0)
            {
                menu_show_message(state, "Cannot release the Console.", MENU_SCREEN_SELECT);
                return;
            }
            if (!state->screens[choice].isInputCapable)
            {
                menu_show_message(state, "Cannot release output-only screens.", MENU_SCREEN_SELECT);
                return;
            }

#if !defined(__EMSCRIPTEN__)
            if (telnet_server)
            {
                // If telnet-connected: disconnect the client
                if (telnet_is_device_connected(telnet_server, state->screens[choice].device))
                {
                    telnet_disconnect_device(telnet_server, state->screens[choice].device);
                    char msg[64];
                    snprintf(msg, sizeof(msg), "%s telnet client disconnected.",
                             state->screens[choice].name);
                    menu_show_message(state, msg, MENU_SCREEN_SELECT);
                    return;
                }

                // If locally active: release for telnet
                if (state->screens[choice].localActive)
                {
                    if (choice == *state->activeScreen)
                    {
                        menu_show_message(state, "Cannot release the active screen. Switch first.",
                                          MENU_SCREEN_SELECT);
                        return;
                    }
                    state->screens[choice].localActive = false;
                    telnet_set_device_locally_active(telnet_server, state->screens[choice].device,
                                                     false);
                    char msg[64];
                    snprintf(msg, sizeof(msg), "%s released for telnet.",
                             state->screens[choice].name);
                    menu_show_message(state, msg, MENU_SCREEN_SELECT);
                    return;
                }

                // Already inactive
                menu_show_message(state, "Terminal is already inactive.", MENU_SCREEN_SELECT);
            }
#endif
        }
        break;

#if !defined(__EMSCRIPTEN__)
    // ----- Pending connections (live view) -----
    case MENU_PENDING_LIST:
        if (is_esc)
        {
            menu_set_mode(state, MENU_SCREEN_SELECT, telnet_server);
            return;
        }
        if (telnet_server)
        {
            if (ch == 'd' || ch == 'D')
            {
                int count = telnet_get_pending_count(telnet_server);
                if (count == 0)
                {
                    menu_show_message(state, "No pending connections to drop.", MENU_PENDING_LIST);
                }
                else if (count == 1)
                {
                    telnet_drop_pending(telnet_server, 0);
                    menu_show_message(state, "Dropped pending connection.", MENU_PENDING_LIST);
                }
                else
                {
                    printf("\nDrop which connection (1-%d)? ", count);
                    fflush(stdout);
                    // We'll handle the digit on the next keypress via a simple approach:
                    // For now, just prompt. The next digit key will be caught here.
                }
            }
            else if (ch >= '1' && ch <= '9')
            {
                int idx = ch - '1';
                if (telnet_drop_pending(telnet_server, idx))
                {
                    draw_pending_list(telnet_server);
                    state->lastRefresh = time(NULL);
                }
            }
            else if (ch == 'a' || ch == 'A')
            {
                telnet_drop_all_pending(telnet_server);
                menu_show_message(state, "All pending connections dropped.", MENU_PENDING_LIST);
            }
        }
        break;
#endif

    // ----- HDLC status (live view) -----
    case MENU_HDLC_STATUS:
        if (is_esc)
        {
            menu_set_mode(state, MENU_F12, telnet_server);
        }
        break;

    // ----- Ethernet status (live view) -----
    case MENU_ETH_STATUS:
        if (is_esc)
        {
            s_eth_choose_card = false;
            menu_set_mode(state, MENU_F12, telnet_server);
        }
        else if (s_eth_choose_card && (ch >= '0') && (ch <= '9'))
        {
            EthernetStatus st;
            s_eth_choose_card = false;
            if (devmgr_get_ethernet_status(ch - '0', &st))
            {
                s_eth_card = ch - '0';
                s_eth_paused = false;
                menu_set_mode(state, MENU_ETH_FRAMES, telnet_server);
            }
        }
        else if ((ch == 'p') || (ch == 'P'))
        {
            EthernetStatus st;
            if (devmgr_get_ethernet_status(1, &st))
            {
                s_eth_choose_card = true; // more than one card: the next digit picks it
            }
            else if (devmgr_get_ethernet_status(0, &st))
            {
                s_eth_card = 0;
                s_eth_paused = false;
                menu_set_mode(state, MENU_ETH_FRAMES, telnet_server);
            }
        }
        break;

    // ----- Ethernet packet list (live view) -----
    case MENU_ETH_FRAMES:
    {
        const bool up = ((key->type == KEY_UNKNOWN) && (key->seqLen == 3) &&
                         ((memcmp(key->seq, "\x1B[A", 3) == 0) || (memcmp(key->seq, "\x1BOA", 3) == 0))) ||
                        (ch == 'k');
        const bool down = ((key->type == KEY_UNKNOWN) && (key->seqLen == 3) &&
                           ((memcmp(key->seq, "\x1B[B", 3) == 0) || (memcmp(key->seq, "\x1BOB", 3) == 0))) ||
                          (ch == 'j');
        if (is_esc)
        {
            s_eth_paused = false;
            menu_set_mode(state, MENU_ETH_STATUS, telnet_server);
        }
        else if (ch == ' ')
        {
            s_eth_paused = !s_eth_paused;
            if (s_eth_paused)
            {
                s_eth_count = devmgr_get_ethernet_frames(s_eth_card, s_eth_frames, ETHERNET_FRAME_LOG_SIZE);
                s_eth_selected = s_eth_count - 1;
            }
            draw_eth_frames();
        }
        else if ((ch == 'c') || (ch == 'C'))
        {
            devmgr_clear_ethernet_frames(s_eth_card);
            s_eth_count = 0;
            s_eth_selected = -1;
            draw_eth_frames();
        }
        else if (s_eth_paused && up && (s_eth_selected > 0))
        {
            s_eth_selected--;
            draw_eth_frames();
        }
        else if (s_eth_paused && down && (s_eth_selected < s_eth_count - 1))
        {
            s_eth_selected++;
            draw_eth_frames();
        }
        else if (s_eth_paused && ((ch == '\r') || (ch == '\n')) && (s_eth_count > 0))
        {
            menu_set_mode(state, MENU_ETH_FRAME_DUMP, telnet_server);
        }
        break;
    }

    // ----- Ethernet frame hex dump -----
    case MENU_ETH_FRAME_DUMP:
        if (is_esc)
        {
            menu_set_mode(state, MENU_ETH_FRAMES, telnet_server);
        }
        break;

    case MENU_CPU_SPEED:
        if (is_esc)
        {
            menu_set_mode(state, MENU_F12, telnet_server);
        }
        else if (ch == 't' || ch == 'T')
        {
            cpu_throttle_set_enabled(!cpu_throttle_get_enabled());
        }
        else if (ch == '+' || ch == '=')
        {
            cpu_throttle_set_mhz(cpu_throttle_get_mhz() * 1.1);
        }
        else if (ch == '-')
        {
            cpu_throttle_set_mhz(cpu_throttle_get_mhz() * 0.9);
        }
        else if (ch == 'a' || ch == 'A')
        {
            // Auto-calibrate: measure current actual speed and set as target
            // This only makes sense when throttle is OFF (measuring unthrottled speed)
            // The user should then enable throttle to lock to this speed
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (cpu_speed_initialized)
            {
                double elapsed = (now.tv_sec - cpu_speed_last_time.tv_sec) +
                                 (now.tv_nsec - cpu_speed_last_time.tv_nsec) / 1e9;
                uint64_t delta = g_instr_counter - cpu_speed_last_instr;
                if (elapsed > 0.1)
                {
                    double actual = (delta / elapsed) / 1e6;
                    cpu_throttle_set_mhz(actual);
                    cpu_throttle_set_enabled(true);
                }
            }
        }
        break;

    case MENU_CHARSET:
        if (is_esc)
        {
            menu_set_mode(state, MENU_F12, telnet_server);
        }
        else if (ch >= '1' && ch <= ('0' + CHARSET_COUNT))
        {
            charset_set((CharsetVariant)(ch - '1'));
            // Repaint in place so the new selection + mappings show immediately
            draw_charset();
        }
        break;

    // ----- Operator's-panel switch register (OPR) editor -----
    // Edits gReg->reg_OPR live; TSS sees it on its next "TRA OPR". Digits shift in
    // from the right just like keying the physical ND panel data switches.
    case MENU_PANEL_SWITCHES:
        if (is_esc)
        {
            menu_set_mode(state, MENU_F12, telnet_server);
        }
        else if (g_reg != NULL)
        {
            if (ch >= '0' && ch <= '7')
            {
                gOPR = (uint16_t)(((gOPR << 3) | (uint16_t)(ch - '0')) & 0xFFFFu);
                draw_panel_switches();
            }
            else if (ch == 'c' || ch == 'C')
            {
                gOPR = 0; // clear all switches
                draw_panel_switches();
            }
            else if (ch == 's' || ch == 'S')
            {
                gOPR = 0131313; // cold start: create the SYSTEM user (SINIT)
                draw_panel_switches();
            }
            else if (ch == 'd' || ch == 'D')
            {
                gOPR = 0111111; // verbose disc-error diagnostics (XDISK)
                draw_panel_switches();
            }
        }
        break;

    case MENU_ABOUT:
        if (is_esc)
        {
            menu_set_mode(state, MENU_F12, telnet_server);
        }
        break;

    case MENU_MESSAGE:
        break;

    default:
        break;
    }
}
