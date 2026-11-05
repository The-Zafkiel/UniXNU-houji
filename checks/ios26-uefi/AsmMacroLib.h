/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Section-generating EDK2 ASM_FUNC semantics for the host COFF layout check.
 * No firmware code or runtime is provided by this peer.
 */
#define ASM_PFX(Name) Name
#define _ASM_FUNC_ALIGN(Name, Section, Align) \
  .global Name; .section #Section, "ax"; .balign Align; Name:
#define ASM_FUNC(Name) _ASM_FUNC_ALIGN(ASM_PFX(Name), .text. ## Name, 4)
#define ASM_FUNC_ALIGN(Name, Align) _ASM_FUNC_ALIGN(ASM_PFX(Name), .text. ## Name, Align)
