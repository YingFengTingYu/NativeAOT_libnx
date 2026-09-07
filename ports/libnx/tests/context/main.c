// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <stdio.h>

u32 __nx_applet_type = AppletType_None;
extern int nx_context_checks(void);
extern uint64_t nx_context_thread_id(void);
static uint64_t worker_id;

static void Record(const char* name, unsigned long long value)
{
    char buffer[128];
    int count = snprintf(buffer, sizeof(buffer), "[AOTCTX] %s=%llu\n", name, value);
    if (count > 0 && count < (int)sizeof(buffer))
        svcOutputDebugString(buffer, count);
}

static void Worker(void* argument)
{
    (void)argument;
    worker_id = nx_context_thread_id();
}

int main(void)
{
    Record("begin", 1);
    int result = nx_context_checks();
    Record("context.checks", result == 0);
    if (result != 0)
    {
        Record("fail.context", result);
        return result;
    }
    uint64_t main_id = nx_context_thread_id();
    Thread thread;
    if (R_FAILED(threadCreate(&thread, Worker, NULL, NULL, 0x10000, 0x2C, -2)) ||
        R_FAILED(threadStart(&thread)) || R_FAILED(threadWaitForExit(&thread)))
    {
        Record("fail.thread", 1);
        return 4;
    }
    threadClose(&thread);
    bool valid = main_id != 0 && worker_id != 0 && main_id != worker_id &&
                 main_id == nx_context_thread_id();
    Record("thread.ids", valid);
    if (!valid)
        return 5;
    Record("pass", 1);
    return 0;
}
