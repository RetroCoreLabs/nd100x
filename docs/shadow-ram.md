# ND-100 Shadow RAM -- Architecture and Implementation

## Overview

Shadow RAM is a dedicated hardware memory chip on the ND-100 CPU board that stores
**page table entries (PTEs)**. It occupies the same physical address range as regular
RAM (top of the first 64K words), but is a **separate, parallel memory** -- the CPU
routes accesses to one or the other based on ring level, paging state, and access type.

This document describes the real hardware behavior and how the nd100x emulator
implements it.

---

## Physical Address Ranges

The shadow RAM address range depends on the Memory Management System type and the
current operating mode:

| Configuration            | SEXI | Address Range       | Octal Range       | Size (words) | Page Tables |
|--------------------------|------|---------------------|--------------------|--------------|-------------|
| Normal mode (4 PT)       | 0    | 0xFF00 -- 0xFFFF    | 177400 -- 177777  | 256          | 4           |
| Extended mode, MMS1 (4 PT)  | 1 | 0xFE00 -- 0xFFFF    | 177000 -- 177777  | 512          | 4           |
| Extended mode, MMS2 (16 PT) | 1 | 0xF800 -- 0xFFFF    | 174000 -- 177777  | 2048         | 16          |

Constants defined in `src/cpu/cpu_types.h`:

```c
#define SHADOW_RAM_NORMAL_MODE_4PT   0xFF00   // 177400 octal
#define SHADOW_RAM_EXTENDED_MODE_4PT 0xFE00   // 177000 octal
#define SHADOW_RAM_EXTENDED_MODE_16PT 0xF800  // 174000 octal
```

**Key point:** Regular RAM exists at these same physical addresses. The CPU has two
separate memories mapped to the same address range -- the routing logic determines
which one is accessed. They never shadow each other; a write to shadow RAM does not
touch regular RAM, and vice versa.

---

## Access Routing Logic

### The Gate Condition

All physical memory reads and writes pass through `IsAddressShadowMemory()` in
`src/cpu/cpu_mms.c:694`. This function returns `true` (route to shadow RAM) or
`false` (route to regular RAM):

```c
bool IsAddressShadowMemory(uint addr, bool privileged)
{
    if (g_dma_access)        return false;   // DMA always hits regular RAM
    if (addr > 0xFFFF)     return false;   // Shadow RAM only in first 64K

    ushort pcr = g_reg->reg_PCR[CurrLEVEL];
    unsigned char ring = pcr & 0x03;
    bool mms2Enabled = ((pcr & 1 << 2) != 0);

    if ((ring == 3) || (!STS_PONI) || privileged)
    {
        // Check address against current shadow RAM range
        // (depends on STS_SEXI, g_mms_type, mms2Enabled)
        ...
        return true;   // if address is in range
    }

    return false;       // not shadow RAM
}
```

### The Three Gate Conditions

The gate opens when **any one** of these is true:

| Condition      | Meaning                                         |
|----------------|--------------------------------------------------|
| `ring == 3`    | CPU is executing in ring 3 (kernel/most privileged) |
| `!STS_PONI`    | Paging is OFF (memory management disabled)       |
| `privileged`   | Caller is a privileged instruction (STDTX, EXAM, DEPO, etc.) |

### Ring-Level Behavior Matrix

#### When `privileged=true` (STDTX, EXAM, DEPO, MOVEW physical modes)

These instructions pass `privileged=true` to the physical memory functions. The
`privileged` flag unconditionally opens the gate, so the ring check is irrelevant.
However, all these instructions enforce ring 3 via `CheckPriv()` before execution,
so they can only run in ring 3 anyway.

| Ring | Can execute? | Hits shadow RAM? |
|------|-------------|------------------|
| 3    | Yes         | **Yes** -- always |
| 2    | No (privilege violation trap) | N/A |
| 1    | No (privilege violation trap) | N/A |
| 0    | No (privilege violation trap) | N/A |

#### When `privileged=false` (Normal virtual memory access after MMU translation)

This is the critical case. When a normal load or store translates through the MMU
and the resulting physical address lands in the shadow RAM range:

| Ring | Paging ON (PONI=1)          | Paging OFF (PONI=0)         |
|------|-----------------------------|-----------------------------|
| **3** | **Shadow RAM** (ring==3 gate) | **Shadow RAM** (!PONI gate) |
| **2** | **Regular RAM** (gate closed) | **Shadow RAM** (!PONI gate) |
| **1** | **Regular RAM** (gate closed) | **Shadow RAM** (!PONI gate) |
| **0** | **Regular RAM** (gate closed) | **Shadow RAM** (!PONI gate) |

