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


#include <assert.h>
#include <string.h>

#include "cpu_types.h"
#include "cpu_protos.h"
#include "../ndlib/log.h"

// Global MMS type variable definition
MMSType mmsType = MMS2; // Change this to force MMS type to 1 or 2
PagingTables g_paging_tables; // Global paging tables structure

/* --ring-at-pf=N: dump the CPU instruction ring at the N'th page fault (0 = off). */
static long s_ring_at_pf = 0;

/**
 * @brief Set the page fault at which to dump the instruction ring (--ring-at-pf).
 * @param n Page-fault number counted from 1; 0 turns the dump off.
 */
void cpu_set_ring_at_pf(long n)
{
    s_ring_at_pf = n > 0 ? n : 0;
}


// Create and initialize PagingTables
// MUST!!!! to be called before using the PagingTables
bool CreatePagingTables(void)
{
    g_paging_tables.mmsType = mmsType;

    // Allocate shadow RAM based on MMS type
    if (mmsType == MMS1)
    {
        g_paging_tables.shadowRamSize = 512;  // 4 page tables = 2 x 64 bit * 4 = 512 Words
        g_paging_tables.shadowRamAddress = SHADOW_RAM_EXTENDED_MODE_4PT;
    }
    else
    {
        g_paging_tables.shadowRamSize = 2048; // 16 page tables = 16 x 64 bit * 4 = 2048 Words
        g_paging_tables.shadowRamAddress = SHADOW_RAM_EXTENDED_MODE_16PT;
    }

    g_paging_tables.shadowRam = (uint16_t*)calloc(g_paging_tables.shadowRamSize, sizeof(uint16_t));
    if (!g_paging_tables.shadowRam)
    {
        LOG(LOG_CAT_MMS, LOG_ERROR, "Failed to allocate shadow RAM");
        return false;
    }

    g_paging_tables.isInitialized = 1;
    return true;
}


// Helper functions
static uint32_t ConvertFrom16BitPTE(uint16_t value)
{
    uint32_t pte = ((uint32_t)(value & 0xFE00) << 16) | (uint32_t)(value & 0x01FF);
    return pte;
}

static uint16_t ConvertTo16BitPTE(uint32_t pageTableEntry)
{
    uint16_t res = (uint16_t)(((pageTableEntry & 0xFE000000) >> 16) | (pageTableEntry & 0x000001FF));
    return res;
}


// Page-table index of a shadow-RAM address (used by the mms trace output)
static uint32_t CalcPageTableAddress(uint32_t address)
{
    uint32_t pageTableAddress;
    if (STS_SEXI)
    {
        // Extended, check if we have MM-1 or MM-II
        if (g_paging_tables.mmsType == MMS1)
        {
            // 4 page tables start at 177000 (0xFE00)
            pageTableAddress = ((address - SHADOW_RAM_EXTENDED_MODE_4PT) & 0x1FF) >> 1;
        }
        else
        {
            // 16 page tables start at 174000 (0xF800)
            pageTableAddress = ((address - SHADOW_RAM_EXTENDED_MODE_16PT) & 0x7FF) >> 1;
        }
    }
    else
    {
        // Normal mode, 4 page tables, all start at 177400 (FF00)
        pageTableAddress = (address & 0x00ff);
    }
    return pageTableAddress;
}

// Clean up PagingTables
void DestroyPagingTables(void)
{
    if (g_paging_tables.shadowRam)
    {
        free(g_paging_tables.shadowRam);
    }
}


// Calculate offset into ShadowRam array
uint16_t GetPTShadowAddress(uint32_t pageTable, uint32_t VPN, PageTableMode ptm)
{
    uint32_t offset = 0;

    if (STS_SEXI)
    {
        // EXTENDED MODE
        switch (ptm)
        {
            case Four:
                offset = SHADOW_RAM_EXTENDED_MODE_4PT - g_paging_tables.shadowRamAddress;
                break;
            case Sixteen: // ONLY for MMS2
                offset = SHADOW_RAM_EXTENDED_MODE_16PT - g_paging_tables.shadowRamAddress;
                break;
        }
    }
    else
    {
        // Normal mode
        offset = SHADOW_RAM_NORMAL_MODE_4PT - g_paging_tables.shadowRamAddress;
    }

    uint32_t pageTableAddress = (pageTable << 6) | VPN;
    if (STS_SEXI)
    {
        // Extended mode, PTe is stored in 2x 16 bits memory addresses
        pageTableAddress = pageTableAddress << 1; // left shift1 == *2
    }
    pageTableAddress += offset;

    return (uint16_t)pageTableAddress;
}

// Write to page tables
void PT_Write(uint32_t address, uint16_t value)
{
    if (!g_paging_tables.shadowRam) return;
    if ((address < g_paging_tables.shadowRamAddress) || (address > 0xFFFF)) return;

    uint32_t offset = address - g_paging_tables.shadowRamAddress;


    g_paging_tables.shadowRam[offset] = value;

    if (ND100X_HOT_TRACE && Log_IsEnabled(LOG_CAT_MMS, LOG_TRACE))
    {
        uint32_t pageTableEntry;
        uint32_t pageTableAddress = CalcPageTableAddress(address);
        uint32_t pageTable = pageTableAddress >> 6;

        if (!STS_SEXI)
        {
            pageTableEntry = ConvertFrom16BitPTE(value);
        }
        else
        {
            if ((address & 0x01) == 0)
            {
                // Even address
                pageTableEntry = ((uint32_t)value << 16) | g_paging_tables.shadowRam[offset + 1];
            }
            else
            {
                // Odd address
                pageTableEntry = ((uint32_t)g_paging_tables.shadowRam[offset - 1] << 16) | value;
            }
        }
        Log_Write(LOG_CAT_MMS, LOG_TRACE, "PT W A=%o PT=%d VPN=%d SEXI=%d V=%o => 0x%08X (%s)\n",  address, pageTable, pageTableAddress & 0x3F, STS_SEXI, value,  pageTableEntry, GetPageTableEntryDebugInfo(pageTableEntry));
    }
}

