#!/usr/bin/env python3
"""Exercise the actual bank0 writer; no ARM execution or device access."""
from pathlib import Path
import re
import subprocess
import tempfile

p = Path(__file__).resolve().parents[1]
source = (p / 'native/gpu/uefi/GpuScratchNative.h').read_text()


def function(name):
    match = re.search(r'STATIC int ' + name + r'\([^;]+?\) \{', source)
    assert match, name
    start = source.index('{', match.start())
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


body = '\n'.join(function(name) for name in ('ScratchExpected', 'ScratchWrite32', 'ScratchWrite64'))
body = body.replace('__asm__ volatile("dsb sy":::"memory");', '')
assert '__asm__' not in body
program = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "GpuScratchContext.h"
#define STATIC static
#define VOID void
#define CONST const
#define BOOLEAN int
#define TRUE 1
#define UINT32 uint32_t
#define UINT64 uint64_t
static unsigned mScratchAssigned=1,mScratchReady=1,healthy=1,writes;
static ArmSmmu4KConfig mScratchConfig;
static ArmSmmu4KResult mScratchBuilt;
static int StockBootstrapHealthy(void*c){(void)c;return healthy;}
static void MmioWrite32(uint32_t a,uint32_t v){(void)a;(void)v;writes++;}
static void MmioWrite64(uint32_t a,uint64_t v){(void)a;(void)v;writes++;}
''' + body + r'''
int main(void){
 uint64_t *t=calloc(3,4096);assert(t);mScratchConfig=(ArmSmmu4KConfig){32,36,0x81111000,HSC_POOL_PAGES};
 ArmSmmu4KRange ranges[HSC_CLASSES];assert(!hsc_pool_ranges(0x82222000,ranges));assert(!ArmSmmu4KBuild(&mScratchConfig,ranges,HSC_CLASSES,t,3,&mScratchBuilt));
 ArmSmmu4KContextImage image;assert(!ScratchExpected(&image));unsigned cases=0;
 const uint32_t fields[]={0,image.cba2r,image.cbar,image.tcr2,image.tcr,0,0,image.mair0,image.mair1};
 for(unsigned i=0;i<9;i++)if(i!=5 && i!=6){assert(!ScratchWrite32(0,hsc_address(i),fields[i]));cases++;}
 assert(!ScratchWrite64(0,0x3dc0020,image.ttbr0) && !ScratchWrite64(0,0x3dc0028,image.ttbr1));cases+=2;
 const uint32_t valid[][2]={{0x3dc0000,0x1027},{0x3dc0004,0x32b},{0x3dc0618,0},{0x3dc07f0,0xffffffff},{0x3da0804,0x80000000},{0x3da0c04,0}};
 for(unsigned i=0;i<sizeof(valid)/sizeof(valid[0]);i++){assert(!ScratchWrite32(0,valid[i][0],valid[i][1]));cases++;}
 const uint32_t denied[][2]={{0x3dc0000,0x1067},{0x3dc0000,0x27},{0x3dc0058,0xffffffff},{0x3da0048,0xffffffff},
 {0x3da6008,0},{0x3da0800,0},{0x3da0c00,0},{0x3dc2000,0},{0x3dc2004,0x32b},{0x3da1808,1},{0x3da1008,0x1f300},{0x3d00000,0}};
 for(unsigned i=0;i<sizeof(denied)/sizeof(denied[0]);i++){unsigned before=writes;assert(ScratchWrite32(0,denied[i][0],denied[i][1]) && writes==before);cases++;}
 for(uint32_t address=0x3da0000;address<0x3de0000;address+=4){
  unsigned before=writes;int status=ScratchWrite32(0,address,0);
  int allowed=address==0x3dc0000 || address==0x3dc003c || address==0x3dc0618 || address==0x3da0c04;
  assert((!status)==allowed && writes==before+(unsigned)allowed);cases++;
 }
 for(unsigned gate=0;gate<3;gate++){
  mScratchAssigned=mScratchReady=healthy=1;if(gate==0)mScratchAssigned=0;if(gate==1)mScratchReady=0;if(gate==2)healthy=0;
  unsigned before=writes;assert(ScratchWrite32(0,0x3dc0000,0x1027) && ScratchWrite64(0,0x3dc0020,image.ttbr0) && writes==before);cases++;
 }
 mScratchAssigned=mScratchReady=healthy=1;
 assert(ScratchWrite64(0,0x3dc0020,image.ttbr0^4096) && ScratchWrite64(0,0x3dc2020,image.ttbr0));cases+=2;
 printf("PASS: %u actual GPU scratch writer cases; bank0/SID0 only, exact owned root/ASID751, bank2/global/IRQ/fault-clear/CP writes rejected\n",cases);free(t);
}
'''
with tempfile.TemporaryDirectory() as directory:
    folder = Path(directory)
    (folder / 'test.c').write_text(program)
    sources = [str(path) for path in (p / 'native/gpu/vendor/ArmSmmu4K').glob('*.c')]
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I', str(p / 'native/gpu'), '-I', str(p / 'native/gpu/uefi'), str(folder / 'test.c'),
                    *sources, '-o', str(folder / 'test')], check=True)
    subprocess.run([str(folder / 'test')], check=True)