**This is the hardware protection mechanism for page tables:**

- **Ring 3 (kernel)** always sees shadow RAM at these addresses, whether through
  privileged instructions or normal memory access. The kernel can read/write PTEs
  using ordinary load/store instructions to virtual addresses that map to the shadow
  range.

- **Rings 0--2 (user programs)** with paging enabled can never reach shadow RAM.
  Even if a user program's page table maps a virtual address to physical 0xF800,
  the access goes to regular RAM. User code cannot read or modify page tables.

- **Paging OFF** opens shadow RAM to all rings. This is the boot/diagnostic state
  before the OS enables paging.

#### DMA Transfers (Device I/O)

DMA bypasses shadow RAM entirely. When `g_dma_access=true`, `IsAddressShadowMemory()`
always returns `false`, routing the access to regular RAM. This matches real hardware
where the DMA controller is on the memory bus, not the CPU's internal shadow RAM bus.

```c
// src/devices/device.c
void Device_DMAWrite(uint32_t coreAddress, uint16_t data) {
    g_dma_access = true;
    WritePhysicalMemory(coreAddress & 0xFFFFFF, data, false);
    g_dma_access = false;
}
```

---

## Shadow RAM Data Structure

### PagingTables Structure

Defined in `src/cpu/cpu_types.h`:

```c
typedef struct {
    MMSType   mmsType;          // MMS1 or MMS2
    ushort   *shadowRam;        // The shadow RAM chip (dynamically allocated)
    uint      shadowRamAddress; // Start address of shadow RAM range
    uint16_t  shadowRamSize;    // Size in words
    bool      isInitialized;
} PagingTables;
```

### Initialization

In `src/cpu/cpu_mms.c:35`, `CreatePagingTables()` allocates shadow RAM:

```c
if (g_mms_type == MMS1) {
    pt.shadowRamSize = 512;     // 4 page tables x 64 entries x 2 words
    pt.shadowRamAddress = SHADOW_RAM_EXTENDED_MODE_4PT;
} else {
    pt.shadowRamSize = 2048;    // 16 page tables x 64 entries x 2 words
    pt.shadowRamAddress = SHADOW_RAM_EXTENDED_MODE_16PT;
}
pt.shadowRam = (ushort*)calloc(pt.shadowRamSize, sizeof(ushort));
```

Shadow RAM is zeroed on allocation (`calloc`). All PTEs start as zero, meaning no
permissions set -- any access triggers a page fault until the kernel populates the
page tables.

### Read/Write Primitives

`PT_Read()` and `PT_Write()` in `src/cpu/cpu_mms.c:153-224` provide the lowest-level
access:

```c
void PT_Write(uint address, ushort value)
{
    if (!pt.shadowRam) return;
    if ((address < pt.shadowRamAddress) || (address > 0xFFFF)) return;
    uint offset = address - pt.shadowRamAddress;
    pt.shadowRam[offset] = value;
}

ushort PT_Read(uint address)
{
    if (!pt.shadowRam) return 0;
    if ((address < pt.shadowRamAddress) || (address > 0xFFFF)) return 0;
    uint offset = address - pt.shadowRamAddress;
    return pt.shadowRam[offset];
}
```

The offset is simply `address - shadowRamAddress`. For MMS2 extended mode, address
0xF800 maps to offset 0, address 0xFFFF maps to offset 2047.

---

## Shadow RAM Address Layout

### Address Calculation

Each page table has 64 entries (one per Virtual Page Number). The shadow RAM address
for a given PTE is calculated by `GetPTShadowAddress()` in `src/cpu/cpu_mms.c:118`:

```
base_offset = (pageTable << 6) | VPN
```

In extended mode (SEXI=1), each PTE occupies 2 words (32-bit), so the offset is
doubled:

```
extended_offset = base_offset << 1
```

The final shadow RAM address adds the range base:

| Mode                | Formula                                          |
|---------------------|--------------------------------------------------|
| Normal (4PT, 16-bit PTE) | `0xFF00 + (PT << 6) + VPN`                 |
| Extended MMS1 (4PT, 32-bit PTE) | `0xFE00 + ((PT << 6) + VPN) << 1`  |
| Extended MMS2 (16PT, 32-bit PTE) | `0xF800 + ((PT << 6) + VPN) << 1` |

### Memory Map Example (MMS2, Extended Mode)

```
0xF800  PT0, VPN0  (high word)
0xF801  PT0, VPN0  (low word)
0xF802  PT0, VPN1  (high word)
0xF803  PT0, VPN1  (low word)
  ...
0xF87E  PT0, VPN63 (high word)
0xF87F  PT0, VPN63 (low word)
0xF880  PT1, VPN0  (high word)
  ...
0xFFFF  PT15, VPN63 (low word)
```

