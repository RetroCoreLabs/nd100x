/*
 * nd100x_shell.h - Interactive shell for loading and running BPUN/PROG files: API.
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

#ifndef ND100X_SHELL_H
#define ND100X_SHELL_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Interactive shell for loading and running BPUN/PROG files on the ND-100
 *
 * The shell provides a command-line interface for:
 * - Listing available BPUN/PROG files
 * - Loading programs into memory
 * - Running programs
 * - Basic file operations
 */

/* Return codes from nd100x_shell_run() */
// clang-format off
#define SHELL_RESULT_EXIT   0   /* user quit the shell (EXIT / EOF) - stop the emulator */
#define SHELL_RESULT_ERROR (-1) /* script execution failed */
#define SHELL_RESULT_RUN    2   /* a program was loaded and armed (gPC=STARTADDR,        */
// clang-format on
/* CPU_RUNNING); the caller should hand control to the   */
/* normal machine run loop to execute it                 */

/**
 * @brief Run the interactive shell: optionally execute a script file first, then
 *        read and execute commands until the user quits or asks to run a program.
 *
 * @param nd100Root Directory to search for BPUN/PROG files (NULL = use current dir)
 * @param scriptPath Path to script file to execute (NULL = no script)
 *
 * @return SHELL_RESULT_EXIT on clean quit, SHELL_RESULT_ERROR on error, or
 *         SHELL_RESULT_RUN when the user asked to run a loaded program (the
 *         caller must then drive the machine run loop).
 */
int nd100x_shell_run(const char *nd100_root, const char *script_path);

#endif /* ND100X_SHELL_H */
