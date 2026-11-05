#!/usr/bin/env python3
"""Compile the actual AArch64 entry assembly as COFF and verify its copied code."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def check_coff(data):
    machine, count, _, symbols_at, symbols_count, optional, _ = struct.unpack_from('<HHIIIHH', data)
    assert machine == 0xaa64 and optional == 0
    strings_at = symbols_at + 18 * symbols_count
    def name(raw):
        if raw[:4] == bytes(4):
            start = strings_at + struct.unpack_from('<I', raw, 4)[0]
            return data[start:data.index(b'\0', start)].decode()
        return raw.rstrip(b'\0').decode()
    symbols = {}
    index = 0
    while index < symbols_count:
        raw, value, section, _, _, aux = struct.unpack_from('<8sIhHBB', data, symbols_at + 18 * index)
        symbols[name(raw)] = (section, value)
        index += aux + 1
    start_section, start = symbols['DarwinSecondaryPacStart']
    key_section, key = symbols['DarwinSecondaryPacKeysAddress']
    end_section, end = symbols['DarwinSecondaryPacEnd']
    assert start_section > 0 and start_section == key_section == end_section, 'copied labels split across sections'
    assert 0 < end-start <= 16384 and 0 <= key-start <= end-start-8
    assert start % 16 == 0 and key % 8 == 0
    header = 20 + (start_section - 1) * 40
    size, at, _, _, relocs, _, flags = struct.unpack_from('<IIIIHHI', data, header + 16)
    assert end <= size and relocs == 0, 'trampoline must contain no external relocations'
    assert ((flags >> 20) & 15) >= 5, 'COFF section must retain 16-byte alignment'
    code = data[at+start:at+end]
    assert code[key-start:key-start+8] == bytes(8)
    # ADR is the second instruction. Its signed immediate must target the key
    # slot within the copied bytes, independent of the firmware load address.
    adr = struct.unpack_from('<I', code, 4)[0]
    assert adr & 0x9f00001f == 0x10000009
    immediate = ((adr >> 5 & 0x7ffff) << 2) | (adr >> 29 & 3)
    if immediate & (1 << 20):
        immediate -= 1 << 21
    assert 4 + immediate == key - start
    return end-start, key-start


with tempfile.TemporaryDirectory(prefix='houji26-trampoline-') as temporary:
    obj = Path(temporary) / 'Entry.obj'
    source = ROOT / 'bootloader/ios26/overlay/DarwinBootPkg/Driver/Entry.S'
    subprocess.run(['clang', '-target', 'aarch64-unknown-windows', '-x', 'assembler-with-cpp',
                    '-I', str(ROOT / 'checks/ios26-uefi'), '-c', str(source), '-o', str(obj)], check=True)
    size, offset = check_coff(obj.read_bytes())
    print(f'PASS: production secondary entry COFF: bytes={size}, key offset={offset}, one aligned section, local ADR and no relocations')
    for path in sys.argv[1:]:
        size, offset = check_coff(Path(path).read_bytes())
        print(f'PASS: supplied UEFI object: bytes={size}, key offset={offset}; no hardware qualification')
