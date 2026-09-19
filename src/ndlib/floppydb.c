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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp / strncasecmp */
#include <time.h>
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

#include "floppydb.h"
#include "download.h"

/* ---- catalog endpoints (same as the F12 browser) ------------------------ */
#define FDB_JSON_URL      "https://ndlib.hackercorp.no/floppies.json"
#define FDB_IMAGES_BASE   "https://ndlib.hackercorp.no/images/"
#define FDB_CACHE_SUFFIX  "/.cache/nd100x"
#define FDB_CACHE_NAME    "floppies.json"
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
    snprintf(d, sizeof(d), "%s/.cache", home);
    FDB_MKDIR(d);
    snprintf(d, sizeof(d), "%s%s", home, FDB_CACHE_SUFFIX);
    FDB_MKDIR(d);
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

/* Extract the value after "<label> :" on its own line in the SINTRAN directory
 * listing (e.g. "Directory name : X", "Filesystem image size : 000232 pages").
 * Writes up to out_len-1 chars to out; returns true if found. */
static bool fdb_extract_field(const char *content, const char *label, char *out, size_t out_len)
{
    if (!content || !label || !out || out_len == 0)
    {
        return false;
    }
    out[0] = '\0';
    const char *p = strstr(content, label);
    if (!p)
    {
        return false;
    }
    p = strchr(p, ':');
    if (!p)
    {
        return false;
    }
    p++; /* past the colon */
    while (*p == ' ' || *p == '\t')
    {
        p++; /* skip leading ws */
    }
    size_t i = 0;
    while (*p && *p != '\r' && *p != '\n' && i < out_len - 1)
    {
        out[i++] = *p++;
    }
    /* trim trailing whitespace */
    while (i > 0 && (out[i - 1] == ' ' || out[i - 1] == '\t'))
    {
        i--;
    }
    out[i] = '\0';
    return i > 0;
}

/* "Filesystem image size : NNN pages" (octal) -> page count, or -1. */
static long fdb_parse_pages(const char *content)
{
    char buf[64];
    if (!fdb_extract_field(content, "Filesystem image size", buf, sizeof(buf)))
    {
        return -1;
    }
    /* buf is like "000232 pages"; stop at the space before "pages". */
    return strtol(buf, NULL, 8);
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

    char *net = download_file(FDB_JSON_URL); /* stub (NULL) on no-curl builds */
    if (net)
    {
        if (path)
        {
            fdb_ensure_cache_dir();
            FILE *f = fopen(path, "wb");
            if (f)
            {
                fputs(net, f);
                fclose(f);
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

        cJSON *status = cJSON_GetObjectItem(item, "Status");
        if (status && cJSON_IsNumber(status) && status->valueint != 0)
        {
            continue; /* Status 0 only */
        }

        FloppyDbEntry *e = &s_entries[s_count];

        cJSON *id = cJSON_GetObjectItem(item, "Id");
        cJSON *name = cJSON_GetObjectItem(item, "Name");
        cJSON *md5 = cJSON_GetObjectItem(item, "Md5");
        cJSON *dir = cJSON_GetObjectItem(item, "DirectoryContent");

        e->id = (id && cJSON_IsNumber(id)) ? id->valueint : 0;

        const char *name_s =
            (name && cJSON_IsString(name) && name->valuestring) ? name->valuestring : "";
        const char *md5_s =
            (md5 && cJSON_IsString(md5) && md5->valuestring) ? md5->valuestring : "";
        const char *dir_s =
            (dir && cJSON_IsString(dir) && dir->valuestring) ? dir->valuestring : "";

        snprintf(e->name, sizeof(e->name), "%s", name_s);
        snprintf(e->md5, sizeof(e->md5), "%s", md5_s);
        e->directory_content = strdup(dir_s ? dir_s : "");

        fdb_extract_field(dir_s, "Directory name", e->directory_name, sizeof(e->directory_name));
        e->filesystem_pages = fdb_parse_pages(dir_s);
        /* > 1000 pages => an SMD image, else a floppy (same heuristic as the F12 browser). */
        e->is_smd = (e->filesystem_pages > 1000);

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
    if (!e || !buf || buflen == 0)
    {
        return NULL;
    }
    snprintf(buf, buflen, "%s%s.img", FDB_IMAGES_BASE, e->md5);
    return buf;
}
