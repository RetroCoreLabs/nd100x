/*
 * floppydb.h - UI-independent access to the online floppy/disk catalog: API.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * floppydb - UI-independent access to the online floppy/disk catalog
 * (https://ndlib.hackercorp.no/floppies.json). Refactored out of the F12
 * "Floppy Database Browser" (menu.c) so the catalog can be searched and mounted
 * from automation (the --pipe control channel) as well as the ncurses UI.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2 or (at your option) any
 * later version. See COPYING.
 */
#ifndef FLOPPYDB_H
#define FLOPPYDB_H

#include <stdbool.h>
#include <stddef.h>

/*
 * NOTE: floppydb is part of ndlib and deliberately does NOT include the machine /
 * cpu headers (that would create an ndlib <-> cpu build-dependency cycle). The
 * drive kind is exposed as a plain `is_smd` flag; the mount caller in machine.c
 * maps it to DRIVE_SMD / DRIVE_FLOPPY.
 *
 * One catalog entry. Two identifiers, mirroring the F12 browser and the ndlib
 * catalog:
 *   - md5            : globally UNIQUE (the image is fetched as images/<md5>.img).
 *   - directory_name : the SINTRAN volume name pulled from DirectoryContent's
 *                      "Directory name : X" line - human-friendly but MAY REPEAT
 *                      (several image versions of the same directory), so a
 *                      directory-name lookup can return several entries.
 */
typedef struct
{
    int id;
    char name[256];           /* JSON "Name" (catalog display name)              */
    char md5[33];             /* JSON "Md5" - UNIQUE key / image filename        */
    char directory_name[128]; /* "Directory name" from DirectoryContent          */
    long filesystem_pages;    /* "Filesystem image size : N pages" (octal)       */
    bool is_smd;              /* true => SMD image (> 1000 pages), else a floppy  */
    char *directory_content;  /* full listing, owned by floppydb (may be "")     */
} FloppyDbEntry;

/**
 * @brief Load the catalog: use the cached floppies.json when fresh, otherwise
 *        download and cache it ($HOME/.cache/nd100x/floppies.json), then parse.
 * @details Only records with Status == 0 are included. Idempotent - any
 *          previous load is freed first.
 * @param force_refresh true bypasses the cache and downloads a fresh copy.
 * @return Entry count on success, or -1 on failure (no data, for example
 *         offline with no cache, or a build without libcurl and no cache).
 */
int floppydb_load(bool force_refresh);

/**
 * @brief Parse a floppies.json text buffer directly into the entry list, with
 *        no cache and no network access.
 * @details Same result as floppydb_load() once the JSON is in hand - used by
 *          the unit tests and by any caller that already has the catalog text.
 *          Idempotent - any previous load is freed first.
 * @param json_text The catalog JSON as a NUL-terminated string.
 * @return Entry count on success, or -1 if json_text is NULL, is not valid
 *         JSON, is not an array, or the entry array could not be allocated.
 */
int floppydb_load_json(const char *json_text);

/**
 * @brief Number of loaded catalog entries.
 * @return Entry count, 0 before a successful load.
 */
int floppydb_count(void);

/**
 * @brief Catalog entry by position.
 * @param index Index in the range [0, floppydb_count()).
 * @return Pointer to the entry, owned by floppydb, or NULL if index is out
 *         of range.
 */
const FloppyDbEntry *floppydb_get(int index);

/**
 * @brief Find the single catalog entry with this md5, which is unique.
 * @param md5 The 32-character md5 string; compared case-insensitively.
 * @return Pointer to the entry, owned by floppydb, or NULL if md5 is NULL or
 *         no entry matches.
 */
const FloppyDbEntry *floppydb_find_md5(const char *md5);

/**
 * @brief Find every entry whose SINTRAN "Directory name" equals
 *        directory_name, compared case-insensitively.
 * @param directory_name The volume name to look for.
 * @param out            Receives up to max entry pointers; may be NULL.
 * @param max            Capacity of out[].
 * @return TOTAL number of matches, which may exceed max. When it is greater
 *         than 1 the caller should disambiguate and pin one image by its md5.
 *         0 when directory_name is NULL or nothing matches.
 */
int floppydb_find_directory(const char *directory_name, const FloppyDbEntry **out, int max);

/**
 * @brief Build the image download URL (<base>/<md5>.img) for an entry.
 * @param e      Catalog entry.
 * @param buf    Destination buffer; the URL is truncated to fit.
 * @param buflen Size of buf in bytes.
 * @return buf on success, or NULL if e or buf is NULL or buflen is 0.
 */
const char *floppydb_image_url(const FloppyDbEntry *e, char *buf, size_t buflen);

/**
 * @brief Free the loaded catalog, including each entry's directory listing,
 *        and set the entry count back to 0.
 */
void floppydb_free(void);

#endif /* FLOPPYDB_H */
