/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <Base.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "BootArgs.h"
#ifdef PRINT_OFFSETS
int main(void) {
    const char *names[]={"BASE","ROW","HEIGHT","DEPTH"};const unsigned indices[]={0,2,4,5};
    for(unsigned i=0;i<4;++i)printf("#define BA_HOUJI_VIDEO_%s %zu\n",names[i],offsetof(DARWIN_BOOT_ARGS,Video)+8*indices[i]);
    return 0;
}
#else
extern void test_screen(DARWIN_BOOT_ARGS *);
static unsigned char storage[4096+64] __attribute__((aligned(16)));
static void skipped(DARWIN_BOOT_ARGS args) {
    memset(storage,0xa5,sizeof storage);test_screen(&args);
    for(unsigned i=0;i<sizeof storage;++i)assert(storage[i]==0xa5);
}
int main(void) {
    DARWIN_BOOT_ARGS args={0};args.Video[0]=(uintptr_t)(storage+32);args.Video[2]=128;args.Video[4]=32;args.Video[5]=32;
    DARWIN_BOOT_ARGS saved=args;
    memset(storage,0xa5,sizeof storage);test_screen(&args);assert(!memcmp(&args,&saved,sizeof args));
    for(unsigned i=0;i<sizeof storage;++i){
        unsigned expected=(i>=32 && i<32+256)?((i-32)%4==1?0:255):0xa5;
        assert(storage[i]==expected);
    }
    DARWIN_BOOT_ARGS bad=args;bad.Video[0]=0;skipped(bad);
    bad=args;bad.Video[0]++;skipped(bad);
    bad=args;bad.Video[0]=UINT64_MAX-7;skipped(bad);
    bad=args;bad.Video[2]=16385;skipped(bad);
    bad=args;bad.Video[4]=16385;skipped(bad);
    bad=args;bad.Video[4]=0;skipped(bad);
    bad=args;bad.Video[5]=16;skipped(bad);
    return 0;
}
#endif
