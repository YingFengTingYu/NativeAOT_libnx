// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include "pal_digest.h"
#include "../System.Native/pal_random.h"

int32_t CryptoNative_GetRandomBytes(uint8_t* buffer, int32_t size)
{
    // RandomNumberGenerator and Guid share the same Horizon CSRNG source.
    // The crypto PAL reports one for success; System.Native reports zero.
    CryptoNative_ErrClearError();
    return SystemNative_GetCryptographicallySecureRandomBytes(buffer, size) == 0 ? 1 : 0;
}