// Read from shadow mem/pagetables
uint16_t PT_Read(uint32_t address)
{
    if (!g_paging_tables.shadowRam) return 0;
    if ((address < g_paging_tables.shadowRamAddress) || (address > 0xFFFF)) return 0;

    uint32_t offset = address - g_paging_tables.shadowRamAddress;
    uint16_t res = g_paging_tables.shadowRam[offset];

    return res;
}

// Get page table entry
uint32_t GetPageTableEntry(uint32_t pageTable, uint32_t VPN,PageTableMode ptm)
{
    if (!g_paging_tables.shadowRam) return 0;
    if (pageTable >= 16) return 0;

    uint32_t PTe = 0;
    int pageTableAddress = GetPTShadowAddress(pageTable, VPN, ptm);

    if (STS_SEXI)
    {
        PTe = ((uint32_t)g_paging_tables.shadowRam[pageTableAddress] << 16) | g_paging_tables.shadowRam[pageTableAddress + 1];
    }
    else
    {
        if (pageTable <= 3)
        {
            PTe = ConvertFrom16BitPTE(g_paging_tables.shadowRam[pageTableAddress]);
        }
    }

    return PTe;
}

// Get page table entry for debugger/inspector use.
//
// GetPageTableEntry() checks STS_SEXI to decide shadow RAM format. STS_SEXI
// reflects the SEXI flag of the currently executing interrupt level, so when
// SEXI is off, PT 4-15 return 0. This is correct for normal CPU operation.
//
// The JS debugger however needs to read any page table at any time, including
// DPIT (PT#7) for SINTRAN kernel inspection. When the CPU is paused, the
// current level may not have SEXI set, even though SINTRAN loaded entries
// into PT 4-15 while running at a level with SEXI enabled. The shadow RAM
// content is still valid - the hardware format (MMS1=4PT/16-bit vs
// MMS2=16PT/32-bit) is fixed and does not depend on the runtime SEXI state.
//
// This function uses mmsType instead of STS_SEXI so the debugger can always
// read all page tables regardless of which level happens to be active.
uint32_t GetPageTableEntryForDebugger(uint32_t pageTable, uint32_t VPN, PageTableMode ptm)
{
    (void)ptm;
    if (!g_paging_tables.shadowRam) return 0;
    if (pageTable >= 16) return 0;

    uint32_t PTe = 0;

    if (mmsType == MMS2)
    {
        // MMS2 hardware: always 32-bit PTEs in extended 16PT area
        uint32_t offset = SHADOW_RAM_EXTENDED_MODE_16PT - g_paging_tables.shadowRamAddress;
        uint32_t pageTableAddress = ((pageTable << 6) | VPN) << 1;
        pageTableAddress += offset;
        PTe = ((uint32_t)g_paging_tables.shadowRam[pageTableAddress] << 16) | g_paging_tables.shadowRam[pageTableAddress + 1];
    }
    else
    {
        // MMS1 hardware: only 4 page tables, 16-bit PTEs
        if (pageTable <= 3)
        {
            uint32_t offset = SHADOW_RAM_NORMAL_MODE_4PT - g_paging_tables.shadowRamAddress;
            uint32_t pageTableAddress = (pageTable << 6) | VPN;
            pageTableAddress += offset;
            PTe = ConvertFrom16BitPTE(g_paging_tables.shadowRam[pageTableAddress]);
        }
    }

    return PTe;
}

// Update page table entry
bool UpdatePageTableEntry(uint32_t pageTable, uint32_t VPN, PageTableMode ptm, uint32_t PTe)
{
    if (!g_paging_tables.shadowRam) return false;
    if (pageTable >= 16) return false;

    int pageTableAddress = GetPTShadowAddress(pageTable, VPN, ptm);

    if (STS_SEXI)
    {
        g_paging_tables.shadowRam[pageTableAddress] = (uint16_t)(PTe >> 16);
        g_paging_tables.shadowRam[pageTableAddress + 1] = (uint16_t)(PTe);
    }
    else
    {
        g_paging_tables.shadowRam[pageTableAddress] = ConvertTo16BitPTE(PTe);
    }

    return true;
}

// Set page used flag
uint32_t SetPageUsed(uint32_t pageTable, uint32_t VPN, PageTableMode ptm, uint32_t PTe)
{
    if ((PTe & PGU_FLAG) == 0)
    {
        PTe |= PGU_FLAG;
        UpdatePageTableEntry(pageTable, VPN, ptm, PTe);

        if (ND100X_HOT_TRACE && Log_IsEnabled(LOG_CAT_MMS, LOG_TRACE))
            Log_Write(LOG_CAT_MMS, LOG_TRACE, "PageTable PGU - PT=%d VPN=%d => Entry=0x%08X (%s)",
                      pageTable, VPN, PTe, GetPageTableEntryDebugInfo(PTe));
    }
    return PTe;
}

// Set page written flag
uint32_t SetPageWritten(uint32_t pageTable, uint32_t VPN,PageTableMode ptm, uint32_t PTe)
{
    if (!g_paging_tables.shadowRam) return PTe;

    if (pageTable >= 16) return PTe;

    if ((PTe & WIP_FLAG) == 0)
    {
        PTe |= WIP_FLAG;
        UpdatePageTableEntry( pageTable, VPN, ptm, PTe);

        if (ND100X_HOT_TRACE && Log_IsEnabled(LOG_CAT_MMS, LOG_TRACE))
            Log_Write(LOG_CAT_MMS, LOG_TRACE, "PageTable WIP - PT=%d VPN=%d => Entry=0x%08X (%s)",
                      pageTable, VPN, PTe, GetPageTableEntryDebugInfo(PTe));
    }
    return PTe;
}


