#!/usr/bin/env python3
"""Prepare the eight-CPU HFS-root ADT; does not bind or boot a device trial."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
from ios26_adt import parse, serialize


def prepare(raw, donor):
    tree = parse(raw)
    if serialize(tree) != raw or b'xiaomi,houji\0' not in (tree.get('compatible') or b''):
        raise ValueError('Expected a round-tripping houji handoff')
    armio = tree.child('arm-io')
    hardware = serialize(armio)
    if armio.get('ranges') != struct.pack('<QQQ', 0, 0x1000000, 0xff000000):
        raise ValueError('Houji SoC window mismatch')
    gic = armio.child('gic')
    if (gic.get('reg') != struct.pack('<QQQQ', 0x16100000, 0x10000, 0x16180000, 0x200000) or
            gic.get('redistributor-stride') != struct.pack('<Q', 0x40000)):
        raise ValueError('Houji GIC ranges/stride mismatch')
    identity = donor['device-tree']
    if (identity.get('model') != 'iPhone18,1' or identity.get('target-type') != 'V53' or
            identity.get('compatible') != ['V53AP', 'iPhone18,1', 'AppleARM']):
        raise ValueError('Expected the pinned v53ap userland donor')
    # Only non-unique userland identity is taken from the donor. Physical nodes,
    # reserved-memory records and all local device identities stay on houji.
    tree.set('model', b'iPhone18,1\0')
    tree.set('target-type', b'V53\0')
    tree.set('compatible', b'V53AP\0iPhone18,1\0xiaomi,houji\0qcom,sm8650\0AppleARM\0')
    cpus = tree.child('cpus')
    if len(cpus.children) != 8:
        raise ValueError('Expected all eight houji CPUs')
    for number, cpu in enumerate(cpus.children):
        if cpu.name != 'cpu' + str(number) or cpu.get('reg') != struct.pack('<I', number << 8):
            raise ValueError('Unexpected houji CPU affinity')
        cpu.properties = [item for item in cpu.properties
                          if item[0] not in ('cluster-id', 'cluster-type', 'cluster-core-id', 'small-core')]
        cpu.set('state', b'running\0' if number == 0 else b'waiting\0')
    chosen = tree.child('chosen')
    for name, length in [('houji,gpu-power-handoff', 1376), ('houji,gpu-zap-reservation', 512)]:
        value = chosen.get(name)
        if value is None or len(value) != length or any(value):
            raise ValueError('GPU handoff must be an empty, correctly sized loader slot')
    chosen.set('dram-base', bytes(8))
    chosen.set('dram-size', bytes(8))
    chosen.set('darwinboot-memory-size', struct.pack('<Q', 8 << 30))
    chosen.set('hakios,secondary-entry', bytes(8))
    chosen.set('houji,smp-enabled', struct.pack('<I', 1))
    chosen.set('firmware-version', b'houji-ios26-arm64e\0')
    chosen.set('random-seed', os.urandom(256))
    chosen.set('houji,pac-entropy', os.urandom(80))
    memory = chosen.child('memory-map')
    memory.properties = [item for item in memory.properties
                         if item[0] not in ('RAMDisk', 'PackedRAMDisk', 'TrustCache')]
    result = serialize(tree)
    if serialize(parse(result)) != result or serialize(armio) != hardware:
        raise ValueError('ADT changed physical hardware nodes or failed to round-trip')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--donor-json', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    os.umask(0o077)
    receipt = args.output.with_suffix('.json')
    if args.output.exists() or receipt.exists():
        parser.error('Use fresh output and receipt paths')
    raw = args.input.read_bytes()
    result = prepare(raw, json.loads(args.donor_json.read_text()))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('xb') as stream:
        stream.write(result)
    with receipt.open('x') as stream:
        json.dump({'profile': 'arm64e-full-hfs-root', 'cpus': 8, 'CPU_pages': 16384,
                   'memory_bytes': 8 << 30, 'arm_io_preserved': True,
                   'input_sha256': hashlib.sha256(raw).hexdigest(),
                   'output_sha256': hashlib.sha256(result).hexdigest(),
                   'single_boot_only': True, 'hardware_verified': False,
                   'requires_fresh_trial_binding': True, 'device_boot_eligible': False}, stream, indent=2)
        stream.write('\n')
    print('Prepared eight-CPU iOS26 ADT; requires fresh UFS/GPU binding before any boot')


if __name__ == '__main__':
    main()
