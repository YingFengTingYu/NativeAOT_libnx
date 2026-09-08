// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include <unixasmmacros.inc>

// Capture registers that are live across a lookup, including integer arguments,
// structure-return x8, scratch registers and complete SIMD register contents.
NESTED_ENTRY CaptureTlsRegisters, _TEXT, NoHandler
    PROLOG_SAVE_REG_PAIR_INDEXED fp, lr, -160
    PROLOG_SAVE_REG_PAIR x19, x20, 16
    stp q8, q9, [sp, #32]
    stp q10, q11, [sp, #64]
    stp q12, q13, [sp, #96]
    stp q14, q15, [sp, #128]
    mov x20, x0
    mov x19, x1
    mov x1, #513
    mov x2, #514
    mov x3, #515
    mov x4, #516
    mov x5, #517
    mov x6, #518
    mov x7, #519
    mov x8, #520
    mov x9, #521
    mov x10, #522
    mov x11, #523
    mov x12, #524
    mov x13, #525
    mov x14, #526
    mov x15, #527
    mov x16, #528
    mov x17, #529
    movi v0.16b, #64
    movi v1.16b, #65
    movi v2.16b, #66
    movi v3.16b, #67
    movi v4.16b, #68
    movi v5.16b, #69
    movi v6.16b, #70
    movi v7.16b, #71
    movi v8.16b, #72
    movi v9.16b, #73
    movi v10.16b, #74
    movi v11.16b, #75
    movi v12.16b, #76
    movi v13.16b, #77
    movi v14.16b, #78
    movi v15.16b, #79
    movi v16.16b, #80
    movi v17.16b, #81
    movi v18.16b, #82
    movi v19.16b, #83
    movi v20.16b, #84
    movi v21.16b, #85
    movi v22.16b, #86
    movi v23.16b, #87
    movi v24.16b, #88
    movi v25.16b, #89
    movi v26.16b, #90
    movi v27.16b, #91
    movi v28.16b, #92
    movi v29.16b, #93
    movi v30.16b, #94
    movi v31.16b, #95
    cbnz x20, 1f
    bl C_FUNC(RhpGetThread)
    b 2f
1:
    bl C_FUNC(RhpGetThunkData)
2:
    stp x1, x2, [x19, #0]
    stp x3, x4, [x19, #16]
    stp x5, x6, [x19, #32]
    stp x7, x8, [x19, #48]
    stp x9, x10, [x19, #64]
    stp x11, x12, [x19, #80]
    stp x13, x14, [x19, #96]
    stp x15, x16, [x19, #112]
    str x17, [x19, #128]
    stp q0, q1, [x19, #144]
    stp q2, q3, [x19, #176]
    stp q4, q5, [x19, #208]
    stp q6, q7, [x19, #240]
    stp q8, q9, [x19, #272]
    stp q10, q11, [x19, #304]
    stp q12, q13, [x19, #336]
    stp q14, q15, [x19, #368]
    stp q16, q17, [x19, #400]
    stp q18, q19, [x19, #432]
    stp q20, q21, [x19, #464]
    stp q22, q23, [x19, #496]
    stp q24, q25, [x19, #528]
    stp q26, q27, [x19, #560]
    stp q28, q29, [x19, #592]
    stp q30, q31, [x19, #624]
    ldp q8, q9, [sp, #32]
    ldp q10, q11, [sp, #64]
    ldp q12, q13, [sp, #96]
    ldp q14, q15, [sp, #128]
    EPILOG_RESTORE_REG_PAIR x19, x20, 16
    EPILOG_RESTORE_REG_PAIR_INDEXED fp, lr, 160
    ret
NESTED_END CaptureTlsRegisters, _TEXT

// Deliberately destroy registers to verify the production bridge, independently
// of the incidental register usage of one pthread/libc implementation.
.macro CLOBBER_LOOKUP Name, Result
    LEAF_ENTRY \Name, _TEXT
    mov x1, #2817
    mov x2, #2818
    mov x3, #2819
    mov x4, #2820
    mov x5, #2821
    mov x6, #2822
    mov x7, #2823
    mov x8, #2824
    mov x9, #2825
    mov x10, #2826
    mov x11, #2827
    mov x12, #2828
    mov x13, #2829
    mov x14, #2830
    mov x15, #2831
    mov x16, #2832
    mov x17, #2833
    movi v0.16b, #0xcc
    movi v1.16b, #0xcc
    movi v2.16b, #0xcc
    movi v3.16b, #0xcc
    movi v4.16b, #0xcc
    movi v5.16b, #0xcc
    movi v6.16b, #0xcc
    movi v7.16b, #0xcc
    movi v8.16b, #0xcc
    movi v9.16b, #0xcc
    movi v10.16b, #0xcc
    movi v11.16b, #0xcc
    movi v12.16b, #0xcc
    movi v13.16b, #0xcc
    movi v14.16b, #0xcc
    movi v15.16b, #0xcc
    movi v16.16b, #0xcc
    movi v17.16b, #0xcc
    movi v18.16b, #0xcc
    movi v19.16b, #0xcc
    movi v20.16b, #0xcc
    movi v21.16b, #0xcc
    movi v22.16b, #0xcc
    movi v23.16b, #0xcc
    movi v24.16b, #0xcc
    movi v25.16b, #0xcc
    movi v26.16b, #0xcc
    movi v27.16b, #0xcc
    movi v28.16b, #0xcc
    movi v29.16b, #0xcc
    movi v30.16b, #0xcc
    movi v31.16b, #0xcc
    mov x0, \Result
    ret
    LEAF_END \Name, _TEXT
.endm

CLOBBER_LOOKUP RhpGetThreadPthread, 0x1234
CLOBBER_LOOKUP RhpGetThunkDataPthread, 0x5678