// Get debug info for page table entry
const char* GetPageTableEntryDebugInfo(uint32_t PTe)
{
    static char debugInfo[256];
    debugInfo[0] = '\0';

    // Map to physical page
    uint16_t PPN = 0;
    if (STS_SEXI)
    {
        // Use lower 14-bit
        PPN = (uint16_t)(PTe & 0x3FFF);
    }
    else
    {
        // "normal" mode, use only the lower 9-bits
        PPN = (uint16_t)(PTe & 0x1FF);
    }

    PTe = PTe >> 16;

    if ((PTe & 1 << 15) != 0) snprintf(debugInfo + strlen(debugInfo), sizeof(debugInfo) - strlen(debugInfo), "%s", "[WPM]");
    if ((PTe & 1 << 14) != 0) snprintf(debugInfo + strlen(debugInfo), sizeof(debugInfo) - strlen(debugInfo), "%s", "[RPM]");
    if ((PTe & 1 << 13) != 0) snprintf(debugInfo + strlen(debugInfo), sizeof(debugInfo) - strlen(debugInfo), "%s", "[FPM]");
    if ((PTe & 1 << 12) != 0) snprintf(debugInfo + strlen(debugInfo), sizeof(debugInfo) - strlen(debugInfo), "%s", "[WIP]");
    if ((PTe & 1 << 11) != 0) snprintf(debugInfo + strlen(debugInfo), sizeof(debugInfo) - strlen(debugInfo), "%s", "[PGU]");

    int ring = (int)((PTe >> 9) & 0x03);
    char ringStr[8];
    snprintf(ringStr, sizeof(ringStr), "[R:%d]", ring);
    snprintf(debugInfo + strlen(debugInfo), sizeof(debugInfo) - strlen(debugInfo), "%s", ringStr);

    char ppnStr[16];
    snprintf(ppnStr, sizeof(ppnStr), "[PPN:0x%04X]", PPN);
    snprintf(debugInfo + strlen(debugInfo), sizeof(debugInfo) - strlen(debugInfo), "%s", ppnStr);

    return debugInfo;
}


// Map virtual address to physical address
int mapVirtualToPhysical(uint32_t virtualAddress, AccessMode am, bool UseAPT)
{


    /* cpu_init() creates the paging tables before anything can translate an
     * address, so this is an internal invariant, not an input error. */
    assert(g_paging_tables.isInitialized);
    if (!g_paging_tables.isInitialized)
    {
        LOG(LOG_CAT_MMS, LOG_ERROR, "FATAL! PagingTables not initialized");
        return -1;
    }

    virtualAddress = virtualAddress & 0xFFFF; // Make sure it's no more than 16-bits
    uint32_t pageTable = 0;

    // Read PCR for the current level and calculate the ring we are executing the code under
    uint16_t pcr = gReg->reg_PCR[CurrLEVEL];
    uint8_t ring = pcr & 0x03;  // 2 lower bits of the PCR is the Ring the current level is using

    // Ring 3 is the most powerful, and for Ring 3 RAM will always be in the shadow of PageTable RAM
    if ((ring == 3) && (IsAddressShadowMemory(virtualAddress, false)))
    {
        return (int)virtualAddress; // Read/WritePhysical will handle the actual access to shadow memory
    }

    // If memory management is not enabled, don't use mapping (physical = virtual)
    if (!STS_PONI) return (int)(virtualAddress & 0xFFFF);

    // Calculate VPN and DIP
    uint32_t DIP = virtualAddress & 0x3FF; // lower 10 bits - Displacement
    uint32_t VPN = (virtualAddress >> 10) & 0x3F; // upper 6 bits - Virtual Page number

    PageTableMode ptm = Four; // Default to four page tables

    // Find PageTable Number and identify if we have the optional 16 page-table mode
    if ((STS_PTM) && (UseAPT))
    {
        if ((pcr & (1 << 2)) != 0 && (mmsType == MMS2))
        {
            // Sixteen page table mode
            pageTable = (pcr >> 7) & 0xF; // AlternativePageTable - 16x
            ptm = Sixteen;
        }
        else
        {
            // Four page table mode
            pageTable = (pcr >> 7) & 0x03; // AlternativePageTable - 4x
            ptm = Four;
        }
    }
    else
    {
        if ((pcr & (1 << 2)) != 0 && (mmsType == MMS2))
        {
            // Sixteen page table mode
            pageTable = (pcr >> 11) & 0xF; // PageTable - 16x
            ptm = Sixteen;
        }
        else
        {
            // Four page table mode
            pageTable = (pcr >> 9) & 0x03; // PageTable - 4x
            ptm = Four;
        }
    }

    /* no debug trace */

    // Find the PageTableEntry, PTe
    uint32_t pageTableEntry = GetPageTableEntry(pageTable, VPN, ptm);

    /* DEBUG VPN25 tracing removed - was temporary overlay debugging */

    if (ND100X_HOT_TRACE && Log_IsEnabled(LOG_CAT_MMSMAP, LOG_TRACE))
        Log_Write(LOG_CAT_MMSMAP, LOG_TRACE, "mapVirtualToPhysical - PT=%d VPN=%d => Entry=0x%08X (%s)",
                  pageTable, VPN, pageTableEntry, GetPageTableEntryDebugInfo(pageTableEntry));

    // Check for page protection
    if (!checkPageProtection(VPN, pageTable, pageTableEntry, am, virtualAddress))
    {
         // We should never get here, but added a return statement anyway! (Will end up here if interrupts are disabled?)
        return -1;
    }

    uint8_t pageTableRing = (pageTableEntry >> 25) & 0x03;

#ifdef _DEGRADE_
    if ((am & FETCH) && (pageTableRing < ring) && (ring == 3))
    {
        if (ND100X_HOT_TRACE && Log_IsEnabled(LOG_CAT_MMS, LOG_TRACE))
        {
            static int degrade_count = 0;
            if (degrade_count < 10)
                Log_Write(LOG_CAT_MMS, LOG_TRACE, "DEGRADE: PIL=%d PC=%06o PT=%d VPN=%d ptRing=%d ring=%d->%d PTe=0x%08X",
                          CurrLEVEL, gPC, pageTable, VPN, pageTableRing, ring, pageTableRing, pageTableEntry);
            degrade_count++;
        }
        ring = pageTableRing;
        gReg->reg_PCR[CurrLEVEL] = (gReg->reg_PCR[CurrLEVEL] & 0xFFFC) | ring;
    }
#endif

    // Check for Ring Protection
    // INFO: For the ND CPU Ring 3 is most powerfull, ring 0 least powerfull.
    // If the current level has a "ring level" that is smaller than the ring level on the page, generate a fault
    //
    // The ring bits of the appropriate PCR are compared with the ring bits of the appropriate page table entry.
    // The PCR ring bits should always be greater than or equal to the PT ring bits. If not, an internal interrupt (MPV) will be generated.

    if (ring < pageTableRing)
    {
        if (ND100X_HOT_TRACE && Log_IsEnabled(LOG_CAT_MMS, LOG_TRACE))
        {
            static int ring_mpv = 0;
            if (ring_mpv < 5) {
                uint16_t pcr_now = gReg->reg_PCR[CurrLEVEL];
                Log_Write(LOG_CAT_MMS, LOG_TRACE, "RING_MPV: PT=%d VPN=%d ring=%d ptRing=%d PCR=0%06o PCR_ring=%d PIL=%d VA=%06o am=%d",
                          pageTable, VPN, ring, pageTableRing, pcr_now, pcr_now & 3, CurrLEVEL, virtualAddress, am);
            }
            ring_mpv++;
        }
        UpdatePGS(pageTable, VPN, am, false);
        if (ND100X_HOT_TRACE && Log_IsEnabled(LOG_CAT_MMS, LOG_TRACE))
            Log_Write(LOG_CAT_MMS, LOG_TRACE, "[%d] Ring Protection Violation. Ring=%d PTRing=%d Accessmode=%d PGS=%06o PT=%d VPN=%d PTe=0x%08X",
                      CurrLEVEL, ring, pageTableRing, am, gReg->reg_PGS, pageTable, VPN, pageTableEntry);
        HandleMPV(virtualAddress);
        return -1;
    }

    // Map to physical page
    uint16_t PPN = 0;
    if (STS_SEXI)
    {
        // Use lower 14-bit
        PPN = (uint16_t)(pageTableEntry & 0x3FFF);
    }
    else
    {
        // "normal" mode, use only the lower 9-bits
        PPN = (uint16_t)(pageTableEntry & 0x1FF);
    }

    // Calculate physical address (24-bit bus)
    int physicalAddress = ((PPN << 10) | DIP) & 0xFFFFFF;

    // Check if memory is out of range
    if ((uint32_t)physicalAddress >= ND_Memsize)
    {
        UpdatePGS(pageTable, VPN, am, false);
        HandleMemoryOutOfRange(physicalAddress);
        return -1;
    }

    // Mark page used
    pageTableEntry = SetPageUsed(pageTable, VPN, ptm, pageTableEntry);
    if (am == WRITE)
    {
        // Mark page as "written to"
        pageTableEntry = SetPageWritten(pageTable, VPN, ptm, pageTableEntry);
    }

    // ECC Memory Parity is checked on the PHYSICAL read/write path
    // (ReadPhysicalMemory / WritePhysicalMemoryWM), NOT here - so it also covers
    // EXAM/DEPO physical accesses that never go through mapVirtualToPhysical.
    // See GetPhysicalMemoryType / nd_ecc_write_latch / nd_ecc_read_detect below.

    if (ND100X_HOT_TRACE && Log_IsEnabled(LOG_CAT_MMS, LOG_TRACE) && physicalAddress == 0)
        Log_Write(LOG_CAT_MMS, LOG_TRACE, "mapVirtualToPhysical - PT=%d VPN=%d => Entry=0x%08X (%s)",
                  pageTable, VPN, pageTableEntry, GetPageTableEntryDebugInfo(pageTableEntry));
    return (int)physicalAddress;
}

