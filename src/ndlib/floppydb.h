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
 * (catalog/floppies.json of the RetroCoreLabs/norskdata-software-archive
 * GitHub repository). Refactored out of the F12
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
 *   - md5            : globally UNIQUE (the image lives in images/<md5>/ of the archive).
 *   - directory_name : the SINTRAN volume name pulled from DirectoryContent's
 *                      "Directory name : X" line - human-friendly but MAY REPEAT
 *                      (several image versions of the same directory), so a
 *                      directory-name lookup can return several entries.
 */
typedef struct
{
    int id;
    char name[256];           /* display name: volumeName, else the image file   */
    char description[1024];   /* product, version, disk n of m, boot, source     */
    char reference[256];      /* JSON "productId" (may be "")                    */
    char md5[33];             /* JSON "md5" - UNIQUE key                         */
    char image_path[512];     /* JSON "storage.git.imagePath": images/<md5>/X.img.gz */
    char directory_name[128]; /* JSON "volumeName" (SINTRAN directory name)      */
    long filesystem_pages;    /* image size in 2048-byte pages, 0 if unknown     */
    bool is_smd;              /* true => SMD image (> 1000 pages), else a floppy  */
    char *directory_content;  /* generated listing, owned by the caller of
                                 floppydb_parse_record() (may be "")             */
} FloppyDbEntry;

struct cJSON;

/**
 * @brief Convert one record of catalog/floppies.json into a FloppyDbEntry.
 * @details Shared by the F12 menu and by floppydb_load_json(). The listing in
 *          out->directory_content is generated from the record's ndfs, dosFiles
 *          or backupFiles data and is allocated with malloc().
 * @param item JSON object, one element of the catalog array.
 * @param out  Receives the entry. On success the caller owns
 *             out->directory_content and must free() it.
 * @return true if the record was converted, false if item is not an object,
 *         has no md5 or no storage.git.imagePath (no image to mount), or
 *         memory ran out.
 */
bool floppydb_parse_record(const struct cJSON *item, FloppyDbEntry *out);

/**
 * @brief Load the catalog: use the cached floppies.json when fresh, otherwise
 *        download and cache it ($HOME/.cache/nd100x/floppies.json), then parse.
 * @details Only records that name an image in the archive are included. Idempotent - any
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
 * @brief Build the download URL of the compressed image (.img.gz) for an entry.
 * @details The URL is the raw-file address in the archive repository followed
 *          by the entry's image_path, with characters outside A-Z a-z 0-9
 *          - _ . / ~ percent-encoded (several file names hold spaces).
 *          machine_mount_drive() decompresses a URL ending in .gz.
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
