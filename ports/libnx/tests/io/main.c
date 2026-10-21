// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "pal_io.h"
#include "LibnxPlatform.h"

u32 __nx_applet_type = AppletType_None;
enum { ChunkSize = 128 * 1024, FileSize = 4 * 1024 * 1024, MappingSize = ChunkSize + 4096 };
static uint64_t s_calls, s_smallCalls, s_invalidBuffers;
Result __real_fsFileRead(FsFile*, s64, void*, u64, u32, u64*);
Result __real_fsStorageRead(FsStorage*, s64, void*, u64);

static void CountRead(uint64_t size, Result result)
{
    s_calls++;
    s_smallCalls += size <= 4096;
    s_invalidBuffers += R_VALUE(result) == 0xD401;
}
Result __wrap_fsFileRead(FsFile* file, s64 offset, void* buffer, u64 size, u32 option, u64* read)
{
    Result result = __real_fsFileRead(file, offset, buffer, size, option, read);
    CountRead(size, result);
    return result;
}
Result __wrap_fsStorageRead(FsStorage* storage, s64 offset, void* buffer, u64 size)
{
    Result result = __real_fsStorageRead(storage, offset, buffer, size);
    CountRead(size, result);
    return result;
}
static void Record(const char* name, uint64_t value)
{
    char text[192];
    int length = snprintf(text, sizeof(text), "[AOTIO] %s=%llu\n", name, (unsigned long long)value);
    if (length > 0 && length < (int)sizeof(text)) svcOutputDebugString(text, length);
}
static bool Verify(const uint8_t* data, size_t count, size_t offset)
{
    for (size_t i = 0; i < count; i++)
        if (data[i] != (uint8_t)((offset + i) * 17 + ((offset + i) >> 12))) return false;
    return true;
}
static void MemoryKind(const char* label, void* buffer)
{
    MemoryInfo info = {0};
    u32 page;
    Result result = svcQueryMemory(&info, &page, (uintptr_t)buffer);
    Record(label, R_SUCCEEDED(result) ? info.type : UINT32_MAX);
}
static bool Measure(const char* label, const char* path, uint8_t* buffer)
{
    intptr_t fd = SystemNative_Open(path, PAL_O_RDONLY, 0);
    if (fd < 0) return false;
    bool passed = SystemNative_LSeek(fd, 19, PAL_SEEK_SET) == 19;
    uint64_t elapsed = 0;
    s_calls = s_smallCalls = s_invalidBuffers = 0;
    for (int offset = 0; offset < FileSize && passed; offset += ChunkSize)
    {
        uint64_t start = armGetSystemTick();
        int count = SystemNative_PRead(fd, buffer, ChunkSize, offset);
        elapsed += armGetSystemTick() - start;
        passed = count == ChunkSize && Verify(buffer, ChunkSize, offset);
    }
    char key[128];
    snprintf(key, sizeof(key), "%s.calls", label); Record(key, s_calls);
    snprintf(key, sizeof(key), "%s.small_calls", label); Record(key, s_smallCalls);
    snprintf(key, sizeof(key), "%s.invalid_buffers", label); Record(key, s_invalidBuffers);
    snprintf(key, sizeof(key), "%s.microseconds", label); Record(key, armTicksToNs(elapsed) / 1000);
    passed = passed && SystemNative_LSeek(fd, 0, PAL_SEEK_CUR) == 19;
    passed = SystemNative_Close(fd) == 0 && passed;
    snprintf(key, sizeof(key), "%s.pass", label); Record(key, passed);
    return passed;
}
static bool PrepareSd(uint8_t* buffer)
{
    intptr_t source = SystemNative_Open("/romfs/fixture.bin", PAL_O_RDONLY, 0);
    intptr_t output = SystemNative_Open("/nativeaot-io-probe.bin", PAL_O_WRONLY | PAL_O_CREAT | PAL_O_TRUNC, 0600);
    bool passed = source >= 0 && output >= 0;
    for (int offset = 0; offset < FileSize && passed; offset += ChunkSize)
        passed = SystemNative_PRead(source, buffer, ChunkSize, offset) == ChunkSize &&
                 SystemNative_Write(output, buffer, ChunkSize) == ChunkSize;
    if (source >= 0) passed = SystemNative_Close(source) == 0 && passed;
    if (output >= 0) passed = SystemNative_Close(output) == 0 && passed;
    return passed;
}
int main(void)
{
    Record("begin", 1);
    if (fsdevGetDeviceFileSystem("sdmc") == NULL && fsdevMountSdmc() != 0)
    { Record("sd.mount", 0); return 1; }
    if (R_FAILED(romfsInit())) { Record("romfs.mount", 0); return 2; }
    uint8_t* heap = malloc(MappingSize);
    uint8_t* mapped = LibnxMemoryReserve(MappingSize, 4096);
    if (!heap || !mapped || !LibnxMemoryCommit(mapped, MappingSize))
    { Record("allocate", 0); return 3; }
    MemoryKind("heap.memory_type", heap + 24);
    MemoryKind("mapped.memory_type", mapped + 24);
    bool passed = PrepareSd(heap);
    if (passed)
    {
        FsFile file;
        Result opened = fsFsOpenFile(fsdevGetDeviceFileSystem("sdmc"), "/nativeaot-io-probe.bin", FsOpenMode_Read, &file);
        passed = R_SUCCEEDED(opened);
        if (passed)
        {
            u64 bytes = 0;
            Result direct = fsFileRead(&file, 0, mapped + 24, ChunkSize, FsReadOption_None, &bytes);
            Record("mapped.direct_result", direct);
            fsFileClose(&file);
        }
        passed = Measure("sd.heap", "/nativeaot-io-probe.bin", heap + 24) && passed;
        passed = Measure("sd.mapped", "/nativeaot-io-probe.bin", mapped + 24) && passed;
        passed = Measure("romfs.heap", "/romfs/fixture.bin", heap + 24) && passed;
        passed = Measure("romfs.mapped", "/romfs/fixture.bin", mapped + 24) && passed;
    }
    bool cleaned = SystemNative_Unlink("/nativeaot-io-probe.bin") == 0 &&
                   LibnxMemoryRelease(mapped, MappingSize);
    free(heap);
    romfsExit();
    Record("cleanup", cleaned);
    Record("pass", passed && cleaned);
    return passed && cleaned ? 0 : 4;
}