// Update PGS (Page Status) register
void UpdatePGS(uint32_t pageTable, uint32_t VPN, AccessMode am, bool permitViolation)
{
    uint16_t tmpPGS = (pageTable << 6) | VPN;

    // Permit violation (read, write, fetch protect system)
    if (permitViolation) tmpPGS |= (1 << 14);


    if (am & FETCH)
    {
        if (am & READ)
        {
            // READ_FETCH - Indirect read during effective address calculation
        }
        else
        {
            tmpPGS |= (1 << 15);
        }
    }

    setPGS(tmpPGS);
}

// Check page protection
bool checkPageProtection(uint32_t VPN, uint32_t pageTable, uint32_t pageTableEntry, AccessMode am, uint32_t virtualAddress)
{
    // Unsigned 32-bit: a PTE is 32 bits, and 1L << 31 overflowed a 32-bit long on wasm.
    uint32_t accessBits = 0;
    uint32_t pfMask = UINT32_C(7) << 29;

    if (am & READ)  accessBits |= UINT32_C(1) << 30; // RPM(Read Permit bit)
    if (am & WRITE) accessBits |= UINT32_C(1) << 31; // WPM (Write Permit bit)
    if (am & FETCH) accessBits |= UINT32_C(1) << 29; // FPM (Fetch Permit bit)

    // Check if page is in memory
    // Page 89 (Chapter 3) in ND-110 Functional Description
    // If the combination of WPM, RPM and FPM are all zero, this is interpreted as page not in memory and will generate an internal interrupt as page fault
    if ((pageTableEntry & pfMask) == 0)
    {
        // ---------------------------------------------------------------
        // DO NOT CHANGE THE PM ARGUMENT BELOW (true). VALIDATED AGAINST
        // REAL HARDWARE BEHAVIOUR BY THE ND PAGING DIAGNOSTIC (TPE).
        // ---------------------------------------------------------------
        // PGS bit 14 (PM - Permit violation) is TRUE here, even though the page
        // is NOT present (WPM, RPM and FPM all zero). That looks wrong by pure
        // reasoning - there are no permissions to violate on an absent page -
        // but it is what the hardware does, so the reasoning is what is wrong.
        //
        // Evidence (14-JUL-2026): ND paging diagnostic TPE, test 6 (PAGE FAULT
        // interrupt), Extended mode, PIT entry 002000B/000054B (PTe 0x0400002C).
        // With PM=true  -> tests 1-11 all pass.
        // With PM=false -> test 6 aborts, reporting "Ring violation instead of
        //                  permit violation" on every P-relative/indirect
        //                  access, because the diagnostic decodes PM=0 as a
        //                  ring violation.
        //
        // History: commit e57c6a5 (16-MAR-2026) set this to false, citing
        // ND-60.062.01 p.25 and a BSD kernel need to tell "page not mapped"
        // from "access denied" via PGS. That broke test 6 and was reverted.
        // If BSD needs to distinguish the two cases, use the interrupt code,
        // NOT this bit: not-present raises IIC=3 (PF, called just below),
        // access-denied raises IIC=2 (MPV, in the branch further down).
        // Re-check against TPE test 6 before touching this line again.

        /*
         * DIAG (--ring-at-pf=<n>): one-shot CPU instruction ring dump at the n'th
         * page fault, so we can see the SINTRAN page-fault handler path that leads back into
         * the ENPT/CLPT swap loop without ever mapping the demanded page.
         */
        {
            static long pf_calls = 0;

            pf_calls++;
            if (s_ring_at_pf > 0 && pf_calls == s_ring_at_pf)
                ring_dump();
        }

        /* DIAG (--trace-nd110): correlate page faults with the ENPT/CLPT swap loop. */
        if (nd110_trace_fp != NULL)
        {
            fprintf(nd110_trace_fp, "  PF   VA=%06o PT=%d VPN=%d PTe=0x%08X am=%d APT=%d PIL=%d PC=%06o\n",
                    virtualAddress, pageTable, VPN, (uint32_t)pageTableEntry, am, gUseAPT ? 1 : 0, CurrLEVEL, gPC);
            fflush(nd110_trace_fp);
        }

        UpdatePGS(pageTable, VPN, am, true);
        HandlePF(virtualAddress);
        return false;
    }

    // Check access permissions
    // The page IS present (at least one of WPM/RPM/FPM is set), but the
    // specific access mode requested is not permitted by the page table entry.
    // This IS a genuine permit violation, so PM=1 is correct here.
    // Triggers IIC=2 (memory protection violation).
    if ((pageTableEntry & accessBits) == 0)
    {
        // if (1) {  /* trace ALL access-denied MPVs */
        //     static int mpv25 = 0;
        //     if (mpv25 < 5)
        //                pageTable, VPN, (uint32_t)pageTableEntry, (unsigned long)accessBits, am, UseAPT, CurrLEVEL, virtualAddress);
        //     mpv25++;
        // }
        /* DIAG (--trace-nd110): correlate permit violations with the ENPT/CLPT swap loop. */
        if (nd110_trace_fp != NULL)
        {
            fprintf(nd110_trace_fp, "  MPV  VA=%06o PT=%d VPN=%d PTe=0x%08X need=0x%08lX am=%d APT=%d PIL=%d PC=%06o\n",
                    virtualAddress, pageTable, VPN, (uint32_t)pageTableEntry,
                    (unsigned long)accessBits, am, gUseAPT ? 1 : 0, CurrLEVEL, gPC);
            fflush(nd110_trace_fp);
        }
        UpdatePGS(pageTable, VPN, am, true);
        HandleMPV(virtualAddress);
        return false;
    }

    return true;
}

