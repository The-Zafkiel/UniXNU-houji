#!/usr/bin/env python3
"""Build a local static arm64e ABI probe, without accessing a device."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if args.output.exists():
    parser.error('Output exists')
args.output.parent.mkdir(parents=True, exist_ok=True)
command = ['xcrun', '--sdk', 'iphoneos', 'clang', '-arch', 'arm64e',
           '-miphoneos-version-min=26.0', '-Os', '-static', '-nostdlib',
           '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
           '-Wl,-e,_start', '-Wl,-pagezero_size,0x100000000',
           str(ROOT / 'userland/ios26/first-entry.c'), '-o', str(args.output)]
subprocess.run(command, check=True)
subprocess.run(['codesign', '--force', '--sign', '-', str(args.output)], check=True)
data = args.output.read_bytes()
magic, cpu, subtype = struct.unpack_from('<III', data)
if magic != 0xfeedfacf or cpu != 0x100000c or subtype & 0xffffff != 2:
    raise ValueError('Expected an arm64e Mach-O')
args.output.with_suffix('.json').write_text(json.dumps({
    'architecture': 'arm64e', 'sha256': hashlib.sha256(data).hexdigest(),
    'bytes': len(data), 'source_sha256': hashlib.sha256((ROOT / 'userland/ios26/first-entry.c').read_bytes()).hexdigest(),
    'contains_apple_binaries': False, 'device_accessed': False,
    'GUI_probe': False, 'GPU_probe': False,
}, indent=2) + '\n')
print('Built static arm64e PID1 probe; this does not qualify a system boot')
