/*
 * nd500_wasm.c - the ND-500 inside nd100x's WebAssembly module.
 *
 * WHY THE ND-500 IS LINKED IN HERE AND NOT LOADED AS A SECOND MODULE
 * -----------------------------------------------------------------
 * An ND-100 + ND-500 machine has SHARED MEMORY (MPM5): the ND-500 reads and
 * writes the ND-100's memory directly. In a browser that means a single
 * WebAssembly.Memory, and a Module owns its memory, so both CPUs have to live
 * in ONE module. Two script tags cannot express the hardware. The whole shape
 * of this file follows from that one fact.
 *
 * WHAT THIS FILE IS, AND IS NOT
 * -----------------------------
 * It is the thin seam between the browser and libnd500: create the machine,
 * hand it a kernel and a disk, boot it, step it, move console bytes. All the
 * hard parts - the MMU setup, the PSEG/DSEG placement, THA/CTE1/CTE2/CAD, the
 * u-area - live in nd500x's own nd500_ndix_boot.c and are called, not copied.
 * That library function exists precisely so a caller with no debugger and no
 * main() can boot NDIX, which is exactly what a browser is.
 *
 * It is NOT the ND-100 <-> ND-500 pairing. Nothing here connects the two
 * machines: the ND-500 runs on its own, answering its own fecalls the way
 * nd500x does natively (front_end = synthetic). Giving SINTRAN a real ND-500
 * over the 3022 bus interface, and the shared-memory window itself, are later
 * milestones (M5).
 *
 * BUILD. Everything below is inside ND100X_WITH_ND500, which the root
 * CMakeLists sets only when an nd500x checkout was found. The stub half at the
 * bottom keeps every exported name present either way - EXPORTED_FUNCTIONS
 * names them and emscripten fails the link on a name it cannot find, so
 * "no ND-500" has to be a value returned at runtime, not a missing symbol.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "nd500_wasm.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define EMSCRIPTEN_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EMSCRIPTEN_EXPORT
#endif

#ifdef ND100X_WITH_ND500

/* nd500x's own headers, addressed BY SUBDIRECTORY on purpose.
 *
 * nd100x and nd500x both have a src/machine/machine_types.h and a
 * src/cpu/cpu_protos.h, and nd100wasm already has ${CMAKE_SOURCE_DIR}/src/machine
 * and .../src/cpu on its include path - so a bare "machine_types.h" here picks
 * up the ND-100's. It did, and the errors were about Nd500Machine being an
 * incomplete type. A header whose contents merely DIFFERED would have compiled
 * and been silently wrong, so this is worth the two-part paths: only nd500x's
 * src/ is on the path as a directory, nd100x's is not.
 *
 * cpu_protos.h is where struct Nd500Cpu is actually DEFINED (line 62); the
 * other cpu headers only forward-declare it, and the machine and CPU below are
 * plain objects with static storage, not pointers. */
#include "machine/machine_types.h"
#include "machine/machine_protos.h"
#include "machine/nd500_ndix_boot.h"
#include "frontend/nd500x/ndix_ffs.h"
#include "cpu/nd500_host.h"
#include "cpu/nd500_fecall.h"
#include "cpu/cpu_protos.h"
#include "cpu/nd500_mmu.h"  /* nd500_mmu_peek - trap-free translate */
#include "cpu/nd500_xmsg.h" /* the uplink seam: set_uplink / frame_in        */

/* ------------------------------------------------------------------ state */

/* One ND-500 per module, matching nd500x's own "one ND-500 per process" rule
 * (see the note in nd500_host.h about why the host ops are module-level). Even
 * in the eventual ND-100 + ND-500 pairing there is one ND-500 per front end. */
static Nd500Machine g_m;
static Nd500Cpu g_cpu;
static int g_created = 0;
static int g_booted = 0;

/* ------------------------------------------------------- disks (host ops)
 *
 * The ND-500's disks are served out of the wasm heap: JS allocates a buffer
 * with _malloc, fills it from OPFS or from a download, and hands the pointer
 * over. That is the same deal MountSMDFromBuffer already makes for the ND-100
 * side, and it is deliberately the SIMPLEST of the three storage paths nd100x
 * has.
 *
 * NOT DONE YET, and worth being explicit about: the OPFS-worker path and the
 * gateway path are not wired to the ND-500. A disk mounted here is a snapshot
 * in memory - writes land in the buffer and JS has to read it back out to keep
 * them. For a root filesystem of any size that is a lot of heap, and streaming
 * it through the existing SharedArrayBuffer disk worker is the real answer.
 * The Nd500HostOps interface is already the right shape for it (byte offsets,
 * per unit), so that is a change behind this seam, not to it.
 */
typedef struct
{
    uint8_t *data;
    uint64_t size;
    int writable;
} Nd500WasmDisk;

static Nd500WasmDisk g_disks[ND500_HOST_MAX_DISKS];

