// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/sockio.h>
#include "pal_networking.h"
#include "pal_io.h"
#include "pal_libnx_networking.h"

u32 __nx_applet_type = AppletType_None;
static void Record(const char* name, long long value)
{
    char message[200];
    int length = snprintf(message, sizeof(message), "[AOTNET] %s=%lld\n", name, value);
    svcOutputDebugString(message, length);
}
#define REQUIRE(expression) do { int r_ = (expression); if (!r_) { Record("failed_line", __LINE__); Record("errno", errno); return false; } } while (0)

static bool CheckSockets(void)
{
    REQUIRE(LibnxConvertPendingSocketError(110) == Error_ETIMEDOUT);
    REQUIRE(LibnxConvertPendingSocketError(115) == Error_EINPROGRESS);
    REQUIRE(LibnxConvertPendingSocketError(111) == Error_ECONNREFUSED);
    REQUIRE(LibnxConvertPendingSocketError(0) == Error_SUCCESS);
    REQUIRE(LibnxConvertPendingSocketError(9999) == Error_ENONSTANDARD);
    Record("pending_error.mapping", 1);
    HostEntry host = {0};
    int dns = SystemNative_GetHostEntryForName((uint8_t*)"example.com", AddressFamily_AF_INET, &host);
    Record("dns.error", dns);
    REQUIRE(dns == 0 && host.IPAddressCount > 0);
    Record("dns.addresses", host.IPAddressCount);
    SystemNative_FreeHostEntry(&host);

    intptr_t listener, client, accepted;
    uint8_t address[128] = {0};
    REQUIRE(SystemNative_SetAddressFamily(address, sizeof(address), AddressFamily_AF_INET) == 0);
    REQUIRE(SystemNative_SetIPv4Address(address, sizeof(address), htonl(INADDR_LOOPBACK)) == 0);
    REQUIRE(SystemNative_SetPort(address, sizeof(address), 0) == 0);
    REQUIRE(SystemNative_Socket(AddressFamily_AF_INET, SocketType_SOCK_STREAM, ProtocolType_PT_TCP, &listener) == 0);
    REQUIRE(SystemNative_Bind(listener, ProtocolType_PT_TCP, address, sizeof(struct sockaddr_in)) == 0);
    REQUIRE(SystemNative_Listen(listener, 4) == 0);
    int length = sizeof(address);
    REQUIRE(SystemNative_GetSockName(listener, address, &length) == 0);
    REQUIRE(SystemNative_Socket(AddressFamily_AF_INET, SocketType_SOCK_STREAM, ProtocolType_PT_TCP, &client) == 0);
    REQUIRE(SystemNative_Connect(client, address, length) == 0);
    uint8_t peer[128];
    int peerLength = sizeof(peer);
    REQUIRE(SystemNative_Accept(listener, peer, &peerLength, &accepted) == 0);
    int sent = 0, received = 0;
    char message[] = "libnx-native-tcp";
    char data[128] = {0};
    REQUIRE(SystemNative_Send(client, message, sizeof(message), 0, &sent) == 0 && sent == sizeof(message));
    REQUIRE(SystemNative_Receive(accepted, data, sizeof(data), 0, &received) == 0 && received == sizeof(message));
    REQUIRE(memcmp(message, data, sizeof(message)) == 0);
    Record("tcp.roundtrip", 1);

    REQUIRE(SystemNative_FcntlSetIsNonBlocking(accepted, 1) == 0);
    intptr_t port;
    REQUIRE(SystemNative_CreateSocketEventPort(&port) == 0);
    REQUIRE(SystemNative_TryChangeSocketEventRegistration(port, accepted, 0, SocketEvents_SA_READ, 0x1234) == 0);
    REQUIRE(SystemNative_Receive(accepted, data, sizeof(data), 0, &received) == Error_EAGAIN);
    for (int iteration = 0; iteration < 32; iteration++)
    {
        REQUIRE(SystemNative_Send(client, message, sizeof(message), 0, &sent) == 0);
        SocketEvent event;
        int count = 1;
        REQUIRE(SystemNative_WaitForSocketEvents(port, &event, &count) == 0 && count == 1);
        REQUIRE(event.Data == 0x1234 && (event.Events & SocketEvents_SA_READ));
        REQUIRE(SystemNative_Receive(accepted, data, sizeof(data), 0, &received) == 0 && received == sizeof(message));
        REQUIRE(SystemNative_Receive(accepted, data, sizeof(data), 0, &received) == Error_EAGAIN);
    }
    REQUIRE(SystemNative_Close(accepted) == 0);
    REQUIRE(SystemNative_CloseSocketEventPort(port) == 0);
    REQUIRE(SystemNative_Close(client) == 0);
    REQUIRE(SystemNative_Close(listener) == 0);
    Record("async.rearm_cleanup", 1);

    intptr_t udp;
    REQUIRE(SystemNative_Socket(AddressFamily_AF_INET, SocketType_SOCK_DGRAM, ProtocolType_PT_UDP, &udp) == 0);
    REQUIRE(SystemNative_SetPort(address, sizeof(address), 0) == 0);
    REQUIRE(SystemNative_Bind(udp, ProtocolType_PT_UDP, address, sizeof(struct sockaddr_in)) == 0);
    length = sizeof(address);
    REQUIRE(SystemNative_GetSockName(udp, address, &length) == 0);
    IOVector vector = {.Base = (uint8_t*)message, .Count = sizeof(message)};
    MessageHeader sendHeader = {.SocketAddress = address, .SocketAddressLen = length, .IOVectors = &vector, .IOVectorCount = 1};
    int64_t sent64 = 0, received64 = 0;
    int sendError = SystemNative_SendMessage(udp, &sendHeader, 0, &sent64);
    Record("udp.sendmsg_error", sendError);
    REQUIRE(sendError == 0 && sent64 == sizeof(message));
    vector.Base = (uint8_t*)data;
    vector.Count = sizeof(data);
    MessageHeader receiveHeader = {.SocketAddress = peer, .SocketAddressLen = sizeof(peer), .IOVectors = &vector, .IOVectorCount = 1};
    int receiveError = SystemNative_ReceiveMessage(udp, &receiveHeader, 0, &received64);
    Record("udp.recvmsg_error", receiveError);
    REQUIRE(receiveError == 0 && received64 == sizeof(message));
    REQUIRE(memcmp(message, data, sizeof(message)) == 0);
    Record("udp.roundtrip", 1);
    vector.Base = (uint8_t*)message;
    vector.Count = sizeof(message);
    REQUIRE(SystemNative_SendMessage(udp, &sendHeader, 0, &sent64) == 0);
    vector.Base = (uint8_t*)data;
    vector.Count = sizeof(data);
    uint8_t control[128] = {0};
    receiveHeader.ControlBuffer = control;
    receiveHeader.ControlBufferLen = sizeof(control);
    receiveHeader.SocketAddressLen = sizeof(peer);
    int ancillaryError = SystemNative_ReceiveMessage(udp, &receiveHeader, 0, &received64);
    Record("udp.ancillary_error", ancillaryError);
    Record("udp.ancillary_result", socketGetLastResult());

    struct ifreq requests[16] = {0};
    struct ifconf config = {.ifc_len = sizeof(requests), .ifc_req = requests};
    int result = ioctl((int)udp, SIOCGIFCONF, &config);
    Record("interfaces.ioctl", result);
    Record("interfaces.errno", errno);
    Record("interfaces.length", config.ifc_len);
    Result nifm = nifmInitialize(NifmServiceType_User);
    Record("nifm.init", nifm);
    if (R_SUCCEEDED(nifm))
    {
        uint32_t ip = 0, mask = 0;
        Result rc = nifmGetCurrentIpConfigInfo(&ip, &mask, NULL, NULL, NULL);
        Record("nifm.config", rc);
        Record("nifm.ip", ntohl(ip));
        Record("nifm.mask", ntohl(mask));
        uint32_t configAddress = 0, configMask = 0;
        REQUIRE(SystemNative_LibnxGetCurrentIpv4Config(&configAddress, &configMask) == Error_SUCCESS);
        REQUIRE(configAddress == ip && configMask == mask);
        Record("nifm.platform_export", 1);
        nifmExit();
    }
    if (result == 0)
        for (int index = 0; index < config.ifc_len / (int)sizeof(struct ifreq); index++)
        {
            char line[100];
            int size = snprintf(line, sizeof(line), "[AOTNET] interface=%s family=%d\n", requests[index].ifr_name, requests[index].ifr_addr.sa_family);
            svcOutputDebugString(line, size);
        }
    REQUIRE(SystemNative_Close(udp) == 0);
    return true;
}

int main(void)
{
    Record("begin", 1);
    Result result = socketInitializeDefault();
    Record("socket.init", result);
    if (R_FAILED(result)) return 1;
    bool passed = CheckSockets();
    socketExit();
    Record("pass", passed);
    return passed ? 0 : 1;
}
