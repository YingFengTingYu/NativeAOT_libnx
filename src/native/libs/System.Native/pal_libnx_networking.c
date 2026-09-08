// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <switch.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include "pal_libnx_networking.h"
#include "pal_interfaceaddresses.h"

// libnx exposes poll, but no epoll/kqueue. .NET keeps a registration for the
// lifetime of a socket. Deliver each ready direction once, then rearm when a
// nonblocking operation reports EAGAIN. This preserves readiness across the
// managed operation queue without continuously reporting writable sockets.
typedef struct LibnxSocketRegistration
{
    int fd;
    int events;
    int armed;
    uintptr_t data;
    struct LibnxSocketRegistration* next;
} LibnxSocketRegistration;

typedef struct LibnxSocketPort
{
    int id;
    LibnxSocketRegistration* registrations;
    struct LibnxSocketPort* next;
} LibnxSocketPort;

static Mutex s_socketLock;
static LibnxSocketPort* s_ports;
static int s_nextPort = 1;
static bool s_socketEventsQuiesced;

// The Unix BCL has no event-engine shutdown protocol. Before socketExit the
// host parks its background waiters here, without returning a fabricated event
// or an error that would make SocketAsyncEngine call FailFast. Process-final
// only: callers must dispose their managed sockets/HTTP clients first.
PALEXPORT void SystemNative_LibnxQuiesceSocketEvents(void)
{
    mutexLock(&s_socketLock);
    s_socketEventsQuiesced = true;
    mutexUnlock(&s_socketLock);
}

static LibnxSocketPort* FindPort(int id)
{
    for (LibnxSocketPort* port = s_ports; port; port = port->next)
        if (port->id == id) return port;
    return NULL;
}

// BSD address output is length-prefixed. Use an ABI-sized scratch buffer and
// copy its actual IPv4/IPv6 address, even when a service reports its entire
// storage size (Eden v0.2.1 reports 0x100 for a 16-byte IPv4 address).
typedef union
{
    struct sockaddr_storage alignment;
    uint8_t bytes[0x100];
} LibnxSocketAddress;

static bool CopySocketAddress(struct sockaddr* output, socklen_t* length, LibnxSocketAddress* input, socklen_t actual)
{
    struct sockaddr* address = (struct sockaddr*)input;
    socklen_t expected = address->sa_family == AF_INET ? sizeof(struct sockaddr_in) :
        address->sa_family == AF_INET6 ? sizeof(struct sockaddr_in6) : actual;
    if (expected > sizeof(*input) || actual < expected || *length < expected) { errno = EINVAL; return false; }
    memcpy(output, input, expected);
    *length = expected;
    return true;
}

int LibnxSocketGetName(int fd, struct sockaddr* address, socklen_t* length)
{
    LibnxSocketAddress buffer = {0};
    socklen_t size = sizeof(buffer);
    int result = getsockname(fd, (struct sockaddr*)&buffer, &size);
    return result < 0 || !CopySocketAddress(address, length, &buffer, size) ? -1 : 0;
}

int LibnxSocketGetPeerName(int fd, struct sockaddr* address, socklen_t* length)
{
    LibnxSocketAddress buffer = {0};
    socklen_t size = sizeof(buffer);
    int result = getpeername(fd, (struct sockaddr*)&buffer, &size);
    return result < 0 || !CopySocketAddress(address, length, &buffer, size) ? -1 : 0;
}

static void RearmSocket(int fd, int events, int savedErrno)
{
    if (savedErrno == EAGAIN || savedErrno == EWOULDBLOCK || savedErrno == EINPROGRESS)
    {
        mutexLock(&s_socketLock);
        for (LibnxSocketPort* port = s_ports; port; port = port->next)
            for (LibnxSocketRegistration* entry = port->registrations; entry; entry = entry->next)
                if (entry->fd == fd) entry->armed |= events & entry->events;
        mutexUnlock(&s_socketLock);
    }
    errno = savedErrno;
}

static void NormalizeMessageError(void)
{
    if (errno == EPIPE)
    {
        Result result = socketGetLastResult();
        if (result == MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer) ||
            result == MAKERESULT(Module_Libnx, LibnxError_InvalidCmifOutHeader))
            errno = ENOTSUP;
    }
}