static int64_t wasm_disk_size(void *ctx, int unit)
{
    (void)ctx;
    if (unit < 0 || unit >= ND500_HOST_MAX_DISKS)
    {
        return -1;
    }
    if (!g_disks[unit].data)
    {
        return -1; /* -1 IS "not mounted" */
    }
    return (int64_t)g_disks[unit].size;
}

static int64_t wasm_disk_read(void *ctx, int unit, uint64_t off, void *dst, uint32_t len)
{
    (void)ctx;
    if (unit < 0 || unit >= ND500_HOST_MAX_DISKS)
    {
        return -1;
    }
    Nd500WasmDisk *d = &g_disks[unit];
    if (!d->data)
    {
        return -1;
    }
    if (off >= d->size)
    {
        return 0; /* past the end: 0 bytes, not an error */
    }
    /* A short count is a real short count - the interface says so, and the
     * fecall layer copes with it. Clamping here is how a read that straddles
     * the end of the image behaves like a real disc rather than failing. */
    if (off + len > d->size)
    {
        len = (uint32_t)(d->size - off);
    }
    memcpy(dst, d->data + off, len);
    return (int64_t)len;
}

static int64_t wasm_disk_write(void *ctx, int unit, uint64_t off, const void *src, uint32_t len)
{
    (void)ctx;
    if (unit < 0 || unit >= ND500_HOST_MAX_DISKS)
    {
        return -1;
    }
    Nd500WasmDisk *d = &g_disks[unit];
    if (!d->data || !d->writable)
    {
        return -1;
    }
    if (off >= d->size)
    {
        return 0;
    }
    if (off + len > d->size)
    {
        len = (uint32_t)(d->size - off);
    }
    memcpy(d->data + off, src, len);
    return (int64_t)len;
}

static int wasm_disk_writable(void *ctx, int unit)
{
    (void)ctx;
    if (unit < 0 || unit >= ND500_HOST_MAX_DISKS)
    {
        return 0;
    }
    return g_disks[unit].data ? g_disks[unit].writable : 0;
}

static const Nd500HostOps g_host_ops = {wasm_disk_size, wasm_disk_read, wasm_disk_write,
                                        wasm_disk_writable};

/* ------------------------------------------------------------- console ---
 *
 * Same shape as the ND-100 terminal ring buffer above in nd100wasm.c: the
 * guest writes whenever it likes, JS drains after each Step. Packing the unit
 * into the high byte keeps one queue for all four tty units, so the order
 * bytes were produced in survives - which matters, because the boot log and a
 * getty banner interleave.
 */
#define ND500_CON_BUF 8192
static struct
{
    uint16_t entries[ND500_CON_BUF];
    volatile int writePos;
    volatile int readPos;
} g_con = {{0}, 0, 0};

static void con_push(int unit, uint8_t c)
{
    int next = (g_con.writePos + 1) % ND500_CON_BUF;
    if (next == g_con.readPos)
    {
        return; /* full: drop, like a real line */
    }
    g_con.entries[g_con.writePos] = (uint16_t)(((unit & 0xFF) << 8) | c);
    g_con.writePos = next;
}

static void nd500_tty_sink(int unit, const unsigned char *buf, int len, void *ctx)
{
    (void)ctx;
    for (int i = 0; i < len; i++)
    {
        con_push(unit, buf[i]);
    }
}

/* ------------------------------------------------------- boot-log capture
 *
 * nd500_ndix_boot() reports each step through a log hook. Natively that goes to
 * stderr; here it goes into the same queue as guest output on a unit of its
 * own (0xFF), so the page can show "mmusetup", "map-kdata", "ndix-uarea" as
 * they happen. When a boot dies, the last line printed is how you know which
 * step it died in - losing that in the browser would make every failure look
 * identical.
 */
static void nd500_log_line(void *ctx, const char *line)
{
    (void)ctx;
    if (!line)
    {
        return;
    }
    for (const char *p = line; *p; p++)
    {
        con_push(0xFF, (uint8_t)*p);
    }
    con_push(0xFF, '\n');
}

/* --------------------------------------------------------- MEMFS staging
 *
 * nd500_ndix_boot() takes FILE PATHS, and reads the a.out header with stat()
 * and fopen(). Rather than fork the library for the browser, the buffers JS
 * hands over are written into emscripten's in-memory filesystem and the paths
 * of those files are what gets passed in. The boot path stays the ONE that is
 * exercised natively - which is the whole point of having lifted it into the
 * library in the first place.
 */
#define ND500_KERNEL_PATH "/vmunix"
#define ND500_PSEG_PATH   "/vmunix.pseg"
#define ND500_DSEG_PATH   "/vmunix.dseg"

/* The path INSIDE the NDIX filesystem, which is a different namespace from the
 * MEMFS paths above even though the kernel happens to be called the same thing
 * in both. */
