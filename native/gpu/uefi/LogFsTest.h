/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Validate the backed-up GPT before connecting its logfs FAT child. The command
 * gate is the final write boundary, even for filesystem metadata/repair paths.
 */
STATIC EFI_STATUS
ValidateLogFsParent (EFI_HANDLE *Parent, EFI_BLOCK_IO_PROTOCOL **ParentIo,
                     EFI_PARTITION_ENTRY *Entry)
{
  EFI_HANDLE *Handles = NULL;
  UINTN Count = 0, Matches = 0;
  EFI_STATUS Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid,
    NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) { return Status; }
  CONST EFI_GUID Disk = {0xE7D1484D,0xC58A,0xAA30,{0x98,0xC1,0xB1,0x10,0xB4,0xBC,0x5E,0xC4}};
  CONST EFI_GUID Part = {0x04BB810B,0x92B4,0x38CA,{0x8E,0xF5,0x9D,0xD0,0x5A,0x04,0xFE,0x5C}};
  for (UINTN I = 0; I < Count; I++) {
    EFI_BLOCK_IO_PROTOCOL *Io = NULL;
    if (EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiBlockIoProtocolGuid, (VOID **)&Io)) ||
        !Io || !Io->Media) { continue; }
    EFI_BLOCK_IO_MEDIA *M = Io->Media;
    if (M->LogicalPartition || !M->MediaPresent || M->ReadOnly ||
        M->BlockSize != 4096 || M->LastBlock != 0x7AABFFF) { continue; }
    UINTN Align = M->IoAlign < EFI_PAGE_SIZE ? EFI_PAGE_SIZE : M->IoAlign;
    if (Align > 0x200000 || (Align & (Align - 1))) { continue; }
    UINT8 *Data = AllocateAlignedPages (3, Align);
    if (!Data) { continue; }
    Status = Io->ReadBlocks (Io, M->MediaId, 1, 3 * 4096, Data);
    EFI_PARTITION_TABLE_HEADER *H = (VOID *)Data;
    UINT32 Crc = 0;
    BOOLEAN Good = !EFI_ERROR (Status) && H->Header.Signature == EFI_PTAB_HEADER_ID &&
      H->Header.Revision == 0x10000 && H->Header.HeaderSize == 92 &&
      H->Header.Reserved == 0 && H->Header.CRC32 == 0x631FC7EA &&
      H->MyLBA == 1 && H->AlternateLBA == M->LastBlock &&
      CompareGuid (&H->DiskGUID, &Disk) && H->PartitionEntryLBA == 2 &&
      H->NumberOfPartitionEntries == 64 && H->SizeOfPartitionEntry == 128 &&
      H->PartitionEntryArrayCRC32 == 0x9F1BB496;
    if (Good) {
      H->Header.CRC32 = 0;
      Good = !EFI_ERROR (gBS->CalculateCrc32 (Data, 92, &Crc)) && Crc == 0x631FC7EA;
    }
    if (Good) {
      Good = !EFI_ERROR (gBS->CalculateCrc32 (Data + 4096, 8192, &Crc)) && Crc == 0x9F1BB496;
    }
    EFI_PARTITION_ENTRY *E = (VOID *)(Data + 4096 + 12 * 128);
    if (Good) {
      Good = E->StartingLBA == HOUJI_LOGFS_FIRST && E->EndingLBA == HOUJI_LOGFS_END - 1 &&
        E->Attributes == 0 && CompareGuid (&E->UniquePartitionGUID, &Part) &&
        StrCmp (E->PartitionName, L"logfs") == 0;
    }
    if (Good) { Matches++; *Parent = Handles[I]; *ParentIo = Io; CopyMem (Entry, E, sizeof (*E)); }
    FreeAlignedPages (Data, 3);
  }
  FreePool (Handles);
  Print (L"HOUJI LOGFS: pinned GPT and logfs entry matches=%u\n", Matches);
  return Matches == 1 ? EFI_SUCCESS : EFI_SECURITY_VIOLATION;
}

