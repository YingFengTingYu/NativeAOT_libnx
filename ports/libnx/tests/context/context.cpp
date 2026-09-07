// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <cstdint>
#include <cstring>
#include "PalLimitedContext.h"
#include "NativeContext.h"
#include "CommonTypes.h"
#include "CommonMacros.h"
#include "Pal.h"
#include "minipal/thread.h"

extern "C" uint64_t nx_context_thread_id()
{
    size_t first = minipal_get_current_thread_id();
    return first == minipal_get_current_thread_id_no_cache() ? first : 0;
}

extern "C" int nx_context_checks()
{
    int64_t atomicValue = INT64_C(0x1122334455667788);
    int64_t previous = PalInterlockedExchange64(&atomicValue, INT64_C(0x7766554433221100));
    if (previous != INT64_C(0x1122334455667788) || atomicValue != INT64_C(0x7766554433221100))
        return 8;
    NATIVE_CONTEXT context = {};
    for (int index = 0; index < 29; index++)
        context.ctx.uc_mcontext.cpu_gprs[index].x = 100 + index;
    context.Fp() = 201;
    context.Lr() = 202;
    context.Sp() = 203;
    context.Pc() = 204;
    PAL_LIMITED_CONTEXT limited = {};
    NativeContextToPalContext(&context.ctx, &limited);
    if (limited.FP != 201 || limited.LR != 202 || limited.SP != 203 || limited.IP != 204 ||
        limited.X19 != 119 || limited.X20 != 120 || limited.X21 != 121 || limited.X22 != 122 ||
        limited.X23 != 123 || limited.X24 != 124 || limited.X25 != 125 || limited.X26 != 126 ||
        limited.X27 != 127 || limited.X28 != 128)
        return 1;

    limited.FP = 301;
    limited.LR = 302;
    limited.SP = 303;
    limited.IP = 304;
    RedirectNativeContext(&context.ctx, &limited, 401, 402);
    if (context.Fp() != 301 || context.Lr() != 302 || context.Sp() != 303 || context.Pc() != 304 ||
        context.X0() != 401 || context.X1() != 402 || context.X19() != 119)
        return 2;

    unsigned count = 0;
    context.ForEachPossibleObjectRef([&count](size_t* slot) { ++*slot; ++count; });
    if (count != 30 || context.X0() != 402 || context.X1() != 403 ||
        context.X28() != 129 || context.Lr() != 303 || context.Sp() != 303 || context.Fp() != 301)
        return 3;
    return 0;
}
