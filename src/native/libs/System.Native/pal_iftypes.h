// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#pragma once

#if HAVE_NET_IF_TYPES_H
#include <net/if_types.h>
#elif defined(__APPLE__)
// Older iPhoneOS SDKs omit if_types.h. These are the Darwin interface-type
// numbers used by sockaddr_dl, including Apple's GIF/STF assignments.
// See apple-oss-distributions/xnu, tag xnu-2422.1.72, bsd/net/if_types.h.
#define IFT_ETHER       0x06
#define IFT_ISO88025    0x09
#define IFT_FDDI        0x0f
#define IFT_ISDNBASIC   0x14
#define IFT_ISDNPRIMARY 0x15
#define IFT_PPP         0x17
#define IFT_LOOP        0x18
#define IFT_XETHER      0x1a
#define IFT_SLIP        0x1c
#define IFT_ATM         0x25
#define IFT_MODEM       0x30
#define IFT_GIF         0x37
#define IFT_STF         0x39
#define IFT_IEEE1394    0x90
#else
#error AF_LINK requires interface-type definitions for this platform.
#endif
