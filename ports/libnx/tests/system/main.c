// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include "pal_io.h"
#include "pal_threading.h"
#include "pal_time.h"
#include "pal_random.h"

u32 __nx_applet_type = AppletType_None;
static LowLevelMonitor* s_monitor;
static bool s_ready;

static void Record(const char* name, unsigned long long value)
{
    char buffer[128];
    int count = snprintf(buffer, sizeof(buffer), "[AOTSYS] %s=%llu\n", name, value);
    if (count > 0 && count < (int)sizeof(buffer))
        svcOutputDebugString(buffer, count);
}
static void Worker(void* ignored)
{
    (void)ignored;
    SystemNative_LowLevelMonitor_Acquire(s_monitor);
    s_ready = true;
    SystemNative_LowLevelMonitor_Signal_Release(s_monitor);
}

static bool FileChecks(void)
{
    DIR* root = opendir("sdmc:/");
    if (root)
        closedir(root);
    else if (fsdevMountSdmc() != 0)
        return false;
    const char* path = "/nativeaot-native-position.tmp";
    intptr_t fd = SystemNative_Open(path, PAL_O_CREAT | PAL_O_TRUNC | PAL_O_RDWR, 0600);
    if (fd < 0)
        return false;
    uint8_t contents[16];
    for (unsigned index = 0; index < sizeof(contents); index++)
        contents[index] = index + 1;
    bool passed = SystemNative_Write(fd, contents, sizeof(contents)) == sizeof(contents) &&
                  SystemNative_LSeek(fd, 5, PAL_SEEK_SET) == 5;
    uint8_t readback[4];
    passed = passed && SystemNative_PRead(fd, readback, sizeof(readback), 8) == sizeof(readback) &&
             memcmp(readback, contents + 8, sizeof(readback)) == 0 &&
             SystemNative_LSeek(fd, 0, PAL_SEEK_CUR) == 5;
    uint8_t replacement = 0x88;
    passed = passed && SystemNative_PWrite(fd, &replacement, 1, 12) == 1 &&
             SystemNative_LSeek(fd, 0, PAL_SEEK_CUR) == 5 &&
             SystemNative_Read(fd, readback, 1) == 1 && readback[0] == 6;
    Record("file.position_preserved", passed);
    FileStatus status;
    passed = passed && SystemNative_FStat(fd, &status) == 0 && status.Size == 16 &&
             (status.Mode & PAL_S_IFMT) == PAL_S_IFREG &&
             SystemNative_FTruncate(fd, 8) == 0 && SystemNative_FStat(fd, &status) == 0 && status.Size == 8;
    Record("file.truncate", passed);
    passed = passed && SystemNative_PRead(fd, readback, 1, -1) == -1 && errno == EINVAL &&
             SystemNative_ConvertErrorPlatformToPal(ENOENT) == Error_ENOENT &&
             SystemNative_ConvertErrorPalToPlatform(Error_EEXIST) == EEXIST;
    bool closed = SystemNative_Close(fd) == 0;
    bool deleted = SystemNative_Unlink(path) == 0;
    Record("file.cleanup", closed && deleted);
    return passed && closed && deleted;
}
int main(void)
{
    Record("begin", 1);
    uint8_t* mapped = SystemNative_MMap(NULL, 0x2000, PAL_PROT_READ | PAL_PROT_WRITE,
                                      PAL_MAP_PRIVATE | PAL_MAP_ANONYMOUS, -1, 0);
    if (mapped == (void*)-1)
        return 1;
    for (unsigned index = 0; index < 0x2000; index++)
    {
        if (mapped[index] != 0)
            return 2;
        mapped[index] = 0xA5;
    }
    bool protection = SystemNative_MProtect(mapped, 0x2000, PAL_PROT_READ) == 0;
    if (protection)
    {
        MemoryInfo info;
        uint32_t page;
        protection = R_SUCCEEDED(svcQueryMemory(&info, &page, (uintptr_t)mapped)) && info.perm == Perm_R;
    }
    protection = protection && SystemNative_MProtect(mapped, 0x2000, PAL_PROT_READ | PAL_PROT_WRITE) == 0;
    Record("mapping.protection", protection);
    if (!protection)
        return 3;
    mapped[0] = 0x5A;
    bool unmapped = SystemNative_MUnmap(mapped, 0x2000) == 0;
    Record("mapping.release", unmapped);

    s_monitor = SystemNative_LowLevelMonitor_Create();
    if (!s_monitor)
        return 4;
    SystemNative_LowLevelMonitor_Acquire(s_monitor);
    bool timeout = SystemNative_LowLevelMonitor_TimedWait(s_monitor, 0) == 0;
    Record("monitor.timeout", timeout);
    Thread worker;
    if (R_FAILED(threadCreate(&worker, Worker, NULL, NULL, 0x10000, 0x2C, -2)) ||
        R_FAILED(threadStart(&worker)))
        return 5;
    while (!s_ready)
    {
        if (!SystemNative_LowLevelMonitor_TimedWait(s_monitor, 1000))
            return 6;
    }
    SystemNative_LowLevelMonitor_Release(s_monitor);
    threadWaitForExit(&worker);
    threadClose(&worker);
    SystemNative_LowLevelMonitor_Destroy(s_monitor);
    Record("monitor.signal", 1);

    int64_t before = SystemNative_GetTimestamp();
    svcSleepThread(10000000);
    bool time = SystemNative_GetTimestamp() > before && SystemNative_GetLowResolutionTimestamp() > 0;
    Record("time.monotonic", time);
    uint8_t first[32], second[32];
    SystemNative_GetNonCryptographicallySecureRandomBytes(first, sizeof(first));
    SystemNative_GetNonCryptographicallySecureRandomBytes(second, sizeof(second));
    bool random = memcmp(first, second, sizeof(first)) != 0;
    Record("random.sanity", random);
    bool files = FileChecks();
    Record("pass", unmapped && timeout && time && random && files);
    return 0;
}
