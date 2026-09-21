/*
 * test_floppydb.c - Unit tests for the floppy/disk catalog API using an embedded fixture.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * test_floppydb - unit tests for the floppy/disk catalog API (src/ndlib/floppydb.c).
 *
 * These tests parse an EMBEDDED floppies.json fixture (no network, no external
 * file), so they are deterministic and run on every build - including Windows
 * w64devkit, where the live HTTP download (libcurl) is unavailable but the cJSON
 * parser is bundled. The fixture deliberately mirrors the real catalog's two
 * identifier axes:
 *   - Md5            : unique per image.
 *   - "Directory name": the SINTRAN volume name inside DirectoryContent, which
 *                       REPEATS across several image versions (here PACK-ONE
 *                       appears twice with different md5 + sizes), so a
 *                       directory-name lookup must return >1 and the caller
 *                       disambiguates by md5.
 * A Status != 0 record must be skipped, and an oversized (> 1000-page) image must
 * classify as SMD rather than FLOPPY.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "floppydb.h"

/* floppydb.c references download_file()/get_downloaded_size() for its network path
 * (fdb_get_json). These tests drive floppydb_load_json() directly - no network - so
 * provide trivial stubs to satisfy the linker without pulling in download.c/libcurl. */
#include "../src/ndlib/download.h"
char *download_file(const char *url)
{
    (void)url;
    return NULL;
}
size_t get_downloaded_size(void)
{
    return 0;
}

static int g_failures = 0;

// clang-format off
#define CHECK(cond, msg) do {                                          \
    if (cond) { printf("  PASS: %s\n", (msg)); }                       \
    else      { printf("  FAIL: %s\n", (msg)); g_failures++; }         \
} while (0)
// clang-format on

/* Two PACK-ONE entries (different md5 + size), one SMD image, one Status=1 that
 * must be excluded. Filesystem image size is octal, as in the real catalog. */
static const char *fixture =
    "[\n"
    "  {\n"
    "    \"Id\": 1, \"Name\": \"Pack One rev A\", \"Status\": 0,\n"
    "    \"Md5\": \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\n"
    "    \"DirectoryContent\": \"Directory name            : PACK-ONE\\r\\nFilesystem image size   "
    "  : 000232 pages\\r\\n\"\n"
    "  },\n"
    "  {\n"
    "    \"Id\": 2, \"Name\": \"Pack One rev B\", \"Status\": 0,\n"
    "    \"Md5\": \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\n"
    "    \"DirectoryContent\": \"Directory name            : PACK-ONE\\r\\nFilesystem image size   "
    "  : 000464 pages\\r\\n\"\n"
    "  },\n"
    "  {\n"
    "    \"Id\": 3, \"Name\": \"Big SMD\", \"Status\": 0,\n"
    "    \"Md5\": \"cccccccccccccccccccccccccccccccc\",\n"
    "    \"DirectoryContent\": \"Directory name            : BIGVOL\\r\\nFilesystem image size     "
    ": 010000 pages\\r\\n\"\n"
    "  },\n"
    "  {\n"
    "    \"Id\": 4, \"Name\": \"Deleted entry\", \"Status\": 1,\n"
    "    \"Md5\": \"dddddddddddddddddddddddddddddddd\",\n"
    "    \"DirectoryContent\": \"Directory name            : GONE\\r\\n\"\n"
    "  }\n"
    "]\n";

