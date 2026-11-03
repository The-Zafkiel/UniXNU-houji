// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <AsmMacroLib.h>

ASM_FUNC(DarwinCurrentEl)
  mrs x0, CurrentEL
  ret

// x0=boot_args PA, x1=entry PA, x2=owned PA, x3=owned size, x4=frame buffer
// PA (0 for none), x5=its size in bytes. EL1 only.
ASM_FUNC(DarwinEnter)
  msr daifset, #15
  mov x12, x6 // per-boot kernel key block, installed after firmware exit
  // Breadcrumb: a green bar (BGRA 0xFF00FF00) over the top 1/16 of the frame
  // buffer shows that this code ran, whatever the kernel does afterwards.
  cbz x4, 2f
  lsr x9, x5, #4
  bic x9, x9, #7
  mov w10, #0xFF00
  movk w10, #0xFF00, lsl #16
  mov x11, x4
3:
  stp w10, w10, [x11], #8
  subs x9, x9, #8
  b.hi 3b
  dsb sy
2:
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
  br x1

// PSCI CPU_ON enters with MMU off and x0 = the real XNU reset-vector PA.
// This code and its adjacent pointer are copied into reserved kernel-owned RAM.
// PC-relative ADR stays valid after copying; there are no external relocations.
.balign 16
ASM_FUNC(DarwinSecondaryPacStart)
  msr daifset, #15
  adr x9, DarwinSecondaryPacKeysAddress
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
ASM_FUNC(DarwinSecondaryPacKeysAddress)
  .quad 0
ASM_FUNC(DarwinSecondaryPacEnd)
