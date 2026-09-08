// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <sys/iosupport.h>
#include <unistd.h>
#include <pthread.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include "pal_console.h"
#include "pal_signal.h"
#include "pal_io.h"

static pthread_once_t s_stdioOnce = PTHREAD_ONCE_INIT;
static void InitializeStdio(void)
{
    consoleDebugInit(debugDevice_SVC);
    devoptab_list[STD_OUT] = devoptab_list[STD_ERR];
}
void LibnxInitializeDebugStdio(void) { pthread_once(&s_stdioOnce, InitializeStdio); }

intptr_t SystemNative_Dup(intptr_t fd)
{
    if (fd < 0 || fd > INT_MAX) { errno = EBADF; return -1; }
    LibnxInitializeDebugStdio();
    return dup((int)fd);
}
int32_t SystemNative_IsATty(intptr_t fd)
{
    (void)fd;
    errno = ENOTTY;
    return 0;
}
int32_t SystemNative_GetWindowSize(intptr_t fd, WinSize* size)
{
    (void)fd;
    if (size)
        memset(size, 0, sizeof(*size));
    errno = ENOTTY;
    return -1;
}
int32_t SystemNative_InitializeTerminalAndSignalHandling(void)
{
    // This host only provides redirected debug output and the default stdin
    // device. No terminal or POSIX signal delivery is advertised by the port.
    LibnxInitializeDebugStdio();
    return 1;
}
void SystemNative_UninitializeTerminal(void) {}
void SystemNative_SetKeypadXmit(intptr_t fd, const char* text) { (void)fd; (void)text; }
void SystemNative_SetTerminalInvalidationHandler(TerminalInvalidationCallback callback) { (void)callback; }
void SystemNative_GetControlCharacters(int32_t* names, uint8_t* values, int32_t count, uint8_t* disabled)
{
    (void)names;
    if (count > 0 && values)
        memset(values, 0, (size_t)count);
    if (disabled)
        *disabled = 0;
}
void SystemNative_InitializeConsoleBeforeRead(uint8_t minimum, uint8_t timeout) { (void)minimum; (void)timeout; }
void SystemNative_UninitializeConsoleAfterRead(void) {}
int32_t SystemNative_ReadStdin(void* buffer, int32_t count) { return SystemNative_Read(0, buffer, count); }
int32_t SystemNative_Poll(PollEvent* events, uint32_t count, int32_t milliseconds, uint32_t* triggered)
{
    return Common_Poll(events, count, milliseconds, triggered);
}
