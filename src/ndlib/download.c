/*
 * nd100x - ND100 Virtual Machine
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "download.h"
#include "log.h"

#if defined(PLATFORM_WASM) || defined(__EMSCRIPTEN__) || defined(PLATFORM_RISCV) || !defined(HAVE_CURL)

// No libcurl on this platform. Provide stubs so machine.c / menu.c still link.
// Triggered on WASM, RISC-V, and any build where CMake did not find CURL
// (e.g. w64devkit without libcurl).

// Get the actual size of downloaded data (not supported in WASM)
size_t get_downloaded_size(void) {
	return 0;
}

// Unified download function stub for WASM build
char* download_file(const char* url) {
	(void)url;
	LOG(LOG_CAT_NET, LOG_WARN, "Download not supported in WASM build (CURL disabled)\n");
	return NULL;
}

#else

#include <curl/curl.h>

#ifdef _WIN32
#include <windows.h>
// Resolve the CA bundle for TLS verification on Windows. The vendored curl-for-win
// build verifies HTTPS against a CA *file* (there is no Windows-cert-store default),
// so we must hand libcurl one or every https:// fetch fails with a verification
// error. Preference order:
//   1. curl-ca-bundle.crt staged next to the .exe (build.bat / Makefile copy it there).
//   2. ND100X_VENDORED_CA_BUNDLE - the external/curl copy, baked in at compile time.
// If neither exists we set nothing and let libcurl fall back to its own default /
// the CURL_CA_BUNDLE env var. We NEVER disable peer verification.
static const char *win_ca_bundle(void)
{
    static char path[MAX_PATH];
    static int  resolved = 0;
    if (resolved) return path[0] ? path : NULL;
    resolved = 1;

    // (1) exe-relative curl-ca-bundle.crt
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe));
    if (n > 0 && n < sizeof(exe)) {
        char *slash = strrchr(exe, '\\');
        if (slash) {
            size_t dirlen = (size_t)(slash - exe) + 1;      // keep the trailing backslash
            const char *leaf = "curl-ca-bundle.crt";
            if (dirlen + strlen(leaf) < sizeof(path)) {
                memcpy(path, exe, dirlen);
                snprintf(path + dirlen, sizeof(path) - dirlen, "%s", leaf);
                FILE *f = fopen(path, "rb");
                if (f) { fclose(f); return path; }
            }
        }
    }

#ifdef ND100X_VENDORED_CA_BUNDLE
    // (2) compile-time vendored path (external/curl/bin/curl-ca-bundle.crt)
    snprintf(path, sizeof(path), "%s", ND100X_VENDORED_CA_BUNDLE);
    FILE *f2 = fopen(path, "rb");
    if (f2) { fclose(f2); return path; }
#endif

    path[0] = '\0';
    return NULL;
}
#endif // _WIN32

// Global variable to track the actual downloaded size
static size_t g_downloaded_size = 0;

// Structure to hold download data and size
typedef struct {
    char *data;
    size_t size;
} DownloadData;

// Callback function for CURL to write received data
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    DownloadData *download_data = (DownloadData *)userp;

    // Calculate new size for realloc
    size_t new_size = download_data->size + realsize;

    // Check for integer overflow
    if (new_size < download_data->size) {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: Integer overflow in realloc size calculation\n");
        return 0;
    }

    // Check for maximum file size (500MB as requested)
    if (new_size > 500 * 1024 * 1024) {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: File size exceeds 500MB limit\n");
        return 0;
    }

    // Reallocate memory
    char *new_data = realloc(download_data->data, new_size);
    if (!new_data) {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: Failed to reallocate memory for download response\n");
        return 0;
    }

    download_data->data = new_data;

    // Copy new data
    memcpy(download_data->data + download_data->size, contents, realsize);
    download_data->size = new_size;

    return realsize;
}

// Get the actual size of downloaded data
size_t get_downloaded_size(void) {
    return g_downloaded_size;
}

// Unified download function that can handle both JSON and binary files
char* download_file(const char* url) {
    if (!url) {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: NULL URL provided to download_file\n");
        return NULL;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: Failed to initialize CURL\n");
        return NULL;
    }

    DownloadData download_data = {NULL, 0};

    // Set up CURL options
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &download_data);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "nd100x/1.0");

#ifdef _WIN32
    // Give the vendored (CA-file-verifying) libcurl a CA bundle for HTTPS. On
    // Linux/macOS the system CA store is used automatically, so this is Windows-only.
    {
        const char *ca = win_ca_bundle();
        if (ca) curl_easy_setopt(curl, CURLOPT_CAINFO, ca);
    }
#endif

    // Perform the request
    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: CURL request failed: %s\n", curl_easy_strerror(res));
        if (download_data.data) {
            free(download_data.data);
        }
        curl_easy_cleanup(curl);
        g_downloaded_size = 0;
        return NULL;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (http_code != 200) {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: HTTP request failed with code %ld\n", http_code);
        if (download_data.data) {
            free(download_data.data);
        }
        curl_easy_cleanup(curl);
        g_downloaded_size = 0;
        return NULL;
    }

    if (download_data.data && download_data.size > 0) {

        // Store the actual size globally
        g_downloaded_size = download_data.size;

        // Add null terminator for compatibility with string functions
        char *final_data = realloc(download_data.data, download_data.size + 1);
        if (!final_data) {
            LOG(LOG_CAT_NET, LOG_ERROR, "Error: Failed to allocate memory for null terminator\n");
            free(download_data.data);
            curl_easy_cleanup(curl);
            g_downloaded_size = 0;
            return NULL;
        }

        final_data[download_data.size] = '\0';
        curl_easy_cleanup(curl);
        return final_data;
    } else {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: No data received from %s\n", url);
        curl_easy_cleanup(curl);
        g_downloaded_size = 0;
        return NULL;
    }
}

#endif // PLATFORM_WASM || __EMSCRIPTEN__