#define ND500_IMAGE_KERNEL "/vmunix"

static int stage_file(const char *path, const uint8_t *data, int len);

/* Pull the kernel out of the mounted root disc.
 *
 * This is the browser's copy of what nd500x's frontend does natively
 * (nd500x_ndix.c extract_kernel(): ndix_ffs_read_file(image, "/vmunix", ...)
 * then write the bytes to a scratch file and hand nd500_ndix_boot() THAT
 * path). It has to exist here too, because nd500_ndix_boot() reads its kernel
 * with fopen()/stat() - it never looks inside a filesystem image itself. Until
 * now the wasm build mounted the disc as bytes for the guest and then asked
 * the library to fopen("/vmunix"), a MEMFS file nobody had written, which is
 * the "cannot read the a.out header of /vmunix" failure.
 *
 * The disc is a buffer, not a file, so it is read through an fmemopen()
 * stream: no second 70 MB copy, and the FFS reader is the same code the
 * native build runs.
 *
 * Returns 0 when /vmunix is staged in MEMFS and ready for the boot. */
static int extract_kernel_from_disc(int unit)
{
    const char *why = "";
    long n = 0;
    uint8_t *data;
    FILE *img;
    int rc;

    if (unit < 0 || unit >= ND500_HOST_MAX_DISKS)
    {
        return -1;
    }
    if (!g_disks[unit].data || g_disks[unit].size == 0)
    {
        fprintf(stderr, "| [WASM] no disc mounted on unit %d to take a kernel from\n", unit);
        return -1;
    }

    img = fmemopen(g_disks[unit].data, (size_t)g_disks[unit].size, "rb");
    if (!img)
    {
        fprintf(stderr, "| [WASM] cannot open the mounted disc as a stream\n");
        return -1;
    }
    data = ndix_ffs_read_file_fp(img, ND500_IMAGE_KERNEL, &n, &why);
    fclose(img);

    if (!data || n <= 0)
    {
        fprintf(stderr, "| [WASM] no %s inside the root disc: %s\n", ND500_IMAGE_KERNEL,
                why && why[0] ? why : "not found");
        free(data);
        return -1;
    }

    rc = stage_file(ND500_KERNEL_PATH, data, (int)n);
    fprintf(stderr, "| [WASM] extracted %s from the root disc: %ld bytes -> %s (%s)\n",
            ND500_IMAGE_KERNEL, n, ND500_KERNEL_PATH, rc == 0 ? "staged" : "STAGING FAILED");
    free(data);
    return rc;
}

static int stage_file(const char *path, const uint8_t *data, int len)
{
    FILE *f = fopen(path, "wb");
    if (!f)
    {
        return -1;
    }
    size_t n = fwrite(data, 1, (size_t)len, f);
    fclose(f);
    return (n == (size_t)len) ? 0 : -1;
}

/* ================================================================ exports */

EMSCRIPTEN_EXPORT int Nd500_Available(void)
{
    return 1;
}

/* Set one of nd500x's ND500X_* switches.
 *
 * nd500x reads them all out of the environment ONCE, the first time
 * nd500_settings() is used, and a browser has no environment - so every
 * diagnostic switch it has was permanently off with no way to reach it. They
 * are precisely what is wanted when a guest boots to a point and stops
 * (ND500X_FEDBG for the front-end calls, ND500X_TICKSTAT for the clock,
 * ND500X_TTYDBG for output on a tty nobody is attached to).
 *
 * MUST be called before Nd500_Create: after the settings have been read once,
 * changing the environment does nothing. */
EMSCRIPTEN_EXPORT int Nd500_SetEnv(const char *name, const char *value)
{
    if (!name || !name[0])
    {
        return -1;
    }
    return setenv(name, value ? value : "1", 1);
}

/* Create the machine. <mem_bytes> 0 takes the 16 MB nd500x uses natively.
 * Returns 0 on success. */
