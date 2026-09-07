// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include "common.h"
#include "CommonTypes.h"
#include "CommonMacros.h"
#include "Pal.h"
#include "NativeContext.h"
#include <switch/arm/thread_context.h>

static PHARDWARE_EXCEPTION_HANDLER s_exceptionHandler;
extern "C" __attribute__((noreturn)) void LibnxRestoreExceptionContext(PAL_LIMITED_CONTEXT* context);

extern "C"
{
    alignas(16) uint8_t __nx_exception_stack[0x8000];
    uint64_t __nx_exception_stack_size = sizeof(__nx_exception_stack);
    uint32_t __nx_exception_ignoredebug = 1;
}

void PalSetHardwareExceptionHandler(PHARDWARE_EXCEPTION_HANDLER handler)
{
    s_exceptionHandler = handler;
}

extern "C" __attribute__((noreturn)) void __libnx_exception_handler(ThreadExceptionDump* dump)
{
    // libnx returns from the kernel exception into this callback on a separate
    // stack. A handled managed fault must tail-resume the runtime throw helper;
    // returning from this callback would make libnx abort the process.
    NativeContextStorage native = {};
    memcpy(native.uc_mcontext.cpu_gprs, dump->cpu_gprs, sizeof(dump->cpu_gprs));
    native.uc_mcontext.fp = dump->fp.x;
    native.uc_mcontext.lr = dump->lr.x;
    native.uc_mcontext.sp = dump->sp.x;
    native.uc_mcontext.pc = dump->pc;
    PAL_LIMITED_CONTEXT context = {};
    NativeContextToPalContext(&native, &context);
    for (int index = 0; index < 8; index++)
        memcpy(&context.D[index], &dump->fpu_gprs[index + 8], sizeof(uint64_t));
    uintptr_t arg0 = 0, arg1 = 0;
    uint32_t exceptionClass = dump->esr >> 26;
    bool memoryFault = exceptionClass == 0x20 || exceptionClass == 0x21 ||
                       exceptionClass == 0x24 || exceptionClass == 0x25;
    if (s_exceptionHandler && memoryFault &&
        s_exceptionHandler(STATUS_ACCESS_VIOLATION, dump->far.x, &context, &arg0, &arg1) == EXCEPTION_CONTINUE_EXECUTION)
    {
        context.X0 = arg0;
        context.X1 = arg1;
        LibnxRestoreExceptionContext(&context);
    }
    PalPrintFatalError("[NativeAOT/libnx] unhandled hardware exception\n");
    RhFailFast();
    __builtin_unreachable();
}

void PalCreateCrashDumpIfEnabled(void* context)
{
    const char* enabled = getenv("DOTNET_DbgEnableMiniDump");
    if (enabled && strcmp(enabled, "0") != 0)
        PalPrintFatalError("[NativeAOT/libnx] native minidump generation is not supported\n");
}

static_assert(offsetof(PAL_LIMITED_CONTEXT, D) == offsetof(PAL_LIMITED_CONTEXT, IP) + sizeof(uintptr_t),
              "The exception resume assembly requires contiguous IP and D fields");
