// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

// Use the actual NativeAOT runtime macro, not a copy in the test.
#include <unixasmmacros.inc>

.text
.p2align 2
LEAF_ENTRY nx_runtime_tls_address, .text
    INLINE_GET_TLS_VAR x9, tls_probe_value
    mov x0, x9
    ret
LEAF_END nx_runtime_tls_address, .text

.p2align 2
LEAF_ENTRY nx_runtime_tls_preserve_args, .text
    INLINE_GET_TLS_VAR x9, tls_probe_value
    cmp x0, #1
    b.ne LOCAL_LABEL(args_failed)
    cmp x1, #2
    b.ne LOCAL_LABEL(args_failed)
    cmp x2, #3
    b.ne LOCAL_LABEL(args_failed)
    cmp x3, #4
    b.ne LOCAL_LABEL(args_failed)
    cmp x4, #5
    b.ne LOCAL_LABEL(args_failed)
    cmp x5, #6
    b.ne LOCAL_LABEL(args_failed)
    cmp x6, #7
    b.ne LOCAL_LABEL(args_failed)
    cmp x7, #8
    b.ne LOCAL_LABEL(args_failed)
    mov x0, #1
    ret
LOCAL_LABEL(args_failed):
    mov x0, #0
    ret
LEAF_END nx_runtime_tls_preserve_args, .text

.section .note.GNU-stack,"",%progbits
