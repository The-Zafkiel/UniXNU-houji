#!/usr/bin/env python3
"""Validate bounded firmware writes and actual AArch64 store instructions."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / 'kernel/ios26/overlay/iokit/Kernel/houji-ufs-root/battery'
unit = r'''
#include <cassert>
#include <cstring>
#include "HoujiFirmwareWrite26.h"
int main(){
 alignas(16) uint32_t storage[20];uint8_t source[64],expected[80];
 auto *bytes=(uint8_t*)storage;
 for(unsigned i=0;i<64;i++)source[i]=(uint8_t)(i*19+7);
 for(unsigned offset=0;offset<=64;offset++)for(unsigned count=0;count<=64-offset;count++){
  memset(storage,0x9d,sizeof(storage));memset(expected,0x9d,sizeof(expected));
  memcpy(expected+8+offset,source,count);
  assert(hjfw26_write(bytes+8,64,offset,source,count,false));
  assert(!memcmp(bytes,expected,sizeof(storage)));
  assert(hjfw26_write(bytes+8,64,offset,nullptr,count,true));
  memset(expected+8+offset,0,count);assert(!memcmp(bytes,expected,sizeof(storage)));
 }
 memset(storage,0x9d,sizeof(storage));memcpy(expected,storage,sizeof(storage));
 assert(!hjfw26_write(bytes+8,64,65,source,0,false));
 assert(!hjfw26_write(bytes+8,64,63,source,2,false));
 assert(!hjfw26_write(bytes+8,63,0,source,1,false));
 assert(!hjfw26_write(bytes+9,64,0,source,1,false));
 assert(!hjfw26_write(nullptr,64,0,source,1,false));
 assert(!hjfw26_write(bytes+8,64,0,nullptr,1,false));
 assert(!hjfw26_write((void*)(UINTPTR_MAX-3),8,0,source,1,false));
 assert(!hjfw26_write(bytes+8,64,0,(void*)(UINTPTR_MAX-1),4,false));
 assert(!hjfw26_write(bytes+8,64,UINT64_MAX,source,4,false));
 assert(!memcmp(bytes,expected,sizeof(storage)));
}
'''
wrapper = r'''
#include "HoujiFirmwareWrite26.h"
extern "C" bool copy_image(volatile void *p,uint64_t n,uint64_t at,const void *src,size_t bytes){
 return hjfw26_write(p,n,at,src,bytes,false);
}
extern "C" bool zero_image(volatile void *p,uint64_t n,uint64_t at,size_t bytes){
 return hjfw26_write(p,n,at,nullptr,bytes,true);
}
'''
with tempfile.TemporaryDirectory(prefix='houji26-firmware-writes-') as name:
    directory = Path(name)
    cpp, binary = directory / 'check.cpp', directory / 'check'
    cpp.write_text(unit)
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I', str(INCLUDE), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    arm, assembly = directory / 'arm.cpp', directory / 'arm.s'
    arm.write_text(wrapper)
    subprocess.run(['clang++', '--target=aarch64-none-elf', '-std=c++17', '-ffreestanding',
                    '-O2', '-S', '-I', str(INCLUDE), str(arm), '-o', str(assembly)], check=True)
    text = assembly.read_text()
    assert not re.search(r'\b(?:bl|b)\s+_*(?:memcpy|memset|bzero)\b|\bdc\s+zva|\b(?:str|stp)\s+q\d', text)
    assert re.search(r'\bstr\s+w\d', text)
    assert re.search(r'\bdsb\s+sy', text)
print('PASS: actual firmware writer preserves adjacent bytes across every small alignment, rejects bounds/overflow, and AArch64 uses word stores without bulk libc/DC ZVA; no device-memory hardware claim')
