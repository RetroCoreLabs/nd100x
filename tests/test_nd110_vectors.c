/*
 * test_nd110_vectors.c - the ND-110/ND-120 instruction group replayed from micro code vectors.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * The files in tests/vectors/*.vec were written by the NDMicroCpu micro code CPU (the RASK and
 * DELILAH-L micro codes agree on every one): for each case the registers and WGLOB globals
 * before, the memory words seeded, the instruction words, and what the micro code left behind -
 * registers after and every memory word it touched. This test replays each case through the
 * real dispatch table and compares the registers and the memory afterwards.
 *
 * nd100x has no per-instruction memory trace, so unlike the RetroCore replay the ORDER of the
 * accesses is not checked here; the end state is. The micro words each case executed ("ucode")
 * are the micro code's own business and are skipped.
 *
 * File format, one fact per line, every number octal:
 *   instr NAME OPCODE / case NAME / regs D P B L A T X STS / glob STBNK STSRT CMBUK /
 *   mem ADDR VALUE / words W0 [W1 ..] / after D P B L A T X STS / trace R|W ADDR VALUE /
 *   memafter ADDR VALUE / ucode .. / end
 *
 * Paging is off, so every address is a physical word address (bank << 16 | word).
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_types.h"

#ifndef VECTOR_DIR
#error "VECTOR_DIR must name the folder holding the .vec files"
#endif

#define TEST_LOCAL_WORDS 0x00040000u /* 256 KW installed: banks 0-3, all the vectors need */
#define MAX_SEEDS        512
#define MAX_AFTER        1024
#define MAX_WORDS        4

struct regs8
{
    uint16_t d, p, b, l, a, t, x, sts;
};

struct word
{
    uint32_t addr;
    uint16_t value;
};

struct vec
{
    char        name[64];
    struct regs8 before;
    struct regs8 after;
    uint16_t    stbnk, stsrt, cmbuk;
    struct word seeds[MAX_SEEDS];
    int         nseeds;
    uint16_t    words[MAX_WORDS];
    int         nwords;
    struct word memafter[MAX_AFTER];
    int         nafter;
};

static struct CpuRegs s_test_regs;
static int            s_cases;
static int            s_failed_cases;

static void fixture(void)
{
    memset(&s_test_regs, 0, sizeof(s_test_regs));
    g_reg = &s_test_regs;
    g_nd_memsize = TEST_LOCAL_WORDS;
    mms_memory_banks_init();
    g_mms_type = MMS1;
    if (!mms_create_paging_tables())
    {
        printf("  FAIL: paging tables did not allocate\n");
        s_failed_cases++;
    }
    cpu_instructions();
}

static uint16_t oct16(const char *s)
{
    return (uint16_t)strtoul(s, NULL, 8);
}

static uint32_t oct32(const char *s)
{
    return (uint32_t)strtoul(s, NULL, 8);
}

static void read_regs(struct regs8 *r, char **f)
{
    r->d = oct16(f[1]);
    r->p = oct16(f[2]);
    r->b = oct16(f[3]);
    r->l = oct16(f[4]);
    r->a = oct16(f[5]);
    r->t = oct16(f[6]);
    r->x = oct16(f[7]);
    r->sts = oct16(f[8]);
}

/* Splits one line on spaces. Returns the field count; f[] points into line. */
static int split(char *line, char **f, int max)
{
    int n = 0;
    char *tok = strtok(line, " \t\r\n");
    while (tok != NULL && n < max)
    {
        f[n++] = tok;
        tok = strtok(NULL, " \t\r\n");
    }
    return n;
}

static void check_reg(const char *instr, const struct vec *v, const char *name, uint16_t local, uint16_t micro,
                      int *diffs)
{
    if (local != micro)
    {
        printf("  %s/%s: %s local %06o, micro code %06o\n", instr, v->name, name, local, micro);
        (*diffs)++;
    }
}

