// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

.syntax unified
.thumb
.text
.globl _PAL_DispatchExceptionReturnOffset
_PAL_DispatchExceptionReturnOffset:
    .long LDispatchExceptionReturn - _PAL_DispatchExceptionWrapper

// The Mach exception handler constructs this frame; it is never entered.
.p2align 2
.globl _PAL_DispatchExceptionWrapper
.thumb_func _PAL_DispatchExceptionWrapper
_PAL_DispatchExceptionWrapper:
    .cfi_startproc
    push {r7, lr}
    .cfi_def_cfa_offset 8
    .cfi_offset r7, -8
    .cfi_offset lr, -4
    mov r7, sp
    .cfi_def_cfa_register r7
    udf #1
    bl _PAL_DispatchException
LDispatchExceptionReturn:
    udf #1
    pop {r7, pc}
    .cfi_endproc
