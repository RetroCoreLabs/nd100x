/*
 * eth_pcap.c - Ethernet frames through a host adapter with Npcap (Windows).
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * See eth_pcap.h.
 *
 * Npcap is used through the classic libpcap calls only (pcap_open_live,
 * pcap_next_ex, pcap_sendpacket, pcap_close, pcap_geterr), declared here rather
 * than taken from the Npcap SDK so nothing extra is needed at build time. The
 * declarations follow libpcap's pcap/pcap.h; on Windows struct timeval is two
 * 32-bit longs (winsock2.h), which is what Npcap's pcap_pkthdr uses.
 *
 * Two handles are opened on the adapter, one for receiving and one for sending:
 * receiving runs on the backend's thread and sending on the emulation thread,
 * and libpcap does not promise that one pcap_t may be used from two threads.
 */

#include "eth_pcap.h"

#include <stdio.h>
#include <string.h>

#include "../../../ndlib/log.h"

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <stdlib.h>

#define PCAP_ERRBUF_SIZE 256
#define PCAP_SNAPLEN 65536
#define PCAP_TIMEOUT_MS 100

typedef struct pcap pcap_t;

struct pcap_pkthdr
{
    struct timeval ts;
    uint32_t caplen;
    uint32_t len;
};

typedef pcap_t *(*PcapOpenLiveFn)(const char *, int, int, int, char *);
typedef int (*PcapNextExFn)(pcap_t *, struct pcap_pkthdr **, const unsigned char **);
typedef int (*PcapSendPacketFn)(pcap_t *, const unsigned char *, int);
typedef void (*PcapCloseFn)(pcap_t *);
typedef char *(*PcapGetErrFn)(pcap_t *);
typedef ULONG(WINAPI *GetAdaptersAddressesFn)(ULONG, ULONG, PVOID, PIP_ADAPTER_ADDRESSES, PULONG);

static HMODULE s_wpcap;
static PcapOpenLiveFn s_open_live;
static PcapNextExFn s_next_ex;
static PcapSendPacketFn s_send_packet;
static PcapCloseFn s_close;
static PcapGetErrFn s_geterr;

struct EthPcap
{
    pcap_t *rx;
    pcap_t *tx;
    char device[300];
};

/* Load wpcap.dll once: Npcap's own folder first (it does not put itself on the search path
 * unless installed in WinPcap-compatible mode), then the normal search order. */
static int load_wpcap(void)
{
    char sys[MAX_PATH];
    char path[MAX_PATH + 32];
    UINT n;

    if (s_wpcap != NULL)
    {
        return 0;
    }
    n = GetSystemDirectoryA(sys, (UINT)sizeof sys);
    if ((n > 0u) && (n < (UINT)sizeof sys))
    {
        (void)snprintf(path, sizeof path, "%s\\Npcap\\wpcap.dll", sys);
        s_wpcap = LoadLibraryA(path);
    }
    if (s_wpcap == NULL)
    {
        s_wpcap = LoadLibraryA("wpcap.dll");
    }
    if (s_wpcap == NULL)
    {
        LOG(LOG_CAT_NET, LOG_ERROR,
            "Ethernet pcap: Npcap is not installed (no wpcap.dll). Install it from "
            "https://npcap.com and run nd100x again\n");
        return -1;
    }
    s_open_live = (PcapOpenLiveFn)(void *)GetProcAddress(s_wpcap, "pcap_open_live");
    s_next_ex = (PcapNextExFn)(void *)GetProcAddress(s_wpcap, "pcap_next_ex");
    s_send_packet = (PcapSendPacketFn)(void *)GetProcAddress(s_wpcap, "pcap_sendpacket");
    s_close = (PcapCloseFn)(void *)GetProcAddress(s_wpcap, "pcap_close");
    s_geterr = (PcapGetErrFn)(void *)GetProcAddress(s_wpcap, "pcap_geterr");
    if ((s_open_live == NULL) || (s_next_ex == NULL) || (s_send_packet == NULL) || (s_close == NULL) ||
        (s_geterr == NULL))
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Ethernet pcap: wpcap.dll lacks the pcap functions nd100x needs\n");
        (void)FreeLibrary(s_wpcap);
        s_wpcap = NULL;
        return -1;
    }
    return 0;
}

