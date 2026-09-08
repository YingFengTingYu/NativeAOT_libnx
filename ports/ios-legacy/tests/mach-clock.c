// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include <minipal/time.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static atomic_int s_start;
static atomic_int s_failures;

static void* RunClockChecks(void* unused)
{
    (void)unused;
    while (!atomic_load(&s_start))
        sched_yield();

    int64_t previous = 0;
    for (int i = 0; i < 10000; i++)
    {
        uint64_t before = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        int64_t ticks = minipal_hires_ticks();
        uint64_t after = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        if (ticks < previous || (uint64_t)ticks < before || (uint64_t)ticks > after)
            atomic_fetch_add(&s_failures, 1);
        previous = ticks;

        int64_t low = minipal_lowres_ticks();
        int64_t latest = minipal_hires_ticks();
        if (low < ticks / 1000000 || low > latest / 1000000)
            atomic_fetch_add(&s_failures, 1);
    }
    return NULL;
}

int main(void)
{
    pthread_t threads[8];
    if (minipal_hires_tick_frequency() != 1000000000)
        return 1;

    for (int i = 0; i < 8; i++)
    {
        if (pthread_create(&threads[i], NULL, RunClockChecks, NULL) != 0)
            return 2;
    }
    atomic_store(&s_start, 1);
    for (int i = 0; i < 8; i++)
        pthread_join(threads[i], NULL);

    if (atomic_load(&s_failures) != 0)
    {
        fprintf(stderr, "Mach clock failures: %d\n", atomic_load(&s_failures));
        return 3;
    }
    puts("PASS: Mach clock fallback agrees with the OS clock across 8 threads");
    return 0;
}
