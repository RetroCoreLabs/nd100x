/*
 * test_srcmap_globals.c - the GLOBAL lines of a .srcmap (libsymbols, srcmap_debug.c).
 *
 * aout2brf -S of nd-lang writes one line per data-element of a PLANC module:
 *     GLOBAL:NAME:TYPE -> ADDRESS     (address in octal)
 * and the TYPE may hold a colon itself ("word(0:300)"). The map of tests/oracle100/N100A.PLNC
 * of nd-lang, as aout2brf wrote it (2026-11-22), is the test input.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include "symbols.h"

static int failed;
static int checks;

static void check(bool ok, const char *what)
{
    checks++;
    if (!ok)
    {
        failed++;
        printf("FAIL %s\n", what);
    }
}

static const char *write_map(const char *text)
{
    static char path[64];
    snprintf(path, sizeof path, "/tmp/test_srcmap_globals_%d.srcmap", (int)getpid());
    FILE *f = fopen(path, "w");
    if (f == NULL)
    {
        return NULL;
    }
    fputs(text, f);
    fclose(f);
    return path;
}

int main(void)
{
    const char *path = write_map("# Source map for nd100x DAP debugger\n"
                                 "# Format: filename:line -> address\n"
                                 "GLOBAL:STACK:word(0:300) -> 000000\n"
                                 "GLOBAL:A:word -> 000455\n"
                                 "GLOBAL:B:word -> 000456\n"
                                 "GLOBAL:C:word -> 000457\n"
                                 "N100A.PLNC:5 -> 000470\n"
                                 "FUNC:MAIN -> 000470\n");
    check(path != NULL, "the map file could not be written");
    if (path == NULL)
    {
        return 1;
    }
    symbol_debug_info_t *info = symbols_debug_info_create();
    check(symbols_load_srcmap_debug(info, path), "the map did not load");
    check(info->global_count == 4, "four GLOBAL lines, four variables");
    if (info->global_count == 4)
    {
        check(strcmp(info->globals[0].name, "STACK") == 0, "the name ends at the first colon");
        check(strcmp(info->globals[0].type_name, "word(0:300)") == 0, "the type keeps its own colon");
        check(info->globals[0].address == 0, "STACK at 0");
        check(strcmp(info->globals[1].name, "A") == 0 && strcmp(info->globals[1].type_name, "word") == 0,
              "A, a word");
        check(info->globals[1].address == 0455, "A at 455 octal");
        check(info->globals[3].address == 0457, "C at 457 octal");
    }
    check(info->function_count == 1 && info->functions[0].start_address == 0470, "the FUNC line still loads");
    symbols_debug_info_free(info);

    /* a map with GLOBAL lines only is debug information too */
    path = write_map("GLOBAL:X:byte -> 000012\n");
    info = symbols_debug_info_create();
    check(symbols_load_srcmap_debug(info, path), "a map with GLOBAL lines only did not load");
    check(info->global_count == 1 && info->function_count == 0, "one variable, no function");
    symbols_debug_info_free(info);

    /* nothing of either kind: no debug information, as before */
    path = write_map("N100A.PLNC:5 -> 000470\n");
    info = symbols_debug_info_create();
    check(!symbols_load_srcmap_debug(info, path), "a map of lines only is no debug information");
    symbols_debug_info_free(info);
    remove(path);

    printf("%d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
