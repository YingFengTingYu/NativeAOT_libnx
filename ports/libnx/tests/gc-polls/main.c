// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>

u32 __nx_applet_type = AppletType_None;
u32 __nx_applet_exit_mode = 1;
extern int managed_gc_poll_main(void);
extern uint64_t RhGetLibnxExternalGcSuspensionCount(void);
static Thread watchdog;
static Event cancel_watchdog;
static atomic_int watchdog_fired;

int gc_poll_external_enabled(void)
{
#ifdef GC_POLL_EXTERNAL_SUSPEND
    return 1;
#else
    return 0;
#endif
}

uint64_t gc_poll_external_count(void)
{
    return RhGetLibnxExternalGcSuspensionCount();
}

void gc_poll_record(int stage, int value)
{
    char message[128];
    int length = snprintf(message, sizeof(message), "[AOTGCPOLL] stage.%d=%d\n", stage, value);
    svcOutputDebugString(message, length);
}

static void watchdog_main(void *release)
{
    if (R_FAILED(eventWait(&cancel_watchdog, 2000000000ULL))) {
        atomic_store_explicit(&watchdog_fired, 1, memory_order_release);
        atomic_store_explicit((atomic_int *)release, 1, memory_order_release);
    }
}

int gc_poll_watchdog_start(void *release)
{
    atomic_store_explicit(&watchdog_fired, 0, memory_order_relaxed);
    Result result = eventCreate(&cancel_watchdog, false);
    if (R_FAILED(result)) return (int)result;
    result = threadCreate(&watchdog, watchdog_main, release, NULL, 0x10000, 0x20, -2);
    if (R_FAILED(result)) { eventClose(&cancel_watchdog); return (int)result; }
    result = threadStart(&watchdog);
    if (R_FAILED(result)) { threadClose(&watchdog); eventClose(&cancel_watchdog); }
    return (int)result;
}

int gc_poll_watchdog_stop(void)
{
    eventFire(&cancel_watchdog);
    threadWaitForExit(&watchdog);
    threadClose(&watchdog);
    eventClose(&cancel_watchdog);
    return atomic_load_explicit(&watchdog_fired, memory_order_acquire);
}

int main(void)
{
#ifdef GC_POLL_EXTERNAL_SUSPEND
    setenv("DOTNET_LibnxExternalGcSuspend", "1", 1);
#endif
    int result = managed_gc_poll_main();
    gc_poll_record(100, result);
    return result;
}
