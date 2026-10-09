/*
 * bootstatus.c - "Booting from ..." line shown until the guest first prints
 * on the console. See bootstatus.h.
 *
 * The line is redrawn in place with '\r' and erased by overwriting it with
 * spaces, so no ANSI erase sequence is needed (the Windows console then needs
 * no VT mode). ASCII only.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <io.h>
#define BS_ISATTY(fd) _isatty(fd)
#define BS_FILENO(f) _fileno(f)
#else
#include <unistd.h>
#define BS_ISATTY(fd) isatty(fd)
#define BS_FILENO(f) fileno(f)
#endif

#include "bootstatus.h"

#define BS_REDRAW_MS 100
#define BS_LINE_LEN 160

static bool s_active = false;
static struct timespec s_start;
static struct timespec s_last;
static int s_spin = 0;
static size_t s_drawn = 0; /* length of the line currently on screen */
static char s_what[96];

static long ms_between(const struct timespec *a, const struct timespec *b)
{
    return (long)((b->tv_sec - a->tv_sec) * 1000L + (b->tv_nsec - a->tv_nsec) / 1000000L);
}

static void draw(const char *line)
{
    size_t len = strlen(line);

    fputc('\r', stdout);
    fputs(line, stdout);
    /* Blank out what is left of a longer previous line. */
    for (size_t i = len; i < s_drawn; i++)
    {
        fputc(' ', stdout);
    }
    if (s_drawn > len)
    {
        fputc('\r', stdout);
        fputs(line, stdout);
    }
    fflush(stdout);
    s_drawn = len;
}

static void redraw(bool waiting)
{
    static const char spinner[4] = {'|', '/', '-', '\\'};
    char line[BS_LINE_LEN];

    if (waiting)
    {
        snprintf(line, sizeof(line), "Booting from %s  -  waiting for debugger   F12 = menu",
                 s_what);
    }
    else
    {
        double secs = (double)ms_between(&s_start, &s_last) / 1000.0;
        snprintf(line, sizeof(line), "Booting from %s  %c  %.1f s   F12 = menu", s_what,
                 spinner[s_spin], secs);
        s_spin = (s_spin + 1) % 4;
    }
    draw(line);
}

void boot_status_start(const char *what)
{
#if defined(__EMSCRIPTEN__)
    (void)what;
    return;
#else
    if (s_active || !BS_ISATTY(BS_FILENO(stdout)))
    {
        return;
    }
    snprintf(s_what, sizeof(s_what), "%s", what ? what : "?");
    clock_gettime(CLOCK_MONOTONIC, &s_start);
    s_last = s_start;
    s_spin = 0;
    s_drawn = 0;
    s_active = true;
    redraw(false);
#endif
}

void boot_status_tick(bool waiting)
{
    struct timespec now;

    if (!s_active)
    {
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (ms_between(&s_last, &now) < BS_REDRAW_MS)
    {
        return;
    }
    s_last = now;
    redraw(waiting);
}

void boot_status_done(void)
{
    if (!s_active)
    {
        return;
    }
    s_active = false;
    fputc('\r', stdout);
    for (size_t i = 0; i < s_drawn; i++)
    {
        fputc(' ', stdout);
    }
    fputc('\r', stdout);
    fflush(stdout);
    s_drawn = 0;
}

bool boot_status_active(void)
{
    return s_active;
}
