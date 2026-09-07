// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <stdlib.h>
#include "LibnxPlatform.h"

typedef struct AttachedThread
{
    void* owner;
    Handle handle;
    void (*onExit)(void*);
    struct AttachedThread* next;
} AttachedThread;

typedef struct OwnedThread
{
    Thread thread;
    uint32_t (*callback)(void*);
    void* argument;
    struct OwnedThread* next;
} OwnedThread;

static Mutex s_attachedLock;
static Mutex s_ownedLock;
static AttachedThread* s_attached;
static CondVar s_finishedCondition;
static OwnedThread* s_finished;
static OwnedThread** s_finishedTail = &s_finished;
static Thread s_reaper;
static bool s_reaperStarted;
#ifdef LIBNX_THREAD_TESTING
static uint32_t s_ownedStarted;
static uint32_t s_ownedReaped;
#endif
static int s_tlsSlot = -1;
static _Thread_local Result s_threadError;
static uint64_t s_retiredCpuTicks;
static bool s_cpuTicksValid = true;

static bool ReadThreadCpuTicks(Handle handle, uint64_t* ticks)
{
    Result result = svcGetInfo(ticks, InfoType_ThreadTickCount, handle, TickCountInfo_Total);
    if (R_FAILED(result))
        result = svcGetInfo(ticks, InfoType_ThreadTickCountDeprecated, handle, TickCountInfo_Total);
    return R_SUCCEEDED(result);
}

static void ThreadExit(void* value)
{
    AttachedThread* entry = value;
    if (entry->onExit)
        entry->onExit(entry->owner);
    mutexLock(&s_attachedLock);
    uint64_t ticks;
    if (ReadThreadCpuTicks(entry->handle, &ticks))
        s_retiredCpuTicks += ticks;
    else
        s_cpuTicksValid = false;
    AttachedThread** current = &s_attached;
    while (*current && *current != entry)
        current = &(*current)->next;
    if (*current)
        *current = entry->next;
    mutexUnlock(&s_attachedLock);
    free(entry);
}

bool LibnxThreadsInitialize(void)
{
    mutexLock(&s_attachedLock);
    if (s_tlsSlot < 0)
        s_tlsSlot = threadTlsAlloc(ThreadExit);
    bool result = s_tlsSlot >= 0;
    mutexUnlock(&s_attachedLock);
    return result;
}

bool LibnxThreadAttach(void* owner, void (*onExit)(void*))
{
    if (s_tlsSlot < 0 || !owner)
        return false;
    AttachedThread* existing = threadTlsGet(s_tlsSlot);
    if (existing)
        return existing->owner == owner;
    AttachedThread* entry = calloc(1, sizeof(*entry));
    if (!entry)
        return false;
    entry->owner = owner;
    entry->onExit = onExit;
    entry->handle = threadGetCurHandle();
    mutexLock(&s_attachedLock);
    entry->next = s_attached;
    s_attached = entry;
    threadTlsSet(s_tlsSlot, entry);
    mutexUnlock(&s_attachedLock);
    return true;
}

static Handle FindThread(void* owner)
{
    for (AttachedThread* entry = s_attached; entry; entry = entry->next)
    {
        if (entry->owner == owner)
            return entry->handle;
    }
    return INVALID_HANDLE;
}

bool LibnxThreadPause(void* owner, void* context)
{
    mutexLock(&s_attachedLock);
    Handle handle = FindThread(owner);
    bool valid = handle != INVALID_HANDLE && handle != threadGetCurHandle();
    s_threadError = valid ? svcSetThreadActivity(handle, ThreadActivity_Paused)
                         : MAKERESULT(Module_Libnx, LibnxError_BadInput);
#ifdef LIBNX_THREAD_TESTING
    svcOutputDebugString("[AOTTHR] pause.svc_returned=1\n", sizeof("[AOTTHR] pause.svc_returned=1\n") - 1);
#endif
    if (s_threadError == 0)
    {
        s_threadError = svcGetThreadContext3(context, handle);
#ifdef LIBNX_THREAD_TESTING
        svcOutputDebugString("[AOTTHR] context.svc_returned=1\n", sizeof("[AOTTHR] context.svc_returned=1\n") - 1);
#endif
        if (s_threadError != 0)
            svcSetThreadActivity(handle, ThreadActivity_Runnable);
    }
    mutexUnlock(&s_attachedLock);
    return s_threadError == 0;
}

bool LibnxThreadResume(void* owner)
{
    mutexLock(&s_attachedLock);
    Handle handle = FindThread(owner);
    s_threadError = handle != INVALID_HANDLE ? svcSetThreadActivity(handle, ThreadActivity_Runnable)
                                            : MAKERESULT(Module_Libnx, LibnxError_BadInput);
    mutexUnlock(&s_attachedLock);
    return s_threadError == 0;
}

bool LibnxFlushThreadWrites(void)
{
    // Capture each registered thread only while it is stopped, then immediately
    // resume it. No allocation or runtime callback is allowed while stopped.
    // This is separate from GC suspension; native/preemptive threads resume here.
    mutexLock(&s_attachedLock);
    Handle current = threadGetCurHandle();
    bool result = true;
    for (AttachedThread* entry = s_attached; entry; entry = entry->next)
    {
        if (entry->handle == current)
            continue;
        ThreadContext context;
        s_threadError = svcSetThreadActivity(entry->handle, ThreadActivity_Paused);
        if (s_threadError != 0)
        {
            result = false;
            break;
        }
        Result captured = svcGetThreadContext3(&context, entry->handle);
        Result resumed = svcSetThreadActivity(entry->handle, ThreadActivity_Runnable);
        if (captured != 0 || resumed != 0)
        {
            s_threadError = captured != 0 ? captured : resumed;
            result = false;
            break;
        }
    }
    __sync_synchronize();
    mutexUnlock(&s_attachedLock);
    return result;
}