/* The adapter table from Windows, or NULL. Free with free(). */
static IP_ADAPTER_ADDRESSES *get_adapters(void)
{
    HMODULE iphlp = LoadLibraryA("iphlpapi.dll");
    GetAdaptersAddressesFn get;
    IP_ADAPTER_ADDRESSES *list = NULL;
    ULONG size = 16384u;
    int tries;

    if (iphlp == NULL)
    {
        return NULL;
    }
    get = (GetAdaptersAddressesFn)(void *)GetProcAddress(iphlp, "GetAdaptersAddresses");
    for (tries = 0; (get != NULL) && (tries < 3); tries++)
    {
        IP_ADAPTER_ADDRESSES *grown = (IP_ADAPTER_ADDRESSES *)realloc(list, size);
        ULONG rc;
        if (grown == NULL)
        {
            break;
        }
        list = grown;
        rc = get(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                 NULL, list, &size);
        if (rc == ERROR_SUCCESS)
        {
            (void)FreeLibrary(iphlp);
            return list;
        }
        if (rc != ERROR_BUFFER_OVERFLOW)
        {
            break;
        }
    }
    free(list);
    (void)FreeLibrary(iphlp);
    return NULL;
}

static void wide_to_utf8(const wchar_t *w, char *out, int size)
{
    out[0] = '\0';
    if ((w == NULL) || (WideCharToMultiByte(CP_UTF8, 0, w, -1, out, size, NULL, NULL) == 0))
    {
        out[0] = '\0';
    }
}

/* Every Ethernet-like adapter Windows knows: connection name, description, Npcap name. */
static void list_adapters(void)
{
    IP_ADAPTER_ADDRESSES *list = get_adapters();
    const IP_ADAPTER_ADDRESSES *a;

    if (list == NULL)
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Ethernet pcap: cannot read the adapter list from Windows\n");
        return;
    }
    LOG(LOG_CAT_NET, LOG_ERROR, "Ethernet pcap: adapters (use the name, e.g. pcap:ND-Loopback):\n");
    for (a = list; a != NULL; a = a->Next)
    {
        char name[256];
        char desc[256];
        wide_to_utf8(a->FriendlyName, name, (int)sizeof name);
        wide_to_utf8(a->Description, desc, (int)sizeof desc);
        LOG(LOG_CAT_NET, LOG_ERROR, "  %-34s %s  \\Device\\NPF_%s\n", name, desc, a->AdapterName);
    }
    free(list);
}

/* Turn ADAPTER into an Npcap device name. Returns 0, or -1 when nothing matched. */
static int resolve_device(const char *adapter, char *device, size_t size)
{
    IP_ADAPTER_ADDRESSES *list;
    const IP_ADAPTER_ADDRESSES *a;
    int found = -1;

    if (_strnicmp(adapter, "\\Device\\NPF_", 12) == 0)
    {
        (void)snprintf(device, size, "%s", adapter);
        return 0;
    }
    if (adapter[0] == '{')
    {
        (void)snprintf(device, size, "\\Device\\NPF_%s", adapter);
        return 0;
    }
    list = get_adapters();
    if (list == NULL)
    {
        return -1;
    }
    /* connection name first, so "Ethernet" is the adapter called Ethernet even when another
     * one's description happens to contain the same word */
    for (a = list; (a != NULL) && (found != 0); a = a->Next)
    {
        char name[256];
        wide_to_utf8(a->FriendlyName, name, (int)sizeof name);
        if (_stricmp(name, adapter) == 0)
        {
            (void)snprintf(device, size, "\\Device\\NPF_%s", a->AdapterName);
            found = 0;
        }
    }
    for (a = list; (a != NULL) && (found != 0); a = a->Next)
    {
        char desc[256];
        wide_to_utf8(a->Description, desc, (int)sizeof desc);
        if (_stricmp(desc, adapter) == 0)
        {
            (void)snprintf(device, size, "\\Device\\NPF_%s", a->AdapterName);
            found = 0;
        }
    }
    free(list);
    return found;
}

