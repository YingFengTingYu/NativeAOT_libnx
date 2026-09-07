// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include "LibnxPlatform.h"

u32 __nx_applet_type = AppletType_None;
size_t __nx_heap_size = 128 * 1024 * 1024;

static void Record(const char* name, unsigned long long value)
{
    char buffer[128];
    int count = snprintf(buffer, sizeof(buffer), "[AOTMEM] %s=%llu\n", name, value);
    if (count > 0 && count < (int)sizeof(buffer))
        svcOutputDebugString(buffer, count);
}

static bool CheckMemoryType(void* address, uint32_t type)
{
    MemoryInfo info;
    uint32_t page;
    return R_SUCCEEDED(svcQueryMemory(&info, &page, (uintptr_t)address)) && info.type == type;
}

static bool MemoryChecks(void)
{
    if (LibnxMemoryReserve(0, 0x1000) || LibnxMemoryReserve(1, 0x1000) ||
        LibnxMemoryReserve(0x1000, 0x3000))
        return false;
    unsigned char* memory = LibnxMemoryReserve(0x4000, 0x10000);
    Record("memory.reserve", memory != NULL && ((uintptr_t)memory % 0x10000) == 0);
    if (!memory)
        return false;
    if (!CheckMemoryType(memory, MemType_Unmapped) || !LibnxMemoryCommit(memory, 0x2000))
    {
        Record("memory.map_error", LibnxMemoryLastError());
        LibnxMemoryRelease(memory, 0x4000);
        return false;
    }
    for (size_t index = 0; index < 0x2000; index++)
    {
        if (memory[index] != 0)
            return false;
        memory[index] = (unsigned char)(index * 7 + 3);
    }
    if (!LibnxMemoryCommit(memory, 0x2000))
        return false;
    for (size_t index = 0; index < 0x2000; index++)
    {
        if (memory[index] != (unsigned char)(index * 7 + 3))
            return false;
    }
    Record("memory.repeat_commit", 1);
    if (LibnxMemoryCommit(memory + 1, 0x1000) || LibnxMemoryCommit(memory + 0x4000, 0x1000) ||
        LibnxMemoryRelease(memory + 0x1000, 0x1000))
        return false;
    Record("memory.bounds", 1);
    if (!LibnxMemoryDecommit(memory, 0x1000) || !CheckMemoryType(memory, MemType_Unmapped) ||
        !LibnxMemoryCommit(memory, 0x1000))
        return false;
    for (size_t index = 0; index < 0x1000; index++)
    {
        if (memory[index] != 0 || memory[index + 0x1000] != (unsigned char)((index + 0x1000) * 7 + 3))
            return false;
    }
    Record("memory.recommit_zero", 1);
    LibnxMemoryTestAllocationLimit(1);
    bool unexpectedlyCommitted = LibnxMemoryCommit(memory, 0x4000);
    LibnxMemoryTestAllocationLimit(-1);
    if (unexpectedlyCommitted || !CheckMemoryType(memory + 0x2000, MemType_Unmapped) ||
        !CheckMemoryType(memory + 0x3000, MemType_Unmapped) || memory[0x1000] != 3)
        return false;
    Record("memory.commit_rollback", 1);
    bool released = LibnxMemoryRelease(memory, 0x4000) && CheckMemoryType(memory, MemType_Unmapped);
    Record("memory.release", released);
    return released;
}

static void SignalWorker(void* event)
{
    svcSleepThread(10000000);
    LibnxEventSet(event);
}

static bool EventChecks(void)
{
    void* event = LibnxEventCreate(false, false);
    if (!event || LibnxEventWait(event, 0) != 258)
        return false;
    Thread worker;
    if (R_FAILED(threadCreate(&worker, SignalWorker, event, NULL, 0x10000, 0x2C, -2)) ||
        R_FAILED(threadStart(&worker)))
        return false;
    bool passed = LibnxEventWait(event, 1000) == 0 && LibnxEventWait(event, 0) == 258;
    threadWaitForExit(&worker);
    threadClose(&worker);
    LibnxEventClose(event);
    Record("event.auto_reset", passed);
    event = LibnxEventCreate(true, true);
    passed = passed && event && LibnxEventWait(event, 0) == 0 && LibnxEventWait(event, 0) == 0;
    if (event)
    {
        LibnxEventReset(event);
        passed = passed && LibnxEventWait(event, 0) == 258;
        LibnxEventClose(event);
    }
    Record("event.manual_reset", passed);
    return passed;
}

int main(void)
{
    Record("begin", 1);
    bool memory = MemoryChecks();
    Record("memory.checks", memory);
    bool events = EventChecks();
    Record("events.checks", events);
    Record("pass", memory && events);
    return memory && events ? 0 : 1;
}
