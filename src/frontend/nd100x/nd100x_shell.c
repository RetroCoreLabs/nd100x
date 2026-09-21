/*
 * nd100x_shell.c - Interactive shell for listing, loading and running BPUN/PROG files.
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

#include "nd100x_shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <dirent.h>
#include <sys/stat.h>

#include "../ndlib/ndlib_types.h"
#include "../ndlib/ndlib_protos.h"
#include "../../machine/machine_types.h"
#include "../../machine/machine_protos.h"
#include "../../cpu/cpu_types.h"
#include "../../cpu/cpu_protos.h"

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#define SHELL_PROMPT "@"
#define MAX_CMD_LEN  256
#define MAX_TOKENS   10

typedef struct
{
    const char *name;
    const char *abbrev;
    const char *help;
    int (*handler)(const char *nd100_root, int argc, char **argv);
} ShellCommand;

/* Forward declarations */
static int cmd_help(const char *nd100_root, int argc, char **argv);
static int cmd_exit(const char *nd100_root, int argc, char **argv);
static int cmd_list_files(const char *nd100_root, int argc, char **argv);
static int cmd_run_program(const char *nd100_root, int argc, char **argv);
static int cmd_show_regs(const char *nd100_root, int argc, char **argv);

// clang-format off
static ShellCommand commands[] = {
    {"HELP",          "HE",   "Show this help message", cmd_help},
    {"LIST-FILES",    "LI-FI", "List BPUN/PROG files", cmd_list_files},
    {"RUN-PROGRAM",   "RU-PR", "Load and run a program", cmd_run_program},
    {"SHOW-REGISTERS","SH-RE", "Display CPU registers", cmd_show_regs},
    {"EXIT",          "EX",   "Exit the shell", cmd_exit},
    {NULL, NULL, NULL, NULL}
};
// clang-format on

/**
 * Check if a command name matches a command (supports abbreviation)
 * Abbreviation rules:
 * - Full name match: exact
 * - Abbreviated match: each part separated by '-' must match the prefix
 *   e.g., "LI-FI" matches "LIST-FILES", "LI" matches "LIST-FILES"
 */
static bool cmd_matches(const char *input, const char *full_name, const char *abbrev)
{
    if (!input || !full_name)
    {
        return false;
    }

    /* Uppercase input for case-insensitive comparison */
    char upper_input[MAX_CMD_LEN];
    int i = 0;
    for (; input[i] && i < MAX_CMD_LEN - 1; i++)
    {
        upper_input[i] = (char)toupper((unsigned char)input[i]);
    }
    upper_input[i] = '\0'; /* input longer than the buffer is cut, not overrun */

    /* Exact match on full name or abbreviation */
    if (strcmp(upper_input, full_name) == 0)
    {
        return true;
    }
    if (abbrev && strcmp(upper_input, abbrev) == 0)
    {
        return true;
    }

    /* Prefix match: "LIST" matches "LIST-FILES" */
    size_t len = strlen(upper_input);
    if (strncmp(upper_input, full_name, len) == 0)
    {
        char next = full_name[len];
        return (next == '\0' || next == '-');
    }

    return false;
}

/**
 * Parse a command line into tokens
 * Returns token count, sets tokens array
 */
static int parse_tokens(char *line, char **tokens, int max_tokens)
{
    int count = 0;
    char *ptr = line;

    while (count < max_tokens && *ptr)
    {
        while (isspace((unsigned char)*ptr))
        {
            ptr++;
        }
        if (!*ptr)
        {
            break;
        }

        tokens[count++] = ptr;
        while (*ptr && !isspace((unsigned char)*ptr))
        {
            ptr++;
        }
        if (*ptr)
        {
            *ptr++ = '\0';
        }
    }

    return count;
}

/**
 * Join a directory and a name into out, collapsing a trailing '/' on dir so we
 * never emit a doubled slash (e.g. "images/BPUN//mac.bpun").
 */
static void join_path(char *out, size_t out_sz, const char *dir, const char *name)
{
    size_t dlen = strlen(dir);
    if (dlen > 0 && dir[dlen - 1] == '/')
    {
        snprintf(out, out_sz, "%s%s", dir, name);
    }
    else
    {
        snprintf(out, out_sz, "%s/%s", dir, name);
    }
}

/**
 * List BPUN/PROG files in the nd100Root directory
 * Supports pattern filtering: *.bpun, *.prog, etc.
 */
