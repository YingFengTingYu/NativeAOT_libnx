// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "pal_io.h"
#include "pal_process.h"
#include "pal_uid.h"
#include "pal_libnx_networking.h"

c_static_assert(sizeof(off_t) == sizeof(int64_t));
c_static_assert(S_IFREG == PAL_S_IFREG && S_IFDIR == PAL_S_IFDIR && S_IFMT == PAL_S_IFMT);
c_static_assert(SEEK_SET == PAL_SEEK_SET && SEEK_CUR == PAL_SEEK_CUR && SEEK_END == PAL_SEEK_END);

// Newlib has no pread/pwrite implementation in this toolchain. Serialize all
// position-changing System.Native operations around seek/read-or-write/restore.
// Sharing these descriptors with concurrent native stdio is not supported yet.
static Mutex s_positionLock;
void LibnxInitializeDebugStdio(void);

#include "../Common/pal_libnx_paths.h"

static bool ValidDescriptor(intptr_t fd)
{
    if (fd < 0 || fd > INT_MAX)
    {
        errno = EBADF;
        return false;
    }
    return true;
}

int32_t SystemNative_ChDir(const char* path)
{
    char buffer[FS_MAX_PATH + 8];
    const char* native = NativePath(path, buffer);
    return native ? chdir(native) : -1;
}

int32_t SystemNative_Rename(const char* oldPath, const char* newPath)
{
    char oldBuffer[FS_MAX_PATH + 8], newBuffer[FS_MAX_PATH + 8];
    const char* nativeOld = NativePath(oldPath, oldBuffer);
    const char* nativeNew = NativePath(newPath, newBuffer);
    return nativeOld && nativeNew ? rename(nativeOld, nativeNew) : -1;
}

static void ConvertStatus(const struct stat* source, FileStatus* output)
{
    memset(output, 0, sizeof(*output));
    output->Mode = source->st_mode;
    output->Uid = source->st_uid;
    output->Gid = source->st_gid;
    output->Size = source->st_size;
    output->ATime = source->st_atime;
    output->MTime = source->st_mtime;
    output->CTime = source->st_ctime;
    output->Dev = source->st_dev;
    output->RDev = source->st_rdev;
    output->Ino = source->st_ino;
}

int32_t SystemNative_FStat(intptr_t fd, FileStatus* output)
{
    if (!ValidDescriptor(fd))
        return -1;
    struct stat status;
    int result = fstat((int)fd, &status);
    if (result == 0)
        ConvertStatus(&status, output);
    return result;
}

int32_t SystemNative_Stat(const char* path, FileStatus* output)
{
    char buffer[FS_MAX_PATH + 8];
    const char* native = NativePath(path, buffer);
    if (!native)
        return -1;
    struct stat status;
    int result = stat(native, &status);
    if (result == 0)
        ConvertStatus(&status, output);
    return result;
}

int32_t SystemNative_LStat(const char* path, FileStatus* output)
{
    // libnx fsdev filesystems do not provide symbolic links.
    return SystemNative_Stat(path, output);
}

intptr_t SystemNative_Open(const char* path, int32_t flags, int32_t mode)
{
    int nativeFlags;
    switch (flags & PAL_O_ACCESS_MODE_MASK)
    {
        case PAL_O_RDONLY: nativeFlags = O_RDONLY; break;
        case PAL_O_WRONLY: nativeFlags = O_WRONLY; break;
        case PAL_O_RDWR: nativeFlags = O_RDWR; break;
        default: errno = EINVAL; return -1;
    }
    const int knownFlags = PAL_O_ACCESS_MODE_MASK | PAL_O_CLOEXEC | PAL_O_CREAT | PAL_O_EXCL |
                           PAL_O_TRUNC | PAL_O_SYNC | PAL_O_NOFOLLOW;
    if (flags & ~knownFlags)
    {
        errno = EINVAL;
        return -1;
    }
    if (flags & PAL_O_CLOEXEC) nativeFlags |= O_CLOEXEC;
    if (flags & PAL_O_CREAT) nativeFlags |= O_CREAT;
    if (flags & PAL_O_EXCL) nativeFlags |= O_EXCL;
    if (flags & PAL_O_TRUNC) nativeFlags |= O_TRUNC;
    if (flags & PAL_O_SYNC) nativeFlags |= O_SYNC;
    if (flags & PAL_O_NOFOLLOW) nativeFlags |= O_NOFOLLOW;
    char buffer[FS_MAX_PATH + 8];
    const char* native = NativePath(path, buffer);
    return native ? open(native, nativeFlags, mode) : -1;
}