STATIC EFI_STATUS
FindLogFsChild (EFI_HANDLE Parent, CONST EFI_PARTITION_ENTRY *Entry, EFI_HANDLE *Child)
{
  EFI_DEVICE_PATH_PROTOCOL *ParentPath = DevicePathFromHandle (Parent);
  if (!ParentPath) { return EFI_NOT_FOUND; }
  UINTN Prefix = GetDevicePathSize (ParentPath) - END_DEVICE_PATH_LENGTH;
  EFI_HANDLE *Handles = NULL;
  UINTN Count = 0, Matches = 0;
  EFI_STATUS Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiPartitionInfoProtocolGuid,
    NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) { return Status; }
  for (UINTN I = 0; I < Count; I++) {
    EFI_PARTITION_INFO_PROTOCOL *Info = NULL;
    EFI_BLOCK_IO_PROTOCOL *Io = NULL;
    EFI_DEVICE_PATH_PROTOCOL *Path = DevicePathFromHandle (Handles[I]);
    if (!Path || GetDevicePathSize (Path) != Prefix + sizeof (HARDDRIVE_DEVICE_PATH) + END_DEVICE_PATH_LENGTH ||
        CompareMem (Path, ParentPath, Prefix) != 0 ||
        EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiPartitionInfoProtocolGuid, (VOID **)&Info)) ||
        !Info || Info->Type != PARTITION_TYPE_GPT || CompareMem (&Info->Info.Gpt, Entry, sizeof (*Entry)) != 0 ||
        EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiBlockIoProtocolGuid, (VOID **)&Io)) ||
        !Io || !Io->Media || !Io->Media->LogicalPartition ||
        Io->Media->BlockSize != 4096 || Io->Media->LastBlock != 0x7FF) { continue; }
    HARDDRIVE_DEVICE_PATH *Hd = (VOID *)((UINT8 *)Path + Prefix);
    if (Hd->Header.Type != MEDIA_DEVICE_PATH || Hd->Header.SubType != MEDIA_HARDDRIVE_DP ||
        Hd->PartitionNumber != 13 || Hd->PartitionStart != HOUJI_LOGFS_FIRST ||
        Hd->PartitionSize != 0x800 || Hd->SignatureType != SIGNATURE_TYPE_GUID ||
        CompareMem (Hd->Signature, &Entry->UniquePartitionGUID, sizeof (EFI_GUID)) != 0) { continue; }
    Matches++; *Child = Handles[I];
  }
  FreePool (Handles);
  Print (L"HOUJI LOGFS: exact parent/partition child matches=%u\n", Matches);
  return Matches == 1 ? EFI_SUCCESS : EFI_NOT_FOUND;
}

STATIC EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *mGpuCheckpointFs;
STATIC EFI_BLOCK_IO_PROTOCOL *mGpuCheckpointParent;

