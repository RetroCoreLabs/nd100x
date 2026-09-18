/*
 * nd100x - ND100 Virtual Machine
 *
 *
 * This file is originated from the nd100x project.
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

#include <string.h>

#include "cpu_types.h"
#include "cpu_protos.h"
#include "../ndlib/log.h"
#include "expr_eval.h"


BreakpointManager *g_breakpoint_mgr;

// PC-breakpoint hot-path gates (mirror the watchpoint design).
int g_breakpoint_entry_count = 0;     // live breakpoint entries (any type)
int g_breakpoint_step_pending = 0;    // nonzero while a single-step is in flight
uint8_t g_breakpoint_bitmap[8192];    // 1 bit per 16-bit PC address

/// @brief Rebuild the PC-breakpoint bitmap from the live entries
static void breakpoint_bitmap_rebuild(void)
{
    memset(g_breakpoint_bitmap, 0, sizeof(g_breakpoint_bitmap));
    g_breakpoint_entry_count = 0;
    if (!g_breakpoint_mgr) return;
    for (int h = 0; h < HASH_SIZE; h++) {
        for (BreakpointEntry *curr = g_breakpoint_mgr->buckets[h]; curr; curr = curr->next) {
            g_breakpoint_bitmap[curr->address >> 3] |= (1 << (curr->address & 7));
            g_breakpoint_entry_count++;
        }
    }
}

/// @brief Hash function for the breakpoint manager
/// @param address Memory address to hash
/// @return Hash value
static int hash_address(uint16_t address)
{
    return address % HASH_SIZE;
}

/// @brief Initialize the breakpoint manager
/// @return void
/// @note Initialize the breakpoint manager
void breakpoint_manager_init(void)
{
    /* Static storage: one manager for the process lifetime, so nothing can
     * fail here and cleanup has nothing to free. */
    static BreakpointManager s_mgr;
    g_breakpoint_mgr = &s_mgr;

    g_breakpoint_mgr->step_count = 0;
    memset(g_breakpoint_mgr->buckets, 0, sizeof(g_breakpoint_mgr->buckets));
}

/// @brief Cleanup the breakpoint manager
/// @return void
/// @note Clean up and free the breakpoint manager memory
void breakpoint_manager_cleanup(void)
{
    if (g_breakpoint_mgr) {
        breakpoint_manager_clear();
        g_breakpoint_mgr = NULL;   /* the lazy "if (mgr == NULL) init" callers re-create it */
    }
}

/// @brief Set the step count to 1
/// @return void
/// @note Used by the debugger to single step
void breakpoint_manager_step_one(void)
{
    g_breakpoint_mgr->step_count=1;
    g_breakpoint_step_pending = 1;
}

/// @brief Add breakpoint (create new entry or append to list)
/// @param address Memory address to add breakpoint to
/// @param type Breakpoint type (user, function, data, instruction, temporary)
/// @param condition Condition expression (optional - not yet supported)
/// @param hitCondition Hit condition expression (optional)
/// @param logMessage Log message (optional)
void breakpoint_manager_add(uint16_t address, BreakpointType type, const char *condition, const char *hitCondition, const char *logMessage)
{

    if (g_breakpoint_mgr == NULL) {
        breakpoint_manager_init();
    }

    int h = hash_address(address);

    // Prevent adding duplicate temporary breakpoint at address
    if (type == BP_TYPE_TEMPORARY) {
    BreakpointEntry* curr = g_breakpoint_mgr->buckets[h];
    while (curr) {
        if (curr->address == address && curr->type == BP_TYPE_TEMPORARY) {
            LOG(LOG_CAT_DAP, LOG_DEBUG, "Temporary breakpoint already exists at %04X", address);
            return; // skip adding
        }
        curr = curr->next;
    }
    }

    BreakpointEntry *entry = (BreakpointEntry *)malloc(sizeof(BreakpointEntry));
    entry->address = address;
    entry->type = type;
    entry->condition = condition ? strdup(condition) : NULL;
    entry->hitCondition = hitCondition ? strdup(hitCondition) : NULL;
    entry->logMessage = logMessage ? strdup(logMessage) : NULL;
    entry->hitCount = 0;
    entry->next = g_breakpoint_mgr->buckets[h];

    g_breakpoint_mgr->buckets[h] = entry;

    g_breakpoint_bitmap[address >> 3] |= (1 << (address & 7));
    g_breakpoint_entry_count++;
}

