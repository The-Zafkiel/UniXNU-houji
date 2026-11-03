#!/usr/bin/env python3
"""Exercise the actual bootloader fixup walker; does not execute ARM PAC."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='houji26-pac-') as tmp:
    binary = Path(tmp) / 'pac'
    subprocess.run([
        os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-g',
        '-I', str(ROOT / 'bootloader/ios26/overlay/DarwinBootPkg/Driver'),
        str(ROOT / 'checks/ios26-pac.c'), '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary), *sys.argv[1:]], check=True)
