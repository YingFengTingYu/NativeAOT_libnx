// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// Implements the existing managed Unix digest PAL ABI using mbedTLS.
// These exports provide hashes only; they do not expose an OpenSSL/TLS backend.
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <mbedtls/md.h>
#include <mbedtls/error.h>
#include "pal_digest.h"

struct DigestContext
{
    mbedtls_md_context_t md;
};

static _Thread_local int s_error;

static int32_t Result(int error)
{
    if (error != 0)
        s_error = error;
    return error == 0;
}

int32_t CryptoNative_EnsureOpenSslInitialized(void)
{
    // The name is part of the stock managed ABI. Verify our actual backend's
    // advertised baseline algorithms, without reporting an OpenSSL version.
    const mbedtls_md_type_t algorithms[] = {
        MBEDTLS_MD_MD5, MBEDTLS_MD_SHA1, MBEDTLS_MD_SHA256, MBEDTLS_MD_SHA384, MBEDTLS_MD_SHA512
    };
    for (unsigned index = 0; index < sizeof(algorithms) / sizeof(algorithms[0]); index++)
    {
        if (!mbedtls_md_info_from_type(algorithms[index]))
            return -1;
    }
    return 0;
}

const DigestType* CryptoNative_EvpMd5(void) { return mbedtls_md_info_from_type(MBEDTLS_MD_MD5); }
const DigestType* CryptoNative_EvpSha1(void) { return mbedtls_md_info_from_type(MBEDTLS_MD_SHA1); }
const DigestType* CryptoNative_EvpSha256(void) { return mbedtls_md_info_from_type(MBEDTLS_MD_SHA256); }
const DigestType* CryptoNative_EvpSha384(void) { return mbedtls_md_info_from_type(MBEDTLS_MD_SHA384); }
const DigestType* CryptoNative_EvpSha512(void) { return mbedtls_md_info_from_type(MBEDTLS_MD_SHA512); }
// The managed layer uses null to report these optional algorithms unsupported.
const DigestType* CryptoNative_EvpSha3_256(void) { return NULL; }
const DigestType* CryptoNative_EvpSha3_384(void) { return NULL; }
const DigestType* CryptoNative_EvpSha3_512(void) { return NULL; }
const DigestType* CryptoNative_EvpShake128(void) { return NULL; }
const DigestType* CryptoNative_EvpShake256(void) { return NULL; }
int32_t CryptoNative_EvpMdSize(const DigestType* type) { return type ? mbedtls_md_get_size(type) : -1; }
int32_t CryptoNative_GetMaxMdSize(void) { return MBEDTLS_MD_MAX_SIZE; }

void CryptoNative_EvpMdCtxDestroy(DigestContext* context)
{
    if (context)
    {
        mbedtls_md_free(&context->md);
        free(context);
    }
}

DigestContext* CryptoNative_EvpMdCtxCreate(const DigestType* type)
{
    s_error = 0;
    if (!type)
    {
        Result(MBEDTLS_ERR_MD_BAD_INPUT_DATA);
        return NULL;
    }
    DigestContext* context = calloc(1, sizeof(*context));
    if (!context)
    {
        Result(MBEDTLS_ERR_MD_ALLOC_FAILED);
        return NULL;
    }
    mbedtls_md_init(&context->md);
    if (!Result(mbedtls_md_setup(&context->md, type, 0)) || !Result(mbedtls_md_starts(&context->md)))
    {
        CryptoNative_EvpMdCtxDestroy(context);
        return NULL;
    }
    return context;
}

int32_t CryptoNative_EvpDigestReset(DigestContext* context, const DigestType* type)
{
    s_error = 0;
    if (!context)
        return Result(MBEDTLS_ERR_MD_BAD_INPUT_DATA);
    if (type && type != context->md.md_info)
    {
        mbedtls_md_free(&context->md);
        mbedtls_md_init(&context->md);
        if (!Result(mbedtls_md_setup(&context->md, type, 0)))
            return 0;
    }
    return Result(mbedtls_md_starts(&context->md));
}

int32_t CryptoNative_EvpDigestUpdate(DigestContext* context, const void* source, int32_t size)
{
    if (!context || size < 0 || (size > 0 && !source))
        return Result(MBEDTLS_ERR_MD_BAD_INPUT_DATA);
    if (size == 0)
        return 1;
    return Result(mbedtls_md_update(&context->md, source, (size_t)size));
}

int32_t CryptoNative_EvpDigestFinalEx(DigestContext* context, uint8_t* destination, uint32_t* size)
{
    s_error = 0;
    if (!context || !destination || !size)
        return Result(MBEDTLS_ERR_MD_BAD_INPUT_DATA);
    int32_t result = Result(mbedtls_md_finish(&context->md, destination));
    if (result)
        *size = mbedtls_md_get_size(context->md.md_info);
    return result;
}

DigestContext* CryptoNative_EvpMdCtxCopyEx(const DigestContext* source)
{
    if (!source)
    {
        Result(MBEDTLS_ERR_MD_BAD_INPUT_DATA);
        return NULL;
    }
    DigestContext* copy = CryptoNative_EvpMdCtxCreate(source->md.md_info);
    if (copy && !Result(mbedtls_md_clone(&copy->md, &source->md)))
    {
        CryptoNative_EvpMdCtxDestroy(copy);
        return NULL;
    }
    return copy;
}

int32_t CryptoNative_EvpDigestCurrent(const DigestContext* context, uint8_t* destination, uint32_t* size)
{
    DigestContext* copy = CryptoNative_EvpMdCtxCopyEx(context);
    if (!copy)
        return 0;
    int32_t result = CryptoNative_EvpDigestFinalEx(copy, destination, size);
    CryptoNative_EvpMdCtxDestroy(copy);
    return result;
}

int32_t CryptoNative_EvpDigestOneShot(const DigestType* type, const void* source, int32_t sourceSize,
                                    uint8_t* destination, uint32_t* size)
{
    s_error = 0;
    if (!type || sourceSize < 0 || (sourceSize > 0 && !source) || !destination || !size)
        return Result(MBEDTLS_ERR_MD_BAD_INPUT_DATA);
    static const uint8_t empty;
    int32_t result = Result(mbedtls_md(type, sourceSize ? source : &empty, (size_t)sourceSize, destination));
    if (result)
        *size = mbedtls_md_get_size(type);
    return result;
}

void CryptoNative_ErrClearError(void) { s_error = 0; }
uint64_t CryptoNative_ErrGetExceptionError(int32_t* allocationFailure)
{
    if (allocationFailure)
        *allocationFailure = s_error == MBEDTLS_ERR_MD_ALLOC_FAILED;
    uint64_t error = (uint64_t)(-(int64_t)s_error);
    s_error = 0;
    return error;
}
void CryptoNative_ErrErrorStringN(uint64_t error, char* buffer, int32_t size)
{
    if (!buffer || size <= 0)
        return;
    int written = snprintf(buffer, (size_t)size, "libnx mbedTLS: ");
    if (written >= 0 && written < size)
        mbedtls_strerror(error <= INT32_MAX ? -(int)error : MBEDTLS_ERR_MD_BAD_INPUT_DATA,
                        buffer + written, (size_t)(size - written));
}
