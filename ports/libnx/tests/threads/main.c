// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <stdio.h>
#include "LibnxPlatform.h"

u32 __nx_applet_type = AppletType_None;
static unsigned s_mainOwner, s_workerOwner;
static unsigned s_ready, s_stop, s_exited;
static uint64_t s_counter;

static void Record(const char* name, unsigned long long value)
{
    char buffer[128];
    int count = snprintf(buffer, sizeof(buffer), "[AOTTHR] %s=%llu\n", name, value);
    if (count > 0 && count < (int)sizeof(buffer))
        svcOutputDebugString(buffer, count);
}
static void OnExit(void* owner)
{
    __atomic_store_n(&s_exited, owner == &s_workerOwner, __ATOMIC_RELEASE);
}
static void Worker(void* ignored)
{
    (void)ignored;
    Record("worker.attach_begin", 1);
    bool attached = LibnxThreadAttach(&s_workerOwner, OnExit);
    Record("worker.attached", attached);
    __atomic_store_n(&s_ready, attached ? 1 : 2, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&s_stop, __ATOMIC_ACQUIRE))
        __atomic_fetch_add(&s_counter, 1, __ATOMIC_RELAXED);
}

int main(void)
{
    Record("begin", 1);
    if (!LibnxThreadsInitialize() || !LibnxThreadAttach(&s_mainOwner, NULL))
        return 1;
    void* low;
    void* high;
    uintptr_t local = (uintptr_t)&low;
    bool stack = LibnxGetStackBounds(&low, &high) && local >= (uintptr_t)low && local < (uintptr_t)high;
    Record("stack.bounds", stack);
    uint64_t allowedMask = 0;
    svcGetInfo(&allowedMask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
    int workerCore = -1;
    for (int core = 0; core < 4; core++)
    {
        if ((allowedMask & (UINT64_C(1) << core)) && core != (int)LibnxCurrentCpu())
        {
            workerCore = core;
            break;
        }
    }
    if (workerCore < 0)
        return 6;
    Record("worker.core", workerCore);
    Thread worker;
    if (R_FAILED(threadCreate(&worker, Worker, NULL, NULL, 0x10000, 0x2C, workerCore)) ||
        R_FAILED(threadStart(&worker)))
        return 2;
    Record("worker.started", 1);
    for (unsigned count = 0; count < 1000 && !__atomic_load_n(&s_ready, __ATOMIC_ACQUIRE); count++)
        LibnxSleep(1);
    if (__atomic_load_n(&s_ready, __ATOMIC_ACQUIRE) != 1)
        return 3;
    ThreadContext context;
    Record("thread.pause_requested", 1);
    bool paused = LibnxThreadPause(&s_workerOwner, &context);
    Record("thread.pause", paused);
    if (!paused)
    {
        Record("thread.error", LibnxThreadLastError());
        __atomic_store_n(&s_stop, 1, __ATOMIC_RELEASE);
        threadWaitForExit(&worker);
        threadClose(&worker);
        return 4;
    }
    uint64_t before = __atomic_load_n(&s_counter, __ATOMIC_RELAXED);
    LibnxSleep(20);
    bool stopped = before == __atomic_load_n(&s_counter, __ATOMIC_RELAXED) && context.pc.x && context.sp;
    Record("thread.stopped_context", stopped);
    bool resumed = LibnxThreadResume(&s_workerOwner);
    LibnxSleep(20);
    resumed = resumed && __atomic_load_n(&s_counter, __ATOMIC_RELAXED) > before;
    Record("thread.resume", resumed);
    bool rendezvous = LibnxFlushThreadWrites();
    Record("thread.rendezvous", rendezvous);
    __atomic_store_n(&s_stop, 1, __ATOMIC_RELEASE);
    threadWaitForExit(&worker);
    threadClose(&worker);
    bool exited = __atomic_load_n(&s_exited, __ATOMIC_ACQUIRE) == 1;
    Record("thread.exit_callback", exited);
    uint64_t total, available;
    bool info = LibnxHeapInfo(&total, &available) && total && available <= total && LibnxCpuCount();
    Record("system.info", info);
    Record("pass", stack && paused && stopped && resumed && rendezvous && exited && info);
    return 0;
}