static int cmd_list_files(const char *nd100_root, int argc, char **argv)
{

    const char *pattern = (argc > 1) ? argv[1] : "*";
    const char *search_dir = nd100_root ? nd100_root : ".";

    DIR *dir = opendir(search_dir);
    if (!dir)
    {
        fprintf(stderr, "Cannot open directory: %s\n", search_dir);
        return -1;
    }

    printf("Files in %s matching '%s':\n", search_dir, pattern);

    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL)
    {
        /* Match pattern */
        bool match = false;
        if (strcmp(pattern, "*") == 0)
        {
            match = true;
        }
        else if (strstr(pattern, "*"))
        {
            /* Simple glob: check extensions */
            const char *ext = strrchr(pattern, '.');
            if (ext)
            {
                const char *file_ext = strrchr(entry->d_name, '.');
                if (file_ext)
                {
                    match = (strcasecmp(file_ext, ext) == 0);
                }
            }
        }
        else
        {
            match = (strcasecmp(entry->d_name, pattern) == 0);
        }

        if (match)
        {
            /* dirent d_type is not portable (MinGW lacks it) - stat instead
             * to keep only regular files. */
            char full[512];
            struct stat st;
            join_path(full, sizeof(full), search_dir, entry->d_name);
            if (stat(full, &st) != 0 || !S_ISREG(st.st_mode))
            {
                continue;
            }

            printf("  %s\n", entry->d_name);
            count++;
        }
    }
    closedir(dir);

    if (count == 0)
    {
        printf("  (no files found)\n");
    }
    return 0;
}

/**
 * Resolve a user-typed program name to an actual regular file in dir.
 *
 * Matching (case-insensitive, so "run mac" finds "MAC.BPUN"):
 *   rank 0: the directory entry equals the typed name exactly
 *   rank 1: entry base name equals the typed name and entry ends in .bpun
 *   rank 2: entry base name equals the typed name and entry ends in .prog
 * The lowest rank wins. If the typed name already resolves to a readable file
 * (e.g. on a case-insensitive host FS), that is used directly.
 *
 * Returns true and fills out[] with the full path on success; false otherwise.
 */
static bool resolve_program_file(const char *dir, const char *name, char *out, size_t out_sz)
{
    /* 1. Exact path as typed - covers absolute names and case-insensitive FS. */
    char cand[512];
    join_path(cand, sizeof(cand), dir, name);
    struct stat st;
    if (stat(cand, &st) == 0 && S_ISREG(st.st_mode))
    {
        snprintf(out, out_sz, "%s", cand);
        return true;
    }

    /* 2. Scan the directory for a case-insensitive match, optionally supplying
     *    a .bpun / .prog extension the user omitted. */
    DIR *d = opendir(dir);
    if (!d)
    {
        return false;
    }

    char best[256] = "";
    int best_rank = 99;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
    {
        /* No d_type filter here - it is not portable (MinGW lacks it); the
         * final stat() below rejects non-files. */
        int rank = 99;
        if (strcasecmp(e->d_name, name) == 0)
        {
            rank = 0;
        }
        else
        {
            const char *dot = strrchr(e->d_name, '.');
            if (dot)
            {
                size_t base_len = (size_t)(dot - e->d_name);
                if (base_len == strlen(name) && strncasecmp(e->d_name, name, base_len) == 0)
                {
                    if (strcasecmp(dot, ".bpun") == 0)
                    {
                        rank = 1;
                    }
                    else if (strcasecmp(dot, ".prog") == 0)
                    {
                        rank = 2;
                    }
                }
            }
        }
        if (rank < best_rank)
        {
            best_rank = rank;
            snprintf(best, sizeof(best), "%s", e->d_name);
        }
    }
    closedir(d);

    if (best_rank == 99)
    {
        return false;
    }

    join_path(cand, sizeof(cand), dir, best);
    if (stat(cand, &st) != 0 || !S_ISREG(st.st_mode))
    {
        return false;
    }
    snprintf(out, out_sz, "%s", cand);
    return true;
}

/**
 * Load and run a BPUN program file
 */