/// @brief Remove entries at address matching type (or all if type == -1)
/// @param address Memory address to remove breakpoints from
/// @param type Breakpoint type to remove (or all if type == -1)
void breakpoint_manager_remove(uint16_t address, int type)
{
    int h = hash_address(address);
    BreakpointEntry *prev = NULL;
    BreakpointEntry *curr = g_breakpoint_mgr->buckets[h];

    while (curr)
    {
        if (curr->address == address && (type == -1 || (int)curr->type == type))
        {
            BreakpointEntry *to_delete = curr;
            if (prev)
                prev->next = curr->next;
            else
                g_breakpoint_mgr->buckets[h] = curr->next;

            free(to_delete->condition);
            free(to_delete->hitCondition);
            free(to_delete->logMessage);
            free(to_delete);
            curr = (prev) ? prev->next : g_breakpoint_mgr->buckets[h];
        }
        else
        {
            prev = curr;
            curr = curr->next;
        }
    }
    breakpoint_bitmap_rebuild();
}

/// @brief Clear all breakpoints
/// @return void
/// @note Clear all breakpoints
void breakpoint_manager_clear(void)
{
    for (int h = 0; h < HASH_SIZE; h++)
    {
        BreakpointEntry *curr = g_breakpoint_mgr->buckets[h];
        while (curr)
        {
            BreakpointEntry *next = curr->next;
            free(curr->condition);
            free(curr->hitCondition);
            free(curr->logMessage);
            free(curr);
            curr = next;
        }
        g_breakpoint_mgr->buckets[h] = NULL;
    }
    memset(g_breakpoint_bitmap, 0, sizeof(g_breakpoint_bitmap));
    g_breakpoint_entry_count = 0;
}

/// @brief Clear only breakpoints of a specific type
/// @param type Breakpoint type to clear (BP_TYPE_USER, BP_TYPE_FUNCTION, etc.)
void breakpoint_manager_clear_type(BreakpointType type)
{
    if (!g_breakpoint_mgr) return;

    for (int h = 0; h < HASH_SIZE; h++)
    {
        BreakpointEntry *prev = NULL;
        BreakpointEntry *curr = g_breakpoint_mgr->buckets[h];
        while (curr)
        {
            BreakpointEntry *next = curr->next;
            if (curr->type == type)
            {
                if (prev)
                    prev->next = next;
                else
                    g_breakpoint_mgr->buckets[h] = next;
                free(curr->condition);
                free(curr->hitCondition);
                free(curr->logMessage);
                free(curr);
            }
            else
            {
                prev = curr;
            }
            curr = next;
        }
    }
    breakpoint_bitmap_rebuild();
}

/// @brief Query breakpoints at address
/// @param address Memory address to query breakpoints from
/// @param matches Array to store matching breakpoints
/// @param matchCount Number of matching breakpoints
/// @return Number of matching entries
int breakpoint_manager_check( uint16_t address, BreakpointEntry** matches[], int* matchCount) {
    int h = hash_address(address);
    BreakpointEntry* curr = g_breakpoint_mgr->buckets[h];

    BreakpointEntry* tempList[10];
    int tempCount = 0;
    BreakpointEntry* userList[10];
    int userCount = 0;

    while (curr) {
        if (curr->address == address) {
            if (curr->type == BP_TYPE_TEMPORARY) {
                tempList[tempCount++] = curr;
                if (tempCount >= 10) break;
            } else {
                userList[userCount++] = curr;
                if (userCount >= 10) break;
            }
        }
        curr = curr->next;
    }

    if (tempCount > 0) {
        *matches = malloc(sizeof(BreakpointEntry*) * tempCount);
        if (!*matches) {
            *matchCount = 0;
            return 0;
        }
        memcpy(*matches, tempList, sizeof(BreakpointEntry*) * tempCount);
        *matchCount = tempCount;
    } else if (userCount > 0) {
        *matches = malloc(sizeof(BreakpointEntry*) * userCount);
        if (!*matches) {
            *matchCount = 0;
            return 0;
        }
        memcpy(*matches, userList, sizeof(BreakpointEntry*) * userCount);
        *matchCount = userCount;
    } else {
        *matches = NULL;
        *matchCount = 0;
    }

    return *matchCount;
}

