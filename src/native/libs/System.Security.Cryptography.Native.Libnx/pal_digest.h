// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#pragma once
#include <stdint.h>
#include <mbedtls/md.h>

typedef mbedtls_md_info_t DigestType;
typedef struct DigestContext DigestContext;

int32_t CryptoNative_EnsureOpenSslInitialized(void);
const DigestType* CryptoNative_EvpMd5(void);
const DigestType* CryptoNative_EvpSha1(void);
const DigestType* CryptoNative_EvpSha256(void);
const DigestType* CryptoNative_EvpSha384(void);
const DigestType* CryptoNative_EvpSha512(void);
const DigestType* CryptoNative_EvpSha3_256(void);
const DigestType* CryptoNative_EvpSha3_384(void);
const DigestType* CryptoNative_EvpSha3_512(void);
const DigestType* CryptoNative_EvpShake128(void);
const DigestType* CryptoNative_EvpShake256(void);
int32_t CryptoNative_EvpMdSize(const DigestType* type);
int32_t CryptoNative_GetMaxMdSize(void);
DigestContext* CryptoNative_EvpMdCtxCreate(const DigestType* type);
void CryptoNative_EvpMdCtxDestroy(DigestContext* context);
DigestContext* CryptoNative_EvpMdCtxCopyEx(const DigestContext* context);
int32_t CryptoNative_EvpDigestReset(DigestContext* context, const DigestType* type);
int32_t CryptoNative_EvpDigestUpdate(DigestContext* context, const void* source, int32_t size);
int32_t CryptoNative_EvpDigestFinalEx(DigestContext* context, uint8_t* destination, uint32_t* size);
int32_t CryptoNative_EvpDigestCurrent(const DigestContext* context, uint8_t* destination, uint32_t* size);
int32_t CryptoNative_EvpDigestOneShot(const DigestType* type, const void* source, int32_t sourceSize,
                                    uint8_t* destination, uint32_t* size);
void CryptoNative_ErrClearError(void);
uint64_t CryptoNative_ErrGetExceptionError(int32_t* allocationFailure);
void CryptoNative_ErrErrorStringN(uint64_t error, char* buffer, int32_t size);
