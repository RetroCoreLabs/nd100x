/*
 * bootstatus.h - "Booting from ..." line shown until the guest first prints
 * on the console.
 *
 * Since start-up became quiet (INFO lines only with --verbose), nothing
 * appears between starting nd100x and the guest's first console output, so a
 * slow boot looks like a hang. This module draws one self-overwriting line on
 * the host terminal with a spinner and the elapsed time, and erases it the
 * moment the console prints. Turned off with --no-boot-status or
 * [runtime] boot_status = off.
 */

#ifndef BOOTSTATUS_H
#define BOOTSTATUS_H

#include <stdbool.h>

/* Start the line. 'what' names the boot device, e.g. "CDC disc (cdc.img)".
 * Does nothing when stdout is not a terminal. */
void boot_status_start(const char *what);

/* Redraw the line; call every pass of the main loop. Redraws at most every
 * 100 ms. 'waiting' = the CPU is paused or at a breakpoint (debugger). */
void boot_status_tick(bool waiting);

/* Erase the line and stop for good. Safe to call any number of times. */
void boot_status_done(void);

/* True while the line is shown. */
bool boot_status_active(void);

#endif /* BOOTSTATUS_H */