int32_t LibnxConvertPendingSocketError(int error)
{
    // getsockopt(SO_ERROR) returns the Horizon BSD service's Linux-numbered
    // errno payload. Unlike failing socket calls, libnx does not convert that
    // payload to Newlib errno (notably timeout 110 vs 116 and in-progress 115
    // vs 119). Translate its network range directly to the portable PAL enum.
    static const int32_t networkErrors[] = {
        Error_ENOTSOCK, Error_EDESTADDRREQ, Error_EMSGSIZE, Error_EPROTOTYPE,
        Error_ENOPROTOOPT, Error_EPROTONOSUPPORT, Error_ESOCKTNOSUPPORT, Error_ENOTSUP,
        Error_EPFNOSUPPORT, Error_EAFNOSUPPORT, Error_EADDRINUSE, Error_EADDRNOTAVAIL,
        Error_ENETDOWN, Error_ENETUNREACH, Error_ENETRESET, Error_ECONNABORTED,
        Error_ECONNRESET, Error_ENOBUFS, Error_EISCONN, Error_ENOTCONN,
        Error_ESHUTDOWN, Error_ENONSTANDARD, Error_ETIMEDOUT, Error_ECONNREFUSED,
        Error_EHOSTDOWN, Error_EHOSTUNREACH, Error_EALREADY, Error_EINPROGRESS
    };
    if (error >= 88 && error <= 115) return networkErrors[error - 88];
    if (error == 125) return Error_ECANCELED;
    if (error >= 0 && error <= 34) return SystemNative_ConvertErrorPlatformToPal(error);
    return Error_ENONSTANDARD;
}

int LibnxCloseDescriptor(int fd)
{
    mutexLock(&s_socketLock);
    for (LibnxSocketPort* port = s_ports; port; port = port->next)
    {
        LibnxSocketRegistration** link = &port->registrations;
        while (*link)
        {
            LibnxSocketRegistration* entry = *link;
            if (entry->fd == fd)
            {
                *link = entry->next;
                free(entry);
            }
            else link = &entry->next;
        }
    }
    int result = close(fd);
    int savedErrno = errno;
    mutexUnlock(&s_socketLock);
    errno = savedErrno;
    return result;
}

ssize_t LibnxSocketReceive(int fd, void* data, size_t count, int flags)
{
    ssize_t result = recv(fd, data, count, flags);
    if (result < 0) RearmSocket(fd, SocketEvents_SA_READ, errno);
    return result;
}

ssize_t LibnxSocketReceiveMessage(int fd, struct msghdr* message, int flags)
{
    ssize_t result;
    if (message->msg_iovlen == 1 && message->msg_controllen == 0)
    {
        LibnxSocketAddress address = {0};
        socklen_t addressSize = sizeof(address);
        result = recvfrom(fd, message->msg_iov[0].iov_base, message->msg_iov[0].iov_len,
            flags, message->msg_name ? (struct sockaddr*)&address : NULL, message->msg_name ? &addressSize : NULL);
        if (result >= 0 && message->msg_name && !CopySocketAddress(message->msg_name, &message->msg_namelen, &address, addressSize))
            result = -1;
        message->msg_flags = 0;
    }
    else
    {
        result = recvmsg(fd, message, flags);
        if (result < 0) NormalizeMessageError();
    }
    if (result < 0) RearmSocket(fd, SocketEvents_SA_READ, errno);
    return result;
}

ssize_t LibnxSocketSend(int fd, const void* data, size_t count, int flags)
{
    ssize_t result = send(fd, data, count, flags);
    if (result < 0) RearmSocket(fd, SocketEvents_SA_WRITE, errno);
    return result;
}

ssize_t LibnxSocketSendMessage(int fd, const struct msghdr* message, int flags)
{
    ssize_t result;
    if (message->msg_iovlen == 1 && message->msg_controllen == 0)
        result = sendto(fd, message->msg_iov[0].iov_base, message->msg_iov[0].iov_len,
            flags, message->msg_name, message->msg_namelen);
    else
    {
        result = sendmsg(fd, message, flags);
        if (result < 0) NormalizeMessageError();
    }
    if (result < 0) RearmSocket(fd, SocketEvents_SA_WRITE, errno);
    return result;
}

int LibnxSocketAccept(int fd, struct sockaddr* address, socklen_t* length)
{
    LibnxSocketAddress buffer = {0};
    socklen_t size = sizeof(buffer);
    int result = accept(fd, (struct sockaddr*)&buffer, &size);
    if (result >= 0 && !CopySocketAddress(address, length, &buffer, size))
    {
        int savedErrno = errno;
        close(result);
        errno = savedErrno;
        return -1;
    }
    if (result < 0) RearmSocket(fd, SocketEvents_SA_READ, errno);
    return result;
}

int LibnxSocketConnect(int fd, const struct sockaddr* address, socklen_t length)
{
    int result = connect(fd, address, length);
    // BSD implementations (including Eden's Windows host) can use EWOULDBLOCK
    // for a pending nonblocking connect; the Unix BCL expects EINPROGRESS.
    if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) errno = EINPROGRESS;
    if (result < 0) RearmSocket(fd, SocketEvents_SA_WRITE, errno);
    return result;
}

