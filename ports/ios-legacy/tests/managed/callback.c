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

double LegacyIOS_OddDouble(int32_t a, double b, int32_t c)
{
    return a * 100 + b * 10 + c;
}

double LegacyIOS_SplitDouble(int32_t a, int32_t b, int32_t c, double d)
{
    return a + b + c + d;
}

int64_t LegacyIOS_OddInt64(int32_t a, int64_t b, int32_t c)
{
    return a + b + c;
}

int64_t LegacyIOS_SplitInt64(int32_t a, int32_t b, int32_t c, int64_t d)
{
    return a + b + c + d;
}

double LegacyIOS_InvokeOddDouble(double (*callback)(int32_t, double, int32_t))
{
    return callback(1, 2.5, 7);
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
