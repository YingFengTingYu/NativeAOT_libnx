// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "pal_dynamicload.h"
#include "pal_process.h"
#include <errno.h>
#include <stddef.h>

// This port only links native dependencies statically. Report the unavailable
// loader through the Unix PAL's failure contract; never invent library handles.
static _Thread_local const char* s_loaderError;

static void SetLoaderError(void)
{
    s_loaderError = "Dynamic library loading is not supported by the libnx static runtime.";
    errno = ENOTSUP;
}

void* SystemNative_LoadLibrary(const char* filename)
{
    (void)filename;
    SetLoaderError();
    return NULL;
}

void* SystemNative_GetLoadLibraryError(void)
{
    const char* error = s_loaderError;
    s_loaderError = NULL;
    // Like dlerror(), this is borrowed storage, not a malloc-owned string.
    return (void*)error;
}

void* SystemNative_GetProcAddress(void* handle, const char* symbol)
{
    (void)handle;
    (void)symbol;
    SetLoaderError();
    return NULL;
}

void SystemNative_FreeLibrary(void* handle)
{
    (void)handle;
    SetLoaderError();
}

void* SystemNative_GetDefaultSearchOrderPseudoHandle(void)
{
    SetLoaderError();
    return NULL;
}

char* SystemNative_GetProcessPath(void)
{
    // No host-provided executable path is currently registered. Environment's
    // nullable ProcessPath API permits the path to be unavailable.
    errno = ENOTSUP;
    return NULL;
}
