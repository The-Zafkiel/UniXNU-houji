#!/usr/bin/env python3
"""Verify the pinned iOS26 IPSW and inventory manifests without mounting it."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def inspect(image, target, check_crc):
    pin = target['userland']
    size = image.stat().st_size
    if size != pin['bytes']:
        raise ValueError(f'IPSW size mismatch: {size}, expected {pin["bytes"]}')
    sha256, sha1 = hashlib.sha256(), hashlib.sha1()
    with image.open('rb') as stream:
        while block := stream.read(8 * 1024 * 1024):
            sha256.update(block)
            sha1.update(block)
    if sha256.hexdigest() != pin['sha256'] or sha1.hexdigest() != pin['sha1']:
        raise ValueError('IPSW digest mismatch')

    with zipfile.ZipFile(image) as archive:
        members = set(archive.namelist())
        manifest_bytes = archive.read('BuildManifest.plist')
        manifest = plistlib.loads(manifest_bytes)
        if manifest.get('ProductVersion') != pin['version']:
            raise ValueError('BuildManifest ProductVersion mismatch')
        if manifest.get('ProductBuildVersion') != pin['build']:
            raise ValueError('BuildManifest ProductBuildVersion mismatch')
        if pin['product'] not in manifest.get('SupportedProductTypes', []):
            raise ValueError('BuildManifest does not support the pinned product')
        identities = []
        for identity in manifest['BuildIdentities']:
            info = identity.get('Info', {})
            components = {}
            for name, component in sorted(identity.get('Manifest', {}).items()):
                path = component.get('Info', {}).get('Path')
                if not path:
                    continue
                if path not in members:
                    raise ValueError(f'Missing manifest component: {path}')
                member = archive.getinfo(path)
                components[name] = {
                    'path': path,
                    'bytes': member.file_size,
                    'crc32': f'{member.CRC:08x}',
                }
            identities.append({
                'device_class': info.get('DeviceClass'),
                'variant': info.get('Variant'),
                'board_id': identity.get('ApBoardID'),
                'chip_id': identity.get('ApChipID'),
                'components': components,
            })
        expected = target['restore_identity']
        selected = [i for i in identities if all(
            i[key] == expected[key] for key in ('device_class', 'variant', 'board_id', 'chip_id')
        )]
        if len(selected) != 1:
            raise ValueError('Pinned restore identity missing or ambiguous')
        for name, path in expected['components'].items():
            if selected[0]['components'].get(name, {}).get('path') != path:
                raise ValueError(f'Pinned restore component mismatch: {name}')
        if check_crc:
            bad = archive.testzip()
            if bad is not None:
                raise ValueError(f'ZIP CRC mismatch: {bad}')

    return manifest_bytes, {
        'format': 1,
        'status': 'verified',
        'filename': image.name,
        'bytes': size,
        'sha256': sha256.hexdigest(),
        'sha1': sha1.hexdigest(),
        'version': manifest['ProductVersion'],
        'build': manifest['ProductBuildVersion'],
        'supported_products': manifest['SupportedProductTypes'],
        'build_manifest_sha256': hashlib.sha256(manifest_bytes).hexdigest(),
        'all_zip_members_crc_checked': check_crc,
        'identities': identities,
        'hardware_boot_verified': False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path,
                        help='New directory outside the public source tree, or under .local/out')
    parser.add_argument('--check-crc', action='store_true', help='Also read and CRC-check every ZIP member')
    args = parser.parse_args()
    image, output = args.image.resolve(), args.output.resolve()
    for path in (image, output):
        if path.is_relative_to(ROOT) and not any(
            path.is_relative_to(ROOT / allowed) for allowed in ('.local', 'out')
        ):
            parser.error('Keep operating-system inputs and extracted metadata outside public source directories')
    if output.exists():
        parser.error('Output directory already exists; choose a new directory')
    target = json.loads((ROOT / 'targets/ios26.json').read_text())
    manifest, receipt = inspect(image, target, args.check_crc)
    output.mkdir(parents=True)
    (output / 'BuildManifest.plist').write_bytes(manifest)
    (output / 'inventory.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(f'PASS: iOS {receipt["version"]} / {receipt["build"]}; {receipt["bytes"]} bytes; SHA-256 and SHA-1 match')
    print(f'Inventoried {len(receipt["identities"])} build identities in {output}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, zipfile.BadZipFile, plistlib.InvalidFileException) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        sys.exit(1)