EMSCRIPTEN_EXPORT int Nd500_Create(int mem_bytes)
{
    if (g_created)
    {
        return 0; /* idempotent, like the ND-100 Init */
    }
    if (mem_bytes <= 0)
    {
        mem_bytes = 16 * 1024 * 1024;
    }

    /* The NDIX boot defaults, before anything reads the settings.
     *
     * nd500x's native frontend sets these and calls each "REQUIRED for the
     * NDIX boot" (nd500x_ndix.c:214-223). They live in that frontend, so a
     * library caller does not get them - and the boot fails in a way that
     * looks like nothing is wrong: NDIX prints its whole banner, reports its
     * memory and buffers, and then idles forever at PC 0x844 taking clock
     * interrupts. nd500x's own comment says why: "ND500X_NOXMSG - bypass XMSG
     * (without it proc0 sleeps forever)". xgattach's XMSG device init
     * otherwise succeeds and then waits for an ND-100 that is not there.
     *
     * Only the two that mean anything in a browser. ND500X_DISK and
     * ND500X_DISK_RW are file paths and a file mode; here disks arrive through
     * Nd500HostOps and writability is per mount. ND500X_CONSOLE_STDIN needs a
     * stdin.
     *
     * setenv with overwrite=0 on purpose: a host that already called
     * Nd500_SetEnv has made a choice, and it keeps it. */
    setenv("ND500X_NOXMSG", "1", 0);
    setenv("ND500X_MMU_GUEST_TABLES", "1", 0);

    memset(&g_m, 0, sizeof g_m);
    memset(&g_cpu, 0, sizeof g_cpu);
    nd500_machine_init(&g_m, (uint32_t)mem_bytes);
    nd500_cpu_init(&g_cpu, &g_m);
    nd500_cpu_reset(&g_cpu);

    /* Disks come from the browser, not from stdio. This must happen before the
     * machine runs - nd500_host.h is explicit that changing hosts mid-boot is
     * not supported. */
    nd500_host_set(&g_host_ops, NULL);

    /* Both boot-log channels into the console queue. */
    nd500_ndix_set_notice_log(nd500_log_line, NULL);
    nd500_ndix_set_verbose_log(nd500_log_line, NULL);

    /* Every tty unit the fecall layer knows about. Without a sink, output for
     * a unit nobody is attached to is dropped - correct natively, wrong here,
     * where the page IS every terminal. */
    for (int u = 0; u < ND500_TTY_MAX_UNITS; u++)
    {
        nd500_fecall_set_tty_output(u, nd500_tty_sink, NULL);
    }

    g_created = 1;
    g_booted = 0;
    return 0;
}

EMSCRIPTEN_EXPORT int Nd500_IsCreated(void)
{
    return g_created;
}
EMSCRIPTEN_EXPORT int Nd500_IsBooted(void)
{
    return g_booted;
}

/* Stage the kernel a.out. Call before Nd500_Boot. */
EMSCRIPTEN_EXPORT int Nd500_LoadKernel(uint8_t *data, int len)
{
    if (!data || len <= 0)
    {
        return -1;
    }
    return stage_file(ND500_KERNEL_PATH, data, len);
}

/* Stage the pre-split segment files, when they exist. BOTH or NEITHER: the
 * library treats a half-present pair as absent and derives the sizes from the
 * a.out header instead, which is the path a kernel extracted from a disk image
 * takes - there are no segment files inside an image. */
EMSCRIPTEN_EXPORT int Nd500_LoadSegments(uint8_t *pseg, int pseg_len, uint8_t *dseg, int dseg_len)
{
    if (!pseg || !dseg || pseg_len <= 0 || dseg_len <= 0)
    {
        return -1;
    }
    if (stage_file(ND500_PSEG_PATH, pseg, pseg_len) != 0)
    {
        return -1;
    }
    return stage_file(ND500_DSEG_PATH, dseg, dseg_len);
}

/* Mount a disk image already in the wasm heap. The buffer is NOT copied and
 * NOT freed here: JS owns it, and that is what lets JS read written blocks
 * back out afterwards. */
EMSCRIPTEN_EXPORT int Nd500_MountDisk(int unit, uint8_t *data, int len, int writable)
{
    if (unit < 0 || unit >= ND500_HOST_MAX_DISKS)
    {
        return -1;
    }
    if (!data || len <= 0)
    {
        return -1;
    }
    g_disks[unit].data = data;
    g_disks[unit].size = (uint64_t)len;
    g_disks[unit].writable = writable ? 1 : 0;
    return 0;
}

EMSCRIPTEN_EXPORT int Nd500_UnmountDisk(int unit)
{
    if (unit < 0 || unit >= ND500_HOST_MAX_DISKS)
    {
        return -1;
    }
    g_disks[unit].data = NULL;
    g_disks[unit].size = 0;
    g_disks[unit].writable = 0;
    return 0;
}

/* Where a unit's bytes live, so JS can read back what the guest wrote. */
EMSCRIPTEN_EXPORT uint8_t *Nd500_GetDiskBuffer(int unit)
{
    if (unit < 0 || unit >= ND500_HOST_MAX_DISKS)
    {
        return NULL;
    }
    return g_disks[unit].data;
}
EMSCRIPTEN_EXPORT int Nd500_GetDiskSize(int unit)
{
    if (unit < 0 || unit >= ND500_HOST_MAX_DISKS)
    {
        return 0;
    }
    return (int)g_disks[unit].size;
}

/* Run the whole NDIX boot sequence. Everything it does belongs to SINTRAN and
 * the ND-100 on real hardware; nd500x stands in for them until the 3022 bus
 * interface is real (M5). */
