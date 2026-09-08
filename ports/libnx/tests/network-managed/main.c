// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
u32 __nx_applet_type = AppletType_None;
extern int managed_network_main(void);
extern void SystemNative_LibnxQuiesceSocketEvents(void);
void network_record(const char* message)
{
    char buffer[4096];
    int count = snprintf(buffer, sizeof(buffer), "[AOTMANET] %s\n", message);
    svcOutputDebugString(buffer, count < (int)sizeof(buffer) ? count : (int)sizeof(buffer) - 1);
}
int main(void)
{
    Result result = socketInitializeDefault();
    char message[100];
    snprintf(message, sizeof(message), "socket.init=%u", result);
    network_record(message);
    if (R_FAILED(result)) return 1;
    setenv("DOTNET_SYSTEM_NET_SOCKETS_THREAD_COUNT", "1", 1);
    int exitCode = managed_network_main();
    SystemNative_LibnxQuiesceSocketEvents();
    socketExit();
    svcSleepThread(100000000);
    network_record("native.quiesce=1");
    return exitCode;
}