Total: 16 page tables x 64 entries x 2 words = 2048 words.

---

## Page Table Entry Formats

### 32-bit PTE (Extended Mode)

Used in MMS2 and MMS1 extended mode. Stored as two consecutive 16-bit words in
shadow RAM (high word first):

```
Bit 31:  WPM   -- Write Permit
Bit 30:  RPM   -- Read Permit
Bit 29:  FPM   -- Fetch (execute) Permit
Bit 28:  WIP   -- Written In Page (dirty bit)
Bit 27:  PGU   -- Page Used (accessed bit)
Bits 26-25: Ring -- Minimum ring required for access (0-3)
Bits 24-14: (reserved)
Bits 13-0:  PPN  -- Physical Page Number (14-bit, addresses 16M words)
```

Flag constants in `src/cpu/cpu_types.h`:

```c
#define PGU_FLAG (1 << 27)   // Page Used
#define WIP_FLAG (1 << 28)   // Written In Page
```

Permission bits from `src/cpu/cpu_mms.c:631-633`:

```c
if (am & READ)  accessBits |= 1L << 30;  // RPM
if (am & WRITE) accessBits |= 1L << 31;  // WPM
if (am & FETCH) accessBits |= 1L << 29;  // FPM
```

### 16-bit PTE (Normal Mode, MMS1 only)

Used when SEXI=0 on MMS1 hardware. One word per entry:

```
Bits 15-9:  Flags -- WPM, RPM, FPM, WIP, PGU, Ring[1:0]
Bits 8-0:   PPN   -- Physical Page Number (9-bit, addresses 512 pages = 512K words)
```

Conversion functions in `src/cpu/cpu_mms.c:66-76` translate between 16-bit storage
and the internal 32-bit format:

```c
static uint ConvertFrom16BitPTE(ushort value)
{
    // Bits 15:9 -> bits 31:25, bits 8:0 -> bits 8:0
    return (uint)((value & 0xFE00) << 16 | (value & 0x01FF));
}

static ushort ConvertTo16BitPTE(uint pageTableEntry)
{
    // Bits 31:25 -> bits 15:9, bits 8:0 -> bits 8:0
    return (ushort)(((pageTableEntry & 0xFE000000) >> 16) | (pageTableEntry & 0x000001FF));
}
```

### MMS1 vs MMS2 Comparison

| Feature              | MMS1                    | MMS2                     |
|----------------------|-------------------------|--------------------------|
| Page Tables          | 4 (PT 0-3)             | 16 (PT 0-15)            |
| PPN Width            | 9 bits                  | 14 bits                  |
| Max Physical Memory  | 512 pages = 512K words  | 16384 pages = 16M words  |
| PTE Storage          | 16-bit (normal), 32-bit (extended) | 32-bit always |
| Shadow RAM Size      | 256-512 words           | 2048 words               |
| Required for VSX     | No                      | Yes                      |

---

## Virtual Address Translation

### Translation Flow

`mapVirtualToPhysical()` in `src/cpu/cpu_mms.c:396` performs the full translation:

1. **Ring 3 shadow intercept** (line 413): If ring==3 and the virtual address falls
   in the shadow range, return it directly as a physical address (identity mapping).
   This allows the kernel to access PTEs via ordinary load/store.

2. **Paging OFF check** (line 420): If `!STS_PONI`, return `virtualAddress & 0xFFFF`
   (identity mapping, no translation).

3. **Extract VA components**:
   - `DIP` = bits 9:0 (displacement within page, 1024 words)
   - `VPN` = bits 15:10 (virtual page number, 64 pages)

4. **Select page table** from PCR register:
   - PTM bit selects between normal PT and alternative PT (APT/D-space)
   - MMS2 bit selects 4-table or 16-table indexing

5. **Read PTE** from shadow RAM via `GetPageTableEntry()`

6. **Check permissions**: FPM/RPM/WPM bits vs access mode. If no permission bits
   are set at all, the page is not mapped (page fault). If the specific access bit
   is missing, it's a permit violation (MPV).

7. **Check ring**: PTE ring field must be <= current ring. A ring-2 page cannot be
   accessed from ring 1 or 0.

8. **Extract PPN** and compute physical address: `(PPN << 10) | DIP`

9. **Update PGU/WIP flags** in the PTE (hardware-maintained dirty/accessed bits).

### Ring 3 Shadow Intercept Detail

