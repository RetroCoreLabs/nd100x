/*
 * test_eth_window.c - The ND-100 reaches the Ethernet II card DRAM, not local RAM.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Goes through the CPU's own physical memory path (mms_read_physical_memory /
 * mms_write_physical_memory_wm in src/cpu/cpu_mms.c), linked with the real
 * cpu/machine/devices libraries. The card's window for thumbwheel 0 is ND-100
 * byte address 0x200000 = word 0x100000, 512 KB (RetroCore NDBusEthernetII:
 * bank 16 * 0x40 * 2048). ND-100 word W = card DRAM bytes 2*(W - 0x100000)
 * (high) and +1 (low), big-endian, the layout the 68000 sees.
 */

#include "eth_test.h"

#include <string.h>

#include "cpu_types.h"
#include "devices_types.h"
#include "device_ethernet.h"

#define WINDOW_WORD 0x100000u

/* Minimal CPU fixture, as tests/test_memory_banks.c: the register set the
 * gECCR macros resolve through; cpu_init() is not called. */
static struct CpuRegs s_test_regs;

static Device *make_card(uint32_t memsize_words)
{
    memset(&s_test_regs, 0, sizeof(s_test_regs));
    g_reg = &s_test_regs;
    g_nd_memsize = memsize_words;
    mms_memory_banks_init();
    return eth_create_device(0);
}

ETH_TEST(Port, Window_Nd100WriteLandsInCardDram)
{
    Device *dev = make_card(ND_WORDS_PER_MB * 2u);
    EthCard *c = eth_card(dev);

    CHECK(dev != NULL);
    if (dev == NULL)
    {
        return;
    }
    mms_write_physical_memory((int)WINDOW_WORD, 0x1234u, true);
    mms_write_physical_memory((int)(WINDOW_WORD + 0x3FFFFu), 0xBEEFu, true);
    CHECK_EQ(c->mem.dram[0], 0x12);
    CHECK_EQ(c->mem.dram[1], 0x34);
    CHECK_EQ(c->mem.dram[0x7FFFE], 0xBE);
    CHECK_EQ(c->mem.dram[0x7FFFF], 0xEF);
    /* the 68000 sees the same bytes through its own memory map */
    CHECK_EQ(ethmem_read_memory(&c->mem, 0u), 0x12);
    CHECK_EQ(ethmem_read_memory(&c->mem, 1u), 0x34);
    CHECK_EQ(c->nd_window_writes, 2);
    dev->Destroy(dev);
}

ETH_TEST(Port, Window_68000WriteIsSeenByNd100)
{
    Device *dev = make_card(ND_WORDS_PER_MB * 2u);
    EthCard *c = eth_card(dev);

    CHECK(dev != NULL);
    if (dev == NULL)
    {
        return;
    }
    ethmem_write_memory(&c->mem, 0x404u, 0x54u);
    ethmem_write_memory(&c->mem, 0x405u, 0x73u);
    CHECK_EQ(mms_read_physical_memory((int)(WINDOW_WORD + 0x202u), true), 0x5473);
    CHECK_EQ(c->nd_window_reads, 1);
    dev->Destroy(dev);
}

ETH_TEST(Port, Window_ByteWritesHitTheRightHalf)
{
    Device *dev = make_card(ND_WORDS_PER_MB * 2u);
    EthCard *c = eth_card(dev);

    CHECK(dev != NULL);
    if (dev == NULL)
    {
        return;
    }
    mms_write_physical_memory((int)(WINDOW_WORD + 8u), 0x0000u, true);
    mms_write_physical_memory_wm((int)(WINDOW_WORD + 8u), 0x00AAu, true, WRITEMODE_MSB);
    CHECK_EQ(c->mem.dram[16], 0xAA);
    CHECK_EQ(c->mem.dram[17], 0x00);
    mms_write_physical_memory_wm((int)(WINDOW_WORD + 8u), 0x0055u, true, WRITEMODE_LSB);
    CHECK_EQ(c->mem.dram[16], 0xAA);
    CHECK_EQ(c->mem.dram[17], 0x55);
    CHECK_EQ(mms_read_physical_memory((int)(WINDOW_WORD + 8u), true), 0xAA55);
    dev->Destroy(dev);
}

ETH_TEST(Port, Window_LocalRamBelowTheWindowIsUntouched)
{
    Device *dev = make_card(ND_WORDS_PER_MB * 2u);
    EthCard *c = eth_card(dev);

    CHECK(dev != NULL);
    if (dev == NULL)
    {
        return;
    }
    mms_write_physical_memory((int)(WINDOW_WORD - 1u), 0x1111u, true); /* last local word */
    mms_write_physical_memory((int)WINDOW_WORD, 0x2222u, true);        /* first card word */
    CHECK_EQ(mms_read_physical_memory((int)(WINDOW_WORD - 1u), true), 0x1111);
    CHECK_EQ(c->mem.dram[0], 0x22);
    CHECK_EQ(c->nd_window_writes, 1); /* only the card word went through the window */
    dev->Destroy(dev);
}

