/*
 * screenmenu.h - F12 menu state machine: modes and API.
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

#ifndef SCREENMENU_H
#define SCREENMENU_H

#include <stdbool.h>
#include <time.h>
#include "vscreen.h"
#include "../../ndlib/ndlib_types.h" // KeyEvent

#if !defined(__EMSCRIPTEN__)
#include "../../ndlib/telnetserver.h"
#endif

// Menu state machine modes
typedef enum
{
    MENU_NONE = 0,
    MENU_F12,
    MENU_SCREEN_SELECT,
    MENU_SCREEN_RELEASE,
    MENU_PENDING_LIST,
    MENU_HDLC_STATUS,
    MENU_CPU_SPEED,
    MENU_CHARSET,
    MENU_PANEL_SWITCHES, // Operator's-panel switch register (OPR / TRA OPR) editor
    MENU_ABOUT,
    MENU_MESSAGE,
} MenuMode;

// Non-blocking menu state machine
// clang-format off
typedef struct {
    MenuMode mode;
    MenuMode returnTo;          // After message timeout, transition here
    time_t messageExpiry;       // When MENU_MESSAGE auto-dismisses
    time_t lastRefresh;         // For live-refresh views (pending list)
    VScreen *screens;
    int screenCount;
    int *activeScreen;
} MenuState;
// clang-format on

// Initialize menu state (call once at startup)

/**
 * @brief Zero the menu state and record the virtual screen array it operates on.
 *        Leaves the menu in MENU_NONE. Call once at startup.
 * @param state Menu state to initialize.
 * @param screens Array of virtual screens the menu can select between.
 * @param screenCount Number of entries in screens.
 * @param activeScreen Pointer to the caller's index of the currently shown screen.
 */
void menu_init(MenuState *state, VScreen *screens, int screen_count, int *active_screen);

// Check if menu is currently active (suppresses VScreen stdout output)
static inline bool menu_is_active(const MenuState *state)
{
    return state->mode != MENU_NONE;
}

// Enter the F12 menu system
#if !defined(__EMSCRIPTEN__)
/**
 * @brief Switch the menu into MENU_F12 and draw the top-level F12 menu.
 * @param state Menu state to change.
 * @param telnetServer Telnet server used by the screen and pending-client views;
 *        may be NULL.
 */
void menu_enter(MenuState *state, TelnetServer *telnet_server);

/**
 * @brief Dispatch one keypress according to the current menu mode. ESC leaves the
 *        current view; KEY_NONE and function or unknown multi-byte keys are ignored.
 * @param state Menu state to act on.
 * @param key The key event; NULL is ignored.
 * @param telnetServer Telnet server used by the screen and pending-client views;
 *        may be NULL.
 */
void menu_process_key(MenuState *state, const KeyEvent *key, TelnetServer *telnet_server);

/**
 * @brief Drive the menu's timed work: dismiss an expired MENU_MESSAGE back to its
 *        return mode, and redraw the pending-client list every 2 seconds and the
 *        HDLC status and CPU speed views every second.
 * @param state Menu state to update.
 * @param telnetServer Telnet server used by the pending-client view; may be NULL.
 */
void menu_tick(MenuState *state, TelnetServer *telnet_server);
#else
/**
 * @brief Switch the menu into MENU_F12 and draw the top-level F12 menu.
 * @param state Menu state to change.
 * @param telnetServer Unused in the WASM build; may be NULL.
 */
void menu_enter(MenuState *state, void *telnetServer);

/**
 * @brief Dispatch one keypress according to the current menu mode. ESC leaves the
 *        current view; KEY_NONE and function or unknown multi-byte keys are ignored.
 * @param state Menu state to act on.
 * @param key The key event; NULL is ignored.
 * @param telnetServer Unused in the WASM build; may be NULL.
 */
void menu_process_key(MenuState *state, const KeyEvent *key, void *telnetServer);

/**
 * @brief Drive the menu's timed work: dismiss an expired MENU_MESSAGE back to its
 *        return mode, and redraw the HDLC status and CPU speed views every second.
 * @param state Menu state to update.
 * @param telnetServer Unused in the WASM build; may be NULL.
 */
void menu_tick(MenuState *state, void *telnetServer);
#endif

#endif // SCREENMENU_H
