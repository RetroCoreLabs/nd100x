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
 * A record with no archive image must be skipped, and an oversized (> 1000-page) image must
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
char *dl_download_file(const char *url)
{
    (void)url;
    return NULL;
}
size_t dl_get_downloaded_size(void)
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

/* Two PACK-ONE entries (different md5 + size), one SMD-sized image, one record with
 * no storage.git.imagePath that must be excluded. Layout follows the real
 * catalog/floppies.json of the norskdata-software-archive repository (camelCase,
 * image size in bytes, 2048-byte pages). */
static const char *fixture =
    "[\n"
    "  {\n"
    "    \"md5\": \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\", \"volumeName\": \"PACK-ONE\",\n"
    "    \"productId\": \"ND-1\", \"version\": \"A\", \"filesystem\": \"ndfs\",\n"
    "    \"imageSizeBytes\": 315392, \"totalPages\": 154,\n"
    "    \"storage\": {\"git\": {\"imagePath\": \"images/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/ONE.img.gz\"}},\n"
    "    \"ndfs\": {\"users\": [{\"name\": \"FLOPPY-USER\", \"pagesUsed\": 5}],\n"
    "             \"files\": [{\"name\": \"HELLO:SYMB\", \"pages\": 5, \"bytes\": 9000,\n"
    "                        \"userName\": \"FLOPPY-USER\"}]}\n"
    "  },\n"
    "  {\n"
    "    \"md5\": \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\", \"volumeName\": \"PACK-ONE\",\n"
    "    \"filesystem\": \"ndfs\", \"imageSizeBytes\": 630784,\n"
    "    \"storage\": {\"git\": {\"imagePath\": \"images/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/Pack One & B.img.gz\"}}\n"
    "  },\n"
    "  {\n"
    "    \"md5\": \"cccccccccccccccccccccccccccccccc\", \"volumeName\": \"BIGVOL\",\n"
    "    \"imageSizeBytes\": 8388608,\n"
    "    \"storage\": {\"git\": {\"imagePath\": \"images/cccccccccccccccccccccccccccccccc/BIG.img.gz\"}}\n"
    "  },\n"
    "  {\n"
    "    \"md5\": \"dddddddddddddddddddddddddddddddd\", \"volumeName\": \"GONE\",\n"
    "    \"storage\": {\"git\": {\"imagePath\": null}}\n"
    "  }\n"
    "]\n";

int main(int argc, char **argv)
{
    printf("=== floppydb catalog API tests ===\n");

    int n = floppydb_load_json(fixture);
    printf("floppydb_load_json -> %d entries\n", n);
    CHECK(n == 3, "record without an archive image excluded (3 of 4 loaded)");
    CHECK(floppydb_count() == 3, "floppydb_count() == 3");

    /* md5 is unique: each hash resolves to exactly its entry. */
    const FloppyDbEntry *a = floppydb_find_md5("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    const FloppyDbEntry *b = floppydb_find_md5("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    const FloppyDbEntry *c = floppydb_find_md5("cccccccccccccccccccccccccccccccc");
    const FloppyDbEntry *gone = floppydb_find_md5("dddddddddddddddddddddddddddddddd");
    CHECK(a && strcmp(a->name, "PACK-ONE") == 0 && a->md5[0] == 'a', "find_md5(aaaa) -> rev A");
    CHECK(b && strcmp(b->name, "PACK-ONE") == 0 && b->md5[0] == 'b', "find_md5(bbbb) -> rev B");
    CHECK(c != NULL, "find_md5(cccc) -> found");
    CHECK(gone == NULL, "find_md5 of excluded (no imagePath) record -> NULL");
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

    /* Size is imageSizeBytes / 2048; > 1000 pages => SMD, else FLOPPY. */
    CHECK(a && a->filesystem_pages == 154, "rev A size is 154 pages");
    CHECK(a && !a->is_smd, "small image classified as FLOPPY (is_smd=false)");
    CHECK(c && c->filesystem_pages == 4096, "SMD size is 4096 pages");
    CHECK(c && c->is_smd, "large image classified as SMD (is_smd=true)");

    /* The generated listing carries the directory name and the files. */
    CHECK(a && a->directory_content && strstr(a->directory_content, "Directory name") &&
              strstr(a->directory_content, "HELLO:SYMB"),
          "listing holds the directory name and the file list");

    /* Image URL is the raw-file address of imagePath, percent-encoded. */
    char url[256];
    const char *u = a ? floppydb_image_url(a, url, sizeof(url)) : NULL;
    printf("image_url(rev A) -> %s\n", u ? u : "(null)");
    CHECK(u && strstr(u, "raw.githubusercontent.com/RetroCoreLabs/norskdata-software-archive/") &&
              strstr(u, "/images/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/ONE.img.gz") != NULL,
          "image_url is the archive raw URL of the .img.gz");
    u = b ? floppydb_image_url(b, url, sizeof(url)) : NULL;
    printf("image_url(rev B) -> %s\n", u ? u : "(null)");
    CHECK(u && strstr(u, "/Pack%20One%20%26%20B.img.gz") != NULL,
          "image_url percent-encodes spaces and ampersands");

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
