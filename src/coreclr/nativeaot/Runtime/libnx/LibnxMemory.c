// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "LibnxPlatform.h"

#define PAGE_SIZE 0x1000u

typedef struct MemoryReservation
{
    uintptr_t address;
    size_t size;
    void** backing;
    VirtmemReservation* reservation;
    struct MemoryReservation* next;
} MemoryReservation;

static MemoryReservation* s_reservations;
static _Thread_local uint32_t s_lastError;

#ifdef LIBNX_MEMORY_TESTING
static int s_testAllocationsRemaining = -1;
void LibnxMemoryTestAllocationLimit(int remaining)
{
    s_testAllocationsRemaining = remaining;
}
#endif

static void* AllocateBackingPage(void)
{
#ifdef LIBNX_MEMORY_TESTING
    if (s_testAllocationsRemaining == 0)
    {
        errno = ENOMEM;
        return NULL;
    }
    if (s_testAllocationsRemaining > 0)
        s_testAllocationsRemaining--;
#endif
    return aligned_alloc(PAGE_SIZE, PAGE_SIZE);
}

uint32_t LibnxMemoryLastError(void)
{
    return s_lastError;
}

static uintptr_t AlignUp(uintptr_t value, size_t alignment)
{
    if (value > UINTPTR_MAX - (alignment - 1))
        return 0;
    return (value + alignment - 1) & ~(alignment - 1);
}

static MemoryReservation* FindReservation(void* address, size_t size)
{
    uintptr_t start = (uintptr_t)address;
    if (size == 0 || (start & (PAGE_SIZE - 1)) != 0 || (size & (PAGE_SIZE - 1)) != 0 ||
        start > UINTPTR_MAX - size)
        return NULL;
    for (MemoryReservation* entry = s_reservations; entry; entry = entry->next)
    {
        if (start >= entry->address && size <= entry->size &&
            start - entry->address <= entry->size - size)
            return entry;
    }
    return NULL;
}

void* LibnxMemoryReserve(size_t size, size_t alignment)
{
    s_lastError = 0;
    if (alignment == 0)
        alignment = PAGE_SIZE;
    if (size == 0 || (size & (PAGE_SIZE - 1)) != 0 || alignment < PAGE_SIZE ||
        (alignment & (alignment - 1)) != 0)
    {
        errno = EINVAL;
        return NULL;
    }
    if (size > SIZE_MAX - alignment)
        return NULL;

    MemoryReservation* entry = calloc(1, sizeof(*entry));
    if (!entry)
        return NULL;
    entry->backing = calloc(size / PAGE_SIZE, sizeof(void*));
    if (!entry->backing)
    {
        free(entry);
        return NULL;
    }

    // Match libnx thread stacks: reserve virtual stack-region addresses and
    // map independently allocated heap pages only when they are committed.
    virtmemLock();
    void* region = virtmemFindStack(size + alignment, PAGE_SIZE);
    uintptr_t candidate = region ? AlignUp((uintptr_t)region, alignment) : 0;
    if (candidate)
    {
        entry->reservation = virtmemAddReservation((void*)candidate, size);
        if (entry->reservation)
        {
            entry->address = candidate;
            entry->size = size;
            entry->next = s_reservations;
            s_reservations = entry;
            virtmemUnlock();
            return (void*)candidate;
        }
    }
    virtmemUnlock();
    free(entry->backing);
    free(entry);
    return NULL;
}

static bool ChangeCommitState(MemoryReservation* entry, void* address, size_t size, bool commit)
{
    size_t first = ((uintptr_t)address - entry->address) / PAGE_SIZE;
    size_t limit = first + size / PAGE_SIZE;
    unsigned char* added = commit ? calloc(limit - first, 1) : NULL;
    if (commit && !added)
        return false;
    for (size_t page = first; page < limit; page++)
    {
        if ((entry->backing[page] != NULL) == commit)
            continue;
        void* destination = (void*)(entry->address + page * PAGE_SIZE);
        if (commit)
        {
            void* backing = AllocateBackingPage();
            if (backing)
            {
                memset(backing, 0, PAGE_SIZE);
                s_lastError = svcMapMemory(destination, backing, PAGE_SIZE);
                if (s_lastError == 0)
                {
                    entry->backing[page] = backing;
                    added[page - first] = 1;
                    continue;
                }
                free(backing);
            }
            // Roll back only pages added by this call; existing live data stays intact.
            for (size_t previous = first; previous < page; previous++)
            {
                if (!added[previous - first])
                    continue;
                Result result = svcUnmapMemory((void*)(entry->address + previous * PAGE_SIZE),
                                               entry->backing[previous], PAGE_SIZE);
                if (R_FAILED(result))
                    diagAbortWithResult(result);
                free(entry->backing[previous]);
                entry->backing[previous] = NULL;
            }
            free(added);
            return false;
        }
        s_lastError = svcUnmapMemory(destination, entry->backing[page], PAGE_SIZE);
        if (s_lastError != 0)
            return false;
        free(entry->backing[page]);
        entry->backing[page] = NULL;
    }
    free(added);
    return true;
}

static bool ChangeRange(void* address, size_t size, bool commit)
{
    s_lastError = 0;
    virtmemLock();
    MemoryReservation* entry = FindReservation(address, size);
    bool result = entry && ChangeCommitState(entry, address, size, commit);
    virtmemUnlock();
    if (!entry)
        errno = EINVAL;
    return result;
}

bool LibnxMemoryCommit(void* address, size_t size)
{
    return ChangeRange(address, size, true);
}

bool LibnxMemoryDecommit(void* address, size_t size)
{
    return ChangeRange(address, size, false);
}

bool LibnxMemoryRelease(void* address, size_t size)
{
    s_lastError = 0;
    virtmemLock();
    MemoryReservation* entry = FindReservation(address, size);
    if (!entry || entry->address != (uintptr_t)address || entry->size != size ||
        !ChangeCommitState(entry, address, size, false))
    {
        virtmemUnlock();
        return false;
    }
    MemoryReservation** previous = &s_reservations;
    while (*previous != entry)
        previous = &(*previous)->next;
    *previous = entry->next;
    virtmemRemoveReservation(entry->reservation);
    virtmemUnlock();
    free(entry->backing);
    free(entry);
    return true;
}

bool LibnxMemoryProtect(void* address, size_t size, uint32_t permission)
{
    if (permission != Perm_None && permission != Perm_R && permission != Perm_Rw)
    {
        errno = ENOTSUP;
        return false;
    }
    s_lastError = 0;
    virtmemLock();
    MemoryReservation* entry = FindReservation(address, size);
    bool committed = entry != NULL;
    if (entry)
    {
        size_t first = ((uintptr_t)address - entry->address) / PAGE_SIZE;
        for (size_t page = first; page < first + size / PAGE_SIZE; page++)
            committed = committed && entry->backing[page] != NULL;
    }
    if (committed)
        s_lastError = svcSetMemoryPermission(address, size, permission);
    virtmemUnlock();
    return committed && s_lastError == 0;
}
