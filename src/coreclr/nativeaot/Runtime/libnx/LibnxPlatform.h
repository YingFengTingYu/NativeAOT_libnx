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

#ifdef __cplusplus
}
#endif