// Check if address is in shadow memory
// Flag set by Device_DMARead/Write to bypass shadow memory for DMA bus transfers
bool gDMAAccess = false;

bool IsAddressShadowMemory(uint32_t addr, bool privileged)
{
    // DMA transfers go directly to physical RAM - never shadow memory
    if (gDMAAccess)
        return false;

    // Shadow memory (page tables) only exists in the first 64K word address space.
    if (addr > 0xFFFF)
        return false;

    uint16_t pcr = gReg->reg_PCR[CurrLEVEL];
    unsigned char ring = pcr & 0x03;
    bool mms2Enabled = ((pcr & 1 << 2) != 0); // Is MMS-2 with 16-page-tables enabled on this PCR level ?


    if ((ring == 3) || (!STS_PONI) || privileged)
    {

        if (STS_SEXI)
        {
            if ((mmsType == MMS2) && mms2Enabled)
            {
                if ((addr >= 0xF800) && (addr <= 0xFFFF))
                {
                    return true;
                }
            }
            else
            {
                if ((addr >= 0xFE00) && (addr <= 0xFFFF))
                {
                    return true;
                }
            }
        }
        else
        {
            if ((addr >= 0xFF00) && (addr <= 0xFFFF))
            {
                return true;
            }
        }
    }

    return false;
}

// Read from virtual memory
int ReadVirtualMemory(uint32_t virtualAddress, bool UseAPT)
{
    if (DISASM)
		disasm_set_isdata(virtualAddress);

    int pa = mapVirtualToPhysical(virtualAddress, READ, UseAPT);
    if (pa == -1) return 0;
    return ReadPhysicalMemory(pa, false);
}

// Read indirect from virtual memory (used only for effective address calculation)
int ReadIndirectVirtualMemory(uint32_t virtualAddress, bool UseAPT)
{
    int pa = mapVirtualToPhysical(virtualAddress, READ_FETCH, UseAPT);
    if (pa == -1) return 0;
    return ReadPhysicalMemory(pa, false);
}

// Fetch from virtual memory
int FetchVirtualMemory(uint32_t virtualAddress, bool UseAPT)
{
    int pa = mapVirtualToPhysical(virtualAddress, FETCH, UseAPT);
    if (pa == -1) return 0;
    return ReadPhysicalMemory(pa, false);
}

// Write to virtual memory
void WriteVirtualMemory(uint32_t virtualAddress, uint16_t value, bool UseAPT, WriteMode wm)
{
    if (DISASM)
		disasm_set_isdata(virtualAddress);

    int pa = mapVirtualToPhysical(virtualAddress, WRITE, UseAPT);
    /* TRACE: detect writes to VA 0x2F9D (_ov_saved_l_bss) - disabled */
    if (pa == -1) return;
    WritePhysicalMemoryWM(pa, value, false,wm);
}




