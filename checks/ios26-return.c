/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef const char CHAR8;
typedef int64_t INT64;
typedef size_t UINTN;
typedef unsigned EFI_STATUS;
typedef int BOOLEAN;
#define STATIC static
#define CONST const
#define VOID void
#define TRUE 1
#define FALSE 0
#define AsciiStrCmp strcmp
#define EFI_INVALID_PARAMETER 2
#define EFI_DEVICE_ERROR 7
#define EfiResetCold 0
static unsigned stalls,resets,draws;
static EFI_STATUS expectedStatus;
static UINTN remaining;
static void stall(UINTN us) { assert(us==1000000); ++stalls; }
static void reset(unsigned type,EFI_STATUS status,UINTN bytes,void *data) {
    assert(type==EfiResetCold && status==expectedStatus && !bytes && !data); ++resets;
}
static struct { void (*Stall)(UINTN); } services={stall}, *gBS=&services;
static struct { void (*ResetSystem)(unsigned,EFI_STATUS,UINTN,void*); } runtime={reset}, *gRT=&runtime;
#include "ReturnPolicy.h"
static void draw(void *context,EFI_STATUS status,UINTN seconds) {
    assert(context==&remaining && status==expectedStatus && seconds==remaining--); ++draws;
}
int main(void) {
    BOOLEAN resetRequested=1;UINTN seconds=99;
    assert(UniXNUParseReturnPolicy("menu",10,&resetRequested,&seconds));
    assert(!resetRequested && seconds==10);
    const char *actions[]={"reset","menu","retry","",NULL};
    const INT64 delays[]={-1,0,1,10,30,31,INT64_MAX};
    for(unsigned a=0;a<5;++a)for(unsigned d=0;d<7;++d) {
        resetRequested=1;seconds=99;
        int valid=UniXNUParseReturnPolicy(actions[a],delays[d],&resetRequested,&seconds);
        assert(valid==(a<2 && delays[d]>=1 && delays[d]<=30));
        if(!valid) assert(!resetRequested && !seconds);
    }
    for(unsigned duration=1;duration<=30;++duration)for(unsigned success=0;success<2;++success) {
        stalls=resets=draws=0;remaining=duration;expectedStatus=success?0:0xdead;
        assert(UniXNUResetAfterReturn(duration,expectedStatus,draw,&remaining)==EFI_DEVICE_ERROR);
        assert(stalls==duration && draws==duration && resets==1 && !remaining);
    }
    stalls=resets=draws=0;
    assert(UniXNUResetAfterReturn(31,0,draw,NULL)==EFI_INVALID_PARAMETER);
    assert(UniXNUResetAfterReturn(0,0,draw,NULL)==EFI_INVALID_PARAMETER);
    assert(UniXNUResetAfterReturn(10,0,NULL,NULL)==EFI_INVALID_PARAMETER);
    assert(!stalls && !resets && !draws);
    puts("PASS: actual boot-return policy: explicit opt-in, bounded countdown, preserved status, one reset, no retry or disk I/O");
}