```c
// src/cpu/cpu_mms.c:413-417
if ((ring == 3) && (IsAddressShadowMemory(virtualAddress, false)))
{
    return (int)virtualAddress;  // Identity-mapped
}
```

When the kernel (ring 3) accesses a virtual address in the range 0xF800-0xFFFF
(or 0xFE00/0xFF00 depending on mode), the MMU skips translation entirely and returns
the address as-is. The subsequent `ReadPhysicalMemory` / `WritePhysicalMemory` call
then routes to shadow RAM because ring==3 opens the gate.

This is how SINTRAN reads and writes page tables without using privileged instructions
like STDTX for every PTE access -- it can use ordinary memory instructions at
addresses in the shadow range.

---

## Instructions That Access Shadow RAM

### Privileged Instructions (Ring 3 Only)

All of these pass `privileged=true` and enforce ring 3 via `CheckPriv()`:

| Instruction | Opcode     | Operation                                    |
|-------------|------------|----------------------------------------------|
| STDTX       | 0143306    | Store A:D double-word to physical T:X+disp   |
| STATX       | 0143304    | Store A to physical T:X+disp                 |
| STZTX       | 0143305    | Store zero to physical T:X+disp              |
| LDATX       | 0143300    | Load A from physical T:X+disp                |
| LDXTX       | 0143301    | Load X from physical T:X+disp                |
| LDDTX       | 0143302    | Load D from physical T:X+disp                |
| LDBTX       | 0143303    | Load B from physical T:X+disp                |
| EXAM        | 0150416    | Load T from physical A:D                     |
| DEPO        | 0150417    | Store T to physical A:D                       |
| MOVEW       | 0143100+d  | Block move (modes 2,5-8 use physical addressing) |

These instructions always hit shadow RAM when the target address is in the shadow
range. They are the primary mechanism for the kernel to manipulate page tables,
especially STDTX which writes a full 32-bit PTE (two words) in a single instruction.

### Normal Memory Instructions (Ring-Dependent)

Any load/store instruction that goes through virtual address translation:

- **Ring 3**: Accesses to virtual addresses in the shadow range are intercepted
  and routed to shadow RAM (identity-mapped).
- **Rings 0-2 with paging ON**: Never reach shadow RAM. If the MMU maps a virtual
  address to a physical address in the shadow range, the access hits regular RAM.
- **Rings 0-2 with paging OFF**: Identity-mapped, and `!STS_PONI` opens the shadow
  gate. Shadow RAM is accessible.

---

## MOVEW and Page Table Manipulation

The MOVEW instruction (`src/cpu/cpu_instr.c:2452`) supports 9 displacement modes
for block transfers between virtual and physical memory:

| Disp | Source          | Destination     | Shadow RAM interaction                  |
|------|-----------------|-----------------|------------------------------------------|
| 0    | PT (virtual)    | PT (virtual)    | Via MMU (ring-dependent)                 |
| 1    | PT (virtual)    | APT (virtual)   | Via MMU (ring-dependent)                 |
| 2    | PT (virtual)    | Physical        | Dest hits shadow if in range (privileged)|
| 3    | APT (virtual)   | PT (virtual)    | Via MMU (ring-dependent)                 |
| 4    | APT (virtual)   | APT (virtual)   | Via MMU (ring-dependent)                 |
| 5    | APT (virtual)   | Physical        | Dest hits shadow if in range (privileged)|
| 6    | Physical        | PT (virtual)    | Src reads shadow if in range (privileged)|
| 7    | Physical        | APT (virtual)   | Src reads shadow if in range (privileged)|
| 8    | Physical        | Physical        | Both can hit shadow if in range (privileged)|

PT = Page Table (I-space), APT = Alternative Page Table (D-space for split I/D).

Modes 2 and 5-8 require ring 3 (`CheckPriv()`). They pass `privileged=true` to the
physical memory functions, so shadow RAM routing is guaranteed when addresses are in
range.

MOVEW is commonly used by the kernel to bulk-load page tables -- for example,
copying a process's PTE block from regular memory into shadow RAM during a context
switch.

---

## STS Register Flags

Three bits in the STS register control shadow RAM behavior:

### STS_PONI (bit 14) -- Paging ON Indicator

```c
#define STS_PONI ((g_reg->reg_STS >> 14) & 0x01)
```

- **1**: Memory management is active. Virtual addresses are translated through page
  tables. Shadow RAM is protected from rings 0-2.
- **0**: Memory management is off. All addresses are identity-mapped (virtual =
  physical). Shadow RAM is accessible from all rings.

When `!STS_PONI`, `mapVirtualToPhysical()` returns the virtual address unchanged,
and `IsAddressShadowMemory()` opens the gate regardless of ring.

