#!/usr/bin/env python3
"""Build the pinned Darwin Turnip backend as arm64e iOS26; no device access."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_identity(source):
    files = {str(p.relative_to(source)): digest(p) for p in sorted(source.rglob('*'))
             if p.is_file() and p.name != '.houji-source.json'}
    return hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='Must not exist')
    parser.add_argument('--meson', type=Path, required=True)
    parser.add_argument('--tools-bin', type=Path, action='append', default=[])
    args = parser.parse_args()
    source, out = args.source.resolve(), args.output.resolve()
    lock = json.loads((ROOT / 'sources.lock.json').read_text())
    pin = lock['components']['graphics/turnip']
    expected = lock['ios26']['turnip']['source_tree_sha256']
    if json.loads((source / '.houji-source.json').read_text()) != {
        'component': 'graphics/turnip', 'revision': pin['revision']
    } or source_identity(source) != expected:
        parser.error('Assemble the pinned Turnip sources in a separate directory first')
    if out.exists():
        parser.error('Output exists')
    abi = ROOT / 'kernel/overlay/iokit/Kernel/houji-ufs-root/Adreno740'
    abi_id = hashlib.sha256(json.dumps({p.name: digest(p) for p in sorted(abi.glob('NativeAdreno*.h'))},
                                     sort_keys=True).encode()).hexdigest()
    target = 'arm64e-apple-ios26.0'
    build_id = hashlib.sha256((expected + '\n' + target + '\n' + abi_id).encode()).hexdigest()
    def xcrun(*options):
        return subprocess.check_output(['xcrun', '--sdk', 'iphoneos', *options], text=True).strip()
    sdk = xcrun('--show-sdk-path')
    binaries = {name: xcrun('--find', name) for name in ('clang', 'clang++', 'ar', 'strip')}
    flags = ['-target', target, '-isysroot', sdk, '-fstack-protector-strong']
    cross = '[binaries]\n' + '\n'.join(f'{key} = {repr(value)}' for key, value in {
        'c': binaries['clang'], 'cpp': binaries['clang++'], 'objc': binaries['clang'],
        'ar': binaries['ar'], 'strip': binaries['strip'], 'pkg-config': '/usr/bin/false'}.items())
    cross += "\n[host_machine]\nsystem = 'darwin'\ncpu_family = 'aarch64'\ncpu = 'arm64e'\nendian = 'little'\n"
    cross += '[properties]\nneeds_exe_wrapper = true\n[built-in options]\n'
    cross += '\n'.join(key + ' = ' + repr(flags) for key in
                       ('c_args', 'cpp_args', 'objc_args', 'c_link_args', 'cpp_link_args', 'objc_link_args')) + '\n'
    out.mkdir(parents=True)
    (out / 'ios.cross').write_text(cross)
    options = ['-Dgallium-drivers=', '-Dvulkan-drivers=freedreno', '-Dfreedreno-kmds=darwin',
               '-Dtu-darwin-abi-dir=' + str(abi), '-Dplatforms=macos', '-Dglx=disabled',
               '-Degl=disabled', '-Dgbm=disabled', '-Dopengl=false', '-Dgles1=disabled',
               '-Dgles2=disabled', '-Dllvm=disabled', '-Dshared-glapi=disabled',
               '-Dxmlconfig=disabled', '-Dshader-cache=enabled', '-Dtools=', '-Dbuild-tests=false',
               '-Dvideo-codecs=', '-Dvulkan-layers=', '-Dlibunwind=disabled', '-Dlmsensors=disabled',
               '-Dvalgrind=disabled', '-Dexpat=disabled', '-Dzstd=disabled', '-Dbuildtype=release',
               '-Db_ndebug=true', '-Dtu-build-id=' + build_id[:40],
               '-Dtu-builtin-source-id=' + build_id, '-Dtu-builtin-pack-header=']
    env = dict(os.environ, CC=binaries['clang'], CXX=binaries['clang++'], OBJC=binaries['clang'],
               CFLAGS='', CXXFLAGS='', OBJCFLAGS='', CPPFLAGS='', LDFLAGS='',
               PYTHONNOUSERSITE='1', IPHONEOS_DEPLOYMENT_TARGET='26.0')
    env['PATH'] = ':'.join(str(p.resolve()) for p in args.tools_bin) + ':' + env['PATH']
    for name in ('DYLD_LIBRARY_PATH', 'SDKROOT', 'MACOSX_DEPLOYMENT_TARGET'):
        env.pop(name, None)
    build = out / 'build'
    commands = [
        [str(args.meson.resolve()), 'setup', str(build), str(source),
         '--cross-file', str(out / 'ios.cross'), '--wrap-mode=nodownload', *options],
        ['ninja', '-C', str(build), '-j8', 'src/freedreno/vulkan/libvulkan_freedreno.dylib'],
    ]
    (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    for command in commands:
        subprocess.run(command, env=env, check=True)
    dylib = out / 'libvulkan_freedreno.dylib'
    shutil.copy2(build / 'src/freedreno/vulkan' / dylib.name, dylib)
    magic, cpu, subtype = struct.unpack_from('<III', dylib.read_bytes())
    if (magic, cpu, subtype & 0xffffff) != (0xfeedfacf, 0x100000c, 2):
        raise ValueError('Turnip is not arm64e')
    subprocess.run(['install_name_tool', '-id', '/usr/lib/libvulkan_freedreno.dylib', str(dylib)], check=True)
    subprocess.run([binaries['strip'], '-x', str(dylib)], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', '--timestamp=none', str(dylib)], check=True)
    subprocess.run(['codesign', '--verify', '--strict', str(dylib)], check=True)
    exports = subprocess.check_output(['xcrun', 'nm', '-gU', str(dylib)], text=True)
    if ' T _vk_icdGetInstanceProcAddr' not in exports:
        raise ValueError('Missing Vulkan ICD entry')
    report = {'architecture': 'arm64e', 'target': target, 'kernel_abi': 1,
              'source_revision': pin['revision'], 'source_tree_sha256': expected,
              'abi_headers_sha256': abi_id, 'build_id': build_id, 'dylib_sha256': digest(dylib),
              'hardware_verified': False, 'device_accessed': False, 'installed': False}
    (out / 'build-result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Built arm64e iOS26 Turnip with the houji ABI v1; no GPU qualification')


if __name__ == '__main__':
    main()
