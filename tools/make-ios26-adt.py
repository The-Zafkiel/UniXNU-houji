#!/usr/bin/env python3
"""Prepare a RAM-only first-entry ADT from the owner's proven houji handoff."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
from ios26_adt import Node, parse, serialize


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--window-gib', type=int, choices=(2, 8), default=2,
                        help='Owned bootstrap RAM window; the minimal probe needs only 2 GiB')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output exists')
    raw = args.input.read_bytes()
    tree = parse(raw)
    if serialize(tree) != raw:
        raise ValueError('Source ADT does not round-trip')
    if b'xiaomi,houji\0' not in (tree.get('compatible') or b''):
        raise ValueError('Not a houji handoff')
    armio = tree.child('arm-io')
    if armio.get('ranges') != struct.pack('<QQQ', 0, 0x1000000, 0xff000000):
        raise ValueError('Houji SoC window mismatch')
    gic = armio.child('gic')
    if gic.get('reg') != struct.pack('<QQQQ', 0x16100000, 0x10000, 0x16180000, 0x200000):
        raise ValueError('Houji GIC ranges mismatch')
    if gic.get('redistributor-stride') != struct.pack('<Q', 0x40000):
        raise ValueError('Houji GIC stride mismatch')
    cpus = tree.child('cpus')
    boot = cpus.child('cpu0')
    if boot.get('reg') != bytes(4):
        raise ValueError('Unexpected boot CPU affinity')
    cpus.children = [boot]
    boot.properties = [(key, flags, value) for key, flags, value in boot.properties
                       if key not in ('cluster-id', 'cluster-type', 'cluster-core-id', 'small-core')]
    boot.set('state', b'running\0')
    chosen = tree.child('chosen')
    removed = []
    for key, flags, value in chosen.properties:
        if key.startswith(('houji,gpu-', 'mac-address-')) or key in ('unique-chip-id', 'houji,platform-uuid'):
            removed.append(key)
    chosen.properties = [item for item in chosen.properties if item[0] not in removed]
    chosen.set('dram-base', bytes(8)); chosen.set('dram-size', bytes(8))
    chosen.set('darwinboot-memory-size', struct.pack('<Q', args.window_gib << 30))
    chosen.set('hakios,secondary-entry', bytes(8))
    chosen.set('firmware-version', b'houji-ios26-arm64e\0')
    chosen.set('random-seed', os.urandom(256))
    maps = [item for item in chosen.children if item.name == 'memory-map']
    if not maps:
        memory = Node([('name', 0, b'memory-map\0')])
        chosen.children.append(memory)
    elif len(maps) == 1:
        memory = maps[0]
    else:
        raise ValueError('Duplicate memory-map')
    memory.set('RAMDisk', bytes(16))
    blob = serialize(tree)
    assert serialize(parse(blob)) == blob
    assert serialize(parse(raw).child('arm-io')) == serialize(armio), 'Hardware nodes changed'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(blob)
    receipt = {'profile': 'arm64e-first-entry', 'cpus': 1, 'CPU_pages': 16384,
               'memory_bytes': args.window_gib << 30, 'arm_io_preserved': True,
               'GPU_requested': False, 'UFS_requested': False, 'hardware_verified': False,
               'input_sha256': hashlib.sha256(raw).hexdigest(),
               'output_sha256': hashlib.sha256(blob).hexdigest(),
               'removed_chosen_properties': removed, 'random_seed_generated': True}
    args.output.with_suffix('.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('Prepared houji arm64e first-entry ADT: one CPU, 16 KiB pages, unchanged hardware nodes')


if __name__ == '__main__':
    main()
