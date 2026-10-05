/*
 * floppydb.c - UI-independent access to the online floppy/disk catalog.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * floppydb - UI-independent floppy/disk catalog access, refactored out of the
 * F12 "Floppy Database Browser" (menu.c) so the catalog can be searched and
 * mounted from automation as well as the ncurses UI.
 *
 * PARSING uses the bundled cJSON, so it builds on EVERY target (incl. Windows).
 * The network fetch of floppies.json uses download_file(), which is real only on
 * libcurl builds; without libcurl it is a stub, so floppydb falls back to the
 * on-disk cache. Callers that need the actual image on a no-curl build download
 * it out of band (e.g. the Python driver) and mount the local file.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2 or (at your option) any
 * later version. See COPYING.
 */
#include "floppydb.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <strings.h> /* strcasecmp / strncasecmp */
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define FDB_MKDIR(p) _mkdir(p)
#else
#include <sys/types.h>
#include <errno.h>
#define FDB_MKDIR(p) mkdir((p), 0755)
#endif

#include "cJSON.h" /* bundled external/cJSON - on every build (see top CMakeLists) */

#include "log.h"
#include "nd_format.h"
#include "download.h"

/* ---- catalog endpoints (same as the F12 browser) ------------------------ */
/* Raw-file address of the RetroCoreLabs/norskdata-software-archive repository.
 * The catalog is <base>catalog/floppies.json and every image is
 * <base>images/<md5>/<file>.img.gz (the path comes from the catalog record). */
#define FDB_ARCHIVE_BASE \
    "https://raw.githubusercontent.com/RetroCoreLabs/norskdata-software-archive/main/"
#define FDB_JSON_URL      FDB_ARCHIVE_BASE "catalog/floppies.json"
#define FDB_CACHE_SUFFIX  "/.cache/nd100x"
/* Not "floppies.json": the old ndlib.hackercorp.no catalog was cached under that
 * name in another layout and must never be read as the new one. */
#define FDB_CACHE_NAME    "floppies-archive.json"
#define FDB_CACHE_MAX_AGE (24L * 60 * 60) /* refresh the cache once a day */

/* ---- module state ------------------------------------------------------- */
static FloppyDbEntry *s_entries = NULL;
static int s_count = 0;

/* ---- small helpers ------------------------------------------------------ */

/* $HOME (or %USERPROFILE% on Windows) so the cache path resolves on both. */
static const char *fdb_home(void)
{
    const char *h = getenv("HOME");
    if (h && *h)
    {
        return h;
    }
#ifdef _WIN32
    h = getenv("USERPROFILE");
    if (h && *h)
    {
        return h;
    }
#endif
    return NULL;
}

static char *fdb_cache_path(void)
{
    const char *home = fdb_home();
    if (!home)
    {
        return NULL;
    }
    size_t len = strlen(home) + strlen(FDB_CACHE_SUFFIX) + 1 + strlen(FDB_CACHE_NAME) + 1;
    char *p = (char *)malloc(len);
    if (!p)
    {
        return NULL;
    }
    snprintf(p, len, "%s%s/%s", home, FDB_CACHE_SUFFIX, FDB_CACHE_NAME);
    return p;
}

static void fdb_ensure_cache_dir(void)
{
    const char *home = fdb_home();
    if (!home)
    {
        return;
    }
    char d[600];
    /* A cut-off path would create a directory other than the one meant. */
    if (ND_PATH(d, "%s/.cache", home))
    {
        FDB_MKDIR(d);
    }
    if (ND_PATH(d, "%s%s", home, FDB_CACHE_SUFFIX))
    {
        FDB_MKDIR(d);
    }
}

