/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Base.h>
typedef int BOOLEAN;
typedef unsigned EFI_STATUS;
#define STATIC static
#define CONST const
#define VOID void
#define TRUE 1
#define FALSE 0
#define MAX_UINT64 UINT64_MAX
#define MAX_UINTN SIZE_MAX
#define EFI_SUCCESS 0
#define EFI_LOAD_ERROR 1
#define EFI_UNSUPPORTED 2
#define SIZE_16KB (16ULL << 10)
#define SIZE_2MB (2ULL << 20)
#define SIZE_32MB (32ULL << 20)
#define WINDOW_SIZE (256ULL << 20)
#define MAX_SEGMENTS 64
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ALIGN_VALUE(v,a) (((v)+(a)-1)&~((a)-1))
#define ZeroMem(p,n) memset((p),0,(n))
#define CompareMem(a,b,n) memcmp((a),(b),(n))
#define AsciiStrCmp(a,b) strcmp((a),(b))
#define ScanMem8(p,n,v) memchr((p),(v),(n))
static UINT32 ReadUnaligned32(const void *p) { UINT32 v; memcpy(&v,p,4); return v; }
static UINT64 ReadUnaligned64(const void *p) { UINT64 v; memcpy(&v,p,8); return v; }
#include "WindowSize.h"
#include "DarwinBootImage.h"
#include "BootArgs.h"

static uint8_t *readFile(const char *name, size_t *bytes) {
    FILE *file=fopen(name,"rb"); assert(file);
    assert(!fseek(file,0,SEEK_END)); long size=ftell(file);
    assert(size>0 && size<=128*1024*1024); rewind(file);
    uint8_t *data=malloc((size_t)size); assert(data);
    assert(fread(data,1,(size_t)size,file)==(size_t)size); fclose(file);
    *bytes=(size_t)size; return data;
}
static void put32(uint8_t *p, uint32_t v) { memcpy(p,&v,4); }
static void put64(uint8_t *p, uint64_t v) { memcpy(p,&v,8); }
int main(int argc,char **argv) {
    assert(DarwinBootVerbose("-v serial=0"));
    assert(DarwinBootVerbose(" \tdebug=1\t-v\t"));
    assert(DarwinBootVerbose("-v"));
    assert(!DarwinBootVerbose(""));
    assert(!DarwinBootVerbose("-verbose foo=-v -v=1"));
    assert(!DarwinBootVerbose("-"));
    if(argc==3 && strcmp(argv[1],"--adt")==0) {
        size_t bytes=0;uint8_t *data=readFile(argv[2],&bytes);
        UINTN at=0,rd=MAX_UINTN,tc=MAX_UINTN,db=MAX_UINTN,ds=MAX_UINTN,se=MAX_UINTN,en=MAX_UINTN;
        UINT64 window=0;BOOLEAN packed=FALSE;
        int valid=AdtNode(data,bytes,&at,0,3,&rd,&tc,&window,&packed,&db,&ds,&se,&en) && at==bytes;
        free(data);return valid ? 0 : 1;
    }
    uint8_t small[4096]={0};
    const uint64_t address=0xfffffff007004000ULL;
    put32(small,0xfeedfacf);put32(small+4,0x100000c);put32(small+8,0x80000002);
    put32(small+12,2);put32(small+16,2);put32(small+20,360);
    put32(small+32,0x19);put32(small+36,72);
    put64(small+56,address);put64(small+64,sizeof(small));
    put64(small+80,sizeof(small));put32(small+92,5);
    put32(small+104,5);put32(small+108,288);put32(small+112,6);put32(small+116,68);
    put64(small+376,address+0x200);
    KERNEL kernel;
    assert(ParseKernel(small,sizeof(small),&kernel)==EFI_SUCCESS);
    for(size_t bytes=0;bytes<392;++bytes)assert(ParseKernel(small,bytes,&kernel)!=EFI_SUCCESS);
    put32(small+8,0);assert(ParseKernel(small,sizeof(small),&kernel)==EFI_UNSUPPORTED);
    put32(small+8,2);put32(small+108,280);
    assert(ParseKernel(small,sizeof(small),&kernel)==EFI_LOAD_ERROR);
    uint8_t trust[28]={0};put32(trust,1);put32(trust+4,8);put32(trust+8,2);
    assert(!ValidTrustCaches(trust,sizeof(trust)));
    _Static_assert(sizeof(((DARWIN_BOOT_ARGS *)0)->CommandLine)==1024,"Darwin 25 command line");
    if(argc==3) {
        size_t bytes=0;uint8_t *data=readFile(argv[1],&bytes);
        assert(ParseKernel(data,bytes,&kernel)==EFI_SUCCESS);
        printf("PASS: production kernel parser: segments=%zu entry=%llx window-base=%llx\n",
            kernel.Count,(unsigned long long)kernel.Entry,(unsigned long long)kernel.Base);
        free(data);data=readFile(argv[2],&bytes);
        UINTN offset=0,rd=MAX_UINTN,tc=MAX_UINTN,db=MAX_UINTN,ds=MAX_UINTN,secondary=MAX_UINTN,entropy=MAX_UINTN;
        UINT64 window=0;BOOLEAN packed=FALSE;
        assert(AdtNode(data,bytes,&offset,0,3,&rd,&tc,&window,&packed,&db,&ds,&secondary,&entropy));
        assert(offset==bytes && rd!=MAX_UINTN && db!=MAX_UINTN && ds!=MAX_UINTN && secondary!=MAX_UINTN);
        assert(!packed && tc==MAX_UINTN && U64(data+rd)==0 && U64(data+rd+8)==0);
        for(size_t cut=0;cut<bytes;++cut) {
            offset=0;rd=tc=db=ds=secondary=entropy=MAX_UINTN;window=0;packed=FALSE;
            assert(!AdtNode(data,cut,&offset,0,3,&rd,&tc,&window,&packed,&db,&ds,&secondary,&entropy));
        }
        free(data);
        puts("PASS: production ADT parser, required handoffs and every truncated boundary");
    } else assert(argc==1);
    puts("PASS: actual DarwinBoot image validation and Darwin 25 boot-args layout; no hardware");
}
