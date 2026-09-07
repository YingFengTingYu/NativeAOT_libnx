// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <malloc.h>
#include "LibnxPlatform.h"

uint64_t LibnxAllowedCores(void)
{
    uint64_t mask = 0;
    if (R_FAILED(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)))
        return 1;
    return mask ? mask : 1;
}
uint32_t LibnxCpuCount(void) { return __builtin_popcountll(LibnxAllowedCores()); }
bool LibnxSetCurrentThreadAffinity(uint32_t core)
{
    if (core >= 64 || !(LibnxAllowedCores() & (UINT64_C(1) << core)))
        return false;
    return R_SUCCEEDED(svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, UINT64_C(1) << core));
}
uint32_t LibnxCurrentCpu(void) { return svcGetCurrentProcessorNumber(); }
uint32_t LibnxProcessId(void)
{
    uint64_t id = 0;
    Result result = svcGetProcessId(&id, CUR_PROCESS_HANDLE);
    if (R_FAILED(result))
        diagAbortWithResult(result);
    return (uint32_t)id;
}
uint64_t LibnxSystemTick(void) { return armGetSystemTick(); }
uint64_t LibnxTickFrequency(void) { return armGetSystemTickFreq(); }
void LibnxSleep(uint32_t milliseconds) { svcSleepThread((uint64_t)milliseconds * 1000000); }
void LibnxYield(void) { svcSleepThread(-1); }

bool LibnxHeapInfo(uint64_t* total, uint64_t* available)
{
    extern char* fake_heap_start;
    extern char* fake_heap_end;
    if (fake_heap_end < fake_heap_start)
        return false;
    uint64_t size = (uintptr_t)fake_heap_end - (uintptr_t)fake_heap_start;
    struct mallinfo info = mallinfo();
    uint64_t freeBytes = info.arena <= size ? size - info.arena + info.fordblks : 0;
    *total = size;
    *available = freeBytes < size ? freeBytes : size;
    return true;
}

uintptr_t LibnxVirtualLimit(bool endAddress)
{
    uint64_t base = 0, size = 0;
    if (R_FAILED(svcGetInfo(&base, endAddress ? InfoType_AslrRegionAddress : InfoType_StackRegionAddress, CUR_PROCESS_HANDLE, 0)) ||
        R_FAILED(svcGetInfo(&size, endAddress ? InfoType_AslrRegionSize : InfoType_StackRegionSize, CUR_PROCESS_HANDLE, 0)) ||
        size == 0 || base > UINTPTR_MAX - size)
        return 0;
    return endAddress ? base + size - 1 : size;
}
