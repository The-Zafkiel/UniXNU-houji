#!/usr/bin/env python3
"""Reject stale trial bindings and malformed embedded shader lists offline."""
from pathlib import Path
import importlib.util
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('metal_builder', ROOT / 'tools/build-metal.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)

with tempfile.TemporaryDirectory(prefix='houji-inputs-') as directory:
    root = Path(directory)
    listing = root / 'prime.txt'
    listing.write_text('/System/Library/b.metallib\n/System/Library/a.metallib\n/System/Library/a.metallib\n')
    assert builder.read_primelist(listing) == '/System/Library/a.metallib\n/System/Library/b.metallib\n'
    for bad in ['', 'relative.metallib', '/System/../private/a.metallib', '/System/a\0.metallib', '/System/not-a-library']:
        listing.write_text(bad)
        try:
            builder.read_primelist(listing)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid shader list was accepted')
    headers = root / 'iokit/Kernel/houji-ufs-root'
    headers.mkdir(parents=True)
    def header(path, name, values):
        path.write_text('const unsigned char ' + name + '[16]={' + values + '};\n')
    log, handoff = headers / 'HoujiLogAllocation.h', headers / 'HoujiZapTrial.h'
    values = ','.join(str(i) for i in range(16))
    header(log, 'kHoujiLogRunId', values)
    command = [sys.executable, str(ROOT / 'tools/check-trial-bindings.py'), str(root)]
    for value, expected in [(values, 0), (','.join(['1'] * 16), 1), ('0,1', 1), ('256,' + values, 1)]:
        header(handoff, 'kHoujiZapTrial', value)
        result = subprocess.run(command, capture_output=True, text=True)
        assert result.returncode == expected, result.stderr
    handoff.unlink()
    assert subprocess.run(command, capture_output=True).returncode == 1
    # A runtime-only installation must not omit a resource sealed by codesign.
    # clang -g puts this directory beside the executable, inside the bundle.
    bundle = root / 'NativeAdrenoMetal.bundle'
    symbols = bundle / 'NativeAdrenoMetal.dSYM'
    symbols.mkdir(parents=True)
    (symbols / 'debug-data').write_bytes(b'new symbols')
    (bundle / 'NativeAdrenoMetal').write_bytes(b'unchanged executable')
    old = root / symbols.name
    old.mkdir()
    (old / 'stale').write_text('old symbols')
    builder.extract_debug_symbols(bundle, root)
    assert not symbols.exists() and not (old / 'stale').exists()
    assert (old / 'debug-data').read_bytes() == b'new symbols'
    assert (bundle / 'NativeAdrenoMetal').read_bytes() == b'unchanged executable'
    builder.extract_debug_symbols(bundle, root)
    # Compile the actual shared policy used by both discovery paths. Default
    # builds retain the BGRA sysmem guard; autotune is an explicit comparison.
    command = ['clang++', '-std=c++17', '-I', str(ROOT / 'graphics/metal/AdrenoMetal'),
               str(ROOT / 'checks/turnip-policy.cpp'), '-o', str(root / 'turnip-policy')]
    for define, flags, mode in [(None, 'sysmem,nocb', 'sysmem'), (1, 'nocb', 'autotune')]:
        subprocess.run(command + ([] if define is None else ['-DHOUJI_TURNIP_RENDER_AUTOTUNE=' + str(define)]), check=True)
        subprocess.run([str(root / 'turnip-policy'), flags, mode], check=True)
    assert subprocess.run(command + ['-DHOUJI_TURNIP_RENDER_AUTOTUNE=2'], capture_output=True).returncode != 0
    # Exercise the actual plugin's diagnostic setup, including trace-only and
    # pixel-only flags and inherited settings that must be cleared when off.
    plugin = (ROOT / 'graphics/metal/AdrenoMetal/NativeAdrenoMetalPlugin.mm').read_text()
    setup = plugin[plugin.index('        if(!HOUJI_METAL_QUIET) {'):plugin.index('        if (strcmp(getprogname(), "backboardd")')]
    fixture = r'''
#include <cassert>
#include <cstdlib>
#include <cstring>
#include "NativeTurnipPolicy.h"
static unsigned flags;
static bool bootArgSet(const char *name) {
 return !strcmp(name,"sheng-diag=1") ? flags&1 : !strcmp(name,"sheng-gpu-trace=1") ? flags&2 : !strcmp(name,"sheng-gpu-pixels=1") ? flags&4 : 0;
}
int main() {
 for(flags=0;flags<8;++flags) {
  setenv("SHENG_DIAG","inherited",1);
''' + setup + r'''
  assert((getenv("SHENG_DIAG")!=nullptr)==bool(!HOUJI_METAL_QUIET && (flags || HOUJI_GPU_STAGE_TRACE)));
  if(HOUJI_METAL_QUIET) {
   assert(!getenv("NATIVE_METAL_JOURNAL") && !getenv("SHENG_LOG_GPU"));
   assert(!strcmp(getenv("NATIVE_METAL_NO_LOG"),"1"));
  }
 }
}
'''
    source = root / 'diagnostics.cpp'; source.write_text(fixture)
    for enabled in (0, 1):
        for quiet in (0, 1):
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Werror', '-I', str(ROOT/'graphics/metal/AdrenoMetal'), '-DHOUJI_GPU_STAGE_TRACE='+str(enabled), '-DHOUJI_METAL_QUIET='+str(quiet), str(source), '-o', str(root/'diagnostics')], check=True)
            subprocess.run([str(root/'diagnostics')], check=True)
print('PASS: embedded shader lists are validated and stale/malformed/missing trial bindings stop the build')
print('PASS: debug symbols stay outside the signed runtime bundle, including rebuilds')
print('PASS: actual Turnip defaults, comparison policy, repeat setup and explicit environment; no GPU')
print('PASS: actual plugin diagnostic setup preserves diag/trace/pixel-only requests and clears inherited diagnostics; no GPU')
