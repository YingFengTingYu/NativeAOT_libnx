// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
#include <unixasmmacros.inc>
#define D_OFFSET (OFFSETOF__PAL_LIMITED_CONTEXT__IP + 8)

.text
.p2align 2
LEAF_ENTRY LibnxRestoreExceptionContext, .text
    mov x9, x0
    ldp x29, x30, [x9, #OFFSETOF__PAL_LIMITED_CONTEXT__FP]
    ldp x19, x20, [x9, #OFFSETOF__PAL_LIMITED_CONTEXT__X19]
    ldp x21, x22, [x9, #OFFSETOF__PAL_LIMITED_CONTEXT__X21]
    ldp x23, x24, [x9, #OFFSETOF__PAL_LIMITED_CONTEXT__X23]
    ldp x25, x26, [x9, #OFFSETOF__PAL_LIMITED_CONTEXT__X25]
    ldp x27, x28, [x9, #OFFSETOF__PAL_LIMITED_CONTEXT__X27]
    ldp d8, d9, [x9, #D_OFFSET]
    ldp d10, d11, [x9, #(D_OFFSET + 16)]
    ldp d12, d13, [x9, #(D_OFFSET + 32)]
    ldp d14, d15, [x9, #(D_OFFSET + 48)]
    ldr x16, [x9, #OFFSETOF__PAL_LIMITED_CONTEXT__IP]
    ldr x17, [x9, #OFFSETOF__PAL_LIMITED_CONTEXT__SP]
    ldp x0, x1, [x9, #OFFSETOF__PAL_LIMITED_CONTEXT__X0]
    mov sp, x17
    br x16
LEAF_END LibnxRestoreExceptionContext, .text

.section .note.GNU-stack,"",%progbits
