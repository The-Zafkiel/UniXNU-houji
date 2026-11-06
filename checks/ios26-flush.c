/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t UINT8;
typedef uint32_t UINT32;
typedef unsigned long UINTN;
typedef unsigned BOOLEAN;
#define STATIC static
#define CONST const
#define TRUE 1
#define FALSE 0
#include "UfsLogWriteGuard.h"
static void le32(uint8_t *p,uint32_t v) { memcpy(p,&v,4); }
int main(void) {
    uint8_t request[0xc8]={0};request[0x48]=1;request[0x5c]=0x35;
    assert(UfsLogWriteAllowed(request,sizeof request,FALSE));
    assert(UfsLogWriteAllowed(request,sizeof request,TRUE));
    for(unsigned size=0;size<sizeof request;++size)assert(!UfsLogWriteAllowed(request,size,FALSE));
    for(unsigned i=1;i<16;++i){request[0x5c+i]=1;assert(!UfsLogWriteAllowed(request,sizeof request,FALSE));request[0x5c+i]=0;}
    const unsigned fields[]={1,5,0x49,0x4a,0x6c};
    for(unsigned i=0;i<sizeof fields/sizeof fields[0];++i){request[fields[i]]=1;assert(!UfsLogWriteAllowed(request,sizeof request,FALSE));request[fields[i]]=0;}
    le32(request+0x54,4096);le32(request+0x88,4096);
    assert(!UfsLogWriteAllowed(request,sizeof request,FALSE));
    request[0x5c]=0x2a;request[1]=1;request[0x49]=0x20;request[0x60]=0x10;request[0x64]=1;
    assert(!UfsLogWriteAllowed(request,sizeof request,FALSE));
    assert(UfsLogWriteAllowed(request,sizeof request,TRUE));
    request[0x60]=0x18;assert(!UfsLogWriteAllowed(request,sizeof request,TRUE));
    request[0x60]=0x10;request[0x64]=0;assert(!UfsLogWriteAllowed(request,sizeof request,TRUE));
    request[0x64]=1;request[0x5c]=0x42;assert(!UfsLogWriteAllowed(request,sizeof request,TRUE));
    return 0;
}
