// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include <switch.h>
#include <cstdint>
#include <cstdio>

extern "C"
{
    u32 __nx_applet_type = AppletType_None;
    thread_local uint64_t tls_probe_value = 7;
    uintptr_t nx_runtime_tls_address();
    uint64_t nx_runtime_tls_preserve_args(uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t);
}

static void Record(const char* name, uint64_t value)
{
    char buffer[192];
    int length = std::snprintf(buffer, sizeof(buffer), "[AOTTLS] %s=%llu\n", name,
                               static_cast<unsigned long long>(value));
    if (length > 0 && static_cast<size_t>(length) < sizeof(buffer))
    {
        svcOutputDebugString(buffer, length);
    }
}

static bool CheckTls(uint64_t value)
{
    // Compare addresses before dereferencing: the unmodified Linux macro is a
    // negative control, and must not read memory through the wrong TLS base.
    if (nx_runtime_tls_address() != reinterpret_cast<uintptr_t>(&tls_probe_value))
    {
        return false;
    }
    uint64_t* address = reinterpret_cast<uint64_t*>(nx_runtime_tls_address());
    if (*address != tls_probe_value)
    {
        return false;
    }
    *address = value;
    return tls_probe_value == value &&
           nx_runtime_tls_preserve_args(1, 2, 3, 4, 5, 6, 7, 8) == 1;
}

struct WorkerState
{
    uint64_t Value;
    bool Passed;
};

static void Worker(void* argument)
{
    WorkerState* state = static_cast<WorkerState*>(argument);
    state->Passed = tls_probe_value == 7 && CheckTls(state->Value);
}

int main()
{
    Record("begin", 1);
#ifdef LIBNX_TLS_NEGATIVE_CONTROL
    Record("linux.control", 1);
#else
    Record("libnx.target", 1);
#endif
    bool mainPassed = tls_probe_value == 7 && CheckTls(11);
    Record("main.address_and_registers", mainPassed);
    if (!mainPassed)
    {
        Record("fail", 1);
        return 1;
    }

    WorkerState states[2] = {{21, false}, {31, false}};
    Thread threads[2];
    for (int index = 0; index < 2; index++)
    {
        Result result = threadCreate(&threads[index], Worker, &states[index], nullptr, 0x10000, 0x2C, -2);
        if (R_FAILED(result))
        {
            Record("fail.thread_create", result);
            return 2;
        }
        result = threadStart(&threads[index]);
        if (R_FAILED(result))
        {
            Record("fail.thread_start", result);
            return 3;
        }
    }
    for (int index = 0; index < 2; index++)
    {
        Result result = threadWaitForExit(&threads[index]);
        threadClose(&threads[index]);
        if (R_FAILED(result) || !states[index].Passed)
        {
            Record("fail.worker", index + 1);
            return 4;
        }
    }

    Record("workers.isolated", 1);
    Record("main.unchanged", tls_probe_value == 11);
    if (tls_probe_value != 11)
    {
        return 5;
    }
    Record("pass", 1);
    return 0;
}