/// @brief Check if the current program counter (PC) matches any breakpoints.
/// @return BreakpointType
/// @note Check if the current program counter (PC) matches any breakpoints.
int check_for_breakpoint(void)
{

    // Auto-initialize the breakpoint manager if it is not initialized
    if (g_breakpoint_mgr == NULL) {
        breakpoint_manager_init();
    }

    BreakpointEntry** hits;
    int hitCount;
    BreakpointType btType = BT_NONE;
    uint16_t pc = gPC;

    // Check for single step
    if (g_breakpoint_mgr->step_count > 0) {
        g_breakpoint_mgr->step_count--;
        if (g_breakpoint_mgr->step_count == 0) {
            g_breakpoint_step_pending = 0;
            set_cpu_stop_reason(STOP_REASON_STEP);
            set_cpu_run_mode(CPU_BREAKPOINT);
            return STOP_REASON_STEP;
        }
    }

    // Check for breakpoints
    if (breakpoint_manager_check(pc, &hits, &hitCount)) {
        for (int i = 0; i < hitCount; i++) {
            BreakpointEntry* bp = hits[i];

            // Evaluate condition expression
            bool condition_ok = true;
            if (bp->condition) {
                const char *err = NULL;
                condition_ok = expr_eval_condition(bp->condition, &err);
                if (err) {
                    LOG(LOG_CAT_DAP, LOG_WARN, "Breakpoint condition error at %06o: %s (expr: %s)",
                           pc, err, bp->condition);
                    condition_ok = false;
                }
            }
            bool hit_ok = true;

            if (bp->hitCondition) {
                int hitCondVal = (int)strtol(bp->hitCondition, NULL, 10);
                hit_ok = (bp->hitCount == hitCondVal);
            }

            if (condition_ok && hit_ok) {
                if (bp->logMessage) {
                    // Expand log message vars (simple demo)
                    LOG(LOG_CAT_DAP, LOG_INFO, "[LOGPOINT] %s", bp->logMessage);
                } else {
                    // Trigger stop event with proper reason based on breakpoint type
                    btType = bp->type;
                    CpuStopReason sr = (bp->type == BP_TYPE_TEMPORARY)
                        ? STOP_REASON_STEP : stopReasonFromBreakpoint(bp->type);
                    set_cpu_stop_reason(sr);
                    set_cpu_run_mode(CPU_BREAKPOINT);
                    // Record hit address for DAP hitBreakpointIds
                    g_breakpoint_mgr->last_hit_address = pc;
                    g_breakpoint_mgr->last_hit_valid = true;
                }

                if (bp->type == BP_TYPE_TEMPORARY) {
                    // Auto-remove temp breakpoint
                    breakpoint_manager_remove(bp->address, BP_TYPE_TEMPORARY);
                }
            }
        }
        free(hits); // free list
    }

    return btType;
}

/// @brief Convert breakpoint type to stop reason
/// @param t Breakpoint type
/// @return Stop reason
CpuStopReason stopReasonFromBreakpoint(BreakpointType t) {
    switch (t) {
        case BP_TYPE_USER: return STOP_REASON_BREAKPOINT;
        case BP_TYPE_FUNCTION: return STOP_REASON_FUNCTION_BREAKPOINT;
        case BP_TYPE_DATA: return STOP_REASON_DATA_BREAKPOINT;
        case BP_TYPE_INSTRUCTION: return STOP_REASON_INSTRUCTION_BREAKPOINT;
        default: return STOP_REASON_BREAKPOINT;
    }
}

/// @brief Get the address of the last breakpoint hit
/// @param address Pointer to store the hit address
/// @return true if a breakpoint was recently hit, false otherwise
bool breakpoint_manager_get_last_hit(uint16_t *address) {
    if (g_breakpoint_mgr && g_breakpoint_mgr->last_hit_valid) {
        if (address) *address = g_breakpoint_mgr->last_hit_address;
        g_breakpoint_mgr->last_hit_valid = false;
        return true;
    }
    return false;
}

//********** Watchpoints (memory access breakpoints) **********
//
// Performance design:
//   - watchpoint_bitmap[8192]: 1 bit per 16-bit address for O(1) fast rejection
//   - watchpoint_count: counter check is the first gate (zero = skip everything)
//   - watchpoint_check_slow(): only called when bitmap says this address has a WP
//   - Both bitmap and count are extern-visible for inlining in cpu.c hot path

static WatchpointEntry s_watchpoints[MAX_WATCHPOINTS];
int g_watchpoint_count = 0;
uint8_t g_watchpoint_bitmap[8192]; // 64K addresses, 1 bit each (8KB, fits L1)

/*
 * Ignore-count: skip the first N watchpoint hits before halting.  Set from the
 * CLI --watch-skip option.  Lets a watchpoint pass legitimate early writes to a
 * reused stack slot (e.g. csav storing a return address) and halt on a later
 * corrupting write instead.  Decremented on each would-trigger match.
 */
int g_watchpoint_skip_hits = 0;