// Classify a PHYSICAL word address into its ND-100 memory TYPE (local vs shared).
//
// Mirrors RetroCore's ND100Memory.FindMemoryBank()/GetMemoryTypeCode(): walk the
// memory-mapped regions in the same priority order - the ND-500 MPM5 window (a
// documented STUB at ND_MPM5_WINDOW_*, above installed RAM at the default size),
// then plain local ND-100 RAM. Only LOCAL RAM (KMECCR) is ECC/parity checked, so
// this gates the parity path exactly like RetroCore's CheckECCR.
NDMemoryType GetPhysicalMemoryType(uint32_t physicalWordAddress)
{
    // ND-500 MPM5 shared-memory window (3022/5015 Port-A). Highest priority.
    if ((physicalWordAddress >= ND_MPM5_WINDOW_START_WORD) &&
        (physicalWordAddress <  ND_MPM5_WINDOW_START_WORD + ND_MPM5_WINDOW_SIZE_WORD))
    {
        return ND_MEM_MPM5; // KMPM5 - not ECC checked
    }

    // Installed local ND-100 RAM (ECC/parity checked).
    if (physicalWordAddress < ND_Memsize)
    {
        return ND_MEM_LOCAL; // KMECCR
    }

    // Nothing claims this address.
    return ND_MEM_NONE;
}

// -- ECC Memory Parity: store-on-write latch + detect-on-read --------------------
// Mirrors RetroCore CpuND100.MMS.cs (CheckECCR / CheckWriteECCR / CheckReadECCR).
// Lives on the PHYSICAL read/write path so it ALSO covers EXAM/DEPO physical
// accesses - which is exactly how TPE CONFIGURATION / SINTRAN probe each bank:
// arm ECCR = SimBit0 + DisableECC(bit3), WRITE the bank (must latch the simulated
// bad ECC even with bit 3 set), then clear bit 3 and READ back; a level-14 parity
// interrupt => the bank has ECC => LOCAL, silence => MPM5. Only LOCAL ND-100 RAM
// (KMECCR) carries ECC. 0x13 = SimBit0(1<<0) | SimBit15(1<<1) | SimBit6(1<<4).
// Per-physical-word simulated bad-ECC latch (0 = good). MUST be per-address, not a
// single global (see nd_ecc_write_latch). Lazily allocated on first simulated error.
// Gating is per-word (a cheap array read), NOT a global count: a diagnostic that leaves
// some words latched (TPE MEM parity test) must not slow every later access - the WALK
// test does hundreds of millions of them.
static uint8_t *gEccLatch = NULL;

static void nd_ecc_write_latch(int physicalAddress)
{
    // The three ECCR simulate bits packed as a byte: SimBit0(1<<0), SimBit15(1<<1), SimBit6(1<<4).
    uint8_t bits = 0;
    if ((gECCR & (1 << 0)) != 0) bits |= (1 << 0);
    if ((gECCR & (1 << 1)) != 0) bits |= (1 << 1);
    if ((gECCR & (1 << 4)) != 0) bits |= (1 << 4);

    if (physicalAddress < 0 || (uint32_t)physicalAddress >= ND_Memsize) return;
    uint8_t latched = (gEccLatch != NULL) ? gEccLatch[physicalAddress] : 0;
    // Clean write to a clean word: nothing to store or clear (the common case, incl. WALK).
    if (bits == 0 && latched == 0) return;
    if (GetPhysicalMemoryType((uint32_t)physicalAddress) != ND_MEM_LOCAL) return;

    if (gEccLatch == NULL)
    {
        gEccLatch = (uint8_t *)calloc((size_t)ND_Memsize, 1);
        if (gEccLatch == NULL) return;
    }

    // STORE-ON-WRITE, PER ADDRESS: a write recomputes THIS word's ECC - bad if a sim bit is
    // armed, good (cleared) otherwise - regardless of DisableECC(bit 3), which only gates
    // DETECTION on read. Per-address (not one global) so an unrelated read - e.g. an
    // instruction FETCH between MEM's write and its read-back - can't consume it.
    gEccLatch[physicalAddress] = bits;
}

static void nd_ecc_read_detect(int physicalAddress)
{
    uint8_t live = (uint8_t)(gECCR & 0x13); // live simulate bits
    if (physicalAddress < 0 || (uint32_t)physicalAddress >= ND_Memsize) return;
    uint8_t latched = (gEccLatch != NULL) ? gEccLatch[physicalAddress] : 0;
    // Fast path: this word is clean AND no live simulate bit armed. (Per-word, so latched
    // errors elsewhere don't penalise reads of clean words - the WALK-test hang fix.)
    if (live == 0 && latched == 0) return;
    if ((gECCR & (1 << 3)) != 0) return; // DisableECC gates DETECTION only

    if (GetPhysicalMemoryType((uint32_t)physicalAddress) != ND_MEM_LOCAL) return;

    // Fire on EITHER a live simulate bit (deterministic capture-on-read, TPE PAGING test 11)
    // OR the per-address latch from a prior local write (MEM / SINTRAN CONFIG probe).
    uint16_t eff = (uint16_t)(live | latched);
    int eccBits = 0;
    if ((eff & (1 << 0)) != 0) eccBits++;
    if ((eff & (1 << 1)) != 0) eccBits++;
    if ((eff & (1 << 4)) != 0) eccBits++;
    if (eccBits == 0) return;

    uint16_t tmpPEA = physicalAddress & 0xFFFF;
    uint16_t tmpPES = (physicalAddress >> 16) & 0xFF;
    uint16_t errorCode = 0;
    if (eccBits == 1)
    {
        // Single-bit = CORRECTABLE. Real ECC hardware silently corrects it and raises the
        // level-14 parity interrupt ONLY when ECCR bit 2 (EnableParityInterruptOnAllErrors)
        // is set. Without this gate a residual single-bit latch - e.g. one left behind by
        // TPE MEM's PARITY-ERROR-DETECTION subtest - fires an UNHANDLED parity interrupt on
        // the next read during the WALK test, and the guest WAITs at PIL 14 (deadlock).
        // Mirrors RetroCore CheckReadECCR's EnableParityInterruptOnAllErrrors gate
        // (Emulated.HW/ND/CPU/ND100/CpuND100.MMS.cs).
        if ((gECCR & (1 << 2)) == 0)
        {
            if (latched != 0) gEccLatch[physicalAddress] = 0; // consume/correct this word's latch
            return;                                            // corrected -> no interrupt
        }
        // Single bit, error table Figure 2.18, page 2-51 in ND-06.014.02
        if ((eff & (1 << 0)) != 0) errorCode = 3;
        if ((eff & (1 << 1)) != 0) errorCode = 0x1C;
        if ((eff & (1 << 4)) != 0) errorCode = 0x0D;
        tmpPES |= errorCode << 8;
    }
    else
    {
        tmpPES |= 1 << 13; // FATAL ERROR (multiple bits)
    }
    setPEA(tmpPEA);
    setPES(tmpPES);
    // Consume the per-address latch (the read corrects/clears that word's bad ECC).
    if (latched != 0) gEccLatch[physicalAddress] = 0;
    interrupt(14, 1 << 8); // PTY - MEMORY_PARITY_ERROR bit 8
}

