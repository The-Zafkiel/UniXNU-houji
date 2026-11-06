#!/usr/bin/env python3
"""Exercise the production full-system ADT builder and applied UEFI profile."""
import copy
import importlib.util
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from ios26_adt import Node, parse, serialize

spec = importlib.util.spec_from_file_location('system_adt', ROOT / 'tools/make-ios26-system-adt.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


def node(name, properties=(), children=()):
    return Node([('name', 0, name.encode() + b'\0'), *[(k, 0, v) for k, v in properties]], list(children))


armio = node('arm-io', [('ranges', struct.pack('<QQQ', 0, 0x1000000, 0xff000000))], [
    node('gic', [('reg', struct.pack('<QQQQ', 0x16100000, 0x10000, 0x16180000, 0x200000)),
                 ('redistributor-stride', struct.pack('<Q', 0x40000))]),
    node('retained-device', [('sentinel', b'unchanged')]),
])
cpus = node('cpus', children=[node('cpu' + str(i), [
    ('reg', struct.pack('<I', i << 8)), ('cluster-id', struct.pack('<I', i % 2)),
    ('cluster-type', b'P\0'), ('cluster-core-id', struct.pack('<I', i)),
    ('small-core', bytes(4)), ('cpu-sentinel', b'retained'),
]) for i in range(8)])
chosen = node('chosen', [('houji,gpu-power-handoff', bytes(1376)),
                        ('houji,gpu-zap-reservation', bytes(512)),
                        ('local-sentinel', b'retained')], [node('memory-map', [
                            ('RAMDisk', bytes(16)), ('TrustCache', bytes(16)),
                            ('PackedRAMDisk', bytes(16)), ('retained-map', bytes(16)),
                        ])])
tree = node('device-tree', [('compatible', b'xiaomi,houji\0')], [armio, cpus, chosen])
donor = {'device-tree': {'model': 'iPhone18,1', 'target-type': 'V53',
                       'compatible': ['V53AP', 'iPhone18,1', 'AppleARM'],
                       'serial-number': 'must-never-be-copied'}}
raw = serialize(tree)
first, second = [parse(builder.prepare(raw, donor)) for _ in range(2)]
assert serialize(first.child('arm-io')) == serialize(armio)
assert first.get('model') == b'iPhone18,1\0' and first.get('serial-number') is None
assert first.child('chosen').get('local-sentinel') == b'retained'
assert first.child('chosen').get('darwinboot-memory-size') == struct.pack('<Q', 8 << 30)
for name, length in [('random-seed', 256), ('houji,pac-entropy', 80)]:
    a, b = first.child('chosen').get(name), second.child('chosen').get(name)
    assert len(a) == len(b) == length and a != b
for i, cpu in enumerate(first.child('cpus').children):
    assert cpu.get('reg') == struct.pack('<I', i << 8)
    assert cpu.get('state') == (b'running\0' if i == 0 else b'waiting\0')
    assert cpu.get('cpu-sentinel') == b'retained'
    assert all(cpu.get(k) is None for k in ('cluster-id', 'cluster-type', 'cluster-core-id', 'small-core'))
memory = first.child('chosen').child('memory-map')
assert all(memory.get(k) is None for k in ('RAMDisk', 'PackedRAMDisk', 'TrustCache'))
assert memory.get('retained-map') == bytes(16)


def rejected(changed, identity=donor):
    try:
        builder.prepare(serialize(changed), identity)
    except ValueError:
        return
    raise AssertionError('Unsafe full-system handoff accepted')


bad = copy.deepcopy(tree);bad.child('cpus').children.pop();rejected(bad)
bad = copy.deepcopy(tree);bad.child('cpus').child('cpu7').set('reg', bytes(4));rejected(bad)
bad = copy.deepcopy(tree);bad.child('arm-io').child('gic').set('redistributor-stride', bytes(8));rejected(bad)
bad = copy.deepcopy(tree);bad.child('chosen').set('houji,gpu-zap-reservation', b'x' * 512);rejected(bad)
bad = copy.deepcopy(tree);bad.child('chosen').set('houji,gpu-power-handoff', bytes(1375));rejected(bad)
rejected(tree, {'device-tree': {**donor['device-tree'], 'model': 'iPhone18,2'}})

with tempfile.TemporaryDirectory(prefix='houji26-system-') as temporary:
    temporary = Path(temporary)
    adt = temporary / 'system.adt';adt.write_bytes(serialize(first))
    subprocess.run([sys.executable, str(ROOT / 'checks/ios26-entry.py'), '--adt', str(adt)], check=True)
    relative = Path('Platforms/Xiaomi/houjiPkg/Application/HoujiStorageProbe/HoujiStorageProbe.c')
    source = temporary / relative;source.parent.mkdir(parents=True)
    source.write_bytes((ROOT / 'uefi/overlay' / relative).read_bytes())
    subprocess.run(['git', 'apply', str(ROOT / 'uefi/ios26/patches/local.patch')], cwd=temporary, check=True)
    content = source.read_text()
    args = re.search(r'CONST CHAR16 \*BaseBootArgs = L"([^"\n]+)";', content)[1]
    tokens = args.split();names = [v.split('=', 1)[0] for v in tokens]
    assert len(set(names)) == len(names) and len(args + ' houji_touch=1') < 1024
    assert 'CHAR16 BootArgs[1024]' in content and 'StrLen(L" houji_touch=1")' in content
    assert all(v in tokens for v in ('cpus=8', 'serial=0', 'houji_native_root=1', 'houji_ufs_log=1',
                                   'houji_return_recovery=1', 'houji_trial_seconds=300',
                                   'houji_gpu_runtime=5', 'gtrace=0', 'sweetsample=-', 'sweetiolog=-'))
    assert 'mTouchHandoffReady?1:0' in content
    assert content.index('Status = RunLogFsTest (ImageHandle);') < content.index('Houji26StartupFailureReset (Status, StartupRecoveryReady)') < content.index('Status=GpuPowerHandoffPrepare(ImageHandle);')
    executable = temporary / 'startup-recovery'
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I', str(ROOT / 'uefi/ios26/overlay' / relative.parent),
                    '-I', str(ROOT / 'uefi/overlay' / relative.parent),
                    str(ROOT / 'checks/ios26-startup.c'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print('PASS: production full-system ADT preserves houji hardware, validates CPU/GPU slots and fresh entropy; applied UEFI uses Darwin 25 bounds and mandatory startup gates; no hardware emulation')
print('PASS: production early-failure recovery requires exact live BCB readback, preserves status, waits ten seconds, retains handlers and performs no writes; host callbacks only')