static int cmd_run_program(const char *nd100_root, int argc, char **argv)
{

    if (argc < 2)
    {
        fprintf(stderr, "Usage: RUN-PROGRAM <filename>\n");
        return -1;
    }

    const char *filename = argv[1];
    const char *search_dir = nd100_root ? nd100_root : ".";

    /* Resolve the typed name to a real file. "run mac" -> "MAC.BPUN". */
    char filepath[512];
    if (!resolve_program_file(search_dir, filename, filepath, sizeof(filepath)))
    {
        fprintf(stderr, "No such program: '%s' in %s\n", filename, search_dir);
        fprintf(stderr, "Use LIST-FILES to see available programs.\n");
        return -1; /* CPU is NOT armed - shell stays at the prompt */
    }

    /* Hard pre-check: program_load()/LoadBPUN() print an error but return 0 on
     * a failed fopen (STARTADDR stays 0), so their return value cannot be
     * trusted to gate execution. Verify the file is actually readable here so
     * we never arm the CPU on a file we could not open. */
    FILE *probe = fopen(filepath, "rb");
    if (!probe)
    {
        fprintf(stderr, "Cannot open '%s': %s\n", filepath, strerror(errno));
        return -1;
    }
    fclose(probe);

    printf("Loading %s...\n", filepath);

    /* Determine file type by extension. */
    const char *ext = strrchr(filepath, '.');
    bool is_prog = (ext && strcasecmp(ext, ".prog") == 0);

    if (is_prog)
    {
        /* SINTRAN :PROG loadable image. program_load() writes the Bank 1 image
         * and sets STARTADDR to the real entry; the entry also comes from the
         * header via GetLastPROGHeader(). :PROG has no BPUN-style action field -
         * it always autostarts at its start address. */
        if (program_load(BOOT_PROG, 0, filepath, true, 0, false) < 0)
        {
            fprintf(stderr, "Could not load :PROG '%s' - not running.\n", filepath);
            return -1;
        }

        PROG_Header phdr;
        if (!GetLastPROGHeader(&phdr))
        {
            fprintf(stderr, "Could not read :PROG header - not running.\n");
            return -1;
        }
        if (phdr.twoBank)
        {
            /* Bank 2 needs the alternative page table, not yet mapped. */
            fprintf(stderr, "This is a 2-bank :PROG - only Bank 1 is loaded; "
                            "Bank 2 (alt page table) is not yet supported.\n");
        }

        gPC = phdr.startAddress;
        set_cpu_run_mode(CPU_RUNNING);
        printf("Starting at 0o%o - handing control to the ND-100...\n\n", phdr.startAddress);
        return SHELL_RESULT_RUN;
    }

    /* Otherwise treat as :BPUN. LoadBPUN() returns only the obsolete
     * bootstrap-loader "boot" address, which is NOT the program entry
     * (e.g. MAC.BPUN has boot=0 but its real entry is start=0164316). The
     * fopen pre-check above guards the file-not-found case; a genuinely corrupt
     * image makes program_load() fail, and the shell stays at its prompt. */
    if (program_load(BOOT_BPUN, 0, filepath, true, 0, false) < 0)
    {
        fprintf(stderr, "Could not load BPUN '%s' - not running.\n", filepath);
        return -1;
    }

    /* Per the :BPUN format: the program entry is the "start" field; the
     * "action" field controls autostart - if action == 0 execution begins at
     * start, otherwise the CPU stays in OPCOM (halted) with P = start. Read the
     * real header (program_load left STARTADDR = boot, which is wrong here). */
    BPUN_Header hdr;
    if (!GetLastBPUNHeader(&hdr))
    {
        fprintf(stderr, "Could not read BPUN header - not running.\n");
        return -1;
    }

    gPC = hdr.start; /* the real program entry (P register) */

    if (hdr.action != 0)
    {
        /* Non-autostart image: leave the CPU halted with P = start, like a
         * real ND-100 would sit in OPCOM. Stay at the shell prompt. */
        set_cpu_run_mode(CPU_STOPPED);
        printf("Loaded. Action=0o%o (non-autostart): P set to 0o%o, CPU held.\n", hdr.action,
               hdr.start);
        printf("Use the debugger to run it, or load an autostart image.\n");
        return 0;
    }

    /* Autostart (action == 0): enter at start and mark the CPU runnable, then
     * hand control back to the caller (main), which drives the real machine
     * run loop - reusing all terminal I/O, menu and telnet plumbing. */
    set_cpu_run_mode(CPU_RUNNING);
    printf("Starting at 0o%o - handing control to the ND-100...\n\n", hdr.start);

    return SHELL_RESULT_RUN;
}

/**
 * Display CPU registers
 */
static int cmd_show_regs(const char *nd100_root, int argc, char **argv)
{
    (void)nd100_root;
    (void)argc;
    (void)argv;


    printf("CPU Registers:\n");

    if (g_reg)
    {
        printf("  A:     0o%06o\n", g_reg->reg[gPIL][_A]);
        printf("  B:     0o%06o\n", g_reg->reg[gPIL][_B]);
        printf("  D:     0o%06o\n", g_reg->reg[gPIL][_D]);
        printf("  X:     0o%06o\n", g_reg->reg[gPIL][_X]);
        printf("  L:     0o%06o\n", g_reg->reg[gPIL][_L]);
        printf("  T:     0o%06o\n", g_reg->reg[gPIL][_T]);
        printf("  P:     0o%06o\n", g_reg->reg[gPIL][_P]);
        printf("  STS:   0o%06o\n", g_reg->reg_STS);
    }

    return 0;
}

/**
 * Show help message
 */
