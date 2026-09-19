/*
 * test_shell.c - Unit tests for the ND-100X interactive shell
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * These tests exercise the REAL shipped shell code, not a re-implementation
 * of its logic. nd100x_shell.c is #included directly so the file-static
 * functions (cmd_matches, parse_tokens, execute_command, cmd_list_files,
 * cmd_exit, ...) are reachable and tested exactly as they ship.
 *
 * The two external symbols the shell references - program_load() and the
 * global gReg register file - are stubbed below so the tests link without
 * pulling in the machine, the CPU core or a disk image.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ---- Stubs for the shell's external dependencies ------------------------ */
/* nd100x_shell.c pulls in cpu_types.h (declares gReg) and machine_protos.h  */
/* (declares program_load).  We provide the definitions here so the shell    */
/* code under test is the real thing while its collaborators are controlled. */

/* Records of the last program_load() call, so RUN-PROGRAM can be verified. */
static int g_prog_load_calls = 0;
static char g_prog_load_path[512];
static int g_prog_load_return = 0100; /* fake entry point (octal 100) */

/* Pull in the real shell implementation (gives access to the statics). */
#include "../src/frontend/nd100x/nd100x_shell.c"

/* Real register file the shell's SHOW-REGISTERS path dereferences. */
static struct CpuRegs g_fake_regs;
struct CpuRegs *g_reg = &g_fake_regs;

/* Stub loader: never touches disk; just records the request. */
int program_load(BOOT_TYPE bootType, int bootUnit, const char *imageFile, bool verbose,
                 uint16_t text_start, bool overlay_deposit)
{
    (void)bootType;
    (void)bootUnit;
    (void)verbose;
    (void)text_start;
    (void)overlay_deposit;
    g_prog_load_calls++;
    if (imageFile)
    {
        snprintf(g_prog_load_path, sizeof(g_prog_load_path), "%s", imageFile);
    }
    else
    {
        g_prog_load_path[0] = '\0';
    }
    return g_prog_load_return;
}

/* STARTADDR is a real global in cpu.c; define it here so the test links. */
uint16_t g_start_addr = 0;

/* Controllable BPUN header the shell reads via GetLastBPUNHeader(). */
static BPUN_Header g_fake_bpun;
static bool g_fake_bpun_valid = true;
bool GetLastBPUNHeader(BPUN_Header *out)
{
    if (!g_fake_bpun_valid || !out)
    {
        return false;
    }
    *out = g_fake_bpun;
    return true;
}

/* Controllable :PROG header the shell reads via GetLastPROGHeader(). */
static PROG_Header g_fake_prog;
static bool g_fake_prog_valid = true;
bool GetLastPROGHeader(PROG_Header *out)
{
    if (!g_fake_prog_valid || !out)
    {
        return false;
    }
    *out = g_fake_prog;
    return true;
}

/* Record the run mode the shell arms via set_cpu_run_mode(). */
static CPURunMode g_run_mode = CPU_STOPPED;
void set_cpu_run_mode(CPURunMode new_mode)
{
    g_run_mode = new_mode;
}
CPURunMode get_cpu_run_mode(void)
{
    return g_run_mode;
}

/* ---- Tiny test harness -------------------------------------------------- */
static int g_checks = 0;
static int g_fails = 0;

// clang-format off
#define CHECK(cond, ...) do {                        \
    g_checks++;                                      \
    if (!(cond)) {                                   \
        g_fails++;                                   \
        printf("  FAIL: ");                          \
        printf(__VA_ARGS__);                         \
        printf("   [%s:%d]\n", __FILE__, __LINE__);  \
    }                                                \
} while (0)
// clang-format on

/* Capture stdout produced by fn() into a buffer, so we can assert on it.
 *
 * The real stdout descriptor is saved with dup() BEFORE any redirection and is
 * ALWAYS restored with dup2() afterwards - on every path, including when the
 * temp file or the dup() itself fails. This guarantees the controlling tty is
 * never left wired to a temp/closed descriptor after this function returns. */
