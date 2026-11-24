/* Windows stand-ins for the POSIX calls nec2c makes, so the pinned upstream source builds
   unchanged with MinGW-w64 (packaging/nec2c/build.sh adds this folder to the include path).

   - times(), struct tms and sysconf(_SC_CLK_TCK) feed only nec2c's run-time report lines,
     never the computed numbers. CPU time comes from GetProcessTimes and is counted in
     milliseconds (a 32-bit clock_t then lasts 24 days), so the clock tick is 1000 per second.
   - sigaction() maps to signal(): nec2c uses it only to print a message on a fatal signal.
     Stopping a run does not depend on it; Workbench terminates and then kills the process.

   windows.h is taken lean and without min/max, and its near/far macros are dropped, since
   nec2c has a struct field named near and its own min(). */
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef near
#undef far

#include <signal.h>
#include <time.h>

struct tms {
    clock_t tms_utime, tms_stime, tms_cutime, tms_cstime;
};

static inline clock_t nec2c_ticks(FILETIME time)
{
    /* FILETIME counts 100 ns intervals. */
    return (clock_t)((((unsigned long long)time.dwHighDateTime << 32) | time.dwLowDateTime) / 10000);
}

static inline clock_t times(struct tms* buffer)
{
    FILETIME created, exited, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    buffer->tms_utime = nec2c_ticks(user);
    buffer->tms_stime = nec2c_ticks(kernel);
    buffer->tms_cutime = buffer->tms_cstime = 0;
    return buffer->tms_utime + buffer->tms_stime;
}

#define _SC_CLK_TCK 2
#define sysconf(name) (1000L)

struct sigaction {
    void (*sa_handler)(int);
    int sa_mask;
    int sa_flags;
};
#define sigemptyset(set) (*(set) = 0)

static inline int sigaction(int number, const struct sigaction* action, struct sigaction* previous)
{
    void (*old)(int) = signal(number, action->sa_handler);
    if (previous) previous->sa_handler = old;
    return old == SIG_ERR ? -1 : 0;
}
