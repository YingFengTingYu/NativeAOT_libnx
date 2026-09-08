// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

.syntax unified
.thumb
.text
.subsections_via_symbols

// Keep the template blocks at 16 KiB, independent of the kernel page size.
// Each 32-byte code slot corresponds to an 8-byte context/target pair.
#define BLOCK_SIZE 0x4000
#define MAP_SIZE 0x8000
#define THUNK_SIZE 32
#define THUNK_COUNT (BLOCK_SIZE / THUNK_SIZE)

.p2align 14
.globl _ThunkPool
_ThunkPool:
.rept 2
    THUNK_INDEX = 0
    .rept THUNK_COUNT
        // PC at the ADD is eight bytes after the beginning of this slot.
        movw r12, #(MAP_SIZE - THUNK_INDEX * (THUNK_SIZE - 8) - 8)
        add r12, pc
        str r12, [sp, #-4]
        bfc r12, #0, #14
        add.w r12, r12, #BLOCK_SIZE
        ldr r12, [r12, #-4]
        bx r12
        .space 8, 0
        THUNK_INDEX = THUNK_INDEX + 1
    .endr
.endr
LThunkPoolEnd:
.if (LThunkPoolEnd - _ThunkPool) != MAP_SIZE
    .error "Unexpected ARM thunk template size"
.endif

.macro HELPER Name
    .p2align 2
    .globl _\Name
    .thumb_func _\Name
_\Name:
.endm

HELPER RhpGetThunksBase
    movw r0, :lower16:(_ThunkPool - (LThunkBasePC + 4))
    movt r0, :upper16:(_ThunkPool - (LThunkBasePC + 4))
LThunkBasePC:
    add r0, pc
    bx lr

HELPER RhpGetNumThunkBlocksPerMapping
    movs r0, #2
    bx lr

HELPER RhpGetNumThunksPerBlock
    movw r0, #THUNK_COUNT
    bx lr

HELPER RhpGetThunkSize
    movs r0, #THUNK_SIZE
    bx lr

HELPER RhpGetThunkBlockSize
    movw r0, #BLOCK_SIZE
    bx lr

HELPER RhpGetThunkDataBlockAddress
    bfc r0, #0, #14
    add.w r0, r0, #MAP_SIZE
    bx lr

HELPER RhpGetThunkStubsBlockAddress
    bfc r0, #0, #14
    sub.w r0, r0, #MAP_SIZE
    bx lr
