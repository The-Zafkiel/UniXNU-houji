#!/usr/bin/env python3
"""Build a RAM-only arm64e kernel/PID1 probe and round-trip all FAT files; never opens a device."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib


def first_entry_config(boot_args):
    tokens = boot_args.split()
    names = [token.split('=', 1)[0] for token in tokens]
    if len(names) != len(set(names)):
        raise ValueError('Duplicate boot arguments are ambiguous')
    required = ('cpus=1', 'rd=md0', 'houji_native_root=0', 'houji_return_recovery=1')
    if not all(token in tokens for token in required):
        raise ValueError('First-entry requires one CPU, RAM root and bounded recovery')
    if any(name.startswith(('houji_gpu_', 'dadagpu', 'dadaadsp', 'dadatouch')) or
           name in ('gpu', 'adsp', 'dadascan', 'apfs_volume') for name in names):
        raise ValueError('No peripheral driver boot arguments in the first-entry profile')
    if len(boot_args.encode()) > 1023 or '\0' in boot_args or '\n' in boot_args:
        raise ValueError('Unexpected boot arguments')
    config = '''version = 1
[boot]
default = "xnu"
timeout = 3
fallback_timeout = 5
on_return = "reset"
return_timeout = 10
rotate = 0
[ui]
title = "Xiaomi 14 iOS26 kernel first entry"
[[driver]]
path = "/Drivers/DarwinBoot.efi"
connect = true
[[entry]]
id = "xnu"
title = "Darwin 25 arm64e - one CPU - RAM only"
type = "xnu"
path = "/Payloads/kernel.macho"
args = "-v serial=0 debug=0x14e keepsyms=1 cpus=1"
initrd = "/Payloads/hello.macho"
[[entry]]
id = "restart"
title = "Restart to prearmed Recovery"
type = "action"
action = "reset"
'''
    config = config.replace('args = "-v serial=0 debug=0x14e keepsyms=1 cpus=1"',
                            'args = ' + json.dumps(boot_args))
    return config


def main():
    from ios26_adt import parse
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--inputs', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--boot-args', default='-v serial=0 debug=0x14e keepsyms=1 cpus=1 rd=md0 houji_native_root=0 houji_return_recovery=1 houji_trial_seconds=90 sweetiolog=-')
    a = p.parse_args()
    if len(a.boot_args.encode()) > 1023 or '\0' in a.boot_args or '\n' in a.boot_args:
        raise ValueError('Unexpected boot arguments')
    kernel = (a.inputs / 'kernel.macho').read_bytes()
    magic, cpu, subtype = struct.unpack_from('<III', kernel)
    if magic != 0xfeedfacf or cpu != 0x100000c or subtype & 0xffffff != 2:
        raise ValueError('Expected an arm64e Mach-O')
    if b'HUPACFG1' not in kernel or b'HOUJI26:' not in kernel:
        raise ValueError('Unexpected kernel profile')
    if a.output.exists():
        raise ValueError('Output exists')
    for tool in ('mformat', 'mmd', 'mcopy'):
        if shutil.which(tool) is None:
            raise ValueError('Missing build dependency: ' + tool)
    tree = parse((a.inputs / 'houji.adt').read_bytes())
    entropy = tree.child('chosen').get('houji,pac-entropy')
    if entropy is None or len(entropy) != 80 or any(not any(entropy[i:i+16]) for i in range(0, 80, 16)):
        raise ValueError('Generate a fresh ADT with independent one-use PAC entropy')
    if not all(token in a.boot_args.split() for token in ('cpus=1', 'rd=md0', 'houji_native_root=0', 'houji_return_recovery=1')) or 'houji_gpu_' in a.boot_args:
        raise ValueError('First-entry profile must be one CPU, RAM root, bounded recovery, no GPU')
    hello = (a.inputs / 'hello.macho').read_bytes()
    _, _, hello_subtype = struct.unpack_from('<III', hello)
    if hello_subtype & 0xffffff != 2 or b'HOUJI IOS26 ARM64E PID1 REACHED' not in hello:
        raise ValueError('Expected the local static arm64e PID1 probe')
    config = first_entry_config(a.boot_args)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with a.output.open('xb') as f:
        os.chmod(a.output, 0o600)
        f.truncate(64 * 1024 * 1024)
    def run(tool, *args):
        subprocess.run([tool, '-i', str(a.output), *map(str, args)], check=True)
    run('mformat', '-F', '-v', 'HOUJIESP', '-N', '0x484f554a', '::')
    run('mmd', '::/EFI', '::/EFI/UniXNU', '::/Payloads', '::/Drivers')
    files = {'Payloads/kernel.macho': a.inputs / 'kernel.macho',
             'Payloads/kernel.macho.adt': a.inputs / 'houji.adt',
             'Payloads/hello.macho': a.inputs / 'hello.macho',
             'Drivers/DarwinBoot.efi': a.inputs / 'DarwinBoot.efi'}
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        (temp / 'unixnu.toml').write_text(config)
        files['EFI/UniXNU/unixnu.toml'] = temp / 'unixnu.toml'
        hashes = {}
        for name, source in files.items():
            run('mcopy', '-o', source, '::/' + name)
            destination = temp / ('roundtrip-' + source.name)
            run('mcopy', '-o', '::/' + name, destination)
            if source.read_bytes() != destination.read_bytes():
                raise ValueError('RAM payload round-trip failed: ' + name)
            hashes[name] = hashlib.sha256(destination.read_bytes()).hexdigest()
    data = a.output.read_bytes()
    if data[71:82] != b'HOUJIESP   ' or data[82:90] != b'FAT32   ':
        raise ValueError('Unexpected FAT boot sector')
    report = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
              'crc32': f'{zlib.crc32(data):08x}', 'files': hashes,
              'contains_xnu': True, 'physical_storage': False, 'hardware_tested': False,
              'architecture': 'arm64e', 'GUI_probe': False, 'GPU_probe': False,
              'PAC_entropy': 'independent host CSPRNG; consumed and erased by loader',
              'single_boot_only': True,
              'test_scope': 'Native kernel and static PID1 only; no physical root or graphics',
              'storage_write_policy': 'No UFS access; recovery reset performs no writes'}
    a.output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
