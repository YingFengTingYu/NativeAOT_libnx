// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

// These helpers are called without an ordinary C call boundary. Preserve every
// argument and scratch register except r0, which carries the result.
.syntax unified
.thumb
.text
.subsections_via_symbols

.macro PTHREAD_TLS_HELPER Name, Implementation
    .p2align 2
    .globl _\Name
    .thumb_func _\Name
_\Name:
    .cfi_startproc
    push {r1-r3, r9, r12, lr}
    .cfi_def_cfa_offset 24
    .cfi_offset r9, -12
    .cfi_offset lr, -4
    vpush {d0-d7}
    .cfi_def_cfa_offset 88
    vpush {d16-d31}
    .cfi_def_cfa_offset 216
    bl _\Implementation
    vpop {d16-d31}
    .cfi_def_cfa_offset 88
    vpop {d0-d7}
    .cfi_def_cfa_offset 24
    pop {r1-r3, r9, r12, pc}
    .cfi_endproc
.endm

PTHREAD_TLS_HELPER RhpGetThread, RhpGetThreadPthread
PTHREAD_TLS_HELPER RhpGetThunkData, RhpGetThunkDataPthread
