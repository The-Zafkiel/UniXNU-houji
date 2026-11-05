#!/usr/bin/env python3
"""Check the production image author's path and disk-image access guards."""
import importlib.util
from pathlib import Path
import plistlib
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ios26_root', ROOT / 'tools/build-ios26-root.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def rejected(function, *args):
    try:
        function(*args)
    except ValueError:
        return
    raise AssertionError('Unsafe image operation accepted')


with tempfile.TemporaryDirectory(prefix='houji26-root-') as temporary:
    base = Path(temporary)
    image, outside = base / 'image', base / 'outside'
    image.mkdir();outside.mkdir()
    sentinel = outside / 'retained'
    sentinel.write_text('must survive')
    (image / 'redirect').symlink_to(outside, target_is_directory=True)
    rejected(module.directory_in_root, image, 'redirect/subdirectory')
    rejected(module.directory_in_root, image, '../outside')
    rejected(module.directory_in_root, image, str(outside))
    assert not (outside / 'subdirectory').exists()
    replacement = module.replace_directory(image, 'redirect')
    assert replacement.is_dir() and not replacement.is_symlink() and sentinel.read_text() == 'must survive'
    real = image / 'ordinary'
    real.write_bytes(b'data')
    module.ordinary_file(real)
    alias = image / 'alias'
    alias.symlink_to(real)
    rejected(module.ordinary_file, alias)
    rejected(module.ordinary_file, Path('/dev/null'))
    valid = {'BusProtocol': 'Disk Image', 'MountPoint': str(image), 'Writable': False}
    with patch.object(module, 'run', return_value=plistlib.dumps(valid)):
        module.mounted_image(image, False)
        rejected(module.mounted_image, image, True)
    for changed in [{'BusProtocol': 'USB'}, {'BusProtocol': 'PCI-Express'},
                    {'MountPoint': '/'}, {'Writable': True}]:
        with patch.object(module, 'run', return_value=plistlib.dumps({**valid, **changed})):
            rejected(module.mounted_image, image, False)
print('PASS: production image guards reject physical media, wrong access and symlink escapes; no HFS or device emulation')
