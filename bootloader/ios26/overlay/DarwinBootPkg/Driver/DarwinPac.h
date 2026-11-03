/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef DARWIN_PAC_H
#define DARWIN_PAC_H
#include <Protocol/Rng.h>
#include "DarwinPacFixups.h"

extern UINT8 DarwinSecondaryPacStart[], DarwinSecondaryPacEnd[], DarwinSecondaryPacKeysAddress[];

STATIC BOOLEAN DarwinPacEntropy(UINT64 Keys[10]) {
  EFI_RNG_PROTOCOL *Rng = NULL;
  EFI_STATUS Status = gBS->LocateProtocol(&gEfiRngProtocolGuid, NULL, (VOID **)&Rng);
  if (!EFI_ERROR(Status) && Rng && !EFI_ERROR(Rng->GetRNG(Rng, NULL, 80, (UINT8 *)Keys)))
    return TRUE;
  /* Architectural RNDR, with the same feature test and NZCV convention as
   * EDK2 BaseRngLib/AArch64/ArmRng.S. No timer-based key fallback. */
  if ((READ_SYSREG(id_aa64isar0_el1) >> 60) == 0) return FALSE;
  for (UINTN Index = 0; Index < 10; ++Index) {
    UINT64 Value = 0;
    UINT32 Ready = 0;
    for (UINTN Attempt = 0; Attempt < 100 && !Ready; ++Attempt) {
      __asm__ volatile("mrs %0, S3_3_C2_C4_0\n\tcset %w1, ne"
        : "=r"(Value), "=r"(Ready) : : "cc");
      if (!Ready) CpuPause();
    }
    if (!Ready) { ZeroMem(Keys, 80); return FALSE; }
    Keys[Index] = Value;
  }
  return TRUE;
}

STATIC VOID DarwinPacReadKeys(UINT64 Keys[10]) {
  __asm__ volatile(
    ".arch_extension pauth\n\t"
    "mrs %0, APIAKeyLo_EL1\n\tmrs %1, APIAKeyHi_EL1\n\t"
    "mrs %2, APIBKeyLo_EL1\n\tmrs %3, APIBKeyHi_EL1\n\t"
    "mrs %4, APDAKeyLo_EL1\n\tmrs %5, APDAKeyHi_EL1\n\t"
    "mrs %6, APDBKeyLo_EL1\n\tmrs %7, APDBKeyHi_EL1\n\t"
    "mrs %8, APGAKeyLo_EL1\n\tmrs %9, APGAKeyHi_EL1"
    : "=r"(Keys[0]), "=r"(Keys[1]), "=r"(Keys[2]), "=r"(Keys[3]), "=r"(Keys[4]),
      "=r"(Keys[5]), "=r"(Keys[6]), "=r"(Keys[7]), "=r"(Keys[8]), "=r"(Keys[9]));
}
STATIC VOID DarwinPacWriteKeys(CONST UINT64 Keys[10]) {
  __asm__ volatile(
    ".arch_extension pauth\n\t"
    "msr APIAKeyLo_EL1, %0\n\tmsr APIAKeyHi_EL1, %1\n\t"
    "msr APIBKeyLo_EL1, %2\n\tmsr APIBKeyHi_EL1, %3\n\t"
    "msr APDAKeyLo_EL1, %4\n\tmsr APDAKeyHi_EL1, %5\n\t"
    "msr APDBKeyLo_EL1, %6\n\tmsr APDBKeyHi_EL1, %7\n\t"
    "msr APGAKeyLo_EL1, %8\n\tmsr APGAKeyHi_EL1, %9\n\tisb"
    : : "r"(Keys[0]), "r"(Keys[1]), "r"(Keys[2]), "r"(Keys[3]), "r"(Keys[4]),
      "r"(Keys[5]), "r"(Keys[6]), "r"(Keys[7]), "r"(Keys[8]), "r"(Keys[9]) : "memory");
}
STATIC int DarwinPacSign(void *Cookie, DpaU64 Target, DpaU64 Modifier, DpaU32 Key, DpaU64 *Result) {
  (VOID)Cookie;
  switch (Key) {
    case 0: __asm__ volatile(".arch_extension pauth\n\tpacia %0, %1" : "+r"(Target) : "r"(Modifier)); break;
    case 1: __asm__ volatile(".arch_extension pauth\n\tpacib %0, %1" : "+r"(Target) : "r"(Modifier)); break;
    case 2: __asm__ volatile(".arch_extension pauth\n\tpacda %0, %1" : "+r"(Target) : "r"(Modifier)); break;
    case 3: __asm__ volatile(".arch_extension pauth\n\tpacdb %0, %1" : "+r"(Target) : "r"(Modifier)); break;
    default: return 1;
  }
  *Result = Target;
  return 0;
}

