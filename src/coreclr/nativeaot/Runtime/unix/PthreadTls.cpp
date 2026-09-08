// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "common.h"
#include "gcenv.h"
#include "thread.h"
#include "Pal.h"
#include "PthreadTls.h"

#include <new>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

namespace
{
    struct PthreadTlsState
    {
        RuntimeThreadLocals ThreadLocals;
        ee_alloc_context::PerThreadRandom Random;
        intptr_t ThunkData;
        void* LastAllocationType;
        void* EventPipeThreadHolder;
        void* AttachedThread;
    };

    pthread_once_t s_tlsOnce = PTHREAD_ONCE_INIT;
    pthread_key_t s_tlsKey;
    int s_tlsKeyStatus;

    void DestroyPthreadTls(void* value)
    {
        PthreadTlsState* state = static_cast<PthreadTlsState*>(value);

        // POSIX clears the key before invoking its destructor. Runtime shutdown
        // still accesses the current thread and EventPipe state, so keep the
        // entire state available until those callbacks have completed.
        if (pthread_setspecific(s_tlsKey, state) != 0)
            abort();

        if (state->AttachedThread != nullptr)
            RuntimeThreadShutdown(state->AttachedThread);

        if (pthread_setspecific(s_tlsKey, nullptr) != 0)
            abort();
        delete state;
    }

    void InitializePthreadTls()
    {
        s_tlsKeyStatus = pthread_key_create(&s_tlsKey, DestroyPthreadTls);
    }

    PthreadTlsState* GetPthreadTlsState()
    {
        int savedError = errno;
        if (!PalInitializePthreadTls())
            abort();

        PthreadTlsState* state = static_cast<PthreadTlsState*>(pthread_getspecific(s_tlsKey));
        if (state == nullptr)
        {
            state = new (std::nothrow) PthreadTlsState();
            if (state == nullptr)
                abort();
            if (pthread_setspecific(s_tlsKey, state) != 0)
            {
                delete state;
                abort();
            }
        }
        errno = savedError;
        return state;
    }
}

bool PalInitializePthreadTls()
{
    return pthread_once(&s_tlsOnce, InitializePthreadTls) == 0 && s_tlsKeyStatus == 0;
}

RuntimeThreadLocals* PalGetPthreadRuntimeThreadLocals()
{
    return &GetPthreadTlsState()->ThreadLocals;
}

void PalAttachPthreadThread(void* thread)
{
    PthreadTlsState* state = GetPthreadTlsState();
    if (thread != &state->ThreadLocals || state->AttachedThread != nullptr)
        abort();
    state->AttachedThread = thread;
}

ee_alloc_context::PerThreadRandom& ee_alloc_context::GetThreadRandom()
{
    return GetPthreadTlsState()->Random;
}

intptr_t* PalGetPthreadThunkData()
{
    return &GetPthreadTlsState()->ThunkData;
}

void* PalGetPthreadLastAllocationType()
{
    return GetPthreadTlsState()->LastAllocationType;
}

void PalSetPthreadLastAllocationType(void* type)
{
    GetPthreadTlsState()->LastAllocationType = type;
}

void* PalGetPthreadEventPipeThreadHolder()
{
    return GetPthreadTlsState()->EventPipeThreadHolder;
}

void PalSetPthreadEventPipeThreadHolder(void* holder)
{
    GetPthreadTlsState()->EventPipeThreadHolder = holder;
}
