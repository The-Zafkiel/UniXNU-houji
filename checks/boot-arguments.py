#!/usr/bin/env python3
"""Check the actual UEFI command line fits Darwin 22 without losing touch."""
from pathlib import Path
import ast,re

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'uefi/overlay/Platforms/Xiaomi/houjiPkg/Application/HoujiStorageProbe/HoujiStorageProbe.c').read_text()
base = re.search(r'BaseBootArgs = L"([^"]+)";', source).group(1)
capacity = int(re.search(r'CHAR16 BootArgs\[(\d+)\];', source).group(1))
template = re.search(r'UnicodeSPrint\(BootArgs,sizeof\(BootArgs\),L"([^"]+)"', source).group(1)
for mhz in (231, 500, 680, 903):
    candidate = re.sub(r'gpu_max_mhz=\d+', f'gpu_max_mhz={mhz}', base)
    assert candidate != base or mhz == 903
    for touch in (0, 1):
        command = template % (candidate, touch)
        assert len(command.encode('ascii')) + 1 <= min(capacity, 608)
        options = [word.split('=', 1)[0] for word in command.split()]
        assert len(options) == len(set(options)), 'duplicate boot argument'
        assert f'gpu_max_mhz={mhz}' in command.split()
        assert command.endswith(f'houji_touch={touch}')
assert 'StrLen(BaseBootArgs) + StrLen(L" houji_touch=1") >= ARRAY_SIZE(BootArgs)' in source
checker=ast.parse((ROOT/'tools/check-compiled-uefi-bootargs.py').read_text())
function=next(n for n in checker.body if isinstance(n,ast.FunctionDef) and n.name=='validate_gpu_startup_arguments')
scope={};exec(compile(ast.Module(body=[function],type_ignores=[]),'<actual boot prerequisite validator>','exec'),scope)
validate=scope['validate_gpu_startup_arguments'];validate(base.split())
healthy=['houji_gpu_external_canary_probe=1','houji_native_root=1','houji_ufs_log=1','houji_return_recovery=1']
validate(healthy+['gtrace=0'])
for name in ('houji_native_root','houji_ufs_log','houji_return_recovery'):
    for replacement in ('0',None):
        bad=[word for word in healthy if not word.startswith(name+'=')]
        if replacement is not None:bad.append(name+'='+replacement)
        try:validate(bad)
        except ValueError:pass
        else:raise AssertionError('Startup guard accepted missing/disabled prerequisite: '+name)
print('PASS: UEFI GPU ceiling and touch handoff fit the 608-byte Darwin 22 command line')
print('PASS: actual boot validator rejects disabled GPU startup prerequisites while permitting gtrace=0')
