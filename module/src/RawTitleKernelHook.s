# Adapted from Loadiine GX2 src/kernel/kernel_hooks.S (GPL-3.0-or-later).
# uFlow changes: persistent-data aliases and full volatile state preservation.
.extern RawTitleKernel_Apply
.extern RawTitleKernelProfile
.extern RawTitleKernelArmed
.global RawTitleKernelHookEntry

RawTitleKernelHookEntry:
    stwu %r1, -0x90(%r1)
    stw %r0, 0x08(%r1)
    mfcr %r0
    stw %r0, 0x0C(%r1)
    mfctr %r0
    stw %r0, 0x10(%r1)
    mfxer %r0
    stw %r0, 0x14(%r1)
    stw %r28, 0x20(%r1)
    stw %r29, 0x24(%r1)
    stw %r30, 0x28(%r1)
    stw %r31, 0x2C(%r1)
    stw %r3,  0x30(%r1)
    stw %r4,  0x34(%r1)
    stw %r5,  0x38(%r1)
    stw %r6,  0x3C(%r1)
    stw %r7,  0x40(%r1)
    stw %r8,  0x44(%r1)
    stw %r9,  0x48(%r1)
    stw %r10, 0x4C(%r1)
    stw %r11, 0x50(%r1)
    stw %r12, 0x54(%r1)
    stw %r13, 0x58(%r1)
    stw %r14, 0x5C(%r1)
    stw %r15, 0x60(%r1)
    stw %r16, 0x64(%r1)
    stw %r17, 0x68(%r1)
    stw %r18, 0x6C(%r1)
    stw %r19, 0x70(%r1)
    stw %r20, 0x74(%r1)
    stw %r21, 0x78(%r1)
    stw %r22, 0x7C(%r1)
    mflr %r31

    addi %r3, %r11, 0x68

    # Match Loadiine's mapping of resident code as data. Preserve DBAT5,
    # then mirror IBAT4 (the low WUMS code region) for the helper's data.
    mfspr %r0, 570
    stw %r0, 0x18(%r1)
    mfspr %r0, 571
    stw %r0, 0x1C(%r1)
    li %r0, 0
    mtspr 570, %r0
    mfspr %r0, 561
    mtspr 571, %r0
    mfspr %r0, 560
    mtspr 570, %r0
    sync
    isync
    lis %r4, RawTitleKernelProfile@ha
    addi %r4, %r4, RawTitleKernelProfile@l
    lis %r5, RawTitleKernelArmed@ha
    addi %r5, %r5, RawTitleKernelArmed@l
    bl RawTitleKernel_Apply
    sync
    li %r0, 0
    mtspr 570, %r0
    lwz %r0, 0x1C(%r1)
    mtspr 571, %r0
    lwz %r0, 0x18(%r1)
    mtspr 570, %r0
    eieio
    isync
    mtlr %r31

    lwz %r28, 0x20(%r1)
    lwz %r29, 0x24(%r1)
    lwz %r30, 0x28(%r1)
    lwz %r31, 0x2C(%r1)
    lwz %r3,  0x30(%r1)
    lwz %r4,  0x34(%r1)
    lwz %r5,  0x38(%r1)
    lwz %r6,  0x3C(%r1)
    lwz %r7,  0x40(%r1)
    lwz %r8,  0x44(%r1)
    lwz %r9,  0x48(%r1)
    lwz %r10, 0x4C(%r1)
    lwz %r11, 0x50(%r1)
    lwz %r12, 0x54(%r1)
    lwz %r13, 0x58(%r1)
    lwz %r14, 0x5C(%r1)
    lwz %r15, 0x60(%r1)
    lwz %r16, 0x64(%r1)
    lwz %r17, 0x68(%r1)
    lwz %r18, 0x6C(%r1)
    lwz %r19, 0x70(%r1)
    lwz %r20, 0x74(%r1)
    lwz %r21, 0x78(%r1)
    lwz %r22, 0x7C(%r1)
    lwz %r0, 0x14(%r1)
    mtxer %r0
    lwz %r0, 0x10(%r1)
    mtctr %r0
    lwz %r0, 0x0C(%r1)
    mtcr %r0
    lwz %r0, 0x08(%r1)
    addi %r1, %r1, 0x90

    clrlwi %r7, %r12, 0
    blr