static void replay(const char *instr, struct vec *v)
{
    int diffs = 0;
    int i;

    s_cases++;

    /* The micro code ran from zeroed memory: clear every word this case will see, then seed. */
    for (i = 0; i < v->nafter; i++)
    {
        mms_write_physical_memory((int)v->memafter[i].addr, 0, true);
    }
    for (i = 0; i < v->nseeds; i++)
    {
        mms_write_physical_memory((int)v->seeds[i].addr, v->seeds[i].value, true);
    }
    for (i = 0; i < v->nwords; i++)
    {
        mms_write_physical_memory((int)(v->before.p + i), v->words[i], true);
    }

    gSTBNK = v->stbnk;
    gSTSRT = v->stsrt;
    gCMBUK = v->cmbuk;
    gD = v->before.d;
    gB = v->before.b;
    gL = v->before.l;
    gA = v->before.a;
    gT = v->before.t;
    gX = v->before.x;
    /* The flag byte P G K Z Q O C M; the level bits in the high byte are not part of a vector. */
    g_reg->reg[gPIL][_STS] = (uint16_t)(v->before.sts & 0x00FF);
    /* The fetch has already advanced P past the instruction word when a handler runs. */
    gPC = (uint16_t)(v->before.p + 1);

    g_instr_funcs[v->words[0]](v->words[0]);

    check_reg(instr, v, "D", gD, v->after.d, &diffs);
    check_reg(instr, v, "P", gPC, v->after.p, &diffs);
    check_reg(instr, v, "B", gB, v->after.b, &diffs);
    check_reg(instr, v, "L", gL, v->after.l, &diffs);
    check_reg(instr, v, "A", gA, v->after.a, &diffs);
    check_reg(instr, v, "T", gT, v->after.t, &diffs);
    check_reg(instr, v, "X", gX, v->after.x, &diffs);
    check_reg(instr, v, "STS flags", (uint16_t)(g_reg->reg[gPIL][_STS] & 0x00FF), (uint16_t)(v->after.sts & 0x00FF),
              &diffs);

    for (i = 0; i < v->nafter; i++)
    {
        uint16_t local = (uint16_t)mms_read_physical_memory((int)v->memafter[i].addr, true);
        if (local != v->memafter[i].value)
        {
            printf("  %s/%s: memory %08o local %06o, micro code %06o\n", instr, v->name, v->memafter[i].addr, local,
                   v->memafter[i].value);
            diffs++;
        }
    }

    if (diffs != 0)
    {
        s_failed_cases++;
    }
}

static void run_file(const char *path)
{
    static struct vec v;
    char   line[512];
    char  *f[16];
    char   instr[32] = "?";
    int    cases = 0;
    int    failed_before = s_failed_cases;
    FILE  *fp = fopen(path, "r");

    if (fp == NULL)
    {
        printf("  FAIL: cannot open %s\n", path);
        s_failed_cases++;
        return;
    }
    while (fgets(line, (int)sizeof(line), fp) != NULL)
    {
        int n;
        if (line[0] == '#')
        {
            continue;
        }
        n = split(line, f, 16);
        if (n == 0)
        {
            continue;
        }
        if (strcmp(f[0], "instr") == 0)
        {
            snprintf(instr, sizeof(instr), "%s", f[1]);
        }
        else if (strcmp(f[0], "case") == 0)
        {
            memset(&v, 0, sizeof(v));
            snprintf(v.name, sizeof(v.name), "%s", f[1]);
        }
        else if (strcmp(f[0], "regs") == 0)
        {
            read_regs(&v.before, f);
        }
        else if (strcmp(f[0], "glob") == 0)
        {
            v.stbnk = oct16(f[1]);
            v.stsrt = oct16(f[2]);
            v.cmbuk = oct16(f[3]);
        }
        else if (strcmp(f[0], "mem") == 0 && v.nseeds < MAX_SEEDS)
        {
            v.seeds[v.nseeds].addr = oct32(f[1]);
            v.seeds[v.nseeds].value = oct16(f[2]);
            v.nseeds++;
        }
        else if (strcmp(f[0], "words") == 0)
        {
            int k;
            v.nwords = 0;
            for (k = 1; k < n && v.nwords < MAX_WORDS; k++)
            {
                v.words[v.nwords++] = oct16(f[k]);
            }
        }
        else if (strcmp(f[0], "after") == 0)
        {
            read_regs(&v.after, f);
        }
        else if (strcmp(f[0], "memafter") == 0 && v.nafter < MAX_AFTER)
        {
            v.memafter[v.nafter].addr = oct32(f[1]);
            v.memafter[v.nafter].value = oct16(f[2]);
            v.nafter++;
        }
        else if (strcmp(f[0], "end") == 0)
        {
            replay(instr, &v);
            cases++;
        }
        /* "trace" and "ucode" lines: not compared here, see the header. */
    }
    fclose(fp);
    printf("%-13s %3d cases, %d differ from the micro code\n", instr, cases, s_failed_cases - failed_before);
}

static int by_name(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int main(void)
{
    DIR           *dir = opendir(VECTOR_DIR);
    struct dirent *ent;
    char          *names[64];
    int            nnames = 0;
    int            i;

    printf("=== ND-110/ND-120 instruction group: micro code vectors from %s ===\n", VECTOR_DIR);
    if (dir == NULL)
    {
        printf("FAIL: cannot open %s\n", VECTOR_DIR);
        return 1;
    }
    while ((ent = readdir(dir)) != NULL && nnames < 64)
    {
        size_t len = strlen(ent->d_name);
        if (len > 4 && strcmp(ent->d_name + len - 4, ".vec") == 0)
        {
            names[nnames++] = strdup(ent->d_name);
        }
    }
    closedir(dir);
    qsort(names, (size_t)nnames, sizeof(names[0]), by_name);

    fixture();
    for (i = 0; i < nnames; i++)
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", VECTOR_DIR, names[i]);
        run_file(path);
        free(names[i]);
    }

    printf("\n%d vector files, %d cases, %d cases differ from the micro code\n", nnames, s_cases, s_failed_cases);
    if (nnames != 29)
    {
        printf("FAIL: expected 29 vector files\n");
        return 1;
    }
    return (s_failed_cases == 0) ? 0 : 1;
}
