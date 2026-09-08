// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include <pthread.h>
#include <stdint.h>
#include <errno.h>

int32_t LegacyIOS_SetErrno(int32_t value)
{
    errno = value;
    return -1;
}

typedef double (*MixedCallback)(int64_t, double, double, double, double, double, double, double, double);

struct CallbackState
{
    MixedCallback Callback;
    double Result;
};

static void* RunCallback(void* argument)
{
    struct CallbackState* state = argument;
    state->Result = state->Callback(37, 1, 2, 3, 4, 5, 6, 7, 8);
    return NULL;
}

double LegacyIOS_InvokeCallback(MixedCallback callback, int32_t foreignThread)
{
    struct CallbackState state = {callback, 0};
    if (foreignThread)
    {
        pthread_t thread;
        if (pthread_create(&thread, NULL, RunCallback, &state) != 0)
            return -1;
        if (pthread_join(thread, NULL) != 0)
            return -2;
    }
    else
    {
        RunCallback(&state);
    }
    return state.Result;
}
