// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// Initial libnx System.Native surface required by the NativeAOT smoke test.
#include <switch.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <minipal/time.h>
#include "pal_threading.h"
#include "pal_time.h"
#include "pal_io.h"
#include "pal_log.h"
#include "pal_random.h"

void SystemNative_Abort(void) { abort(); }
void* SystemNative_Malloc(uintptr_t size) { return malloc(size); }
void SystemNative_Free(void* memory) { free(memory); }
char* SystemNative_GetEnv(const char* name) { return getenv(name); }
int32_t SystemNative_GetErrNo(void) { return errno; }
void SystemNative_SetErrNo(int32_t error) { errno = error; }
int32_t SystemNative_SchedGetCpu(void) { return svcGetCurrentProcessorNumber(); }
int64_t SystemNative_GetTimestamp(void) { return minipal_hires_ticks(); }
int64_t SystemNative_GetLowResolutionTimestamp(void) { return minipal_lowres_ticks(); }
void SystemNative_LogError(uint8_t* buffer, int32_t length)
{
    if (buffer && length > 0)
        svcOutputDebugString((char*)buffer, length);
}
void SystemNative_GetNonCryptographicallySecureRandomBytes(uint8_t* buffer, int32_t length)
{
    if (buffer && length > 0)
        randomGet(buffer, length);
}

struct LowLevelMonitor
{
    pthread_mutex_t mutex;
    pthread_cond_t condition;
};
LowLevelMonitor* SystemNative_LowLevelMonitor_Create(void)
{
    LowLevelMonitor* monitor = malloc(sizeof(*monitor));
    if (!monitor)
        return NULL;
    if (pthread_mutex_init(&monitor->mutex, NULL) != 0)
    {
        free(monitor);
        return NULL;
    }
    if (pthread_cond_init(&monitor->condition, NULL) != 0)
    {
        pthread_mutex_destroy(&monitor->mutex);
        free(monitor);
        return NULL;
    }
    return monitor;
}
void SystemNative_LowLevelMonitor_Destroy(LowLevelMonitor* monitor)
{
    if (pthread_cond_destroy(&monitor->condition) != 0 || pthread_mutex_destroy(&monitor->mutex) != 0)
        abort();
    free(monitor);
}
void SystemNative_LowLevelMonitor_Acquire(LowLevelMonitor* monitor)
{
    if (pthread_mutex_lock(&monitor->mutex) != 0)
        abort();
}
void SystemNative_LowLevelMonitor_Release(LowLevelMonitor* monitor)
{
    if (pthread_mutex_unlock(&monitor->mutex) != 0)
        abort();
}
void SystemNative_LowLevelMonitor_Wait(LowLevelMonitor* monitor)
{
    if (pthread_cond_wait(&monitor->condition, &monitor->mutex) != 0)
        abort();
}
int32_t SystemNative_LowLevelMonitor_TimedWait(LowLevelMonitor* monitor, int32_t milliseconds)
{
    if (milliseconds < 0)
        abort();
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        abort();
    uint64_t nanoseconds = (uint64_t)deadline.tv_nsec + (uint64_t)milliseconds * 1000000;
    deadline.tv_sec += nanoseconds / 1000000000;
    deadline.tv_nsec = nanoseconds % 1000000000;
    int result = pthread_cond_timedwait(&monitor->condition, &monitor->mutex, &deadline);
    if (result != 0 && result != ETIMEDOUT)
        abort();
    return result == 0;
}
void SystemNative_LowLevelMonitor_Signal_Release(LowLevelMonitor* monitor)
{
    if (pthread_cond_signal(&monitor->condition) != 0 || pthread_mutex_unlock(&monitor->mutex) != 0)
        abort();
}

typedef struct AnonymousMapping
{
    uintptr_t address;
    size_t size;
    struct AnonymousMapping* next;
} AnonymousMapping;
static Mutex s_mappingLock;
static AnonymousMapping* s_mappings;

void* SystemNative_MMap(void* hint, uint64_t length, int32_t protection, int32_t flags, intptr_t fd, int64_t offset)
{
    (void)hint;
    if (length == 0 || length > SIZE_MAX - 0xFFF || flags != (PAL_MAP_PRIVATE | PAL_MAP_ANONYMOUS) ||
        fd != -1 || offset != 0 || (protection != PAL_PROT_NONE && protection != PAL_PROT_READ &&
        protection != (PAL_PROT_READ | PAL_PROT_WRITE)))
    {
        errno = ENOTSUP;
        return (void*)-1;
    }
    size_t size = (length + 0xFFF) & ~(size_t)0xFFF;
    AnonymousMapping* entry = malloc(sizeof(*entry));
    void* memory = aligned_alloc(0x1000, size);
    if (!entry || !memory)
    {
        free(entry);
        free(memory);
        errno = ENOMEM;
        return (void*)-1;
    }
    memset(memory, 0, size);
    if (protection != (PAL_PROT_READ | PAL_PROT_WRITE) &&
        R_FAILED(svcSetMemoryPermission(memory, size, protection)))
    {
        free(memory);
        free(entry);
        errno = ENOTSUP;
        return (void*)-1;
    }
    entry->address = (uintptr_t)memory;
    entry->size = size;
    mutexLock(&s_mappingLock);
    entry->next = s_mappings;
    s_mappings = entry;
    mutexUnlock(&s_mappingLock);
    return memory;
}

int32_t SystemNative_MUnmap(void* address, uint64_t length)
{
    if (length == 0 || length > SIZE_MAX - 0xFFF)
    {
        errno = EINVAL;
        return -1;
    }
    size_t size = (length + 0xFFF) & ~(size_t)0xFFF;
    mutexLock(&s_mappingLock);
    AnonymousMapping** current = &s_mappings;
    while (*current && ((*current)->address != (uintptr_t)address || (*current)->size != size))
        current = &(*current)->next;
    if (!*current || R_FAILED(svcSetMemoryPermission(address, size, Perm_Rw)))
    {
        mutexUnlock(&s_mappingLock);
        errno = EINVAL;
        return -1;
    }
    AnonymousMapping* entry = *current;
    *current = entry->next;
    mutexUnlock(&s_mappingLock);
    free(address);
    free(entry);
    return 0;
}

int32_t SystemNative_MProtect(void* address, uint64_t length, int32_t protection)
{
    uintptr_t start = (uintptr_t)address;
    if (length == 0 || (start & 0xFFF) != 0 || length > SIZE_MAX - 0xFFF ||
        (protection != PAL_PROT_NONE && protection != PAL_PROT_READ && protection != (PAL_PROT_READ | PAL_PROT_WRITE)))
    {
        errno = EINVAL;
        return -1;
    }
    size_t size = (length + 0xFFF) & ~(size_t)0xFFF;
    mutexLock(&s_mappingLock);
    bool found = false;
    for (AnonymousMapping* entry = s_mappings; entry; entry = entry->next)
    {
        if (start >= entry->address && size <= entry->size && start - entry->address <= entry->size - size)
        {
            found = true;
            break;
        }
    }
    Result result = found ? svcSetMemoryPermission(address, size, protection) : 1;
    mutexUnlock(&s_mappingLock);
    if (result != 0)
    {
        errno = EINVAL;
        return -1;
    }
    return 0;
}