ETH_TEST(Port, Window_CardTakesTheWindowOverLocalRam)
{
    /* 4 MB of local RAM covers words 0-0x1FFFFF, which includes the window
     * 0x100000-0x13FFFF: the card wins there, local RAM keeps the rest. */
    Device *dev = make_card(ND_WORDS_PER_MB * 4u);
    EthCard *c = eth_card(dev);

    CHECK(dev != NULL);
    if (dev == NULL)
    {
        return;
    }
    CHECK(mms_get_physical_memory_type(WINDOW_WORD - 1u) == ND_MEM_LOCAL);
    CHECK(mms_get_physical_memory_type(WINDOW_WORD) == ND_MEM_PIOC);
    CHECK(mms_get_physical_memory_type(WINDOW_WORD + 0x3FFFFu) == ND_MEM_PIOC);
    CHECK(mms_get_physical_memory_type(WINDOW_WORD + 0x40000u) == ND_MEM_LOCAL);
    CHECK(mms_get_physical_memory_type(ND_WORDS_PER_MB * 4u - 1u) == ND_MEM_LOCAL);
    CHECK(mms_get_physical_memory_type(ND_WORDS_PER_MB * 4u) == ND_MEM_NONE);

    mms_write_physical_memory((int)WINDOW_WORD, 0xCAFEu, true);
    mms_write_physical_memory((int)(WINDOW_WORD + 0x40000u), 0x7777u, true);
    CHECK_EQ(c->mem.dram[0], 0xCA);
    CHECK_EQ(c->mem.dram[1], 0xFE);
    CHECK_EQ(g_volatile_memory.n_Array[WINDOW_WORD + 0x40000u], 0x7777);
    CHECK_EQ(mms_read_physical_memory((int)(WINDOW_WORD + 0x40000u), true), 0x7777);
    CHECK_EQ(c->nd_window_writes, 1);

    /* removing the card gives the window back to local RAM */
    dev->Destroy(dev);
    CHECK(mms_get_physical_memory_type(WINDOW_WORD) == ND_MEM_LOCAL);
}

ETH_TEST(Port, Window_StrapMovesTheWindow)
{
    Device *dev;
    EthCard *c;

    memset(&s_test_regs, 0, sizeof(s_test_regs));
    g_reg = &s_test_regs;
    g_nd_memsize = ND_WORDS_PER_MB * 2u;
    mms_memory_banks_init();
    dev = eth_create_device_strap(0, 24u); /* bank 30B, the real ND-110 sample */
    c = eth_card(dev);
    CHECK(dev != NULL);
    if (dev == NULL)
    {
        return;
    }
    CHECK_EQ(c->physical_page_start, 0x300000);
    mms_write_physical_memory(0x180000, 0x0102u, true);
    CHECK_EQ(c->mem.dram[0], 0x01);
    CHECK_EQ(c->mem.dram[1], 0x02);
    CHECK(eth_create_device_strap(1, 6u) == NULL); /* not a multiple of 4 */
    dev->Destroy(dev);
}

/* F12 page accessor: every field the page shows is the card's own state. */
ETH_TEST(Port, Status_SnapshotMatchesTheCard)
{
    EthernetStatus st;
    Device *dev = NULL;

    memset(&s_test_regs, 0, sizeof(s_test_regs));
    g_reg = &s_test_regs;
    g_nd_memsize = ND_WORDS_PER_MB * 2u;
    mms_memory_banks_init();
    CHECK_EQ(devmgr_init(), 0);
    CHECK(!devmgr_get_ethernet_status(0, &st)); /* no card yet */

    CHECK(devmgr_add_ethernet_device(0, 0, NULL, "none"));
    CHECK(devmgr_get_ethernet_status(0, &st));
    CHECK(!devmgr_get_ethernet_status(1, &st)); /* index 1: no second card */
    CHECK(devmgr_get_ethernet_status(0, &st));
    for (int i = 0; i < devmgr_get_device_count(); i++)
    {
        Device *d = devmgr_get_device_by_index(i);
        if ((d != NULL) && (d->type == DEVICE_TYPE_ETHERNET))
        {
            dev = d;
        }
    }
    CHECK(dev != NULL);
    if (dev == NULL)
    {
        devmgr_destroy();
        return;
    }
    CHECK_EQ(st.thumbwheel, 0);
    CHECK_EQ(st.iox_start, 0140360);
    CHECK_EQ(st.iox_end, 0140363);
    CHECK_EQ(st.ident, 0140034);
    CHECK_EQ(st.level, 12);
    CHECK_EQ(st.memory_bank, 16);
    CHECK_EQ(st.window_byte_address, 0x200000);
    CHECK(st.net_attached);
    CHECK(st.net_active);
    CHECK(strcmp(st.net_description, "none") == 0);
    CHECK_EQ(st.nd_window_writes, 0);

    /* counters move with the card: one ND-100 write and one read through the window */
    mms_write_physical_memory((int)WINDOW_WORD, 0x1234u, true);
    (void)mms_read_physical_memory((int)WINDOW_WORD, true);
    CHECK(devmgr_get_ethernet_status(0, &st));
    CHECK_EQ(st.nd_window_writes, eth_card(dev)->nd_window_writes);
    CHECK_EQ(st.nd_window_writes, 1);
    CHECK_EQ(st.nd_window_reads, eth_card(dev)->nd_window_reads);
    CHECK_EQ(st.lance_csr0, eth_card(dev)->mem.lance.csr[0]);
    CHECK_EQ(st.tx_packets, eth_card(dev)->tx_packets);
    CHECK_EQ(st.m68k_running, eth_is_68k_running(eth_card(dev)));

    devmgr_destroy();
}
