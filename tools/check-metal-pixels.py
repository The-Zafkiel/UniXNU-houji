#!/usr/bin/env python3
"""Exercise the live frontend's defined pixels on native Metal and a host Vulkan GPU.

The archive must be a macOS build of graphics/bridge with the locked runtime.
No phone is accessed; a pass does not establish Adreno or display correctness.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--loader', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    frontend = ROOT / 'graphics/metal/AdrenoMetal'
    binary = out / 'render-pixels'
    command = ['xcrun', 'clang++', '-std=c++17', '-fobjc-arc', '-O1',
               '-Wno-deprecated-declarations', '-DHOUJI_GPU_CPU_PROFILE=0', '-DHOUJI_GPU_STAGE_TRACE=0',
               '-I', str(frontend), str(frontend / 'NativeBridgeMetalFrontend.mm'),
               str(ROOT / 'checks/metal-render-pixels.mm'), str(args.archive.resolve()),
               '-framework', 'Foundation', '-framework', 'Metal', '-framework', 'IOSurface', '-o', str(binary)]
    (out / 'build-command.json').write_text(json.dumps(command, indent=2) + '\n')
    with (out / 'build.log').open('w') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    results = {}
    for label, loader, policy in [('apple', 'apple', '0'),
                                  ('requested', str(args.loader.resolve()), '0'),
                                  ('single', str(args.loader.resolve()), '1')]:
        cache = out / (label + '-cache')
        cache.mkdir()
        env = dict(os.environ, NATIVE_METAL_SINGLE_SAMPLE=policy)
        run = subprocess.run([str(binary), loader, str(cache)], env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=180)
        (out / (label + '.log')).write_text(run.stdout)
        passed = run.returncode == 0 and 'PASS: defined render' in run.stdout
        results[label] = dict(passed=passed, returncode=run.returncode,
                              cases=sum(line.startswith('PASS pixels') for line in run.stdout.splitlines()))
        print(label + ': ' + ('PASS' if passed else 'FAIL'), flush=True)
    report = dict(host_gpu_only=True, device_accessed=False, results=results,
                  frontend_sha256=hashlib.sha256((frontend / 'NativeBridgeMetalFrontend.mm').read_bytes()).hexdigest(),
                  archive_sha256=hashlib.sha256(args.archive.read_bytes()).hexdigest())
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    if not all(row['passed'] for row in results.values()):
        raise SystemExit('pixel check failed; see logs under ' + str(out))


if __name__ == '__main__':
    main()
