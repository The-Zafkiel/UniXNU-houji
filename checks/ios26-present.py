#!/usr/bin/env python3
"""Test the actual new presentation glue; register and cache operations are peers."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='houji26-present-') as tmp:
    binary = Path(tmp) / 'present'
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I', str(ROOT / 'kernel/ios26/overlay/iokit/Kernel/arm'),
                    '-I', str(ROOT / 'kernel/overlay/iokit/Kernel/arm'),
                    str(ROOT / 'checks/ios26-present.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
