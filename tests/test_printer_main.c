/*
 * Main test runner for printer module unit tests.
 *
 * Runs all test suites: pdfwriter, escp, printjob.
 * Uses a temp directory for output files, cleaned up on exit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Linker stubs for symbols declared in ndlib_types.h but defined in
 * the cpu module.  The test code never calls these, but the linker
 * may pull them in transitively through ndlib object files. */
#include <stdint.h>
#include <stdbool.h>
/* Prototypes match the real functions in src/cpu (cpu_protos.h). */
int ReadPhysicalMemory(int physicalAddress, bool privileged);
void WritePhysicalMemory(int physicalAddress, uint16_t value, bool privileged);
int ReadPhysicalMemory(int physicalAddress, bool privileged)
{
    (void)physicalAddress; (void)privileged;
    return 0;
}
void WritePhysicalMemory(int physicalAddress, uint16_t value, bool privileged)
{
    (void)physicalAddress; (void)value; (void)privileged;
}

/* Suite runners (defined in each test file) */
#include "test_suites.h"

static void cleanup_tmpdir(const char *path)
{
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    system(cmd);
}

int main(void)
{
    /* Create temp directory */
    char tmpdir[] = "/tmp/test_printer_XXXXXX";
    if (mkdtemp(tmpdir) == NULL) {
        perror("mkdtemp");
        return 1;
    }

    printf("test_printer: temp dir = %s\n\n", tmpdir);

    int total_failures = 0;

    printf("[pdfwriter]\n");
    total_failures += run_pdfwriter_tests(tmpdir);
    printf("\n");

    printf("[escp]\n");
    total_failures += run_escp_tests();
    printf("\n");

    printf("[printjob]\n");
    total_failures += run_printjob_tests(tmpdir);
    printf("\n");

    /* Cleanup */
    cleanup_tmpdir(tmpdir);

    if (total_failures == 0) {
        printf("All tests PASSED.\n");
    } else {
        printf("%d test(s) FAILED.\n", total_failures);
    }

    return total_failures ? 1 : 0;
}
