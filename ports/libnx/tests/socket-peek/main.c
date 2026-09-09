// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
u32 __nx_applet_type = AppletType_None;
static void record(const char* key, int value)
{
    char line[128];
    int count = snprintf(line, sizeof(line), "[AOTPEEK] %s=%d\n", key, value);
    svcOutputDebugString(line, count);
}
int main(void)
{
    record("begin", 1);
    if (R_FAILED(socketInitializeDefault())) return 1;
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int sender = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int receiver = -1;
    int passed = 0;
    struct sockaddr_in address = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    unsigned char raw_address[0x100] = {0};
    socklen_t size = sizeof(raw_address);
    if (listener < 0 || sender < 0 || bind(listener, (struct sockaddr*)&address, sizeof(address)) != 0 ||
        listen(listener, 1) != 0 || getsockname(listener, (struct sockaddr*)raw_address, &size) != 0)
        goto done;
    if (connect(sender, (struct sockaddr*)raw_address, sizeof(address)) != 0) goto done;
    receiver = accept(listener, NULL, NULL);
    if (receiver < 0) goto done;
    const unsigned char expected[] = { 0x16, 0x03, 0x01, 0x04 };
    unsigned char peek = 0, actual[sizeof(expected)] = {0};
    if (send(sender, expected, sizeof(expected), 0) != sizeof(expected)) goto done;
    int peek_count = recv(receiver, &peek, 1, MSG_PEEK);
    int read_count = recv(receiver, actual, sizeof(actual), 0);
    record("peek.count", peek_count);
    record("peek.byte", peek);
    record("read.count", read_count);
    record("read.first", actual[0]);
    passed = peek_count == 1 && peek == expected[0] && read_count == sizeof(expected) &&
        memcmp(expected, actual, sizeof(expected)) == 0;
done:
    if (receiver >= 0) close(receiver);
    if (sender >= 0) close(sender);
    if (listener >= 0) close(listener);
    socketExit();
    record("peek.preserves_data", passed);
    record("pass", passed);
    return passed ? 0 : 1;
}
