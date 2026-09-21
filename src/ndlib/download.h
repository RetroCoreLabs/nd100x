/*
 * download.h - File download API for JSON and binary files.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * This file is originated from the nd100x project and the RetroCore project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the nd100em
 * distribution in the file COPYING); if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DOWNLOAD_H
#define DOWNLOAD_H

#include <stddef.h>

/**
 * @brief Fetch a URL over HTTP/HTTPS with libcurl into one heap buffer.
 * @details Follows redirects, times out after 30 seconds, rejects a body
 *          larger than 500 MB and requires HTTP status 200. The buffer is
 *          NUL-terminated one byte past the body, so it also works with
 *          string functions for JSON. The caller frees it with free().
 *          Builds without libcurl (WebAssembly, RISC-V, HAVE_CURL unset)
 *          link a stub that always fails.
 * @param url Absolute URL to fetch; NULL is an error.
 * @return Pointer to the NUL-terminated body on success, NULL on any error.
 */
char *dl_download_file(const char *url);

/**
 * @brief Body size in bytes of the last successful download_file() call.
 * @details Set by download_file() on success and cleared to 0 on failure.
 *          Use this for binary data, where the NUL terminator makes strlen()
 *          useless. The stub build always returns 0.
 * @return Number of body bytes, not counting the added NUL terminator.
 */
size_t dl_get_downloaded_size(void);

#endif // DOWNLOAD_H