// Read from physical memory
int ReadPhysicalMemory(int physicalAddress, bool privileged)
{
    if (physicalAddress < 0)
    {
        return 0x00;
    }

#ifdef WITH_DEBUGGER
    if (phys_watchpoint_count > 0
        && phys_watchpoint_page_armed((uint32_t)physicalAddress)
        && phys_watchpoint_check((uint32_t)physicalAddress, false)) {
        cpu_watchpoint_triggered((uint32_t)physicalAddress, false);
    }
#endif

    if (IsAddressShadowMemory(physicalAddress, privileged))
    {
        int tmp = PT_Read(physicalAddress);
        return tmp;
    }

    // Check memory bounds
    if (((uint32_t)physicalAddress >= ND_Memsize)||(physicalAddress < 0))
    {
        HandleMemoryOutOfRange(physicalAddress);
        return 0x00;
    }

    nd_ecc_read_detect(physicalAddress);
    return VolatileMemory.n_Array[physicalAddress];
}

// Wrapper for WritePhysicalMemoryWM to write a word (16 bits)
void WritePhysicalMemory(int physicalAddress, uint16_t value, bool privileged)
{
    WritePhysicalMemoryWM(physicalAddress, value,privileged, WRITEMODE_WORD);
}


// Write to physical memory (with writemode to handle MSB/LSB/WORD)
void WritePhysicalMemoryWM(int physicalAddress, uint16_t value, bool privileged, WriteMode wm)
{
#ifdef WITH_DEBUGGER
    if (phys_watchpoint_count > 0
        && phys_watchpoint_page_armed((uint32_t)physicalAddress)
        && phys_watchpoint_check((uint32_t)physicalAddress, true)) {
        cpu_watchpoint_triggered((uint32_t)physicalAddress, true);
    }
#endif

    if (IsAddressShadowMemory(physicalAddress, privileged))
    {
        switch (wm)
        {
        case WRITEMODE_MSB:
            {
                uint16_t cur = PT_Read(physicalAddress);
                PT_Write(physicalAddress, (cur & 0xFF) | (value << 8));
            }
            break;
        case WRITEMODE_LSB:
            {
                uint16_t cur = PT_Read(physicalAddress);
                PT_Write(physicalAddress, (cur & 0xFF00) | (value & 0xFF));
            }
            break;
        case WRITEMODE_WORD:
        default:
            PT_Write(physicalAddress, value);
            break;
        }
        return;
    }

    // Check memory bounds
    if (((uint32_t)physicalAddress >= ND_Memsize)||(physicalAddress < 0))
    {
        HandleMemoryOutOfRange(physicalAddress);
        return;
    }

    nd_ecc_write_latch(physicalAddress);

    uint16_t *p_phy_addr;
    p_phy_addr = &VolatileMemory.n_Array[physicalAddress];

	switch (wm)
	{
	case WRITEMODE_MSB: /* Even, which means MSB byte, or bits 15-8 */
		*p_phy_addr = (*p_phy_addr & 0xFF) | (value << 8);
		break;
	case WRITEMODE_LSB: /*Odd, which means LSB byte, or bits 7-0 */
		*p_phy_addr = (*p_phy_addr & 0xFF00) | (value & 0xFF);
		break;
    case WRITEMODE_WORD: // full word
	default:
		*p_phy_addr = value;
		break;
	}

/*

	ushort *p_phy_addr;
	p_phy_addr = &VolatileMemory.n_Array[physicalAddress];
	*p_phy_addr = value;
 */
}

// Handle memory out of range error
extern void ring_dump(void);

void HandleMemoryOutOfRange(uint32_t physicalAddress)
{
    // Uncomment ring_dump() to trace instructions leading to MOR
    // ring_dump();

    setPEA(physicalAddress & 0xFFFF);
    setPES((physicalAddress >> 16) & 0xFF);

    interrupt(14, 1 << 9); // Memory out of range
}

/// @brief Handle memory protection violation. Will TRAP the instruction
/// @param virtualAddress
void HandleMPV(uint32_t virtualAddress)
{
    if (ND100X_HOT_TRACE && Log_IsEnabled(LOG_CAT_MMS, LOG_TRACE))
    {
    static int mpv_count = 0;
    uint32_t VPN = (virtualAddress >> 10) & 0x3F;
    if (mpv_count < 5) {
        /* Also dump the PTE that was used */
        uint16_t pcr = gReg->reg_PCR[CurrLEVEL];
        int useAPT = STS_PTM; /* data access uses APT when PTM=1 */
        uint32_t pt;
        if (useAPT) {
            pt = ((pcr & (1<<2)) && (mmsType == MMS2)) ? (pcr >> 7) & 0xF : (pcr >> 7) & 0x3;
        } else {
            pt = ((pcr & (1<<2)) && (mmsType == MMS2)) ? (pcr >> 11) & 0xF : (pcr >> 9) & 0x3;
        }
        uint32_t pte = GetPageTableEntry(pt, VPN, Sixteen);
        Log_Write(LOG_CAT_MMS, LOG_TRACE, "HandleMPV: VA=%06o VPN=%d PT=%d PTe=0x%08X PIL=%d PC=%06o",
                  virtualAddress, VPN, pt, pte, CurrLEVEL, gPC);
    }
    mpv_count++;
    }
    interrupt(14, 1 << 2);
}

/// @brief Handle page fault. Will TRAP the instruction
/// @param virtualAddress
void HandlePF(uint32_t virtualAddress)
{
    (void)virtualAddress;
    interrupt(14, 1 << 3); // PF - PAGE_FAULT bit 3
}

