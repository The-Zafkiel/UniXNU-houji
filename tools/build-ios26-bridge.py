#!/usr/bin/env python3
"""Build the arm64e Rust Metal bridge and std with the pinned PAC ABI; no device I/O."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, default=ROOT / '.deps/metal2vulkan')
    parser.add_argument('--toolchain', type=Path, required=True, help='Local pinned nightly prefix; no system install')
    parser.add_argument('--resume', action='store_true', help='Resume only when the generated manifest and lock still match')
    parser.add_argument('--output', type=Path, required=True, help='Must not exist')
    parser.add_argument('--fixture-air', type=Path, required=True)
    parser.add_argument('--function-constant-air', type=Path, required=True)
    parser.add_argument('--scanout-air', type=Path, required=True)
    parser.add_argument('--cargo-home', type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() and not args.resume:
        parser.error('Output exists')
    lock = json.loads((ROOT / 'sources.lock.json').read_text())
    pin = lock['ios26']['rust']
    toolchain = args.toolchain.resolve()
    rustc, cargo = toolchain / 'bin/rustc', toolchain / 'bin/cargo'
    version = subprocess.check_output([str(rustc), '--version'], text=True).strip()
    if version != 'rustc ' + pin['compiler']:
        parser.error('Nightly compiler differs from the pinned PAC toolchain')
    if not (toolchain / 'lib/rustlib/src/rust/library/std/Cargo.toml').is_file():
        parser.error('Matching rust-src is required for the arm64e std rebuild')
    target = ROOT / pin['target']
    if digest(target) != pin['target_sha256']:
        parser.error('PAC target specification differs')
    runtime = args.runtime.resolve()
    origin = json.loads((runtime / '.houji-source.json').read_text())
    if origin != {'component': 'graphics/runtime', 'revision': lock['components']['graphics/runtime']['revision']}:
        parser.error('Assemble the locked graphics/runtime first')
    files = {str(f.relative_to(runtime)): digest(f) for f in sorted((runtime / 'crates').rglob('*.rs'))}
    source_id = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()[:32]
    if source_id != lock['metal_frontend']['runtime_id']:
        parser.error('Runtime source identity differs')
    for path in (args.fixture_air, args.function_constant_air, args.scanout_air):
        if not path.is_file():
            parser.error('Missing authored AIR input')
    sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True).strip()
    cc = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--find', 'clang'], text=True).strip()
    out.mkdir(parents=True, exist_ok=args.resume)
    manifest = (ROOT / 'graphics/bridge/Cargo.toml').read_text()
    manifest = manifest.replace('"../metal/AdrenoMetal/bridge/NativeMetalBridge.rs"',
                                json.dumps(str(ROOT / 'graphics/metal/AdrenoMetal/bridge/NativeMetalBridge.rs')))
    for name in ('metal2vulkan', 'metal2vulkan-runtime'):
        manifest = manifest.replace('"../../.deps/metal2vulkan/crates/' + name + '"',
                                    json.dumps(str(runtime / 'crates' / name)))
    # cc-rs interprets the experimental pauthtest ABI as musl. Use sha2's
    # portable Rust implementation rather than selecting the wrong assembler ABI.
    manifest = manifest.replace('sha2 = { version = "0.10", features = ["asm"] }', 'sha2 = "0.10"')
    if args.resume:
        if not (out / 'Cargo.toml').is_file() or (out / 'Cargo.toml').read_text() != manifest:
            parser.error('Existing generated manifest differs; use a new output directory')
        if (out / 'Cargo.lock').read_bytes() != (ROOT / 'graphics/ios26/Cargo.lock').read_bytes():
            parser.error('Existing lock differs; use a new output directory')
    (out / 'Cargo.toml').write_text(manifest)
    shutil.copy2(ROOT / 'graphics/ios26/Cargo.lock', out / 'Cargo.lock')
    runtime_id = hashlib.sha256((source_id + '\narm64e-ios26\n' + pin['target_sha256']).encode()).hexdigest()[:32]
    env = dict(os.environ, RUSTC=str(rustc), PATH=str(toolchain / 'bin') + ':' + os.environ['PATH'],
               CARGO_HOME=str((args.cargo_home or out / 'cargo-home').resolve()), CARGO_BUILD_JOBS='4',
               CARGO_INCREMENTAL='0', IPHONEOS_DEPLOYMENT_TARGET='26.0', MACOSX_DEPLOYMENT_TARGET='14.0',
               PAUTHTEST_SYSROOT=sdk, CC_arm64e_apple_ios_pauth=cc,
               CFLAGS_arm64e_apple_ios_pauth=shlex.join(['-target', 'arm64e-apple-ios26.0', '-isysroot', sdk]),
               RUSTFLAGS='-Zpointer-authentication=' + ','.join('+' + x for x in pin['pointer_authentication']),
               NMB_RUNTIME_ID=runtime_id, NMB_FIXTURE_AIR=str(args.fixture_air.resolve()),
               NMB_FC_AIR=str(args.function_constant_air.resolve()), NMB_SCANOUT_AIR=str(args.scanout_air.resolve()),
               HOUJI_DIAGNOSTIC_FRESH_PATTERN='')
    env.pop('DYLD_LIBRARY_PATH', None)
    command = [str(cargo), 'build', '--locked', '--release', '--manifest-path', str(out / 'Cargo.toml'),
               '--target', str(target), '-Zjson-target-spec', '-Zbuild-std=std,panic_unwind',
               '--target-dir', str(out / 'target')]
    subprocess.run(command, env=env, check=True)
    archive = out / 'target' / target.stem / 'release/libnative_metal_bridge.a'
    shutil.copy2(archive, out / archive.name)
    # Inspect emitted machine code, not just the custom target's declared features.
    counts = {'signed_indirect_calls': 0, 'signed_returns': 0}
    with (out / 'disassembly.txt').open('w') as stream:
        subprocess.run(['xcrun', 'otool', '-tvV', str(archive)], stdout=stream, check=True)
    with (out / 'disassembly.txt').open() as stream:
        for line in stream:
            counts['signed_indirect_calls'] += bool(re.search(r'\bblraa[z]?\b', line))
            counts['signed_returns'] += bool(re.search(r'\bret(?:aa|ab)\b', line))
    if not all(counts.values()):
        raise RuntimeError('Emitted bridge/std code lacks the required authenticated calls or returns')
    (out / 'build-result.json').write_text(json.dumps({
        'status': 'built-not-hardware-qualified', 'architecture': 'arm64e', 'deployment_target': '26.0',
        'compiler': version, 'target_sha256': pin['target_sha256'], 'runtime_revision': origin['revision'],
        'runtime_source_id': source_id, 'runtime_id': runtime_id,
        'archive_sha256': digest(archive), 'std_rebuilt': True, 'sha2_assembly': False,
        'authored_air_sha256': {'fixture': digest(args.fixture_air),
                               'function_constant': digest(args.function_constant_air),
                               'scanout': digest(args.scanout_air)},
        'machine_code': counts, 'device_accessed': False, 'installed': False,
    }, indent=2) + '\n')
    print('Built PAC-enabled arm64e bridge and std; no device or GPU qualification')


if __name__ == '__main__':
    main()