static void capture_stdout(void (*fn)(void *), void *arg, char *out, size_t out_sz)
{
    out[0] = '\0';

    /* Save the real stdout first; if we cannot, run without capturing so we
     * never risk clobbering the tty. */
    fflush(stdout);
    int saved = dup(fileno(stdout));
    if (saved < 0)
    {
        fn(arg);
        return;
    }

    char tmpl[] = "/tmp/nd100x-shell-cap-XXXXXX";
    int tmpfd = mkstemp(tmpl);
    if (tmpfd < 0)
    {
        /* No temp file: run uncaptured, nothing was redirected, tty intact. */
        close(saved);
        fn(arg);
        return;
    }

    /* Redirect. If dup2 fails, restore immediately and bail. */
    if (dup2(tmpfd, fileno(stdout)) < 0)
    {
        dup2(saved, fileno(stdout));
        close(saved);
        close(tmpfd);
        unlink(tmpl);
        fn(arg);
        return;
    }

    fn(arg);

    /* Restore the real stdout - this is the critical tty reset. */
    fflush(stdout);
    dup2(saved, fileno(stdout));
    close(saved);

    lseek(tmpfd, 0, SEEK_SET);
    ssize_t n = read(tmpfd, out, out_sz - 1);
    out[n > 0 ? n : 0] = '\0';
    close(tmpfd);
    unlink(tmpl);
}

/* ======================================================================== */
/* Test: the REAL cmd_matches() abbreviation/prefix logic                    */
/* ======================================================================== */
static void test_cmd_matches(void)
{
    printf("TEST: cmd_matches() (real shell function)\n");

    /* full-name exact, case-insensitive */
    CHECK(cmd_matches("HELP", "HELP", "HE"), "HELP should match HELP");
    CHECK(cmd_matches("help", "HELP", "HE"), "help (lowercase) should match HELP");
    CHECK(cmd_matches("HeLp", "HELP", "HE"), "mixed case should match HELP");

    /* declared abbreviation exact */
    CHECK(cmd_matches("HE", "HELP", "HE"), "HE should match HELP via abbrev");
    CHECK(cmd_matches("LI-FI", "LIST-FILES", "LI-FI"), "LI-FI should match LIST-FILES");
    CHECK(cmd_matches("SH-RE", "SHOW-REGISTERS", "SH-RE"), "SH-RE should match SHOW-REGISTERS");

    /* word-boundary prefix match */
    CHECK(cmd_matches("LIST", "LIST-FILES", "LI-FI"), "LIST should prefix-match LIST-FILES");
    CHECK(cmd_matches("SHOW", "SHOW-REGISTERS", "SH-RE"),
          "SHOW should prefix-match SHOW-REGISTERS");

    /* NEGATIVE cases - must NOT match */
    CHECK(!cmd_matches("HELPX", "HELP", "HE"), "HELPX must NOT match HELP (overlong)");
    CHECK(!cmd_matches("SH", "SHOW-REGISTERS", "SH-RE"),
          "SH must NOT prefix-match SHOW-REGISTERS (not a word boundary: next char 'O')");
    CHECK(!cmd_matches("XYZ", "HELP", "HE"), "XYZ must NOT match HELP");
    CHECK(!cmd_matches("", "HELP", "HE"), "empty string must NOT match HELP");
}

/* ======================================================================== */
/* Test: the REAL parse_tokens() tokenizer                                   */
/* ======================================================================== */
static void test_parse_tokens(void)
{
    printf("TEST: parse_tokens() (real shell function)\n");

    {
        char line[] = "LIST-FILES *.bpun extra";
        char *tok[MAX_TOKENS];
        int n = parse_tokens(line, tok, MAX_TOKENS);
        CHECK(n == 3, "expected 3 tokens, got %d", n);
        CHECK(n >= 1 && strcmp(tok[0], "LIST-FILES") == 0, "tok0 wrong");
        CHECK(n >= 2 && strcmp(tok[1], "*.bpun") == 0, "tok1 wrong");
        CHECK(n >= 3 && strcmp(tok[2], "extra") == 0, "tok2 wrong");
    }
    { /* leading/trailing/multiple spaces and tabs */
        char line[] = "   HELP\t\t  ";
        char *tok[MAX_TOKENS];
        int n = parse_tokens(line, tok, MAX_TOKENS);
        CHECK(n == 1, "whitespace-heavy line: expected 1 token, got %d", n);
        CHECK(n == 1 && strcmp(tok[0], "HELP") == 0, "expected HELP token");
    }
    { /* empty line */
        char line[] = "";
        char *tok[MAX_TOKENS];
        int n = parse_tokens(line, tok, MAX_TOKENS);
        CHECK(n == 0, "empty line: expected 0 tokens, got %d", n);
    }
    { /* max-tokens clamp */
        char line[] = "a b c d e f g h i j k l m n";
        char *tok[MAX_TOKENS];
        int n = parse_tokens(line, tok, MAX_TOKENS);
        CHECK(n == MAX_TOKENS, "expected clamp to %d tokens, got %d", MAX_TOKENS, n);
    }
}

