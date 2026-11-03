#!/usr/bin/env python3
"""Assemble the pinned iOS26 source profile in a new directory; no device I/O."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(*command, **kwargs):
    return subprocess.run(list(map(str, command)), check=True, **kwargs)


def archive(upstream, revision, destination):
    run('git', '-C', upstream, 'cat-file', '-e', revision + '^{commit}')
    process = subprocess.Popen(['git', '-C', str(upstream), 'archive', revision], stdout=subprocess.PIPE)
    try:
        with tarfile.open(fileobj=process.stdout, mode='r|') as source:
            for member in source:
                path = PurePosixPath(member.name)
                if path.is_absolute() or '..' in path.parts:
                    raise ValueError('Unsafe archive path')
                source.extract(member, destination, filter='data')
        if process.wait():
            raise RuntimeError('git archive failed')
    finally:
        process.stdout.close()
        if process.poll() is None:
            process.kill()
            process.wait()


def apply(tree, patch):
    run('git', 'apply', '--check', patch, cwd=tree)
    run('git', 'apply', patch, cwd=tree)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('component', choices=('kernel', 'bootloader', 'boot_manager'))
    parser.add_argument('--upstream', required=True, type=Path, help='Read-only upstream Git checkout')
    parser.add_argument('--hfs-upstream', type=Path, help='Pinned Apple HFS Git checkout, required for kernel')
    parser.add_argument('--target-conditionals', type=Path, help='Optional local SDK TargetConditionals.h; stays in the private output')
    parser.add_argument('--reference', type=Path, help='Pinned hakios Git checkout, required for kernel')
    parser.add_argument('--output', required=True, type=Path, help='Must not exist')
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        parser.error('Output exists')
    lock = json.loads((ROOT / 'sources.lock.json').read_text())
    profile = lock['ios26']
    entry = profile[args.component]
    reference_patch = None
    if args.component == 'kernel':
        if args.reference is None or args.hfs_upstream is None:
            parser.error('Kernel assembly requires --reference and --hfs-upstream')
        reference = profile['reference']
        reference_patch = subprocess.check_output([
            'git', '-C', str(args.reference), 'show',
            reference['revision'] + ':' + reference['kernel_patch']])
        if hashlib.sha256(reference_patch).hexdigest() != reference['kernel_patch_sha256']:
            raise ValueError('Pinned reference patch hash differs')
        upstream = entry
    elif args.component == 'bootloader':
        upstream = lock['components'][entry['baseline_component']]
    else:
        upstream = entry
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='houji26-source-') as temporary:
        temporary = Path(temporary)
        tree = temporary / 'source'
        tree.mkdir()
        archive(args.upstream, upstream['revision'], tree)
        if args.component == 'kernel':
            patch = temporary / 'reference.patch'
            patch.write_bytes(reference_patch)
            apply(tree, patch)
            apply(tree, ROOT / entry['patch'])
            reuse = json.loads((ROOT / entry['reuse']).read_text())
            excluded = set(reuse['excluded_local_inputs'])
            for name in reuse['files']:
                path = PurePosixPath(name)
                if path.is_absolute() or '..' in path.parts or path.name in excluded:
                    raise ValueError('Unsafe reuse input: ' + name)
                destination = tree / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / reuse['overlay'] / name, destination)
            apply(tree, ROOT / entry['reuse_patch'])
            hfs = profile['hfs']
            hfs_source = temporary / 'hfs'
            hfs_source.mkdir()
            archive(args.hfs_upstream, hfs['revision'], hfs_source)
            for name in hfs['directories']:
                shutil.copytree(hfs_source / name, tree / hfs['target'] / name)
            apply(tree / hfs['target'], ROOT / hfs['patch'])
            if args.target_conditionals is not None:
                shutil.copy2(args.target_conditionals, tree / hfs['target'] / 'TargetConditionals.h')
        elif args.component == 'bootloader':
            apply(tree, ROOT / upstream['patch'])
            shutil.copytree(ROOT / upstream['overlay'], tree, dirs_exist_ok=True)
            apply(tree, ROOT / entry['patch'])
        else:
            apply(tree, ROOT / entry['patch'])
        shutil.copytree(ROOT / entry['overlay'], tree, dirs_exist_ok=True)
        (tree / '.houji-source.json').write_text(json.dumps({
            'profile': 'ios26', 'component': args.component,
            'revision': upstream['revision'],
            'architecture': 'arm64e' if args.component == 'kernel' else 'aarch64-uefi',
            'payload_architecture': 'arm64e',
            'hardware_verified': False, 'vendor_firmware_supplied': False,
        }, indent=2) + '\n')
        shutil.move(str(tree), output)
    print(f'Prepared iOS26 {args.component}: {output}')
    if args.component == 'kernel':
        print('Local firmware and fresh trial headers must be supplied separately before building.')


if __name__ == '__main__':
    main()
