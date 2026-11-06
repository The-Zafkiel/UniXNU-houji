/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>
typedef unsigned BOOLEAN;
typedef void VOID;
typedef void *EFI_HANDLE;
typedef uint8_t UINT8;
typedef size_t UINTN;
typedef uint64_t EFI_STATUS;
#define STATIC static
#define TRUE 1
#define FALSE 0
#define EFI_SUCCESS 0
#define FAILURE (UINT64_C(1) << 63)
#define EFI_ERROR(s) (((s) & FAILURE) != 0)
#define EfiResetCold 0
#define CompareMem memcmp
typedef struct { unsigned unused; } EFI_PARTITION_ENTRY;
typedef struct { unsigned MediaPresent, MediaId, BlockSize, IoAlign; uint64_t LastBlock; } MEDIA;
typedef struct IO IO;
struct IO { MEDIA *Media; EFI_STATUS (*ReadBlocks)(IO *, unsigned, uint64_t, size_t, void *); };
typedef IO EFI_BLOCK_IO_PROTOCOL;
static unsigned reads, stalls, resets, corrupt, parent_bad;
static uint64_t waited;
static EFI_STATUS read_error, reset_status;
#include "GpuRecoveryRequest.h"
static EFI_STATUS read_blocks(IO *io, unsigned media, uint64_t lba, size_t bytes, void *buffer) {
    assert(io && media == 7 && lba == 1024 && bytes == 4096 && !((uintptr_t)buffer & 4095));
    ++reads;
    memset(buffer, 0xa5, bytes);
    assert(hgr_record(buffer, 864));
    if(corrupt) ((uint8_t *)buffer)[863] ^= 1;
    return read_error;
}
static MEDIA media = {1, 7, 4096, 4096, 2048};
static IO io = {&media, read_blocks};
static EFI_STATUS ValidateLogFsParent(EFI_HANDLE *parent, IO **result, EFI_PARTITION_ENTRY *entry) {
    (void)entry;*parent=&io;*result=&io;return parent_bad ? FAILURE : EFI_SUCCESS;
}
static void stall(size_t us) { assert(us == 1000000);++stalls;waited += us; }
static void reset(unsigned kind, EFI_STATUS status, size_t bytes, void *data) {
    assert(kind == EfiResetCold && !bytes && !data);++resets;reset_status=status;
}
static void print(const wchar_t *format, ...) { (void)format; }
#define Print print
static struct { void (*Stall)(size_t); } bs = {stall}, *gBS = &bs;
static struct { void (*ResetSystem)(unsigned, EFI_STATUS, size_t, void *); } rt = {reset}, *gRT = &rt;
#include "Houji26StartupRecovery.h"
int main(void) {
    assert(Houji26StartupRecoveryReady() && reads == 1);
    corrupt=1;assert(!Houji26StartupRecoveryReady());corrupt=0;
    read_error=FAILURE;assert(!Houji26StartupRecoveryReady());read_error=0;
    unsigned old=reads;
    parent_bad=1;assert(!Houji26StartupRecoveryReady());parent_bad=0;
    io.ReadBlocks=NULL;assert(!Houji26StartupRecoveryReady());io.ReadBlocks=read_blocks;
    media.MediaPresent=0;assert(!Houji26StartupRecoveryReady());media.MediaPresent=1;
    media.BlockSize=512;assert(!Houji26StartupRecoveryReady());media.BlockSize=4096;
    media.IoAlign=8192;assert(!Houji26StartupRecoveryReady());media.IoAlign=4096;
    media.LastBlock=1000;assert(!Houji26StartupRecoveryReady());media.LastBlock=2048;
    assert(old == reads && !stalls && !resets);
    Houji26StartupFailureReset(EFI_SUCCESS, TRUE);
    Houji26StartupFailureReset(FAILURE | 20, FALSE);
    assert(!stalls && !resets);
    Houji26StartupFailureReset(FAILURE | 20, Houji26StartupRecoveryReady());
    assert(stalls == 10 && waited == 10000000 && resets == 1 && reset_status == (FAILURE | 20));
    return 0;
}
