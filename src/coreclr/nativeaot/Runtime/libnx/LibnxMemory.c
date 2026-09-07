// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "LibnxPlatform.h"

#define PAGE_SIZE 0x1000u
#define BLOCK_PAGES 16u
#define BLOCK_SIZE (PAGE_SIZE * BLOCK_PAGES)

typedef struct BackingBlock
{
    void* address;
    uint32_t committed;
} BackingBlock;

typedef struct MemoryReservation
{
    uintptr_t address;
    size_t size;
    BackingBlock** blocks;
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

static void* AllocateBackingBlock(size_t size)
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
    return aligned_alloc(PAGE_SIZE, size);
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
    entry->blocks = calloc(size / BLOCK_SIZE + (size % BLOCK_SIZE != 0), sizeof(BackingBlock*));
    if (!entry->blocks)
    {
        free(entry);
        return NULL;
    }

    // Match libnx thread stacks: reserve virtual stack-region addresses and
    // map heap backing only when committed. Group backing into 64 KiB blocks
    // to avoid exhausting Horizon's memory-block bookkeeping with large arrays.
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
    free(entry->blocks);
    free(entry);
    return NULL;
}

static bool IsCommitted(MemoryReservation* entry, size_t page)
{
    BackingBlock* block = entry->blocks[page / BLOCK_PAGES];
    return block && (block->committed & (1u << (page % BLOCK_PAGES))) != 0;
}

static void FreeEmptyBlock(MemoryReservation* entry, size_t index)
{
    BackingBlock* block = entry->blocks[index];
    if (block && block->committed == 0)
    {
        free(block->address);
        free(block);
        entry->blocks[index] = NULL;
    }
}

static bool UnmapPages(MemoryReservation* entry, size_t first, size_t limit)
{
    for (size_t page = first; page < limit;)
    {
        if (!IsCommitted(entry, page))
        {
            page++;
            continue;
        }
        size_t index = page / BLOCK_PAGES;
        BackingBlock* block = entry->blocks[index];
        size_t end = page + 1;
        while (end < limit && end / BLOCK_PAGES == index && IsCommitted(entry, end))
            end++;
        uint32_t mask = ((1u << (end - page)) - 1) << (page % BLOCK_PAGES);
        s_lastError = svcUnmapMemory((void*)(entry->address + page * PAGE_SIZE),
                                    (char*)block->address + (page % BLOCK_PAGES) * PAGE_SIZE,
                                    (end - page) * PAGE_SIZE);
        if (s_lastError != 0)
            return false;
        block->committed &= ~mask;
        FreeEmptyBlock(entry, index);
        page = end;
    }
    return true;
}

static bool ChangeCommitState(MemoryReservation* entry, void* address, size_t size, bool commit)
{
    size_t first = ((uintptr_t)address - entry->address) / PAGE_SIZE;
    size_t limit = first + size / PAGE_SIZE;
    if (!commit)
        return UnmapPages(entry, first, limit);
    unsigned char* added = calloc(limit - first, 1);
    if (!added)
        return false;
    size_t page = first;
    for (; page < limit;)
    {
        if (IsCommitted(entry, page))
        {
            page++;
            continue;
        }
        size_t index = page / BLOCK_PAGES;
        BackingBlock* block = entry->blocks[index];
        if (!block)
        {
            size_t blockSize = entry->size - index * BLOCK_SIZE;
            if (blockSize > BLOCK_SIZE)
                blockSize = BLOCK_SIZE;
            block = calloc(1, sizeof(*block));
            if (block)
                block->address = AllocateBackingBlock(blockSize);
            if (!block || !block->address)
            {
                free(block);
                s_lastError = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
                goto rollback;
            }
            entry->blocks[index] = block;
        }
        size_t end = page + 1;
        while (end < limit && end / BLOCK_PAGES == index && !IsCommitted(entry, end))
            end++;
        void* source = (char*)block->address + (page % BLOCK_PAGES) * PAGE_SIZE;
        memset(source, 0, (end - page) * PAGE_SIZE);
        s_lastError = svcMapMemory((void*)(entry->address + page * PAGE_SIZE), source, (end - page) * PAGE_SIZE);
        if (s_lastError != 0)
        {
            FreeEmptyBlock(entry, index);
            goto rollback;
        }
        block->committed |= ((1u << (end - page)) - 1) << (page % BLOCK_PAGES);
        memset(added + page - first, 1, end - page);
        page = end;
    }
    free(added);
    return true;

rollback:
    {
        Result failed = s_lastError;
        char message[160];
        int messageSize = snprintf(message, sizeof(message),
            "[AOTVM] commit.failed size=%zu page=%zu result=%u errno=%d\n", size, page - first, failed, errno);
        if (messageSize > 0 && messageSize < (int)sizeof(message))
            svcOutputDebugString(message, messageSize);
        for (size_t previous = first; previous < page;)
        {
            if (!added[previous - first])
            {
                previous++;
                continue;
            }
            size_t end = previous + 1;
            while (end < page && added[end - first])
                end++;
            if (!UnmapPages(entry, previous, end))
                diagAbortWithResult(s_lastError);
            previous = end;
        }
        s_lastError = failed;
    }
    free(added);
    return false;
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
    free(entry->blocks);
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
            committed = committed && IsCommitted(entry, page);
    }
    if (committed)
        s_lastError = svcSetMemoryPermission(address, size, permission);
    virtmemUnlock();
    return committed && s_lastError == 0;
}
