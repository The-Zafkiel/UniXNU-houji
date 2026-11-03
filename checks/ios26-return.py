#!/usr/bin/env python3
"""Exercise the production one-attempt boot policy with firmware peers."""
from pathlib import Path
import subprocess
import importlib.util
import tomllib
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ios26_payload', ROOT / 'tools/make-ios26-first-entry.py')
payload = importlib.util.module_from_spec(spec)
spec.loader.exec_module(payload)
arguments = '-v cpus=1 rd=md0 houji_native_root=0 houji_return_recovery=1'
config = tomllib.loads(payload.first_entry_config(arguments))
assert config['boot']['timeout'] == 3 and config['boot']['fallback_timeout'] == 5
assert config['boot']['on_return'] == 'reset' and config['boot']['return_timeout'] == 10
for bad in (arguments + ' cpus=8', arguments + ' gpu=1', arguments + ' dadagpu=6',
            arguments.replace('cpus=1', 'cpus=8')):
    try:
        payload.first_entry_config(bad)
    except ValueError:
        pass
    else:
        raise AssertionError('Invalid first-entry arguments were accepted')
with tempfile.TemporaryDirectory(prefix='houji26-return-') as tmp:
    binary = Path(tmp) / 'policy'
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I', str(ROOT / 'bootloader/ios26/manager/overlay/package/UniXNUPkg/Library/UniXNULib'),
                    str(ROOT / 'checks/ios26-return.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
