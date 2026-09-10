// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#pragma once
#include <stdint.h>

// Opt-in read staging for libnx hosts. The default preserves the original path.
void SystemNative_LibnxSetReadBuffering(int32_t enabled);
uint64_t SystemNative_LibnxGetBufferedReadCount(void);
