// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include "common.h"
#include "gcenv.h"
#include "gcenv.ee.h"
#include "gcconfig.h"
#include "RhConfig.h"
#include "rhbinder.h"
#include "thread.h"
#include "NativeContext.h"
#include "LibnxPlatform.h"
#include <strings.h>
#include <errno.h>
#include <time.h>
#include <minipal/thread.h>

extern "C" char __start__[];
extern "C" char __end__[];
extern "C" uint32_t svcOutputDebugString(const char*, uint64_t);

uint32_t g_RhNumberOfProcessors;
PLATFORM_THREAD_LOCAL intptr_t tls_thunkData;

void PalPrintFatalError(const char* text)
{
    if (text)
        svcOutputDebugString(text, strlen(text));
}
void RhFailFast()
{
    PalPrintFatalError("[NativeAOT/libnx] fail-fast\n");
    abort();
}
bool PalInit()
{
    GCConfig::Initialize();
    if (!GCToOSInterface::Initialize())
        return false;
    g_RhNumberOfProcessors = LibnxCpuCount();
    return g_RhNumberOfProcessors != 0;
}
void PalAttachThread(void* thread)
{
    if (!LibnxThreadAttach(thread, RuntimeThreadShutdown))
        RhFailFast();
}
uint32_t PalGetOsPageSize() { return 0x1000; }
int32_t PalGetProcessCpuCount() { return g_RhNumberOfProcessors; }
uint64_t PalGetCurrentOSThreadId() { return minipal_get_current_thread_id(); }
uint32_t PalGetCurrentProcessId() { return LibnxProcessId(); }
bool PalGetMaximumStackBounds(void** low, void** high) { return LibnxGetStackBounds(low, high); }
void PalSleep(uint32_t milliseconds) { LibnxSleep(milliseconds); }
UInt32_BOOL PalSwitchToThread() { LibnxYield(); return UInt32_FALSE; }
UInt32_BOOL PalAreShadowStacksEnabled() { return UInt32_FALSE; }
bool PalSetCurrentThreadName(const char* name) { errno = ENOTSUP; return false; }
bool PalStartBackgroundGCThread(BackgroundCallback callback, void* context) { return LibnxStartThread(callback, context, 0); }
bool PalStartFinalizerThread(BackgroundCallback callback, void* context) { return LibnxStartThread(callback, context, 0); }
bool PalStartEventPipeHelperThread(BackgroundCallback callback, void* context) { errno = ENOTSUP; return false; }

HANDLE PalCreateEventW(LPSECURITY_ATTRIBUTES attributes, UInt32_BOOL manualReset, UInt32_BOOL initialState, LPCWSTR name)
{
    void* event = LibnxEventCreate(manualReset != 0, initialState != 0);
    return event ? event : INVALID_HANDLE_VALUE;
}
UInt32_BOOL PalCloseHandle(HANDLE handle)
{
    if (!handle || handle == INVALID_HANDLE_VALUE)
        return UInt32_FALSE;
    LibnxEventClose(handle);
    return UInt32_TRUE;
}
UInt32_BOOL PalSetEvent(HANDLE event) { LibnxEventSet(event); return UInt32_TRUE; }
UInt32_BOOL PalResetEvent(HANDLE event) { LibnxEventReset(event); return UInt32_TRUE; }
uint32_t PalWaitForSingleObjectEx(HANDLE event, uint32_t milliseconds, UInt32_BOOL alertable)
{
    return LibnxEventWait(event, milliseconds);
}
uint32_t PalCompatibleWaitAny(UInt32_BOOL alertable, uint32_t timeout, uint32_t count, HANDLE* handles, UInt32_BOOL reentrant)
{
    return count == 1 ? LibnxEventWait(handles[0], timeout) : WAIT_FAILED;
}
HANDLE PalCreateLowMemoryResourceNotification() { return nullptr; }

