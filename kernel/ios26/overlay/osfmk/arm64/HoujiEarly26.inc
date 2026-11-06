/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Screen-only cold-boot breadcrumbs, while the MMU is still disabled.
 * Boot args PA is in x20. Only x9..x11 are clobbered; no stack or calls.
 */
.macro HOUJI26_SCREEN
#if defined(HOUJI_IOS26)
    cbz x20, 9f
    ldr x11, [x20, BA_HOUJI_VIDEO_BASE]
    cbz x11, 9f
    tst x11, #7
    b.ne 9f
    ldr x9, [x20, BA_HOUJI_VIDEO_DEPTH]
    and x9, x9, #0xff
    cmp x9, #32
    b.ne 9f
    ldr x9, [x20, BA_HOUJI_VIDEO_ROW]
    cbz x9, 9f
    cmp x9, #16384
    b.hi 9f
    ldr x10, [x20, BA_HOUJI_VIDEO_HEIGHT]
    cbz x10, 9f
    cmp x10, #16384
    b.hi 9f
    mul x9, x9, x10
    lsr x9, x9, #4
    bic x9, x9, #7
    cbz x9, 9f
    adds x10, x11, x9
    b.cs 9f
    mov w10, #$0
    movk w10, #$1, lsl #16
8:
    stp w10, w10, [x11], #8
    subs x9, x9, #8
    b.hi 8b
    dsb sy
9:
#endif
.endmacro
