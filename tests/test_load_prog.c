/*
 * test_load_prog.c - Unit tests for the SINTRAN :PROG loader.
 *
 * Exercises the REAL loader by #including src/ndlib/load_prog.c and stubbing its
 * two externals (WritePhysicalMemory + disasm hook), so the header parsing,
 * bank-1 word math and big-endian data placement are tested exactly as shipped,
 * without the machine or the CPU.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

/* ---- Stubs for load_prog.c's externals -------------------------------- */
/* Stubs for the real functions in src/cpu (cpu_protos.h); prototypes match. */
void WritePhysicalMemory(int physicalAddress, uint16_t value, bool privileged);
void disasm_addword(uint16_t addr, uint16_t myword);

static uint16_t g_mem[65536];
static int g_writes;
void WritePhysicalMemory(int physicalAddress, uint16_t value, bool privileged)
{
    (void)privileged;
    if (physicalAddress >= 0 && physicalAddress < 65536)
    {
        g_mem[physicalAddress] = value;
        g_writes++;
    }
}
int g_disasm = 0;
void disasm_addword(uint16_t addr, uint16_t myword)
{
    (void)addr;
    (void)myword;
}

#include "../src/ndlib/load_prog.c"

/* ---- Tiny harness ------------------------------------------------------ */
static int g_checks = 0, g_fails = 0;
#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if (!(cond))                                                                               \
        {                                                                                          \
            g_fails++;                                                                             \
            printf("  FAIL: ");                                                                    \
            printf(__VA_ARGS__);                                                                   \
            printf("  [%s:%d]\n", __FILE__, __LINE__);                                             \
        }                                                                                          \
    } while (0)

static void put_be16(FILE *f, uint16_t v)
{
    fputc((v >> 8) & 0xFF, f);
    fputc(v & 0xFF, f);
}

/* Write a synthetic 1-bank :PROG: header (6 words) padded to 512 bytes,
 * then 'n' big-endian data words. Returns the temp path (static buffer). */
static const char *make_prog(uint16_t start, uint16_t first, uint16_t last, uint16_t fB2,
                             uint16_t lB2, const uint16_t *data, int n)
{
    static char path[] = "/tmp/nd100x-prog-XXXXXX";
    snprintf(path, sizeof(path), "%s", "/tmp/nd100x-prog-XXXXXX");
    int fd = mkstemp(path);
    if (fd < 0)
    {
        return NULL;
    }
    FILE *f = fdopen(fd, "wb");
    put_be16(f, start);
    put_be16(f, start); /* start, restart */
    put_be16(f, first);
    put_be16(f, last);
    put_be16(f, fB2);
    put_be16(f, lB2);
    for (int i = 12; i < 512; i++)
    {
        fputc(0, f); /* pad header block */
    }
    for (int i = 0; i < n; i++)
    {
        put_be16(f, data[i]);
    }
    fclose(f);
    return path;
}

static void test_one_bank(void)
{
    printf("TEST: LoadPROG 1-bank\n");
    memset(g_mem, 0, sizeof(g_mem));
    g_writes = 0;

    uint16_t data[4] = {0x1111, 0x2222, 0x3333, 0x4444};
    /* first=0o200, last=0o203 -> (203-200)+1 = 4 words; 1-bank sentinel B2. */
    const char *p = make_prog(0100, 0200, 0203, 0xFFFF, 0x0000, data, 4);
    CHECK(p != NULL, "could not create temp .prog");
    if (!p)
    {
        return;
    }

    int rc = LoadPROG(p, true);
    CHECK(rc == 0100, "start should be 0o100, got 0o%o", rc);
    CHECK(g_writes == 4, "should write exactly 4 words, wrote %d", g_writes);
    CHECK(g_mem[0200] == 0x1111, "word@0o200 = 0x%04X, want 0x1111", g_mem[0200]);
    CHECK(g_mem[0201] == 0x2222, "word@0o201 = 0x%04X, want 0x2222", g_mem[0201]);
    CHECK(g_mem[0202] == 0x3333, "word@0o202 = 0x%04X, want 0x3333", g_mem[0202]);
    CHECK(g_mem[0203] == 0x4444, "word@0o203 = 0x%04X, want 0x4444", g_mem[0203]);
    CHECK(g_mem[0177] == 0 && g_mem[0204] == 0, "must not write outside the bank");

    PROG_Header h = {0};
    CHECK(GetLastPROGHeader(&h), "GetLastPROGHeader should succeed");
    CHECK(h.startAddress == 0100 && h.firstBank1 == 0200 && h.lastBank1 == 0203,
          "header fields wrong: start=0o%o first=0o%o last=0o%o", h.startAddress, h.firstBank1,
          h.lastBank1);
    CHECK(h.twoBank == false, "1-bank image must have twoBank == false");
    unlink(p);
}

static void test_two_bank_detect(void)
{
    printf("TEST: LoadPROG 2-bank detection (Bank 1 loaded, Bank 2 flagged)\n");
    memset(g_mem, 0, sizeof(g_mem));
    g_writes = 0;

    uint16_t data[2] = {0xAAAA, 0xBBBB};
    /* firstB2=0, lastB2=05 -> NOT the sentinel -> twoBank must be true. */
    const char *p = make_prog(026111, 0, 1, 0, 05, data, 2);
    CHECK(p != NULL, "could not create temp .prog");
    if (!p)
    {
        return;
    }

    int rc = LoadPROG(p, false);
    CHECK(rc == 026111, "start should be 0o26111, got 0o%o", rc);
    CHECK(g_mem[0] == 0xAAAA && g_mem[1] == 0xBBBB, "Bank 1 must still load");
    PROG_Header h = {0};
    CHECK(GetLastPROGHeader(&h) && h.twoBank == true, "twoBank must be detected");
    unlink(p);
}

static void test_truncated(void)
{
    printf("TEST: LoadPROG truncated image -> error\n");
    /* Header claims 4 words but only 1 is present after the 512-byte header. */
    uint16_t data[1] = {0x9999};
    const char *p = make_prog(0100, 0200, 0203, 0xFFFF, 0x0000, data, 1);
    CHECK(p != NULL, "temp");
    if (!p)
    {
        return;
    }
    int rc = LoadPROG(p, false);
    CHECK(rc == -1, "truncated data must return -1, got %d", rc);
    unlink(p);
}

static void test_missing(void)
{
    printf("TEST: LoadPROG missing file -> error\n");
    int rc = LoadPROG("/tmp/nd100x-nope-xyz.prog", false);
    CHECK(rc == -1, "missing file must return -1, got %d", rc);
}

int main(void)
{
    printf("\n========================================\n");
    printf("ND-100X :PROG Loader Unit Tests\n");
    printf("========================================\n\n");
    test_one_bank();
    test_two_bank_detect();
    test_truncated();
    test_missing();
    printf("\nChecks: %d   Failures: %d\nResult: %s\n\n", g_checks, g_fails,
           g_fails ? "FAIL" : "PASS");
    return g_fails ? 1 : 0;
}