void* PalVirtualAlloc(uintptr_t size, uint32_t protect)
{
    if (size == 0 || size > SIZE_MAX - 0xFFF || (protect != PAGE_READWRITE && protect != PAGE_NOACCESS))
        return nullptr;
    size = (size + 0xFFF) & ~(uintptr_t)0xFFF;
    void* memory = LibnxMemoryReserve(size, 0x1000);
    if (memory && protect == PAGE_READWRITE && !LibnxMemoryCommit(memory, size))
    {
        LibnxMemoryRelease(memory, size);
        return nullptr;
    }
    return memory;
}
void PalVirtualFree(void* address, uintptr_t size)
{
    if (size > SIZE_MAX - 0xFFF || !LibnxMemoryRelease(address, (size + 0xFFF) & ~(uintptr_t)0xFFF))
        RhFailFast();
}
UInt32_BOOL PalVirtualProtect(void* address, uintptr_t size, uint32_t protect)
{
    uint32_t permission;
    switch (protect)
    {
        case PAGE_NOACCESS: permission = 0; break;
        case PAGE_READONLY: permission = 1; break;
        case PAGE_READWRITE: permission = 3; break;
        default: errno = ENOTSUP; return UInt32_FALSE;
    }
    return LibnxMemoryProtect(address, size, permission);
}
void PalFlushInstructionCache(void* address, size_t size)
{
    __builtin___clear_cache((char*)address, (char*)address + size);
}
void PalFlushProcessWriteBuffers() { GCToOSInterface::FlushProcessWriteBuffers(); }

HANDLE PalGetModuleHandleFromPointer(void* pointer)
{
    uintptr_t value = (uintptr_t)pointer;
    return value >= (uintptr_t)__start__ && value < (uintptr_t)__end__ ? __start__ : nullptr;
}
void PalGetModuleBounds(HANDLE module, uint8_t** low, uint8_t** high)
{
    *low = (uint8_t*)__start__;
    *high = (uint8_t*)__end__ - 1;
}
int32_t PalGetModuleFileName(const TCHAR** name, HANDLE module)
{
    *name = "nativeaot-libnx.nro";
    return (int32_t)strlen(*name);
}
void PalGetPDBInfo(HANDLE module, GUID* guid, uint32_t* age, WCHAR* path, int32_t length, uint32_t* buildIdSize, void** buildId)
{
    memset(guid, 0, sizeof(*guid));
    *age = 0;
    *buildIdSize = 0;
    *buildId = nullptr;
    if (length > 0)
        path[0] = 0;
}
char* PalCopyTCharAsChar(const TCHAR* text) { return strdup(text); }
HANDLE PalLoadLibrary(const char* name) { errno = ENOTSUP; return nullptr; }
void* PalGetProcAddress(HANDLE module, const char* name) { errno = ENOTSUP; return nullptr; }
int32_t _stricmp(const char* left, const char* right) { return strcasecmp(left, right); }
uint16_t PalCaptureStackBackTrace(uint32_t skip, uint32_t count, void* frames, uint32_t* hash) { return 0; }
uint32_t PalGetEnvironmentVariable(const char* name, char* buffer, uint32_t capacity)
{
    const char* value = getenv(name);
    if (!value)
        return 0;
    size_t length = strlen(value);
    if (length >= capacity)
        return (uint32_t)length + 1;
    memcpy(buffer, value, length + 1);
    return (uint32_t)length;
}
void PalGetSystemTimeAsFileTime(FILETIME* result)
{
    uint64_t ticks = ((uint64_t)time(nullptr) + UINT64_C(11644473600)) * UINT64_C(10000000);
    result->dwLowDateTime = (uint32_t)ticks;
    result->dwHighDateTime = (uint32_t)(ticks >> 32);
}
UInt32_BOOL PalMarkThunksAsValidCallTargets(void* address, int thunkSize, int perBlock, int blockSize, int blocks)
{
    // There is no CFG registration API on Horizon.
    return UInt32_TRUE;
}
FCIMPL0(intptr_t, RhGetCurrentThunkContext)
{
    return tls_thunkData;
}
FCIMPLEND

#ifdef FEATURE_HIJACK
HijackFunc* PalGetHijackTarget(HijackFunc* target) { return target; }
void PalHijack(Thread* thread)
{
    NATIVE_CONTEXT context = {};
    if (!LibnxThreadPause(thread, &context.ctx))
        RhFailFast();
    NativeContextStorage original = context.ctx;
    Thread::HijackCallback(&context, thread, false);
    bool registersUnchanged = memcmp(&original, &context.ctx, sizeof(original)) == 0;
    if (!LibnxThreadResume(thread) || !registersUnchanged)
        RhFailFast();
}
#endif
