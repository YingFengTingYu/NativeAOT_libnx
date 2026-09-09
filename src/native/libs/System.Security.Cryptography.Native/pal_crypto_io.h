// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#pragma once

#if defined(TARGET_LIBNX)
#include "../Common/pal_libnx_paths.h"
#include <dirent.h>
#include <stdio.h>
#include <unistd.h>

// Certificate stores receive the same managed Unix paths as System.Native.
// Do not let native stdio interpret /romfs or the SD root differently.
static inline BIO* PalBioNewFile(const char* path, const char* mode)
{
    char buffer[FS_MAX_PATH + 8];
    const char* native = NativePath(path, buffer);
    return native ? BIO_new_file(native, mode) : NULL;
}
static inline FILE* PalFOpen(const char* path, const char* mode)
{
    char buffer[FS_MAX_PATH + 8];
    const char* native = NativePath(path, buffer);
    return native ? fopen(native, mode) : NULL;
}
static inline DIR* PalOpenDir(const char* path)
{
    char buffer[FS_MAX_PATH + 8];
    const char* native = NativePath(path, buffer);
    return native ? opendir(native) : NULL;
}
static inline int PalUnlink(const char* path)
{
    char buffer[FS_MAX_PATH + 8];
    const char* native = NativePath(path, buffer);
    return native ? unlink(native) : -1;
}
#else
#define PalBioNewFile BIO_new_file
#define PalFOpen fopen
#define PalOpenDir opendir
#define PalUnlink unlink
#endif