static int cmd_help(const char *nd100_root, int argc, char **argv)
{
    (void)nd100_root;
    (void)argc;
    (void)argv;

    printf("ND-100 Interactive Shell - Available Commands:\n\n");
    for (int i = 0; commands[i].name; i++)
    {
        printf("  %-15s  %s\n", commands[i].name, commands[i].help);
        printf("      (abbrev: %s)\n", commands[i].abbrev);
    }
    printf("\nNote: Commands are case-insensitive and support abbreviation.\n");
    printf("      File names use host extensions (.bpun, .prog, etc)\n");

    return 0;
}

/**
 * Exit the shell
 */
static int cmd_exit(const char *nd100_root, int argc, char **argv)
{
    (void)nd100_root;
    (void)argc;
    (void)argv;

    printf("Exiting shell.\n");
    return 1; /* Signal to exit the shell loop */
}

/**
 * Execute a single command
 * Returns: 0 = continue, 1 = exit shell, -1 = error
 */
static int execute_command(const char *nd100_root, char *line)
{
    if (!line || *line == '\0')
    {
        return 0;
    }

    char *tokens[MAX_TOKENS];
    int argc = parse_tokens(line, tokens, MAX_TOKENS);

    if (argc == 0)
    {
        return 0;
    }

    /* Find matching command */
    for (int i = 0; commands[i].name; i++)
    {
        if (cmd_matches(tokens[0], commands[i].name, commands[i].abbrev))
        {
            int result = commands[i].handler(nd100_root, argc, tokens);
            return result;
        }
    }

    printf("Unknown command: %s\n", tokens[0]);
    printf("Type 'HELP' for available commands.\n");
    return -1;
}

/**
 * Read a line from input (with or without readline)
 */
static char *read_line(void)
{
#ifdef HAVE_READLINE
    return readline(SHELL_PROMPT " ");
#else
    static char buffer[MAX_CMD_LEN];
    printf("%s ", SHELL_PROMPT);
    fflush(stdout);
    if (fgets(buffer, sizeof(buffer), stdin))
    {
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n')
        {
            buffer[len - 1] = '\0';
        }
        return buffer;
    }
    return NULL;
#endif
}

/**
 * Execute commands from a script file
 */
static int execute_script(const char *nd100_root, const char *script_path)
{
    FILE *f = fopen(script_path, "r");
    if (!f)
    {
        fprintf(stderr, "Cannot open script file: %s\n", script_path);
        return -1;
    }

    char line[MAX_CMD_LEN];
    int line_num = 0;
    while (fgets(line, sizeof(line), f))
    {
        line_num++;

        /* Remove trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
        {
            line[len - 1] = '\0';
        }

        /* Skip empty lines and comments */
        if (!*line || *line == '#')
        {
            continue;
        }

        printf("%s %s\n", SHELL_PROMPT, line);
        int result = execute_command(nd100_root, line);
        if (result == 1)
        {
            break; /* EXIT command */
        }
        if (result == SHELL_RESULT_RUN)
        { /* RUN-PROGRAM armed the CPU */
            fclose(f);
            return SHELL_RESULT_RUN;
        }
        if (result < 0)
        {
            fprintf(stderr, "Script error at line %d\n", line_num);
            fclose(f);
            return -1;
        }
    }

    fclose(f);
    return 0;
}

/**
 * Main shell loop
 */
int nd100x_shell_run(const char *nd100_root, const char *script_path)
{
    printf("\n");
    printf("ND-100 Interactive Shell\n");
    printf("Type 'HELP' for available commands\n");
    printf("\n");

#ifdef HAVE_READLINE
    using_history();
#endif

    /* Execute script if provided */
    if (script_path)
    {
        printf("Loading script: %s\n\n", script_path);
        int result = execute_script(nd100_root, script_path);
        if (result == SHELL_RESULT_RUN)
        {
            return SHELL_RESULT_RUN; /* script launched a program */
        }
        if (result < 0)
        {
            fprintf(stderr, "Script execution failed\n");
            return -1;
        }
    }

    /* Main REPL loop */
    while (1)
    {
        char *line = read_line();
        if (!line)
        {
            printf("\n");
            break; /* EOF */
        }

#ifdef HAVE_READLINE
        if (*line)
        {
            add_history(line);
        }
#endif

        int result = execute_command(nd100_root, line);
        if (result == 1)
        {
#ifdef HAVE_READLINE
            free(line);
#endif
            break; /* EXIT command */
        }
        if (result == SHELL_RESULT_RUN)
        {
#ifdef HAVE_READLINE
            free(line);
#endif
            return SHELL_RESULT_RUN; /* hand control to the machine run loop */
        }

#ifdef HAVE_READLINE
        free(line);
#endif
    }

    printf("Shell exited.\n");
    return 0;
}