EMSCRIPTEN_EXPORT int Nd500_Boot(void)
{
    if (!g_created)
    {
        return -1;
    }
    Nd500NdixBoot cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.kernel_path = ND500_KERNEL_PATH;

    /* Disc-only boot. No kernel was uploaded, so the one inside the root disc
     * is the kernel - the same order nd500x uses natively, where the image's
     * own /vmunix comes FIRST and a separate file is only the fallback. When a
     * kernel WAS staged, it stays: an explicitly uploaded kernel is a choice,
     * and it wins over whatever the disc happens to carry. */
    FILE *fk = fopen(ND500_KERNEL_PATH, "rb");
    if (fk)
    {
        fclose(fk);
        fprintf(stderr, "| [WASM] using the uploaded kernel at %s\n", ND500_KERNEL_PATH);
    }
    else
    {
        fprintf(stderr, "| [WASM] no kernel staged - taking %s out of the root disc\n",
                ND500_IMAGE_KERNEL);
        if (extract_kernel_from_disc(0) != 0)
        {
            fprintf(stderr, "| [WASM] boot aborted: no kernel to run\n");
            return -1;
        }
    }

    fprintf(stderr, "| [WASM] Booting ND-500, kernel path: %s\n", ND500_KERNEL_PATH);

    /* Offer the segment files only when both were staged. fopen is the test:
     * MEMFS has no stat cost worth avoiding and this needs no extra header. */
    FILE *fp = fopen(ND500_PSEG_PATH, "rb");
    FILE *fd = fopen(ND500_DSEG_PATH, "rb");
    if (fp && fd)
    {
        cfg.pseg_path = ND500_PSEG_PATH;
        cfg.dseg_path = ND500_DSEG_PATH;
        fprintf(stderr, "| [WASM] Segment files found\n");
    }
    if (fp)
    {
        fclose(fp);
    }
    if (fd)
    {
        fclose(fd);
    }

    /* The u-area step IS done here. Natively it is the frontend's job because
     * two different boot routes need it; here there is only one route. */
    cfg.with_uarea = 1;

    fprintf(stderr, "| [WASM] Calling nd500_ndix_boot()...\n");
    int rc = nd500_ndix_boot(&g_m, &cfg);
    fprintf(stderr, "| [WASM] nd500_ndix_boot returned: %d\n", rc);
    if (rc != 0)
    {
        return rc;
    }

    /* ARM the machine. The wasm build of nd500_dbg_run() (nd500x
     * debug_api.c, inside #ifdef __EMSCRIPTEN__) does NOT start a thread the
     * way the native one does - it sets run_flag and, in its own words, "lets
     * the caller drive stepping itself". Nd500_Step does the driving, but
     * without this the machine executes with run_flag == 0: running, while
     * telling everything that asks that it is not. */
    nd500_dbg_run(&g_m);

    g_booted = 1;
    return 0;
}

/* Advance the ND-500 by <count> instructions. Stepping rather than running:
 * a browser has one thread for the page, and nd500_dbg_run() does not come
 * back until the guest stops. The caller decides the slice. */
EMSCRIPTEN_EXPORT int Nd500_Step(int count)
{
    if (!g_created || !g_booted)
    {
        return -1;
    }
    if (count <= 0)
    {
        count = 1;
    }
    nd500_dbg_step(&g_m, (uint32_t)count);
    return (int)g_m.stop_reason;
}

EMSCRIPTEN_EXPORT int Nd500_GetStopReason(void)
{
    return g_created ? (int)g_m.stop_reason : 0;
}

/* Is the machine still running?
 *
 * Ask THIS, not "is the stop reason still none". stop_reason is left set by
 * things that are entirely normal in a paging OS - nd500_dbg_step itself only
 * treats a stop as real when run_flag has gone too (nd500x debug_api.c:439) -
 * and NDIX takes page faults constantly by design, because that is what demand
 * paging is. A caller driving the CPU in slices needs the flag. */
EMSCRIPTEN_EXPORT int Nd500_IsRunning(void)
{
    return (g_created && g_booted) ? nd500_dbg_is_running(&g_m) : 0;
}

/* Why it stopped, in words. nd500x already has the table; there is no reason
 * for JS to carry a second copy of the enum. */
EMSCRIPTEN_EXPORT const char *Nd500_GetStopReasonText(void)
{
    if (!g_created)
    {
        return "no machine";
    }
    return nd500_stop_reason_str(g_m.stop_reason);
}

EMSCRIPTEN_EXPORT uint32_t Nd500_GetPC(void)
{
    return g_created ? g_cpu.PC : 0u;
}

/* Physical memory, bypassing the MMU.
 *
 * PHYSICAL on purpose. The questions these answer - where does the emulator's
 * private window end, did the guest allocator eat the shared-segment pages -
 * are all about physical addresses, and translating them would answer a
 * different question. nd500_bus_read8/write8 are the bus itself.
 *
 * Returns the number of bytes moved, which is 0 for an address past the end of
 * memory rather than an error: a host reading a window is entitled to ask about
 * an address that turns out not to exist. */
