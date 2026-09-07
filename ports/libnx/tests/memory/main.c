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
    unsigned char* rollback = LibnxMemoryReserve(0x30000, 0x1000);
    if (!rollback || !LibnxMemoryCommit(rollback, 0x10000))
        return false;
    rollback[0] = 0x91;
    LibnxMemoryTestAllocationLimit(1);
    bool unexpectedlyCommitted = LibnxMemoryCommit(rollback, 0x30000);
    LibnxMemoryTestAllocationLimit(-1);
    if (unexpectedlyCommitted || !CheckMemoryType(rollback + 0x10000, MemType_Unmapped) ||
        !CheckMemoryType(rollback + 0x20000, MemType_Unmapped) || rollback[0] != 0x91 ||
        !LibnxMemoryRelease(rollback, 0x30000))
        return false;
    Record("memory.commit_rollback", 1);
    bool released = LibnxMemoryRelease(memory, 0x4000) && CheckMemoryType(memory, MemType_Unmapped);
    Record("memory.release", released);
    return released;
}

static bool LargeMemoryChecks(void)
{
    const size_t size = 64 * 1024 * 1024;
    unsigned char* memory = LibnxMemoryReserve(size, 0x10000);
    if (!memory || !LibnxMemoryCommit(memory, size))
        return false;
    for (size_t offset = 0; offset < size; offset += 0x1000)
    {
        if (memory[offset] != 0)
            return false;
        memory[offset] = (unsigned char)(offset / 0x1000 + 7);
    }
    // Decommit through a backing-block boundary while retaining adjacent pages.
    if (!LibnxMemoryDecommit(memory + 0xF000, 0x2000) ||
        !CheckMemoryType(memory + 0xF000, MemType_Unmapped) ||
        !CheckMemoryType(memory + 0x10000, MemType_Unmapped) ||
        memory[0xE000] != 21 || memory[0x11000] != 24 ||
        !LibnxMemoryCommit(memory + 0xF000, 0x2000) ||
        memory[0xF000] != 0 || memory[0x10000] != 0)
        return false;
    bool released = LibnxMemoryRelease(memory, size) && CheckMemoryType(memory, MemType_Unmapped);
    Record("memory.large_and_partial", released);
    return released;
}

static void SignalWorker(void* event)
{
    svcSleepThread(10000000);
    LibnxEventSet(event);
}

typedef struct
{
    void* event;
    uint32_t result;
} WaitWorkerState;

static void WaitWorker(void* value)
{
    WaitWorkerState* state = value;
    state->result = LibnxEventWait(state->event, 2000);
}

static bool EventBroadcastChecks(void)
{
    void* event = LibnxEventCreate(true, false);
    if (!event)
        return false;
    bool passed = true;
    for (int pass = 0; pass < 64 && passed; pass++)
    {
        Thread threads[2];
        WaitWorkerState states[2] = { { event, UINT32_MAX }, { event, UINT32_MAX } };
        for (int index = 0; index < 2; index++)
        {
            if (R_FAILED(threadCreate(&threads[index], WaitWorker, &states[index], NULL, 0x10000, 0x2C, -2)) ||
                R_FAILED(threadStart(&threads[index])))
                return false;
        }
        uint64_t deadline = armGetSystemTick() + armNsToTicks(1000000000);
        while (LibnxEventTestWaiterCount(event) != 2 && armGetSystemTick() < deadline)
            svcSleepThread(100000);
        passed = LibnxEventTestWaiterCount(event) == 2;
        LibnxEventSet(event);
        LibnxEventReset(event);
        for (int index = 0; index < 2; index++)
        {
            threadWaitForExit(&threads[index]);
            threadClose(&threads[index]);
            passed = passed && states[index].result == 0;
        }
        passed = passed && LibnxEventWait(event, 0) == 258;
    }
    LibnxEventClose(event);
    Record("event.broadcast_reset", passed);
    return passed;
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
    bool memory = MemoryChecks() && LargeMemoryChecks();
    Record("memory.checks", memory);
    bool events = EventChecks() && EventBroadcastChecks();
    Record("events.checks", events);
    Record("pass", memory && events);
    return memory && events ? 0 : 1;
}
