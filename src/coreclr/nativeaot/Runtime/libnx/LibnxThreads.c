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
static OwnedThread* s_owned;
static int s_tlsSlot = -1;
static _Thread_local Result s_threadError;

static void ThreadExit(void* value)
{
    AttachedThread* entry = value;
    if (entry->onExit)
        entry->onExit(entry->owner);
    mutexLock(&s_attachedLock);
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
}

bool LibnxStartThread(uint32_t (*callback)(void*), void* argument, size_t stackSize)
{
    mutexLock(&s_ownedLock);
    OwnedThread** current = &s_owned;
    while (*current)
    {
        OwnedThread* entry = *current;
        if (R_SUCCEEDED(waitSingleHandle(entry->thread.handle, 0)))
        {
            *current = entry->next;
            threadClose(&entry->thread);
            free(entry);
        }
        else
            current = &entry->next;
    }
    OwnedThread* entry = calloc(1, sizeof(*entry));
    if (!entry)
    {
        mutexUnlock(&s_ownedLock);
        return false;
    }
    entry->callback = callback;
    entry->argument = argument;
    if (stackSize == 0)
        stackSize = 1024 * 1024;
    s_threadError = threadCreate(&entry->thread, OwnedThreadEntry, entry, NULL, stackSize, 0x2C, -2);
    if (s_threadError == 0)
    {
        uint64_t coreMask = 0;
        s_threadError = svcGetInfo(&coreMask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
        if (s_threadError == 0)
            s_threadError = svcSetThreadCoreMask(entry->thread.handle, -1, coreMask);
        if (s_threadError == 0)
            s_threadError = threadStart(&entry->thread);
        if (s_threadError != 0)
            threadClose(&entry->thread);
    }
    if (s_threadError == 0)
    {
        entry->next = s_owned;
        s_owned = entry;
    }
    else
        free(entry);
    mutexUnlock(&s_ownedLock);
    return s_threadError == 0;
}

uint32_t LibnxThreadLastError(void)
{
    return s_threadError;
}
