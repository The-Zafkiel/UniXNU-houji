/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <type_traits>
static int nextToken;
static bool held;
static unsigned acquired,released;
extern "C" int native_adreno_present_begin_frame(uint32_t fence) {
    assert((fence==42 || fence==0) && !held);++acquired;held=nextToken>0;return nextToken;
}
extern "C" uint32_t native_adreno_present_capture_fence(void) { return 42; }
extern "C" void native_adreno_present_end(void) { assert(held);held=false;++released; }
#include "HoujiPresent26.h"
struct Cache {
    unsigned attr[3]={0xff,0x44,0xff};
    unsigned barriers=0;
    std::vector<uintptr_t> cleaned;
    unsigned attribute(uintptr_t address) { assert(address>=0x4000 && address<0x10000);return attr[(address-0x4000)/16384]; }
    void cleanInvalidate(uintptr_t address) { assert(held && !(address&63));cleaned.push_back(address); }
    void barrier() { assert(held);++barriers; }
};
int main() {
    static_assert(!std::is_copy_constructible<HoujiPresentLease26>::value,"unique gate ownership");
    for(int token:{-1,0,1}) for(uint32_t fence:{0U,42U}) {
        nextToken=token;acquired=released=0;
        {
            HoujiPresentLease26 lease(fence);
            assert(lease.ready()==(token>0 || (token==0 && fence==0)) && lease.gpu()==(token>0));
            assert(held==(token>0) && !released);
        }
        assert(!held && acquired==1 && released==unsigned(token>0));
    }
    nextToken=1;
    {
        HoujiPresentLease26 lease(42);Cache cache;
        assert(houji26AcquirePixels(0x4000,49152,0x7ff0,48,64,16384,cache));
        assert(cache.cleaned==std::vector<uintptr_t>{0x7fc0} && cache.barriers==1);
        Cache bad;bad.attr[1]=0;
        assert(!houji26AcquirePixels(0x4000,49152,0x7ff0,48,64,16384,bad));
        assert(bad.cleaned.empty() && !bad.barriers);
        for(auto &attribute:bad.attr)attribute=0x44;
        assert(houji26AcquirePixels(0x4000,49152,0x4000,49152,64,16384,bad));
        assert(bad.cleaned.empty() && bad.barriers==1);
        for(unsigned line:{0,1,48,4096}) {
            Cache invalid;assert(!houji26AcquirePixels(0x4000,49152,0x7ff0,48,line,16384,invalid));
            assert(invalid.cleaned.empty() && !invalid.barriers);
        }
        Cache bounds;
        assert(!houji26AcquirePixels(0x4001,63,0x4001,1,64,16384,bounds));
        assert(!houji26AcquirePixels(0x4000,49152,0xffff,2,64,16384,bounds));
        assert(!houji26AcquirePixels(UINTPTR_MAX-8,16,UINTPTR_MAX-8,8,64,16384,bounds));
        assert(bounds.cleaned.empty() && !bounds.barriers);
    }
    assert(!held);
    puts("PASS: actual iOS26 present lease and CPU16K cache acquisition: fence ownership, WB/NC views, late Device/fault rejection, bounds, overflow and unlock; no GPU");
}