STATIC EFI_STATUS
RunLogFsTest (EFI_HANDLE ImageHandle)
{
  EFI_HANDLE Parent = NULL, Child = NULL;
  EFI_BLOCK_IO_PROTOCOL *ParentIo = NULL;
  EFI_PARTITION_ENTRY Entry;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
  EFI_FILE_PROTOCOL *Root = NULL, *File = NULL;
  VOID *Readback = NULL;
  EFI_STATUS Status = ValidateLogFsParent (&Parent, &ParentIo, &Entry);
  if (EFI_ERROR (Status)) { return Status; }
  EFI_LOADED_IMAGE_PROTOCOL *Self = NULL;
  EFI_HANDLE PartitionDriver = NULL;
  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Self);
  if (EFI_ERROR (Status) || !Self || !Self->DeviceHandle) { return EFI_NOT_FOUND; }
  EFI_DEVICE_PATH_PROTOCOL *Path = FileDevicePath (Self->DeviceHandle, L"\\Drivers\\PartitionDxe.efi");
  if (!Path) { return EFI_OUT_OF_RESOURCES; }
  Status = gBS->LoadImage (FALSE, ImageHandle, Path, NULL, 0, &PartitionDriver);
  FreePool (Path);
  Print (L"HOUJI LOGFS r87: PartitionDxe LoadImage=%r\n", Status);
  if (EFI_ERROR (Status)) { return Status; }
  Status = gBS->StartImage (PartitionDriver, NULL, NULL);
  Print (L"HOUJI LOGFS: PartitionDxe StartImage=%r\n", Status);
  if (EFI_ERROR (Status)) { return Status; }
  VOID *DiskIo = NULL;
  EFI_STATUS DiskBefore = gBS->HandleProtocol (Parent, &gEfiDiskIoProtocolGuid, &DiskIo);
  Print (L"HOUJI LOGFS: parent DevicePath=%u DiskIo before=%r\n",
    DevicePathFromHandle (Parent) != NULL, DiskBefore);
  // FALSE is intentional: enumerate partition handles, do not mount all FATs.
  Status = gBS->ConnectController (Parent, NULL, NULL, FALSE);
  EFI_STATUS DiskAfter = gBS->HandleProtocol (Parent, &gEfiDiskIoProtocolGuid, &DiskIo);
  Print (L"HOUJI LOGFS: parent nonrecursive connect=%r DiskIo after=%r\n", Status, DiskAfter);
  if (EFI_ERROR (Status)) { return Status; }
  Status = FindLogFsChild (Parent, &Entry, &Child);
  if (EFI_ERROR (Status)) { return Status; }
  mLogFsWritesArmed = TRUE;
  Print (L"HOUJI LOGFS: writes armed ONLY physical LUN0 [1000,1800)\n");
  Status = gBS->ConnectController (Child, NULL, NULL, FALSE);
  Print (L"HOUJI LOGFS: child FAT connect=%r\n", Status);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = gBS->HandleProtocol (Child, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR (Status) || !Fs) { Status = EFI_NOT_FOUND; goto Done; }
  Status = Fs->OpenVolume (Fs, &Root);
  Print (L"HOUJI LOGFS: OpenVolume=%r\n", Status);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = Root->Open (Root, &File, L"\\UniXNU.log", EFI_FILE_MODE_READ, 0);
  if (!EFI_ERROR (Status)) { Status = EFI_ALREADY_STARTED; goto Done; }
  if (Status != EFI_NOT_FOUND) { goto Done; }
  File = NULL;
  if (mProbeLogTruncated) { Status = EFI_BUFFER_TOO_SMALL; goto Done; }
  Print (L"HOUJI LOGFS: r87 snapshot end; app text only; commit/readback follows\n");
  UINTN PayloadBytes = mProbeLogLength;
  UINT32 PayloadCrc = 0;
  Status = gBS->CalculateCrc32 (mProbeLog, PayloadBytes, &PayloadCrc);
  if (EFI_ERROR (Status)) { goto Done; }
  Print (L"HOUJI LOGFS CHECKSUM: payload-bytes=%u payload-crc32=%08x\n", PayloadBytes, PayloadCrc);
  if (mProbeLogTruncated) { Status = EFI_BUFFER_TOO_SMALL; goto Done; }
  mProbeLogFrozen = TRUE;
  UINTN Bytes = mProbeLogLength, Written = Bytes;
  UINT32 Crc = 0, BackCrc = 0;
  Status = gBS->CalculateCrc32 (mProbeLog, Bytes, &Crc);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = Root->Open (Root, &File, L"\\UniXNU.log",
    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
  Print (L"HOUJI LOGFS: create=%r bytes=%u CRC=%08x\n", Status, Bytes, Crc);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = File->Write (File, &Written, mProbeLog);
  Print (L"HOUJI LOGFS: Write=%r bytes=%u\n", Status, Written);
  if (EFI_ERROR (Status) || Written != Bytes) { Status = EFI_DEVICE_ERROR; goto Done; }
  Status = File->Flush (File);
  Print (L"HOUJI LOGFS: file Flush=%r\n", Status);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = File->Close (File); File = NULL;
  if (EFI_ERROR (Status)) { goto Done; }
  Status = Root->Open (Root, &File, L"\\UniXNU.log", EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) { goto Done; }
  Readback = AllocatePool (Bytes + 1);
  if (!Readback) { Status = EFI_OUT_OF_RESOURCES; goto Done; }
  UINTN Read = Bytes + 1;
  Status = File->Read (File, &Read, Readback);
  if (EFI_ERROR (Status) || Read != Bytes || CompareMem (Readback, mProbeLog, Bytes) != 0) {
    Status = EFI_CRC_ERROR; goto Done;
  }
  Status = gBS->CalculateCrc32 (Readback, Read, &BackCrc);
  if (EFI_ERROR (Status) || Crc != BackCrc) { Status = EFI_CRC_ERROR; goto Done; }
  Print (L"HOUJI LOGFS: file readback PASS bytes=%u CRC=%08x\n", Read, BackCrc);
Done:
  if (File) { EFI_STATUS S = File->Close (File); if (EFI_ERROR (S) && !EFI_ERROR (Status)) { Status = S; } }
  if (Root) { EFI_STATUS S = Root->Close (Root); if (EFI_ERROR (S) && !EFI_ERROR (Status)) { Status = S; } }
  if (Readback) { FreePool (Readback); }
  if (mLogFsWritesArmed) {
    EFI_STATUS Flush = ParentIo->FlushBlocks (ParentIo);
    Print (L"HOUJI LOGFS: device Flush=%r\n", Flush);
    if (EFI_ERROR (Flush) && !EFI_ERROR (Status)) { Status = Flush; }
  }
  mLogFsWritesArmed = FALSE;
  Print (L"HOUJI LOGFS: result=%r write-commands=%u cache-flushes=%u denied=%u\n",
    Status, mLogFsWriteCommands, mLogFsFlushCommands, mUfsDenied);
  if (!EFI_ERROR (Status) && mLogFsWriteCommands > 0 && mUfsDenied == 0) {
    if (mXnuUfsRun) {
      Print (L"HOUJI LOGFS: PASS; checkpoint closed and write gate disabled before XNU\n");
      mGpuCheckpointFs=Fs;mGpuCheckpointParent=ParentIo;
      mProbeLogFrozen=FALSE;
      return EFI_SUCCESS;
    }
    Print (L"HOUJI LOGFS: PASS; returning to Android for independent ADB readback\n");
    gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
    return EFI_DEVICE_ERROR;
  }
  return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
}

