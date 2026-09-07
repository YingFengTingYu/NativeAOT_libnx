// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <stdlib.h>
#include "LibnxPlatform.h"

typedef struct EventWaiter
{
    bool released;
    struct EventWaiter* next;
} EventWaiter;

typedef struct LibnxEvent
{
    Mutex mutex;
    CondVar condition;
    bool manualReset;
    bool signaled;
    EventWaiter* waiters;
} LibnxEvent;

void* LibnxEventCreate(bool manualReset, bool initiallySignaled)
{
    LibnxEvent* event = calloc(1, sizeof(*event));
    if (!event)
        return NULL;
    event->manualReset = manualReset;
    event->signaled = initiallySignaled;
    return event;
}

void LibnxEventSet(void* value)
{
    LibnxEvent* event = value;
    mutexLock(&event->mutex);
    bool released = false;
    for (EventWaiter* waiter = event->waiters; waiter; waiter = waiter->next)
    {
        if (!waiter->released)
        {
            waiter->released = true;
            released = true;
            if (!event->manualReset)
                break;
        }
    }
    if (event->manualReset || !released)
        event->signaled = true;
    if (released)
        condvarWakeAll(&event->condition);
    mutexUnlock(&event->mutex);
}

void LibnxEventReset(void* value)
{
    LibnxEvent* event = value;
    mutexLock(&event->mutex);
    // Reset must not revoke a signal already granted to a waiting thread.
    event->signaled = false;
    mutexUnlock(&event->mutex);
}

uint32_t LibnxEventWait(void* value, uint32_t milliseconds)
{
    LibnxEvent* event = value;
    bool finite = milliseconds != UINT32_MAX;
    uint64_t deadline = finite ? armGetSystemTick() + armNsToTicks((uint64_t)milliseconds * 1000000) : 0;
    mutexLock(&event->mutex);
    if (event->signaled)
    {
        if (!event->manualReset)
            event->signaled = false;
        mutexUnlock(&event->mutex);
        return 0;
    }

    EventWaiter waiter = { false, NULL };
    EventWaiter** tail = &event->waiters;
    while (*tail)
        tail = &(*tail)->next;
    *tail = &waiter;
    uint32_t result = 0;
    while (!waiter.released)
    {
        int64_t remaining = finite ? (int64_t)(deadline - armGetSystemTick()) : 0;
        if (finite && remaining <= 0)
        {
            result = 258;
            break;
        }
        Result waited = condvarWaitTimeout(&event->condition, &event->mutex,
                                           finite ? armTicksToNs(remaining) : UINT64_MAX);
        if (R_FAILED(waited) && R_VALUE(waited) != KERNELRESULT(TimedOut))
        {
            result = UINT32_MAX;
            break;
        }
    }
    EventWaiter** current = &event->waiters;
    while (*current != &waiter)
        current = &(*current)->next;
    *current = waiter.next;
    mutexUnlock(&event->mutex);
    return result;
}

void LibnxEventClose(void* event)
{
    // The caller must ensure that no thread is waiting on this event.
    free(event);
}

#ifdef LIBNX_EVENT_TESTING
uint32_t LibnxEventTestWaiterCount(void* value)
{
    LibnxEvent* event = value;
    mutexLock(&event->mutex);
    uint32_t count = 0;
    for (EventWaiter* waiter = event->waiters; waiter; waiter = waiter->next)
        count++;
    mutexUnlock(&event->mutex);
    return count;
}
#endif