/* ======================================================================== */
/* Test: the REAL execute_command() dispatch + return codes                  */
/* ======================================================================== */
static void run_exec(void *arg)
{
    /* arg is a char* mutable command line */
    int *rc = ((struct {
                  char *line;
                  int *rc;
              } *)arg)
                  ->rc;
    char *line = ((struct {
                     char *line;
                     int *rc;
                 } *)arg)
                     ->line;
    *rc = execute_command(NULL, line);
}

static int exec_line(const char *cmd)
{
    /* execute_command mutates the buffer (tokenizing in place). */
    char buf[MAX_CMD_LEN];
    snprintf(buf, sizeof(buf), "%s", cmd);
    char sink[4096];
    int rc = 0;
    struct
    {
        char *line;
        int *rc;
    } a = {buf, &rc};
    capture_stdout(run_exec, &a, sink, sizeof(sink));
    return rc;
}

static void test_execute_command(void)
{
    printf("TEST: execute_command() (real dispatch)\n");

    CHECK(exec_line("EXIT") == 1, "EXIT must return 1 (exit shell)");
    CHECK(exec_line("EX") == 1, "EX abbrev must return 1");
    CHECK(exec_line("HELP") == 0, "HELP must return 0 (continue)");
    CHECK(exec_line("HE") == 0, "HE abbrev must return 0");
    CHECK(exec_line("") == 0, "empty line must return 0 (no-op)");
    CHECK(exec_line("BOGUS-COMMAND") == -1, "unknown command must return -1");

    /* RUN-PROGRAM with no filename -> usage error (-1), no load attempted */
    g_prog_load_calls = 0;
    CHECK(exec_line("RUN-PROGRAM") == -1, "RUN-PROGRAM with no arg must return -1");
    CHECK(g_prog_load_calls == 0, "no filename -> program_load must NOT be called");
}

/* ======================================================================== */
/* Test: RUN-PROGRAM builds the correct path and calls the loader            */
/* ======================================================================== */
static int g_runprog_rc;
static char g_runprog_root[256];
static char g_runprog_line[MAX_CMD_LEN];
static void run_runprog(void *arg)
{
    (void)arg;
    g_runprog_rc = execute_command(g_runprog_root, g_runprog_line);
}

static void make_file(const char *dir, const char *name)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    FILE *f = fopen(p, "w");
    if (f)
    {
        fputs("x", f);
        fclose(f);
    }
}

/* RUN-PROGRAM on a :PROG image: always autostarts at hdr.startAddress. */
static void check_run_prog_image(const char *dir, char *sink, size_t sink_size)
{
    /* :PROG image: always autostarts at hdr.startAddress (no action field). */
    make_file(dir, "tool.prog");
    g_prog_load_calls = 0;
    g_fake_prog_valid = true;
    g_fake_prog.startAddress = 0177777;
    g_fake_prog.twoBank = false;
    g_run_mode = CPU_STOPPED;
    g_fake_regs.reg[0][_P] = 0;
    snprintf(g_runprog_line, sizeof(g_runprog_line), "RUN-PROGRAM tool.prog");
    capture_stdout(run_runprog, NULL, sink, sink_size);
    CHECK(g_runprog_rc == SHELL_RESULT_RUN, ":PROG image must return SHELL_RESULT_RUN");
    CHECK(g_prog_load_calls == 1, ":PROG must call program_load once, got %d", g_prog_load_calls);
    CHECK(g_run_mode == CPU_RUNNING, ":PROG must arm CPU_RUNNING");
    CHECK(g_fake_regs.reg[0][_P] == 0177777, ":PROG must set gPC = hdr.startAddress");
}

/* RUN-PROGRAM on a missing file: error, loader not called, CPU not armed. */
static void check_run_missing_file(char *sink, size_t sink_size)
{
    /* Missing file: no resolve -> error, loader NOT called, CPU NOT armed. */
    g_prog_load_calls = 0;
    g_run_mode = CPU_STOPPED;
    snprintf(g_runprog_line, sizeof(g_runprog_line), "RUN-PROGRAM does-not-exist");
    capture_stdout(run_runprog, NULL, sink, sink_size);
    CHECK(g_runprog_rc == -1, "missing file must return -1, got %d", g_runprog_rc);
    CHECK(g_prog_load_calls == 0, "missing file: program_load must NOT be called");
    CHECK(g_run_mode == CPU_STOPPED, "missing file must NOT arm the CPU");
}

