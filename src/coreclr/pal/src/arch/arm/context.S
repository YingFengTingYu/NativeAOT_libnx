// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

.syntax unified
.thumb
.text
#if defined(_DEBUG)
.globl _DBG_CheckStackAlignment
.thumb_func _DBG_CheckStackAlignment
_DBG_CheckStackAlignment:
    mov r12, sp
    tst r12, #7
    beq 1f
    udf #1
1:
    bx lr
#endif
