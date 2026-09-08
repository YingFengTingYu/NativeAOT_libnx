// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include <unixasmmacros.inc>

// Existing TLS access sites assume that lookup preserves their live registers.
// pthread_getspecific and first-use allocation use the ordinary C ABI, so bridge
// that ABI here and preserve every other caller-visible register, including SIMD.
.macro PTHREAD_TLS_HELPER Name, Implementation
    NESTED_ENTRY \Name, _TEXT, NoHandler
    stp fp, lr, [sp, #-16]!
    .cfi_def_cfa_offset 16
    .cfi_offset fp, -16
    .cfi_offset lr, -8
    mov fp, sp
    .cfi_def_cfa_register fp
    sub sp, sp, #656
    stp x1, x2, [sp, #0]
    .cfi_offset x1, -672
    .cfi_offset x2, -664
    stp x3, x4, [sp, #16]
    .cfi_offset x3, -656
    .cfi_offset x4, -648
    stp x5, x6, [sp, #32]
    .cfi_offset x5, -640
    .cfi_offset x6, -632
    stp x7, x8, [sp, #48]
    .cfi_offset x7, -624
    .cfi_offset x8, -616
    stp x9, x10, [sp, #64]
    .cfi_offset x9, -608
    .cfi_offset x10, -600
    stp x11, x12, [sp, #80]
    .cfi_offset x11, -592
    .cfi_offset x12, -584
    stp x13, x14, [sp, #96]
    .cfi_offset x13, -576
    .cfi_offset x14, -568
    stp x15, x16, [sp, #112]
    .cfi_offset x15, -560
    .cfi_offset x16, -552
    str x17, [sp, #128]
    .cfi_offset x17, -544
    stp q0, q1, [sp, #144]
    stp q2, q3, [sp, #176]
    stp q4, q5, [sp, #208]
    stp q6, q7, [sp, #240]
    stp q8, q9, [sp, #272]
    stp q10, q11, [sp, #304]
    stp q12, q13, [sp, #336]
    stp q14, q15, [sp, #368]
    stp q16, q17, [sp, #400]
    stp q18, q19, [sp, #432]
    stp q20, q21, [sp, #464]
    stp q22, q23, [sp, #496]
    stp q24, q25, [sp, #528]
    stp q26, q27, [sp, #560]
    stp q28, q29, [sp, #592]
    stp q30, q31, [sp, #624]

    bl C_FUNC(\Implementation)

    ldp q0, q1, [sp, #144]
    ldp q2, q3, [sp, #176]
    ldp q4, q5, [sp, #208]
    ldp q6, q7, [sp, #240]
    ldp q8, q9, [sp, #272]
    ldp q10, q11, [sp, #304]
    ldp q12, q13, [sp, #336]
    ldp q14, q15, [sp, #368]
    ldp q16, q17, [sp, #400]
    ldp q18, q19, [sp, #432]
    ldp q20, q21, [sp, #464]
    ldp q22, q23, [sp, #496]
    ldp q24, q25, [sp, #528]
    ldp q26, q27, [sp, #560]
    ldp q28, q29, [sp, #592]
    ldp q30, q31, [sp, #624]
    ldp x1, x2, [sp, #0]
    .cfi_restore x1
    .cfi_restore x2
    ldp x3, x4, [sp, #16]
    .cfi_restore x3
    .cfi_restore x4
    ldp x5, x6, [sp, #32]
    .cfi_restore x5
    .cfi_restore x6
    ldp x7, x8, [sp, #48]
    .cfi_restore x7
    .cfi_restore x8
    ldp x9, x10, [sp, #64]
    .cfi_restore x9
    .cfi_restore x10
    ldp x11, x12, [sp, #80]
    .cfi_restore x11
    .cfi_restore x12
    ldp x13, x14, [sp, #96]
    .cfi_restore x13
    .cfi_restore x14
    ldp x15, x16, [sp, #112]
    .cfi_restore x15
    .cfi_restore x16
    ldr x17, [sp, #128]
    .cfi_restore x17
    add sp, sp, #656
    ldp fp, lr, [sp], #16
    .cfi_def_cfa sp, 0
    .cfi_restore fp
    .cfi_restore lr
    ret
    NESTED_END \Name, _TEXT
.endm

PTHREAD_TLS_HELPER RhpGetThread, RhpGetThreadPthread
PTHREAD_TLS_HELPER RhpGetThunkData, RhpGetThunkDataPthread