static void test_run_program_path(void)
{
    printf("TEST: RUN-PROGRAM resolve + pre-check + CPU arming\n");

    char sink[4096];

    /* Real temp dir with real files - resolve_program_file() and the fopen
     * pre-check now require the file to actually exist. Note the UPPERCASE
     * name: "run mac" must resolve to it case-insensitively. */
    char tmpl[] = "/tmp/nd100x-shell-run-XXXXXX";
    char *dir = mkdtemp(tmpl);
    CHECK(dir != NULL, "could not create temp dir");
    if (!dir)
    {
        return;
    }
    make_file(dir, "MAC.BPUN");
    make_file(dir, "hello.bpun");

    /* Success path (action==0 autostart): resolve, load, enter at hdr.start,
     * arm the CPU, ask caller to run. */
    g_prog_load_calls = 0;
    g_fake_bpun_valid = true;
    g_fake_bpun.start = 0164316; /* real program entry */
    g_fake_bpun.boot = 0;        /* obsolete bootstrap addr - must be ignored */
    g_fake_bpun.action = 0;      /* autostart */
    g_run_mode = CPU_STOPPED;
    g_fake_regs.reg_STS = 0; /* gPIL = 0 */
    g_fake_regs.reg[0][_P] = 0;
    snprintf(g_runprog_root, sizeof(g_runprog_root), "%s", dir);
    snprintf(g_runprog_line, sizeof(g_runprog_line), "RUN-PROGRAM hello.bpun");
    capture_stdout(run_runprog, NULL, sink, sizeof(sink));

    CHECK(g_prog_load_calls == 1, "program_load must be called exactly once, got %d",
          g_prog_load_calls);
    CHECK(g_runprog_rc == SHELL_RESULT_RUN,
          "RUN-PROGRAM autostart must return SHELL_RESULT_RUN(%d), got %d", SHELL_RESULT_RUN,
          g_runprog_rc);
    CHECK(g_run_mode == CPU_RUNNING, "RUN-PROGRAM must arm CPU_RUNNING");
    CHECK(g_fake_regs.reg[0][_P] == 0164316,
          "RUN-PROGRAM must set gPC = hdr.start (0o164316), NOT boot; got 0o%o",
          g_fake_regs.reg[0][_P]);

    /* Case-insensitive resolution + omitted extension: "run mac" -> MAC.BPUN. */
    g_prog_load_calls = 0;
    g_run_mode = CPU_STOPPED;
    snprintf(g_runprog_line, sizeof(g_runprog_line), "RUN-PROGRAM mac");
    capture_stdout(run_runprog, NULL, sink, sizeof(sink));
    CHECK(g_runprog_rc == SHELL_RESULT_RUN, "'run mac' should resolve MAC.BPUN and run");
    {
        char want[512];
        snprintf(want, sizeof(want), "%s/MAC.BPUN", dir);
        CHECK(strcmp(g_prog_load_path, want) == 0, "'run mac' loaded '%s', expected '%s'",
              g_prog_load_path, want);
    }

    /* Non-autostart image (action != 0): must NOT run; CPU held with P=start. */
    g_prog_load_calls = 0;
    g_fake_bpun.start = 0100;
    g_fake_bpun.action = 1;   /* non-zero -> OPCOM hold */
    g_run_mode = CPU_RUNNING; /* prove it gets set to STOPPED */
    g_fake_regs.reg[0][_P] = 0;
    snprintf(g_runprog_line, sizeof(g_runprog_line), "RUN-PROGRAM MAC.BPUN");
    capture_stdout(run_runprog, NULL, sink, sizeof(sink));
    CHECK(g_runprog_rc == 0, "action!=0 must return 0 (stay at prompt), got %d", g_runprog_rc);
    CHECK(g_run_mode == CPU_STOPPED, "action!=0 must hold the CPU (CPU_STOPPED)");
    CHECK(g_fake_regs.reg[0][_P] == 0100, "action!=0 must still set P = hdr.start");
    g_fake_bpun.action = 0; /* restore for later cases */

    check_run_prog_image(dir, sink, sizeof(sink));
    check_run_missing_file(sink, sizeof(sink));

    /* Cleanup */
    char p[512];
    snprintf(p, sizeof(p), "%s/MAC.BPUN", dir);
    unlink(p);
    snprintf(p, sizeof(p), "%s/hello.bpun", dir);
    unlink(p);
    snprintf(p, sizeof(p), "%s/tool.prog", dir);
    unlink(p);
    rmdir(dir);
}

