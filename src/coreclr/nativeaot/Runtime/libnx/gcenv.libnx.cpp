// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include "common.h"
#include "gcenv.h"
#include "LibnxPlatform.h"

static AffinitySet s_gcAffinity;
uint32_t g_pageSizeUnixInl = 0x1000;

static size_t PageAlignedSize(size_t size)
{
    return size <= SIZE_MAX - 0xFFF ? (size + 0xFFF) & ~(size_t)0xFFF : 0;
}

GCEvent::GCEvent() : m_impl(nullptr) {}
void GCEvent::CloseEvent()
{
    if (m_impl)
        LibnxEventClose(m_impl);
    m_impl = nullptr;
}
bool GCEvent::CreateManualEventNoThrow(bool state) { m_impl = static_cast<Impl*>(LibnxEventCreate(true, state)); return m_impl != nullptr; }
bool GCEvent::CreateAutoEventNoThrow(bool state) { m_impl = static_cast<Impl*>(LibnxEventCreate(false, state)); return m_impl != nullptr; }
bool GCEvent::CreateOSManualEventNoThrow(bool state) { return CreateManualEventNoThrow(state); }
bool GCEvent::CreateOSAutoEventNoThrow(bool state) { return CreateAutoEventNoThrow(state); }
void GCEvent::Set() { LibnxEventSet(m_impl); }
void GCEvent::Reset() { LibnxEventReset(m_impl); }
uint32_t GCEvent::Wait(uint32_t timeout, bool alertable) { return LibnxEventWait(m_impl, timeout); }

bool GCToOSInterface::Initialize()
{
    uint64_t total, available;
    return LibnxThreadsInitialize() && LibnxHeapInfo(&total, &available) && total != 0;
}
void GCToOSInterface::Shutdown() {}

void* GCToOSInterface::VirtualReserve(size_t size, size_t alignment, uint32_t flags, uint16_t node)
{
    void* result = flags == 0 ? LibnxMemoryReserve(PageAlignedSize(size), std::max<size_t>(alignment, 0x1000)) : nullptr;
    static unsigned diagnostics;
    if (diagnostics++ < 24)
    {
        char text[192];
        snprintf(text, sizeof(text), "[AOTVM] reserve size=%zu alignment=%zu flags=%u result=%p error=%u\n",
                 size, alignment, flags, result, LibnxMemoryLastError());
        PalPrintFatalError(text);
    }
    return result;
}
bool GCToOSInterface::VirtualRelease(void* address, size_t size) { return LibnxMemoryRelease(address, PageAlignedSize(size)); }
bool GCToOSInterface::VirtualCommit(void* address, size_t size, uint16_t node) { return LibnxMemoryCommit(address, PageAlignedSize(size)); }
bool GCToOSInterface::VirtualDecommit(void* address, size_t size) { return LibnxMemoryDecommit(address, PageAlignedSize(size)); }
void* GCToOSInterface::VirtualReserveAndCommitLargePages(size_t size, uint16_t node) { return nullptr; }
bool GCToOSInterface::VirtualReset(void* address, size_t size, bool unlock) { return false; }
bool GCToOSInterface::SupportsWriteWatch() { return false; }
void GCToOSInterface::ResetWriteWatch(void* address, size_t size) { abort(); }
bool GCToOSInterface::GetWriteWatch(bool reset, void* address, size_t size, void** pages, uintptr_t* count) { return false; }

void GCToOSInterface::Sleep(uint32_t milliseconds) { LibnxSleep(milliseconds); }
void GCToOSInterface::YieldThread(uint32_t count) { LibnxYield(); }
uint32_t GCToOSInterface::GetCurrentProcessorNumber() { return LibnxCurrentCpu(); }
bool GCToOSInterface::CanGetCurrentProcessorNumber() { return true; }
bool GCToOSInterface::SetCurrentThreadIdealAffinity(uint16_t source, uint16_t destination) { return LibnxSetCurrentThreadAffinity(destination); }
bool GCToOSInterface::GetCurrentThreadIdealProc(uint16_t* processor) { *processor = LibnxCurrentCpu(); return true; }
uint64_t GCToOSInterface::GetCurrentThreadIdForLogging() { return PalGetCurrentOSThreadId(); }
uint32_t GCToOSInterface::GetCurrentProcessId() { return LibnxProcessId(); }
size_t GCToOSInterface::GetCacheSizePerLogicalCpu(bool trueSize) { return 256 * 1024; }
bool GCToOSInterface::SetThreadAffinity(uint16_t processor) { return LibnxSetCurrentThreadAffinity(processor); }
bool GCToOSInterface::BoostThreadPriority() { return false; }