### STS_SEXI (bit 13) -- Extended MMS Addressing

```c
#define STS_SEXI ((g_reg->reg_STS >> 13) & 0x01)
```

- **1**: Extended mode. 24-bit physical addresses, 32-bit PTEs. Shadow range
  depends on MMS type (0xFE00 for MMS1, 0xF800 for MMS2).
- **0**: Normal mode. 19-bit physical addresses, 16-bit PTEs. Shadow range is
  always 0xFF00-0xFFFF.

### STS_PTM -- Page Table Mode

Controls whether the CPU uses the normal page table or the alternative page table
(APT) for data accesses. This enables split instruction/data spaces (I-space and
D-space), where code and data can be mapped to different physical pages even though
they share the same virtual address range.

---

## Debugger Access

The debugger needs to read page tables regardless of current CPU state. Two separate
access paths exist:

### GetPageTableEntry (Runtime)

Used during normal CPU execution (`src/cpu/cpu_mms.c:227`). Respects `STS_SEXI` to
determine PTE format, which is correct because it reflects the current execution
context.

### GetPageTableEntryForDebugger (Debug-Only)

Used by the DAP debugger (`src/cpu/cpu_mms.c:265`). **Bypasses `STS_SEXI`** and uses
the hardware `g_mms_type` instead:

```c
uint GetPageTableEntryForDebugger(uint pageTable, uint VPN, PageTableMode ptm)
{
    if (g_mms_type == MMS2) {
        // Always use 32-bit format, 16PT layout
        ...
    } else {
        // Always use 16-bit format, 4PT layout
        ...
    }
}
```

This is necessary because `STS_SEXI` reflects the current interrupt level's state,
not the hardware capability. On MMS2 hardware, shadow RAM physically stores 32-bit
PTEs in 16-table format regardless of what SEXI says at the current priority level.
The debugger must read the raw hardware format.

---

## Common Questions

### Does STDTX to physical 0xF800 hit shadow RAM or regular RAM?

**Shadow RAM only.** STDTX passes `privileged=true`, which unconditionally opens the
shadow gate. The write goes to `PT_Write()` and returns before reaching the regular
RAM path. Regular RAM at 0xF800 is untouched.

### Does EXAM from 0xF800 read shadow RAM or regular RAM?

**Shadow RAM only.** Same logic -- EXAM passes `privileged=true`, so
`IsAddressShadowMemory()` returns true, and `PT_Read()` is called. The
`g_volatile_memory` array is never consulted.

### Is there an instruction that writes ONLY to shadow RAM?

There is no dedicated "shadow RAM only" instruction. The routing is automatic and
address-based inside `WritePhysicalMemoryWM()` / `ReadPhysicalMemory()`. Any
instruction that writes to a physical address in the shadow range (with the gate
open) writes to shadow RAM and **only** shadow RAM. The access is always either/or,
never both.

### Can user programs read page tables?

**No**, as long as paging is enabled. When PONI=1 and the ring is 0-2, the shadow
gate is closed. Physical addresses in the shadow range hit regular RAM instead.
Even if a clever page table configuration maps a virtual address to physical 0xF800,
the user program reads whatever is in regular RAM at that address, not the PTE.

### What about DMA to the shadow range?

**DMA always hits regular RAM.** The `g_dma_access` flag forces
`IsAddressShadowMemory()` to return false. This matches the real hardware where the
DMA controller operates on the memory bus, which connects to physical RAM chips. The
shadow RAM chip is internal to the CPU and not bus-accessible.

### Can the kernel access PTEs without STDTX?

**Yes.** In ring 3, normal load/store instructions to virtual addresses 0xF800-0xFFFF
(in MMS2 extended mode) are intercepted by `mapVirtualToPhysical()` and identity-
mapped to the same physical addresses. The subsequent physical memory access sees
ring==3, opens the shadow gate, and routes to shadow RAM. STDTX is not the only way
-- it's just the most explicit.

---

## Implementation Files

| File                          | Contents                                      |
|-------------------------------|-----------------------------------------------|
| `src/cpu/cpu_types.h`         | Shadow RAM constants, PagingTables struct, STS macros, flag definitions |
| `src/cpu/cpu_mms.c`           | All shadow RAM logic: PT_Read/PT_Write, IsAddressShadowMemory, address translation, PTE format conversion, debugger access |
| `src/cpu/cpu_instr.c`         | Privileged instruction implementations (STDTX, EXAM, DEPO, MOVEW) |
| `src/devices/device.c`        | DMA read/write with g_dma_access bypass          |
