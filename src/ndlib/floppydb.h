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

/*
 * Load the catalog: use the cached floppies.json when fresh, otherwise download
 * and cache it ($HOME/.cache/nd100x/floppies.json), then parse. force_refresh
 * bypasses the cache. Only records with Status == 0 are included. Idempotent -
 * frees any previous load first. Returns the entry count, or -1 on failure
 * (no data, e.g. offline AND no cache, or a build without libcurl and no cache).
 */
int floppydb_load(bool force_refresh);

/*
 * Parse a floppies.json text buffer directly into the entry list (no cache, no
 * network). Same result as floppydb_load() once the JSON is in hand - used by the
 * unit tests and by any caller that already has the catalog text. Returns the
 * entry count, or -1 on a parse error. Idempotent (frees any previous load).
 */
int floppydb_load_json(const char *json_text);

/* Number of loaded entries (0 before a successful load). */
int floppydb_count(void);

/* Entry by index [0, floppydb_count()), or NULL if out of range. */
const FloppyDbEntry *floppydb_get(int index);

/* Entry whose md5 matches (case-insensitive), or NULL. md5 is unique. */
const FloppyDbEntry *floppydb_find_md5(const char *md5);

/*
 * All entries whose SINTRAN "Directory name" equals directory_name
 * (case-insensitive). Fills out[] with up to max pointers and returns the TOTAL
 * number of matches (which may exceed max - the caller should log/disambiguate
 * when it is > 1, then pin a specific image by its md5).
 */
int floppydb_find_directory(const char *directory_name, const FloppyDbEntry **out, int max);

/* Build the image download URL (images/<md5>.img) into buf. Returns buf or NULL. */
const char *floppydb_image_url(const FloppyDbEntry *e, char *buf, size_t buflen);

/* Free the loaded catalog. */
void floppydb_free(void);

#endif /* FLOPPYDB_H */
