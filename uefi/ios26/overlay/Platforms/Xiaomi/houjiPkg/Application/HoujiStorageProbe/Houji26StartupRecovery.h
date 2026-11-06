/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Early startup failure only: retain the resident UFS exception handler and
 * reset without filesystem I/O after an actual exact Recovery-BCB readback.
 * This must run before any GPU preparation; it is not a GPU ownership escape.
 */
STATIC BOOLEAN
Houji26StartupRecoveryReady (VOID)
{
  EFI_HANDLE Parent = NULL;
  EFI_BLOCK_IO_PROTOCOL *Io = NULL;
  EFI_PARTITION_ENTRY Entry;
  STATIC UINT8 Block[4096] __attribute__((aligned(4096)));
  UINT8 Expected[864];
  if (EFI_ERROR (ValidateLogFsParent (&Parent, &Io, &Entry)) || !Io || !Io->Media || !Io->ReadBlocks ||
      !Io->Media->MediaPresent || Io->Media->BlockSize != sizeof (Block) ||
      Io->Media->IoAlign > sizeof (Block) || Io->Media->LastBlock < 1024 ||
      !hgr_record (Expected, sizeof (Expected))) { return FALSE; }
  EFI_STATUS Status = Io->ReadBlocks (Io, Io->Media->MediaId, 1024, sizeof (Block), Block);
  return !EFI_ERROR (Status) && CompareMem (Block, Expected, sizeof (Expected)) == 0;
}

STATIC VOID
Houji26StartupFailureReset (EFI_STATUS Status, BOOLEAN RecoveryReady)
{
  if (!RecoveryReady || !EFI_ERROR (Status)) { return; }
  for (UINTN Seconds = 10; Seconds != 0; --Seconds) {
    Print (L"HOUJI26 startup failed: %r; Recovery reset in %u seconds\n", Status, Seconds);
    gBS->Stall (1000000);
  }
  // Do not return/unload the app while the OEM driver's BRK handler is resident.
  // If reset returns unexpectedly, the existing error path remains stopped.
  gRT->ResetSystem (EfiResetCold, Status, 0, NULL);
  Print (L"HOUJI26 Recovery reset returned; stopped before GPU/XNU\n");
}
