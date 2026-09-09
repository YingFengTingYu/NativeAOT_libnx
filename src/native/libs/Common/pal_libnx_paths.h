// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#pragma once
#include <switch.h>
#include <errno.h>
#include <string.h>

// SD remains the managed Unix root; /romfs is reserved for the host-mounted
// read-only RomFS. Managed paths must be Unix-rooted before BCL normalization.
// Relative paths follow libnx's working directory.
static inline const char* NativePath(const char* path, char buffer[FS_MAX_PATH + 8])
{
    if (!path)
    {
        errno = EINVAL;
        return NULL;
    }
    if (path[0] != '/')
        return path;
    size_t length = strlen(path);
    if (length >= FS_MAX_PATH)
    {
        errno = ENAMETOOLONG;
        return NULL;
    }
    if (strncmp(path, "/romfs", 6) == 0 && (path[6] == '/' || path[6] == '\0'))
    {
        memcpy(buffer, "romfs:", 6);
        if (path[6] == '\0')
            memcpy(buffer + 6, "/", 2);
        else
            memcpy(buffer + 6, path + 6, length - 5);
        return buffer;
    }
    memcpy(buffer, "sdmc:", 5);
    memcpy(buffer + 5, path, length + 1);
    return buffer;
}
