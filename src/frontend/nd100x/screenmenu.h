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
void menu_init(MenuState *state, VScreen *screens, int screenCount, int *activeScreen);

// Check if menu is currently active (suppresses VScreen stdout output)
static inline bool menu_is_active(const MenuState *state)
{
    return state->mode != MENU_NONE;
}

// Enter the F12 menu system
#if !defined(__EMSCRIPTEN__)
void menu_enter(MenuState *state, TelnetServer *telnetServer);
void menu_process_key(MenuState *state, const KeyEvent *key, TelnetServer *telnetServer);
void menu_tick(MenuState *state, TelnetServer *telnetServer);
#else
void menu_enter(MenuState *state, void *telnetServer);
void menu_process_key(MenuState *state, const KeyEvent *key, void *telnetServer);
void menu_tick(MenuState *state, void *telnetServer);
#endif

#endif // SCREENMENU_H
