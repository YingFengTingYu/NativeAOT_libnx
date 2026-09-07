// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include "pal_digest.h"

u32 __nx_applet_type = AppletType_None;
static uint32_t s_ready, s_release, s_isolated;

static void Record(const char* name, unsigned value)
{
    char buffer[128];
    int size = snprintf(buffer, sizeof(buffer), "[AOTCRYPTO] %s=%u\n", name, value);
    if (size > 0 && size < (int)sizeof(buffer))
        svcOutputDebugString(buffer, size);
}

static void Worker(void* ignored)
{
    (void)ignored;
    CryptoNative_EvpDigestUpdate(NULL, NULL, 1);
    __atomic_store_n(&s_ready, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&s_release, __ATOMIC_ACQUIRE))
        svcSleepThread(1000000);
    int32_t allocation;
    bool ownError = CryptoNative_ErrGetExceptionError(&allocation) != 0 && !allocation;
    __atomic_store_n(&s_isolated, ownError, __ATOMIC_RELEASE);
}

int main(void)
{
    Record("begin", 1);
    if (CryptoNative_EnsureOpenSslInitialized() != 0)
        return 1;
    uint8_t output[64];
    uint32_t size = sizeof(output);
    const DigestType* type = CryptoNative_EvpSha256();
    bool invalid = CryptoNative_EvpDigestOneShot(type, NULL, -1, output, &size) == 0;
    int32_t allocation;
    uint64_t error = CryptoNative_ErrGetExceptionError(&allocation);
    char message[64] = {0};
    CryptoNative_ErrErrorStringN(error, message, sizeof(message));
    invalid = invalid && error != 0 && !allocation && strstr(message, "mbedTLS") != NULL &&
              CryptoNative_ErrGetExceptionError(&allocation) == 0;
    Record("errors.report_and_clear", invalid);

    Thread worker;
    if (R_FAILED(threadCreate(&worker, Worker, NULL, NULL, 0x10000, 0x2C, -2)) ||
        R_FAILED(threadStart(&worker)))
        return 2;
    for (unsigned attempt = 0; attempt < 1000 && !__atomic_load_n(&s_ready, __ATOMIC_ACQUIRE); attempt++)
        svcSleepThread(1000000);
    bool isolated = __atomic_load_n(&s_ready, __ATOMIC_ACQUIRE) != 0 &&
                    CryptoNative_ErrGetExceptionError(&allocation) == 0;
    __atomic_store_n(&s_release, 1, __ATOMIC_RELEASE);
    threadWaitForExit(&worker);
    threadClose(&worker);
    isolated = isolated && __atomic_load_n(&s_isolated, __ATOMIC_ACQUIRE) != 0;
    Record("errors.thread_local", isolated);
    bool unsupported = !CryptoNative_EvpSha3_256() && !CryptoNative_EvpShake128();
    Record("capabilities.unsupported", unsupported);
    Record("pass", invalid && isolated && unsupported);
    return 0;
}
