/*
 * gunzip.h - In-memory gzip (RFC 1952) decompression: API.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2025 Ronny Hansen
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2 or (at your option) any
 * later version. See COPYING.
 */
#ifndef GUNZIP_H
#define GUNZIP_H

#include <stddef.h>

/**
 * @brief Decompress one gzip member held in memory.
 * @details Self-contained DEFLATE decoder, so no zlib is needed on any build
 *          target. The gzip trailer is checked: the CRC-32 and the stored
 *          uncompressed size must both match the decoded data. The result is
 *          NUL-terminated one byte past the data, like dl_download_file(), so
 *          the caller can free() it the same way.
 * @param in      Compressed bytes (the whole .gz file).
 * @param in_len  Number of bytes at in.
 * @param out_len Receives the number of decompressed bytes, not counting the
 *                added NUL; set to 0 on failure. May be NULL.
 * @return Pointer to the decompressed data, or NULL if the input is not valid
 *         gzip, fails the trailer check, is larger than 500 MB uncompressed,
 *         or memory ran out.
 */
unsigned char *nd_gunzip(const unsigned char *in, size_t in_len, size_t *out_len);

#endif /* GUNZIP_H */