bool LibnxGetStackBounds(void** low, void** high)
{
    Thread* thread = threadGetSelf();
    if (!thread || !thread->stack_mirror || !thread->stack_sz)
        return false;
    *low = thread->stack_mirror;
    *high = (char*)thread->stack_mirror + thread->stack_sz;
    return true;
}

static void OwnedThreadEntry(void* value)
{
    OwnedThread* entry = value;
    entry->callback(entry->argument);
    mutexLock(&s_ownedLock);
    *s_finishedTail = entry;
    s_finishedTail = &entry->next;
    condvarWakeOne(&s_finishedCondition);
    mutexUnlock(&s_ownedLock);
}

static void ReaperEntry(void* unused)
{
    (void)unused;
    for (;;)
    {
        mutexLock(&s_ownedLock);
        while (!s_finished)
        {
            if (R_FAILED(condvarWait(&s_finishedCondition, &s_ownedLock)))
                abort();
        }
        OwnedThread* entry = s_finished;
        s_finished = entry->next;
        if (!s_finished)
            s_finishedTail = &s_finished;
        mutexUnlock(&s_ownedLock);
        // The entry wrapper queues before libnx runs TLS destructors. Wait for
        // actual kernel termination before reclaiming the stack and TLS storage.
        if (R_FAILED(threadWaitForExit(&entry->thread)) || R_FAILED(threadClose(&entry->thread)))
            abort();
        free(entry);
#ifdef LIBNX_THREAD_TESTING
        __atomic_add_fetch(&s_ownedReaped, 1, __ATOMIC_RELEASE);
#endif
    }
}

static Result CreateKernelThread(Thread* thread, void (*callback)(void*), void* argument, size_t stackSize)
{
    Result result = threadCreate(thread, callback, argument, NULL, stackSize, 0x2C, -2);
    if (R_FAILED(result))
        return result;
    uint64_t coreMask = 0;
    result = svcGetInfo(&coreMask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
    if (result == 0)
    {
        int idealCore = svcGetCurrentProcessorNumber();
        for (int core = 0; core < 4; core++)
        {
            if ((coreMask & (UINT64_C(1) << core)) && core != idealCore)
            {
                idealCore = core;
                break;
            }
        }
        result = svcSetThreadCoreMask(thread->handle, idealCore, coreMask);
    }
    if (result == 0)
        result = threadStart(thread);
    if (result != 0)
        threadClose(thread);
    return result;
}

bool LibnxStartThread(uint32_t (*callback)(void*), void* argument, size_t stackSize)
{
    if (!callback || stackSize > SIZE_MAX - 0xFFF)
    {
        s_threadError = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        return false;
    }
    if (stackSize == 0)
        stackSize = 1024 * 1024;
    if (stackSize < 0x4000)
        stackSize = 0x4000;
    stackSize = (stackSize + 0xFFF) & ~(size_t)0xFFF;

    mutexLock(&s_ownedLock);
    if (!s_reaperStarted)
    {
        s_threadError = CreateKernelThread(&s_reaper, ReaperEntry, NULL, 0x10000);
        if (s_threadError != 0)
        {
            mutexUnlock(&s_ownedLock);
            return false;
        }
        s_reaperStarted = true;
    }
    OwnedThread* entry = calloc(1, sizeof(*entry));
    if (!entry)
    {
        mutexUnlock(&s_ownedLock);
        return false;
    }
    entry->callback = callback;
    entry->argument = argument;
    s_threadError = CreateKernelThread(&entry->thread, OwnedThreadEntry, entry, stackSize);
    if (s_threadError == 0)
    {
#ifdef LIBNX_THREAD_TESTING
        __atomic_add_fetch(&s_ownedStarted, 1, __ATOMIC_RELEASE);
#endif
    }
    else
        free(entry);
    mutexUnlock(&s_ownedLock);
    return s_threadError == 0;
}

#ifdef LIBNX_THREAD_TESTING
void LibnxThreadTestCounts(uint32_t* started, uint32_t* reaped)
{
    *started = __atomic_load_n(&s_ownedStarted, __ATOMIC_ACQUIRE);
    *reaped = __atomic_load_n(&s_ownedReaped, __ATOMIC_ACQUIRE);
}
#endif

uint32_t LibnxThreadLastError(void)
{
    return s_threadError;
}

bool LibnxGetRuntimeCpuTicks(uint64_t* ticks)
{
    mutexLock(&s_attachedLock);
    uint64_t total = s_retiredCpuTicks;
    bool valid = s_cpuTicksValid;
    for (AttachedThread* entry = s_attached; valid && entry; entry = entry->next)
    {
        uint64_t current;
        valid = ReadThreadCpuTicks(entry->handle, &current);
        if (valid)
            total += current;
    }
    mutexUnlock(&s_attachedLock);
    if (valid)
        *ticks = total;
    return valid;
}