// Update only the file created by this boot's validated logfs test. Every
// checkpoint closes, flushes and independently reads back before returning.
// The GPU adapter calls this at application TPL, with CM3 still in reset.
EFI_STATUS EFIAPI ProbeCheckpoint(CONST CHAR8 *Stage) {
  EFI_FILE_PROTOCOL *Root=NULL,*File=NULL;
  VOID *Back=NULL;
  EFI_STATUS Status;
  UINTN Bytes=0,Written=0,Read=0;
  UINT32 Crc=0,BackCrc=0;
  if(!Stage || !mXnuUfsRun || !mGpuCheckpointFs || !mGpuCheckpointParent ||
     mLogFsWritesArmed || mProbeLogFrozen)return EFI_NOT_READY;
  EFI_TPL Tpl=gBS->RaiseTPL(TPL_HIGH_LEVEL);gBS->RestoreTPL(Tpl);
  if(Tpl!=TPL_APPLICATION)return EFI_UNSUPPORTED;
  Print(L"HOUJI GPU CHECKPOINT: %a\n",Stage);
  if(mProbeLogTruncated)return EFI_BUFFER_TOO_SMALL;
  Bytes=mProbeLogLength;
  Status=gBS->CalculateCrc32(mProbeLog,Bytes,&Crc);
  if(EFI_ERROR(Status))return Status;
  Print(L"HOUJI LOGFS CHECKSUM: payload-bytes=%u payload-crc32=%08x\n",Bytes,Crc);
  if(mProbeLogTruncated)return EFI_BUFFER_TOO_SMALL;
  mProbeLogFrozen=TRUE;Bytes=mProbeLogLength;
  Status=gBS->CalculateCrc32(mProbeLog,Bytes,&Crc);
  if(EFI_ERROR(Status))goto Done;
  mLogFsWritesArmed=TRUE;
  Status=mGpuCheckpointFs->OpenVolume(mGpuCheckpointFs,&Root);
  if(EFI_ERROR(Status))goto Done;
  Status=Root->Open(Root,&File,L"\\UniXNU.log",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
  if(EFI_ERROR(Status))goto Done;
  Status=File->SetPosition(File,0);
  if(EFI_ERROR(Status))goto Done;
  mLogFsWritesArmed=TRUE;Written=Bytes;
  Status=File->Write(File,&Written,mProbeLog);
  if(EFI_ERROR(Status) || Written!=Bytes) {Status=EFI_DEVICE_ERROR;goto Done;}
  Status=File->Flush(File);
  if(EFI_ERROR(Status))goto Done;
  Status=File->Close(File);File=NULL;
  if(EFI_ERROR(Status))goto Done;
  Status=mGpuCheckpointParent->FlushBlocks(mGpuCheckpointParent);
  if(EFI_ERROR(Status))goto Done;
  Status=Root->Open(Root,&File,L"\\UniXNU.log",EFI_FILE_MODE_READ,0);
  if(EFI_ERROR(Status))goto Done;
  Back=AllocatePool(Bytes+1);
  if(!Back) {Status=EFI_OUT_OF_RESOURCES;goto Done;}
  Read=Bytes+1;Status=File->Read(File,&Read,Back);
  if(EFI_ERROR(Status) || Read!=Bytes || CompareMem(Back,mProbeLog,Bytes)) {
    Status=EFI_CRC_ERROR;goto Done;
  }
  Status=gBS->CalculateCrc32(Back,Read,&BackCrc);
  if(EFI_ERROR(Status) || BackCrc!=Crc)Status=EFI_CRC_ERROR;
Done:
  if(File) {EFI_STATUS S=File->Close(File);if(EFI_ERROR(S) && !EFI_ERROR(Status))Status=S;}
  if(Root) {EFI_STATUS S=Root->Close(Root);if(EFI_ERROR(S) && !EFI_ERROR(Status))Status=S;}
  if(mLogFsWritesArmed) {
    EFI_STATUS S=mGpuCheckpointParent->FlushBlocks(mGpuCheckpointParent);
    if(EFI_ERROR(S) && !EFI_ERROR(Status))Status=S;
  }
  mLogFsWritesArmed=FALSE;mProbeLogFrozen=FALSE;
  if(Back)FreePool(Back);
  Print(L"HOUJI GPU CHECKPOINT: %a result=%r written=%u read=%u crc=%08x gate=closed\n",Stage,Status,Written,Read,BackCrc);
  return Status;
}