const AffinitySet* GCToOSInterface::SetGCThreadsAffinitySet(uintptr_t mask, const AffinitySet* requested)
{
    s_gcAffinity = AffinitySet();
    uint64_t allowed = LibnxAllowedCores();
    for (uint16_t core = 0; core < 64; core++)
    {
        if ((allowed & (UINT64_C(1) << core)) && (!mask || (mask & (UINT64_C(1) << core))) &&
            (!requested || requested->Contains(core)))
            s_gcAffinity.Add(core);
    }
    return &s_gcAffinity;
}
size_t GCToOSInterface::GetVirtualMemoryLimit() { return LibnxVirtualLimit(false); }
size_t GCToOSInterface::GetVirtualMemoryMaxAddress() { return LibnxVirtualLimit(true); }
uint64_t GCToOSInterface::GetPhysicalMemoryLimit(bool* restricted)
{
    uint64_t total = 0, available = 0;
    LibnxHeapInfo(&total, &available);
    if (restricted)
        *restricted = true;
    return total;
}
void GCToOSInterface::GetMemoryStatus(uint64_t restricted, uint32_t* load, uint64_t* physical, uint64_t* pageFile)
{
    uint64_t total = 0, available = 0;
    LibnxHeapInfo(&total, &available);
    uint64_t used = total - available;
    uint64_t limit = restricted && restricted < total ? restricted : total;
    uint64_t remaining = used < limit ? limit - used : 0;
    if (load)
        *load = limit ? (uint32_t)std::min<uint64_t>(100, used * 100 / limit) : 100;
    if (physical)
        *physical = remaining;
    if (pageFile)
        *pageFile = remaining;
}
void GCToOSInterface::FlushProcessWriteBuffers()
{
    if (!LibnxFlushThreadWrites())
        abort();
}
void GCToOSInterface::DebugBreak() { __builtin_trap(); }
int64_t GCToOSInterface::QueryPerformanceCounter() { return LibnxSystemTick(); }
int64_t GCToOSInterface::QueryPerformanceFrequency() { return LibnxTickFrequency(); }
uint64_t GCToOSInterface::GetLowPrecisionTimeStamp() { return LibnxSystemTick() / (LibnxTickFrequency() / 1000); }
uint32_t GCToOSInterface::GetTotalProcessorCount() { return LibnxCpuCount(); }
bool GCToOSInterface::CanEnableGCNumaAware() { return false; }
bool GCToOSInterface::GetNumaInfo(uint16_t* nodes, uint32_t* maxProcessors) { return false; }
bool GCToOSInterface::CanEnableGCCPUGroups() { return false; }
bool GCToOSInterface::GetCPUGroupInfo(uint16_t* groups, uint32_t* maxProcessors) { return false; }
bool GCToOSInterface::GetProcessorForHeap(uint16_t heap, uint16_t* processor, uint16_t* node)
{
    unsigned selected = 0;
    uint64_t allowed = LibnxAllowedCores();
    for (uint16_t core = 0; core < 64; core++)
    {
        if ((allowed & (UINT64_C(1) << core)) && selected++ == heap)
        {
            *processor = core;
            if (node)
                *node = NUMA_NODE_UNDEFINED;
            return true;
        }
    }
    return false;
}
bool GCToOSInterface::ParseGCHeapAffinitizeRangesEntry(const char** text, size_t* first, size_t* last)
{
    // Explicit affinity range syntax is not exposed by this initial port.
    return false;
}
