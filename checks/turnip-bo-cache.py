#!/usr/bin/env python3
"""Execute the current Darwin KMD's actual BO-cache operations; no GPU."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
text=(ROOT/'graphics/turnip/overlay/src/freedreno/vulkan/tu_knl_darwin.cc').read_text()
def struct(name):
    a=text.index('struct '+name+' {');return text[a:text.index('\n};',a)+3]
def function(name):
    a=text.index(name+'(');a=text.rfind('\nstatic ',0,a)+1;b=text.index('\n}\n',a)+3;return text[a:b]
unit=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
using VkResult=int;
constexpr int VK_SUCCESS=0,VK_NOT_READY=1;
static uint64_t clock_s=10;static uint32_t darwin_memory_pressure=0;
static uint64_t os_time_get_nano(){return clock_s*1000000000ull;}
static uint32_t p_atomic_read(const uint32_t *p){return *p;}
struct tu_device {};
'''+ '\n'.join(struct(n) for n in ['darwin_retired_bo','darwin_cached_bo','darwin_bo_cache_stats'])+r'''
struct darwin_retire_state {
 darwin_cached_bo *cache=nullptr;unsigned cache_count=0,cache_capacity=0;
 uint64_t cache_bytes=0,cache_stamp=0,cache_cleaned_s=0;
 uint32_t pressure_seen=0;darwin_bo_cache_stats cache_stats{};
 struct {uint64_t holes=0;} va;
};
static std::vector<uint64_t> freed;static bool refuse;
static VkResult darwin_release_native(tu_device*,darwin_retire_state*,uint64_t h,uint64_t,uint64_t,void **mapping,uint32_t *token){
 if(refuse)return VK_NOT_READY;assert(std::find(freed.begin(),freed.end(),h)==freed.end());freed.push_back(h);*mapping=nullptr;*token=0;return VK_SUCCESS;
}
'''+ '\n'.join(function(n) for n in ['darwin_cache_log','darwin_cache_evict','darwin_cache_put','darwin_cache_get','darwin_cache_flush','darwin_cache_trim'])+r'''
static void put(tu_device &d,darwin_retire_state &r,uint64_t id,uint64_t bytes,bool ro=false,bool mapped=true){
 darwin_retired_bo b{id,id*4096,bytes,mapped?(void*)1:nullptr,mapped?7u:0u,0,true,ro};assert(darwin_cache_put(&d,&r,&b));
}
int main(){
 tu_device d;darwin_retire_state r;darwin_cached_bo got{};
 put(d,r,1,100);put(d,r,2,120);put(d,r,3,100);put(d,r,4,100,true);put(d,r,5,100,false,false);
 assert(darwin_cache_get(&r,100,false,true,&got)&&got.handle==3); // smallest fit, then newest
 assert(got.mapping==(void*)1&&got.token==7&&got.iova==3*4096); // whole allocation and lease retained
 assert(darwin_cache_get(&r,100,false,true,&got)&&got.handle==1);
 assert(darwin_cache_get(&r,95,true,true,&got));assert(got.handle==4); // access class exact
 assert(!darwin_cache_get(&r,95,false,true,&got)); // 120 is more than 25% larger
 assert(darwin_cache_get(&r,100,false,false,&got)&&got.handle==5);
 assert(darwin_cache_get(&r,100,false,true,&got)&&got.handle==2);
 assert(r.cache_count==0&&r.cache_bytes==0);
 // Refused release retains both entry and byte accounting, then a later trim succeeds.
 put(d,r,6,128);clock_s+=3;refuse=true;darwin_cache_trim(&d,&r);assert(r.cache_count==1&&r.cache_bytes==128&&freed.empty());
 refuse=false;++clock_s;darwin_cache_trim(&d,&r);assert(r.cache_count==0&&freed==std::vector<uint64_t>{6});
 put(d,r,7,128);++darwin_memory_pressure;darwin_cache_trim(&d,&r);assert(r.cache_count==0&&freed.back()==7);
 // Dynamic growth retains all entries without losing a handle.
 for(uint64_t id=10;id<210;++id)put(d,r,id,64);
 assert(r.cache_count==200&&r.cache_capacity>=200);assert(darwin_cache_flush(&d,&r));assert(!darwin_cache_flush(&d,&r));
 assert(r.cache_count==0&&r.cache_bytes==0&&freed.size()==202);free(r.cache);
 puts("PASS: active whole-BO cache fit/lease isolation, expiry, pressure, refused-release ownership and dynamic growth; no GPU");
}
'''
with tempfile.TemporaryDirectory(prefix='houji-current-bo-cache-') as tmp:
    p=Path(tmp);(p/'check.cpp').write_text(unit)
    subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined',str(p/'check.cpp'),'-o',str(p/'check')],check=True)
    subprocess.run([str(p/'check')],check=True,timeout=20)
