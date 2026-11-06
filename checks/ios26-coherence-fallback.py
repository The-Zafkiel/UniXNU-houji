#!/usr/bin/env python3
"""Exercise the actual iOS26 fallback's cleanup and capability publication order."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
GPU = ROOT / 'kernel/overlay/iokit/Kernel/houji-ufs-root'
source = (ROOT / 'kernel/ios26/overlay/iokit/Kernel/houji-ufs-root/HoujiCoherenceFallback26.inc').read_text()
unit = r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include "Adreno740/NativeAdrenoCoherencePolicy.h"
static constexpr unsigned HoujiCoherenceAttempts=8, HoujiCoherenceBytes=65536;
static constexpr unsigned NativeAdrenoCleanInvalidate=3;
static volatile uint32_t native_adreno_io_coherent;
static unsigned resourceFail, auditFail, operations, audits, idleCalls;
static bool views=true, idle=true, outstanding=false, lease=true, tables=true, postIdle=true;
static std::vector<unsigned> resources;
static unsigned operation(unsigned kind) {
 resources.push_back(kind);return ++operations==resourceFail?1:0;
}
struct Backing { void *mapping(){return (void*)0x4000;} };
static struct {
 unsigned active,phase,code,attempts,dirty,copies,draws,pass,render_pass,unmapped,command_mapped;
 uint64_t handle;Backing backing;
} gHoujiCoherence;
static struct { unsigned mode; } gHoujiLive;
static struct {
 unsigned phase;uint64_t handle[3];
 struct { unsigned unmap(uint64_t handle){assert(handle==77);return operation(1);} } vm;
 struct { unsigned unmapRetainedForTrial(uint64_t handle){assert(handle==88);return operation(2);} } client;
 struct { bool outstanding(){return ::outstanding;} } submit;
} gHoujiApi;
static bool houjiCoherenceCpuViews(){return views;}
static bool houjiVmIdle(){return idle && (++idleCalls==1 || postIdle);}
static bool houjiFullLease(){return lease;}
static bool houjiFullTables(){return tables;}
static unsigned houjiApiAuditTables(){return ++audits==auditFail?1:0;}
static bool houjiApiBo(unsigned index,unsigned mapped){assert(index==2 && !mapped);return true;}
static unsigned NativeAdrenoMaintainOwnedPages(void *p,unsigned bytes,unsigned mode){
 assert(p==(void*)0x4000 && bytes==65536 && mode==NativeAdrenoCleanInvalidate);return operation(3);
}
'''
unit += source
unit += r'''
static void reset(){
 gHoujiCoherence={};gHoujiApi={};gHoujiLive.mode=5;native_adreno_io_coherent=0;
 auto &p=gHoujiCoherence;p.active=1;p.phase=3;p.attempts=8;p.handle=77;p.command_mapped=1;
 gHoujiApi.phase=12;gHoujiApi.handle[2]=88;
 resourceFail=auditFail=operations=audits=idleCalls=0;resources.clear();
 views=idle=lease=tables=postIdle=true;outstanding=false;
}
static void unpublished(){assert(gHoujiLive.mode==5 && gHoujiApi.phase==12 && gHoujiCoherence.active && !native_adreno_io_coherent);}
int main(){
 for(unsigned gate=0;gate<22;gate++){
  reset();auto &p=gHoujiCoherence;unsigned status=11;
  switch(gate){
  case 0:status=12;break;case 1:gHoujiLive.mode=4;break;case 2:native_adreno_io_coherent=2;break;
  case 3:p.active=0;break;case 4:p.phase=4;break;case 5:p.code=1;break;
  case 6:p.attempts=7;break;case 7:p.dirty=48;break;case 8:p.copies=1;break;
  case 9:p.draws=1;break;case 10:p.pass=1;break;case 11:p.render_pass=1;break;
  case 12:p.unmapped=1;break;case 13:p.handle=0;break;case 14:p.command_mapped=0;break;
  case 15:gHoujiApi.phase=11;break;case 16:views=false;break;case 17:idle=false;break;
  case 18:outstanding=true;break;case 19:lease=false;break;case 20:tables=false;break;
  case 21:status=0;break;
  }
  assert(!houjiCoherenceFallback26(status) && resources.empty());
 }
 for(unsigned fail=1;fail<=3;fail++){
  reset();resourceFail=fail;assert(!houjiCoherenceFallback26(11));unpublished();assert(resources.size()==fail);
 }
 for(unsigned fail=1;fail<=2;fail++){
  reset();auditFail=fail;assert(!houjiCoherenceFallback26(11));unpublished();assert(resources.size()==fail);
 }
 reset();postIdle=false;assert(!houjiCoherenceFallback26(11));unpublished();
 for(unsigned dirty:{0u,47u}){
  reset();gHoujiCoherence.dirty=dirty;assert(houjiCoherenceFallback26(11));
  assert((resources==std::vector<unsigned>{1,2,3}));
  assert(gHoujiCoherence.unmapped && !gHoujiCoherence.handle && !gHoujiCoherence.command_mapped);
  assert(!gHoujiCoherence.active && gHoujiApi.phase==10 && gHoujiLive.mode==3);
  assert(!native_adreno_io_coherent && !NativeAdrenoCoherentMapping(native_adreno_io_coherent,1,3));
  for(unsigned permissions=0;permissions<4;permissions++)assert(!NativeAdrenoCoherentImport(native_adreno_io_coherent,permissions));
 }
}
'''
with tempfile.TemporaryDirectory(prefix='houji26-coherence-fallback-') as name:
    directory = Path(name)
    cpp, binary = directory / 'check.cpp', directory / 'check'
    cpp.write_text(unit)
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I', str(GPU), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PASS: actual iOS26 fallback denies non-precondition failures, removes both probe mappings before continuation, keeps resources pinned and coherence off, and fails closed on cleanup errors; host callbacks, no GPU claim')