// ---------------------------------------------------------------------------
// Debugger-only physical memory accessors.
//
// These bypass the MMU, watchpoint hooks, and protection traps so that the
// DAP debugger can inspect/modify any 22-bit physical address (e.g. kernel
// data above 64K in a split I/D 0411 binary) without disturbing CPU state.
//
// IMPORTANT: these are NOT called from the CPU hot path - only from
// debugger.c command handlers when WITH_DEBUGGER is enabled. The runtime
// memory access path (ReadPhysicalMemory / WritePhysicalMemoryWM) is
// unchanged, so when no watchpoints are active there is zero added cost
// per memory access.
// ---------------------------------------------------------------------------
int Dbg_ReadPhysicalMemory(uint32_t physicalAddress)
{
    if (physicalAddress >= (uint32_t)ND_Memsize)
        return -1;
    return (int)VolatileMemory.n_Array[physicalAddress];
}

int Dbg_WritePhysicalMemory(uint32_t physicalAddress, uint16_t value)
{
    if (physicalAddress >= (uint32_t)ND_Memsize)
        return -1;
    VolatileMemory.n_Array[physicalAddress] = value;
    return 0;
}

// ---------------------------------------------------------------------------
// Debugger-only I-space / D-space virtual memory reads.
//
// These explicitly select the instruction (PT) or data (APT) page table
// from the current level's PCR, bypassing the STS_PTM + UseAPT logic in
// mapVirtualToPhysical.  No traps are generated and PGU/WIP bits are not
// modified, so these are safe to call from debugger command handlers
// without disturbing CPU state.
//
// This fixes the split I/D (PTM=1) debugger bug where disassembly of
// overlay code showed D-space garbage instead of I-space instructions.
// ---------------------------------------------------------------------------
static int Dbg_MapVirtualToPhysical(uint32_t virtualAddress, bool useAPT, int8_t pil)
{
    virtualAddress &= 0xFFFF;

    if (!g_paging_tables.isInitialized)
        return -1;

    int level = (pil >= 0 && pil <= 15) ? pil : CurrLEVEL;
    uint16_t pcr = gReg->reg_PCR[level];
    uint8_t ring = pcr & 0x03;

    // Ring 3 shadow RAM access (same check as mapVirtualToPhysical)
    if ((ring == 3) && (IsAddressShadowMemory(virtualAddress, false)))
        return (int)virtualAddress;

    // No paging = identity map
    if (!STS_PONI)
        return (int)(virtualAddress & 0xFFFF);

    uint32_t DIP = virtualAddress & 0x3FF;
    uint32_t VPN = (virtualAddress >> 10) & 0x3F;
    uint32_t pageTable;
    PageTableMode ptm;

    if (useAPT) {
        // D-space: APT field
        if ((pcr & (1 << 2)) != 0 && (mmsType == MMS2)) {
            pageTable = (pcr >> 7) & 0xF;
            ptm = Sixteen;
        } else {
            pageTable = (pcr >> 7) & 0x03;
            ptm = Four;
        }
    } else {
        // I-space: PT field
        if ((pcr & (1 << 2)) != 0 && (mmsType == MMS2)) {
            pageTable = (pcr >> 11) & 0xF;
            ptm = Sixteen;
        } else {
            pageTable = (pcr >> 9) & 0x03;
            ptm = Four;
        }
    }

    uint32_t pageTableEntry = GetPageTableEntry(pageTable, VPN, ptm);

    // Check if page is present (any permission bit set)
    if ((pageTableEntry & (7L << 29)) == 0)
        return -1;

    uint16_t PPN;
    if (STS_SEXI)
        PPN = (uint16_t)(pageTableEntry & 0x3FFF);
    else
        PPN = (uint16_t)(pageTableEntry & 0x1FF);

    int physicalAddress = ((PPN << 10) | DIP) & 0xFFFFFF;

    if ((uint32_t)physicalAddress >= ND_Memsize)
        return -1;

    return physicalAddress;
}

// PIL-aware variants: pil=-1 means use CurrLEVEL (default)
int Dbg_ReadVirtualMemoryISpace_PIL(uint32_t virtualAddress, int8_t pil)
{
    int pa = Dbg_MapVirtualToPhysical(virtualAddress, false, pil);
    if (pa < 0) return -1;
    return Dbg_ReadPhysicalMemory((uint32_t)pa);
}

int Dbg_ReadVirtualMemoryDSpace_PIL(uint32_t virtualAddress, int8_t pil)
{
    int pa = Dbg_MapVirtualToPhysical(virtualAddress, true, pil);
    if (pa < 0) return -1;
    return Dbg_ReadPhysicalMemory((uint32_t)pa);
}

int Dbg_WriteVirtualMemoryISpace_PIL(uint32_t virtualAddress, uint16_t value, int8_t pil)
{
    int pa = Dbg_MapVirtualToPhysical(virtualAddress, false, pil);
    if (pa < 0) return -1;
    return Dbg_WritePhysicalMemory((uint32_t)pa, value);
}

int Dbg_WriteVirtualMemoryDSpace_PIL(uint32_t virtualAddress, uint16_t value, int8_t pil)
{
    int pa = Dbg_MapVirtualToPhysical(virtualAddress, true, pil);
    if (pa < 0) return -1;
    return Dbg_WritePhysicalMemory((uint32_t)pa, value);
}

// Backward-compatible wrappers (use current PIL)
int Dbg_ReadVirtualMemoryISpace(uint32_t virtualAddress)
{
    return Dbg_ReadVirtualMemoryISpace_PIL(virtualAddress, -1);
}

int Dbg_ReadVirtualMemoryDSpace(uint32_t virtualAddress)
{
    return Dbg_ReadVirtualMemoryDSpace_PIL(virtualAddress, -1);
}

int Dbg_WriteVirtualMemoryISpace(uint32_t virtualAddress, uint16_t value)
{
    return Dbg_WriteVirtualMemoryISpace_PIL(virtualAddress, value, -1);
}

int Dbg_WriteVirtualMemoryDSpace(uint32_t virtualAddress, uint16_t value)
{
    return Dbg_WriteVirtualMemoryDSpace_PIL(virtualAddress, value, -1);
}

// Check if privileged instruction execution is allowed

