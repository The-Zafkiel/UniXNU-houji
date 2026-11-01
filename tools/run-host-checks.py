#!/usr/bin/env python3
"""Run bounded policy and BO reuse regression checks. Does not emulate a GPU."""
from pathlib import Path
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='houji-check-') as temporary:
    for filename, compiler, standard in [
        ('gpu-governor.cpp', 'clang++', 'c++17'),
        ('gpu-hfi.c', 'clang', 'c11'),
        ('cpu-governor.cpp', 'clang++', 'c++17'),
        ('power-display.cpp', 'clang++', 'c++17'),
        ('sample-retention.c', 'clang', 'c11'),
    ]:
        binary = Path(temporary) / Path(filename).stem
        subprocess.run([compiler, '-std=' + standard, '-Wall', '-Wextra', '-Werror', str(ROOT / 'checks' / filename), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/cpu-lut.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/gpu-input-prewarm.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/cpu-snapshot.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/backlight-properties.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/battery-survey.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/battery-sparse-survey.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/battery-protocol.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/battery-startup.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/battery-firmware.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/battery-cmd-db.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/battery-spi.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/battery-adsp-codecs.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/battery-adsp-policy.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/battery-publisher.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/turnip-bo-retire.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/turnip-bo-cache.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/native-gpu/run.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/native-gpu/transport.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/native-gpu/board-gate.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/native-gpu/dispatch-gate.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/native-gpu/fence-poll.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/gpu-address-space.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/gpu-client-slots.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/gpu-coherence.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/gpu-latency.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/gpu-issuer-trace.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/shader-prime.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/metal-constants.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/texture-properties.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/metal-render-pass.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/texture-init-probe.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/gpu-memory-pressure.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/gpu-memory-cache.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/gpu-room-worker.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/gpu-stage-trace.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/gpu-cpu-profile.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/metal-device-answers.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/metal-no-copy-pages.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/metal-surface-use.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/display-queue.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/display-snapshot.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/display-timing.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/boot-arguments.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/build-inputs.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/recovery-logs.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/userland-logs.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/metal-async.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/metal-trace-id.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/network-stack.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/system-apps.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/system-app-dyld.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/tcc-media-compatibility.py')], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/metal-command-dictionary.py')], check=True)
