// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#ifndef NATIVEAOT_PTHREAD_TLS_H
#define NATIVEAOT_PTHREAD_TLS_H

#ifdef FEATURE_PTHREAD_TLS
#include <stdint.h>

struct RuntimeThreadLocals;

bool PalInitializePthreadTls();
RuntimeThreadLocals* PalGetPthreadRuntimeThreadLocals();
void PalAttachPthreadThread(void* thread);
intptr_t* PalGetPthreadThunkData();
void* PalGetPthreadLastAllocationType();
void PalSetPthreadLastAllocationType(void* type);
void* PalGetPthreadEventPipeThreadHolder();
void PalSetPthreadEventPipeThreadHolder(void* holder);
#endif

#endif
