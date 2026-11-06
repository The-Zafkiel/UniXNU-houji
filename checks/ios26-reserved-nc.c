/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <string.h>
#include "HoujiReservedNc.h"
int main(void) {
    const uint64_t raw=UINT64_C(0xd2001000),meta=UINT64_C(0xd2004000),kernel=UINT64_C(0x8c0000000),size=UINT64_C(8)<<30;
    HjncPolicy state={0};assert(!hjnc_covers(&state,meta,HJNC_PAGE));
    assert(hjnc_register(&state,meta,raw,7*4096,kernel,size));
    assert(!hjnc_register(&state,meta,raw,7*4096,kernel,size));
    for(unsigned i=0;i<4;++i){assert(hjnc_covers(&state,meta+i*4096,4096));assert(hjnc_covers(&state,HJNC_TARGET+i*4096,4096));}
    assert(hjnc_covers(&state,meta,HJNC_PAGE) && hjnc_covers(&state,HJNC_TARGET,HJNC_PAGE));
    assert(!hjnc_covers(&state,raw,4096));
    assert(!hjnc_covers(&state,meta-1,1));assert(!hjnc_covers(&state,meta+HJNC_PAGE,1));
    assert(!hjnc_covers(&state,meta,2*HJNC_PAGE));assert(!hjnc_covers(&state,meta+HJNC_PAGE-1,2));
    assert(!hjnc_covers(&state,HJNC_TARGET-4096,4096));assert(!hjnc_covers(&state,HJNC_TARGET+HJNC_PAGE,4096));
    assert(!hjnc_covers(&state,UINT64_MAX-1,4));assert(!hjnc_covers(&state,0,4096));
    for(unsigned test=0;test<8;++test){
        HjncPolicy bad={0};uint64_t m=meta,r=raw,n=7*4096,k=kernel,s=size;
        switch(test){case 0:m++;break;case 1:r=0x1000;m=0x4000;break;case 2:n=8*4096;break;
        case 3:r=UINT64_MAX-8191;break;case 4:m=raw-4096;break;case 5:r=HJNC_TARGET;m=HJNC_TARGET;break;
        case 6:k=raw;s=0x100000;break;case 7:k=HJNC_TARGET;s=0x100000;break;}
        assert(!hjnc_register(&bad,m,r,n,k,s) && !bad.ready);
    }
    return 0;
}