static pcap_t *open_handle(const char *device)
{
    char err[PCAP_ERRBUF_SIZE];
    pcap_t *h;

    err[0] = '\0';
    /* promiscuous: the ND has its own MAC address, which is not the adapter's */
    h = s_open_live(device, PCAP_SNAPLEN, 1, PCAP_TIMEOUT_MS, err);
    if (h == NULL)
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Ethernet pcap: cannot open %s: %s\n", device, err);
    }
    return h;
}

EthPcap *eth_pcap_open(const char *adapter)
{
    EthPcap *p;
    char device[300];

    if ((adapter == NULL) || (adapter[0] == '\0'))
    {
        return NULL;
    }
    if (_stricmp(adapter, "list") == 0)
    {
        list_adapters();
        return NULL;
    }
    if (load_wpcap() != 0)
    {
        return NULL;
    }
    if (resolve_device(adapter, device, sizeof device) != 0)
    {
        LOG(LOG_CAT_NET, LOG_ERROR, "Ethernet pcap: no adapter called \"%s\"\n", adapter);
        list_adapters();
        return NULL;
    }
    p = (EthPcap *)calloc(1u, sizeof *p);
    if (p == NULL)
    {
        return NULL;
    }
    (void)snprintf(p->device, sizeof p->device, "%s", device);
    p->rx = open_handle(device);
    p->tx = (p->rx != NULL) ? open_handle(device) : NULL;
    if (p->tx == NULL)
    {
        eth_pcap_close(p);
        return NULL;
    }
    LOG(LOG_CAT_NET, LOG_INFO, "Ethernet pcap: attached to %s (%s)\n", adapter, device);
    return p;
}

int eth_pcap_receive(EthPcap *p, uint8_t *buf, int size)
{
    struct pcap_pkthdr *hdr;
    const unsigned char *data;
    int r;
    int len;

    if ((p == NULL) || (p->rx == NULL))
    {
        return -1;
    }
    r = s_next_ex(p->rx, &hdr, &data);
    if (r == 0)
    {
        return 0; /* timeout */
    }
    if (r < 0)
    {
        return -1;
    }
    len = (int)hdr->caplen;
    if (len > size)
    {
        len = size;
    }
    memcpy(buf, data, (size_t)len);
    return len;
}

int eth_pcap_send(EthPcap *p, const uint8_t *data, int length)
{
    if ((p == NULL) || (p->tx == NULL))
    {
        return -1;
    }
    return (s_send_packet(p->tx, data, length) == 0) ? 0 : -1;
}

void eth_pcap_close(EthPcap *p)
{
    if (p == NULL)
    {
        return;
    }
    if (p->rx != NULL)
    {
        s_close(p->rx);
    }
    if (p->tx != NULL)
    {
        s_close(p->tx);
    }
    free(p);
}

const char *eth_pcap_device(const EthPcap *p)
{
    return (p != NULL) ? p->device : "";
}

#else /* not Windows */

EthPcap *eth_pcap_open(const char *adapter)
{
    LOG(LOG_CAT_NET, LOG_ERROR,
        "Ethernet pcap:%s: the pcap backend is built for Windows (Npcap) only; on Linux use "
        "tap:IFNAME\n", (adapter != NULL) ? adapter : "");
    return NULL;
}

int eth_pcap_receive(EthPcap *p, uint8_t *buf, int size)
{
    (void)p;
    (void)buf;
    (void)size;
    return -1;
}

int eth_pcap_send(EthPcap *p, const uint8_t *data, int length)
{
    (void)p;
    (void)data;
    (void)length;
    return -1;
}

void eth_pcap_close(EthPcap *p)
{
    (void)p;
}

const char *eth_pcap_device(const EthPcap *p)
{
    (void)p;
    return "";
}

#endif /* _WIN32 */
