// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <AsmMacroLib.h>

ASM_FUNC(DarwinCurrentEl)
  mrs x0, CurrentEL
  ret

.macro DarwinPaint lo, hi
  cbz x13, 9f
  mov x9, x14
  cbz x9, 9f
  mov w10, #\lo
  movk w10, #\hi, lsl #16
  mov x11, x13
8:
  stp w10, w10, [x11], #8
  subs x9, x9, #8
  b.hi 8b
  dsb sy
9:
.endm

// x0=boot_args PA, x1=entry PA, x2=owned PA, x3=owned size, x4=frame buffer
// PA (0 for none), x5=its size in bytes. EL1 only.
ASM_FUNC(DarwinEnter)
  msr daifset, #15
  mov x12, x6 // per-boot kernel key block, installed after firmware exit
  // Registers x0..x3 and the key pointer survive every screen-only marker.
  mov x13, x4
  lsr x14, x5, #4
  bic x14, x14, #7
  DarwinPaint 0xFF00, 0xFF00 // green: post-EBS assembly entered
  add x5, x2, x3
  mrs x6, ctr_el0
  ubfx x6, x6, #16, #4
  mov x7, #4
  lsl x6, x7, x6
  sub x7, x6, #1
  bic x4, x2, x7
  dsb sy
1:
  dc civac, x4
  add x4, x4, x6
  cmp x4, x5
  b.lo 1b
  dsb sy
  ic iallu
  dsb sy
  isb
  DarwinPaint 0xFF00, 0xFFFF // yellow: whole-window cache maintenance finished

  // UEFI used its own keys through ExitBootServices. Install XNU's keys now.
  cbz x12, 4f
  .arch_extension pauth
  ldp x6, x7, [x12, #0]
  msr APIAKeyLo_EL1, x6
  msr APIAKeyHi_EL1, x7
  ldp x6, x7, [x12, #16]
  msr APIBKeyLo_EL1, x6
  msr APIBKeyHi_EL1, x7
  ldp x6, x7, [x12, #32]
  msr APDAKeyLo_EL1, x6
  msr APDAKeyHi_EL1, x7
  ldp x6, x7, [x12, #48]
  msr APDBKeyLo_EL1, x6
  msr APDBKeyHi_EL1, x7
  ldp x6, x7, [x12, #64]
  msr APGAKeyLo_EL1, x6
  msr APGAKeyHi_EL1, x7
  isb
4:
  mrs x4, sctlr_el1
  bic x4, x4, #1
  bic x4, x4, #4
  bic x4, x4, #0x1000
  msr sctlr_el1, x4
  isb
  tlbi vmalle1
  dsb sy
  isb
  DarwinPaint 0xFFFF, 0xFF00 // cyan: XNU keys installed, MMU off, about to branch
  br x1

// PSCI CPU_ON enters with MMU off and x0 = the real XNU reset-vector PA.
// This code and its adjacent pointer are copied into reserved kernel-owned RAM.
// PC-relative ADR stays valid after copying; there are no external relocations.
ASM_FUNC_ALIGN(DarwinSecondaryPacStart, 16)
  msr daifset, #15
  adr x9, ASM_PFX(DarwinSecondaryPacKeysAddress)
  ldr x9, [x9]
  .arch_extension pauth
  ldp x10, x11, [x9, #0]
  msr APIAKeyLo_EL1, x10
  msr APIAKeyHi_EL1, x11
  ldp x10, x11, [x9, #16]
  msr APIBKeyLo_EL1, x10
  msr APIBKeyHi_EL1, x11
  ldp x10, x11, [x9, #32]
  msr APDAKeyLo_EL1, x10
  msr APDAKeyHi_EL1, x11
  ldp x10, x11, [x9, #48]
  msr APDBKeyLo_EL1, x10
  msr APDBKeyHi_EL1, x11
  ldp x10, x11, [x9, #64]
  msr APGAKeyLo_EL1, x10
  msr APGAKeyHi_EL1, x11
  mrs x10, cpacr_el1
  mov x11, #0x300000
  orr x10, x10, x11
  msr cpacr_el1, x10
  isb
  br x0
.balign 8
// These are labels inside the copied function, not separate functions.
// ASM_FUNC would create separate PE/COFF sections that the linker can reorder.
.global ASM_PFX(DarwinSecondaryPacKeysAddress)
ASM_PFX(DarwinSecondaryPacKeysAddress):
  .quad 0
.global ASM_PFX(DarwinSecondaryPacEnd)
ASM_PFX(DarwinSecondaryPacEnd):
