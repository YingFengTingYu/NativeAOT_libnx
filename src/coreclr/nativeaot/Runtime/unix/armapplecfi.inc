// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

// Register sets used by the shared ARM helpers. Fail closed when a new set
// is introduced so its DWARF save/restore description cannot silently vanish.
.macro ARM_CFI_MASK Registers
    __cfi_mask = 0
    .ifc "\Registers","{d0-d3}"
        __cfi_mask = 0xf
    .endif
    .ifc "\Registers","{d0-d7}"
        __cfi_mask = 0xff
    .endif
    .ifc "\Registers","{d8-d15}"
        __cfi_mask = 0xff00
    .endif
    .ifc "\Registers","{r0,lr}"
        __cfi_mask = 0x4001
    .endif
    .ifc "\Registers","{r0,r1}"
        __cfi_mask = 0x3
    .endif
    .ifc "\Registers","{r0,r2-r11,lr}"
        __cfi_mask = 0x4ffd
    .endif
    .ifc "\Registers","{r0,r4-r11,lr}"
        __cfi_mask = 0x4ff1
    .endif
    .ifc "\Registers","{r0-r2}"
        __cfi_mask = 0x7
    .endif
    .ifc "\Registers","{r0-r3}"
        __cfi_mask = 0xf
    .endif
    .ifc "\Registers","{r0-r4, lr}"
        __cfi_mask = 0x401f
    .endif
    .ifc "\Registers","{r1,r4-r11,lr}"
        __cfi_mask = 0x4ff2
    .endif
    .ifc "\Registers","{r1,r4-r11,pc}"
        __cfi_mask = 0x8ff2
    .endif
    .ifc "\Registers","{r11,lr}"
        __cfi_mask = 0x4800
    .endif
    .ifc "\Registers","{r2,r4-r11,lr}"
        __cfi_mask = 0x4ff4
    .endif
    .ifc "\Registers","{r2,r4-r11,pc}"
        __cfi_mask = 0x8ff4
    .endif
    .ifc "\Registers","{r3}"
        __cfi_mask = 0x8
    .endif
    .ifc "\Registers","{r4,lr}"
        __cfi_mask = 0x4010
    .endif
    .ifc "\Registers","{r4,pc}"
        __cfi_mask = 0x8010
    .endif
    .ifc "\Registers","{r4-r10}"
        __cfi_mask = 0x7f0
    .endif
    .ifc "\Registers","{r4-r11, lr}"
        __cfi_mask = 0x4ff0
    .endif
    .ifc "\Registers","{r4-r6,lr}"
        __cfi_mask = 0x4070
    .endif
    .ifc "\Registers","{r4-r6,pc}"
        __cfi_mask = 0x8070
    .endif
    .ifc "\Registers","{r5,lr}"
        __cfi_mask = 0x4020
    .endif
    .ifc "\Registers","{r5,pc}"
        __cfi_mask = 0x8020
    .endif
    .ifc "\Registers","{r7}"
        __cfi_mask = 0x80
    .endif
    .if __cfi_mask == 0
        .error "Unsupported ARM unwind register set"
    .endif
.endm

.macro ARM_CFI_REGS Registers, Size, DwarfBase, Restore=0
    ARM_CFI_MASK "\Registers"
    __cfi_count = 0
    __cfi_reg = 0
    .rept 32
        .if __cfi_mask & (1 << __cfi_reg)
            __cfi_count = __cfi_count + 1
        .endif
        __cfi_reg = __cfi_reg + 1
    .endr
    .if \Restore == 0
        .cfi_adjust_cfa_offset (__cfi_count * \Size)
    .endif
    __cfi_reg = 0
    __cfi_slot = 0
    .rept 32
        .if __cfi_mask & (1 << __cfi_reg)
            .if \Restore
                .cfi_restore (__cfi_reg + \DwarfBase)
            .else
                .cfi_rel_offset (__cfi_reg + \DwarfBase), (__cfi_slot * \Size)
            .endif
            __cfi_slot = __cfi_slot + 1
        .endif
        __cfi_reg = __cfi_reg + 1
    .endr
    .if \Restore
        .cfi_adjust_cfa_offset -(__cfi_count * \Size)
    .endif
.endm