EMSCRIPTEN_EXPORT int Nd500_ReadPhys(uint32_t addr, uint8_t *dst, int len)
{
    if (!g_created || !dst || len <= 0)
    {
        return 0;
    }
    int n = 0;
    for (; n < len; n++)
    {
        if (addr + (uint32_t)n >= g_m.memory_size)
        {
            break;
        }
        dst[n] = nd500_bus_read8(&g_m, addr + (uint32_t)n);
    }
    return n;
}

EMSCRIPTEN_EXPORT int Nd500_WritePhys(uint32_t addr, const uint8_t *src, int len)
{
    if (!g_created || !src || len <= 0)
    {
        return 0;
    }
    int n = 0;
    for (; n < len; n++)
    {
        if (addr + (uint32_t)n >= g_m.memory_size)
        {
            break;
        }
        nd500_bus_write8(&g_m, addr + (uint32_t)n, src[n]);
    }
    return n;
}

/* How much physical memory the machine has, so a caller can size a window
 * instead of guessing. */
EMSCRIPTEN_EXPORT uint32_t Nd500_MemorySize(void)
{
    return g_created ? g_m.memory_size : 0u;
}

/* Where a GUEST VIRTUAL address currently lives, or 0xFFFFFFFF if it is not
 * mapped. Trap-free and state-free - nd500_mmu_peek exists for diagnostics
 * exactly so that asking does not change the answer.
 *
 * Needed because guessing is not good enough. The segment-6 window NDIX shares
 * with the ND-100 is DEMAND-mapped a page at a time, so its two 2 KB halves -
 * the command ring at 0x30000000 and the response ring at 0x30000800 - are
 * separate pages that need not be next to each other in physical memory, and
 * are not. Reading them at a physical address inferred from a boot-log line
 * produces plausible-looking rubbish, which is precisely the failure mode the
 * XMSG work has to avoid. Ask the MMU instead. */
EMSCRIPTEN_EXPORT uint32_t Nd500_TranslateVirt(uint32_t vaddr)
{
    if (!g_created)
    {
        return 0xFFFFFFFFu;
    }
    return nd500_mmu_peek(&g_cpu, vaddr);
}

/* Drain one console byte: (unit << 8) | byte, or -1 when empty.
 * Unit 0xFF is this file's own boot log, not guest output. */
EMSCRIPTEN_EXPORT int Nd500_PollConsole(void)
{
    if (g_con.readPos == g_con.writePos)
    {
        return -1;
    }
    uint16_t e = g_con.entries[g_con.readPos];
    g_con.readPos = (g_con.readPos + 1) % ND500_CON_BUF;
    return (int)e;
}

/* Type at a guest terminal. */
EMSCRIPTEN_EXPORT void Nd500_SendInput(int unit, const char *text, int len)
{
    if (!g_created || !text || len <= 0)
    {
        return;
    }
    nd500_fecall_tty_input(unit, text, len);
}

/* ------------------------------------------------------------- ethernet ---
 *
 * NDIX's et0 on the gateway's emulated segment. Frame types 0x30 (RX), 0x31
 * (TX) and 0x32 (link) - see docs/GATEWAY-PROTOCOL.md.
 *
 *   RX: gateway 0x30 -> Nd500_Eth_InjectRxFrame -> nd500_xmsg_frame_in
 *   TX: NDIX XETHER -> nd500x uplink seam -> eth_tx_ring -> worker polls
 *       Nd500_Eth_PollTxFrame -> gateway 0x31
 *
 * Deliberately the same shape as HDLC_InjectRxFrame / HDLC_PollTxFrame in
 * nd100wasm.c. A browser cannot be called back into synchronously from C
 * without EM_ASM, and the worker already polls once per frame for HDLC, so
 * ethernet rides the same loop rather than inventing a second mechanism.
 *
 * WHY A POLL RATHER THAN A PUSH, given nd500x's seam was designed for a
 * WebSocket that pushes: the PUSH direction that matters is inbound (a frame
 * arriving from the gateway calls straight into nd500_xmsg_frame_in with no
 * polling at all, which is the part that would otherwise need
 * nd500_xmsg_set_uplink_poll). Outbound has to cross into JS, and that is a
 * queue whichever way round it is written.
 */

// clang-format off
#define ND500_ETH_TX_RING  16
#define ND500_ETH_MAX_FRAME 2048     /* RETH's limit, gateway.js RETH_MAX_FRAME */
// clang-format on

static struct
{
    int segment;
    int length;
    uint8_t data[ND500_ETH_MAX_FRAME];
} g_eth_tx_ring[ND500_ETH_TX_RING];

static int g_eth_tx_head = 0, g_eth_tx_tail = 0;
static int g_eth_last_seg = 0, g_eth_last_len = 0;
static uint8_t *g_eth_last_buf = 0;
static int g_eth_segment = 0; /* which segment this machine is on   */
static unsigned long g_eth_tx_dropped = 0;

