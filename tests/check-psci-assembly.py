#!/usr/bin/env python3
"""Compile the real PSCI call for Darwin arm64; require both barrier and SMC opcodes."""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

P=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--kernel',type=Path,help='Also inspect the linked kernel function')
args=parser.parse_args()
source=(P/'native/smp/SweetCPU.cpp').read_text()
begin=source.index('  register uint64_t x0 __asm__("x0") = 0xc4000003ULL;')
end=source.index('\n  IOLog("HOUJI SMP: PSCI CPU_ON returned',begin)
body=source[begin:end]
program=('typedef unsigned long long uint64_t;\n'
         'extern "C" uint64_t test_cpu_on(unsigned physicalID,uint64_t entry,uint64_t context) {\n'
         +body+'\n  return x0;\n}\n')
with tempfile.TemporaryDirectory() as directory:
    d=Path(directory)
    for name,text,expected in [('fixed',program,True),
                               ('regression',program.replace('dsb sy\\n\\tsmc #0','dsb sy; smc #0'),False)]:
        file=d/(name+'.cpp');obj=d/(name+'.o');file.write_text(text)
        subprocess.run(['xcrun','clang++','-target','arm64-apple-macos14',
                        '-ffreestanding','-O2','-Wall','-Wextra','-Werror',
                        '-c',str(file),'-o',str(obj)],check=True)
        asm=subprocess.check_output(['xcrun','llvm-objdump','-d',str(obj)],text=True)
        found=bool(re.search(r'd4000003\s+smc\s+#0',asm))
        assert found==expected,(name,asm)
        assert 'd5033f9f' in asm and 'dsb\tsy' in asm
        if expected:
            assert asm.index('d5033f9f')<asm.index('d4000003')
    print('PASS: Darwin arm64 emits DSB then SMC; original semicolon regression detected')
if args.kernel:
    symbols=subprocess.check_output(['xcrun','nm',str(args.kernel)],text=True)
    parsed=[(int(m[1],16),m[2]) for line in symbols.splitlines()
            if (m:=re.fullmatch(r'([0-9a-f]+) [SsTt] (\S+)',line))]
    start=next(address for address,name in parsed if name=='__ZN8SweetCPU8startCPUEmm')
    stop=min(address for address,name in parsed if address>start)
    asm=subprocess.check_output(['xcrun','llvm-objdump','-d',
        '--start-address='+hex(start),'--stop-address='+hex(stop),str(args.kernel)],text=True)
    assert len(re.findall(r'd4000003\s+smc\s+#0',asm))==1,asm
    assert asm.index('d5033f9f')<asm.index('d4000003')
    print('PASS: linked SweetCPU::startCPU contains exactly one real SMC after DSB')
    start=next(address for address,name in parsed if name=='__ZN8SweetCPU7initCPUEb')
    stop=min(address for address,name in parsed if address>start)
    asm=subprocess.check_output(['xcrun','llvm-objdump','-d',
        '--start-address='+hex(start),'--stop-address='+hex(stop),str(args.kernel)],text=True)
    assert not re.search(r'<_(IOLog|printf|kprintf|console_write)>',asm),asm
    print('PASS: linked secondary initCPU has no direct console output; diagnostics deferred')
    if any(name=='_houji_smp_capture_gic' for address,name in parsed):
        start=next(address for address,name in parsed if name=='_houji_smp_capture_gic')
        stop=min(address for address,name in parsed if address>start)
        asm=subprocess.check_output(['xcrun','llvm-objdump','-d',
            '--start-address='+hex(start),'--stop-address='+hex(stop),str(args.kernel)],text=True)
        assert not re.search(r'<_(IOLog|printf|kprintf|console_write)>',asm),asm
        print('PASS: masked-IRQ GIC capture contains no direct console output')
