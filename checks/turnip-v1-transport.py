#!/usr/bin/env python3
"""Run the selected Darwin transport against a recording IOKit peer; no GPU."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
text=(ROOT/'graphics/turnip/overlay/src/freedreno/vulkan/tu_knl_darwin.cc').read_text()
def function(signature):
    a=text.index(signature);b=text.index('\n}\n',a)+3
    return text[a:b]
unit=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <pthread.h>
#include <sched.h>
#include "NativeAdrenoABI.h"
using kern_return_t=int;using VkResult=int;
constexpr int KERN_SUCCESS=0,kIOReturnBadArgument=-1,kIOReturnBusy=-2,VK_SUCCESS=0;
constexpr unsigned DW_TUNNEL_MAGIC=0x4e544e4c;
struct {bool tunnel=false;unsigned conn=1,selector=29;NativeAdrenoAbiParams params{};pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;unsigned inflight_count=0;uint32_t inflight[8]{};} dw;
static unsigned calls,seen_method;static size_t seen_bytes;static NativeAdrenoAbiSubmitBatch seen{};static int forced_error;
static int IOConnectCallStructMethod(unsigned,uint32_t method,const void *data,size_t bytes,void *out,size_t *out_bytes){
 ++calls;
 if(dw.tunnel){assert(method==dw.selector&&bytes>=8);auto h=(const uint32_t*)data;assert(h[0]==DW_TUNNEL_MAGIC);method=h[1];data=(const uint8_t*)data+8;bytes-=8;}
 seen_method=method;seen_bytes=bytes;assert(bytes<=sizeof seen);seen={};if(bytes)memcpy(&seen,data,bytes);
 if(forced_error)return forced_error;
 assert(out&&out_bytes&&*out_bytes==sizeof(NativeAdrenoAbiSubmitReply));((NativeAdrenoAbiSubmitReply*)out)->fence=77;return 0;
}
struct tu_device {int vk=0;};struct tu_queue {tu_device *device;};
struct tu_darwin_submit_rec {uint32_t call_us=0,busy=0,space_wait_us=0;};
static uint64_t tu_trace_clock_ns(){static uint64_t v;return ++v;}
static int vk_device_set_lost(int*,const char*,...){return -3;}
static int dw_wait_fence_locked_free(tu_device*,uint32_t,uint64_t){return 0;}
#define MIN2(a,b) std::min((a),(b))
'''
unit+='static kern_return_t\n'+function('dw_call(uint32_t method,')
unit+='\nextern "C" bool\n'+function('tu_darwin_set_priority(uint32_t foreground, uint32_t background)\n{')
unit+='\nextern "C" bool\n'+function('tu_darwin_note(uint32_t kind, const uint32_t *words, uint32_t count)\n{')
unit+='\nstatic VkResult\n'+function('darwin_submit_chunk(struct tu_queue *queue,')
unit+=r'''
int main(){
 static_assert(NativeAdrenoAbiVersion==1&&NativeAdrenoMethodSubmitBatch==17&&NativeAdrenoParamsSubmitBatch==2);
 static_assert(sizeof(NativeAdrenoAbiSubmit)==416&&sizeof(NativeAdrenoAbiSubmitBatch)==3104);
 tu_device dev;tu_queue q{&dev};NativeAdrenoAbiSubmitBuffer entries[128]{};
 for(unsigned i=0;i<128;++i)entries[i]={i+1,0x10000+i*4096ull,64};
 for(bool tunnel:{false,true})for(bool batch:{false,true}){
  dw.tunnel=tunnel;dw.params.flags=batch?NativeAdrenoParamsSubmitBatch:0;uint32_t fence=0;tu_darwin_submit_rec rec{};
  const unsigned count=batch?128:16;
  assert(darwin_submit_chunk(&q,entries,count,&fence,&rec)==0&&fence==77);
  assert(seen_method==(batch?17:5)&&seen_bytes==(batch?3104:416)&&seen.buffer_count==count);
  assert(!memcmp(seen.buffers,entries,count*sizeof entries[0]));
  unsigned before=calls;assert(!tu_darwin_set_priority(0,2));assert(!tu_darwin_note(1,nullptr,0));assert(calls==before);
  forced_error=-9;assert(darwin_submit_chunk(&q,entries,count,&fence,&rec)==-3);assert(calls==before+1);forced_error=0;
  before=calls;assert(darwin_submit_chunk(&q,entries,count+1,&fence,&rec)==-3&&calls==before);
 }
}
'''
with tempfile.TemporaryDirectory(prefix='houji-v1-transport-') as tmp:
    p=Path(tmp);(p/'check.cpp').write_text(unit)
    subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I',str(ROOT/'kernel/overlay/iokit/Kernel/houji-ufs-root/Adreno740'),str(p/'check.cpp'),'-o',str(p/'check')],check=True)
    subprocess.run([str(p/'check')],check=True,timeout=10)
print('PASS: actual v1 direct/tunnel transport, 416/3104-byte submit selection, bounds, fence reply, no v2 priority/note calls and no failed-submit retry; no GPU')
