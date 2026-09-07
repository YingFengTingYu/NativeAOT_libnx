// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <stdlib.h>
#include "LibnxPlatform.h"

void* LibnxEventCreate(bool manualReset, bool initiallySignaled)
{
    UEvent* event = malloc(sizeof(*event));
    if (!event)
        return NULL;
    ueventCreate(event, !manualReset);
    if (initiallySignaled)
        ueventSignal(event);
    return event;
}

void LibnxEventSet(void* event)
{
    ueventSignal(event);
}

void LibnxEventReset(void* event)
{
    ueventClear(event);
}

uint32_t LibnxEventWait(void* event, uint32_t milliseconds)
{
    uint64_t timeout = milliseconds == UINT32_MAX ? UINT64_MAX : (uint64_t)milliseconds * 1000000;
    Result result = waitSingle(waiterForUEvent(event), timeout);
    if (R_SUCCEEDED(result))
        return 0;
    return R_VALUE(result) == KERNELRESULT(TimedOut) ? 258 : UINT32_MAX;
}

void LibnxEventClose(void* event)
{
    // The caller must ensure that no thread is waiting on this event.
    free(event);
}
