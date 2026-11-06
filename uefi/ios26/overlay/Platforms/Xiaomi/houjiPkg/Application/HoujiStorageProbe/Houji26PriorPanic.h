/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * An optional exact prior-trial RAM read, before GPU preparation or RAM tests.
 * Invalid, unavailable or overwritten records are never interpreted as text.
 */
#include "HoujiPanic26.h"
#include "Houji26PriorPanicConfig.h"

STATIC VOID Houji26ReadPriorPanic(BOOLEAN RecoveryReady) {
  if (!HOUJI26_PRIOR_PANIC_ENABLED) return;
  EFI_DXE_SERVICES *Dxe = NULL;
  EFI_STATUS Status = EfiGetSystemConfigurationTable(&gEfiDxeServicesTableGuid, (VOID **)&Dxe);
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR Descriptor = {0};
  if (EFI_ERROR(Status) || !Dxe ||
      EFI_ERROR(Dxe->GetMemorySpaceDescriptor(mHouji26PriorPanicPa, &Descriptor)) ||
      Descriptor.GcdMemoryType != EfiGcdMemoryTypeSystemMemory ||
      (Descriptor.Attributes & EFI_MEMORY_RP) ||
      !hp26_range(mHouji26PriorPanicPa, mHouji26PriorOwnedBase, mHouji26PriorOwnedBytes,
        Descriptor.BaseAddress, Descriptor.Length)) {
    Print(L"HOUJI26 PRIOR PANIC: exact prior range rejected; no RAM read\n");
    return;
  }
  // AllocateAddress reserves only free conventional RAM and does not clear it.
  // An address currently used by this firmware must fail, never be invalidated.
  CONST EFI_PHYSICAL_ADDRESS ExpectedPage = mHouji26PriorPanicPa & ~(EFI_PHYSICAL_ADDRESS)4095;
  EFI_PHYSICAL_ADDRESS Page = ExpectedPage;
  UINTN Pages = EFI_SIZE_TO_PAGES(mHouji26PriorPanicPa - Page + sizeof(Hp26Record));
  Status = gBS->AllocatePages(AllocateAddress, EfiLoaderData, Pages, &Page);
  if (EFI_ERROR(Status)) {
    Print(L"HOUJI26 PRIOR PANIC: prior page is unavailable; no RAM read\n");
    return;
  }
  BOOLEAN Mapped = Page == ExpectedPage;
  UINT64 FirstAttribute = 0;
  UINT64 SavedPar;
  __asm__ volatile("mrs %0, par_el1" : "=r"(SavedPar));
  for (UINT64 At = Page; At < Page + Pages * 4096; At += 4096) {
    UINT64 Par;
    __asm__ volatile("at s1e1r, %1\n\tisb\n\tmrs %0, par_el1" : "=r"(Par) : "r"(At) : "memory");
    UINT64 Attribute = Par >> 56;
    if (At == Page) FirstAttribute = Attribute;
    if ((Par & 1) || (Par & 0x0000FFFFFFFFF000ULL) != At ||
        (Attribute != 0xff && Attribute != 0x44)) Mapped = FALSE;
  }
  __asm__ volatile("msr par_el1, %0\n\tisb" : : "r"(SavedPar) : "memory");
  Hp26Record Record = {0};
  if (Mapped) {
    UINT64 Line = mHouji26PriorPanicPa & ~63ULL;
    for (; Line < mHouji26PriorPanicPa + sizeof(Record); Line += 64)
      __asm__ volatile("dc ivac, %0" : : "r"(Line) : "memory");
    __asm__ volatile("dsb sy" : : : "memory");
    CONST volatile UINT8 *Source = (CONST volatile UINT8 *)(UINTN)mHouji26PriorPanicPa;
    for (UINTN I = 0; I < sizeof(Record); ++I) ((UINT8 *)&Record)[I] = Source[I];
  }
  EFI_STATUS Freed = gBS->FreePages(Page, Pages);
  if (EFI_ERROR(Freed)) {
    Print(L"HOUJI26 PRIOR PANIC: page release failed (%r); stop before kernel\n", Freed);
    Houji26StartupFailureReset(Freed, RecoveryReady);
    for (;;) gBS->Stall(1000000);
  }
  if (!Mapped || !hp26_valid(&Record, mHouji26PriorRunId)) {
    BOOLEAN Magic = CompareMem(Record.magic, "HJPAN026", 8) == 0;
    BOOLEAN Trial = CompareMem(Record.run_id, mHouji26PriorRunId, 16) == 0;
    BOOLEAN Integrity = hp26_crc(&Record) == Record.crc32;
    BOOLEAN Empty = Mapped;
    for (UINTN I = 0; I < sizeof(Record); ++I) if (((UINT8 *)&Record)[I]) Empty = FALSE;
    if (Mapped && hp26_envelope(&Record, mHouji26PriorRunId) && !Record.length)
      Print(L"HOUJI26 PRIOR PANIC: initialized record verified; no panic reason captured\n");
    else
      Print(L"HOUJI26 PRIOR PANIC: unavailable mapped=%u attr=%lx magic=%u trial=%u crc=%u zero=%u; no error text accepted\n",
        Mapped, FirstAttribute, Magic, Trial, Integrity, Empty);
    return;
  }
  for (UINTN I = 0; I < Record.length; ++I)
    if (((UINT8)Record.text[I] < 32 && Record.text[I] != '\n') || (UINT8)Record.text[I] > 126)
      Record.text[I] = '?';
  Print(L"HOUJI26 PRIOR PANIC: VERIFIED: %a\n", Record.text);
  ZeroMem(&Record, sizeof(Record));
}