int32_t SystemNative_Close(intptr_t fd)
{
    if (!ValidDescriptor(fd)) return -1;
    mutexLock(&s_positionLock);
    int result = LibnxCloseDescriptor((int)fd);
    mutexUnlock(&s_positionLock);
    return result;
}
int32_t SystemNative_FSync(intptr_t fd) { return ValidDescriptor(fd) ? fsync((int)fd) : -1; }
int32_t SystemNative_FTruncate(intptr_t fd, int64_t length)
{
    if (length < 0) { errno = EINVAL; return -1; }
    if (!ValidDescriptor(fd)) return -1;
    mutexLock(&s_positionLock);
    int result = ftruncate((int)fd, length);
    mutexUnlock(&s_positionLock);
    return result;
}
int64_t SystemNative_LSeek(intptr_t fd, int64_t offset, int32_t whence)
{
    if (whence != PAL_SEEK_SET && whence != PAL_SEEK_CUR && whence != PAL_SEEK_END)
    {
        errno = EINVAL;
        return -1;
    }
    if (!ValidDescriptor(fd)) return -1;
    mutexLock(&s_positionLock);
    int64_t result = lseek((int)fd, offset, whence);
    mutexUnlock(&s_positionLock);
    return result;
}
int32_t SystemNative_Read(intptr_t fd, void* buffer, int32_t size)
{
    if (size < 0) { errno = EINVAL; return -1; }
    if (!ValidDescriptor(fd)) return -1;
    mutexLock(&s_positionLock);
    int32_t result = read((int)fd, buffer, (size_t)size);
    mutexUnlock(&s_positionLock);
    return result;
}
int32_t SystemNative_Write(intptr_t fd, const void* buffer, int32_t size)
{
    if (size < 0) { errno = EINVAL; return -1; }
    if (!ValidDescriptor(fd)) return -1;
    if (fd == 1 || fd == 2)
        LibnxInitializeDebugStdio();
    mutexLock(&s_positionLock);
    int32_t result = write((int)fd, buffer, (size_t)size);
    mutexUnlock(&s_positionLock);
    return result;
}

static int64_t PositionedIoLocked(intptr_t fd, void* buffer, size_t size, int64_t offset, bool writing)
{
    int64_t result = -1;
    off_t saved = lseek((int)fd, 0, SEEK_CUR);
    if (saved >= 0 && lseek((int)fd, offset, SEEK_SET) >= 0)
    {
        result = writing ? write((int)fd, buffer, (size_t)size) : read((int)fd, buffer, (size_t)size);
        int operationError = errno;
        if (lseek((int)fd, saved, SEEK_SET) < 0 && result >= 0)
            result = -1;
        else
            errno = operationError;
    }
    return result;
}
static int32_t PositionedIo(intptr_t fd, void* buffer, int32_t size, int64_t offset, bool writing)
{
    if (size < 0 || offset < 0) { errno = EINVAL; return -1; }
    if (!ValidDescriptor(fd)) return -1;
    mutexLock(&s_positionLock);
    int32_t result = PositionedIoLocked(fd, buffer, (size_t)size, offset, writing);
    mutexUnlock(&s_positionLock);
    return result;
}
int32_t SystemNative_PRead(intptr_t fd, void* buffer, int32_t size, int64_t offset)
{
    return PositionedIo(fd, buffer, size, offset, false);
}
int32_t SystemNative_PWrite(intptr_t fd, void* buffer, int32_t size, int64_t offset)
{
    return PositionedIo(fd, buffer, size, offset, true);
}

static int64_t PositionedVectorIo(intptr_t fd, IOVector* vectors, int32_t count, int64_t offset, bool writing)
{
    if (count < 0 || count > 1024 || offset < 0 || (count > 0 && !vectors))
    {
        errno = EINVAL;
        return -1;
    }
    if (!ValidDescriptor(fd)) return -1;
    uint64_t requested = 0;
    for (int32_t index = 0; index < count; index++)
    {
        if (vectors[index].Count > (uint64_t)INT64_MAX - (uint64_t)offset - requested)
        {
            errno = EOVERFLOW;
            return -1;
        }
        requested += vectors[index].Count;
    }
    mutexLock(&s_positionLock);
    int64_t total = 0;
    if (count == 0 && lseek((int)fd, 0, SEEK_CUR) < 0)
        total = -1;
    for (int32_t index = 0; index < count; index++)
    {
        int64_t completed = PositionedIoLocked(fd, vectors[index].Base, vectors[index].Count, offset + total, writing);
        if (completed < 0)
        {
            if (total == 0)
                total = -1;
            break;
        }
        total += completed;
        if ((uint64_t)completed < vectors[index].Count)
            break;
    }
    mutexUnlock(&s_positionLock);
    return total;
}
int64_t SystemNative_PReadV(intptr_t fd, IOVector* vectors, int32_t count, int64_t offset)
{
    return PositionedVectorIo(fd, vectors, count, offset, false);
}
int64_t SystemNative_PWriteV(intptr_t fd, IOVector* vectors, int32_t count, int64_t offset)
{
    return PositionedVectorIo(fd, vectors, count, offset, true);
}

