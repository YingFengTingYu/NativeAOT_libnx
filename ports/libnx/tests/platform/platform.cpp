// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include <cstdint>

#if !defined(HOST_LIBNX) || !defined(TARGET_LIBNX) || !defined(HOST_ARM64) || !defined(TARGET_ARM64)
#error The libnx platform configuration must select the matching host and target ABI.
#endif

#if defined(HOST_LINUX) || defined(TARGET_LINUX)
#error Linux platform definitions must not leak into the libnx build.
#endif

static_assert(sizeof(void*) == 8, "The libnx port is AArch64-only.");

thread_local uintptr_t libnx_platform_probe_value = 42;

extern "C" uintptr_t* libnx_platform_probe_tls()
{
    return &libnx_platform_probe_value;
}