int32_t LibnxCreateSocketEventPort(int32_t* id)
{
    LibnxSocketPort* port = calloc(1, sizeof(*port));
    if (!port) return Error_ENOMEM;
    mutexLock(&s_socketLock);
    if (s_nextPort == INT_MAX)
    {
        mutexUnlock(&s_socketLock);
        free(port);
        return Error_ENOSPC;
    }
    port->id = s_nextPort++;
    port->next = s_ports;
    s_ports = port;
    *id = port->id;
    mutexUnlock(&s_socketLock);
    return Error_SUCCESS;
}

int32_t LibnxCloseSocketEventPort(int32_t id)
{
    mutexLock(&s_socketLock);
    LibnxSocketPort** link = &s_ports;
    while (*link && (*link)->id != id) link = &(*link)->next;
    LibnxSocketPort* port = *link;
    if (!port)
    {
        mutexUnlock(&s_socketLock);
        return Error_EBADF;
    }
    *link = port->next;
    while (port->registrations)
    {
        LibnxSocketRegistration* entry = port->registrations;
        port->registrations = entry->next;
        free(entry);
    }
    free(port);
    mutexUnlock(&s_socketLock);
    return Error_SUCCESS;
}

int32_t LibnxChangeSocketEvents(int32_t id, int32_t fd, SocketEvents currentEvents, SocketEvents newEvents, uintptr_t data)
{
    (void)currentEvents;
    mutexLock(&s_socketLock);
    LibnxSocketPort* port = FindPort(id);
    if (!port)
    {
        mutexUnlock(&s_socketLock);
        return Error_EBADF;
    }
    LibnxSocketRegistration** link = &port->registrations;
    while (*link && (*link)->fd != fd) link = &(*link)->next;
    if (newEvents == SocketEvents_SA_NONE)
    {
        LibnxSocketRegistration* entry = *link;
        if (entry) { *link = entry->next; free(entry); }
    }
    else
    {
        if (!*link)
        {
            *link = calloc(1, sizeof(**link));
            if (!*link) { mutexUnlock(&s_socketLock); return Error_ENOMEM; }
        }
        (*link)->fd = fd;
        (*link)->events = newEvents;
        (*link)->armed = newEvents;
        (*link)->data = data;
    }
    mutexUnlock(&s_socketLock);
    return Error_SUCCESS;
}

int32_t LibnxWaitForSocketEvents(int32_t id, SocketEvent* buffer, int32_t* count)
{
    if (*count <= 0) return Error_EINVAL;
    for (;;)
    {
        // Poll while holding the registration lock: close cannot recycle a
        // libnx descriptor during devoptab-to-BSD descriptor translation.
        // Poll itself is nonblocking, keeping all mutations short; idle waits
        // occur outside the lock and notice a new registration within 2 ms.
        mutexLock(&s_socketLock);
        if (s_socketEventsQuiesced)
        {
            mutexUnlock(&s_socketLock);
            svcSleepThread(100000000);
            continue;
        }
        LibnxSocketPort* port = FindPort(id);
        if (!port) { mutexUnlock(&s_socketLock); *count = 0; return Error_EBADF; }
        int emitted = 0;
        for (LibnxSocketRegistration* entry = port->registrations; entry && emitted < *count; entry = entry->next)
        {
            if (!entry->armed) continue;
            struct pollfd pollFd = { .fd = entry->fd };
            if (entry->armed & (SocketEvents_SA_READ | SocketEvents_SA_READCLOSE)) pollFd.events |= POLLIN;
            if (entry->armed & SocketEvents_SA_WRITE) pollFd.events |= POLLOUT;
            int result = poll(&pollFd, 1, 0);
            if (result < 0)
            {
                int error = SystemNative_ConvertErrorPlatformToPal(errno);
                if (error == Error_EINTR) continue;
                mutexUnlock(&s_socketLock);
                *count = 0;
                return error;
            }
            int events = 0;
            if (pollFd.revents & POLLIN) events |= SocketEvents_SA_READ;
            if (pollFd.revents & POLLOUT) events |= SocketEvents_SA_WRITE;
            if (pollFd.revents & POLLHUP)
                events |= SocketEvents_SA_READ | SocketEvents_SA_WRITE | SocketEvents_SA_READCLOSE | SocketEvents_SA_CLOSE;
            if (pollFd.revents & (POLLERR | POLLNVAL))
                events |= SocketEvents_SA_READ | SocketEvents_SA_WRITE | SocketEvents_SA_ERROR;
            if ((pollFd.revents & POLLIN) && (entry->armed & SocketEvents_SA_READCLOSE))
            {
                uint8_t peek;
                if (recv(entry->fd, &peek, 1, MSG_PEEK | MSG_DONTWAIT) == 0)
                    events |= SocketEvents_SA_READCLOSE;
            }
            events &= entry->armed;
            if (events)
            {
                entry->armed &= ~events;
                buffer[emitted++] = (SocketEvent){ .Data = entry->data, .Events = events };
            }
        }
        mutexUnlock(&s_socketLock);
        if (emitted) { *count = emitted; return Error_SUCCESS; }
        svcSleepThread(2000000);
    }
}