/*
 * Value filter: when nonzero, a WRITE watchpoint only triggers if the value
 * being stored is >= watchpoint_min_value.  Set from --watch-min-value.  Lets a
 * watchpoint on a reused stack slot ignore legitimate text-range writes (csav
 * storing a return address) and halt only on an out-of-range value (a heap
 * pointer smashed into the return slot).  0 = disabled.
 */
int g_watchpoint_min_value = 0;

/// @brief Rebuild bitmap from active watchpoints (called after remove/clear)
static void watchpoint_bitmap_rebuild(void)
{
    memset(g_watchpoint_bitmap, 0, sizeof(g_watchpoint_bitmap));
    for (int i = 0; i < g_watchpoint_count; i++)
        if (s_watchpoints[i].active)
            g_watchpoint_bitmap[s_watchpoints[i].address >> 3] |= (1 << (s_watchpoints[i].address & 7));
}

/// @brief Add a watchpoint at a memory address
/// @param address Memory address to watch
/// @param type WATCH_READ, WATCH_WRITE, or WATCH_READWRITE
/// @param space WATCH_SPACE_ANY, WATCH_SPACE_ISPACE, or WATCH_SPACE_DSPACE
/// @param pil -1 for any PIL, 0-15 for specific PIL
/// @return 0 on success, -1 if full
int watchpoint_add(uint16_t address, WatchpointType type, WatchpointSpace space, int8_t pil)
{
    /* Update existing watchpoint at same address+space+pil */
    for (int i = 0; i < g_watchpoint_count; i++) {
        if (s_watchpoints[i].active && s_watchpoints[i].address == address
            && s_watchpoints[i].space == space && s_watchpoints[i].pil == pil) {
            s_watchpoints[i].type = type;
            return 0;
        }
    }
    if (g_watchpoint_count >= MAX_WATCHPOINTS) return -1;

    s_watchpoints[g_watchpoint_count].address = address;
    s_watchpoints[g_watchpoint_count].type = type;
    s_watchpoints[g_watchpoint_count].space = space;
    s_watchpoints[g_watchpoint_count].pil = pil;
    s_watchpoints[g_watchpoint_count].active = true;
    g_watchpoint_bitmap[address >> 3] |= (1 << (address & 7));
    g_watchpoint_count++;
    return 0;
}

/// @brief Remove watchpoint at address
void watchpoint_remove(uint16_t address)
{
    for (int i = 0; i < g_watchpoint_count; i++) {
        if (s_watchpoints[i].active && s_watchpoints[i].address == address) {
            s_watchpoints[i] = s_watchpoints[g_watchpoint_count - 1];
            s_watchpoints[g_watchpoint_count - 1].active = false;
            g_watchpoint_count--;
            watchpoint_bitmap_rebuild();
            return;
        }
    }
}

/// @brief Slow-path watchpoint check (called only when bitmap indicates a match)
/// @param address Memory address being accessed
/// @param isWrite true if write, false if read
/// @param useAPT true if D-space access, false if I-space
/// @return 1 if watchpoint hit, 0 otherwise
int watchpoint_check_slow(uint16_t address, bool isWrite, bool useAPT)
{
    int8_t curPIL = (int8_t)CurrLEVEL;
    for (int i = 0; i < g_watchpoint_count; i++) {
        WatchpointEntry *w = &s_watchpoints[i];
        if (!w->active) continue;
        if (w->address != address) continue;
        if (w->pil >= 0 && w->pil != curPIL) continue;
        if (w->space == WATCH_SPACE_ISPACE && useAPT) continue;
        if (w->space == WATCH_SPACE_DSPACE && !useAPT) continue;
        WatchpointType t = w->type;
        int matched = (t == WATCH_READWRITE)
                   || (isWrite && t == WATCH_WRITE)
                   || (!isWrite && t == WATCH_READ);
        if (matched) {
            /* Ignore-count: swallow the first N matches, halt after. */
            if (g_watchpoint_skip_hits > 0) {
                g_watchpoint_skip_hits--;
                return 0;
            }
            return 1;
        }
    }
    return 0;
}

/// @brief Legacy check (backward compat, no UseAPT)
int watchpoint_check(uint16_t address, bool isWrite)
{
    return watchpoint_check_slow(address, isWrite, false);
}

/// @brief Clear all watchpoints
void watchpoint_clear(void)
{
    for (int i = 0; i < MAX_WATCHPOINTS; i++)
        s_watchpoints[i].active = false;
    g_watchpoint_count = 0;
    memset(g_watchpoint_bitmap, 0, sizeof(g_watchpoint_bitmap));
}

/// @brief Get number of active watchpoints
int watchpoint_get_count(void)
{
    return g_watchpoint_count;
}