/* NDIX transmitted a frame. Called from nd500x's XMSG server through the
 * one-slot uplink seam. Must not block and must not call into JS. */
static void eth_frame_out(void *ctx, const uint8_t *frame, uint32_t len)
{
    int next;
    (void)ctx;
    if (!frame || len == 0 || len > ND500_ETH_MAX_FRAME)
    {
        return;
    }

    next = (g_eth_tx_head + 1) % ND500_ETH_TX_RING;
    if (next == g_eth_tx_tail)
    {
        /* Ring full: the page is not draining. Drop and count rather than
         * block - ethernet is allowed to lose frames, and stalling the guest's
         * CPU inside a transmit would be far worse than a lost packet. */
        g_eth_tx_dropped++;
        return;
    }
    g_eth_tx_ring[g_eth_tx_head].segment = g_eth_segment;
    g_eth_tx_ring[g_eth_tx_head].length = (int)len;
    memcpy(g_eth_tx_ring[g_eth_tx_head].data, frame, len);
    g_eth_tx_head = next;
}

/* Put this machine on a segment and start carrying frames. Safe to call twice;
 * the uplink slot holds one function pointer and re-registering is harmless. */
EMSCRIPTEN_EXPORT int Nd500_Eth_Attach(int segment)
{
    if (!g_created)
    {
        return -1;
    }
    g_eth_segment = segment;
    g_eth_tx_head = g_eth_tx_tail = 0;
    g_eth_tx_dropped = 0;
    nd500_xmsg_set_uplink(eth_frame_out, NULL);
    /* No set_uplink_poll: a frame arriving from the gateway calls
     * Nd500_Eth_InjectRxFrame directly, so there is nothing to poll. This is
     * the case nd500_xmsg.h's poll comment says the seam was designed for. */
    return 0;
}

/* Stop carrying frames. The guest keeps running with an ethernet that has
 * nothing on the other end, which is what it had before this was called. */
EMSCRIPTEN_EXPORT void Nd500_Eth_Detach(void)
{
    nd500_xmsg_set_uplink(NULL, NULL);
    g_eth_tx_head = g_eth_tx_tail = 0;
}

/* A frame arrived from the segment (gateway type 0x30). */
EMSCRIPTEN_EXPORT int Nd500_Eth_InjectRxFrame(int segment, const uint8_t *data, int length)
{
    if (!g_created || !g_booted || !data)
    {
        return -1;
    }
    if (length <= 0 || length > ND500_ETH_MAX_FRAME)
    {
        return -1;
    }
    if (segment != g_eth_segment)
    {
        return -1; /* not this machine's wire */
    }
    /* frame_in raises the receive interrupt as well as queueing, which is what
     * makes NDIX come and collect it. Its return says whether the guest had
     * room; a full guest queue is counted inside nd500x, not here. */
    return nd500_xmsg_frame_in(&g_cpu, data, (uint32_t)length);
}

/* Poll for one frame NDIX transmitted. 1 = a frame is ready, 0 = nothing.
 * Same three-getter idiom as HDLC_PollTxFrame so the worker code matches. */
EMSCRIPTEN_EXPORT int Nd500_Eth_PollTxFrame(void)
{
    if (g_eth_tx_head == g_eth_tx_tail)
    {
        return 0;
    }
    g_eth_last_seg = g_eth_tx_ring[g_eth_tx_tail].segment;
    g_eth_last_len = g_eth_tx_ring[g_eth_tx_tail].length;
    g_eth_last_buf = g_eth_tx_ring[g_eth_tx_tail].data;
    g_eth_tx_tail = (g_eth_tx_tail + 1) % ND500_ETH_TX_RING;
    return 1;
}

EMSCRIPTEN_EXPORT int Nd500_Eth_GetLastTxSegment(void)
{
    return g_eth_last_seg;
}
EMSCRIPTEN_EXPORT int Nd500_Eth_GetLastTxLength(void)
{
    return g_eth_last_len;
}
EMSCRIPTEN_EXPORT uint8_t *Nd500_Eth_GetLastTxBuffer(void)
{
    return g_eth_last_buf;
}

/* How many outbound frames were dropped because the page stopped draining.
 * Exported rather than only logged: "the network is slow" and "the browser tab
 * is not polling" look identical from inside the guest. */
EMSCRIPTEN_EXPORT unsigned long Nd500_Eth_GetTxDropped(void)
{
    return g_eth_tx_dropped;
}

/* Gateway type 0x32. Informational for now - NDIX has no carrier-sense concept
 * and et0 stays up either way - but the page can show it, and a future uplink
 * could use it to stop queueing into a segment with nobody on it. */
EMSCRIPTEN_EXPORT void Nd500_Eth_SetLink(int segment, int present)
{
    (void)segment;
    (void)present;
}

