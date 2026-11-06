#!/usr/bin/env python3
"""Exercise the actual bounded startup writer with recording host callbacks."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'kernel/ios26/overlay/iokit/Kernel/arm/HoujiPlatform26.cpp').read_text()
start = source.index('/* Optional, finite PID1 bring-up results.')
end = source.index('extern "C" uint64_t houji_cpu_idle_mach', start)
unit = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using thread_t = void *;
using wait_result_t = int;
constexpr thread_t THREAD_NULL = nullptr;
constexpr int KERN_SUCCESS = 0;
using entry_t = void (*)(void *, wait_result_t);
static int enabled, native_root, recovery, panic_state, thread_error;
static bool interruptible = true;
static unsigned thread_calls, log_calls;
static entry_t queued;
static std::vector<std::string> writes;
static bool PE_parse_boot_argn(const char *name, void *value, int size) {
    assert(size == sizeof(int));
    int result = !strcmp(name,"houji_pid1_startup") ? enabled :
        !strcmp(name,"houji_native_root") ? native_root : recovery;
    memcpy(value, &result, sizeof result); return true;
}
static unsigned panic_active() { return panic_state; }
static bool ml_get_interrupts_enabled() { return interruptible; }
static void IOLog(const char *, ...) { ++log_calls; }
extern "C" void houji_ufs_checkpoint(const char *text, unsigned stop) {
    assert(stop == 0 && !panic_state && interruptible);
    writes.emplace_back(text); assert(writes.back().size() < 768);
}
static int kernel_thread_start(entry_t entry, void *, thread_t *thread) {
    ++thread_calls; if(thread_error) return thread_error;
    queued = entry; *thread = (void *)1; return 0;
}
static void thread_deallocate(thread_t thread) { assert(thread == (void *)1); }
'''
unit += source[start:end]
unit += r'''
static void reset() {
    enabled = native_root = recovery = 1; panic_state = thread_error = 0;
    interruptible = true; thread_calls = log_calls = bootResults = bootMainSeen = 0;
    queued = nullptr; writes.clear();
}
int main() {
    for(unsigned gate=0; gate<5; ++gate) {
        reset();
        if(gate==0) enabled=0;
        if(gate==1) native_root=0;
        if(gate==2) recovery=0;
        if(gate==3) panic_state=1;
        if(gate==4) interruptible=false;
        houji26_boot_exec("/sbin/launchd",8,0);
        houji26_boot_reboot("launchd",8,"reason");
        houji26_boot_main();
        assert(writes.empty() && !log_calls && !thread_calls);
    }
    reset();
    houji26_boot_exec("/Applications/arbitrary",0,1);
    houji26_boot_exec("/sbin/launchd",0,-1);
    houji26_boot_exec("/sbin/launchd",0,6);
    assert(writes.empty());
    for(const char *path:{"/sbin/launchd","/redwood/sbin/init","/sbin/launchd.ios"}) {
        houji26_boot_exec(path,0,1);houji26_boot_exec(path,0,0);
    }
    assert(writes.size()==6);
    houji26_boot_main();
    assert(thread_calls==1 && queued && writes.size()==6);
    queued(nullptr,0);
    assert(writes.size()==7 && writes.back().find("stock-launchd-main")!=std::string::npos);
    houji26_boot_main();houji26_boot_exec("/sbin/launchd",0,1);
    assert(thread_calls==1 && writes.size()==7);
    std::string large(4096,'x');
    houji26_boot_reboot("launchd",8,large.c_str());
    for(unsigned i=0;i<20;++i) houji26_boot_reboot("launchd",8,nullptr);
    assert(writes.size()==8 && log_calls==8);
    assert(writes.back().find("result=8 main=1")!=std::string::npos);
    reset();houji26_boot_main();panic_state=1;queued(nullptr,0);
    assert(writes.empty());
    reset();thread_error=1;houji26_boot_main();assert(!queued && writes.empty());
    reset();
    for(int phase=2;phase<=5;++phase) houji26_boot_exec("/sbin/launchd",phase,phase);
    assert(writes.size()==4);
    assert(writes[0].find("pid1-clone-ready")!=std::string::npos);
    assert(writes[1].find("pid1-image-activated")!=std::string::npos);
    assert(writes[2].find("pid1-signature-checked")!=std::string::npos);
    assert(writes[3].find("pid1-exec-ready")!=std::string::npos);
}
'''
with tempfile.TemporaryDirectory(prefix='houji26-startup-results-') as name:
    directory = Path(name)
    cpp, binary = directory / 'check.cpp', directory / 'check'
    cpp.write_text(unit)
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PASS: actual startup writer requires all boot gates and normal context, bounds eight records, filters paths, defers main notification and preserves disabled/failed-worker behavior; host peers, no device or GPU')