STATIC EFI_STATUS DarwinPreparePac(CONST VOID *File, UINTN FileSize, UINT64 VirtualBase,
    UINT64 PhysicalBase, UINT64 WindowBytes, UINT64 *KeysPhysical) {
  DpaReport Report;
  int Code = DpaProcess(File, FileSize, VirtualBase, WindowBytes, NULL, NULL, NULL, &Report);
  if (Code != DPA_OK) {
    Print(L"HOUJI26 PAC: kernel rejected, code=%d\n", Code);
    return EFI_COMPROMISED_DATA;
  }
  UINT64 Isar1 = READ_SYSREG(id_aa64isar1_el1);
  UINT64 Isar2 = READ_SYSREG(id_aa64isar2_el1);
  BOOLEAN AddressPac = ((Isar1 >> 4) & 15) || ((Isar1 >> 8) & 15) || ((Isar2 >> 12) & 15);
  BOOLEAN GenericPac = ((Isar1 >> 24) & 15) || ((Isar1 >> 28) & 15) || ((Isar2 >> 8) & 15);
  if (!AddressPac || !GenericPac) return EFI_UNSUPPORTED;
  UINT64 Keys[10], Saved[10], ReadBack[10];
  ZeroMem(Keys, sizeof(Keys));
  if (!DarwinPacEntropy(Keys)) return EFI_NOT_READY;
  *KeysPhysical = PhysicalBase + Report.KeysOffset;
  CopyMem((VOID *)(UINTN)*KeysPhysical, Keys, sizeof(Keys));
  UINT64 Daif = READ_SYSREG(daif);
  __asm__ volatile("msr daifset, #15\n\tisb" : : : "memory");
  UINT64 Tcr = READ_SYSREG(tcr_el1), Sctlr = READ_SYSREG(sctlr_el1);
  DarwinPacReadKeys(Saved);
  DarwinPacWriteKeys(Keys);
  UINT64 SignTcr = (Tcr & ~DPA_TCR_MASK) | Report.TcrBits;
  UINT64 SignSctlr = Sctlr | (1ULL << 31) | (1ULL << 30) | (1ULL << 27) | (1ULL << 13);
  __asm__ volatile("msr tcr_el1, %0\n\tmsr sctlr_el1, %1\n\tisb" : : "r"(SignTcr), "r"(SignSctlr) : "memory");
  /* Some hypervisors ignore key writes at an unqualified VA. Verify them
   * before mutating any fixup. Do not authenticate a speculative pointer here:
   * a failed AUT instruction may trap rather than return an error on FEAT_FPAC. */
  DarwinPacReadKeys(ReadBack);
  BOOLEAN Programmed = CompareMem(Keys, ReadBack, sizeof(Keys)) == 0 &&
    (READ_SYSREG(tcr_el1) & DPA_TCR_MASK) == Report.TcrBits &&
    (READ_SYSREG(sctlr_el1) & ((1ULL << 31) | (1ULL << 30) | (1ULL << 27) | (1ULL << 13))) ==
      ((1ULL << 31) | (1ULL << 30) | (1ULL << 27) | (1ULL << 13));
  if (Programmed) {
    /* Check that all address-key instructions actually sign. A PAC may
     * coincidentally be zero; use three independent modifiers before failing. */
    for (UINT32 Key = 0; Key < 4 && Programmed; ++Key) {
      BOOLEAN Changed = FALSE;
      for (UINT64 Modifier = 1; Modifier <= 3; ++Modifier) {
        DpaU64 Signed = VirtualBase;
        if (DarwinPacSign(NULL, VirtualBase, Modifier, Key, &Signed)) break;
        if (Signed != VirtualBase) Changed = TRUE;
      }
      Programmed = Changed;
    }
  }
  Code = Programmed ? DpaProcess(File, FileSize, VirtualBase, WindowBytes,
    (DpaU8 *)(UINTN)PhysicalBase, DarwinPacSign, NULL, &Report) : DPA_CONFIG;
  /* All firmware callbacks execute with the firmware's keys and controls.
   * Entry.S installs the kernel's keys after ExitBootServices, and the PSCI
   * trampoline installs them on every secondary core before entering XNU. */
  DarwinPacWriteKeys(Saved);
  __asm__ volatile("msr tcr_el1, %0\n\tmsr sctlr_el1, %1\n\tisb\n\tmsr daif, %2"
    : : "r"(Tcr), "r"(Sctlr), "r"(Daif) : "memory");
  ZeroMem(Keys, sizeof(Keys)); ZeroMem(Saved, sizeof(Saved)); ZeroMem(ReadBack, sizeof(ReadBack));
  if (!Programmed) {
    ZeroMem((VOID *)(UINTN)*KeysPhysical, 80);
    *KeysPhysical = 0;
    Print(L"HOUJI26 PAC: hardware key/control/signing verification failed\n");
    return EFI_SECURITY_VIOLATION;
  }
  if (Code != DPA_OK) return EFI_COMPROMISED_DATA;
  Print(L"HOUJI26 PAC: authenticated=%u rebased=%u TCR-geometry=%lx; keys withheld\n",
    Report.Authenticated, Report.Rebased, Report.TcrBits);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS DarwinPrepareSecondaryPac(UINT64 Destination, UINT64 KeysPhysical) {
  UINTN Bytes = (UINTN)DarwinSecondaryPacEnd - (UINTN)DarwinSecondaryPacStart;
  UINTN KeysOffset = (UINTN)DarwinSecondaryPacKeysAddress - (UINTN)DarwinSecondaryPacStart;
  if (Bytes == 0 || Bytes > SIZE_16KB || KeysOffset > Bytes || Bytes - KeysOffset < 8) return EFI_LOAD_ERROR;
  CopyMem((VOID *)(UINTN)Destination, DarwinSecondaryPacStart, Bytes);
  WriteUnaligned64((UINT64 *)(UINTN)(Destination + KeysOffset), KeysPhysical);
  return EFI_SUCCESS;
}
#endif
