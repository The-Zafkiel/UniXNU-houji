#!/usr/bin/env python3
"""Exercise production entropy selection and ADT input validation under ASan/UBSan."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from ios26_adt import Node, serialize

with tempfile.TemporaryDirectory(prefix='houji26-entropy-') as temporary:
    tmp = Path(temporary)
    def compile_source(name):
        binary = tmp / name
        subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-fno-omit-frame-pointer',
                        '-I', str(ROOT / 'bootloader/ios26/overlay/DarwinBootPkg/Driver'),
                        '-I', str(ROOT / 'checks/ios26-uefi'),
                        str(ROOT / 'checks' / (name + '.c')), '-o', str(binary)], check=True)
        return binary
    subprocess.run([str(compile_source('ios26-entropy'))], check=True)
    entry = compile_source('ios26-entry')

    def fixture(entropy=None, wrong_node=False):
        chosen = Node([('name', 0, b'chosen\0'), ('dram-base', 0, bytes(8)),
                       ('dram-size', 0, bytes(8)), ('hakios,secondary-entry', 0, bytes(8)),
                       ('darwinboot-memory-size', 0, struct.pack('<Q', 2 << 30))])
        root = Node([('name', 0, b'device-tree\0')], [chosen])
        if entropy is not None:
            (root if wrong_node else chosen).set('houji,pac-entropy', entropy)
        return root

    cases = [
        ('absent', serialize(fixture()), True),
        ('valid', serialize(fixture(bytes(range(1, 81)))), True),
        ('short', serialize(fixture(bytes(79))), False),
        ('long', serialize(fixture(bytes(81))), False),
        ('wrong-parent', serialize(fixture(bytes(80), True)), False),
    ]
    tree = fixture(bytes(80))
    tree.children.append(tree.children[0])
    cases.append(('duplicate-chosen', serialize(tree), False))
    # Bypass the fixture codec's duplicate protection to test firmware's parser.
    tree = fixture(bytes(80))
    chosen = serialize(tree.children[0])
    prop = struct.pack('<32sI', b'houji,pac-entropy', 80) + bytes(80)
    chosen = struct.pack('<I', 7) + chosen[4:] + prop
    root_prefix = serialize(Node([('name', 0, b'device-tree\0')]))
    duplicate = root_prefix[:4] + struct.pack('<I', 1) + root_prefix[8:] + chosen
    cases.append(('duplicate-property', duplicate, False))
    valid = serialize(fixture(bytes(range(1, 81))))
    for cut in range(len(valid)):
        if cut:
            cases.append((f'truncated-{cut}', valid[:cut], False))
    for name, data, expected in cases:
        path = tmp / (name + '.adt')
        path.write_bytes(data)
        result = subprocess.run([str(entry), '--adt', str(path)])
        assert result.returncode == (0 if expected else 1), name
    print('PASS: production ADT entropy parsing: path, size, duplicates and all truncated boundaries')
