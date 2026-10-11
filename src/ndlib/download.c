/*
 * download.c - One HTTP/HTTPS GET into a heap buffer.
 *
 * Backends:
 *   - Windows: WinHTTP (winhttp.dll, part of every Windows since XP). TLS and
 *     certificate validation against the Windows certificate store are done
 *     by the OS, so no library and no CA file ship with the exe.
 *   - Linux / BSD / macOS: libcurl, the library the OS or the distro already
 *     provides. It reads the platform root store on its own.
 *   - WASM / RISC-V / builds without HAVE_CURL: stubs that return NULL.
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

#include "download.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

// Upper bound on one download (500 MB), shared by every backend.
#define DL_MAX_BYTES ((size_t)500 * 1024 * 1024)

#if defined(__EMSCRIPTEN__) || defined(__riscv) || (!defined(_WIN32) && !defined(HAVE_CURL))

// No HTTP client on this platform. Provide stubs so machine.c / menu.c still link.
// Triggered on WASM, RISC-V, and any non-Windows build where CMake did not find CURL.

// Get the actual size of downloaded data (not supported in WASM)
size_t dl_get_downloaded_size(void)
{
    return 0;
}

// Unified download function stub for WASM build
char *dl_download_file(const char *url)
{
    (void)url;
    LOG(LOG_CAT_NET, LOG_WARN, "Download not supported in this build (no HTTP client)\n");
    return NULL;
}

#elif defined(_WIN32)

// ---------------------------------------------------------------------------
// Windows backend: WinHTTP.
//
// Same contract as the libcurl backend below: one GET, follow redirects,
// 30 second timeouts, user agent "nd100x/1.0", HTTP 200 required, body
// returned NUL-terminated in a malloc'd buffer, size in g_downloaded_size.
// Certificate validation is left at the WinHTTP default (on, Windows root
// store). No SECURITY_FLAG_IGNORE_* bits are set anywhere in this file.
// ---------------------------------------------------------------------------

#include <windows.h>
#include <winhttp.h>

// Older mingw-w64 headers lack the TLS 1.3 bit. Value from the Windows SDK.
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

// Global variable to track the actual downloaded size
static size_t g_downloaded_size = 0;

// Log a WinHTTP / Win32 error code with its message text.
static void dl_log_win_error(const char *what, DWORD err)
{
    wchar_t wmsg[512];
    char msg[512];
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                             GetModuleHandleW(L"winhttp.dll"), err, 0,
                             wmsg, (DWORD)(sizeof(wmsg) / sizeof(wmsg[0])), NULL);
    while (n > 0 && (wmsg[n - 1] == L'\r' || wmsg[n - 1] == L'\n' || wmsg[n - 1] == L' '))
    {
        wmsg[--n] = L'\0';
    }
    if (n == 0 || !WideCharToMultiByte(CP_UTF8, 0, wmsg, -1, msg, (int)sizeof(msg), NULL, NULL))
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: %s failed (WinHTTP error %lu)\n", what, (unsigned long)err);
        return;
    }
    LOG(LOG_CAT_NET, LOG_ERROR, "Error: %s failed: %s (%lu)\n", what, msg, (unsigned long)err);
}

// UTF-8 -> UTF-16 on the heap. Returns NULL on failure.
static wchar_t *dl_utf8_to_wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0)
    {
        return NULL;
    }
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!w)
    {
        return NULL;
    }
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n))
    {
        free(w);
        return NULL;
    }
    return w;
}

// Copy a (pointer, length) slice that WinHttpCrackUrl returned into its own
// NUL-terminated heap string.
static wchar_t *dl_wide_slice(const wchar_t *p, DWORD len)
{
    wchar_t *w = (wchar_t *)malloc(((size_t)len + 1) * sizeof(wchar_t));
    if (!w)
    {
        return NULL;
    }
    memcpy(w, p, (size_t)len * sizeof(wchar_t));
    w[len] = L'\0';
    return w;
}

// Get the actual size of downloaded data
size_t dl_get_downloaded_size(void)
{
    return g_downloaded_size;
}

// Unified download function that can handle both JSON and binary files
char *dl_download_file(const char *url)
{
    wchar_t *wurl = NULL;
    wchar_t *host = NULL;
    wchar_t *path = NULL;
    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;
    char *data = NULL;
    size_t size = 0;
    char *result = NULL;

    g_downloaded_size = 0;

    if (!url)
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: NULL URL provided to download_file\n");
        return NULL;
    }

    wurl = dl_utf8_to_wide(url);
    if (!wurl)
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: cannot convert URL to UTF-16: %s\n", url);
        goto done;
    }

    // Split the URL. Lengths of (DWORD)-1 ask WinHttpCrackUrl to point into
    // wurl and fill in the real lengths; the pieces are not NUL-terminated.
    URL_COMPONENTSW uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = (DWORD)-1;
    uc.dwHostNameLength = (DWORD)-1;
    uc.dwUrlPathLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc))
    {
        dl_log_win_error("WinHttpCrackUrl", GetLastError());
        goto done;
    }

    BOOL secure;
    if (uc.nScheme == INTERNET_SCHEME_HTTPS)
    {
        secure = TRUE;
    }
    else if (uc.nScheme == INTERNET_SCHEME_HTTP)
    {
        secure = FALSE;
    }
    else
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: URL scheme is not http or https: %s\n", url);
        goto done;
    }

    if (uc.dwHostNameLength == 0)
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: URL has no host name: %s\n", url);
        goto done;
    }
    host = dl_wide_slice(uc.lpszHostName, uc.dwHostNameLength);

    // Path and query are adjacent in the original URL, so one slice covers both.
    if (uc.dwUrlPathLength == 0)
    {
        path = dl_wide_slice(L"/", 1);
    }
    else
    {
        path = dl_wide_slice(uc.lpszUrlPath, uc.dwUrlPathLength + uc.dwExtraInfoLength);
    }
    if (!host || !path)
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: out of memory while splitting URL\n");
        goto done;
    }

    hSession = WinHttpOpen(L"nd100x/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession)
    {
        dl_log_win_error("WinHttpOpen", GetLastError());
        goto done;
    }

    // 30 s for resolve, connect, send and receive (matches CURLOPT_TIMEOUT 30).
    WinHttpSetTimeouts(hSession, 30000, 30000, 30000, 30000);

    // Do not rely on the OS default protocol set (Windows 7 defaults to TLS 1.0
    // without KB3140245). Ask for TLS 1.2 + 1.3; if the TLS 1.3 bit is unknown
    // to this Windows, fall back to TLS 1.2 alone.
    {
        DWORD protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        if (!WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos)))
        {
            protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            if (!WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos)))
            {
                LOG(LOG_CAT_NET, LOG_WARN, "WinHTTP: cannot set TLS protocol list, using the OS default\n");
            }
        }
    }

    hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConnect)
    {
        dl_log_win_error("WinHttpConnect", GetLastError());
        goto done;
    }

    // Redirects are followed by the WinHTTP default policy
    // (WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP, max 10).
    hRequest = WinHttpOpenRequest(hConnect, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  secure ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest)
    {
        dl_log_win_error("WinHttpOpenRequest", GetLastError());
        goto done;
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
    {
        dl_log_win_error("WinHttpSendRequest", GetLastError());
        goto done;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL))
    {
        dl_log_win_error("WinHttpReceiveResponse", GetLastError());
        goto done;
    }

    {
        DWORD status = 0;
        DWORD status_size = sizeof(status);
        if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                                 WINHTTP_NO_HEADER_INDEX))
        {
            dl_log_win_error("WinHttpQueryHeaders(status)", GetLastError());
            goto done;
        }
        if (status != 200)
        {
            LOG(LOG_CAT_NET, LOG_ERROR, "Error: HTTP request failed with code %lu\n", (unsigned long)status);
            goto done;
        }
    }

    // Read the body. Same growth, overflow and size-cap rules as write_callback()
    // in the libcurl backend.
    for (;;)
    {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail))
        {
            dl_log_win_error("WinHttpQueryDataAvailable", GetLastError());
            goto done;
        }
        if (avail == 0)
        {
            break;
        }

        size_t new_size = size + (size_t)avail;
        if (new_size < size)
        {
            LOG(LOG_CAT_NET, LOG_ERROR, "Error: Integer overflow in realloc size calculation\n");
            goto done;
        }
        if (new_size > DL_MAX_BYTES)
        {
            LOG(LOG_CAT_NET, LOG_ERROR, "Error: File size exceeds 500MB limit\n");
            goto done;
        }

        char *grown = (char *)realloc(data, new_size);
        if (!grown)
        {
            LOG(LOG_CAT_NET, LOG_ERROR, "Error: Failed to reallocate memory for download response\n");
            goto done;
        }
        data = grown;

        DWORD got = 0;
        if (!WinHttpReadData(hRequest, data + size, avail, &got))
        {
            dl_log_win_error("WinHttpReadData", GetLastError());
            goto done;
        }
        if (got == 0)
        {
            break;
        }
        size += (size_t)got;
    }

    if (!data || size == 0)
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: No data received from %s\n", url);
        goto done;
    }

    {
        // Add null terminator for compatibility with string functions
        char *final_data = (char *)realloc(data, size + 1);
        if (!final_data)
        {
            LOG(LOG_CAT_NET, LOG_ERROR, "Error: Failed to allocate memory for null terminator\n");
            goto done;
        }
        final_data[size] = '\0';
        data = NULL; // ownership moves to the caller
        g_downloaded_size = size;
        result = final_data;
    }

done:
    if (hRequest)
    {
        WinHttpCloseHandle(hRequest);
    }
    if (hConnect)
    {
        WinHttpCloseHandle(hConnect);
    }
    if (hSession)
    {
        WinHttpCloseHandle(hSession);
    }
    free(data);
    free(path);
    free(host);
    free(wurl);
    return result;
}

#else

// ---------------------------------------------------------------------------
// POSIX backend: libcurl (system library on Linux, BSD and macOS).
// ---------------------------------------------------------------------------

#include <curl/curl.h>

// Global variable to track the actual downloaded size
static size_t g_downloaded_size = 0;

// Structure to hold download data and size
typedef struct
{
    char *data;
    size_t size;
} DownloadData;

// Callback function for CURL to write received data
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    DownloadData *download_data = (DownloadData *)userp;

    // Calculate new size for realloc
    size_t new_size = download_data->size + realsize;

    // Check for integer overflow
    if (new_size < download_data->size)
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: Integer overflow in realloc size calculation\n");
        return 0;
    }

    // Check for maximum file size (500MB as requested)
    if (new_size > DL_MAX_BYTES)
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: File size exceeds 500MB limit\n");
        return 0;
    }

    // Reallocate memory
    char *new_data = realloc(download_data->data, new_size);
    if (!new_data)
    {
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
size_t dl_get_downloaded_size(void)
{
    return g_downloaded_size;
}

// Unified download function that can handle both JSON and binary files
char *dl_download_file(const char *url)
{
    if (!url)
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: NULL URL provided to download_file\n");
        return NULL;
    }

    CURL *curl = curl_easy_init();
    if (!curl)
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: Failed to initialize CURL\n");
        return NULL;
    }

    DownloadData download_data = {NULL, 0};

    // Set up CURL options. TLS trust comes from the platform root store that
    // the system libcurl is built against; nothing is configured here.
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &download_data);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "nd100x/1.0");

    // Perform the request
    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK)
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: CURL request failed: %s\n", curl_easy_strerror(res));
        if (download_data.data)
        {
            free(download_data.data);
        }
        curl_easy_cleanup(curl);
        g_downloaded_size = 0;
        return NULL;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (http_code != 200)
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: HTTP request failed with code %ld\n", http_code);
        if (download_data.data)
        {
            free(download_data.data);
        }
        curl_easy_cleanup(curl);
        g_downloaded_size = 0;
        return NULL;
    }

    if (download_data.data && download_data.size > 0)
    {

        // Store the actual size globally
        g_downloaded_size = download_data.size;

        // Add null terminator for compatibility with string functions
        char *final_data = realloc(download_data.data, download_data.size + 1);
        if (!final_data)
        {
            LOG(LOG_CAT_NET, LOG_ERROR, "Error: Failed to allocate memory for null terminator\n");
            free(download_data.data);
            curl_easy_cleanup(curl);
            g_downloaded_size = 0;
            return NULL;
        }

        final_data[download_data.size] = '\0';
        curl_easy_cleanup(curl);
        return final_data;
    }
    else
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Error: No data received from %s\n", url);
        curl_easy_cleanup(curl);
        g_downloaded_size = 0;
        return NULL;
    }
}

#endif // backend selection
