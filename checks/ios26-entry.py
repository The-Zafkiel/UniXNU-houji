#!/usr/bin/env python3
"""Compile the production entry parsers, optionally checking local kernel/ADT files."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='houji26-entry-') as tmp:
    binary = Path(tmp) / 'entry'
    subprocess.run([
        'clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
        '-I', str(ROOT / 'bootloader/ios26/overlay/DarwinBootPkg/Driver'),
        '-I', str(ROOT / 'checks/ios26-uefi'),
        str(ROOT / 'checks/ios26-entry.c'), '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary), *sys.argv[1:]], check=True)
