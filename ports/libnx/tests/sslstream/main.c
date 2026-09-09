// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
u32 __nx_applet_type = AppletType_None;
extern int managed_tls_main(void);
extern void SystemNative_LibnxQuiesceSocketEvents(void);
void tls_record(const char* message)
{
    char buffer[8192];
    int count = snprintf(buffer, sizeof(buffer), "[AOTSSL] %s\n", message);
    svcOutputDebugString(buffer, count < (int)sizeof(buffer) ? count : (int)sizeof(buffer) - 1);
}
int main(void)
{
    Result result = socketInitializeDefault();
    if (R_FAILED(result)) { tls_record("socket.init.failed=1"); return 1; }
    tls_record("socket.init=0");
    setenv("DOTNET_SYSTEM_NET_SOCKETS_THREAD_COUNT", "1", 1);
    // Normal OpenSSL/BCL trust-store configuration, not a validation bypass.
    setenv("SSL_CERT_FILE", "/dotnet/ssl/cert.pem", 1);
    int exitCode = managed_tls_main();
    SystemNative_LibnxQuiesceSocketEvents();
    socketExit();
    svcSleepThread(100000000);
    tls_record("native.quiesce=1");
    return exitCode;
}
