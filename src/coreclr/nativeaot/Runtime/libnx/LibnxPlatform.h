// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void* LibnxMemoryReserve(size_t size, size_t alignment);
bool LibnxMemoryCommit(void* address, size_t size);
bool LibnxMemoryDecommit(void* address, size_t size);
bool LibnxMemoryRelease(void* address, size_t size);
bool LibnxMemoryProtect(void* address, size_t size, uint32_t permission);
uint32_t LibnxMemoryLastError(void);
#ifdef LIBNX_MEMORY_TESTING
void LibnxMemoryTestAllocationLimit(int remaining);
#endif

void* LibnxEventCreate(bool manualReset, bool initiallySignaled);
void LibnxEventSet(void* event);
void LibnxEventReset(void* event);
uint32_t LibnxEventWait(void* event, uint32_t milliseconds);
void LibnxEventClose(void* event);
#ifdef LIBNX_EVENT_TESTING
uint32_t LibnxEventTestWaiterCount(void* event);
#endif

bool LibnxThreadsInitialize(void);
bool LibnxThreadAttach(void* owner, void (*onExit)(void*));
bool LibnxThreadPause(void* owner, void* context);
bool LibnxThreadResume(void* owner);
bool LibnxFlushThreadWrites(void);
bool LibnxGetStackBounds(void** low, void** high);
bool LibnxStartThread(uint32_t (*callback)(void*), void* argument, size_t stackSize);
uint32_t LibnxThreadLastError(void);
bool LibnxGetRuntimeCpuTicks(uint64_t* ticks);
#ifdef LIBNX_THREAD_TESTING
void LibnxThreadTestCounts(uint32_t* started, uint32_t* reaped);
#endif
uint32_t LibnxCpuCount(void);
uint64_t LibnxAllowedCores(void);
bool LibnxSetCurrentThreadAffinity(uint32_t core);
uint32_t LibnxCurrentCpu(void);
uint32_t LibnxProcessId(void);
uint64_t LibnxSystemTick(void);
uint64_t LibnxTickFrequency(void);
void LibnxSleep(uint32_t milliseconds);
void LibnxYield(void);
bool LibnxHeapInfo(uint64_t* total, uint64_t* available);
uintptr_t LibnxVirtualLimit(bool endAddress);

#ifdef __cplusplus
}
#endif