/* ======================================================================== */
/* Test: the REAL cmd_list_files() against a real temp directory             */
/* ======================================================================== */
static char g_list_dir[256];
static char g_list_pattern[64];

static void run_list(void *arg)
{
    (void)arg;
    char *argv[2] = {(char *)"LIST-FILES", g_list_pattern};
    cmd_list_files(g_list_dir, 2, argv);
}

static void test_list_files(void)
{
    printf("TEST: cmd_list_files() (real dir scan + glob)\n");

    char tmpl[] = "/tmp/nd100x-shell-dir-XXXXXX";
    char *dir = mkdtemp(tmpl);
    CHECK(dir != NULL, "could not create temp dir");
    if (!dir)
    {
        return;
    }
    snprintf(g_list_dir, sizeof(g_list_dir), "%s", dir);

    /* Create a mix of files. */
    const char *names[] = {"alpha.bpun", "beta.bpun", "gamma.prog", "readme.txt"};
    for (int i = 0; i < 4; i++)
    {
        char p[512];
        snprintf(p, sizeof(p), "%s/%s", dir, names[i]);
        FILE *f = fopen(p, "w");
        CHECK(f != NULL, "could not create %s", names[i]);
        if (f)
        {
            fputs("x", f);
            fclose(f);
        }
    }

    char out[8192];

    /* Pattern *.bpun -> exactly the two .bpun files, no .prog/.txt */
    snprintf(g_list_pattern, sizeof(g_list_pattern), "*.bpun");
    capture_stdout(run_list, NULL, out, sizeof(out));
    CHECK(strstr(out, "alpha.bpun") != NULL, "*.bpun should list alpha.bpun\n--- output ---\n%s",
          out);
    CHECK(strstr(out, "beta.bpun") != NULL, "*.bpun should list beta.bpun");
    CHECK(strstr(out, "gamma.prog") == NULL, "*.bpun must NOT list gamma.prog");
    CHECK(strstr(out, "readme.txt") == NULL, "*.bpun must NOT list readme.txt");

    /* Pattern * -> everything (4 files) */
    snprintf(g_list_pattern, sizeof(g_list_pattern), "*");
    capture_stdout(run_list, NULL, out, sizeof(out));
    CHECK(strstr(out, "alpha.bpun") && strstr(out, "gamma.prog") && strstr(out, "readme.txt"),
          "'*' should list all files\n--- output ---\n%s", out);

    /* Exact filename match */
    snprintf(g_list_pattern, sizeof(g_list_pattern), "gamma.prog");
    capture_stdout(run_list, NULL, out, sizeof(out));
    CHECK(strstr(out, "gamma.prog") != NULL, "exact match should find gamma.prog");
    CHECK(strstr(out, "alpha.bpun") == NULL, "exact match must not find alpha.bpun");

    /* Cleanup */
    for (int i = 0; i < 4; i++)
    {
        char p[512];
        snprintf(p, sizeof(p), "%s/%s", dir, names[i]);
        unlink(p);
    }
    rmdir(dir);
}

/* ======================================================================== */
/* Test: cmd_exit prints the exit line and signals termination               */
/* ======================================================================== */
static void run_exit(void *arg)
{
    (void)arg;
    int *rc = (int *)arg;
    char *argv[1] = {(char *)"EXIT"};
    *rc = cmd_exit(NULL, 1, argv);
}

static void test_exit(void)
{
    printf("TEST: cmd_exit() return value + message\n");
    char out[1024];
    int rc = 0;
    capture_stdout(run_exit, &rc, out, sizeof(out));
    CHECK(rc == 1, "cmd_exit must return 1, got %d", rc);
    CHECK(strstr(out, "Exiting shell") != NULL, "cmd_exit should print an exit message");
}

/* ======================================================================== */
int main(void)
{
    printf("\n========================================\n");
    printf("ND-100X Shell Unit Tests (real code)\n");
    printf("========================================\n\n");

    test_cmd_matches();
    test_parse_tokens();
    test_execute_command();
    test_run_program_path();
    test_list_files();
    test_exit();

    printf("\n========================================\n");
    printf("Checks: %d   Failures: %d\n", g_checks, g_fails);
    printf("Result: %s\n", g_fails == 0 ? "PASS" : "FAIL");
    printf("========================================\n\n");

    return g_fails == 0 ? 0 : 1;
}