int main(int argc, char **argv)
{
    printf("=== floppydb catalog API tests ===\n");

    int n = floppydb_load_json(fixture);
    printf("floppydb_load_json -> %d entries\n", n);
    CHECK(n == 3, "Status!=0 record excluded (3 of 4 loaded)");
    CHECK(floppydb_count() == 3, "floppydb_count() == 3");

    /* md5 is unique: each hash resolves to exactly its entry. */
    const FloppyDbEntry *a = floppydb_find_md5("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    const FloppyDbEntry *b = floppydb_find_md5("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    const FloppyDbEntry *c = floppydb_find_md5("cccccccccccccccccccccccccccccccc");
    const FloppyDbEntry *gone = floppydb_find_md5("dddddddddddddddddddddddddddddddd");
    CHECK(a && strcmp(a->name, "Pack One rev A") == 0, "find_md5(aaaa) -> rev A");
    CHECK(b && strcmp(b->name, "Pack One rev B") == 0, "find_md5(bbbb) -> rev B");
    CHECK(c != NULL, "find_md5(cccc) -> found");
    CHECK(gone == NULL, "find_md5 of excluded (Status=1) record -> NULL");
    CHECK(floppydb_find_md5("ffffffffffffffffffffffffffffffff") == NULL,
          "find_md5 of unknown -> NULL");

    /* md5 lookup is case-insensitive (catalog md5 may be upper- or lower-case). */
    CHECK(floppydb_find_md5("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA") == a,
          "find_md5 is case-insensitive");

    /* "Directory name" REPEATS: PACK-ONE must return both rev A and rev B. */
    const FloppyDbEntry *matches[8];
    int m = floppydb_find_directory("PACK-ONE", matches, 8);
    printf("find_directory(PACK-ONE) -> %d matches\n", m);
    CHECK(m == 2, "find_directory(PACK-ONE) -> 2 images (ambiguous, disambiguate by md5)");
    CHECK(m == 2 && matches[0] != matches[1], "the two PACK-ONE matches are distinct entries");

    /* Log the disambiguation set exactly as the mount path would. */
    if (m > 1)
    {
        printf("  [ambiguous] PACK-ONE resolves to %d images:\n", m);
        for (int i = 0; i < m; i++)
        {
            printf("    - dir='%s' size=%ld pages md5=%s\n", matches[i]->directory_name,
                   matches[i]->filesystem_pages, matches[i]->md5);
        }
    }

    int bign = floppydb_find_directory("BIGVOL", matches, 8);
    CHECK(bign == 1, "find_directory(BIGVOL) -> 1 match");
    CHECK(floppydb_find_directory("NOPE", matches, 8) == 0, "find_directory of unknown -> 0");

    /* Filesystem image size parsed as OCTAL; > 1000 pages => SMD, else FLOPPY. */
    CHECK(a && a->filesystem_pages == 0232 /*octal*/, "rev A size parsed as octal 000232");
    CHECK(a && !a->is_smd, "small image classified as FLOPPY (is_smd=false)");
    CHECK(c && c->filesystem_pages == 010000 /*octal*/, "SMD size parsed as octal 010000");
    CHECK(c && c->is_smd, "large image classified as SMD (is_smd=true)");

    /* Image URL is images/<md5>.img. */
    char url[256];
    const char *u = a ? floppydb_image_url(a, url, sizeof(url)) : NULL;
    printf("image_url(rev A) -> %s\n", u ? u : "(null)");
    CHECK(u && strstr(u, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.img") != NULL,
          "image_url contains <md5>.img");

    /* Optional smoke test against a real floppies.json if a path is provided. */
    const char *real = (argc > 1) ? argv[1] : getenv("FLOPPYDB_TEST_JSON");
    if (real && real[0])
    {
        FILE *f = fopen(real, "rb");
        if (f)
        {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *buf = (char *)malloc((size_t)sz + 1);
            if (buf)
            {
                size_t rd = fread(buf, 1, (size_t)sz, f);
                buf[rd] = '\0';
                int rn = floppydb_load_json(buf);
                printf("real catalog '%s' -> %d entries\n", real, rn);
                CHECK(rn > 0, "real floppies.json parses to > 0 entries");
                free(buf);
            }
            fclose(f);
        }
        else
        {
            printf("  (skip real-catalog smoke test: cannot open %s)\n", real);
        }
    }

    floppydb_free();

    printf("=== %s (%d failure%s) ===\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures,
           g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
