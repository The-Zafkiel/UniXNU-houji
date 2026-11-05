/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t UINT8;
typedef size_t UINTN;
typedef int BOOLEAN;
#define STATIC static
#define CONST const
#define VOID void
#define TRUE 1
#define FALSE 0
#define CopyMem(d,s,n) memcpy(d,s,n)
#include "DarwinPacEntropy.h"

static BOOLEAN hardware(void *cookie, UINT8 *keys) {
    int mode = *(int *)cookie;
    memset(keys, mode == 2 ? 0 : 0x53, 80);
    return mode != 0;
}
static int empty(const UINT8 *bytes) {
    UINT8 result=0;
    for(size_t i=0;i<80;i++) result |= bytes[i];
    return result==0;
}
int main(void) {
    UINT8 keys[80], trial[80], expected[80];
    BOOLEAN used=TRUE;
    int mode=1;
    memset(trial,0x74,80);
    assert(DarwinPacSelectEntropy(keys,hardware,&mode,trial,80,&used));
    assert(!used && keys[0]==0x53 && empty(trial));
    mode=0;
    for(size_t i=0;i<80;i++) expected[i]=trial[i]=(UINT8)(i+1);
    assert(DarwinPacSelectEntropy(keys,hardware,&mode,trial,80,&used));
    assert(used && memcmp(keys,expected,80)==0 && empty(trial));
    assert(!DarwinPacSelectEntropy(keys,hardware,&mode,trial,80,&used));
    assert(!used && empty(keys));
    assert(!DarwinPacSelectEntropy(keys,hardware,&mode,NULL,0,&used));
    assert(empty(keys)); // Hardware partial writes never become keys.
    mode=2;
    assert(!DarwinPacSelectEntropy(keys,hardware,&mode,NULL,0,&used));
    assert(empty(keys)); // A successful all-zero hardware reply is rejected.
    for(size_t key=0;key<5;key++) {
        memset(trial,0x74,80);memset(trial+16*key,0,16);
        assert(!DarwinPacSelectEntropy(keys,hardware,&mode,trial,80,&used));
        assert(empty(keys) && empty(trial));
    }
    memset(trial,0x74,80);
    assert(!DarwinPacSelectEntropy(keys,hardware,&mode,trial,79,&used));
    assert(empty(keys) && trial[79]==0x74);
    assert(!DarwinPacSelectEntropy(keys,hardware,&mode,NULL,80,&used));
    assert(empty(keys));
    DarwinPacErase(trial,80);assert(empty(trial));
    puts("PASS: production PAC entropy selection, partial errors and one-use erasure; no hardware");
}