static char *fdb_read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0)
    {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

static long fdb_file_age(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
    {
        return -1;
    }
    return (long)(time(NULL) - st.st_mtime);
}

/* ---- catalog record conversion ------------------------------------------ */

/* Longest listing line of the generated directory text; longer text is cut. */
#define FDB_LINE_MAX   512
#define FDB_PAGE_BYTES 2048L
/* An image of more than this many 2048-byte pages is an SMD disk, not a floppy. */
#define FDB_SMD_PAGES  1000L

typedef struct
{
    char *data;
    size_t len;
    size_t cap;
    bool failed;
} FdbText;

/* Append printf-style text to a growing buffer. Sets t->failed on no memory. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static void fdb_text_add(FdbText *t, const char *fmt, ...)
{
    char line[FDB_LINE_MAX];
    va_list ap;

    if (t->failed)
    {
        return;
    }
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0)
    {
        return;
    }
    size_t add = ((size_t)n < sizeof(line)) ? (size_t)n : sizeof(line) - 1;
    if (t->len + add + 2 > t->cap)
    {
        size_t cap = (t->cap ? t->cap : 1024);
        while (t->len + add + 2 > cap)
        {
            cap *= 2;
        }
        char *grown = (char *)realloc(t->data, cap);
        if (!grown)
        {
            t->failed = true;
            return;
        }
        t->data = grown;
        t->cap = cap;
    }
    memcpy(t->data + t->len, line, add);
    t->len += add;
    t->data[t->len++] = '\n';
    t->data[t->len] = '\0';
}

/* String value of a JSON member, or NULL if missing, null or not a string. */
static const char *fdb_str(const cJSON *obj, const char *key)
{
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(obj, key);
    return (cJSON_IsString(m) && m->valuestring) ? m->valuestring : NULL;
}

/* Number value of a JSON member, or def if missing, null or not a number. */
static double fdb_num(const cJSON *obj, const char *key, double def)
{
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(m) ? m->valuedouble : def;
}

/* Directory listing text from the record's ndfs / dosFiles / backupFiles /
 * backupSet data. Plain lines, one per file, in the order of the catalog. */
static void fdb_build_listing(const cJSON *item, FdbText *t)
{
    const char *fs = fdb_str(item, "filesystem");
    const char *vol = fdb_str(item, "volumeName");
    long pages = (long)fdb_num(item, "totalPages", 0);

    if (vol)
    {
        fdb_text_add(t, "Directory name            : %s", vol);
    }
    if (pages > 0)
    {
        fdb_text_add(t, "Filesystem image size     : %06lo pages", (unsigned long)pages);
    }
    fdb_text_add(t, "Filesystem                : %s", fs ? fs : "unknown");
    const char *boot = fdb_str(item, "bootFormat");
    const char *prog = fdb_str(item, "bootProgram");
    if (boot && strcmp(boot, "none") != 0)
    {
        fdb_text_add(t, "Boot                      : %s %s", boot, prog ? prog : "");
    }

    const cJSON *ndfs = cJSON_GetObjectItemCaseSensitive(item, "ndfs");
    const cJSON *users = cJSON_GetObjectItemCaseSensitive(ndfs, "users");
    const cJSON *files = cJSON_GetObjectItemCaseSensitive(ndfs, "files");
    const cJSON *user = NULL;
    cJSON_ArrayForEach(user, users)
    {
        const char *uname = fdb_str(user, "name");
        fdb_text_add(t, "---- User %s (%ld pages)", uname ? uname : "?",
                     (long)fdb_num(user, "pagesUsed", 0));
        const cJSON *f = NULL;
        cJSON_ArrayForEach(f, files)
        {
            const char *fuser = fdb_str(f, "userName");
            if (uname && fuser && strcmp(uname, fuser) != 0)
            {
                continue;
            }
            const char *fname = fdb_str(f, "name");
            const char *when = fdb_str(f, "dateCreatedStr");
            fdb_text_add(t, "  %-24s %5ld pages %9ld bytes  %s", fname ? fname : "?",
                         (long)fdb_num(f, "pages", 0), (long)fdb_num(f, "bytes", 0),
                         when ? when : "");
        }
    }

    const cJSON *dos = cJSON_GetObjectItemCaseSensitive(item, "dosFiles");
    const cJSON *df = NULL;
    cJSON_ArrayForEach(df, dos)
    {
        const char *path = fdb_str(df, "path");
        const char *when = fdb_str(df, "modified");
        fdb_text_add(t, "  %-32s %9ld bytes  %s", path ? path : "?", (long)fdb_num(df, "bytes", 0),
                     when ? when : "");
    }

    const cJSON *set = cJSON_GetObjectItemCaseSensitive(item, "backupSet");
    if (cJSON_IsObject(set))
    {
        const char *sname = fdb_str(set, "name");
        const char *slabel = fdb_str(set, "label");
        const char *skind = fdb_str(set, "kind");
        fdb_text_add(t, "---- %s set %s  label %s", skind ? skind : "backup", sname ? sname : "?",
                     slabel ? slabel : "?");
    }
    const cJSON *backup = cJSON_GetObjectItemCaseSensitive(item, "backupFiles");
    const cJSON *bf = NULL;
    cJSON_ArrayForEach(bf, backup)
    {
        const char *fname = fdb_str(bf, "name");
        const char *when = fdb_str(bf, "created");
        fdb_text_add(t, "  %-32s %9ld bytes  %s", fname ? fname : "?", (long)fdb_num(bf, "bytes", 0),
                     when ? when : "");
    }
}

/* Last path component of the image path, without ".img.gz". */
static void fdb_image_stem(const char *image_path, char *out, size_t out_len)
{
    const char *slash = strrchr(image_path, '/');
    const char *base = slash ? slash + 1 : image_path;
    size_t n = strlen(base);
    if (n > 7 && strcmp(base + n - 7, ".img.gz") == 0)
    {
        n -= 7;
    }
    if (n >= out_len)
    {
        n = out_len - 1;
    }
    memcpy(out, base, n);
    out[n] = '\0';
}

bool floppydb_parse_record(const struct cJSON *item, FloppyDbEntry *out)
{
    if (!out || !cJSON_IsObject(item))
    {
        return false;
    }
    memset(out, 0, sizeof(*out));

    const char *md5 = fdb_str(item, "md5");
    const cJSON *storage = cJSON_GetObjectItemCaseSensitive(item, "storage");
    const cJSON *git = cJSON_GetObjectItemCaseSensitive(storage, "git");
    const char *image_path = fdb_str(git, "imagePath");
    if (!md5 || !*md5 || !image_path || !*image_path)
    {
        return false; /* no image in the archive to mount */
    }

    snprintf(out->md5, sizeof(out->md5), "%s", md5);
    snprintf(out->image_path, sizeof(out->image_path), "%s", image_path);

    const char *vol = fdb_str(item, "volumeName");
    const cJSON *set = cJSON_GetObjectItemCaseSensitive(item, "backupSet");
    const char *set_name = fdb_str(set, "name");
    char stem[128];
    fdb_image_stem(image_path, stem, sizeof(stem));
    snprintf(out->name, sizeof(out->name), "%s", vol ? vol : (set_name ? set_name : stem));
    snprintf(out->directory_name, sizeof(out->directory_name), "%s", vol ? vol : "");

    const char *product = fdb_str(item, "productId");
    snprintf(out->reference, sizeof(out->reference), "%s", product ? product : "");

    /* Description: "<product> <version> disk n of m - <image file> - from <who>" */
    const char *version = fdb_str(item, "version");
    const cJSON *prov = cJSON_GetObjectItemCaseSensitive(item, "provenance");
    const char *who = fdb_str(prov, "contributor");
    char disk[48] = "";
    double dn = fdb_num(item, "diskNumber", 0);
    double dt = fdb_num(item, "diskTotal", 0);
    if (dn > 0)
    {
        snprintf(disk, sizeof(disk), " disk %d of %d", (int)dn, (int)dt);
    }
    snprintf(out->description, sizeof(out->description), "%s%s%s%s - %s%s%s", product ? product : "",
             version ? " " : "", version ? version : "", disk, stem, who ? " - from " : "",
             who ? who : "");

    double bytes = fdb_num(item, "imageSizeBytes", 0);
    out->filesystem_pages = (long)(bytes / (double)FDB_PAGE_BYTES);
    out->is_smd = (out->filesystem_pages > FDB_SMD_PAGES);

    FdbText t = {NULL, 0, 0, false};
    fdb_build_listing(item, &t);
    if (t.failed)
    {
        free(t.data);
        return false;
    }
    out->directory_content = t.data ? t.data : strdup("");
    return out->directory_content != NULL;
}

/* ---- catalog fetch ------------------------------------------------------ */

/* Get floppies.json: fresh cache -> use it; else download (libcurl builds) and
 * cache; else fall back to a stale cache. Returns malloc'd text or NULL. */
static char *fdb_get_json(bool force_refresh)
{
    char *path = fdb_cache_path();

    if (!force_refresh && path)
    {
        long age = fdb_file_age(path);
        if (age >= 0 && age < FDB_CACHE_MAX_AGE)
        {
            char *cached = fdb_read_file(path);
            if (cached)
            {
                free(path);
                return cached;
            }
        }
    }

    char *net = dl_download_file(FDB_JSON_URL); /* stub (NULL) on no-curl builds */
    if (net)
    {
        if (path)
        {
            fdb_ensure_cache_dir();
            FILE *f = fopen(path, "wb");
            if (f)
            {
                /* Cache write only: a failure costs one refetch next time,
                 * so it is logged and the fetched data still returned. */
                bool lost = (fputs(net, f) == EOF);
                if (fclose(f) != 0)
                {
                    lost = true;
                }
                if (lost)
                {
                    LOG(LOG_CAT_GENERAL, LOG_WARN, "could not cache the floppy database to %s",
                        path);
                }
            }
        }
        free(path);
        return net;
    }

    /* offline / no libcurl: use whatever cache we have, however old. */
    if (path)
    {
        char *stale = fdb_read_file(path);
        free(path);
        return stale;
    }
    return NULL;
}

/* ---- public API --------------------------------------------------------- */

void floppydb_free(void)
{
    if (s_entries)
    {
        for (int i = 0; i < s_count; i++)
        {
            free(s_entries[i].directory_content);
        }
        free(s_entries);
        s_entries = NULL;
    }
    s_count = 0;
}

/* Parse a floppies.json text buffer into the entry list. Returns count or -1. */
int floppydb_load_json(const char *json_text)
{
    floppydb_free();
    if (!json_text)
    {
        return -1;
    }

    cJSON *root = cJSON_Parse(json_text);
    if (!root)
    {
        return -1;
    }
    if (!cJSON_IsArray(root))
    {
        cJSON_Delete(root);
        return -1;
    }

    int n = cJSON_GetArraySize(root);
    s_entries = (FloppyDbEntry *)calloc((size_t)(n > 0 ? n : 1), sizeof(FloppyDbEntry));
    if (!s_entries)
    {
        cJSON_Delete(root);
        return -1;
    }

    for (int i = 0; i < n; i++)
    {
        cJSON *item = cJSON_GetArrayItem(root, i);
        if (!cJSON_IsObject(item))
        {
            continue;
        }

        FloppyDbEntry *e = &s_entries[s_count];
        if (!floppydb_parse_record(item, e))
        {
            continue; /* no image to mount, or out of memory */
        }
        e->id = s_count + 1;

        s_count++;
    }

    cJSON_Delete(root);
    return s_count;
}

int floppydb_load(bool force_refresh)
{
    char *json = fdb_get_json(force_refresh);
    if (!json)
    {
        return -1;
    }
    int rc = floppydb_load_json(json);
    free(json);
    return rc;
}

int floppydb_count(void)
{
    return s_count;
}

/* Currently unused: nothing in this tree calls it. Kept deliberately. */
const FloppyDbEntry *floppydb_get(int index)
{
    if (index < 0 || index >= s_count)
    {
        return NULL;
    }
    return &s_entries[index];
}

const FloppyDbEntry *floppydb_find_md5(const char *md5)
{
    if (!md5)
    {
        return NULL;
    }
    for (int i = 0; i < s_count; i++)
    {
        if (strcasecmp(s_entries[i].md5, md5) == 0)
        {
            return &s_entries[i];
        }
    }
    return NULL;
}

int floppydb_find_directory(const char *directory_name, const FloppyDbEntry **out, int max)
{
    if (!directory_name)
    {
        return 0;
    }
    int matches = 0;
    for (int i = 0; i < s_count; i++)
    {
        if (strcasecmp(s_entries[i].directory_name, directory_name) == 0)
        {
            if (out && matches < max)
            {
                out[matches] = &s_entries[i];
            }
            matches++;
        }
    }
    return matches;
}

const char *floppydb_image_url(const FloppyDbEntry *e, char *buf, size_t buflen)
{
    static const char hex[] = "0123456789ABCDEF";

    if (!e || !buf || buflen == 0)
    {
        return NULL;
    }
    size_t pos = 0;
    for (const char *c = FDB_ARCHIVE_BASE; *c && pos + 1 < buflen; c++)
    {
        buf[pos++] = *c;
    }
    for (const unsigned char *c = (const unsigned char *)e->image_path; *c; c++)
    {
        bool plain = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
                     (*c >= '0' && *c <= '9') || *c == '-' || *c == '_' || *c == '.' ||
                     *c == '/' || *c == '~';
        if (plain)
        {
            if (pos + 1 >= buflen)
            {
                break;
            }
            buf[pos++] = (char)*c;
        }
        else
        {
            if (pos + 3 >= buflen)
            {
                break;
            }
            buf[pos++] = '%';
            buf[pos++] = hex[*c >> 4];
            buf[pos++] = hex[*c & 15];
        }
    }
    buf[pos] = '\0';
    return buf;
}