int32_t SystemNative_MkDir(const char* path, int32_t mode)
{
    char buffer[FS_MAX_PATH + 8];
    const char* native = NativePath(path, buffer);
    return native ? mkdir(native, mode) : -1;
}
int32_t SystemNative_RmDir(const char* path)
{
    char buffer[FS_MAX_PATH + 8];
    const char* native = NativePath(path, buffer);
    return native ? rmdir(native) : -1;
}
int32_t SystemNative_Unlink(const char* path)
{
    char buffer[FS_MAX_PATH + 8];
    const char* native = NativePath(path, buffer);
    return native ? unlink(native) : -1;
}
DIR* SystemNative_OpenDir(const char* path)
{
    char buffer[FS_MAX_PATH + 8];
    const char* native = NativePath(path, buffer);
    return native ? opendir(native) : NULL;
}
int32_t SystemNative_CloseDir(DIR* directory) { return closedir(directory); }
int32_t SystemNative_ReadDir(DIR* directory, DirectoryEntry* output)
{
    errno = 0;
    struct dirent* entry = readdir(directory);
    memset(output, 0, sizeof(*output));
    if (!entry)
        return errno ? errno : -1;
    output->Name = entry->d_name;
    output->NameLength = strlen(entry->d_name);
    output->InodeType = PAL_DT_UNKNOWN;
    return 0;
}
char* SystemNative_GetCwd(char* buffer, int32_t size)
{
    if (!buffer || size <= 0) { errno = EINVAL; return NULL; }
    char native[FS_MAX_PATH + 8];
    if (!getcwd(native, sizeof(native)))
        return NULL;
    const char* path = native;
    if (strncmp(path, "sdmc:", 5) == 0)
        path += 5;
    else if (strncmp(path, "romfs:/", 7) == 0)
    {
        // Replacing "romfs:" with "/romfs" preserves the path length.
        memcpy(native, "/romfs", 6);
    }
    if (path[0] != '/') { errno = ENOTSUP; return NULL; }
    size_t length = strlen(path);
    if (length >= (size_t)size) { errno = ERANGE; return NULL; }
    memcpy(buffer, path, length + 1);
    return buffer;
}

// Optional filesystem capabilities: report absence so managed callers can use
// their supported fallback paths, without claiming that locking succeeded.
int32_t SystemNative_FLock(intptr_t fd, int32_t operation) { (void)fd; (void)operation; errno = ENOTSUP; return -1; }
int32_t SystemNative_FAllocate(intptr_t fd, int64_t offset, int64_t length)
{ (void)fd; (void)offset; (void)length; errno = ENOTSUP; return -1; }
int32_t SystemNative_PosixFAdvise(intptr_t fd, int64_t offset, int64_t length, int32_t advice)
{ (void)fd; (void)offset; (void)length; (void)advice; return ENOTSUP; }
uint32_t SystemNative_GetFileSystemType(intptr_t fd) { (void)fd; return 0; }
int32_t SystemNative_LChflagsCanSetHiddenFlag(void) { return 0; }
int32_t SystemNative_CanGetHiddenFlag(void) { return 0; }

// fsdev exposes a single synthetic Unix owner for its filesystem entries.
uint32_t SystemNative_GetEUid(void) { return 0; }
uint32_t SystemNative_GetEGid(void) { return 0; }
int32_t SystemNative_GetGroups(int32_t count, uint32_t* groups)
{
    if (count < 0 || (count > 0 && !groups)) { errno = EINVAL; return -1; }
    if (count > 0)
        groups[0] = 0;
    return 1;
}

int32_t SystemNative_ReadLink(const char* path, char* output, int32_t size)
{
    (void)output;
    if (size < 0) { errno = EINVAL; return -1; }
    char buffer[FS_MAX_PATH + 8];
    const char* native = NativePath(path, buffer);
    if (!native)
        return -1;
    struct stat status;
    if (stat(native, &status) != 0)
        return -1;
    errno = EINVAL; // fsdev entries are not symbolic links.
    return -1;
}
int32_t SystemNative_GetPwUidR(uint32_t uid, Passwd* result, char* buffer, int32_t size)
{
    (void)uid; (void)buffer;
    if (!result || size < 0) return EINVAL;
    memset(result, 0, sizeof(*result));
    return -1; // This filesystem host has no passwd database.
}