int32_t SystemNative_FcntlSetIsNonBlocking(intptr_t fd, int32_t isNonBlocking)
{
    int flags = fcntl(ToFileDescriptor(fd), F_GETFL);
    if (flags < 0) return -1;
    return fcntl(ToFileDescriptor(fd), F_SETFL, isNonBlocking ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
}

int32_t SystemNative_FcntlGetIsNonBlocking(intptr_t fd, int32_t* value)
{
    if (!value) return Error_EFAULT;
    int flags = fcntl(ToFileDescriptor(fd), F_GETFL);
    *value = flags < 0 ? 0 : (flags & O_NONBLOCK) != 0;
    return flags < 0 ? -1 : 0;
}

PALEXPORT int32_t SystemNative_LibnxGetCurrentIpv4Config(uint32_t* address, uint32_t* mask)
{
    if (!address || !mask) return Error_EFAULT;
    *address = *mask = 0;
    Result result = nifmInitialize(NifmServiceType_User);
    if (R_FAILED(result)) return Error_ENETDOWN;
    result = nifmGetCurrentIpConfigInfo(address, mask, NULL, NULL, NULL);
    nifmExit();
    if (R_FAILED(result)) { *address = *mask = 0; return Error_ENETDOWN; }
    return Error_SUCCESS;
}

int32_t SystemNative_GetNetworkInterfaces(int32_t* interfaceCount, NetworkInterfaceInfo** interfaces,
    int32_t* addressCount, IpAddressInfo** addressList)
{
    if (!interfaceCount || !interfaces || !addressCount || !addressList) { errno = EFAULT; return -1; }
    *interfaceCount = *addressCount = 0;
    *interfaces = NULL;
    *addressList = NULL;
    Result result = nifmInitialize(NifmServiceType_User);
    if (R_FAILED(result)) { errno = ENETDOWN; return -1; }
    uint32_t address = 0, mask = 0;
    result = nifmGetCurrentIpConfigInfo(&address, &mask, NULL, NULL, NULL);
    NifmInternetConnectionType type = 0;
    NifmInternetConnectionStatus status = 0;
    Result statusResult = nifmGetInternetConnectionStatus(&type, NULL, &status);
    NifmNetworkProfileData profile = {0};
    Result profileResult = nifmGetCurrentNetworkProfile(&profile);
    nifmExit();
    if (R_FAILED(result)) { errno = ENETDOWN; return -1; }
    if (!address) return 0;
    NetworkInterfaceInfo* info = calloc(1, sizeof(*info) + sizeof(IpAddressInfo));
    if (!info) { errno = ENOMEM; return -1; }
    // Public nifm gives the active network, not BSD interface indices or its
    // complete interface table. Index 0 means the default interface, and all
    // selection uses the actual address. Never invent a BSD index or MAC.
    strcpy(info->Name, "nifm-default");
    info->Speed = -1;
    info->InterfaceIndex = 0;
    info->Mtu = R_SUCCEEDED(profileResult) ? profile.ip_setting_data.mtu : 0;
    info->HardwareType = R_SUCCEEDED(statusResult) && type == NifmInternetConnectionType_WiFi
        ? NetworkInterfaceType_Wireless80211 : R_SUCCEEDED(statusResult) && type == NifmInternetConnectionType_Ethernet
        ? NetworkInterfaceType_Ethernet : NetworkInterfaceType_Unknown;
    info->OperationalState = OperationalStatus_Up;
    info->SupportsMulticast = 1;
    IpAddressInfo* ip = (IpAddressInfo*)(info + 1);
    memcpy(ip->AddressBytes, &address, 4);
    ip->NumAddressBytes = 4;
    uint32_t bits = ntohl(mask);
    while (bits & 0x80000000u) { ip->PrefixLength++; bits <<= 1; }
    if (bits != 0) { free(info); errno = EINVAL; return -1; }
    *interfaceCount = *addressCount = 1;
    *interfaces = info;
    *addressList = ip;
    return 0;
}