/// @brief Get watchpoint at index
int watchpoint_get(int index, uint16_t *out_addr, int *out_type)
{
    if (index < 0 || index >= g_watchpoint_count) return -1;
    if (!s_watchpoints[index].active) return -1;
    *out_addr = s_watchpoints[index].address;
    *out_type = (int)s_watchpoints[index].type;
    return 0;
}

//********** Physical Watchpoints **********

static PhysicalWatchpointEntry phys_watchpoints[MAX_WATCHPOINTS];
int g_phys_watchpoint_count = 0;                       // non-static: read on cpu_mms.c hot path
uint8_t g_phys_watchpoint_pagemap[PHYS_WP_BITMAP_BYTES];

/// @brief Rebuild the physical-watchpoint page bitmap from active entries
static void phys_watchpoint_pagemap_rebuild(void)
{
    memset(g_phys_watchpoint_pagemap, 0, sizeof(g_phys_watchpoint_pagemap));
    for (int i = 0; i < g_phys_watchpoint_count; i++) {
        if (!phys_watchpoints[i].active) continue;
        uint32_t idx = (phys_watchpoints[i].address >> 10) & (PHYS_WP_BITMAP_BYTES * 8u - 1u);
        g_phys_watchpoint_pagemap[idx >> 3] |= (1u << (idx & 7u));
    }
}

/// @brief Add a physical memory watchpoint
int phys_watchpoint_add(uint32_t address, WatchpointType type, int8_t pil)
{
    for (int i = 0; i < g_phys_watchpoint_count; i++) {
        if (phys_watchpoints[i].active && phys_watchpoints[i].address == address
            && phys_watchpoints[i].pil == pil) {
            phys_watchpoints[i].type = type;
            return 0;
        }
    }
    if (g_phys_watchpoint_count >= MAX_WATCHPOINTS) return -1;

    phys_watchpoints[g_phys_watchpoint_count].address = address;
    phys_watchpoints[g_phys_watchpoint_count].type = type;
    phys_watchpoints[g_phys_watchpoint_count].pil = pil;
    phys_watchpoints[g_phys_watchpoint_count].active = true;
    g_phys_watchpoint_count++;
    uint32_t idx = (address >> 10) & (PHYS_WP_BITMAP_BYTES * 8u - 1u);
    g_phys_watchpoint_pagemap[idx >> 3] |= (1u << (idx & 7u));
    return 0;
}

/// @brief Remove physical watchpoint at address
void phys_watchpoint_remove(uint32_t address)
{
    for (int i = 0; i < g_phys_watchpoint_count; i++) {
        if (phys_watchpoints[i].active && phys_watchpoints[i].address == address) {
            phys_watchpoints[i] = phys_watchpoints[g_phys_watchpoint_count - 1];
            phys_watchpoints[g_phys_watchpoint_count - 1].active = false;
            g_phys_watchpoint_count--;
            phys_watchpoint_pagemap_rebuild();
            return;
        }
    }
}

/// @brief Check if a physical memory access hits a watchpoint (with PIL check)
int phys_watchpoint_check(uint32_t address, bool isWrite)
{
    int8_t curPIL = (int8_t)CurrLEVEL;
    for (int i = 0; i < g_phys_watchpoint_count; i++) {
        if (!phys_watchpoints[i].active) continue;
        if (phys_watchpoints[i].address != address) continue;
        if (phys_watchpoints[i].pil >= 0 && phys_watchpoints[i].pil != curPIL) continue;
        WatchpointType t = phys_watchpoints[i].type;
        if (t == WATCH_READWRITE) return 1;
        if (isWrite && (t == WATCH_WRITE)) return 1;
        if (!isWrite && (t == WATCH_READ)) return 1;
    }
    return 0;
}

/// @brief Clear all physical watchpoints
void phys_watchpoint_clear(void)
{
    for (int i = 0; i < MAX_WATCHPOINTS; i++)
        phys_watchpoints[i].active = false;
    g_phys_watchpoint_count = 0;
    memset(g_phys_watchpoint_pagemap, 0, sizeof(g_phys_watchpoint_pagemap));
}

/// @brief Get number of active physical watchpoints
int phys_watchpoint_get_count(void)
{
    return g_phys_watchpoint_count;
}

/// @brief Get physical watchpoint at index
int phys_watchpoint_get(int index, uint32_t *out_addr, int *out_type)
{
    if (index < 0 || index >= g_phys_watchpoint_count) return -1;
    if (!phys_watchpoints[index].active) return -1;
    *out_addr = phys_watchpoints[index].address;
    *out_type = (int)phys_watchpoints[index].type;
    return 0;
}