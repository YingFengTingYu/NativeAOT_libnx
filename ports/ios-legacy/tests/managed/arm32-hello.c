// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include <stdio.h>

void LegacyIOS_Print(void)
{
    printf("ARM32 NativeAOT reached managed Main, pointer=%u\n", (unsigned)sizeof(void*));
}
