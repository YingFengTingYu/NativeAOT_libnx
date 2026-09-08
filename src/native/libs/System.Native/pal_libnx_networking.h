// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#pragma once
#include "pal_networking.h"

int LibnxCloseDescriptor(int fd);
ssize_t LibnxSocketReceive(int fd, void* data, size_t count, int flags);
ssize_t LibnxSocketReceiveMessage(int fd, struct msghdr* message, int flags);
ssize_t LibnxSocketSend(int fd, const void* data, size_t count, int flags);
ssize_t LibnxSocketSendMessage(int fd, const struct msghdr* message, int flags);
int LibnxSocketAccept(int fd, struct sockaddr* address, socklen_t* length);
int LibnxSocketConnect(int fd, const struct sockaddr* address, socklen_t length);
int LibnxSocketGetName(int fd, struct sockaddr* address, socklen_t* length);
int LibnxSocketGetPeerName(int fd, struct sockaddr* address, socklen_t* length);
int32_t LibnxConvertPendingSocketError(int error);
PALEXPORT void SystemNative_LibnxQuiesceSocketEvents(void);
PALEXPORT int32_t SystemNative_LibnxGetCurrentIpv4Config(uint32_t* address, uint32_t* mask);
int32_t LibnxCreateSocketEventPort(int32_t* port);
int32_t LibnxCloseSocketEventPort(int32_t port);
int32_t LibnxChangeSocketEvents(int32_t port, int32_t fd, SocketEvents currentEvents, SocketEvents newEvents, uintptr_t data);
int32_t LibnxWaitForSocketEvents(int32_t port, SocketEvent* buffer, int32_t* count);