#else /* ---------------------------------------------- no nd500x checkout */

/*
 * The same names, doing nothing. EXPORTED_FUNCTIONS lists them unconditionally
 * and emscripten fails the link on a name it cannot find, so the absence of an
 * ND-500 has to be something the page ASKS about (Nd500_Available returns 0)
 * rather than a module that will not load.
 */
EMSCRIPTEN_EXPORT int Nd500_Available(void)
{
    return 0;
}
EMSCRIPTEN_EXPORT int Nd500_SetEnv(const char *n, const char *v)
{
    (void)n;
    (void)v;
    return -1;
}
EMSCRIPTEN_EXPORT int Nd500_Create(int mem_bytes)
{
    (void)mem_bytes;
    return -1;
}
EMSCRIPTEN_EXPORT int Nd500_IsCreated(void)
{
    return 0;
}
EMSCRIPTEN_EXPORT int Nd500_IsBooted(void)
{
    return 0;
}
EMSCRIPTEN_EXPORT int Nd500_LoadKernel(uint8_t *d, int n)
{
    (void)d;
    (void)n;
    return -1;
}
EMSCRIPTEN_EXPORT int Nd500_LoadSegments(uint8_t *p, int pn, uint8_t *d, int dn)
{
    (void)p;
    (void)pn;
    (void)d;
    (void)dn;
    return -1;
}
EMSCRIPTEN_EXPORT int Nd500_MountDisk(int u, uint8_t *d, int n, int w)
{
    (void)u;
    (void)d;
    (void)n;
    (void)w;
    return -1;
}
EMSCRIPTEN_EXPORT int Nd500_UnmountDisk(int u)
{
    (void)u;
    return -1;
}
EMSCRIPTEN_EXPORT uint8_t *Nd500_GetDiskBuffer(int u)
{
    (void)u;
    return 0;
}
EMSCRIPTEN_EXPORT int Nd500_GetDiskSize(int u)
{
    (void)u;
    return 0;
}
EMSCRIPTEN_EXPORT int Nd500_Boot(void)
{
    return -1;
}
EMSCRIPTEN_EXPORT int Nd500_Step(int count)
{
    (void)count;
    return -1;
}
EMSCRIPTEN_EXPORT int Nd500_GetStopReason(void)
{
    return 0;
}
EMSCRIPTEN_EXPORT int Nd500_IsRunning(void)
{
    return 0;
}
EMSCRIPTEN_EXPORT const char *Nd500_GetStopReasonText(void)
{
    return "no ND-500 in this build";
}
EMSCRIPTEN_EXPORT uint32_t Nd500_GetPC(void)
{
    return 0u;
}
EMSCRIPTEN_EXPORT int Nd500_ReadPhys(uint32_t a, uint8_t *d, int n)
{
    (void)a;
    (void)d;
    (void)n;
    return 0;
}
EMSCRIPTEN_EXPORT int Nd500_WritePhys(uint32_t a, const uint8_t *s, int n)
{
    (void)a;
    (void)s;
    (void)n;
    return 0;
}
EMSCRIPTEN_EXPORT uint32_t Nd500_MemorySize(void)
{
    return 0u;
}
EMSCRIPTEN_EXPORT uint32_t Nd500_TranslateVirt(uint32_t v)
{
    (void)v;
    return 0xFFFFFFFFu;
}
EMSCRIPTEN_EXPORT int Nd500_PollConsole(void)
{
    return -1;
}
EMSCRIPTEN_EXPORT void Nd500_SendInput(int u, const char *t, int n)
{
    (void)u;
    (void)t;
    (void)n;
}
EMSCRIPTEN_EXPORT int Nd500_Eth_Attach(int s)
{
    (void)s;
    return -1;
}
EMSCRIPTEN_EXPORT void Nd500_Eth_Detach(void)
{
}
EMSCRIPTEN_EXPORT int Nd500_Eth_InjectRxFrame(int s, const uint8_t *d, int n)
{
    (void)s;
    (void)d;
    (void)n;
    return -1;
}
EMSCRIPTEN_EXPORT int Nd500_Eth_PollTxFrame(void)
{
    return 0;
}
EMSCRIPTEN_EXPORT int Nd500_Eth_GetLastTxSegment(void)
{
    return 0;
}
EMSCRIPTEN_EXPORT int Nd500_Eth_GetLastTxLength(void)
{
    return 0;
}
EMSCRIPTEN_EXPORT uint8_t *Nd500_Eth_GetLastTxBuffer(void)
{
    return 0;
}
EMSCRIPTEN_EXPORT unsigned long Nd500_Eth_GetTxDropped(void)
{
    return 0;
}
EMSCRIPTEN_EXPORT void Nd500_Eth_SetLink(int s, int p)
{
    (void)s;
    (void)p;
}

#endif /* ND100X_WITH_ND500 */
